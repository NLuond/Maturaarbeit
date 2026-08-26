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

static const TwistTuning kT{};
static const float       kDt = 0.005f;   // 5 ms je Aufruf

// Dreht den Unterarm mit fester Rate. Das Modul bekommt nur noch die Drehrate,
// keinen Winkel mehr - der Ausschlag entsteht durch Integration ab dem Beginn
// der Bewegung. dps mal ms ergibt also direkt den Ausschlag in Grad.
static TwistEvent turn(TwistToggle& t, float dps, uint32_t ms,
                       bool level, uint32_t& now) {
    TwistEvent seen = TwistEvent::None;
    for (uint32_t i = 0; i < ms; i += 5) {
        const TwistEvent e = t.tick(dps, level, kDt, now);
        if (e != TwistEvent::None) seen = e;
        now += 5;
    }
    return seen;
}

static TwistEvent rest(TwistToggle& t, uint32_t ms, bool level, uint32_t& now) {
    return turn(t, 0.f, ms, level, now);
}

// --- Die Geste selbst --------------------------------------------------

static void test_flickOutAndBackToggles() {
    TwistToggle t;
    uint32_t now = 1000;
    rest(t, 200, true, now);
    CHECK(turn(t, 250.f, 250, true, now) == TwistEvent::None,   // 62 Grad raus
          "das blosse Ausdrehen schaltet schon");
    rest(t, 100, true, now);
    CHECK(turn(t, -250.f, 250, true, now) == TwistEvent::Toggle,
          "raus und zurueck schaltet nicht");
}

static void test_stayingOutReportsNothing() {
    TwistToggle t;
    uint32_t now = 1000;
    rest(t, 200, true, now);
    turn(t, 250.f, 250, true, now);
    CHECK(rest(t, 3000, true, now) == TwistEvent::None,
          "die gehaltene Ausdrehung meldet ein Ereignis");
}

// Ohne diese Pruefung waere die gesenkte Schwelle gefaehrlich: erst die
// Forderung nach einem ZUEGIGEN Hinweg trennt die Geste von einer beilaeufigen
// Armdrehung, die denselben Ausschlag ueber mehrere Sekunden erreicht.
static void test_slowOutwardIsRejected() {
    TwistToggle t;
    uint32_t now = 1000;
    rest(t, 200, true, now);
    turn(t, 70.f, 700, true, now);            // 49 Grad, aber ueber outMaxMs
    CHECK(turn(t, -70.f, 700, true, now) != TwistEvent::Toggle,
          "eine langsam ausgefuehrte Drehung schaltet");
}

static void test_wholeMovementMustFitTheWindow() {
    TwistToggle t;
    uint32_t now = 1000;
    rest(t, 200, true, now);
    turn(t, 250.f, 250, true, now);
    rest(t, 1100, true, now);                 // zu lange draussen geblieben
    CHECK(turn(t, -250.f, 250, true, now) != TwistEvent::Toggle,
          "eine ueber maxMs gedehnte Bewegung schaltet trotzdem");
}

static void test_shallowExcursionDoesNotToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    rest(t, 200, true, now);
    turn(t, 250.f, 120, true, now);           // 30 Grad, unter onDeg
    CHECK(turn(t, -250.f, 120, true, now) != TwistEvent::Toggle,
          "eine zu flache Ausdrehung schaltet");
}

// Der haeufigste Fall bei einer kleinen Geste: der erste Versuch faellt zu
// knapp aus und man wiederholt ihn sofort. Bliebe das Modul bis zum Ablauf des
// Fensters haengen, ginge der zweite Versuch verloren.
// Die rest() an den Umkehrpunkten sind kein Zugestaendnis an den Code: eine
// Drehrichtung laesst sich nicht umkehren, ohne dass die Rate durch null geht.
static void test_retryAfterShallowAttemptWorks() {
    TwistToggle t;
    uint32_t now = 1000;
    rest(t, 200, true, now);
    turn(t,  250.f, 120, true, now);          // zu flach
    rest(t, 60, true, now);
    turn(t, -250.f, 120, true, now);          // wieder daheim
    rest(t, 300, true, now);                  // kurz gestutzt, dann noch einmal
    turn(t,  250.f, 250, true, now);          // zweiter, richtiger Versuch
    rest(t, 60, true, now);
    CHECK(turn(t, -250.f, 250, true, now) == TwistEvent::Toggle,
          "der zweite Versuch nach einer zu flachen Drehung schaltet nicht");
}

