#pragma once
#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "OneEuro.h"
#include "ArmOrientation.h"

// Rechnet Drehraten in Pixel um:
//   gx, gz -> arm::rates() -> Deadzone -> 1-Euro-Filter -> Beschleunigung
//          -> Ausblendung nach oben -> * SENS * dt
//
// Die Vorgaben kommen aus cfg::, lassen sich aber pro Instanz ueberschreiben -
// damit laufen fuer die Evaluation zwei Kennlinien im selben Programm
// (PointerTuning t; t.accelK = 0.f;).
struct PointerTuning {
    float sensX         = cfg::SENS_X;
    float sensY         = cfg::SENS_Y;
    float accelK        = cfg::ACCEL_K;
    float accelMax      = cfg::ACCEL_MAX;
    float deadzone      = cfg::DEADZONE;
    float euroMinCutoff = cfg::EURO_MIN_CUTOFF;
    float euroBeta      = cfg::EURO_BETA;
    float euroDCutoff   = cfg::EURO_DCUTOFF;
    float smoothTau     = cfg::SMOOTH_TAU;
    float elevLimit     = cfg::ELEV_LIMIT;
    float elevFade      = cfg::ELEV_FADE;
};

class OrientationPointer {
public:
    explicit OrientationPointer(const PointerTuning& t = PointerTuning())
        : t_(t),
          euroX_(t.euroMinCutoff, t.euroBeta, t.euroDCutoff),
          euroY_(t.euroMinCutoff, t.euroBeta, t.euroDCutoff) {}

    // twistDeg: geglaettete Verdrehung gegenueber der Zeige-Haltung,
    // elevDeg: Armneigung aus der Waagerechten - beide aus arm::.
    void update(float gX, float gZ, float twistDeg, float elevDeg, float dt,
                float& outX, float& outY) {
    #if USE_ROLL_COMP
        float yaw, nick;
        arm::rates(gX, gZ, twistDeg, yaw, nick);
    #else
        // Vergleichspfad: Achsen der Platine, ungedreht.
        (void)twistDeg;
        const float yaw = -gZ, nick = gX;
    #endif

        float rateX = deadzone(yaw);    // Gier  -> waagerecht
        float rateY = deadzone(nick);   // Nick  -> senkrecht
        float speed = sqrtf(rateX*rateX + rateY*rateY);

    #if USE_ONE_EURO
        // Beide Achsen bekommen dieselbe Grenzfrequenz aus der gemeinsamen
        // Geschwindigkeit; getrennte Werte wuerden schraege Striche verbiegen.
        rateX = euroX_.update(rateX, speed, dt);
        rateY = euroY_.update(rateY, speed, dt);
        speed = sqrtf(rateX*rateX + rateY*rateY);
    #endif

        const float raw   = 1.f + t_.accelK * speed / 200.f;
        const float accel = constrain(raw, 1.f, t_.accelMax);

        // Nach oben ausblenden, bevor der Arm an seine Reichweite laeuft.
        const float over = elevDeg - (t_.elevLimit - t_.elevFade);
        if (over > 0.f && rateY > 0.f) {
            const float fade = 1.f - over / t_.elevFade;
            rateY *= constrain(fade, 0.f, 1.f);
        }

        const float stepX = rateX * t_.sensX * accel * dt;
        const float stepY = rateY * t_.sensY * accel * dt;

        dbgRateX_ = rateX;  dbgRateY_ = rateY;
        dbgAccel_ = accel;

    #if USE_ONE_EURO
        outX = stepX;
        outY = stepY;
    #else
        // Vergleichspfad: eine Zeitkonstante fuer alle Geschwindigkeiten.
        const float alpha = 1.f - expf(-dt / t_.smoothTau);
        smX_ += alpha * (stepX - smX_);
        smY_ += alpha * (stepY - smY_);
        outX = smX_;
        outY = smY_;
    #endif
    }

    // Beim Moduswechsel aufrufen, damit kein Restwert als Sprung in die
    // Zeige-Haltung hineinlaeuft.
    void reset() {
        smX_ = smY_ = 0.f;
        euroX_.reset();
        euroY_.reset();
    }

    // Nur fuer die Teleplot-Kanaele rx/ry/pacc.
    float rateX() const { return dbgRateX_; }
    float rateY() const { return dbgRateY_; }
    float accel() const { return dbgAccel_; }

private:
    PointerTuning t_;
    OneEuroFilter euroX_, euroY_;
    float smX_ = 0.f, smY_ = 0.f;
    float dbgRateX_ = 0.f, dbgRateY_ = 0.f, dbgAccel_ = 1.f;

    // Weich statt hart: unterhalb der Schwelle null, darueber wird die Schwelle
    // abgezogen, damit die Bewegung stetig bei null beginnt.
    float deadzone(float v) const {
        if (v >  t_.deadzone) return v - t_.deadzone;
        if (v < -t_.deadzone) return v + t_.deadzone;
        return 0.f;
    }
};
