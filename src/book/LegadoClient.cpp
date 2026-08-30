#include "book/LegadoClient.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringDecoder>
#include <QTimer>
#include <QUrl>

#include "book/BookSource.h"
#include "book/RuleEngine.h"
#include "infrastructure/Logger.h"

namespace {

constexpr int kFetchTimeoutMs = 20000;

QMap<QString, QString> parseHeaderJson(const QString &headerJson) {
    QMap<QString, QString> headers;
    if (headerJson.trimmed().isEmpty())
        return headers;
    const QJsonDocument doc = QJsonDocument::fromJson(headerJson.toUtf8());
    if (!doc.isObject())
        return headers;
    const QJsonObject object = doc.object();
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.value().isString())
            headers.insert(it.key(), it.value().toString());
        else if (!it.value().isUndefined() && !it.value().isNull())
            headers.insert(it.key(), it.value().toVariant().toString());
    }
    return headers;
}

}  // namespace

LegadoClient::LegadoClient(QObject *parent)
    : QObject(parent), engine_(new RuleEngine(this, this)) {
}

LegadoClient::~LegadoClient() = default;

QNetworkAccessManager *LegadoClient::nam() {
    if (!nam_)
        nam_ = new QNetworkAccessManager(this);
    return nam_;
}

QString LegadoClient::decodeBody(const QByteArray &bytes, const QString &contentType,
                                 const QString &charset) {
    QString codecName = charset.trimmed();
    if (codecName.isEmpty() && contentType.contains(QLatin1String("charset="), Qt::CaseInsensitive)) {
        const int pos = contentType.indexOf(QLatin1String("charset="), 0, Qt::CaseInsensitive);
        codecName = contentType.mid(pos + 8).split(QLatin1Char(';')).value(0).trimmed();
        codecName.remove(QLatin1Char('"'));
    }
    if (!codecName.isEmpty() && codecName.compare(QLatin1String("utf-8"), Qt::CaseInsensitive) != 0
        && codecName.compare(QLatin1String("utf8"), Qt::CaseInsensitive) != 0) {
        QStringDecoder decoder(codecName.toUtf8());
        if (decoder.isValid()) {
            const QString decoded = decoder.decode(bytes);
            if (!decoder.hasError())
                return decoded;
            Logger::instance().warn(QStringLiteral("[书源] %1 解码存在错误字节").arg(codecName));
        } else {
            Logger::instance().warn(QStringLiteral("[书源] 不支持的编码 %1，回退 UTF-8")
                .arg(codecName));
        }
    }
    // 先严格校验 UTF-8，失败再退回系统本地编码（常见于 GBK 站点未声明 charset）。
    QStringDecoder utf8Strict(QStringDecoder::Utf8);
    const QString decoded = utf8Strict.decode(bytes);
    if (!utf8Strict.hasError())
        return decoded;
    QStringDecoder localDecoder(QStringDecoder::System);
    return localDecoder.decode(bytes);
}

QString LegadoClient::resolveUrlRule(const QString &urlRule, const QString &baseUrl,
                                    const QString &key, int page,
                                    LegadoFetchOptions *options) {
    QString raw = urlRule.trimmed();
    if (raw.isEmpty())
        return QString();

    RuleContext ctx;
    ctx.baseUrl = baseUrl;
    ctx.key = key;
    ctx.page = page;
    ctx.result = QString();
    if (engine_ && options)
        options->headers.insert(QStringLiteral("Referer"), baseUrl);

    // @js: 生成 URL（key/page/searchKey/searchPage 以 JS 变量注入，不做模板替换）
    if (raw.startsWith(QLatin1String("@js:"))) {
        const QString code = raw.mid(4);
        raw = engine_->evaluateString(QStringLiteral("@js:") + code, ctx,
                                      RuleNode::fromText(QString()));
        raw = raw.trimmed();
        if (raw.isEmpty())
            return QString();
    }

    // 尾部 ,{...} 请求选项
    const int optionPos = static_cast<int>(raw.lastIndexOf(QStringLiteral(",{\"")));
    if (optionPos > 0 && raw.trimmed().endsWith(QLatin1Char('}'))) {
        const QString optionJson = raw.mid(optionPos + 1);
        const QJsonDocument doc = QJsonDocument::fromJson(optionJson.toUtf8());
        if (doc.isObject() && options) {
            const QJsonObject object = doc.object();
            const QString method = object.value(QStringLiteral("method")).toString();
            if (!method.isEmpty())
                options->method = method.toUpper();
            options->body = object.value(QStringLiteral("body")).toString();
            options->charset = object.value(QStringLiteral("charset")).toString();
            options->type = object.value(QStringLiteral("type")).toString();
            const QJsonValue headers = object.value(QStringLiteral("headers"));
            if (headers.isObject()) {
                const QJsonObject headerObject = headers.toObject();
                for (auto it = headerObject.begin(); it != headerObject.end(); ++it)
                    options->headers.insert(it.key(), it.value().toString());
            }
        }
        raw = raw.left(optionPos).trimmed();
        if (raw.startsWith(QLatin1Char(',')))
            raw.remove(0, 1);
    }

    // {{key}}/{{page}}/{{expr}} 模板
    raw = engine_->evalTemplate(raw, ctx);
    if (!options)
        return raw;
    if (!options->body.isEmpty())
        options->body = engine_->evalTemplate(options->body, ctx);
    return raw;
}

