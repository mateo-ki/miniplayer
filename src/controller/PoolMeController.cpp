#include "controller/PoolMeController.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "controller/AuthController.h"
#include "infrastructure/Logger.h"

namespace {

// 服务端把 video_id 存成 TEXT，正常给字符串；万一是数字，也得按整数打出来，
// 18 位编号经 double->toString 会变成 3.18e+17 这种没法回读的写法。
QString keyTextOf(const QVariant &value) {
    if (value.typeId() == QMetaType::Double || value.typeId() == QMetaType::LongLong)
        return QString::number(value.toLongLong());
    return value.toString().trimmed();
}

QString favoriteKeyOf(const QVariant &item) {
    const QVariantMap map = item.toMap();
    const QString key = keyTextOf(map.value(QStringLiteral("fav_key")));
    if (!key.isEmpty())
        return key;
    return keyTextOf(map.value(QStringLiteral("id")));
}

QByteArray jsonBody(const QJsonObject &object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace

PoolMeController::PoolMeController(AuthController *auth, QObject *parent)
    : QObject(parent), auth_(auth) {}

void PoolMeController::send(const QString &path, const QByteArray &body, const QByteArray &verb,
                            Callback cb) {
    const QString token = auth_ ? auth_->sessionToken() : QString();
    if (token.isEmpty()) {
        cb(PoolApi::Result{false, QJsonValue(), QStringLiteral("请先登录")});
        return;
    }
    // 出站与"连接类失败补发一次"都在 PoolApi::sendRequest 里，六类请求同一个通道。
    PoolApi::sendRequest(this, nam_, auth_->serverUrl(), path, body, verb, token, cb);
}

void PoolMeController::setFavoritesLoading(bool loading) {
    if (favoritesLoading_ == loading)
        return;
    favoritesLoading_ = loading;
    emit favoritesChanged();
}

void PoolMeController::setHistoryLoading(bool loading) {
    if (historyLoading_ == loading)
        return;
    historyLoading_ = loading;
    emit historyChanged();
}

int PoolMeController::favoriteIndex(const QString &videoId, const QString &name) const {
    const QString wanted = videoId.trimmed();
    for (int i = 0; i < favorites_.size(); ++i) {
        const QVariantMap map = favorites_.at(i).toMap();
        if (!wanted.isEmpty() && favoriteKeyOf(favorites_.at(i)) == wanted)
            return i;
        // 服务端会把数字 id 解析成雪花号存键，桌面端手里的 id 可能对不上，
        // 片名一致就认它是同一条。
        if (!name.isEmpty() && map.value(QStringLiteral("name")).toString() == name)
            return i;
    }
    return -1;
}

bool PoolMeController::isFavorite(const QString &videoId, const QString &name) const {
    return favoriteIndex(videoId, name) >= 0;
}

void PoolMeController::refreshFavorites() {
    if (favoritesLoading_)
        return;
    setFavoritesLoading(true);
    send(QStringLiteral("/api/anime/favorites"), {}, "GET", [this](const PoolApi::Result &result) {
        setFavoritesLoading(false);
        if (!result.ok) {
            favoritesMessage_ = result.message;
            emit favoritesChanged();
            return;
        }
        favorites_.clear();
        const QJsonArray items = result.data.toObject().value(QStringLiteral("items")).toArray();
        for (const auto &item : items)
            favorites_.append(item.toVariant());
        favoritesMessage_.clear();
        emit favoritesChanged();
    });
}

void PoolMeController::refreshHistory() {
    if (historyLoading_)
        return;
    setHistoryLoading(true);
    send(QStringLiteral("/api/anime/history"), {}, "GET", [this](const PoolApi::Result &result) {
        setHistoryLoading(false);
        if (!result.ok) {
            historyMessage_ = result.message;
            emit historyChanged();
            return;
        }
        history_.clear();
        const QJsonArray items = result.data.toObject().value(QStringLiteral("items")).toArray();
        for (const auto &item : items)
            history_.append(item.toVariant());
        historyMessage_.clear();
        emit historyChanged();
    });
}

void PoolMeController::toggleFavorite(const QString &rawId, const QString &name, const QString &pic) {
    const QString videoId = rawId.trimmed();
    if (videoId.isEmpty()) {
        favoritesMessage_ = QStringLiteral("这条内容没有可用的编号，无法收藏");
        emit favoritesChanged();
        return;
    }

    const int index = favoriteIndex(videoId, name);
    if (index >= 0) {
        const QString key = favoriteKeyOf(favorites_.at(index));
        send(QStringLiteral("/api/anime/favorites/%1").arg(QString::fromUtf8(QUrl::toPercentEncoding(key))),
             {}, "DELETE", [this](const PoolApi::Result &result) {
                 if (!result.ok) {
                     favoritesMessage_ = result.message;
                     emit favoritesChanged();
                 }
                 refreshFavorites();
             });
        return;
    }

    QJsonObject info;
    info.insert(QStringLiteral("name"), name);
    info.insert(QStringLiteral("pic"), pic);
    QJsonObject body;
    // id 一律按字符串下发：上游 18 位新 id 走 JSON 数字会丢精度。
    body.insert(QStringLiteral("video_id"), videoId);
    body.insert(QStringLiteral("info"), info);
    send(QStringLiteral("/api/anime/favorites"), jsonBody(body), "POST",
         [this](const PoolApi::Result &result) {
             if (!result.ok) {
                 favoritesMessage_ = result.message;
                 emit favoritesChanged();
                 return;
             }
             refreshFavorites();
         });
}

void PoolMeController::reportPlay(const QString &rawId, const QString &name, const QString &part) {
    const QString videoId = rawId.trimmed();
    if (videoId.isEmpty())
        return;
    QJsonObject body;
    body.insert(QStringLiteral("video_id"), videoId);
    body.insert(QStringLiteral("name"), name);
    body.insert(QStringLiteral("part"), part);
    send(QStringLiteral("/api/anime/play/report"), jsonBody(body), "POST",
         [videoId, name](const PoolApi::Result &result) {
             if (!result.ok) {
                 Logger::instance().warn(QStringLiteral("观看上报失败 (%1 %2): %3")
                     .arg(videoId).arg(name, result.message));
             }
         });
}
