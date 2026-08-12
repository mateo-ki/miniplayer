#pragma once

#include <atomic>

#include <QObject>

#include "infrastructure/FfmpegWrappers.h"

class QAudioSink;
class QIODevice;
class PacketQueue;
class AudioDecoder;
class AudioClock;

class AudioOutput final : public QObject {
    Q_OBJECT
public:
    void configure(PacketQueue *queue, AudioDecoder *decoder, AudioClock *clock);
    void setSeekTarget(double sec);
    void setPlaybackRate(float rate);
    void setVolume(float volume);

    void resetSink();

public slots:
    void start();
    void abort();
    void pause();
    void resume();

signals:
    void finished();
    void error(const QString &message);
    void positionUpdated(double sec);
    void seekTargetReached();

private:
    enum class FadeState { None, FadingIn, FadingOut };

    PacketQueue *queue_ = nullptr;
    AudioDecoder *decoder_ = nullptr;
    AudioClock *clock_ = nullptr;
    QAudioSink *audioSink_ = nullptr;
    QIODevice *ioDevice_ = nullptr;
    UniqueSwrContext swrCtx_;
    std::atomic<bool> aborted_ = false;
    std::atomic<bool> paused_ = false;
    int outputSampleRate_ = 44100;
    int outputChannels_ = 2;
    int bytesPerSample_ = 2;
    std::atomic<double> seekTargetSec_{-1.0};
    std::atomic<float> playbackRate_{1.0f};
    int sourceSampleRate_ = 0;
    std::atomic<float> volume_{1.0f};

    // Fade in/out state
    FadeState fadeState_ = FadeState::None;
    float fadeMultiplier_ = 1.0f;
    static constexpr float kFadeStep = 0.05f; // ~20 steps for full fade
    static constexpr float kFadeMin = 0.0f;
    static constexpr float kFadeMax = 1.0f;

    void setupResampler();
    void cleanup();
    void updateFade();
};
