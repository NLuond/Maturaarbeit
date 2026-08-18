// Test der Klick-Entscheidung. Laeuft auf dem PC - PinchDetector haengt seit
// dem Umbau auf PinchTuning an keiner Hardware und kennt das Edge-Impulse-SDK
// ohnehin nie: der Klassifikator kommt als aufrufbares Objekt herein.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/PinchDetector -o "$env:TEMP\pinch.exe" test/test_pinch_detector/test_pinch_detector.cpp
//   & "$env:TEMP\pinch.exe"
//
#include "PinchDetector.h"
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

static const PinchTuning kT{};

// Zaehlt mit, wie oft der Klassifikator ueberhaupt befragt wurde. Genau das
// ist die Eigenschaft, um deretwillen er als Callable uebergeben wird: bei
// geschlossenem Gate darf keine Inferenz laufen.
struct MlStub {
    bool answer = true;
    int  calls  = 0;
    bool operator()() { calls++; return answer; }
};

// --- Das bi-level Gate -------------------------------------------------

static void test_gateOpensAndCloses() {
    PinchDetector d;
    MlStub ml;
    CHECK(!d.envGate(), "das Gate ist schon vor dem ersten Takt offen");

    d.tick(kT.envOn - 0.001f, 0.f, 0.f, 100, ml);
    CHECK(!d.envGate(), "das Gate oeffnet schon unterhalb von envOn");

    d.tick(kT.envOn + 0.001f, 0.f, 0.f, 200, ml);
    CHECK(d.envGate(), "das Gate oeffnet nicht ueber envOn");

    // Zwischen envOff und envOn bleibt es stehen - das ist die Hysterese.
    d.tick(0.5f * (kT.envOn + kT.envOff), 0.f, 0.f, 300, ml);
    CHECK(d.envGate(), "das Gate flattert im Hysteresefenster zu");

    d.tick(kT.envOff - 0.001f, 0.f, 0.f, 400, ml);
    CHECK(!d.envGate(), "das Gate schliesst nicht unter envOff");
}

// Die Kurzschlussauswertung ist kein Detail, sondern der Grund fuer den
// Zuschnitt: eine Inferenz je Takt waere Rechenzeit und Strom fuer nichts.
static void test_noInferenceWhileGateClosed() {
    PinchDetector d;
    MlStub ml;
    for (int i = 0; i < 50; i++) d.tick(0.001f, 0.f, 0.f, 10u * i, ml);
    CHECK(ml.calls == 0, "der Klassifikator laeuft bei geschlossenem Gate");

    d.tick(kT.envOn + 0.01f, 0.f, 0.f, 1000, ml);
    CHECK(ml.calls == 1, "der Klassifikator laeuft bei offenem Gate nicht");
}

// --- Das Armierungsfenster ---------------------------------------------

// DER Grund fuer das Fenster: das Gate oeffnet am Anfang des Impulses, aber das
// ML-Fenster enthaelt den Impuls erst 50 bis 150 ms spaeter. Wird nur bei
// offenem Gate klassifiziert, verliert man diesen Wettlauf - am Geraet kamen so
// nur rund 20 Prozent der Pinches an.
static void test_modelIsAskedAfterTheGateClosed() {
    PinchDetector d;
    MlStub ml;
    ml.answer = false;                                  // Modell noch unentschieden

    d.tick(kT.envOn + 0.01f, 0.f, 0.f, 1000, ml);            // Impuls: Gate auf, armiert
    d.tick(0.f, 0.f, 0.f, 1030, ml);                         // Huellkurve schon wieder unten
    CHECK(!d.envGate(), "das Gate ist nach dem Impuls noch offen");
    CHECK(d.armed(), "das Fenster endet mit dem Gate - der alte Wettlauf");

    // Jetzt erst wird das Modell sicher: der Impuls steht im Fenster.
    ml.answer = true;
    CHECK(d.tick(0.f, 0.f, 0.f, 1100, ml),
          "der Pinch wird verworfen, weil das Gate schon zu ist");
}

static void test_armWindowExpires() {
    PinchDetector d;
    MlStub ml;
    ml.answer = false;
    d.tick(kT.envOn + 0.01f, 0.f, 0.f, 1000, ml);
    d.tick(0.f, 0.f, 0.f, 1030, ml);

    ml.answer = true;
    CHECK(!d.tick(0.f, 0.f, 0.f, 1000 + kT.armMs + 50, ml),
          "das Armierungsfenster laeuft nie ab");
    CHECK(!d.armed(), "armed() meldet nach Ablauf noch true");
}

