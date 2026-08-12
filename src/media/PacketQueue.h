#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>

struct AVPacket;

class PacketQueue {
public:
    explicit PacketQueue(const char *name = "default", size_t maxSize = 512);
    ~PacketQueue();

    void push(AVPacket *packet);
    AVPacket *pop();
    AVPacket *tryPop();
    void clear();
    bool isEmpty() const;
    void abort();
    void resume();
    void signalEof();

private:
    const char *name_;
    size_t maxSize_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<AVPacket *> queue_;
    std::atomic<bool> aborted_ = false;
    std::atomic<bool> eofSignaled_ = false;
};
