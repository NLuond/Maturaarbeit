#pragma once
#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "AirMouseState.h"

// Bildet Verdrehung und Armneigung auf eine Haltung ab. Liefert nur das
// Ergebnis - was daraus folgt, entscheidet der Zustandsautomat.
//
//   Point  - Hand gerade gehalten, Cursor folgt der Bewegung.
//   Idle   - abgedreht oder Arm nicht waagrecht. Nichts passiert.
//   Turned - nach aussen gedreht, Neigung wirkt wie ein Joystick.
//
// Beide Winkel haben einen festen Bezug, keinen aus der laufenden Sitzung:
//
//   twist  gegen cfg::TWIST_NEUTRAL_DEG. Das Board sitzt immer gleich am Arm,
//          der Nullpunkt ist damit eine Eigenschaft der Bauform.
//   elev   gegen die Waagerechte. "Waagrecht" ist eine Aussage ueber den Raum,
//          nicht ueber die Einschalthaltung - relativ gemessen koennte man die
//          Bedingung durch Einschalten mit haengendem Arm aushebeln.
class PoseDetector {
public:
    // Setzt beim Einschalten nur die Haltung zurueck - der Nullpunkt der
    // Verdrehung ist fest (cfg::TWIST_NEUTRAL_DEG) und wird hier bewusst nicht
    // angefasst. Frueher wurde er hier auf die aktuelle Lage kalibriert; das
    // geschieht unmittelbar nach dem Schuetteln, wenn die Lageschaetzung von
    // der Schuettelbewegung am staerksten gestoert ist, und ergab bei jedem
    // Einschalten einen anderen Bezugspunkt. Ein Winkel, der ueber Minuten
    // gilt, darf nicht aus dem unruhigsten Moment stammen.
    void reset() {
        pose_    = Pose::Point;
        pending_ = Pose::Point;
    }

    Pose update(float twistDeg, float elevDeg, float gyroSum, float dt, uint32_t now_ms) {
        // Erst glaetten: beide Winkel schwanken beim normalen Zeigen um mehrere
        // zehn Grad, teils weil das Handgelenk mitdreht, teils weil Madgwick bei
        // schnellen Bewegungen von der Linearbeschleunigung gestoert wird. Ohne
        // Glaettung wuerde die Haltung mitten in der Bewegung umspringen.
        //
        // Das laeuft unabhaengig von USE_POSE_MODE: TwistToggle liest
        // relTwistDeg() direkt, ohne ueber die Klassifikation unten zu gehen.
        // Wuerden die Winkel hier auf null gehalten, saehe die Ein/Aus-Geste die
        // Drehung nie - "Haltungserkennung aus" darf nur heissen, dass die
        // KLASSIFIKATION dauerhaft Point liefert, nicht dass die Winkelplumbing
        // stillsteht.
        const float a = 1.f - expf(-dt / cfg::MODE_TAU);
        fTwist_ = wrapDeg(fTwist_ + a * wrapDeg(twistDeg - fTwist_));
        fElev_ += a * (elevDeg - fElev_);
        rel_    = wrapDeg(fTwist_ - cfg::TWIST_NEUTRAL_DEG);

        // Zweite, langsamere Glaettung fuer die Roll-Kompensation. Sie laeuft
        // auch waehrend der Bewegungssperre weiter, wie die uebrigen Winkel.
        const float aSlow = 1.f - expf(-dt / cfg::ROLLCOMP_TAU);
        fTwistSlow_ = wrapDeg(fTwistSlow_ + aSlow * wrapDeg(twistDeg - fTwistSlow_));
        relSlow_    = wrapDeg(fTwistSlow_ - cfg::TWIST_NEUTRAL_DEG);

        // Waagrecht-Gate mit eigener Hysterese, sonst flattert es genau an der
        // Schwelle - und ein Flattern hier wuerde die ganze Haltung mitreissen.
        // Auch dieses Gate bleibt in Betrieb: TwistToggle bekommt level() als
        // zweite Bedingung, unabhaengig von USE_POSE_MODE.
        const float tilt = fabsf(fElev_);
        if (level_) { if (tilt > cfg::LEVEL_MAX_DEG)                          level_ = false; }
        else        { if (tilt < cfg::LEVEL_MAX_DEG - cfg::LEVEL_HYST_DEG)    level_ = true;  }

    #if !USE_POSE_MODE
        (void)gyroSum; (void)now_ms;
        return pose_ = Pose::Point;
    #else
        // Waehrend einer heftigen Bewegung bleibt die Haltung stehen. Die
        // Winkel oben laufen weiter mit - nur entschieden wird nichts. Eine
        // gehaltene Haltung ist per Definition nichts, was man mitten im
        // Schwung einnimmt, und die zuegige Ein/Aus-Drehung (TwistToggle)
        // reisst die Erkennung sonst durch Idle bis Turned.
        if (gyroSum > cfg::POSE_STILL_DPS) tMoving_ = now_ms;
        if (now_ms - tMoving_ < cfg::POSE_CALM_MS) {
            // Haltezeit neu anlaufen lassen, sonst waere sie in dem Moment,
            // in dem die Bewegung aufhoert, schon halb abgelaufen.
            pending_  = pose_;
            tPending_ = now_ms;
            return pose_;
        }

        // Dann halten: ein Wechsel zaehlt erst, wenn er kurz stabil anliegt.
        // Das gilt auch fuer das Gate - ein kurzes Durchschwingen des Arms beim
        // Zeigen darf die Maus nicht abschalten.
        const Pose want = level_ ? classify() : Pose::Idle;
        if (want == pose_) {
            pending_ = pose_;
        } else if (want != pending_) {
            pending_  = want;
            tPending_ = now_ms;
        } else if (now_ms - tPending_ >= cfg::MODE_DWELL_MS) {
            pose_ = want;
        }
        return pose_;
    #endif
    }

    Pose  pose()         const { return pose_; }
    float relTwistDeg()  const { return rel_; }
    float relTwistSlow() const { return relSlow_; }
    float elevDeg()      const { return fElev_; }
    bool  level()        const { return level_; }

private:
    float    fTwist_     = 0.f;
    float    fElev_      = 0.f;
    float    rel_        = 0.f;
    float    fTwistSlow_ = 0.f;
    float    relSlow_    = 0.f;
    bool     level_    = true;
    Pose     pose_     = Pose::Point;
    Pose     pending_  = Pose::Point;
    uint32_t tPending_ = 0;
    uint32_t tMoving_  = 0;

    // Nur noch zwei Zonen auf der Verdrehachse, mit Hysterese. Idle taucht
    // hier nicht auf: es ist keine Zone der Verdrehung mehr, sondern das
    // Ergebnis des Waagrecht-Gates in update().
    Pose classify() const {
        const float tilt = fabsf(rel_);
        if (pose_ == Pose::Turned) {
            return (tilt < cfg::TURN_OFF_DEG) ? Pose::Point : Pose::Turned;
        }
        // Deckt Point und Idle ab: aus beiden fuehrt derselbe Eintrittspunkt
        // nach Turned.
        return (tilt > cfg::TURN_ON_DEG) ? Pose::Turned : Pose::Point;
    }

    static float wrapDeg(float a) {
        while (a >  180.f) a -= 360.f;
        while (a < -180.f) a += 360.f;
        return a;
    }
};
