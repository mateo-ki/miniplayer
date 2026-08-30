#include "controllers/UpdateController.h"

#include <QDesktopServices>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>

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

bool UpdateController::updateChecking() const { return updateChecking_; }
bool UpdateController::updateDownloading() const { return updateDownloading_; }
bool UpdateController::updateAvailable() const { return updateAvailable_; }
QString UpdateController::updateVersion() const { return updateVersion_; }
QString UpdateController::updateMessage() const { return updateMessage_; }
double UpdateController::updateDownloadProgress() const { return updateDownloadProgress_; }
namespace {
QString updaterOwner() {
    return QStringLiteral(MELOBOX_UPDATE_OWNER).trimmed();
}

QString updaterRepo() {
    return QStringLiteral(MELOBOX_UPDATE_REPO).trimmed();
}
}

#ifndef MELOBOX_APP_VERSION
#define MELOBOX_APP_VERSION "1.0"
#endif

#ifndef MELOBOX_UPDATE_OWNER
#define MELOBOX_UPDATE_OWNER ""
#endif

#ifndef MELOBOX_UPDATE_REPO
#define MELOBOX_UPDATE_REPO ""
#endif

UpdateController::UpdateController(QObject *parent)
    : QObject(parent) {
}

QString UpdateController::appVersion() const {
    return QStringLiteral(MELOBOX_APP_VERSION);
}


QString UpdateController::normalizeVersion(QString version) {
    version = version.trimmed();
    if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        version.remove(0, 1);
    return version;
}

void UpdateController::setUpdateState(bool checking, bool downloading, bool available, const QString &message) {
    bool changed = false;
    if (updateChecking_ != checking) {
        updateChecking_ = checking;
        changed = true;
    }
    if (updateDownloading_ != downloading) {
        updateDownloading_ = downloading;
        changed = true;
    }
    if (updateAvailable_ != available) {
        updateAvailable_ = available;
        changed = true;
    }
    if (updateMessage_ != message) {
        updateMessage_ = message;
        changed = true;
    }
    if (changed)
        emit updateStateChanged();
}


int UpdateController::compareVersions(const QString &left, const QString &right) {
    const QStringList leftParts = normalizeVersion(left).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const QStringList rightParts = normalizeVersion(right).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const int count = qMax(leftParts.size(), rightParts.size());
    for (int i = 0; i < count; ++i) {
        const int leftValue = i < leftParts.size() ? leftParts.at(i).toInt() : 0;
        const int rightValue = i < rightParts.size() ? rightParts.at(i).toInt() : 0;
        if (leftValue < rightValue) return -1;
        if (leftValue > rightValue) return 1;
    }
    return 0;
}


void UpdateController::checkForUpdates() {
    if (updateChecking_ || updateDownloading_)
        return;

    const QString owner = updaterOwner();
    const QString repo = updaterRepo();
    if (owner.isEmpty() || repo.isEmpty()) {
        setUpdateState(false, false, false, QStringLiteral("未配置 GitHub 更新源"));
        return;
    }

    updateAssetUrl_.clear();
    updateAssetName_.clear();
    updateVersion_.clear();
    updateDownloadProgress_ = 0.0;
    emit updateStateChanged();
    setUpdateState(true, false, false, QStringLiteral("正在检查更新..."));

    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest").arg(owner, repo));
    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", QByteArrayLiteral("MeloBox-Updater/1.0"));
    request.setRawHeader("Accept", QByteArrayLiteral("application/vnd.github+json"));

    QNetworkReply *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            setUpdateState(false, false, false, QStringLiteral("检查更新失败：%1").arg(reply->errorString()));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            setUpdateState(false, false, false, QStringLiteral("更新响应格式无效"));
            return;
        }

        const QJsonObject root = doc.object();
        const QString latestVersion = normalizeVersion(root.value(QStringLiteral("tag_name")).toString(
            root.value(QStringLiteral("name")).toString()));
        if (latestVersion.isEmpty()) {
            setUpdateState(false, false, false, QStringLiteral("没有读取到最新版本号"));
            return;
        }

        QString assetUrl;
        QString assetName;
        const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
        for (const QJsonValue &value : assets) {
            const QJsonObject asset = value.toObject();
            const QString name = asset.value(QStringLiteral("name")).toString();
            if (!name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
                continue;
            if (!name.contains(QStringLiteral("MeloBox"), Qt::CaseInsensitive))
                continue;
            assetName = name;
            assetUrl = asset.value(QStringLiteral("browser_download_url")).toString();
            break;
        }

        if (compareVersions(appVersion(), latestVersion) >= 0) {
            updateVersion_ = latestVersion;
            setUpdateState(false, false, false, QStringLiteral("当前已是最新版本：%1").arg(appVersion()));
            return;
        }

        if (assetUrl.isEmpty()) {
            updateVersion_ = latestVersion;
            setUpdateState(false, false, false, QStringLiteral("发现新版本 %1，但没有找到安装包").arg(latestVersion));
            return;
        }

        updateVersion_ = latestVersion;
        updateAssetUrl_ = assetUrl;
        updateAssetName_ = assetName.isEmpty()
            ? QStringLiteral("MeloBox-Setup-%1.exe").arg(latestVersion)
            : assetName;
        setUpdateState(false, false, true, QStringLiteral("发现新版本：%1").arg(latestVersion));
    });
}


void UpdateController::downloadAndInstallUpdate() {
    if (!updateAvailable_ || updateAssetUrl_.isEmpty() || updateDownloading_)
        return;

    updateDownloadProgress_ = 0.0;
    emit updateStateChanged();
    setUpdateState(false, true, true, QStringLiteral("正在下载更新..."));

    QString folder = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (folder.isEmpty())
        folder = QCoreApplication::applicationDirPath();
    folder = QDir(folder).filePath(QStringLiteral("MeloBox-update"));
    QDir().mkpath(folder);

    const QString fileName = updateAssetName_.isEmpty() ? QStringLiteral("MeloBox-Setup.exe") : updateAssetName_;
    const QString installerPath = QDir(folder).filePath(fileName);

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(updateAssetUrl_)};
    request.setRawHeader("User-Agent", QByteArrayLiteral("MeloBox-Updater/1.0"));
    QNetworkReply *reply = nam->get(request);

    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total > 0) {
            updateDownloadProgress_ = qBound(0.0, static_cast<double>(received) / static_cast<double>(total), 1.0);
            emit updateStateChanged();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, installerPath]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            setUpdateState(false, false, updateAvailable_, QStringLiteral("下载更新失败：%1").arg(reply->errorString()));
            return;
        }

        QFile file(installerPath);
        if (!file.open(QIODevice::WriteOnly)) {
            setUpdateState(false, false, updateAvailable_, QStringLiteral("保存更新失败：%1").arg(file.errorString()));
            return;
        }
        file.write(reply->readAll());
        file.close();

        updateDownloadProgress_ = 1.0;
        emit updateStateChanged();
        setUpdateState(false, false, true, QStringLiteral("更新已下载，正在启动安装包..."));
        emit installStarted();
        if (!QProcess::startDetached(installerPath, QStringList{})) {
            setUpdateState(false, false, true, QStringLiteral("安装包启动失败：%1").arg(installerPath));
        }
    });
}