// Ohne Erschuetterung wird gar nicht klassifiziert - das ist der Grund fuer die
// ganze Konstruktion: keine Inferenz, solange nichts passiert.
static void test_noInferenceWithoutTrigger() {
    PinchDetector d;
    MlStub ml;
    for (int i = 0; i < 200; i++) d.tick(0.001f, 0.f, 0.f, 1000u + 5u * i, ml);
    CHECK(ml.calls == 0, "der Klassifikator laeuft ohne Erschuetterung");
    CHECK(!d.armed(), "ohne Erschuetterung ist das Fenster armiert");
}

// Nur jeder n-te Takt kostet eine Inferenz - sonst waeren es bei 209 Hz rund
// 52 Stueck je Pinch a 3 ms.
static void test_strideLimitsInferenceCount() {
    PinchTuning t;
    t.mlStride = 4;
    PinchDetector d(t);
    MlStub ml;
    ml.answer = false;

    d.tick(t.envOn + 0.01f, 0.f, 0.f, 1000, ml);
    for (int i = 1; i < 40; i++) d.tick(0.f, 0.f, 0.f, 1000u + 5u * i, ml);

    // 40 Takte, jeder vierte klassifiziert.
    CHECK(ml.calls >= 8 && ml.calls <= 12,
          "die Schrittweite begrenzt die Zahl der Inferenzen nicht");
}

// --- Die Flanke --------------------------------------------------------

static void test_firesOnlyOnRisingEdge() {
    PinchDetector d;
    MlStub ml;
    CHECK(d.tick(kT.envOn + 0.01f, 0.f, 0.f, 1000, ml), "die erste Flanke feuert nicht");
    CHECK(!d.tick(kT.envOn + 0.01f, 0.f, 0.f, 1005, ml),
          "ein anhaltender Pinch feuert in jedem Takt");
    CHECK(!d.tick(kT.envOn + 0.01f, 0.f, 0.f, 1010, ml), "dasselbe im dritten Takt");
}

static void test_noFireWhenModelSaysNo() {
    PinchDetector d;
    MlStub ml;
    ml.answer = false;
    CHECK(!d.tick(kT.envOn + 0.01f, 0.f, 0.f, 1000, ml),
          "feuert, obwohl das Modell ablehnt");
    CHECK(d.envGate(), "das Gate haengt am Modell statt an der Schwelle");
    CHECK(ml.calls == 1, "das Modell wurde bei offenem Gate nicht befragt");
}

// --- Entprellung und Gyro-Guard ---------------------------------------

static void test_debounceBlocksAndCounts() {
    PinchDetector d;
    MlStub ml;
    CHECK(d.tick(kT.envOn + 0.01f, 0.f, 0.f, 1000, ml), "der erste Pinch feuert nicht");
    d.tick(0.f, 0.f, 0.f, 1010, ml);                       // Gate zu, Flanke frei

    const uint32_t tooEarly = 1000 + kT.debounceMs - 20;
    CHECK(!d.tick(kT.envOn + 0.01f, 0.f, 0.f, tooEarly, ml),
          "der zweite Pinch kommt innerhalb der Entprellung durch");
    CHECK(d.blockedByDebounce() == 1, "die Ablehnung wird nicht gezaehlt");
    CHECK(d.blockedByGyro() == 0, "die Ablehnung wird dem falschen Zaehler angelastet");

    d.tick(0.f, 0.f, 0.f, tooEarly + 10, ml);
    const uint32_t lateEnough = 1000 + kT.debounceMs + 20;
    CHECK(d.tick(kT.envOn + 0.01f, 0.f, 0.f, lateEnough, ml),
          "nach der Entprellung feuert es nicht wieder");
}

static void test_gyroGuardBlocksAndCounts() {
    PinchDetector d;
    MlStub ml;
    CHECK(!d.tick(kT.envOn + 0.01f, kT.gyroGuardDps + 1.f, 0.f, 5000, ml),
          "eine Erschuetterung mitten in heftiger Bewegung feuert");
    CHECK(d.blockedByGyro() == 1, "die Ablehnung wird nicht gezaehlt");
    CHECK(d.blockedByDebounce() == 0, "die Ablehnung wird dem falschen Zaehler angelastet");

    d.tick(0.f, 0.f, 0.f, 5010, ml);
    CHECK(d.tick(kT.envOn + 0.01f, kT.gyroGuardDps - 1.f, 0.f, 6000, ml),
          "knapp unterhalb des Guards feuert es nicht");
}

