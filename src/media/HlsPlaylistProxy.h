#pragma once

#include <QObject>
#include <QUrl>

class QNetworkAccessManager;
class QTcpServer;
class QTcpSocket;

class HlsPlaylistProxy final : public QObject {
    Q_OBJECT
public:
    explicit HlsPlaylistProxy(QObject *parent = nullptr);

    bool start();
    void stop();
    QUrl proxyUrl(const QUrl &source) const;

private:
    void acceptConnections();
    void handleRequest(QTcpSocket *socket, const QByteArray &requestData);
    void fetchPlaylist(QTcpSocket *socket, const QUrl &source, int attempt);
    QString rewritePlaylistUris(const QString &playlist, const QUrl &source) const;
    void sendResponse(QTcpSocket *socket, int statusCode, const QByteArray &body,
                      const QByteArray &contentType = "text/plain; charset=utf-8");
    void sendRedirect(QTcpSocket *socket, const QUrl &source);

    QTcpServer *server_ = nullptr;
    QNetworkAccessManager *network_ = nullptr;
};
