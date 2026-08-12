#include "ApiSiteModel.h"

#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <memory>

namespace {
constexpr auto kSharePrefix = "MINIPLAYER_SITE_SHARE_V1:";
constexpr auto kShareType = "api-sites";
constexpr int kAccessUnknown = 0;
constexpr int kAccessChecking = 1;
constexpr int kAccessOk = 2;
constexpr int kAccessFailed = 3;
constexpr int kAccessTimeoutMs = 8000;

QByteArray shareSecret() {
    return QByteArrayLiteral("miniPlayer.site-share.v1.fixed-client-key.2026");
}

QByteArray sha256(const QByteArray &data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

QByteArray randomSalt() {
    QByteArray salt;
    salt.resize(16);
    for (auto &byte : salt) {
        byte = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    return salt;
}

QByteArray cryptBytes(const QByteArray &input, const QByteArray &salt) {
    const QByteArray key = sha256(shareSecret() + salt);
    QByteArray output;
    output.resize(input.size());

    QByteArray stream;
    int counter = 0;
    while (stream.size() < input.size()) {
        stream += sha256(key + salt + QByteArray::number(counter++));
    }

    for (int i = 0; i < input.size(); ++i) {
        output[i] = static_cast<char>(input.at(i) ^ stream.at(i));
    }
    return output;
}

QByteArray payloadMac(const QByteArray &salt, const QByteArray &cipher) {
    return sha256(QByteArrayLiteral("miniPlayer-site-share-mac") + shareSecret() + salt + cipher).left(16);
}

QString toBase64Text(const QByteArray &data) {
    return QString::fromLatin1(data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QByteArray fromBase64Text(const QString &text, bool *ok) {
    const auto result = QByteArray::fromBase64Encoding(text.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    if (ok) {
        *ok = result.decodingStatus == QByteArray::Base64DecodingStatus::Ok;
    }
    return result.decoded;
}
}

ApiSiteModel::ApiSiteModel(QObject *parent)
    : QAbstractListModel(parent), configPath_(resolveConfigPath()) {
    loadFromFile();
    ensureDefaults();
    ensureShareSelectionSize();
    ensureAccessStateSize();
}

QString ApiSiteModel::resolveConfigPath() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty())
        dir = QDir::currentPath();
    QDir().mkpath(dir);
    return dir + "/api_sites.json";
}

int ApiSiteModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent)
    return items_.size();
}

QVariant ApiSiteModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= items_.size())
        return {};

    const auto &item = items_[index.row()];
    switch (role) {
    case NameRole:
        return item.name;
    case BaseUrlRole:
        return item.baseUrl;
    case SiteTypeRole:
        return item.type;
    case ShareSelectedRole:
        return shareSelected_.value(index.row(), false);
    case AccessStatusRole:
        return accessStatus_.value(index.row(), kAccessUnknown);
    case AccessStatusTextRole:
        return accessStatusText(accessStatus_.value(index.row(), kAccessUnknown),
                                accessLatencyMs_.value(index.row(), -1));
    case AccessLatencyMsRole:
        return accessLatencyMs_.value(index.row(), -1);
    default:
        return {};
    }
}

QHash<int, QByteArray> ApiSiteModel::roleNames() const {
    return {
        {NameRole, "name"},
        {BaseUrlRole, "baseUrl"},
        {SiteTypeRole, "siteType"},
        {ShareSelectedRole, "shareSelected"},
        {AccessStatusRole, "accessStatus"},
        {AccessStatusTextRole, "accessStatusText"},
        {AccessLatencyMsRole, "accessLatencyMs"}
    };
}

int ApiSiteModel::count() const { return items_.size(); }

int ApiSiteModel::currentIndex() const { return currentIndex_; }

void ApiSiteModel::setCurrentIndex(int index) {
    if (index < 0 || index >= items_.size() || index == currentIndex_)
        return;

    const int previousIndex = currentIndex_;
    currentIndex_ = index;
    saveToFile();
    emit currentIndexChanged();
    emit currentSiteChanged();

    if (previousIndex >= 0 && previousIndex < items_.size()) {
        const auto previous = createIndex(previousIndex, 0);
        emit dataChanged(previous, previous, {});
    }
    const auto current = createIndex(currentIndex_, 0);
    emit dataChanged(current, current, {});
}

