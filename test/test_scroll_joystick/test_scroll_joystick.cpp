// Test des Scroll-Joysticks. Laeuft auf dem PC - ScrollJoystick haengt seit
// dem Umbau auf ScrollTuning an keiner Hardware.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/ScrollJoystick -o "$env:TEMP\scroll.exe" test/test_scroll_joystick/test_scroll_joystick.cpp
//   & "$env:TEMP\scroll.exe"
//
#include "ScrollJoystick.h"
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

static const ScrollTuning kT{};

static const float kDt   = 0.005f;   // s je Takt
static const uint32_t kMs = 5;       // dieselbe Zeit in ms

// Laesst den Joystick ms Millisekunden bei fester Neigung laufen und summiert
// die ausgegebenen Schritte.
static int run(ScrollJoystick& s, float elev, uint32_t& now, uint32_t ms) {
    int total = 0;
    for (uint32_t i = 0; i < ms; i += kMs) {
        total += s.update(elev, kDt, now);
        now += kMs;
    }
    return total;
}

// --- Nullpunkt und Totzone --------------------------------------------

// Der Eintrittswinkel wird zur Mitte - egal, wie der Arm dabei steht. Das ist
// der Unterschied zu einer absoluten Schwelle: man kann den Scroll-Modus in
// jeder Armhaltung betreten.
static void test_entryAngleBecomesTheCentre() {
    ScrollJoystick s;
    uint32_t now = 1000;
    s.enter(30.f);
    CHECK(s.inDeadzone(), "direkt nach dem Eintritt wird schon gescrollt");
    CHECK(run(s, 30.f, now, 1000) == 0,
          "die Eintrittsneigung selbst erzeugt Schritte");

    ScrollJoystick s2;
    uint32_t now2 = 1000;
    s2.enter(-40.f);
    CHECK(run(s2, -40.f, now2, 1000) == 0,
          "der Nullpunkt haengt an der absoluten Neigung statt am Eintritt");
}

static void test_deadzoneHoldsStill() {
    ScrollJoystick s;
    uint32_t now = 1000;
    s.enter(0.f);
    CHECK(run(s, kT.deadDeg - 0.5f, now, 2000) == 0,
          "innerhalb der Totzone wird gescrollt");
    CHECK(s.inDeadzone(), "inDeadzone() meldet die Totzone nicht");
    CHECK(s.rateHz() == 0.f, "die Rate ist in der Totzone nicht null");
}

static void test_leavingTheDeadzoneStarts() {
    ScrollJoystick s;
    uint32_t now = 1000;
    s.enter(0.f);
    run(s, kT.deadDeg + 5.f, now, 100);
    CHECK(!s.inDeadzone(), "inDeadzone() bleibt ausserhalb der Totzone stehen");
    CHECK(s.rateHz() > 0.f, "die Rate bleibt ausserhalb der Totzone null");
}

// --- Die Kennlinie ------------------------------------------------------

static void test_rateFollowsTheGain() {
    ScrollJoystick s;
    uint32_t now = 1000;
    s.enter(0.f);
    const float dev = kT.deadDeg + 10.f;
    run(s, dev, now, 100);
    // Die Schwelle wird abgezogen, damit die Rate stetig bei null beginnt.
    CHECK(std::fabs(s.rateHz() - 10.f * kT.gain) < 0.01f,
          "die Kennlinie rechnet nicht (|dev| - deadDeg) * gain");
}

static void test_rateIsCappedAndSigned() {
    ScrollJoystick s;
    uint32_t now = 1000;
    s.enter(0.f);
    run(s, 500.f, now, 100);
    CHECK(std::fabs(s.rateHz() - kT.maxHz) < 0.01f,
          "die Rate laeuft ueber maxHz hinaus");

    s.enter(0.f);
    run(s, -500.f, now, 100);
    CHECK(std::fabs(s.rateHz() + kT.maxHz) < 0.01f,
          "die Gegenrichtung wird nicht negativ oder nicht begrenzt");
}

