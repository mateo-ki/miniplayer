#include "controller/ImageController.h"

#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "models/ApiSiteModel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrlQuery>

#include "common/MediaUtils.h"
#include "infrastructure/Logger.h"

bool ImageController::imageLoading() const { return imageLoading_; }
QString ImageController::currentImageUrl() const { return currentImageUrl_; }
QString ImageController::currentImageDisplayUrl() const { return currentImageDisplayUrl_; }
QString ImageController::imageMessage() const { return imageMessage_; }
int ImageController::imageDiskCacheCount() const { return imageCache_.count(); }
qint64 ImageController::imageDiskCacheBytes() const { return imageCache_.sizeBytes(); }

ImageController::ImageController(ApiSiteModel *apiSiteModel, QObject *parent)
    : QObject(parent), apiSiteModel_(apiSiteModel), imageCache_() {
}

void ImageController::setImageState(bool loading, const QString &url, const QString &message, const QString &displayUrl) {
    bool changed = false;
    const QString resolvedDisplayUrl = displayUrl.isNull() ? currentImageDisplayUrl_ : displayUrl;
    // setImageState 由 QML 频繁触发 (imageLoading/图片源切换), display 常是 data: base64
    // 图片, 整行记进日志会令 miniplayer.log 膨胀到上百 MB。状态变更逻辑本身无需日志佐证,
    // 去掉这条 info, 保留真实的重定向/下载错误日志 (见下方 redirect/error)。
    if (imageLoading_ != loading) {
        imageLoading_ = loading;
        changed = true;
    }
    if (currentImageUrl_ != url) {
        currentImageUrl_ = url;
        changed = true;
    }
    if (currentImageDisplayUrl_ != resolvedDisplayUrl) {
        currentImageDisplayUrl_ = resolvedDisplayUrl;
        changed = true;
    }
    if (imageMessage_ != message) {
        imageMessage_ = message;
        changed = true;
    }
    if (changed)
        emit imageStateChanged();
}

void ImageController::showPrefetchedImage(const QString &imageUrl) {
    const QString trimmed = imageUrl.trimmed();
    if (trimmed.isEmpty()) return;

    currentImageBytes_.clear();
    currentImageContentType_.clear();
    const CachedImageEntry cached = imageCache_.findByDisplayUrl(trimmed);
    if (!cached.bytes.isEmpty()) {
        currentImageBytes_ = cached.bytes;
        currentImageContentType_ = cached.mimeType;
    } else if (trimmed.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive)) {
        // 兼容 data: 链接
        const int separator = trimmed.indexOf(QLatin1Char(','));
        const int typeEnd = trimmed.indexOf(QLatin1Char(';'));
        if (separator > 0) {
            currentImageContentType_ = trimmed.mid(5, typeEnd > 5 ? typeEnd - 5 : separator - 5);
            currentImageBytes_ = QByteArray::fromBase64(trimmed.mid(separator + 1).toLatin1());
        }
    } else {
        loadImageBytesFromLocalUrl(trimmed);
    }
    setImageState(false, QStringLiteral("prefetched://image"), QStringLiteral("已从缓存池加载图片"), trimmed);
}

