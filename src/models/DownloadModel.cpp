#include "models/DownloadModel.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSharedPointer>
#include <QUrl>

#include <functional>

#include "infrastructure/Logger.h"

DownloadModel::DownloadModel(QObject *parent)
    : QAbstractListModel(parent) {
    loadIndex();
}

int DownloadModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent)
    return videos_.size();
}

QVariant DownloadModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= videos_.size()) return {};
    const auto &video = videos_[index.row()];
    int doneCount = 0;
    int progressTotal = 0;
    QString status = QStringLiteral("等待下载");
    for (const auto &episode : video.episodes) {
        if (episode.status == QStringLiteral("完成")) ++doneCount;
        if (episode.status == QStringLiteral("下载中")) status = QStringLiteral("下载中");
        else if (status != QStringLiteral("下载中") && episode.status == QStringLiteral("失败")) status = QStringLiteral("有失败");
        progressTotal += episode.progress;
    }
    if (!video.episodes.isEmpty() && doneCount == video.episodes.size()) status = QStringLiteral("已完成");
    const int progress = video.episodes.isEmpty() ? 0 : progressTotal / video.episodes.size();

    switch (role) {
    case NameRole: return video.name;
    case PosterRole: return video.poster;
    case FolderPathRole: return video.folderPath;
    case TotalCountRole: return video.episodes.size();
    case DoneCountRole: return doneCount;
    case StatusRole: return status;
    case ProgressRole: return progress;
    default: return {};
    }
}

QHash<int, QByteArray> DownloadModel::roleNames() const {
    return {
        {NameRole, "name"},
        {PosterRole, "poster"},
        {FolderPathRole, "folderPath"},
        {TotalCountRole, "totalCount"},
        {DoneCountRole, "doneCount"},
        {StatusRole, "status"},
        {ProgressRole, "progress"}
    };
}

int DownloadModel::count() const { return videos_.size(); }
bool DownloadModel::active() const { return active_; }
int DownloadModel::revision() const { return revision_; }

QString DownloadModel::rootFolder() const {
    return videoRootPath();
}

void DownloadModel::enqueueVideo(const QString &videoName, const QString &poster, const QVariantList &episodes) {
    enqueueVideoInternal(videoName, poster, episodes, false);
}

void DownloadModel::enqueueVideoM3u8Only(const QString &videoName, const QString &poster, const QVariantList &episodes) {
    enqueueVideoInternal(videoName, poster, episodes, true);
}

