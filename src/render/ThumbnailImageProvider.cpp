#include "render/ThumbnailImageProvider.h"

ThumbnailImageProvider::ThumbnailImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage ThumbnailImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    QMutexLocker lock(&mutex_);
    auto it = cache_.find(id);
    if (it == cache_.end()) return {};
    if (size) *size = it->size();
    if (requestedSize.isValid()) {
        return it->scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return *it;
}

void ThumbnailImageProvider::update(const QString &key, const QImage &image) {
    QMutexLocker lock(&mutex_);
    cache_.insert(key, image);
    // Keep cache small — remove entries except the latest
    while (cache_.size() > 8) {
        cache_.erase(cache_.begin());
    }
}
