#include "controller/PlayerController.h"

#include <QFileDialog>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>
#include <QTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>
#include <QAudioOutput>
#include <QBuffer>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QImageReader>
#include <QMediaPlayer>
#include <QMimeData>
#include <QProcess>
#include <QStandardPaths>
#include <QVideoSink>
#include <QWindow>

#include <algorithm>
#include <functional>
#include <memory>
#include <numeric>

#include "infrastructure/Logger.h"
#include "media/HlsPlaylistFilter.h"
#include "media/HlsPlaylistProxy.h"
#include "render/VideoFrameBridge.h"

#ifndef MELOBOX_APP_VERSION
#define MELOBOX_APP_VERSION "1.0"
#endif

#ifndef MELOBOX_UPDATE_OWNER
#define MELOBOX_UPDATE_OWNER ""
#endif

#ifndef MELOBOX_UPDATE_REPO
#define MELOBOX_UPDATE_REPO ""
#endif

namespace {
QImage imageFromBytes(const QByteArray &bytes) {
    QBuffer inputBuffer;
    inputBuffer.setData(bytes);
    inputBuffer.open(QIODevice::ReadOnly);
    QImageReader reader(&inputBuffer);
    reader.setAutoTransform(true);
    return reader.read();
}

QString redirectedUrlFromReply(QNetworkReply *reply) {
    if (!reply)
        return {};

    QUrl redirectUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (redirectUrl.isEmpty()) {
        const QVariant locationHeader = reply->header(QNetworkRequest::LocationHeader);
        if (locationHeader.isValid())
            redirectUrl = locationHeader.toUrl();
        if (redirectUrl.isEmpty() && locationHeader.canConvert<QString>())
            redirectUrl = QUrl(locationHeader.toString());
    }

    if (redirectUrl.isEmpty())
        return {};

    const QUrl resolvedUrl = reply->url().resolved(redirectUrl);
    if (!resolvedUrl.isValid() || resolvedUrl.isEmpty())
        return {};

    const QString url = resolvedUrl.toString();
    Logger::instance().info(QStringLiteral("[ImageDebug] redirect detected: %1 -> %2")
        .arg(reply->url().toString(), url));
    return url;
}

qint64 lyricTimestampMs(const QString &minutes, const QString &seconds, const QString &fraction) {
    const qint64 minuteMs = minutes.toLongLong() * 60 * 1000;
    const qint64 secondMs = seconds.toLongLong() * 1000;
    QString normalizedFraction = fraction;
    while (normalizedFraction.length() < 3)
        normalizedFraction.append(QLatin1Char('0'));
    if (normalizedFraction.length() > 3)
        normalizedFraction.truncate(3);
    return minuteMs + secondMs + normalizedFraction.toLongLong();
}

QString updaterOwner() {
    return QStringLiteral(MELOBOX_UPDATE_OWNER).trimmed();
}

QString updaterRepo() {
    return QStringLiteral(MELOBOX_UPDATE_REPO).trimmed();
}

QString normalizeVersion(QString version) {
    version = version.trimmed();
    if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        version.remove(0, 1);
    return version;
}
}

PlayerController::PlayerController(QObject *parent)
    : QObject(parent) {
    Logger::instance().setSink([this](const QString &level, const QString &message) {
        runtimeLogModel_.append(level, message);
    });

    qtPlayer_ = new QMediaPlayer(this);
    qtAudioOutput_ = new QAudioOutput(this);
    qtVideoSink_ = new QVideoSink(this);
    seekWatchdog_ = new QTimer(this);
    seekWatchdog_->setSingleShot(true);
    seekWatchdog_->setInterval(8000);
    playbackLoadGuardTimer_ = new QTimer(this);
    playbackLoadGuardTimer_->setSingleShot(true);
    playbackLoadGuardTimer_->setInterval(6000);
    cursorResetTimer_ = new QTimer(this);
    cursorResetTimer_->setSingleShot(true);
    cursorResetTimer_->setInterval(800);
    firstFrameTimer_ = new QTimer(this);
    firstFrameTimer_->setSingleShot(true);
    firstFrameTimer_->setInterval(10000);
    hlsPlaylistProxy_ = new HlsPlaylistProxy(this);
    qtPlayer_->setAudioOutput(qtAudioOutput_);
    qtPlayer_->setVideoSink(qtVideoSink_);
    qtAudioOutput_->setVolume(volume_);

    connect(qtVideoSink_, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (!qtNetworkMode_ || !engine_.videoBridge() || !frame.isValid()) return;
        QImage image = frame.toImage();
        if (!image.isNull()) {
            engine_.videoBridge()->present(image);
        }
    });
    connect(qtPlayer_, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
        if (!qtNetworkMode_) return;
        positionMs_ = position;
        updateCurrentMusicLyricIndex();
        emit timelineChanged();
    });
    connect(qtPlayer_, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        if (!qtNetworkMode_) return;
        durationMs_ = duration;
        emit timelineChanged();
    });
    connect(qtPlayer_, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (!qtNetworkMode_) return;
        isPlaying_ = state == QMediaPlayer::PlayingState;
        isPaused_ = state == QMediaPlayer::PausedState;
        emit playbackStateChanged();
    });
    connect(qtPlayer_, &QMediaPlayer::errorOccurred, this, [](QMediaPlayer::Error, const QString &errorString) {
        Logger::instance().error("QtMediaPlayer: " + errorString);
    });
    connect(seekWatchdog_, &QTimer::timeout, this, [this]() {
        if (pendingSeekMs_ >= 0) {
            Logger::instance().warn("Seek watchdog timeout, resuming playback");
            finishSeek();
        }
    });
    connect(playbackLoadGuardTimer_, &QTimer::timeout, this, [this]() {
        if (mpvMode_ && loading_) {
            Logger::instance().warn("Playback load guard timeout, clearing loading state");
            setLoading(false);
        }
    });
    connect(cursorResetTimer_, &QTimer::timeout, this, []() {
        if (QGuiApplication::overrideCursor())
            QGuiApplication::restoreOverrideCursor();
    });
    connect(firstFrameTimer_, &QTimer::timeout, this, [this]() {
        if (mpvMode_ && positionMs_ <= 0)
            suggestSourceSwitch(QStringLiteral("首帧加载超过 10 秒"));
    });
    connect(&mpvBackend_, &MpvBackend::positionChanged, this, [this](qint64 position) {
        if (!mpvMode_) return;
        if (pendingSeekMs_ >= 0) {
            if (position < pendingSeekMs_ - 1000) {
                return;
            }
            if (qAbs(position - pendingSeekMs_) <= 1500 || position >= pendingSeekMs_ - 500) {
                finishSeek();
            }
        }
        if (loading_ && isPlaying_ && position > 0) {
            setLoading(false);
        }
        positionMs_ = position;
        applyPendingSuspendedVideoState();
        if (position > 0)
            markPlaybackProgress();
        emit timelineChanged();
    });
    connect(&mpvBackend_, &MpvBackend::durationChanged, this, [this](qint64 duration) {
        if (!mpvMode_) return;
        durationMs_ = duration;
        emit timelineChanged();
        applyPendingSuspendedVideoState();
    });
    connect(&mpvBackend_, &MpvBackend::playingChanged, this, [this](bool playing) {
        if (!mpvMode_) return;
        isPlaying_ = playing;
        isPaused_ = !playing && userPausedPlayback_;
        if (playing) {
            userPausedPlayback_ = false;
            updateMpvVideoWindowVisibility();
        }
        emit playbackStateChanged();
    });
    connect(&mpvBackend_, &MpvBackend::bufferingChanged, this, [this](bool buffering) {
        if (!mpvMode_) return;
        mpvBuffering_ = buffering;
        if (!buffering || !isPlaying_) {
            setLoading(buffering);
        }
    });
    connect(&mpvBackend_, &MpvBackend::cacheProgressChanged, this, [this](double progress) {
        if (!mpvMode_) return;
        mpvBufferProgress_ = progress;
        emit timelineChanged();
    });
    connect(&mpvBackend_, &MpvBackend::playbackEnded, this, [this]() {
        if (!mpvMode_) return;
        Logger::instance().info(QStringLiteral("[ShortVideoDebug][C++] mpv playbackEnded currentFile=%1 currentShortVideoUrl=%2 position=%3 duration=%4 loading=%5 isPlaying=%6")
            .arg(currentFile_, currentShortVideoUrl_)
            .arg(positionMs_)
            .arg(durationMs_)
            .arg(loading_)
            .arg(isPlaying_));
        setLoading(false);
        mpvBuffering_ = false;
        if (firstFrameTimer_) firstFrameTimer_->stop();
        userPausedPlayback_ = false;
        mpvPlaybackFinished_ = true;
        isPlaying_ = false;
        isPaused_ = false;
        if (mpvVideoWindow_) {
            mpvVideoWindow_->hide();
        }
        emit playbackStateChanged();
        emit playbackEnded();
    });
   connect(&mpvBackend_, &MpvBackend::errorOccurred, this, [this](const QString &message) {
       Logger::instance().error("mpv: " + message);
        const bool shortVideoFailed = !currentShortVideoUrl_.isEmpty()
            && currentFile_ == currentShortVideoUrl_;
       if (mpvMode_) {
            setLoading(false);
            mpvBuffering_ = false;
            if (firstFrameTimer_) firstFrameTimer_->stop();
            mpvPlaybackFinished_ = true;
            isPlaying_ = false;
            isPaused_ = false;
            updateMpvVideoWindowVisibility();
            emit playbackStateChanged();
       }
        suggestSourceSwitch(QStringLiteral("当前线路播放失败：%1").arg(message));
        if (shortVideoFailed)
            emit playbackFailed(message);
   });
    connect(&apiSiteModel_, &ApiSiteModel::currentIndexChanged, this, [this]() {
        Logger::instance().info("API site changed, loading default VOD list: " + apiSiteModel_.currentBaseUrl());
        videoSearchModel_.clear();
        loadVideoList(1);
    });

    wireEngineSignals();
    loadSettings();
    QTimer::singleShot(0, this, [this]() {
        loadVideoList(1);
    });
    Logger::instance().info("PlayerController initialized");
}

bool PlayerController::isPlaying() const { return isPlaying_; }
bool PlayerController::isPaused() const { return isPaused_; }
bool PlayerController::seeking() const { return seeking_; }
qint64 PlayerController::durationMs() const { return durationMs_; }
qint64 PlayerController::positionMs() const { return positionMs_; }
float PlayerController::volume() const { return volume_; }
bool PlayerController::muted() const { return muted_; }
QString PlayerController::currentFile() const { return currentFile_; }
int PlayerController::decodedFrames() const { return engine_.decodedFrames(); }
int PlayerController::totalFrames() const { return engine_.totalFrames(); }
int PlayerController::currentIndex() const { return currentIndex_; }
float PlayerController::playbackRate() const { return playbackRate_; }
PlayerController::RepeatMode PlayerController::repeatMode() const { return repeatMode_; }
bool PlayerController::loading() const { return loading_; }
bool PlayerController::isNetwork() const { return mpvMode_ || engine_.isNetwork(); }
bool PlayerController::isSeekable() const { return mpvMode_ || engine_.isSeekable(); }
bool PlayerController::isLive() const { return !mpvMode_ && engine_.isLive(); }
double PlayerController::bufferProgress() const {
    if (mpvMode_) return mpvBufferProgress_;
    if (qtNetworkMode_ && qtPlayer_) {
        return qtPlayer_->bufferProgress();
    }
    return engine_.bufferProgress();
}
QVariantList PlayerController::audioTracksQml() const {
    QVariantList list;
    for (const auto &track : engine_.audioTracks()) {
        QVariantMap map;
        map["index"] = track.index;
        map["language"] = track.language;
        map["title"] = track.title;
        list.append(map);
    }
    return list;
}
int PlayerController::currentAudioTrack() const { return engine_.currentAudioTrack(); }
QVariantList PlayerController::subtitleTracksQml() const {
    QVariantList list;
    for (const auto &track : engine_.subtitleTracks()) {
        QVariantMap map;
        map["index"] = track.index;
        map["language"] = track.language;
        map["title"] = track.title;
        list.append(map);
    }
    return list;
}
int PlayerController::currentSubtitleTrack() const { return engine_.currentSubtitleTrack(); }
bool PlayerController::subtitlesEnabled() const { return engine_.subtitlesEnabled(); }
QImage PlayerController::thumbnailPreview() const { return thumbnailPreview_; }
double PlayerController::thumbnailPosition() const { return thumbnailPosition_; }
bool PlayerController::thumbnailReady() const { return thumbnailReady_; }
MediaInfoModel *PlayerController::mediaInfoModel() { return &mediaInfoModel_; }
PlaylistModel *PlayerController::playlistModel() { return &playlistModel_; }
PlaylistModel *PlayerController::historyModel() { return &historyModel_; }
RuntimeLogModel *PlayerController::runtimeLogModel() { return &runtimeLogModel_; }
VideoSearchModel *PlayerController::sourceSearchModel() { return &sourceSearchModel_; }
QString PlayerController::currentVodName() const { return currentVodName_; }
bool PlayerController::sourceSwitchSuggested() const { return sourceSwitchSuggested_; }
QString PlayerController::sourceSwitchReason() const { return sourceSwitchReason_; }
QString PlayerController::sourceSwitchCandidate() const { return sourceSwitchCandidate_; }
bool PlayerController::imageLoading() const { return imageLoading_; }
QString PlayerController::currentImageUrl() const { return currentImageUrl_; }
QString PlayerController::currentImageDisplayUrl() const { return currentImageDisplayUrl_; }
QString PlayerController::imageMessage() const { return imageMessage_; }
int PlayerController::imageDiskCacheCount() const { return imageCache_.count(); }
qint64 PlayerController::imageDiskCacheBytes() const { return imageCache_.sizeBytes(); }
bool PlayerController::memeLoading() const { return memeLoading_; }
QVariantList PlayerController::memeImages() const { return memeImages_; }
QString PlayerController::memeMessage() const { return memeMessage_; }
QString PlayerController::currentShortVideoUrl() const { return currentShortVideoUrl_; }
QString PlayerController::shortVideoMessage() const { return shortVideoMessage_; }
QString PlayerController::currentVoiceUrl() const { return currentVoiceUrl_; }
QString PlayerController::voiceMessage() const { return voiceMessage_; }
bool PlayerController::musicLoading() const { return musicLoading_; }
QVariantList PlayerController::musicResults() const { return musicResults_; }
QString PlayerController::musicMessage() const { return musicMessage_; }
QString PlayerController::currentMusicUrl() const { return currentMusicUrl_; }
QString PlayerController::currentMusicTitle() const { return currentMusicTitle_; }
QString PlayerController::currentMusicArtist() const { return currentMusicArtist_; }
QString PlayerController::currentMusicPic() const { return currentMusicPic_; }
QString PlayerController::currentMusicLrc() const { return currentMusicLrc_; }
QVariantList PlayerController::currentMusicLyricLines() const { return currentMusicLyricLines_; }
int PlayerController::currentMusicLyricIndex() const { return currentMusicLyricIndex_; }
int PlayerController::musicLyricOffsetMs() const { return musicLyricOffsetMs_; }
bool PlayerController::musicShowTranslation() const { return musicShowTranslation_; }
bool PlayerController::musicShowRomanization() const { return musicShowRomanization_; }
QString PlayerController::currentMusicLyricSource() const { return currentMusicLyricSource_; }
bool PlayerController::hotNewsLoading() const { return hotNewsLoading_; }
QVariantList PlayerController::hotNewsItems() const { return hotNewsItems_; }
QString PlayerController::hotNewsMessage() const { return hotNewsMessage_; }
QString PlayerController::appVersion() const { return QStringLiteral(MELOBOX_APP_VERSION); }
bool PlayerController::updateChecking() const { return updateChecking_; }
bool PlayerController::updateDownloading() const { return updateDownloading_; }
bool PlayerController::updateAvailable() const { return updateAvailable_; }
QString PlayerController::updateVersion() const { return updateVersion_; }
QString PlayerController::updateMessage() const { return updateMessage_; }
double PlayerController::updateDownloadProgress() const { return updateDownloadProgress_; }

void PlayerController::openFile() {
    const QString path = QFileDialog::getOpenFileName(
        nullptr, "Open Media", QString(),
        "Video Files (*.mp4 *.mkv *.mov *.avi *.webm *.flv *.ts *.m3u8 *.m3u);;All Files (*.*)");

    if (path.isEmpty()) {
        Logger::instance().info("Open file cancelled");
        return;
    }

    openFileAtPath(path);
}

void PlayerController::openFileAtPath(const QString &path, bool autoPlay) {
    if (opening_) return;
    opening_ = true;
    setLoading(true);

    // If same file is already open, just seek to beginning
    if (engine_.isOpen() && currentFile_ == path) {
        playlistModel_.clear();
        playlistModel_.addFile(path);
        currentIndex_ = 0;
        recordPlaybackHistory(path, playlistModel_.titleAt(currentIndex_));
        saveSettings();
        emit currentIndexChanged();
        opening_ = false;
        setLoading(false);
        return;
    }

    // Reset timeline immediately
    positionMs_ = 0;
    durationMs_ = 0;
    emit timelineChanged();

    // Phase 1: abort old pipeline (non-blocking)
    engine_.abortPipeline();

    // Phase 2: poll until old threads exit, then open new file
    auto *timer = new QTimer(this);
    timer->setInterval(16); // ~60fps polling
    connect(timer, &QTimer::timeout, this, [this, timer, path, autoPlay]() {
        if (engine_.areThreadsBusy()) {
            return; // still waiting for old threads
        }
        timer->stop();
        timer->deleteLater();

        // Old threads are done, now open new file
        engine_.finalizeStop();
        auto result = engine_.openFile(path);
        if (!result.ok) {
            Logger::instance().error("Failed to open: " + result.message);
            opening_ = false;
            setLoading(false);
            return;
        }

        currentFile_ = path;
        durationMs_ = static_cast<qint64>(engine_.durationSec() * 1000);
        mediaInfoModel_.replaceAll(engine_.mediaInfoModel()->items());
        playlistModel_.clear();
        playlistModel_.addFile(path);
        currentIndex_ = 0;
        recordPlaybackHistory(path, playlistModel_.titleAt(currentIndex_));
        saveSettings();
        emit currentFileChanged();
        emit currentIndexChanged();
        emit timelineChanged();
        Logger::instance().info("File opened: " + path);
        opening_ = false;
        setLoading(false);

        if (autoPlay) {
            play();
        }
    });
    timer->start();
}

