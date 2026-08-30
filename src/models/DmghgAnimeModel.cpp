#include "DmghgAnimeModel.h"
#include "media/DmghgClient.h"

#include <QCoreApplication>
#include "infrastructure/Logger.h"
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kAnimeListWatchdogMs = 120000;
constexpr int kAnimePageSize = 20;
}

DmghgAnimeModel::DmghgAnimeModel(QObject *parent)
    : QAbstractListModel(parent) {
}

int DmghgAnimeModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent)
    return items_.size();
}

QVariant DmghgAnimeModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= items_.size())
        return {};
    const auto &item = items_[index.row()];
    switch (role) {
    case VodIdRole: return item.vodId;
    case VodNameRole: return item.vodName;
    case VodPicRole: return item.vodPic;
    case VodRemarksRole: return item.vodRemarks;
    case VodYearRole: return item.vodYear;
    case VodAreaRole: return item.vodArea;
    case TypeNameRole: return item.typeName;
    default: return {};
    }
}

QHash<int, QByteArray> DmghgAnimeModel::roleNames() const {
    return {
        {VodIdRole, "vodId"},
        {VodNameRole, "vodName"},
        {VodPicRole, "vodPic"},
        {VodRemarksRole, "vodRemarks"},
        {VodYearRole, "vodYear"},
        {VodAreaRole, "vodArea"},
        {TypeNameRole, "typeName"},
    };
}

bool DmghgAnimeModel::loading() const { return loading_; }
int DmghgAnimeModel::totalCount() const { return totalCount_; }
int DmghgAnimeModel::count() const { return items_.size(); }
int DmghgAnimeModel::currentPage() const { return currentPage_; }
int DmghgAnimeModel::totalPages() const { return totalPages_; }
QString DmghgAnimeModel::errorMessage() const { return errorMessage_; }
bool DmghgAnimeModel::detailLoading() const { return detailLoading_; }
QVariantMap DmghgAnimeModel::detail() const { return detail_; }
QVariantList DmghgAnimeModel::episodes() const { return episodes_; }
int DmghgAnimeModel::currentSource() const { return currentSource_; }

void DmghgAnimeModel::setCurrentSource(int source) {
    if (currentSource_ == source) return;
    currentSource_ = source;
    // 切换线路时刷新选集视图。
    if (source >= 0 && source < playSources_.size()) {
        episodes_ = playSources_.at(source).toMap()
            .value(QStringLiteral("episodes")).toList();
    } else {
        episodes_.clear();
    }
    emit detailChanged();
}

void DmghgAnimeModel::setLoading(bool loading) {
    if (loading_ == loading) return;
    loading_ = loading;
    emit loadingChanged();
}

void DmghgAnimeModel::setDetailLoading(bool loading) {
    if (detailLoading_ == loading) return;
    detailLoading_ = loading;
    emit detailLoadingChanged();
}

void DmghgAnimeModel::setErrorMessage(const QString &msg) {
    if (errorMessage_ == msg) return;
    errorMessage_ = msg;
    emit errorMessageChanged();
}

DmghgClient *DmghgAnimeModel::client() {
    static DmghgClient *instance = nullptr;
    if (!instance)
        instance = new DmghgClient(qApp);
    return instance;
}

bool DmghgAnimeModel::loadCachedPage(const QString &key, quint64 serial, bool hasTotal) {
    const auto it = pageCache_.constFind(key);
    if (it == pageCache_.constEnd())
        return false;
    if (it->expiresAt <= QDateTime::currentSecsSinceEpoch()) {
        pageCache_.remove(key);
        return false;
    }

    const CachedPage cached = it.value();
    QTimer::singleShot(0, this, [this, cached, serial, hasTotal]() {
        if (serial != requestSerial_)
            return;
        setLoading(false);
        publishSearchResults(cached.items);
        const int cachedTotal = hasTotal ? cached.total : cached.items.size();
        if (totalCount_ != cachedTotal) {
            totalCount_ = cachedTotal;
            emit totalCountChanged();
        }
        const int cachedPages = hasTotal
            ? (cached.total > 0 ? int(std::ceil(cached.total / double(kAnimePageSize))) : currentPage_)
            : (cached.items.size() < kAnimePageSize ? currentPage_ : currentPage_ + 1);
        if (totalPages_ != cachedPages) {
            totalPages_ = cachedPages;
            emit totalPagesChanged();
        }
    });
    return true;
}

