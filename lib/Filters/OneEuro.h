#pragma once
#include <math.h>

// 1-Euro-Filter nach Casiez, Roussel und Vogel (CHI 2012): ein Tiefpass, dessen
// Grenzfrequenz mit der Bewegungsgeschwindigkeit mitwaechst. Steht die Hand
// still, filtert er stark und unterdrueckt das Zittern; bewegt sie sich, oeffnet
// er und erzeugt kaum Verzoegerung.
//
// Abweichung vom Original: dort wird die Geschwindigkeit aus dem verrauschten
// Positionssignal geschaetzt, hier liefert das Gyroskop sie direkt. Die
// Ableitungsstufe entfaellt deshalb, der Tiefpass auf der Geschwindigkeit
// (dCutoff) aber nicht - ohne ihn stuende die Grenzfrequenz bei Handzittern
// genau auf den Spitzen am weitesten offen.
class OneEuroFilter {
public:
    // minCutoff in Hz, beta in Hz pro Einheit von speed, dCutoff in Hz - ohne
    // Vorgabewert, damit er an jeder Aufrufstelle sichtbar ist.
    OneEuroFilter(float minCutoff, float beta, float dCutoff)
        : minCutoff_(minCutoff), beta_(beta), dCutoff_(dCutoff) {}

    // speed = Betrag der Bewegung, steuert die Grenzfrequenz.
    float update(float x, float speed, float dt) {
        if (dt <= 0.f) return y_;

        const float s = fabsf(speed);

        if (!init_) { y_ = x; sp_ = s; init_ = true; return y_; }

        sp_ += alphaFor(dCutoff_, dt) * (s - sp_);

        const float cutoff = minCutoff_ + beta_ * sp_;
        y_ += alphaFor(cutoff, dt) * (x - y_);
        return y_;
    }

    void reset() { init_ = false; y_ = 0.f; sp_ = 0.f; }

private:
    static float alphaFor(float cutoffHz, float dt) {
        const float tau = 1.f / (2.f * 3.14159265f * cutoffHz);
        return 1.f / (1.f + tau / dt);
    }

    float minCutoff_;
    float beta_;
    float dCutoff_;
    float y_    = 0.f;
    float sp_   = 0.f;   // geglaettete Geschwindigkeit
    bool  init_ = false;
};
