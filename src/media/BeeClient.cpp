#include "BeeClient.h"

#include "infrastructure/Logger.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QNetworkRequest>
#include <QSslError>
#include <QSslSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>
#include <QRegularExpression>
#include <QTimer>

#include <openssl/evp.h>

namespace {
// The Bee gateway currently closes Qt's TLS connection during its
// renegotiation phase, while the same endpoint is stable over HTTP (verified
// against /vod/search and /vod/play). Keep the API transport on HTTP until
// the provider fixes that gateway behavior; media episode URLs remain as
// returned by the service.
constexpr const char *kBase = "http://java.beeweb.cc";
constexpr int kTimeoutMs = 20000;
const QByteArray kAesKey = QByteArrayLiteral("AD42F8897B035B751599513577538520");
const QByteArray kAesIv = QByteArrayLiteral("8866668815935700");
// ConstEncrypt.b(0x6c657ba42e84e5a2, -0x36fc0f0639ca0fb2) == 1000 — the Bee
// app divides currentTimeMillis() by this constant to obtain unix seconds,
// matching the jk/mu path in tu1.smali and ym2.smali's qt() signing helper.
constexpr qint64 kSecondDivisor = 1000;

QString field(const QJsonObject &o, const char *name) {
    const QJsonValue v = o.value(QString::fromLatin1(name));
    return v.isString() ? v.toString() : (v.isDouble() ? QString::number(v.toDouble()) : QString());
}

QString normalizeImageUrl(const QString &value) {
    QString imageUrl = value.trimmed();
    if (imageUrl.startsWith(QStringLiteral("//")))
        imageUrl.prepend(QStringLiteral("https:"));
    QUrl url(imageUrl);
    if (!url.isValid() || url.host().isEmpty())
        return imageUrl;
    QString path = url.path();
    while (path.startsWith(QStringLiteral("//")))
        path.remove(0, 1);
    url.setPath(path);
    return url.toString(QUrl::FullyEncoded);
}

QString imageDataUrl(const QByteArray &bytes, const QString &url) {
    if (bytes.startsWith(QByteArrayLiteral("\xFF\xD8\xFF")))
        return QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(bytes.toBase64());
    if (bytes.startsWith(QByteArrayLiteral("\x89PNG\r\n\x1A\n")))
        return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
    if (bytes.size() >= 12 && bytes.left(4) == QByteArrayLiteral("RIFF")
            && bytes.mid(8, 4) == QByteArrayLiteral("WEBP"))
        return QStringLiteral("data:image/webp;base64,") + QString::fromLatin1(bytes.toBase64());
    if (bytes.startsWith(QByteArrayLiteral("GIF8")))
        return QStringLiteral("data:image/gif;base64,") + QString::fromLatin1(bytes.toBase64());
    const QString lowerUrl = url.toLower();
    const QString mime = lowerUrl.endsWith(QStringLiteral(".png")) ? QStringLiteral("image/png")
        : lowerUrl.endsWith(QStringLiteral(".webp")) ? QStringLiteral("image/webp")
        : lowerUrl.endsWith(QStringLiteral(".gif")) ? QStringLiteral("image/gif")
        : QStringLiteral("image/jpeg");
    return QStringLiteral("data:%1;base64,%2").arg(mime, QString::fromLatin1(bytes.toBase64()));
}

QString normalizePlayUrl(const QString &value) {
    QStringList normalizedSources;
    const QStringList sources = value.split(QStringLiteral("$$$"), Qt::KeepEmptyParts);
    normalizedSources.reserve(sources.size());

    for (const QString &source : sources) {
        QStringList normalizedEpisodes;
        const QStringList records = source.split(
            QRegularExpression(QStringLiteral("[#\\r\\n]+")), Qt::SkipEmptyParts);
        normalizedEpisodes.reserve(records.size());

        for (const QString &record : records) {
            const QString trimmedRecord = record.trimmed();
            const qsizetype separator = trimmedRecord.indexOf(QLatin1Char('$'));
            if (separator <= 0)
                continue;

            const QString name = trimmedRecord.left(separator).trimmed();
            const QString stream = trimmedRecord.mid(separator + 1).trimmed();
            if (!name.isEmpty() && !stream.isEmpty())
                normalizedEpisodes.append(name + QLatin1Char('$') + stream);
        }

        normalizedSources.append(normalizedEpisodes.join(QLatin1Char('#')));
    }

    return normalizedSources.join(QStringLiteral("$$$"));
}

QByteArray decryptB64(const QString &encoded) {
    const QByteArray input = QByteArray::fromBase64(encoded.toUtf8());
    if (input.isEmpty() || input.size() % 16 != 0)
        return {};
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    QByteArray output(input.size() + 16, '\0');
    int outLen = 0;
    int finalLen = 0;
    const bool ok = ctx
        && EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
            reinterpret_cast<const unsigned char *>(kAesKey.constData()),
            reinterpret_cast<const unsigned char *>(kAesIv.constData())) == 1
        && EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(output.data()), &outLen,
            reinterpret_cast<const unsigned char *>(input.constData()), input.size()) == 1
        && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(output.data()) + outLen, &finalLen) == 1;
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    return ok ? output.left(outLen + finalLen) : QByteArray();
}

