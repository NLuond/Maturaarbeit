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

    // Ruhe bis zum Abschalten aus dem eingeschalteten Zustand. Deutlich laenger
    // als sleepAfterMs: eine Pause im Gebrauch darf die Maus nicht wegnehmen,
    // eine abgelegte Maus soll aber nicht die Nacht durchlaufen.
    uint32_t offAfterMs   = 300000;  // ms
};

enum class SleepEvent : uint8_t {
    None,
    PowerOff,    // lange Ruhe im Gebrauch - Maus ausschalten, danach BEREIT
    GoToSleep    // Ruhezeit erreicht - Hardware herunterfahren
};

class SleepPolicy {
public:
    explicit SleepPolicy(const SleepTuning& t = SleepTuning()) : t_(t) {}

    SleepEvent tick(bool mouseOn, float gyroSum, uint32_t now_ms) {
        if (gyroSum >= t_.stillDps) tQuiet_ = now_ms;

        // Eingeschaltet wird nie direkt geschlafen: haelt man den Cursor ruhig
        // auf einem Ziel, ist gyroSum klein, und die Maus verschwaende mitten im
        // Gebrauch. Nach offAfterMs geht sie stattdessen aus und faellt damit
        // nach BEREIT, wo die kurze Ruhezeit uebernimmt.
        if (mouseOn) {
            if ((now_ms - tQuiet_) < t_.offAfterMs) return SleepEvent::None;
            // Neu anlaufen lassen: sonst folgte GoToSleep im selben Takt, und
            // der lange Ein/Aus-Impuls braucht Ticks, um wieder abzuschalten.
            tQuiet_ = now_ms;
            return SleepEvent::PowerOff;
        }

        if (!wants_ && (now_ms - tQuiet_) >= t_.sleepAfterMs) {
            wants_ = true;
            return SleepEvent::GoToSleep;
        }
        return SleepEvent::None;
    }

    // Nach dem Aufwachen aufrufen. tQuiet_ muss mit zuruecklaufen, sonst
    // schliefe das Geraet unmittelbar wieder ein.
    void wake(uint32_t now_ms) {
        wants_  = false;
        tQuiet_ = now_ms;
    }

    bool wantsSleep() const { return wants_; }

private:
    SleepTuning t_;
    uint32_t tQuiet_ = 0;
    bool     wants_  = false;
};
