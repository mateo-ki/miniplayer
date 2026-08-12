#include "infrastructure/Logger.h"

#include <utility>
#include <QDateTime>

Logger::Logger() {
    logFile_.setFileName("miniplayer.log");
    if (logFile_.open(QIODevice::Append | QIODevice::Text)) {
        stream_ = new QTextStream(&logFile_);
    }
}

Logger::~Logger() {
    if (stream_) {
        stream_->flush();
        delete stream_;
        stream_ = nullptr;
    }
    if (logFile_.isOpen()) {
        logFile_.close();
    }
}

Logger &Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::setSink(Sink sink) {
    std::scoped_lock lock(mutex_);
    sink_ = std::move(sink);
}

void Logger::writeToFile(const QString &level, const QString &message) const {
    if (!stream_) return;
    *stream_ << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
             << " [" << level << "] " << message << "\n";
    stream_->flush();
}

void Logger::info(const QString &message) const {
    std::scoped_lock lock(mutex_);
    if (sink_) sink_("info", message);
    writeToFile("INFO", message);
}

void Logger::warn(const QString &message) const {
    std::scoped_lock lock(mutex_);
    if (sink_) sink_("warn", message);
    writeToFile("WARN", message);
}

void Logger::error(const QString &message) const {
    std::scoped_lock lock(mutex_);
    if (sink_) sink_("error", message);
    writeToFile("ERROR", message);
}
