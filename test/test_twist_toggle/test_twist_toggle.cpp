// Test der Ein/Aus-Drehgeste. Laeuft auf dem PC - TwistToggle haengt bewusst
// an keiner Hardware und bindet weder Arduino.h noch config.h ein.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/TwistToggle -o "$env:TEMP\twist.exe" test/test_twist_toggle/test_twist_toggle.cpp
//   & "$env:TEMP\twist.exe"
//
#include "TwistToggle.h"
#include <cstdio>
#include <math.h>

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

// Dreht den Unterarm gleichmaessig von einem Winkel zum naechsten und liefert
// die dazu passende Drehrate gleich mit - am Geraet kommt sie aus derselben
// Lageschaetzung wie der Winkel (TwistGuard::rateDps()). Getaktet wie die
// Firmware, rund 200 Aufrufe je Sekunde.
static TwistEvent sweep(TwistToggle& t, float from, float to, uint32_t ms,
                        bool level, uint32_t& now) {
    const float rate = fabsf(to - from) * 1000.f / (float)ms;
    TwistEvent seen = TwistEvent::None;
    for (uint32_t i = 0; i < ms; i += 5) {
        const float deg = from + (to - from) * (float)i / (float)ms;
        const TwistEvent e = t.tick(deg, rate, level, now);
        if (e != TwistEvent::None) seen = e;
        now += 5;
    }
    return seen;
}

// Haelt den Unterarm still: der Winkel steht, die Drehrate ist null.
static TwistEvent hold(TwistToggle& t, float deg, uint32_t ms,
                       bool level, uint32_t& now) {
    TwistEvent seen = TwistEvent::None;
    for (uint32_t i = 0; i < ms; i += 5) {
        const TwistEvent e = t.tick(deg, 0.f, level, now);
        if (e != TwistEvent::None) seen = e;
        now += 5;
    }
    return seen;
}

// --- Die Geste selbst --------------------------------------------------

static void test_flickOutAndBackToggles() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f, 200, true, now);
    CHECK(sweep(t, 0.f, 90.f, 300, true, now) == TwistEvent::None,
          "das blosse Ausdrehen schaltet schon");
    hold(t, 90.f, 100, true, now);
    CHECK(sweep(t, 90.f, 0.f, 300, true, now) == TwistEvent::Toggle,
          "raus und zurueck schaltet nicht");
}

static void test_stayingOutReportsNothing() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f, 200, true, now);
    sweep(t, 0.f, 90.f, 300, true, now);
    CHECK(hold(t, 90.f, 3000, true, now) == TwistEvent::None,
          "die gehaltene Ausdrehung meldet ein Ereignis");
}

// Das Fenster gilt fuer die GANZE Bewegung. Frueher lief es erst ab dem
// Ueberschreiten von onDeg, ein langsames Ausdrehen war damit gratis.
static void test_wholeMovementMustFitTheWindow() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f, 200, true, now);
    sweep(t, 0.f, 90.f, 500, true, now);
    hold(t, 90.f, 1000, true, now);
    CHECK(sweep(t, 90.f, 0.f, 400, true, now) != TwistEvent::Toggle,
          "eine ueber maxMs gedehnte Bewegung schaltet trotzdem");
}

static void test_shallowExcursionDoesNotToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f, 200, true, now);
    sweep(t, 0.f, 50.f, 200, true, now);   // unter onDeg umgekehrt
    CHECK(sweep(t, 50.f, 0.f, 200, true, now) != TwistEvent::Toggle,
          "eine zu flache Ausdrehung schaltet");
}

static void test_partialReturnDoesNotToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f, 200, true, now);
    sweep(t, 0.f, 90.f, 300, true, now);
    // 50 Grad liegt unter onDeg, aber ueber backDeg - keine echte Rueckkehr.
    CHECK(sweep(t, 90.f, 50.f, 200, true, now) != TwistEvent::Toggle,
          "halbe Rueckkehr schaltet");
}

// Das Vorzeichen darf keine Rolle spielen: aus der Zeige-Haltung heraus ist
// die Richtung anatomisch ohnehin festgelegt, und der Code muss sie nicht
// kennen.
static void test_signDoesNotMatter() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f, 200, true, now);
    sweep(t, 0.f, -90.f, 300, true, now);
    CHECK(sweep(t, -90.f, 0.f, 300, true, now) == TwistEvent::Toggle,
          "negative Verdrehung schaltet nicht");
}

