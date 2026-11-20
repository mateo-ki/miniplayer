#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QString>
#include <QThread>
#include <QVariantList>
#include <QVector>
#include <QVideoFrame>

#include "core/PlayerEngine.h"
#include "models/MediaInfoModel.h"
#include "models/PlaylistModel.h"
#include "models/RuntimeLogModel.h"
#include "mpv/MpvBackend.h"

class VideoFrameBridge;
class HlsPlaylistProxy;
class AppServices;
class QAudioOutput;
class QMediaPlayer;
class QNetworkReply;
class QTimer;
class QVideoSink;
class QWindow;

class PlayerController final : public QObject {
    Q_OBJECT
public:
    enum RepeatMode { NoRepeat, RepeatOne, RepeatAll };
    Q_ENUM(RepeatMode)

    // 片头/片尾跳过判定：每集从头播时跳到片头结束，播到 duration-片尾 时按
    // "播完"处理走现有 EOF/连播通道。纯函数，便于脱离引擎单测。
    enum class SkipAction { None, JumpIntro, FinishEpisode };
    static SkipAction skipAction(qint64 positionMs, qint64 durationMs,
                                 int introMs, int outroMs,
                                 bool introDone, bool outroDone);

    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playbackStateChanged)
    Q_PROPERTY(bool isPaused READ isPaused NOTIFY playbackStateChanged)
    Q_PROPERTY(bool seeking READ seeking NOTIFY seekingChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY timelineChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY timelineChanged)
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY currentFileChanged)
    /// 影视（dongpian 系）弹幕：[{id,text,color,timePoint,mode,size}]，与 DanmakuOverlay 兼容
    Q_PROPERTY(QVariantList videoDanmaku READ videoDanmaku NOTIFY videoDanmakuChanged)
    Q_PROPERTY(int decodedFrames READ decodedFrames NOTIFY timelineChanged)
    Q_PROPERTY(int totalFrames READ totalFrames NOTIFY timelineChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(bool canPlayPrevious READ canPlayPrevious NOTIFY currentIndexChanged)
    Q_PROPERTY(bool canPlayNext READ canPlayNext NOTIFY currentIndexChanged)
    Q_PROPERTY(QString previousEpisodeTitle READ previousEpisodeTitle NOTIFY currentIndexChanged)
    Q_PROPERTY(QString nextEpisodeTitle READ nextEpisodeTitle NOTIFY currentIndexChanged)
    Q_PROPERTY(QString currentEpisodeTitle READ currentEpisodeTitle NOTIFY currentIndexChanged)
    Q_PROPERTY(float playbackRate READ playbackRate WRITE setPlaybackRate NOTIFY playbackRateChanged)
    Q_PROPERTY(RepeatMode repeatMode READ repeatMode WRITE setRepeatMode NOTIFY repeatModeChanged)
    Q_PROPERTY(int skipIntroMs READ skipIntroMs WRITE setSkipIntroMs NOTIFY skipMarkersChanged)
    Q_PROPERTY(int skipOutroMs READ skipOutroMs WRITE setSkipOutroMs NOTIFY skipMarkersChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool isNetwork READ isNetwork NOTIFY currentFileChanged)
    Q_PROPERTY(bool isSeekable READ isSeekable NOTIFY currentFileChanged)
    Q_PROPERTY(bool isLive READ isLive NOTIFY currentFileChanged)
    Q_PROPERTY(double bufferProgress READ bufferProgress NOTIFY timelineChanged)
    Q_PROPERTY(QVariantList audioTracks READ audioTracksQml NOTIFY currentFileChanged)
    Q_PROPERTY(int currentAudioTrack READ currentAudioTrack NOTIFY currentFileChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracksQml NOTIFY currentFileChanged)
    Q_PROPERTY(int currentSubtitleTrack READ currentSubtitleTrack NOTIFY currentFileChanged)
    Q_PROPERTY(bool subtitlesEnabled READ subtitlesEnabled WRITE setSubtitlesEnabled NOTIFY subtitlesEnabledChanged)
    Q_PROPERTY(QImage thumbnailPreview READ thumbnailPreview NOTIFY thumbnailPreviewChanged)
    Q_PROPERTY(double thumbnailPosition READ thumbnailPosition NOTIFY thumbnailPreviewChanged)
    Q_PROPERTY(bool thumbnailReady READ thumbnailReady NOTIFY thumbnailPreviewChanged)
    Q_PROPERTY(MediaInfoModel *mediaInfoModel READ mediaInfoModel CONSTANT)
    Q_PROPERTY(PlaylistModel *playlistModel READ playlistModel CONSTANT)
    Q_PROPERTY(PlaylistModel *historyModel READ historyModel CONSTANT)
    Q_PROPERTY(RuntimeLogModel *runtimeLogModel READ runtimeLogModel CONSTANT)
    Q_PROPERTY(QString currentVodName READ currentVodName NOTIFY currentVodNameChanged)
    Q_PROPERTY(QString currentShortVideoUrl READ currentShortVideoUrl NOTIFY shortVideoChanged)
    Q_PROPERTY(QString shortVideoMessage READ shortVideoMessage NOTIFY shortVideoChanged)
    Q_PROPERTY(bool musicLoading READ musicLoading NOTIFY musicChanged)
    Q_PROPERTY(QVariantList musicResults READ musicResults NOTIFY musicChanged)
    Q_PROPERTY(QString musicMessage READ musicMessage NOTIFY musicChanged)
    Q_PROPERTY(QString currentMusicUrl READ currentMusicUrl NOTIFY musicChanged)
    Q_PROPERTY(QString currentMusicTitle READ currentMusicTitle NOTIFY musicChanged)
    Q_PROPERTY(QString currentMusicArtist READ currentMusicArtist NOTIFY musicChanged)
    Q_PROPERTY(QString currentMusicPic READ currentMusicPic NOTIFY musicChanged)
    Q_PROPERTY(QString currentMusicLrc READ currentMusicLrc NOTIFY musicChanged)
    Q_PROPERTY(QVariantList currentMusicLyricLines READ currentMusicLyricLines NOTIFY musicChanged)
    Q_PROPERTY(int currentMusicLyricIndex READ currentMusicLyricIndex NOTIFY musicLyricIndexChanged)
    Q_PROPERTY(int musicLyricOffsetMs READ musicLyricOffsetMs NOTIFY musicChanged)
    Q_PROPERTY(bool musicShowTranslation READ musicShowTranslation NOTIFY musicChanged)
    Q_PROPERTY(bool musicShowRomanization READ musicShowRomanization NOTIFY musicChanged)
    Q_PROPERTY(QString currentMusicLyricSource READ currentMusicLyricSource NOTIFY musicChanged)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)

