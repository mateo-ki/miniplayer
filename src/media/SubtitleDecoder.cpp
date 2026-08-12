#include "media/SubtitleDecoder.h"

#include "infrastructure/Logger.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

SubtitleDecoder::~SubtitleDecoder() = default;

Error SubtitleDecoder::initialize(AVFormatContext *ctx, int streamIndex) {
    if (streamIndex < 0 || streamIndex >= static_cast<int>(ctx->nb_streams)) {
        return Error::failure("invalid subtitle stream index", ErrorCode::StreamNotFound);
    }

    stream_ = ctx->streams[streamIndex];
    streamIndex_ = streamIndex;

    const AVCodec *codec = avcodec_find_decoder(stream_->codecpar->codec_id);
    if (!codec) {
        return Error::failure("subtitle decoder not found", ErrorCode::DecoderInitFailed);
    }

    codecCtx_.reset(avcodec_alloc_context3(codec));
    if (!codecCtx_) {
        return Error::failure("failed to allocate subtitle codec context", ErrorCode::DecoderInitFailed);
    }

    if (avcodec_parameters_to_context(codecCtx_.get(), stream_->codecpar) < 0) {
        return Error::failure("failed to copy subtitle codec parameters", ErrorCode::DecoderInitFailed);
    }

    if (avcodec_open2(codecCtx_.get(), codec, nullptr) < 0) {
        return Error::failure("failed to open subtitle codec", ErrorCode::DecoderInitFailed);
    }

    Logger::instance().info("SubtitleDecoder initialized: codec="
        + QString::fromUtf8(codec->name));

    return Error::success();
}

std::vector<SubtitleRect> SubtitleDecoder::decode(AVPacket *packet) {
    std::vector<SubtitleRect> results;
    if (!codecCtx_) return results;

    AVSubtitle sub{};
    int gotSub = 0;
    int ret = avcodec_decode_subtitle2(codecCtx_.get(), &sub, &gotSub, packet);
    if (ret < 0 || !gotSub) return results;

    for (unsigned i = 0; i < sub.num_rects; ++i) {
        AVSubtitleRect *rect = sub.rects[i];
        SubtitleRect sr;
        sr.rect = QRect(rect->x, rect->y, rect->w, rect->h);
        sr.type = rect->type;

        if (rect->type == SUBTITLE_TEXT && rect->text) {
            sr.text = QString::fromUtf8(rect->text);
        } else if (rect->type == SUBTITLE_ASS && rect->ass) {
            sr.text = QString::fromUtf8(rect->ass);
        }

        if (!sr.text.isEmpty()) {
            results.push_back(std::move(sr));
        }
    }

    avsubtitle_free(&sub);
    return results;
}

void SubtitleDecoder::flush() {
    if (codecCtx_) {
        avcodec_flush_buffers(codecCtx_.get());
    }
}

void SubtitleDecoder::reset() {
    codecCtx_.reset();
    stream_ = nullptr;
    streamIndex_ = -1;
}