// Eine sehr weite Ausdrehung ist dieselbe Geste wie eine knappe: die Achse
// traegt nur eine Bedeutung. Ein tieferer Scheitelwinkel hat hier einmal das
// Ziehen getragen und machte Ein/Aus unzuverlaessig.
static void test_veryDeepIsStillJustAToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f, 200, true, now);
    sweep(t, 0.f, 170.f, 400, true, now);
    CHECK(sweep(t, 170.f, 0.f, 400, true, now) == TwistEvent::Toggle,
          "eine sehr weite Ausdrehung schaltet nicht mehr ein oder aus");
}

// --- Die Erschuetterung: Geste gegen Pinch -----------------------------

// Der Kern des Umbaus. Der Anschlag am Ende einer zuegigen Drehung hebt die
// Huellkurve ueber TWIST_CANCEL_ENV - das ist die Geste selbst und kein Pinch.
// Frueher verwarf genau das die Geste, und weil der Controller nur im
// EINGESCHALTETEN Zustand meldete, ging die Maus an, aber nicht wieder aus.
static void test_shockWhileTurningIsNotCountedAsPinch() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f, 200, true, now);
    sweep(t, 0.f, 90.f, 300, true, now);
    t.reportShock(now);                    // Anschlag am Scheitel der Drehung
    sweep(t, 90.f, 0.f, 300, true, now);
    CHECK(t.rejectedByCancel() == 0, "die eigene Erschuetterung wird als Pinch gezaehlt");
}

static void test_shockWhileTurningStillToggles() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f, 200, true, now);
    sweep(t, 0.f, 90.f, 300, true, now);
    t.reportShock(now);
    CHECK(sweep(t, 90.f, 0.f, 300, true, now) == TwistEvent::Toggle,
          "die eigene Erschuetterung der Drehung verwirft die Geste");
}

// Die Gegenprobe: ruht der Unterarm ausgedreht, war die Erschuetterung ein
// Pinch (Rechtsklick). Dann darf das Zurueckdrehen nicht abschalten.
static void test_shockWhileRestingCancels() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f, 200, true, now);
    sweep(t, 0.f, 90.f, 300, true, now);
    hold(t, 90.f, 300, true, now);         // ausgedreht stehen geblieben
    t.reportShock(now);
    CHECK(sweep(t, 90.f, 0.f, 300, true, now) != TwistEvent::Toggle,
          "der Pinch im ausgedrehten Stand schaltet die Maus ab");
}

// Ein Abbruch gilt nur fuer die laufende Ausdrehung. Die naechste muss wieder
// schalten koennen - sonst haengt Ein/Aus zufaellig davon ab, ob zwischendurch
// geklickt wurde.
static void test_cancelOnlyAffectsTheCurrentExcursion() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f, 200, true, now);
    sweep(t, 0.f, 90.f, 300, true, now);
    hold(t, 90.f, 300, true, now);
    t.reportShock(now);
    sweep(t, 90.f, 0.f, 300, true, now);   // wirkungslos zurueck

    hold(t, 0.f, 900, true, now);
    sweep(t, 0.f, 90.f, 300, true, now);
    CHECK(sweep(t, 90.f, 0.f, 300, true, now) == TwistEvent::Toggle,
          "die naechste Ausdrehung schaltet nach einem Abbruch nicht mehr");
}

// --- Die Bremsen -------------------------------------------------------

static void test_levelLossBlocksToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f, 200, true, now);
    sweep(t, 0.f, 90.f, 300, true, now);
    hold(t, 90.f, 100, false, now);        // Arm nicht mehr waagrecht
    CHECK(sweep(t, 90.f, 0.f, 300, true, now) != TwistEvent::Toggle,
          "schaltet trotz weggefallenem Waagrecht-Gate");
}

// Nach einem Abbruch muss der Unterarm erst wieder heim, bevor eine neue Geste
// anlaeuft. Ohne diese Flanke heilte ein gerissenes Gate sich selbst, indem die
// Ausdrehung im Stand einfach neu begaenne.
static void test_restartNeedsAReturnToNeutral() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f, 200, true, now);
    sweep(t, 0.f, 90.f, 300, true, now);
    hold(t, 90.f, 100, false, now);        // Abbruch, Arm bleibt ausgedreht
    hold(t, 90.f, 100, true, now);         // Gate wieder da, Arm unveraendert
    CHECK(sweep(t, 90.f, 0.f, 300, true, now) != TwistEvent::Toggle,
          "die abgebrochene Geste laeuft im Stand neu an");
}

