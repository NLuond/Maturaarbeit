#pragma once
#include <stdint.h>

// Zustandsautomat der Air Mouse. Zwei getrennte Achsen statt eines flachen
// Zustands:
//
//   Power - Off / On, umgeschaltet durch die Drehgeste (TwistToggle).
//   Pose  - Point / Idle / Turned, aus der Haltung (PoseDetector).
//
//   Ereignis    | Off    | On/Point       | On/Idle        | On/Turned
//   ------------|--------|----------------|----------------|----------------
//   onPower     | -> On  | -> Off         | -> Off         | -> Off
//   onPose      | ignor. | Zeiger zurueck | ggf. Wechsel   | Joystick aus
//   onTwistHeld | ignor. | ignor.         | ignor.         | Scroll-Start
//   onPinch     | ignor. | Linksklick**   | ignoriert      | Rechtsklick*
//
//   *  ausser der Joystick laeuft UND die Neigung steht ausserhalb der Totzone
//   ** ausser armOut: der Arm ist koerperlich schon draussen, siehe onPinch()
//
// Der Automat fuehrt selbst nichts aus: jedes Ereignis liefert ein Actions-
// Struct zurueck, das der Aufrufer erledigt. Damit haengt er an keiner Hardware
// und alle Seiteneffekte stehen an genau einer Stelle im Controller.
//
// Es gibt bewusst keine Grab-Achse und kein Ziehen - solange nichts eine Taste
// gedrueckt haelt, waere ein Zustand dafuer nur Ballast.

enum class Power : uint8_t { Off, On };
enum class Pose  : uint8_t { Point, Idle, Turned };

struct Actions {
    bool click        = false;  // links
    bool rightClick   = false;
    bool resetPointer = false;
    bool enterScroll  = false;  // Scroll-Joystick auf aktuelle Neigung nullen
    bool resetPose    = false;  // Haltungserkennung auf Point zuruecksetzen

    // 0 = still, 1 = Linksklick / Haltungswechsel / Ein-Aus, 2 = Rechtsklick.
    // Der Rechtsklick passiert in einer Haltung, in der man den Cursor nicht
    // sieht, und muss an der Hand unterscheidbar sein.
    uint8_t hapticPulses = 0;
};

class AirMouseState {
public:
    // Die Drehgeste hat vollstaendig durchgelaufen (TwistEvent::Toggle).
    Actions onPower() {
        Actions a;
        if (power_ == Power::Off) {
            power_ = Power::On;
            pose_  = Pose::Point;
            scrollOn_      = false;
            a.resetPose    = true;
            a.resetPointer = true;
            a.hapticPulses = 1;
            return a;
        }
        power_    = Power::Off;
        scrollOn_ = false;
        a.resetPointer = true;
        a.hapticPulses = 1;
        return a;
    }

    Actions onPose(Pose next) {
        Actions a;
        if (power_ != Power::On || next == pose_) return a;

        if (pose_ == Pose::Point)  a.resetPointer = true;   // verlassen
        if (pose_ == Pose::Turned) scrollOn_ = false;       // Joystick aus

        pose_ = next;

        switch (next) {                                     // betreten
            // Turned schaltet den Joystick NICHT zu - das macht erst
            // onTwistHeld(). Sonst wuerde jede Ein/Aus-Geste nebenbei scrollen.
            case Pose::Turned: a.hapticPulses = 1; break;
            case Pose::Point:  a.resetPointer = true; a.hapticPulses = 1; break;
            // Idle bleibt still, sonst brummt es zweimal auf dem Weg vom Zeigen
            // in die abgedrehte Haltung.
            case Pose::Idle:   break;
        }
        return a;
    }

    // Die Ausdrehung wurde laenger als das Schaltfenster gehalten.
    Actions onTwistHeld() {
        Actions a;
        if (power_ != Power::On || pose_ != Pose::Turned || scrollOn_) return a;
        scrollOn_      = true;
        a.enterScroll  = true;
        a.hapticPulses = 1;
        return a;
    }

    // scrollIdle: die Neigung steht in der Totzone des Joysticks. Zaehlt nur,
    // wenn der Joystick laeuft - sein dead_ friert sonst auf dem letzten Wert
    // ein und wuerde den schnellen Rechtsklick nach jeder Scroll-Sitzung toeten.
    //
    // armOut: TwistToggle sieht den Arm koerperlich als ausgedreht, waehrend
    // pose_ hier noch Point meldet (die FSM-Haltung kommt 400 bis 700 ms
    // spaeter an). Ein Pinch in diesem Fenster wird unterdrueckt statt auf
    // Rechtsklick umgeleitet: nichts zu tun ist rueckholbar, ein Klick auf die
    // falsche Taste am unsichtbaren Cursor nicht.
    Actions onPinch(bool scrollIdle, bool armOut) {
        Actions a;
        if (power_ != Power::On) return a;

        switch (pose_) {
            case Pose::Point:
                if (armOut) break;
                a.click = true; a.hapticPulses = 1;
                break;
            case Pose::Turned:
                if (!scrollOn_ || scrollIdle) { a.rightClick = true; a.hapticPulses = 2; }
                break;
            case Pose::Idle:
                break;
        }
        return a;
    }

    Pose pose() const { return pose_; }

    bool on()        const { return power_ == Power::On; }
    bool pointing()  const { return power_ == Power::On && pose_ == Pose::Point; }
    bool scrolling() const { return power_ == Power::On && pose_ == Pose::Turned && scrollOn_; }

private:
    Power power_    = Power::Off;
    Pose  pose_     = Pose::Point;
    bool  scrollOn_ = false;
};
