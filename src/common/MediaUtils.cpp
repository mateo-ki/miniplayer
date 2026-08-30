#include "common/MediaUtils.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <functional>

#include "infrastructure/Logger.h"

namespace MediaUtils {

QImage imageFromBytes(const QByteArray &bytes) {
    QBuffer inputBuffer;
    inputBuffer.setData(bytes);
    inputBuffer.open(QIODevice::ReadOnly);
    QImageReader reader(&inputBuffer);
    reader.setAutoTransform(true);
    return reader.read();
}

QString redirectedUrlFromReply(QNetworkReply *reply) {
    if (!reply)
        return {};

    QUrl redirectUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (redirectUrl.isEmpty()) {
        const QVariant locationHeader = reply->header(QNetworkRequest::LocationHeader);
        if (locationHeader.isValid())
            redirectUrl = locationHeader.toUrl();
        if (redirectUrl.isEmpty() && locationHeader.canConvert<QString>())
            redirectUrl = QUrl(locationHeader.toString());
    }

    if (redirectUrl.isEmpty())
        return {};

    const QUrl resolvedUrl = reply->url().resolved(redirectUrl);
    if (!resolvedUrl.isValid() || resolvedUrl.isEmpty())
        return {};

    const QString url = resolvedUrl.toString();
    Logger::instance().info(QStringLiteral("[ImageDebug] redirect detected: %1 -> %2")
        .arg(reply->url().toString(), url));
    return url;
}

QString extractImageUrlFromResponse(const QByteArray &bytes, const QUrl &responseUrl,
                                    const QString &contentType) {
    if (contentType.startsWith(QStringLiteral("image/"), Qt::CaseInsensitive))
        return responseUrl.toString();

    const auto resolveUrl = [&responseUrl](const QString &raw) {
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty())
            return QString();
        const QUrl parsed(trimmed);
        if (parsed.isValid() && !parsed.isRelative())
            return trimmed;
        return responseUrl.resolved(parsed).toString();
    };

    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (doc.isObject()) {
        const QJsonObject root = doc.object();
        const QStringList topKeys = {QStringLiteral("url"), QStringLiteral("link"), QStringLiteral("image"), QStringLiteral("img"), QStringLiteral("imgurl")};
        for (const auto &key : topKeys) {
            const QString value = root.value(key).toString();
            if (!value.trimmed().isEmpty())
                return resolveUrl(value);
        }

        const QJsonValue data = root.value(QStringLiteral("data"));
        if (data.isString())
            return resolveUrl(data.toString());
        if (data.isObject()) {
            const QJsonObject object = data.toObject();
            for (const auto &key : topKeys) {
                const QString value = object.value(key).toString();
                if (!value.trimmed().isEmpty())
                    return resolveUrl(value);
            }
            const QJsonObject urls = object.value(QStringLiteral("urls")).toObject();
            const QString original = urls.value(QStringLiteral("original")).toString();
            if (!original.trimmed().isEmpty())
                return resolveUrl(original);
        }
        if (data.isArray() && !data.toArray().isEmpty()) {
            const QJsonValue first = data.toArray().first();
            if (first.isString())
                return resolveUrl(first.toString());
            if (first.isObject()) {
                const QJsonObject object = first.toObject();
                for (const auto &key : topKeys) {
                    const QString value = object.value(key).toString();
                    if (!value.trimmed().isEmpty())
                        return resolveUrl(value);
                }
                const QString original = object.value(QStringLiteral("urls")).toObject().value(QStringLiteral("original")).toString();
                if (!original.trimmed().isEmpty())
                    return resolveUrl(original);
            }
        }
    }

    const QString text = QString::fromUtf8(bytes).trimmed();
    static const QRegularExpression xmlUrl(QStringLiteral("<(?:url|link|image|img)>\\s*([^<]+)\\s*</(?:url|link|image|img)>"),
                                           QRegularExpression::CaseInsensitiveOption);
    const auto match = xmlUrl.match(text);
    if (match.hasMatch())
        return resolveUrl(match.captured(1));

