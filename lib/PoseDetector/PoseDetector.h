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
//   Scroll - nach aussen gedreht, Neigung wirkt wie ein Joystick.
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
    #if !USE_POSE_MODE
        (void)twistDeg; (void)elevDeg; (void)gyroSum; (void)dt; (void)now_ms;
        rel_ = 0.f; fElev_ = 0.f; level_ = true;
        return pose_ = Pose::Point;
    #else
        // Erst glaetten: beide Winkel schwanken beim normalen Zeigen um mehrere
        // zehn Grad, teils weil das Handgelenk mitdreht, teils weil Madgwick bei
        // schnellen Bewegungen von der Linearbeschleunigung gestoert wird. Ohne
        // Glaettung wuerde die Haltung mitten in der Bewegung umspringen.
        const float a = 1.f - expf(-dt / cfg::MODE_TAU);
        fTwist_ = wrapDeg(fTwist_ + a * wrapDeg(twistDeg - fTwist_));
        fElev_ += a * (elevDeg - fElev_);
        rel_    = wrapDeg(fTwist_ - cfg::TWIST_NEUTRAL_DEG);

        // Waagrecht-Gate mit eigener Hysterese, sonst flattert es genau an der
        // Schwelle - und ein Flattern hier wuerde die ganze Haltung mitreissen.
        const float tilt = fabsf(fElev_);
        if (level_) { if (tilt > cfg::LEVEL_MAX_DEG)                          level_ = false; }
        else        { if (tilt < cfg::LEVEL_MAX_DEG - cfg::LEVEL_HYST_DEG)    level_ = true;  }

        // Waehrend einer heftigen Bewegung bleibt die Haltung stehen. Die
        // Winkel oben laufen weiter mit - nur entschieden wird nichts. Eine
        // gehaltene Haltung ist per Definition nichts, was man mitten im
        // Schwung einnimmt, und das Einschalt-Schuetteln reisst die Erkennung
        // sonst durch Idle bis Scroll.
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

    Pose  pose()        const { return pose_; }
    float relTwistDeg() const { return rel_; }
    float elevDeg()     const { return fElev_; }
    bool  level()       const { return level_; }

private:
    float    fTwist_   = 0.f;
    float    fElev_    = 0.f;
    float    rel_      = 0.f;
    bool     level_    = true;
    Pose     pose_     = Pose::Point;
    Pose     pending_  = Pose::Point;
    uint32_t tPending_ = 0;
    uint32_t tMoving_  = 0;

    // Die Schwellen sind beim Verlassen und beim Wiedereintritt verschieden,
    // sonst flackert die Haltung genau an der Grenze.
    //
    // Betrag statt Vorzeichen: aus der Zeige-Haltung heraus laesst sich der
    // Unterarm rund 90 Grad supinieren, aber nur 10 bis 30 Grad weiter
    // pronieren. SCROLL_ON_DEG ist damit anatomisch nur in einer Richtung
    // erreichbar, und welche das ist, muss der Code nicht wissen. Vorher stand
    // hier ein Vorzeichen SCROLL_DIR, das sich nur durch Ausprobieren am
    // Handgelenk bestimmen liess und bei falscher Einstellung den Scroll-Modus
    // unerreichbar machte, ohne dass man es der Konfiguration ansah.
    Pose classify() const {
        const float tilt = fabsf(rel_);

        switch (pose_) {
            case Pose::Point:
                return (tilt > cfg::POINT_MAX_DEG) ? Pose::Idle : Pose::Point;

            case Pose::Scroll:
                return (tilt < cfg::SCROLL_ON_DEG - cfg::MODE_HYST_DEG)
                       ? Pose::Idle : Pose::Scroll;

            case Pose::Idle:
                if (tilt > cfg::SCROLL_ON_DEG)                         return Pose::Scroll;
                if (tilt < cfg::POINT_MAX_DEG - cfg::MODE_HYST_DEG)    return Pose::Point;
                return Pose::Idle;
        }
        return Pose::Idle;   // nicht erreichbar, beruhigt den Compiler
    }

    static float wrapDeg(float a) {
        while (a >  180.f) a -= 360.f;
        while (a < -180.f) a += 360.f;
        return a;
    }
};