void DmghgAnimeModel::saveCachedPage(const QString &key, const QVariantList &items, int total) {
    CachedPage cached;
    cached.items = items;
    cached.total = total;
    cached.expiresAt = QDateTime::currentSecsSinceEpoch() + 3600;
    pageCache_.insert(key, cached);
}

void DmghgAnimeModel::publishSearchResults(const QVariantList &items) {
    beginResetModel();
    items_.clear();
    for (const QVariant &v : items) {
        const QVariantMap m = v.toMap();
        Item item;
        item.vodId = m.value(QStringLiteral("vodId")).toInt();
        item.vodName = m.value(QStringLiteral("vodName")).toString();
        item.vodPic = m.value(QStringLiteral("vodPic")).toString();
        item.vodRemarks = m.value(QStringLiteral("vodRemarks")).toString();
        item.vodYear = m.value(QStringLiteral("vodYear")).toString();
        item.vodArea = m.value(QStringLiteral("vodArea")).toString();
        item.typeName = m.value(QStringLiteral("typeName")).toString();
        items_.append(item);
    }
    endResetModel();
    emit countChanged();
}

void DmghgAnimeModel::publishDetail(const QVariantMap &d) {
    detail_ = d;
    playSources_ = d.value(QStringLiteral("playSources")).toList();
    if (currentSource_ < 0 || currentSource_ >= playSources_.size())
        currentSource_ = 0;
    if (currentSource_ >= 0 && currentSource_ < playSources_.size())
        episodes_ = playSources_.at(currentSource_).toMap()
            .value(QStringLiteral("episodes")).toList();
    else
        episodes_.clear();
    emit detailChanged();
}

void DmghgAnimeModel::search(const QString &keyword, int page) {
    const quint64 serial = ++requestSerial_;
    setLoading(true);
    setErrorMessage({});
    currentPage_ = page;
    emit currentPageChanged();
    // 搜索协议/解析逻辑升级后不要复用旧版本可能缓存的空结果。
    const QString cacheKey = QStringLiteral("search:v2:%1:%2").arg(keyword.trimmed(), QString::number(page));
    if (loadCachedPage(cacheKey, serial, false))
        return;
    client()->search(keyword, page, kAnimePageSize, [this, serial, page, keyword](bool ok, const QVariantList &items, const QString &err) {
        if (serial != requestSerial_) return;
        setLoading(false);
        if (!ok) {
            setErrorMessage(err.isEmpty() ? QStringLiteral("搜索失败") : err);
            return;
        }
        publishSearchResults(items);
        saveCachedPage(QStringLiteral("search:v2:%1:%2").arg(keyword.trimmed(), QString::number(page)), items, items.size());
        // 总数未知,按当前页估算分页。
        if (totalCount_ < items.size()) {
            totalCount_ = items.size();
            emit totalCountChanged();
        }
        totalPages_ = items.size() < kAnimePageSize ? page : page + 1;
        emit totalPagesChanged();
    });
    QTimer::singleShot(kAnimeListWatchdogMs, this, [this, serial]() {
        if (serial != requestSerial_ || !loading_)
            return;
        ++requestSerial_;
        setLoading(false);
        setErrorMessage(QStringLiteral("动漫搜索请求超时,请稍后重试"));
        Logger::instance().warn(QStringLiteral("[DmghgAnimeModel] search watchdog expired"));
    });
}

