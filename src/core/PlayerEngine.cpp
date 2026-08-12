#include "core/PlayerEngine.h"

#include <QTimer>

#include "infrastructure/Logger.h"
#include "media/DemuxWorker.h"
#include "media/ImageHlsWorker.h"
#include "media/PcmAudioPlayer.h"

extern "C" {
#include <libavformat/avformat.h>
}

PlayerEngine::PlayerEngine(QObject *parent)
    : QObject(parent)
    , audioPacketQueue_("audio", 512)
    , videoPacketQueue_("video", 256) {
    syncTimer_ = new QTimer(this);
    syncTimer_->setInterval(16);
    connect(syncTimer_, &QTimer::timeout, this, &PlayerEngine::onSyncTimerTick);
}

PlayerEngine::~PlayerEngine() {
    stop();
}

Error PlayerEngine::open(const QString &path) {
    stop();

    auto result = session_.open(path);
    if (!result.ok) {
        Logger::instance().error("PlayerEngine: open failed - " + result.message);
        return result;
    }

    currentPath_ = path;

    // Image HLS mode: skip FFmpeg pipeline entirely
    if (session_.isImageHls()) {
        return openImageHls();
    }

    findStreams();

    if (audioStreamIndex_ >= 0) {
        auto audioResult = audioDecoder_.initialize(session_.formatContext(), audioStreamIndex_);
        if (!audioResult.ok) {
            Logger::instance().error("PlayerEngine: audio decoder init failed - " + audioResult.message);
        }
    }

    if (videoStreamIndex_ >= 0) {
        auto videoResult = videoDecoder_.initialize(session_.formatContext(), videoStreamIndex_);
        if (!videoResult.ok) {
            Logger::instance().error("PlayerEngine: video decoder init failed - " + videoResult.message);
        }
    }

    if (subtitleStreamIndex_ >= 0) {
        auto subResult = subtitleDecoder_.initialize(session_.formatContext(), subtitleStreamIndex_);
        if (!subResult.ok) {
            Logger::instance().warn("PlayerEngine: subtitle decoder init failed - " + subResult.message);
        }
    }

    AVFormatContext *ctx = session_.formatContext();
    if (ctx->duration > 0) {
        durationSec_ = static_cast<double>(ctx->duration) / AV_TIME_BASE;
    }

    auto items = extractor_.extract(path, ctx);
    mediaInfoModel_.replaceAll(items);
    emit mediaInfoReady();

    decodedFrames_ = 0;

    // 获取视频总帧数
    totalFrames_ = 0;
    if (videoStreamIndex_ >= 0) {
        AVStream *vs = ctx->streams[videoStreamIndex_];
        if (vs->nb_frames > 0) {
            totalFrames_ = static_cast<int>(vs->nb_frames);
        } else {
            // nb_frames 可能为 0，用帧率和时长估算
            double fps = av_q2d(vs->r_frame_rate);
            if (fps > 0 && durationSec_ > 0) {
                totalFrames_ = static_cast<int>(fps * durationSec_);
            }
        }
    }

    // Extract and display the first video frame immediately
    if (!session_.isNetwork() && videoStreamIndex_ >= 0 && videoBridge_) {
        presentFirstFrame();
    }

    // Start thumbnail worker
    if (!session_.isNetwork() && videoStreamIndex_ >= 0) {
        thumbnailWorker_ = new ThumbnailWorker;
        thumbnailWorker_->setSource(path);
        thumbnailWorker_->moveToThread(&thumbnailThread_);
        connect(&thumbnailThread_, &QThread::started, thumbnailWorker_, &ThumbnailWorker::start);
        connect(thumbnailWorker_, &ThumbnailWorker::finished, &thumbnailThread_, &QThread::quit);
        connect(thumbnailWorker_, &ThumbnailWorker::thumbnailReady, this, &PlayerEngine::thumbnailReady);
        thumbnailThread_.start();
    }

    Logger::instance().info("PlayerEngine: opened " + path
        + ", audio=#" + QString::number(audioStreamIndex_)
        + " video=#" + QString::number(videoStreamIndex_)
        + " duration=" + QString::number(durationSec_, 'f', 2) + "s");

    return Error::success();
}

void PlayerEngine::close() {
    stop();

    // Stop thumbnail worker
    if (thumbnailWorker_) {
        thumbnailWorker_->abort();
        thumbnailThread_.wait(2000);
        if (thumbnailThread_.isFinished()) {
            delete thumbnailWorker_;
        }
        thumbnailWorker_ = nullptr;
        disconnect(&thumbnailThread_, nullptr, nullptr, nullptr);
    }

    session_.close();
    currentPath_.clear();
    durationSec_ = 0.0;
    pendingSeekSec_ = -1.0;
    audioStreamIndex_ = -1;
    videoStreamIndex_ = -1;
    imageHlsMode_ = false;
}

void PlayerEngine::abortPipeline() {
    syncTimer_->stop();
    pipelineRunning_ = false;
    session_.abortIo();

    if (imageHlsMode_) {
        videoFrameQueue_.abort();
        if (imageHlsWorker_) imageHlsWorker_->abort();
        return;
    }

    audioPacketQueue_.abort();
    videoPacketQueue_.abort();
    videoFrameQueue_.abort();
    if (audioOutput_) audioOutput_->abort();
    if (demuxWorker_) demuxWorker_->abort();
    if (videoDecodeWorker_) videoDecodeWorker_->abort();
}

