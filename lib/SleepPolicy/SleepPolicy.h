#pragma once
#include <stdint.h>

// Entscheidet, wann das Geraet in den Ruhezustand geht - nicht, wie das
// ausgefuehrt wird. Die Hardware-Seite liegt im Controller und in main.cpp.
//
// Ohne config.h und ohne Arduino.h, damit der PC-Test laeuft.
struct SleepTuning {
    // Bewegung an der Drehrate gemessen, nicht an der Beschleunigung: eine
    // ruhig gehaltene, aber getragene Hand liegt konstant bei 1 g und soll
    // nicht als Ruhe zaehlen.
    float    stillDps     = 20.f;    // Grad/s
    uint32_t sleepAfterMs = 60000;   // ms Ruhe bis zum Schlaf

    // Einschwingfenster nach dem Aufwachen, in dem Madgwick mit erhoehtem Beta
    // laeuft: 28.6 Grad/s mal 1500 ms sind rund 43 Grad Nachfuehrung.
    uint32_t settleMs     = 1500;
};

enum class SleepEvent : uint8_t {
    None,
    GoToSleep,   // Ruhezeit erreicht - Hardware herunterfahren
    Settled      // Einschwingfenster vorbei - Madgwick-Beta zurueckstellen
};

class SleepPolicy {
public:
    explicit SleepPolicy(const SleepTuning& t = SleepTuning()) : t_(t) {}

    SleepEvent tick(bool mouseOn, float gyroSum, uint32_t now_ms) {
        // Eingeschaltet wird nie geschlafen: haelt man den Cursor ruhig auf
        // einem Ziel, ist gyroSum klein, und die Maus verschwaende mitten im
        // Gebrauch.
        if (mouseOn || gyroSum >= t_.stillDps) tQuiet_ = now_ms;

        if (settling_ && (now_ms - tWake_) >= t_.settleMs) {
            settling_ = false;
            return SleepEvent::Settled;
        }

        if (!wants_ && !settling_ && (now_ms - tQuiet_) >= t_.sleepAfterMs) {
            wants_ = true;
            return SleepEvent::GoToSleep;
        }
        return SleepEvent::None;
    }

    // Nach dem Aufwachen aufrufen. tQuiet_ muss mit zuruecklaufen, sonst
    // schliefe das Geraet unmittelbar wieder ein.
    void wake(uint32_t now_ms) {
        wants_    = false;
        settling_ = true;
        tWake_    = now_ms;
        tQuiet_   = now_ms;
    }

    // Waehrend des Einschwingens ist die Lageschaetzung noch nicht wieder
    // eingerastet; die Drehgeste haengt an ihr und bleibt so lange gesperrt.
    bool settling()   const { return settling_; }
    bool wantsSleep() const { return wants_; }

private:
    SleepTuning t_;
    uint32_t tQuiet_   = 0;
    uint32_t tWake_    = 0;
    bool     wants_    = false;
    bool     settling_ = false;
};
