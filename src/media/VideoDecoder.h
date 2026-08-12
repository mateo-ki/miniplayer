#pragma once

#include "infrastructure/Error.h"
#include "infrastructure/FfmpegWrappers.h"

#include <QImage>
#include <deque>
#include <memory>
#include <vector>

struct AVFormatContext;
struct AVPacket;
struct AVFrame;
struct AVStream;
class IDecoderFactory;

class VideoDecoder {
public:
    ~VideoDecoder();

    Error initialize(AVFormatContext *ctx, int streamIndex, IDecoderFactory *factory = nullptr);
    QImage decode(AVPacket *packet);
    QImage receiveBuffered();
    void flush();
    AVCodecContext *codecContext() const;
    int streamIndex() const;
    double timeBase() const;

private:
    UniqueAvCodecContext codecCtx_;
    UniqueSwsContext swsCtx_;
    AVStream *stream_ = nullptr;
    int streamIndex_ = -1;
    std::deque<UniqueAvFrame> remainingFrames_;

    // Dedicated sws output buffer (avoids writing directly into QImage)
    std::vector<uint8_t> swsBuffer_;
    uint8_t *swsDestData_[4] = {};
    int swsDestLinesize_[4] = {};
};
