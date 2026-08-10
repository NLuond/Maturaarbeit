#pragma once
#include <stdint.h>

// Entscheidet, wann eine Erschuetterung als Klick gilt.
//
// Der Klassifikator kommt als Callable in tick() herein und wird per
// Kurzschlussauswertung nur bei offenem Gate befragt. Damit laeuft die Inferenz
// nicht in jedem Takt, und das Edge-Impulse-SDK bleibt aus dieser Datei heraus.
//
// Ohne config.h, damit der PC-Test laeuft; die Werte stehen in PinchTuning und
// sind per static_assert an cfg:: gebunden.
struct PinchTuning {
    // Bi-Level-Schwelle auf der Huellkurve: oeffnet ueber envOn, schliesst erst
    // unter envOff (Katsuragawa et al. 2019).
    float    envOn        = 0.035f;
    float    envOff       = 0.020f;

    float    gyroGuardDps = 100.f;   // darueber gilt es als Bewegung, nicht als Pinch
    uint32_t debounceMs   = 180;     // kuerzester Abstand zweier Klicks
    uint32_t freezeMaxMs  = 60;      // Notbremse fuer ein haengendes Gate
};

class PinchDetector {
public:
    explicit PinchDetector(const PinchTuning& t = PinchTuning()) : t_(t) {}

    template <class MlGate>
    bool tick(float env, float gyroSum, uint32_t now_ms, MlGate&& ml) {
        const bool ready = (now_ms - tLastPinch_) >= t_.debounceMs;
        const bool calm  = (gyroSum < t_.gyroGuardDps);

        if (envGate_) { if (env < t_.envOff) envGate_ = false; }
        else          { if (env > t_.envOn)  envGate_ = true;  }

        const bool hot = envGate_ && ml();

        bool fired = false;
        if (hot && !wasHot_) {
            // Die Zaehler trennen "nicht erkannt" von "erkannt, aber verworfen";
            // eine hier haengende Flanke waere im Teleplot sonst unsichtbar.
            if      (!ready) nDebounce_++;
            else if (!calm)  nGyro_++;
            else { tLastPinch_ = now_ms; fired = true; }
        }
        wasHot_ = hot;
        return fired;
    }

    // Der Zeiger ruht, solange die Erschuetterung anliegt - nicht eine feste
    // Zeit lang. Ein kurzer, sauberer Pinch gibt den Cursor sofort wieder frei.
    bool inFreeze(uint32_t now_ms) const {
        return envGate_ && (now_ms - tLastPinch_) < t_.freezeMaxMs;
    }

    bool     envGate()           const { return envGate_; }
    uint16_t blockedByDebounce() const { return nDebounce_; }
    uint16_t blockedByGyro()     const { return nGyro_; }

private:
    PinchTuning t_;
    bool     wasHot_     = false;   // gewertet wird nur die steigende Flanke
    bool     envGate_    = false;
    uint32_t tLastPinch_ = 0;
    uint16_t nDebounce_  = 0;
    uint16_t nGyro_      = 0;
};