public:
    // services 为组合根持有的功能模型容器，生命周期必须长于本对象。
    explicit PlayerController(AppServices *services, QObject *parent = nullptr);

    bool isPlaying() const;
    bool isPaused() const;
    bool seeking() const;
    qint64 durationMs() const;
    qint64 positionMs() const;
    float volume() const;
    bool muted() const;
    QString currentFile() const;
    QVariantList videoDanmaku() const { return videoDanmaku_; }
    Q_INVOKABLE void clearVideoDanmaku();
    void fetchVideoDanmaku(const QString &episodeToken);
    int decodedFrames() const;
    int totalFrames() const;
    int skipIntroMs() const;
    int skipOutroMs() const;
    int currentIndex() const;
    bool canPlayPrevious() const;
    bool canPlayNext() const;
    QString previousEpisodeTitle() const;
    QString nextEpisodeTitle() const;
    QString currentEpisodeTitle() const;
    float playbackRate() const;
    RepeatMode repeatMode() const;
    bool loading() const;
    bool isNetwork() const;
    bool isSeekable() const;
    bool isLive() const;
    double bufferProgress() const;
    QVariantList audioTracksQml() const;
    int currentAudioTrack() const;
    QVariantList subtitleTracksQml() const;
    int currentSubtitleTrack() const;
    bool subtitlesEnabled() const;
    QImage thumbnailPreview() const;
    double thumbnailPosition() const;
    bool thumbnailReady() const;
    MediaInfoModel *mediaInfoModel();
    PlaylistModel *playlistModel();
    PlaylistModel *historyModel();
    RuntimeLogModel *runtimeLogModel();
    QString currentVodName() const;
    QString currentShortVideoUrl() const;
    QString shortVideoMessage() const;
    bool musicLoading() const;
    QVariantList musicResults() const;
    QString musicMessage() const;
    QString currentMusicUrl() const;
    QString currentMusicTitle() const;
    QString currentMusicArtist() const;
    QString currentMusicPic() const;
    QString currentMusicLrc() const;
    QVariantList currentMusicLyricLines() const;
    int currentMusicLyricIndex() const;
    int musicLyricOffsetMs() const;
    bool musicShowTranslation() const;
    bool musicShowRomanization() const;
    QString currentMusicLyricSource() const;
    QString appVersion() const;

    Q_INVOKABLE void setCurrentVodName(const QString &name);
    // pool 统一点播源：分集 token -> /api/vod/play 解析直链
    void resolvePoolPlayUrl(const QString &episodeToken);
    Q_INVOKABLE void downloadCurrentPlaylist();
    Q_INVOKABLE void saveCurrentPlaylistM3u8();
    Q_INVOKABLE void playVideoUrl(const QString &url);
    Q_INVOKABLE void playVodUrl(const QString &url);
    Q_INVOKABLE void resolveAndPlayUrl(const QString &url);
    Q_INVOKABLE void playShortVideo(int sourceIndex = 0);
    Q_INVOKABLE void prefetchShortVideo(int sourceIndex, const QString &sourceUrl, const QString &label);
    Q_INVOKABLE void playShortVideoUrl(const QString &url, const QString &label = QString());
    Q_INVOKABLE void stopShortVideo();
    Q_INVOKABLE void logShortVideoDebug(const QString &message);
    // 检测/安装 Microsoft VC++ 2015-2022 x64 运行库: libmpv 加载失败且运行库
    // 缺失时 QML 弹提示, 用户确认后下载官方 vc_redist 静默安装。
    Q_INVOKABLE static bool isVcRuntimeInstalled();
    Q_INVOKABLE void installVcRuntime();
    Q_INVOKABLE void installVcRuntimeFromFile();
    void runVcRedistInstaller(const QString &installerPath);
    void notifyVcRuntimeMissing();
    Q_INVOKABLE void playVoice(int sourceIndex = 0);
    // 语音模块取数完成后由播放器播放本地文件（AppServices 信号接线）
    void playLocalAudioFile(const QString &path);
    void preemptPlaybackForLocalAudio();
    Q_INVOKABLE void searchMusic(const QString &keyword, const QString &server = QString());
    Q_INVOKABLE void loadMusicPlaylist(const QString &playlistId, const QString &server = QStringLiteral("netease"));
    Q_INVOKABLE void resolveAndPlayMusic(const QString &source, const QString &id, const QString &title,
                                         const QString &artist, const QString &picUrl);
    Q_INVOKABLE void playMusic(const QString &url, const QString &title, const QString &artist, const QString &lrcUrl, const QString &picUrl);
    Q_INVOKABLE void saveCurrentMusic();
    Q_INVOKABLE void openMusicDownloadFolder();
    Q_INVOKABLE void adjustMusicLyricOffset(int deltaMs);
    Q_INVOKABLE void resetMusicLyricOffset();
    Q_INVOKABLE void setMusicLyricDisplayOptions(bool showTranslation, bool showRomanization);
    Q_INVOKABLE void reloadCurrentMusicLyrics();
    Q_INVOKABLE QVariant uiSetting(const QString &key, const QVariant &defaultValue = QVariant()) const;
    Q_INVOKABLE void saveUiSetting(const QString &key, const QVariant &value);

    Q_INVOKABLE void openFile();
    Q_INVOKABLE void openFileAtPath(const QString &path, bool autoPlay = false);
    Q_INVOKABLE void openUrl(const QString &url);
    Q_INVOKABLE void play();
    Q_INVOKABLE void setVideoBridge(QObject *bridge);
    Q_INVOKABLE void setMpvWindowId(qint64 windowId);
    Q_INVOKABLE void setMpvVideoGeometry(qreal x, qreal y, qreal width, qreal height);
    Q_INVOKABLE void setMpvSurfaceVisible(bool visible);
    Q_INVOKABLE void refreshMpvVideoWindow();
    Q_INVOKABLE void resetMouseCursor();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    // 应用退出快速关停: 中止播放/管线/代理, 线程等待上限 ~400ms,
    // 供 aboutToQuit 调用 (stop() 的 3s x3 等待只用于播放切换场景)。
    Q_INVOKABLE void prepareExit();
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void playFromPlaylist(int index);
    Q_INVOKABLE void playFromHistory(int index);
    Q_INVOKABLE void clearPlaybackHistory();
    Q_INVOKABLE void setPlaylistEpisodes(const QVariantList &episodes, int currentIndex);
    Q_INVOKABLE void setPlaybackSources(const QVariantList &sources, int currentSource, int currentEpisode);

    Q_INVOKABLE void setVolume(float volume);
    Q_INVOKABLE void setMuted(bool muted);
    Q_INVOKABLE void saveScreenshot();
    Q_INVOKABLE void setPlaybackRate(float rate);
    Q_INVOKABLE void setRepeatMode(RepeatMode mode);
    Q_INVOKABLE void setSkipIntroMs(int ms);
    Q_INVOKABLE void setSkipOutroMs(int ms);
    Q_INVOKABLE void switchAudioTrack(int streamIndex);
    Q_INVOKABLE void switchSubtitleTrack(int streamIndex);
    Q_INVOKABLE void setSubtitlesEnabled(bool enabled);
    Q_INVOKABLE void cycleAspectRatio();
    Q_INVOKABLE void requestThumbnail(qint64 positionMs);
    void setLoading(bool loading);
    Q_INVOKABLE void playNext();
    Q_INVOKABLE void playPrevious();