void ApiSiteModel::add(const QString &name, const QString &baseUrl, const QString &siteType) {
    beginInsertRows(QModelIndex(), items_.size(), items_.size());
    items_.append({name.trimmed(), baseUrl.trimmed(), normalizeSiteType(siteType)});
    shareSelected_.append(false);
    accessStatus_.append(kAccessUnknown);
    accessLatencyMs_.append(-1);
    endInsertRows();
    saveToFile();
    emit countChanged();
}

void ApiSiteModel::removeAt(int index) {
    if (index < 0 || index >= items_.size())
        return;

    const int previousCurrentIndex = currentIndex_;
    const QString previousCurrentName = currentName();
    const QString previousCurrentBaseUrl = currentBaseUrl();

    beginRemoveRows(QModelIndex(), index, index);
    items_.removeAt(index);
    if (index < shareSelected_.size())
        shareSelected_.removeAt(index);
    if (index < accessStatus_.size())
        accessStatus_.removeAt(index);
    if (index < accessLatencyMs_.size())
        accessLatencyMs_.removeAt(index);
    endRemoveRows();

    if (items_.isEmpty()) {
        currentIndex_ = 0;
    } else if (index < previousCurrentIndex) {
        currentIndex_ = previousCurrentIndex - 1;
    } else if (currentIndex_ >= items_.size()) {
        currentIndex_ = items_.size() - 1;
    }

    saveToFile();
    emit countChanged();

    if (currentIndex_ != previousCurrentIndex)
        emit currentIndexChanged();
    if (currentName() != previousCurrentName || currentBaseUrl() != previousCurrentBaseUrl)
        emit currentSiteChanged();
}

void ApiSiteModel::update(int index, const QString &name, const QString &baseUrl, const QString &siteType) {
    if (index < 0 || index >= items_.size())
        return;

    items_[index].name = name.trimmed();
    items_[index].baseUrl = baseUrl.trimmed();
    items_[index].type = normalizeSiteType(siteType);
    accessStatus_[index] = kAccessUnknown;
    accessLatencyMs_[index] = -1;

    auto idx = createIndex(index, 0);
    emit dataChanged(idx, idx, {NameRole, BaseUrlRole, SiteTypeRole, AccessStatusRole, AccessStatusTextRole, AccessLatencyMsRole});
    saveToFile();
    if (index == currentIndex_)
        emit currentSiteChanged();
}

bool ApiSiteModel::selectAt(int index) {
    if (index < 0 || index >= items_.size())
        return false;
    setCurrentIndex(index);
    return currentIndex_ == index;
}

QString ApiSiteModel::nameAt(int index) const {
    if (index < 0 || index >= items_.size())
        return {};
    return items_[index].name;
}

QString ApiSiteModel::baseUrlAt(int index) const {
    if (index < 0 || index >= items_.size())
        return {};
    return items_[index].baseUrl;
}

QString ApiSiteModel::typeAt(int index) const {
    if (index < 0 || index >= items_.size())
        return {};
    return normalizeSiteType(items_[index].type);
}

bool ApiSiteModel::shareSelectedAt(int index) const {
    if (index < 0 || index >= items_.size())
        return false;
    return shareSelected_.value(index, false);
}

int ApiSiteModel::accessStatusAt(int index) const {
    if (index < 0 || index >= items_.size())
        return kAccessUnknown;
    return accessStatus_.value(index, kAccessUnknown);
}

QString ApiSiteModel::accessStatusTextAt(int index) const {
    if (index < 0 || index >= items_.size())
        return accessStatusText(kAccessUnknown, -1);
    return accessStatusText(accessStatus_.value(index, kAccessUnknown),
                            accessLatencyMs_.value(index, -1));
}

QString ApiSiteModel::currentBaseUrl() const {
    return baseUrlAt(currentIndex_);
}

QString ApiSiteModel::currentVideoBaseUrl() const {
    if (currentIndex_ >= 0 && currentIndex_ < items_.size() && typeAt(currentIndex_) == QStringLiteral("video"))
        return items_[currentIndex_].baseUrl;

    for (const auto &item : items_) {
        if (normalizeSiteType(item.type) == QStringLiteral("video"))
            return item.baseUrl;
    }
    return {};
}

