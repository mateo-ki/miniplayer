#include "controller/PlayerController.h"

#include "app/AppServices.h"
#include "infrastructure/PoolApi.h"

#include "common/MediaUtils.h"
#include "controller/VoiceController.h"
#include "controller/UpdateController.h"

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

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

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
// 片头/片尾跳过秒数上限: 20 分钟, 防止误输入把整集吃掉。
constexpr int kSkipMarkerMaxMs = 20 * 60 * 1000;

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



QString normalizeVersion(QString version) {
    version = version.trimmed();
    if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        version.remove(0, 1);
    return version;
}
}

PlayerController::PlayerController(AppServices *services, QObject *parent)
    : QObject(parent), services_(services) {
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
        if (position > 0) {
            hasFirstFrame_ = true;
        }
        if (loading_ && isPlaying_ && hasFirstFrame_) {
            setLoading(false);
        }
        positionMs_ = position;
        applyPendingSuspendedVideoState();
        emit timelineChanged();
    });
    connect(&mpvBackend_, &MpvBackend::firstFrameReady, this, [this]() {
        if (!mpvMode_) return;
        hasFirstFrame_ = true;
        setLoading(false);
        updateMpvVideoWindowVisibility();
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
        if (buffering) {
            setLoading(true);
        } else if (hasFirstFrame_) {
            setLoading(false);
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
        hasFirstFrame_ = false;
        setLoading(false);
        mpvBuffering_ = false;
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
            hasFirstFrame_ = false;
            setLoading(false);
            mpvBuffering_ = false;
            mpvPlaybackFinished_ = true;
            isPlaying_ = false;
            isPaused_ = false;
            if (mpvVideoWindow_) {
                mpvVideoWindow_->hide();
            }
            updateMpvVideoWindowVisibility();
            emit playbackStateChanged();
       }
        if (shortVideoFailed)
            emit playbackFailed(message);
   });
    // 语音模块：取数与播放解耦——打断当前播放 + 播放缓存文件
    connect(services_->voiceController(), &VoiceController::preemptPlaybackRequested, this,
        [this]() { preemptPlaybackForLocalAudio(); });
    connect(services_->voiceController(), &VoiceController::playbackRequested, this,
        &PlayerController::playLocalAudioFile);
    // 更新器拉起安装包前停止播放
    connect(services_->updateController(), &UpdateController::installStarted, this,
        [this]() { stop(); });

    // 当前片名同步给视频目录控制器（换源搜索用）
    connect(this, &PlayerController::currentVodNameChanged, this, [this]() {
        services_->videoCatalog()->setCurrentVodName(currentVodName_);
    });
    services_->videoCatalog()->setCurrentVodName(currentVodName_);

    wireEngineSignals();
    connect(&playlistModel_, &PlaylistModel::countChanged, this, [this]() {
        emit currentIndexChanged();
    });
    connect(this, &PlayerController::repeatModeChanged, this, [this]() {
        emit currentIndexChanged();
    });
    loadSettings();
    // 片头/片尾跳过只挂这一处: 自研引擎、mpv、QMediaPlayer 三条时间线通道
    // 都发 timelineChanged，挂在这里就都覆盖到，不用每个通道各写一遍。
    connect(this, &PlayerController::timelineChanged, this, &PlayerController::applySkipMarkers);
    QTimer::singleShot(0, this, [this]() {
        services_->videoCatalog()->loadVideoList(1);
    });
    // 启动即探测 mpv 可用性: libmpv 起不来 (通常 VC++ 运行库缺失) 时立即弹
    // 安装提示, 不等用户点播放才发现。装好后无需重启, 每次播放都会重新加载。
    QTimer::singleShot(2500, this, [this]() {
        if (!mpvBackend_.ensureAvailable()) {
            Logger::instance().warn(QStringLiteral(
                "[VcRuntime] mpv unavailable at startup, prompting install"));
            notifyVcRuntimeMissing();
        } else {
            Logger::instance().info(QStringLiteral("[VcRuntime] mpv backend available"));
        }
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
QString PlayerController::currentVodName() const { return currentVodName_; }
QString PlayerController::currentShortVideoUrl() const { return currentShortVideoUrl_; }
QString PlayerController::shortVideoMessage() const { return shortVideoMessage_; }
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
QString PlayerController::appVersion() const { return QStringLiteral(MELOBOX_APP_VERSION); }

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
    Q_UNUSED(autoPlay);
    if (opening_) return;
    opening_ = true;

    // 本地文件同样只走 mpv: 自研 FFmpeg 回退管线已移除 (见 playVideoInput)。
    playlistModel_.clear();
    playlistModel_.addFile(path);
    currentIndex_ = 0;
    currentFile_ = path;
    recordPlaybackHistory(path, playlistModel_.titleAt(0));
    saveSettings();
    emit currentIndexChanged();
    opening_ = false;
    playVideoUrl(path);
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

    // ***的集数地址需要异步解析，列表模型先保存占位地址；
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
    if (lower.startsWith(QStringLiteral("http://")) || lower.startsWith(QStringLiteral("https://"))
        || !QFileInfo::exists(path)) {
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
    if (lower.startsWith(QStringLiteral("http://")) || lower.startsWith(QStringLiteral("https://"))
        || !QFileInfo::exists(path)) {
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
    services_->videoCatalog()->sourceSearchModel()->clear();
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

void PlayerController::prepareExit() {
    cancelHlsFilterRequest();
    removeSanitizedHlsPlaylist();
    if (hlsPlaylistProxy_)
        hlsPlaylistProxy_->stop();
    // 新的播放目标与当前弹幕 token 无关联时清空（dongpian 拉取会在 resolve 后重设）
    if (mpvMode_)
        mpvBackend_.stop();
    if (qtPlayer_) {
        qtPlayer_->stop();
        qtPlayer_->setSource({});
    }
    engine_.shutdownFast();
    saveSettings();
    Logger::instance().info("Exit: playback shut down (fast path)");
}

void PlayerController::stop() {
    cancelHlsFilterRequest();
    removeSanitizedHlsPlaylist();
    userPausedPlayback_ = false;
    endHoldSpeed();
    pendingSeekDeltaMs_ = 0;
    if (seekDebounceTimer_)
        seekDebounceTimer_->stop();
    if (hlsPlaylistProxy_)
        hlsPlaylistProxy_->stop();
    mpvBuffering_ = false;
    hasFirstFrame_ = false;
    setLoading(false);
    if (mpvVideoWindow_) {
        mpvVideoWindow_->hide();
    }
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

void PlayerController::beginHoldSpeed() {
    if (holdSpeedActive_ || currentFile_.isEmpty()) return;
    rateBeforeHold_ = playbackRate_;
    holdSpeedActive_ = true;
    setPlaybackRate(3.0f);
    Logger::instance().info("Long press: speed up to 3.0x");
    emit holdSpeedActiveChanged();
}

void PlayerController::endHoldSpeed() {
    if (!holdSpeedActive_) return;
    holdSpeedActive_ = false;
    setPlaybackRate(rateBeforeHold_);
    Logger::instance().info(QStringLiteral("Long press ended: restored speed to %1x").arg(rateBeforeHold_));
    emit holdSpeedActiveChanged();
}

void PlayerController::seekRelative(qint64 deltaMs) {
    if (durationMs_ <= 0) return;

    if (!seekDebounceTimer_) {
        seekDebounceTimer_ = new QTimer(this);
        seekDebounceTimer_->setSingleShot(true);
        seekDebounceTimer_->setInterval(250);
        connect(seekDebounceTimer_, &QTimer::timeout, this, [this]() {
            if (durationMs_ <= 0) {
                pendingSeekDeltaMs_ = 0;
                return;
            }
            const qint64 target = qBound<qint64>(0, positionMs_ + pendingSeekDeltaMs_, durationMs_);
            pendingSeekDeltaMs_ = 0;
            seek(target);
        });
    }

    pendingSeekDeltaMs_ += deltaMs;
    const qint64 previewTarget = qBound<qint64>(0, positionMs_ + pendingSeekDeltaMs_, durationMs_);
    seekDebounceTimer_->start();

    emit seekFeedbackRequested(pendingSeekDeltaMs_, previewTarget, durationMs_);
}

void PlayerController::setRepeatMode(RepeatMode mode) {
    if (repeatMode_ == mode) return;
    repeatMode_ = mode;
    Logger::instance().info("Repeat mode changed to " + QString::number(static_cast<int>(mode)));
    emit repeatModeChanged();
}

int PlayerController::skipIntroMs() const {
    return skipIntroMs_;
}

int PlayerController::skipOutroMs() const {
    return skipOutroMs_;
}

void PlayerController::setSkipIntroMs(int ms) {
    const int bounded = qBound(0, ms, kSkipMarkerMaxMs);
    if (skipIntroMs_ == bounded) return;
    skipIntroMs_ = bounded;
    Logger::instance().info("跳过片头改为 " + QString::number(bounded) + "ms");
    saveSettings();
    emit skipMarkersChanged();
}

void PlayerController::setSkipOutroMs(int ms) {
    const int bounded = qBound(0, ms, kSkipMarkerMaxMs);
    if (skipOutroMs_ == bounded) return;
    skipOutroMs_ = bounded;
    Logger::instance().info("跳过片尾改为 " + QString::number(bounded) + "ms");
    saveSettings();
    emit skipMarkersChanged();
}

PlayerController::SkipAction PlayerController::skipAction(qint64 positionMs, qint64 durationMs,
                                                         int introMs, int outroMs,
                                                         bool introDone, bool outroDone) {
    if (durationMs <= 0)
        return SkipAction::None;
    // 片头设在不到时长一半处: 小视频/预览片不该被推进后半段。
    if (introMs > 0 && !introDone && positionMs < introMs && introMs * 2 < durationMs)
        return SkipAction::JumpIntro;
    if (outroMs > 0 && !outroDone && positionMs >= durationMs - outroMs)
        return SkipAction::FinishEpisode;
    return SkipAction::None;
}

void PlayerController::applySkipMarkers() {
    if (!currentShortVideoUrl_.isEmpty() || !currentMusicUrl_.isEmpty())
        return;
    if (skipIntroMs_ <= 0 && skipOutroMs_ <= 0)
        return;
    if (seeking_ || pendingSeekMs_ >= 0) // 用户正在拖进度条, 不抢
        return;

    switch (skipAction(positionMs_, durationMs_, skipIntroMs_, skipOutroMs_,
                       skipIntroDone_, skipOutroDone_)) {
    case SkipAction::None:
        break;
    case SkipAction::JumpIntro:
        skipIntroDone_ = true;
        Logger::instance().info(QStringLiteral("跳过片头: %1 -> %2ms").arg(positionMs_).arg(skipIntroMs_));
        seek(skipIntroMs_);
        break;
    case SkipAction::FinishEpisode:
        skipOutroDone_ = true;
        Logger::instance().info(QStringLiteral("跳过片尾: pos=%1 duration=%2ms").arg(positionMs_).arg(durationMs_));
        handleEof();
        break;
    }
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
    if (next >= count) {
        if (repeatMode_ == RepeatAll)
            next = 0;
        else
            return;
    }
    playFromPlaylist(next);
}

void PlayerController::playPrevious() {
    int count = playlistModel_.count();
    if (count == 0) return;
    int prev = currentIndex_ - 1;
    if (prev < 0) {
        if (repeatMode_ == RepeatAll)
            prev = count - 1;
        else
            return;
    }
    playFromPlaylist(prev);
}

bool PlayerController::canPlayPrevious() const {
    const int count = playlistModel_.count();
    if (count <= 1) return false;
    return currentIndex_ > 0 || repeatMode_ == RepeatAll;
}

bool PlayerController::canPlayNext() const {
    const int count = playlistModel_.count();
    if (count <= 1) return false;
    return (currentIndex_ >= 0 && currentIndex_ + 1 < count) || repeatMode_ == RepeatAll;
}

QString PlayerController::previousEpisodeTitle() const {
    const int count = playlistModel_.count();
    if (count <= 1) return {};
    int prev = currentIndex_ - 1;
    if (prev < 0) {
        if (repeatMode_ == RepeatAll) prev = count - 1;
        else return {};
    }
    return playlistModel_.titleAt(prev);
}

QString PlayerController::nextEpisodeTitle() const {
    const int count = playlistModel_.count();
    if (count <= 1) return {};
    int next = currentIndex_ + 1;
    if (next >= count) {
        if (repeatMode_ == RepeatAll) next = 0;
        else return {};
    }
    return playlistModel_.titleAt(next);
}

QString PlayerController::currentEpisodeTitle() const {
    if (currentIndex_ >= 0 && currentIndex_ < playlistModel_.count()) {
        return playlistModel_.titleAt(currentIndex_);
    }
    return {};
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

#ifdef Q_OS_WIN
static WNDPROC s_originalMpvWndProc = nullptr;

static LRESULT CALLBACK MpvChildWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST) {
        return HTTRANSPARENT;
    }
    return CallWindowProc(s_originalMpvWndProc, hwnd, msg, wParam, lParam);
}
#endif

void PlayerController::ensureMpvVideoWindow() {
    if (mpvVideoWindow_ || !mpvParentWindow_) return;

    mpvVideoWindow_ = new QWindow(mpvParentWindow_);
    mpvVideoWindow_->setFlags(Qt::FramelessWindowHint);
    mpvVideoWindow_->resize(1, 1);
    mpvVideoWindow_->hide();

#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(mpvVideoWindow_->winId());
    SetClassLongPtr(hwnd,
                    GCLP_HBRBACKGROUND,
                    reinterpret_cast<LONG_PTR>(GetStockObject(BLACK_BRUSH)));

    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT);

    if (!s_originalMpvWndProc) {
        s_originalMpvWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(MpvChildWndProc)));
    }
#endif

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
    // 子窗口实际拿到多少物理像素：dpr=1 而屏幕在缩放的画面上，等于 mpv 画完还要被 Windows 拉伸，
    // 这条和 mpv 的"源→输出"合起来才能判断糊该算在谁头上。尺寸变了才写一行，不跟着鼠标刷屏。
    const double dpr = mpvVideoWindow_->devicePixelRatio();
    const QString stamp = QStringLiteral("%1x%2 dpr=%3").arg(boundedWidth).arg(boundedHeight)
        .arg(dpr, 0, 'f', 2);
    if (stamp != mpvGeometryStamp_) {
        mpvGeometryStamp_ = stamp;
        Logger::instance().info(QStringLiteral("[Video] mpv 子窗口 %1 → 物理 %2x%3")
            .arg(stamp)
            .arg(qRound(boundedWidth * dpr))
            .arg(qRound(boundedHeight * dpr)));
    }
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
    if (mpvSurfaceVisible_ && mpvMode_ && !loading_ && hasFirstFrame_ && !mpvPlaybackFinished_ && !currentFile_.isEmpty()) {
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
}

void PlayerController::updateMpvVideoWindowVisibility() {
    if (!mpvVideoWindow_) return;
    if (mpvSurfaceVisible_ && mpvMode_ && !loading_ && hasFirstFrame_ && !mpvPlaybackFinished_ && !currentFile_.isEmpty()) {
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
    skipIntroMs_ = qBound(0, s.value("skip/introMs", 0).toInt(), kSkipMarkerMaxMs);
    skipOutroMs_ = qBound(0, s.value("skip/outroMs", 0).toInt(), kSkipMarkerMaxMs);
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
    s.setValue("skip/introMs", skipIntroMs_);
    s.setValue("skip/outroMs", skipOutroMs_);
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
    services_->downloadModel()->enqueueVideoM3u8Only(name, QString(), episodes);
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
    services_->downloadModel()->enqueueVideo(name, QString(), episodes);
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
    skipIntroDone_ = false;
    skipOutroDone_ = false;
    mpvBufferProgress_ = 0.0;
    mpvBuffering_ = false;
    hasFirstFrame_ = false;
    setLoading(true);
    if (mpvVideoWindow_) {
        mpvVideoWindow_->hide();
    }
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
        return;
    }
    // FFmpeg 回退管线已移除: 视频 (网络流/本地文件) 只走 mpv。libmpv 加载
    // 失败 (通常是 VC++ 运行库缺失) 时不进入破损的回退管线, 直接报错并
    // 引导安装运行库; 装好后下一次播放自动用上 mpv (后端每次播放重新尝试加载)。
    Logger::instance().warn(QStringLiteral(
        "libmpv backend unavailable, mpv-only playback: %1").arg(mpvBackend_.errorString()));
    notifyVcRuntimeMissing();
    if (qtPlayer_) {
        qtPlayer_->stop();
        qtPlayer_->setSource({});
    }
    qtNetworkMode_ = false;
    setLoading(false);
    emit playbackStateChanged();
    emit playbackFailed(QStringLiteral("播放器初始化失败: 缺少 VC++ 运行库, 请按提示安装后重试"));
}

bool PlayerController::isVcRuntimeInstalled() {
    // 仅认微软官方注册表键。System32 里有零散 DLL 不代表运行库完整
    // (虚拟机实测: 有 DLL 但 libmpv 仍初始化失败, DLL 探测会误判"已安装"
    // 从而吞掉安装提示)。
    const QSettings runtimes(
        QStringLiteral(R"(HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64)"),
        QSettings::NativeFormat);
    return runtimes.value(QStringLiteral("Installed")).toInt() == 1;
}

void PlayerController::notifyVcRuntimeMissing() {
    // mpv 加载失败即提示, 不做任何"可能已安装"的预判——
    // 判定不可靠时宁可多提示一次。
    if (vcRuntimePromptShown_)
        return;
    vcRuntimePromptShown_ = true;
    Logger::instance().warn("libmpv load failed; prompting VC++ runtime install");
    emit vcRuntimeMissing();
}

void PlayerController::installVcRuntime() {
    if (vcInstallRunning_) {
        // 已在下载/安装中: 给可见反馈而不是无声返回。
        emit vcInstallStateChanged(QStringLiteral("正在下载/安装中, 请稍候..."));
        return;
    }
    if (isVcRuntimeInstalled()) {
        emit vcInstallStateChanged(QStringLiteral("运行库已安装但 mpv 仍无法加载, 可能缺少系统媒体组件 (如 Windows N 版需安装媒体功能包)"));
        return;
    }
    vcInstallRunning_ = true;

    // 多下载源依次尝试 (aka.ms 微软官方 → Gitee 仓库镜像)；
    // 不想联网的机器用对话框里的"使用本地安装包"自选已下好的 vc_redist.x64.exe。
    static const QStringList kVcRedistUrls = {
        QStringLiteral("https://aka.ms/vs/17/release/vc_redist.x64.exe"),
        QStringLiteral("https://gitee.com/mateo-ki/melo-box-app/raw/master/redist/vc_redist.x64.exe"),
    };

    auto tryDownload = std::make_shared<std::function<void(int)>>();
    *tryDownload = [this, tryDownload](int urlIndex) {
        if (urlIndex >= kVcRedistUrls.size()) {
            vcInstallRunning_ = false;
            Logger::instance().error("[VcRuntime] all download sources failed");
            emit vcInstallStateChanged(QStringLiteral("所有下载源均失败, 可点\"使用本地安装包\"选择已下载的 vc_redist.x64.exe"));
            return;
        }
        const QUrl url(kVcRedistUrls.at(urlIndex));
        Logger::instance().info(QStringLiteral("[VcRuntime] downloading from %1").arg(url.toString()));
        emit vcInstallStateChanged(QStringLiteral("正在下载 VC++ 运行库 (源 %1/%2)...").arg(urlIndex + 1).arg(kVcRedistUrls.size()));
        auto *nam = new QNetworkAccessManager(this);
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(25000);  // 25s 无数据即切下一个源
        auto *reply = nam->get(request);
        connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
            if (total > 0)
                emit vcInstallStateChanged(QStringLiteral("正在下载 VC++ 运行库... %1MB / %2MB (%3%)")
                    .arg(received / 1048576.0, 0, 'f', 1)
                    .arg(total / 1048576.0, 0, 'f', 1)
                    .arg(int(received * 100 / total)));
            else
                emit vcInstallStateChanged(QStringLiteral("正在下载 VC++ 运行库... %1KB").arg(received / 1024));
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, nam, tryDownload, urlIndex]() {
            const QByteArray bytes = reply->readAll();
            const auto error = reply->error();
            reply->deleteLater();
            nam->deleteLater();
            // 体积过小 (404 页/空响应) 与传输错误同样视为源失败。
            if (error != QNetworkReply::NoError || bytes.size() < 1024 * 1024) {
                Logger::instance().warn(QStringLiteral(
                    "[VcRuntime] source failed (error=%1 bytes=%2), switching source")
                    .arg(int(error)).arg(bytes.size()));
                QTimer::singleShot(300, this, [tryDownload, urlIndex]() { (*tryDownload)(urlIndex + 1); });
                return;
            }
            const QString downloadPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                .filePath(QStringLiteral("vc_redist.x64.exe"));
            QFile file(downloadPath);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
                vcInstallRunning_ = false;
                emit vcInstallStateChanged(QStringLiteral("安装包保存失败, 请重试"));
                return;
            }
            file.close();
            Logger::instance().info(QStringLiteral("[VcRuntime] redist downloaded to %1 (%2 bytes)")
                .arg(downloadPath).arg(bytes.size()));
            runVcRedistInstaller(downloadPath);
        });
    };
    (*tryDownload)(0);
}

