#include "media/ImageHlsWorker.h"

#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QEventLoop>
#include <QTimer>

#include "media/FrameQueue.h"
#include "infrastructure/Logger.h"

#include "media/PcmAudioPlayer.h"

#include <png.h>
#include <zlib.h>

#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

// --- libpng helpers ---

struct PngReadBuffer {
    const uint8_t *data;
    size_t size;
    size_t offset;
};

static void pngReadCallback(png_structp png, png_bytep out, png_size_t count) {
    auto *buf = static_cast<PngReadBuffer *>(png_get_io_ptr(png));
    if (buf->offset + count > buf->size) {
        png_error(png, "read past end");
        return;
    }
    memcpy(out, buf->data + buf->offset, count);
    buf->offset += count;
}

static QImage decodePngWithLibpng(const QByteArray &data) {
    if (data.size() < 8) return {};
    if (png_sig_cmp(reinterpret_cast<const uint8_t *>(data.constData()), 0, 8) != 0) return {};

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) return {};

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        return {};
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        return {};
    }

    PngReadBuffer buf{reinterpret_cast<const uint8_t *>(data.constData()),
                      static_cast<size_t>(data.size()), 0};
    png_set_read_fn(png, &buf, pngReadCallback);
    png_read_info(png, info);

    int width = png_get_image_width(png, info);
    int height = png_get_image_height(png, info);
    png_byte colorType = png_get_color_type(png, info);
    png_byte bitDepth = png_get_bit_depth(png, info);

    // Normalize to 8-bit RGBA
    if (bitDepth == 16) png_set_strip_16(png);
    if (colorType == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (colorType == PNG_COLOR_TYPE_RGB || colorType == PNG_COLOR_TYPE_GRAY ||
        colorType == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    QImage image(width, height, QImage::Format_RGBA8888);
    std::vector<png_bytep> rows(height);
    for (int y = 0; y < height; ++y) {
        rows[y] = image.scanLine(y);
    }
    png_read_image(png, rows.data());

    png_destroy_read_struct(&png, &info, nullptr);
    return image;
}

// --- Find IEND chunk offset in PNG data ---
// Returns the offset of the IEND chunk start, or -1 if not found.
static int findIendOffset(const QByteArray &data) {
    // PNG structure: 8-byte signature, then chunks: 4-byte len + 4-byte type + data + 4-byte crc
    for (int off = 8; off + 8 <= data.size(); ) {
        uint32_t chunkLen = (static_cast<uint8_t>(data[off]) << 24) |
                            (static_cast<uint8_t>(data[off+1]) << 16) |
                            (static_cast<uint8_t>(data[off+2]) << 8) |
                             static_cast<uint8_t>(data[off+3]);
        if (data.mid(off + 4, 4) == "IEND") {
            return off;
        }
        if (chunkLen > static_cast<uint32_t>(data.size()) - off - 12) break;
        off += 12 + static_cast<int>(chunkLen);
    }
    return -1;
}

// --- FFmpeg memory-buffer IO for trailing data ---

struct AvioBufferData {
    const uint8_t *data;
    size_t size;
    size_t offset;
};

static int avioReadCallback(void *opaque, uint8_t *buf, int bufSize) {
    auto *bd = static_cast<AvioBufferData *>(opaque);
    if (bd->offset >= bd->size) return AVERROR_EOF;
    auto toRead = static_cast<int>(std::min(static_cast<size_t>(bufSize), bd->size - bd->offset));
    memcpy(buf, bd->data + bd->offset, toRead);
    bd->offset += toRead;
    return toRead;
}

// Decode the first video frame from a memory buffer using FFmpeg.
// Returns a QImage (RGB32) or null QImage on failure.
static QImage decodeTrailingWithFFmpeg(const uint8_t *data, size_t size) {
    if (!data || size < 16) return {};

    AVFormatContext *fmtCtx = avformat_alloc_context();
    if (!fmtCtx) return {};

    AvioBufferData bd{data, size, 0};
    constexpr int avioBufSize = 4096;
    uint8_t *avioBuf = static_cast<uint8_t *>(av_malloc(avioBufSize));
    if (!avioBuf) {
        avformat_free_context(fmtCtx);
        return {};
    }

    AVIOContext *avio = avio_alloc_context(avioBuf, avioBufSize, 0, &bd, avioReadCallback, nullptr, nullptr);
    if (!avio) {
        av_free(avioBuf);
        avformat_free_context(fmtCtx);
        return {};
    }

    fmtCtx->pb = avio;
    fmtCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

    int ret = 0;
    bool isMpegTs = (data[0] == 0x47);

    if (isMpegTs) {
        const AVInputFormat *tsFmt = av_find_input_format("mpegts");
        ret = avformat_open_input(&fmtCtx, nullptr, tsFmt, nullptr);
        if (ret < 0) {
            avio_context_free(&avio);
            avformat_free_context(fmtCtx);
            return {};
        }
        fmtCtx->max_analyze_duration = 500000; // 0.5s in microseconds
        fmtCtx->probesize = 500000;            // 500KB probe
        avformat_find_stream_info(fmtCtx, nullptr);
    } else {
        AVProbeData probeData{};
        probeData.filename = "";
        probeData.buf = const_cast<uint8_t *>(data);
        probeData.buf_size = static_cast<int>(std::min(size, static_cast<size_t>(512)));
        const AVInputFormat *ifmt = av_probe_input_format(&probeData, 1);

        ret = avformat_open_input(&fmtCtx, nullptr, ifmt, nullptr);
        if (ret < 0) {
            avio_context_free(&avio);
            avformat_free_context(fmtCtx);
            return {};
        }

        fmtCtx->max_analyze_duration = AV_TIME_BASE;
        if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
            avformat_close_input(&fmtCtx);
            return {};
        }
    }

    // Log all streams found
    Logger::instance().info("FFmpeg: " + QString::number(fmtCtx->nb_streams) + " streams, format=" +
        QString(fmtCtx->iformat ? fmtCtx->iformat->name : "?"));
    for (unsigned i = 0; i < fmtCtx->nb_streams; ++i) {
        AVStream *s = fmtCtx->streams[i];
        Logger::instance().info("FFmpeg: stream " + QString::number(i) +
            " type=" + QString::number(s->codecpar->codec_type) +
            " codec_id=" + QString::number(s->codecpar->codec_id) +
            " " + QString::number(s->codecpar->width) + "x" + QString::number(s->codecpar->height));
    }

    int videoIdx = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; ++i) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoIdx = static_cast<int>(i);
            break;
        }
    }

    if (videoIdx < 0) {
        Logger::instance().warn("FFmpeg: no video stream found");
        avformat_close_input(&fmtCtx);
        return {};
    }

    AVStream *stream = fmtCtx->streams[videoIdx];
    Logger::instance().info("FFmpeg: video stream " + QString::number(videoIdx) +
        " codec_id=" + QString::number(stream->codecpar->codec_id) +
        " " + QString::number(stream->codecpar->width) + "x" + QString::number(stream->codecpar->height));
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmtCtx);
        return {};
    }

    AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        avformat_close_input(&fmtCtx);
        return {};
    }

    if (avcodec_parameters_to_context(codecCtx, stream->codecpar) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return {};
    }

    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        Logger::instance().warn("FFmpeg: failed to open codec " + QString(codec->name));
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return {};
    }

    Logger::instance().info("FFmpeg: using decoder=" + QString(codec->name));

    AVPacket *pkt = av_packet_alloc();
    AVFrame *avFrame = av_frame_alloc();
    QImage result;
    int decodedFrames = 0;

    while (av_read_frame(fmtCtx, pkt) >= 0) {
        if (pkt->stream_index != videoIdx) {
            av_packet_unref(pkt);
            continue;
        }

        ret = avcodec_send_packet(codecCtx, pkt);
        av_packet_unref(pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) break;

        ret = avcodec_receive_frame(codecCtx, avFrame);
        if (ret < 0) continue;

        int w = avFrame->width;
        int h = avFrame->height;
        if (w > 0 && h > 0) {
            int srcFmt = avFrame->format;
            SwsContext *sws = sws_getContext(
                w, h, (AVPixelFormat)srcFmt,
                w, h, AV_PIX_FMT_RGB32,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (sws) {
                result = QImage(w, h, QImage::Format_RGB32);
                uint8_t *dest[4] = { result.bits(), nullptr, nullptr, nullptr };
                int destLinesize[4] = { static_cast<int>(result.bytesPerLine()), 0, 0, 0 };
                sws_scale(sws, avFrame->data, avFrame->linesize, 0, h, dest, destLinesize);
                sws_freeContext(sws);
            }
        }

        ++decodedFrames;
        Logger::instance().info("FFmpeg: decoded frame " + QString::number(decodedFrames) +
            " " + QString::number(avFrame->width) + "x" + QString::number(avFrame->height));

        av_frame_free(&avFrame);
        av_packet_free(&pkt);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return result;
    }

    Logger::instance().info("FFmpeg: decoded " + QString::number(decodedFrames) + " frames total");
    av_frame_free(&avFrame);
    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
    return {};
}