void PlayerController::openUrl(const QString &url) {
    if (opening_) return;
    QString trimmed = url.trimmed();
    if (trimmed.isEmpty()) return;

    Logger::instance().info("Opening URL: " + trimmed);
    playlistModel_.clear();
    currentIndex_ = 0;
    emit currentIndexChanged();
    playVideoUrl(trimmed);
}

void PlayerController::playFromPlaylist(int index) {
    const QString path = playlistModel_.filePathAt(index);
    if (path.isEmpty()) return;

    if (currentIndex_ != index) {
        currentIndex_ = index;
        emit currentIndexChanged();
    }
    if (!playbackSources_.isEmpty())
        currentPlaybackEpisode_ = index;

    // 动漫共和国的集数地址需要异步解析，列表模型先保存占位地址；
    // 由 QML 接到信号后调用动漫模型解析真实播放地址。
    if (path.startsWith(QStringLiteral("dmghg://"))) {
        emit playlistEpisodeRequested(index, playlistModel_.titleAt(index));
        return;
    }

    if (currentFile_ == path) {
        Logger::instance().info("Same file already playing, ignoring playlist click");
        recordPlaybackHistory(path, playlistModel_.titleAt(index));
        return;
    }

    const QString lower = path.toLower();
    if (lower.startsWith(QStringLiteral("http://")) || lower.startsWith(QStringLiteral("https://"))) {
        playVodUrl(path);
    } else {
        openFileAtPath(path, true);
    }
}

void PlayerController::playFromHistory(int index) {
    const QString path = historyModel_.filePathAt(index);
    if (path.isEmpty()) return;

    const QString title = historyModel_.titleAt(index);
    playlistModel_.clear();
    playlistModel_.addItem(path, title);
    currentIndex_ = 0;
    emit currentIndexChanged();

    const QString lower = path.toLower();
    if (lower.startsWith(QStringLiteral("http://")) || lower.startsWith(QStringLiteral("https://"))) {
        playVodUrl(path);
    } else {
        openFileAtPath(path, true);
    }
}

void PlayerController::clearPlaybackHistory() {
    historyModel_.clear();
    saveSettings();
}

void PlayerController::setPlaylistEpisodes(const QVariantList &episodes, int currentIndex) {
    playlistModel_.setItems(episodes);
    const int boundedIndex = qBound(0, currentIndex, qMax(0, playlistModel_.count() - 1));
    if (currentIndex_ != boundedIndex) {
        currentIndex_ = boundedIndex;
        emit currentIndexChanged();
    }
}

void PlayerController::setCurrentVodName(const QString &name) {
    const QString trimmed = name.trimmed();
    if (currentVodName_ == trimmed) return;
    currentVodName_ = trimmed;
    sourceSearchModel_.clear();
    emit currentVodNameChanged();
}

void PlayerController::play() {
    userPausedPlayback_ = false;
    if (mpvMode_) {
        mpvPlaybackFinished_ = false;
        mpvBackend_.play();
        isPlaying_ = true;
        isPaused_ = false;
        updateMpvVideoWindowVisibility();
        emit playbackStateChanged();
        return;
    }
    if (qtNetworkMode_) {
        qtPlayer_->play();
        isPlaying_ = true;
        isPaused_ = false;
        emit playbackStateChanged();
        return;
    }
    engine_.play();
    isPlaying_ = true;
    isPaused_ = false;
    Logger::instance().info("Playback entered play state");
    emit playbackStateChanged();
}

void PlayerController::pause() {
    userPausedPlayback_ = true;
    if (mpvMode_) {
        mpvBackend_.pause();
        isPlaying_ = false;
        isPaused_ = true;
        updateMpvVideoWindowVisibility();
        emit playbackStateChanged();
        return;
    }
    if (qtNetworkMode_) {
        qtPlayer_->pause();
        isPlaying_ = false;
        isPaused_ = true;
        emit playbackStateChanged();
        return;
    }
    engine_.pause();
    isPaused_ = true;
    Logger::instance().warn("Playback entered pause state");
    emit playbackStateChanged();
}

void PlayerController::stop() {
    cancelHlsFilterRequest();
    removeSanitizedHlsPlaylist();
    userPausedPlayback_ = false;
    if (hlsPlaylistProxy_)
        hlsPlaylistProxy_->stop();
    mpvBuffering_ = false;
    if (firstFrameTimer_) firstFrameTimer_->stop();
    if (mpvMode_) {
        mpvBackend_.stop();
        mpvMode_ = false;
        updateMpvVideoWindowVisibility();
    }
    if (qtNetworkMode_) {
        qtPlayer_->stop();
        qtPlayer_->setSource({});
        qtNetworkMode_ = false;
    }
    engine_.close();
    isPlaying_ = false;
    isPaused_ = false;
    positionMs_ = 0;
    durationMs_ = 0;
    updateCurrentMusicLyricIndex();
    currentFile_.clear();
    Logger::instance().info("Playback stopped and session closed");
    emit playbackStateChanged();
    emit timelineChanged();
    emit currentFileChanged();
}

void PlayerController::seek(qint64 positionMs) {
    if (mpvMode_) {
        seeking_ = true;
        pendingSeekMs_ = positionMs;
        positionMs_ = positionMs;
        updateCurrentMusicLyricIndex();
        setLoading(true);
        emit seekingChanged();
        emit timelineChanged();
        mpvBackend_.seek(positionMs);
        mpvBackend_.play();
        seekWatchdog_->start();
        Logger::instance().info("mpv seek requested to " + QString::number(positionMs) + " ms");
        return;
    }
    if (qtNetworkMode_) {
        qtPlayer_->setPosition(positionMs);
        positionMs_ = positionMs;
        updateCurrentMusicLyricIndex();
        emit timelineChanged();
        return;
    }
    seeking_ = true;
    pendingSeekMs_ = positionMs;
    positionMs_ = positionMs;
    updateCurrentMusicLyricIndex();
    setLoading(true);
    emit timelineChanged();
    emit seekingChanged();
    engine_.seek(positionMs);
    seekWatchdog_->start();
    Logger::instance().info("Seek requested to " + QString::number(positionMs) + " ms");
}

void PlayerController::setVolume(float volume) {
    const float boundedVolume = qBound(0.0f, volume, 1.0f);
    if (qFuzzyCompare(volume_, boundedVolume)) return;
    volume_ = boundedVolume;
    engine_.setVolume(volume_);
    if (mpvMode_) {
        mpvBackend_.setVolume(volume_);
    }
    if (qtAudioOutput_) {
        qtAudioOutput_->setVolume(volume_);
    }
    saveSettings();
    Logger::instance().info("Volume changed to " + QString::number(volume_));
    emit volumeChanged();
}

void PlayerController::setMuted(bool muted) {
    if (muted_ == muted) return;
    muted_ = muted;
    engine_.setMuted(muted_);
    Logger::instance().info(muted_ ? "Muted" : "Unmuted");
    emit mutedChanged();
}

void PlayerController::setPlaybackRate(float rate) {
    if (qFuzzyCompare(playbackRate_, rate)) return;
    playbackRate_ = rate;
    engine_.setPlaybackRate(rate);
    if (mpvMode_) {
        mpvBackend_.setSpeed(rate);
    }
    Logger::instance().info("Playback rate changed to " + QString::number(rate, 'f', 2));
    emit playbackRateChanged();
}

void PlayerController::setRepeatMode(RepeatMode mode) {
    if (repeatMode_ == mode) return;
    repeatMode_ = mode;
    Logger::instance().info("Repeat mode changed to " + QString::number(static_cast<int>(mode)));
    emit repeatModeChanged();
}

void PlayerController::switchAudioTrack(int streamIndex) {
    auto result = engine_.switchAudioTrack(streamIndex);
    if (!result.ok) {
        Logger::instance().error("Failed to switch audio track: " + result.message);
        return;
    }
    emit currentFileChanged(); // triggers audioTracks property update
    Logger::instance().info("Switched to audio track #" + QString::number(streamIndex));
}

void PlayerController::switchSubtitleTrack(int streamIndex) {
    auto result = engine_.switchSubtitleTrack(streamIndex);
    if (!result.ok) {
        Logger::instance().error("Failed to switch subtitle track: " + result.message);
        return;
    }
    emit currentFileChanged();
    Logger::instance().info("Switched to subtitle track #" + QString::number(streamIndex));
}

void PlayerController::setSubtitlesEnabled(bool enabled) {
    engine_.setSubtitlesEnabled(enabled);
    emit subtitlesEnabledChanged();
}

void PlayerController::cycleAspectRatio() {
    auto *bridge = engine_.videoBridge();
    if (!bridge) return;
    int current = static_cast<int>(bridge->aspectRatio());
    int next = (current + 1) % 4;
    bridge->setAspectRatio(static_cast<VideoFrameBridge::AspectRatio>(next));
    QStringList names = {"Keep", "Fill", "4:3", "16:9"};
    Logger::instance().info("Aspect ratio: " + names[next]);
}

void PlayerController::requestThumbnail(qint64 positionMs) {
    double sec = static_cast<double>(positionMs) / 1000.0;
    if (thumbnailReady_) {
        thumbnailReady_ = false;
        emit thumbnailPreviewChanged();
    }
    engine_.requestThumbnail(sec);
}

void PlayerController::setLoading(bool loading) {
    if (loading_ == loading) {
        if (loading_ && mpvMode_ && playbackLoadGuardTimer_ && !playbackLoadGuardTimer_->isActive())
            playbackLoadGuardTimer_->start();
        if (mpvMode_)
            updateMpvVideoWindowVisibility();
        return;
    }
    loading_ = loading;
    if (playbackLoadGuardTimer_) {
        if (loading_ && mpvMode_) {
            playbackLoadGuardTimer_->start();
        } else if (!loading_) {
            playbackLoadGuardTimer_->stop();
        }
    }
    emit loadingChanged();
    if (mpvMode_)
        updateMpvVideoWindowVisibility();
}

void PlayerController::playNext() {
    int count = playlistModel_.count();
    if (count == 0) return;
    int next = currentIndex_ + 1;
    if (next >= count) next = 0;
    playFromPlaylist(next);
}

void PlayerController::playPrevious() {
    int count = playlistModel_.count();
    if (count == 0) return;
    int prev = currentIndex_ - 1;
    if (prev < 0) prev = count - 1;
    playFromPlaylist(prev);
}

void PlayerController::setVideoBridge(QObject *bridge) {
    auto *vf = qobject_cast<VideoFrameBridge *>(bridge);
    if (vf) {
        engine_.setVideoBridge(vf);
        Logger::instance().info("VideoFrameBridge connected to engine");
    }
}

void PlayerController::setMpvWindowId(qint64 windowId) {
    mpvParentWindow_ = QWindow::fromWinId(static_cast<WId>(windowId));
    if (mpvParentWindow_) {
        ensureMpvVideoWindow();
    }
    Logger::instance().info("mpv parent window id set: " + QString::number(windowId));
}

void PlayerController::ensureMpvVideoWindow() {
    if (mpvVideoWindow_ || !mpvParentWindow_) return;

    mpvVideoWindow_ = new QWindow(mpvParentWindow_);
    mpvVideoWindow_->setFlags(Qt::FramelessWindowHint);
    mpvVideoWindow_->resize(1, 1);
    mpvVideoWindow_->hide();

    mpvBackend_.setWindowId(static_cast<qint64>(mpvVideoWindow_->winId()));
    Logger::instance().info("mpv video child window id set: "
        + QString::number(static_cast<qint64>(mpvVideoWindow_->winId())));
}

void PlayerController::setMpvVideoGeometry(qreal x, qreal y, qreal width, qreal height) {
    ensureMpvVideoWindow();
    if (!mpvVideoWindow_) return;

    const int boundedWidth = qMax(1, qRound(width));
    const int boundedHeight = qMax(1, qRound(height));
    mpvVideoWindow_->setGeometry(qRound(x), qRound(y), boundedWidth, boundedHeight);
    updateMpvVideoWindowVisibility();
}

void PlayerController::setMpvSurfaceVisible(bool visible) {
    if (mpvSurfaceVisible_ == visible) return;
    mpvSurfaceVisible_ = visible;
    updateMpvVideoWindowVisibility();
}

void PlayerController::refreshMpvVideoWindow() {
    ensureMpvVideoWindow();
    if (!mpvVideoWindow_) return;
    updateMpvVideoWindowVisibility();
    if (mpvSurfaceVisible_ && mpvMode_ && !loading_ && !mpvPlaybackFinished_ && !currentFile_.isEmpty()) {
        mpvVideoWindow_->show();
        mpvVideoWindow_->raise();
    }
}

void PlayerController::resetMouseCursor() {
    while (QGuiApplication::overrideCursor()) {
        QGuiApplication::restoreOverrideCursor();
    }

    // The embedded mpv window is a native child window and can retain the
    // previous Windows resize cursor when the app is activated again.
    if (mpvVideoWindow_) {
        mpvVideoWindow_->setCursor(QCursor(Qt::ArrowCursor));
    }
    if (cursorResetTimer_)
        cursorResetTimer_->start();
}

void PlayerController::setPlaybackSources(const QVariantList &sources, int currentSource,
                                          int currentEpisode) {
    playbackSources_ = sources;
    currentPlaybackSource_ = sources.isEmpty()
        ? -1
        : qBound(0, currentSource, sources.size() - 1);
    currentPlaybackEpisode_ = qMax(0, currentEpisode);
    sourceSuggestionCooldowns_.clear();
    clearSourceSwitchSuggestion();
}

int PlayerController::bestAlternativeSource() const {
    int bestIndex = -1;
    int bestScore = -1;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int index = 0; index < playbackSources_.size(); ++index) {
        if (index == currentPlaybackSource_) continue;
        const QVariantMap source = playbackSources_.at(index).toMap();
        const QVariantList episodes = source.value(QStringLiteral("episodes")).toList();
        if (currentPlaybackEpisode_ < 0 || currentPlaybackEpisode_ >= episodes.size()) continue;
        const QString url = episodes.at(currentPlaybackEpisode_).toMap()
            .value(QStringLiteral("url")).toString().trimmed();
        if (url.isEmpty()) continue;
        if (sourceSuggestionCooldowns_.value(index, 0) > now) continue;

        const QString lower = url.toLower();
        int score = 80;
        if (lower.contains(QStringLiteral(".m3u8"))) score = 400;
        else if (lower.startsWith(QStringLiteral("https://")) && lower.contains(QStringLiteral(".mp4"))) score = 320;
        else if (lower.startsWith(QStringLiteral("http://")) && lower.contains(QStringLiteral(".mp4"))) score = 260;
        else if (lower.startsWith(QStringLiteral("https://"))) score = 220;
        else if (lower.startsWith(QStringLiteral("http://"))) score = 180;
        else if (lower.startsWith(QStringLiteral("rtsp://")) || lower.startsWith(QStringLiteral("rtmp://"))) score = 120;
        if (score > bestScore) {
            bestScore = score;
            bestIndex = index;
        }
    }
    return bestIndex;
}

void PlayerController::suggestSourceSwitch(const QString &reason) {
    if (sourceSwitchSuggested_ || playbackSources_.size() < 2 || currentShortVideoUrl_ == currentFile_)
        return;
    const int candidate = bestAlternativeSource();
    if (candidate < 0) return;
    pendingPlaybackSource_ = candidate;
    sourceSwitchReason_ = reason;
    const QVariantMap source = playbackSources_.at(candidate).toMap();
    sourceSwitchCandidate_ = source.value(QStringLiteral("name")).toString();
    if (sourceSwitchCandidate_.isEmpty())
        sourceSwitchCandidate_ = QStringLiteral("线路 %1").arg(candidate + 1);
    sourceSwitchSuggested_ = true;
    sourceSuggestionCooldowns_[currentPlaybackSource_] = QDateTime::currentMSecsSinceEpoch() + 10 * 60 * 1000;
    emit sourceSwitchSuggestionChanged();
}

void PlayerController::clearSourceSwitchSuggestion() {
    if (!sourceSwitchSuggested_ && pendingPlaybackSource_ < 0
            && sourceSwitchReason_.isEmpty() && sourceSwitchCandidate_.isEmpty()) return;
    sourceSwitchSuggested_ = false;
    pendingPlaybackSource_ = -1;
    sourceSwitchReason_.clear();
    sourceSwitchCandidate_.clear();
    emit sourceSwitchSuggestionChanged();
}

void PlayerController::acceptSourceSwitch() {
    const int sourceIndex = pendingPlaybackSource_;
    if (sourceIndex < 0 || sourceIndex >= playbackSources_.size()) {
        clearSourceSwitchSuggestion();
        return;
    }
    const QVariantMap source = playbackSources_.at(sourceIndex).toMap();
    const QVariantList episodes = source.value(QStringLiteral("episodes")).toList();
    if (currentPlaybackEpisode_ < 0 || currentPlaybackEpisode_ >= episodes.size()) {
        clearSourceSwitchSuggestion();
        return;
    }

    pendingSourceSeekMs_ = positionMs_;
    pendingSourcePaused_ = isPaused_ || userPausedPlayback_;
    currentPlaybackSource_ = sourceIndex;
    playlistModel_.setItems(episodes);
    currentIndex_ = qBound(0, currentPlaybackEpisode_, qMax(0, playlistModel_.count() - 1));
    emit currentIndexChanged();
    const QString url = episodes.at(currentPlaybackEpisode_).toMap()
        .value(QStringLiteral("url")).toString();
    clearSourceSwitchSuggestion();
    playVodUrl(url);
}

