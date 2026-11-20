#include "controller/AuthController.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QSslConfiguration>
#include <QSslError>
#include <QTimer>
#include <QUrl>

#include "infrastructure/Logger.h"

namespace {
const QString kDefaultServerUrl = QStringLiteral("https://pool.983698.xyz");
const QString kDefaultEmail;
const QString kDefaultPassword;
}

AuthController::AuthController(QObject *parent)
    : QObject(parent),
      m_serverUrl(kDefaultServerUrl),
      m_savedEmail(kDefaultEmail),
      m_savedPassword(kDefaultPassword) {
    m_nam.setCookieJar(new QNetworkCookieJar(this));
    loadSavedSettings();
    // 在事件循环启动后（QML 完成绑定）再执行自动登录，避免信号在构造期丢失
    QTimer::singleShot(0, this, &AuthController::tryAutoLogin);
}

void AuthController::setServerUrl(const QString &url) {
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty() || m_serverUrl == trimmed) {
        return;
    }
    m_serverUrl = trimmed;
    QSettings s(QStringLiteral("MeloBox"), QStringLiteral("MeloBox"));
    s.setValue(QStringLiteral("auth/serverUrl"), m_serverUrl);
    emit serverUrlChanged();
}

void AuthController::setRememberPassword(bool remember) {
    if (m_rememberPassword == remember) {
        return;
    }
    m_rememberPassword = remember;
    QSettings s(QStringLiteral("MeloBox"), QStringLiteral("MeloBox"));
    s.setValue(QStringLiteral("auth/rememberPassword"), m_rememberPassword);
    if (!m_rememberPassword) {
        clearSavedPassword();
    }
    emit rememberPasswordChanged();
}

void AuthController::loadSavedSettings() {
    QSettings s(QStringLiteral("MeloBox"), QStringLiteral("MeloBox"));
    m_serverUrl = s.value(QStringLiteral("auth/serverUrl"), kDefaultServerUrl).toString().trimmed();
    if (m_serverUrl.isEmpty()) {
        m_serverUrl = kDefaultServerUrl;
    }

    m_rememberPassword = s.value(QStringLiteral("auth/rememberPassword"), true).toBool();
    m_savedEmail = s.value(QStringLiteral("auth/email"), kDefaultEmail).toString().trimmed();
    if (m_rememberPassword) {
        m_savedPassword = s.value(QStringLiteral("auth/password"), kDefaultPassword).toString();
    } else {
        m_savedPassword.clear();
    }
}

void AuthController::tryAutoLogin() {
    // 会话 token 本身持久化：只要服务端那 7 天没过期，重启就直接回到已登录态，
    // 不必再走一次密码登录。校验由 refreshUserInfo() 负责，401 会自行降级。
    QSettings tokens(QStringLiteral("MeloBox"), QStringLiteral("MeloBox"));
    const QString storedToken = tokens.value(QStringLiteral("auth/sessionToken")).toString().trimmed();
    if (!storedToken.isEmpty()) {
        m_sessionToken = storedToken;
        m_isLoggedIn = true;
        emit loginStatusChanged();
        Logger::instance().info(QStringLiteral("Auto-login: 复用已保存的会话 token，正在校验"));
        refreshUserInfo();
        return;
    }
    if (!m_rememberPassword) {
        Logger::instance().info(QStringLiteral("Auto-login skipped: rememberPassword is false"));
        return;
    }
    if (m_savedEmail.isEmpty() || m_savedPassword.isEmpty()) {
        Logger::instance().info(QStringLiteral("Auto-login skipped: no saved credentials"));
        return;
    }
    Logger::instance().info(QStringLiteral("Auto-login: attempting with saved credentials for %1").arg(m_savedEmail));
    login(m_savedEmail, m_savedPassword, true);
}

void AuthController::saveCredentials(const QString &email, const QString &password) {
    m_savedEmail = email.trimmed();
    m_savedPassword = password;
    QSettings s(QStringLiteral("MeloBox"), QStringLiteral("MeloBox"));
    s.setValue(QStringLiteral("auth/email"), m_savedEmail);
    if (m_rememberPassword) {
        s.setValue(QStringLiteral("auth/password"), m_savedPassword);
    } else {
        s.remove(QStringLiteral("auth/password"));
    }
    emit savedCredentialsChanged();
}

void AuthController::clearSavedPassword() {
    m_savedPassword.clear();
    QSettings s(QStringLiteral("MeloBox"), QStringLiteral("MeloBox"));
    s.remove(QStringLiteral("auth/password"));
    emit savedCredentialsChanged();
}