// --- Decode video + audio from MPEG-TS trailing data using FFmpeg ---
// Callbacks are invoked for each decoded frame. Return true from video callback to continue.
using VideoFrameCallback = std::function<void(QImage &&image, double ptsSec)>;
using AudioFrameCallback = std::function<void(PcmAudioFrame &&frame)>;

static int decodeTrailingFull(const uint8_t *data, size_t size,
                              const VideoFrameCallback &onVideo,
                              const AudioFrameCallback &onAudio) {
    if (!data || size < 16) return 0;

    AVFormatContext *fmtCtx = avformat_alloc_context();
    if (!fmtCtx) return 0;

    AvioBufferData bd{data, size, 0};
    constexpr int avioBufSize = 4096;
    uint8_t *avioBuf = static_cast<uint8_t *>(av_malloc(avioBufSize));
    if (!avioBuf) {
        avformat_free_context(fmtCtx);
        return 0;
    }

    AVIOContext *avio = avio_alloc_context(avioBuf, avioBufSize, 0, &bd, avioReadCallback, nullptr, nullptr);
    if (!avio) {
        av_free(avioBuf);
        avformat_free_context(fmtCtx);
        return 0;
    }

    fmtCtx->pb = avio;
    fmtCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

    bool isMpegTs = (data[0] == 0x47);
    int ret = 0;

    if (isMpegTs) {
        const AVInputFormat *tsFmt = av_find_input_format("mpegts");
        ret = avformat_open_input(&fmtCtx, nullptr, tsFmt, nullptr);
        if (ret < 0) {
            avio_context_free(&avio);
            avformat_free_context(fmtCtx);
            return 0;
        }
        fmtCtx->max_analyze_duration = 500000;
        fmtCtx->probesize = 500000;
        avformat_find_stream_info(fmtCtx, nullptr);
    } else {
        AVProbeData probeData{};
        probeData.filename = "";
        probeData.buf = const_cast<uint8_t *>(data);
        probeData.buf_size = static_cast<int>(std::min(size, static_cast<size_t>(512)));
        const AVInputFormat *ifmt = av_probe_input_format(&probeData, 1);
        ret = avformat_open_input(&fmtCtx, nullptr, ifmt, nullptr);
        if (ret < 0) {
            avio_context_free(&avio);
            avformat_free_context(fmtCtx);
            return 0;
        }
        fmtCtx->max_analyze_duration = AV_TIME_BASE;
        if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
            avformat_close_input(&fmtCtx);
            return 0;
        }
    }

    // Find video and audio streams
    int videoIdx = -1;
    int audioIdx = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; ++i) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoIdx < 0)
            videoIdx = static_cast<int>(i);
        else if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioIdx < 0)
            audioIdx = static_cast<int>(i);
    }

    // Init video decoder
    AVCodecContext *videoCtx = nullptr;
    const AVCodec *videoCodec = nullptr;
    if (videoIdx >= 0) {
        videoCodec = avcodec_find_decoder(fmtCtx->streams[videoIdx]->codecpar->codec_id);
        if (videoCodec) {
            videoCtx = avcodec_alloc_context3(videoCodec);
            if (videoCtx) {
                avcodec_parameters_to_context(videoCtx, fmtCtx->streams[videoIdx]->codecpar);
                avcodec_open2(videoCtx, videoCodec, nullptr);
            }
        }
    }

    // Init audio decoder
    AVCodecContext *audioCtx = nullptr;
    const AVCodec *audioCodec = nullptr;
    SwrContext *swrCtx = nullptr;
    if (audioIdx >= 0) {
        audioCodec = avcodec_find_decoder(fmtCtx->streams[audioIdx]->codecpar->codec_id);
        if (audioCodec) {
            audioCtx = avcodec_alloc_context3(audioCodec);
            if (audioCtx) {
                avcodec_parameters_to_context(audioCtx, fmtCtx->streams[audioIdx]->codecpar);
                if (avcodec_open2(audioCtx, audioCodec, nullptr) >= 0) {
                    // Setup resampler: source → S16 interleaved
                    swrCtx = swr_alloc();
                    if (swrCtx) {
                        AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
                        if (audioCtx->ch_layout.nb_channels == 1)
                            outLayout = AV_CHANNEL_LAYOUT_MONO;
                        av_opt_set_chlayout(swrCtx, "in_chlayout", &audioCtx->ch_layout, 0);
                        av_opt_set_int(swrCtx, "in_sample_rate", audioCtx->sample_rate, 0);
                        av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", audioCtx->sample_fmt, 0);
                        av_opt_set_chlayout(swrCtx, "out_chlayout", &outLayout, 0);
                        av_opt_set_int(swrCtx, "out_sample_rate", audioCtx->sample_rate, 0);
                        av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                        if (swr_init(swrCtx) < 0) {
                            swr_free(&swrCtx);
                            swrCtx = nullptr;
                        }
                    }
                }
            }
        }
    }

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int videoFrameCount = 0;
    int audioFrameCount = 0;

    // For video PTS calculation
    AVRational videoTimeBase = videoIdx >= 0 ? fmtCtx->streams[videoIdx]->time_base : AVRational{1, 1};
    AVRational audioTimeBase = audioIdx >= 0 ? fmtCtx->streams[audioIdx]->time_base : AVRational{1, 1};

    while (av_read_frame(fmtCtx, pkt) >= 0) {
        // Decode video - get ALL frames, not just the first
        if (videoCtx && pkt->stream_index == videoIdx) {
            ret = avcodec_send_packet(videoCtx, pkt);
            av_packet_unref(pkt);
            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) continue;

            // Drain all available frames from the decoder
            while (true) {
                ret = avcodec_receive_frame(videoCtx, frame);
                if (ret < 0) break; // EAGAIN or EOF

                int w = frame->width;
                int h = frame->height;
                if (w > 0 && h > 0) {
                    // Calculate PTS in seconds
                    double ptsSec = 0.0;
                    if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                        ptsSec = frame->best_effort_timestamp * av_q2d(videoTimeBase);
                    }

                    int srcFmt = frame->format;
                    SwsContext *sws = sws_getContext(w, h, (AVPixelFormat)srcFmt,
                        w, h, AV_PIX_FMT_RGB32, SWS_BILINEAR, nullptr, nullptr, nullptr);
                    if (sws) {
                        QImage img(w, h, QImage::Format_RGB32);
                        uint8_t *dest[4] = { img.bits(), nullptr, nullptr, nullptr };
                        int destLinesize[4] = { static_cast<int>(img.bytesPerLine()), 0, 0, 0 };
                        sws_scale(sws, frame->data, frame->linesize, 0, h, dest, destLinesize);
                        sws_freeContext(sws);
                        onVideo(std::move(img), ptsSec);
                        ++videoFrameCount;
                    }
                }
                av_frame_unref(frame);
            }
            continue;
        }

        // Decode audio
        if (audioCtx && swrCtx && pkt->stream_index == audioIdx) {
            ret = avcodec_send_packet(audioCtx, pkt);
            av_packet_unref(pkt);
            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) continue;

            while (avcodec_receive_frame(audioCtx, frame) >= 0) {
                int outSamples = swr_get_out_samples(swrCtx, frame->nb_samples);
                int outChannels = audioCtx->ch_layout.nb_channels > 0 ? audioCtx->ch_layout.nb_channels : 2;
                int bufSize = outSamples * outChannels * 2; // S16 = 2 bytes per sample
                PcmAudioFrame pcmFrame;
                pcmFrame.data.resize(bufSize);
                pcmFrame.sampleRate = audioCtx->sample_rate;
                pcmFrame.channels = outChannels;

                // Calculate PTS
                double framePts = 0.0;
                if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                    framePts = frame->best_effort_timestamp * av_q2d(audioTimeBase);
                }
                pcmFrame.ptsSec = framePts;

                uint8_t *outBuf[] = { pcmFrame.data.data() };
                int converted = swr_convert(swrCtx, outBuf, outSamples,
                    const_cast<const uint8_t **>(frame->data), frame->nb_samples);
                if (converted > 0) {
                    pcmFrame.data.resize(converted * outChannels * 2);
                    onAudio(std::move(pcmFrame));
                    ++audioFrameCount;
                }
                av_frame_unref(frame);
            }
        } else {
            av_packet_unref(pkt);
        }
    }

    Logger::instance().info("decodeTrailingFull: " + QString::number(videoFrameCount) +
        " video frames, " + QString::number(audioFrameCount) + " audio frames");

    av_frame_free(&frame);
    av_packet_free(&pkt);
    if (videoCtx) avcodec_free_context(&videoCtx);
    if (audioCtx) avcodec_free_context(&audioCtx);
    if (swrCtx) swr_free(&swrCtx);
    avformat_close_input(&fmtCtx);

    return videoFrameCount;
}

