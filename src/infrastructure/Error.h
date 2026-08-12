#pragma once

#include <QString>

enum class ErrorCode {
    None,
    FileNotFound,
    InvalidFormat,
    DecoderInitFailed,
    NetworkTimeout,
    StreamNotFound,
    PipelineError,
    Unknown
};

struct Error {
    bool ok = true;
    QString message;
    ErrorCode code = ErrorCode::None;

    static Error success() {
        return {};
    }

    static Error failure(const QString &message, ErrorCode code = ErrorCode::Unknown) {
        return { false, message, code };
    }
};
