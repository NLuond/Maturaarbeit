// Test der Schlaf-Entscheidung. Laeuft auf dem PC - SleepPolicy haengt
// bewusst an keiner Hardware.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/SleepPolicy -o build/sleep.exe test/test_sleep_policy.cpp && ./build/sleep.exe
//
#include "SleepPolicy.h"
#include <cstdio>

static int failures = 0;
static int checks   = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        checks++;                                                           \
        if (!(cond)) {                                                      \
            failures++;                                                     \
            std::printf("  FEHLER Zeile %d: %s\n", __LINE__, (msg));        \
        }                                                                   \
    } while (0)

// Laesst die Zeit in 10-ms-Schritten laufen und meldet, welches Ereignis
// dabei kam.
static SleepEvent run(SleepPolicy& p, bool mouseOn, float gyroSum,
                      uint32_t& now, uint32_t ms) {
    SleepEvent seen = SleepEvent::None;
    for (uint32_t i = 0; i < ms; i += 10) {
        const SleepEvent e = p.tick(mouseOn, gyroSum, now);
        if (e != SleepEvent::None) seen = e;
        now += 10;
    }
    return seen;
}

static void test_sleepsAfterQuietTime() {
    SleepPolicy p;
    uint32_t now = 1000;
    CHECK(run(p, false, 0.f, now, 30000) == SleepEvent::None,
          "schlaeft schon nach 30 s");
    CHECK(run(p, false, 0.f, now, 35000) == SleepEvent::GoToSleep,
          "schlaeft nicht nach 60 s Ruhe");
}

// Die wichtigste Eigenschaft: waehrend die Maus benutzt wird, darf sie nicht
// verschwinden - auch nicht, wenn man den Cursor minutenlang ruhig auf einem
// Ziel haelt.
static void test_neverSleepsWhileMouseOn() {
    SleepPolicy p;
    uint32_t now = 1000;
    CHECK(run(p, true, 0.f, now, 300000) == SleepEvent::None,
          "schlaeft ein, obwohl die Maus eingeschaltet ist");
}

static void test_motionResetsTheTimer() {
    SleepPolicy p;
    uint32_t now = 1000;
    run(p, false, 0.f,   now, 50000);   // fast eingeschlafen
    run(p, false, 200.f, now,   500);   // Bewegung
    CHECK(run(p, false, 0.f, now, 30000) == SleepEvent::None,
          "die Bewegung hat den Zeitgeber nicht zurueckgesetzt");
    CHECK(run(p, false, 0.f, now, 35000) == SleepEvent::GoToSleep,
          "schlaeft danach gar nicht mehr");
}

// Eine ruhig gehaltene, aber getragene Hand zaehlt nicht als Ruhe.
static void test_smallMotionCountsAsQuiet() {
    SleepPolicy p;
    uint32_t now = 1000;
    // 5 Grad/s liegt unter stillDps (20) und darf den Zeitgeber nicht halten.
    CHECK(run(p, false, 5.f, now, 65000) == SleepEvent::GoToSleep,
          "kleines Rauschen verhindert das Einschlafen");
}

static void test_wantsSleepLatchesUntilWake() {
    SleepPolicy p;
    uint32_t now = 1000;
    CHECK(!p.wantsSleep(), "will schon vor dem Ereignis schlafen");
    run(p, false, 0.f, now, 65000);
    CHECK(p.wantsSleep(), "meldet den Schlafwunsch nicht");
    p.wake(now);
    CHECK(!p.wantsSleep(), "der Schlafwunsch bleibt nach dem Aufwachen stehen");
}

static void test_goToSleepComesOnce() {
    SleepPolicy p;
    uint32_t now = 1000;
    CHECK(run(p, false, 0.f, now, 65000) == SleepEvent::GoToSleep, "kein GoToSleep");
    CHECK(run(p, false, 0.f, now, 65000) == SleepEvent::None,
          "GoToSleep kommt mehrfach ohne zwischenzeitliches wake()");
}

static void test_settlingWindowAfterWake() {
    SleepPolicy p;
    uint32_t now = 1000;
    run(p, false, 0.f, now, 65000);
    p.wake(now);
    CHECK(p.settling(), "nach dem Aufwachen wird nicht eingeschwungen");
    CHECK(run(p, false, 100.f, now, 200) == SleepEvent::None,
          "Settled kommt zu frueh");
    CHECK(p.settling(), "das Einschwingfenster endet zu frueh");
    CHECK(run(p, false, 100.f, now, 200) == SleepEvent::Settled,
          "Settled kommt gar nicht");
    CHECK(!p.settling(), "settling() bleibt nach Settled stehen");
}

static void test_settledComesOnce() {
    SleepPolicy p;
    uint32_t now = 1000;
    run(p, false, 0.f, now, 65000);
    p.wake(now);
    run(p, false, 100.f, now, 400);                       // Settled
    CHECK(run(p, false, 100.f, now, 400) == SleepEvent::None,
          "Settled kommt mehrfach");
}

// Nach dem Aufwachen laeuft der Zeitgeber neu an - sonst schliefe das Geraet
// unmittelbar nach dem Wecken wieder ein.
//
// Geprueft wird "kein GoToSleep" und nicht "None": in den ersten 300 ms nach
// dem Aufwachen kommt zwangslaeufig das Settled-Ereignis, und run() liefert
// das zuletzt gesehene zurueck.
static void test_timerRestartsAfterWake() {
    SleepPolicy p;
    uint32_t now = 1000;
    run(p, false, 0.f, now, 65000);
    p.wake(now);
    CHECK(run(p, false, 0.f, now, 30000) != SleepEvent::GoToSleep,
          "schlaeft direkt nach dem Aufwachen wieder ein");
}

int main() {
    test_sleepsAfterQuietTime();
    test_neverSleepsWhileMouseOn();
    test_motionResetsTheTimer();
    test_smallMotionCountsAsQuiet();
    test_wantsSleepLatchesUntilWake();
    test_goToSleepComesOnce();
    test_settlingWindowAfterWake();
    test_settledComesOnce();
    test_timerRestartsAfterWake();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
