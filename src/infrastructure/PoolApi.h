#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <functional>

// pool 后端的统一信封: {"ok":bool,"message":str,"data":...}。
// 解析做成纯函数，控制器只负责发请求和存结果。
namespace PoolApi {

struct Result {
    bool ok = false;
    QJsonValue data;
    QString message;
};

using ResultCallback = std::function<void(const Result &)>;

// transportError 非空表示网络层就失败了 (超时/DNS/断连)。
Result parseEnvelope(const QByteArray &body, int status, const QString &transportError);

// GET/DELETE 用；POST 的 JSON 体由调用方自己准备好传 execute。
QNetworkRequest buildRequest(const QString &baseUrl,
                             const QString &pathAndQuery,
                             const QString &sessionToken);

/// 连接类失败：一个 HTTP 状态都没拿到，且错误在断连/DNS/拒绝这一族里。
/// 超时和 4xx/5xx 都不算 —— 服务端答过了，补发可能把同一条消息发两遍。
bool isTransientTransport(QNetworkReply::NetworkError error, int status);

/// 六类 pool 请求共用的出站口：带一次自动补发（只对 isTransientTransport 那种失败）。
/// context 决定回调的生命周期，请求挂在它销毁时一起断掉。
void sendRequest(QObject *context,
                 QNetworkAccessManager &nam,
                 const QString &baseUrl,
                 const QString &pathAndQuery,
                 const QByteArray &body,
                 const QByteArray &verb,
                 const QString &sessionToken,
                 const ResultCallback &cb,
                 int attempt = 0,
                 const QByteArray &contentType = QByteArray(),
                 int timeoutMs = 15000);

} // namespace PoolApi