void ImageController::prefetchImage(const QString &apiUrl) {
    const QString trimmedApiUrl = apiUrl.trimmed();
    if (trimmedApiUrl.isEmpty()) {
        emit imagePrefetched(trimmedApiUrl, {});
        return;
    }

    // 预载池只装网络新图：缓存优先会让预载池被旧图占满，一旦缓存
    // 非空就再也不发网络请求，"换一张"只能反复回放同一批旧图。
    // 旧缓存图留给 loadRandomImage 的兜底路径在预载池为空时应急。

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(trimmedApiUrl)};
    request.setTransferTimeout(5000);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "application/json,image/*,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = nam->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, trimmedApiUrl]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            nam->deleteLater();
            emit imagePrefetched(trimmedApiUrl, {});
            return;
        }

        const QByteArray bytes = reply->readAll();
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        const QString redirectedUrl = MediaUtils::redirectedUrlFromReply(reply);
        const QString imageUrl = redirectedUrl.isEmpty()
            ? MediaUtils::extractImageUrlFromResponse(bytes, reply->url(), contentType)
            : redirectedUrl;
        if (contentType.startsWith(QStringLiteral("image/"), Qt::CaseInsensitive)) {
            nam->deleteLater();
            // 剥掉拼在图片字节前的 PHP Warning 等 HTML；剥不出有效图片按
            // 预载失败处理 (QML 侧会重试)，绝不把无效数据写进缓存。
            const QByteArray imageBytes = MediaUtils::sanitizedImageBytes(bytes);
            if (imageBytes.isEmpty()) {
                emit imagePrefetched(trimmedApiUrl, {});
                return;
            }
            const QString effectiveMime = ImageCacheService::mimeFromExtension(
                ImageCacheService::extensionFromBytes(imageBytes,
                    contentType.section(QLatin1Char(';'), 0, 0).trimmed()));
            const QString displayUrl = cacheImageBytes(trimmedApiUrl, reply->url().toString(), imageBytes, effectiveMime);
            emit imageCacheChanged();
            emit imagePrefetched(trimmedApiUrl, displayUrl);
            return;
        }
        if (imageUrl.trimmed().isEmpty()) {
            nam->deleteLater();
            emit imagePrefetched(trimmedApiUrl, {});
            return;
        }

        QNetworkRequest imageRequest{QUrl(imageUrl)};
        imageRequest.setTransferTimeout(5000);
        imageRequest.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
        imageRequest.setRawHeader("Accept", "image/*,*/*");
        imageRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        auto *imageReply = nam->get(imageRequest);
        connect(imageReply, &QNetworkReply::finished, this,
                [this, imageReply, nam, trimmedApiUrl]() {
            imageReply->deleteLater();
            nam->deleteLater();
            if (imageReply->error() != QNetworkReply::NoError) {
                emit imagePrefetched(trimmedApiUrl, {});
                return;
            }
            const QByteArray imageBytes = imageReply->readAll();
            if (imageBytes.isEmpty() || imageBytes.size() > 15 * 1024 * 1024) {
                emit imagePrefetched(trimmedApiUrl, {});
                return;
            }
            const QByteArray sanitizedBytes = MediaUtils::sanitizedImageBytes(imageBytes);
            if (sanitizedBytes.isEmpty()) {
                emit imagePrefetched(trimmedApiUrl, {});
                return;
            }
            const QString effectiveMime = ImageCacheService::mimeFromExtension(
                ImageCacheService::extensionFromBytes(sanitizedBytes,
                    imageReply->header(QNetworkRequest::ContentTypeHeader).toString()
                        .section(QLatin1Char(';'), 0, 0).trimmed()));
            const QString displayUrl = cacheImageBytes(trimmedApiUrl, imageReply->url().toString(), sanitizedBytes, effectiveMime);
            emit imageCacheChanged();
            emit imagePrefetched(trimmedApiUrl, displayUrl);
        });
    });
}

void ImageController::clearImageCache() {
    imageCache_.clear();
    imageMessage_ = QStringLiteral("图片内存缓存已清理");
    emit imageCacheChanged();
    emit imageStateChanged();
}

