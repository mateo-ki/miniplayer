#include "media/ThumbnailWorker.h"

#include "infrastructure/Logger.h"

#include <QThread>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

ThumbnailWorker::ThumbnailWorker(QObject *parent)
    : QObject(parent) {}

void ThumbnailWorker::setSource(const QString &path) {
    path_ = path;
}

void ThumbnailWorker::request(double positionSec) {
    requestedPos_ = positionSec;
}

void ThumbnailWorker::start() {
    if (!openDecoder()) {
        emit finished();
        return;
    }

    while (!aborted_) {
        if (requestedPos_ < 0) {
            QThread::msleep(50);
            continue;
        }

        double pos = requestedPos_;
        requestedPos_ = -1.0;

        QImage frame = decodeAt(pos);
        if (!frame.isNull() && !aborted_) {
            emit thumbnailReady(pos, frame);
        }
    }

    closeDecoder();
    emit finished();
}

void ThumbnailWorker::abort() {
    aborted_ = true;
}

bool ThumbnailWorker::openDecoder() {
    if (path_.isEmpty()) return false;

    if (avformat_open_input(&fmtCtx_, path_.toUtf8().constData(), nullptr, nullptr) < 0) {
        Logger::instance().error("ThumbnailWorker: failed to open file");
        return false;
    }

    if (avformat_find_stream_info(fmtCtx_, nullptr) < 0) {
        closeDecoder();
        return false;
    }

    for (unsigned i = 0; i < fmtCtx_->nb_streams; ++i) {
        if (fmtCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex_ = static_cast<int>(i);
            break;
        }
    }

    if (videoStreamIndex_ < 0) {
        closeDecoder();
        return false;
    }

    stream_ = fmtCtx_->streams[videoStreamIndex_];
    const AVCodec *codec = avcodec_find_decoder(stream_->codecpar->codec_id);
    if (!codec) {
        closeDecoder();
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        closeDecoder();
        return false;
    }

    if (avcodec_parameters_to_context(codecCtx_, stream_->codecpar) < 0) {
        closeDecoder();
        return false;
    }

    if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
        closeDecoder();
        return false;
    }

    Logger::instance().info("ThumbnailWorker: decoder opened for " + path_);
    return true;
}

void ThumbnailWorker::closeDecoder() {
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
    if (fmtCtx_) {
        avformat_close_input(&fmtCtx_);
        fmtCtx_ = nullptr;
    }
    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }
    stream_ = nullptr;
    videoStreamIndex_ = -1;
}

QImage ThumbnailWorker::decodeAt(double positionSec) {
    if (!fmtCtx_ || !codecCtx_) return {};

    int64_t seekTarget = static_cast<int64_t>(positionSec * AV_TIME_BASE);
    av_seek_frame(fmtCtx_, -1, seekTarget, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codecCtx_);

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    QImage result;

    while (!aborted_ && av_read_frame(fmtCtx_, pkt) >= 0) {
        if (pkt->stream_index != videoStreamIndex_) {
            av_packet_unref(pkt);
            continue;
        }

        int ret = avcodec_send_packet(codecCtx_, pkt);
        av_packet_unref(pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) break;

        ret = avcodec_receive_frame(codecCtx_, frame);
        if (ret < 0) continue;

        double pts = 0.0;
        if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
            pts = frame->best_effort_timestamp * av_q2d(stream_->time_base);
        }

        // Generate thumbnail at reduced resolution (320px wide)
        int width = 320;
        int height = static_cast<int>(width * static_cast<double>(frame->height) / frame->width);
        if (height < 1) height = 1;

        if (!swsCtx_) {
            swsCtx_ = sws_getContext(
                frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                width, height, AV_PIX_FMT_RGB32,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
        }
        if (!swsCtx_) break;

        result = QImage(width, height, QImage::Format_RGB32);
        uint8_t *dest[4] = { result.bits(), nullptr, nullptr, nullptr };
        int destLinesize[4] = { static_cast<int>(result.bytesPerLine()), 0, 0, 0 };
        sws_scale(swsCtx_, frame->data, frame->linesize, 0, frame->height, dest, destLinesize);
        result.setText("pts", QString::number(pts));

        av_frame_free(&frame);
        av_packet_free(&pkt);
        return result;
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    return result;
}