static void test_partialReturnDoesNotToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    rest(t, 200, true, now);
    turn(t,  250.f, 250, true, now);          // 62 Grad raus
    CHECK(turn(t, -250.f, 100, true, now) != TwistEvent::Toggle,
          "halbe Rueckkehr schaltet");        // nur 25 Grad zurueck
}

static void test_signDoesNotMatter() {
    TwistToggle t;
    uint32_t now = 1000;
    rest(t, 200, true, now);
    turn(t, -250.f, 250, true, now);
    CHECK(turn(t, 250.f, 250, true, now) == TwistEvent::Toggle,
          "die andere Drehrichtung schaltet nicht");
}

static void test_veryDeepIsStillJustAToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    rest(t, 200, true, now);
    turn(t,  400.f, 400, true, now);          // 160 Grad
    CHECK(turn(t, -400.f, 400, true, now) == TwistEvent::Toggle,
          "eine sehr weite Ausdrehung schaltet nicht mehr ein oder aus");
}

// --- Kein absoluter Bezug mehr -----------------------------------------

// Der Kern des Umbaus. Frueher hing die Geste an arm::twistDeg() gegen einen
// fest verdrahteten Nullpunkt; ruhte der Unterarm woanders, war die Geste
// unerreichbar. Jetzt zaehlt allein die Aenderung ab dem Beginn der Bewegung.
static void test_restingPostureDoesNotMatter() {
    TwistToggle t;
    uint32_t now = 1000;
    // Der Arm liegt weit ausserhalb jeder gedachten Neutrallage - hier
    // dargestellt durch eine lange, langsame Drehung dorthin.
    turn(t, 20.f, 3000, true, now);           // 60 Grad weggedreht, unter startDps
    rest(t, 500, true, now);

    turn(t,  250.f, 250, true, now);
    CHECK(turn(t, -250.f, 250, true, now) == TwistEvent::Toggle,
          "die Geste haengt noch an einer absoluten Ruhelage");
}

// Gyro-Nullpunktfehler duerfen sich nicht aufsummieren: ausserhalb der Geste
// wird gar nicht integriert, innerhalb sind es hoechstens maxMs.
static void test_gyroBiasDoesNotAccumulate() {
    TwistToggle t;
    uint32_t now = 1000;
    turn(t, 3.f, 20000, true, now);           // 20 s Restdrift, 60 Grad Summe
    CHECK(t.state() == 0, "die Restdrift hat eine Geste angestossen");

    turn(t,  250.f, 250, true, now);
    CHECK(turn(t, -250.f, 250, true, now) == TwistEvent::Toggle,
          "nach langer Restdrift schaltet die Geste nicht mehr");
}

// --- Die Erschuetterung: Geste gegen Pinch -----------------------------

static void test_shockWhileTurningStillToggles() {
    TwistToggle t;
    uint32_t now = 1000;
    rest(t, 200, true, now);
    turn(t, 250.f, 250, true, now);
    t.reportShock(now);                       // Anschlag am Scheitel der Drehung
    CHECK(turn(t, -250.f, 250, true, now) == TwistEvent::Toggle,
          "die eigene Erschuetterung der Drehung verwirft die Geste");
}

static void test_shockWhileRestingCancels() {
    TwistToggle t;
    uint32_t now = 1000;
    rest(t, 200, true, now);
    turn(t, 250.f, 250, true, now);
    rest(t, 300, true, now);                  // ausgedreht stehen geblieben
    t.reportShock(now);
    CHECK(turn(t, -250.f, 250, true, now) != TwistEvent::Toggle,
          "der Pinch im ausgedrehten Stand schaltet die Maus ab");
}

