#pragma once

#include <QObject>
#include <QTimer>
#include <QThread>
#include <QElapsedTimer>
#include <QVariantList>

#include "infrastructure/Error.h"
#include "core/MediaSession.h"
#include "media/MediaInfoExtractor.h"
#include "media/PacketQueue.h"
#include "media/FrameQueue.h"
#include "media/AudioClock.h"
#include "media/AudioDecoder.h"
#include "media/AudioOutput.h"
#include "media/VideoDecoder.h"
#include "media/VideoSyncScheduler.h"
#include "media/VideoDecodeWorker.h"
#include "media/ThumbnailWorker.h"
#include "media/SubtitleDecoder.h"
#include "render/VideoFrameBridge.h"
#include "models/MediaInfoModel.h"

class DemuxWorker;
class ImageHlsWorker;
class PcmAudioPlayer;

class PlayerEngine final : public QObject {
    Q_OBJECT
public:
    struct TrackInfo {
        int index;
        QString type; // "audio", "subtitle"
        QString language;
        QString title;
    };

    explicit PlayerEngine(QObject *parent = nullptr);
    ~PlayerEngine();

    Error open(const QString &path);
    void close();
    void play();
    void pause();
    void stop();

    // Non-blocking stop/open API
    void abortPipeline();
    bool areThreadsBusy() const;
    void finalizeStop();
    Error openFile(const QString &path);
    void seek(qint64 positionMs);
    void setVolume(float volume);
    void setMuted(bool muted);
    void setPlaybackRate(float rate);
    float volume() const { return volume_; }

    bool isOpen() const;
    bool isNetwork() const;
    bool isSeekable() const;
    bool isLive() const;
    bool isPlaying() const;
    bool isPaused() const;
    double durationSec() const;
    double bufferProgress() const;
    int decodedFrames() const;
    int totalFrames() const;

    // Track enumeration and switching
    QList<TrackInfo> audioTracks() const;
    QList<TrackInfo> subtitleTracks() const;
    int currentAudioTrack() const { return audioStreamIndex_; }
    int currentSubtitleTrack() const { return subtitleStreamIndex_; }
    Error switchAudioTrack(int streamIndex);
    Error switchSubtitleTrack(int streamIndex);
    void setSubtitlesEnabled(bool enabled);
    bool subtitlesEnabled() const { return subtitlesEnabled_; }

    // Thumbnail
    void requestThumbnail(double positionSec);

    MediaInfoModel *mediaInfoModel();
    VideoFrameBridge *videoBridge();
    void setVideoBridge(VideoFrameBridge *bridge);

    bool eofReached() const { return eofReached_; }

signals:
    void stateChanged();
    void positionUpdated(double sec);
    void mediaInfoReady();
    void seekTargetReached();
    void bufferingReady();
    void thumbnailReady(double positionSec, const QImage &image);

private:
    enum class State { Idle, Playing, Paused, Stopped };

    // Session
    MediaSession session_;
    MediaInfoExtractor extractor_;

    // Queues
    PacketQueue audioPacketQueue_;
    PacketQueue videoPacketQueue_;
    PacketQueue subtitlePacketQueue_;
    FrameQueue videoFrameQueue_;

    // Clock & sync
    AudioClock audioClock_;
    VideoSyncScheduler videoSyncScheduler_;

    // Decode pipeline
    AudioDecoder audioDecoder_;
    VideoDecoder videoDecoder_;
    SubtitleDecoder subtitleDecoder_;

    // Workers & threads
    DemuxWorker *demuxWorker_ = nullptr;
    AudioOutput *audioOutput_ = nullptr;
    VideoDecodeWorker *videoDecodeWorker_ = nullptr;
    ThumbnailWorker *thumbnailWorker_ = nullptr;
    QThread demuxThread_;
    QThread audioThread_;
    QThread videoThread_;
    QThread thumbnailThread_;

    // Image HLS pipeline
    ImageHlsWorker *imageHlsWorker_ = nullptr;
    QThread imageHlsThread_;
    PcmAudioPlayer *pcmAudioPlayer_ = nullptr;
    QThread pcmAudioThread_;
    bool imageHlsMode_ = false;
    QElapsedTimer imageHlsClock_;
    double imageHlsStartPts_ = 0.0;
    bool imageHlsFirstFrame_ = true;

    // Presentation
    QTimer *syncTimer_ = nullptr;
    VideoFrameBridge *videoBridge_ = nullptr;
    MediaInfoModel mediaInfoModel_;

    // Wall clock for video-only playback (no audio)
    QElapsedTimer wallClock_;
    double wallClockStartPts_ = 0.0;
    bool wallClockStarted_ = false;
    bool networkBuffering_ = false;
    QElapsedTimer networkBufferTimer_;
    bool audioStartDelayedForBuffer_ = false;

    // State
    State state_ = State::Idle;
    bool eofReached_ = false;
    QString currentPath_;
    int audioStreamIndex_ = -1;
    int videoStreamIndex_ = -1;
    int subtitleStreamIndex_ = -1;
    bool subtitlesEnabled_ = true;
    QString currentSubtitleText_;
    double durationSec_ = 0.0;
    double pendingSeekSec_ = -1.0;
    bool pipelineRunning_ = false;
    int decodedFrames_ = 0;
    int totalFrames_ = 0;
    float volume_ = 1.0f;
    float playbackRate_ = 1.0f;

    void findStreams();
    void presentFirstFrame();
    void startPipeline();
    void startAudioOutputThread();
    void stopPipeline();
    void flushQueues();
    void onSyncTimerTick();
    void processSubtitlePackets();

    // Image HLS
    Error openImageHls();
    void startImageHlsPipeline();
    void stopImageHlsPipeline();
    void imageHlsSeek(qint64 positionMs);
};
