#pragma once
#include <stdint.h>
#include "config.h"

// Reine Entscheidungslogik: wann gilt eine Erschuetterung als Klick?
//
// Der Detektor kennt den Klassifikator nicht. Er bekommt in tick() eine
// Funktion uebergeben, die er nur dann befragt, wenn sein eigenes Gate offen
// ist - das haelt das Edge-Impulse-SDK aus dieser Datei heraus und macht die
// Klasse ohne Modell und ohne Hardware pruefbar.
class PinchDetector {
public:
    // ml: aufrufbar, liefert true, wenn der Klassifikator einen Pinch sieht.
    template <class MlGate>
    bool tick(float env, float gyroSum, uint32_t now, MlGate&& ml) {
        const bool ready = (now - tLastPinch_) >= cfg::DEBOUNCE_MS;
        const bool calm  = (gyroSum < cfg::PINCH_GYRO_GUARD);

        // Bi-Level-Schwelle: das Gate oeffnet erst ueber ENV_ON und schliesst erst
        // wieder unter ENV_OFF. Dazwischen bleibt es stehen, also kein Flattern.
        if (envGate_) { if (env < cfg::ENV_OFF) envGate_ = false; }
        else          { if (env > cfg::ENV_ON)  envGate_ = true;  }

        // Kurzschlussauswertung: bei geschlossenem Gate laeuft keine Inferenz.
        const bool hot = envGate_ && ml();

        bool fired = false;
        if (hot && !above_) {
            // Eine erkannte Flanke, die hier haengen bleibt, ist im Teleplot
            // sonst unsichtbar - man sieht nur, dass nichts passiert. Die
            // Zaehler trennen "nicht erkannt" von "erkannt, aber verworfen".
            if      (!ready) nDebounce_++;
            else if (!calm)  nGyro_++;
            else { tLastPinch_ = now; fired = true; }
        }
        above_ = hot;
        return fired;
    }

    // Der Zeiger ruht, solange die Erschuetterung des Pinches anliegt - nicht
    // eine feste Zeit lang. Ein kurzer, sauberer Pinch gibt den Cursor damit
    // sofort wieder frei, statt ihn pauschal auszubremsen.
    bool inFreeze(uint32_t now) const {
        return envGate_ && (now - tLastPinch_) < cfg::FREEZE_MAX_MS;
    }

    bool envGate() const { return envGate_; }

    // Diagnose: wie oft eine erkannte Flanke am Entprellfenster bzw. am
    // Gyro-Guard gescheitert ist. Steigt der eine Zaehler beim Doppeltippen,
    // ist die Ursache gefunden.
    uint16_t blockedByDebounce() const { return nDebounce_; }
    uint16_t blockedByGyro()     const { return nGyro_; }

private:
    bool     above_      = false;
    bool     envGate_    = false;
    uint32_t tLastPinch_ = 0;
    uint16_t nDebounce_  = 0;
    uint16_t nGyro_      = 0;
};
