#pragma once

#include <QString>

#include "infrastructure/Error.h"
#include "infrastructure/FfmpegWrappers.h"

class MediaSession {
public:
    Error open(const QString &path);
    void close();
    bool isOpen() const;
    AVFormatContext *formatContext() const;

private:
    UniqueAvFormatContext formatContext_;
};