void DownloadModel::enqueueVideoInternal(const QString &videoName,
                                         const QString &poster,
                                         const QVariantList &episodes,
                                         bool m3u8Only) {
    const QString trimmedName = videoName.trimmed().isEmpty() ? QStringLiteral("未命名视频") : videoName.trimmed();
    int videoIndex = findVideo(trimmedName);
    if (videoIndex < 0) {
        DownloadVideo video;
        video.name = trimmedName;
        video.poster = poster;
        video.folderPath = QDir(videoRootPath()).filePath(safeName(trimmedName));
        QDir().mkpath(video.folderPath);

        beginInsertRows(QModelIndex(), videos_.size(), videos_.size());
        videos_.append(video);
        videoIndex = videos_.size() - 1;
        endInsertRows();
        emit countChanged();
    } else if (!poster.trimmed().isEmpty() && videos_[videoIndex].poster != poster) {
        videos_[videoIndex].poster = poster;
        emit dataChanged(index(videoIndex), index(videoIndex));
    }

    bool changed = false;
    for (const QVariant &value : episodes) {
        const QVariantMap map = value.toMap();
        const QString url = map.value(QStringLiteral("url")).toString().trimmed();
        if (url.isEmpty()) continue;
        const int existingEpisodeIndex = findEpisode(videoIndex, url);
        if (existingEpisodeIndex >= 0) {
            auto &episode = videos_[videoIndex].episodes[existingEpisodeIndex];
            if (episode.status != QStringLiteral("完成") &&
                episode.status != QStringLiteral("排队中") &&
                episode.status != QStringLiteral("下载中")) {
                episode.title = map.value(QStringLiteral("title")).toString().trimmed();
                if (episode.title.isEmpty()) episode.title = map.value(QStringLiteral("name")).toString().trimmed();
                if (episode.title.isEmpty()) episode.title = QStringLiteral("第 %1 集").arg(existingEpisodeIndex + 1);
                episode.localPath.clear();
                episode.m3u8Only = m3u8Only;
                episode.status = QStringLiteral("排队中");
                episode.progress = 0;
                queue_.enqueue({videoIndex, existingEpisodeIndex});
                changed = true;
            }
            continue;
        }

        DownloadEpisode episode;
        episode.title = map.value(QStringLiteral("title")).toString().trimmed();
        if (episode.title.isEmpty()) episode.title = map.value(QStringLiteral("name")).toString().trimmed();
        if (episode.title.isEmpty()) episode.title = QStringLiteral("第 %1 集").arg(videos_[videoIndex].episodes.size() + 1);
        episode.url = url;
        episode.m3u8Only = m3u8Only;
        episode.status = QStringLiteral("排队中");
        episode.progress = 0;
        videos_[videoIndex].episodes.append(episode);
        queue_.enqueue({videoIndex, static_cast<int>(videos_[videoIndex].episodes.size()) - 1});
        changed = true;
    }

    if (changed) {
        saveIndex();
        emit dataChanged(index(videoIndex), index(videoIndex));
        bumpRevision();
        startNext();
    }
}

QVariantList DownloadModel::episodesForVideo(int videoIndex) const {
    QVariantList list;
    if (videoIndex < 0 || videoIndex >= videos_.size()) return list;
    for (const auto &episode : videos_[videoIndex].episodes) {
        QVariantMap map;
        map[QStringLiteral("title")] = episode.title;
        map[QStringLiteral("url")] = episode.url;
        map[QStringLiteral("localPath")] = episode.localPath;
        map[QStringLiteral("status")] = episode.status;
        map[QStringLiteral("progress")] = episode.progress;
        map[QStringLiteral("m3u8Only")] = episode.m3u8Only;
        list.append(map);
    }
    return list;
}

void DownloadModel::openVideoFolder(int videoIndex) const {
    if (videoIndex < 0 || videoIndex >= videos_.size()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(videos_[videoIndex].folderPath));
}

bool DownloadModel::deleteVideo(int videoIndex, bool removeFiles) {
    if (active_ || videoIndex < 0 || videoIndex >= videos_.size()) return false;

    const QString folderPath = videos_[videoIndex].folderPath;
    removeQueuedTasksForVideo(videoIndex);

    beginRemoveRows(QModelIndex(), videoIndex, videoIndex);
    videos_.removeAt(videoIndex);
    endRemoveRows();

    if (removeFiles) {
        removePathInsideDownloadRoot(folderPath);
    }

    saveIndex();
    emit countChanged();
    bumpRevision();
    return true;
}

bool DownloadModel::deleteEpisode(int videoIndex, int episodeIndex, bool removeFiles) {
    if (active_ || videoIndex < 0 || videoIndex >= videos_.size() ||
        episodeIndex < 0 || episodeIndex >= videos_[videoIndex].episodes.size()) {
        return false;
    }

    const DownloadEpisode episode = videos_[videoIndex].episodes[episodeIndex];
    removeQueuedTasksForEpisode(videoIndex, episodeIndex);

    if (removeFiles) {
        if (!episode.localPath.trimmed().isEmpty()) {
            const QFileInfo localInfo(episode.localPath);
            if (localInfo.fileName().compare(QStringLiteral("index.m3u8"), Qt::CaseInsensitive) == 0) {
                removePathInsideDownloadRoot(localInfo.absolutePath());
            } else {
                removePathInsideDownloadRoot(episode.localPath);
            }
        }
        removePathInsideDownloadRoot(QDir(videos_[videoIndex].folderPath).filePath(safeName(episode.title)));
    }

    videos_[videoIndex].episodes.removeAt(episodeIndex);
    if (videos_[videoIndex].episodes.isEmpty()) {
        return deleteVideo(videoIndex, removeFiles);
    }

    saveIndex();
    emit dataChanged(index(videoIndex), index(videoIndex));
    bumpRevision();
    return true;
}

