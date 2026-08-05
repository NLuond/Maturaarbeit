#pragma once
#include <Arduino.h>
#include <math.h>
#include "config.h"

// Scrollen als Joystick statt als Geste: nach dem Abdrehen zaehlt nicht mehr
// die Drehrate, sondern der gehaltene Neigungswinkel. Weiter geneigt heisst
// schneller scrollen, zurueck in die Mitte heisst Stopp. Das ist ein
// Positions- und kein Ratensignal, deshalb driftet es nicht weg.
class ScrollJoystick {
public:
    // Beim Eintritt in den Scroll-Modus wird die aktuelle Neigung zur Mitte.
    // elevDeg ist die Armneigung aus der Waagerechten (arm::elevDeg) - die
    // Handverdrehung waehlt den Modus aus und taugt hier nicht als Eingang.
    void enter(float elevDeg) {
        refElev_ = elevDeg;
        acc_     = 0.f;
        rate_    = 0.f;
    }

    // Liefert die Anzahl Wheel-Schritte fuer diesen Aufruf, meistens 0.
    int8_t update(float elevDeg, float dt, uint32_t now_ms) {
        const float dev = elevDeg - refElev_;
        const float mag = fabsf(dev) - cfg::SCROLL_DEAD_DEG;

        if (mag <= 0.f) { rate_ = 0.f; return 0; }

        rate_ = mag * cfg::SCROLL_GAIN;
        if (rate_ > cfg::SCROLL_MAX_HZ) rate_ = cfg::SCROLL_MAX_HZ;
        if (dev < 0.f) rate_ = -rate_;
        rate_ *= cfg::SCROLL_INVERT;

        acc_ += rate_ * dt;

        if (now_ms - tLast_ < cfg::SCROLL_INTERVAL_MS) return 0;
        tLast_ = now_ms;

        int steps = (int)acc_;              // schneidet Richtung null ab
        if (steps == 0) return 0;
        steps = constrain(steps, -127, 127);
        acc_ -= steps;
        return (int8_t)steps;
    }

    float rate() const { return rate_; }

private:
    float    refElev_ = 0.f;
    float    acc_     = 0.f;
    float    rate_    = 0.f;
    uint32_t tLast_   = 0;
};