void AuthController::extractSessionCookie(QNetworkReply *reply) {
    if (!reply) return;

    // 1. 从 Set-Cookie 头提取
    const QVariant cookieVar = reply->header(QNetworkRequest::SetCookieHeader);
    if (cookieVar.isValid()) {
        const QList<QNetworkCookie> cookies = qvariant_cast<QList<QNetworkCookie>>(cookieVar);
        for (const auto &c : cookies) {
            if (c.name() == "vpn_session") {
                m_sessionToken = QString::fromUtf8(c.value()).trimmed();
                Logger::instance().info(QStringLiteral("Found vpn_session cookie from SetCookieHeader"));
                return;
            }
        }
    }

    // 2. 从原始响应头提取
    for (const auto &pair : reply->rawHeaderPairs()) {
        if (pair.first.compare("Set-Cookie", Qt::CaseInsensitive) == 0) {
            const QString headerStr = QString::fromUtf8(pair.second);
            const auto parts = headerStr.split(QLatin1Char(';'));
            for (const QString &part : parts) {
                const QString trimmed = part.trimmed();
                if (trimmed.startsWith(QLatin1String("vpn_session="))) {
                    m_sessionToken = trimmed.mid(12).trimmed();
                    Logger::instance().info(QStringLiteral("Found vpn_session cookie from raw header"));
                    return;
                }
            }
        }
    }

    // 3. 从 CookieJar 提取
    if (m_nam.cookieJar()) {
        const QList<QNetworkCookie> jarCookies = m_nam.cookieJar()->cookiesForUrl(reply->url());
        for (const auto &c : jarCookies) {
            if (c.name() == "vpn_session") {
                m_sessionToken = QString::fromUtf8(c.value()).trimmed();
                Logger::instance().info(QStringLiteral("Found vpn_session cookie from CookieJar"));
                return;
            }
        }
    }
}