// AesNative.encrypt(plain) in the Bee app: AES-256-CBC(KEY,IV) of the UTF-8
// plaintext with PKCS#7 padding, then base64. Used to sign the jk (追剧日历)
// timestamp query parameter and the ym2 interceptor's form fields. Mirrors
// bee_aes.encrypt_pt in the RE reference scripts.
QString encryptPt(const QString &plain) {
    const QByteArray input = plain.toUtf8();
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    QByteArray output((input.size() / 16 + 2) * 16, '\0');
    int outLen = 0;
    int finalLen = 0;
    const bool ok = ctx
        && EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
            reinterpret_cast<const unsigned char *>(kAesKey.constData()),
            reinterpret_cast<const unsigned char *>(kAesIv.constData())) == 1
        && EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char *>(output.data()), &outLen,
            reinterpret_cast<const unsigned char *>(input.constData()), input.size()) == 1
        && EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(output.data()) + outLen, &finalLen) == 1;
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    return ok ? QString::fromLatin1(output.left(outLen + finalLen).toBase64()) : QString();
}

using BodyCallback = std::function<void(bool, const QByteArray &, const QString &)>;

void getBody(QNetworkAccessManager &nam, const QUrl &url, const QByteArray &accept,
             const BodyCallback &callback, int attempt = 0) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("okhttp/5.4.0"));
    request.setRawHeader("Accept", accept);
    request.setRawHeader("Connection", "close");
    // BeeWeb's gateway performs TLS renegotiation and is more reliable over
    // HTTP/1.1 than Qt's default HTTP/2 negotiation on Windows.
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setTransferTimeout(kTimeoutMs);
    Logger::instance().info(QStringLiteral("[Bee] GET %1 (attempt %2)")
                                .arg(url.toString(), QString::number(attempt + 1)));
    QNetworkReply *reply = nam.get(request);
    QObject::connect(reply, &QNetworkReply::sslErrors, reply,
                     [reply, url](const QList<QSslError> &errors) {
        if (!url.host().endsWith(QStringLiteral("beeweb.cc"), Qt::CaseInsensitive))
            return;
        Logger::instance().warn(QStringLiteral("[Bee] ignoring %1 TLS certificate errors for %2")
                                    .arg(QString::number(errors.size()), url.toString()));
        reply->ignoreSslErrors();
    });
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [reply, callback, &nam, url, accept, attempt]() {
        const auto networkError = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QString errorText = reply->errorString();
        const bool retryable = status == 502 || status == 503 || status == 504
            || networkError == QNetworkReply::RemoteHostClosedError
            || networkError == QNetworkReply::TimeoutError
            || networkError == QNetworkReply::TemporaryNetworkFailureError
            || networkError == QNetworkReply::NetworkSessionFailedError;
        Logger::instance().info(QStringLiteral("[Bee] response status=%1 bytes=%2 error=%3 url=%4")
                                    .arg(QString::number(status), QString::number(body.size()),
                                         errorText, url.toString()));
        reply->deleteLater();

        if (retryable && attempt < 2) {
            QNetworkAccessManager *manager = &nam;
            QTimer::singleShot(350 * (attempt + 1), manager,
                               [manager, url, accept, callback, attempt]() {
                getBody(*manager, url, accept, callback, attempt + 1);
            });
            return;
        }
        if (networkError != QNetworkReply::NoError) {
            // Some Bee endpoints (notably film-schedule) return a 4xx with a
            // valid JSON envelope — e.g. 403 + {"code":500,"film_schedule":[]}
            // when the timestamp skew/rate-limit trips. Deliver the body so the
            // caller can parse it as "no data" instead of surfacing a hard error.
            const QString message = status > 0
                ? QStringLiteral("Bee 请求失败 (HTTP %1): %2").arg(status).arg(errorText)
                : QStringLiteral("Bee 请求失败: %1").arg(errorText);
            callback(false, body, message);
            return;
        }
        callback(true, body, {});
    });
}