bool PlayerEngine::areThreadsBusy() const {
    if (imageHlsMode_) {
        return imageHlsWorker_ && !imageHlsThread_.isFinished();
    }
    bool busy = false;
    if (demuxWorker_ && !demuxThread_.isFinished()) busy = true;
    if (audioOutput_ && !audioThread_.isFinished()) busy = true;
    if (videoDecodeWorker_ && !videoThread_.isFinished()) busy = true;
    return busy;
}

void PlayerEngine::finalizeStop() {
    if (imageHlsMode_) {
        delete imageHlsWorker_;
        imageHlsWorker_ = nullptr;
        disconnect(&imageHlsThread_, nullptr, nullptr, nullptr);
        videoFrameQueue_.resume();
        flushQueues();
        pendingSeekSec_ = -1.0;
        eofReached_ = false;
        state_ = State::Idle;
        imageHlsMode_ = false;
        return;
    }

    // Clean up workers (threads are guaranteed finished)
    delete demuxWorker_;
    demuxWorker_ = nullptr;
    delete audioOutput_;
    audioOutput_ = nullptr;
    delete videoDecodeWorker_;
    videoDecodeWorker_ = nullptr;

    disconnect(&demuxThread_, nullptr, nullptr, nullptr);
    disconnect(&audioThread_, nullptr, nullptr, nullptr);
    disconnect(&videoThread_, nullptr, nullptr, nullptr);

    audioPacketQueue_.resume();
    videoPacketQueue_.resume();
    videoFrameQueue_.resume();

    audioDecoder_.flush();
    videoDecoder_.flush();
    audioClock_.reset();
    flushQueues();
    pendingSeekSec_ = -1.0;
    eofReached_ = false;
    state_ = State::Idle;
}

Error PlayerEngine::openFile(const QString &path) {
    session_.close();
    auto result = session_.open(path);
    if (!result.ok) {
        Logger::instance().error("PlayerEngine: open failed - " + result.message);
        return result;
    }

    currentPath_ = path;

    // Image HLS mode: skip FFmpeg pipeline entirely
    if (session_.isImageHls()) {
        return openImageHls();
    }

    findStreams();

    if (audioStreamIndex_ >= 0) {
        auto audioResult = audioDecoder_.initialize(session_.formatContext(), audioStreamIndex_);
        if (!audioResult.ok) {
            Logger::instance().error("PlayerEngine: audio decoder init failed - " + audioResult.message);
        }
    }

    if (videoStreamIndex_ >= 0) {
        auto videoResult = videoDecoder_.initialize(session_.formatContext(), videoStreamIndex_);
        if (!videoResult.ok) {
            Logger::instance().error("PlayerEngine: video decoder init failed - " + videoResult.message);
        }
    }

    if (subtitleStreamIndex_ >= 0) {
        auto subResult = subtitleDecoder_.initialize(session_.formatContext(), subtitleStreamIndex_);
        if (!subResult.ok) {
            Logger::instance().warn("PlayerEngine: subtitle decoder init failed - " + subResult.message);
        }
    }

    AVFormatContext *ctx = session_.formatContext();
    if (ctx->duration > 0) {
        durationSec_ = static_cast<double>(ctx->duration) / AV_TIME_BASE;
    }

    auto items = extractor_.extract(path, ctx);
    mediaInfoModel_.replaceAll(items);
    emit mediaInfoReady();

    decodedFrames_ = 0;
    totalFrames_ = 0;
    if (videoStreamIndex_ >= 0) {
        AVStream *vs = ctx->streams[videoStreamIndex_];
        if (vs->nb_frames > 0) {
            totalFrames_ = static_cast<int>(vs->nb_frames);
        } else {
            double fps = av_q2d(vs->r_frame_rate);
            if (fps > 0 && durationSec_ > 0) {
                totalFrames_ = static_cast<int>(fps * durationSec_);
            }
        }
    }

    if (!session_.isNetwork() && videoStreamIndex_ >= 0 && videoBridge_) {
        presentFirstFrame();
    }

    // Start thumbnail worker
    if (!session_.isNetwork() && videoStreamIndex_ >= 0) {
        thumbnailWorker_ = new ThumbnailWorker;
        thumbnailWorker_->setSource(path);
        thumbnailWorker_->moveToThread(&thumbnailThread_);
        connect(&thumbnailThread_, &QThread::started, thumbnailWorker_, &ThumbnailWorker::start);
        connect(thumbnailWorker_, &ThumbnailWorker::finished, &thumbnailThread_, &QThread::quit);
        connect(thumbnailWorker_, &ThumbnailWorker::thumbnailReady, this, &PlayerEngine::thumbnailReady);
        thumbnailThread_.start();
    }

    Logger::instance().info("PlayerEngine: opened " + path
        + ", audio=#" + QString::number(audioStreamIndex_)
        + " video=#" + QString::number(videoStreamIndex_)
        + " duration=" + QString::number(durationSec_, 'f', 2) + "s");

    return Error::success();
}

void PlayerEngine::requestThumbnail(double positionSec) {
    if (thumbnailWorker_) {
        thumbnailWorker_->request(positionSec);
    }
}

