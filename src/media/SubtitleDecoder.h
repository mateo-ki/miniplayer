#pragma once

#include "infrastructure/Error.h"
#include "infrastructure/FfmpegWrappers.h"

#include <QString>
#include <QImage>
#include <QRect>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

struct SubtitleRect {
    QString text;
    QRect rect;
    int type; // 0=none, 1=bitmap, 2=text, 3=ass
};

class SubtitleDecoder {
public:
    ~SubtitleDecoder();

    Error initialize(AVFormatContext *ctx, int streamIndex);
    std::vector<SubtitleRect> decode(AVPacket *packet);
    void flush();
    void reset();
    int streamIndex() const { return streamIndex_; }
    bool isInitialized() const { return codecCtx_ != nullptr; }

private:
    UniqueAvCodecContext codecCtx_;
    AVStream *stream_ = nullptr;
    int streamIndex_ = -1;
};
