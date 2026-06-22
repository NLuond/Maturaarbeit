#pragma once
#include <Arduino.h>
#include "config.h"

class ShakeToggle {
public:
    // gyroSum = |gx|+|gy|+|gz| in Grad/s. Gibt true bei Toggle-Event.
    bool tick(float gyroSum, uint32_t now) {
        // Nach einem Umschalten kurz nichts annehmen
        if (now - tToggle_ < cfg::SHAKE_LOCKOUT_MS) {
            st_ = WAIT_1;
            peakActive_ = false;
            return false;
        }

        // Peak-Erkennung mit Hysterese: eine steigende Flanke = ein Schüttler
        bool risingPeak = false;
        if (!peakActive_ && gyroSum > cfg::SHAKE_ON)  { peakActive_ = true;  risingPeak = true; }
        if ( peakActive_ && gyroSum < cfg::SHAKE_OFF) { peakActive_ = false; }

        switch (st_) {
            case WAIT_1:
                if (risingPeak) { tPeak1_ = now; tLast_ = now; st_ = REFRACT_1; }
                break;

            case REFRACT_1:                                  // ersten Schüttler ausklingen lassen
                if (now - tLast_ >= cfg::SHAKE_REFRACT_MS) st_ = WAIT_2;
                break;

            case WAIT_2:
                if (now - tPeak1_ > cfg::SHAKE_GAP_MAX_MS) { // zu spät -> reset
                    st_ = WAIT_1;
                    break;
                }
                if (risingPeak && (now - tPeak1_) >= cfg::SHAKE_GAP_MIN_MS) {
                    tToggle_ = now;
                    st_ = WAIT_1;
                    peakActive_ = false;
                    return true;                             // zweiter Schüttler im Fenster -> TOGGLE
                }
                break;
        }
        return false;
    }

    void reset() {
        st_ = WAIT_1;
        peakActive_ = false;
    }

private:
    enum State { WAIT_1, REFRACT_1, WAIT_2 };

    State    st_         = WAIT_1;
    bool     peakActive_ = false;
    uint32_t tPeak1_     = 0;    // Zeit des ersten Schüttlers
    uint32_t tLast_      = 0;    // für die Ausklingsperre
    uint32_t tToggle_    = 0;    // Zeit des letzten Umschaltens
};