void PlayerController::rejectSourceSwitch() {
    if (pendingPlaybackSource_ >= 0)
        sourceSuggestionCooldowns_[pendingPlaybackSource_] = QDateTime::currentMSecsSinceEpoch() + 10 * 60 * 1000;
    clearSourceSwitchSuggestion();
}

void PlayerController::armFirstFrameDetection() {
    if (firstFrameTimer_ && playbackSources_.size() > 1)
        firstFrameTimer_->start();
}

void PlayerController::markPlaybackProgress() {
    if (firstFrameTimer_) firstFrameTimer_->stop();
    if (pendingSourceSeekMs_ >= 0 && durationMs_ > 0) {
        const qint64 seekTarget = qMin(pendingSourceSeekMs_, durationMs_);
        pendingSourceSeekMs_ = -1;
        seek(seekTarget);
        if (pendingSourcePaused_) {
            pendingSourcePaused_ = false;
            QTimer::singleShot(250, this, &PlayerController::pause);
        }
    }
}

void PlayerController::updateMpvVideoWindowVisibility() {
    if (!mpvVideoWindow_) return;
    if (mpvSurfaceVisible_ && mpvMode_ && !loading_ && !mpvPlaybackFinished_ && !currentFile_.isEmpty()) {
        mpvVideoWindow_->show();
    } else {
        mpvVideoWindow_->hide();
    }
}

void PlayerController::saveScreenshot() {
    auto *bridge = engine_.videoBridge();
    if (!bridge) {
        Logger::instance().warn("No video bridge for screenshot");
        return;
    }
    QImage frame = bridge->currentFrame();
    if (frame.isNull()) {
        Logger::instance().warn("No frame to save");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        nullptr, "Save Screenshot", "screenshot.png",
        "PNG Files (*.png);;All Files (*.*)");
    if (path.isEmpty()) return;
    if (frame.save(path)) {
        Logger::instance().info("Screenshot saved: " + path);
    } else {
        Logger::instance().error("Failed to save screenshot: " + path);
    }
}

void PlayerController::handleEof() {
    switch (repeatMode_) {
    case RepeatOne:
        Logger::instance().info("EOF: repeat one, restarting");
        play();
        break;
    case RepeatAll:
        Logger::instance().info("EOF: repeat all, playing next");
        playNext();
        break;
    case NoRepeat:
    default:
        Logger::instance().info("EOF: no repeat, stopped");
        setLoading(false);
        isPlaying_ = false;
        isPaused_ = false;
        emit playbackStateChanged();
        emit playbackEnded();
        break;
    }
}

void PlayerController::finishSeek() {
    if (seekWatchdog_) {
        seekWatchdog_->stop();
    }
    if (pendingSeekMs_ >= 0) {
        positionMs_ = pendingSeekMs_;
        pendingSeekMs_ = -1;
    }
    seeking_ = false;
    setLoading(false);
    emit seekingChanged();
    emit timelineChanged();
    if (mpvMode_) {
        mpvBackend_.play();
    } else if (!engine_.isPlaying()) {
        engine_.play();
    }
    isPlaying_ = true;
    isPaused_ = false;
    emit playbackStateChanged();
}

void PlayerController::loadSettings() {
    QSettings s("MeloBox", "MeloBox");
    volume_ = qBound(0.0f, s.value("volume", 1.0f).toFloat(), 1.0f);
    if (volume_ <= 0.01f) {
        volume_ = 1.0f;
        s.setValue("volume", volume_);
        Logger::instance().warn("Saved volume was muted, reset to 1.0");
    }
    engine_.setVolume(volume_);
    if (qtAudioOutput_) {
        qtAudioOutput_->setVolume(volume_);
    }
    historyModel_.setItems(s.value("history").toList());

    Logger::instance().info("Settings loaded: volume=" + QString::number(volume_)
        + ", history=" + QString::number(historyModel_.count()) + " items");
}

void PlayerController::saveSettings() {
    QSettings s("MeloBox", "MeloBox");
    s.setValue("volume", volume_);
    s.setValue("history", historyModel_.toVariantList());

    s.remove("playlist");
}

QVariant PlayerController::uiSetting(const QString &key, const QVariant &defaultValue) const {
    if (key.trimmed().isEmpty()) return defaultValue;
    QSettings s("MeloBox", "MeloBox");
    return s.value(QStringLiteral("ui/%1").arg(key), defaultValue);
}

void PlayerController::saveUiSetting(const QString &key, const QVariant &value) {
    if (key.trimmed().isEmpty()) return;
    QSettings s("MeloBox", "MeloBox");
    s.setValue(QStringLiteral("ui/%1").arg(key), value);
}

void PlayerController::recordPlaybackHistory(const QString &filePath, const QString &title) {
    if (filePath.trimmed().isEmpty()) return;
    historyModel_.addHistory(filePath, title);
    saveSettings();
}

void PlayerController::wireEngineSignals() {
    connect(&engine_, &PlayerEngine::positionUpdated, this, [this](double sec) {
        if (opening_) return; // Ignore stale updates during file switch
        qint64 ms = static_cast<qint64>(sec * 1000);
        // Ignore position updates that are far before the seek target
        if (pendingSeekMs_ >= 0 && ms < pendingSeekMs_ - 100) {
            return;
        }
        if (pendingSeekMs_ >= 0 && engine_.isNetwork()) {
            positionMs_ = pendingSeekMs_;
            emit timelineChanged();
            return;
        }
        if (pendingSeekMs_ >= 0 && ms >= pendingSeekMs_ - 100) {
            finishSeek();
        }
        if (ms < positionMs_ - 1000) {
            return;
        }
        positionMs_ = ms;
        emit timelineChanged();
    });

    connect(&engine_, &PlayerEngine::stateChanged, this, [this]() {
        isPlaying_ = engine_.isPlaying();
        isPaused_ = engine_.isPaused();
        emit playbackStateChanged();
        if (engine_.eofReached()) {
            handleEof();
        }
    });

    connect(&engine_, &PlayerEngine::mediaInfoReady, this, [this]() {
        mediaInfoModel_.replaceAll(engine_.mediaInfoModel()->items());
    });

    connect(&engine_, &PlayerEngine::seekTargetReached, this, [this]() {
        if (pendingSeekMs_ >= 0 && !engine_.isNetwork()) {
            finishSeek();
        }
    });

    connect(&engine_, &PlayerEngine::bufferingReady, this, [this]() {
        if (pendingSeekMs_ >= 0) {
            finishSeek();
        }
    });

    connect(&engine_, &PlayerEngine::thumbnailReady, this, [this](double pos, const QImage &img) {
        thumbnailPreview_ = img;
        thumbnailPosition_ = pos;
        thumbnailReady_ = true;
        emit thumbnailReadyForProvider(QString::number(pos, 'f', 3), img);
        emit thumbnailPreviewChanged();
    });
}

VideoSearchModel *PlayerController::videoSearchModel() { return &videoSearchModel_; }
VideoSearchModel *PlayerController::detailSearchModel() { return &detailSearchModel_; }
ApiSiteModel *PlayerController::apiSiteModel() { return &apiSiteModel_; }
DmghgAnimeModel *PlayerController::dmghgAnimeModel() { return &dmghgAnimeModel_; }
BeeVideoModel *PlayerController::beeVideoModel() { return &beeVideoModel_; }
BeeScheduleModel *PlayerController::beeScheduleModel() { return &beeScheduleModel_; }
DownloadModel *PlayerController::downloadModel() { return &downloadModel_; }

void PlayerController::searchCurrentVodOnSite(int siteIndex) {
    if (currentVodName_.trimmed().isEmpty()) {
        Logger::instance().warn("No current VOD name for source search");
        return;
    }

    const QString baseUrl = apiSiteModel_.baseUrlAt(siteIndex);
    if (baseUrl.isEmpty()) {
        Logger::instance().error("Invalid API site index for source search: " + QString::number(siteIndex));
        return;
    }

    Logger::instance().info("Source search: " + currentVodName_ + " on " + baseUrl);
    sourceSearchModel_.search(baseUrl, currentVodName_, 1);
}

void PlayerController::downloadVideoEpisodes(const QString &vodName, const QString &poster, const QVariantList &episodes) {
    downloadModel_.enqueueVideo(vodName, poster, episodes);
}

void PlayerController::saveVideoM3u8Episodes(const QString &vodName, const QString &poster, const QVariantList &episodes) {
    downloadModel_.enqueueVideoM3u8Only(vodName, poster, episodes);
}

void PlayerController::downloadCurrentPlaylist() {
    QVariantList episodes;
    for (int i = 0; i < playlistModel_.count(); ++i) {
        const QString url = playlistModel_.filePathAt(i);
        if (url.trimmed().isEmpty()) continue;
        QVariantMap episode;
        episode[QStringLiteral("title")] = QStringLiteral("第 %1 集").arg(i + 1);
        episode[QStringLiteral("url")] = url;
        episodes.append(episode);
    }

    const QString name = currentVodName_.trimmed().isEmpty()
        ? QStringLiteral("当前播放")
        : currentVodName_;
    downloadModel_.enqueueVideo(name, QString(), episodes);
}

void PlayerController::saveCurrentPlaylistM3u8() {
    QVariantList episodes;
    for (int i = 0; i < playlistModel_.count(); ++i) {
        const QString url = playlistModel_.filePathAt(i);
        if (url.trimmed().isEmpty()) continue;
        QVariantMap episode;
        episode[QStringLiteral("title")] = QStringLiteral("第 %1 集").arg(i + 1);
        episode[QStringLiteral("url")] = url;
        episodes.append(episode);
    }

    const QString name = currentVodName_.trimmed().isEmpty()
        ? QStringLiteral("当前播放")
        : currentVodName_;
    downloadModel_.enqueueVideoM3u8Only(name, QString(), episodes);
}

void PlayerController::searchVideos(const QString &keyword, int page, bool forceRefresh) {
    QString baseUrl = apiSiteModel_.currentVideoBaseUrl();
    if (baseUrl.isEmpty()) {
        Logger::instance().error("No API site configured");
        return;
    }
    Logger::instance().info("Searching: " + keyword + " on " + baseUrl);
    videoSearchModel_.search(baseUrl, keyword, page, forceRefresh);
}

void PlayerController::searchVideoById(const QString &vodId) {
    QString baseUrl = apiSiteModel_.currentVideoBaseUrl();
    if (baseUrl.isEmpty()) {
        Logger::instance().error("No API site configured");
        return;
    }
    Logger::instance().info("Searching by ID: " + vodId);
    videoSearchModel_.searchById(baseUrl, vodId);
}

void PlayerController::loadVideoDetail(int vodId) {
    if (vodId <= 0) {
        Logger::instance().warn("Invalid VOD id for detail: " + QString::number(vodId));
        return;
    }
    QString baseUrl = apiSiteModel_.currentVideoBaseUrl();
    if (baseUrl.isEmpty()) {
        Logger::instance().error("No API site configured");
        return;
    }
    Logger::instance().info("Loading VOD detail: " + QString::number(vodId) + " on " + baseUrl);
    detailSearchModel_.searchById(baseUrl, QString::number(vodId));
}

void PlayerController::loadVideoList(int page) {
    QString baseUrl = apiSiteModel_.currentVideoBaseUrl();
    if (baseUrl.isEmpty()) {
        Logger::instance().error("No API site configured");
        return;
    }
    Logger::instance().info("Loading video list page " + QString::number(page));
    videoSearchModel_.loadList(baseUrl, page);
}

void PlayerController::loadVideoListByCategory(const QString &typeId, int page) {
    QString baseUrl = apiSiteModel_.currentVideoBaseUrl();
    if (baseUrl.isEmpty()) {
        Logger::instance().error("No API site configured");
        return;
    }
    Logger::instance().info("Loading video list page " + QString::number(page)
        + " category " + typeId);
    videoSearchModel_.loadList(baseUrl, page, typeId);
}

void PlayerController::preparePlaybackTarget(const QString &displayUrl) {
    if (qtPlayer_) {
        qtPlayer_->stop();
        qtPlayer_->setSource({});
    }
    qtNetworkMode_ = false;
    currentFile_ = displayUrl.trimmed();
    positionMs_ = 0;
    durationMs_ = 0;
    mpvBufferProgress_ = 0.0;
    mpvBuffering_ = false;
    setLoading(true);
    emit currentFileChanged();
    emit timelineChanged();
}

void PlayerController::playVideoInput(const QString &inputUrl, const QString &displayUrl) {
    const QString trimmedInput = inputUrl.trimmed();
    const QString trimmedDisplay = displayUrl.trimmed();
    if (trimmedInput.isEmpty() || trimmedDisplay.isEmpty()) return;

    if (trimmedInput != sanitizedHlsPath_)
        removeSanitizedHlsPlaylist();

    Logger::instance().info(QStringLiteral("[Playback] mpv input=%1 display=%2")
        .arg(trimmedInput, trimmedDisplay));
    if (mpvBackend_.ensureAvailable()) {
        engine_.abortPipeline();
        mpvMode_ = true;
        userPausedPlayback_ = false;
        mpvPlaybackFinished_ = false;
        isPlaying_ = true;
        isPaused_ = false;
        if (playbackLoadGuardTimer_) {
            playbackLoadGuardTimer_->start();
        }
        int playlistIndex = playlistModel_.indexOfPath(trimmedDisplay);
        if (playlistIndex < 0) {
            if (playlistModel_.count() == 0) {
                playlistModel_.addFile(trimmedDisplay);
                playlistIndex = 0;
            } else {
                playlistIndex = qBound(0, currentIndex_, playlistModel_.count() - 1);
            }
        }
        currentIndex_ = playlistIndex;
        recordPlaybackHistory(trimmedDisplay, playlistModel_.titleAt(playlistIndex));
        saveSettings();
        emit currentIndexChanged();
        emit playbackStateChanged();
        ensureMpvVideoWindow();
        if (mpvVideoWindow_) {
            mpvBackend_.setWindowId(static_cast<qint64>(mpvVideoWindow_->winId()));
        }
        updateMpvVideoWindowVisibility();
        refreshMpvVideoWindow();
        mpvBackend_.setVolume(volume_);
        mpvBackend_.setSpeed(playbackRate_);
        Logger::instance().info(QStringLiteral("[Playback] mpv load input=%1 currentFile=%2")
            .arg(trimmedInput, currentFile_));
        mpvBackend_.load(trimmedInput);
        mpvBackend_.play();
        armFirstFrameDetection();
        return;
    }
    Logger::instance().warn("libmpv backend unavailable, falling back to FFmpeg pipeline: " + mpvBackend_.errorString());
    if (qtPlayer_) {
        qtPlayer_->stop();
        qtPlayer_->setSource({});
    }
    qtNetworkMode_ = false;
    openFileAtPath(trimmedInput, true);
}

void PlayerController::removeSanitizedHlsPlaylist() {
    if (sanitizedHlsPath_.isEmpty())
        return;
    QFile::remove(sanitizedHlsPath_);
    sanitizedHlsPath_.clear();
}

void PlayerController::cancelHlsFilterRequest() {
    ++hlsRequestSerial_;
    if (hlsFilterReply_) {
        hlsFilterReply_->abort();
        hlsFilterReply_->deleteLater();
        hlsFilterReply_ = nullptr;
    }
}

void PlayerController::fetchAndFilterHls(const QString &url, quint64 requestSerial) {
    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "application/vnd.apple.mpegurl, application/x-mpegURL, */*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(3500);
    auto *reply = nam->get(request);
    hlsFilterReply_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, url, requestSerial]() {
        reply->deleteLater();
        nam->deleteLater();
        if (hlsFilterReply_ == reply)
            hlsFilterReply_ = nullptr;
        if (requestSerial != hlsRequestSerial_)
            return;

        QString inputUrl = url;
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray raw = reply->readAll();
            const QString content = QString::fromUtf8(raw);
            if (content.trimmed().startsWith(QStringLiteral("#EXTM3U"))) {
                const QUrl manifestUrl = reply->url().isValid() ? reply->url() : QUrl(url);
                const auto result = HlsPlaylistFilter::filterOutOfSequenceAds(content, manifestUrl);
                if (result.skippedSegments > 0) {
                    removeSanitizedHlsPlaylist();
                    const QString path = QDir(QDir::tempPath()).filePath(
                        QStringLiteral("melobox-hls-%1-%2.m3u8")
                            .arg(QCoreApplication::applicationPid())
                            .arg(requestSerial));
                    QFile file(path);
                    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        file.write(result.playlist.toUtf8());
                        file.close();
                        sanitizedHlsPath_ = path;
                        inputUrl = path;
                        Logger::instance().info(QStringLiteral("[HLS] skipped %1 out-of-sequence ad segments")
                            .arg(result.skippedSegments));
                    } else {
                        Logger::instance().warn(QStringLiteral("[HLS] cannot write filtered playlist: %1").arg(path));
                    }
                }
            }
        } else {
            Logger::instance().warn(QStringLiteral("[HLS] manifest filter request failed: %1; using original URL")
                .arg(reply->errorString()));
        }
        playVideoInput(inputUrl, url);
    });
}

