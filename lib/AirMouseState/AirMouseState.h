#pragma once
#include <stdint.h>

// Zustandsautomat der Air Mouse.
//
// Aufbau: zwei getrennte Achsen statt eines flachen Zustands.
//
//   Power - Off / On, umgeschaltet durch Schuetteln.
//   Pose  - Point / Idle / Scroll, kommt aus der Verdrehung (PoseDetector).
//
// Die Haltung waehlt, was ein Pinch bedeutet:
//
//   Point  - Hand gerade      -> Linksklick, Cursor folgt der Bewegung.
//   Idle   - Hand abgedreht   -> Rechtsklick.
//   Scroll - weit abgedreht   -> Pinch ohne Wirkung, die Neigung scrollt.
//
// Uebergangstabelle:
//
//   Ereignis | Off    | On/Point       | On/Idle        | On/Scroll
//   ---------|--------|----------------|----------------|----------------
//   Shake    | -> On  | -> Off         | -> Off         | -> Off
//   Pose     | ignor. | Zeiger zurueck | ggf. Wechsel   | Scroll-Start
//   Pinch    | ignor. | Linksklick     | Rechtsklick    | ignoriert
//
// Der Automat fuehrt selbst nichts aus. Er meldet ueber Actions zurueck, was zu
// tun ist, und der Aufrufer erledigt es. Damit haengt er an keiner Hardware,
// laesst sich auf dem PC testen (test/test_state_machine.cpp), und alle
// Seiteneffekte stehen an genau einer Stelle im Controller.
//
// Es gibt bewusst keine Grab-Achse und kein pressButton/releaseButton mehr.
// Zwei Anlaeufe zum Ziehen sind wieder ausgebaut worden - erst der Doppel-Pinch
// (brauchte ein Wartefenster, das auf jedem gewoehnlichen Klick lag), dann
// Pinch-plus-Abdrehen (kollidierte mit dem Rechtsklick auf derselben Geste).
// Solange nichts eine Taste gedrueckt haelt, waere ein Zustand dafuer nur
// mitgeschleppter Ballast. Kommt das Ziehen spaeter ueber eine 180-Grad-Drehung
// zurueck, gehoert dazu wieder eine eigene Achse mit der Invariante
// "Power::Off impliziert Taste frei" - und ein Test dafuer.

enum class Power : uint8_t { Off, On };
enum class Pose  : uint8_t { Point, Idle, Scroll };

struct Actions {
    bool click         = false;  // links
    bool rightClick    = false;
    bool haptic        = false;
    bool resetPointer  = false;
    bool enterScroll   = false;  // Scroll-Joystick auf aktuelle Neigung nullen
    bool resetPose     = false;  // Haltungserkennung auf Point zuruecksetzen
};

class AirMouseState {
public:
    Actions onShake() {
        Actions a;
        if (power_ == Power::Off) {
            power_ = Power::On;
            pose_  = Pose::Point;
            a.resetPose    = true;
            a.resetPointer = true;
            a.haptic       = true;
            return a;
        }
        power_ = Power::Off;
        a.resetPointer = true;
        a.haptic       = true;
        return a;
    }

    Actions onPose(Pose next) {
        Actions a;
        if (power_ != Power::On || next == pose_) return a;

        if (pose_ == Pose::Point) a.resetPointer = true;   // verlassen
        pose_ = next;

        switch (next) {                                    // betreten
            case Pose::Scroll: a.enterScroll  = true; a.haptic = true; break;
            case Pose::Point:  a.resetPointer = true; a.haptic = true; break;
            // Idle bleibt bewusst still, sonst brummt es zweimal auf dem Weg
            // vom Zeigen in die Scroll-Haltung.
            case Pose::Idle:   break;
        }
        return a;
    }

    // Die Haltung entscheidet ueber die Taste. In der Scroll-Haltung bleibt der
    // Pinch wirkungslos: dort ist die Hand mit Scrollen beschaeftigt, und ein
    // Klick mitten im Lauf waere fuer den Nutzer nicht vorhersehbar.
    Actions onPinch() {
        Actions a;
        if (power_ != Power::On) return a;

        switch (pose_) {
            case Pose::Point: a.click      = true; a.haptic = true; break;
            case Pose::Idle:  a.rightClick = true; a.haptic = true; break;
            case Pose::Scroll: break;
        }
        return a;
    }

    Power power() const { return power_; }
    Pose  pose()  const { return pose_; }

    bool on()        const { return power_ == Power::On; }
    bool pointing()  const { return power_ == Power::On && pose_ == Pose::Point; }
    bool scrolling() const { return power_ == Power::On && pose_ == Pose::Scroll; }

private:
    Power power_ = Power::Off;
    Pose  pose_  = Pose::Point;
};