static void test_lockoutBlocksSecondToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f, 200, true, now);
    sweep(t, 0.f, 90.f, 300, true, now);
    CHECK(sweep(t, 90.f, 0.f, 300, true, now) == TwistEvent::Toggle,
          "erste Geste schaltet nicht");
    // Sofort noch einmal, innerhalb von lockoutMs.
    sweep(t, 0.f, 90.f, 200, true, now);
    CHECK(sweep(t, 90.f, 0.f, 200, true, now) != TwistEvent::Toggle,
          "zweite Geste schaltet trotz Lockout");
}

static void test_toggleWorksAgainAfterLockout() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f, 200, true, now);
    sweep(t, 0.f, 90.f, 300, true, now);
    sweep(t, 90.f, 0.f, 300, true, now);   // erster Toggle
    hold(t, 0.f, 1000, true, now);         // Lockout abwarten
    sweep(t, 0.f, 90.f, 300, true, now);
    CHECK(sweep(t, 90.f, 0.f, 300, true, now) == TwistEvent::Toggle,
          "nach dem Lockout schaltet es nicht wieder");
}

// Jede der drei Bremsen verwirft die Geste lautlos. Ohne Zaehler ist das von
// Unzuverlaessigkeit nicht zu unterscheiden - genau daran hing die Fehlersuche.
static void test_rejectionsAreCounted() {
    TwistToggle a;
    uint32_t now = 1000;
    hold(a, 0.f, 200, true, now);
    sweep(a, 0.f, 90.f, 300, true, now);
    hold(a, 90.f, 300, true, now);
    a.reportShock(now);
    sweep(a, 90.f, 0.f, 300, true, now);
    CHECK(a.rejectedByCancel() == 1, "die abgebrochene Ausdrehung wird nicht gezaehlt");
    CHECK(a.rejectedByLevel() == 0 && a.rejectedByTime() == 0,
          "die Ablehnung wird dem falschen Zaehler angelastet");

    TwistToggle b;
    now = 1000;
    hold(b, 0.f, 200, true, now);
    sweep(b, 0.f, 90.f, 300, true, now);
    hold(b, 90.f, 100, false, now);
    CHECK(b.rejectedByLevel() == 1, "das gerissene Waagrecht-Gate wird nicht gezaehlt");

    TwistToggle c;
    now = 1000;
    hold(c, 0.f, 200, true, now);
    sweep(c, 0.f, 90.f, 300, true, now);
    CHECK(c.rejectedByTime() == 0, "zu frueh als verspaetet gezaehlt");
    hold(c, 90.f, 2000, true, now);        // ueber maxMs draussen geblieben
    sweep(c, 90.f, 0.f, 300, true, now);
    CHECK(c.rejectedByTime() == 1, "die verspaetete Rueckkehr wird nicht gezaehlt");
    CHECK(c.rejectedByCancel() == 0, "die Verspaetung wird als Abbruch gezaehlt");
}

// Der Teleplot-Kanal tw muss die Phase zeigen, sonst ist am Geraet nicht zu
// sehen, wo eine Geste haengen bleibt.
static void test_stateReportsThePhase() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f, 200, true, now);
    CHECK(t.state() == 0, "der Ruhezustand meldet nicht 0");
    sweep(t, 0.f, 90.f, 300, true, now);
    CHECK(t.state() == 1, "die laufende Ausdrehung meldet nicht 1");
    sweep(t, 90.f, 50.f, 100, true, now);
    CHECK(t.state() == 2, "die Rueckkehr meldet nicht 2");
    sweep(t, 50.f, 0.f, 100, true, now);
    CHECK(t.state() == 3, "der Lockout nach dem Schalten meldet nicht 3");
}

int main() {
    test_flickOutAndBackToggles();
    test_stayingOutReportsNothing();
    test_wholeMovementMustFitTheWindow();
    test_shallowExcursionDoesNotToggle();
    test_partialReturnDoesNotToggle();
    test_signDoesNotMatter();
    test_veryDeepIsStillJustAToggle();
    test_shockWhileTurningIsNotCountedAsPinch();
    test_shockWhileTurningStillToggles();
    test_shockWhileRestingCancels();
    test_cancelOnlyAffectsTheCurrentExcursion();
    test_levelLossBlocksToggle();
    test_restartNeedsAReturnToNeutral();
    test_lockoutBlocksSecondToggle();
    test_toggleWorksAgainAfterLockout();
    test_rejectionsAreCounted();
    test_stateReportsThePhase();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
