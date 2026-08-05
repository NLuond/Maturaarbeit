#pragma once
#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "OneEuro.h"
#include "ArmOrientation.h"

// Einstellwerte der Cursor-Kennlinie. Die Vorgaben kommen aus cfg::, lassen
// sich aber pro Instanz ueberschreiben - damit laufen fuer die Evaluation zwei
// Kennlinien im selben Programm, statt fuer jede Variante neu zu flashen:
//
//   PointerTuning t;  t.accelK = 0.f;   // Vergleich ohne Beschleunigung
//   OrientationPointer alternativ(t);
struct PointerTuning {
    float sensX         = cfg::SENS_X;
    float sensY         = cfg::SENS_Y;
    float accelK        = cfg::ACCEL_K;
    float accelMax      = cfg::ACCEL_MAX;
    float deadzone      = cfg::DEADZONE;
    float euroMinCutoff = cfg::EURO_MIN_CUTOFF;
    float euroBeta      = cfg::EURO_BETA;
    float smoothTau     = cfg::SMOOTH_TAU;
    float elevLimit     = cfg::ELEV_LIMIT;
    float elevFade      = cfg::ELEV_FADE;
};

class OrientationPointer {
public:
    explicit OrientationPointer(const PointerTuning& t = PointerTuning())
        : t_(t),
          euroX_(t.euroMinCutoff, t.euroBeta),
          euroY_(t.euroMinCutoff, t.euroBeta) {}

    // twistDeg ist die geglaettete Verdrehung gegenueber der Zeige-Haltung,
    // elevDeg die Neigung des Unterarms aus der Waagerechten (beide aus
    // arm::), nicht irgendein Roll oder Pitch der Platine.
    void update(float gX, float gZ, float twistDeg, float elevDeg, float dt,
                float& outX, float& outY) {
    #if USE_ROLL_COMP
        // Bezogen auf den Raum: eine waagerechte Handbewegung bleibt waagerecht,
        // auch wenn die Hand dabei verdreht gehalten wird.
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
        // Geschwindigkeit. Getrennte Werte wuerden die Achsen verschieden stark
        // verzoegern und schraege Striche verbiegen.
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

        // Diagnose: rateX/rateY nach Deadzone und Filter. Gegen die rohen
        // Kanaele gx/gz gehalten zeigt sich, wie viel Bewegung der 1-Euro-Filter
        // im Einschwingen wegnimmt - der Verdaechtige bei zu traegem Cursor.
        dbgRateX_ = rateX;  dbgRateY_ = rateY;
        dbgAccel_ = accel;

    #if USE_ONE_EURO
        outX = stepX;
        outY = stepY;
    #else
        // Vergleichspfad: eine Zeitkonstante fuer alle Geschwindigkeiten, also
        // immer derselbe Kompromiss aus Zittern und Verzoegerung.
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

    float rateX() const { return dbgRateX_; }
    float rateY() const { return dbgRateY_; }
    float accel() const { return dbgAccel_; }

private:
    PointerTuning t_;
    OneEuroFilter euroX_, euroY_;
    float smX_ = 0.f, smY_ = 0.f;
    float dbgRateX_ = 0.f, dbgRateY_ = 0.f, dbgAccel_ = 1.f;

    // Weich statt hart: unterhalb der Schwelle null, darueber wird die Schwelle
    // abgezogen, damit die Bewegung stetig bei null beginnt. Die Schwelle faengt
    // nur noch den Rest-Nullpunktfehler ab, den die Bias-Korrektur uebriglaesst.
    float deadzone(float v) const {
        if (v >  t_.deadzone) return v - t_.deadzone;
        if (v < -t_.deadzone) return v + t_.deadzone;
        return 0.f;
    }
};