bool decodeEnvelope(const QByteArray &body, QJsonObject &result, QString &error) {
        QJsonParseError parseError;
        const QJsonDocument envelopeDoc = QJsonDocument::fromJson(body, &parseError);
        if (!envelopeDoc.isObject()) {
            error = QStringLiteral("Bee 响应 JSON 解析失败: %1").arg(parseError.errorString());
            return false;
        }
        const QJsonObject envelope = envelopeDoc.object();
        const QString encoded = envelope.value(QStringLiteral("data")).toString().isEmpty()
            ? envelope.value(QStringLiteral("list")).toString()
            : envelope.value(QStringLiteral("data")).toString();
        if (encoded.isEmpty()) {
            error = QStringLiteral("Bee 响应缺少密文数据");
            return false;
        }
        const QByteArray plain = decryptB64(encoded);
        const QJsonDocument plainDoc = QJsonDocument::fromJson(plain, &parseError);
        if (!plainDoc.isArray()) {
            error = QStringLiteral("Bee 数据解密失败: %1").arg(parseError.errorString());
            return false;
        }
        result = envelope;
        result.insert(QStringLiteral("decoded"), plainDoc.array());
        return true;
}

QVariantMap listItem(const QJsonObject &o) {
    QVariantMap item;
    const bool isSliceRecord = !field(o, "qt").isEmpty()
        || !field(o, "du").isEmpty()
        || !field(o, "qx").isEmpty()
        || !field(o, "mg").isEmpty();
    // SliceStyleBean uses qt as the real MacCMS vod_id. Never let a slice
    // identifier (silce_id/qx) become the detail request ID.
    QString vodId = isSliceRecord ? field(o, "qt") : field(o, "vod_id");
    if (vodId.isEmpty() || vodId == QStringLiteral("0"))
        vodId = field(o, "vodId");
    if (vodId.isEmpty() || vodId == QStringLiteral("0"))
        vodId = field(o, "silce_id");
    if (vodId.isEmpty() || vodId == QStringLiteral("0"))
        vodId = field(o, "id");
    QString vodName = isSliceRecord ? field(o, "du") : field(o, "vod_name");
    if (vodName.isEmpty()) vodName = field(o, "vod_name");
    if (vodName.isEmpty()) vodName = field(o, "video_name");
    QString vodPic = isSliceRecord ? field(o, "fw") : field(o, "vod_pic");
    if (vodPic.isEmpty()) vodPic = field(o, "vod_pic");
    if (vodPic.isEmpty()) vodPic = field(o, "image_url");
    const QString vodClass = field(o, "vod_class").isEmpty()
        ? field(o, "type_name") : field(o, "vod_class");
    item.insert(QStringLiteral("vodId"), vodId);
    item.insert(QStringLiteral("vodName"), vodName);
    item.insert(QStringLiteral("vodPic"), normalizeImageUrl(vodPic));
    item.insert(QStringLiteral("vodRemarks"), field(o, "vod_remarks"));
    item.insert(QStringLiteral("typeName"), field(o, "type_name").isEmpty()
                    ? vodClass : field(o, "type_name"));
    item.insert(QStringLiteral("vodYear"), field(o, "vod_year"));
    item.insert(QStringLiteral("vodArea"), field(o, "vod_area"));
    item.insert(QStringLiteral("vodClass"), vodClass);
    QString blurb = isSliceRecord ? field(o, "mp") : field(o, "vod_blurb");
    if (blurb.isEmpty()) blurb = field(o, "video_text");
    if (blurb.isEmpty()) blurb = field(o, "vod_blurb");
    item.insert(QStringLiteral("vodBlurb"), blurb);
    item.insert(QStringLiteral("vodVersion"), field(o, "vod_version"));
    return item;
}

}