void PlayerController::playVideoUrl(const QString &url) {
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty()) return;
    Logger::instance().info("Playing video URL: " + trimmed);
    Logger::instance().info(QStringLiteral("[ShortVideoDebug][C++] playVideoUrl enter url=%1 currentShortVideoUrl=%2 mpvMode=%3 loading=%4 position=%5 duration=%6")
        .arg(url, currentShortVideoUrl_)
        .arg(mpvMode_)
        .arg(loading_)
        .arg(positionMs_)
        .arg(durationMs_));

    cancelHlsFilterRequest();
    removeSanitizedHlsPlaylist();
    if (hlsPlaylistProxy_)
        hlsPlaylistProxy_->stop();
    preparePlaybackTarget(trimmed);
    const QUrl parsed(trimmed);
    const bool isRemoteHls = parsed.scheme().startsWith(QStringLiteral("http"), Qt::CaseInsensitive)
        && parsed.path().endsWith(QStringLiteral(".m3u8"), Qt::CaseInsensitive);
    if (isRemoteHls) {
        if (hlsPlaylistProxy_ && hlsPlaylistProxy_->start()) {
            const QUrl localUrl = hlsPlaylistProxy_->proxyUrl(parsed);
            Logger::instance().info(QStringLiteral("[HLS] playing through playlist proxy: %1")
                .arg(localUrl.toString()));
            playVideoInput(localUrl.toString(), trimmed);
        } else {
            Logger::instance().warn(QStringLiteral("[HLS] playlist proxy unavailable; using original URL"));
            playVideoInput(trimmed, trimmed);
        }
        return;
    }
    playVideoInput(trimmed, trimmed);
}


void PlayerController::playVodUrl(const QString &url) {
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty()) return;

    if (!currentShortVideoUrl_.isEmpty()) {
        currentShortVideoUrl_.clear();
        shortVideoMessage_.clear();
        emit shortVideoChanged();
    }
    Logger::instance().info("Playing VOD URL: " + trimmed);
    playVideoUrl(trimmed);
}
void PlayerController::resolveAndPlayUrl(const QString &url) {
    if (url.isEmpty()) return;

    // If already a direct m3u8/ts/mp4 link, play directly
    QString lower = url.toLower();
    if (lower.contains(".m3u8") || lower.contains(".mp4") || lower.contains(".ts")) {
        playVideoUrl(url);
        return;
    }

    // Otherwise, it's a share page - fetch and extract the real video URL
    Logger::instance().info("Resolving share URL: " + url);
    setLoading(true);

    QNetworkAccessManager *nam = new QNetworkAccessManager(this);
    QUrl qurl(url);
    QNetworkRequest req(qurl);
    req.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");

    QNetworkReply *reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, url]() {
        reply->deleteLater();
        nam->deleteLater();
        setLoading(false);

        if (reply->error() != QNetworkReply::NoError) {
            Logger::instance().error("Failed to resolve URL: " + reply->errorString());
            // Try playing original URL as fallback
            playVideoUrl(url);
            return;
        }

        QString html = QString::fromUtf8(reply->readAll());

        // Extract m3u8 URL from JavaScript: var main = "/path/to/video.m3u8?sign=xxx";
        static QRegularExpression re("var\\s+main\\s*=\\s*\"([^\"]+\\.m3u8[^\"]*)\"");
        auto match = re.match(html);

        if (match.hasMatch()) {
            QString m3u8Path = match.captured(1);
            // Build full URL from the share page URL
            QUrl shareUrl(url);
            QString resolved = shareUrl.scheme() + "://" + shareUrl.host();
            if (shareUrl.port() > 0) resolved += ":" + QString::number(shareUrl.port());
            resolved += m3u8Path;

            Logger::instance().info("Resolved m3u8: " + resolved);
            playVideoUrl(resolved);
        } else {
            Logger::instance().warn("No m3u8 found in share page, trying original URL");
            playVideoUrl(url);
        }
    });
}

void PlayerController::setImageState(bool loading, const QString &url, const QString &message, const QString &displayUrl) {
    bool changed = false;
    const QString resolvedDisplayUrl = displayUrl.isNull() ? currentImageDisplayUrl_ : displayUrl;
    // setImageState 由 QML 频繁触发 (imageLoading/图片源切换), display 常是 data: base64
    // 图片, 整行记进日志会令 miniplayer.log 膨胀到上百 MB。状态变更逻辑本身无需日志佐证,
    // 去掉这条 info, 保留真实的重定向/下载错误日志 (见下方 redirect/error)。
    if (imageLoading_ != loading) {
        imageLoading_ = loading;
        changed = true;
    }
    if (currentImageUrl_ != url) {
        currentImageUrl_ = url;
        changed = true;
    }
    if (currentImageDisplayUrl_ != resolvedDisplayUrl) {
        currentImageDisplayUrl_ = resolvedDisplayUrl;
        changed = true;
    }
    if (imageMessage_ != message) {
        imageMessage_ = message;
        changed = true;
    }
    if (changed)
        emit imageStateChanged();
}

void PlayerController::showPrefetchedImage(const QString &imageUrl) {
    const QString trimmed = imageUrl.trimmed();
    if (trimmed.isEmpty()) return;

    currentImageBytes_.clear();
    currentImageContentType_.clear();
    if (trimmed.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive)) {
        // 兼容旧预载池里的 data: 链接。
        const int separator = trimmed.indexOf(QLatin1Char(','));
        const int typeEnd = trimmed.indexOf(QLatin1Char(';'));
        if (separator > 0) {
            currentImageContentType_ = trimmed.mid(5, typeEnd > 5 ? typeEnd - 5 : separator - 5);
            currentImageBytes_ = QByteArray::fromBase64(trimmed.mid(separator + 1).toLatin1());
        }
    } else {
        loadImageBytesFromLocalUrl(trimmed);
    }
    setImageState(false, QStringLiteral("prefetched://image"), QStringLiteral("已从缓存池加载图片"), trimmed);
}

void PlayerController::prefetchImage(const QString &apiUrl) {
    const QString trimmedApiUrl = apiUrl.trimmed();
    if (trimmedApiUrl.isEmpty()) {
        emit imagePrefetched(trimmedApiUrl, {});
        return;
    }

    const CachedImageEntry cached = imageCache_.take(trimmedApiUrl);
    if (!cached.bytes.isEmpty() && !cached.localPath.isEmpty()) {
        emit imagePrefetched(trimmedApiUrl, localImageFileUrl(cached.localPath));
        emit imageCacheChanged();
        return;
    }

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(trimmedApiUrl)};
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "application/json,image/*,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = nam->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, trimmedApiUrl]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            nam->deleteLater();
            emit imagePrefetched(trimmedApiUrl, {});
            return;
        }

        const QByteArray bytes = reply->readAll();
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        const QString redirectedUrl = redirectedUrlFromReply(reply);
        const QString imageUrl = redirectedUrl.isEmpty()
            ? extractImageUrlFromResponse(bytes, reply->url(), contentType)
            : redirectedUrl;
        if (contentType.startsWith(QStringLiteral("image/"), Qt::CaseInsensitive)) {
            nam->deleteLater();
            const QString mime = contentType.section(QLatin1Char(';'), 0, 0).trimmed();
            const QString effectiveMime = mime.isEmpty() ? QStringLiteral("image/png") : mime;
            const QString localPath = cacheImageBytes(trimmedApiUrl, reply->url().toString(), bytes, effectiveMime);
            emit imageCacheChanged();
            emit imagePrefetched(trimmedApiUrl,
                localPath.isEmpty()
                    ? QStringLiteral("data:%1;base64,%2").arg(effectiveMime,
                        QString::fromLatin1(bytes.toBase64()))
                    : localImageFileUrl(localPath));
            return;
        }
        if (imageUrl.trimmed().isEmpty()) {
            nam->deleteLater();
            emit imagePrefetched(trimmedApiUrl, {});
            return;
        }

        QNetworkRequest imageRequest{QUrl(imageUrl)};
        imageRequest.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
        imageRequest.setRawHeader("Accept", "image/*,*/*");
        imageRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        auto *imageReply = nam->get(imageRequest);
        connect(imageReply, &QNetworkReply::finished, this,
                [this, imageReply, nam, trimmedApiUrl]() {
            imageReply->deleteLater();
            nam->deleteLater();
            if (imageReply->error() != QNetworkReply::NoError) {
                emit imagePrefetched(trimmedApiUrl, {});
                return;
            }
            const QByteArray imageBytes = imageReply->readAll();
            if (imageBytes.isEmpty() || imageBytes.size() > 15 * 1024 * 1024) {
                emit imagePrefetched(trimmedApiUrl, {});
                return;
            }
            QString mime = imageReply->header(QNetworkRequest::ContentTypeHeader).toString()
                .section(QLatin1Char(';'), 0, 0).trimmed();
            if (mime.isEmpty()) mime = QStringLiteral("image/png");
            const QString localPath = cacheImageBytes(trimmedApiUrl, imageReply->url().toString(), imageBytes, mime);
            emit imageCacheChanged();
            emit imagePrefetched(trimmedApiUrl,
                localPath.isEmpty()
                    ? QStringLiteral("data:%1;base64,%2").arg(mime,
                        QString::fromLatin1(imageBytes.toBase64()))
                    : localImageFileUrl(localPath));
        });
    });
}

void PlayerController::clearImageCache() {
    imageCache_.clear();
    imageMessage_ = QStringLiteral("图片磁盘缓存已清理");
    emit imageCacheChanged();
    emit imageStateChanged();
}
void PlayerController::loadRandomImage(const QString &requestedApiUrl) {
    if (imageLoading_) {
        Logger::instance().info(QStringLiteral("[ImageDebug] ignore duplicate loadRandomImage while loading"));
        return;
    }
    lastImageLoadRequestMs_ = QDateTime::currentMSecsSinceEpoch();

    const QString apiUrl = requestedApiUrl.trimmed().isEmpty()
        ? apiSiteModel_.imageBaseUrl().trimmed()
        : requestedApiUrl.trimmed();
    Logger::instance().info(QStringLiteral("[ImageDebug] loadRandomImage api=%1 currentSiteIndex=%2")
        .arg(apiUrl)
        .arg(apiSiteModel_.currentIndex()));
    if (apiUrl.isEmpty()) {
        setImageState(false, currentImageUrl_, QStringLiteral("没有配置图片站点"));
        return;
    }

    // 磁盘缓存有直接可用的图片时先展示，用户点击“换一张”不必等网络往返；
    // 预载池会继续在后台补新图。
    const CachedImageEntry cached = imageCache_.take(apiUrl);
    if (!cached.bytes.isEmpty() && !cached.localPath.isEmpty()) {
        currentImageBytes_ = cached.bytes;
        currentImageContentType_ = cached.mimeType;
        setImageState(false, cached.sourceUrl.isEmpty() ? currentImageUrl_ : cached.sourceUrl,
            QStringLiteral("已从磁盘缓存加载图片"), localImageFileUrl(cached.localPath));
        emit imageCacheChanged();
        return;
    }

    setImageState(true, currentImageUrl_, QStringLiteral("正在获取图片..."), QStringLiteral(""));
    currentImageBytes_.clear();
    currentImageContentType_.clear();

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(apiUrl)};
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "application/json,image/*,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            nam->deleteLater();
            setImageState(false, currentImageUrl_, QStringLiteral("图片请求失败：%1").arg(reply->errorString()));
            return;
        }

        const QByteArray bytes = reply->readAll();
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        Logger::instance().info(QStringLiteral("[ImageDebug] api reply url=%1 status=%2 contentType=%3 bytes=%4")
            .arg(reply->url().toString())
            .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
            .arg(contentType)
            .arg(bytes.size()));
        const QString redirectedImageUrl = redirectedUrlFromReply(reply);
        const QString imageUrl = redirectedImageUrl.isEmpty()
            ? extractImageUrlFromResponse(bytes, reply->url(), contentType)
            : redirectedImageUrl;
        Logger::instance().info(QStringLiteral("[ImageDebug] resolved imageUrl=%1 redirected=%2")
            .arg(imageUrl, redirectedImageUrl));
        if (imageUrl.trimmed().isEmpty()) {
            nam->deleteLater();
            setImageState(false, currentImageUrl_, QStringLiteral("没有从接口响应里解析到图片地址"));
            return;
        }

        if (contentType.startsWith(QStringLiteral("image/"), Qt::CaseInsensitive)) {
            nam->deleteLater();
            currentImageBytes_ = bytes;
            currentImageContentType_ = contentType;
            const QString mime = contentType.section(QLatin1Char(';'), 0, 0).trimmed();
            const QString effectiveMime = mime.isEmpty() ? QStringLiteral("image/png") : mime;
            const QString localPath = cacheImageBytes(apiSiteModel_.imageBaseUrl().trimmed(), reply->url().toString(), bytes, effectiveMime);
            emit imageCacheChanged();
            const QString displayUrl = localPath.isEmpty()
                ? QStringLiteral("data:%1;base64,%2").arg(effectiveMime,
                    QString::fromLatin1(bytes.toBase64()))
                : localImageFileUrl(localPath);
            setImageState(false, imageUrl, QStringLiteral("图片已加载"), displayUrl);
            return;
        }

        QNetworkRequest imageRequest{QUrl(imageUrl)};
        imageRequest.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
        imageRequest.setRawHeader("Accept", "image/*,*/*");
        imageRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

        auto *imageReply = nam->get(imageRequest);
        connect(imageReply, &QNetworkReply::finished, this, [this, imageReply, nam, imageUrl]() {
            imageReply->deleteLater();
            nam->deleteLater();

            if (imageReply->error() != QNetworkReply::NoError) {
                Logger::instance().warn(QStringLiteral("[ImageDebug] image download error url=%1 error=%2")
                    .arg(imageUrl, imageReply->errorString()));
                setImageState(false, imageUrl, QStringLiteral("图片下载失败：%1").arg(imageReply->errorString()));
                return;
            }

            const QByteArray imageBytes = imageReply->readAll();
            const QString imageContentType = imageReply->header(QNetworkRequest::ContentTypeHeader).toString();
            Logger::instance().info(QStringLiteral("[ImageDebug] image reply url=%1 status=%2 contentType=%3 bytes=%4")
                .arg(imageReply->url().toString())
                .arg(imageReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                .arg(imageContentType)
                .arg(imageBytes.size()));
            const QString imageReplyRedirectedUrl = redirectedUrlFromReply(imageReply);
            const QString finalImageUrl = !imageReplyRedirectedUrl.isEmpty()
                ? imageReplyRedirectedUrl
                : (imageReply->url().isValid() && !imageReply->url().isEmpty()
                    ? imageReply->url().toString()
                    : imageUrl);
            Logger::instance().info(QStringLiteral("[ImageDebug] finalImageUrl=%1 imageRedirect=%2")
                .arg(finalImageUrl, imageReplyRedirectedUrl));
            currentImageBytes_ = imageBytes;
            currentImageContentType_ = imageContentType.isEmpty() ? QStringLiteral("image/png") : imageContentType;
            const QString mime = currentImageContentType_.section(QLatin1Char(';'), 0, 0).trimmed();
            const QString effectiveMime = mime.isEmpty() ? QStringLiteral("image/png") : mime;
            const QString localPath = cacheImageBytes(apiSiteModel_.imageBaseUrl().trimmed(), finalImageUrl, imageBytes, effectiveMime);
            emit imageCacheChanged();
            const QString displayUrl = localPath.isEmpty()
                ? QStringLiteral("data:%1;base64,%2").arg(effectiveMime,
                    QString::fromLatin1(imageBytes.toBase64()))
                : localImageFileUrl(localPath);
            setImageState(false, finalImageUrl, QStringLiteral("图片已加载"), displayUrl);
        });
    });
}

void PlayerController::saveCurrentImage() {
    const QString imageUrl = currentImageUrl_.trimmed();
    if (imageUrl.isEmpty()) {
        setImageState(false, currentImageUrl_, QStringLiteral("请先加载一张图片"));
        return;
    }

    const QUrl sourceUrl(imageUrl);
    if (!sourceUrl.isValid() || sourceUrl.scheme().isEmpty()) {
        setImageState(false, currentImageUrl_, QStringLiteral("当前图片地址无效"));
        return;
    }

    if (!currentImageBytes_.isEmpty()) {
        saveImageBytes(currentImageBytes_, sourceUrl, currentImageContentType_);
        return;
    }

    setImageState(true, currentImageUrl_, QStringLiteral("正在下载图片..."));
    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(sourceUrl);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "image/*,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, sourceUrl]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            setImageState(false, currentImageUrl_, QStringLiteral("图片下载失败：%1").arg(reply->errorString()));
            return;
        }

        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        saveImageBytes(reply->readAll(), sourceUrl, contentType);
    });
}

void PlayerController::copyCurrentImage() {
    const QString imageUrl = currentImageUrl_.trimmed();
    if (imageUrl.isEmpty()) {
        setImageState(false, currentImageUrl_, QStringLiteral("请先加载一张图片"));
        return;
    }

    const auto copyBytesToClipboard = [this](const QByteArray &bytes) {
        const QImage image = imageFromBytes(bytes);
        if (image.isNull()) {
            imageMessage_ = QStringLiteral("图片解码失败，无法复制");
            emit imageStateChanged();
            return false;
        }

        if (auto *clipboard = QGuiApplication::clipboard()) {
            clipboard->setImage(image);
            imageMessage_ = QStringLiteral("已复制图片");
        } else {
            imageMessage_ = QStringLiteral("系统剪贴板不可用");
        }
        emit imageStateChanged();
        return true;
    };

    if (!currentImageBytes_.isEmpty()) {
        copyBytesToClipboard(currentImageBytes_);
        return;
    }

    const QUrl sourceUrl(imageUrl);
    if (!sourceUrl.isValid() || sourceUrl.scheme().isEmpty()) {
        setImageState(false, currentImageUrl_, QStringLiteral("当前图片地址无效"));
        return;
    }

    imageMessage_ = QStringLiteral("正在复制图片...");
    emit imageStateChanged();
    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(sourceUrl);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "image/*,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, copyBytesToClipboard]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            imageMessage_ = QStringLiteral("图片复制失败：%1").arg(reply->errorString());
            emit imageStateChanged();
            return;
        }

        const QByteArray bytes = reply->readAll();
        currentImageBytes_ = bytes;
        currentImageContentType_ = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        copyBytesToClipboard(bytes);
    });
}

