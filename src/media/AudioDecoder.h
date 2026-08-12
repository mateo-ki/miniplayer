#pragma once

#include "infrastructure/Error.h"
#include "infrastructure/FfmpegWrappers.h"

struct AVFormatContext;
struct AVPacket;
struct AVFrame;
struct AVStream;
class IDecoderFactory;

class AudioDecoder {
public:
    ~AudioDecoder();

    Error initialize(AVFormatContext *ctx, int streamIndex, IDecoderFactory *factory = nullptr);
    AVFrame *decode(AVPacket *packet);
    AVFrame *receive();
    void flush();
    AVCodecContext *codecContext() const;
    int streamIndex() const;
    double timeBase() const;

private:
    UniqueAvCodecContext codecCtx_;
    AVStream *stream_ = nullptr;
    int streamIndex_ = -1;
};
