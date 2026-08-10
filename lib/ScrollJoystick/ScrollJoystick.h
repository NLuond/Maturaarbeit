#pragma once
#include <stdint.h>
#include <math.h>

// Scrollen als Joystick: es zaehlt der gehaltene Neigungswinkel relativ zum
// Eintrittswinkel, nicht die Drehrate. Ein Positions- und kein Ratensignal,
// deshalb driftet es nicht weg.
//
// Ohne config.h und ohne Arduino.h, damit der PC-Test laeuft; die Werte stehen
// in ScrollTuning und sind per static_assert an cfg:: gebunden.
struct ScrollTuning {
    // Totzone um den Eintrittswinkel, Grad. Nicht auf null: sonst wuerde schon
    // das Zittern der gehaltenen Hand langsam scrollen.
    float    deadDeg    = 3.f;

    float    gain       = 1.2f;   // Schritte/s pro Grad ausserhalb der Totzone
    float    maxHz      = 25.f;   // Schritte/s Obergrenze
    float    invert     = 1.f;    // -1.f dreht die Richtung um
    uint32_t intervalMs = 40;     // Ausgabetakt
};

class ScrollJoystick {
public:
    explicit ScrollJoystick(const ScrollTuning& t = ScrollTuning()) : t_(t) {}

    // Beim Eintritt wird die aktuelle Neigung zur Mitte. Eingang ist die
    // Armneigung, nicht die Handverdrehung - die steht waehrend des Scrollens
    // konstant bei rund 90 Grad.
    void enter(float elevDeg) {
        refElev_ = elevDeg;
        acc_     = 0.f;
        rate_    = 0.f;
        dead_    = true;
    }

    // Liefert die Anzahl Wheel-Schritte fuer diesen Aufruf, meistens 0.
    int8_t update(float elevDeg, float dt, uint32_t now_ms) {
        const float dev = elevDeg - refElev_;
        const float mag = fabsf(dev) - t_.deadDeg;
        dead_ = (mag <= 0.f);

        if (dead_) { rate_ = 0.f; return 0; }

        rate_ = mag * t_.gain;
        if (rate_ > t_.maxHz) rate_ = t_.maxHz;
        if (dev < 0.f) rate_ = -rate_;
        rate_ *= t_.invert;

        acc_ += rate_ * dt;

        if (now_ms - tLast_ < t_.intervalMs) return 0;
        tLast_ = now_ms;

        int steps = (int)acc_;              // schneidet Richtung null ab
        if (steps == 0) return 0;
        if (steps >  127) steps =  127;
        if (steps < -127) steps = -127;
        acc_ -= steps;                      // Rest bleibt stehen und laeuft auf
        return (int8_t)steps;
    }

    // Schritte pro Sekunde, vorzeichenbehaftet. Nur fuer den Teleplot-Kanal.
    float rateHz() const { return rate_; }

    // Der Zustandsautomat entscheidet daran, ob ein Pinch in der abgedrehten
    // Haltung als Rechtsklick gilt.
    bool inDeadzone() const { return dead_; }

private:
    ScrollTuning t_;
    float    refElev_ = 0.f;
    float    acc_     = 0.f;
    float    rate_    = 0.f;
    uint32_t tLast_   = 0;
    bool     dead_    = true;
};
