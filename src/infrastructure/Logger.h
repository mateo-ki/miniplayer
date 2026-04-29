#pragma once

#include <functional>

#include <QString>

class Logger {
public:
    using Sink = std::function<void(const QString &, const QString &)>;

    static Logger &instance();

    void setSink(Sink sink);
    void info(const QString &message) const;
    void warn(const QString &message) const;
    void error(const QString &message) const;

private:
    Sink sink_;
};
