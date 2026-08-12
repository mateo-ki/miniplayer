#pragma once

#include <atomic>
#include <chrono>
#include <vector>

#include <QString>

#include "infrastructure/Error.h"
#include "infrastructure/FfmpegWrappers.h"
#include "media/ImageHlsSegment.h"

class MediaSession {
public:
    Error open(const QString &path);
    void close();
    void abortIo();
    void resumeIo();
    bool isOpen() const;
    bool isNetwork() const;
    bool isSeekable() const; // true for VOD streams (has duration)
    bool isLive() const;     // true for live streams (no duration)
    bool isImageHls() const;
    AVFormatContext *formatContext() const;
    const std::vector<ImageHlsSegment> &imageHlsSegments() const;
    double imageHlsDuration() const;

    // Buffer progress: 0.0 ~ 1.0, -1 if unknown
    double bufferProgress() const;

private:
    UniqueAvFormatContext formatContext_;
    bool network_ = false;
    bool imageHls_ = false;
    QString tempM3u8Path_;
    std::vector<ImageHlsSegment> imageHlsSegments_;
    double imageHlsDuration_ = 0.0;

    // Interrupt callback state for network timeout
    struct InterruptData {
        std::atomic<bool> abort{false};
        std::atomic<int64_t> timeoutMs{15000};
        std::chrono::steady_clock::time_point lastActivity;
    };
    InterruptData interruptData_;

    Error openNetwork(const QString &url);
    bool parseImageHls(const QString &content);
    static int interruptCallback(void *ctx);
};
