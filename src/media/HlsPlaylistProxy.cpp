#include "media/HlsPlaylistProxy.h"

#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>

#include "infrastructure/Logger.h"
#include "media/HlsPlaylistFilter.h"

namespace {
bool isPlaylistUrl(const QUrl &url) {
    return url.path().endsWith(QStringLiteral(".m3u8"), Qt::CaseInsensitive);
}

constexpr int kFetchAttemptCount = 3;
}

HlsPlaylistProxy::HlsPlaylistProxy(QObject *parent)
    : QObject(parent), server_(new QTcpServer(this)), network_(new QNetworkAccessManager(this)) {
    network_->setProxy(QNetworkProxy::NoProxy);
    connect(server_, &QTcpServer::newConnection, this, &HlsPlaylistProxy::acceptConnections);
}

bool HlsPlaylistProxy::start() {
    if (server_->isListening())
        return true;
    if (!server_->listen(QHostAddress::LocalHost, 0)) {
        Logger::instance().warn(QStringLiteral("[HLS] proxy listen failed: %1")
            .arg(server_->errorString()));
        return false;
    }
    Logger::instance().info(QStringLiteral("[HLS] playlist proxy listening on 127.0.0.1:%1")
        .arg(server_->serverPort()));
    return true;
}

void HlsPlaylistProxy::stop() {
    server_->close();
}

QUrl HlsPlaylistProxy::proxyUrl(const QUrl &source) const {
    if (!server_->isListening() || !source.isValid())
        return {};
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(QStringLiteral("127.0.0.1"));
    url.setPort(server_->serverPort());
    url.setPath(QStringLiteral("/playlist.m3u8"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("url"), source.toString());
    url.setQuery(query);
    return url;
}

void HlsPlaylistProxy::acceptConnections() {
    while (server_->hasPendingConnections()) {
        QTcpSocket *socket = server_->nextPendingConnection();
        socket->setParent(this);
        auto *buffer = new QByteArray;
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            if (!buffer->contains("\r\n\r\n"))
                return;
            const QByteArray request = *buffer;
            buffer->clear();
            handleRequest(socket, request);
        });
        connect(socket, &QTcpSocket::disconnected, socket, [buffer]() { delete buffer; });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void HlsPlaylistProxy::handleRequest(QTcpSocket *socket, const QByteArray &requestData) {
    const QByteArray requestLine = requestData.left(requestData.indexOf("\r\n"));
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2 || parts[0] != "GET") {
        sendResponse(socket, 400, "Bad Request");
        return;
    }

    const QUrl requestUrl = QUrl::fromEncoded(parts[1]);
    const QUrl source(QUrlQuery(requestUrl).queryItemValue(QStringLiteral("url")));
    if (!source.isValid() || !source.scheme().startsWith(QStringLiteral("http"))) {
        sendResponse(socket, 400, "Invalid source URL");
        return;
    }

    fetchPlaylist(socket, source, 0);
}