QString ApiSiteModel::imageBaseUrl() const {
    if (currentIndex_ >= 0
            && currentIndex_ < items_.size()
            && typeAt(currentIndex_) == QStringLiteral("image")) {
        return items_[currentIndex_].baseUrl;
    }

    for (const auto &item : items_) {
        const QString baseUrl = item.baseUrl.trimmed();
        if (normalizeSiteType(item.type) == QStringLiteral("image") &&
            !baseUrl.contains(QStringLiteral("t.alcy.cc"), Qt::CaseInsensitive)) {
            return item.baseUrl;
        }
    }

    for (const auto &item : items_) {
        if (normalizeSiteType(item.type) == QStringLiteral("image"))
            return item.baseUrl;
    }
    return {};
}

QString ApiSiteModel::currentName() const {
    return nameAt(currentIndex_);
}

bool ApiSiteModel::matchesFilter(int index, const QString &filter) const {
    if (index < 0 || index >= items_.size())
        return false;

    const QString needle = filter.trimmed();
    if (needle.isEmpty())
        return true;

    const auto &item = items_[index];
    return item.name.contains(needle, Qt::CaseInsensitive)
        || item.baseUrl.contains(needle, Qt::CaseInsensitive);
}

QString ApiSiteModel::deduplicateByUrl() {
    if (items_.size() <= 1)
        return QStringLiteral("没有可去重的站点");

    QHash<QString, int> firstIndexByUrl;
    QVector<ApiSite> uniqueItems;
    QVector<bool> uniqueShareSelected;
    QVector<int> uniqueAccessStatus;
    QVector<int> uniqueAccessLatencyMs;
    QVector<int> oldToNewIndex(items_.size(), -1);
    int removed = 0;

    ensureShareSelectionSize();
    ensureAccessStateSize();

    for (int i = 0; i < items_.size(); ++i) {
        const QString normalized = normalizeBaseUrl(items_[i].baseUrl);
        const auto existing = firstIndexByUrl.constFind(normalized);
        if (existing != firstIndexByUrl.constEnd()) {
            oldToNewIndex[i] = existing.value();
            ++removed;
            continue;
        }

        const int newIndex = uniqueItems.size();
        firstIndexByUrl.insert(normalized, newIndex);
        oldToNewIndex[i] = newIndex;
        uniqueItems.append(items_[i]);
        uniqueShareSelected.append(shareSelected_.value(i, false));
        uniqueAccessStatus.append(accessStatus_.value(i, kAccessUnknown));
        uniqueAccessLatencyMs.append(accessLatencyMs_.value(i, -1));
    }

    if (removed == 0)
        return QStringLiteral("没有发现重复 URL");

    const int previousCurrentIndex = currentIndex_;
    const QString previousCurrentName = currentName();
    const QString previousCurrentBaseUrl = currentBaseUrl();

    beginResetModel();
    items_ = uniqueItems;
    shareSelected_ = uniqueShareSelected;
    accessStatus_ = uniqueAccessStatus;
    accessLatencyMs_ = uniqueAccessLatencyMs;
    currentIndex_ = oldToNewIndex.value(previousCurrentIndex, 0);
    if (currentIndex_ < 0 || currentIndex_ >= items_.size())
        currentIndex_ = items_.isEmpty() ? 0 : items_.size() - 1;
    endResetModel();

    saveToFile();
    emit countChanged();
    if (currentIndex_ != previousCurrentIndex)
        emit currentIndexChanged();
    if (currentName() != previousCurrentName || currentBaseUrl() != previousCurrentBaseUrl)
        emit currentSiteChanged();

    return QStringLiteral("Deduplicated by URL, removed %1 duplicate sites").arg(removed);
}

void ApiSiteModel::setShareSelected(int index, bool selected) {
    if (index < 0 || index >= items_.size())
        return;

    ensureShareSelectionSize();
    if (shareSelected_[index] == selected)
        return;

    shareSelected_[index] = selected;
    auto idx = createIndex(index, 0);
    emit dataChanged(idx, idx, {ShareSelectedRole});
}