BeeClient::BeeClient(QObject *parent)
    : nam_(parent) {
    // The desktop may have a system proxy enabled (for example v2rayN), but
    // BeeWeb's gateway rejects proxied/reused requests intermittently with
    // 503. Match the verified curl path and connect directly.
    nam_.setProxy(QNetworkProxy::NoProxy);
    // Prefer the OpenSSL backend when it is deployed. Schannel can report a
    // misleading "Connection closed" for BeeWeb's TLS renegotiation.
    if (QSslSocket::availableBackends().contains(QStringLiteral("openssl")))
        QSslSocket::setActiveBackend(QStringLiteral("openssl"));
}

void BeeClient::recommended(int page, int limit,
        const std::function<void(bool, const QVariantList &, const QString &)> &callback) {
    QUrl url(QStringLiteral("%1/tb-slice-detail-model/select").arg(kBase));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page"), QString::number(qMax(1, page)));
    query.addQueryItem(QStringLiteral("limit"), QString::number(qMax(1, limit)));
    url.setQuery(query);
    getBody(nam_, url, QByteArrayLiteral("application/json"),
            [callback](bool ok, const QByteArray &body, const QString &err) {
        if (!ok) { callback(false, {}, err); return; }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (!document.isArray()) {
            callback(false, {}, QStringLiteral("Bee 推荐列表解析失败: %1")
                                      .arg(parseError.errorString()));
            return;
        }
        QVariantList out;
        for (const QJsonValue &value : document.array())
            out.append(listItem(value.toObject()));
        callback(true, out, {});
    });
}

void BeeClient::searchRank(int page,
        const std::function<void(bool, const QVariantList &, int totalPages, const QString &)> &callback) {
    // 近期热播 (mb): /tb-search-rank-model/select?page=N. Plaintext JSON envelope
    // {data:[...], total_count, total_pages, current_page}; each card is a
    // standard MacCMS-style vod record (vod_id/vod_name/vod_pic/...). 10/page.
    const int requestedPage = qMax(1, page);
    QUrl url(QStringLiteral("%1/tb-search-rank-model/select").arg(kBase));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page"), QString::number(requestedPage));
    url.setQuery(query);
    getBody(nam_, url, QByteArrayLiteral("application/json"),
            [callback, requestedPage](bool ok, const QByteArray &body, const QString &err) {
        if (!ok) { callback(false, {}, 0, err); return; }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (!document.isObject()) {
            callback(false, {}, 0, QStringLiteral("Bee 近期热播解析失败: %1")
                                        .arg(parseError.errorString()));
            return;
        }
        const QJsonObject envelope = document.object();
        const QJsonArray data = envelope.value(QStringLiteral("data")).toArray();
        QVariantList out;
        for (const QJsonValue &value : data)
            out.append(listItem(value.toObject()));
        const int totalPages = envelope.value(QStringLiteral("total_pages")).toInt(
            envelope.value(QStringLiteral("totalPages")).toInt());
        const bool hasNext = requestedPage < totalPages && !data.isEmpty();
        callback(true, out, hasNext ? totalPages : requestedPage, {});
    });
}

