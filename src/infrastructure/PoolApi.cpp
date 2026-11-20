#include "infrastructure/PoolApi.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>

#include "infrastructure/Logger.h"

namespace PoolApi {

Result parseEnvelope(const QByteArray &body, int status, const QString &transportError) {
    Result parsed;
    if (!transportError.isEmpty()) {
        parsed.message = transportError;
        return parsed;
    }

    QJsonParseError jsonError;
    const auto doc = QJsonDocument::fromJson(body, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject()) {
        parsed.message = QStringLiteral("服务器响应异常 (HTTP %1)").arg(status);
        return parsed;
    }

    const QJsonObject root = doc.object();
    parsed.ok = root.value(QStringLiteral("ok")).toBool();
    parsed.message = root.value(QStringLiteral("message")).toString();
    parsed.data = root.value(QStringLiteral("data"));
    if (!parsed.ok && parsed.message.isEmpty())
        parsed.message = QStringLiteral("请求失败 (HTTP %1)").arg(status);
    return parsed;
}

QNetworkRequest buildRequest(const QString &baseUrl,
                             const QString &pathAndQuery,
                             const QString &sessionToken) {
    QNetworkRequest request{QUrl(baseUrl.trimmed() + pathAndQuery)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("User-Agent", "MeloBox/1.0");
    // 显式关掉 HTTP/2。实测（ModelTests::asksForNoHttp2BecauseStreamsGetReset）这份 Qt 默认
    // 就把到 pool 的请求协商成 h2，而他日志里那两条 "HTTP/2 protocol error"（22:36 群聊增量、
    // 22:43 私信增量）是 h2 流被中途 RST：没有状态码、也没有响应体，补发那条通道救不了它。
    // 这里一次只发一条请求，多路复用本来用不上，走 1.1 就没有这条路可坏。
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    if (!sessionToken.isEmpty()) {
        request.setRawHeader("Cookie", QByteArrayLiteral("vpn_session=") + sessionToken.toUtf8());
    }
    request.setTransferTimeout(15000);
    return request;
}

bool isTransientTransport(QNetworkReply::NetworkError error, int status) {
    // 拿到状态码就说明服务端答过了，补发会把同一条消息发两遍；只有"连接根本没建成"才补。
    if (status != 0)
        return false;
    switch (error) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::ProxyConnectionClosedError:
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::UnknownNetworkError:
        return true;
    default:
        // 超时(TimeoutError/OperationCanceledError)、加密层、代理找不到/代理超时这类配置问题不补。
        return false;
    }
}

void sendRequest(QObject *context, QNetworkAccessManager &nam, const QString &baseUrl,
                 const QString &pathAndQuery, const QByteArray &body, const QByteArray &verb,
                 const QString &sessionToken, const ResultCallback &cb, int attempt,
                 const QByteArray &contentType, int timeoutMs) {
    QNetworkRequest request = buildRequest(baseUrl, pathAndQuery, sessionToken);
    if (!contentType.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    // 发图要把几 MB 中继到图床，15 秒的默认值太紧；按调用方给的额度走。
    request.setTransferTimeout(timeoutMs);
    QNetworkReply *reply = nullptr;
    if (!body.isEmpty()) {
        reply = nam.post(request, body);
    } else if (verb == "DELETE") {
        reply = nam.sendCustomRequest(request, "DELETE");
    } else {
        reply = nam.get(request);
    }

    QObject::connect(reply, &QNetworkReply::finished, context,
                     [context, reply, cb, attempt, &nam, baseUrl, pathAndQuery, body, verb, sessionToken,
                          contentType, timeoutMs]() {
                         const int status =
                             reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                         const QNetworkReply::NetworkError error = reply->error();
                         if (attempt == 0 && isTransientTransport(error, status)) {
                             const QString reason = reply->errorString();
                             reply->deleteLater();
                             Logger::instance().warn(QStringLiteral("pool 连接失败，补发一次 (%1): %2")
                                 .arg(pathAndQuery, reason));
                             sendRequest(context, nam, baseUrl, pathAndQuery, body, verb,
                                         sessionToken, cb, attempt + 1, contentType, timeoutMs);
                             return;
                         }
                         const QString transportError =
                             error != QNetworkReply::NoError ? reply->errorString() : QString();
                         const Result result =
                             parseEnvelope(reply->readAll(), status, transportError);
                         reply->deleteLater();
                         cb(result);
                     });
}

} // namespace PoolApi