// 以管理员权限静默运行 vc_redist 并轮询注册表验证完成 (最长 5 分钟)。
void PlayerController::runVcRedistInstaller(const QString &installerPath) {
    vcInstallerPath_ = installerPath;
    emit vcInstallStateChanged(QStringLiteral("正在安装, 如弹出 UAC 窗口请选择\"是\"..."));
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    const QString exePath = QDir::toNativeSeparators(installerPath);
    const wchar_t *params = L"/install /quiet /norestart";
    sei.lpFile = reinterpret_cast<const wchar_t *>(exePath.utf16());
    sei.lpParameters = params;
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    if (!ShellExecuteExW(&sei) || reinterpret_cast<intptr_t>(sei.hInstApp) <= 32) {
        vcInstallRunning_ = false;
        Logger::instance().error("[VcRuntime] elevation declined or launch failed");
        emit vcInstallStateChanged(QStringLiteral("无法启动安装程序 (UAC 被拒绝?), 可右键以管理员身份手动运行安装包"));
        return;
    }
    if (sei.hProcess)
        CloseHandle(sei.hProcess);
    vcInstallPollCount_ = 0;
    disconnect(&vcInstallPollTimer_, nullptr, nullptr, nullptr);
    connect(&vcInstallPollTimer_, &QTimer::timeout, this, [this]() {
        if (isVcRuntimeInstalled()) {
            vcInstallPollTimer_.stop();
            vcInstallRunning_ = false;
            Logger::instance().info("[VcRuntime] install verified via registry");
            emit vcInstallStateChanged(QStringLiteral("VC++ 运行库安装完成, 下次播放将自动启用高性能播放器"));
            return;
        }
        vcInstallPollCount_ += 1;
        if (vcInstallPollCount_ > 150) {
            vcInstallPollTimer_.stop();
            vcInstallRunning_ = false;
            emit vcInstallStateChanged(QStringLiteral("等待安装超时, 可重新点击\"立即安装\"或手动安装"));
        }
    });
    vcInstallPollTimer_.start(2000);
}

