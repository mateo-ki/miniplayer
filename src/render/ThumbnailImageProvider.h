#pragma once

#include <QQuickImageProvider>
#include <QImage>
#include <QMutex>
#include <QMap>

class ThumbnailImageProvider : public QQuickImageProvider {
public:
    ThumbnailImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
    void update(const QString &key, const QImage &image);

private:
    QMap<QString, QImage> cache_;
    QMutex mutex_;
};