    if (text.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
            || text.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
        return resolveUrl(text);

    return {};
}

QStringList extractImageUrlsFromResponse(const QByteArray &bytes, const QUrl &responseUrl,
                                         const QString &contentType) {
    QStringList urls;
    const auto appendUrl = [&urls, &responseUrl](const QString &raw) {
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty())
            return;
        QUrl parsed(trimmed);
        const QString resolved = (parsed.isValid() && !parsed.isRelative())
            ? trimmed
            : responseUrl.resolved(parsed).toString();
        if (!resolved.trimmed().isEmpty() && !urls.contains(resolved))
            urls.append(resolved);
    };

    if (contentType.startsWith(QStringLiteral("image/"), Qt::CaseInsensitive)) {
        appendUrl(responseUrl.toString());
        return urls;
    }

    const std::function<void(const QJsonValue &)> collectJson = [&](const QJsonValue &value) {
        if (value.isString()) {
            const QString text = value.toString();
            if (text.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
                    || text.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)
                    || text.contains(QRegularExpression(QStringLiteral("\\.(?:jpg|jpeg|png|gif|webp)(?:$|[?#])"),
                                                        QRegularExpression::CaseInsensitiveOption))) {
                appendUrl(text);
            }
            return;
        }
        if (value.isArray()) {
            for (const QJsonValue &item : value.toArray())
                collectJson(item);
            return;
        }
        if (value.isObject()) {
            const QJsonObject object = value.toObject();
            for (const QString &key : object.keys())
                collectJson(object.value(key));
        }
    };

    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (!doc.isNull()) {
        collectJson(doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object()));
        if (!urls.isEmpty())
            return urls;
    }

    const QString text = QString::fromUtf8(bytes);
    static const QRegularExpression imageUrlRe(
        QStringLiteral("https?://[^\\s\"'<>]+\\.(?:jpg|jpeg|png|gif|webp)(?:[^\\s\"'<>]*)?"),
        QRegularExpression::CaseInsensitiveOption);
    auto match = imageUrlRe.globalMatch(text);
    while (match.hasNext())
        appendUrl(match.next().captured(0));

    if (urls.isEmpty()) {
        const QString trimmed = text.trimmed();
        if (trimmed.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
                || trimmed.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
            appendUrl(trimmed);
        }
    }

    return urls;
}

QString imageExtension(const QUrl &url, const QString &contentType) {
    const QString type = contentType.toLower();
    if (type.contains(QStringLiteral("webp"))) return QStringLiteral("webp");
    if (type.contains(QStringLiteral("png"))) return QStringLiteral("png");
    if (type.contains(QStringLiteral("gif"))) return QStringLiteral("gif");
    if (type.contains(QStringLiteral("bmp"))) return QStringLiteral("bmp");
    if (type.contains(QStringLiteral("jpeg")) || type.contains(QStringLiteral("jpg"))) return QStringLiteral("jpg");

    const QString suffix = QFileInfo(url.path()).suffix().toLower();
    if (!suffix.isEmpty() && suffix.size() <= 5)
        return suffix;
    return QStringLiteral("jpg");
}

QString audioExtension(const QUrl &url, const QString &contentType) {
    const QString type = contentType.toLower();
    if (type.contains(QStringLiteral("mpeg")) || type.contains(QStringLiteral("mp3")))
        return QStringLiteral("mp3");
    if (type.contains(QStringLiteral("wav")))
        return QStringLiteral("wav");
    if (type.contains(QStringLiteral("ogg")))
        return QStringLiteral("ogg");
    if (type.contains(QStringLiteral("aac")))
        return QStringLiteral("aac");
    if (type.contains(QStringLiteral("flac")))
        return QStringLiteral("flac");
    if (type.contains(QStringLiteral("m4a")) || type.contains(QStringLiteral("mp4")))
        return QStringLiteral("m4a");

    const QString suffix = QFileInfo(url.path()).suffix().toLower();
    if (!suffix.isEmpty() && suffix.length() <= 5)
        return suffix;
    return QStringLiteral("mp3");
}

QString localFileUrl(const QString &path) {
    if (path.trimmed().isEmpty()) return {};
    return QUrl::fromLocalFile(QDir::cleanPath(path)).toString();
}

}  // namespace MediaUtils
