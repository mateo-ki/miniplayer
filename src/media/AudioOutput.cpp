#include "media/AudioOutput.h"

#include "infrastructure/Logger.h"
#include "media/AudioDecoder.h"
#include "media/AudioClock.h"
#include "media/PacketQueue.h"

#include <QAudioSink>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QThread>

#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

void AudioOutput::configure(PacketQueue *queue, AudioDecoder *decoder, AudioClock *clock) {
    queue_ = queue;
    decoder_ = decoder;
    clock_ = clock;
}

void AudioOutput::setSeekTarget(double sec) {
    seekTargetSec_.store(sec);
}

void AudioOutput::setPlaybackRate(float rate) {
    playbackRate_ = rate;
    Logger::instance().info("AudioOutput: playback rate=" + QString::number(rate, 'f', 2));
}

void AudioOutput::setVolume(float volume) {
    volume_ = volume;
}

void AudioOutput::setupResampler() {
    AVCodecContext *codecCtx = decoder_->codecContext();

    sourceSampleRate_ = codecCtx->sample_rate > 0 ? codecCtx->sample_rate : 44100;
    outputSampleRate_ = sourceSampleRate_;
    outputChannels_ = 2;

    swrCtx_.reset(swr_alloc());
    if (!swrCtx_) {
        Logger::instance().error("AudioOutput: failed to allocate resampler");
        return;
    }

    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;

    av_opt_set_chlayout(swrCtx_.get(), "in_chlayout", &codecCtx->ch_layout, 0);
    av_opt_set_int(swrCtx_.get(), "in_sample_rate", codecCtx->sample_rate, 0);
    av_opt_set_sample_fmt(swrCtx_.get(), "in_sample_fmt", codecCtx->sample_fmt, 0);

    av_opt_set_chlayout(swrCtx_.get(), "out_chlayout", &outLayout, 0);
    av_opt_set_int(swrCtx_.get(), "out_sample_rate", outputSampleRate_, 0);
    av_opt_set_sample_fmt(swrCtx_.get(), "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    if (swr_init(swrCtx_.get()) < 0) {
        Logger::instance().error("AudioOutput: failed to initialize resampler");
        swrCtx_.reset();
        return;
    }

    Logger::instance().info("AudioOutput: resampler initialized to "
        + QString::number(outputSampleRate_) + "Hz "
        + QString::number(outputChannels_) + "ch s16");
}