LegadoFetchResult LegadoClient::fetchRule(const QString &urlRule, const QString &baseUrl,
                                          const QString &key, int page,
                                          const QMap<QString, QString> &extraHeaders) {
    LegadoFetchOptions options;
    QString url = resolveUrlRule(urlRule, baseUrl, key, page, &options);
    if (url.isEmpty()) {
        LegadoFetchResult result;
        result.error = QStringLiteral("URL 规则解析为空");
        return result;
    }
    // 书源常写相对路径，按书源地址补全
    const QUrl resolved(url);
    if (!resolved.host().isEmpty()) {
        // 绝对地址
    } else if (url.startsWith(QLatin1String("//"))) {
        url = QStringLiteral("https:") + url;
    } else {
        url = QUrl(baseUrl).resolved(QUrl(url)).toString();
    }
    for (auto it = extraHeaders.begin(); it != extraHeaders.end(); ++it)
        options.headers.insert(it.key(), it.value());
    return fetch(QUrl(url), options, nullptr);
}

LegadoFetchResult LegadoClient::fetch(const QUrl &url, const LegadoFetchOptions &options,
                                      const BookSource *source) {
    LegadoFetchResult result;
    if (!url.isValid() || url.host().isEmpty()) {
        result.error = QStringLiteral("无效的 URL: %1").arg(url.toString());
        return result;
    }

    QNetworkRequest request(url);
    request.setTransferTimeout(kFetchTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    // 请求头优先级：默认 UA < 源 header < 规则 header
    QMap<QString, QString> headers;
    headers.insert(QStringLiteral("User-Agent"),
        QStringLiteral("Mozilla/5.0 (Linux; Android 14) AppleWebKit/537.36 (KHTML, like Gecko) "
                       "Chrome/124.0.0.0 Mobile Safari/537.36"));
    if (source)
        headers.insert(parseHeaderJson(source->header));
    headers.insert(parseHeaderJson(sourceHeader_));
    headers.insert(options.headers);

    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        if (it.key().compare(QLatin1String("Content-Length"), Qt::CaseInsensitive) == 0)
            continue;
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    QNetworkReply *reply = nullptr;
    if (options.method.compare(QLatin1String("POST"), Qt::CaseInsensitive) == 0) {
        request.setHeader(QNetworkRequest::ContentTypeHeader,
            QStringLiteral("application/x-www-form-urlencoded"));
        QByteArray bodyBytes = options.body.toUtf8();
        if (!options.charset.isEmpty()
            && options.charset.compare(QLatin1String("utf-8"), Qt::CaseInsensitive) != 0) {
            QStringEncoder encoder(options.charset.toUtf8());
            if (encoder.isValid())
                bodyBytes = encoder.encode(options.body);
        }
        reply = nam()->post(request, bodyBytes);
    } else {
        reply = nam()->get(request);
    }

    QEventLoop loop;
    QTimer::singleShot(kFetchTimeoutMs + 2000, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        result.error = reply->errorString();
        Logger::instance().warn(QStringLiteral("[书源] 请求失败 %1: %2")
            .arg(url.toString(), result.error));
        return result;
    }

    result.ok = true;
    result.bytes = reply->readAll();
    result.contentType = QString::fromUtf8(reply->header(QNetworkRequest::ContentTypeHeader)
        .toByteArray());
    result.finalUrl = reply->url().toString();
    result.text = decodeBody(result.bytes, result.contentType, options.charset);
    return result;
}

QString LegadoClient::jsGet(const QString &url, const QMap<QString, QString> &headers) {
    LegadoFetchOptions options;
    options.headers = headers;
    const LegadoFetchResult result = fetch(QUrl(url), options, nullptr);
    return result.ok ? result.text : QString();
}

QString LegadoClient::jsPost(const QString &url, const QString &body,
                             const QMap<QString, QString> &headers) {
    LegadoFetchOptions options;
    options.method = QStringLiteral("POST");
    options.body = body;
    options.headers = headers;
    const LegadoFetchResult result = fetch(QUrl(url), options, nullptr);
    return result.ok ? result.text : QString();
}

void LegadoClient::setSourceHeader(const QString &headerJson) {
    sourceHeader_ = headerJson;
}
