#include "VideoSearchModel.h"
#include <QCryptographicHash>
#include <QUrl>
#include <QUrlQuery>
#include <QDebug>
#include <QHash>
#include <QMap>
#include <QDateTime>
#include <QSettings>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <utility>

namespace {
QString jsonValueToString(const QJsonValue &value) {
    if (value.isString()) return value.toString();
    if (value.isDouble()) return QString::number(value.toInt());
    return QString();
}

QVariantMap vodItemToMap(const VodItem &item) {
    QVariantMap map;
    map[QStringLiteral("vodId")] = item.vodId;
    map[QStringLiteral("vodName")] = item.vodName;
    map[QStringLiteral("vodPic")] = item.vodPic;
    map[QStringLiteral("vodRemarks")] = item.vodRemarks;
    map[QStringLiteral("vodYear")] = item.vodYear;
    map[QStringLiteral("vodArea")] = item.vodArea;
    map[QStringLiteral("vodClass")] = item.vodClass;
    map[QStringLiteral("vodActor")] = item.vodActor;
    map[QStringLiteral("vodDirector")] = item.vodDirector;
    map[QStringLiteral("vodBlurb")] = item.vodBlurb;
    map[QStringLiteral("vodContent")] = item.vodContent;
    map[QStringLiteral("vodPlayFrom")] = item.vodPlayFrom;
    map[QStringLiteral("vodPlayUrl")] = item.vodPlayUrl;
    map[QStringLiteral("vodScore")] = item.vodScore;
    map[QStringLiteral("typeName")] = item.typeName;
    return map;
}

QString normalizeAssetUrl(const QString &baseUrl, const QString &rawUrl) {
    const QString trimmed = rawUrl.trimmed();
    if (trimmed.isEmpty()) return {};

    if (trimmed.startsWith(QStringLiteral("//"))) {
        const QUrl base(baseUrl);
        return (base.scheme().isEmpty() ? QStringLiteral("https") : base.scheme())
            + QStringLiteral(":") + trimmed;
    }

    const QUrl parsed(trimmed);
    if (parsed.isValid() && !parsed.isRelative()) {
        return trimmed;
    }

    const QUrl base(baseUrl);
    if (!base.isValid() || base.scheme().isEmpty() || base.host().isEmpty()) {
        return trimmed;
    }

    QUrl origin;
    origin.setScheme(base.scheme());
    origin.setHost(base.host());
    origin.setPort(base.port());
    origin.setPath(QStringLiteral("/"));
    return origin.resolved(QUrl(trimmed)).toString();
}

QString md5Hex(const QString &text) {
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Md5).toHex());
}

constexpr qint64 kListCacheLifetimeMs = 60 * 60 * 1000;
constexpr int kMaxListCaches = 20;
const QString kListCachePrefix = QStringLiteral("videoListCache/");
}

VideoSearchModel::VideoSearchModel(QObject *parent)
    : QAbstractListModel(parent) {
}

int VideoSearchModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent)
    return items_.size();
}

QVariant VideoSearchModel::data(const QModelIndex &index, int role) const {
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
    case VodClassRole: return item.vodClass;
    case VodActorRole: return item.vodActor;
    case VodDirectorRole: return item.vodDirector;
    case VodBlurbRole: return item.vodBlurb;
    case VodContentRole: return item.vodContent;
    case VodPlayFromRole: return item.vodPlayFrom;
    case VodPlayUrlRole: return item.vodPlayUrl;
    case VodScoreRole: return item.vodScore;
    case TypeNameRole: return item.typeName;
    default: return {};
    }
}

QHash<int, QByteArray> VideoSearchModel::roleNames() const {
    return {
        {VodIdRole, "vodId"},
        {VodNameRole, "vodName"},
        {VodPicRole, "vodPic"},
        {VodRemarksRole, "vodRemarks"},
        {VodYearRole, "vodYear"},
        {VodAreaRole, "vodArea"},
        {VodClassRole, "vodClass"},
        {VodActorRole, "vodActor"},
        {VodDirectorRole, "vodDirector"},
        {VodBlurbRole, "vodBlurb"},
        {VodContentRole, "vodContent"},
        {VodPlayFromRole, "vodPlayFrom"},
        {VodPlayUrlRole, "vodPlayUrl"},
        {VodScoreRole, "vodScore"},
        {TypeNameRole, "typeName"}
    };
}

