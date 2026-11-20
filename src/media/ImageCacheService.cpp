#include "media/ImageCacheService.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <algorithm>

namespace {
constexpr qint64 kMaxMemoryCacheBytes = 100LL * 1024 * 1024; // 100 MB 内存上限
constexpr int kMaxMemoryEntries = 50;                       // 内存中最多保留 50 张图片

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
} // namespace

ImageCacheService::ImageCacheService() {
    // 启动时清理历史版本残留的磁盘缓存文件，不占用户硬盘空间
    cleanupLegacyDiskFiles();
}

void ImageCacheService::cleanupLegacyDiskFiles() {
    const QString legacyDir = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("images"));
    if (QDir(legacyDir).exists()) {
        QDir(legacyDir).removeRecursively();
    }
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

CachedImageEntry ImageCacheService::take(const QString &apiUrl) {
    // 从内存中挑选本会话尚未消费过的缓存图片条目
    QList<int> candidates;
    for (int index = 0; index < entries_.size(); ++index) {
        const auto &entry = entries_.at(index);
        if (entry.apiUrl != apiUrl)
            continue;
        if (consumedThisSession_.contains(entry.key))
            continue;
        candidates.append(index);
    }
    if (candidates.isEmpty())
        return {};

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int pickedIndex = candidates.at(
        QRandomGenerator::global()->bounded(candidates.size()));
    auto &entry = entries_[pickedIndex];
    consumedThisSession_.insert(entry.key);
    entry.lastAccessMs = now;
    return entry;
}

CachedImageEntry ImageCacheService::findByDisplayUrl(const QString &displayUrl) const {
    if (displayUrl.isEmpty()) return {};
    for (const auto &entry : entries_) {
        if (entry.displayUrl == displayUrl)
            return entry;
    }
    return {};
}

QString ImageCacheService::put(const QString &apiUrl, const QString &sourceUrl,
                               const QByteArray &bytes, const QString &mimeType) {
    if (bytes.isEmpty() || bytes.size() > 15 * 1024 * 1024) return {};

    const QByteArray digest = QCryptographicHash::hash(
        apiUrl.toUtf8() + '\0' + sourceUrl.toUtf8() + '\0' + bytes.left(128),
        QCryptographicHash::Sha256).toHex();
    const QString key = QString::fromLatin1(digest);
    const QString extension = extensionFromBytes(bytes, mimeType);
    const QString effectiveMime = mimeType.isEmpty() ? mimeFromExtension(extension) : mimeType;
    const QString displayUrl = QStringLiteral("data:%1;base64,%2").arg(
        effectiveMime, QString::fromLatin1(bytes.toBase64()));

    // 若本会话已消费过该 key，返回已生成的 displayUrl
    if (consumedThisSession_.contains(key)) {
        for (const auto &e : entries_) {
            if (e.key == key) return e.displayUrl;
        }
        return displayUrl;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 移除已有相同 key 的旧条目
    for (int index = entries_.size() - 1; index >= 0; --index) {
        if (entries_.at(index).key == key) {
            entries_.removeAt(index);
        }
    }

    CachedImageEntry entry;
    entry.key = key;
    entry.bytes = bytes;
    entry.mimeType = effectiveMime;
    entry.sourceUrl = sourceUrl;
    entry.apiUrl = apiUrl;
    entry.displayUrl = displayUrl;
    entry.size = bytes.size();
    entry.lastAccessMs = now;

    entries_.append(entry);
    prune();
    return displayUrl;
}

void ImageCacheService::prune() {
    // 内存淘汰策略 (LRU: 超出数量或内存容量时，淘汰最久未访问的条目)
    if (entries_.size() > kMaxMemoryEntries || sizeBytes() > kMaxMemoryCacheBytes) {
        std::sort(entries_.begin(), entries_.end(), [](const CachedImageEntry &left, const CachedImageEntry &right) {
            return left.lastAccessMs < right.lastAccessMs;
        });
        while ((entries_.size() > kMaxMemoryEntries || sizeBytes() > kMaxMemoryCacheBytes) && !entries_.isEmpty()) {
            entries_.removeFirst();
        }
    }
}

void ImageCacheService::clear() {
    entries_.clear();
    consumedThisSession_.clear();
}

int ImageCacheService::count() const {
    return entries_.size();
}

qint64 ImageCacheService::sizeBytes() const {
    qint64 total = 0;
    for (const auto &entry : entries_) {
        total += entry.size;
    }
    return total;
}
