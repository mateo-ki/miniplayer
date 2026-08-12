#include "media/PacketQueue.h"

#include "infrastructure/Logger.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

PacketQueue::PacketQueue(const char *name, size_t maxSize) : name_(name), maxSize_(maxSize) {}

PacketQueue::~PacketQueue() {
    clear();
}

void PacketQueue::push(AVPacket *packet) {
    AVPacket *ref = av_packet_alloc();
    av_packet_ref(ref, packet);

    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return queue_.size() < maxSize_ || aborted_; });
    if (aborted_) {
        av_packet_free(&ref);
        return;
    }
    queue_.push(ref);
    cv_.notify_one();
}

AVPacket *PacketQueue::pop() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty() || aborted_ || eofSignaled_; });

    if (aborted_) {
        return nullptr;
    }
    if (queue_.empty() && eofSignaled_) {
        return nullptr;
    }

    AVPacket *pkt = queue_.front();
    queue_.pop();
    cv_.notify_one(); // wake blocked producers
    return pkt;
}

AVPacket *PacketQueue::tryPop() {
    std::scoped_lock lock(mutex_);
    if (queue_.empty()) return nullptr;
    AVPacket *pkt = queue_.front();
    queue_.pop();
    cv_.notify_one();
    return pkt;
}

void PacketQueue::clear() {
    std::scoped_lock lock(mutex_);
    int count = 0;
    while (!queue_.empty()) {
        AVPacket *pkt = queue_.front();
        av_packet_free(&pkt);
        queue_.pop();
        ++count;
    }
    if (count > 0) {
        Logger::instance().info("PacketQueue[" + QString::fromUtf8(name_)
            + "] cleared, " + QString::number(count) + " packets dropped");
    }
}

bool PacketQueue::isEmpty() const {
    std::scoped_lock lock(mutex_);
    return queue_.empty();
}

void PacketQueue::abort() {
    std::scoped_lock lock(mutex_);
    aborted_ = true;
    cv_.notify_all();
}

void PacketQueue::resume() {
    std::scoped_lock lock(mutex_);
    aborted_ = false;
    eofSignaled_ = false;
}

void PacketQueue::signalEof() {
    std::scoped_lock lock(mutex_);
    eofSignaled_ = true;
    cv_.notify_all();
}