bool VideoSearchModel::loading() const { return loading_; }
int VideoSearchModel::totalCount() const { return totalCount_; }
int VideoSearchModel::count() const { return items_.size(); }
int VideoSearchModel::currentPage() const { return currentPage_; }
int VideoSearchModel::totalPages() const { return totalPages_; }
bool VideoSearchModel::loadingMore() const { return loadingMore_; }
bool VideoSearchModel::hasMore() const { return currentPage_ < totalPages_; }
bool VideoSearchModel::restoredFromCache() const { return restoredFromCache_; }
QString VideoSearchModel::errorMessage() const { return errorMessage_; }
QVariantList VideoSearchModel::categories() const { return categories_; }

void VideoSearchModel::setLoading(bool loading) {
    if (loading_ == loading) return;
    loading_ = loading;
    emit loadingChanged();
}

void VideoSearchModel::setLoadingMore(bool loading) {
    if (loadingMore_ == loading) return;
    loadingMore_ = loading;
    emit loadingMoreChanged();
}

void VideoSearchModel::setRestoredFromCache(bool restored) {
    if (restoredFromCache_ == restored) return;
    restoredFromCache_ = restored;
    emit restoredFromCacheChanged();
}

void VideoSearchModel::setErrorMessage(const QString &msg) {
    if (errorMessage_ == msg) return;
    errorMessage_ = msg;
    emit errorMessageChanged();
}

QNetworkRequest VideoSearchModel::makeRequest(const QString &url) const {
    QUrl qurl(url);
    QNetworkRequest req(qurl);
    req.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    req.setRawHeader("Accept", "application/json");
    return req;
}

QString VideoSearchModel::cacheKey(const QString &type, const QString &baseUrl,
                                   const QString &parameter, int page) const {
    return kListCachePrefix + md5Hex(type + QLatin1Char('|') + baseUrl
        + QLatin1Char('|') + parameter + QLatin1Char('|') + QString::number(page));
}

bool VideoSearchModel::restoreCache(const QString &key, bool append) {
    QSettings settings;
    const qint64 savedAt = settings.value(key + QStringLiteral("/savedAt")).toLongLong();
    if (savedAt <= 0 || QDateTime::currentMSecsSinceEpoch() - savedAt > kListCacheLifetimeMs) {
        settings.remove(key);
        return false;
    }

    const QByteArray payload = settings.value(key + QStringLiteral("/payload")).toByteArray();
    const QString baseUrl = settings.value(key + QStringLiteral("/baseUrl")).toString();
    const int page = settings.value(key + QStringLiteral("/page"), 1).toInt();
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (payload.isEmpty() || baseUrl.isEmpty() || doc.isNull() || !doc.isObject()) {
        settings.remove(key);
        return false;
    }

    currentPage_ = page;
    parseListResponse(doc, baseUrl, append, false);
    setLoading(false);
    setLoadingMore(false);
    setRestoredFromCache(true);
    return true;
}

void VideoSearchModel::writeCache(const QString &key, const QJsonDocument &doc,
                                  const QString &baseUrl) {
    QSettings settings;
    settings.setValue(key + QStringLiteral("/savedAt"), QDateTime::currentMSecsSinceEpoch());
    settings.setValue(key + QStringLiteral("/payload"), doc.toJson(QJsonDocument::Compact));
    settings.setValue(key + QStringLiteral("/baseUrl"), baseUrl);
    settings.setValue(key + QStringLiteral("/page"), currentPage_);
    settings.sync();
    pruneCaches();
}

void VideoSearchModel::pruneCaches() {
    QSettings settings;
    QMap<qint64, QString> cachesByTime;
    const QString suffix = QStringLiteral("/savedAt");
    for (const QString &key : settings.allKeys()) {
        if (!key.startsWith(kListCachePrefix) || !key.endsWith(suffix)) continue;
        const QString cacheRoot = key.left(key.size() - suffix.size());
        cachesByTime.insert(settings.value(key).toLongLong(), cacheRoot);
    }

    while (cachesByTime.size() > kMaxListCaches) {
        const auto oldest = cachesByTime.begin();
        settings.remove(oldest.value());
        cachesByTime.erase(oldest);
    }
}

