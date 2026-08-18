#pragma once
#include <stdint.h>
#include <math.h>

// Macht aus der Zeigerbewegung Radschritte: im Scroll-Modus geht die senkrechte
// Bewegung hierhin statt an den Cursor. Damit zaehlt derselbe Weg wie beim
// Zeigen und es ist nur eine Bewegungsart zu lernen.
//
// Ohne config.h und ohne Arduino.h, damit der PC-Test laeuft; die Werte stehen
// in ScrollTuning und sind per static_assert an cfg:: gebunden.
struct ScrollTuning {
    // Pixel je Radschritt. Gross genug, dass eine ruhig gehaltene Hand nicht
    // von selbst scrollt - die Totzone des Zeigers greift hier nicht mehr.
    float    pxPerStep  = 40.f;

    float    maxPerTick = 8.f;    // Obergrenze je Ausgabe, gegen Spruenge
    float    invert     = 1.f;    // -1.f dreht die Richtung um
    uint32_t intervalMs = 40;     // Ausgabetakt
};

class ScrollWheel {
public:
    explicit ScrollWheel(const ScrollTuning& t = ScrollTuning()) : t_(t) {}

    // Ausserhalb des Scroll-Modus aufrufen: der aufgelaufene Rest darf nicht in
    // die naechste Sitzung ueberschwappen.
    void reset() { acc_ = 0.f; }

    // dyPx: senkrechte Zeigerbewegung dieses Takts, in Pixeln.
    // Rueckgabe: Radschritte fuer diesen Aufruf, meistens 0.
    int8_t update(float dyPx, uint32_t now_ms) {
        acc_ += (dyPx / t_.pxPerStep) * t_.invert;

        if (now_ms - tLast_ < t_.intervalMs) return 0;
        tLast_ = now_ms;

        float steps = truncf(acc_);          // schneidet Richtung null ab
        if (steps == 0.f) return 0;
        if (steps >  t_.maxPerTick) steps =  t_.maxPerTick;
        if (steps < -t_.maxPerTick) steps = -t_.maxPerTick;

        acc_ -= steps;                       // Rest bleibt stehen und laeuft auf
        return (int8_t)steps;
    }

    // Nur fuer den Teleplot-Kanal sacc.
    float pending() const { return acc_; }

private:
    ScrollTuning t_;
    float    acc_   = 0.f;
    uint32_t tLast_ = 0;
};