void BeeClient::filmSchedule(const QString &tag,
        const std::function<void(bool, const QVariantList &, const QString &)> &callback) {
    // 追剧日历 (jk): /film-schedule-model/select?timestamp=<AesNative.encrypt(
    // unix秒)>&tag=<Uri.encode(tag)>. Reverse-engineered from tu1.mu in the Bee
    // app (seg1='?timestamp=' / seg2='&tag=' / method='GET'). The server returns
    // a plaintext {film_schedule:[{date,weekday,items:[...]}]} envelope; an empty
    // array means no schedule for the given tag (or today is genuinely empty).
    // Note: the gateway intermittently answers 403 + {"code":500,...} when the
    // timestamp is seconds-stale or rate-limited; treat any reachable JSON
    // envelope (even on HTTP error) as success-with-data rather than a hard
    // failure, so the UI shows "no schedule today" instead of an error banner.
    const QString timestamp = encryptPt(QString::number(
        QDateTime::currentDateTimeUtc().toMSecsSinceEpoch() / kSecondDivisor));
    QUrl url(QStringLiteral("%1/film-schedule-model/select").arg(kBase));
    // The base64 timestamp carries '+', '/' and '=' which QUrlQuery leaves bare
    // in the query (Qt treats them as safe sub-delimiters). The Bee gateway
    // form-decodes a bare '+' as a space, corrupting the base64 and returning
    // 403. The Bee app's OkHttp stack percent-encodes these chars, so build the
    // query with QUrl::toPercentEncoding and hand it to setQuery verbatim —
    // setQuery takes an already-encoded string (preserving %2B/%2F/%3D on the
    // wire), unlike QUrlQuery::addQueryItem which would double-encode a
    // pre-encoded value and still emit a bare '+'.
    QByteArray query;
    if (!timestamp.isEmpty()) {
        query.append("timestamp=");
        query.append(QUrl::toPercentEncoding(timestamp));
    }
    const QString trimmedTag = tag.trimmed();
    if (!trimmedTag.isEmpty()) {
        if (!query.isEmpty())
            query.append('&');
        query.append("tag=");
        query.append(QUrl::toPercentEncoding(trimmedTag));
    }
    url.setQuery(QString::fromLatin1(query));
    getBody(nam_, url, QByteArrayLiteral("application/json"),
            [callback](bool ok, const QByteArray &body, const QString &err) {
        if (!ok && body.isEmpty()) { callback(false, {}, err); return; }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (!document.isObject()) {
            callback(false, {}, QStringLiteral("Bee 追剧日历解析失败: %1")
                                        .arg(parseError.errorString()));
            return;
        }
        const QJsonArray schedule = document.object()
            .value(QStringLiteral("film_schedule")).toArray();
        QVariantList out;
        for (const QJsonValue &dayValue : schedule) {
            const QJsonObject day = dayValue.toObject();
            QVariantMap dayMap;
            dayMap.insert(QStringLiteral("date"), day.value(QStringLiteral("date")).toString());
            dayMap.insert(QStringLiteral("weekday"), day.value(QStringLiteral("weekday")).toInt());
            QVariantList items;
            for (const QJsonValue &itemValue : day.value(QStringLiteral("items")).toArray()) {
                const QJsonObject item = itemValue.toObject();
                QVariantMap itemMap;
                itemMap.insert(QStringLiteral("vodId"),
                    QString::number(item.value(QStringLiteral("vod_id")).toInt()));
                itemMap.insert(QStringLiteral("title"),
                    item.value(QStringLiteral("title")).toString());
                itemMap.insert(QStringLiteral("cover"),
                    normalizeImageUrl(item.value(QStringLiteral("cover")).toString()));
                itemMap.insert(QStringLiteral("episodeStatus"),
                    item.value(QStringLiteral("episode_status")).toString());
                itemMap.insert(QStringLiteral("deltaEpisode"),
                    item.value(QStringLiteral("delta_episode")).toString());
                items.append(itemMap);
            }
            dayMap.insert(QStringLiteral("items"), items);
            out.append(dayMap);
        }
        callback(true, out, {});
    });
}

void BeeClient::search(const QString &keyword,
        const std::function<void(bool, const QVariantList &, const QString &)> &callback) {
    QUrl url(QStringLiteral("%1/vod/search").arg(kBase));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("wd"), keyword.trimmed());
    url.setQuery(query);
    getBody(nam_, url, QByteArrayLiteral("application/json"),
            [callback](bool ok, const QByteArray &body, const QString &err) {
        if (!ok) { callback(false, {}, err); return; }
        QJsonObject root;
        QString decodeError;
        if (!decodeEnvelope(body, root, decodeError)) {
            callback(false, {}, decodeError);
            return;
        }
        QVariantList out;
        for (const QJsonValue &value : root.value(QStringLiteral("decoded")).toArray())
            out.append(listItem(value.toObject()));
        callback(true, out, {});
    });
}

