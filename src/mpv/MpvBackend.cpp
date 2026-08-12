#include "mpv/MpvBackend.h"

#include <QCoreApplication>
#include <QDir>
#include <QStringList>

#include "infrastructure/Logger.h"

struct mpv_event {
    int event_id;
    int error;
    quint64 reply_userdata;
    void *data;
};

struct MpvEventEndFile {
    int reason;
    int error;
    qint64 playlist_entry_id;
    qint64 playlist_insert_id;
    int playlist_insert_num_entries;
};

MpvBackend::MpvBackend(QObject *parent)
    : QObject(parent) {
    connect(&pollTimer_, &QTimer::timeout, this, &MpvBackend::pollEvents);
    pollTimer_.setInterval(10);
}

MpvBackend::~MpvBackend() {
    if (handle_ && mpv_terminate_destroy_) {
        mpv_terminate_destroy_(handle_);
        handle_ = nullptr;
    }
}

bool MpvBackend::isAvailable() const {
    return handle_ != nullptr;
}

bool MpvBackend::ensureAvailable() {
    if (handle_) return true;
    return initialize();
}

QString MpvBackend::errorString() const {
    return errorString_;
}

bool MpvBackend::loadLibrary() {
    if (library_.isLoaded()) return true;

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QStringLiteral("mpv-2"),
        QStringLiteral("libmpv-2"),
        QDir(appDir).filePath(QStringLiteral("mpv-2.dll")),
        QDir(appDir).filePath(QStringLiteral("libmpv-2.dll"))
    };

    QStringList errors;
    for (const QString &candidate : candidates) {
        library_.setFileName(candidate);
        if (!library_.load()) {
            errors.append(candidate + QStringLiteral(": ") + library_.errorString());
            continue;
        }
        Logger::instance().info("Loaded libmpv library: " + library_.fileName());
        break;
    }

    if (!library_.isLoaded()) {
        errorString_ = QStringLiteral("Cannot load libmpv. Tried: ") + errors.join(QStringLiteral(" | "));
        Logger::instance().warn(errorString_);
        return false;
    }

    mpv_create_ = reinterpret_cast<mpv_create_fn>(library_.resolve("mpv_create"));
    mpv_initialize_ = reinterpret_cast<mpv_initialize_fn>(library_.resolve("mpv_initialize"));
    mpv_terminate_destroy_ = reinterpret_cast<mpv_terminate_destroy_fn>(library_.resolve("mpv_terminate_destroy"));
    mpv_set_option_ = reinterpret_cast<mpv_set_option_fn>(library_.resolve("mpv_set_option"));
    mpv_set_option_string_ = reinterpret_cast<mpv_set_option_string_fn>(library_.resolve("mpv_set_option_string"));
    mpv_set_property_ = reinterpret_cast<mpv_set_property_fn>(library_.resolve("mpv_set_property"));
    mpv_command_async_ = reinterpret_cast<mpv_command_async_fn>(library_.resolve("mpv_command_async"));
    mpv_observe_property_ = reinterpret_cast<mpv_observe_property_fn>(library_.resolve("mpv_observe_property"));
    mpv_wait_event_ = reinterpret_cast<mpv_wait_event_fn>(library_.resolve("mpv_wait_event"));
    mpv_error_string_ = reinterpret_cast<mpv_error_string_fn>(library_.resolve("mpv_error_string"));

    if (!mpv_create_ || !mpv_initialize_ || !mpv_terminate_destroy_ || !mpv_set_option_ ||
        !mpv_set_option_string_ || !mpv_set_property_ || !mpv_command_async_ ||
        !mpv_observe_property_ || !mpv_wait_event_ || !mpv_error_string_) {
        errorString_ = QStringLiteral("mpv-2.dll is missing required libmpv symbols");
        Logger::instance().error(errorString_);
        library_.unload();
        return false;
    }

    return true;
}

