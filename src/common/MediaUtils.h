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

// 字节流是否以常见图片格式魔数开头 (JPEG/PNG/GIF/WEBP/BMP)。
bool hasImageMagic(const QByteArray &bytes);

// 服务端 (如 api.yujn.cn) 偶发在图片字节前拼出 PHP Warning HTML，Content-Type
// 仍谎称 image/*，导致整段数据无法解码。这里定位数据段中最早的图片魔数并剥掉
// 前缀；找不到任何图片数据时返回空，调用方应视为"接口返回了无效图片"。
QByteArray sanitizedImageBytes(const QByteArray &bytes);

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

// 用户可见文本打码: 骂人词与内容来源品牌/域名替换为 *** (pool 接入前的
// 客户端侧兜底; 品牌词用 PCRE2 unicode 转义书写, 源码不留明文)。
QString sanitizeUserText(const QString &text);


}  // namespace MediaUtils