bool DownloadModel::retryEpisode(int videoIndex, int episodeIndex) {
    if (active_ || videoIndex < 0 || videoIndex >= videos_.size() ||
        episodeIndex < 0 || episodeIndex >= videos_[videoIndex].episodes.size()) {
        return false;
    }

    auto &episode = videos_[videoIndex].episodes[episodeIndex];
    if (episode.status == QStringLiteral("完成") ||
        episode.status == QStringLiteral("排队中") ||
        episode.status == QStringLiteral("下载中")) {
        return false;
    }

    episode.localPath.clear();
    episode.status = QStringLiteral("排队中");
    episode.progress = 0;
    queue_.enqueue({videoIndex, episodeIndex});
    saveIndex();
    emit dataChanged(index(videoIndex), index(videoIndex));
    bumpRevision();
    startNext();
    return true;
}

QString DownloadModel::indexPath() const {
    return QDir(videoRootPath()).filePath(QStringLiteral("downloads.json"));
}

QString DownloadModel::videoRootPath() const {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("download/video"));
}

void DownloadModel::loadIndex() {
    QDir().mkpath(videoRootPath());
    QFile file(indexPath());
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonArray videos = doc.object().value(QStringLiteral("videos")).toArray();
    beginResetModel();
    videos_.clear();
    for (const QJsonValue &videoValue : videos) {
        const QJsonObject videoObject = videoValue.toObject();
        DownloadVideo video;
        video.name = videoObject.value(QStringLiteral("name")).toString();
        video.poster = videoObject.value(QStringLiteral("poster")).toString();
        video.folderPath = videoObject.value(QStringLiteral("folderPath")).toString();
        const QJsonArray episodes = videoObject.value(QStringLiteral("episodes")).toArray();
        for (const QJsonValue &episodeValue : episodes) {
            const QJsonObject episodeObject = episodeValue.toObject();
            DownloadEpisode episode;
            episode.title = episodeObject.value(QStringLiteral("title")).toString();
            episode.url = episodeObject.value(QStringLiteral("url")).toString();
            episode.localPath = episodeObject.value(QStringLiteral("localPath")).toString();
            episode.status = episodeObject.value(QStringLiteral("status")).toString(QStringLiteral("排队中"));
            episode.progress = episodeObject.value(QStringLiteral("progress")).toInt();
            episode.m3u8Only = episodeObject.value(QStringLiteral("m3u8Only")).toBool(false);
            if (episode.status == QStringLiteral("下载中")) episode.status = QStringLiteral("排队中");
            video.episodes.append(episode);
        }
        if (!video.name.isEmpty()) videos_.append(video);
    }
    endResetModel();
    emit countChanged();
    bumpRevision();
}