void VideoSearchModel::search(const QString &baseUrl, const QString &keyword, int page,
                              bool forceRefresh, bool append) {
    const int requestSerial = ++requestSerial_;
    setRestoredFromCache(false);
    setLoadingMore(append);
    setLoading(!append);
    setErrorMessage({});

    const QString key = cacheKey(QStringLiteral("search"), baseUrl, keyword.trimmed(), page);
    if (!forceRefresh && restoreCache(key, append)) {
        emit searchCompleted();
        return;
    }

    QString url = baseUrl + "?ac=detail&wd=" + QUrl::toPercentEncoding(keyword) + "&pg=" + QString::number(page);
    auto *reply = nam_.get(makeRequest(url));

    connect(reply, &QNetworkReply::finished, this, [this, reply, page, requestSerial, baseUrl, append, key]() {
        reply->deleteLater();
        if (requestSerial != requestSerial_) return;
        setLoading(false);
        setLoadingMore(false);

        if (reply->error() != QNetworkReply::NoError) {
            setErrorMessage(reply->errorString());
            emit searchCompleted();
            return;
        }

        auto doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isNull() || !doc.isObject()) {
            setErrorMessage(QStringLiteral("Invalid JSON response"));
            emit searchCompleted();
            return;
        }

        currentPage_ = page;
        parseListResponse(doc, baseUrl, append);
        writeCache(key, doc, baseUrl);
        emit searchCompleted();
    });
}

void VideoSearchModel::searchById(const QString &baseUrl, const QString &ids) {
    const int requestSerial = ++requestSerial_;
    setLoading(true);
    setErrorMessage({});

    QString url = baseUrl + "?ac=detail&ids=" + ids;
    auto *reply = nam_.get(makeRequest(url));

    connect(reply, &QNetworkReply::finished, this, [this, reply, requestSerial, baseUrl]() {
        reply->deleteLater();
        if (requestSerial != requestSerial_) return;
        setLoading(false);

        if (reply->error() != QNetworkReply::NoError) {
            setErrorMessage(reply->errorString());
            emit searchCompleted();
            return;
        }

        auto doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isNull() || !doc.isObject()) {
            setErrorMessage(QStringLiteral("Invalid JSON response"));
            emit searchCompleted();
            return;
        }

        parseDetailResponse(doc, baseUrl);
        emit searchCompleted();
    });
}

void VideoSearchModel::loadList(const QString &baseUrl, int page, const QString &typeId,
                                bool forceRefresh, bool append) {
    const int requestSerial = ++requestSerial_;
    setRestoredFromCache(false);
    setLoadingMore(append);
    setLoading(!append);
    setErrorMessage({});

    const QString trimmedTypeId = typeId.trimmed();
    const QString key = cacheKey(QStringLiteral("list"), baseUrl, trimmedTypeId, page);
    if (!forceRefresh && restoreCache(key, append)) {
        emit searchCompleted();
        return;
    }

    QString url = baseUrl + "?ac=list&pg=" + QString::number(page);
    if (!trimmedTypeId.isEmpty()) {
        url += "&t=" + QUrl::toPercentEncoding(trimmedTypeId);
    }
    auto *reply = nam_.get(makeRequest(url));

    connect(reply, &QNetworkReply::finished, this, [this, reply, page, requestSerial, baseUrl, append, key]() {
        reply->deleteLater();
        if (requestSerial != requestSerial_) return;
        setLoading(false);
        setLoadingMore(false);

        if (reply->error() != QNetworkReply::NoError) {
            setErrorMessage(reply->errorString());
            emit searchCompleted();
            return;
        }

        auto doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isNull() || !doc.isObject()) {
            setErrorMessage(QStringLiteral("Invalid JSON response"));
            emit searchCompleted();
            return;
        }

        currentPage_ = page;
        writeCache(key, doc, baseUrl);
        parseListResponse(doc, baseUrl, append);
        emit searchCompleted();
    });
}

void VideoSearchModel::clear() {
    ++requestSerial_;
    beginResetModel();
    items_.clear();
    endResetModel();
    totalCount_ = 0;
    currentPage_ = 1;
    totalPages_ = 1;
    setLoading(false);
    setLoadingMore(false);
    setRestoredFromCache(false);
    setErrorMessage({});
    emit countChanged();
    emit totalCountChanged();
    emit currentPageChanged();
    emit totalPagesChanged();
}

