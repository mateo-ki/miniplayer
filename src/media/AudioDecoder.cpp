#include "media/AudioDecoder.h"

#include "infrastructure/Logger.h"
#include "media/IDecoderFactory.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

AudioDecoder::~AudioDecoder() = default;

Error AudioDecoder::initialize(AVFormatContext *ctx, int streamIndex, IDecoderFactory *factory) {
    if (streamIndex < 0 || streamIndex >= static_cast<int>(ctx->nb_streams)) {
        return Error::failure("invalid audio stream index", ErrorCode::StreamNotFound);
    }

    stream_ = ctx->streams[streamIndex];
    streamIndex_ = streamIndex;

    const AVCodec *codec = factory
        ? factory->findDecoder(stream_->codecpar->codec_id)
        : avcodec_find_decoder(stream_->codecpar->codec_id);
    if (!codec) {
        return Error::failure("audio decoder not found", ErrorCode::DecoderInitFailed);
    }

    codecCtx_.reset(avcodec_alloc_context3(codec));
    if (!codecCtx_) {
        return Error::failure("failed to allocate audio codec context", ErrorCode::DecoderInitFailed);
    }

    if (avcodec_parameters_to_context(codecCtx_.get(), stream_->codecpar) < 0) {
        return Error::failure("failed to copy audio codec parameters", ErrorCode::DecoderInitFailed);
    }

    if (factory) {
        factory->configureContext(codecCtx_.get());
    }

    if (avcodec_open2(codecCtx_.get(), codec, nullptr) < 0) {
        return Error::failure("failed to open audio codec", ErrorCode::DecoderInitFailed);
    }

    Logger::instance().info("AudioDecoder initialized: codec="
        + QString::fromUtf8(codec->name)
        + ", sample_rate=" + QString::number(codecCtx_->sample_rate)
        + ", channels=" + QString::number(codecCtx_->ch_layout.nb_channels));

    return Error::success();
}

AVFrame *AudioDecoder::decode(AVPacket *packet) {
    int ret = avcodec_send_packet(codecCtx_.get(), packet);
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
        return nullptr;
    }

    return receive();
}

AVFrame *AudioDecoder::receive() {
    AVFrame *frame = av_frame_alloc();
    int ret = avcodec_receive_frame(codecCtx_.get(), frame);
    if (ret < 0) {
        av_frame_free(&frame);
        return nullptr;
    }

    return frame;
}

void AudioDecoder::flush() {
    if (codecCtx_) {
        avcodec_flush_buffers(codecCtx_.get());
    }
}

AVCodecContext *AudioDecoder::codecContext() const {
    return codecCtx_.get();
}

int AudioDecoder::streamIndex() const {
    return streamIndex_;
}

double AudioDecoder::timeBase() const {
    if (stream_) {
        return av_q2d(stream_->time_base);
    }
    return 0.0;
}
