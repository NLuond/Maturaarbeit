// Test der Rad-Ausgabe. Laeuft auf dem PC - ScrollWheel haengt an keiner
// Hardware und bindet weder Arduino.h noch config.h ein.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/ScrollWheel -o "$env:TEMP\wheel.exe" test/test_scroll_wheel/test_scroll_wheel.cpp
//   & "$env:TEMP\wheel.exe"
//
#include "ScrollWheel.h"
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

// Weg fuer n Radschritte, mit einem Hauch Zugabe: die Summe vieler kleiner
// float-Portionen landet sonst knapp unter der Schwelle und der Schritt kaeme
// erst einen Takt spaeter. Am Geraet ist das ohne Belang, im Test waere es
// eine Scheingenauigkeit.
static float pxFor(int steps) { return (steps + 0.01f) * ScrollTuning().pxPerStep; }

// Schiebt einen Weg in Portionen hinein und summiert die Radschritte. Die Zeit
// laeuft dabei mit, damit die Drosselung greift wie am Geraet.
static int feed(ScrollWheel& w, float totalPx, int ticks, uint32_t& now) {
    int steps = 0;
    for (int i = 0; i < ticks; i++) {
        steps += w.update(totalPx / ticks, now);
        now += 5;
    }
    // Was beim letzten Durchlauf noch im Rest stand, kommt erst im naechsten
    // Ausgabefenster heraus. Am Geraet laeuft die Schleife einfach weiter -
    // hier muss das Fenster von Hand nachgeschoben werden.
    now += ScrollTuning().intervalMs + 5;
    steps += w.update(0.f, now);
    return steps;
}

// --- Der Grundfall -----------------------------------------------------

static void test_stillHandDoesNotScroll() {
    ScrollWheel w;
    uint32_t now = 1000;
    CHECK(feed(w, 0.f, 200, now) == 0, "eine stehende Hand scrollt");
}

static void test_oneStepPerThreshold() {
    ScrollWheel w;
    uint32_t now = 1000;
    // Genau ein Schritt Weg, ueber viele Takte verteilt.
    CHECK(feed(w, pxFor(1), 100, now) == 1,
          "ein Schritt Weg ergibt nicht genau einen Radschritt");
}

static void test_stepsScaleWithDistance() {
    ScrollWheel w;
    uint32_t now = 1000;
    const int steps = feed(w, pxFor(5), 300, now);
    CHECK(steps == 5, "fuenf Schritte Weg ergeben nicht fuenf Radschritte");
}

// Die Richtung folgt dem Vorzeichen der Bewegung.
static void test_directionFollowsSign() {
    ScrollWheel w;
    uint32_t now = 1000;
    CHECK(feed(w, -pxFor(3), 200, now) == -3,
          "die Gegenrichtung ergibt nicht die negative Schrittzahl");
}

static void test_invertFlipsDirection() {
    ScrollTuning t;
    t.invert = -1.f;
    ScrollWheel w(t);
    uint32_t now = 1000;
    CHECK(feed(w, pxFor(3), 200, now) == -3, "invert dreht die Richtung nicht um");
}

// --- Rest, Drosselung, Begrenzung --------------------------------------

// Bruchteile duerfen nicht verlorengehen: eine langsame, gleichmaessige
// Bewegung muss dieselbe Gesamtzahl ergeben wie eine schnelle.
static void test_fractionsAccumulate() {
    ScrollWheel slow, fast;
    uint32_t nowSlow = 1000, nowFast = 1000;
    const float dist = pxFor(4);
    CHECK(feed(slow, dist, 400, nowSlow) == feed(fast, dist, 40, nowFast),
          "langsame und schnelle Bewegung ergeben verschiedene Schrittzahlen");
}

static void test_outputIsThrottled() {
    ScrollWheel w;
    uint32_t now = 1000;
    // Der ganze Weg in einem einzigen Takt, danach kein Zeitfortschritt.
    const int8_t first = w.update(10.f * kT.pxPerStep, now);
    CHECK(first != 0, "die erste Ausgabe bleibt aus");
    CHECK(w.update(10.f * kT.pxPerStep, now) == 0,
          "es wird mehrfach im selben Intervall ausgegeben");
}

static void test_burstIsCapped() {
    ScrollWheel w;
    uint32_t now = 1000;
    const int8_t steps = w.update(100.f * kT.pxPerStep, now);
    CHECK(steps == (int8_t)kT.maxPerTick, "ein Sprung wird nicht begrenzt");
    CHECK(w.pending() > 0.f, "der abgeschnittene Rest geht verloren");
}

// Der Rest darf nicht in die naechste Scroll-Sitzung ueberschwappen.
static void test_resetDropsThePendingRest() {
    ScrollWheel w;
    uint32_t now = 1000;
    w.update(0.9f * kT.pxPerStep, now);
    CHECK(w.pending() > 0.f, "gar kein Rest aufgelaufen");
    w.reset();
    CHECK(std::fabs(w.pending()) < 1e-6f, "reset() verwirft den Rest nicht");
}

int main() {
    test_stillHandDoesNotScroll();
    test_oneStepPerThreshold();
    test_stepsScaleWithDistance();
    test_directionFollowsSign();
    test_invertFlipsDirection();
    test_fractionsAccumulate();
    test_outputIsThrottled();
    test_burstIsCapped();
    test_resetDropsThePendingRest();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
