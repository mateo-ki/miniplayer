#pragma once

#include <QByteArray>
#include <QSet>
#include <QString>
#include <QVariantList>

struct CachedImageEntry {
    QByteArray bytes;
    QString mimeType;
    QString sourceUrl;
    QString localPath;
};

class ImageCacheService final {
public:
    ImageCacheService();

    QString directory() const;
    CachedImageEntry take(const QString &apiUrl);
    QString put(const QString &apiUrl, const QString &sourceUrl,
                const QByteArray &bytes, const QString &mimeType);
    void clear();
    int count() const;
    qint64 sizeBytes() const;

    static QString extensionFromBytes(const QByteArray &bytes, const QString &mimeType);
    static QString mimeFromExtension(const QString &extension);

private:
    QString cacheDirectory_;
    QVariantList entries_;
    QSet<QString> consumedThisSession_;

    void loadIndex();
    void saveIndex() const;
    void prune();
    void migrateLegacyBinFiles();
    QString absolutePath(const QString &fileName) const;
};
