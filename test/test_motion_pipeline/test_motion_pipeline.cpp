// Test des Rueckstaus zwischen Zeiger und HID. Laeuft auf dem PC -
// MotionPipeline haengt an keiner Hardware und bindet weder Arduino.h noch
// config.h ein.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/MotionPipeline -I lib/ScrollWheel -o "$env:TEMP\motion.exe" test/test_motion_pipeline/test_motion_pipeline.cpp
//   & "$env:TEMP\motion.exe"
//
#include "MotionPipeline.h"
#include <cstdio>
#include <cmath>

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

static const MotionTuning kT{};
static const uint32_t kIntervalUs = 7500;

// Faengt auf, was die Pipeline ans HID gaebe. accept schaltet die Gegenstelle
// auf "Warteschlange voll" - das ist der Fall, um den es hier vor allem geht.
struct Sink {
    int   packets = 0;
    long  sumX    = 0, sumY = 0;
    int   ticks   = 0;
    bool  accept  = true;

    // Lambdas statt Methoden, damit run() sie als Callable nimmt.
    auto sender()   { return [this](int8_t dx, int8_t dy) {
        if (!accept) return false;
        packets++; sumX += dx; sumY += dy; return true;
    }; }
    auto scroller() { return [this](int8_t n) { ticks += n; }; }
};

// Ein Takt mit abgelaufenem Sendefenster.
static void tick(MotionPipeline& m, Sink& s, MotionTarget target,
                 float dx, float dy, uint32_t& now_us, uint32_t& now_ms) {
    m.run(target, dx, dy, now_us, now_ms, kIntervalUs, s.sender(), s.scroller());
    now_us += kIntervalUs;
    now_ms += 5;
}


// --- Cursor ------------------------------------------------------------

static void test_movementReachesTheHid() {
    MotionPipeline m;  Sink s;
    uint32_t us = 100000, ms = 1000;
    tick(m, s, MotionTarget::Cursor, 10.f, -4.f, us, ms);
    CHECK(s.packets == 1, "der erste Takt sendet kein Paket");
    CHECK(s.sumX == 10 && s.sumY == -4, "die Pixel kommen veraendert an");
}

// Bruchteile eines Pixels duerfen nicht verlorengehen, sonst bewegt sich der
// Cursor bei langsamer Hand gar nicht.
static void test_subPixelMotionAccumulates() {
    MotionPipeline m;  Sink s;
    uint32_t us = 100000, ms = 1000;
    for (int i = 0; i < 10; i++) tick(m, s, MotionTarget::Cursor, 0.4f, 0.f, us, ms);
    CHECK(s.sumX == 4, "aufsummierte Bruchteile ergeben nicht vier Pixel");
}

static void test_outputIsThrottled() {
    MotionPipeline m;  Sink s;
    const uint32_t us = 100000, ms = 1000;
    m.run(MotionTarget::Cursor, 20.f, 0.f, us, ms, kIntervalUs, s.sender(), s.scroller());
    m.run(MotionTarget::Cursor, 20.f, 0.f, us + kIntervalUs / 2, ms, kIntervalUs,
          s.sender(), s.scroller());
    CHECK(s.packets == 1, "es wird mehrfach im selben Sendefenster gesendet");
}

// Ein Bericht traegt hoechstens 127 px je Achse.
static void test_largeBurstIsSplitAcrossReports() {
    MotionPipeline m;  Sink s;
    uint32_t us = 100000, ms = 1000;
    tick(m, s, MotionTarget::Cursor, 300.f, 0.f, us, ms);
    CHECK(s.packets == 3, "ein grosser Weg wird nicht auf mehrere Pakete verteilt");
    CHECK(s.sumX == 300, "beim Aufteilen gehen Pixel verloren");
}

static void test_reportsPerTickAreCapped() {
    MotionPipeline m;  Sink s;
    uint32_t us = 100000, ms = 1000;
    tick(m, s, MotionTarget::Cursor, 1000.f, 0.f, us, ms);
    CHECK(s.packets == kT.maxReports, "die Paketzahl je Takt wird nicht begrenzt");
    CHECK(m.pendingX() > 0.f, "der Rest wird nicht fuer den naechsten Takt behalten");
}

// --- Volle Warteschlange -----------------------------------------------

static void test_rejectedPacketKeepsTheMotion() {
    MotionPipeline m;  Sink s;
    uint32_t us = 100000, ms = 1000;
    s.accept = false;
    tick(m, s, MotionTarget::Cursor, 50.f, 0.f, us, ms);
    CHECK(s.packets == 0, "ein abgelehntes Paket zaehlt als gesendet");
    CHECK(std::fabs(m.pendingX() - 50.f) < 1e-3f,
          "abgelehnte Bewegung wird trotzdem abgezogen");

    s.accept = true;
    tick(m, s, MotionTarget::Cursor, 0.f, 0.f, us, ms);
    CHECK(s.sumX == 50, "die aufgestaute Bewegung kommt nicht nach");
}

