// Test der Ein/Aus-Drehgeste. Laeuft auf dem PC - TwistToggle haengt bewusst
// an keiner Hardware und bindet weder Arduino.h noch config.h ein.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/TwistToggle -o "$env:TEMP\twist.exe" test/test_twist_toggle/test_twist_toggle.cpp
//   & "$env:TEMP\twist.exe"
//
#include "TwistToggle.h"
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

// Haelt einen Winkel fuer eine Dauer und meldet, welches Ereignis dabei kam.
// Getaktet wie die Firmware: rund 209 Aufrufe je Sekunde.
static TwistEvent hold(TwistToggle& t, float deg, bool level,
                       uint32_t& now, uint32_t ms) {
    TwistEvent seen = TwistEvent::None;
    for (uint32_t i = 0; i < ms; i += 5) {
        const TwistEvent e = t.tick(deg, level, now);
        if (e != TwistEvent::None) seen = e;
        now += 5;
    }
    return seen;
}

static void test_quickOutAndBackToggles() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,  true, now, 200);
    CHECK(hold(t, 90.f, true, now, 300) == TwistEvent::None, "Ausdrehen allein schaltet schon");
    CHECK(hold(t, 0.f,  true, now, 100) == TwistEvent::Toggle, "raus und zurueck schaltet nicht");
}

static void test_slowReturnDoesNotToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,  true, now, 200);
    hold(t, 90.f, true, now, 1500);          // laenger als maxMs gehalten
    CHECK(hold(t, 0.f, true, now, 200) != TwistEvent::Toggle,
          "zu spaete Rueckkehr schaltet trotzdem");
}

static void test_heldComesOnceAfterMaxMs() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f, true, now, 200);
    CHECK(hold(t, 90.f, true, now, 1500) == TwistEvent::Held, "Held kommt nicht");
    // Ein zweites Mal darf es in derselben Ausdrehung nicht kommen.
    CHECK(hold(t, 90.f, true, now, 1500) == TwistEvent::None, "Held kommt mehrfach");
}

static void test_cancelBlocksToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,  true, now, 200);
    hold(t, 90.f, true, now, 300);
    t.cancel();                              // env-Gate ging auf: Pinch erkannt
    CHECK(hold(t, 0.f, true, now, 200) != TwistEvent::Toggle,
          "abgebrochene Ausdrehung schaltet trotzdem");
}

// cancel() darf nur den Toggle unterdruecken, nicht den Uebergang in den
// Scroll-Modus: ein Pinch waehrend der Ausdrehung ist ein Rechtsklick, keine
// Absage an die Geste selbst. Bleibt die Hand danach weiter draussen, muss
// Held trotzdem kommen - sonst haengt das Scrollen zufaellig davon ab, ob
// zwischendurch geklickt wurde.
static void test_cancelDoesNotBlockHeld() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,  true, now, 200);
    hold(t, 90.f, true, now, 300);
    t.cancel();                              // env-Gate ging auf: Pinch erkannt
    CHECK(hold(t, 90.f, true, now, 1500) == TwistEvent::Held,
          "Held kommt nach cancel() nicht");
    CHECK(hold(t, 0.f, true, now, 200) != TwistEvent::Toggle,
          "Rueckkehr nach Held und cancel() schaltet trotzdem");
}

static void test_partialReturnDoesNotToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,  true, now, 200);
    hold(t, 90.f, true, now, 300);
    // 50 Grad liegt unter onDeg, aber ueber backDeg - keine echte Rueckkehr.
    CHECK(hold(t, 50.f, true, now, 400) != TwistEvent::Toggle,
          "halbe Rueckkehr schaltet");
}

static void test_levelLossBlocksToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,  true,  now, 200);
    hold(t, 90.f, true,  now, 200);
    hold(t, 90.f, false, now, 100);          // Arm nicht mehr waagrecht
    CHECK(hold(t, 0.f, true, now, 200) != TwistEvent::Toggle,
          "schaltet trotz weggefallenem Waagrecht-Gate");
}

static void test_lockoutBlocksSecondToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,  true, now, 200);
    hold(t, 90.f, true, now, 300);
    CHECK(hold(t, 0.f, true, now, 100) == TwistEvent::Toggle, "erste Geste schaltet nicht");
    // Sofort noch einmal, innerhalb von lockoutMs.
    hold(t, 90.f, true, now, 200);
    CHECK(hold(t, 0.f, true, now, 100) != TwistEvent::Toggle,
          "zweite Geste schaltet trotz Lockout");
}