void PlayerEngine::play() {
    Logger::instance().info("PlayerEngine::play() called, state="
        + QString::number(static_cast<int>(state_))
        + " pendingSeek=" + QString::number(pendingSeekSec_, 'f', 2)
        + " imageHls=" + QString::number(imageHlsMode_));

    if (!session_.isOpen()) {
        Logger::instance().warn("PlayerEngine: cannot play, no session open");
        return;
    }

    // Image HLS play
    if (imageHlsMode_) {
        if (state_ == State::Paused || state_ == State::Idle) {
            if (eofReached_) {
                flushQueues();
                eofReached_ = false;
            }
            startImageHlsPipeline();
            state_ = State::Playing;
            syncTimer_->start();
            pendingSeekSec_ = -1.0;
            Logger::instance().info("PlayerEngine: image HLS playback started");
            emit stateChanged();
        }
        return;
    }

    if (state_ == State::Paused) {
        if (eofReached_) {
            // EOF 后重播，回到开头
            if (!session_.isNetwork() || session_.isSeekable()) {
                av_seek_frame(session_.formatContext(), -1, 0, AVSEEK_FLAG_BACKWARD);
            }
            audioDecoder_.flush();
            videoDecoder_.flush();
            audioClock_.reset();
            flushQueues();
            eofReached_ = false;
            startPipeline();
            state_ = State::Playing;
            syncTimer_->start();
            pendingSeekSec_ = -1.0;
            Logger::instance().info("PlayerEngine: restarting from beginning after EOF");
            emit stateChanged();
            return;
        }
        if (audioOutput_) {
            // 暂停后恢复
            audioPacketQueue_.resume();
            videoPacketQueue_.resume();
            videoFrameQueue_.resume();
            audioOutput_->resume();
            state_ = State::Playing;
            syncTimer_->start();
            Logger::instance().info("PlayerEngine: resumed");
        } else {
            // seek 后首次播放，管线从关键帧 T' 开始
            audioClock_.reset();
            flushQueues();
            startPipeline();
            state_ = State::Playing;
            syncTimer_->start();
            pendingSeekSec_ = -1.0;
            Logger::instance().info("PlayerEngine: started from seek position");
        }
        emit stateChanged();
        return;
    }

    if (state_ == State::Idle) {
        // EOF 后重播，回到开头
        if (!session_.isNetwork() || session_.isSeekable()) {
            av_seek_frame(session_.formatContext(), -1, 0, AVSEEK_FLAG_BACKWARD);
        }
        audioDecoder_.flush();
        videoDecoder_.flush();
        audioClock_.reset();
        wallClockStarted_ = false;
        flushQueues();
        Logger::instance().info("PlayerEngine: restarting from beginning");
    }

    startPipeline();
    state_ = State::Playing;
    syncTimer_->start();
    pendingSeekSec_ = -1.0;
    Logger::instance().info("PlayerEngine: playback started");
    emit stateChanged();
}

void PlayerEngine::pause() {
    if (state_ != State::Playing) return;

    if (imageHlsMode_) {
        stopImageHlsPipeline();
        state_ = State::Paused;
        Logger::instance().info("PlayerEngine: image HLS paused");
        emit stateChanged();
        return;
    }

    if (audioOutput_) audioOutput_->pause();
    syncTimer_->stop();
    state_ = State::Paused;
    Logger::instance().info("PlayerEngine: paused");
    emit stateChanged();
}

void PlayerEngine::stop() {
    if (state_ == State::Idle) return;

    if (imageHlsMode_) {
        stopImageHlsPipeline();
        flushQueues();
        pendingSeekSec_ = -1.0;
        eofReached_ = false;
        state_ = State::Idle;
        imageHlsMode_ = false;
        Logger::instance().info("PlayerEngine: image HLS stopped");
        emit stateChanged();
        return;
    }

    stopPipeline();
    flushQueues();
    audioClock_.reset();
    pendingSeekSec_ = -1.0;
    eofReached_ = false;
    state_ = State::Idle;
    Logger::instance().info("PlayerEngine: stopped");
    emit stateChanged();
}

