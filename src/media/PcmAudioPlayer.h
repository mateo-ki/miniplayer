#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <queue>

#include <QObject>

class QAudioSink;
class QIODevice;

struct PcmAudioFrame {
    std::vector<uint8_t> data; // S16 interleaved PCM
    int sampleRate = 0;
    int channels = 0;
    double ptsSec = 0.0;
};

class PcmAudioPlayer final : public QObject {
    Q_OBJECT
public:
    void setVolume(float volume);
    void pushFrame(PcmAudioFrame frame);

public slots:
    void start();
    void abort();
    void pause();
    void resume();

signals:
    void finished();
    void positionUpdated(double sec);

private:
    QAudioSink *audioSink_ = nullptr;
    QIODevice *ioDevice_ = nullptr;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<PcmAudioFrame> queue_;

    std::atomic<bool> aborted_ = false;
    std::atomic<bool> paused_ = false;
    std::atomic<float> volume_{1.0f};

    void cleanup();
};
