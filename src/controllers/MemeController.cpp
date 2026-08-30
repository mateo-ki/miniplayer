#include "controllers/MemeController.h"

#include <QClipboard>
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

bool MemeController::memeLoading() const { return memeLoading_; }
QVariantList MemeController::memeImages() const { return memeImages_; }
QString MemeController::memeMessage() const { return memeMessage_; }

MemeController::MemeController(QObject *parent)
    : QObject(parent) {
}

void MemeController::searchMemes(const QString &keyword, int count) {
    const QString trimmedKeyword = keyword.trimmed().isEmpty() ? QStringLiteral("龙图") : keyword.trimmed();
    QUrl url(QStringLiteral("https://api.yujn.cn/api/bbq_ss.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("count"), QString::number(qBound(1, count, 30)));
    query.addQueryItem(QStringLiteral("msg"), trimmedKeyword);
    query.addQueryItem(QStringLiteral("_t"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.setQuery(query);

    memeLoading_ = true;
    memeMessage_ = QStringLiteral("正在搜索表情包...");
    emit memeStateChanged();

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "application/json,image/*,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, trimmedKeyword]() {
        reply->deleteLater();
        nam->deleteLater();
        memeLoading_ = false;

        if (reply->error() != QNetworkReply::NoError) {
            memeMessage_ = QStringLiteral("表情包请求失败：%1").arg(reply->errorString());
            emit memeStateChanged();
            return;
        }

        const QStringList urls = MediaUtils::extractImageUrlsFromResponse(reply->readAll(),
                                                              reply->url(),
                                                              reply->header(QNetworkRequest::ContentTypeHeader).toString());
        memeImages_.clear();
        for (const QString &url : urls) {
            if (!url.trimmed().isEmpty() && !memeImages_.contains(url))
                memeImages_.append(url);
        }
        memeMessage_ = memeImages_.isEmpty()
            ? QStringLiteral("没有解析到“%1”的表情包").arg(trimmedKeyword)
            : QStringLiteral("已加载 %1 张“%2”表情包").arg(memeImages_.size()).arg(trimmedKeyword);
        emit memeStateChanged();
    });
}

void MemeController::loadDragonMeme() {
    QUrl url(QStringLiteral("https://api.yujn.cn/api/long.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("image"));
    query.addQueryItem(QStringLiteral("_t"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.setQuery(query);

    memeImages_.clear();
    memeImages_.append(url.toString());
    memeMessage_ = QStringLiteral("已加载随机龙图");
    emit memeStateChanged();
}

void MemeController::saveMemeImage(const QString &imageUrl) {
    const QUrl sourceUrl(imageUrl.trimmed());
    if (!sourceUrl.isValid() || sourceUrl.scheme().isEmpty()) {
        memeMessage_ = QStringLiteral("表情包地址无效");
        emit memeStateChanged();
        return;
    }

    memeMessage_ = QStringLiteral("正在下载表情包...");
    emit memeStateChanged();

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
            memeMessage_ = QStringLiteral("表情包下载失败：%1").arg(reply->errorString());
            emit memeStateChanged();
            return;
        }

        const QByteArray bytes = reply->readAll();
        if (bytes.isEmpty()) {
            memeMessage_ = QStringLiteral("表情包内容为空，无法保存");
            emit memeStateChanged();
            return;
        }

        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        const QString folder = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("download/meme"));
        QDir().mkpath(folder);
        const QString fileName = QStringLiteral("meme-%1.%2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz")),
                 MediaUtils::imageExtension(sourceUrl, contentType));
        const QString path = QDir(folder).filePath(fileName);

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            memeMessage_ = QStringLiteral("保存失败：%1").arg(file.errorString());
            emit memeStateChanged();
            return;
        }
        file.write(bytes);
        file.close();
        memeMessage_ = QStringLiteral("已保存：%1").arg(path);
        emit memeStateChanged();
    });
}

void MemeController::copyMemeUrl(const QString &imageUrl) {
    const QUrl sourceUrl(imageUrl.trimmed());
    if (!sourceUrl.isValid() || sourceUrl.scheme().isEmpty()) {
        memeMessage_ = QStringLiteral("没有可复制的表情包地址");
        emit memeStateChanged();
        return;
    }

    memeMessage_ = QStringLiteral("正在复制表情包图片...");
    emit memeStateChanged();

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(sourceUrl);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "image/*,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            memeMessage_ = QStringLiteral("表情包复制失败：%1").arg(reply->errorString());
            emit memeStateChanged();
            return;
        }

        const QImage image = MediaUtils::imageFromBytes(reply->readAll());
        if (image.isNull()) {
            memeMessage_ = QStringLiteral("表情包解码失败，无法复制图片");
            emit memeStateChanged();
            return;
        }

        if (auto *clipboard = QGuiApplication::clipboard()) {
            clipboard->setImage(image);
            memeMessage_ = QStringLiteral("已复制表情包图片");
        } else {
            memeMessage_ = QStringLiteral("系统剪贴板不可用");
        }
        emit memeStateChanged();
    });
}
