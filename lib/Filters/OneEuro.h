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
// Was NICHT entfallen darf, ist der Tiefpass auf der Geschwindigkeit (dcutoff,
// im Original 1 Hz). Ohne ihn geht die rohe Rate in die Grenzfrequenz ein, und
// die folgt dann dem Betrag des Signals: bei physiologischem Tremor - 8 bis
// 12 Hz, rund 12 Grad/s - steht sie genau auf den Spitzen am weitesten offen,
// die Spitzen kommen also besser durch als der Mittelwert vermuten laesst.
//
// Der wirksame Hebel gegen das Wackeln ist trotzdem beta und nicht dcutoff:
// der Mittelwert von |12*sin(2*pi*10t)| ist 7.64 Grad/s, ob geglaettet oder
// nicht. Mit beta = 0.55 ergibt das eine mittlere Grenzfrequenz von 5.1 Hz und
// damit kaum Daempfung bei 10 Hz; mit beta = 0.2 sind es 2.5 Hz. dcutoff ist
// eine Korrektheitsreparatur, beta die Einstellung.
//
// Die Parameter kommen wie bei HighPass/LowPass ueber den Konstruktor und nicht
// aus cfg::. So lassen sich fuer die Evaluation zwei verschieden eingestellte
// Filter im selben Programm gegeneinander laufen lassen.
class OneEuroFilter {
public:
    // minCutoff in Hz, beta in Hz pro Einheit von speed, dCutoff in Hz.
    // dCutoff hat bewusst keinen Vorgabewert: er ist der wirksame Hebel gegen
    // das Zittern und soll an jeder Aufrufstelle sichtbar sein.
    OneEuroFilter(float minCutoff, float beta, float dCutoff)
        : minCutoff_(minCutoff), beta_(beta), dCutoff_(dCutoff) {}

    // speed = Betrag der Bewegung, steuert die Grenzfrequenz.
    float update(float x, float speed, float dt) {
        if (dt <= 0.f) return y_;

        const float s = fabsf(speed);

        if (!init_) { y_ = x; sp_ = s; init_ = true; return y_; }

        // Erst die Geschwindigkeit glaetten, dann daraus die Grenzfrequenz.
        sp_ += alphaFor(dCutoff_, dt) * (s - sp_);

        const float cutoff = minCutoff_ + beta_ * sp_;
        y_ += alphaFor(cutoff, dt) * (x - y_);
        return y_;
    }

    void reset() { init_ = false; y_ = 0.f; sp_ = 0.f; }

private:
    // Zeitkonstante eines Tiefpasses erster Ordnung, in den Schrittfaktor
    // umgerechnet. Aus dem Original uebernommen.
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