bool MpvBackend::initialize() {
    if (!loadLibrary()) return false;

    handle_ = mpv_create_();
    if (!handle_) {
        errorString_ = QStringLiteral("mpv_create failed");
        Logger::instance().error(errorString_);
        return false;
    }

    mpv_set_option_string_(handle_, "terminal", "no");
    mpv_set_option_string_(handle_, "msg-level", "all=warn");
    mpv_set_option_string_(handle_, "vo", "gpu");
    mpv_set_option_string_(handle_, "hwdec", "auto-safe");
    mpv_set_option_string_(handle_, "cache", "yes");
    mpv_set_option_string_(handle_, "cache-pause", "yes");
    mpv_set_option_string_(handle_, "cache-pause-initial", "yes");
    mpv_set_option_string_(handle_, "cache-pause-wait", "3");
    mpv_set_option_string_(handle_, "demuxer-max-bytes", "256MiB");
    mpv_set_option_string_(handle_, "demuxer-readahead-secs", "45");
    mpv_set_option_string_(handle_, "hr-seek", "yes");

    if (pendingWindowId_ > 0) {
        qint64 wid = pendingWindowId_;
        mpv_set_option_(handle_, "wid", MPV_FORMAT_INT64, &wid);
    }

    int ret = mpv_initialize_(handle_);
    if (ret < 0) {
        errorString_ = QStringLiteral("mpv_initialize failed: ") + mpvError(ret);
        Logger::instance().error(errorString_);
        mpv_terminate_destroy_(handle_);
        handle_ = nullptr;
        return false;
    }

    mpv_observe_property_(handle_, 1, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property_(handle_, 2, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property_(handle_, 3, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property_(handle_, 4, "cache-buffering-state", MPV_FORMAT_INT64);
    mpv_observe_property_(handle_, 5, "demuxer-cache-duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property_(handle_, 6, "demuxer-cache-time", MPV_FORMAT_DOUBLE);

    pollTimer_.start();
    emit availableChanged();
    Logger::instance().info("libmpv backend initialized");
    return true;
}

QString MpvBackend::mpvError(int code) const {
    if (!mpv_error_string_) return QString::number(code);
    return QString::fromUtf8(mpv_error_string_(code));
}

void MpvBackend::setWindowId(qint64 windowId) {
    pendingWindowId_ = windowId;
    if (handle_) {
        qint64 wid = windowId;
        mpv_set_option_(handle_, "wid", MPV_FORMAT_INT64, &wid);
    }
}

void MpvBackend::command(std::initializer_list<QByteArray> args) {
    if (!handle_) return;
    QVector<const char *> argv;
    argv.reserve(static_cast<int>(args.size()) + 1);
    for (const QByteArray &arg : args) {
        argv.append(arg.constData());
    }
    argv.append(nullptr);
    int ret = mpv_command_async_(handle_, 0, argv.data());
    if (ret < 0) {
        Logger::instance().warn("mpv command failed: " + mpvError(ret));
    }
}

void MpvBackend::load(const QString &url) {
    if (!ensureAvailable()) {
        emit errorOccurred(errorString_);
        return;
    }
    Logger::instance().info(QStringLiteral("[ShortVideoDebug][mpv] loadfile %1").arg(url));
    positionMs_ = 0;
    durationMs_ = 0;
    cacheDurationSec_ = 0.0;
    cacheTimeSec_ = -1.0;
    emit cacheProgressChanged(0.0);
    emit bufferingChanged(true);
    ignoreNextEndFile_ = true;
    if (pendingWindowId_ > 0) {
        qint64 wid = pendingWindowId_;
        mpv_set_option_(handle_, "wid", MPV_FORMAT_INT64, &wid);
    }
    command({"loadfile", url.toUtf8(), "replace"});
}

void MpvBackend::play() {
    int pause = 0;
    if (handle_) mpv_set_property_(handle_, "pause", MPV_FORMAT_FLAG, &pause);
}

void MpvBackend::pause() {
    int pause = 1;
    if (handle_) mpv_set_property_(handle_, "pause", MPV_FORMAT_FLAG, &pause);
}

void MpvBackend::stop() {
    command({"stop"});
}

void MpvBackend::seek(qint64 positionMs) {
    command({"seek", QByteArray::number(positionMs / 1000.0, 'f', 3), "absolute", "exact"});
}

void MpvBackend::setVolume(double volume) {
    double mpvVolume = qBound(0.0, volume * 100.0, 100.0);
    if (handle_) mpv_set_property_(handle_, "volume", MPV_FORMAT_DOUBLE, &mpvVolume);
}

void MpvBackend::setSpeed(double speed) {
    double bounded = qBound(0.25, speed, 4.0);
    if (handle_) mpv_set_property_(handle_, "speed", MPV_FORMAT_DOUBLE, &bounded);
}

void MpvBackend::pollEvents() {
    if (!handle_) return;
    while (true) {
        mpv_event *event = mpv_wait_event_(handle_, 0.0);
        if (!event || event->event_id == MPV_EVENT_NONE) break;

        if (event->event_id == MPV_EVENT_PROPERTY_CHANGE && event->data) {
            auto *prop = static_cast<MpvEventProperty *>(event->data);
            if (!prop->data || !prop->name) continue;
            const QByteArray name(prop->name);
            if (name == "time-pos" && prop->format == MPV_FORMAT_DOUBLE) {
                positionMs_ = static_cast<qint64>(*static_cast<double *>(prop->data) * 1000.0);
                emit positionChanged(positionMs_);
            } else if (name == "duration" && prop->format == MPV_FORMAT_DOUBLE) {
                durationMs_ = static_cast<qint64>(*static_cast<double *>(prop->data) * 1000.0);
                emit durationChanged(durationMs_);
            } else if (name == "pause" && prop->format == MPV_FORMAT_FLAG) {
                emit playingChanged(*static_cast<int *>(prop->data) == 0);
            } else if (name == "cache-buffering-state" && prop->format == MPV_FORMAT_INT64) {
                emit bufferingChanged(*static_cast<qint64 *>(prop->data) >= 0);
            } else if (name == "demuxer-cache-duration" && prop->format == MPV_FORMAT_DOUBLE) {
                cacheDurationSec_ = qMax(0.0, *static_cast<double *>(prop->data));
                if (durationMs_ > 0) {
                    const double cachedEndMs = cacheTimeSec_ >= 0.0
                        ? cacheTimeSec_ * 1000.0
                        : static_cast<double>(positionMs_) + cacheDurationSec_ * 1000.0;
                    emit cacheProgressChanged(qBound(0.0, cachedEndMs / durationMs_, 1.0));
                }
            } else if (name == "demuxer-cache-time" && prop->format == MPV_FORMAT_DOUBLE) {
                cacheTimeSec_ = *static_cast<double *>(prop->data);
                if (durationMs_ > 0 && cacheTimeSec_ >= 0.0) {
                    emit cacheProgressChanged(qBound(0.0, cacheTimeSec_ * 1000.0 / durationMs_, 1.0));
                }
            }
        } else if (event->event_id == MPV_EVENT_START_FILE) {
            ignoreNextEndFile_ = false;
            emit playingChanged(true);
            emit bufferingChanged(false);
        } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
            ignoreNextEndFile_ = false;
            emit playingChanged(true);
            emit bufferingChanged(false);
        } else if (event->event_id == MPV_EVENT_END_FILE) {
            int reason = -1;
            int endError = event->error;
            if (event->data) {
                auto *endFile = static_cast<MpvEventEndFile *>(event->data);
                reason = endFile->reason;
                endError = endFile->error;
            }
            Logger::instance().info(QStringLiteral("[ShortVideoDebug][mpv] END_FILE reason=%1 eventError=%2 endError=%3 position=%4 duration=%5")
                .arg(reason)
                .arg(event->error)
                .arg(endError)
                .arg(positionMs_)
                .arg(durationMs_));
            if (ignoreNextEndFile_) {
                ignoreNextEndFile_ = false;
                Logger::instance().info(QStringLiteral("[ShortVideoDebug][mpv] ignored stale END_FILE while replacing file"));
                continue;
            }
            emit bufferingChanged(false);
            emit playingChanged(false);
            emit playbackEnded();
        } else if (event->event_id == MPV_EVENT_SHUTDOWN) {
            emit bufferingChanged(false);
            emit playingChanged(false);
            break;
        }
    }
}