static void test_cancelOnlyAffectsTheCurrentExcursion() {
    TwistToggle t;
    uint32_t now = 1000;
    rest(t, 200, true, now);
    turn(t, 250.f, 250, true, now);
    rest(t, 300, true, now);
    t.reportShock(now);
    turn(t, -250.f, 250, true, now);          // wirkungslos zurueck

    rest(t, 300, true, now);
    turn(t, 250.f, 250, true, now);
    CHECK(turn(t, -250.f, 250, true, now) == TwistEvent::Toggle,
          "die naechste Ausdrehung schaltet nach einem Abbruch nicht mehr");
}

// --- Die Bremsen -------------------------------------------------------

// Das Waagrecht-Gate entscheidet, OB die Geste anfaengt - hier zaehlt es.
// Ohne diese Bedingung schaltet jede Alltagsdrehung von 45 Grad die Maus
// wieder ein, denn der Ausschlag allein unterscheidet sie von nichts.
static void test_gestureNeedsALevelArmAtTheStart() {
    TwistToggle t;
    uint32_t now = 1000;
    rest(t, 300, false, now);
    turn(t,  250.f, 250, false, now);         // Arm haengt, nicht waagrecht
    CHECK(turn(t, -250.f, 250, false, now) != TwistEvent::Toggle,
          "die Geste laeuft auch ausserhalb der waagrechten Haltung an");
    CHECK(t.rejectedByLevel() > 0, "die verhinderte Fehlauslösung wird nicht gezaehlt");
}

// Waehrend der Geste dagegen NICHT mehr: die Drehung schwenkt elev selbst mit,
// weil die Platinenachse nicht exakt auf der anatomischen Drehachse liegt. Ein
// laufendes Gate wuerde die eigene Geste abwuergen, je enger desto haeufiger.
static void test_levelLossDuringTheGestureIsTolerated() {
    TwistToggle t;
    uint32_t now = 1000;
    rest(t, 300, true, now);
    turn(t, 250.f, 250, true,  now);
    rest(t, 100, false, now);                 // Arm kippt waehrend der Geste weg
    CHECK(turn(t, -250.f, 250, false, now) == TwistEvent::Toggle,
          "das Gate wuergt die laufende Geste ab");
}

// Zweite Bremse gegen Fehlauslösungen: eine bewusste Geste beginnt aus der
// Ruhe. Alltagsbewegung ist dagegen durchgehend - der Unterarm kommt dabei nie
// armedMs lang zur Ruhe.
static void test_gestureNeedsAQuietForearmFirst() {
    TwistToggle t;
    uint32_t now = 1000;
    rest(t, 300, true, now);
    turn(t, 55.f, 2000, true, now);           // dauernd in Bewegung, unter startDps
    turn(t,  250.f, 250, true, now);
    CHECK(turn(t, -250.f, 250, true, now) != TwistEvent::Toggle,
          "die Geste laeuft mitten aus einer laufenden Bewegung heraus an");
    CHECK(t.rejectedByMotion() > 0, "die verhinderte Fehlauslösung wird nicht gezaehlt");
}

static void test_lockoutBlocksSecondToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    rest(t, 200, true, now);
    turn(t, 250.f, 250, true, now);
    CHECK(turn(t, -250.f, 250, true, now) == TwistEvent::Toggle,
          "erste Geste schaltet nicht");
    turn(t,  250.f, 250, true, now);          // sofort noch einmal
    CHECK(turn(t, -250.f, 250, true, now) != TwistEvent::Toggle,
          "zweite Geste schaltet trotz Lockout");
}

static void test_toggleWorksAgainAfterLockout() {
    TwistToggle t;
    uint32_t now = 1000;
    rest(t, 200, true, now);
    turn(t,  250.f, 250, true, now);
    turn(t, -250.f, 250, true, now);          // erster Toggle
    rest(t, 1000, true, now);                 // Lockout abwarten
    turn(t,  250.f, 250, true, now);
    CHECK(turn(t, -250.f, 250, true, now) == TwistEvent::Toggle,
          "nach dem Lockout schaltet es nicht wieder");
}

