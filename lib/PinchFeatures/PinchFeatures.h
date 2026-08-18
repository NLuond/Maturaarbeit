#pragma once
#include "ImuSample.h"

// Einzige Stelle, an der die Kanaele des Modells festgelegt sind: sowohl der
// COLLECT_MODE in main.cpp (Trainingsdaten) als auch das Inferenz-Fenster in
// PinchClassifier gehen hier durch. Stuende die Reihenfolge an zwei Orten,
// gaebe eine vertauschte Achse weder Compiler- noch Laufzeitfehler - das Modell
// wuerde nur still schlechter. Aenderungen hier machen jedes bisher trainierte
// Modell ungueltig.
namespace feat {

    constexpr int CHANNELS = 5;

    inline void pack(const ImuSample& s, float env, float* out) {
        out[0] = env;
        out[1] = s.gyroSum / 100.f;  // auf eine aehnliche Groessenordnung wie g
        out[2] = s.lax;              // linear, nicht roh - siehe ImuSample.h
        out[3] = s.lay;
        out[4] = s.laz;
    }

}
