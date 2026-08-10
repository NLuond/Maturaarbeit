#pragma once
#include <stdint.h>
#include <math.h>
#include "AirMouseState.h"

// Bildet Verdrehung und Armneigung auf eine Haltung ab:
//
//   Point  - Hand gerade gehalten.
//   Idle   - Arm zu steil. Ergebnis allein des Neigungs-Gates, keine Zone der
//            Verdrehung.
//   Turned - Hand abgedreht. Der Scroll-Joystick kommt nicht mit dieser
//            Haltung, sondern erst ueber AirMouseState::onTwistHeld().
//
// Zwei Gates: das enge, symmetrische level() ist Voraussetzung der
// Ein/Aus-Drehgeste (TwistToggle liest es), das weite und asymmetrische
// poseUp/poseDown entscheidet ueber Idle. Laeuft der Scroll-Modus (holdTurned),
// gilt letzteres gar nicht - Scrollen heisst den Arm neigen.
//
// Ohne config.h und ohne Arduino.h, damit der PC-Test laeuft; die Werte stehen
// in PoseTuning und sind per static_assert an cfg:: gebunden.
struct PoseTuning {
    float    twistNeutralDeg =  0.f;   // Bezugspunkt der Verdrehung, Grad
    float    turnOnDeg       = 70.f;   // ab hier gilt die Hand als abgedreht
    float    turnOffDeg      = 55.f;   // erst hier wieder als gerade

    float    levelMaxDeg     = 50.f;   // Waagrecht-Gate der Geste, Grad
    float    levelHystDeg    =  8.f;   // Hysterese beider Gates

    // Neigungs-Gate der Haltung. Weiter als levelMaxDeg, weil die Verdrehung
    // dort noch gut beobachtbar ist (cos 65 Grad = 0.42 der Schwerkraft quer
    // zur Armachse); asymmetrisch, weil der haengende Arm Ruhezustand ist.
    float    poseUpMaxDeg    = 65.f;   // Grad, Hand oben
    float    poseDownMaxDeg  = 35.f;   // Grad, Hand unten
    float    poseHoldMaxDeg  = 80.f;   // auch im Scroll-Modus nicht mehr entscheiden

    float    modeTau         = 0.10f;  // Glaettung fuer Haltung und Geste, s
    float    rollCompTau     = 0.25f;  // langsamere Fassung fuer den Zeiger, s

    uint32_t dwellMs         = 150;    // Haltezeit vor einem Wechsel

    float    stillDps        = 300.f;  // darueber wird nicht entschieden
    uint32_t calmMs          = 250;    // Ruhezeit danach

    // Aus: dauerhaft Point. Die Winkel laufen weiter mit - die Ein/Aus-Geste
    // liest relTwistDeg() und level() direkt und muss auch dann funktionieren.
    bool     classify        = true;
};

class PoseDetector {
public:
    explicit PoseDetector(const PoseTuning& t = PoseTuning()) : t_(t) {}

    // Setzt nur die Haltung zurueck. Der Nullpunkt der Verdrehung ist fest.
    void reset() {
        pose_    = Pose::Point;
        pending_ = Pose::Point;
    }

    // holdTurned: der Scroll-Modus laeuft. Dann entscheidet die Verdrehung
    // allein, das Neigungs-Gate gilt nicht.
    Pose update(float twistDeg, float elevDeg, float gyroSum, float dt, uint32_t now_ms,
                bool holdTurned = false) {
        smoothAngles(twistDeg, elevDeg, dt);
        updateLevelGate();
        updatePoseGate();

        if (!t_.classify) return pose_ = Pose::Point;

        // Waehrend einer heftigen Bewegung bleibt die Haltung stehen; die
        // Winkel oben laufen weiter.
        if (gyroSum > t_.stillDps) tMoving_ = now_ms;
        if (now_ms - tMoving_ < t_.calmMs) return freeze(now_ms);

        if (holdTurned) {
            // So steil steht der Unterarm fast senkrecht und die Verdrehung ist
            // nicht mehr beobachtbar - dort lieber stehen bleiben als auf einem
            // Rauschwert umspringen.
            if (fabsf(fElev_) > t_.poseHoldMaxDeg) return freeze(now_ms);
            return settle(classify(), now_ms);
        }

        return settle(poseGate_ ? classify() : Pose::Idle, now_ms);
    }