// --- Try to decode trailing data as JPEG by scanning for SOI marker ---
static QImage decodeTrailingAsJpeg(const QByteArray &data) {
    // Scan for JPEG SOI: FF D8 FF
    for (int i = 0; i + 3 <= data.size(); ++i) {
        if (static_cast<uint8_t>(data[i]) == 0xFF &&
            static_cast<uint8_t>(data[i+1]) == 0xD8 &&
            static_cast<uint8_t>(data[i+2]) == 0xFF) {
            QByteArray jpegData = data.mid(i);
            QImage img;
            if (img.loadFromData(jpegData, "JPEG")) {
                Logger::instance().info("decodeTrailingAsJpeg: found JPEG at offset " +
                    QString::number(i) + ", decoded " + QString::number(img.width()) +
                    "x" + QString::number(img.height()));
                return img;
            }
        }
    }
    return {};
}

// --- Scan trailing data for embedded image signatures ---
// Returns decoded QImage or null. Much faster than FFmpeg full pipeline.
static QImage decodeTrailingFast(const QByteArray &trailing) {
    const uint8_t *d = reinterpret_cast<const uint8_t *>(trailing.constData());
    int len = trailing.size();

    // Scan for JPEG SOI: FF D8 FF
    for (int i = 0; i + 3 <= len; ++i) {
        if (d[i] == 0xFF && d[i+1] == 0xD8 && d[i+2] == 0xFF) {
            QImage img;
            if (img.loadFromData(trailing.mid(i), "JPEG")) {
                return img;
            }
        }
    }

    // Scan for PNG signature: 89 50 4E 47
    for (int i = 0; i + 8 <= len; ++i) {
        if (d[i] == 0x89 && d[i+1] == 0x50 && d[i+2] == 0x4E && d[i+3] == 0x47) {
            QImage img;
            if (img.loadFromData(trailing.mid(i), "PNG")) {
                return img;
            }
            // Try libpng directly
            img = decodePngWithLibpng(trailing.mid(i));
            if (!img.isNull() && img.width() > 1) {
                return img;
            }
        }
    }

    // Scan for WebP: RIFF....WEBP
    for (int i = 0; i + 12 <= len; ++i) {
        if (d[i] == 'R' && d[i+1] == 'I' && d[i+2] == 'F' && d[i+3] == 'F' &&
            d[i+8] == 'W' && d[i+9] == 'E' && d[i+10] == 'B' && d[i+11] == 'P') {
            QImage img;
            if (img.loadFromData(trailing.mid(i), "WEBP")) {
                return img;
            }
        }
    }

    // Try Qt auto-detect on trailing data
    QImage img;
    if (img.loadFromData(trailing)) {
        return img;
    }

    return {};
}