void DownloadModel::saveIndex() const {
    QDir().mkpath(videoRootPath());
    QJsonArray videos;
    for (const auto &video : videos_) {
        QJsonObject videoObject;
        videoObject[QStringLiteral("name")] = video.name;
        videoObject[QStringLiteral("poster")] = video.poster;
        videoObject[QStringLiteral("folderPath")] = video.folderPath;
        QJsonArray episodes;
        for (const auto &episode : video.episodes) {
            QJsonObject episodeObject;
            episodeObject[QStringLiteral("title")] = episode.title;
            episodeObject[QStringLiteral("url")] = episode.url;
            episodeObject[QStringLiteral("localPath")] = episode.localPath;
            episodeObject[QStringLiteral("status")] = episode.status;
            episodeObject[QStringLiteral("progress")] = episode.progress;
            episodeObject[QStringLiteral("m3u8Only")] = episode.m3u8Only;
            episodes.append(episodeObject);
        }
        videoObject[QStringLiteral("episodes")] = episodes;
        videos.append(videoObject);
    }
    QFile file(indexPath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QJsonObject root;
        root[QStringLiteral("videos")] = videos;
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

void DownloadModel::bumpRevision() {
    ++revision_;
    emit revisionChanged();
}

int DownloadModel::findVideo(const QString &videoName) const {
    for (int i = 0; i < videos_.size(); ++i) {
        if (videos_[i].name == videoName) return i;
    }
    return -1;
}

int DownloadModel::findEpisode(int videoIndex, const QString &url) const {
    if (videoIndex < 0 || videoIndex >= videos_.size()) return -1;
    const auto &episodes = videos_[videoIndex].episodes;
    for (int i = 0; i < episodes.size(); ++i) {
        if (episodes[i].url == url) return i;
    }
    return -1;
}

void DownloadModel::startNext() {
    if (active_) return;
    if (queue_.isEmpty()) {
        active_ = false;
        emit activeChanged();
        return;
    }
    active_ = true;
    emit activeChanged();
    const DownloadTask task = queue_.dequeue();
    downloadEpisode(task.videoIndex, task.episodeIndex);
}

void DownloadModel::finishCurrentTask() {
    active_ = false;
    emit activeChanged();
    startNext();
}

void DownloadModel::downloadEpisode(int videoIndex, int episodeIndex) {
    if (videoIndex < 0 || videoIndex >= videos_.size() ||
        episodeIndex < 0 || episodeIndex >= videos_[videoIndex].episodes.size()) {
        active_ = false;
        emit activeChanged();
        startNext();
        return;
    }
    setEpisodeState(videoIndex, episodeIndex, QStringLiteral("下载中"), 1);
    resolveAndDownload(videoIndex, episodeIndex, videos_[videoIndex].episodes[episodeIndex].url);
}

void DownloadModel::resolveAndDownload(int videoIndex, int episodeIndex, const QString &url) {
    const QString lower = url.toLower();
    if (lower.contains(QStringLiteral(".m3u8"))) {
        downloadM3u8(videoIndex, episodeIndex, url);
        return;
    }
    if (lower.contains(QStringLiteral(".mp4")) || lower.contains(QStringLiteral(".ts"))) {
        if (videos_[videoIndex].episodes[episodeIndex].m3u8Only) {
            failEpisode(videoIndex, episodeIndex, QStringLiteral("当前链接不是 m3u8，已跳过 ts/mp4 下载"));
            return;
        }
        downloadDirectFile(videoIndex, episodeIndex, url);
        return;
    }

    QUrl requestUrl(url);
    QNetworkRequest req{requestUrl};
    applyRequestHeaders(req, requestUrl);
    auto *reply = nam_.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, videoIndex, episodeIndex, url]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            failEpisode(videoIndex, episodeIndex, reply->errorString());
            return;
        }
        const QString html = QString::fromUtf8(reply->readAll());
        static QRegularExpression re("var\\s+main\\s*=\\s*\"([^\"]+\\.m3u8[^\"]*)\"");
        const auto match = re.match(html);
        if (!match.hasMatch()) {
            failEpisode(videoIndex, episodeIndex, QStringLiteral("无法从分享页解析 m3u8"));
            return;
        }
        QUrl base(url);
        const QUrl resolved = base.resolved(QUrl(match.captured(1)));
        downloadM3u8(videoIndex, episodeIndex, resolved.toString());
    });
}

