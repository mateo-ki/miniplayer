#pragma once

extern "C" {
#include <libavformat/avformat.h>
}

#include <memory>

struct AvFormatContextDeleter {
    void operator()(AVFormatContext *context) const {
        if (context) {
            avformat_close_input(&context);
        }
    }
};

using UniqueAvFormatContext = std::unique_ptr<AVFormatContext, AvFormatContextDeleter>;