signals:
    void videoDanmakuChanged();
    void playbackStateChanged();
    void timelineChanged();
    void seekingChanged();
    void volumeChanged();
    void mutedChanged();
    void currentFileChanged();
    void playbackRateChanged();
    void repeatModeChanged();
    void skipMarkersChanged();
    void currentIndexChanged();
    void loadingChanged();
    void thumbnailPreviewChanged();
    void thumbnailReadyForProvider(const QString &key, const QImage &image);
    void subtitlesEnabledChanged();
    void currentVodNameChanged();
    void playlistEpisodeRequested(int index, const QString &title);
    void playbackEnded();
    void playbackFailed(const QString &message);
    void shortVideoChanged();
    void shortVideoPrefetched(int sourceIndex, const QString &videoUrl, const QString &label);
    void musicChanged();
    void musicLyricIndexChanged();
    void vcRuntimeMissing();
    void vcInstallStateChanged(const QString &message);

private:
    AppServices *services_ = nullptr;  // 功能模型容器（非持有）
    QNetworkAccessManager poolNam_;    // pool 统一点播源请求
    PlayerEngine engine_;
    MediaInfoModel mediaInfoModel_;
    PlaylistModel playlistModel_;
    PlaylistModel historyModel_;
    RuntimeLogModel runtimeLogModel_;
    MpvBackend mpvBackend_;
    QMediaPlayer *qtPlayer_ = nullptr;
    QAudioOutput *qtAudioOutput_ = nullptr;
    QVideoSink *qtVideoSink_ = nullptr;
    QTimer *seekWatchdog_ = nullptr;
    QTimer *playbackLoadGuardTimer_ = nullptr;
    QTimer *cursorResetTimer_ = nullptr;
    QPointer<QNetworkReply> hlsFilterReply_;
    HlsPlaylistProxy *hlsPlaylistProxy_ = nullptr;
    QWindow *mpvParentWindow_ = nullptr;
    QWindow *mpvVideoWindow_ = nullptr;
    bool mpvSurfaceVisible_ = true;
    QString mpvGeometryStamp_;
    bool mpvMode_ = false;
    bool qtNetworkMode_ = false;
    bool userPausedPlayback_ = false;
    bool mpvPlaybackFinished_ = false;
    bool mpvBuffering_ = false;
    bool isPlaying_ = false;
    bool isPaused_ = false;
    bool seeking_ = false;
    qint64 durationMs_ = 0;
    qint64 positionMs_ = 0;
    float volume_ = 1.0f;
    bool muted_ = false;
    float playbackRate_ = 1.0f;
    RepeatMode repeatMode_ = NoRepeat;
    int currentIndex_ = -1;
    QString currentFile_;
    QVariantList videoDanmaku_;
    QString videoDanmakuToken_;
    QString currentVodName_;
    QVariantList playbackSources_;
    int currentPlaybackSource_ = -1;
    int currentPlaybackEpisode_ = -1;
    QString currentShortVideoUrl_;
    QString shortVideoMessage_;
    QString suspendedVideoUrl_;
    qint64 suspendedVideoPositionMs_ = 0;
    bool suspendedVideoWasPlaying_ = false;
    bool suspendedVideoWasPaused_ = false;
    bool suspendedVideoRestorePending_ = false;
    bool musicLoading_ = false;
    QVariantList musicResults_;
    QString musicMessage_;
    QString currentMusicUrl_;
    QString currentMusicTitle_;
    QString currentMusicArtist_;
    QString currentMusicPic_;
    QString currentMusicSource_;
    QString currentMusicId_;
    QString currentMusicLrc_;
    QVariantList currentMusicLyricLines_;

    // VC++ 运行库检测/自动安装状态
    bool vcRuntimePromptShown_ = false;
    bool vcInstallRunning_ = false;
    int vcInstallPollCount_ = 0;
    QString vcInstallerPath_;
    QTimer vcInstallPollTimer_;
    QVector<qint64> currentMusicLyricTimes_;
    int currentMusicLyricIndex_ = -1;
    int musicLyricOffsetMs_ = 0;
    bool musicShowTranslation_ = true;
    bool musicShowRomanization_ = false;
    QString currentMusicLyricSource_;
    QString currentMusicLyricTranslation_;
    QString currentMusicLyricRomanization_;
    quint64 musicSearchSerial_ = 0;
    quint64 musicLyricRequestSerial_ = 0;

    int skipIntroMs_ = 0;
    int skipOutroMs_ = 0;
    bool skipIntroDone_ = false;
    bool skipOutroDone_ = false;

    bool opening_ = false;
    bool loading_ = false;
    qint64 pendingSeekMs_ = -1;
    double mpvBufferProgress_ = -1.0;
    QImage thumbnailPreview_;
    double thumbnailPosition_ = -1.0;
    bool thumbnailReady_ = false;
    quint64 hlsRequestSerial_ = 0;
    QString sanitizedHlsPath_;
    void ensureMpvVideoWindow();
    void updateMpvVideoWindowVisibility();
    void preparePlaybackTarget(const QString &displayUrl);
    void playVideoInput(const QString &inputUrl, const QString &displayUrl);
    void fetchAndFilterHls(const QString &url, quint64 requestSerial);
    void cancelHlsFilterRequest();
    void removeSanitizedHlsPlaylist();
    void wireEngineSignals();
    void handleEof();
    void finishSeek();
    void applySkipMarkers();
    void loadSettings();
    void saveSettings();
    void recordPlaybackHistory(const QString &filePath, const QString &title);
    void setCurrentMusicLrc(const QString &lrc);
    void updateCurrentMusicLyricIndex();
    void loadMusicLyricsForCurrentTrack(bool forceRefresh = false);
    bool loadLocalMusicLyrics();
    static QVariantList parseMusicLyricTrack(const QString &text, const QString &fieldName);
    void suspendCurrentVideoForShortVideo();
    void restoreSuspendedVideoAfterShortVideo();
    void applyPendingSuspendedVideoState();
};
