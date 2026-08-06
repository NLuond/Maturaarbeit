#pragma once
#include <stdint.h>
#include <math.h>

// Bremst den Zeiger, waehrend sich der Unterarm um seine eigene Achse dreht.
//
// Warum es das braucht: die abgedrehte Haltung traegt den Rechtsklick, und die
// Ein/Aus-Geste geht durch dieselbe Bewegung. Liefe der Cursor dabei mit, waere
// er nach dem Hindrehen nicht mehr auf dem Ziel - der Rechtsklick waere
// praktisch nicht zielbar.
//
// Warum die Rate aus der Lageschaetzung kommt und nicht aus gy: die
// Unterarmachse faellt nicht exakt mit einer Platinenachse zusammen, das Board
// sitzt am Arm und nicht im Gelenk. Eine Verdrehung leckt deshalb immer auch in
// gx und gz - genau die beiden Achsen, aus denen der Zeiger seine Bewegung
// zieht. Die Ableitung von arm::twistDeg() misst dagegen die Verdrehung selbst,
// unabhaengig von der Einbaulage. Dass es diesen Winkel ueberhaupt gibt, ist
// der Verdienst des Madgwick-Filters.
//
// Der Faktor greift auf die fertigen Pixel und nicht auf die Rate vor dem
// 1-Euro-Filter: so laeuft der Filter waehrend der Drehung weiter mit und
// bleibt eingeschwungen. Andernfalls kaeme nach jeder Drehung eine
// Anfahrverzoegerung obendrauf.
//
// Kein #include "config.h" und kein <Arduino.h>: dieser Header muss sich ohne
// Toolchain uebersetzen lassen (test/test_twist_guard.cpp).
struct TwistGuardTuning {
    float lowDps   = 25.f;    // darunter volle Bewegung
    float highDps  = 70.f;    // darueber gar keine
    float rateTau  = 0.03f;   // Glaettung der abgeleiteten Rate, Sekunden
    float releaseS = 0.20f;   // Zeit fuer die volle Rueckkehr auf 1.0
};

class TwistGuard {
public:
    explicit TwistGuard(const TwistGuardTuning& t = TwistGuardTuning()) : t_(t) {}

    // twistDeg: Verdrehung aus der Lageschaetzung (arm::twistDeg).
    // Rueckgabe: Faktor 0..1 fuer die Cursorbewegung dieses Takts.
    float update(float twistDeg, float dt) {
        if (dt <= 0.f) return gain_;

        if (!init_) {
            // Ohne Vorwert waere die Ableitung im ersten Takt riesig und der
            // Cursor beim Einschalten kurz tot.
            prev_ = twistDeg;
            init_ = true;
            return gain_;
        }

        // wrapDeg, weil der Winkel bei +-180 Grad umschlaegt. Ohne das waere
        // der Umschlag eine scheinbare Rate von zehntausenden Grad pro Sekunde
        // und die Bremse bliebe danach eine halbe Sekunde zu.
        const float d = wrapDeg(twistDeg - prev_);
        prev_ = twistDeg;

        const float a = 1.f - expf(-dt / t_.rateTau);
        rate_ += a * (fabsf(d) / dt - rate_);

        float want = 1.f;
        if      (rate_ >= t_.highDps) want = 0.f;
        else if (rate_ >  t_.lowDps)  want = 1.f - (rate_ - t_.lowDps) / (t_.highDps - t_.lowDps);

        // Sofort zu, langsam wieder auf: am Ende einer Drehung klingt die Rate
        // aus und kreuzt die Schwelle mehrfach. Ohne die begrenzte Rueckkehr
        // zuckte der Cursor dabei wiederholt an.
        if (want < gain_) gain_ = want;
        else {
            gain_ += dt / t_.releaseS;
            if (gain_ > want) gain_ = want;
        }
        if (gain_ < 0.f) gain_ = 0.f;
        if (gain_ > 1.f) gain_ = 1.f;
        return gain_;
    }

    // Nur den Referenzwinkel verwerfen (init_ = false erzwingt eine neue
    // Vorwert-Aufnahme im naechsten update()), rate_ und gain_ bleiben
    // stehen. Der Aufrufer ruft reset() genau dann, wenn die Ein/Aus-Geste
    // feuert - also mitten in der Drehung, die den Schalter ausgeloest hat.
    // rate_ und gain_ sind zu diesem Zeitpunkt eine gueltige, laufende
    // Schaetzung dieser Drehung; sie auf 0/1 zu nullen wuerde die Bremse
    // fuer den naechsten Takt oeffnen, obwohl der Arm noch dreht - und im
    // selben apply() setzt pointer_.reset() den 1-Euro-Filter auf
    // Durchgriff, der Cursor liefe also ungebremst UND ungefiltert.
    void reset() { init_ = false; }

    float gain()    const { return gain_; }
    float rateDps() const { return rate_; }

private:
    static float wrapDeg(float a) {
        while (a >  180.f) a -= 360.f;
        while (a < -180.f) a += 360.f;
        return a;
    }

    TwistGuardTuning t_;
    bool  init_ = false;
    float prev_ = 0.f;
    float rate_ = 0.f;
    float gain_ = 1.f;
};
