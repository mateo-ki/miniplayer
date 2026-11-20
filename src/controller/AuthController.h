#pragma once

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QObject>
#include <QString>

class AuthController final : public QObject {
    Q_OBJECT

    // 登录与连接状态
    Q_PROPERTY(bool isLoggedIn READ isLoggedIn NOTIFY loginStatusChanged)
    Q_PROPERTY(bool isLoggingIn READ isLoggingIn NOTIFY loggingInChanged)
    Q_PROPERTY(QString loginError READ loginError NOTIFY loginErrorChanged)
    Q_PROPERTY(QString serverUrl READ serverUrl WRITE setServerUrl NOTIFY serverUrlChanged)

    // 凭据记忆
    Q_PROPERTY(QString savedEmail READ savedEmail NOTIFY savedCredentialsChanged)
    Q_PROPERTY(QString savedPassword READ savedPassword NOTIFY savedCredentialsChanged)
    Q_PROPERTY(bool rememberPassword READ rememberPassword WRITE setRememberPassword NOTIFY rememberPasswordChanged)

    // 用户个人信息 (/api/auth/me)
    Q_PROPERTY(QString userId READ userId NOTIFY userInfoChanged)
    Q_PROPERTY(QString username READ username NOTIFY userInfoChanged)
    Q_PROPERTY(QString email READ email NOTIFY userInfoChanged)
    Q_PROPERTY(QString role READ role NOTIFY userInfoChanged)
    Q_PROPERTY(QString tier READ tier NOTIFY userInfoChanged)
    Q_PROPERTY(qint64 dailyLimit READ dailyLimit NOTIFY userInfoChanged)
    Q_PROPERTY(qint64 usedToday READ usedToday NOTIFY userInfoChanged)
    Q_PROPERTY(qint64 points READ points NOTIFY userInfoChanged)
    Q_PROPERTY(QString createdAt READ createdAt NOTIFY userInfoChanged)
    Q_PROPERTY(QString lastLoginAt READ lastLoginAt NOTIFY userInfoChanged)
    Q_PROPERTY(bool subscriptionEnabled READ subscriptionEnabled NOTIFY userInfoChanged)
    Q_PROPERTY(bool linuxdoBound READ linuxdoBound NOTIFY userInfoChanged)
    Q_PROPERTY(bool isRefreshingUserInfo READ isRefreshingUserInfo NOTIFY refreshingUserInfoChanged)

public:
    explicit AuthController(QObject *parent = nullptr);

    bool isLoggedIn() const { return m_isLoggedIn; }
    bool isLoggingIn() const { return m_isLoggingIn; }
    QString loginError() const { return m_loginError; }
    QString serverUrl() const { return m_serverUrl; }
    void setServerUrl(const QString &url);
    // 网页会话 Cookie 值 (vpn_session)。只读：收藏/历史/聊天都要带它去请求 pool。
    QString sessionToken() const { return m_sessionToken; }

    QString savedEmail() const { return m_savedEmail; }
    QString savedPassword() const { return m_savedPassword; }
    bool rememberPassword() const { return m_rememberPassword; }
    void setRememberPassword(bool remember);

    QString userId() const { return m_userId; }
    QString username() const { return m_username; }
    QString email() const { return m_email; }
    QString role() const { return m_role; }
    QString tier() const { return m_tier; }
    qint64 dailyLimit() const { return m_dailyLimit; }
    qint64 usedToday() const { return m_usedToday; }
    qint64 points() const { return m_points; }
    QString createdAt() const { return m_createdAt; }
    QString lastLoginAt() const { return m_lastLoginAt; }
    bool subscriptionEnabled() const { return m_subscriptionEnabled; }
    bool linuxdoBound() const { return m_linuxdoBound; }
    bool isRefreshingUserInfo() const { return m_isRefreshingUserInfo; }

    Q_INVOKABLE void login(const QString &email, const QString &password, bool remember = true);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void refreshUserInfo();

signals:
    void loginStatusChanged();
    void loggingInChanged();
    void loginErrorChanged();
    void serverUrlChanged();
    void savedCredentialsChanged();
    void rememberPasswordChanged();
    void userInfoChanged();
    void refreshingUserInfoChanged();
    void loginSuccess();
    void loginFailed(const QString &message);
    void loggedOut();

private:
    void loadSavedSettings();
    void tryAutoLogin();
    void saveCredentials(const QString &email, const QString &password);
    void clearSavedPassword();
    void extractSessionCookie(QNetworkReply *reply);

    QNetworkAccessManager m_nam;
    QString m_serverUrl;
    QString m_sessionToken;

    bool m_isLoggedIn = false;
    bool m_isLoggingIn = false;
    QString m_loginError;

    QString m_savedEmail;
    QString m_savedPassword;
    bool m_rememberPassword = true;

    // 用户个人数据
    QString m_userId;
    QString m_username;
    QString m_email;
    QString m_role;
    QString m_tier;
    qint64 m_dailyLimit = 0;
    qint64 m_usedToday = 0;
    qint64 m_points = 0;
    QString m_createdAt;
    QString m_lastLoginAt;
    bool m_subscriptionEnabled = false;
    bool m_linuxdoBound = false;
    bool m_isRefreshingUserInfo = false;
};
