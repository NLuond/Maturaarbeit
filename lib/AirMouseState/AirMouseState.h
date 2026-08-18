#pragma once
#include <stdint.h>

// Zustandsautomat der Air Mouse. Zwei getrennte Achsen statt eines flachen
// Zustands:
//
//   Power - Off / On, umgeschaltet durch die Drehgeste (TwistToggle).
//   Pose  - Point / Idle / Turned, aus der Haltung (PoseDetector).
//
// DIE HALTUNG BESTIMMT, WOHIN DIE BEWEGUNG GEHT; DER PINCH KLICKT.
//
//                | Bewegung   | Pinch
//   -------------|------------|--------------
//   Arm gerade   | Cursor     | Linksklick
//   Arm gedreht  | Scrollen   | Rechtsklick
//
//   Ereignis | Off    | On/Point       | On/Idle      | On/Turned
//   ---------|--------|----------------|--------------|---------------
//   onPower  | -> On  | -> Off         | -> Off       | -> Off
//   onPose   | ignor. | Zeiger zurueck | ggf. Wechsel | ggf. Wechsel
//   onPinch  | ignor. | Linksklick*    | ignoriert    | Rechtsklick
//
//   * ausser armOut: der Arm ist koerperlich schon draussen, siehe onPinch()
//
// Der Automat fuehrt selbst nichts aus: jedes Ereignis liefert ein
// Actions-Struct zurueck, das der Aufrufer erledigt. Damit haengt er an keiner
// Hardware und alle Seiteneffekte stehen an genau einer Stelle im Controller.
//
// Es gibt bewusst kein Ziehen und keine gehaltene Taste: der Klick ist
// unteilbar und loest sofort aus. Jede Geste, die auf ein zweites Ereignis
// wartet, legt ihre Fensterlaenge als Verzoegerung auf JEDEN gewoehnlichen
// Klick - ueber BLE deutlich spuerbar.

enum class Power : uint8_t { Off, On };
enum class Pose  : uint8_t { Point, Idle, Turned };

struct Actions {
    bool click        = false;  // links, als ganzer Klick
    bool rightClick   = false;
    bool resetPointer = false;
    bool resetPose    = false;  // Haltungserkennung auf Point zuruecksetzen

    // 0 = still, 1 = Linksklick / Haltungswechsel, 2 = Rechtsklick. Der
    // Rechtsklick passiert in einer Haltung ohne sichtbaren Cursor und muss an
    // der Hand unterscheidbar sein.
    uint8_t hapticPulses = 0;

    // Ein/Aus bekommt einen langen Puls: es ist das einzige Ereignis, nach dem
    // gar nichts mehr geht.
    bool hapticLong = false;
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

    // armOut: TwistToggle sieht den Arm koerperlich als ausgedreht, waehrend
    // pose_ noch Point meldet (die FSM-Haltung kommt 400 bis 700 ms spaeter an).
    // Ein Pinch in diesem Fenster wird unterdrueckt statt umgeleitet: nichts zu
    // tun ist rueckholbar, ein Klick auf die falsche Taste am unsichtbaren
    // Cursor nicht.
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

    // Gescrollt wird, sobald der Arm abgedreht ist - ohne Griff, ohne Haltezeit.
    // Deshalb braucht es dafuer keinen eigenen Zustand.
    bool scrolling() const { return power_ == Power::On && pose_ == Pose::Turned; }

private:
    Power power_ = Power::Off;
    Pose  pose_  = Pose::Point;
};
