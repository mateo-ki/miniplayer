#pragma once

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class BookSource;
class RuleEngine;

// 对应 Legado 的 AnalyzeUrl：把书源里的 searchUrl/exploreUrl/tocUrl 等
// 规则字符串解析成真正的请求（含 @js:、{{key}}/{{page}} 模板、POST 选项）。
struct LegadoFetchOptions {
    QString method = QStringLiteral("GET");
    QString body;
    QString charset;
    QMap<QString, QString> headers;
    QString type;   // 期望的响应类型提示（可选）
};

struct LegadoFetchResult {
    bool ok = false;
    QByteArray bytes;
    QString text;
    QString contentType;
    QString finalUrl;
    QString error;
};

class LegadoClient final : public QObject {
    Q_OBJECT
public:
    explicit LegadoClient(QObject *parent = nullptr);
    ~LegadoClient() override;

    RuleEngine *engine() const { return engine_; }

    // 解析 URL 规则；返回最终请求地址，并把方法/请求体/头写入 options。
    QString resolveUrlRule(const QString &urlRule, const QString &baseUrl,
                           const QString &key, int page,
                           LegadoFetchOptions *options);

    // 解析并请求，返回解码后的文本。
    LegadoFetchResult fetchRule(const QString &urlRule, const QString &baseUrl,
                                const QString &key, int page,
                                const QMap<QString, QString> &extraHeaders = {});

    // 直接请求（URL 已就绪），供 JS 的 java.get/post 使用。
    LegadoFetchResult fetch(const QUrl &url, const LegadoFetchOptions &options,
                            const BookSource *source);

    // JS 绑定需要的同步请求：结果为文本，失败返回空串。
    QString jsGet(const QString &url, const QMap<QString, QString> &headers);
    QString jsPost(const QString &url, const QString &body, const QMap<QString, QString> &headers);

    void setSourceHeader(const QString &headerJson);
    QString sourceHeader() const { return sourceHeader_; }

private:
    static QString decodeBody(const QByteArray &bytes, const QString &contentType,
                              const QString &charset);
    QNetworkAccessManager *nam();
    QNetworkAccessManager *nam_ = nullptr;
    RuleEngine *engine_ = nullptr;
    QString sourceHeader_;
};