void PlayerEngine::seek(qint64 positionMs) {
    if (!session_.isOpen()) return;
    if (session_.isNetwork() && !session_.isSeekable()) {
        Logger::instance().warn("PlayerEngine: seek disabled for live streams");
        return;
    }

    Logger::instance().info("PlayerEngine::seek(" + QString::number(positionMs) + ") called, state="
        + QString::number(static_cast<int>(state_))
        + " imageHls=" + QString::number(imageHlsMode_));
    session_.resumeIo();

    // Image HLS seek
    if (imageHlsMode_) {
        imageHlsSeek(positionMs);
        return;
    }

    bool wasPlaying = (state_ == State::Playing);
    double seekSec = static_cast<double>(positionMs) / 1000.0;

    if (session_.isNetwork()) {
        if (state_ != State::Idle) {
            stopPipeline();
        }
        session_.resumeIo();
        audioDecoder_.flush();
        videoDecoder_.flush();
        audioClock_.reset();
        wallClockStarted_ = false;
        flushQueues();

        AVFormatContext *ctx = session_.formatContext();
        int64_t seekTarget = static_cast<int64_t>(seekSec * AV_TIME_BASE);
        int ret = av_seek_frame(ctx, -1, seekTarget, AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            Logger::instance().warn("PlayerEngine: network seek failed");
            state_ = State::Paused;
            emit stateChanged();
            emit positionUpdated(seekSec);
            return;
        }

        if (audioStreamIndex_ >= 0) {
            audioDecoder_.initialize(ctx, audioStreamIndex_);
        }
        if (videoStreamIndex_ >= 0) {
            videoDecoder_.initialize(ctx, videoStreamIndex_);
        }

        pendingSeekSec_ = seekSec;
        eofReached_ = false;
        if (wasPlaying) {
            startPipeline();
            state_ = State::Playing;
            syncTimer_->start();
            emit stateChanged();
            Logger::instance().info("PlayerEngine: network seek resumed playback");
        } else {
            state_ = State::Paused;
            emit stateChanged();
        }

        emit positionUpdated(seekSec);
        Logger::instance().info("PlayerEngine: network seek completed, pending "
            + QString::number(seekSec, 'f', 2) + "s");
        return;
    }

    // 1. 停止当前管线
    if (state_ != State::Idle) {
        stopPipeline();
    }

    // 2. 清空解码器缓冲，重置时钟
    audioDecoder_.flush();
    videoDecoder_.flush();
    audioClock_.reset();
    flushQueues();

    // 3. 重新打开文件并定位
    session_.close();
    session_.open(currentPath_);
    AVFormatContext *ctx = session_.formatContext();

    if (audioStreamIndex_ >= 0) {
        audioDecoder_.initialize(ctx, audioStreamIndex_);
    }
    if (videoStreamIndex_ >= 0) {
        videoDecoder_.initialize(ctx, videoStreamIndex_);
    }

    int64_t seekTarget = static_cast<int64_t>(positionMs) * 1000;
    av_seek_frame(ctx, -1, seekTarget, AVSEEK_FLAG_BACKWARD);

    // 4. 解码并显示预览帧（目标位置附近的视频帧）
    if (videoStreamIndex_ >= 0 && videoBridge_) {
        AVPacket *pkt = av_packet_alloc();
        QImage lastFrame;
        double lastPts = 0;
        double targetSec = static_cast<double>(positionMs) / 1000.0;
        while (av_read_frame(ctx, pkt) >= 0) {
            if (pkt->stream_index == videoStreamIndex_) {
                QImage frame = videoDecoder_.decode(pkt);
                av_packet_unref(pkt);
                if (!frame.isNull()) {
                    lastFrame = frame;
                    lastPts = frame.text("pts").toDouble();
                    if (lastPts >= targetSec) break;
                }
            } else {
                av_packet_unref(pkt);
            }
        }
        av_packet_free(&pkt);
        if (!lastFrame.isNull()) {
            videoBridge_->present(lastFrame);
            ++decodedFrames_;
        }
        // 回到关键帧位置
        av_seek_frame(ctx, -1, seekTarget, AVSEEK_FLAG_BACKWARD);
        audioDecoder_.flush();
        videoDecoder_.flush();
    }

    // 记录 seek 目标
    pendingSeekSec_ = seekSec;

    // 5. 恢复之前的状态：如果之前在播放则自动恢复，否则暂停等待
    if (wasPlaying) {
        play();
    } else {
        state_ = State::Paused;
        emit stateChanged();
    }

    emit positionUpdated(seekSec);
    Logger::instance().info("PlayerEngine: seek completed, pending "
        + QString::number(seekSec, 'f', 2) + "s");
}

void PlayerEngine::setVolume(float volume) {
    volume_ = volume;
    if (audioOutput_) {
        audioOutput_->setVolume(volume);
    }
    if (pcmAudioPlayer_) {
        pcmAudioPlayer_->setVolume(volume);
    }
}

void PlayerEngine::setMuted(bool muted) {
    Q_UNUSED(muted)
}

void PlayerEngine::setPlaybackRate(float rate) {
    playbackRate_ = rate;
    if (audioOutput_) {
        audioOutput_->setPlaybackRate(rate);
    }
}

bool PlayerEngine::isOpen() const {
    return session_.isOpen();
}

bool PlayerEngine::isNetwork() const {
    return session_.isNetwork();
}

bool PlayerEngine::isSeekable() const {
    return session_.isSeekable();
}

bool PlayerEngine::isLive() const {
    return session_.isLive();
}

bool PlayerEngine::isPlaying() const {
    return state_ == State::Playing;
}

bool PlayerEngine::isPaused() const {
    return state_ == State::Paused;
}

double PlayerEngine::durationSec() const {
    return durationSec_;
}

int PlayerEngine::decodedFrames() const {
    return decodedFrames_;
}

double PlayerEngine::bufferProgress() const {
    return session_.bufferProgress();
}

int PlayerEngine::totalFrames() const {
    return totalFrames_;
}

MediaInfoModel *PlayerEngine::mediaInfoModel() {
    return &mediaInfoModel_;
}

VideoFrameBridge *PlayerEngine::videoBridge() {
    return videoBridge_;
}

void PlayerEngine::setVideoBridge(VideoFrameBridge *bridge) {
    videoBridge_ = bridge;
}

void PlayerEngine::findStreams() {
    AVFormatContext *ctx = session_.formatContext();
    audioStreamIndex_ = -1;
    videoStreamIndex_ = -1;
    subtitleStreamIndex_ = -1;

    for (unsigned i = 0; i < ctx->nb_streams; ++i) {
        AVStream *stream = ctx->streams[i];
        if (!stream || !stream->codecpar) continue;

        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamIndex_ < 0) {
            audioStreamIndex_ = static_cast<int>(i);
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStreamIndex_ < 0) {
            videoStreamIndex_ = static_cast<int>(i);
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE && subtitleStreamIndex_ < 0) {
            subtitleStreamIndex_ = static_cast<int>(i);
        }
    }
}

QList<PlayerEngine::TrackInfo> PlayerEngine::audioTracks() const {
    QList<TrackInfo> tracks;
    if (!session_.isOpen() || imageHlsMode_) return tracks;
    AVFormatContext *ctx = session_.formatContext();
    for (unsigned i = 0; i < ctx->nb_streams; ++i) {
        AVStream *stream = ctx->streams[i];
        if (!stream || !stream->codecpar) continue;
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            TrackInfo info;
            info.index = static_cast<int>(i);
            info.type = "audio";
            AVDictionaryEntry *lang = av_dict_get(stream->metadata, "language", nullptr, 0);
            info.language = lang ? QString::fromUtf8(lang->value) : "unknown";
            AVDictionaryEntry *title = av_dict_get(stream->metadata, "title", nullptr, 0);
            info.title = title ? QString::fromUtf8(title->value) : "";
            tracks.append(info);
        }
    }
    return tracks;
}

