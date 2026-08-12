#pragma once

#include <QAbstractListModel>
#include <QNetworkAccessManager>
#include <QQueue>
#include <QString>
#include <QVariantList>
#include <QVector>

class QNetworkRequest;

struct DownloadEpisode {
    QString title;
    QString url;
    QString localPath;
    QString status;
    int progress = 0;
    bool m3u8Only = false;
};

struct DownloadVideo {
    QString name;
    QString poster;
    QString folderPath;
    QVector<DownloadEpisode> episodes;
};

class DownloadModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PosterRole,
        FolderPathRole,
        TotalCountRole,
        DoneCountRole,
        StatusRole,
        ProgressRole
    };

    explicit DownloadModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    bool active() const;
    int revision() const;

    Q_INVOKABLE void enqueueVideo(const QString &videoName,
                                  const QString &poster,
                                  const QVariantList &episodes);
    Q_INVOKABLE void enqueueVideoM3u8Only(const QString &videoName,
                                          const QString &poster,
                                          const QVariantList &episodes);
    Q_INVOKABLE QVariantList episodesForVideo(int videoIndex) const;
    Q_INVOKABLE void openVideoFolder(int videoIndex) const;
    Q_INVOKABLE bool deleteVideo(int videoIndex, bool removeFiles = true);
    Q_INVOKABLE bool deleteEpisode(int videoIndex, int episodeIndex, bool removeFiles = true);
    Q_INVOKABLE bool retryEpisode(int videoIndex, int episodeIndex);
    Q_INVOKABLE QString rootFolder() const;

signals:
    void countChanged();
    void activeChanged();
    void revisionChanged();

private:
    struct DownloadTask {
        int videoIndex = -1;
        int episodeIndex = -1;
    };

    QVector<DownloadVideo> videos_;
    QNetworkAccessManager nam_;
    QQueue<DownloadTask> queue_;
    bool active_ = false;
    int revision_ = 0;

    QString indexPath() const;
    QString videoRootPath() const;
    void loadIndex();
    void saveIndex() const;
    void bumpRevision();
    int findVideo(const QString &videoName) const;
    int findEpisode(int videoIndex, const QString &url) const;
    void enqueueVideoInternal(const QString &videoName,
                              const QString &poster,
                              const QVariantList &episodes,
                              bool m3u8Only);
    void startNext();
    void downloadEpisode(int videoIndex, int episodeIndex);
    void resolveAndDownload(int videoIndex, int episodeIndex, const QString &url);
    void downloadDirectFile(int videoIndex, int episodeIndex, const QString &url);
    void downloadM3u8(int videoIndex, int episodeIndex, const QString &url);
    void downloadM3u8Segments(int videoIndex,
                              int episodeIndex,
                              const QString &playlistUrl,
                              const QString &playlistText);
    void saveM3u8Only(int videoIndex,
                      int episodeIndex,
                      const QString &playlistUrl,
                      const QString &playlistText);
    void setEpisodeState(int videoIndex, int episodeIndex, const QString &status, int progress);
    void failEpisode(int videoIndex, int episodeIndex, const QString &reason);
    void finishCurrentTask();
    void applyRequestHeaders(QNetworkRequest &request, const QUrl &url) const;
    void removeQueuedTasksForVideo(int videoIndex);
    void removeQueuedTasksForEpisode(int videoIndex, int episodeIndex);
    bool removePathInsideDownloadRoot(const QString &path) const;

    static QString safeName(QString text);
    static QString extensionFromUrl(const QString &url, const QString &fallback);
};