// 离线机器的自备安装包入口: 从其他设备拷贝 vc_redist.x64.exe 过来本地安装。
// 文件选择用 C++ 原生 QFileDialog (Win32), 不依赖任何 QML 对话框模块。
void PlayerController::installVcRuntimeFromFile() {
    if (vcInstallRunning_)
        return;
    if (isVcRuntimeInstalled()) {
        emit vcInstallStateChanged(QStringLiteral("运行库已安装但 mpv 仍无法加载, 可能缺少系统媒体组件 (如 Windows N 版需安装媒体功能包)"));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        nullptr, QStringLiteral("选择 VC++ 运行库安装包"),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        QStringLiteral("VC++ 安装包 (vc_redist*.exe);;所有文件 (*.exe)"));
    const QString trimmed = QDir::toNativeSeparators(path.trimmed());
    if (trimmed.isEmpty())
        return;
    const QFileInfo info(trimmed);
    if (!info.exists() || info.size() < 1024 * 1024) {
        emit vcInstallStateChanged(QStringLiteral("未找到有效的 vc_redist 安装包文件"));
        return;
    }
    vcInstallRunning_ = true;
    Logger::instance().info(QStringLiteral("[VcRuntime] installing from local file: %1").arg(trimmed));
    runVcRedistInstaller(trimmed);
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

void PlayerController::clearVideoDanmaku() {
    if (videoDanmaku_.isEmpty() && videoDanmakuToken_.isEmpty()) return;
    videoDanmaku_.clear();
    videoDanmakuToken_.clear();
    emit videoDanmakuChanged();
}

/// dongpian 系影视：按分集 token 拉 Pool 弹幕（/api/movie/danmaku），转成
/// DanmakuOverlay 兼容的 [{id,text,color,timePoint,mode,size}]
void PlayerController::fetchVideoDanmaku(const QString &episodeToken) {
    auto *sites = services_ ? services_->apiSiteModel() : nullptr;
    const QString sourceId = sites ? sites->currentVideoSourceId() : QString();
    const QString server = sites ? sites->poolServerUrl() : QString();
    const QString sessionToken = sites ? sites->poolSessionToken() : QString();
    // 只有 dongpian 系官方源的分集有 Pool 弹幕；token 变化才重新拉
    if (!sourceId.startsWith(QStringLiteral("dongpian")) || server.isEmpty()
        || sessionToken.isEmpty() || episodeToken.trimmed().isEmpty()) {
        clearVideoDanmaku();
        return;
    }
    if (videoDanmakuToken_ == episodeToken && !videoDanmaku_.isEmpty()) return;
    videoDanmakuToken_ = episodeToken;
    videoDanmaku_.clear();
    emit videoDanmakuChanged();
    const QString path = QStringLiteral("/api/movie/danmaku?token=%1")
                             .arg(QString::fromUtf8(QUrl::toPercentEncoding(episodeToken)));
    QPointer<PlayerController> guard(this);
    PoolApi::sendRequest(this, poolNam_, server, path, {}, QByteArrayLiteral("GET"),
        sessionToken, [guard, episodeToken](const PoolApi::Result &result) {
            if (!guard) return;
            if (!result.ok) return;
            // 弹幕下载慢（上游 manifest+分段），响应到达时若已切到别的分集则丢弃，
            // 否则旧集弹幕会覆盖新集 —— 出现"别的影视的弹幕"
            if (guard->videoDanmakuToken_ != episodeToken) return;
            const auto data = result.data.toObject();
            if (data.value(QStringLiteral("status")).toString() != QStringLiteral("ready")) return;
            QVariantList list;
            int idx = 0;
            for (const auto &v : data.value(QStringLiteral("items")).toArray()) {
                const auto it = v.toObject();
                QVariantMap m;
                m.insert(QStringLiteral("id"), idx++);
                m.insert(QStringLiteral("text"), it.value(QStringLiteral("text")).toString());
                m.insert(QStringLiteral("color"), it.value(QStringLiteral("color")).toString(QStringLiteral("#ffffff")));
                m.insert(QStringLiteral("timePoint"), (it.value(QStringLiteral("time")).toDouble()) * 1000.0);
                m.insert(QStringLiteral("mode"), it.value(QStringLiteral("mode")).toInt());
                m.insert(QStringLiteral("size"), it.value(QStringLiteral("size")).toString(QStringLiteral("M")));
                list.append(m);
            }
            if (list.isEmpty()) return;
            guard->videoDanmaku_ = list;
            emit guard->videoDanmakuChanged();
            Logger::instance().info(QStringLiteral("[Danmaku] video danmaku loaded: %1").arg(list.size()));
        });
}

void PlayerController::resolvePoolPlayUrl(const QString &episodeToken) {
    auto *sites = services_ ? services_->apiSiteModel() : nullptr;
    const QString sourceId = sites ? sites->currentVideoSourceId() : QString();
    const QString server = sites ? sites->poolServerUrl() : QString();
    const QString sessionToken = sites ? sites->poolSessionToken() : QString();
    if (sourceId.isEmpty() || server.isEmpty() || sessionToken.isEmpty()) {
        emit playbackFailed(QStringLiteral("请先登录后再播放"));
        return;
    }
    const QString path = QStringLiteral("/api/vod/play?site=%1&url=%2")
                             .arg(sourceId,
                                  QString::fromUtf8(QUrl::toPercentEncoding(episodeToken)));
    PoolApi::sendRequest(this, poolNam_, server, path, {}, QByteArrayLiteral("GET"),
                         sessionToken, [this, episodeToken](const PoolApi::Result &result) {
            if (!result.ok) {
                emit playbackFailed(result.message.isEmpty()
                                        ? QStringLiteral("解析播放线路失败")
                                        : result.message);
                return;
            }
            const auto lines =
                result.data.toObject().value(QStringLiteral("lines")).toArray();
            QString bestUrl;
            int bestScore = -1;
            for (const auto &line : lines) {
                const auto obj = line.toObject();
                // featured 置顶（无水印/牛牛/最大），再按 priority 择优
                const int score =
                    (obj.value(QStringLiteral("is_featured")).toBool() ? 1000 : 0)
                    + obj.value(QStringLiteral("priority")).toInt();
                if (score > bestScore) {
                    bestScore = score;
                    bestUrl = obj.value(QStringLiteral("url")).toString();
                }
            }
            if (bestUrl.isEmpty()) {
                emit playbackFailed(QStringLiteral("该分集暂无可用播放线路"));
                return;
            }
            fetchVideoDanmaku(episodeToken);
            playVideoUrl(bestUrl);
        });
}

void PlayerController::playVideoUrl(const QString &url) {
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty()) return;
    // pool 统一点播源：非直链分集标识（如短片源 token）先解析成直链再走常规路径
    if (!trimmed.startsWith(QStringLiteral("http"))) {
        resolvePoolPlayUrl(trimmed);
        return;
    }
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

    // 注意: 不要过滤 v1-ad.video.yximgs.com / adVideoLp 之类的 URL——
    // 接口下发的正片就可能落在这些"广告"域名/路径上 (实测 23 分钟 4K HEVC
    // 正片由此下发且可正常播放), 拦截会导致动漫/短视频"播放不了"。

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
        // 注意: 不过滤 adVideoLp / v1-ad.video.yximgs.com 之类的 URL——
        // 接口下发的正片就可能由此下发且可正常播放, 过滤会让内容"播放不了"。
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



void PlayerController::playVoice(int sourceIndex) {
    services_->voiceController()->playVoice(sourceIndex);
}

void PlayerController::preemptPlaybackForLocalAudio() {
    if (mpvMode_) {
        mpvBackend_.stop();
        mpvMode_ = false;
        updateMpvVideoWindowVisibility();
    }
    engine_.abortPipeline();
}

void PlayerController::playLocalAudioFile(const QString &path) {
    if (!qtPlayer_) {
        Logger::instance().error("QtMediaPlayer is unavailable for local audio playback");
        return;
    }

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
                 MediaUtils::audioExtension(url, contentType));
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