// --- Main decode logic for a single segment ---
// Calls onVideo/onAudio for each decoded frame. Returns total video frame count.
static int decodeSegment(const QByteArray &data,
                         const VideoFrameCallback &onVideo,
                         const AudioFrameCallback &onAudio) {
    // Fast path: check if this is a PNG with trailing data (1x1 PNG + media stream)
    int iendOff = findIendOffset(data);
    if (iendOff >= 0) {
        int trailingStart = iendOff + 12; // IEND: len(4) + type(4) + crc(4) = 12
        if (trailingStart < data.size()) {
            QByteArray trailing = data.mid(trailingStart);
            const uint8_t *tData = reinterpret_cast<const uint8_t *>(trailing.constData());
            size_t tSize = static_cast<size_t>(trailing.size());

            // MPEG-TS: use full decoder (video + audio)
            if (tSize > 0 && tData[0] == 0x47) {
                int count = decodeTrailingFull(tData, tSize, onVideo, onAudio);
                if (count > 0) return count;
            }

            // Try fast signature scan for non-TS trailing data
            QImage fastResult = decodeTrailingFast(trailing);
            if (!fastResult.isNull()) {
                onVideo(std::move(fastResult), 0.0);
                return 1;
            }

            // Fallback: FFmpeg video-only
            QImage ffmpegResult = decodeTrailingWithFFmpeg(tData, tSize);
            if (!ffmpegResult.isNull()) {
                onVideo(std::move(ffmpegResult), 0.0);
                return 1;
            }
        }
    }

    // Fallback: try standard PNG decode (real PNG files)
    QImage image = decodePngWithLibpng(data);
    if (!image.isNull() && image.width() > 1 && image.height() > 1) {
        onVideo(std::move(image), 0.0);
        return 1;
    }

    // Fallback: try Qt decoder on full data
    QImage qtResult;
    if (qtResult.loadFromData(data)) {
        onVideo(std::move(qtResult), 0.0);
        return 1;
    }

    return 0;
}

