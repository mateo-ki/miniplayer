#pragma once

#include <atomic>

class AudioClock {
public:
    void update(double positionSec);
    double positionSec() const;
    void reset();

private:
    std::atomic<double> positionSec_ = 0.0;
};