void AudioOutput::start() {
    aborted_ = false;
    paused_ = false;
    fadeState_ = FadeState::None;
    fadeMultiplier_ = 1.0f;
    setupResampler();

    QAudioFormat format;
    format.setSampleRate(outputSampleRate_);
    format.setChannelCount(outputChannels_);
    format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();
    if (!outputDevice.isFormatSupported(format)) {
        Logger::instance().warn("AudioOutput: requested format unsupported, using preferred device format");
        format = outputDevice.preferredFormat();
        outputSampleRate_ = format.sampleRate();
        outputChannels_ = format.channelCount();
    }

    audioSink_ = new QAudioSink(outputDevice, format);
    audioSink_->setVolume(volume_.load());
    connect(audioSink_, &QAudioSink::stateChanged, this, [](QAudio::State state) {
        Logger::instance().info("AudioOutput: sink state=" + QString::number(static_cast<int>(state)));
    });
    ioDevice_ = audioSink_->start();

    if (!ioDevice_) {
        Logger::instance().error("AudioOutput: failed to start audio sink");
        emit error("failed to start audio sink");
        cleanup();
        emit finished();
        return;
    }

    Logger::instance().info("AudioOutput: initial clock=" + QString::number(clock_->positionSec(), 'f', 3) + "s");
    Logger::instance().info("AudioOutput started: format="
        + QString::number(outputSampleRate_) + "Hz "
        + QString::number(outputChannels_) + "ch 16bit");

    int audioFrameCount = 0;
    while (!aborted_) {
        if (paused_) {
            QThread::msleep(50);
            continue;
        }

        // Update fade multiplier
        updateFade();

        AVPacket *pkt = queue_->pop();
        if (!pkt || aborted_) {
            if (pkt) av_packet_free(&pkt);
            if (!aborted_) {
                while (true) {
                    AVFrame *drainedFrame = decoder_->decode(nullptr);
                    if (!drainedFrame) break;

                    ++audioFrameCount;
                    double framePts = -1.0;
                    if (drainedFrame->best_effort_timestamp != AV_NOPTS_VALUE) {
                        framePts = drainedFrame->best_effort_timestamp * decoder_->timeBase();
                    }
                    if (swrCtx_) {
                        int outSamples = swr_get_out_samples(swrCtx_.get(), drainedFrame->nb_samples);
                        int bufSize = outSamples * outputChannels_ * bytesPerSample_;
                        auto *buf = new uint8_t[bufSize];
                        uint8_t *outBuf[] = { buf };
                        int converted = swr_convert(swrCtx_.get(), outBuf, outSamples,
                            const_cast<const uint8_t **>(drainedFrame->extended_data), drainedFrame->nb_samples);
                        if (converted > 0 && !aborted_) {
                            int writeSize = converted * outputChannels_ * bytesPerSample_;
                            float vol = volume_.load() * fadeMultiplier_;
                            auto *samples = reinterpret_cast<int16_t *>(buf);
                            if (vol < 0.99f || vol > 1.01f || fadeState_ != FadeState::None) {
                                for (int i = 0; i < converted * outputChannels_; ++i) {
                                    float s = samples[i] * vol;
                                    if (s > 32767.0f) s = 32767.0f;
                                    if (s < -32768.0f) s = -32768.0f;
                                    samples[i] = static_cast<int16_t>(s);
                                }
                            }
                            int totalWritten = 0;
                            while (totalWritten < writeSize) {
                                if (aborted_) break;
                                int free = audioSink_->bytesFree();
                                if (free <= 0) {
                                    QThread::msleep(2);
                                    continue;
                                }
                                int chunk = std::min(free, writeSize - totalWritten);
                                qint64 w = ioDevice_->write(reinterpret_cast<const char *>(buf) + totalWritten, chunk);
                                if (w > 0) {
                                    totalWritten += static_cast<int>(w);
                                } else {
                                    QThread::msleep(2);
                                }
                            }
                            if (framePts >= 0) {
                                clock_->update(framePts);
                                emit positionUpdated(framePts);
                            }
                        }
                        delete[] buf;
                    }
                    av_frame_free(&drainedFrame);
                }
            }
            break;
        }

        AVFrame *frame = decoder_->decode(pkt);
        av_packet_free(&pkt);

        if (!frame || aborted_) {
            if (!frame && !aborted_) {
                Logger::instance().warn("AudioOutput: decode returned null frame, skipping");
            }
            if (frame) av_frame_free(&frame);
            continue;
        }

        ++audioFrameCount;

        // 获取帧的 PTS
        double framePts = -1.0;
        if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
            framePts = frame->best_effort_timestamp * decoder_->timeBase();
        }

        // 跳过 seek 目标之前的帧（不播放，只更新时钟）
        if (seekTargetSec_ >= 0 && framePts >= 0 && framePts < seekTargetSec_) {
            clock_->update(framePts);
            av_frame_free(&frame);
            continue;
        }
        // 已到达 seek 目标，清除标记
        if (seekTargetSec_ >= 0) {
            Logger::instance().info("AudioOutput: seek target reached at " + QString::number(framePts, 'f', 3) + "s");
            seekTargetSec_ = -1.0;
            emit seekTargetReached();
        }

        if (swrCtx_) {
            int outSamples = swr_get_out_samples(swrCtx_.get(), frame->nb_samples);
            int bufSize = outSamples * outputChannels_ * bytesPerSample_;
            auto *buf = new uint8_t[bufSize];

            uint8_t *outBuf[] = { buf };
            int converted = swr_convert(swrCtx_.get(), outBuf, outSamples,
                const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);

            if (converted > 0 && !aborted_) {
                float rate = playbackRate_.load();
                int totalSamples = converted * outputChannels_;
                auto *srcSamples = reinterpret_cast<int16_t *>(buf);
                // 倍速：线性插值
                int outCount = static_cast<int>(totalSamples / rate);
                outCount = std::max(outCount, 1);
                auto *outBuf2 = new int16_t[outCount];
                for (int i = 0; i < outCount; ++i) {
                    float srcIdx = i * rate;
                    int idx0 = static_cast<int>(srcIdx);
                    float frac = srcIdx - idx0;
                    int idx1 = idx0 + 1;
                    if (idx0 >= totalSamples) idx0 = totalSamples - 1;
                    if (idx1 >= totalSamples) idx1 = totalSamples - 1;
                    float s0 = srcSamples[idx0];
                    float s1 = srcSamples[idx1];
                    outBuf2[i] = static_cast<int16_t>(s0 + frac * (s1 - s0));
                }
                int writeSize = outCount * bytesPerSample_;
                // 应用音量 + fade
                float vol = volume_.load() * fadeMultiplier_;
                if (vol < 0.99f || vol > 1.01f || fadeState_ != FadeState::None) {
                    for (int i = 0; i < outCount; ++i) {
                        float s = outBuf2[i] * vol;
                        if (s > 32767.0f) s = 32767.0f;
                        if (s < -32768.0f) s = -32768.0f;
                        outBuf2[i] = static_cast<int16_t>(s);
                    }
                }
                // Non-blocking write: abort drops remaining data immediately
                int totalWritten = 0;
                while (totalWritten < writeSize) {
                    if (aborted_) break;
                    int free = audioSink_->bytesFree();
                    if (free <= 0) {
                        QThread::msleep(2);
                        continue;
                    }
                    int chunk = std::min(free, writeSize - totalWritten);
                    qint64 w = ioDevice_->write(
                        reinterpret_cast<const char *>(outBuf2) + totalWritten,
                        chunk);
                    if (w > 0) {
                        totalWritten += static_cast<int>(w);
                    } else {
                        QThread::msleep(2);
                    }
                }
                delete[] outBuf2;
                // 用帧 PTS 更新时钟
                if (framePts >= 0) {
                    clock_->update(framePts);
                    emit positionUpdated(framePts);
                }
            }

            delete[] buf;
        }

        av_frame_free(&frame);

        while (!aborted_) {
            AVFrame *bufferedFrame = decoder_->receive();
            if (!bufferedFrame) break;

            ++audioFrameCount;

            double bufferedPts = -1.0;
            if (bufferedFrame->best_effort_timestamp != AV_NOPTS_VALUE) {
                bufferedPts = bufferedFrame->best_effort_timestamp * decoder_->timeBase();
            }

            if (seekTargetSec_ >= 0 && bufferedPts >= 0 && bufferedPts < seekTargetSec_) {
                clock_->update(bufferedPts);
                av_frame_free(&bufferedFrame);
                continue;
            }
            if (seekTargetSec_ >= 0) {
                Logger::instance().info("AudioOutput: seek target reached at " + QString::number(bufferedPts, 'f', 3) + "s");
                seekTargetSec_ = -1.0;
                emit seekTargetReached();
            }

            if (swrCtx_) {
                int outSamples = swr_get_out_samples(swrCtx_.get(), bufferedFrame->nb_samples);
                int bufSize = outSamples * outputChannels_ * bytesPerSample_;
                auto *buf = new uint8_t[bufSize];

                uint8_t *outBuf[] = { buf };
                int converted = swr_convert(swrCtx_.get(), outBuf, outSamples,
                    const_cast<const uint8_t **>(bufferedFrame->extended_data), bufferedFrame->nb_samples);

                if (converted > 0 && !aborted_) {
                    float rate = playbackRate_.load();
                    int totalSamples = converted * outputChannels_;
                    auto *srcSamples = reinterpret_cast<int16_t *>(buf);
                    int outCount = static_cast<int>(totalSamples / rate);
                    outCount = std::max(outCount, 1);
                    auto *outBuf2 = new int16_t[outCount];
                    for (int i = 0; i < outCount; ++i) {
                        float srcIdx = i * rate;
                        int idx0 = static_cast<int>(srcIdx);
                        float frac = srcIdx - idx0;
                        int idx1 = idx0 + 1;
                        if (idx0 >= totalSamples) idx0 = totalSamples - 1;
                        if (idx1 >= totalSamples) idx1 = totalSamples - 1;
                        float s0 = srcSamples[idx0];
                        float s1 = srcSamples[idx1];
                        outBuf2[i] = static_cast<int16_t>(s0 + frac * (s1 - s0));
                    }
                    int writeSize = outCount * bytesPerSample_;
                    float vol = volume_.load() * fadeMultiplier_;
                    if (vol < 0.99f || vol > 1.01f || fadeState_ != FadeState::None) {
                        for (int i = 0; i < outCount; ++i) {
                            float s = outBuf2[i] * vol;
                            if (s > 32767.0f) s = 32767.0f;
                            if (s < -32768.0f) s = -32768.0f;
                            outBuf2[i] = static_cast<int16_t>(s);
                        }
                    }
                    int totalWritten = 0;
                    while (totalWritten < writeSize) {
                        if (aborted_) break;
                        int free = audioSink_->bytesFree();
                        if (free <= 0) {
                            QThread::msleep(2);
                            continue;
                        }
                        int chunk = std::min(free, writeSize - totalWritten);
                        qint64 w = ioDevice_->write(
                            reinterpret_cast<const char *>(outBuf2) + totalWritten,
                            chunk);
                        if (w > 0) {
                            totalWritten += static_cast<int>(w);
                        } else {
                            QThread::msleep(2);
                        }
                    }
                    delete[] outBuf2;
                    if (bufferedPts >= 0) {
                        clock_->update(bufferedPts);
                        emit positionUpdated(bufferedPts);
                    }
                }

                delete[] buf;
            }

            av_frame_free(&bufferedFrame);
        }
    }

    Logger::instance().info("AudioOutput: stopped");
    cleanup();
    emit finished();
}