void ApiSiteModel::toggleShareSelected(int index) {
    if (index < 0 || index >= items_.size())
        return;
    ensureShareSelectionSize();
    setShareSelected(index, !shareSelected_.value(index, false));
}

void ApiSiteModel::selectAllForShare(bool selected) {
    ensureShareSelectionSize();
    if (items_.isEmpty())
        return;

    bool changed = false;
    for (int i = 0; i < shareSelected_.size(); ++i) {
        if (shareSelected_[i] != selected) {
            shareSelected_[i] = selected;
            changed = true;
        }
    }
    if (changed) {
        emit dataChanged(createIndex(0, 0), createIndex(items_.size() - 1, 0), {ShareSelectedRole});
    }
}

QString ApiSiteModel::shareSelectedToClipboard() {
    ensureShareSelectionSize();

    QJsonArray sites;
    for (int i = 0; i < items_.size(); ++i) {
        if (!shareSelected_.value(i, false))
            continue;

        const auto &item = items_[i];
        if (item.name.trimmed().isEmpty() || item.baseUrl.trimmed().isEmpty())
            continue;

        QJsonObject site;
        site["name"] = item.name.trimmed();
        site["baseUrl"] = item.baseUrl.trimmed();
        site["siteType"] = normalizeSiteType(item.type);
        sites.append(site);
    }

    if (sites.isEmpty())
        return QStringLiteral("Please select sites to share");

    QJsonObject payload;
    payload["type"] = QString::fromLatin1(kShareType);
    payload["version"] = 1;
    payload["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    payload["sites"] = sites;

    const QString shareText = encodeSharePayload(payload);
    if (auto *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(shareText);
        return QStringLiteral("Encrypted %1 sites to clipboard").arg(sites.size());
    }

    return QStringLiteral("Copy failed: clipboard unavailable");
}

QString ApiSiteModel::importSitesFromClipboard() {
    const auto *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return QStringLiteral("Import failed: clipboard unavailable");

    QJsonObject payload;
    if (!decodeSharePayload(clipboard->text().trimmed(), &payload))
        return QStringLiteral("剪贴板里没有可识别的 MeloBox 加密站点分享");

    if (payload.value("type").toString() != QString::fromLatin1(kShareType))
        return QStringLiteral("Share content type mismatch");

    const auto sites = payload.value("sites").toArray();
    if (sites.isEmpty())
        return QStringLiteral("Share content has no sites");

    int imported = 0;
    int updated = 0;
    int skipped = 0;

    for (const auto &value : sites) {
        if (!value.isObject()) {
            ++skipped;
            continue;
        }

        const auto site = value.toObject();
        const QString name = site.value("name").toString().trimmed();
        const QString baseUrl = site.value("baseUrl").toString().trimmed();
        if (name.isEmpty() || baseUrl.isEmpty()) {
            ++skipped;
            continue;
        }

        QString siteType = normalizeSiteType(site.value("siteType").toString());
        if (siteType.isEmpty() || siteType == QStringLiteral("video")) {
            const QString legacyType = site.value("type").toString();
            if (!legacyType.trimmed().isEmpty())
                siteType = normalizeSiteType(legacyType);
        }

        const QString normalized = normalizeBaseUrl(baseUrl);
        int existingIndex = -1;
        for (int i = 0; i < items_.size(); ++i) {
            if (normalizeBaseUrl(items_[i].baseUrl) == normalized) {
                existingIndex = i;
                break;
            }
        }

        if (existingIndex >= 0) {
            if (items_[existingIndex].name != name || items_[existingIndex].baseUrl != baseUrl || normalizeSiteType(items_[existingIndex].type) != siteType) {
                items_[existingIndex] = {name, baseUrl, siteType};
                accessStatus_[existingIndex] = kAccessUnknown;
                accessLatencyMs_[existingIndex] = -1;
                auto idx = createIndex(existingIndex, 0);
                emit dataChanged(idx, idx, {NameRole, BaseUrlRole, SiteTypeRole, AccessStatusRole, AccessStatusTextRole, AccessLatencyMsRole});
                ++updated;
            } else {
                ++skipped;
            }
            continue;
        }

        beginInsertRows(QModelIndex(), items_.size(), items_.size());
        items_.append({name, baseUrl, siteType});
        shareSelected_.append(false);
        accessStatus_.append(kAccessUnknown);
        accessLatencyMs_.append(-1);
        endInsertRows();
        ++imported;
    }

    if (imported > 0)
        emit countChanged();

    if (imported > 0 || updated > 0)
        saveToFile();

    return QStringLiteral("Import complete: added %1, updated %2, skipped %3").arg(imported).arg(updated).arg(skipped);
}

bool ApiSiteModel::hasShareContentInClipboard() const {
    const auto *clipboard = QGuiApplication::clipboard();
    return clipboard && clipboard->text().trimmed().startsWith(QString::fromLatin1(kSharePrefix));
}

void ApiSiteModel::refreshSiteStatusAt(int index) {
    if (index < 0 || index >= items_.size())
        return;

    const QString baseUrl = items_[index].baseUrl;
    const QString normalized = normalizeBaseUrl(baseUrl);
    const QString siteType = typeAt(index);
    const QUrl url = (siteType == QStringLiteral("image") || siteType == QStringLiteral("shortvideo"))
        ? QUrl(baseUrl.trimmed())
        : statusCheckUrl(baseUrl);
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty()) {
        setAccessState(index, kAccessFailed);
        return;
    }

    setAccessState(index, kAccessChecking);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("MeloBox/1.0 site-check"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kAccessTimeoutMs);

    auto *reply = accessManager_.get(request);
    const QPointer<QNetworkReply> guardedReply(reply);
    const auto timer = std::make_shared<QElapsedTimer>();
    timer->start();

    QTimer::singleShot(kAccessTimeoutMs + 500, reply, [guardedReply]() {
        if (guardedReply && guardedReply->isRunning()) {
            guardedReply->abort();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, guardedReply, normalized, timer]() {
        if (!guardedReply)
            return;

        int row = -1;
        for (int i = 0; i < items_.size(); ++i) {
            if (normalizeBaseUrl(items_[i].baseUrl) == normalized) {
                row = i;
                break;
            }
        }

        const int elapsedMs = static_cast<int>(timer->elapsed());
        const auto httpStatus = guardedReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool networkOk = guardedReply->error() == QNetworkReply::NoError;
        const bool httpOk = httpStatus == 0 || (httpStatus >= 200 && httpStatus < 400);
        if (row >= 0) {
            setAccessState(row, networkOk && httpOk ? kAccessOk : kAccessFailed, elapsedMs);
        }

        guardedReply->deleteLater();
    });
}

void ApiSiteModel::refreshAllSiteStatuses() {
    for (int i = 0; i < items_.size(); ++i) {
        refreshSiteStatusAt(i);
    }
}

void ApiSiteModel::loadFromFile() {
    QFile file(configPath_);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    const auto doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject())
        return;

    const auto root = doc.object();
    currentIndex_ = root.value("currentIndex").toInt(0);

    const auto arr = root.value("sites").toArray();
    for (const auto &val : arr) {
        if (!val.isObject())
            continue;
        const auto obj = val.toObject();
        const QString name = obj.value("name").toString().trimmed();
        const QString baseUrl = obj.value("baseUrl").toString().trimmed();
        const QString type = normalizeSiteType(obj.value("type").toString(QStringLiteral("video")));
        if (!name.isEmpty() && !baseUrl.isEmpty()) {
            items_.append({name, baseUrl, type});
        }
    }
}

