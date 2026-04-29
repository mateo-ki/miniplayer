#include "core/MediaSession.h"

#include <QFileInfo>

Error MediaSession::open(const QString &path) {
    close();

    if (!QFileInfo::exists(path)) {
        return Error::failure("file does not exist");
    }

    AVFormatContext *rawContext = nullptr;
    if (avformat_open_input(&rawContext, path.toUtf8().constData(), nullptr, nullptr) < 0) {
        return Error::failure("avformat_open_input failed");
    }

    formatContext_.reset(rawContext);
    if (avformat_find_stream_info(formatContext_.get(), nullptr) < 0) {
        close();
        return Error::failure("avformat_find_stream_info failed");
    }

    return Error::success();
}

void MediaSession::close() {
    formatContext_.reset();
}

bool MediaSession::isOpen() const {
    return formatContext_ != nullptr;
}

AVFormatContext *MediaSession::formatContext() const {
    return formatContext_.get();
}
