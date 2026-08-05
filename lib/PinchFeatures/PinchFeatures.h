#pragma once
#include "ImuSample.h"

// Einzige Stelle, an der die Kanaele des Modells festgelegt sind.
//
// Der COLLECT_MODE in main.cpp (erzeugt die Trainingsdaten) und der
// Inferenz-Pfad in PinchClassifier (erzeugt das Fenster zur Laufzeit) rufen
// beide diese Funktion auf. Vorher stand die Reihenfolge an zwei Orten - eine
// dort vertauschte Achse gibt weder Compiler- noch Laufzeitfehler, das Modell
// wird nur still schlechter und der Fehler ist in der Auswertung nicht von
// einem Modellproblem zu unterscheiden.
//
// Aenderungen hier machen jedes bisher trainierte Modell ungueltig.
namespace feat {

    constexpr int CHANNELS = 5;

    inline void pack(const ImuSample& s, float env, float* out) {
        out[0] = env;
        out[1] = s.gyroSum / 100.f;  // auf eine aehnliche Groessenordnung wie g
        out[2] = s.ax;
        out[3] = s.ay;
        out[4] = s.az;
    }

}
