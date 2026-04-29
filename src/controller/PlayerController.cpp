#include "controller/PlayerController.h"

#include <QtGlobal>

#include "infrastructure/Logger.h"

PlayerController::PlayerController(QObject *parent)
    : QObject(parent) {
    Logger::instance().setSink([this](const QString &level, const QString &message) {
        runtimeLogModel_.append(level, message);
    });

    seedInitialModels();
    Logger::instance().info("PlayerController initialized");
}

bool PlayerController::isPlaying() const {
    return isPlaying_;
}

bool PlayerController::isPaused() const {
    return isPaused_;
}

qint64 PlayerController::durationMs() const {
    return durationMs_;
}

qint64 PlayerController::positionMs() const {
    return positionMs_;
}

float PlayerController::volume() const {
    return volume_;
}

bool PlayerController::muted() const {
    return muted_;
}

QString PlayerController::currentFile() const {
    return currentFile_;
}

MediaInfoModel *PlayerController::mediaInfoModel() {
    return &mediaInfoModel_;
}

RuntimeLogModel *PlayerController::runtimeLogModel() {
    return &runtimeLogModel_;
}

void PlayerController::openFile() {
    Logger::instance().info("Open requested - file picker will be connected in a later task");
}

void PlayerController::play() {
    isPlaying_ = true;
    isPaused_ = false;
    Logger::instance().info("Playback entered play state");
    emit playbackStateChanged();
}

void PlayerController::pause() {
    isPaused_ = true;
    Logger::instance().warn("Playback entered pause state");
    emit playbackStateChanged();
}

void PlayerController::stop() {
    isPlaying_ = false;
    isPaused_ = false;
    positionMs_ = 0;
    Logger::instance().info("Playback stopped and position reset");
    emit playbackStateChanged();
    emit timelineChanged();
}

void PlayerController::seek(qint64 positionMs) {
    positionMs_ = qBound<qint64>(0, positionMs, durationMs_ > 0 ? durationMs_ : positionMs);
    Logger::instance().info("Seek requested to " + QString::number(positionMs_) + " ms");
    emit timelineChanged();
}

void PlayerController::setVolume(float volume) {
    const float boundedVolume = qBound(0.0f, volume, 1.0f);
    if (qFuzzyCompare(volume_, boundedVolume)) {
        return;
    }
    volume_ = boundedVolume;
    Logger::instance().info("Volume changed to " + QString::number(volume_));
    emit volumeChanged();
}

void PlayerController::setMuted(bool muted) {
    if (muted_ == muted) {
        return;
    }
    muted_ = muted;
    Logger::instance().info(muted_ ? "Muted" : "Unmuted");
    emit mutedChanged();
}

void PlayerController::seedInitialModels() {
    mediaInfoModel_.replaceAll({
        { "Container", "Shell mock session" },
        { "Video", "No active stream yet" },
        { "Audio", "No active stream yet" }
    });
}
