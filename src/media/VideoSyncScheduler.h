#pragma once

class VideoSyncScheduler {
public:
    enum class Decision { Present, Drop, Wait };

    Decision evaluate(double framePtsSec, double audioClockSec, double maxDelaySec = 0.5) const;
    double computeDelay(double framePtsSec, double audioClockSec) const;
};
