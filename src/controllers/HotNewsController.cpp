#include "controllers/HotNewsController.h"

#include <QDateTime>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrlQuery>

#include "common/MediaUtils.h"
#include "infrastructure/Logger.h"

bool HotNewsController::hotNewsLoading() const { return hotNewsLoading_; }
QVariantList HotNewsController::hotNewsItems() const { return hotNewsItems_; }
QString HotNewsController::hotNewsMessage() const { return hotNewsMessage_; }

HotNewsController::HotNewsController(QObject *parent)
    : QObject(parent) {
}

void HotNewsController::loadHotNews(const QString &type) {
    static const QSet<QString> allowedTypes = {
        QStringLiteral("baidu"),
        QStringLiteral("tieba"),
        QStringLiteral("zhihu"),
        QStringLiteral("weibo"),
        QStringLiteral("douyin"),
        QStringLiteral("bilihot"),
        QStringLiteral("sspai"),
        QStringLiteral("biliall"),
        QStringLiteral("history")
    };
    const QString trimmedType = type.trimmed();
    const QString normalizedType = allowedTypes.contains(trimmedType) ? trimmedType : QStringLiteral("baidu");

    QUrl url(QStringLiteral("http://api.yujn.cn/api/hotlist.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("type"), normalizedType);
    query.addQueryItem(QStringLiteral("_t"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.setQuery(query);

    hotNewsLoading_ = true;
    hotNewsMessage_ = QStringLiteral("正在加载热讯...");
    emit hotNewsChanged();

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "application/json,text/plain,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, normalizedType]() {
        reply->deleteLater();
        nam->deleteLater();
        hotNewsLoading_ = false;

        if (reply->error() != QNetworkReply::NoError) {
            hotNewsMessage_ = QStringLiteral("热讯请求失败：%1").arg(reply->errorString());
            emit hotNewsChanged();
            return;
        }

        const QByteArray bytes = reply->readAll();
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
        QJsonArray rows;
        if (parseError.error == QJsonParseError::NoError) {
            if (doc.isArray()) {
                rows = doc.array();
            } else if (doc.isObject()) {
                const QJsonObject root = doc.object();
                const QStringList keys = {
                    QStringLiteral("data"),
                    QStringLiteral("list"),
                    QStringLiteral("result"),
                    QStringLiteral("news"),
                    QStringLiteral("items")
                };
                for (const QString &key : keys) {
                    const QJsonValue rootValue = root.value(key);
                    if (rootValue.isArray()) {
                        rows = rootValue.toArray();
                        break;
                    }
                    if (rootValue.isObject()) {
                        const QJsonObject nested = rootValue.toObject();
                        for (const QString &nestedKey : keys) {
                            if (nested.value(nestedKey).isArray()) {
                                rows = nested.value(nestedKey).toArray();
                                break;
                            }
                        }
                    }
                    if (!rows.isEmpty()) {
                        break;
                    }
                }
            }
        }

        hotNewsItems_.clear();
        int rank = 1;
        for (const QJsonValue &value : rows) {
            if (!value.isObject()) {
                continue;
            }
            const QJsonObject item = value.toObject();
            const QString title = item.value(QStringLiteral("title")).toString(
                item.value(QStringLiteral("name")).toString(
                    item.value(QStringLiteral("word")).toString(
                        item.value(QStringLiteral("query")).toString())));
            if (title.trimmed().isEmpty()) {
                continue;
            }

            QVariantMap map;
            map[QStringLiteral("rank")] = rank++;
            map[QStringLiteral("title")] = title.trimmed();
            map[QStringLiteral("url")] = item.value(QStringLiteral("url")).toString(
                item.value(QStringLiteral("link")).toString(
                    item.value(QStringLiteral("mobileUrl")).toString(
                        item.value(QStringLiteral("mobilUrl")).toString())));
            map[QStringLiteral("hot")] = item.value(QStringLiteral("hot")).toString(
                item.value(QStringLiteral("desc")).toString(
                    item.value(QStringLiteral("score")).toString(
                        item.value(QStringLiteral("num")).toString())));
            hotNewsItems_.append(map);
        }

        hotNewsMessage_ = hotNewsItems_.isEmpty()
            ? QStringLiteral("没有解析到热讯数据")
            : QStringLiteral("已加载 %1 条 %2 热讯").arg(hotNewsItems_.size()).arg(normalizedType);
        emit hotNewsChanged();
    });
}