QList<PlayerEngine::TrackInfo> PlayerEngine::subtitleTracks() const {
    QList<TrackInfo> tracks;
    if (!session_.isOpen() || imageHlsMode_) return tracks;
    AVFormatContext *ctx = session_.formatContext();
    for (unsigned i = 0; i < ctx->nb_streams; ++i) {
        AVStream *stream = ctx->streams[i];
        if (!stream || !stream->codecpar) continue;
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            TrackInfo info;
            info.index = static_cast<int>(i);
            info.type = "subtitle";
            AVDictionaryEntry *lang = av_dict_get(stream->metadata, "language", nullptr, 0);
            info.language = lang ? QString::fromUtf8(lang->value) : "unknown";
            AVDictionaryEntry *title = av_dict_get(stream->metadata, "title", nullptr, 0);
            info.title = title ? QString::fromUtf8(title->value) : "";
            tracks.append(info);
        }
    }
    return tracks;
}

Error PlayerEngine::switchAudioTrack(int streamIndex) {
    if (!session_.isOpen()) return Error::failure("no session", ErrorCode::PipelineError);
    AVFormatContext *ctx = session_.formatContext();
    if (streamIndex < 0 || streamIndex >= static_cast<int>(ctx->nb_streams)) {
        return Error::failure("invalid stream index", ErrorCode::StreamNotFound);
    }
    AVStream *stream = ctx->streams[streamIndex];
    if (!stream || stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
        return Error::failure("not an audio stream", ErrorCode::StreamNotFound);
    }

    // Remember current position
    double currentPos = audioClock_.positionSec();
    bool wasPlaying = (state_ == State::Playing);

    // Stop pipeline
    if (state_ != State::Idle) {
        stopPipeline();
    }
    flushQueues();
    audioDecoder_.flush();
    videoDecoder_.flush();

    // Switch stream
    audioStreamIndex_ = streamIndex;
    auto result = audioDecoder_.initialize(ctx, audioStreamIndex_);
    if (!result.ok) {
        Logger::instance().error("PlayerEngine: audio track switch failed - " + result.message);
        return result;
    }

    // Seek to current position
    int64_t seekTarget = static_cast<int64_t>(currentPos * AV_TIME_BASE);
    av_seek_frame(ctx, -1, seekTarget, AVSEEK_FLAG_BACKWARD);
    audioClock_.reset();

    Logger::instance().info("PlayerEngine: switched to audio track #" + QString::number(streamIndex));

    // Resume playback if was playing
    if (wasPlaying) {
        startPipeline();
        state_ = State::Playing;
        syncTimer_->start();
    }

    return Error::success();
}

void PlayerEngine::presentFirstFrame() {
    AVFormatContext *ctx = session_.formatContext();
    AVPacket *pkt = av_packet_alloc();

    // Read packets until we get the first video frame
    while (av_read_frame(ctx, pkt) >= 0) {
        if (pkt->stream_index == videoStreamIndex_) {
            QImage frame = videoDecoder_.decode(pkt);
            av_packet_unref(pkt);
            if (!frame.isNull()) {
                if (videoBridge_) {
                    videoBridge_->present(frame);
                }
                ++decodedFrames_;
                Logger::instance().info("PlayerEngine: first frame presented");
                break;
            }
        } else {
            av_packet_unref(pkt);
        }
    }

    av_packet_free(&pkt);

    // Seek back to the beginning for proper playback
    av_seek_frame(ctx, -1, 0, AVSEEK_FLAG_BACKWARD);
    videoDecoder_.flush();
}

void PlayerEngine::startPipeline() {
    session_.resumeIo();
    pipelineRunning_ = true;
    networkBuffering_ = session_.isNetwork();
    audioStartDelayedForBuffer_ = false;
    if (networkBuffering_) {
        networkBufferTimer_.restart();
        Logger::instance().info("PlayerEngine: network buffering started");
    }

    // Demux worker
    demuxWorker_ = new DemuxWorker;
    demuxWorker_->configure(session_.formatContext(),
        &audioPacketQueue_, &videoPacketQueue_, &subtitlePacketQueue_,
        audioStreamIndex_, videoStreamIndex_, subtitleStreamIndex_);
    demuxWorker_->moveToThread(&demuxThread_);
    connect(&demuxThread_, &QThread::started, demuxWorker_, &DemuxWorker::start);
    connect(demuxWorker_, &DemuxWorker::finished, &demuxThread_, &QThread::quit);
    connect(demuxWorker_, &DemuxWorker::error, this, [this](const QString &msg) {
        Logger::instance().error("DemuxWorker error: " + msg);
    });

    // Audio output
    if (audioStreamIndex_ >= 0) {
        audioOutput_ = new AudioOutput;
        audioOutput_->configure(&audioPacketQueue_, &audioDecoder_, &audioClock_);
        if (pendingSeekSec_ >= 0) {
            audioOutput_->setSeekTarget(pendingSeekSec_);
        }
        audioOutput_->setVolume(volume_);
        audioOutput_->setPlaybackRate(playbackRate_);
        connect(audioOutput_, &AudioOutput::finished, this, [this]() {
            if (!pipelineRunning_) return;
            Logger::instance().info("PlayerEngine: playback finished (EOF)");
            pipelineRunning_ = false;
            eofReached_ = true;
            stopPipeline();
            flushQueues();
            state_ = State::Paused;
            emit stateChanged();
        });
        connect(audioOutput_, &AudioOutput::positionUpdated, this, [this](double sec) {
            emit positionUpdated(sec);
        });
        connect(audioOutput_, &AudioOutput::seekTargetReached, this, [this]() {
            emit seekTargetReached();
        });
    }

    // Video decode worker
    if (videoStreamIndex_ >= 0) {
        videoDecodeWorker_ = new VideoDecodeWorker;
        videoDecodeWorker_->configure(&videoPacketQueue_, &videoDecoder_, &videoFrameQueue_);
        if (pendingSeekSec_ >= 0) {
            videoDecodeWorker_->setSeekTarget(pendingSeekSec_);
        }
        videoDecodeWorker_->moveToThread(&videoThread_);
        connect(&videoThread_, &QThread::started, videoDecodeWorker_, &VideoDecodeWorker::start);
        connect(videoDecodeWorker_, &VideoDecodeWorker::finished, &videoThread_, &QThread::quit);
        // If no audio stream, detect EOF from video worker
        if (audioStreamIndex_ < 0) {
            connect(videoDecodeWorker_, &VideoDecodeWorker::finished, this, [this]() {
                if (!pipelineRunning_) return;
                Logger::instance().info("PlayerEngine: playback finished (EOF, no audio)");
                pipelineRunning_ = false;
                stopPipeline();
                flushQueues();
                state_ = State::Idle;
                emit stateChanged();
            });
        }
    }

    // Start threads
    audioStartDelayedForBuffer_ = false;
    if (audioStreamIndex_ >= 0) startAudioOutputThread();
    if (videoStreamIndex_ >= 0) videoThread_.start();
    demuxThread_.start();

    Logger::instance().info("PlayerEngine: pipeline started");
}

