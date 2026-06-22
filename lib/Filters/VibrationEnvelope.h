#pragma once
#include <math.h>
#include "HighPass.h"
#include "LowPass.h"
#include "config.h"

class VibrationEnvelope {
public:
    VibrationEnvelope() : hp(cfg::HP_CUTOFF_HZ), lp(cfg::ENV_LP_HZ) {}
    float update(float accMag, float dt) {
        return lp.run(fabsf(hp.run(accMag, dt)), dt);
    }
private:
    HighPass hp;
    LowPass  lp;
};