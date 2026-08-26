// Test des Lage-Setzens aus der Schwerkraft. Laeuft auf dem PC - MadgwickAHRS
// haengt bewusst an keiner Hardware.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/MadgwickAHRS -o build/mad.exe test/test_madgwick_seed/test_madgwick_seed.cpp && ./build/mad.exe
//
#include "MadgwickAHRS.h"
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

// Der Kern: nach dem Setzen muss die geschaetzte Richtung von "oben" mit dem
// gemessenen Beschleunigungsvektor zusammenfallen.
static void expectUp(float ax, float ay, float az, const char* what) {
    MadgwickAHRS m;
    m.seedFromAccel(ax, ay, az);

    const float n = std::sqrt(ax*ax + ay*ay + az*az);
    const float tol = 1e-4f;
    CHECK(std::fabs(m.upX() - ax/n) < tol, what);
    CHECK(std::fabs(m.upY() - ay/n) < tol, what);
    CHECK(std::fabs(m.upZ() - az/n) < tol, what);
}

// Die sechs Achsenlagen, dazu eine schraege - jede Vorzeichenverwechslung in
// der Quaternion faellt hier auf.
static void test_axisAlignedPoses() {
    expectUp( 0.f,  0.f,  1.f, "flach, Chip oben");
    expectUp( 0.f,  0.f, -1.f, "flach, auf dem Kopf");
    expectUp( 1.f,  0.f,  0.f, "um 90 Grad verdreht");
    expectUp(-1.f,  0.f,  0.f, "um -90 Grad verdreht");
    expectUp( 0.f,  1.f,  0.f, "Unterarm senkrecht nach oben");
    expectUp( 0.f, -1.f,  0.f, "Unterarm senkrecht nach unten");
    expectUp( 0.3f, -0.5f, 0.8f, "schraege Lage");
}

// Der Messwert kommt roh aus dem Sensor und ist nie exakt 1 g lang.
static void test_unnormalisedInput() {
    expectUp(0.f, 0.f, 2.f,       "doppelter Betrag");
    expectUp(0.15f, 0.02f, 0.97f, "leicht schraeg, knapp unter 1 g");
}

// Ohne Eingabe darf nichts kaputtgehen: die bisherige Lage bleibt stehen.
static void test_zeroVectorKeepsPose() {
    MadgwickAHRS m;
    m.seedFromAccel(1.f, 0.f, 0.f);
    m.seedFromAccel(0.f, 0.f, 0.f);
    CHECK(std::fabs(m.upX() - 1.f) < 1e-4f, "der Nullvektor hat die Lage verworfen");
}

// Das eigentliche Versprechen: gesetzt statt eingeschwungen. Ohne Setzen
// braucht der Filter aus der Flach-Annahme viele Takte bis zur wahren Lage.
static void test_seedIsInstantWhereConvergenceIsNot() {
    const float dt = 1.f / 208.f;
    // Unterarm senkrecht: 90 Grad weg von der Startannahme.
    const float ax = 0.f, ay = 1.f, az = 0.f;

    MadgwickAHRS convergent(0.033f);
    for (int i = 0; i < 208; i++) convergent.update(0,0,0, ax,ay,az, dt);
    CHECK(convergent.upY() < 0.5f,
          "der Filter waere nach einer Sekunde schon eingelaufen - Test wertlos");

    MadgwickAHRS seeded(0.033f);
    seeded.seedFromAccel(ax, ay, az);
    CHECK(seeded.upY() > 0.999f, "das Setzen trifft die Lage nicht sofort");
}

// Nach dem Setzen laeuft der Filter normal weiter und bleibt bei ruhigem
// Sensor auf der gesetzten Lage stehen.
static void test_stableAfterSeed() {
    const float dt = 1.f / 208.f;
    MadgwickAHRS m(0.033f);
    m.seedFromAccel(1.f, 0.f, 0.f);
    for (int i = 0; i < 416; i++) m.update(0,0,0, 1.f,0.f,0.f, dt);
    CHECK(std::fabs(m.upX() - 1.f) < 1e-3f, "die gesetzte Lage laeuft davon");
}

int main() {
    test_axisAlignedPoses();
    test_unnormalisedInput();
    test_zeroVectorKeepsPose();
    test_seedIsInstantWhereConvergenceIsNot();
    test_stableAfterSeed();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