void PlayerEngine::startAudioOutputThread() {
    if (!audioOutput_ || audioThread_.isRunning()) return;
    audioOutput_->moveToThread(&audioThread_);
    connect(&audioThread_, &QThread::started, audioOutput_, &AudioOutput::start);
    connect(audioOutput_, &AudioOutput::finished, &audioThread_, &QThread::quit);
    audioThread_.start();
}

void PlayerEngine::stopPipeline() {
    syncTimer_->stop();
    pipelineRunning_ = false;
    session_.abortIo();

    // 1. Abort queues — unblocks workers waiting on pop()
    audioPacketQueue_.abort();
    videoPacketQueue_.abort();
    videoFrameQueue_.abort();

    // 2. Signal workers to stop (sets aborted_ flags)
    if (audioOutput_) audioOutput_->abort();
    if (demuxWorker_) demuxWorker_->abort();
    if (videoDecodeWorker_) videoDecodeWorker_->abort();

    // 3. Wait for threads — abort checks at every stage make them exit within ~10ms
    const int waitMs = session_.isNetwork() ? 3000 : 3000;
    demuxThread_.wait(waitMs);
    audioThread_.wait(waitMs);
    videoThread_.wait(waitMs);

    // 4. Clean up workers
    if (demuxThread_.isFinished()) {
        delete demuxWorker_;
    } else {
        Logger::instance().warn("PlayerEngine: demux thread stuck, leaking");
    }
    demuxWorker_ = nullptr;

    if (audioThread_.isFinished()) {
        delete audioOutput_;
    } else {
        Logger::instance().warn("PlayerEngine: audio thread stuck, leaking");
    }
    audioOutput_ = nullptr;

    if (videoThread_.isFinished()) {
        delete videoDecodeWorker_;
    } else {
        Logger::instance().warn("PlayerEngine: video thread stuck, leaking");
    }
    videoDecodeWorker_ = nullptr;

    // 5. Disconnect and resume
    disconnect(&demuxThread_, nullptr, nullptr, nullptr);
    disconnect(&audioThread_, nullptr, nullptr, nullptr);
    disconnect(&videoThread_, nullptr, nullptr, nullptr);
    audioPacketQueue_.resume();
    videoPacketQueue_.resume();
    videoFrameQueue_.resume();

    Logger::instance().info("PlayerEngine: pipeline stopped");
}

void PlayerEngine::flushQueues() {
    audioPacketQueue_.clear();
    videoPacketQueue_.clear();
    videoFrameQueue_.clear();
}

