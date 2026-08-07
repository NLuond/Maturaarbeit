#pragma once
#include <stdint.h>

// Entscheidet, wann das Geraet in den Ruhezustand geht - nicht, wie das
// ausgefuehrt wird. Die Hardware-Seite (IMU-Rate, Funk, suspendLoop) liegt
// im Controller und in main.cpp.
//
// Kein #include "config.h" und kein <Arduino.h>: dieser Header muss sich
// ohne Toolchain uebersetzen lassen (test/test_sleep_policy.cpp). Die Werte
// stehen deshalb in SleepTuning - dieselbe bewusste Ausnahme wie bei
// TwistTuning und TwistGuardTuning.
struct SleepTuning {
    // Bewegung wird an der Drehrate gemessen und nicht an der
    // Beschleunigung: eine ruhig gehaltene, aber getragene Hand soll nicht
    // als Ruhe zaehlen, und die Erdbeschleunigung liegt immer an.
    float    stillDps   = 20.f;
    uint32_t sleepAfter = 60000;   // ms Ruhe bis zum Schlaf

    // Sperre nach dem Aufwachen, und zugleich das Fenster, in dem Madgwick
    // mit cfg::MADGWICK_BETA_FAST laeuft. Die beiden Zahlen gehoeren
    // miteinander gerechnet: Beta 0.5 rad/s sind rund 28.6 Grad/s
    // Korrekturgeschwindigkeit, die frueheren 300 ms erlaubten also nur rund
    // 8.6 Grad Nachfuehrung - viel zu wenig fuer den Zweck, denn nach einem
    // Schlaf, waehrend dessen der Arm langsam gedreht wurde, kann die
    // Lageschaetzung um ein Vielfaches danebenliegen. 1500 ms ergeben rund
    // 43 Grad. Die Zeit kostet nichts: der Benutzer hebt in dieser Sekunde
    // ohnehin gerade den Arm.
    uint32_t settleMs   = 1500;
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
        // Eingeschaltet wird nie geschlafen. Sonst verschwaende die Maus
        // mitten im Gebrauch, waehrend man den Cursor nur ruhig auf einem
        // Ziel haelt - dort ist gyroSum naemlich klein.
        if (mouseOn || gyroSum >= t_.stillDps) tQuiet_ = now_ms;

        if (settling_ && (now_ms - tWake_) >= t_.settleMs) {
            settling_ = false;
            return SleepEvent::Settled;
        }

        if (!wants_ && !settling_ && (now_ms - tQuiet_) >= t_.sleepAfter) {
            wants_ = true;
            return SleepEvent::GoToSleep;
        }
        return SleepEvent::None;
    }

    // Nach dem Aufwachen aufrufen. Startet das Einschwingfenster und laesst
    // den Ruhe-Zeitgeber neu anlaufen - ohne das schliefe das Geraet
    // unmittelbar wieder ein, weil tQuiet_ noch aus der Zeit vor dem Schlaf
    // stammt.
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
