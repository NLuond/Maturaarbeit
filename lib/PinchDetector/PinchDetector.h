#pragma once
#include <Arduino.h>
#include "config.h"

class PinchDetector {
public:
    bool tick(float env, float gyroSum, bool mlPinch, uint32_t now) {
        bool ready = (now - tLastPinch_) >= cfg::DEBOUNCE_MS;
        bool calm  = (gyroSum < cfg::PINCH_GYRO_GUARD);
    #if USE_ML_PINCH
        bool hot = mlPinch && calm; (void)env;
    #else
        bool hot = (env > cfg::ENV_ABS) && calm; (void)mlPinch;
    #endif
        bool fired = false;
        if (hot && !above_ && ready) { tLastPinch_ = now; fired = true; }
        above_ = hot;
        return fired;
    }

    bool inFreeze(uint32_t now) const {
        return (now - tLastPinch_) < cfg::FREEZE_MS;
    }

private:
    bool     above_ = false;
    uint32_t tLastPinch_ = 0;
};