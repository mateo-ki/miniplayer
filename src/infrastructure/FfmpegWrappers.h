#pragma once

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
}

#include <memory>

struct AvFormatContextDeleter {
    void operator()(AVFormatContext *context) const {
        if (context) {
            avformat_close_input(&context);
        }
    }
};

struct AvCodecContextDeleter {
    void operator()(AVCodecContext *ctx) const {
        if (ctx) {
            avcodec_free_context(&ctx);
        }
    }
};

struct SwsContextDeleter {
    void operator()(SwsContext *ctx) const {
        if (ctx) {
            sws_freeContext(ctx);
        }
    }
};

struct SwrContextDeleter {
    void operator()(SwrContext *ctx) const {
        if (ctx) {
            swr_free(&ctx);
        }
    }
};

struct AvFrameDeleter {
    void operator()(AVFrame *frame) const {
        if (frame) {
            av_frame_free(&frame);
        }
    }
};

struct AvPacketDeleter {
    void operator()(AVPacket *pkt) const {
        if (pkt) {
            av_packet_free(&pkt);
        }
    }
};

using UniqueAvFormatContext = std::unique_ptr<AVFormatContext, AvFormatContextDeleter>;
using UniqueAvCodecContext = std::unique_ptr<AVCodecContext, AvCodecContextDeleter>;
using UniqueSwsContext = std::unique_ptr<SwsContext, SwsContextDeleter>;
using UniqueSwrContext = std::unique_ptr<SwrContext, SwrContextDeleter>;
using UniqueAvFrame = std::unique_ptr<AVFrame, AvFrameDeleter>;
using UniqueAvPacket = std::unique_ptr<AVPacket, AvPacketDeleter>;