// --- Download helper (runs in worker threads) ---
// Note: QNetworkAccessManager must be created INSIDE the thread that uses it.

static QByteArray downloadSegment(const QString &url, std::atomic<bool> *aborted) {
    QNetworkAccessManager nam;
    QUrl qurl(url);
    QNetworkRequest request(qurl);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/148.0.0.0 Safari/537.36");
    request.setRawHeader("Referer", "https://bbys.app/");
    request.setRawHeader("Origin", "https://bbys.app");
    request.setRawHeader("Accept", "*/*");

    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    sslConfig.setProtocol(QSsl::TlsV1_2);
    request.setSslConfiguration(sslConfig);

    QNetworkReply *reply = nam.get(request);
    QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &) {
        reply->ignoreSslErrors();
    });

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer abortCheck;
    abortCheck.setInterval(100);
    QObject::connect(&abortCheck, &QTimer::timeout, [aborted, &loop]() {
        if (aborted->load()) loop.quit();
    });
    abortCheck.start();
    loop.exec();
    abortCheck.stop();

    if (aborted->load() || reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return {};
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();
    return data;
}

// --- ImageHlsWorker ---

void ImageHlsWorker::configure(std::vector<ImageHlsSegment> segments,
                                FrameQueue *frameQueue, int startSegment,
                                PcmAudioPlayer *audioPlayer) {
    segments_ = std::move(segments);
    frameQueue_ = frameQueue;
    startSegment_ = startSegment;
    audioPlayer_ = audioPlayer;
}

