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

    d.tick(kT.envOn - 0.001f, 0.f, 100, ml);
    CHECK(!d.envGate(), "das Gate oeffnet schon unterhalb von envOn");

    d.tick(kT.envOn + 0.001f, 0.f, 200, ml);
    CHECK(d.envGate(), "das Gate oeffnet nicht ueber envOn");

    // Zwischen envOff und envOn bleibt es stehen - das ist die Hysterese.
    d.tick(0.5f * (kT.envOn + kT.envOff), 0.f, 300, ml);
    CHECK(d.envGate(), "das Gate flattert im Hysteresefenster zu");

    d.tick(kT.envOff - 0.001f, 0.f, 400, ml);
    CHECK(!d.envGate(), "das Gate schliesst nicht unter envOff");
}

// Die Kurzschlussauswertung ist kein Detail, sondern der Grund fuer den
// Zuschnitt: eine Inferenz je Takt waere Rechenzeit und Strom fuer nichts.
static void test_noInferenceWhileGateClosed() {
    PinchDetector d;
    MlStub ml;
    for (int i = 0; i < 50; i++) d.tick(0.001f, 0.f, 10u * i, ml);
    CHECK(ml.calls == 0, "der Klassifikator laeuft bei geschlossenem Gate");

    d.tick(kT.envOn + 0.01f, 0.f, 1000, ml);
    CHECK(ml.calls == 1, "der Klassifikator laeuft bei offenem Gate nicht");
}

// --- Die Flanke --------------------------------------------------------

static void test_firesOnlyOnRisingEdge() {
    PinchDetector d;
    MlStub ml;
    CHECK(d.tick(kT.envOn + 0.01f, 0.f, 1000, ml), "die erste Flanke feuert nicht");
    CHECK(!d.tick(kT.envOn + 0.01f, 0.f, 1005, ml),
          "ein anhaltender Pinch feuert in jedem Takt");
    CHECK(!d.tick(kT.envOn + 0.01f, 0.f, 1010, ml), "dasselbe im dritten Takt");
}

static void test_noFireWhenModelSaysNo() {
    PinchDetector d;
    MlStub ml;
    ml.answer = false;
    CHECK(!d.tick(kT.envOn + 0.01f, 0.f, 1000, ml),
          "feuert, obwohl das Modell ablehnt");
    CHECK(d.envGate(), "das Gate haengt am Modell statt an der Schwelle");
    CHECK(ml.calls == 1, "das Modell wurde bei offenem Gate nicht befragt");
}

// --- Entprellung und Gyro-Guard ---------------------------------------

static void test_debounceBlocksAndCounts() {
    PinchDetector d;
    MlStub ml;
    CHECK(d.tick(kT.envOn + 0.01f, 0.f, 1000, ml), "der erste Pinch feuert nicht");
    d.tick(0.f, 0.f, 1010, ml);                       // Gate zu, Flanke frei

    const uint32_t tooEarly = 1000 + kT.debounceMs - 20;
    CHECK(!d.tick(kT.envOn + 0.01f, 0.f, tooEarly, ml),
          "der zweite Pinch kommt innerhalb der Entprellung durch");
    CHECK(d.blockedByDebounce() == 1, "die Ablehnung wird nicht gezaehlt");
    CHECK(d.blockedByGyro() == 0, "die Ablehnung wird dem falschen Zaehler angelastet");

    d.tick(0.f, 0.f, tooEarly + 10, ml);
    const uint32_t lateEnough = 1000 + kT.debounceMs + 20;
    CHECK(d.tick(kT.envOn + 0.01f, 0.f, lateEnough, ml),
          "nach der Entprellung feuert es nicht wieder");
}

static void test_gyroGuardBlocksAndCounts() {
    PinchDetector d;
    MlStub ml;
    CHECK(!d.tick(kT.envOn + 0.01f, kT.gyroGuardDps + 1.f, 5000, ml),
          "eine Erschuetterung mitten in heftiger Bewegung feuert");
    CHECK(d.blockedByGyro() == 1, "die Ablehnung wird nicht gezaehlt");
    CHECK(d.blockedByDebounce() == 0, "die Ablehnung wird dem falschen Zaehler angelastet");

    d.tick(0.f, 0.f, 5010, ml);
    CHECK(d.tick(kT.envOn + 0.01f, kT.gyroGuardDps - 1.f, 6000, ml),
          "knapp unterhalb des Guards feuert es nicht");
}

// Die Reihenfolge ist beobachtbar und soll es bleiben: die Zaehler trennen
// "am Entprellfenster gescheitert" von "am Guard gescheitert", und bei zwei
// gleichzeitigen Gruenden waere sonst nicht klar, welcher gemeint ist.
static void test_debounceTakesPrecedenceOverGyro() {
    PinchDetector d;
    MlStub ml;
    d.tick(kT.envOn + 0.01f, 0.f, 1000, ml);          // feuert
    d.tick(0.f, 0.f, 1010, ml);
    d.tick(kT.envOn + 0.01f, kT.gyroGuardDps + 100.f, 1050, ml);
    CHECK(d.blockedByDebounce() == 1 && d.blockedByGyro() == 0,
          "bei beiden Gruenden zaehlt nicht die Entprellung zuerst");
}

// --- Das Einfrieren des Cursors ---------------------------------------

static void test_freezeFollowsTheGateNotAFixedTime() {
    PinchDetector d;
    MlStub ml;
    d.tick(kT.envOn + 0.01f, 0.f, 1000, ml);
    CHECK(d.inFreeze(1000), "der Cursor friert waehrend des Pinches nicht ein");
    CHECK(d.inFreeze(1000 + kT.freezeMaxMs - 1), "das Fenster endet zu frueh");
    CHECK(!d.inFreeze(1000 + kT.freezeMaxMs),
          "die Notbremse freezeMaxMs greift nicht");

    // Ein kurzer, sauberer Pinch gibt den Cursor sofort wieder frei: schliesst
    // das Gate, ist das Einfrieren vorbei, auch weit vor freezeMaxMs.
    d.tick(0.f, 0.f, 1005, ml);
    CHECK(!d.inFreeze(1006), "der Cursor bleibt nach geschlossenem Gate eingefroren");
}

int main() {
    test_gateOpensAndCloses();
    test_noInferenceWhileGateClosed();
    test_firesOnlyOnRisingEdge();
    test_noFireWhenModelSaysNo();
    test_debounceBlocksAndCounts();
    test_gyroGuardBlocksAndCounts();
    test_debounceTakesPrecedenceOverGyro();
    test_freezeFollowsTheGateNotAFixedTime();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
