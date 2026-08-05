#pragma once
#include <math.h>

// 1-Euro-Filter nach Casiez, Roussel und Vogel (CHI 2012): ein Tiefpass, dessen
// Grenzfrequenz mit der Bewegungsgeschwindigkeit mitwaechst. Steht die Hand
// still, filtert er stark und unterdrueckt das Zittern; bewegt sie sich, oeffnet
// er und erzeugt kaum Verzoegerung. Ein fester Tiefpass muss sich zwischen
// diesen beiden Faellen entscheiden, dieser nicht.
//
// Abweichung vom Original: dort wird die Geschwindigkeit aus dem verrauschten
// Positionssignal geschaetzt. Hier ist sie die Messgroesse selbst - das Gyroskop
// liefert die Drehrate direkt. Die Ableitungsstufe des Originals entfaellt
// deshalb ersatzlos.
//
// Die Parameter kommen wie bei HighPass/LowPass ueber den Konstruktor und nicht
// aus cfg::. So lassen sich fuer die Evaluation zwei verschieden eingestellte
// Filter im selben Programm gegeneinander laufen lassen.
class OneEuroFilter {
public:
    // minCutoff in Hz, beta in Hz pro Einheit von speed.
    OneEuroFilter(float minCutoff, float beta)
        : minCutoff_(minCutoff), beta_(beta) {}

    // speed = Betrag der Bewegung, steuert die Grenzfrequenz.
    float update(float x, float speed, float dt) {
        if (dt <= 0.f) return y_;

        const float cutoff = minCutoff_ + beta_ * fabsf(speed);
        const float tau    = 1.f / (2.f * 3.14159265f * cutoff);
        const float alpha  = 1.f / (1.f + tau / dt);

        if (!init_) { y_ = x; init_ = true; return y_; }

        y_ += alpha * (x - y_);
        return y_;
    }

    void reset() { init_ = false; y_ = 0.f; }

private:
    float minCutoff_;
    float beta_;
    float y_    = 0.f;
    bool  init_ = false;
};