void ImageController::loadRandomImage(const QString &requestedApiUrl) {
    if (imageLoading_) {
        Logger::instance().info(QStringLiteral("[ImageDebug] ignore duplicate loadRandomImage while loading"));
        return;
    }
    lastImageLoadRequestMs_ = QDateTime::currentMSecsSinceEpoch();

    const QString apiUrl = requestedApiUrl.trimmed().isEmpty()
        ? apiSiteModel_->imageBaseUrl().trimmed()
        : requestedApiUrl.trimmed();
    Logger::instance().info(QStringLiteral("[ImageDebug] loadRandomImage api=%1 currentSiteIndex=%2")
        .arg(apiUrl)
        .arg(apiSiteModel_->currentIndex()));
    if (apiUrl.isEmpty()) {
        setImageState(false, currentImageUrl_, QStringLiteral("没有配置图片站点"));
        return;
    }

    // 内存缓存有直接可用的图片时先展示，用户点击“换一张”不必等网络往返；
    // 预载池会继续在后台补新图。
    const CachedImageEntry cached = imageCache_.take(apiUrl);
    if (!cached.bytes.isEmpty() && !cached.displayUrl.isEmpty()) {
        currentImageBytes_ = cached.bytes;
        currentImageContentType_ = cached.mimeType;
        setImageState(false, cached.sourceUrl.isEmpty() ? currentImageUrl_ : cached.sourceUrl,
            QStringLiteral("已从内存缓存加载图片"), cached.displayUrl);
        emit imageCacheChanged();
        return;
    }

    setImageState(true, currentImageUrl_, QStringLiteral("正在获取图片..."), QStringLiteral(""));
    currentImageBytes_.clear();
    currentImageContentType_.clear();

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(apiUrl)};
    request.setTransferTimeout(5000);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "application/json,image/*,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            nam->deleteLater();
            setImageState(false, currentImageUrl_, QStringLiteral("图片请求失败：%1").arg(reply->errorString()));
            return;
        }

        const QByteArray bytes = reply->readAll();
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        Logger::instance().info(QStringLiteral("[ImageDebug] api reply url=%1 status=%2 contentType=%3 bytes=%4")
            .arg(reply->url().toString())
            .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
            .arg(contentType)
            .arg(bytes.size()));
        const QString redirectedImageUrl = MediaUtils::redirectedUrlFromReply(reply);
        const QString imageUrl = redirectedImageUrl.isEmpty()
            ? MediaUtils::extractImageUrlFromResponse(bytes, reply->url(), contentType)
            : redirectedImageUrl;
        Logger::instance().info(QStringLiteral("[ImageDebug] resolved imageUrl=%1 redirected=%2")
            .arg(imageUrl, redirectedImageUrl));
        if (imageUrl.trimmed().isEmpty()) {
            nam->deleteLater();
            setImageState(false, currentImageUrl_, QStringLiteral("没有从接口响应里解析到图片地址"));
            return;
        }

        if (contentType.startsWith(QStringLiteral("image/"), Qt::CaseInsensitive)) {
            nam->deleteLater();
            // 服务端 (api.yujn.cn 等) 偶发把 PHP Warning 拼在图片字节前且
            // Content-Type 仍为 image/*，先剥前缀；剥不出有效图片按失败处理。
            const QByteArray imageBytes = MediaUtils::sanitizedImageBytes(bytes);
            if (imageBytes.isEmpty()) {
                setImageState(false, currentImageUrl_, QStringLiteral("接口返回异常数据，未包含有效图片"));
                return;
            }
            currentImageBytes_ = imageBytes;
            const QString claimedMime = contentType.section(QLatin1Char(';'), 0, 0).trimmed();
            const QString effectiveMime = ImageCacheService::mimeFromExtension(
                ImageCacheService::extensionFromBytes(imageBytes, claimedMime));
            currentImageContentType_ = effectiveMime;
            const QString displayUrl = cacheImageBytes(apiSiteModel_->imageBaseUrl().trimmed(), reply->url().toString(), imageBytes, effectiveMime);
            emit imageCacheChanged();
            setImageState(false, imageUrl, QStringLiteral("图片已加载"), displayUrl);
            return;
        }

        QNetworkRequest imageRequest{QUrl(imageUrl)};
        imageRequest.setTransferTimeout(5000);
        imageRequest.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
        imageRequest.setRawHeader("Accept", "image/*,*/*");
        imageRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

        auto *imageReply = nam->get(imageRequest);
        connect(imageReply, &QNetworkReply::finished, this, [this, imageReply, nam, imageUrl]() {
            imageReply->deleteLater();
            nam->deleteLater();

            if (imageReply->error() != QNetworkReply::NoError) {
                Logger::instance().warn(QStringLiteral("[ImageDebug] image download error url=%1 error=%2")
                    .arg(imageUrl, imageReply->errorString()));
                setImageState(false, imageUrl, QStringLiteral("图片下载失败：%1").arg(imageReply->errorString()));
                return;
            }

            const QByteArray imageBytes = imageReply->readAll();
            const QString imageContentType = imageReply->header(QNetworkRequest::ContentTypeHeader).toString();
            Logger::instance().info(QStringLiteral("[ImageDebug] image reply url=%1 status=%2 contentType=%3 bytes=%4")
                .arg(imageReply->url().toString())
                .arg(imageReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                .arg(imageContentType)
                .arg(imageBytes.size()));
            const QString imageReplyRedirectedUrl = MediaUtils::redirectedUrlFromReply(imageReply);
            const QString finalImageUrl = !imageReplyRedirectedUrl.isEmpty()
                ? imageReplyRedirectedUrl
                : (imageReply->url().isValid() && !imageReply->url().isEmpty()
                    ? imageReply->url().toString()
                    : imageUrl);
            Logger::instance().info(QStringLiteral("[ImageDebug] finalImageUrl=%1 imageRedirect=%2")
                .arg(finalImageUrl, imageReplyRedirectedUrl));
            // 剥掉可能拼在图片字节前的 PHP Warning 等 HTML 前缀。
            const QByteArray sanitizedBytes = MediaUtils::sanitizedImageBytes(imageBytes);
            if (sanitizedBytes.isEmpty()) {
                setImageState(false, finalImageUrl, QStringLiteral("接口返回异常数据，未包含有效图片"));
                return;
            }
            currentImageBytes_ = sanitizedBytes;
            const QString claimedMime = imageContentType.section(QLatin1Char(';'), 0, 0).trimmed();
            const QString effectiveMime = ImageCacheService::mimeFromExtension(
                ImageCacheService::extensionFromBytes(sanitizedBytes, claimedMime));
            currentImageContentType_ = effectiveMime;
            const QString displayUrl = cacheImageBytes(apiSiteModel_->imageBaseUrl().trimmed(), finalImageUrl, sanitizedBytes, effectiveMime);
            emit imageCacheChanged();
            setImageState(false, finalImageUrl, QStringLiteral("图片已加载"), displayUrl);
        });
    });
}