void PlayerController::searchMemes(const QString &keyword, int count) {
    const QString trimmedKeyword = keyword.trimmed().isEmpty() ? QStringLiteral("龙图") : keyword.trimmed();
    QUrl url(QStringLiteral("https://api.yujn.cn/api/bbq_ss.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("count"), QString::number(qBound(1, count, 30)));
    query.addQueryItem(QStringLiteral("msg"), trimmedKeyword);
    query.addQueryItem(QStringLiteral("_t"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.setQuery(query);

    memeLoading_ = true;
    memeMessage_ = QStringLiteral("正在搜索表情包...");
    emit memeStateChanged();

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "application/json,image/*,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, trimmedKeyword]() {
        reply->deleteLater();
        nam->deleteLater();
        memeLoading_ = false;

        if (reply->error() != QNetworkReply::NoError) {
            memeMessage_ = QStringLiteral("表情包请求失败：%1").arg(reply->errorString());
            emit memeStateChanged();
            return;
        }

        const QStringList urls = extractImageUrlsFromResponse(reply->readAll(),
                                                              reply->url(),
                                                              reply->header(QNetworkRequest::ContentTypeHeader).toString());
        memeImages_.clear();
        for (const QString &url : urls) {
            if (!url.trimmed().isEmpty() && !memeImages_.contains(url))
                memeImages_.append(url);
        }
        memeMessage_ = memeImages_.isEmpty()
            ? QStringLiteral("没有解析到“%1”的表情包").arg(trimmedKeyword)
            : QStringLiteral("已加载 %1 张“%2”表情包").arg(memeImages_.size()).arg(trimmedKeyword);
        emit memeStateChanged();
    });
}

void PlayerController::loadDragonMeme() {
    QUrl url(QStringLiteral("https://api.yujn.cn/api/long.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("image"));
    query.addQueryItem(QStringLiteral("_t"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.setQuery(query);

    memeImages_.clear();
    memeImages_.append(url.toString());
    memeMessage_ = QStringLiteral("已加载随机龙图");
    emit memeStateChanged();
}

void PlayerController::saveMemeImage(const QString &imageUrl) {
    const QUrl sourceUrl(imageUrl.trimmed());
    if (!sourceUrl.isValid() || sourceUrl.scheme().isEmpty()) {
        memeMessage_ = QStringLiteral("表情包地址无效");
        emit memeStateChanged();
        return;
    }

    memeMessage_ = QStringLiteral("正在下载表情包...");
    emit memeStateChanged();

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(sourceUrl);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "image/*,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, sourceUrl]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            memeMessage_ = QStringLiteral("表情包下载失败：%1").arg(reply->errorString());
            emit memeStateChanged();
            return;
        }

        const QByteArray bytes = reply->readAll();
        if (bytes.isEmpty()) {
            memeMessage_ = QStringLiteral("表情包内容为空，无法保存");
            emit memeStateChanged();
            return;
        }

        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        const QString folder = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("download/meme"));
        QDir().mkpath(folder);
        const QString fileName = QStringLiteral("meme-%1.%2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz")),
                 imageExtension(sourceUrl, contentType));
        const QString path = QDir(folder).filePath(fileName);

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            memeMessage_ = QStringLiteral("保存失败：%1").arg(file.errorString());
            emit memeStateChanged();
            return;
        }
        file.write(bytes);
        file.close();
        memeMessage_ = QStringLiteral("已保存：%1").arg(path);
        emit memeStateChanged();
    });
}

void PlayerController::copyMemeUrl(const QString &imageUrl) {
    const QUrl sourceUrl(imageUrl.trimmed());
    if (!sourceUrl.isValid() || sourceUrl.scheme().isEmpty()) {
        memeMessage_ = QStringLiteral("没有可复制的表情包地址");
        emit memeStateChanged();
        return;
    }

    memeMessage_ = QStringLiteral("正在复制表情包图片...");
    emit memeStateChanged();

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(sourceUrl);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "image/*,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            memeMessage_ = QStringLiteral("表情包复制失败：%1").arg(reply->errorString());
            emit memeStateChanged();
            return;
        }

        const QImage image = imageFromBytes(reply->readAll());
        if (image.isNull()) {
            memeMessage_ = QStringLiteral("表情包解码失败，无法复制图片");
            emit memeStateChanged();
            return;
        }

        if (auto *clipboard = QGuiApplication::clipboard()) {
            clipboard->setImage(image);
            memeMessage_ = QStringLiteral("已复制表情包图片");
        } else {
            memeMessage_ = QStringLiteral("系统剪贴板不可用");
        }
        emit memeStateChanged();
    });
}

void PlayerController::prefetchShortVideo(int sourceIndex, const QString &sourceUrl,
                                          const QString &label) {
    const QString trimmedUrl = sourceUrl.trimmed();
    if (trimmedUrl.isEmpty()) {
        emit shortVideoPrefetched(sourceIndex, {}, label);
        return;
    }

    QUrl requestUrl(trimmedUrl);
    QUrlQuery query(requestUrl);
    query.removeQueryItem(QStringLiteral("_t"));
    query.addQueryItem(QStringLiteral("_t"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    requestUrl.setQuery(query);

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(requestUrl);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "video/*,application/json,text/plain,*/*");
    request.setRawHeader("Range", "bytes=0-0");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = nam->get(request);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, nam, sourceIndex, trimmedUrl, label]() {
        reply->deleteLater();
        nam->deleteLater();
        if (reply->error() != QNetworkReply::NoError
                && reply->error() != QNetworkReply::ContentAccessDenied) {
            emit shortVideoPrefetched(sourceIndex, {}, label);
            return;
        }

        const QByteArray bytes = reply->readAll();
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        QString resolved = redirectedUrlFromReply(reply);

        if (resolved.isEmpty() && !contentType.startsWith(QStringLiteral("video/"), Qt::CaseInsensitive)) {
            const QJsonDocument doc = QJsonDocument::fromJson(bytes);
            if (doc.isObject()) {
                const QJsonObject object = doc.object();
                const QStringList keys = {QStringLiteral("url"), QStringLiteral("video"),
                                          QStringLiteral("videoUrl"), QStringLiteral("data")};
                for (const QString &key : keys) {
                    const QJsonValue value = object.value(key);
                    if (value.isString() && QUrl(value.toString()).isValid()) {
                        resolved = value.toString();
                        break;
                    }
                    if (value.isObject()) {
                        const QJsonObject nested = value.toObject();
                        for (const QString &nestedKey : keys) {
                            const QString candidate = nested.value(nestedKey).toString();
                            if (!candidate.isEmpty() && QUrl(candidate).isValid()) {
                                resolved = candidate;
                                break;
                            }
                        }
                    }
                    if (!resolved.isEmpty()) break;
                }
            }
            if (resolved.isEmpty()) {
                static const QRegularExpression mediaUrlPattern(
                    QStringLiteral(R"((https?://[^\s"']+\.(?:mp4|m3u8|ts)(?:\?[^\s"']*)?))"),
                    QRegularExpression::CaseInsensitiveOption);
                const auto match = mediaUrlPattern.match(QString::fromUtf8(bytes));
                if (match.hasMatch()) resolved = match.captured(1);
            }
        }

        if (resolved.isEmpty() && contentType.startsWith(QStringLiteral("video/"), Qt::CaseInsensitive))
            resolved = reply->url().toString();
        emit shortVideoPrefetched(sourceIndex, resolved, label);
    });
}
void PlayerController::playShortVideo(int sourceIndex) {
    suspendCurrentVideoForShortVideo();
    const QStringList sources = {
        QStringLiteral("https://api.yujn.cn/api/zzxjj.php?type=video"),
        QStringLiteral("https://api.yujn.cn/api/nvda.php?type=video"),
        QStringLiteral("https://api.yujn.cn/api/nvgao.php?type=video"),
        QStringLiteral("https://api.yujn.cn/api/duilian.php?type=video"),
        QStringLiteral("https://api.yujn.cn/api/heisis.php?type=video"),
        QStringLiteral("https://api.yujn.cn/api/baisis.php?type=video"),
        QStringLiteral("https://api.yujn.cn/api/manzhan.php?type=video"),
        QStringLiteral("http://api.yujn.cn/api/juhexjj.php?type=video"),
        QStringLiteral("http://api.yujn.cn/api/wmsc.php?type=video"),
        QStringLiteral("http://api.yujn.cn/api/COS.php?type=video"),
        QStringLiteral("http://api.yujn.cn/api/hanfu.php"),
        QStringLiteral("http://api.yujn.cn/api/diaodai.php?type=video"),
        QStringLiteral("http://api.yujn.cn/api/manyao.php?type=video"),
        QStringLiteral("http://api.yujn.cn/api/jpmt.php?type=video"),
        QStringLiteral("http://api.yujn.cn/api/qingchun.php?type=video"),
        QStringLiteral("http://api.yujn.cn/api/ksbianzhuang.php?type=video"),
        QStringLiteral("http://api.yujn.cn/api/ksbianzhuang.php?type=video"),
        QStringLiteral("http://api.yujn.cn/api/luoli.php?type=video"),
        QStringLiteral("http://api.yujn.cn/api/rewu.php?type=video"),
        QStringLiteral("http://api.yujn.cn/api/bianzhuang.php??"),
        QStringLiteral("http://api.yujn.cn/api/ksxjjsp.php?"),
        QStringLiteral("http://api.yujn.cn/api/zzxjj.php")
    };
    const int index = qBound(0, sourceIndex, sources.size() - 1);
    QUrl url(sources[index]);
    QUrlQuery query(url);
    query.removeQueryItem(QStringLiteral("_t"));
    query.addQueryItem(QStringLiteral("_t"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.setQuery(query);

    currentShortVideoUrl_ = url.toString();
    shortVideoMessage_ = index == 0
        ? QStringLiteral("正在播放小姐姐短视频")
        : QStringLiteral("正在播放女大短视频");
    if (index == 2) {
        shortVideoMessage_ = QStringLiteral("正在播放女高短视频");
    }
    if (index == 3) {
        shortVideoMessage_ = QStringLiteral("正在播放怼脸短视频");
    }
    if (index == 4) {
        shortVideoMessage_ = QStringLiteral("正在播放黑丝短视频");
    }
    if (index == 5) {
        shortVideoMessage_ = QStringLiteral("正在播放白丝短视频");
    }
    if (index == 6) {
        shortVideoMessage_ = QStringLiteral("正在播放漫展短视频");
    }
    if (index == 7) {
        shortVideoMessage_ = QStringLiteral("正在播放小姐姐短视频");
    }
    if (index == 8) {
        shortVideoMessage_ = QStringLiteral("正在播放完美身材短视频");
    }
    if (index == 9) {
        shortVideoMessage_ = QStringLiteral("正在播放cosplay短视频");
    }
    if (index == 10) {
        shortVideoMessage_ = QStringLiteral("正在播放特色服装短视频");
    }
    if (index == 11) {
        shortVideoMessage_ = QStringLiteral("正在播放吊带短视频");
    }
    if (index == 12) {
        shortVideoMessage_ = QStringLiteral("正在播放小蛮腰短视频");
    }
    if (index == 13) {
        shortVideoMessage_ = QStringLiteral("正在播放足控短视频");
    }
    if (index == 14) {
        shortVideoMessage_ = QStringLiteral("正在播放清纯短视频");
    }
    if (index == 15) {
        shortVideoMessage_ = QStringLiteral("正在播放快手便装短视频");
    }
    if (index == 16) {
        shortVideoMessage_ = QStringLiteral("正在播放双倍快乐短视频");
    }
    if (index == 17) {
        shortVideoMessage_ = QStringLiteral("正在播放萝莉短视频");
    }
    if (index == 18) {
        shortVideoMessage_ = QStringLiteral("正在播放小姐姐热舞视频");
    }
    if (index == 19) {
        shortVideoMessage_ = QStringLiteral("正在播放变装短视频");
    }
    if (index == 20) {
        shortVideoMessage_ = QStringLiteral("正在播放小姐姐短视频");
    }
    if (index == 21) {
        shortVideoMessage_ = QStringLiteral("正在播放小姐姐短视频");
    }
    emit shortVideoChanged();
    playVideoUrl(currentShortVideoUrl_);
}

void PlayerController::playShortVideoUrl(const QString &sourceUrl, const QString &label) {
    suspendCurrentVideoForShortVideo();
    const QString trimmedUrl = sourceUrl.trimmed();
    Logger::instance().info(QStringLiteral("[ShortVideoDebug][C++] playShortVideoUrl source=%1 label=%2")
        .arg(trimmedUrl, label));
    if (trimmedUrl.isEmpty()) {
        shortVideoMessage_ = QStringLiteral("短视频站点地址为空");
        currentShortVideoUrl_.clear();
        emit shortVideoChanged();
        return;
    }

    QUrl url(trimmedUrl);
    QUrlQuery query(url);
    query.removeQueryItem(QStringLiteral("_t"));
    query.addQueryItem(QStringLiteral("_t"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.setQuery(query);

    currentShortVideoUrl_ = url.toString();
    const QString title = label.trimmed().isEmpty() ? QStringLiteral("短视频") : label.trimmed();
    shortVideoMessage_ = QStringLiteral("正在播放%1").arg(title);
    Logger::instance().info(QStringLiteral("[ShortVideoDebug][C++] short video resolved url=%1 title=%2")
        .arg(currentShortVideoUrl_, title));
    emit shortVideoChanged();
    playVideoUrl(currentShortVideoUrl_);
}

void PlayerController::stopShortVideo() {
    const bool ownsPlayback = !currentShortVideoUrl_.isEmpty()
        && currentFile_ == currentShortVideoUrl_;
    if (ownsPlayback)
        stop();
    if (currentShortVideoUrl_.isEmpty() && shortVideoMessage_.isEmpty())
        return;
    currentShortVideoUrl_.clear();
    shortVideoMessage_.clear();
    emit shortVideoChanged();
    restoreSuspendedVideoAfterShortVideo();
}

void PlayerController::suspendCurrentVideoForShortVideo() {
    if (!currentShortVideoUrl_.isEmpty() || currentFile_.trimmed().isEmpty()
        || suspendedVideoRestorePending_ || !suspendedVideoUrl_.isEmpty()) {
        return;
    }

    suspendedVideoUrl_ = currentFile_;
    suspendedVideoPositionMs_ = qMax<qint64>(0, positionMs_);
    suspendedVideoWasPlaying_ = isPlaying_;
    suspendedVideoWasPaused_ = isPaused_;
    Logger::instance().info(QStringLiteral("[Playback] suspended video url=%1 position=%2 playing=%3 paused=%4")
        .arg(suspendedVideoUrl_)
        .arg(suspendedVideoPositionMs_)
        .arg(suspendedVideoWasPlaying_)
        .arg(suspendedVideoWasPaused_));
}

void PlayerController::restoreSuspendedVideoAfterShortVideo() {
    if (suspendedVideoUrl_.isEmpty())
        return;

    suspendedVideoRestorePending_ = true;
    const QString restoreUrl = suspendedVideoUrl_;
    Logger::instance().info(QStringLiteral("[Playback] restoring suspended video url=%1 position=%2")
        .arg(restoreUrl)
        .arg(suspendedVideoPositionMs_));
    playVideoUrl(restoreUrl);
}

void PlayerController::applyPendingSuspendedVideoState() {
    if (!suspendedVideoRestorePending_ || currentFile_ != suspendedVideoUrl_)
        return;
    if (durationMs_ <= 0 && positionMs_ <= 0)
        return;

    const qint64 restorePosition = suspendedVideoPositionMs_;
    const bool shouldPlay = suspendedVideoWasPlaying_;
    const bool shouldPause = suspendedVideoWasPaused_ || !suspendedVideoWasPlaying_;
    suspendedVideoRestorePending_ = false;
    suspendedVideoUrl_.clear();
    suspendedVideoPositionMs_ = 0;
    suspendedVideoWasPlaying_ = false;
    suspendedVideoWasPaused_ = false;

    if (restorePosition > 0)
        seek(restorePosition);
    if (shouldPause)
        pause();
    else if (shouldPlay)
        play();
}

void PlayerController::logShortVideoDebug(const QString &message) {
    Logger::instance().info(QStringLiteral("[ShortVideoDebug][QML] %1").arg(message));
}

QUrl PlayerController::voiceRequestUrl(int sourceIndex, QString *label) const {
    const QString currentTime = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    const QStringList sources = {
        QStringLiteral("http://api.yujn.cn/api/and.php?"),
        QStringLiteral("http://api.yujn.cn/api/yujie.php?"),
        QStringLiteral("https://api.yujn.cn/api/sjkunkun.php?"),
        QStringLiteral("http://api.yujn.cn/api/lvcha.php?"),
        QStringLiteral("http://api.yujn.cn/api/baoshi.php?msg=%1").arg(currentTime)
    };
    const QStringList labels = {
        QStringLiteral("可爱的配音"),
        QStringLiteral("随机播放一条御姐撒娇语音包"),
        QStringLiteral("坤坤语音"),
        QStringLiteral("绿茶"),
        QStringLiteral("语音整点报时")
    };
    const int index = qBound(0, sourceIndex, sources.size() - 1);
    if (label)
        *label = labels[index];
    QUrl url(sources[index]);
    QUrlQuery query(url);
    query.removeQueryItem(QStringLiteral("_t"));
    query.addQueryItem(QStringLiteral("_t"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.setQuery(query);
    return url;
}

void PlayerController::fetchVoiceFile(int sourceIndex, const QString &actionText, const std::function<void(const QString &, const QString &, const QString &)> &onReady) {
    QString label;
    const QUrl url = voiceRequestUrl(sourceIndex, &label);
    const QString requestUrl = url.toString();
    voiceMessage_ = QStringLiteral("正在%1%2").arg(actionText, label);
    emit voiceChanged();

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "audio/*,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, url, requestUrl, label, onReady]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            voiceMessage_ = QStringLiteral("语音获取失败：%1").arg(reply->errorString());
            emit voiceChanged();
            return;
        }

        const QByteArray bytes = reply->readAll();
        if (bytes.isEmpty()) {
            voiceMessage_ = QStringLiteral("语音获取失败：内容为空");
            emit voiceChanged();
            return;
        }

        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        const QString folder = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("cache/voice-current"));
        QDir().mkpath(folder);
        const QString fileName = QStringLiteral("voice-current-%1.%2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz")),
                 audioExtension(url, contentType));
        const QString path = QDir(folder).filePath(fileName);

        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            voiceMessage_ = QStringLiteral("语音缓存失败：%1").arg(path);
            emit voiceChanged();
            return;
        }

        currentVoiceUrl_ = requestUrl;
        currentVoiceFilePath_ = path;
        onReady(path, requestUrl, label);
    });
}