void ApiSiteModel::saveToFile() {
    QJsonObject root;
    root["currentIndex"] = currentIndex_;

    QJsonArray arr;
    for (const auto &item : items_) {
        QJsonObject obj;
        obj["name"] = item.name;
        obj["baseUrl"] = item.baseUrl;
        obj["type"] = normalizeSiteType(item.type);
        arr.append(obj);
    }
    root["sites"] = arr;

    QFile file(configPath_);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
    }
}

void ApiSiteModel::ensureDefaults() {
    if (items_.isEmpty()) {
        add(QStringLiteral("默认视频"), QStringLiteral("https://mtzy2.com/provide/vod"), QStringLiteral("video"));
    }

    bool hasImageSite = false;
    bool hasGzlAcgSite = false;
    bool hasCxkSite = false;
    bool hasEcySite = false;
    const QString gzlAcgUrl = QStringLiteral("https://api.yujn.cn/api/gzl_ACG.php?type=image&form=pc");
    const QString normalizedGzlAcgUrl = normalizeBaseUrl(gzlAcgUrl);
    const QString cxkUrl = QStringLiteral("http://api.yujn.cn/api/cxk.php?");
    const QString normalizedCxkUrl = normalizeBaseUrl(cxkUrl);
    const QString ecyUrl = QStringLiteral("http://api.yujn.cn/api/ecy.php?");
    const QString normalizedEcyUrl = normalizeBaseUrl(ecyUrl);
    for (const auto &item : items_) {
        if (normalizeSiteType(item.type) == QStringLiteral("image")) {
            hasImageSite = true;
            if (normalizeBaseUrl(item.baseUrl) == normalizedGzlAcgUrl) {
                hasGzlAcgSite = true;
            }
            if (normalizeBaseUrl(item.baseUrl) == normalizedCxkUrl) {
                hasCxkSite = true;
            }
            if (normalizeBaseUrl(item.baseUrl) == normalizedEcyUrl) {
                hasEcySite = true;
            }
        }
    }
    if (!hasImageSite) {
        add(QStringLiteral("随机图片"), QStringLiteral("https://t.alcy.cc/json?pc"), QStringLiteral("image"));
    }

    if (!hasGzlAcgSite) {
        add(QStringLiteral("ACG图片"), gzlAcgUrl, QStringLiteral("image"));
    }
    if (!hasCxkSite) {
        add(QStringLiteral("Heiliao image"), cxkUrl, QStringLiteral("image"));
    }
    if (!hasEcySite) {
        add(QStringLiteral("ACG image"), ecyUrl, QStringLiteral("image"));
    }
    auto addImageDefaultIfMissing = [this](const QString &name, const QString &url) {
        const QString normalizedUrl = normalizeBaseUrl(url);
        for (const auto &item : items_) {
            if (normalizeSiteType(item.type) == QStringLiteral("image") &&
                normalizeBaseUrl(item.baseUrl) == normalizedUrl) {
                return;
            }
        }
        add(name, url, QStringLiteral("image"));
    };
    addImageDefaultIfMissing(QStringLiteral("萌宠"), QStringLiteral("http://api.yujn.cn/api/mc.php??"));
    addImageDefaultIfMissing(QStringLiteral("鍘熺"), QStringLiteral("http://api.yujn.cn/api/ys.php??"));
    addImageDefaultIfMissing(QStringLiteral("Girl"), QStringLiteral("http://api.yujn.cn/api/jk.php??"));
    addImageDefaultIfMissing(QStringLiteral("白丝"), QStringLiteral("http://api.yujn.cn/api/baisi.php?"));
    addImageDefaultIfMissing(QStringLiteral("ACG"), QStringLiteral("https://app.zichen.zone/api/acg/api.php"));
    addImageDefaultIfMissing(QStringLiteral("1080P"), QStringLiteral("https://picapi.pai.al/api/1080P.php"));
    addImageDefaultIfMissing(QStringLiteral("镜花随机"), QStringLiteral("https://imgapi.jinghuashang.cn/random"));
    addImageDefaultIfMissing(QStringLiteral("Loli ACG"), QStringLiteral("https://www.loliapi.com/acg/"));
    addImageDefaultIfMissing(QStringLiteral("动漫"), QStringLiteral("https://api.fuchenboke.cn/api/dongman.php"));

    auto addShortVideoDefaultIfMissing = [this](const QString &name, const QString &url) {
        const QString normalizedUrl = normalizeBaseUrl(url);
        for (const auto &item : items_) {
            if (normalizeSiteType(item.type) == QStringLiteral("shortvideo") &&
                normalizeBaseUrl(item.baseUrl) == normalizedUrl) {
                return;
            }
        }
        add(name, url, QStringLiteral("shortvideo"));
    };
    addShortVideoDefaultIfMissing(QStringLiteral("Short video"), QStringLiteral("https://api.yujn.cn/api/zzxjj.php?type=video"));
    addShortVideoDefaultIfMissing(QStringLiteral("女大"), QStringLiteral("https://api.yujn.cn/api/nvda.php?type=video"));
    addShortVideoDefaultIfMissing(QStringLiteral("女高"), QStringLiteral("https://api.yujn.cn/api/nvgao.php?type=video"));
    addShortVideoDefaultIfMissing(QStringLiteral("怼脸"), QStringLiteral("https://api.yujn.cn/api/duilian.php?type=video"));
    addShortVideoDefaultIfMissing(QStringLiteral("黑丝"), QStringLiteral("https://api.yujn.cn/api/heisis.php?type=video"));
    addShortVideoDefaultIfMissing(QStringLiteral("白丝"), QStringLiteral("https://api.yujn.cn/api/baisis.php?type=video"));
    addShortVideoDefaultIfMissing(QStringLiteral("漫展"), QStringLiteral("https://api.yujn.cn/api/manzhan.php?type=video"));
    addShortVideoDefaultIfMissing(QStringLiteral("Short mix"), QStringLiteral("http://api.yujn.cn/api/juhexjj.php?type=video"));
    addShortVideoDefaultIfMissing(QStringLiteral("完美身材"), QStringLiteral("http://api.yujn.cn/api/wmsc.php?type=video"));
    addShortVideoDefaultIfMissing(QStringLiteral("cosplay"), QStringLiteral("http://api.yujn.cn/api/COS.php?type=video"));
    addShortVideoDefaultIfMissing(QStringLiteral("特色服装"), QStringLiteral("http://api.yujn.cn/api/hanfu.php"));
    addShortVideoDefaultIfMissing(QStringLiteral("吊带"), QStringLiteral("http://api.yujn.cn/api/diaodai.php?type=video"));
    addShortVideoDefaultIfMissing(QStringLiteral("Manyao"), QStringLiteral("http://api.yujn.cn/api/manyao.php?type=video"));
    addShortVideoDefaultIfMissing(QStringLiteral("足控"), QStringLiteral("http://api.yujn.cn/api/jpmt.php?type=video"));
    addShortVideoDefaultIfMissing(QStringLiteral("清纯"), QStringLiteral("http://api.yujn.cn/api/qingchun.php?type=video"));
    addShortVideoDefaultIfMissing(QStringLiteral("蹇墜鍙樿"), QStringLiteral("http://api.yujn.cn/api/ksbianzhuang.php?type=video"));
    addShortVideoDefaultIfMissing(QStringLiteral("Happy"), QStringLiteral("http://api.yujn.cn/api/ksbianzhuang.php?type=video"));
    addShortVideoDefaultIfMissing(QStringLiteral("萝莉"), QStringLiteral("http://api.yujn.cn/api/luoli.php?type=video"));
    addShortVideoDefaultIfMissing(QStringLiteral("Dance"), QStringLiteral("http://api.yujn.cn/api/rewu.php?type=video"));
    addShortVideoDefaultIfMissing(QStringLiteral("鍙樿"), QStringLiteral("http://api.yujn.cn/api/bianzhuang.php??"));
    addShortVideoDefaultIfMissing(QStringLiteral("Kuaishou girl"), QStringLiteral("http://api.yujn.cn/api/ksxjjsp.php?"));
    addShortVideoDefaultIfMissing(QStringLiteral("Girl backup"), QStringLiteral("http://api.yujn.cn/api/zzxjj.php"));

    if (currentIndex_ < 0 || currentIndex_ >= items_.size())
        currentIndex_ = 0;
    ensureShareSelectionSize();
    ensureAccessStateSize();
}