    Pose  pose()            const { return pose_; }
    float relTwistDeg()     const { return rel_; }
    float relTwistSlowDeg() const { return relSlow_; }
    bool  level()           const { return level_; }
    bool  poseGate()        const { return poseGate_; }

private:
    PoseTuning t_;
    float    fTwist_     = 0.f;
    float    fElev_      = 0.f;
    float    rel_        = 0.f;
    float    fTwistSlow_ = 0.f;
    float    relSlow_    = 0.f;
    bool     level_      = true;
    bool     poseGate_   = true;
    Pose     pose_       = Pose::Point;
    Pose     pending_    = Pose::Point;
    uint32_t tPending_   = 0;
    uint32_t tMoving_    = 0;

    // Beide Winkel schwanken beim normalen Zeigen um mehrere zehn Grad. Die
    // zweite, langsamere Verdrehung geht in die Drehmatrix des Zeigers, wo
    // Rauschen unmittelbar als Zittern im Cursor landet.
    void smoothAngles(float twistDeg, float elevDeg, float dt) {
        const float a = 1.f - expf(-dt / t_.modeTau);
        fTwist_ = wrapDeg(fTwist_ + a * wrapDeg(twistDeg - fTwist_));
        fElev_ += a * (elevDeg - fElev_);
        rel_    = wrapDeg(fTwist_ - t_.twistNeutralDeg);

        const float aSlow = 1.f - expf(-dt / t_.rollCompTau);
        fTwistSlow_ = wrapDeg(fTwistSlow_ + aSlow * wrapDeg(twistDeg - fTwistSlow_));
        relSlow_    = wrapDeg(fTwistSlow_ - t_.twistNeutralDeg);
    }

    void updateLevelGate() {
        const float tilt = fabsf(fElev_);
        if (level_) { if (tilt > t_.levelMaxDeg)                    level_ = false; }
        else        { if (tilt < t_.levelMaxDeg - t_.levelHystDeg)  level_ = true;  }
    }

    // Vorzeichenbehaftet, im Gegensatz zum Gate der Geste: oben und unten haben
    // verschiedene Schranken.
    void updatePoseGate() {
        if (poseGate_) {
            if (fElev_ > t_.poseUpMaxDeg || fElev_ < -t_.poseDownMaxDeg) poseGate_ = false;
        } else if (fElev_ <  t_.poseUpMaxDeg   - t_.levelHystDeg &&
                   fElev_ > -t_.poseDownMaxDeg + t_.levelHystDeg) {
            poseGate_ = true;
        }
    }

    // Die Entscheidung ruht. Die Haltezeit laeuft dabei neu an, sonst waere sie
    // beim Ende der Sperre schon halb abgelaufen.
    Pose freeze(uint32_t now_ms) {
        pending_  = pose_;
        tPending_ = now_ms;
        return pose_;
    }

    // Ein Wechsel zaehlt erst, wenn er dwellMs stabil anliegt.
    Pose settle(Pose want, uint32_t now_ms) {
        if (want == pose_) {
            pending_ = pose_;
        } else if (want != pending_) {
            pending_  = want;
            tPending_ = now_ms;
        } else if (now_ms - tPending_ >= t_.dwellMs) {
            pose_ = want;
        }
        return pose_;
    }

    // Nur zwei Zonen, mit Hysterese, verglichen wird der Betrag: turnOnDeg ist
    // anatomisch ohnehin nur in einer Drehrichtung erreichbar.
    Pose classify() const {
        const float tilt = fabsf(rel_);
        if (pose_ == Pose::Turned) {
            return (tilt < t_.turnOffDeg) ? Pose::Point : Pose::Turned;
        }
        return (tilt > t_.turnOnDeg) ? Pose::Turned : Pose::Point;
    }

    static float wrapDeg(float a) {
        while (a >  180.f) a -= 360.f;
        while (a < -180.f) a += 360.f;
        return a;
    }
};
