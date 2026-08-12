#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
}

class IDecoderFactory {
public:
    virtual ~IDecoderFactory() = default;

    virtual const AVCodec *findDecoder(AVCodecID codecId) = 0;
    virtual void configureContext(AVCodecContext *ctx) = 0;
};
