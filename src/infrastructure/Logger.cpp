#include "infrastructure/Logger.h"

#include <utility>

Logger &Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::setSink(Sink sink) {
    sink_ = std::move(sink);
}

void Logger::info(const QString &message) const {
    if (sink_) {
        sink_("info", message);
    }
}

void Logger::warn(const QString &message) const {
    if (sink_) {
        sink_("warn", message);
    }
}

void Logger::error(const QString &message) const {
    if (sink_) {
        sink_("error", message);
    }
}