void ApiSiteModel::ensureShareSelectionSize() {
    while (shareSelected_.size() < items_.size())
        shareSelected_.append(false);
    while (shareSelected_.size() > items_.size())
        shareSelected_.removeLast();
}

void ApiSiteModel::ensureAccessStateSize() {
    while (accessStatus_.size() < items_.size())
        accessStatus_.append(kAccessUnknown);
    while (accessStatus_.size() > items_.size())
        accessStatus_.removeLast();

    while (accessLatencyMs_.size() < items_.size())
        accessLatencyMs_.append(-1);
    while (accessLatencyMs_.size() > items_.size())
        accessLatencyMs_.removeLast();
}

void ApiSiteModel::setAccessState(int index, int status, int latencyMs) {
    if (index < 0 || index >= items_.size())
        return;

    ensureAccessStateSize();
    if (accessStatus_[index] == status && accessLatencyMs_[index] == latencyMs)
        return;

    accessStatus_[index] = status;
    accessLatencyMs_[index] = latencyMs;
    const auto idx = createIndex(index, 0);
    emit dataChanged(idx, idx, {AccessStatusRole, AccessStatusTextRole, AccessLatencyMsRole});
}

QString ApiSiteModel::normalizeBaseUrl(const QString &baseUrl) {
    QString normalized = baseUrl.trimmed();
    while (normalized.endsWith('/'))
        normalized.chop(1);
    return normalized.toLower();
}

