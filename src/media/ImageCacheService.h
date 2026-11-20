#pragma once

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>

struct CachedImageEntry {
    QString key;
    QByteArray bytes;
    QString mimeType;
    QString sourceUrl;
    QString apiUrl;
    QString displayUrl;
    qint64 size = 0;
    qint64 lastAccessMs = 0;
};

// 图片内存缓存服务 (纯内存缓存，不持久化到硬盘，提前预载)
class ImageCacheService final {
public:
    ImageCacheService();
    ~ImageCacheService() = default;

    // 内存缓存接口
    CachedImageEntry take(const QString &apiUrl);
    QString put(const QString &apiUrl, const QString &sourceUrl,
                const QByteArray &bytes, const QString &mimeType);
    void clear();
    int count() const;
    qint64 sizeBytes() const;

    CachedImageEntry findByDisplayUrl(const QString &displayUrl) const;

    static QString extensionFromBytes(const QByteArray &bytes, const QString &mimeType);
    static QString mimeFromExtension(const QString &extension);

private:
    QList<CachedImageEntry> entries_;
    QSet<QString> consumedThisSession_;

    void prune();
    void cleanupLegacyDiskFiles();
};