void DownloadModel::downloadDirectFile(int videoIndex, int episodeIndex, const QString &url) {
    const QString ext = extensionFromUrl(url, QStringLiteral(".mp4"));
    const QString fileName = safeName(videos_[videoIndex].episodes[episodeIndex].title) + ext;
    const QString filePath = QDir(videos_[videoIndex].folderPath).filePath(fileName);
    QUrl requestUrl(url);
    QNetworkRequest req{requestUrl};
    applyRequestHeaders(req, requestUrl);
    auto *reply = nam_.get(req);
    connect(reply, &QNetworkReply::downloadProgress, this, [this, videoIndex, episodeIndex](qint64 received, qint64 total) {
        if (total > 0) setEpisodeState(videoIndex, episodeIndex, QStringLiteral("下载中"), qBound(1, int(received * 100 / total), 99));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, videoIndex, episodeIndex, filePath]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QFile file(filePath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                file.write(reply->readAll());
                videos_[videoIndex].episodes[episodeIndex].localPath = filePath;
                setEpisodeState(videoIndex, episodeIndex, QStringLiteral("完成"), 100);
            } else {
                failEpisode(videoIndex, episodeIndex, QStringLiteral("无法写入文件: %1").arg(filePath));
                return;
            }
        } else {
            failEpisode(videoIndex, episodeIndex, reply->errorString());
            return;
        }
        finishCurrentTask();
    });
}

void DownloadModel::downloadM3u8(int videoIndex, int episodeIndex, const QString &url) {
    QUrl requestUrl(url);
    QNetworkRequest req{requestUrl};
    applyRequestHeaders(req, requestUrl);
    auto *reply = nam_.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, videoIndex, episodeIndex, url]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            failEpisode(videoIndex, episodeIndex, reply->errorString());
            return;
        }
        downloadM3u8Segments(videoIndex, episodeIndex, url, QString::fromUtf8(reply->readAll()));
    });
}

void DownloadModel::downloadM3u8Segments(int videoIndex,
                                         int episodeIndex,
                                         const QString &playlistUrl,
                                         const QString &playlistText) {
    QStringList lines = playlistText.split('\n');
    QVector<int> mediaLineIndexes;
    QVector<QUrl> mediaUrls;
    QUrl baseUrl(playlistUrl);
    bool hasMediaSegments = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (line.startsWith(QStringLiteral("#EXTINF"))) {
            hasMediaSegments = true;
        }
        if (line.isEmpty() || line.startsWith('#')) continue;
        mediaLineIndexes.append(i);
        mediaUrls.append(baseUrl.resolved(QUrl(line)));
    }
    if (!hasMediaSegments && !mediaUrls.isEmpty()) {
        Logger::instance().info(QStringLiteral("Download m3u8 variant playlist: %1")
            .arg(mediaUrls.last().toString()));
        downloadM3u8(videoIndex, episodeIndex, mediaUrls.last().toString());
        return;
    }
    if (videos_[videoIndex].episodes[episodeIndex].m3u8Only) {
        saveM3u8Only(videoIndex, episodeIndex, playlistUrl, playlistText);
        return;
    }
    if (mediaUrls.isEmpty()) {
        failEpisode(videoIndex, episodeIndex, QStringLiteral("m3u8 没有可下载分片"));
        return;
    }

    const QString episodeDirName = safeName(videos_[videoIndex].episodes[episodeIndex].title);
    const QString episodeDir = QDir(videos_[videoIndex].folderPath).filePath(episodeDirName);
    QDir().mkpath(episodeDir);

    auto linePtr = QSharedPointer<QStringList>::create(lines);
    auto indexPtr = QSharedPointer<int>::create(0);
    auto downloadNext = QSharedPointer<std::function<void()>>::create();
    *downloadNext = [this, videoIndex, episodeIndex, mediaUrls, mediaLineIndexes, episodeDir, linePtr, indexPtr, downloadNext]() {
        if (*indexPtr >= mediaUrls.size()) {
            const QString playlistPath = QDir(episodeDir).filePath(QStringLiteral("index.m3u8"));
            QFile playlist(playlistPath);
            if (playlist.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                playlist.write(linePtr->join('\n').toUtf8());
                videos_[videoIndex].episodes[episodeIndex].localPath = playlistPath;
                setEpisodeState(videoIndex, episodeIndex, QStringLiteral("完成"), 100);
            } else {
                failEpisode(videoIndex, episodeIndex, QStringLiteral("无法写入播放列表: %1").arg(playlistPath));
                return;
            }
            finishCurrentTask();
            return;
        }

        const int current = *indexPtr;
        const QString ext = extensionFromUrl(mediaUrls[current].toString(), QStringLiteral(".ts"));
        const QString segmentName = QStringLiteral("segment_%1%2").arg(current, 5, 10, QLatin1Char('0')).arg(ext);
        const QString segmentPath = QDir(episodeDir).filePath(segmentName);
        (*linePtr)[mediaLineIndexes[current]] = segmentName;

        QNetworkRequest req(mediaUrls[current]);
        applyRequestHeaders(req, mediaUrls[current]);
        auto *reply = nam_.get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, videoIndex, episodeIndex, segmentPath, indexPtr, mediaUrls, downloadNext]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                failEpisode(videoIndex, episodeIndex, reply->errorString());
                return;
            }
            QFile file(segmentPath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                failEpisode(videoIndex, episodeIndex, QStringLiteral("无法写入分片: %1").arg(segmentPath));
                return;
            }
            file.write(reply->readAll());
            ++(*indexPtr);
            setEpisodeState(videoIndex, episodeIndex, QStringLiteral("下载中"), qBound(1, int((*indexPtr) * 100 / mediaUrls.size()), 99));
            (*downloadNext)();
        });
    };
    (*downloadNext)();
}