void DmghgAnimeModel::loadList(int page, const QString &type, int channel) {
    const quint64 serial = ++requestSerial_;
    setLoading(true);
    setErrorMessage({});
    currentPage_ = page;
    emit currentPageChanged();
    const QString cacheKey = QStringLiteral("list:%1:%2:%3")
        .arg(QString::number(page), type.trimmed(), QString::number(channel));
    if (loadCachedPage(cacheKey, serial, true))
        return;
    client()->listVideos(channel, page, kAnimePageSize, QStringLiteral("hits"), type,
        [this, serial, page, cacheKey](bool ok, const QVariantList &items, int total, const QString &err) {
            if (serial != requestSerial_) return;
            setLoading(false);
            if (!ok) {
                setErrorMessage(err.isEmpty() ? QStringLiteral("列表加载失败") : err);
                return;
            }
            publishSearchResults(items);
            saveCachedPage(cacheKey, items, total);
            if (totalCount_ != total) {
                totalCount_ = total;
                emit totalCountChanged();
            }
            totalPages_ = total > 0 ? int(std::ceil(total / double(kAnimePageSize))) : page;
            emit totalPagesChanged();
        });
    QTimer::singleShot(kAnimeListWatchdogMs, this, [this, serial]() {
        if (serial != requestSerial_ || !loading_)
            return;
        ++requestSerial_;
        setLoading(false);
        setErrorMessage(QStringLiteral("动漫列表请求超时,请稍后重试"));
        Logger::instance().warn(QStringLiteral("[DmghgAnimeModel] list watchdog expired"));
    });
}

void DmghgAnimeModel::loadDetail(int vodId) {
    if (vodId <= 0) return;
    const quint64 serial = ++requestSerial_;
    detail_.clear();
    episodes_.clear();
    playSources_.clear();
    currentSource_ = 0;
    comments_.clear();
    emit detailChanged();
    emit commentsChanged();
    setDetailLoading(true);
    setErrorMessage({});
    client()->videoDetail(vodId, [this, serial](bool ok, const QVariantMap &d, const QString &err) {
        if (serial != requestSerial_) return;
        setDetailLoading(false);
        if (!ok) {
            setErrorMessage(err.isEmpty() ? QStringLiteral("详情加载失败") : err);
            return;
        }
        publishDetail(d);
    });
}

void DmghgAnimeModel::playEpisode(int vid, const QString &part) {
    if (vid <= 0 || part.isEmpty()) {
        emit episodeResolved({}, {}, QStringLiteral("缺少视频 ID 或集名"));
        return;
    }
    // 每次选集都使上一次尚未完成的播放解析失效,避免旧集结果覆盖新集。
    const quint64 serial = ++requestSerial_;
    setDetailLoading(true);
    // 第一步:play 接口拿 source 标识。
    QString playSource;
    if (currentSource_ >= 0 && currentSource_ < playSources_.size())
        playSource = playSources_.at(currentSource_).toMap()
            .value(QStringLiteral("name")).toString();
    client()->playVideo(vid, part, playSource,
        [this, vid, part, serial](bool ok, const QString &source, const QString &err) {
            if (serial != requestSerial_) return;
            if (!ok || source.isEmpty()) {
                setDetailLoading(false);
                emit episodeResolved({}, part, err.isEmpty() ? QStringLiteral("获取播放地址失败") : err);
                return;
            }
            // 第二步:二次解析拿真实流地址。
            client()->parseSource(source, [this, part, serial](bool ok2, const QVariantList &urls, const QString &err2) {
                if (serial != requestSerial_) return;
                setDetailLoading(false);
                if (!ok2 || urls.isEmpty()) {
                    emit episodeResolved({}, part, err2.isEmpty() ? QStringLiteral("解析播放地址失败") : err2);
                    return;
                }
                // 当前解析结果中 4K 地址为无前置片段的主片源，优先使用 4K。
                QVariantMap best;
                for (const QVariant &candidate : urls) {
                    const QVariantMap item = candidate.toMap();
                    const QString label = item.value(QStringLiteral("name")).toString();
                    if (label.contains(QStringLiteral("4K"), Qt::CaseInsensitive)) {
                        best = item;
                        break;
                    }
                }
                if (best.isEmpty())
                    best = urls.first().toMap();
                emit episodeResolved(best.value(QStringLiteral("url")).toString(), part, {});
            });
        });
}

