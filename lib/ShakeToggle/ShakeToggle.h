#pragma once
#include <Arduino.h>
#include "config.h"

class ShakeToggle {
public:
    bool tick(float gyroSum, uint32_t now) {
        if (now - tToggle_ < cfg::SHAKE_LOCKOUT_MS) {
            st_ = WAIT_1;
            peakActive_ = false;
            return false;
        }


        bool risingPeak = false;
        if (!peakActive_ && gyroSum > cfg::SHAKE_ON)  { peakActive_ = true;  risingPeak = true; }
        if ( peakActive_ && gyroSum < cfg::SHAKE_OFF) { peakActive_ = false; }

        switch (st_) {
            case WAIT_1:
                if (risingPeak) { tPeak1_ = now; tLast_ = now; st_ = REFRACT_1; }
                break;

            case REFRACT_1:
                if (now - tLast_ >= cfg::SHAKE_REFRACT_MS) st_ = WAIT_2;
                break;

            case WAIT_2:
                if (now - tPeak1_ > cfg::SHAKE_GAP_MAX_MS) {
                    st_ = WAIT_1;
                    break;
                }
                if (risingPeak && (now - tPeak1_) >= cfg::SHAKE_GAP_MIN_MS) {
                    tToggle_ = now;
                    st_ = WAIT_1;
                    peakActive_ = false;
                    return true;
                }
                break;
        }
        return false;
    }

private:
    enum State { WAIT_1, REFRACT_1, WAIT_2 };

    State    st_         = WAIT_1;
    bool     peakActive_ = false;
    uint32_t tPeak1_     = 0;
    uint32_t tLast_      = 0;  
    uint32_t tToggle_    = 0; 
};