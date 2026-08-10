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
    // Kuerzester Abstand zweier gewerteter Pinches - und damit zugleich der
    // untere Rand des Fensters, in dem der zweite Pinch das Ziehen verriegelt.
    uint32_t debounceMs   = 200;
    uint32_t freezeMaxMs  = 60;      // Notbremse fuer ein haengendes Gate
};

class PinchDetector {
public:
    explicit PinchDetector(const PinchTuning& t = PinchTuning()) : t_(t) {}

    // Sperrt ueber die Entprellung hinaus. Der Controller ruft das nach einem
    // Rechtsklick: die zwei Haptikpulse und der Impuls beim Loesen des Pinch
    // erzeugen sonst eine zweite Flanke, die das Kontextmenue gleich wieder
    // schliesst. tLastPinch_ bleibt unberuehrt, die Entprellung laeuft weiter.
    void holdOff(uint32_t now_ms, uint32_t ms) {
        tBlock_  = now_ms + ms;
        blocked_ = true;
    }

    // relaxed: das Fenster des Doppel-Pinch steht offen. Dann zaehlt allein die
    // Huellkurve - weder Klassifikator noch Gyro-Guard werden gefragt.
    //
    // Beide stehen dem zweiten Pinch systematisch im Weg: der Guard, weil die
    // Hand vom ersten Pinch noch in Bewegung ist, und das Modell, weil sein
    // Fenster (196 ms) beim zweiten Pinch noch den Schwanz des ersten enthaelt.
    // Der Preis ist tragbar: eine Fehlerkennung hier verriegelt ein Ziehen, das
    // ein weiterer Pinch sofort wieder loest - waehrend die strenge Pruefung das
    // Ziehen ueberhaupt nicht zustande kommen liess.
    template <class MlGate>
    bool tick(float env, float gyroSum, uint32_t now_ms, MlGate&& ml,
              bool relaxed = false) {
        // Vorzeichenbehaftete Differenz, damit der Ueberlauf von now_ms die
        // Sperre nicht dauerhaft stehen laesst.
        if (blocked_ && (int32_t)(now_ms - tBlock_) >= 0) blocked_ = false;

        const bool ready = !blocked_ && (now_ms - tLastPinch_) >= t_.debounceMs;
        const bool calm  = relaxed || (gyroSum < t_.gyroGuardDps);

        if (envGate_) { if (env < t_.envOff) envGate_ = false; }
        else          { if (env > t_.envOn)  envGate_ = true;  }

        const bool hot = envGate_ && (relaxed || ml());

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
    uint32_t tBlock_     = 0;
    bool     blocked_    = false;
    uint16_t nDebounce_  = 0;
    uint16_t nGyro_      = 0;
};
