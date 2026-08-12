#include "media/VideoSyncScheduler.h"

VideoSyncScheduler::Decision VideoSyncScheduler::evaluate(
    double framePtsSec, double audioClockSec, double maxDelaySec) const {
    if (framePtsSec + maxDelaySec < audioClockSec) {
        return Decision::Drop;
    }
    if (framePtsSec > audioClockSec) {
        return Decision::Wait;
    }
    return Decision::Present;
}

double VideoSyncScheduler::computeDelay(double framePtsSec, double audioClockSec) const {
    if (framePtsSec > audioClockSec) {
        return framePtsSec - audioClockSec;
    }
    return 0.0;
}
