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

// Wie run(), meldet aber, ob ein bestimmtes Ereignis dabei war: seit dem
// Auto-Aus koennen in einem Fenster zwei verschiedene auftreten, und run()
// liefert nur das zuletzt gesehene.
static bool saw(SleepPolicy& p, bool mouseOn, float gyroSum,
                uint32_t& now, uint32_t ms, SleepEvent want) {
    bool found = false;
    for (uint32_t i = 0; i < ms; i += 10) {
        if (p.tick(mouseOn, gyroSum, now) == want) found = true;
        now += 10;
    }
    return found;
}

static void test_sleepsAfterQuietTime() {
    SleepPolicy p;
    uint32_t now = 1000;
    CHECK(run(p, false, 0.f, now, 30000) == SleepEvent::None,
          "schlaeft schon nach 30 s");
    CHECK(run(p, false, 0.f, now, 35000) == SleepEvent::GoToSleep,
          "schlaeft nicht nach 60 s Ruhe");
}

// Die wichtigste Eigenschaft: aus dem eingeschalteten Zustand wird nie direkt
// geschlafen - auch nicht, wenn man den Cursor minutenlang ruhig auf einem Ziel
// haelt. Der Weg fuehrt immer ueber PowerOff und damit ueber BEREIT.
static void test_neverSleepsWhileMouseOn() {
    SleepPolicy p;
    uint32_t now = 1000;
    CHECK(!saw(p, true, 0.f, now, 600000, SleepEvent::GoToSleep),
          "schlaeft ein, obwohl die Maus eingeschaltet ist");
}

// Wer die Maus eingeschaltet ablegt, soll sie nicht die ganze Nacht mit vollem
// Takt und Funk laufen lassen.
static void test_switchesOffAfterLongStillness() {
    SleepPolicy p;
    uint32_t now = 1000;
    CHECK(!saw(p, true, 0.f, now, 240000, SleepEvent::PowerOff),
          "schaltet schon vor offAfterMs ab");
    CHECK(saw(p, true, 0.f, now, 70000, SleepEvent::PowerOff),
          "schaltet nach 5 min Ruhe nicht ab");
}

static void test_motionKeepsItOn() {
    SleepPolicy p;
    uint32_t now = 1000;
    CHECK(!saw(p, true, 200.f, now, 600000, SleepEvent::PowerOff),
          "schaltet trotz Bewegung ab");
}

// Nach dem Abschalten laeuft die Ruhezeit neu an. Ohne das ginge das Geraet im
// selben Takt schlafen, und Haptic::update() wuerde nie wieder aufgerufen - der
// Motor des langen Ein/Aus-Impulses bliebe an.
static void test_sleepFollowsPowerOffWithDelay() {
    SleepPolicy p;
    uint32_t now = 1000;
    run(p, true, 0.f, now, 301000);   // PowerOff bei 300000, danach noch 1 s
    CHECK(!saw(p, false, 0.f, now, 50000, SleepEvent::GoToSleep),
          "schlaeft sofort nach dem Abschalten, der Haptik-Impuls bricht ab");
    CHECK(saw(p, false, 0.f, now, 15000, SleepEvent::GoToSleep),
          "schlaeft nach dem Abschalten gar nicht mehr");
}

static void test_powerOffComesOnce() {
    SleepPolicy p;
    uint32_t now = 1000;
    CHECK(saw(p, true, 0.f, now, 310000, SleepEvent::PowerOff), "kein PowerOff");
    CHECK(!saw(p, true, 0.f, now, 240000, SleepEvent::PowerOff),
          "PowerOff wiederholt sich vor Ablauf der neuen Ruhezeit");
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

// Nach dem Aufwachen laeuft der Zeitgeber neu an - sonst schliefe das Geraet
// unmittelbar nach dem Wecken wieder ein.
static void test_timerRestartsAfterWake() {
    SleepPolicy p;
    uint32_t now = 1000;
    run(p, false, 0.f, now, 65000);
    p.wake(now);
    CHECK(run(p, false, 0.f, now, 30000) == SleepEvent::None,
          "schlaeft direkt nach dem Aufwachen wieder ein");
    CHECK(run(p, false, 0.f, now, 35000) == SleepEvent::GoToSleep,
          "schlaeft nach dem Aufwachen gar nicht mehr");
}

int main() {
    test_sleepsAfterQuietTime();
    test_neverSleepsWhileMouseOn();
    test_switchesOffAfterLongStillness();
    test_motionKeepsItOn();
    test_sleepFollowsPowerOffWithDelay();
    test_powerOffComesOnce();
    test_motionResetsTheTimer();
    test_smallMotionCountsAsQuiet();
    test_wantsSleepLatchesUntilWake();
    test_goToSleepComesOnce();
    test_timerRestartsAfterWake();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
