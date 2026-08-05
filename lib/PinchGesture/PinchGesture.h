#pragma once
#include <Arduino.h>
#include "config.h"

enum class PinchEvent : uint8_t { None = 0, Single = 1, Double = 2 };

// Eine Ebene ueber dem Impuls-Detektor: unterscheidet einzelne von doppelten
// Pinches.
//
// Der einzelne Pinch wird bis zum Ablauf des Doppel-Fensters zurueckgehalten.
// Das kostet jeden gewoehnlichen Klick die Fensterlaenge als Verzoegerung -
// deshalb ist DOUBLE_MS bewusst knapp ueber DEBOUNCE_MS gelegt und nicht
// grosszuegig.
//
// Vorher wurde der einzelne Pinch sofort gemeldet und das Doppel-Ereignis kam
// hinterher: schneller, aber jeder Doppel-Pinch loeste zuerst einen Klick aus.
// Beim Ziehen ist das keine Kleinigkeit - der Klick landet auf dem Objekt,
// bevor das Ziehen ueberhaupt beginnt. Deshalb jetzt andersherum.
class PinchGesture {
public:
    PinchEvent update(bool pinched, uint32_t now_ms) {
        // Zuerst das abgelaufene Fenster: kam kein zweiter Pinch, gilt der
        // erste rueckwirkend als einzelner.
        if (armed_ && (now_ms - tFirst_) > cfg::DOUBLE_MS) {
            armed_   = false;
            pending_ = true;
        }

        if (pinched) {
            if (armed_) {              // zweiter innerhalb des Fensters
                armed_   = false;
                pending_ = false;      // der erste geht im Doppel-Ereignis auf
                return PinchEvent::Double;
            }
            tFirst_ = now_ms;
            armed_  = true;
            // Ein in diesem Takt faelliger einzelner Pinch gehoert noch zur
            // vorherigen Geste und geht unten hinaus, bevor die neue laeuft.
        }

        if (pending_) {
            pending_ = false;
            return PinchEvent::Single;
        }
        return PinchEvent::None;
    }

private:
    bool     armed_   = false;
    bool     pending_ = false;
    uint32_t tFirst_  = 0;
};
