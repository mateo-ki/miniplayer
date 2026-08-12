#pragma once

#include <QLibrary>
#include <QObject>
#include <QTimer>

struct mpv_handle;
struct mpv_event;

class MpvBackend final : public QObject {
    Q_OBJECT
public:
    explicit MpvBackend(QObject *parent = nullptr);
    ~MpvBackend() override;

    bool ensureAvailable();
    bool isAvailable() const;
    QString errorString() const;
    void setWindowId(qint64 windowId);
    void load(const QString &url);
    void play();
    void pause();
    void stop();
    void seek(qint64 positionMs);
    void setVolume(double volume);
    void setSpeed(double speed);

signals:
    void availableChanged();
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void playingChanged(bool playing);
    void bufferingChanged(bool buffering);
    void cacheProgressChanged(double progress);
    void playbackEnded();
    void errorOccurred(const QString &message);

private:
    enum MpvFormat {
        MPV_FORMAT_NONE = 0,
        MPV_FORMAT_STRING = 1,
        MPV_FORMAT_OSD_STRING = 2,
        MPV_FORMAT_FLAG = 3,
        MPV_FORMAT_INT64 = 4,
        MPV_FORMAT_DOUBLE = 5
    };

    enum MpvEventId {
        MPV_EVENT_NONE = 0,
        MPV_EVENT_SHUTDOWN = 1,
        MPV_EVENT_LOG_MESSAGE = 2,
        MPV_EVENT_GET_PROPERTY_REPLY = 3,
        MPV_EVENT_SET_PROPERTY_REPLY = 4,
        MPV_EVENT_COMMAND_REPLY = 5,
        MPV_EVENT_START_FILE = 6,
        MPV_EVENT_END_FILE = 7,
        MPV_EVENT_FILE_LOADED = 8,
        MPV_EVENT_PROPERTY_CHANGE = 22
    };

    struct MpvEventProperty {
        const char *name;
        MpvFormat format;
        void *data;
    };

    using mpv_create_fn = mpv_handle *(*)();
    using mpv_initialize_fn = int (*)(mpv_handle *);
    using mpv_terminate_destroy_fn = void (*)(mpv_handle *);
    using mpv_set_option_fn = int (*)(mpv_handle *, const char *, MpvFormat, void *);
    using mpv_set_option_string_fn = int (*)(mpv_handle *, const char *, const char *);
    using mpv_set_property_fn = int (*)(mpv_handle *, const char *, MpvFormat, void *);
    using mpv_command_async_fn = int (*)(mpv_handle *, quint64, const char **);
    using mpv_observe_property_fn = int (*)(mpv_handle *, quint64, const char *, MpvFormat);
    using mpv_wait_event_fn = mpv_event *(*)(mpv_handle *, double);
    using mpv_error_string_fn = const char *(*)(int);

    bool loadLibrary();
    bool initialize();
    void pollEvents();
    QString mpvError(int code) const;
    void command(std::initializer_list<QByteArray> args);

    QLibrary library_;
    mpv_handle *handle_ = nullptr;
    QTimer pollTimer_;
    QString errorString_;
    qint64 pendingWindowId_ = 0;
    qint64 positionMs_ = 0;
    qint64 durationMs_ = 0;
    double cacheDurationSec_ = 0.0;
    double cacheTimeSec_ = -1.0;
    bool ignoreNextEndFile_ = false;

    mpv_create_fn mpv_create_ = nullptr;
    mpv_initialize_fn mpv_initialize_ = nullptr;
    mpv_terminate_destroy_fn mpv_terminate_destroy_ = nullptr;
    mpv_set_option_fn mpv_set_option_ = nullptr;
    mpv_set_option_string_fn mpv_set_option_string_ = nullptr;
    mpv_set_property_fn mpv_set_property_ = nullptr;
    mpv_command_async_fn mpv_command_async_ = nullptr;
    mpv_observe_property_fn mpv_observe_property_ = nullptr;
    mpv_wait_event_fn mpv_wait_event_ = nullptr;
    mpv_error_string_fn mpv_error_string_ = nullptr;
};
