#include "media/SoftwareDecoderFactory.h"

const AVCodec *SoftwareDecoderFactory::findDecoder(AVCodecID codecId) {
    return avcodec_find_decoder(codecId);
}

void SoftwareDecoderFactory::configureContext(AVCodecContext *ctx) {
    // Force software decoding
    ctx->hw_device_ctx = nullptr;
    ctx->get_format = nullptr;
}