static void test_rejectionsAreCounted() {
    MotionPipeline m;  Sink s;
    uint32_t us = 100000, ms = 1000;
    s.accept = false;
    tick(m, s, MotionTarget::Cursor, 50.f, 0.f, us, ms);
    tick(m, s, MotionTarget::Cursor, 50.f, 0.f, us, ms);
    CHECK(m.rejected() == 2, "abgelehnte Pakete werden nicht gezaehlt");
}

// Nimmt die Gegenstelle minutenlang nichts an, darf der Rueckstau nicht
// mitwachsen - sonst schiesst der Cursor beim Verbinden quer ueber den Schirm.
static void test_backlogIsCapped() {
    MotionPipeline m;  Sink s;
    uint32_t us = 100000, ms = 1000;
    s.accept = false;
    for (int i = 0; i < 500; i++) tick(m, s, MotionTarget::Cursor, 100.f, -100.f, us, ms);
    CHECK(m.pendingX() <=  kT.backlogMax + 100.f, "der Rueckstau waechst unbegrenzt");
    CHECK(m.pendingY() >= -kT.backlogMax - 100.f,
          "der Rueckstau waechst in der Gegenrichtung unbegrenzt");
}

// --- Rad ---------------------------------------------------------------

static void test_wheelTargetBypassesTheCursor() {
    MotionPipeline m;  Sink s;
    uint32_t us = 100000, ms = 1000;
    for (int i = 0; i < 20; i++) tick(m, s, MotionTarget::Wheel, 5.f, 30.f, us, ms);
    CHECK(s.ticks != 0, "im Scroll-Modus kommt kein Radschritt heraus");
    CHECK(s.packets == 0, "im Scroll-Modus bewegt sich zusaetzlich der Cursor");
}

// Der Rest des Rades darf nicht in die naechste Scroll-Sitzung ueberschwappen.
static void test_leavingTheWheelDropsItsRest() {
    MotionPipeline m;  Sink s;
    uint32_t us = 100000, ms = 1000;
    m.run(MotionTarget::Wheel, 0.f, 30.f, us, ms, kIntervalUs, s.sender(), s.scroller());
    CHECK(m.pendingWheel() > 0.f, "gar kein Rest aufgelaufen");
    tick(m, s, MotionTarget::Cursor, 0.f, 0.f, us, ms);
    CHECK(std::fabs(m.pendingWheel()) < 1e-6f, "der Rest des Rades bleibt stehen");
}

// --- Kein Ziel ---------------------------------------------------------

static void test_noTargetClearsEverything() {
    MotionPipeline m;  Sink s;
    uint32_t us = 100000, ms = 1000;
    s.accept = false;
    tick(m, s, MotionTarget::Cursor, 60.f, 60.f, us, ms);
    CHECK(m.pendingX() > 0.f, "gar kein Rueckstau aufgelaufen");

    s.accept = true;
    tick(m, s, MotionTarget::None, 99.f, 99.f, us, ms);
    CHECK(s.packets == 0, "ohne Ziel wird gesendet");
    CHECK(std::fabs(m.pendingX()) < 1e-6f, "ohne Ziel bleibt der Rueckstau stehen");
}

static void test_resetClearsCursorAndWheel() {
    MotionPipeline m;  Sink s;
    uint32_t us = 100000, ms = 1000;
    s.accept = false;
    tick(m, s, MotionTarget::Cursor, 60.f, 60.f, us, ms);
    m.run(MotionTarget::Wheel, 0.f, 30.f, us, ms, kIntervalUs, s.sender(), s.scroller());
    m.reset();
    CHECK(std::fabs(m.pendingX()) < 1e-6f, "reset() laesst den Cursor-Rueckstau stehen");
    CHECK(std::fabs(m.pendingWheel()) < 1e-6f, "reset() laesst den Rest des Rades stehen");
}

int main() {
    test_movementReachesTheHid();
    test_subPixelMotionAccumulates();
    test_outputIsThrottled();
    test_largeBurstIsSplitAcrossReports();
    test_reportsPerTickAreCapped();
    test_rejectedPacketKeepsTheMotion();
    test_rejectionsAreCounted();
    test_backlogIsCapped();
    test_wheelTargetBypassesTheCursor();
    test_leavingTheWheelDropsItsRest();
    test_noTargetClearsEverything();
    test_resetClearsCursorAndWheel();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