void DownloadModel::saveM3u8Only(int videoIndex,
                                 int episodeIndex,
                                 const QString &playlistUrl,
                                 const QString &playlistText) {
    QStringList lines = playlistText.split('\n');
    QUrl baseUrl(playlistUrl);
    bool hasMediaSegments = false;
    bool hasMediaUrls = false;

    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines[i].trimmed();
        if (trimmed.startsWith(QStringLiteral("#EXTINF"))) {
            hasMediaSegments = true;
        }
        if (trimmed.isEmpty() || trimmed.startsWith('#')) continue;
        hasMediaUrls = true;
        lines[i] = baseUrl.resolved(QUrl(trimmed)).toString();
    }

    if (!hasMediaSegments || !hasMediaUrls) {
        failEpisode(videoIndex, episodeIndex, QStringLiteral("m3u8 没有可保存的媒体分片"));
        return;
    }

    const QString fileName = safeName(videos_[videoIndex].episodes[episodeIndex].title) + QStringLiteral(".m3u8");
    const QString playlistPath = QDir(videos_[videoIndex].folderPath).filePath(fileName);
    QFile playlist(playlistPath);
    if (!playlist.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        failEpisode(videoIndex, episodeIndex, QStringLiteral("无法写入 m3u8: %1").arg(playlistPath));
        return;
    }

    playlist.write(lines.join('\n').toUtf8());
    videos_[videoIndex].episodes[episodeIndex].localPath = playlistPath;
    setEpisodeState(videoIndex, episodeIndex, QStringLiteral("完成"), 100);
    finishCurrentTask();
}

void DownloadModel::setEpisodeState(int videoIndex, int episodeIndex, const QString &status, int progress) {
    if (videoIndex < 0 || videoIndex >= videos_.size() ||
        episodeIndex < 0 || episodeIndex >= videos_[videoIndex].episodes.size()) return;
    auto &episode = videos_[videoIndex].episodes[episodeIndex];
    episode.status = status;
    episode.progress = progress;
    saveIndex();
    emit dataChanged(index(videoIndex), index(videoIndex));
    bumpRevision();
    Logger::instance().info(QStringLiteral("Download %1/%2: %3 %4%")
        .arg(videos_[videoIndex].name, episode.title, status)
        .arg(progress));
}

