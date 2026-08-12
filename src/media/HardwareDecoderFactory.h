#pragma once

#include "media/IDecoderFactory.h"

extern "C" {
#include <libavutil/hwcontext.h>
}

class HardwareDecoderFactory : public IDecoderFactory {
public:
    HardwareDecoderFactory();
    ~HardwareDecoderFactory() override;

    const AVCodec *findDecoder(AVCodecID codecId) override;
    void configureContext(AVCodecContext *ctx) override;

    bool isHardwareAccelerated() const { return hwDeviceCtx_ != nullptr; }

private:
    AVBufferRef *hwDeviceCtx_ = nullptr;
    enum AVPixelFormat hwFormat_ = AV_PIX_FMT_NONE;

    bool initHardwareContext();
    static enum AVPixelFormat getFormatCallback(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts);
};