void PlayerEngine::onSyncTimerTick() {
    if (state_ != State::Playing) return;
    if (networkBuffering_) {
        const size_t queuedFrames = videoFrameQueue_.size();
        const bool enoughFrames = queuedFrames >= 12;
        const bool timeout = networkBufferTimer_.isValid() && networkBufferTimer_.elapsed() >= 800;
        if (!enoughFrames && !timeout) {
            return;
        }
        networkBuffering_ = false;
        Logger::instance().info("PlayerEngine: network buffering completed, frames="
            + QString::number(static_cast<qulonglong>(queuedFrames)));
        emit bufferingReady();
    }

    // Image HLS: track position by wall clock, present frame matching current time
    if (imageHlsMode_) {
        size_t qSize = videoFrameQueue_.size();

        // Start clock on first frame arrival
        if (imageHlsFirstFrame_ && qSize > 0) {
            auto peekFrame = videoFrameQueue_.peek();
            imageHlsStartPts_ = peekFrame.ptsSec;
            imageHlsClock_.start();
            imageHlsFirstFrame_ = false;
        }

        if (imageHlsFirstFrame_) {
            return; // Waiting for first frame
        }

        double clockPos = imageHlsStartPts_ +
            static_cast<double>(imageHlsClock_.elapsed()) / 1000.0 * playbackRate_;

        // If the queue has frames and clock is far ahead (download delay), reset clock
        // to the oldest frame's PTS to avoid skipping all frames
        if (qSize > 0) {
            auto peekFrame = videoFrameQueue_.peek();
            if (peekFrame.ptsSec > 0 && clockPos > peekFrame.ptsSec + 2.0) {
                // Clock is more than 2s ahead of the oldest frame — reset
                imageHlsStartPts_ = peekFrame.ptsSec;
                imageHlsClock_.start();
                clockPos = imageHlsStartPts_;
            }
        }

        // Pop frames that are older than current clock, keep the latest valid one
        std::optional<TimedVideoFrame> bestFrame;
        while (true) {
            auto peekFrame = videoFrameQueue_.peek();
            if (peekFrame.ptsSec > clockPos + 0.5) break; // frame is in the future, stop
            auto optFrame = videoFrameQueue_.tryPop();
            if (!optFrame) break;
            bestFrame = std::move(optFrame);
        }

        // Present the frame if we have one and it's different from what's showing
        if (bestFrame && videoBridge_) {
            videoBridge_->present((*bestFrame).image);
            ++decodedFrames_;
        }

        emit positionUpdated(clockPos);
        return;
    }

    // Process subtitle packets
    processSubtitlePackets();

    // AudioClock is PTS-based (content time). At non-1.0x playback rates,
    // audio frames arrive faster/slower so the clock runs at playbackRate x real-time.
    // Scale frame PTS to clock-time domain for comparison.
    double clockPos = audioClock_.positionSec();
    float rate = playbackRate_;
    if (rate < 0.01f) rate = 1.0f;
    if (audioStreamIndex_ < 0) {
        auto peekFrame = videoFrameQueue_.peek();
        if (!wallClockStarted_ && !peekFrame.image.isNull()) {
            wallClockStartPts_ = peekFrame.ptsSec;
            wallClock_.start();
            wallClockStarted_ = true;
        }
        if (wallClockStarted_) {
            clockPos = wallClockStartPts_ + static_cast<double>(wallClock_.elapsed()) / 1000.0 * rate;
        }
    } else if (qFuzzyIsNull(clockPos) && videoFrameQueue_.size() > 0) {
        return;
    }

    // Present or drop video frames
    while (true) {
        auto optFrame = videoFrameQueue_.tryPop();
        if (!optFrame) break;
        auto &frame = *optFrame;
        // Scale frame PTS to clock-time domain: at 2x, frame at 4s PTS -> 8s clock-time
        double scaledPts = frame.ptsSec * rate;
        auto decision = videoSyncScheduler_.evaluate(scaledPts, clockPos);

        if (decision == VideoSyncScheduler::Decision::Drop) {
            continue;
        } else if (decision == VideoSyncScheduler::Decision::Present) {
            if (videoBridge_) {
                videoBridge_->setSubtitleText(subtitlesEnabled_ ? currentSubtitleText_ : QString());
                videoBridge_->present(frame.image);
            }
            ++decodedFrames_;
        } else {
            // Wait -- put frame back at front (push is not ideal but safe)
            videoFrameQueue_.pushFront(std::move(frame));
            break;
        }
    }

    // For video-only files (no audio), emit position from sync timer
    if (audioStreamIndex_ < 0) {
        emit positionUpdated(clockPos / rate);
    }
}

void PlayerEngine::processSubtitlePackets() {
    if (!subtitleDecoder_.isInitialized()) return;

    while (true) {
        AVPacket *pkt = subtitlePacketQueue_.tryPop();
        if (!pkt) break;

        auto rects = subtitleDecoder_.decode(pkt);
        av_packet_free(&pkt);

        for (const auto &rect : rects) {
            if (rect.type == 2 || rect.type == 3) { // TEXT or ASS
                currentSubtitleText_ = rect.text;
                // Strip ASS formatting tags
                if (rect.type == 3) {
                    // Simple ASS tag stripping: remove {...} tags
                    QString cleaned;
                    bool inTag = false;
                    for (const QChar &c : currentSubtitleText_) {
                        if (c == '{') { inTag = true; continue; }
                        if (c == '}') { inTag = false; continue; }
                        if (!inTag) cleaned += c;
                    }
                    currentSubtitleText_ = cleaned.trimmed();
                }
            }
        }
    }
}

Error PlayerEngine::switchSubtitleTrack(int streamIndex) {
    if (!session_.isOpen()) return Error::failure("no session", ErrorCode::PipelineError);
    AVFormatContext *ctx = session_.formatContext();
    if (streamIndex < 0 || streamIndex >= static_cast<int>(ctx->nb_streams)) {
        return Error::failure("invalid stream index", ErrorCode::StreamNotFound);
    }
    AVStream *stream = ctx->streams[streamIndex];
    if (!stream || stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
        return Error::failure("not a subtitle stream", ErrorCode::StreamNotFound);
    }

    subtitleStreamIndex_ = streamIndex;
    subtitleDecoder_.reset();
    auto result = subtitleDecoder_.initialize(ctx, subtitleStreamIndex_);
    if (!result.ok) {
        Logger::instance().error("PlayerEngine: subtitle track switch failed - " + result.message);
        return result;
    }

    subtitlePacketQueue_.clear();
    currentSubtitleText_.clear();
    Logger::instance().info("PlayerEngine: switched to subtitle track #" + QString::number(streamIndex));
    return Error::success();
}

void PlayerEngine::setSubtitlesEnabled(bool enabled) {
    subtitlesEnabled_ = enabled;
    if (!enabled && videoBridge_) {
        videoBridge_->setSubtitleText(QString());
    }
    Logger::instance().info("PlayerEngine: subtitles " + QString(enabled ? "enabled" : "disabled"));
}

// Image HLS support

