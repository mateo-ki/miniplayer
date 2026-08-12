#include "media/VideoDecoder.h"

#include "infrastructure/Logger.h"
#include "media/IDecoderFactory.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

VideoDecoder::~VideoDecoder() = default;

Error VideoDecoder::initialize(AVFormatContext *ctx, int streamIndex, IDecoderFactory *factory) {
    if (streamIndex < 0 || streamIndex >= static_cast<int>(ctx->nb_streams)) {
        return Error::failure("invalid video stream index", ErrorCode::StreamNotFound);
    }

    stream_ = ctx->streams[streamIndex];
    streamIndex_ = streamIndex;

    const AVCodec *codec = factory
        ? factory->findDecoder(stream_->codecpar->codec_id)
        : avcodec_find_decoder(stream_->codecpar->codec_id);
    if (!codec) {
        return Error::failure("video decoder not found", ErrorCode::DecoderInitFailed);
    }

    codecCtx_.reset(avcodec_alloc_context3(codec));
    if (!codecCtx_) {
        return Error::failure("failed to allocate video codec context", ErrorCode::DecoderInitFailed);
    }

    // Reset sws context and buffered frames for new video
    swsCtx_.reset();
    remainingFrames_.clear();

    if (avcodec_parameters_to_context(codecCtx_.get(), stream_->codecpar) < 0) {
        return Error::failure("failed to copy video codec parameters", ErrorCode::DecoderInitFailed);
    }

    if (factory) {
        factory->configureContext(codecCtx_.get());
    }

    if (avcodec_open2(codecCtx_.get(), codec, nullptr) < 0) {
        return Error::failure("failed to open video codec", ErrorCode::DecoderInitFailed);
    }

    double fps = 0.0;
    if (stream_->avg_frame_rate.den > 0) {
        fps = av_q2d(stream_->avg_frame_rate);
    }

    Logger::instance().info("VideoDecoder initialized: codec="
        + QString::fromUtf8(codec->name)
        + ", " + QString::number(codecCtx_->width) + "x" + QString::number(codecCtx_->height)
        + ", fps=" + QString::number(fps, 'f', 2));

    return Error::success();
}

QImage VideoDecoder::decode(AVPacket *packet) {
    int ret = avcodec_send_packet(codecCtx_.get(), packet);
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
        Logger::instance().warn("VideoDecoder: avcodec_send_packet failed, ret=" + QString::number(ret));
        return {};
    }

    // Drain all available frames from the decoder (HEVC B-frame reordering)
    UniqueAvFrame frame(av_frame_alloc());
    ret = avcodec_receive_frame(codecCtx_.get(), frame.get());
    if (ret < 0) {
        return {};
    }

    const int width = frame->width;
    const int height = frame->height;

    if (!swsCtx_) {
        swsCtx_.reset(sws_getContext(
            width, height, static_cast<AVPixelFormat>(frame->format),
            width, height, AV_PIX_FMT_RGB32,
            SWS_BILINEAR, nullptr, nullptr, nullptr));
        if (!swsCtx_) {
            Logger::instance().error("VideoDecoder: failed to create sws context");
            return {};
        }
        // Allocate dedicated output buffer for sws_scale
        int align = 1;
        swsBuffer_.resize(av_image_get_buffer_size(AV_PIX_FMT_RGB32, width, height, align));
        av_image_fill_arrays(swsDestData_, swsDestLinesize_,
            swsBuffer_.data(), AV_PIX_FMT_RGB32, width, height, align);
    }

    sws_scale(swsCtx_.get(), frame->data, frame->linesize, 0, height, swsDestData_, swsDestLinesize_);

    // Copy from sws buffer to QImage
    QImage image(width, height, QImage::Format_RGB32);
    int bpl = image.bytesPerLine();
    int copyWidth = width * 4;
    uint8_t *dst = image.bits();
    const uint8_t *src = swsDestData_[0];
    int srcLinesize = swsDestLinesize_[0];
    if (srcLinesize == bpl) {
        memcpy(dst, src, bpl * height);
    } else {
        for (int y = 0; y < height; ++y) {
            memcpy(dst + y * bpl, src + y * srcLinesize, copyWidth);
        }
    }

    double ptsSec = 0.0;
    if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
        ptsSec = frame->best_effort_timestamp * av_q2d(stream_->time_base);
    }

    image.setText("pts", QString::number(ptsSec));

    // Buffer remaining frames for subsequent calls
    while (ret >= 0) {
        remainingFrames_.emplace_back(av_frame_alloc());
        ret = avcodec_receive_frame(codecCtx_.get(), remainingFrames_.back().get());
        if (ret < 0) {
            remainingFrames_.pop_back();
            break;
        }
    }

    return image;
}

QImage VideoDecoder::receiveBuffered() {
    if (remainingFrames_.empty()) return {};

    auto &frame = remainingFrames_.front();
    const int width = frame->width;
    const int height = frame->height;

    sws_scale(swsCtx_.get(), frame->data, frame->linesize, 0, height, swsDestData_, swsDestLinesize_);

    QImage image(width, height, QImage::Format_RGB32);
    int bpl = image.bytesPerLine();
    uint8_t *dst = image.bits();
    const uint8_t *src = swsDestData_[0];
    int srcLinesize = swsDestLinesize_[0];
    for (int y = 0; y < height; ++y) {
        memcpy(dst + y * bpl, src + y * srcLinesize, width * 4);
    }

    double ptsSec = 0.0;
    if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
        ptsSec = frame->best_effort_timestamp * av_q2d(stream_->time_base);
    }

    image.setText("pts", QString::number(ptsSec));
    remainingFrames_.pop_front();
    return image;
}

void VideoDecoder::flush() {
    remainingFrames_.clear();
    if (codecCtx_) {
        avcodec_flush_buffers(codecCtx_.get());
    }
    swsCtx_.reset();
    swsBuffer_.clear();
    memset(swsDestData_, 0, sizeof(swsDestData_));
    memset(swsDestLinesize_, 0, sizeof(swsDestLinesize_));
}

AVCodecContext *VideoDecoder::codecContext() const {
    return codecCtx_.get();
}

int VideoDecoder::streamIndex() const {
    return streamIndex_;
}

double VideoDecoder::timeBase() const {
    if (stream_) {
        return av_q2d(stream_->time_base);
    }
    return 0.0;
}
