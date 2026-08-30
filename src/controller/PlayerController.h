#pragma once

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
#include "models/VideoSearchModel.h"
#include "models/ApiSiteModel.h"
#include "models/DmghgAnimeModel.h"
#include "models/BeeVideoModel.h"
#include "models/BeeScheduleModel.h"
#include "models/DownloadModel.h"
#include "mpv/MpvBackend.h"
#include "media/ImageCacheService.h"

class VideoFrameBridge;
class HlsPlaylistProxy;
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

    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playbackStateChanged)
    Q_PROPERTY(bool isPaused READ isPaused NOTIFY playbackStateChanged)
    Q_PROPERTY(bool seeking READ seeking NOTIFY seekingChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY timelineChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY timelineChanged)
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY currentFileChanged)
    Q_PROPERTY(int decodedFrames READ decodedFrames NOTIFY timelineChanged)
    Q_PROPERTY(int totalFrames READ totalFrames NOTIFY timelineChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(float playbackRate READ playbackRate WRITE setPlaybackRate NOTIFY playbackRateChanged)
    Q_PROPERTY(RepeatMode repeatMode READ repeatMode WRITE setRepeatMode NOTIFY repeatModeChanged)
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
    Q_PROPERTY(VideoSearchModel *videoSearchModel READ videoSearchModel CONSTANT)
    Q_PROPERTY(VideoSearchModel *detailSearchModel READ detailSearchModel CONSTANT)
    Q_PROPERTY(VideoSearchModel *sourceSearchModel READ sourceSearchModel CONSTANT)
    Q_PROPERTY(ApiSiteModel *apiSiteModel READ apiSiteModel CONSTANT)
    Q_PROPERTY(DmghgAnimeModel *dmghgAnimeModel READ dmghgAnimeModel CONSTANT)
    Q_PROPERTY(BeeVideoModel *beeVideoModel READ beeVideoModel CONSTANT)
    Q_PROPERTY(BeeScheduleModel *beeScheduleModel READ beeScheduleModel CONSTANT)
    Q_PROPERTY(DownloadModel *downloadModel READ downloadModel CONSTANT)
    Q_PROPERTY(QString currentVodName READ currentVodName NOTIFY currentVodNameChanged)
    Q_PROPERTY(bool imageLoading READ imageLoading NOTIFY imageStateChanged)
    Q_PROPERTY(QString currentImageUrl READ currentImageUrl NOTIFY imageStateChanged)
    Q_PROPERTY(QString currentImageDisplayUrl READ currentImageDisplayUrl NOTIFY imageStateChanged)
    Q_PROPERTY(QString imageMessage READ imageMessage NOTIFY imageStateChanged)
    Q_PROPERTY(int imageDiskCacheCount READ imageDiskCacheCount NOTIFY imageCacheChanged)
    Q_PROPERTY(qint64 imageDiskCacheBytes READ imageDiskCacheBytes NOTIFY imageCacheChanged)
    Q_PROPERTY(bool memeLoading READ memeLoading NOTIFY memeStateChanged)
    Q_PROPERTY(QVariantList memeImages READ memeImages NOTIFY memeStateChanged)
    Q_PROPERTY(QString memeMessage READ memeMessage NOTIFY memeStateChanged)
    Q_PROPERTY(QString currentShortVideoUrl READ currentShortVideoUrl NOTIFY shortVideoChanged)
    Q_PROPERTY(QString shortVideoMessage READ shortVideoMessage NOTIFY shortVideoChanged)
    Q_PROPERTY(QString currentVoiceUrl READ currentVoiceUrl NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceMessage READ voiceMessage NOTIFY voiceChanged)
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
    Q_PROPERTY(bool hotNewsLoading READ hotNewsLoading NOTIFY hotNewsChanged)
    Q_PROPERTY(QVariantList hotNewsItems READ hotNewsItems NOTIFY hotNewsChanged)
    Q_PROPERTY(QString hotNewsMessage READ hotNewsMessage NOTIFY hotNewsChanged)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(bool updateChecking READ updateChecking NOTIFY updateStateChanged)
    Q_PROPERTY(bool updateDownloading READ updateDownloading NOTIFY updateStateChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateStateChanged)
    Q_PROPERTY(QString updateVersion READ updateVersion NOTIFY updateStateChanged)
    Q_PROPERTY(QString updateMessage READ updateMessage NOTIFY updateStateChanged)
    Q_PROPERTY(double updateDownloadProgress READ updateDownloadProgress NOTIFY updateStateChanged)
    Q_PROPERTY(bool sourceSwitchSuggested READ sourceSwitchSuggested NOTIFY sourceSwitchSuggestionChanged)
    Q_PROPERTY(QString sourceSwitchReason READ sourceSwitchReason NOTIFY sourceSwitchSuggestionChanged)
    Q_PROPERTY(QString sourceSwitchCandidate READ sourceSwitchCandidate NOTIFY sourceSwitchSuggestionChanged)

public:
    explicit PlayerController(QObject *parent = nullptr);

    bool isPlaying() const;
    bool isPaused() const;
    bool seeking() const;
    qint64 durationMs() const;
    qint64 positionMs() const;
    float volume() const;
    bool muted() const;
    QString currentFile() const;
    int decodedFrames() const;
    int totalFrames() const;
    int currentIndex() const;
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
    VideoSearchModel *videoSearchModel();
    VideoSearchModel *detailSearchModel();
    VideoSearchModel *sourceSearchModel();
    ApiSiteModel *apiSiteModel();
    DmghgAnimeModel *dmghgAnimeModel();
    BeeVideoModel *beeVideoModel();
    BeeScheduleModel *beeScheduleModel();
    DownloadModel *downloadModel();
    QString currentVodName() const;
    bool imageLoading() const;
    QString currentImageUrl() const;
    QString currentImageDisplayUrl() const;
    QString imageMessage() const;
    int imageDiskCacheCount() const;
    qint64 imageDiskCacheBytes() const;
    bool memeLoading() const;
    QVariantList memeImages() const;
    QString memeMessage() const;
    QString currentShortVideoUrl() const;
    QString shortVideoMessage() const;
    QString currentVoiceUrl() const;
    QString voiceMessage() const;
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
    bool hotNewsLoading() const;
    QVariantList hotNewsItems() const;
    QString hotNewsMessage() const;
    QString appVersion() const;
    bool updateChecking() const;
    bool updateDownloading() const;
    bool updateAvailable() const;
    QString updateVersion() const;
    QString updateMessage() const;
    double updateDownloadProgress() const;
    bool sourceSwitchSuggested() const;
    QString sourceSwitchReason() const;
    QString sourceSwitchCandidate() const;

    Q_INVOKABLE void searchVideos(const QString &keyword, int page = 1, bool forceRefresh = false);
    Q_INVOKABLE void searchVideoById(const QString &vodId);
    Q_INVOKABLE void loadVideoDetail(int vodId);
    Q_INVOKABLE void loadVideoList(int page = 1);
    Q_INVOKABLE void loadVideoListByCategory(const QString &typeId, int page = 1);
    Q_INVOKABLE void setCurrentVodName(const QString &name);
    Q_INVOKABLE void searchCurrentVodOnSite(int siteIndex);
    Q_INVOKABLE void downloadVideoEpisodes(const QString &vodName, const QString &poster, const QVariantList &episodes);
    Q_INVOKABLE void saveVideoM3u8Episodes(const QString &vodName, const QString &poster, const QVariantList &episodes);
    Q_INVOKABLE void downloadCurrentPlaylist();
    Q_INVOKABLE void saveCurrentPlaylistM3u8();
    Q_INVOKABLE void playVideoUrl(const QString &url);
    Q_INVOKABLE void playVodUrl(const QString &url);
    Q_INVOKABLE void resolveAndPlayUrl(const QString &url);
    Q_INVOKABLE void loadRandomImage(const QString &apiUrl = QString());
    Q_INVOKABLE void showPrefetchedImage(const QString &imageUrl);
    Q_INVOKABLE void prefetchImage(const QString &apiUrl);
    Q_INVOKABLE void clearImageCache();
    Q_INVOKABLE void saveCurrentImage();
    Q_INVOKABLE void copyCurrentImage();
    Q_INVOKABLE void searchMemes(const QString &keyword = QStringLiteral("龙图"), int count = 10);
    Q_INVOKABLE void loadDragonMeme();
    Q_INVOKABLE void saveMemeImage(const QString &imageUrl);
    Q_INVOKABLE void copyMemeUrl(const QString &imageUrl);
    Q_INVOKABLE void playShortVideo(int sourceIndex = 0);
    Q_INVOKABLE void prefetchShortVideo(int sourceIndex, const QString &sourceUrl, const QString &label);
    Q_INVOKABLE void playShortVideoUrl(const QString &url, const QString &label = QString());
    Q_INVOKABLE void stopShortVideo();
    Q_INVOKABLE void logShortVideoDebug(const QString &message);
    Q_INVOKABLE void playVoice(int sourceIndex = 0);
    Q_INVOKABLE void saveVoice(int sourceIndex = 0);
    Q_INVOKABLE void copyVoice(int sourceIndex = 0);
    Q_INVOKABLE void saveCurrentVoice();
    Q_INVOKABLE void copyCurrentVoice();
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
    Q_INVOKABLE void loadHotNews(const QString &type = QStringLiteral("baidu"));
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void downloadAndInstallUpdate();
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
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void playFromPlaylist(int index);
    Q_INVOKABLE void playFromHistory(int index);
    Q_INVOKABLE void clearPlaybackHistory();
    Q_INVOKABLE void setPlaylistEpisodes(const QVariantList &episodes, int currentIndex);
    Q_INVOKABLE void setPlaybackSources(const QVariantList &sources, int currentSource, int currentEpisode);
    Q_INVOKABLE void acceptSourceSwitch();
    Q_INVOKABLE void rejectSourceSwitch();

    Q_INVOKABLE void setVolume(float volume);
    Q_INVOKABLE void setMuted(bool muted);
    Q_INVOKABLE void saveScreenshot();
    Q_INVOKABLE void setPlaybackRate(float rate);
    Q_INVOKABLE void setRepeatMode(RepeatMode mode);
    Q_INVOKABLE void switchAudioTrack(int streamIndex);
    Q_INVOKABLE void switchSubtitleTrack(int streamIndex);
    Q_INVOKABLE void setSubtitlesEnabled(bool enabled);
    Q_INVOKABLE void cycleAspectRatio();
    Q_INVOKABLE void requestThumbnail(qint64 positionMs);
    void setLoading(bool loading);
    Q_INVOKABLE void playNext();
    Q_INVOKABLE void playPrevious();

signals:
    void playbackStateChanged();
    void timelineChanged();
    void seekingChanged();
    void volumeChanged();
    void mutedChanged();
    void currentFileChanged();
    void playbackRateChanged();
    void repeatModeChanged();
    void currentIndexChanged();
    void loadingChanged();
    void thumbnailPreviewChanged();
    void thumbnailReadyForProvider(const QString &key, const QImage &image);
    void subtitlesEnabledChanged();
    void currentVodNameChanged();
    void playlistEpisodeRequested(int index, const QString &title);
    void playbackEnded();
    void playbackFailed(const QString &message);
    void imageStateChanged();
    void imageCacheChanged();
    void memeStateChanged();
    void shortVideoChanged();
    void imagePrefetched(const QString &apiUrl, const QString &imageUrl);
    void shortVideoPrefetched(int sourceIndex, const QString &videoUrl, const QString &label);
    void voiceChanged();
    void musicChanged();
    void musicLyricIndexChanged();
    void hotNewsChanged();
    void updateStateChanged();
    void sourceSwitchSuggestionChanged();

private:
    PlayerEngine engine_;
    MediaInfoModel mediaInfoModel_;
    PlaylistModel playlistModel_;
    PlaylistModel historyModel_;
    RuntimeLogModel runtimeLogModel_;
    VideoSearchModel videoSearchModel_;
    VideoSearchModel detailSearchModel_;
    VideoSearchModel sourceSearchModel_;
    ApiSiteModel apiSiteModel_;
    DmghgAnimeModel dmghgAnimeModel_;
    BeeVideoModel beeVideoModel_;
    BeeScheduleModel beeScheduleModel_;
    DownloadModel downloadModel_;
    ImageCacheService imageCache_;
    MpvBackend mpvBackend_;
    QMediaPlayer *qtPlayer_ = nullptr;
    QAudioOutput *qtAudioOutput_ = nullptr;
    QVideoSink *qtVideoSink_ = nullptr;
    QTimer *seekWatchdog_ = nullptr;
    QTimer *playbackLoadGuardTimer_ = nullptr;
    QTimer *cursorResetTimer_ = nullptr;
    QTimer *firstFrameTimer_ = nullptr;
    QPointer<QNetworkReply> hlsFilterReply_;
    HlsPlaylistProxy *hlsPlaylistProxy_ = nullptr;
    QWindow *mpvParentWindow_ = nullptr;
    QWindow *mpvVideoWindow_ = nullptr;
    bool mpvSurfaceVisible_ = true;
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
    QString currentVodName_;
    QVariantList playbackSources_;
    int currentPlaybackSource_ = -1;
    int currentPlaybackEpisode_ = -1;
    int pendingPlaybackSource_ = -1;
    bool sourceSwitchSuggested_ = false;
    QString sourceSwitchReason_;
    QString sourceSwitchCandidate_;
    QHash<int, qint64> sourceSuggestionCooldowns_;
    qint64 pendingSourceSeekMs_ = -1;
    bool pendingSourcePaused_ = false;
    bool imageLoading_ = false;
    QString currentImageUrl_;
    QString currentImageDisplayUrl_;
    QString imageMessage_;
    QByteArray currentImageBytes_;
    QString currentImageContentType_;
    qint64 lastImageLoadRequestMs_ = 0;
    bool memeLoading_ = false;
    QVariantList memeImages_;
    QString memeMessage_;
    QString currentShortVideoUrl_;
    QString shortVideoMessage_;
    QString suspendedVideoUrl_;
    qint64 suspendedVideoPositionMs_ = 0;
    bool suspendedVideoWasPlaying_ = false;
    bool suspendedVideoWasPaused_ = false;
    bool suspendedVideoRestorePending_ = false;
    QString currentVoiceUrl_;
    QString currentVoiceFilePath_;
    QString voiceMessage_;
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
    bool hotNewsLoading_ = false;
    QVariantList hotNewsItems_;
    QString hotNewsMessage_;
    bool updateChecking_ = false;
    bool updateDownloading_ = false;
    bool updateAvailable_ = false;
    QString updateVersion_;
    QString updateMessage_;
    QString updateAssetUrl_;
    QString updateAssetName_;
    double updateDownloadProgress_ = 0.0;

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
    void loadSettings();
    void saveSettings();
    void recordPlaybackHistory(const QString &filePath, const QString &title);
    void setImageState(bool loading, const QString &url, const QString &message, const QString &displayUrl = QString());
    static QString localImageFileUrl(const QString &path);
    bool loadImageBytesFromLocalUrl(const QString &url);
    QString cacheImageBytes(const QString &apiUrl, const QString &sourceUrl,
                            const QByteArray &bytes, const QString &mimeType);
    void saveImageBytes(const QByteArray &bytes, const QUrl &sourceUrl, const QString &contentType);
    static QString extractImageUrlFromResponse(const QByteArray &bytes, const QUrl &responseUrl, const QString &contentType);
    static QStringList extractImageUrlsFromResponse(const QByteArray &bytes, const QUrl &responseUrl, const QString &contentType);
    static QString imageExtension(const QUrl &url, const QString &contentType);
    void setCurrentMusicLrc(const QString &lrc);
    void updateCurrentMusicLyricIndex();
    void loadMusicLyricsForCurrentTrack(bool forceRefresh = false);
    bool loadLocalMusicLyrics();
    static QVariantList parseMusicLyricTrack(const QString &text, const QString &fieldName);
    QUrl voiceRequestUrl(int sourceIndex, QString *label = nullptr) const;
    void fetchVoiceFile(int sourceIndex, const QString &actionText, const std::function<void(const QString &, const QString &, const QString &)> &onReady);
    static QString audioExtension(const QUrl &url, const QString &contentType);
    void setUpdateState(bool checking, bool downloading, bool available, const QString &message);
    static int compareVersions(const QString &left, const QString &right);
    int bestAlternativeSource() const;
    void suggestSourceSwitch(const QString &reason);
    void clearSourceSwitchSuggestion();
    void armFirstFrameDetection();
    void markPlaybackProgress();
    void suspendCurrentVideoForShortVideo();
    void restoreSuspendedVideoAfterShortVideo();
    void applyPendingSuspendedVideoState();
};