void DownloadModel::failEpisode(int videoIndex, int episodeIndex, const QString &reason) {
    QString episodeTitle;
    QString episodeUrl;
    if (videoIndex >= 0 && videoIndex < videos_.size() &&
        episodeIndex >= 0 && episodeIndex < videos_[videoIndex].episodes.size()) {
        episodeTitle = videos_[videoIndex].episodes[episodeIndex].title;
        episodeUrl = videos_[videoIndex].episodes[episodeIndex].url;
    }
    Logger::instance().error(QStringLiteral("Download failed: %1 %2 %3")
        .arg(episodeTitle, episodeUrl, reason));
    setEpisodeState(videoIndex, episodeIndex, QStringLiteral("失败"), 0);
    finishCurrentTask();
}

void DownloadModel::applyRequestHeaders(QNetworkRequest &request, const QUrl &url) const {
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    request.setRawHeader("User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/125.0 Safari/537.36");
    request.setRawHeader("Accept", "*/*");
    request.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8");
    request.setRawHeader("Connection", "keep-alive");
    if (!url.scheme().isEmpty() && !url.host().isEmpty()) {
        const QByteArray origin = (url.scheme() + QStringLiteral("://") + url.host()).toUtf8();
        request.setRawHeader("Origin", origin);
        request.setRawHeader("Referer", origin + "/");
    }
}

void DownloadModel::removeQueuedTasksForVideo(int videoIndex) {
    QQueue<DownloadTask> filtered;
    while (!queue_.isEmpty()) {
        DownloadTask task = queue_.dequeue();
        if (task.videoIndex == videoIndex) continue;
        if (task.videoIndex > videoIndex) --task.videoIndex;
        filtered.enqueue(task);
    }
    queue_ = filtered;
}

void DownloadModel::removeQueuedTasksForEpisode(int videoIndex, int episodeIndex) {
    QQueue<DownloadTask> filtered;
    while (!queue_.isEmpty()) {
        DownloadTask task = queue_.dequeue();
        if (task.videoIndex == videoIndex && task.episodeIndex == episodeIndex) continue;
        if (task.videoIndex == videoIndex && task.episodeIndex > episodeIndex) --task.episodeIndex;
        filtered.enqueue(task);
    }
    queue_ = filtered;
}

bool DownloadModel::removePathInsideDownloadRoot(const QString &path) const {
    if (path.trimmed().isEmpty()) return false;

    const QString root = QDir::cleanPath(QDir(videoRootPath()).absolutePath()).replace('\\', '/');
    const QString target = QDir::cleanPath(QFileInfo(path).absoluteFilePath()).replace('\\', '/');
    if (!target.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive) &&
        target.compare(root, Qt::CaseInsensitive) != 0) {
        Logger::instance().warn(QStringLiteral("Refuse to delete outside download root: %1").arg(target));
        return false;
    }

    QFileInfo info(target);
    if (!info.exists()) return true;
    if (info.isDir()) {
        return QDir(target).removeRecursively();
    }
    return QFile::remove(target);
}

QString DownloadModel::safeName(QString text) {
    text = text.trimmed();
    if (text.isEmpty()) text = QStringLiteral("untitled");
    text.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\r\\n\\t]")), QStringLiteral("_"));
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text.left(80);
}

QString DownloadModel::extensionFromUrl(const QString &url, const QString &fallback) {
    const QString path = QUrl(url).path().toLower();
    const QString suffix = QFileInfo(path).suffix();
    if (!suffix.isEmpty() && suffix.size() <= 5) return QStringLiteral(".") + suffix;
    return fallback;
}
