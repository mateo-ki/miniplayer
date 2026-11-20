#pragma once

#include <QUrl>
#include <functional>

#include <QObject>
#include <QString>

// 语音模块：语音包获取、缓存、保存与复制。
// 播放本身走播放器：preemptPlaybackRequested 请求打断当前播放，
// playbackRequested 携带已缓存的本地文件路径。
class VoiceController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentVoiceUrl READ currentVoiceUrl NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceMessage READ voiceMessage NOTIFY voiceChanged)
public:
    explicit VoiceController(QObject *parent = nullptr);

    QString currentVoiceUrl() const;
    QString voiceMessage() const;

    Q_INVOKABLE void playVoice(int sourceIndex = 0);
    Q_INVOKABLE void saveVoice(int sourceIndex = 0);
    Q_INVOKABLE void copyVoice(int sourceIndex = 0);
    Q_INVOKABLE void saveCurrentVoice();
    Q_INVOKABLE void copyCurrentVoice();

    // 由 PlayerController 借用：获取一条语音到本地缓存后回调。
    void fetchVoiceFile(int sourceIndex, const QString &actionText,
                        const std::function<void(const QString &, const QString &, const QString &)> &onReady);

signals:
    void voiceChanged();
    void preemptPlaybackRequested();
    void playbackRequested(const QString &localFile);

private:
    QUrl voiceRequestUrl(int sourceIndex, QString *label = nullptr) const;

    QString currentVoiceUrl_;
    QString currentVoiceFilePath_;
    QString voiceMessage_;
};
