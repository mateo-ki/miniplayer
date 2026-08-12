#include "media/AudioClock.h"

void AudioClock::update(double positionSec) {
    positionSec_.store(positionSec);
}

double AudioClock::positionSec() const {
    return positionSec_.load();
}

void AudioClock::reset() {
    positionSec_.store(0.0);
}
