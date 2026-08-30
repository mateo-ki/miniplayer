#include "media/ImageCacheService.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <QRandomGenerator>

namespace {
constexpr qint64 kMaxCacheBytes = 200LL * 1024 * 1024;
constexpr qint64 kMaxAgeMs = 7LL * 24 * 60 * 60 * 1000;

bool isJpeg(const QByteArray &bytes) {
    return bytes.size() >= 3
        && static_cast<unsigned char>(bytes[0]) == 0xFF
        && static_cast<unsigned char>(bytes[1]) == 0xD8
        && static_cast<unsigned char>(bytes[2]) == 0xFF;
}

bool isPng(const QByteArray &bytes) {
    return bytes.size() >= 8
        && static_cast<unsigned char>(bytes[0]) == 0x89
        && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G';
}

bool isGif(const QByteArray &bytes) {
    return bytes.startsWith("GIF8");
}

bool isWebp(const QByteArray &bytes) {
    return bytes.size() >= 12
        && bytes.startsWith("RIFF")
        && bytes.mid(8, 4) == "WEBP";
}

bool isBmp(const QByteArray &bytes) {
    return bytes.startsWith("BM");
}
}

ImageCacheService::ImageCacheService() {
    cacheDirectory_ = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("images"));
    QDir().mkpath(cacheDirectory_);
    loadIndex();
    migrateLegacyBinFiles();
    prune();
}

QString ImageCacheService::directory() const {
    return cacheDirectory_;
}

QString ImageCacheService::absolutePath(const QString &fileName) const {
    return QDir(cacheDirectory_).filePath(fileName);
}

QString ImageCacheService::extensionFromBytes(const QByteArray &bytes, const QString &mimeType) {
    if (isJpeg(bytes)) return QStringLiteral("jpg");
    if (isPng(bytes)) return QStringLiteral("png");
    if (isGif(bytes)) return QStringLiteral("gif");
    if (isWebp(bytes)) return QStringLiteral("webp");
    if (isBmp(bytes)) return QStringLiteral("bmp");

    const QString mime = mimeType.toLower();
    if (mime.contains(QStringLiteral("jpeg")) || mime.contains(QStringLiteral("jpg")))
        return QStringLiteral("jpg");
    if (mime.contains(QStringLiteral("png"))) return QStringLiteral("png");
    if (mime.contains(QStringLiteral("gif"))) return QStringLiteral("gif");
    if (mime.contains(QStringLiteral("webp"))) return QStringLiteral("webp");
    if (mime.contains(QStringLiteral("bmp"))) return QStringLiteral("bmp");
    return QStringLiteral("jpg");
}

QString ImageCacheService::mimeFromExtension(const QString &extension) {
    const QString ext = extension.toLower();
    if (ext == QLatin1String("png")) return QStringLiteral("image/png");
    if (ext == QLatin1String("gif")) return QStringLiteral("image/gif");
    if (ext == QLatin1String("webp")) return QStringLiteral("image/webp");
    if (ext == QLatin1String("bmp")) return QStringLiteral("image/bmp");
    return QStringLiteral("image/jpeg");
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

void ImageCacheService::migrateLegacyBinFiles() {
    bool changed = false;
    for (int index = 0; index < entries_.size(); ++index) {
        QVariantMap entry = entries_.at(index).toMap();
        const QString fileName = entry.value(QStringLiteral("file")).toString();
        if (!fileName.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive))
            continue;

        const QString oldPath = absolutePath(fileName);
        QFile file(oldPath);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray bytes = file.readAll();
        file.close();
        if (bytes.isEmpty())
            continue;

        const QString extension = extensionFromBytes(bytes, entry.value(QStringLiteral("mimeType")).toString());
        const QString newName = QFileInfo(fileName).completeBaseName() + QLatin1Char('.') + extension;
        const QString newPath = absolutePath(newName);
        if (oldPath != newPath) {
            if (QFile::exists(newPath))
                QFile::remove(oldPath);
            else if (!QFile::rename(oldPath, newPath))
                continue;
        }
        entry[QStringLiteral("file")] = newName;
        if (entry.value(QStringLiteral("mimeType")).toString().isEmpty())
            entry[QStringLiteral("mimeType")] = mimeFromExtension(extension);
        entries_[index] = entry;
        changed = true;
    }

    const QStringList leftover = QDir(cacheDirectory_).entryList(
        QStringList{QStringLiteral("*.bin")}, QDir::Files);
    for (const QString &binName : leftover) {
        bool referenced = false;
        for (const QVariant &value : entries_) {
            if (value.toMap().value(QStringLiteral("file")).toString().compare(
                    binName, Qt::CaseInsensitive) == 0) {
                referenced = true;
                break;
            }
        }
        if (!referenced)
            QFile::remove(absolutePath(binName));
    }

    if (changed)
        saveIndex();
}

