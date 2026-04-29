#pragma once

#include <QString>

struct Error {
    bool ok = true;
    QString message;

    static Error success() {
        return {};
    }

    static Error failure(const QString &message) {
        return { false, message };
    }
};
