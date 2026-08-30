#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QUrl>

class QNetworkReply;

// PlayerController 与各功能控制器共用的网络/媒体小工具。
namespace MediaUtils {

QImage imageFromBytes(const QByteArray &bytes);

// 取 QNetworkReply 的重定向目标地址（合并到最终 URL），无重定向返回空。
QString redirectedUrlFromReply(QNetworkReply *reply);

// 从接口响应（JSON / XML / 纯文本）中解析图片直链。
QString extractImageUrlFromResponse(const QByteArray &bytes, const QUrl &responseUrl,
                                    const QString &contentType);
QStringList extractImageUrlsFromResponse(const QByteArray &bytes, const QUrl &responseUrl,
                                         const QString &contentType);

QString imageExtension(const QUrl &url, const QString &contentType);
QString audioExtension(const QUrl &url, const QString &contentType);

QString localFileUrl(const QString &path);

}  // namespace MediaUtils