void HlsPlaylistProxy::fetchPlaylist(QTcpSocket *socket, const QUrl &source, int attempt) {
    if (!socket || attempt < 0 || attempt >= kFetchAttemptCount)
        return;

    QNetworkRequest request(source);
    request.setRawHeader("Accept", "*/*");
    if (attempt == 0) {
        request.setRawHeader("User-Agent", "mpv/0.40.0");
    } else {
        request.setRawHeader("User-Agent",
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/148.0.0.0 Safari/537.36");
        request.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8");
        if (attempt == 2) {
            const QByteArray origin = source.adjusted(QUrl::RemovePath | QUrl::RemoveQuery
                | QUrl::RemoveFragment | QUrl::StripTrailingSlash).toString().toUtf8();
            request.setRawHeader("Origin", origin);
            request.setRawHeader("Referer", origin + '/');
        }
    }
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setTransferTimeout(12000);

    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(ssl);

    QNetworkReply *reply = network_->get(request);
    connect(reply, &QNetworkReply::sslErrors, reply,
            [reply](const QList<QSslError> &) { reply->ignoreSslErrors(); });
    const QPointer<QTcpSocket> guardedSocket(socket);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, guardedSocket, source, attempt]() {
        reply->deleteLater();
        if (!guardedSocket || guardedSocket->state() == QAbstractSocket::UnconnectedState)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            Logger::instance().warn(QStringLiteral("[HLS] proxy fetch attempt %1/%2 failed: status=%3 error=%4 finalUrl=%5")
                .arg(attempt + 1)
                .arg(kFetchAttemptCount)
                .arg(status)
                .arg(reply->errorString(), reply->url().toString()));
            if (attempt + 1 < kFetchAttemptCount) {
                fetchPlaylist(guardedSocket, source, attempt + 1);
                return;
            }
            Logger::instance().warn(QStringLiteral("[HLS] all proxy fetch attempts failed; playing unfiltered source url=%1")
                .arg(source.toString()));
            sendRedirect(guardedSocket, source);
            return;
        }

        const QUrl finalUrl = reply->url().isValid() ? reply->url() : source;
        const QString playlist = QString::fromUtf8(reply->readAll());
        if (!playlist.trimmed().startsWith(QStringLiteral("#EXTM3U"))) {
            sendResponse(guardedSocket, 502, "Upstream response is not an HLS playlist");
            return;
        }

        const auto filtered = HlsPlaylistFilter::filterOutOfSequenceAds(playlist, finalUrl);
        Logger::instance().info(QStringLiteral("[HLS] proxy filter complete: skipped=%1 attempt=%2 url=%3")
            .arg(filtered.skippedSegments)
            .arg(attempt + 1)
            .arg(finalUrl.toString()));
        const QByteArray body = rewritePlaylistUris(filtered.playlist, finalUrl).toUtf8();
        sendResponse(guardedSocket, 200, body, "application/vnd.apple.mpegurl; charset=utf-8");
    });
}

QString HlsPlaylistProxy::rewritePlaylistUris(const QString &playlist, const QUrl &source) const {
    static const QRegularExpression uriAttribute(QStringLiteral("URI=\\\"([^\\\"]+)\\\""));
    QStringList output;
    const QStringList lines = playlist.split(QRegularExpression(QStringLiteral("\\r?\\n")));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('#'))) {
            QString rewritten = line;
            int offset = 0;
            while (true) {
                const auto match = uriAttribute.match(rewritten, offset);
                if (!match.hasMatch())
                    break;
                const QUrl absolute = source.resolved(QUrl(match.captured(1)));
                const QString target = isPlaylistUrl(absolute)
                    ? proxyUrl(absolute).toString() : absolute.toString();
                rewritten.replace(match.capturedStart(1), match.capturedLength(1), target);
                offset = match.capturedStart(1) + target.size();
            }
            output.append(rewritten);
        } else if (trimmed.isEmpty()) {
            output.append(QString());
        } else {
            const QUrl absolute = source.resolved(QUrl(trimmed));
            output.append(isPlaylistUrl(absolute) ? proxyUrl(absolute).toString() : absolute.toString());
        }
    }
    return output.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

void HlsPlaylistProxy::sendResponse(QTcpSocket *socket, int statusCode, const QByteArray &body,
                                    const QByteArray &contentType) {
    if (!socket)
        return;
    const QByteArray reason = statusCode == 200 ? "OK"
        : statusCode == 400 ? "Bad Request" : "Bad Gateway";
    QByteArray response = "HTTP/1.1 " + QByteArray::number(statusCode) + ' ' + reason + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    socket->write(response);
    socket->disconnectFromHost();
}

void HlsPlaylistProxy::sendRedirect(QTcpSocket *socket, const QUrl &source) {
    if (!socket)
        return;
    QByteArray response = "HTTP/1.1 302 Found\r\n";
    response += "Location: " + source.toEncoded() + "\r\n";
    response += "Content-Length: 0\r\n";
    response += "Connection: close\r\n\r\n";
    socket->write(response);
    socket->disconnectFromHost();
}