void DmghgAnimeModel::clear() {
    ++requestSerial_;
    beginResetModel();
    items_.clear();
    endResetModel();
    totalCount_ = 0;
    currentPage_ = 1;
    totalPages_ = 1;
    setLoading(false);
    setDetailLoading(false);
    setErrorMessage({});
    detail_.clear();
    episodes_.clear();
    playSources_.clear();
    pageCache_.clear();
    currentSource_ = 0;
    danmaku_.clear();
    danmakuSeen_.clear();
    danmakuCursorMs_ = 0;
    danmakuWindows_ = 0;
    ++danmakuSerial_;
    emit danmakuChanged();
    emit countChanged();
    emit totalCountChanged();
    emit currentPageChanged();
    emit totalPagesChanged();
    emit detailChanged();
}

bool DmghgAnimeModel::commentsLoading() const { return commentsLoading_; }
QVariantList DmghgAnimeModel::comments() const { return comments_; }

void DmghgAnimeModel::loadComments(int vid, int page) {
    if (vid <= 0) return;
    commentsLoading_ = true; commentsVid_ = vid; emit commentsChanged();
    client()->comments(vid, page, 50, [this](bool ok, const QVariantList &items, int total, const QString &err) {
        Q_UNUSED(total); commentsLoading_ = false;
        if (ok) { comments_ = items; setErrorMessage({}); }
        else setErrorMessage(err.isEmpty() ? QStringLiteral("评论加载失败") : err);
        emit commentsChanged();
    });
}

void DmghgAnimeModel::submitComment(const QString &text) {
    if (text.trimmed().isEmpty()) return;
    client()->createComment(text.trimmed(), QString(), QString(), [this](bool ok, const QString &err) {
        if (!ok) setErrorMessage(err.isEmpty() ? QStringLiteral("发表评论失败,请先登录") : err);
        else loadComments(commentsVid_);
    });
}

QVariantList DmghgAnimeModel::danmaku() const { return danmaku_; }

void DmghgAnimeModel::loadDanmaku(int vid, const QString &part) {
    // 取当前线路的 play 源名(与 playEpisode 一致);空则 "cn"。
    QString playSource;
    if (currentSource_ >= 0 && currentSource_ < playSources_.size())
        playSource = playSources_.at(currentSource_).toMap()
            .value(QStringLiteral("name")).toString();
    if (playSource.isEmpty()) playSource = QStringLiteral("cn");

    // 弹幕加载用独立序号,不干扰选集解析(requestSerial_)。
    const quint64 serial = ++danmakuSerial_;
    danmaku_.clear();
    danmakuSeen_.clear();
    danmakuCursorMs_ = 0;
    danmakuWindows_ = 0;
    emit danmakuChanged();

    if (vid <= 0 || part.isEmpty()) return;
    fetchDanmakuWindow(serial, vid, part, playSource);
}

void DmghgAnimeModel::fetchDanmakuWindow(quint64 serial, int vid, const QString &part, const QString &play) {
    if (serial != danmakuSerial_) return;
    if (danmakuWindows_ >= kDanmakuMaxWindows) return;
    const qint64 start = danmakuCursorMs_;
    const qint64 end = start + 60000;
    client()->danmaku(vid, part, play, int(start), int(end),
        [this, serial, vid, part, play](bool ok, const QVariantList &items, int total, const QString &err) {
            Q_UNUSED(total);
            if (serial != danmakuSerial_) return;
            if (!ok) {
                Logger::instance().warn(QStringLiteral("[Dmghg] danmaku window failed: %1 (cursor=%2)")
                    .arg(err.isEmpty() ? QStringLiteral("unknown") : err)
                    .arg(danmakuCursorMs_));
                return;
            }
            bool added = false;
            for (const QVariant &v : items) {
                const QVariantMap m = v.toMap();
                const qint64 id = m.value(QStringLiteral("id")).toLongLong();
                if (danmakuSeen_.contains(id)) continue;
                danmakuSeen_.insert(id, 1);
                danmaku_.append(v);
                added = true;
            }
            danmakuWindows_ += 1;
            danmakuCursorMs_ += 60000;
            if (added) emit danmakuChanged();
            if (items.isEmpty()) return; // 连续空窗口 -> 停(与 get_all_danmaku_internal 一致)
            fetchDanmakuWindow(serial, vid, part, play);
        });
}
