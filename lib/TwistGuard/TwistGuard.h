#pragma once
#include <stdint.h>
#include <math.h>

// Bremst den Zeiger, waehrend sich der Unterarm um seine eigene Achse dreht -
// sonst waere der Cursor nach dem Hindrehen nicht mehr auf dem Ziel und der
// Rechtsklick praktisch nicht zielbar.
//
// Die Rate kommt aus der Lageschaetzung und nicht aus gy: die Unterarmachse
// faellt nicht exakt mit einer Platinenachse zusammen, eine Verdrehung leckt
// deshalb auch in gx und gz - genau die beiden Achsen, aus denen der Zeiger
// seine Bewegung zieht.
//
// Ohne config.h und ohne Arduino.h, damit der PC-Test laeuft.
struct TwistGuardTuning {
    float lowDps   = 25.f;    // darunter volle Bewegung
    float highDps  = 70.f;    // darueber gar keine
    float rateTau  = 0.03f;   // Glaettung der abgeleiteten Rate, s
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
            prev_ = twistDeg;
            init_ = true;
            return gain_;
        }

        // wrapDeg, weil der Winkel bei +-180 Grad umschlaegt - ohne das waere
        // der Umschlag eine scheinbare Rate von zehntausenden Grad pro Sekunde.
        const float d = wrapDeg(twistDeg - prev_);
        prev_ = twistDeg;

        const float a = 1.f - expf(-dt / t_.rateTau);
        rate_ += a * (fabsf(d) / dt - rate_);

        float want = 1.f;
        if      (rate_ >= t_.highDps) want = 0.f;
        else if (rate_ >  t_.lowDps)  want = 1.f - (rate_ - t_.lowDps) / (t_.highDps - t_.lowDps);

        // Sofort zu, langsam wieder auf: am Ende einer Drehung klingt die Rate
        // aus und kreuzt die Schwelle mehrfach, der Cursor zuckte sonst
        // wiederholt an.
        if (want < gain_) gain_ = want;
        else {
            gain_ += dt / t_.releaseS;
            if (gain_ > want) gain_ = want;
        }
        if (gain_ < 0.f) gain_ = 0.f;
        if (gain_ > 1.f) gain_ = 1.f;
        return gain_;
    }

    // Verwirft nur den Referenzwinkel. rate_ und gain_ bleiben stehen: reset()
    // faellt mitten in die Drehung, die gerade geschaltet hat, und dort sind
    // beide eine gueltige laufende Schaetzung.
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