void ImageController::saveCurrentImage() {
    const QString imageUrl = currentImageUrl_.trimmed();
    if (imageUrl.isEmpty()) {
        setImageState(false, currentImageUrl_, QStringLiteral("请先加载一张图片"));
        return;
    }

    const QUrl sourceUrl(imageUrl);
    if (!sourceUrl.isValid() || sourceUrl.scheme().isEmpty()) {
        setImageState(false, currentImageUrl_, QStringLiteral("当前图片地址无效"));
        return;
    }

    if (!currentImageBytes_.isEmpty()) {
        saveImageBytes(currentImageBytes_, sourceUrl, currentImageContentType_);
        return;
    }

    setImageState(true, currentImageUrl_, QStringLiteral("正在下载图片..."));
    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(sourceUrl);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "image/*,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, sourceUrl]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            setImageState(false, currentImageUrl_, QStringLiteral("图片下载失败：%1").arg(reply->errorString()));
            return;
        }

        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        saveImageBytes(reply->readAll(), sourceUrl, contentType);
    });
}

void ImageController::copyCurrentImage() {
    const QString imageUrl = currentImageUrl_.trimmed();
    if (imageUrl.isEmpty()) {
        setImageState(false, currentImageUrl_, QStringLiteral("请先加载一张图片"));
        return;
    }

    const auto copyBytesToClipboard = [this](const QByteArray &bytes) {
        const QImage image = MediaUtils::imageFromBytes(bytes);
        if (image.isNull()) {
            imageMessage_ = QStringLiteral("图片解码失败，无法复制");
            emit imageStateChanged();
            return false;
        }

        if (auto *clipboard = QGuiApplication::clipboard()) {
            clipboard->setImage(image);
            imageMessage_ = QStringLiteral("已复制图片");
        } else {
            imageMessage_ = QStringLiteral("系统剪贴板不可用");
        }
        emit imageStateChanged();
        return true;
    };

    if (!currentImageBytes_.isEmpty()) {
        copyBytesToClipboard(currentImageBytes_);
        return;
    }

    const QUrl sourceUrl(imageUrl);
    if (!sourceUrl.isValid() || sourceUrl.scheme().isEmpty()) {
        setImageState(false, currentImageUrl_, QStringLiteral("当前图片地址无效"));
        return;
    }

    imageMessage_ = QStringLiteral("正在复制图片...");
    emit imageStateChanged();
    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(sourceUrl);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "image/*,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, copyBytesToClipboard]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            imageMessage_ = QStringLiteral("图片复制失败：%1").arg(reply->errorString());
            emit imageStateChanged();
            return;
        }

        const QByteArray bytes = reply->readAll();
        currentImageBytes_ = bytes;
        currentImageContentType_ = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        copyBytesToClipboard(bytes);
    });
}

bool ImageController::loadImageBytesFromLocalUrl(const QString &url) {
    const QUrl qurl(url);
    if (!qurl.isLocalFile()) return false;
    QFile file(qurl.toLocalFile());
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty()) return false;
    currentImageBytes_ = bytes;
    currentImageContentType_ = ImageCacheService::mimeFromExtension(QFileInfo(file).suffix());
    return true;
}

QString ImageController::cacheImageBytes(const QString &apiUrl, const QString &sourceUrl,
                                          const QByteArray &bytes, const QString &mimeType) {
    return imageCache_.put(apiUrl, sourceUrl, bytes, mimeType);
}

void ImageController::saveImageBytes(const QByteArray &bytes, const QUrl &sourceUrl, const QString &contentType) {
    if (bytes.isEmpty()) {
        setImageState(false, currentImageUrl_, QStringLiteral("图片内容为空，无法保存"));
        return;
    }

    const QString folder = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("download/image"));
    QDir().mkpath(folder);
    const QString fileName = QStringLiteral("image-%1.%2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz")),
             MediaUtils::imageExtension(sourceUrl, contentType));
    const QString path = QDir(folder).filePath(fileName);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setImageState(false, currentImageUrl_, QStringLiteral("保存失败：%1").arg(file.errorString()));
        return;
    }
    file.write(bytes);
    file.close();
    currentImageBytes_ = bytes;
    currentImageContentType_ = contentType;
    setImageState(false, currentImageUrl_, QStringLiteral("已保存：%1").arg(path));
}
