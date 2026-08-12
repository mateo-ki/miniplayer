#include "media/FrameQueue.h"

FrameQueue::FrameQueue(size_t maxSize) : maxSize_(maxSize) {}

void FrameQueue::push(TimedVideoFrame frame) {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return queue_.size() < maxSize_ || aborted_; });
    if (aborted_) return;
    queue_.push_back(std::move(frame));
    cv_.notify_one();
}

void FrameQueue::pushFront(TimedVideoFrame frame) {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return queue_.size() < maxSize_ || aborted_; });
    if (aborted_) return;
    queue_.push_front(std::move(frame));
    cv_.notify_one();
}

TimedVideoFrame FrameQueue::pop() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty() || aborted_; });

    if (aborted_ && queue_.empty()) {
        return {};
    }

    TimedVideoFrame frame = std::move(queue_.front());
    queue_.pop_front();
    cv_.notify_one(); // wake blocked producers
    return frame;
}

std::optional<TimedVideoFrame> FrameQueue::tryPop() {
    std::scoped_lock lock(mutex_);
    if (queue_.empty()) return std::nullopt;
    TimedVideoFrame frame = std::move(queue_.front());
    queue_.pop_front();
    cv_.notify_one();
    return frame;
}

TimedVideoFrame FrameQueue::peek() const {
    std::scoped_lock lock(mutex_);
    if (queue_.empty()) {
        return {};
    }
    return queue_.front();
}

void FrameQueue::clear() {
    std::scoped_lock lock(mutex_);
    std::deque<TimedVideoFrame> empty;
    queue_.swap(empty);
    cv_.notify_all();
}

bool FrameQueue::isEmpty() const {
    std::scoped_lock lock(mutex_);
    return queue_.empty();
}

size_t FrameQueue::size() const {
    std::scoped_lock lock(mutex_);
    return queue_.size();
}

void FrameQueue::abort() {
    std::scoped_lock lock(mutex_);
    aborted_ = true;
    cv_.notify_all();
}

void FrameQueue::resume() {
    std::scoped_lock lock(mutex_);
    aborted_ = false;
}
