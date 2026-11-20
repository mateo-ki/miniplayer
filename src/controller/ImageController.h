#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include "media/ImageCacheService.h"

class ApiSiteModel;

// 图片模块：随机图片获取、预载池、内存缓存 (不落盘)、保存与复制。
class ImageController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool imageLoading READ imageLoading NOTIFY imageStateChanged)
    Q_PROPERTY(QString currentImageUrl READ currentImageUrl NOTIFY imageStateChanged)
    Q_PROPERTY(QString currentImageDisplayUrl READ currentImageDisplayUrl NOTIFY imageStateChanged)
    Q_PROPERTY(QString imageMessage READ imageMessage NOTIFY imageStateChanged)
    Q_PROPERTY(int imageDiskCacheCount READ imageDiskCacheCount NOTIFY imageCacheChanged)
    Q_PROPERTY(qint64 imageDiskCacheBytes READ imageDiskCacheBytes NOTIFY imageCacheChanged)
    Q_PROPERTY(int imageCacheCount READ imageDiskCacheCount NOTIFY imageCacheChanged)
    Q_PROPERTY(qint64 imageCacheBytes READ imageDiskCacheBytes NOTIFY imageCacheChanged)
public:
    explicit ImageController(ApiSiteModel *apiSiteModel, QObject *parent = nullptr);

    bool imageLoading() const;
    QString currentImageUrl() const;
    QString currentImageDisplayUrl() const;
    QString imageMessage() const;
    int imageDiskCacheCount() const;
    qint64 imageDiskCacheBytes() const;

    Q_INVOKABLE void loadRandomImage(const QString &apiUrl = QString());
    Q_INVOKABLE void showPrefetchedImage(const QString &imageUrl);
    Q_INVOKABLE void prefetchImage(const QString &apiUrl);
    Q_INVOKABLE void clearImageCache();
    Q_INVOKABLE void saveCurrentImage();
    Q_INVOKABLE void copyCurrentImage();

signals:
    void imageStateChanged();
    void imageCacheChanged();
    void imagePrefetched(const QString &apiUrl, const QString &imageUrl);

private:
    void setImageState(bool loading, const QString &url, const QString &message,
                       const QString &displayUrl = QString());
    bool loadImageBytesFromLocalUrl(const QString &url);
    QString cacheImageBytes(const QString &apiUrl, const QString &sourceUrl,
                            const QByteArray &bytes, const QString &mimeType);
    void saveImageBytes(const QByteArray &bytes, const QUrl &sourceUrl, const QString &contentType);

    ApiSiteModel *apiSiteModel_;
    ImageCacheService imageCache_;
    bool imageLoading_ = false;
    QString currentImageUrl_;
    QString currentImageDisplayUrl_;
    QString imageMessage_;
    QByteArray currentImageBytes_;
    QString currentImageContentType_;
    qint64 lastImageLoadRequestMs_ = 0;
};
