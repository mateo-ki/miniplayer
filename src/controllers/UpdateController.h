#pragma once

#include <QObject>
#include <QString>

// GitHub Releases 自动更新：检查、下载并启动安装包。
class UpdateController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(bool updateChecking READ updateChecking NOTIFY updateStateChanged)
    Q_PROPERTY(bool updateDownloading READ updateDownloading NOTIFY updateStateChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateStateChanged)
    Q_PROPERTY(QString updateVersion READ updateVersion NOTIFY updateStateChanged)
    Q_PROPERTY(QString updateMessage READ updateMessage NOTIFY updateStateChanged)
    Q_PROPERTY(double updateDownloadProgress READ updateDownloadProgress NOTIFY updateStateChanged)
public:
    explicit UpdateController(QObject *parent = nullptr);

    QString appVersion() const;
    bool updateChecking() const;
    bool updateDownloading() const;
    bool updateAvailable() const;
    QString updateVersion() const;
    QString updateMessage() const;
    double updateDownloadProgress() const;

    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void downloadAndInstallUpdate();

signals:
    void updateStateChanged();
    // 安装包即将启动：播放器应停止当前播放。
    void installStarted();

private:
    static QString normalizeVersion(QString version);
    static int compareVersions(const QString &left, const QString &right);
    void setUpdateState(bool checking, bool downloading, bool available, const QString &message);

    bool updateChecking_ = false;
    bool updateDownloading_ = false;
    bool updateAvailable_ = false;
    QString updateVersion_;
    QString updateMessage_;
    QString updateAssetUrl_;
    QString updateAssetName_;
    double updateDownloadProgress_ = 0.0;
};
