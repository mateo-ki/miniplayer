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

namespace {
bool startsWithImageMagic(const QByteArray &bytes) {
    return bytes.startsWith("\xff\xd8\xff")            // JPEG
        || bytes.startsWith("\x89PNG")                 // PNG
        || bytes.startsWith("GIF8")                    // GIF
        || (bytes.size() >= 12 && bytes.startsWith("RIFF")
            && bytes.mid(8, 4) == "WEBP")              // WEBP
        || bytes.startsWith("BM");                     // BMP
}
}  // namespace

bool hasImageMagic(const QByteArray &bytes) {
    return startsWithImageMagic(bytes);
}

QImage imageFromBytes(const QByteArray &bytes) {
    QBuffer inputBuffer;
    inputBuffer.setData(bytes);
    inputBuffer.open(QIODevice::ReadOnly);
    QImageReader reader(&inputBuffer);
    reader.setAutoTransform(true);
    return reader.read();
}

QByteArray sanitizedImageBytes(const QByteArray &bytes) {
    if (startsWithImageMagic(bytes))
        return bytes;

    // 中流搜索只信高强度魔数: JPEG/PNG/GIF 与 RIFF+WEBP 组合。
    // BMP 的 "BM" 只有 2 字节且可能出现在文本里, 不做中流匹配。
    qsizetype best = -1;
    const auto consider = [&best](qsizetype pos) {
        if (pos >= 0 && (best < 0 || pos < best))
            best = pos;
    };
    consider(bytes.indexOf("\xff\xd8\xff"));
    consider(bytes.indexOf("\x89PNG"));
    consider(bytes.indexOf("GIF8"));
    qsizetype riff = bytes.indexOf("RIFF");
    while (riff >= 0) {
        if (bytes.size() >= riff + 12 && bytes.mid(riff + 8, 4) == "WEBP") {
            consider(riff);
            break;
        }
        riff = bytes.indexOf("RIFF", riff + 1);
    }
    if (best < 0)
        return {};
    return bytes.mid(int(best));
}

QString sanitizeUserText(const QString &text) {
    if (text.isEmpty())
        return text;
    QString out = text;

    // 来源品牌与域名 -> *** (展示层屏蔽内容来源)。
    // 品牌词用 PCRE2 unicode 转义书写, 避免被批量字面替换破坏正则。
    static const QRegularExpression brandPat(
        QStringLiteral("\\x{52a8}\\x{6f2b}\\x{5171}\\x{548c}\\x{56fd}|(?:www\\.)?oneghg(?:\\.com)?|(?:www\\.)?dongman\\.to"));
    out.replace(brandPat, QStringLiteral("***"));

    // 骂人词 -> *** (保守词表, 只列明确粗俗项, 大小写不敏感)
    static const QStringList profanity = {
        QStringLiteral("傻逼"), QStringLiteral("煞笔"), QStringLiteral("草泥马"),
        QStringLiteral("操你"), QStringLiteral("日你"), QStringLiteral("他妈的"),
        QStringLiteral("你妈的"), QStringLiteral("妈的"), QStringLiteral("鸡巴"),
        QStringLiteral("狗屁"), QStringLiteral("贱人"), QStringLiteral("尼玛"),
        QStringLiteral("我操"), QStringLiteral("nmsl"), QStringLiteral("fuck"),
        QStringLiteral("shit"), QStringLiteral("bitch"), QStringLiteral("asshole"),
    };
    for (const QString &word : profanity)
        out.replace(word, QStringLiteral("***"), Qt::CaseInsensitive);
    return out;
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