void BeeClient::detail(const QString &vodId,
        const std::function<void(bool, const QVariantMap &, const QString &)> &callback) {
    QUrl url(QStringLiteral("%1/vod/play").arg(kBase));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("id"), vodId);
    url.setQuery(query);
    getBody(nam_, url, QByteArrayLiteral("application/json"),
            [callback, vodId](bool ok, const QByteArray &body, const QString &err) {
        if (!ok) { callback(false, {}, err); return; }
        QJsonObject root;
        QString decodeError;
        if (!decodeEnvelope(body, root, decodeError)) {
            callback(false, {}, decodeError);
            return;
        }
        const QJsonArray values = root.value(QStringLiteral("decoded")).toArray();
        if (values.isEmpty()) { callback(false, {}, QStringLiteral("Bee 详情为空")); return; }
        const QJsonObject o = values.first().toObject();
        QVariantMap detail = listItem(o);
        const QString returnedVodId = detail.value(QStringLiteral("vodId")).toString();
        if (!returnedVodId.isEmpty() && returnedVodId != vodId) {
            callback(false, {}, QStringLiteral("Bee 详情 ID 不匹配: 请求 %1, 返回 %2")
                                      .arg(vodId, returnedVodId));
            return;
        }
        detail.insert(QStringLiteral("vodRemarks"), field(o, "vod_remarks"));
        detail.insert(QStringLiteral("vodYear"), field(o, "vod_year"));
        detail.insert(QStringLiteral("vodArea"), field(o, "vod_area"));
        detail.insert(QStringLiteral("vodClass"), field(o, "vod_class").isEmpty()
                          ? (field(o, "vod_type").isEmpty() ? field(o, "type_name")
                                                             : field(o, "vod_type"))
                          : field(o, "vod_class"));
        detail.insert(QStringLiteral("vodActor"), field(o, "vod_actor"));
        detail.insert(QStringLiteral("vodDirector"), field(o, "vod_director"));
        detail.insert(QStringLiteral("vodBlurb"), field(o, "vod_blurb"));
        detail.insert(QStringLiteral("vodContent"), field(o, "vod_content"));
        detail.insert(QStringLiteral("vodScore"), field(o, "vod_score"));
        detail.insert(QStringLiteral("vodPlayFrom"), field(o, "vod_play_from"));
        const QString playUrl = normalizePlayUrl(field(o, "vod_play_url"));
        detail.insert(QStringLiteral("vodPlayUrl"), playUrl);
        QVariantList episodes;
        const QString firstSource = playUrl.section(QStringLiteral("$$$"), 0, 0);
        for (const QString &line : firstSource.split(QLatin1Char('#'), Qt::SkipEmptyParts)) {
            const int separator = line.indexOf(QLatin1Char('$'));
            if (separator <= 0) continue;
            const QString name = line.left(separator).trimmed();
            const QString stream = line.mid(separator + 1).trimmed();
            if (name.isEmpty() || stream.isEmpty()) continue;
            QVariantMap episode;
            episode.insert(QStringLiteral("name"), name);
            episode.insert(QStringLiteral("url"), stream);
            episodes.append(episode);
        }
        detail.insert(QStringLiteral("episodes"), episodes);
        callback(true, detail, {});
    });
}

void BeeClient::fetchImage(
        const QString &url,
        const std::function<void(bool, const QString &, const QString &)> &callback) {
    const QString normalizedUrl = normalizeImageUrl(url);
    if (normalizedUrl.isEmpty()) {
        callback(false, {}, QStringLiteral("Bee 图片地址为空"));
        return;
    }
    getBody(nam_, QUrl(normalizedUrl), QByteArrayLiteral("image/*"),
            [callback, normalizedUrl](bool ok, const QByteArray &body, const QString &error) {
        if (!ok) {
            callback(false, {}, error);
            return;
        }
        if (body.isEmpty()) {
            callback(false, {}, QStringLiteral("Bee 图片响应为空"));
            return;
        }
        callback(true, imageDataUrl(body, normalizedUrl), {});
    });
}
