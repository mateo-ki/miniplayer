#include "media/ImageCacheService.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace {
constexpr qint64 kMaxCacheBytes = 200LL * 1024 * 1024;
constexpr qint64 kMaxAgeMs = 7LL * 24 * 60 * 60 * 1000;
}

ImageCacheService::ImageCacheService() {
    cacheDirectory_ = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("images"));
    QDir().mkpath(cacheDirectory_);
    loadIndex();
    prune();
}

void ImageCacheService::loadIndex() {
    QFile file(QDir(cacheDirectory_).filePath(QStringLiteral("index.json")));
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
    for (const QJsonValue &value : array)
        entries_.append(value.toObject().toVariantMap());
}

void ImageCacheService::saveIndex() const {
    QJsonArray array;
    for (const QVariant &entry : entries_)
        array.append(QJsonObject::fromVariantMap(entry.toMap()));
    QSaveFile file(QDir(cacheDirectory_).filePath(QStringLiteral("index.json")));
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
    file.commit();
}

CachedImageEntry ImageCacheService::take(const QString &apiUrl) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int index = entries_.size() - 1; index >= 0; --index) {
        QVariantMap entry = entries_.at(index).toMap();
        const QString key = entry.value(QStringLiteral("key")).toString();
        if (entry.value(QStringLiteral("apiUrl")).toString() != apiUrl
                || consumedThisSession_.contains(key)) continue;
        QFile file(QDir(cacheDirectory_).filePath(entry.value(QStringLiteral("file")).toString()));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QByteArray bytes = file.readAll();
        if (bytes.isEmpty()) continue;
        consumedThisSession_.insert(key);
        entry[QStringLiteral("lastAccessMs")] = now;
        entries_[index] = entry;
        saveIndex();
        return {bytes, entry.value(QStringLiteral("mimeType")).toString(),
                entry.value(QStringLiteral("sourceUrl")).toString()};
    }
    return {};
}

void ImageCacheService::put(const QString &apiUrl, const QString &sourceUrl,
                            const QByteArray &bytes, const QString &mimeType) {
    if (bytes.isEmpty() || bytes.size() > 15 * 1024 * 1024) return;
    const QByteArray digest = QCryptographicHash::hash(
        apiUrl.toUtf8() + '\0' + sourceUrl.toUtf8() + '\0' + bytes.left(128),
        QCryptographicHash::Sha256).toHex();
    const QString key = QString::fromLatin1(digest);
    const QString fileName = key + QStringLiteral(".bin");
    if (consumedThisSession_.contains(key)) return;

    QSaveFile file(QDir(cacheDirectory_).filePath(fileName));
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(bytes);
    if (!file.commit()) return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int index = entries_.size() - 1; index >= 0; --index) {
        if (entries_.at(index).toMap().value(QStringLiteral("key")).toString() == key)
            entries_.removeAt(index);
    }
    QVariantMap entry;
    entry[QStringLiteral("key")] = key;
    entry[QStringLiteral("file")] = fileName;
    entry[QStringLiteral("apiUrl")] = apiUrl;
    entry[QStringLiteral("sourceUrl")] = sourceUrl;
    entry[QStringLiteral("mimeType")] = mimeType;
    entry[QStringLiteral("size")] = bytes.size();
    entry[QStringLiteral("createdMs")] = now;
    entry[QStringLiteral("lastAccessMs")] = now;
    entries_.append(entry);
    prune();
}

void ImageCacheService::prune() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int index = entries_.size() - 1; index >= 0; --index) {
        const QVariantMap entry = entries_.at(index).toMap();
        const QString path = QDir(cacheDirectory_).filePath(entry.value(QStringLiteral("file")).toString());
        if (now - entry.value(QStringLiteral("createdMs")).toLongLong() > kMaxAgeMs
                || !QFile::exists(path)) {
            QFile::remove(path);
            entries_.removeAt(index);
        }
    }
    std::sort(entries_.begin(), entries_.end(), [](const QVariant &left, const QVariant &right) {
        return left.toMap().value(QStringLiteral("lastAccessMs")).toLongLong()
            < right.toMap().value(QStringLiteral("lastAccessMs")).toLongLong();
    });
    while (sizeBytes() > kMaxCacheBytes && !entries_.isEmpty()) {
        const QVariantMap entry = entries_.takeFirst().toMap();
        QFile::remove(QDir(cacheDirectory_).filePath(entry.value(QStringLiteral("file")).toString()));
    }
    saveIndex();
}

void ImageCacheService::clear() {
    for (const QVariant &value : entries_)
        QFile::remove(QDir(cacheDirectory_).filePath(value.toMap().value(QStringLiteral("file")).toString()));
    entries_.clear();
    consumedThisSession_.clear();
    saveIndex();
}

int ImageCacheService::count() const { return entries_.size(); }

qint64 ImageCacheService::sizeBytes() const {
    qint64 total = 0;
    for (const QVariant &value : entries_)
        total += value.toMap().value(QStringLiteral("size")).toLongLong();
    return total;
}