void PlayerController::playVoice(int sourceIndex) {
    currentVoiceFilePath_.clear();

    if (mpvMode_) {
        mpvBackend_.stop();
        mpvMode_ = false;
        updateMpvVideoWindowVisibility();
    }
    engine_.abortPipeline();

    if (!qtPlayer_) {
        Logger::instance().error("QtMediaPlayer is unavailable for voice playback");
        return;
    }

    fetchVoiceFile(sourceIndex, QStringLiteral("获取"), [this](const QString &path, const QString &, const QString &label) {
        voiceMessage_ = QStringLiteral("正在播放%1").arg(label);
        emit voiceChanged();

        qtPlayer_->stop();
        qtPlayer_->setSource(QUrl::fromLocalFile(path));
        qtPlayer_->setPlaybackRate(playbackRate_);
        qtNetworkMode_ = false;
        userPausedPlayback_ = false;
        currentFile_ = path;
        positionMs_ = 0;
        durationMs_ = 0;
        isPlaying_ = true;
        isPaused_ = false;
        setLoading(false);
        emit currentFileChanged();
        emit timelineChanged();
        emit playbackStateChanged();
        qtPlayer_->play();
    });
}

void PlayerController::saveVoice(int sourceIndex) {
    fetchVoiceFile(sourceIndex, QStringLiteral("下载"), [this](const QString &, const QString &, const QString &) {
        saveCurrentVoice();
    });
}

void PlayerController::copyVoice(int sourceIndex) {
    fetchVoiceFile(sourceIndex, QStringLiteral("复制"), [this](const QString &, const QString &, const QString &) {
        copyCurrentVoice();
    });
}

void PlayerController::saveCurrentVoice() {
    if (currentVoiceFilePath_.trimmed().isEmpty() || !QFileInfo::exists(currentVoiceFilePath_)) {
        voiceMessage_ = QStringLiteral("暂无可下载的语音，请先播放一条语音");
        emit voiceChanged();
        return;
    }

    const QString folder = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("download/voice"));
    QDir().mkpath(folder);
    const QString suffix = QFileInfo(currentVoiceFilePath_).suffix().isEmpty()
        ? QStringLiteral("mp3")
        : QFileInfo(currentVoiceFilePath_).suffix();
    const QString path = QDir(folder).filePath(QStringLiteral("voice-%1.%2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz")), suffix));

    if (!QFile::copy(currentVoiceFilePath_, path)) {
        voiceMessage_ = QStringLiteral("语音保存失败：%1").arg(path);
        emit voiceChanged();
        return;
    }

    voiceMessage_ = QStringLiteral("语音已保存：%1").arg(path);
    emit voiceChanged();
}

void PlayerController::copyCurrentVoice() {
    if (currentVoiceFilePath_.trimmed().isEmpty() || !QFileInfo::exists(currentVoiceFilePath_)) {
        voiceMessage_ = QStringLiteral("暂无可复制的语音，请先播放一条语音");
        emit voiceChanged();
        return;
    }

    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        voiceMessage_ = QStringLiteral("复制失败：剪贴板不可用");
        emit voiceChanged();
        return;
    }

    auto *mimeData = new QMimeData();
    mimeData->setUrls({QUrl::fromLocalFile(currentVoiceFilePath_)});
    clipboard->setMimeData(mimeData);
    voiceMessage_ = QStringLiteral("已复制当前语音文件：%1").arg(currentVoiceFilePath_);
    emit voiceChanged();
}

void PlayerController::searchMusic(const QString &keyword, const QString &server) {
    const QString trimmedKeyword = keyword.trimmed();
    if (trimmedKeyword.isEmpty()) {
        musicMessage_ = QStringLiteral("请输入歌曲名、作者或关键字");
        emit musicChanged();
        return;
    }

    Q_UNUSED(server)
    const quint64 requestSerial = ++musicSearchSerial_;
    musicLoading_ = true;
    musicResults_.clear();
    musicMessage_ = QStringLiteral("正在聚合搜索音乐：%1").arg(trimmedKeyword);
    emit musicChanged();

    struct MusicSearchState {
        int pending = 3;
        QVariantList results;
        QSet<QString> seen;
        QStringList errors;
    };
    const auto state = std::make_shared<MusicSearchState>();
    auto *searchManager = new QNetworkAccessManager(this);
    const auto makeRequest = [](const QUrl &requestUrl) {
        QNetworkRequest request(requestUrl);
        request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
        request.setRawHeader("Accept", "application/json,text/plain,*/*");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        return request;
    };
    const auto submit = [this, searchManager, state, requestSerial, trimmedKeyword](
                            const QString &source, QNetworkReply *searchReply) {
        connect(searchReply, &QNetworkReply::finished, this,
                [this, searchManager, state, requestSerial, trimmedKeyword, source, searchReply]() {
            const QByteArray body = searchReply->readAll();
            if (searchReply->error() == QNetworkReply::NoError) {
                QJsonParseError parseError;
                const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
                const QJsonObject root = document.isObject() ? document.object() : QJsonObject();
                QJsonArray rows;
                if (parseError.error == QJsonParseError::NoError
                        && root.value(QStringLiteral("status")).toInt() == 200) {
                    rows = source == QStringLiteral("kuwo")
                        ? root.value(QStringLiteral("data")).toObject()
                              .value(QStringLiteral("songs")).toArray()
                        : root.value(QStringLiteral("result")).toArray();
                } else {
                    state->errors.append(QStringLiteral("%1 返回无效数据").arg(source));
                }
                for (const QJsonValue &value : rows) {
                    if (!value.isObject())
                        continue;
                    const QJsonObject item = value.toObject();
                    const QString id = source == QStringLiteral("tencent")
                        ? item.value(QStringLiteral("mid")).toVariant().toString().trimmed()
                        : source == QStringLiteral("kuwo")
                            ? item.value(QStringLiteral("rid")).toVariant().toString().trimmed()
                            : item.value(QStringLiteral("id")).toVariant().toString().trimmed();
                    const QString title = item.value(QStringLiteral("name")).toString().trimmed();
                    const QString uniqueKey = source + QLatin1Char(':') + id;
                    if (id.isEmpty() || title.isEmpty() || state->seen.contains(uniqueKey))
                        continue;
                    state->seen.insert(uniqueKey);

                    const QJsonValue artistValue = source == QStringLiteral("tencent")
                        ? item.value(QStringLiteral("singer"))
                        : source == QStringLiteral("kuwo")
                            ? item.value(QStringLiteral("artist"))
                            : item.value(QStringLiteral("artists"));
                    QString artist;
                    if (artistValue.isArray()) {
                        QStringList names;
                        for (const QJsonValue &artistItem : artistValue.toArray()) {
                            const QString name = artistItem.isObject()
                                ? artistItem.toObject().value(QStringLiteral("name")).toString().trimmed()
                                : artistItem.toString().trimmed();
                            if (!name.isEmpty())
                                names.append(name);
                        }
                        artist = names.join(QStringLiteral(" / "));
                    } else {
                        artist = artistValue.toVariant().toString().trimmed();
                    }

                    QVariantMap track;
                    track[QStringLiteral("source")] = source;
                    track[QStringLiteral("id")] = id;
                    track[QStringLiteral("title")] = title;
                    track[QStringLiteral("artist")] =
                        artist.isEmpty() ? QStringLiteral("未知歌手") : artist;
                    track[QStringLiteral("album")] =
                        item.value(QStringLiteral("album")).toVariant().toString();
                    track[QStringLiteral("pic")] = source == QStringLiteral("netease")
                        ? item.value(QStringLiteral("picUrl")).toString()
                        : item.value(QStringLiteral("pic")).toString();
                    track[QStringLiteral("url")] = QString();
                    track[QStringLiteral("lrc")] = QString();
                    state->results.append(track);
                }
            } else {
                state->errors.append(
                    QStringLiteral("%1：%2").arg(source, searchReply->errorString()));
            }
            searchReply->deleteLater();
            if (--state->pending > 0)
                return;
            searchManager->deleteLater();
            if (requestSerial != musicSearchSerial_)
                return;
            musicLoading_ = false;
            musicResults_ = state->results;
            if (!musicResults_.isEmpty()) {
                musicMessage_ = QStringLiteral("已聚合找到 %1 首“%2”相关音乐")
                                    .arg(musicResults_.size()).arg(trimmedKeyword);
            } else if (!state->errors.isEmpty()) {
                musicMessage_ = QStringLiteral("音乐搜索失败：%1")
                                    .arg(state->errors.join(QStringLiteral("；")));
            } else {
                musicMessage_ = QStringLiteral("没有搜索到“%1”的音乐").arg(trimmedKeyword);
            }
            emit musicChanged();
        });
    };

    QUrl neteaseUrl(QStringLiteral("https://music.nekofun.top/search"));
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("keywords"), trimmedKeyword);
    form.addQueryItem(QStringLiteral("limit"), QStringLiteral("20"));
    QNetworkRequest neteaseRequest = makeRequest(neteaseUrl);
    neteaseRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                             QStringLiteral("application/x-www-form-urlencoded"));
    submit(QStringLiteral("netease"),
           searchManager->post(neteaseRequest, form.toString(QUrl::FullyEncoded).toUtf8()));

    const QList<QPair<QString, QString>> getSources = {
        {QStringLiteral("tencent"), QStringLiteral("/qq/search")},
        {QStringLiteral("kuwo"), QStringLiteral("/kuwo/search")}
    };
    for (const auto &entry : getSources) {
        QUrl requestUrl(QStringLiteral("https://music.nekofun.top") + entry.second);
        QUrlQuery requestQuery;
        requestQuery.addQueryItem(QStringLiteral("keywords"), trimmedKeyword);
        requestQuery.addQueryItem(QStringLiteral("limit"), QStringLiteral("20"));
        requestUrl.setQuery(requestQuery);
        submit(entry.first, searchManager->get(makeRequest(requestUrl)));
    }
}

void PlayerController::resolveAndPlayMusic(const QString &source, const QString &id,
                                           const QString &title, const QString &artist,
                                           const QString &picUrl) {
    const QString normalizedSource = source.trimmed().toLower();
    const QString trimmedId = id.trimmed();
    if (trimmedId.isEmpty()
            || (normalizedSource != QStringLiteral("netease")
                && normalizedSource != QStringLiteral("tencent")
                && normalizedSource != QStringLiteral("kuwo"))) {
        musicMessage_ = QStringLiteral("歌曲来源或 ID 无效");
        emit musicChanged();
        return;
    }

    musicLoading_ = true;
    musicMessage_ = QStringLiteral("正在解析播放地址：%1").arg(title);
    emit musicChanged();

    QUrl url(QStringLiteral("https://music-api.gdstudio.xyz/api.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("types"), QStringLiteral("url"));
    query.addQueryItem(QStringLiteral("source"), normalizedSource);
    query.addQueryItem(QStringLiteral("id"), trimmedId);
    query.addQueryItem(QStringLiteral("br"), QStringLiteral("320"));
    url.setQuery(query);

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    request.setRawHeader("Accept", "application/json,text/plain,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, nam, reply, normalizedSource, trimmedId, title, artist, picUrl]() {
        const QByteArray body = reply->readAll();
        QString playbackUrl;
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonDocument document = QJsonDocument::fromJson(body);
            if (document.isObject())
                playbackUrl = document.object().value(QStringLiteral("url")).toString().trimmed();
            const QUrl parsed(playbackUrl);
            if (!parsed.isValid() || parsed.host().isEmpty()
                    || (parsed.scheme() != QStringLiteral("http")
                        && parsed.scheme() != QStringLiteral("https"))) {
                playbackUrl.clear();
            }
        }
        reply->deleteLater();
        nam->deleteLater();

        if (playbackUrl.isEmpty()) {
            QUrl fallback;
            QUrlQuery fallbackQuery;
            if (normalizedSource == QStringLiteral("kuwo")) {
                fallback = QUrl(QStringLiteral("https://musicapi.haitangw.net/music/kw.php"));
                fallbackQuery.addQueryItem(QStringLiteral("type"), QStringLiteral("mp3"));
                fallbackQuery.addQueryItem(QStringLiteral("id"), trimmedId);
                fallbackQuery.addQueryItem(QStringLiteral("level"), QStringLiteral("exhigh"));
            } else {
                fallback = QUrl(QStringLiteral("https://music.3e0.cn/"));
                fallbackQuery.addQueryItem(QStringLiteral("server"), normalizedSource);
                fallbackQuery.addQueryItem(QStringLiteral("type"), QStringLiteral("url"));
                fallbackQuery.addQueryItem(QStringLiteral("id"), trimmedId);
            }
            fallback.setQuery(fallbackQuery);
            playbackUrl = fallback.toString();
        }

        musicLoading_ = false;
        playMusic(playbackUrl, title, artist, QString(), picUrl);
        currentMusicSource_ = normalizedSource;
        currentMusicId_ = trimmedId;
        loadMusicLyricsForCurrentTrack();

    });
}

void PlayerController::loadMusicPlaylist(const QString &playlistId, const QString &server) {
    const QString trimmedId = playlistId.trimmed();
    if (trimmedId.isEmpty()) {
        musicMessage_ = QStringLiteral("请输入歌单 ID");
        emit musicChanged();
        return;
    }

    QUrl url(QStringLiteral("https://music.api.songziheng.com/api"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("server"), server.trimmed().isEmpty() ? QStringLiteral("netease") : server.trimmed());
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("playlist"));
    query.addQueryItem(QStringLiteral("id"), trimmedId);
    query.addQueryItem(QStringLiteral("auth"), QStringLiteral("undefined"));
    query.addQueryItem(QStringLiteral("r"), QString::number(QRandomGenerator::global()->generateDouble()));
    url.setQuery(query);

    musicLoading_ = true;
    musicMessage_ = QStringLiteral("正在加载歌单：%1").arg(trimmedId);
    emit musicChanged();

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "application/json,text/plain,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, trimmedId]() {
        reply->deleteLater();
        nam->deleteLater();
        musicLoading_ = false;

        if (reply->error() != QNetworkReply::NoError) {
            musicMessage_ = QStringLiteral("歌单加载失败：%1").arg(reply->errorString());
            emit musicChanged();
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonArray rows;
        if (doc.isArray()) {
            rows = doc.array();
        } else if (doc.isObject()) {
            const QJsonObject root = doc.object();
            const QStringList keys = {QStringLiteral("data"), QStringLiteral("playlist"), QStringLiteral("songs"), QStringLiteral("tracks")};
            for (const QString &key : keys) {
                const QJsonValue v = root.value(key);
                if (v.isArray()) {
                    rows = v.toArray();
                    break;
                }
                if (v.isObject()) {
                    const QJsonObject object = v.toObject();
                    for (const QString &nestedKey : keys) {
                        if (object.value(nestedKey).isArray()) {
                            rows = object.value(nestedKey).toArray();
                            break;
                        }
                    }
                }
                if (!rows.isEmpty())
                    break;
            }
        }

        musicResults_.clear();
        for (const QJsonValue &value : rows) {
            if (!value.isObject())
                continue;
            const QJsonObject item = value.toObject();
            QVariantMap map;
            map[QStringLiteral("title")] = item.value(QStringLiteral("title")).toString(item.value(QStringLiteral("name")).toString());
            map[QStringLiteral("artist")] = item.value(QStringLiteral("author")).toString(item.value(QStringLiteral("artist")).toString());
            map[QStringLiteral("pic")] = item.value(QStringLiteral("pic")).toString(item.value(QStringLiteral("cover")).toString());
            map[QStringLiteral("url")] = item.value(QStringLiteral("url")).toString();
            map[QStringLiteral("lrc")] = item.value(QStringLiteral("lrc")).toString();
            if (!map.value(QStringLiteral("title")).toString().trimmed().isEmpty()
                    && !map.value(QStringLiteral("url")).toString().trimmed().isEmpty()) {
                musicResults_.append(map);
            }
        }

        musicMessage_ = musicResults_.isEmpty()
            ? QStringLiteral("没有解析到歌单 %1 的歌曲").arg(trimmedId)
            : QStringLiteral("已加载歌单 %1：%2 首").arg(trimmedId).arg(musicResults_.size());
        emit musicChanged();
    });
}

