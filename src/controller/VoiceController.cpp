#include "controller/VoiceController.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QSaveFile>
#include <QTime>

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

QString VoiceController::currentVoiceUrl() const { return currentVoiceUrl_; }
QString VoiceController::voiceMessage() const { return voiceMessage_; }

VoiceController::VoiceController(QObject *parent)
    : QObject(parent) {
}

QUrl VoiceController::voiceRequestUrl(int sourceIndex, QString *label) const {
    const QString currentTime = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    const QStringList sources = {
        QStringLiteral("http://api.yujn.cn/api/and.php?"),
        QStringLiteral("http://api.yujn.cn/api/yujie.php?"),
        QStringLiteral("https://api.yujn.cn/api/sjkunkun.php?"),
        QStringLiteral("http://api.yujn.cn/api/lvcha.php?"),
        QStringLiteral("http://api.yujn.cn/api/baoshi.php?msg=%1").arg(currentTime)
    };
    const QStringList labels = {
        QStringLiteral("可爱的配音"),
        QStringLiteral("随机播放一条御姐撒娇语音包"),
        QStringLiteral("坤坤语音"),
        QStringLiteral("绿茶"),
        QStringLiteral("语音整点报时")
    };
    const int index = qBound(0, sourceIndex, sources.size() - 1);
    if (label)
        *label = labels[index];
    QUrl url(sources[index]);
    QUrlQuery query(url);
    query.removeQueryItem(QStringLiteral("_t"));
    query.addQueryItem(QStringLiteral("_t"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.setQuery(query);
    return url;
}

void VoiceController::fetchVoiceFile(int sourceIndex, const QString &actionText, const std::function<void(const QString &, const QString &, const QString &)> &onReady) {
    QString label;
    const QUrl url = voiceRequestUrl(sourceIndex, &label);
    const QString requestUrl = url.toString();
    voiceMessage_ = QStringLiteral("正在%1%2").arg(actionText, label);
    emit voiceChanged();

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "audio/*,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, url, requestUrl, label, onReady]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            voiceMessage_ = QStringLiteral("语音获取失败：%1").arg(reply->errorString());
            emit voiceChanged();
            return;
        }

        const QByteArray bytes = reply->readAll();
        if (bytes.isEmpty()) {
            voiceMessage_ = QStringLiteral("语音获取失败：内容为空");
            emit voiceChanged();
            return;
        }

        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        const QString folder = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("cache/voice-current"));
        QDir().mkpath(folder);
        const QString fileName = QStringLiteral("voice-current-%1.%2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz")),
                 MediaUtils::audioExtension(url, contentType));
        const QString path = QDir(folder).filePath(fileName);

        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            voiceMessage_ = QStringLiteral("语音缓存失败：%1").arg(path);
            emit voiceChanged();
            return;
        }

        currentVoiceUrl_ = requestUrl;
        currentVoiceFilePath_ = path;
        onReady(path, requestUrl, label);
    });
}

void VoiceController::saveVoice(int sourceIndex) {
    fetchVoiceFile(sourceIndex, QStringLiteral("下载"), [this](const QString &, const QString &, const QString &) {
        saveCurrentVoice();
    });
}

void VoiceController::copyVoice(int sourceIndex) {
    fetchVoiceFile(sourceIndex, QStringLiteral("复制"), [this](const QString &, const QString &, const QString &) {
        copyCurrentVoice();
    });
}

void VoiceController::saveCurrentVoice() {
    if (currentVoiceFilePath_.trimmed().isEmpty() || !QFileInfo::exists(currentVoiceFilePath_)) {
        voiceMessage_ = QStringLiteral("暂无可下载的语音，请先播放一条语音");
        emit voiceChanged();
        return;
    }

    const QString folder = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("download/voice"));
    QDir().mkpath(folder);
    const QString suffix = QFileInfo(currentVoiceFilePath_).suffix().isEmpty()
        ? QStringLiteral("mp3")
        : QFileInfo(currentVoiceFilePath_).suffix();
    const QString path = QDir(folder).filePath(QStringLiteral("voice-%1.%2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz")), suffix));

    if (!QFile::copy(currentVoiceFilePath_, path)) {
        voiceMessage_ = QStringLiteral("语音保存失败：%1").arg(path);
        emit voiceChanged();
        return;
    }

    voiceMessage_ = QStringLiteral("语音已保存：%1").arg(path);
    emit voiceChanged();
}

void VoiceController::copyCurrentVoice() {
    if (currentVoiceFilePath_.trimmed().isEmpty() || !QFileInfo::exists(currentVoiceFilePath_)) {
        voiceMessage_ = QStringLiteral("暂无可复制的语音，请先播放一条语音");
        emit voiceChanged();
        return;
    }

    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        voiceMessage_ = QStringLiteral("复制失败：剪贴板不可用");
        emit voiceChanged();
        return;
    }

    auto *mimeData = new QMimeData();
    mimeData->setUrls({QUrl::fromLocalFile(currentVoiceFilePath_)});
    clipboard->setMimeData(mimeData);
    voiceMessage_ = QStringLiteral("已复制当前语音文件：%1").arg(currentVoiceFilePath_);
    emit voiceChanged();
}

void VoiceController::playVoice(int sourceIndex) {
    currentVoiceFilePath_.clear();
    emit preemptPlaybackRequested();
    fetchVoiceFile(sourceIndex, QStringLiteral("获取"), [this](const QString &path, const QString &, const QString &label) {
        voiceMessage_ = QStringLiteral("正在播放%1").arg(label);
        emit voiceChanged();
        emit playbackRequested(path);
    });
}