void AudioOutput::resetSink() {
    if (audioSink_) {
        audioSink_->reset();
    }
}

void AudioOutput::abort() {
    aborted_ = true;
    if (queue_) queue_->abort();
}

void AudioOutput::pause() {
    fadeState_ = FadeState::FadingOut;
    Logger::instance().info("AudioOutput: fading out");
}

void AudioOutput::resume() {
    paused_ = false;
    fadeState_ = FadeState::FadingIn;
    Logger::instance().info("AudioOutput: fading in");
}

void AudioOutput::cleanup() {
    if (audioSink_) {
        audioSink_->reset();
        delete audioSink_;
        audioSink_ = nullptr;
        ioDevice_ = nullptr;
    }
    swrCtx_.reset();
}

void AudioOutput::updateFade() {
    if (fadeState_ == FadeState::FadingOut) {
        fadeMultiplier_ -= kFadeStep;
        if (fadeMultiplier_ <= kFadeMin) {
            fadeMultiplier_ = kFadeMin;
            fadeState_ = FadeState::None;
            paused_ = true;
            Logger::instance().info("AudioOutput: fade-out complete, paused");
        }
    } else if (fadeState_ == FadeState::FadingIn) {
        fadeMultiplier_ += kFadeStep;
        if (fadeMultiplier_ >= kFadeMax) {
            fadeMultiplier_ = kFadeMax;
            fadeState_ = FadeState::None;
            Logger::instance().info("AudioOutput: fade-in complete");
        }
    }
}