void PlayerController::playMusic(const QString &url, const QString &title, const QString &artist, const QString &lrcUrl, const QString &picUrl) {
    const QString trimmedUrl = url.trimmed();
    if (trimmedUrl.isEmpty()) {
        musicMessage_ = QStringLiteral("歌曲播放地址为空");
        emit musicChanged();
        return;
    }

    if (mpvMode_) {
        mpvBackend_.stop();
        mpvMode_ = false;
        updateMpvVideoWindowVisibility();
    }
    engine_.abortPipeline();

    if (!qtPlayer_) {
        musicMessage_ = QStringLiteral("QtMediaPlayer 不可用，无法播放音乐");
        emit musicChanged();
        return;
    }

    currentMusicUrl_ = trimmedUrl;
    currentMusicTitle_ = title.trimmed().isEmpty() ? QStringLiteral("未知歌曲") : title.trimmed();
    currentMusicArtist_ = artist.trimmed().isEmpty() ? QStringLiteral("未知歌手") : artist.trimmed();
    currentMusicPic_ = picUrl.trimmed();
    currentMusicSource_.clear();
    currentMusicId_.clear();
    currentMusicLyricSource_.clear();
    currentMusicLyricTranslation_.clear();
    currentMusicLyricRomanization_.clear();
    ++musicLyricRequestSerial_;
    setCurrentMusicLrc(QString());
    musicMessage_ = QStringLiteral("正在播放：%1 - %2").arg(currentMusicTitle_, currentMusicArtist_);
    emit musicChanged();

    qtPlayer_->stop();
    qtPlayer_->setSource(QUrl(currentMusicUrl_));
    qtPlayer_->setPlaybackRate(playbackRate_);
    qtNetworkMode_ = true;
    userPausedPlayback_ = false;
    currentFile_ = currentMusicUrl_;
    positionMs_ = 0;
    durationMs_ = 0;
    isPlaying_ = true;
    isPaused_ = false;
    setLoading(false);
    emit currentFileChanged();
    emit timelineChanged();
    emit playbackStateChanged();
    qtPlayer_->play();

    const QString trimmedLrc = lrcUrl.trimmed();
    if (!trimmedLrc.isEmpty()) {
        const quint64 lyricRequestSerial = musicLyricRequestSerial_;
        const QString lyricTrackUrl = currentMusicUrl_;
        auto *nam = new QNetworkAccessManager(this);
        QNetworkRequest request{QUrl(trimmedLrc)};
        request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
        request.setRawHeader("Accept", "text/plain,*/*");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        auto *reply = nam->get(request);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, nam, lyricRequestSerial, lyricTrackUrl]() {
            const QByteArray body = reply->readAll();
            const auto error = reply->error();
            reply->deleteLater();
            nam->deleteLater();
            if (lyricRequestSerial != musicLyricRequestSerial_ || lyricTrackUrl != currentMusicUrl_)
                return;
            if (error != QNetworkReply::NoError) {
                currentMusicLyricSource_ = QStringLiteral("在线歌词加载失败");
                setCurrentMusicLrc(QStringLiteral("歌词加载失败"));
            } else {
                QString lrc = QString::fromUtf8(body).trimmed();
                if (lrc.isEmpty())
                    lrc = QStringLiteral("暂无歌词");
                currentMusicLyricSource_ = QStringLiteral("在线歌词");
                setCurrentMusicLrc(lrc);
            }
            emit musicChanged();
        });
    } else {
        setCurrentMusicLrc(QStringLiteral("暂无歌词"));
        emit musicChanged();
    }
}

void PlayerController::saveCurrentMusic() {
    const QUrl url(currentMusicUrl_);
    if (!url.isValid() || url.isEmpty()) {
        musicMessage_ = QStringLiteral("暂无可下载的音乐，请先播放一首歌曲");
        emit musicChanged();
        return;
    }

    musicMessage_ = QStringLiteral("正在下载当前音乐...");
    emit musicChanged();

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "audio/*,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, url]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            musicMessage_ = QStringLiteral("音乐下载失败：%1").arg(reply->errorString());
            emit musicChanged();
            return;
        }

        const QByteArray bytes = reply->readAll();
        if (bytes.isEmpty()) {
            musicMessage_ = QStringLiteral("音乐下载失败：内容为空");
            emit musicChanged();
            return;
        }

        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        const QString folder = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("download/music"));
        QDir().mkpath(folder);
        const QString fileName = QStringLiteral("music-%1.%2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz")),
                 audioExtension(url, contentType));
        const QString path = QDir(folder).filePath(fileName);

        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            musicMessage_ = QStringLiteral("音乐保存失败：%1").arg(path);
            emit musicChanged();
            return;
        }

        musicMessage_ = QStringLiteral("音乐已保存：%1").arg(path);
        emit musicChanged();
    });
}

void PlayerController::openMusicDownloadFolder() {
    const QString folder = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("download/music"));
    QDir().mkpath(folder);
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(folder))) {
        musicMessage_ = QStringLiteral("已打开音乐下载文件夹：%1").arg(folder);
    } else {
        musicMessage_ = QStringLiteral("打开音乐下载文件夹失败：%1").arg(folder);
    }
    emit musicChanged();
}

void PlayerController::adjustMusicLyricOffset(int deltaMs) {
    musicLyricOffsetMs_ = qBound(-120000, musicLyricOffsetMs_ + deltaMs, 120000);
    updateCurrentMusicLyricIndex();
    emit musicChanged();
}

void PlayerController::resetMusicLyricOffset() {
    musicLyricOffsetMs_ = 0;
    updateCurrentMusicLyricIndex();
    emit musicChanged();
}

void PlayerController::setMusicLyricDisplayOptions(bool showTranslation, bool showRomanization) {
    musicShowTranslation_ = showTranslation;
    musicShowRomanization_ = showRomanization;
    emit musicChanged();
}

void PlayerController::reloadCurrentMusicLyrics() {
    if (currentMusicUrl_.trimmed().isEmpty())
        return;
    ++musicLyricRequestSerial_;
    loadMusicLyricsForCurrentTrack(true);
}

QVariantList PlayerController::parseMusicLyricTrack(const QString &text, const QString &fieldName) {
    QVariantList result;
    const QRegularExpression tag(QStringLiteral("\\[(\\d{1,3}):(\\d{2})(?:[.:](\\d{1,3}))?\\]"));
    const QStringList rows = text.split(QRegularExpression(QStringLiteral("\\r?\\n")), Qt::SkipEmptyParts);
    for (const QString &raw : rows) {
        QRegularExpressionMatchIterator matches = tag.globalMatch(raw);
        QVector<qint64> times;
        while (matches.hasNext()) {
            const auto match = matches.next();
            times.append(lyricTimestampMs(match.captured(1), match.captured(2), match.captured(3)));
        }
        QString value = raw;
        value.remove(tag);
        value = value.trimmed();
        if (value.isEmpty())
            continue;
        if (times.isEmpty())
            times.append(-1);
        for (qint64 timeMs : times) {
            QVariantMap line;
            line[QStringLiteral("timeMs")] = timeMs;
            line[fieldName] = value;
            result.append(line);
        }
    }
    return result;
}

bool PlayerController::loadLocalMusicLyrics() {
    const QUrl musicUrl(currentMusicUrl_);
    if (!musicUrl.isLocalFile())
        return false;
    const QFileInfo audioInfo(musicUrl.toLocalFile());
    QFile lyricFile(audioInfo.dir().filePath(audioInfo.completeBaseName() + QStringLiteral(".lrc")));
    if (!lyricFile.open(QIODevice::ReadOnly))
        return false;
    currentMusicLyricSource_ = QStringLiteral("本地 LRC");
    currentMusicLyricTranslation_.clear();
    currentMusicLyricRomanization_.clear();
    setCurrentMusicLrc(QString::fromUtf8(lyricFile.readAll()));
    musicMessage_ = QStringLiteral("已加载本地歌词");
    emit musicChanged();
    return true;
}

void PlayerController::loadMusicLyricsForCurrentTrack(bool forceRefresh) {
    if (currentMusicUrl_.trimmed().isEmpty())
        return;
    if (loadLocalMusicLyrics())
        return;
    if (currentMusicSource_.isEmpty() || currentMusicId_.isEmpty()) {
        currentMusicLyricSource_ = QStringLiteral("未匹配");
        setCurrentMusicLrc(QStringLiteral("暂无歌词"));
        emit musicChanged();
        return;
    }

    const quint64 requestSerial = ++musicLyricRequestSerial_;
    const QString trackUrl = currentMusicUrl_;
    const QString title = currentMusicTitle_.trimmed();
    const QString artist = currentMusicArtist_.trimmed();
    QVector<QPair<QString, QString>> candidates;
    candidates.append({currentMusicSource_, currentMusicId_});
    for (const QVariant &value : musicResults_) {
        const QVariantMap track = value.toMap();
        const QString source = track.value(QStringLiteral("source")).toString().trimmed().toLower();
        const QString id = track.value(QStringLiteral("id")).toString().trimmed();
        const QString candidateTitle = track.value(QStringLiteral("title")).toString().trimmed();
        const QString candidateArtist = track.value(QStringLiteral("artist")).toString().trimmed();
        if (source.isEmpty() || id.isEmpty() || (source == currentMusicSource_ && id == currentMusicId_))
            continue;
        const bool titleMatches = !title.isEmpty() && candidateTitle.compare(title, Qt::CaseInsensitive) == 0;
        const bool artistMatches = artist.isEmpty() || candidateArtist.isEmpty()
            || candidateArtist.contains(artist, Qt::CaseInsensitive)
            || artist.contains(candidateArtist, Qt::CaseInsensitive);
        if (titleMatches && artistMatches
                && !candidates.contains(qMakePair(source, id)))
            candidates.append({source, id});
        if (candidates.size() >= 8)
            break;
    }

    musicMessage_ = forceRefresh ? QStringLiteral("正在重新匹配歌词...") : QStringLiteral("正在加载歌词...");
    currentMusicLyricSource_ = QStringLiteral("匹配中");
    setCurrentMusicLrc(QString());
    emit musicChanged();

    const QString cacheDir = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("music-lyrics"));
    QDir().mkpath(cacheDir);
    const auto applyLyrics = [this](const QString &source, const QString &lrc,
                                                const QString &translation, const QString &romanization,
                                                const QString &cachePath, const QString &suffix) {
        currentMusicLyricTranslation_ = translation;
        currentMusicLyricRomanization_ = romanization;
        currentMusicLyricSource_ = source + suffix;
        QJsonObject cached;
        cached[QStringLiteral("savedAt")] = QDateTime::currentMSecsSinceEpoch();
        cached[QStringLiteral("source")] = source;
        cached[QStringLiteral("lrc")] = lrc;
        cached[QStringLiteral("translation")] = translation;
        cached[QStringLiteral("romanization")] = romanization;
        QSaveFile cacheFile(cachePath);
        if (cacheFile.open(QIODevice::WriteOnly)) {
            cacheFile.write(QJsonDocument(cached).toJson(QJsonDocument::Compact));
            cacheFile.commit();
        }
        setCurrentMusicLrc(lrc);
        musicMessage_ = QStringLiteral("歌词已加载");
        emit musicChanged();
    };

    auto fetchCandidate = std::make_shared<std::function<void(int)>>();
    const std::weak_ptr<std::function<void(int)>> weakFetchCandidate = fetchCandidate;
    *fetchCandidate = [this, weakFetchCandidate, candidates, forceRefresh, requestSerial, trackUrl,
                       cacheDir, applyLyrics](int index) {
        const auto continueFetch = weakFetchCandidate.lock();
        if (!continueFetch)
            return;
        if (requestSerial != musicLyricRequestSerial_ || trackUrl != currentMusicUrl_)
            return;
        if (index >= candidates.size()) {
            currentMusicLyricSource_ = QStringLiteral("未匹配");
            currentMusicLyricTranslation_.clear();
            currentMusicLyricRomanization_.clear();
            setCurrentMusicLrc(QStringLiteral("暂无歌词"));
            musicMessage_ = QStringLiteral("没有匹配到歌词");
            emit musicChanged();
            return;
        }

        const QString source = candidates.at(index).first;
        const QString id = candidates.at(index).second;
        const QString cacheKey = QString::fromLatin1(QCryptographicHash::hash(
            (source + QLatin1Char('\n') + id + QLatin1Char('\n')
             + currentMusicTitle_ + QLatin1Char('\n') + currentMusicArtist_).toUtf8(),
            QCryptographicHash::Sha256).toHex());
        const QString cachePath = QDir(cacheDir).filePath(cacheKey + QStringLiteral(".json"));

        if (!forceRefresh) {
            QFile cacheFile(cachePath);
            if (cacheFile.open(QIODevice::ReadOnly)) {
                const QJsonObject cached = QJsonDocument::fromJson(cacheFile.readAll()).object();
                const qint64 savedAt = cached.value(QStringLiteral("savedAt")).toVariant().toLongLong();
                const QString lrc = cached.value(QStringLiteral("lrc")).toString().trimmed();
                if (!lrc.isEmpty() && QDateTime::currentMSecsSinceEpoch() - savedAt <= 30LL * 24 * 60 * 60 * 1000) {
                    applyLyrics(source, lrc, cached.value(QStringLiteral("translation")).toString(),
                                cached.value(QStringLiteral("romanization")).toString(), cachePath,
                                QStringLiteral(" · 缓存"));
                    return;
                }
            }
        }

        QUrl requestUrl(QStringLiteral("https://music-api.gdstudio.xyz/api.php"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("types"), QStringLiteral("lrc"));
        query.addQueryItem(QStringLiteral("source"), source);
        query.addQueryItem(QStringLiteral("id"), id);
        requestUrl.setQuery(query);
        auto *manager = new QNetworkAccessManager(this);
        QNetworkRequest request(requestUrl);
        request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
        request.setRawHeader("Accept", "application/json,text/plain,*/*");
        auto *reply = manager->get(request);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, manager, source, cachePath, index, continueFetch,
                 requestSerial, trackUrl, applyLyrics]() {
            const QByteArray body = reply->readAll();
            const auto error = reply->error();
            reply->deleteLater();
            manager->deleteLater();
            if (requestSerial != musicLyricRequestSerial_ || trackUrl != currentMusicUrl_)
                return;
            QString lrc;
            QString translation;
            QString romanization;
            if (error == QNetworkReply::NoError) {
                const QJsonDocument document = QJsonDocument::fromJson(body);
                if (document.isObject()) {
                    const QJsonObject object = document.object();
                    lrc = object.value(QStringLiteral("lrc")).toString();
                    if (lrc.isEmpty()) lrc = object.value(QStringLiteral("lyric")).toString();
                    if (lrc.isEmpty()) lrc = object.value(QStringLiteral("data")).toString();
                    translation = object.value(QStringLiteral("tlyric")).toString();
                    if (translation.isEmpty()) translation = object.value(QStringLiteral("translation")).toString();
                    romanization = object.value(QStringLiteral("rlyric")).toString();
                    if (romanization.isEmpty()) romanization = object.value(QStringLiteral("romanization")).toString();
                } else {
                    lrc = QString::fromUtf8(body).trimmed();
                }
            }
            if (!lrc.trimmed().isEmpty()) {
                applyLyrics(source, lrc, translation, romanization, cachePath, QStringLiteral(" · 在线"));
                return;
            }
            (*continueFetch)(index + 1);
        });
    };
    (*fetchCandidate)(0);
}
void PlayerController::setCurrentMusicLrc(const QString &lrc) {
    currentMusicLrc_ = lrc.trimmed();
    currentMusicLyricLines_.clear();
    currentMusicLyricTimes_.clear();
    currentMusicLyricIndex_ = -1;

    QVariantList original = parseMusicLyricTrack(currentMusicLrc_, QStringLiteral("text"));
    const QVariantList translations = parseMusicLyricTrack(
        currentMusicLyricTranslation_, QStringLiteral("translation"));
    const QVariantList romanizations = parseMusicLyricTrack(
        currentMusicLyricRomanization_, QStringLiteral("romanization"));

    const auto mergeTrack = [](QVariantList &target, const QVariantList &extra,
                               const QString &fieldName) {
        for (const QVariant &value : extra) {
            const QVariantMap extraLine = value.toMap();
            const qint64 extraTime = extraLine.value(QStringLiteral("timeMs")).toLongLong();
            int bestIndex = -1;
            qint64 bestDistance = 301;
            for (int i = 0; i < target.size(); ++i) {
                const qint64 targetTime = target.at(i).toMap()
                                                .value(QStringLiteral("timeMs")).toLongLong();
                if (extraTime < 0 || targetTime < 0)
                    continue;
                const qint64 distance = qAbs(targetTime - extraTime);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestIndex = i;
                }
            }
            if (bestIndex >= 0) {
                QVariantMap merged = target.at(bestIndex).toMap();
                merged[fieldName] = extraLine.value(fieldName);
                target[bestIndex] = merged;
            }
        }
    };
    mergeTrack(original, translations, QStringLiteral("translation"));
    mergeTrack(original, romanizations, QStringLiteral("romanization"));

    QVariantList timedLines;
    QVariantList untimedLines;
    for (const QVariant &value : std::as_const(original)) {
        if (value.toMap().value(QStringLiteral("timeMs")).toLongLong() >= 0)
            timedLines.append(value);
        else
            untimedLines.append(value);
    }
    std::sort(timedLines.begin(), timedLines.end(), [](const QVariant &left, const QVariant &right) {
        return left.toMap().value(QStringLiteral("timeMs")).toLongLong()
            < right.toMap().value(QStringLiteral("timeMs")).toLongLong();
    });
    currentMusicLyricLines_ = timedLines;
    for (const QVariant &value : std::as_const(timedLines)) {
        currentMusicLyricTimes_.append(
            value.toMap().value(QStringLiteral("timeMs")).toLongLong());
    }
    currentMusicLyricLines_.append(untimedLines);
    updateCurrentMusicLyricIndex();
}
void PlayerController::updateCurrentMusicLyricIndex() {
    int nextIndex = -1;
    if (!currentMusicLyricTimes_.isEmpty()) {
        for (int i = 0; i < currentMusicLyricTimes_.size(); ++i) {
            if (currentMusicLyricTimes_.at(i) <= positionMs_ + musicLyricOffsetMs_)
                nextIndex = i;
            else
                break;
        }
    }

    if (nextIndex == currentMusicLyricIndex_)
        return;

    currentMusicLyricIndex_ = nextIndex;
    emit musicLyricIndexChanged();
}

