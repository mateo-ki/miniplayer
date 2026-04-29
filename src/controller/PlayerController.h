#pragma once

#include <QObject>
#include <QString>

#include "models/MediaInfoModel.h"
#include "models/RuntimeLogModel.h"

class PlayerController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playbackStateChanged)
    Q_PROPERTY(bool isPaused READ isPaused NOTIFY playbackStateChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY timelineChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY timelineChanged)
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY currentFileChanged)
    Q_PROPERTY(MediaInfoModel *mediaInfoModel READ mediaInfoModel CONSTANT)
    Q_PROPERTY(RuntimeLogModel *runtimeLogModel READ runtimeLogModel CONSTANT)

public:
    explicit PlayerController(QObject *parent = nullptr);

    bool isPlaying() const;
    bool isPaused() const;
    qint64 durationMs() const;
    qint64 positionMs() const;
    float volume() const;
    bool muted() const;
    QString currentFile() const;
    MediaInfoModel *mediaInfoModel();
    RuntimeLogModel *runtimeLogModel();

    Q_INVOKABLE void openFile();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 positionMs);

    void setVolume(float volume);
    void setMuted(bool muted);

signals:
    void playbackStateChanged();
    void timelineChanged();
    void volumeChanged();
    void mutedChanged();
    void currentFileChanged();

private:
    void seedInitialModels();

    MediaInfoModel mediaInfoModel_;
    RuntimeLogModel runtimeLogModel_;
    bool isPlaying_ = false;
    bool isPaused_ = false;
    qint64 durationMs_ = 0;
    qint64 positionMs_ = 0;
    float volume_ = 1.0f;
    bool muted_ = false;
    QString currentFile_;
};