VodItem VideoSearchModel::itemAt(int index) const {
    if (index < 0 || index >= items_.size()) return {};
    return items_[index];
}

QVariantMap VideoSearchModel::itemMapAt(int index) const {
    if (index < 0 || index >= items_.size()) return {};
    return vodItemToMap(items_[index]);
}

static VodItem parseVodObject(const QJsonObject &obj, const QString &baseUrl) {
    auto valueToString = [](const QJsonValue &value) {
        if (value.isString()) return value.toString();
        if (value.isDouble()) return QString::number(value.toInt());
        return QString();
    };

    auto episodeCountFromPlayUrl = [](const QString &playUrl) {
        int maxCount = 0;
        const auto sources = playUrl.split(QStringLiteral("$$$"), Qt::SkipEmptyParts);
        for (const QString &source : sources) {
            int count = 0;
            const auto episodes = source.split('#', Qt::SkipEmptyParts);
            for (const QString &episode : episodes) {
                if (!episode.trimmed().isEmpty()) {
                    ++count;
                }
            }
            maxCount = qMax(maxCount, count);
        }
        return maxCount;
    };

    VodItem item;
    item.vodId = obj.value("vod_id").toInt();
    item.vodName = obj.value("vod_name").toString();
    item.vodPic = normalizeAssetUrl(baseUrl, obj.value("vod_pic").toString());
    item.vodRemarks = valueToString(obj.value("vod_remarks"));
    item.vodYear = obj.value("vod_year").toString();
    item.vodArea = obj.value("vod_area").toString();
    item.vodClass = obj.value("vod_class").toString();
    item.vodActor = obj.value("vod_actor").toString();
    item.vodDirector = obj.value("vod_director").toString();
    item.vodBlurb = obj.value("vod_blurb").toString();
    item.vodContent = obj.value("vod_content").toString();
    item.vodPlayFrom = obj.value("vod_play_from").toString();
    item.vodPlayUrl = obj.value("vod_play_url").toString();
    item.vodScore = obj.value("vod_score").toString();
    item.typeName = obj.value("type_name").toString();

    if (item.vodRemarks.trimmed().isEmpty()) {
        const QString serial = valueToString(obj.value("vod_serial"));
        const QString total = valueToString(obj.value("vod_total"));
        if (!serial.isEmpty() && serial != QStringLiteral("0")) {
            item.vodRemarks = QStringLiteral("更新至%1集").arg(serial);
        } else if (!total.isEmpty() && total != QStringLiteral("0")) {
            item.vodRemarks = QStringLiteral("共%1集").arg(total);
        } else {
            const int episodeCount = episodeCountFromPlayUrl(item.vodPlayUrl);
            if (episodeCount > 0) {
                item.vodRemarks = QStringLiteral("共%1集").arg(episodeCount);
            }
        }
    }

    return item;
}

