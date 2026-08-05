// Test des 1-Euro-Filters. Laeuft auf dem PC - OneEuro.h braucht nur <math.h>.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/Filters -o build/euro.exe test/test_one_euro.cpp && ./build/euro.exe
//
// Geprueft wird die Eigenschaft, um die es geht: Handzittern darf die
// Grenzfrequenz NICHT aufreissen, eine gehaltene Bewegung schon.
#include "OneEuro.h"
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

static const float DT = 1.f / 209.f;   // Abtastrate der Firmware

// Effektivwert der Filterausgabe fuer ein reines Zittersignal. Der Aufrufer
// gibt Frequenz und Amplitude in Grad/s vor; speed ist wie in
// OrientationPointer der Betrag der Rate.
static float tremorRms(OneEuroFilter& f, float hz, float amp, float seconds) {
    double sum = 0.0;
    int    n   = 0;
    const int steps = (int)(seconds / DT);
    for (int i = 0; i < steps; i++) {
        const float t = i * DT;
        const float x = amp * std::sin(2.f * 3.14159265f * hz * t);
        const float y = f.update(x, std::fabs(x), DT);
        // Erste halbe Sekunde ist Einschwingen und zaehlt nicht mit.
        if (t > 0.5f) { sum += (double)y * y; n++; }
    }
    return n ? (float)std::sqrt(sum / n) : 0.f;
}

// Tremor mit 10 Hz und 12 Grad/s muss deutlich gedaempft werden. Der
// Effektivwert eines Sinus ist Amplitude/sqrt(2) = 8.49.
static void test_tremorIsAttenuated() {
    OneEuroFilter f(1.0f, 0.2f, 1.0f);
    const float rms = tremorRms(f, 10.f, 12.f, 3.f);
    CHECK(rms < 0.45f * 8.49f, "Tremor wird kaum gedaempft");
}

// Die Gegenprobe gegen die bisherige Einstellung. Geprueft wird die
// Anforderung - deutlich weniger Wackeln - und nicht der Mechanismus: der
// wirksame Hebel ist beta, dcutoff ist die Korrektheitsreparatur daneben.
static void test_neueEinstellungDaempftStaerker() {
    OneEuroFilter alt(0.9f, 0.55f, 1000.0f);   // wie bisher, ungeglaettete Rate
    OneEuroFilter neu(1.0f, 0.20f,    1.0f);
    const float rmsAlt = tremorRms(alt, 10.f, 12.f, 3.f);
    const float rmsNeu = tremorRms(neu, 10.f, 12.f, 3.f);
    CHECK(rmsNeu < 0.7f * rmsAlt, "die neue Einstellung daempft nicht spuerbar staerker");
}

// Eine gehaltene Bewegung darf nicht traege werden: nach einem Sprung auf
// 100 Grad/s muss der Filter binnen 200 ms mindestens 90 Prozent erreichen.
static void test_stepIsFast() {
    OneEuroFilter f(1.0f, 0.2f, 1.0f);
    const int steps = (int)(0.200f / DT);
    float y = 0.f;
    for (int i = 0; i < steps; i++) y = f.update(100.f, 100.f, DT);
    CHECK(y > 90.f, "Sprungantwort zu langsam");
}

// Ein konstanter Eingang muss exakt erreicht werden, sonst bliebe ein
// dauerhafter Versatz im Cursor stehen.
static void test_convergesToConstant() {
    OneEuroFilter f(1.0f, 0.2f, 1.0f);
    float y = 0.f;
    for (int i = 0; i < 5000; i++) y = f.update(7.f, 7.f, DT);
    CHECK(std::fabs(y - 7.f) < 1e-3f, "konvergiert nicht auf den Eingang");
}

// Der erste Wert nach reset() wird uebernommen, nicht aus der Vorgeschichte
// hochgezogen - sonst laeuft beim Haltungswechsel ein Rest in den Cursor.
static void test_resetTakesFirstSample() {
    OneEuroFilter f(1.0f, 0.2f, 1.0f);
    for (int i = 0; i < 500; i++) f.update(50.f, 50.f, DT);
    f.reset();
    const float y = f.update(3.f, 3.f, DT);
    CHECK(std::fabs(y - 3.f) < 1e-6f, "reset uebernimmt den ersten Wert nicht");
}

// dt <= 0 darf den Filter nicht zerstoeren (Division durch null).
static void test_zeroDtIsSafe() {
    OneEuroFilter f(1.0f, 0.2f, 1.0f);
    f.update(5.f, 5.f, DT);
    const float y = f.update(9.f, 9.f, 0.f);
    CHECK(std::isfinite(y), "dt = 0 liefert keinen endlichen Wert");
}

int main() {
    test_tremorIsAttenuated();
    test_neueEinstellungDaempftStaerker();
    test_stepIsFast();
    test_convergesToConstant();
    test_resetTakesFirstSample();
    test_zeroDtIsSafe();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