QString PlayerController::audioExtension(const QUrl &url, const QString &contentType) {
    const QString type = contentType.toLower();
    if (type.contains(QStringLiteral("mpeg")) || type.contains(QStringLiteral("mp3")))
        return QStringLiteral("mp3");
    if (type.contains(QStringLiteral("wav")))
        return QStringLiteral("wav");
    if (type.contains(QStringLiteral("ogg")))
        return QStringLiteral("ogg");
    if (type.contains(QStringLiteral("aac")))
        return QStringLiteral("aac");
    if (type.contains(QStringLiteral("flac")))
        return QStringLiteral("flac");
    if (type.contains(QStringLiteral("m4a")) || type.contains(QStringLiteral("mp4")))
        return QStringLiteral("m4a");

    const QString suffix = QFileInfo(url.path()).suffix().toLower();
    if (!suffix.isEmpty() && suffix.length() <= 5)
        return suffix;
    return QStringLiteral("mp3");
}

void PlayerController::loadHotNews(const QString &type) {
    static const QSet<QString> allowedTypes = {
        QStringLiteral("baidu"),
        QStringLiteral("tieba"),
        QStringLiteral("zhihu"),
        QStringLiteral("weibo"),
        QStringLiteral("douyin"),
        QStringLiteral("bilihot"),
        QStringLiteral("sspai"),
        QStringLiteral("biliall"),
        QStringLiteral("history")
    };
    const QString trimmedType = type.trimmed();
    const QString normalizedType = allowedTypes.contains(trimmedType) ? trimmedType : QStringLiteral("baidu");

    QUrl url(QStringLiteral("http://api.yujn.cn/api/hotlist.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("type"), normalizedType);
    query.addQueryItem(QStringLiteral("_t"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.setQuery(query);

    hotNewsLoading_ = true;
    hotNewsMessage_ = QStringLiteral("正在加载热讯...");
    emit hotNewsChanged();

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "application/json,text/plain,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, normalizedType]() {
        reply->deleteLater();
        nam->deleteLater();
        hotNewsLoading_ = false;

        if (reply->error() != QNetworkReply::NoError) {
            hotNewsMessage_ = QStringLiteral("热讯请求失败：%1").arg(reply->errorString());
            emit hotNewsChanged();
            return;
        }

        const QByteArray bytes = reply->readAll();
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
        QJsonArray rows;
        if (parseError.error == QJsonParseError::NoError) {
            if (doc.isArray()) {
                rows = doc.array();
            } else if (doc.isObject()) {
                const QJsonObject root = doc.object();
                const QStringList keys = {
                    QStringLiteral("data"),
                    QStringLiteral("list"),
                    QStringLiteral("result"),
                    QStringLiteral("news"),
                    QStringLiteral("items")
                };
                for (const QString &key : keys) {
                    const QJsonValue rootValue = root.value(key);
                    if (rootValue.isArray()) {
                        rows = rootValue.toArray();
                        break;
                    }
                    if (rootValue.isObject()) {
                        const QJsonObject nested = rootValue.toObject();
                        for (const QString &nestedKey : keys) {
                            if (nested.value(nestedKey).isArray()) {
                                rows = nested.value(nestedKey).toArray();
                                break;
                            }
                        }
                    }
                    if (!rows.isEmpty()) {
                        break;
                    }
                }
            }
        }

        hotNewsItems_.clear();
        int rank = 1;
        for (const QJsonValue &value : rows) {
            if (!value.isObject()) {
                continue;
            }
            const QJsonObject item = value.toObject();
            const QString title = item.value(QStringLiteral("title")).toString(
                item.value(QStringLiteral("name")).toString(
                    item.value(QStringLiteral("word")).toString(
                        item.value(QStringLiteral("query")).toString())));
            if (title.trimmed().isEmpty()) {
                continue;
            }

            QVariantMap map;
            map[QStringLiteral("rank")] = rank++;
            map[QStringLiteral("title")] = title.trimmed();
            map[QStringLiteral("url")] = item.value(QStringLiteral("url")).toString(
                item.value(QStringLiteral("link")).toString(
                    item.value(QStringLiteral("mobileUrl")).toString(
                        item.value(QStringLiteral("mobilUrl")).toString())));
            map[QStringLiteral("hot")] = item.value(QStringLiteral("hot")).toString(
                item.value(QStringLiteral("desc")).toString(
                    item.value(QStringLiteral("score")).toString(
                        item.value(QStringLiteral("num")).toString())));
            hotNewsItems_.append(map);
        }

        hotNewsMessage_ = hotNewsItems_.isEmpty()
            ? QStringLiteral("没有解析到热讯数据")
            : QStringLiteral("已加载 %1 条 %2 热讯").arg(hotNewsItems_.size()).arg(normalizedType);
        emit hotNewsChanged();
    });
}

QString PlayerController::localImageFileUrl(const QString &path) {
    if (path.trimmed().isEmpty()) return {};
    return QUrl::fromLocalFile(QDir::cleanPath(path)).toString();
}

bool PlayerController::loadImageBytesFromLocalUrl(const QString &url) {
    const QUrl qurl(url);
    if (!qurl.isLocalFile()) return false;
    QFile file(qurl.toLocalFile());
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty()) return false;
    currentImageBytes_ = bytes;
    currentImageContentType_ = ImageCacheService::mimeFromExtension(QFileInfo(file).suffix());
    return true;
}

QString PlayerController::cacheImageBytes(const QString &apiUrl, const QString &sourceUrl,
                                          const QByteArray &bytes, const QString &mimeType) {
    return imageCache_.put(apiUrl, sourceUrl, bytes, mimeType);
}

void PlayerController::saveImageBytes(const QByteArray &bytes, const QUrl &sourceUrl, const QString &contentType) {
    if (bytes.isEmpty()) {
        setImageState(false, currentImageUrl_, QStringLiteral("图片内容为空，无法保存"));
        return;
    }

    const QString folder = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("download/image"));
    QDir().mkpath(folder);
    const QString fileName = QStringLiteral("image-%1.%2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz")),
             imageExtension(sourceUrl, contentType));
    const QString path = QDir(folder).filePath(fileName);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setImageState(false, currentImageUrl_, QStringLiteral("保存失败：%1").arg(file.errorString()));
        return;
    }
    file.write(bytes);
    file.close();
    currentImageBytes_ = bytes;
    currentImageContentType_ = contentType;
    setImageState(false, currentImageUrl_, QStringLiteral("已保存：%1").arg(path));
}

QString PlayerController::extractImageUrlFromResponse(const QByteArray &bytes, const QUrl &responseUrl, const QString &contentType) {
    if (contentType.startsWith(QStringLiteral("image/"), Qt::CaseInsensitive))
        return responseUrl.toString();

    const auto resolveUrl = [&responseUrl](const QString &raw) {
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty())
            return QString();
        const QUrl parsed(trimmed);
        if (parsed.isValid() && !parsed.isRelative())
            return trimmed;
        return responseUrl.resolved(parsed).toString();
    };

    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (doc.isObject()) {
        const QJsonObject root = doc.object();
        const QStringList topKeys = {QStringLiteral("url"), QStringLiteral("link"), QStringLiteral("image"), QStringLiteral("img"), QStringLiteral("imgurl")};
        for (const auto &key : topKeys) {
            const QString value = root.value(key).toString();
            if (!value.trimmed().isEmpty())
                return resolveUrl(value);
        }

        const QJsonValue data = root.value(QStringLiteral("data"));
        if (data.isString())
            return resolveUrl(data.toString());
        if (data.isObject()) {
            const QJsonObject object = data.toObject();
            for (const auto &key : topKeys) {
                const QString value = object.value(key).toString();
                if (!value.trimmed().isEmpty())
                    return resolveUrl(value);
            }
            const QJsonObject urls = object.value(QStringLiteral("urls")).toObject();
            const QString original = urls.value(QStringLiteral("original")).toString();
            if (!original.trimmed().isEmpty())
                return resolveUrl(original);
        }
        if (data.isArray() && !data.toArray().isEmpty()) {
            const QJsonValue first = data.toArray().first();
            if (first.isString())
                return resolveUrl(first.toString());
            if (first.isObject()) {
                const QJsonObject object = first.toObject();
                for (const auto &key : topKeys) {
                    const QString value = object.value(key).toString();
                    if (!value.trimmed().isEmpty())
                        return resolveUrl(value);
                }
                const QString original = object.value(QStringLiteral("urls")).toObject().value(QStringLiteral("original")).toString();
                if (!original.trimmed().isEmpty())
                    return resolveUrl(original);
            }
        }
    }

    const QString text = QString::fromUtf8(bytes).trimmed();
    static const QRegularExpression xmlUrl(QStringLiteral("<(?:url|link|image|img)>\\s*([^<]+)\\s*</(?:url|link|image|img)>"),
                                           QRegularExpression::CaseInsensitiveOption);
    const auto match = xmlUrl.match(text);
    if (match.hasMatch())
        return resolveUrl(match.captured(1));

    if (text.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
            || text.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
        return resolveUrl(text);

    return {};
}

QStringList PlayerController::extractImageUrlsFromResponse(const QByteArray &bytes, const QUrl &responseUrl, const QString &contentType) {
    QStringList urls;
    const auto appendUrl = [&urls, &responseUrl](const QString &raw) {
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty())
            return;
        QUrl parsed(trimmed);
        const QString resolved = (parsed.isValid() && !parsed.isRelative())
            ? trimmed
            : responseUrl.resolved(parsed).toString();
        if (!resolved.trimmed().isEmpty() && !urls.contains(resolved))
            urls.append(resolved);
    };

    if (contentType.startsWith(QStringLiteral("image/"), Qt::CaseInsensitive)) {
        appendUrl(responseUrl.toString());
        return urls;
    }

    const std::function<void(const QJsonValue &)> collectJson = [&](const QJsonValue &value) {
        if (value.isString()) {
            const QString text = value.toString();
            if (text.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
                    || text.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)
                    || text.contains(QRegularExpression(QStringLiteral("\\.(?:jpg|jpeg|png|gif|webp)(?:$|[?#])"),
                                                        QRegularExpression::CaseInsensitiveOption))) {
                appendUrl(text);
            }
            return;
        }
        if (value.isArray()) {
            for (const QJsonValue &item : value.toArray())
                collectJson(item);
            return;
        }
        if (value.isObject()) {
            const QJsonObject object = value.toObject();
            for (const QString &key : object.keys())
                collectJson(object.value(key));
        }
    };

    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (!doc.isNull()) {
        collectJson(doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object()));
        if (!urls.isEmpty())
            return urls;
    }

    const QString text = QString::fromUtf8(bytes);
    static const QRegularExpression imageUrlRe(
        QStringLiteral("https?://[^\\s\"'<>]+\\.(?:jpg|jpeg|png|gif|webp)(?:[^\\s\"'<>]*)?"),
        QRegularExpression::CaseInsensitiveOption);
    auto match = imageUrlRe.globalMatch(text);
    while (match.hasNext())
        appendUrl(match.next().captured(0));

    if (urls.isEmpty()) {
        const QString trimmed = text.trimmed();
        if (trimmed.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
                || trimmed.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
            appendUrl(trimmed);
        }
    }

    return urls;
}

QString PlayerController::imageExtension(const QUrl &url, const QString &contentType) {
    const QString type = contentType.toLower();
    if (type.contains(QStringLiteral("webp"))) return QStringLiteral("webp");
    if (type.contains(QStringLiteral("png"))) return QStringLiteral("png");
    if (type.contains(QStringLiteral("gif"))) return QStringLiteral("gif");
    if (type.contains(QStringLiteral("bmp"))) return QStringLiteral("bmp");
    if (type.contains(QStringLiteral("jpeg")) || type.contains(QStringLiteral("jpg"))) return QStringLiteral("jpg");

    const QString suffix = QFileInfo(url.path()).suffix().toLower();
    if (!suffix.isEmpty() && suffix.size() <= 5)
        return suffix;
    return QStringLiteral("jpg");
}

void PlayerController::setUpdateState(bool checking, bool downloading, bool available, const QString &message) {
    bool changed = false;
    if (updateChecking_ != checking) {
        updateChecking_ = checking;
        changed = true;
    }
    if (updateDownloading_ != downloading) {
        updateDownloading_ = downloading;
        changed = true;
    }
    if (updateAvailable_ != available) {
        updateAvailable_ = available;
        changed = true;
    }
    if (updateMessage_ != message) {
        updateMessage_ = message;
        changed = true;
    }
    if (changed)
        emit updateStateChanged();
}

int PlayerController::compareVersions(const QString &left, const QString &right) {
    const QStringList leftParts = normalizeVersion(left).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const QStringList rightParts = normalizeVersion(right).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const int count = qMax(leftParts.size(), rightParts.size());
    for (int i = 0; i < count; ++i) {
        const int leftValue = i < leftParts.size() ? leftParts.at(i).toInt() : 0;
        const int rightValue = i < rightParts.size() ? rightParts.at(i).toInt() : 0;
        if (leftValue < rightValue) return -1;
        if (leftValue > rightValue) return 1;
    }
    return 0;
}

void PlayerController::checkForUpdates() {
    if (updateChecking_ || updateDownloading_)
        return;

    const QString owner = updaterOwner();
    const QString repo = updaterRepo();
    if (owner.isEmpty() || repo.isEmpty()) {
        setUpdateState(false, false, false, QStringLiteral("未配置 GitHub 更新源"));
        return;
    }

    updateAssetUrl_.clear();
    updateAssetName_.clear();
    updateVersion_.clear();
    updateDownloadProgress_ = 0.0;
    emit updateStateChanged();
    setUpdateState(true, false, false, QStringLiteral("正在检查更新..."));

    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest").arg(owner, repo));
    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", QByteArrayLiteral("MeloBox-Updater/1.0"));
    request.setRawHeader("Accept", QByteArrayLiteral("application/vnd.github+json"));

    QNetworkReply *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            setUpdateState(false, false, false, QStringLiteral("检查更新失败：%1").arg(reply->errorString()));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            setUpdateState(false, false, false, QStringLiteral("更新响应格式无效"));
            return;
        }

        const QJsonObject root = doc.object();
        const QString latestVersion = normalizeVersion(root.value(QStringLiteral("tag_name")).toString(
            root.value(QStringLiteral("name")).toString()));
        if (latestVersion.isEmpty()) {
            setUpdateState(false, false, false, QStringLiteral("没有读取到最新版本号"));
            return;
        }

        QString assetUrl;
        QString assetName;
        const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
        for (const QJsonValue &value : assets) {
            const QJsonObject asset = value.toObject();
            const QString name = asset.value(QStringLiteral("name")).toString();
            if (!name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
                continue;
            if (!name.contains(QStringLiteral("MeloBox"), Qt::CaseInsensitive))
                continue;
            assetName = name;
            assetUrl = asset.value(QStringLiteral("browser_download_url")).toString();
            break;
        }

        if (compareVersions(appVersion(), latestVersion) >= 0) {
            updateVersion_ = latestVersion;
            setUpdateState(false, false, false, QStringLiteral("当前已是最新版本：%1").arg(appVersion()));
            return;
        }

        if (assetUrl.isEmpty()) {
            updateVersion_ = latestVersion;
            setUpdateState(false, false, false, QStringLiteral("发现新版本 %1，但没有找到安装包").arg(latestVersion));
            return;
        }

        updateVersion_ = latestVersion;
        updateAssetUrl_ = assetUrl;
        updateAssetName_ = assetName.isEmpty()
            ? QStringLiteral("MeloBox-Setup-%1.exe").arg(latestVersion)
            : assetName;
        setUpdateState(false, false, true, QStringLiteral("发现新版本：%1").arg(latestVersion));
    });
}

void PlayerController::downloadAndInstallUpdate() {
    if (!updateAvailable_ || updateAssetUrl_.isEmpty() || updateDownloading_)
        return;

    updateDownloadProgress_ = 0.0;
    emit updateStateChanged();
    setUpdateState(false, true, true, QStringLiteral("正在下载更新..."));

    QString folder = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (folder.isEmpty())
        folder = QCoreApplication::applicationDirPath();
    folder = QDir(folder).filePath(QStringLiteral("MeloBox-update"));
    QDir().mkpath(folder);

    const QString fileName = updateAssetName_.isEmpty() ? QStringLiteral("MeloBox-Setup.exe") : updateAssetName_;
    const QString installerPath = QDir(folder).filePath(fileName);

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(updateAssetUrl_)};
    request.setRawHeader("User-Agent", QByteArrayLiteral("MeloBox-Updater/1.0"));
    QNetworkReply *reply = nam->get(request);

    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total > 0) {
            updateDownloadProgress_ = qBound(0.0, static_cast<double>(received) / static_cast<double>(total), 1.0);
            emit updateStateChanged();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, installerPath]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            setUpdateState(false, false, updateAvailable_, QStringLiteral("下载更新失败：%1").arg(reply->errorString()));
            return;
        }

        QFile file(installerPath);
        if (!file.open(QIODevice::WriteOnly)) {
            setUpdateState(false, false, updateAvailable_, QStringLiteral("保存更新失败：%1").arg(file.errorString()));
            return;
        }
        file.write(reply->readAll());
        file.close();

        updateDownloadProgress_ = 1.0;
        emit updateStateChanged();
        setUpdateState(false, false, true, QStringLiteral("更新已下载，正在启动安装包..."));
        stop();
        if (!QProcess::startDetached(installerPath, QStringList{})) {
            setUpdateState(false, false, true, QStringLiteral("安装包启动失败：%1").arg(installerPath));
        }
    });
}