QString ApiSiteModel::normalizeSiteType(const QString &siteType) {
    const QString normalized = siteType.trimmed().toLower();
    if (normalized == QStringLiteral("image"))
        return QStringLiteral("image");
    if (normalized == QStringLiteral("shortvideo")
        || normalized == QStringLiteral("short_video")
        || normalized == QStringLiteral("short-video"))
        return QStringLiteral("shortvideo");
    return QStringLiteral("video");
}

QString ApiSiteModel::accessStatusText(int status, int latencyMs) {
    switch (status) {
    case kAccessChecking:
        return QStringLiteral("检测中");
    case kAccessOk:
        return latencyMs >= 0 ? QStringLiteral("Reachable | %1ms").arg(latencyMs) : QStringLiteral("Reachable");
    case kAccessFailed:
        return QStringLiteral("不可访问");
    default:
        return QStringLiteral("Not checked");
    }
}

QUrl ApiSiteModel::statusCheckUrl(const QString &baseUrl) {
    QUrl url(baseUrl.trimmed());
    if (!url.isValid())
        return url;

    QString query = url.query();
    if (!query.contains("ac=")) {
        if (!query.isEmpty())
            query += '&';
        query += "ac=list";
        url.setQuery(query);
    }
    return url;
}

QString ApiSiteModel::encodeSharePayload(const QJsonObject &payload) {
    const QByteArray plain = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const QByteArray salt = randomSalt();
    const QByteArray cipher = cryptBytes(plain, salt);

    QJsonObject package;
    package["v"] = 1;
    package["salt"] = toBase64Text(salt);
    package["data"] = toBase64Text(cipher);
    package["mac"] = toBase64Text(payloadMac(salt, cipher));

    const QByteArray packageJson = QJsonDocument(package).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(kSharePrefix) + toBase64Text(packageJson);
}