void ImageHlsWorker::start() {
    if (!frameQueue_ || segments_.empty()) {
        emit error("ImageHlsWorker: not configured");
        emit finished();
        return;
    }

    constexpr int PREFETCH_COUNT = 4;
    const int totalSegments = static_cast<int>(segments_.size());

    // Prefetch buffer: segment index → downloaded data
    std::map<int, QByteArray> downloadBuffer;
    std::mutex bufferMutex;
    std::condition_variable bufferCv;

    // Track active download threads
    std::vector<std::thread> downloadThreads;
    std::atomic<int> activeDownloads{0};
    std::atomic<bool> allDownloadsLaunched{false};

    // Launch prefetch downloads
    auto launchDownload = [&](int segIdx) {
        ++activeDownloads;
        downloadThreads.emplace_back([&, segIdx]() {
            QByteArray data = downloadSegment(segments_[segIdx].url, &aborted_);
            {
                std::lock_guard<std::mutex> lock(bufferMutex);
                downloadBuffer[segIdx] = std::move(data);
            }
            bufferCv.notify_one();
            --activeDownloads;
        });
    };

    // Launch initial prefetch batch
    int nextToLaunch = startSegment_;
    auto launchPrefetchBatch = [&]() {
        while (nextToLaunch < totalSegments &&
               nextToLaunch < startSegment_ + PREFETCH_COUNT &&
               !aborted_.load()) {
            launchDownload(nextToLaunch);
            ++nextToLaunch;
        }
    };

    launchPrefetchBatch();

    // Main decode loop
    for (int i = startSegment_; i < totalSegments; ++i) {
        if (aborted_.load()) break;

        // Wait for this segment's data to be downloaded
        QByteArray data;
        {
            std::unique_lock<std::mutex> lock(bufferMutex);
            bufferCv.wait(lock, [&]() {
                return downloadBuffer.count(i) > 0 || aborted_.load();
            });
            if (aborted_.load()) break;
            data = std::move(downloadBuffer[i]);
            downloadBuffer.erase(i);
        }

        // Launch next prefetch
        if (nextToLaunch < totalSegments && !aborted_.load()) {
            launchDownload(nextToLaunch);
            ++nextToLaunch;
        }

        if (data.isEmpty()) {
            Logger::instance().warn("ImageHlsWorker: empty data for segment " + QString::number(i));
            continue;
        }

        // Decode video + audio from segment
        double segStartPts = segments_[i].startPtsSec;
        int pushCount = 0;
        int videoCount = decodeSegment(data,
            [&](QImage &&img, double ptsSec) {
                // Video frame callback: push to frame queue
                if (aborted_.load()) return;
                TimedVideoFrame frame;
                frame.image = std::move(img);
                // Use per-frame PTS if available (non-zero), otherwise use segment start PTS
                frame.ptsSec = (ptsSec > 0.0) ? segStartPts + ptsSec : segStartPts;
                frameQueue_->push(std::move(frame));
                ++pushCount;
            },
            [&](PcmAudioFrame &&pcmFrame) {
                // Audio frame callback: push to audio player
                if (aborted_.load() || !audioPlayer_) return;
                // Offset audio PTS by segment start time
                pcmFrame.ptsSec += segStartPts;
                audioPlayer_->pushFrame(std::move(pcmFrame));
            });

        if (videoCount == 0) {
            Logger::instance().warn("ImageHlsWorker: failed to decode segment "
                + QString::number(i) + " (data size=" + QString::number(data.size()) + ")");
            continue;
        }

        Logger::instance().info("ImageHlsWorker: segment " + QString::number(i)
            + " decoded=" + QString::number(videoCount) + " pushed=" + QString::number(pushCount)
            + " qsize=" + QString::number(frameQueue_->size())
            + " pts=" + QString::number(segStartPts, 'f', 2) + "s");
    }

    // Wait for all download threads to finish
    for (auto &t : downloadThreads) {
        if (t.joinable()) t.join();
    }

    Logger::instance().info("ImageHlsWorker: finished");
    emit finished();
}

void ImageHlsWorker::abort() {
    aborted_ = true;
    if (currentReply_) {
        currentReply_->abort();
    }
    if (frameQueue_) {
        frameQueue_->abort();
    }
}