static void test_directionFollowsTheTilt() {
    ScrollJoystick s;
    uint32_t now = 1000;
    s.enter(0.f);
    CHECK(run(s, 20.f, now, 1000) > 0, "Neigung nach oben scrollt nicht positiv");

    s.enter(0.f);
    CHECK(run(s, -20.f, now, 1000) < 0, "Neigung nach unten scrollt nicht negativ");
}

static void test_invertFlipsTheDirection() {
    ScrollTuning t;
    t.invert = -1.f;
    ScrollJoystick s(t);
    uint32_t now = 1000;
    s.enter(0.f);
    CHECK(run(s, 20.f, now, 1000) < 0, "invert dreht die Richtung nicht um");
}

// --- Positionssignal, nicht Ratensignal --------------------------------

// Die eigentliche Eigenschaft des Joysticks: bei gehaltener Neigung kommt
// ueber eine Sekunde genau die Rate an Schritten heraus. Ein Ratensignal
// (Drehrate) wuerde stattdessen mit der Zeit wegdriften.
static void test_stepsPerSecondMatchTheRate() {
    ScrollJoystick s;
    uint32_t now = 1000;
    s.enter(0.f);
    const float dev      = kT.deadDeg + 10.f;
    const float expected = 10.f * kT.gain;          // Schritte je Sekunde
    const int   got      = run(s, dev, now, 1000);
    CHECK(std::fabs((float)got - expected) <= 1.f,
          "die Schrittzahl je Sekunde passt nicht zur Rate");
}

// Nach dem Zurueckkippen in die Mitte hoert das Scrollen sofort auf. Ein
// integrierendes Ratensignal wuerde hier nachlaufen.
static void test_returningToCentreStopsAtOnce() {
    ScrollJoystick s;
    uint32_t now = 1000;
    s.enter(0.f);
    run(s, 20.f, now, 1000);
    CHECK(run(s, 0.f, now, 2000) == 0, "das Scrollen laeuft nach dem Zurueckkippen nach");
    CHECK(s.inDeadzone(), "inDeadzone() meldet die Mitte nicht");
}

// --- Der Ausgabetakt ----------------------------------------------------

// Schritte kommen nur alle intervalMs heraus, damit bei hoher Rate ein
// gleichmaessiger Lauf entsteht und kein Sprung aus mehreren Schritten.
static void test_outputIsThrottled() {
    ScrollJoystick s;
    uint32_t now = 1000;
    s.enter(0.f);
    CHECK(run(s, 500.f, now, kT.intervalMs - kMs) == 0,
          "es kommen Schritte vor Ablauf des Ausgabetakts");
    CHECK(run(s, 500.f, now, 2 * kMs) > 0, "nach dem Ausgabetakt kommt nichts");
}

// Der angefangene Schritt geht nicht verloren: der Rest bleibt im
// Akkumulator stehen und kommt beim naechsten Mal mit heraus. Bei einer Rate
// unterhalb eines Schritts je Ausgabetakt scrollt es sonst gar nicht.
static void test_fractionalStepsAccumulate() {
    ScrollJoystick s;
    uint32_t now = 1000;
    s.enter(0.f);
    // Rate knapp ueber 1 Schritt/s: je 40-ms-Takt sind das 0.04 Schritte, ein
    // einzelner Takt liefert also nie einen Schritt.
    const float dev = kT.deadDeg + 1.f / kT.gain;
    CHECK(run(s, dev, now, 200) == 0, "eine sehr kleine Rate scrollt zu frueh");
    CHECK(run(s, dev, now, 1600) >= 1,
          "der angefangene Schritt geht verloren statt aufzulaufen");
}

int main() {
    test_entryAngleBecomesTheCentre();
    test_deadzoneHoldsStill();
    test_leavingTheDeadzoneStarts();
    test_rateFollowsTheGain();
    test_rateIsCappedAndSigned();
    test_directionFollowsTheTilt();
    test_invertFlipsTheDirection();
    test_stepsPerSecondMatchTheRate();
    test_returningToCentreStopsAtOnce();
    test_outputIsThrottled();
    test_fractionalStepsAccumulate();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
