#pragma once

#include <functional>
#include <mutex>

#include <QString>
#include <QFile>
#include <QTextStream>

class Logger {
public:
    using Sink = std::function<void(const QString &, const QString &)>;

    static Logger &instance();
    ~Logger();

    void setSink(Sink sink);
    void info(const QString &message) const;
    void warn(const QString &message) const;
    void error(const QString &message) const;

private:
    Logger();
    void writeToFile(const QString &level, const QString &message) const;

    mutable std::mutex mutex_;
    Sink sink_;
    mutable QFile logFile_;
    mutable QTextStream *stream_ = nullptr;
};