// Jede Bremse verwirft die Geste lautlos. Ohne Zaehler ist das von
// Unzuverlaessigkeit nicht zu unterscheiden - genau daran hing die Fehlersuche.
static void test_rejectionsAreCounted() {
    TwistToggle a;
    uint32_t now = 1000;
    rest(a, 200, true, now);
    turn(a, 250.f, 250, true, now);
    rest(a, 300, true, now);
    a.reportShock(now);
    turn(a, -250.f, 250, true, now);
    CHECK(a.rejectedByCancel() == 1, "die abgebrochene Ausdrehung wird nicht gezaehlt");
    CHECK(a.rejectedByLevel() == 0 && a.rejectedByTime() == 0,
          "die Ablehnung wird dem falschen Zaehler angelastet");

    TwistToggle b;
    now = 1000;
    rest(b, 300, false, now);
    turn(b, 250.f, 250, false, now);
    CHECK(b.rejectedByLevel() == 1, "das Waagrecht-Gate am Start wird nicht gezaehlt");
    CHECK(b.rejectedByMotion() == 0, "die Ablehnung wird dem falschen Zaehler angelastet");

    TwistToggle c;
    now = 1000;
    rest(c, 200, true, now);
    turn(c, 250.f, 250, true, now);
    CHECK(c.rejectedByTime() == 0, "zu frueh als verspaetet gezaehlt");
    rest(c, 1200, true, now);                 // ueber maxMs draussen geblieben
    turn(c, -250.f, 250, true, now);
    CHECK(c.rejectedByTime() == 1, "die verspaetete Rueckkehr wird nicht gezaehlt");
    CHECK(c.rejectedByCancel() == 0, "die Verspaetung wird als Abbruch gezaehlt");

    // Auch der zu langsame HINWEG muss sich melden, sonst ist die haeufigste
    // Fehlbedienung der kleinen Geste die einzige unsichtbare.
    TwistToggle d;
    now = 1000;
    rest(d, 200, true, now);
    turn(d, 70.f, 700, true, now);
    CHECK(d.rejectedByTime() == 1, "der zu langsame Hinweg wird nicht gezaehlt");
}

// Der Teleplot-Kanal tw muss die Phase zeigen, sonst ist am Geraet nicht zu
// sehen, wo eine Geste haengen bleibt.
static void test_stateReportsThePhase() {
    TwistToggle t;
    uint32_t now = 1000;
    rest(t, 200, true, now);
    CHECK(t.state() == 0, "der Ruhezustand meldet nicht 0");
    turn(t, 250.f, 120, true, now);           // 30 Grad, noch unter onDeg
    CHECK(t.state() == 1, "die laufende Ausdrehung meldet nicht 1");
    turn(t, 250.f, 130, true, now);           // jetzt ueber onDeg
    CHECK(t.state() == 2, "die erreichte Ausdrehung meldet nicht 2");
    turn(t, -250.f, 250, true, now);
    CHECK(t.state() == 3, "der Lockout nach dem Schalten meldet nicht 3");
}

// Der Ausschlag gehoert nach aussen: nur so laesst sich am Geraet ablesen, wie
// weit man tatsaechlich dreht, und onDeg danach einstellen.
static void test_excursionIsObservable() {
    TwistToggle t;
    uint32_t now = 1000;
    rest(t, 200, true, now);
    turn(t, 250.f, 200, true, now);           // 50 Grad
    CHECK(fabsf(t.excursionDeg() - 50.f) < 3.f,
          "der gemeldete Ausschlag passt nicht zur gedrehten Rate");
}

int main() {
    test_flickOutAndBackToggles();
    test_stayingOutReportsNothing();
    test_slowOutwardIsRejected();
    test_wholeMovementMustFitTheWindow();
    test_shallowExcursionDoesNotToggle();
    test_retryAfterShallowAttemptWorks();
    test_partialReturnDoesNotToggle();
    test_signDoesNotMatter();
    test_veryDeepIsStillJustAToggle();
    test_restingPostureDoesNotMatter();
    test_gyroBiasDoesNotAccumulate();
    test_shockWhileTurningStillToggles();
    test_shockWhileRestingCancels();
    test_cancelOnlyAffectsTheCurrentExcursion();
    test_gestureNeedsALevelArmAtTheStart();
    test_levelLossDuringTheGestureIsTolerated();
    test_gestureNeedsAQuietForearmFirst();
    test_lockoutBlocksSecondToggle();
    test_toggleWorksAgainAfterLockout();
    test_rejectionsAreCounted();
    test_stateReportsThePhase();
    test_excursionIsObservable();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
