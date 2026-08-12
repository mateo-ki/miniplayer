#include "media/PcmAudioPlayer.h"

#include "infrastructure/Logger.h"

#include <QAudioSink>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QThread>

void PcmAudioPlayer::setVolume(float volume) {
    volume_ = volume;
}

void PcmAudioPlayer::pushFrame(PcmAudioFrame frame) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Keep queue bounded to avoid memory blowup (max ~30 frames ≈ few seconds)
        while (queue_.size() > 30) {
            queue_.pop();
        }
        queue_.push(std::move(frame));
    }
    cv_.notify_one();
}

void PcmAudioPlayer::start() {
    // Wait for first frame to know the audio format
    PcmAudioFrame firstFrame;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return !queue_.empty() || aborted_.load(); });
        if (aborted_) {
            emit finished();
            return;
        }
        firstFrame = std::move(queue_.front());
        queue_.pop();
    }

    QAudioFormat format;
    format.setSampleRate(firstFrame.sampleRate);
    format.setChannelCount(firstFrame.channels);
    format.setSampleFormat(QAudioFormat::Int16);

    audioSink_ = new QAudioSink(QMediaDevices::defaultAudioOutput(), format);
    ioDevice_ = audioSink_->start();

    if (!ioDevice_) {
        Logger::instance().error("PcmAudioPlayer: failed to start audio sink");
        cleanup();
        emit finished();
        return;
    }

    Logger::instance().info("PcmAudioPlayer started: " +
        QString::number(firstFrame.sampleRate) + "Hz " +
        QString::number(firstFrame.channels) + "ch");

    // Write first frame
    {
        const auto &d = firstFrame.data;
        int totalWritten = 0;
        int writeSize = static_cast<int>(d.size());
        float vol = volume_.load();
        auto *buf = const_cast<uint8_t *>(d.data());

        // Apply volume if needed
        std::vector<uint8_t> volBuf;
        if (vol < 0.99f || vol > 1.01f) {
            volBuf = d;
            auto *samples = reinterpret_cast<int16_t *>(volBuf.data());
            int count = writeSize / 2;
            for (int i = 0; i < count; ++i) {
                float s = samples[i] * vol;
                if (s > 32767.0f) s = 32767.0f;
                if (s < -32768.0f) s = -32768.0f;
                samples[i] = static_cast<int16_t>(s);
            }
            buf = volBuf.data();
        }

        while (totalWritten < writeSize && !aborted_) {
            int free = audioSink_->bytesFree();
            if (free <= 0) {
                QThread::msleep(2);
                continue;
            }
            int chunk = std::min(free, writeSize - totalWritten);
            qint64 w = ioDevice_->write(reinterpret_cast<const char *>(buf) + totalWritten, chunk);
            if (w > 0) totalWritten += static_cast<int>(w);
            else QThread::msleep(2);
        }
    }

    emit positionUpdated(firstFrame.ptsSec);

    // Main loop
    while (!aborted_) {
        if (paused_) {
            QThread::msleep(50);
            continue;
        }

        PcmAudioFrame frame;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(100),
                [this]() { return !queue_.empty() || aborted_.load(); });
            if (aborted_) break;
            if (queue_.empty()) continue;
            frame = std::move(queue_.front());
            queue_.pop();
        }

        const auto &d = frame.data;
        int writeSize = static_cast<int>(d.size());
        float vol = volume_.load();
        auto *buf = d.data();

        // Apply volume
        std::vector<uint8_t> volBuf;
        if (vol < 0.99f || vol > 1.01f) {
            volBuf = d;
            auto *samples = reinterpret_cast<int16_t *>(volBuf.data());
            int count = writeSize / 2;
            for (int i = 0; i < count; ++i) {
                float s = samples[i] * vol;
                if (s > 32767.0f) s = 32767.0f;
                if (s < -32768.0f) s = -32768.0f;
                samples[i] = static_cast<int16_t>(s);
            }
            buf = volBuf.data();
        }

        int totalWritten = 0;
        while (totalWritten < writeSize && !aborted_) {
            int free = audioSink_->bytesFree();
            if (free <= 0) {
                QThread::msleep(2);
                continue;
            }
            int chunk = std::min(free, writeSize - totalWritten);
            qint64 w = ioDevice_->write(reinterpret_cast<const char *>(buf) + totalWritten, chunk);
            if (w > 0) totalWritten += static_cast<int>(w);
            else QThread::msleep(2);
        }

        emit positionUpdated(frame.ptsSec);
    }

    Logger::instance().info("PcmAudioPlayer: stopped");
    cleanup();
    emit finished();
}

void PcmAudioPlayer::abort() {
    aborted_ = true;
    cv_.notify_all();
}

void PcmAudioPlayer::pause() {
    paused_ = true;
}

void PcmAudioPlayer::resume() {
    paused_ = false;
}

void PcmAudioPlayer::cleanup() {
    if (audioSink_) {
        audioSink_->reset();
        delete audioSink_;
        audioSink_ = nullptr;
        ioDevice_ = nullptr;
    }
}
