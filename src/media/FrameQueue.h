#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <deque>

#include <QImage>

struct TimedVideoFrame {
    QImage image;
    double ptsSec = 0.0;
};

class FrameQueue {
public:
    explicit FrameQueue(size_t maxSize = 256);
    void push(TimedVideoFrame frame);
    void pushFront(TimedVideoFrame frame);
    TimedVideoFrame pop();
    TimedVideoFrame peek() const;
    std::optional<TimedVideoFrame> tryPop();
    void clear();
    bool isEmpty() const;
    size_t size() const;
    void abort();
    void resume();

private:
    size_t maxSize_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<TimedVideoFrame> queue_;
    std::atomic<bool> aborted_ = false;
};
