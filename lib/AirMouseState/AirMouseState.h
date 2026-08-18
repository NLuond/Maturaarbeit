#pragma once
#include <stdint.h>

// Zustandsautomat der Air Mouse. Zwei getrennte Achsen statt eines flachen
// Zustands: Power (Off/On, aus der Drehgeste) und Pose (Point/Idle/Turned, aus
// der Haltung). Die Haltung bestimmt, wohin die Bewegung geht, der Pinch
// klickt - gerader Arm zeigt und klickt links, abgedrehter scrollt und klickt
// rechts.
//
// Der Automat fuehrt selbst nichts aus: jedes Ereignis liefert ein
// Actions-Struct zurueck, das der Aufrufer erledigt. Damit haengt er an keiner
// Hardware und alle Seiteneffekte stehen an genau einer Stelle im Controller.
//
// Uebergangstabelle und die Begruendung, warum es kein Ziehen gibt:
// docs/Programmcode.md, Abschnitt 7.

enum class Power : uint8_t { Off, On };
enum class Pose  : uint8_t { Point, Idle, Turned };

struct Actions {
    bool click        = false;  // links, als ganzer Klick
    bool rightClick   = false;
    bool resetPointer = false;
    bool resetPose    = false;  // Haltungserkennung auf Point zuruecksetzen

    // 0 = still, 1 = Linksklick oder Haltungswechsel, 2 = Rechtsklick. Der
    // Rechtsklick passiert ohne sichtbaren Cursor und muss an der Hand
    // unterscheidbar sein; Ein/Aus hebt sich als langer Puls ab.
    uint8_t hapticPulses = 0;
    bool    hapticLong   = false;
};

class AirMouseState {
public:
    // Die Drehgeste hat vollstaendig durchgelaufen (TwistEvent::Toggle).
    Actions onPower() {
        Actions a;
        a.resetPointer = true;
        a.hapticPulses = 1;
        a.hapticLong   = true;

        if (power_ == Power::Off) {
            power_ = Power::On;
            pose_  = Pose::Point;
            a.resetPose = true;
        } else {
            power_ = Power::Off;
        }
        return a;
    }

    Actions onPose(Pose next) {
        Actions a;
        if (power_ != Power::On || next == pose_) return a;

        if (pose_ == Pose::Point) a.resetPointer = true;   // verlassen
        pose_ = next;

        switch (next) {                                    // betreten
            case Pose::Turned: a.hapticPulses = 1; break;
            case Pose::Point:  a.resetPointer = true; a.hapticPulses = 1; break;
            // Idle bleibt still, sonst brummt es zweimal auf dem Weg vom Zeigen
            // in die abgedrehte Haltung.
            case Pose::Idle:   break;
        }
        return a;
    }

    // armOut deckt das Fenster ab, in dem der Arm koerperlich schon ausgedreht
    // ist, pose_ aber noch Point meldet. Unterdruecken statt umleiten: nichts zu
    // tun ist rueckholbar, ein Klick auf die falsche Taste nicht.
    Actions onPinch(bool armOut) {
        Actions a;
        if (power_ != Power::On) return a;

        switch (pose_) {
            case Pose::Point:
                if (armOut) break;
                a.click        = true;
                a.hapticPulses = 1;
                break;
            case Pose::Turned:
                a.rightClick   = true;
                a.hapticPulses = 2;
                break;
            case Pose::Idle:
                break;
        }
        return a;
    }

    Pose pose() const { return pose_; }

    bool on()       const { return power_ == Power::On; }
    bool pointing() const { return power_ == Power::On && pose_ == Pose::Point; }

    // Abgedreht heisst scrollen - ohne Griff und ohne Haltezeit, deshalb ohne
    // eigenen Zustand.
    bool scrolling() const { return power_ == Power::On && pose_ == Pose::Turned; }

private:
    Power power_ = Power::Off;
    Pose  pose_  = Pose::Point;
};