void VideoSearchModel::requestSupplementDetails(const QString &baseUrl, int requestSerial) {
    QStringList ids;
    ids.reserve(items_.size());
    for (const VodItem &item : std::as_const(items_)) {
        if (item.vodId <= 0) continue;
        if (!item.vodPic.trimmed().isEmpty() && !item.vodPlayUrl.trimmed().isEmpty()) continue;
        ids.append(QString::number(item.vodId));
    }

    if (ids.isEmpty()) return;

    QString url = baseUrl + "?ac=detail&ids=" + ids.join(',');
    auto *reply = nam_.get(makeRequest(url));

    connect(reply, &QNetworkReply::finished, this, [this, reply, requestSerial, baseUrl]() {
        reply->deleteLater();
        if (requestSerial != requestSerial_) return;
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "VideoSearchModel: supplement detail failed:" << reply->errorString();
            return;
        }

        auto doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isNull() || !doc.isObject()) return;

        auto list = doc.object().value("list").toArray();
        if (list.isEmpty()) return;

        QHash<int, int> rowById;
        for (int row = 0; row < items_.size(); ++row) {
            rowById.insert(items_[row].vodId, row);
        }

        for (const auto &value : list) {
            if (!value.isObject()) continue;
            VodItem detail = parseVodObject(value.toObject(), baseUrl);
            if (!rowById.contains(detail.vodId)) continue;

            const int row = rowById.value(detail.vodId);
            VodItem &item = items_[row];
            item.vodPic = detail.vodPic.isEmpty() ? item.vodPic : detail.vodPic;
            item.vodRemarks = detail.vodRemarks.isEmpty() ? item.vodRemarks : detail.vodRemarks;
            item.vodYear = detail.vodYear.isEmpty() ? item.vodYear : detail.vodYear;
            item.vodArea = detail.vodArea.isEmpty() ? item.vodArea : detail.vodArea;
            item.vodClass = detail.vodClass.isEmpty() ? item.vodClass : detail.vodClass;
            item.vodActor = detail.vodActor.isEmpty() ? item.vodActor : detail.vodActor;
            item.vodDirector = detail.vodDirector.isEmpty() ? item.vodDirector : detail.vodDirector;
            item.vodBlurb = detail.vodBlurb.isEmpty() ? item.vodBlurb : detail.vodBlurb;
            item.vodContent = detail.vodContent.isEmpty() ? item.vodContent : detail.vodContent;
            item.vodPlayFrom = detail.vodPlayFrom.isEmpty() ? item.vodPlayFrom : detail.vodPlayFrom;
            item.vodPlayUrl = detail.vodPlayUrl.isEmpty() ? item.vodPlayUrl : detail.vodPlayUrl;
            item.vodScore = detail.vodScore.isEmpty() ? item.vodScore : detail.vodScore;
            item.typeName = detail.typeName.isEmpty() ? item.typeName : detail.typeName;

            const QModelIndex changed = index(row, 0);
            emit dataChanged(changed, changed, {
                VodPicRole,
                VodRemarksRole,
                VodYearRole,
                VodAreaRole,
                VodClassRole,
                VodActorRole,
                VodDirectorRole,
                VodBlurbRole,
                VodContentRole,
                VodPlayFromRole,
                VodPlayUrlRole,
                VodScoreRole,
                TypeNameRole
            });
        }
    });
}

void VideoSearchModel::parseListResponse(const QJsonDocument &doc, const QString &baseUrl,
                                         bool append, bool requestSupplement) {
    auto root = doc.object();
    int newTotal = root.value("total").toInt();
    int newPages = root.value("pagecount").toInt();

    if (root.contains(QStringLiteral("class")) && root.value(QStringLiteral("class")).isArray()) {
        QVariantList newCategories;
        const auto classList = root.value(QStringLiteral("class")).toArray();
        for (const auto &value : classList) {
            if (!value.isObject()) continue;
            const auto obj = value.toObject();
            QVariantMap category;
            category[QStringLiteral("typeId")] = jsonValueToString(obj.value(QStringLiteral("type_id")));
            category[QStringLiteral("typeName")] = obj.value(QStringLiteral("type_name")).toString();
            if (!category.value(QStringLiteral("typeId")).toString().isEmpty()
                && !category.value(QStringLiteral("typeName")).toString().isEmpty()) {
                newCategories.append(category);
            }
        }
        if (categories_ != newCategories) {
            categories_ = newCategories;
            emit categoriesChanged();
        }
    }

    beginResetModel();
    if (!append)
        items_.clear();
    QSet<int> existingIds;
    for (const auto &item : std::as_const(items_))
        existingIds.insert(item.vodId);
    auto list = root.value("list").toArray();
    for (const auto &val : list) {
        if (!val.isObject()) continue;
        const auto item = parseVodObject(val.toObject(), baseUrl);
        if (!append || !existingIds.contains(item.vodId)) {
            items_.append(item);
            existingIds.insert(item.vodId);
        }
    }
    endResetModel();
    emit countChanged();

    if (totalCount_ != newTotal) {
        totalCount_ = newTotal;
        emit totalCountChanged();
    }
    if (totalPages_ != newPages) {
        totalPages_ = newPages;
        emit totalPagesChanged();
    }
    emit currentPageChanged();
    if (requestSupplement)
        requestSupplementDetails(baseUrl, requestSerial_);
}

void VideoSearchModel::parseDetailResponse(const QJsonDocument &doc, const QString &baseUrl) {
    auto root = doc.object();
    auto list = root.value("list").toArray();
    if (list.isEmpty()) return;

    // Detail response: update existing items or append
    beginResetModel();
    items_.clear();
    for (const auto &val : list) {
        if (val.isObject())
            items_.append(parseVodObject(val.toObject(), baseUrl));
    }
    endResetModel();
    emit countChanged();

    emit detailReceived(0);
}
