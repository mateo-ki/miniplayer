#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <functional>

#include "infrastructure/PoolApi.h"

class AuthController;

// 「我的」页数据：pool 后端的动漫收藏与观看历史，外加起播后的观看上报。
// 鉴权完全复用 AuthController 的网页会话 Cookie，不另存一份凭据。
class PoolMeController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool favoritesLoading READ favoritesLoading NOTIFY favoritesChanged)
    Q_PROPERTY(QVariantList favorites READ favorites NOTIFY favoritesChanged)
    Q_PROPERTY(QString favoritesMessage READ favoritesMessage NOTIFY favoritesChanged)
    Q_PROPERTY(bool historyLoading READ historyLoading NOTIFY historyChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(QString historyMessage READ historyMessage NOTIFY historyChanged)

public:
    explicit PoolMeController(AuthController *auth, QObject *parent = nullptr);

    bool favoritesLoading() const { return favoritesLoading_; }
    QVariantList favorites() const { return favorites_; }
    QString favoritesMessage() const { return favoritesMessage_; }
    bool historyLoading() const { return historyLoading_; }
    QVariantList history() const { return history_; }
    QString historyMessage() const { return historyMessage_; }

    Q_INVOKABLE void refreshFavorites();
    Q_INVOKABLE void refreshHistory();
    // 已收藏则取消，未收藏则加入；info 里存片名与海报，列表可直接渲染。
    // 键一律用字符串：上游既有旧整数 id 也有 18 位新 id，走整数会截断。
    Q_INVOKABLE void toggleFavorite(const QString &videoId, const QString &name, const QString &pic);
    Q_INVOKABLE bool isFavorite(const QString &videoId, const QString &name = QString()) const;
    Q_INVOKABLE void reportPlay(const QString &videoId, const QString &name, const QString &part);

signals:
    void favoritesChanged();
    void historyChanged();

private:
    using Callback = std::function<void(const PoolApi::Result &)>;

    void send(const QString &path, const QByteArray &body, const QByteArray &verb, Callback cb);
    void setFavoritesLoading(bool loading);
    void setHistoryLoading(bool loading);
    int favoriteIndex(const QString &videoId, const QString &name) const;

    AuthController *auth_ = nullptr;
    QNetworkAccessManager nam_;
    QVariantList favorites_;
    QVariantList history_;
    bool favoritesLoading_ = false;
    bool historyLoading_ = false;
    QString favoritesMessage_;
    QString historyMessage_;
};
