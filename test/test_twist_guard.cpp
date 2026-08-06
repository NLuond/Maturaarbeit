// Test der Verdrehungs-Bremse. Laeuft auf dem PC - TwistGuard braucht nur
// <math.h> und <stdint.h>.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/TwistGuard -o build/guard.exe test/test_twist_guard.cpp && ./build/guard.exe
//
#include "TwistGuard.h"
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

static const float DT = 1.f / 209.f;

// Dreht mit konstanter Rate weiter und liefert den letzten Faktor.
static float turnAt(TwistGuard& g, float& deg, float dps, float seconds) {
    float out = 1.f;
    const int steps = (int)(seconds / DT);
    for (int i = 0; i < steps; i++) {
        deg += dps * DT;
        out  = g.update(deg, DT);
    }
    return out;
}

// Ruhig gehaltene Hand: der Zeiger muss voll durchkommen.
static void test_stillMeansFullGain() {
    TwistGuard g;
    float deg = 0.f;
    CHECK(turnAt(g, deg, 0.f, 1.f) > 0.99f, "ruhige Hand wird gebremst");
}

// Der allererste Aufruf darf nicht bremsen: ohne Vorwert waere die Ableitung
// sonst riesig und der Cursor beim Einschalten kurz tot.
static void test_firstSampleDoesNotBrake() {
    TwistGuard g;
    const float out = g.update(90.f, DT);
    CHECK(out > 0.99f, "der erste Aufruf bremst");
}

// Eine zuegige Verdrehung muss den Zeiger vollstaendig stilllegen.
static void test_fastTwistClosesFully() {
    TwistGuard g;
    float deg = 0.f;
    turnAt(g, deg, 0.f, 0.5f);
    CHECK(turnAt(g, deg, 200.f, 0.3f) < 0.01f, "schnelle Verdrehung bremst nicht");
}

// Eine langsame Verdrehung ist keine Geste, sondern Zeigen mit leicht
// mitdrehender Hand - sie darf den Cursor nicht abwuergen.
static void test_slowTwistPassesThrough() {
    TwistGuard g;
    float deg = 0.f;
    turnAt(g, deg, 0.f, 0.5f);
    CHECK(turnAt(g, deg, 10.f, 0.5f) > 0.9f, "langsame Verdrehung bremst zu stark");
}

// Das Vorzeichen darf keine Rolle spielen.
static void test_signDoesNotMatter() {
    TwistGuard g;
    float deg = 0.f;
    turnAt(g, deg, 0.f, 0.5f);
    CHECK(turnAt(g, deg, -200.f, 0.3f) < 0.01f, "Verdrehung nach der anderen Seite bremst nicht");
}

// Sofort zu, langsam wieder auf. Am Ende einer Drehung klingt die Rate aus und
// kreuzt die Schwelle mehrfach; ohne begrenzte Rueckkehr zuckte der Cursor
// dabei wiederholt an.
static void test_releaseIsSlewLimited() {
    TwistGuard g;
    float deg = 0.f;
    turnAt(g, deg, 0.f, 0.5f);
    turnAt(g, deg, 200.f, 0.3f);              // zu

    const float kurz = turnAt(g, deg, 0.f, 0.05f);
    CHECK(kurz < 0.6f, "die Bremse oeffnet zu schnell wieder");

    const float lang = turnAt(g, deg, 0.f, 0.5f);
    CHECK(lang > 0.99f, "die Bremse oeffnet gar nicht mehr");
}

// Der Faktor bleibt in seinen Grenzen - er multipliziert eine Pixelzahl.
static void test_gainStaysBounded() {
    TwistGuard g;
    float deg = 0.f;
    for (int i = 0; i < 2000; i++) {
        const float dps = (i % 7 == 0) ? 300.f : ((i % 3 == 0) ? -150.f : 5.f);
        deg += dps * DT;
        const float out = g.update(deg, DT);
        if (out < 0.f || out > 1.f) { CHECK(false, "Faktor ausserhalb 0..1"); return; }
    }
    CHECK(true, "Faktor bleibt in den Grenzen");
}

// Der Winkel springt bei plus/minus 180 Grad um. Ohne wrapDeg waere der Sprung
// eine Differenz von 358 statt 2 Grad, also eine scheinbare Rate von rund
// 75'000 Grad/s statt der tatsaechlichen ~420.
//
// Geprueft wird die Rate und nicht der Faktor: 2 Grad in einem Takt SIND eine
// schnelle Drehung, die Bremse darf und soll dabei zugehen. Falsch waere nur
// die Groessenordnung. Die Schranke liegt weit ueber dem richtigen Wert und
// weit unter dem falschen, trifft also keine Aussage ueber die Glaettung.
static void test_wrapAroundIsNotARate() {
    TwistGuard g;
    float deg = 179.f;
    turnAt(g, deg, 0.f, 0.5f);
    deg = -179.f;                              // Sprung ueber die Grenze
    g.update(deg, DT);
    CHECK(g.rateDps() < 1000.f, "der Umschlag bei 180 Grad wird als Drehung gelesen");
}

// reset() wird aufgerufen, waehrend TwistToggle mitten in der Drehung
// schaltet (Toggle feuert schon bei backDeg=30, der Arm ist noch lange nicht
// zurueck bei 0). Die Bremse muss dabei zu bleiben - sonst liefe der Cursor
// fuer die restliche Drehung ungebremst weiter.
static void test_resetDoesNotReopenBrakeDuringRotation() {
    TwistGuard g;
    float deg = 0.f;
    turnAt(g, deg, 0.f, 0.5f);
    turnAt(g, deg, 200.f, 0.3f);              // Bremse zu waehrend der Drehung
    CHECK(g.rateDps() > 100.f, "Testverdrehung ist fuer den Test nicht schnell genug");
    CHECK(g.gain() < 0.1f, "Bremse ist vor dem reset() noch nicht zu");

    g.reset();
    const float out = g.update(deg, DT);      // erster Takt nach reset(), Winkel unveraendert
    CHECK(out < 0.1f, "reset() oeffnet die Bremse mitten in der Drehung");
}

int main() {
    test_stillMeansFullGain();
    test_firstSampleDoesNotBrake();
    test_fastTwistClosesFully();
    test_slowTwistPassesThrough();
    test_signDoesNotMatter();
    test_releaseIsSlewLimited();
    test_gainStaysBounded();
    test_wrapAroundIsNotARate();
    test_resetDoesNotReopenBrakeDuringRotation();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