static void test_toggleWorksAgainAfterLockout() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,  true, now, 200);
    hold(t, 90.f, true, now, 300);
    hold(t, 0.f,  true, now, 100);           // erster Toggle
    hold(t, 0.f,  true, now, 1000);          // Lockout abwarten
    hold(t, 90.f, true, now, 300);
    CHECK(hold(t, 0.f, true, now, 100) == TwistEvent::Toggle,
          "nach dem Lockout schaltet es nicht wieder");
}

static void test_afterHeldNoToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,  true, now, 200);
    hold(t, 90.f, true, now, 1500);          // Held
    CHECK(hold(t, 0.f, true, now, 200) != TwistEvent::Toggle,
          "Rueckkehr nach dem Scroll-Modus schaltet ab");
}

// Das Vorzeichen darf keine Rolle spielen: aus der Zeige-Haltung heraus ist
// die Richtung anatomisch ohnehin festgelegt, und der Code muss sie nicht
// kennen. Dieselbe Geste nach der anderen Seite muss gleich wirken.
static void test_signDoesNotMatter() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,   true, now, 200);
    hold(t, -90.f, true, now, 300);
    CHECK(hold(t, 0.f, true, now, 100) == TwistEvent::Toggle,
          "negative Verdrehung schaltet nicht");
}

// Eine sehr weite Ausdrehung ist dieselbe Geste wie eine knappe. Ein tieferer
// Scheitelwinkel hat hier einmal das Ziehen getragen; das lag auf derselben
// Achse wie Ein/Aus, und eine etwas zu weit geratene Schaltgeste wurde
// dadurch stillschweigend zum Ziehen. Diese Pruefung haelt fest, dass die
// Achse wieder nur eine Bedeutung hat.
static void test_veryDeepIsStillJustAToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,   true, now, 200);
    hold(t, 170.f, true, now, 300);
    CHECK(hold(t, 0.f, true, now, 100) == TwistEvent::Toggle,
          "eine sehr weite Ausdrehung schaltet nicht mehr ein oder aus");
}

// Jede der drei Bremsen verwirft die Geste lautlos. Ohne Zaehler ist das von
// Unzuverlaessigkeit nicht zu unterscheiden - genau daran hing die Fehlersuche.
static void test_rejectionsAreCounted() {
    TwistToggle a;
    uint32_t now = 1000;
    hold(a, 0.f,  true, now, 200);
    hold(a, 90.f, true, now, 300);
    a.cancel();
    hold(a, 0.f,  true, now, 200);
    CHECK(a.rejectedByCancel() == 1, "die abgebrochene Ausdrehung wird nicht gezaehlt");
    CHECK(a.rejectedByLevel() == 0 && a.rejectedByTime() == 0,
          "die Ablehnung wird dem falschen Zaehler angelastet");

    TwistToggle b;
    now = 1000;
    hold(b, 0.f,  true,  now, 200);
    hold(b, 90.f, true,  now, 200);
    hold(b, 90.f, false, now, 100);
    CHECK(b.rejectedByLevel() == 1, "das gerissene Waagrecht-Gate wird nicht gezaehlt");

    TwistToggle c;
    now = 1000;
    hold(c, 0.f,  true, now, 200);
    hold(c, 90.f, true, now, 200);
    CHECK(c.rejectedByTime() == 0, "zu frueh als verspaetet gezaehlt");
    hold(c, 90.f, true, now, 2000);          // ueber maxMs: erst Held
    hold(c, 0.f,  true, now, 200);
    CHECK(c.rejectedByCancel() == 0, "Held wird als Abbruch gezaehlt");
}

int main() {
    test_quickOutAndBackToggles();
    test_slowReturnDoesNotToggle();
    test_heldComesOnceAfterMaxMs();
    test_cancelBlocksToggle();
    test_cancelDoesNotBlockHeld();
    test_partialReturnDoesNotToggle();
    test_levelLossBlocksToggle();
    test_lockoutBlocksSecondToggle();
    test_toggleWorksAgainAfterLockout();
    test_afterHeldNoToggle();
    test_signDoesNotMatter();
    test_veryDeepIsStillJustAToggle();
    test_rejectionsAreCounted();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