Error PlayerEngine::openImageHls() {
    imageHlsMode_ = true;
    durationSec_ = session_.imageHlsDuration();
    decodedFrames_ = 0;
    totalFrames_ = static_cast<int>(session_.imageHlsSegments().size());

    // Build simple media info
    QVector<MediaInfoItem> items;
    items.append(MediaInfoItem{"Format", "Image HLS"});
    items.append(MediaInfoItem{"Segments", QString::number(totalFrames_)});
    items.append(MediaInfoItem{"Duration", QString::number(durationSec_, 'f', 1) + "s"});
    mediaInfoModel_.replaceAll(items);
    emit mediaInfoReady();

    // First frame will be presented when play() starts the pipeline.
    // No synchronous download here to avoid blocking the UI thread.

    Logger::instance().info("PlayerEngine: image HLS opened, " +
        QString::number(totalFrames_) + " segments, " +
        QString::number(durationSec_, 'f', 2) + "s");

    return Error::success();
}

void PlayerEngine::startImageHlsPipeline() {
    pipelineRunning_ = true;

    // Determine start segment from pending seek
    int startSegment = 0;
    if (pendingSeekSec_ >= 0) {
        const auto &segments = session_.imageHlsSegments();
        for (int i = 0; i < static_cast<int>(segments.size()); ++i) {
            double segEnd = segments[i].startPtsSec + segments[i].durationSec;
            if (segEnd > pendingSeekSec_) {
                startSegment = i;
                break;
            }
        }
        imageHlsStartPts_ = pendingSeekSec_;
    } else {
        imageHlsStartPts_ = 0.0;
    }

    // Create audio player for image HLS
    pcmAudioPlayer_ = new PcmAudioPlayer;
    pcmAudioPlayer_->setVolume(volume_);
    pcmAudioPlayer_->moveToThread(&pcmAudioThread_);
    connect(&pcmAudioThread_, &QThread::started, pcmAudioPlayer_, &PcmAudioPlayer::start);
    connect(pcmAudioPlayer_, &PcmAudioPlayer::finished, &pcmAudioThread_, &QThread::quit);
    pcmAudioThread_.start();

    imageHlsWorker_ = new ImageHlsWorker;
    imageHlsWorker_->configure(session_.imageHlsSegments(), &videoFrameQueue_, startSegment, pcmAudioPlayer_);
    imageHlsWorker_->moveToThread(&imageHlsThread_);

    connect(&imageHlsThread_, &QThread::started, imageHlsWorker_, &ImageHlsWorker::start);
    connect(imageHlsWorker_, &ImageHlsWorker::finished, &imageHlsThread_, &QThread::quit);
    connect(imageHlsWorker_, &ImageHlsWorker::finished, this, [this]() {
        if (!pipelineRunning_) return;
        Logger::instance().info("PlayerEngine: image HLS playback finished (EOF)");
        pipelineRunning_ = false;
        eofReached_ = true;
        stopImageHlsPipeline();
        flushQueues();
        state_ = State::Paused;
        emit stateChanged();
    });
    connect(imageHlsWorker_, &ImageHlsWorker::error, this, [this](const QString &msg) {
        Logger::instance().error("ImageHlsWorker error: " + msg);
    });

    imageHlsThread_.start();
    // Clock starts when first frame is presented, not here.
    // This prevents frames from being dropped as "too old" due to download delay.
    imageHlsFirstFrame_ = true;

    Logger::instance().info("PlayerEngine: image HLS pipeline started from segment "
        + QString::number(startSegment));
}

void PlayerEngine::stopImageHlsPipeline() {
    syncTimer_->stop();
    pipelineRunning_ = false;

    videoFrameQueue_.abort();

    // Stop audio player first
    if (pcmAudioPlayer_) {
        pcmAudioPlayer_->abort();
    }
    pcmAudioThread_.wait(3000);
    if (pcmAudioThread_.isFinished()) {
        delete pcmAudioPlayer_;
    } else {
        Logger::instance().warn("PlayerEngine: PCM audio thread stuck, leaking");
    }
    pcmAudioPlayer_ = nullptr;
    disconnect(&pcmAudioThread_, nullptr, nullptr, nullptr);

    if (imageHlsWorker_) {
        imageHlsWorker_->abort();
    }

    imageHlsThread_.wait(3000);

    if (imageHlsThread_.isFinished()) {
        delete imageHlsWorker_;
    } else {
        Logger::instance().warn("PlayerEngine: image HLS thread stuck, leaking");
    }
    imageHlsWorker_ = nullptr;

    disconnect(&imageHlsThread_, nullptr, nullptr, nullptr);
    videoFrameQueue_.resume();

    Logger::instance().info("PlayerEngine: image HLS pipeline stopped");
}

void PlayerEngine::imageHlsSeek(qint64 positionMs) {
    double targetSec = static_cast<double>(positionMs) / 1000.0;
    Logger::instance().info("PlayerEngine::imageHlsSeek(" + QString::number(positionMs) + ")");

    bool wasPlaying = (state_ == State::Playing);

    // Stop current pipeline
    if (state_ != State::Idle) {
        stopImageHlsPipeline();
    }
    flushQueues();

    // Record seek target
    pendingSeekSec_ = targetSec;
    imageHlsStartPts_ = targetSec;
    emit positionUpdated(targetSec);

    if (wasPlaying) {
        play();
    } else {
        // Not playing — just update position, first frame arrives on next play()
        state_ = State::Idle;
        emit stateChanged();
    }

    Logger::instance().info("PlayerEngine: image HLS seek completed to "
        + QString::number(targetSec, 'f', 2) + "s");
}