void AuthController::login(const QString &email, const QString &password, bool remember) {
    const QString trimmedEmail = email.trimmed();
    if (trimmedEmail.isEmpty()) {
        m_loginError = QStringLiteral("请输入邮箱地址");
        emit loginErrorChanged();
        emit loginFailed(m_loginError);
        return;
    }
    if (password.isEmpty()) {
        m_loginError = QStringLiteral("请输入密码");
        emit loginErrorChanged();
        emit loginFailed(m_loginError);
        return;
    }

    setRememberPassword(remember);

    m_isLoggingIn = true;
    m_loginError.clear();
    emit loggingInChanged();
    emit loginErrorChanged();

    QUrl url(m_serverUrl + QStringLiteral("/api/auth/login"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("User-Agent", "MeloBox/1.0");

    QSslConfiguration sslConf = QSslConfiguration::defaultConfiguration();
    sslConf.setPeerVerifyMode(QSslSocket::VerifyPeer);
    request.setSslConfiguration(sslConf);

    QJsonObject body;
    body.insert(QStringLiteral("email"), trimmedEmail);
    body.insert(QStringLiteral("password"), password);
    const QByteArray requestData = QJsonDocument(body).toJson(QJsonDocument::Compact);

    Logger::instance().info(QStringLiteral("Initiating login request to %1 for user %2").arg(url.toString(), trimmedEmail));

    QNetworkReply *reply = m_nam.post(request, requestData);

    connect(reply, &QNetworkReply::sslErrors, this, [reply](const QList<QSslError> &errors) {
        for (const auto &err : errors) {
            Logger::instance().warn(QStringLiteral("SSL Error on login: %1").arg(err.errorString()));
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, trimmedEmail, password]() {
        reply->deleteLater();
        m_isLoggingIn = false;
        emit loggingInChanged();

        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray responseData = reply->readAll();
        Logger::instance().info(QStringLiteral("Login response status: %1, body len: %2")
            .arg(statusCode).arg(responseData.size()));

        extractSessionCookie(reply);

        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseErr);
        if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject root = doc.object();
            const bool ok = root.value(QStringLiteral("ok")).toBool();
            if (ok && statusCode == 200) {
                Logger::instance().info(QStringLiteral("Login successful for: %1").arg(trimmedEmail));
                m_isLoggedIn = true;
                m_loginError.clear();
                emit loginErrorChanged();
                emit loginStatusChanged();
                {
                    QSettings st(QStringLiteral("MeloBox"), QStringLiteral("MeloBox"));
                    st.setValue(QStringLiteral("auth/sessionToken"), m_sessionToken);
                }

                if (m_rememberPassword) {
                    saveCredentials(trimmedEmail, password);
                } else {
                    m_savedEmail = trimmedEmail;
                    clearSavedPassword();
                }

                emit loginSuccess();
                refreshUserInfo();
                return;
            } else {
                QString msg = root.value(QStringLiteral("message")).toString();
                if (msg.isEmpty()) {
                    msg = QStringLiteral("登录失败，请检查账号密码");
                }
                m_loginError = msg;
                Logger::instance().warn(QStringLiteral("Login failed: %1").arg(msg));
                emit loginErrorChanged();
                emit loginFailed(msg);
                return;
            }
        }

        // 网络错误或非 JSON 响应
        if (reply->error() != QNetworkReply::NoError) {
            m_loginError = QStringLiteral("网络连接错误: %1").arg(reply->errorString());
        } else {
            m_loginError = QStringLiteral("服务器响应异常 (HTTP %1)").arg(statusCode);
        }
        Logger::instance().error(QStringLiteral("Login network/protocol error: %1").arg(m_loginError));
        emit loginErrorChanged();
        emit loginFailed(m_loginError);
    });
}

void AuthController::refreshUserInfo() {
    if (m_serverUrl.isEmpty()) {
        return;
    }

    m_isRefreshingUserInfo = true;
    emit refreshingUserInfoChanged();

    QUrl url(m_serverUrl + QStringLiteral("/api/auth/me"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("User-Agent", "MeloBox/1.0");

    if (!m_sessionToken.isEmpty()) {
        request.setRawHeader("Cookie", QStringLiteral("vpn_session=%1").arg(m_sessionToken).toUtf8());
    }

    QSslConfiguration sslConf = QSslConfiguration::defaultConfiguration();
    request.setSslConfiguration(sslConf);

    Logger::instance().info(QStringLiteral("Fetching user info from: %1").arg(url.toString()));

    QNetworkReply *reply = m_nam.get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_isRefreshingUserInfo = false;
        emit refreshingUserInfoChanged();

        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray responseData = reply->readAll();

        if (statusCode == 401) {
            Logger::instance().warn(QStringLiteral("Session expired (401), logging out"));
            m_isLoggedIn = false;
            m_sessionToken.clear();
            QSettings(QStringLiteral("MeloBox"), QStringLiteral("MeloBox"))
                .remove(QStringLiteral("auth/sessionToken"));
            emit loginStatusChanged();
            // token 过期就退回凭据自动登录（没存凭据时它会自己跳过）
            tryAutoLogin();
            return;
        }

        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseErr);
        if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject root = doc.object();
            if (root.value(QStringLiteral("ok")).toBool()) {
                const QJsonObject data = root.value(QStringLiteral("data")).toObject();
                m_userId = data.value(QStringLiteral("id")).toString();
                m_username = data.value(QStringLiteral("username")).toString();
                m_email = data.value(QStringLiteral("email")).toString();
                m_role = data.value(QStringLiteral("role")).toString();
                m_tier = data.value(QStringLiteral("tier")).toString();
                m_dailyLimit = data.value(QStringLiteral("daily_limit")).toInteger();
                m_usedToday = data.value(QStringLiteral("used_today")).toInteger();
                m_points = data.value(QStringLiteral("points")).toInteger();
                m_createdAt = data.value(QStringLiteral("created_at")).toString();

                const QString rawLastLogin = data.value(QStringLiteral("last_login_at")).toString();
                QDateTime dt = QDateTime::fromString(rawLastLogin, Qt::ISODateWithMs);
                if (!dt.isValid()) {
                    dt = QDateTime::fromString(rawLastLogin, Qt::ISODate);
                }
                if (dt.isValid()) {
                    m_lastLoginAt = dt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                } else {
                    m_lastLoginAt = rawLastLogin;
                }

                m_subscriptionEnabled = data.value(QStringLiteral("subscription_enabled")).toBool();
                m_linuxdoBound = data.value(QStringLiteral("linuxdo_bound")).toBool();

                Logger::instance().info(QStringLiteral("User info loaded: %1 (%2), role: %3, tier: %4, points: %5")
                    .arg(m_username, m_email, m_role, m_tier).arg(m_points));

                emit userInfoChanged();
                return;
            }
        }

        Logger::instance().warn(QStringLiteral("Failed to parse user info: HTTP %1, body: %2")
            .arg(statusCode).arg(QString::fromUtf8(responseData)));
    });
}

void AuthController::logout() {
    Logger::instance().info(QStringLiteral("Logging out user"));

    if (!m_sessionToken.isEmpty() && !m_serverUrl.isEmpty()) {
        QUrl url(m_serverUrl + QStringLiteral("/api/auth/logout"));
        QNetworkRequest request(url);
        request.setRawHeader("User-Agent", "MeloBox/1.0");
        request.setRawHeader("Cookie", QStringLiteral("vpn_session=%1").arg(m_sessionToken).toUtf8());
        QNetworkReply *reply = m_nam.post(request, QByteArray());
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }

    m_sessionToken.clear();
    QSettings(QStringLiteral("MeloBox"), QStringLiteral("MeloBox"))
        .remove(QStringLiteral("auth/sessionToken"));
    m_isLoggedIn = false;
    m_loginError.clear();

    m_userId.clear();
    m_username.clear();
    m_email.clear();
    m_role.clear();
    m_tier.clear();
    m_dailyLimit = 0;
    m_usedToday = 0;
    m_points = 0;
    m_createdAt.clear();
    m_lastLoginAt.clear();
    m_subscriptionEnabled = false;
    m_linuxdoBound = false;

    if (m_nam.cookieJar()) {
        m_nam.setCookieJar(new QNetworkCookieJar(this));
    }

    emit loginErrorChanged();
    emit loginStatusChanged();
    emit userInfoChanged();
    emit loggedOut();
}
