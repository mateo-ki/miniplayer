#pragma once

#include <QString>

struct ImageHlsSegment {
    QString url;
    double durationSec = 0.0;
    double startPtsSec = 0.0;
};
