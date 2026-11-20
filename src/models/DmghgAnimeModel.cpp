#include "DmghgAnimeModel.h"
#include "media/DmghgClient.h"
#include "../dmghgv3/DmghgClientV3.h"
#include "../dmghgv3/DmghgProtocolSwitch.h"

#include <QCoreApplication>
#include "infrastructure/Logger.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <climits>
#include <cmath>

namespace {
constexpr int kAnimeListWatchdogMs = 120000;
constexpr int kAnimePageSize = 20;

QString poolServerUrl() {
    QSettings s;
    QString url = s.value(QStringLiteral("auth/serverUrl"),
                          QStringLiteral("https://pool.983698.xyz"))
                      .toString()
                      .trimmed();
    if (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    return url;
}

QString poolSessionToken() {
    QSettings tokens(QStringLiteral("MeloBox"), QStringLiteral("MeloBox"));
    return tokens.value(QStringLiteral("auth/sessionToken")).toString().trimmed();
}
}

DmghgAnimeModel::DmghgAnimeModel(QObject *parent)
    : QAbstractListModel(parent) {
    // 提前初始化 DmghgClient 以尽早触发 bootstrap 发现与 DoH 域名解析
    client();
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
    useNewApi_ = dmghg::useV3();
    if (!client_)
        client_ = useNewApi_ ? static_cast<DmghgClient *>(new DmghgClientV3(qApp))
                             : new DmghgClient(qApp);
    return client_;
}

bool DmghgAnimeModel::useNewApi() const {
    return dmghg::useV3();
}

void DmghgAnimeModel::setUseNewApi(bool on) {
    if (dmghg::useV3() == on)
        return;
    dmghg::setUseV3(on);
    useNewApi_ = on;
    // 两套协议的 id 体系不同(旧=整数, 新=19位雪花号), 切换必须丢掉会话内所有缓存。
    if (client_) {
        client_->deleteLater();
        client_ = nullptr;
    }
    clear();
    // 频道/类型下拉各自来自一套协议，切换后必须重取。
    channelOptions_.clear();
    typeOptions_.clear();
    scheduleDays_.clear();
    emit scheduleDaysChanged();
    emit filterOptionsChanged();
    emit useNewApiChanged();
    if (on) {
        loadWeeklySchedule();
    }
}

QVariantList DmghgAnimeModel::channelOptions() const { return channelOptions_; }
QVariantList DmghgAnimeModel::typeOptions() const { return typeOptions_; }

void DmghgAnimeModel::loadChannelOptions() {
    // 走 client() 而不是裸 client_：setUseNewApi() 刚把 client_ 置空，界面紧接着
    // 就要刷新下拉，那一刻只有工厂能拿到新协议客户端。
    if (!channelOptions_.isEmpty()) return;
    client()->loadChannelOptions([this](bool ok, const QVariantList &options, const QString &err) {
        if (!ok && !err.isEmpty())
            Logger::instance().warn(QStringLiteral("[DmghgAnimeModel] 频道数据拉取失败: %1").arg(err));
        channelOptions_ = options;
        emit filterOptionsChanged();
    });
}

void DmghgAnimeModel::loadTypeOptions() {
    if (!typeOptions_.isEmpty()) return;
    client()->loadTypeOptions([this](bool ok, const QVariantList &options, const QString &err) {
        if (!ok && !err.isEmpty())
            Logger::instance().warn(QStringLiteral("[DmghgAnimeModel] 类型数据拉取失败: %1").arg(err));
        typeOptions_ = options;
        emit filterOptionsChanged();
    });
}

bool DmghgAnimeModel::loadCachedPage(const QString &key, quint64 serial, bool hasTotal) {
    const auto it = pageCache_.constFind(key);
    if (it == pageCache_.constEnd())
        return false;
    if (it->expiresAt <= QDateTime::currentSecsSinceEpoch() || it->items.isEmpty()) {
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
    if (items.isEmpty())
        return;
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

void DmghgAnimeModel::loadDetailByKey(const QString &key) {
    const int handle = client()->handleForKey(key);
    if (handle <= 0) {
        setErrorMessage(QStringLiteral("这条记录的编号在当前动漫接口下打不开"));
        return;
    }
    loadDetail(handle);
}

void DmghgAnimeModel::playEpisode(int vid, const QString &part) {
    if (vid <= 0 || part.isEmpty()) {
        emit episodeResolved({}, {}, QStringLiteral("缺少视频 ID 或集名"));
        return;
    }
    // 每次选集都使上一次尚未完成的播放解析失效,避免旧集结果覆盖新集。
    const quint64 serial = ++requestSerial_;
    setDetailLoading(true);

    // 1. 候选线路扫描（对标 pool 的 candidate_plays）：
    // 先搜寻包含当前所选分集的播放线路，当前选中的线路排首位，其余可用线路补入；
    // 为空时兜底 newup-jp 和 cn。
    QStringList candidatePlays;
    QString currentPlayName;
    if (currentSource_ >= 0 && currentSource_ < playSources_.size()) {
        currentPlayName = playSources_.at(currentSource_).toMap()
                              .value(QStringLiteral("name")).toString().trimmed();
    }

    // 搜寻包含分集的线路
    for (int i = 0; i < playSources_.size(); ++i) {
        const QVariantMap s = playSources_.at(i).toMap();
        const QString name = s.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty()) continue;
        const QVariantList eps = s.value(QStringLiteral("episodes")).toList();
        bool hasPart = false;
        for (const QVariant &ep : eps) {
            const QString epName = ep.toString();
            if (epName == part || epName.contains(part)) {
                hasPart = true;
                break;
            }
        }
        if (hasPart && !candidatePlays.contains(name)) {
            if (name == currentPlayName)
                candidatePlays.prepend(name);
            else
                candidatePlays.append(name);
        }
    }
    // 补入当前线路（若未加入）
    if (!currentPlayName.isEmpty() && !candidatePlays.contains(currentPlayName)) {
        candidatePlays.prepend(currentPlayName);
    }
    // 补入其他全部线路
    for (int i = 0; i < playSources_.size(); ++i) {
        const QString name = playSources_.at(i).toMap().value(QStringLiteral("name")).toString().trimmed();
        if (!name.isEmpty() && !candidatePlays.contains(name)) {
            candidatePlays.append(name);
        }
    }
    if (candidatePlays.isEmpty()) {
        candidatePlays.append(QStringLiteral("newup-jp"));
        candidatePlays.append(QStringLiteral("cn"));
    }

    // 2. 候选集名表达（例如 "第165集" 和 "165"）
    QStringList candidateParts = { part };
    QString stripped = part;
    if (stripped.startsWith(QStringLiteral("第"))) stripped.remove(0, 1);
    if (stripped.endsWith(QStringLiteral("集"))) stripped.chop(1);
    stripped = stripped.trimmed();
    bool isNum = false;
    stripped.toInt(&isNum);
    if (isNum && !candidateParts.contains(stripped)) {
        candidateParts.append(stripped);
    }

    // 3. 构建线路与集名的候选笛卡尔积列表
    QVector<QPair<QString, QString>> candidatePairs;
    for (const QString &pPlay : candidatePlays) {
        for (const QString &pPart : candidateParts) {
            candidatePairs.append({pPlay, pPart});
        }
    }

    auto pairIndex = std::make_shared<int>(0);
    auto lastError = std::make_shared<QString>();
    auto tryNextPair = std::make_shared<std::function<void(const QString &)>>();

    *tryNextPair = [this, vid, part, serial, candidatePairs, pairIndex, lastError, tryNextPair](const QString &prevErr) {
        if (serial != requestSerial_) return;
        if (!prevErr.isEmpty()) *lastError = prevErr;
        if (*pairIndex >= candidatePairs.size()) {
            setDetailLoading(false);
            emit episodeResolved({}, part, lastError->isEmpty() ? QStringLiteral("获取播放地址失败") : *lastError);
            return;
        }

        const auto currentPair = candidatePairs.at(*pairIndex);
        ++(*pairIndex);
        const QString pPlay = currentPair.first;
        const QString pPart = currentPair.second;

        client()->playVideo(vid, pPart, pPlay,
            [this, vid, part, serial, tryNextPair, pPlay, pPart](bool ok, const QString &source, const QString &err) {
                if (serial != requestSerial_) return;
                if (!ok || source.isEmpty()) {
                    Logger::instance().info(QStringLiteral("[DmghgAnimeModel] 线路 %1 (%2) playVideo 失败: %3，自动切换下一候选")
                        .arg(pPlay, pPart, err));
                    (*tryNextPair)(err);
                    return;
                }

                // 新协议 exchange 可能直接返回直链（无 new- 标识）：跳过 jx 二次解析
                if (source.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
                    || source.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
                    setDetailLoading(false);
                    emit episodeResolved(source, part, {});
                    return;
                }

                // 第二步:二次解析拿真实流地址。
                client()->parseSource(source, [this, part, serial, tryNextPair, pPlay](bool ok2, const QVariantList &urls, const QString &err2) {
                    if (serial != requestSerial_) return;
                    if (!ok2 || urls.isEmpty()) {
                        Logger::instance().info(QStringLiteral("[DmghgAnimeModel] 线路 %1 parseSource 失败: %2，自动切换下一候选")
                            .arg(pPlay, err2));
                        (*tryNextPair)(err2);
                        return;
                    }
                    setDetailLoading(false);
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
    };

    (*tryNextPair)({});
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
    danmakuRequestedWindows_.clear();
    danmakuFetching_ = false;
    danmakuRetryAfterMs_ = 0;
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

void DmghgAnimeModel::ensureDanmakuProgress(int vid, const QString &part, qint64 positionMs) {
    const quint64 serial = danmakuSerial_;
    if (vid <= 0 || part.isEmpty() || serial == 0 || danmakuFetching_) return;
    if (positionMs < 0) positionMs = 0;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now < danmakuRetryAfterMs_) return;

    constexpr qint64 kWindowMs = 60000;
    constexpr qint64 kPrefetchBeforeMs = 5000;
    const qint64 currentWindowStart = (positionMs / kWindowMs) * kWindowMs;
    const bool nearWindowEnd = positionMs - currentWindowStart >= kWindowMs - kPrefetchBeforeMs;

    // 向后 seek 可能落在尚未加载的窗口;向前 seek 直接跳到目标窗口,避免突发补齐中间窗口。
    const bool needsCurrent = !danmakuRequestedWindows_.contains(currentWindowStart)
        && (currentWindowStart + kWindowMs < danmakuCursorMs_ || currentWindowStart >= danmakuCursorMs_);
    const bool needsPrefetch = danmakuRequestedWindows_.contains(currentWindowStart)
        && nearWindowEnd && danmakuCursorMs_ == currentWindowStart + kWindowMs;

    if (!needsCurrent && !needsPrefetch) return;
    QString playSource;
    if (currentSource_ >= 0 && currentSource_ < playSources_.size())
        playSource = playSources_.at(currentSource_).toMap()
            .value(QStringLiteral("name")).toString();
    if (playSource.isEmpty()) playSource = QStringLiteral("cn");
    if (needsCurrent) danmakuCursorMs_ = currentWindowStart;
    fetchDanmakuWindow(serial, vid, part, playSource);
}

void DmghgAnimeModel::fetchDanmakuWindow(quint64 serial, int vid, const QString &part, const QString &play) {
    if (serial != danmakuSerial_) return;
    if (danmakuWindows_ >= kDanmakuMaxWindows) return;
    if (danmakuFetching_) return;
    const qint64 start = danmakuCursorMs_;
    const qint64 end = start + 60000;
    danmakuFetching_ = true;
    danmakuRequestedWindows_.insert(start, 1);
    client()->danmaku(vid, part, play, int(start), int(end),
        [this, serial, vid, part, play](bool ok, const QVariantList &items, int total, const QString &err) {
            Q_UNUSED(total);
            danmakuFetching_ = false;
            if (serial != danmakuSerial_) return;
            if (!ok) {
                Logger::instance().warn(QStringLiteral("[Dmghg] danmaku window failed: %1 (cursor=%2)")
                    .arg(err.isEmpty() ? QStringLiteral("unknown") : err)
                    .arg(danmakuCursorMs_));
                danmakuRequestedWindows_.remove(danmakuCursorMs_);
                danmakuRetryAfterMs_ = QDateTime::currentMSecsSinceEpoch() + 15000;
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
            Q_UNUSED(play);
            if (items.isEmpty()) {
                // 空窗口仍标记已请求;后续跨窗 seek 会按需探测,但不做连续递归。
                return;
            }
        });
}

bool DmghgAnimeModel::scheduleLoading() const {
    return scheduleLoading_;
}

QVariantMap DmghgAnimeModel::scheduleDays() const {
    return scheduleDays_;
}

int DmghgAnimeModel::currentScheduleDay() const {
    return currentScheduleDay_;
}

void DmghgAnimeModel::setCurrentScheduleDay(int day) {
    if (day < 1 || day > 7 || currentScheduleDay_ == day) return;
    currentScheduleDay_ = day;
    emit currentScheduleDayChanged();
}

void DmghgAnimeModel::loadWeeklySchedule() {
    if (scheduleLoading_) return;
    scheduleLoading_ = true;
    emit scheduleLoadingChanged();

    const QString server = poolServerUrl();
    const QString token = poolSessionToken();

    QString path = QStringLiteral("/api/anime/schedule");
    if (!token.isEmpty()) {
        path += QStringLiteral("?token=") + QString::fromUtf8(QUrl::toPercentEncoding(token));
    }
    const QUrl url(server + path);

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", QByteArrayLiteral("MeloBox/1.0"));
    request.setRawHeader("Accept", QByteArrayLiteral("application/json"));
    if (!token.isEmpty()) {
        request.setRawHeader("Cookie", QStringLiteral("token=%1").arg(token).toUtf8());
    }

    QNetworkReply *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() == QNetworkReply::NoError) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject()) {
                const QJsonObject root = doc.object();
                if (root.value(QStringLiteral("ok")).toBool()) {
                    const QJsonObject data = root.value(QStringLiteral("data")).toObject();
                    const QJsonObject daysObj = data.value(QStringLiteral("days")).toObject();
                    QVariantMap daysMap;
                    for (int d = 1; d <= 7; ++d) {
                        const QString key = QString::number(d);
                        const QJsonArray arr = daysObj.value(key).toArray();
                        QVariantList list;
                        for (const QJsonValue &v : arr) {
                            const QJsonObject obj = v.toObject();
                            QVariantMap card;
                            const QString idStr = obj.value(QStringLiteral("id")).toVariant().toString();
                            const qlonglong idVal = idStr.toLongLong();
                            card.insert(QStringLiteral("vodId"), (idVal > 0 && idVal < INT_MAX) ? static_cast<int>(idVal) : 0);
                            card.insert(QStringLiteral("vodIdStr"), idStr);
                            card.insert(QStringLiteral("vodName"), obj.value(QStringLiteral("name")).toString(obj.value(QStringLiteral("title")).toString()));
                            card.insert(QStringLiteral("vodPic"), obj.value(QStringLiteral("pic")).toString(obj.value(QStringLiteral("poster_url")).toString()));
                            card.insert(QStringLiteral("vodRemarks"), obj.value(QStringLiteral("continu")).toString(obj.value(QStringLiteral("remarks")).toString()));
                            card.insert(QStringLiteral("schedule"), obj.value(QStringLiteral("schedule")).toString());
                            card.insert(QStringLiteral("typeName"), obj.value(QStringLiteral("type")).toString(obj.value(QStringLiteral("category")).toString()));
                            card.insert(QStringLiteral("vodYear"), obj.value(QStringLiteral("year")).toString());
                            card.insert(QStringLiteral("vodArea"), obj.value(QStringLiteral("area")).toString());
                            list.append(card);
                        }
                        daysMap.insert(key, list);
                    }
                    scheduleDays_ = daysMap;
                    scheduleLoading_ = false;
                    emit scheduleDaysChanged();
                    emit scheduleLoadingChanged();
                    return;
                }
            }
        }

        // pool 接口请求异常时，尝试兜底走本地客户端解析
        Logger::instance().warn(QStringLiteral("[DmghgAnimeModel] pool schedule request failed: %1, falling back to client")
                                    .arg(reply->errorString()));
        client()->loadWeeklySchedule([this](bool ok, const QVariantMap &days, const QString &err) {
            scheduleLoading_ = false;
            emit scheduleLoadingChanged();
            if (ok) {
                scheduleDays_ = days;
                emit scheduleDaysChanged();
            } else {
                Logger::instance().warn(QStringLiteral("[DmghgAnimeModel] fallback loadWeeklySchedule failed: %1").arg(err));
            }
        });
    });
}
