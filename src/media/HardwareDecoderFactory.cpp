#include "media/HardwareDecoderFactory.h"

#include "infrastructure/Logger.h"

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

namespace {
// Preferred hardware device types in order
const AVHWDeviceType kPreferredTypes[] = {
    AV_HWDEVICE_TYPE_D3D11VA,
    AV_HWDEVICE_TYPE_DXVA2,
    AV_HWDEVICE_TYPE_CUDA,
    AV_HWDEVICE_TYPE_VAAPI,
    AV_HWDEVICE_TYPE_VDPAU,
    AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
};
}

HardwareDecoderFactory::HardwareDecoderFactory() {
    initHardwareContext();
}

HardwareDecoderFactory::~HardwareDecoderFactory() {
    if (hwDeviceCtx_) {
        av_buffer_unref(&hwDeviceCtx_);
    }
}

bool HardwareDecoderFactory::initHardwareContext() {
    for (auto type : kPreferredTypes) {
        AVBufferRef *deviceCtx = nullptr;
        int ret = av_hwdevice_ctx_create(&deviceCtx, type, nullptr, nullptr, 0);
        if (ret >= 0) {
            hwDeviceCtx_ = deviceCtx;
            Logger::instance().info("HardwareDecoderFactory: initialized "
                + QString::fromUtf8(av_hwdevice_get_type_name(type)));

            // Find the hardware pixel format
            AVHWFramesConstraints *constraints = av_hwdevice_get_hwframe_constraints(hwDeviceCtx_, nullptr);
            if (constraints) {
                if (constraints->valid_sw_formats && constraints->valid_sw_formats[0] != AV_PIX_FMT_NONE) {
                    hwFormat_ = constraints->valid_hw_formats[0];
                }
                av_hwframe_constraints_free(&constraints);
            }
            return true;
        }
    }
    Logger::instance().warn("HardwareDecoderFactory: no hardware acceleration available");
    return false;
}

const AVCodec *HardwareDecoderFactory::findDecoder(AVCodecID codecId) {
    // Try hardware-specific decoder first, fall back to software
    if (hwDeviceCtx_) {
        // For H.264, try h264_cuvid, h264_qsv, etc.
        // For HEVC, try hevc_cuvid, hevc_qsv, etc.
        // Fall back to generic decoder which will use hwaccel
    }
    return avcodec_find_decoder(codecId);
}

void HardwareDecoderFactory::configureContext(AVCodecContext *ctx) {
    if (!hwDeviceCtx_) return;

    ctx->hw_device_ctx = av_buffer_ref(hwDeviceCtx_);
    ctx->get_format = getFormatCallback;
    ctx->opaque = &hwFormat_;
}

enum AVPixelFormat HardwareDecoderFactory::getFormatCallback(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
    auto *targetFormat = static_cast<enum AVPixelFormat *>(ctx->opaque);
    if (targetFormat && *targetFormat != AV_PIX_FMT_NONE) {
        for (const enum AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
            if (*p == *targetFormat) return *p;
        }
    }
    // Fallback to first hardware format
    for (const enum AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);
        if (desc && (desc->flags & AV_PIX_FMT_FLAG_HWACCEL)) return *p;
    }
    return pix_fmts[0];
}
