#pragma once
#include <stdint.h>

// Entscheidet, wann eine Erschuetterung als Klick gilt.
//
// Der Klassifikator kommt als Callable in tick() herein und wird nur waehrend
// eines ARMIERUNGSFENSTERS befragt: die steigende Flanke der Huellkurve
// armiert, danach wird armMs lang weitergefragt. Am Gate selbst zu
// klassifizieren waere ein Wettlauf - es oeffnet am ANFANG des Impulses, und da
// enthaelt das ML-Fenster fast nur die Zeit davor. So bleibt ausserdem das
// Edge-Impulse-SDK aus dieser Datei heraus, und die Inferenz laeuft nicht in
// jedem Takt.
//
// Ohne config.h, damit der PC-Test laeuft; die Werte stehen in PinchTuning und
// sind per static_assert an cfg:: gebunden.
struct PinchTuning {
    // Bi-Level-Schwelle auf der Huellkurve: oeffnet ueber envOn, schliesst erst
    // unter envOff (Katsuragawa et al. 2019).
    float    envOn        = 0.035f;
    float    envOff       = 0.020f;

    // Muss laenger sein als das ML-Fenster braucht, um den Impuls aufzunehmen.
    uint32_t armMs        = 250;

    // Nur jeder n-te Takt des Fensters wird klassifiziert; eine Inferenz dauert
    // rund 3 ms von 4785 us Taktbudget.
    uint8_t  mlStride     = 2;

    float    gyroGuardDps = 100.f;   // darueber gilt es als Bewegung, nicht als Pinch

    // Eigener Guard auf der Verdrehung des Unterarms. Er gilt auch im
    // Scroll-Modus, wo gyroGuardDps ausgesetzt ist: die Ein/Aus-Geste dreht
    // genau um diese Achse und erschuettert das Board dabei selbst.
    float    twistGuardDps = 60.f;

    uint32_t debounceMs   = 200;     // kuerzester Abstand zweier gewerteter Pinches
    uint32_t freezeMaxMs  = 60;      // Notbremse fuer ein haengendes Gate
};

class PinchDetector {
public:
    explicit PinchDetector(const PinchTuning& t = PinchTuning()) : t_(t) {}

    // Sperrt ueber die Entprellung hinaus - nach einem Rechtsklick und nach
    // einer langen Vibration. tLastPinch_ bleibt unberuehrt, die Entprellung
    // laeuft daneben weiter.
    void holdOff(uint32_t now_ms, uint32_t ms) {
        tBlock_  = now_ms + ms;
        blocked_ = true;
    }

    // twistDps: Drehrate um die Unterarmachse (TwistGuard::rateDps()).
    //
    // relaxed: im Scroll-Modus zaehlt fast allein die Huellkurve - weder
    // Klassifikator noch Gyro-Guard werden gefragt. Der Rechtsklick faellt dort
    // per Definition in eine Armbewegung, und der Guard verwuerfe ihn genau
    // dann, wenn er gebraucht wird. Der Dreh-Guard bleibt als einziger stehen:
    // gescrollt wird durch Neigen und Schwenken, nicht durch Verdrehen.
    template <class MlGate>
    bool tick(float env, float gyroSum, float twistDps, uint32_t now_ms, MlGate&& ml,
              bool relaxed = false) {
        // Vorzeichenbehaftete Differenz, damit der Ueberlauf von now_ms die
        // Sperre nicht dauerhaft stehen laesst.
        if (blocked_ && (int32_t)(now_ms - tBlock_) >= 0) blocked_ = false;

        const bool ready   = !blocked_ && (now_ms - tLastPinch_) >= t_.debounceMs;
        const bool calm    = relaxed || (gyroSum < t_.gyroGuardDps);
        const bool steady  = twistDps < t_.twistGuardDps;

        if (envGate_) { if (env < t_.envOff) envGate_ = false; }
        else if (env > t_.envOn) {
            envGate_ = true;
            tArm_    = now_ms;      // steigende Flanke armiert das Fenster
            armed_   = true;
            nSkip_   = t_.mlStride; // im naechsten Schritt sofort klassifizieren

            // Eine NEUE Erschuetterung setzt das Urteil zurueck: das Modell muss
            // sie eigens bestaetigen, und die Flanke darf wieder zaehlen. Gegen
            // ein Flattern der Huellkurve schuetzt die Entprellung.
            mlHot_  = false;
            wasHot_ = false;
        }

        if (armed_ && (now_ms - tArm_) >= t_.armMs) { armed_ = false; mlHot_ = false; }

        // Nur jeder n-te Takt kostet eine Inferenz; dazwischen gilt das letzte
        // Ergebnis weiter, damit die Flanke nicht zwischen zwei Takten verloren
        // geht.
        if (armed_) {
            if (++nSkip_ >= t_.mlStride) {
                nSkip_ = 0;
                mlHot_ = relaxed || ml();
            }
        }

        const bool hot = armed_ && mlHot_;

        bool fired = false;
        if (hot && !wasHot_) {
            // Die Zaehler trennen "nicht erkannt" von "erkannt, aber verworfen";
            // eine hier haengende Flanke waere im Teleplot sonst unsichtbar.
            if      (!ready)   nDebounce_++;
            else if (!calm)    nGyro_++;
            else if (!steady)  nTwist_++;
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
    bool     armed()             const { return armed_; }   // Teleplot-Kanal arm
    uint16_t blockedByDebounce() const { return nDebounce_; }
    uint16_t blockedByGyro()     const { return nGyro_; }
    uint16_t blockedByTwist()    const { return nTwist_; }

private:
    PinchTuning t_;
    bool     wasHot_     = false;   // gewertet wird nur die steigende Flanke
    bool     envGate_    = false;
    bool     armed_      = false;   // Klassifikator wird gerade befragt
    bool     mlHot_      = false;   // letztes Ergebnis, gilt zwischen zwei Inferenzen
    uint8_t  nSkip_      = 0;
    uint32_t tArm_       = 0;
    uint32_t tLastPinch_ = 0;
    uint32_t tBlock_     = 0;
    bool     blocked_    = false;
    uint16_t nDebounce_  = 0;
    uint16_t nGyro_      = 0;
    uint16_t nTwist_     = 0;
};