// Die Reihenfolge ist beobachtbar und soll es bleiben: die Zaehler trennen
// "am Entprellfenster gescheitert" von "am Guard gescheitert", und bei zwei
// gleichzeitigen Gruenden waere sonst nicht klar, welcher gemeint ist.
static void test_debounceTakesPrecedenceOverGyro() {
    PinchDetector d;
    MlStub ml;
    d.tick(kT.envOn + 0.01f, 0.f, 0.f, 1000, ml);          // feuert
    d.tick(0.f, 0.f, 0.f, 1010, ml);
    d.tick(kT.envOn + 0.01f, kT.gyroGuardDps + 100.f, 0.f, 1050, ml);
    CHECK(d.blockedByDebounce() == 1 && d.blockedByGyro() == 0,
          "bei beiden Gruenden zaehlt nicht die Entprellung zuerst");
}

// --- Die Sperre nach dem Rechtsklick ----------------------------------

// Der Controller sperrt nach einem Rechtsklick laenger als die Entprellung:
// Haptik und Loese-Impuls des Fingers erzeugten sonst eine zweite Flanke, die
// das eben geoeffnete Kontextmenue wieder schloss.
static void test_holdOffOutlastsDebounce() {
    PinchDetector d;
    MlStub ml;
    CHECK(d.tick(kT.envOn + 0.01f, 0.f, 0.f, 1000, ml), "der erste Pinch feuert nicht");
    d.holdOff(1000, 700);

    d.tick(0.f, 0.f, 0.f, 1010, ml);                       // Gate zu, Flanke frei
    CHECK(!d.tick(kT.envOn + 0.01f, 0.f, 0.f, 1000 + kT.debounceMs + 20, ml),
          "die Sperre endet schon mit der Entprellung");
    CHECK(d.blockedByDebounce() == 1, "die gesperrte Flanke wird nicht gezaehlt");

    d.tick(0.f, 0.f, 0.f, 1650, ml);
    CHECK(d.tick(kT.envOn + 0.01f, 0.f, 0.f, 1701, ml),
          "nach Ablauf der Sperre feuert es nicht wieder");
}

// tLastPinch_ bleibt unberuehrt: die Sperre verlaengert das Fenster, sie
// verschiebt es nicht. Sonst zoege jede Sperre die Entprellung mit sich.
static void test_holdOffDoesNotRestartDebounce() {
    PinchDetector d;
    MlStub ml;
    d.tick(kT.envOn + 0.01f, 0.f, 0.f, 1000, ml);
    d.holdOff(1000, 300);
    d.tick(0.f, 0.f, 0.f, 1010, ml);

    CHECK(d.tick(kT.envOn + 0.01f, 0.f, 0.f, 1301, ml),
          "nach Ablauf der Sperre wirkt die Entprellung noch einmal von vorne");
}

// Haelt die Erschuetterung ueber die ganze Sperre an, bleibt wasHot_ wahr - es
// darf danach nicht nachzuenden, sondern erst bei der naechsten echten Flanke.
static void test_noReignitionAfterHoldOff() {
    PinchDetector d;
    MlStub ml;
    d.tick(kT.envOn + 0.01f, 0.f, 0.f, 1000, ml);
    d.holdOff(1000, 300);
    for (uint32_t t = 1005; t <= 1400; t += 5) {
        CHECK(!d.tick(kT.envOn + 0.01f, 0.f, 0.f, t, ml),
              "eine anhaltende Erschuetterung zuendet nach der Sperre nach");
    }
}

// --- Der gelockerte Pfad im Scroll-Modus -------------------------------

// Dort zaehlt allein die Huellkurve. Beide Bremsen stehen dem Rechtsklick
// systematisch im Weg: der Guard, weil das Scrollen selbst eine Armbewegung
// ist, und das Modell, weil sein Fenster diese Bewegung mit enthaelt.
static void test_relaxedIgnoresGyroGuard() {
    PinchDetector d;
    MlStub ml;
    const float loud = kT.gyroGuardDps + 150.f;
    CHECK(!d.tick(kT.envOn + 0.01f, loud, 0.f, 1000, ml), "der Guard greift ohne relaxed nicht");

    PinchDetector e;
    CHECK(e.tick(kT.envOn + 0.01f, loud, 0.f, 1000, ml, true),
          "der Gyro-Guard blockt den zweiten Pinch trotz offenem Fenster");
    CHECK(e.blockedByGyro() == 0, "der geblockte Zaehler laeuft trotz relaxed hoch");
}

static void test_relaxedSkipsTheModel() {
    PinchDetector d;
    MlStub ml;
    ml.answer = false;                                  // Modell lehnt ab
    CHECK(d.tick(kT.envOn + 0.01f, 0.f, 0.f, 1000, ml, true),
          "das ablehnende Modell blockt den zweiten Pinch");
    CHECK(ml.calls == 0, "das Modell wird trotz relaxed befragt");
}

