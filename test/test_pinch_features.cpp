// Test der Kanalbelegung des Modells. Laeuft auf dem PC - PinchFeatures.h und
// ImuSample.h haengen bewusst an keiner Hardware.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/ImuReader -I lib/PinchFeatures \
//       -o build/feat.exe test/test_pinch_features.cpp && ./build/feat.exe
//
// Warum es diesen Test gibt: die Kanal-Reihenfolge steht nur an dieser einen
// Stelle, und eine dort vertauschte Achse gibt weder Compiler- noch
// Laufzeitfehler. Das Modell wird nur still schlechter, und in der Auswertung
// ist das von einem Modellproblem nicht zu unterscheiden.
#include "PinchFeatures.h"
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

// Nicht "near" nennen: das ist auf Windows-Toolchains stellenweise ein Makro.
static bool istGleich(float a, float b) { return std::fabs(a - b) < 1e-5f; }

static void test_channelCount() {
    // Das Modell erwartet genau so viele Kanaele je Zeitschritt. Aendert sich
    // die Zahl, ist jedes trainierte Modell ungueltig.
    CHECK(feat::CHANNELS == 5, "Kanalzahl ist nicht mehr 5");
}

static void test_channelOrder() {
    ImuSample s{};
    s.ax  = 9.f;  s.ay  = 8.f;  s.az  = 7.f;    // roh: darf NICHT im Fenster landen
    s.lax = 1.f;  s.lay = 2.f;  s.laz = 3.f;    // linear: gehoert hinein
    s.gyroSum = 250.f;

    float f[feat::CHANNELS];
    feat::pack(s, 0.042f, f);

    CHECK(istGleich(f[0], 0.042f), "Kanal 0 ist nicht die Huellkurve");
    CHECK(istGleich(f[1], 2.5f),   "Kanal 1 ist nicht gyroSum/100");
    CHECK(istGleich(f[2], 1.f),    "Kanal 2 ist nicht lax");
    CHECK(istGleich(f[3], 2.f),    "Kanal 3 ist nicht lay");
    CHECK(istGleich(f[4], 3.f),    "Kanal 4 ist nicht laz");
}

static void test_rawAccelIsNotUsed() {
    // Der Kern der Aenderung: die Handhaltung darf im Fenster nicht sichtbar
    // sein. Dieselbe Bewegung, aber um 90 Grad verdrehte Erdbeschleunigung,
    // muss dasselbe Fenster ergeben.
    ImuSample flach{}, gedreht{};
    flach.lax = gedreht.lax = 0.10f;
    flach.lay = gedreht.lay = -0.05f;
    flach.laz = gedreht.laz = 0.02f;
    flach.gyroSum = gedreht.gyroSum = 30.f;
    flach.az   = 1.f;    // Hand gerade:   Erdbeschleunigung auf Z
    gedreht.ax = 1.f;    // Hand gedreht:  Erdbeschleunigung auf X

    float a[feat::CHANNELS], b[feat::CHANNELS];
    feat::pack(flach,   0.05f, a);
    feat::pack(gedreht, 0.05f, b);

    for (int i = 0; i < feat::CHANNELS; i++) {
        CHECK(istGleich(a[i], b[i]), "Handhaltung ist im Modell-Fenster sichtbar");
    }
}

int main() {
    test_channelCount();
    test_channelOrder();
    test_rawAccelIsNotUsed();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