bool ApiSiteModel::decodeSharePayload(const QString &text, QJsonObject *payload) {
    if (!payload)
        return false;

    const QString prefix = QString::fromLatin1(kSharePrefix);
    const QString trimmed = text.trimmed();
    if (!trimmed.startsWith(prefix))
        return false;

    bool ok = false;
    const QByteArray packageBytes = fromBase64Text(trimmed.mid(prefix.size()), &ok);
    if (!ok)
        return false;

    QJsonParseError packageError;
    const auto packageDoc = QJsonDocument::fromJson(packageBytes, &packageError);
    if (packageError.error != QJsonParseError::NoError || !packageDoc.isObject())
        return false;

    const auto package = packageDoc.object();
    if (package.value("v").toInt() != 1)
        return false;

    const QByteArray salt = fromBase64Text(package.value("salt").toString(), &ok);
    if (!ok || salt.isEmpty())
        return false;

    const QByteArray cipher = fromBase64Text(package.value("data").toString(), &ok);
    if (!ok || cipher.isEmpty())
        return false;

    const QByteArray mac = fromBase64Text(package.value("mac").toString(), &ok);
    if (!ok || mac != payloadMac(salt, cipher))
        return false;

    const QByteArray plain = cryptBytes(cipher, salt);
    QJsonParseError payloadError;
    const auto payloadDoc = QJsonDocument::fromJson(plain, &payloadError);
    if (payloadError.error != QJsonParseError::NoError || !payloadDoc.isObject())
        return false;

    *payload = payloadDoc.object();
    return true;
}