CachedImageEntry ImageCacheService::take(const QString &apiUrl) {
    // 收集与 apiUrl 匹配且未被本会话消费的条目，随机抽一个返回，
    // 避免"换一张"总是回灌同一张。全消费后清空标记让图片循环复用。
    QList<int> candidates;
    bool hasEntryForUrl = false;
    bool allConsumed = true;
    for (int index = 0; index < entries_.size(); ++index) {
        const QVariantMap entry = entries_.at(index).toMap();
        if (entry.value(QStringLiteral("apiUrl")).toString() != apiUrl)
            continue;
        hasEntryForUrl = true;
        const QString key = entry.value(QStringLiteral("key")).toString();
        if (consumedThisSession_.contains(key))
            continue;
        allConsumed = false;
        candidates.append(index);
    }
    if (hasEntryForUrl && allConsumed) {
        consumedThisSession_.clear();
        // 重置后再收集一次候选（清空后全部条目都可用）。
        candidates.clear();
        for (int index = 0; index < entries_.size(); ++index) {
            const QVariantMap entry = entries_.at(index).toMap();
            if (entry.value(QStringLiteral("apiUrl")).toString() == apiUrl)
                candidates.append(index);
        }
    }
    if (candidates.isEmpty())
        return {};

    const int pickedIndex = candidates.at(
        QRandomGenerator::global()->bounded(candidates.size()));

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVariantMap entry = entries_.at(pickedIndex).toMap();
    const QString key = entry.value(QStringLiteral("key")).toString();
    const QString fileName = entry.value(QStringLiteral("file")).toString();
    const QString path = absolutePath(fileName);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty())
        return {};
    consumedThisSession_.insert(key);
    entry[QStringLiteral("lastAccessMs")] = now;
    entries_[pickedIndex] = entry;
    saveIndex();
    QString mime = entry.value(QStringLiteral("mimeType")).toString();
    if (mime.isEmpty())
        mime = mimeFromExtension(QFileInfo(fileName).suffix());
    return {bytes, mime, entry.value(QStringLiteral("sourceUrl")).toString(), path};
}

QString ImageCacheService::put(const QString &apiUrl, const QString &sourceUrl,
                               const QByteArray &bytes, const QString &mimeType) {
    if (bytes.isEmpty() || bytes.size() > 15 * 1024 * 1024) return {};
    const QByteArray digest = QCryptographicHash::hash(
        apiUrl.toUtf8() + '\0' + sourceUrl.toUtf8() + '\0' + bytes.left(128),
        QCryptographicHash::Sha256).toHex();
    const QString key = QString::fromLatin1(digest);
    const QString extension = extensionFromBytes(bytes, mimeType);
    const QString fileName = key + QLatin1Char('.') + extension;
    const QString path = absolutePath(fileName);

    auto existingPathForKey = [this, &key, &path]() -> QString {
        for (const QVariant &value : entries_) {
            const QVariantMap entry = value.toMap();
            if (entry.value(QStringLiteral("key")).toString() != key)
                continue;
            const QString existing = absolutePath(entry.value(QStringLiteral("file")).toString());
            if (QFile::exists(existing))
                return existing;
        }
        if (QFile::exists(path))
            return path;
        return {};
    };

    if (consumedThisSession_.contains(key))
        return existingPathForKey();

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(bytes);
    if (!file.commit()) return {};

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
    entry[QStringLiteral("mimeType")] = mimeType.isEmpty() ? mimeFromExtension(extension) : mimeType;
    entry[QStringLiteral("size")] = bytes.size();
    entry[QStringLiteral("createdMs")] = now;
    entry[QStringLiteral("lastAccessMs")] = now;
    entries_.append(entry);
    prune();
    return path;
}

void ImageCacheService::prune() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int index = entries_.size() - 1; index >= 0; --index) {
        const QVariantMap entry = entries_.at(index).toMap();
        const QString path = absolutePath(entry.value(QStringLiteral("file")).toString());
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
        QFile::remove(absolutePath(entry.value(QStringLiteral("file")).toString()));
    }
    saveIndex();
}

void ImageCacheService::clear() {
    for (const QVariant &value : entries_)
        QFile::remove(absolutePath(value.toMap().value(QStringLiteral("file")).toString()));
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
