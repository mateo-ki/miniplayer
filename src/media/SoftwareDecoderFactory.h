#pragma once

#include "media/IDecoderFactory.h"

class SoftwareDecoderFactory : public IDecoderFactory {
public:
    const AVCodec *findDecoder(AVCodecID codecId) override;
    void configureContext(AVCodecContext *ctx) override;
};