// Die Entprellung bleibt: sie ist der untere Rand des Fensters und sperrt den
// Loese-Impuls des ersten Pinch aus.
static void test_relaxedStillRespectsDebounce() {
    PinchDetector d;
    MlStub ml;
    d.tick(kT.envOn + 0.01f, 0.f, 0.f, 1000, ml);
    d.tick(0.f, 0.f, 0.f, 1010, ml);
    CHECK(!d.tick(kT.envOn + 0.01f, 0.f, 0.f, 1000 + kT.debounceMs - 20, ml, true),
          "relaxed hebelt die Entprellung aus");
}

// --- Der Dreh-Guard ---------------------------------------------------

// Die Ein/Aus-Geste erschuettert das Board selbst, am staerksten am Scheitel
// der Drehung. Ohne diesen Guard wurde daraus im Scroll-Modus ein Rechtsklick,
// weil dort weder Modell noch Gyro-Guard gefragt werden.
static void test_twistGuardBlocksAndCounts() {
    PinchDetector d;
    MlStub ml;
    CHECK(!d.tick(kT.envOn + 0.01f, 0.f, kT.twistGuardDps + 1.f, 1000, ml),
          "die Erschuetterung einer Unterarmdrehung feuert");
    CHECK(d.blockedByTwist() == 1, "die Ablehnung wird nicht gezaehlt");
    CHECK(d.blockedByGyro() == 0 && d.blockedByDebounce() == 0,
          "die Ablehnung wird dem falschen Zaehler angelastet");

    d.tick(0.f, 0.f, 0.f, 1010, ml);
    CHECK(d.tick(kT.envOn + 0.01f, 0.f, kT.twistGuardDps - 1.f, 2000, ml),
          "knapp unterhalb des Dreh-Guards feuert es nicht");
}

// Der eine Guard, den relaxed NICHT aufhebt. Gescrollt wird durch Neigen und
// Schwenken des Arms, nicht durch Verdrehen - der Rechtsklick verliert also
// nichts, waehrend die Schaltgeste sicher stumm bleibt.
static void test_relaxedKeepsTheTwistGuard() {
    PinchDetector d;
    MlStub ml;
    CHECK(!d.tick(kT.envOn + 0.01f, kT.gyroGuardDps + 150.f,
                  kT.twistGuardDps + 1.f, 1000, ml, true),
          "die Schaltgeste loest im Scroll-Modus einen Rechtsklick aus");
    CHECK(d.blockedByTwist() == 1, "die Ablehnung wird trotz relaxed nicht gezaehlt");
}

// --- Das Einfrieren des Cursors ---------------------------------------

static void test_freezeFollowsTheGateNotAFixedTime() {
    PinchDetector d;
    MlStub ml;
    d.tick(kT.envOn + 0.01f, 0.f, 0.f, 1000, ml);
    CHECK(d.inFreeze(1000), "der Cursor friert waehrend des Pinches nicht ein");
    CHECK(d.inFreeze(1000 + kT.freezeMaxMs - 1), "das Fenster endet zu frueh");
    CHECK(!d.inFreeze(1000 + kT.freezeMaxMs),
          "die Notbremse freezeMaxMs greift nicht");

    // Ein kurzer, sauberer Pinch gibt den Cursor sofort wieder frei: schliesst
    // das Gate, ist das Einfrieren vorbei, auch weit vor freezeMaxMs.
    d.tick(0.f, 0.f, 0.f, 1005, ml);
    CHECK(!d.inFreeze(1006), "der Cursor bleibt nach geschlossenem Gate eingefroren");
}

int main() {
    test_gateOpensAndCloses();
    test_modelIsAskedAfterTheGateClosed();
    test_armWindowExpires();
    test_noInferenceWithoutTrigger();
    test_strideLimitsInferenceCount();
    test_noInferenceWhileGateClosed();
    test_firesOnlyOnRisingEdge();
    test_noFireWhenModelSaysNo();
    test_debounceBlocksAndCounts();
    test_gyroGuardBlocksAndCounts();
    test_debounceTakesPrecedenceOverGyro();
    test_holdOffOutlastsDebounce();
    test_holdOffDoesNotRestartDebounce();
    test_noReignitionAfterHoldOff();
    test_relaxedIgnoresGyroGuard();
    test_relaxedSkipsTheModel();
    test_relaxedStillRespectsDebounce();
    test_twistGuardBlocksAndCounts();
    test_relaxedKeepsTheTwistGuard();
    test_freezeFollowsTheGateNotAFixedTime();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
