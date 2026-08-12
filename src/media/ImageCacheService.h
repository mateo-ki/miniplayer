#pragma once

#include <QByteArray>
#include <QSet>
#include <QString>
#include <QVariantList>

struct CachedImageEntry {
    QByteArray bytes;
    QString mimeType;
    QString sourceUrl;
};

class ImageCacheService final {
public:
    ImageCacheService();

    CachedImageEntry take(const QString &apiUrl);
    void put(const QString &apiUrl, const QString &sourceUrl,
             const QByteArray &bytes, const QString &mimeType);
    void clear();
    int count() const;
    qint64 sizeBytes() const;

private:
    QString cacheDirectory_;
    QVariantList entries_;
    QSet<QString> consumedThisSession_;

    void loadIndex();
    void saveIndex() const;
    void prune();
};
