#pragma once
#include <stdint.h>

// Zustandsautomat der Air Mouse.
//
// Aufbau: zwei getrennte Achsen statt eines flachen Zustands.
//
//   Power - Off / On, umgeschaltet durch die Drehgeste (TwistToggle).
//   Pose  - Point / Idle / Turned, kommt aus der Verdrehung (PoseDetector).
//
// Die Haltung waehlt, was ein Pinch bedeutet:
//
//   Point  - Hand gerade         -> Linksklick, Cursor folgt der Bewegung.
//   Idle   - Arm nicht waagrecht -> nichts. Idle ist KEINE Zone der
//            Verdrehung mehr, sondern allein das Ergebnis des Waagrecht-Gates.
//   Turned - Hand abgedreht      -> Rechtsklick; nach einer Sekunde zusaetzlich
//            Scroll-Joystick ueber die Armneigung.
//
// Uebergangstabelle:
//
//   Ereignis   | Off    | On/Point       | On/Idle        | On/Turned
//   -----------|--------|----------------|----------------|----------------
//   onPower    | -> On  | -> Off         | -> Off         | -> Off
//   onPose     | ignor. | Zeiger zurueck | ggf. Wechsel   | ggf. Wechsel
//   onTwistHeld| ignor. | ignor.         | ignor.         | Scroll-Start
//   onPinch    | ignor. | Linksklick**   | ignoriert      | Rechtsklick*
//
//   * ausser der Scroll-Joystick laeuft UND die Armneigung steht gerade
//     ausserhalb der Totzone (scrollIdle == false) - dann ist die Hand mit
//     Scrollen beschaeftigt, und ein Klick mitten im Ausschlag waere fuer den
//     Nutzer nicht vorhersehbar. Laeuft der Joystick nicht, gibt es nichts,
//     womit die Neigung kollidieren koennte, und der Rechtsklick zaehlt immer.
//
//  ** ausser armOut == true: die FSM-Pose ist um POSE_CALM_MS + MODE_DWELL_MS +
//     MODE_TAU verzoegert und zeigt nach einer zuegigen Ausdrehung noch Point,
//     waehrend der Arm koerperlich schon draussen ist. Ein Pinch in diesem
//     Fenster bleibt wirkungslos statt links zu klicken - siehe onPinch().
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
enum class Pose  : uint8_t { Point, Idle, Turned };

struct Actions {
    bool click        = false;  // links
    bool rightClick   = false;
    bool resetPointer = false;
    bool enterScroll  = false;  // Scroll-Joystick auf aktuelle Neigung nullen
    bool resetPose    = false;  // Haltungserkennung auf Point zuruecksetzen

    // 0 = still, 1 = Linksklick / Haltungswechsel / Ein-Aus, 2 = Rechtsklick.
    // Der Rechtsklick ist an der Hand sonst nicht vom Linksklick zu
    // unterscheiden - und er passiert in einer Haltung, in der man den Cursor
    // nicht sieht.
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
            // onTwistHeld() nach einer Sekunde. Sonst wuerde jede Ein/Aus-Geste
            // nebenbei ein Stueck weit scrollen.
            case Pose::Turned: a.hapticPulses = 1; break;
            case Pose::Point:  a.resetPointer = true; a.hapticPulses = 1; break;
            // Idle bleibt bewusst still, sonst brummt es zweimal auf dem Weg
            // vom Zeigen in die abgedrehte Haltung.
            case Pose::Idle:   break;
        }
        return a;
    }

    // Die Ausdrehung wurde laenger als das Schaltfenster gehalten: aus der
    // Ein/Aus-Geste ist der Scroll-Modus geworden.
    Actions onTwistHeld() {
        Actions a;
        if (power_ != Power::On || pose_ != Pose::Turned || scrollOn_) return a;
        scrollOn_      = true;
        a.enterScroll  = true;
        a.hapticPulses = 1;
        return a;
    }

    // Die Haltung entscheidet ueber die Taste. scrollIdle sagt, ob die
    // Armneigung in der Totzone des Joysticks steht - das zaehlt aber nur,
    // wenn der Joystick ueberhaupt laeuft (scrollOn_): wer gerade scrollt und
    // den Arm kippt, bekaeme sonst einen Klick mitten im Ausschlag. Laeuft der
    // Joystick nicht - die ersten Sekunde der Ausdrehung, oder danach wieder
    // nach dem Zurueckdrehen - ist die Neigung fuer den Klick bedeutungslos
    // und der Rechtsklick zaehlt immer.
    //
    // armOut meldet, dass TwistToggle den Arm gerade koerperlich als
    // ausgedreht sieht (Betrag ueber TURN_ON_DEG), auch wenn pose_ hier noch
    // Point ist. Die FSM-Pose kommt erst nach POSE_CALM_MS + MODE_DWELL_MS +
    // MODE_TAU (Glaettung, Bewegungssperre, Haltezeit) an - eine zuegige
    // 90-Grad-Drehung braucht dafuer 400 bis 700 ms. Ein Pinch in diesem
    // Fenster waere sonst ein Linksklick an einer Cursorposition, die der
    // Nutzer nicht sieht (die Hand zeigt ja gerade nicht mehr geradeaus),
    // waehrend derselbe Pinch ueber cancel() im selben Moment die laufende
    // Schaltgeste killt.
    //
    // Absichtlich unterdrueckt statt auf Rechtsklick umgeleitet: nichts zu tun
    // ist rueckholbar - der naechste Pinch eine halbe Sekunde spaeter trifft
    // wieder die richtige Taste -, ein Klick auf die falsche Taste am
    // unsichtbaren Cursor waere es nicht.
    Actions onPinch(bool scrollIdle, bool armOut) {
        Actions a;
        if (power_ != Power::On) return a;

        switch (pose_) {
            case Pose::Point:
                if (armOut) break;   // siehe Kommentar oben: lieber nichts als falsch
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

    Power power() const { return power_; }
    Pose  pose()  const { return pose_; }

    bool on()        const { return power_ == Power::On; }
    bool pointing()  const { return power_ == Power::On && pose_ == Pose::Point; }
    bool scrolling() const { return power_ == Power::On && pose_ == Pose::Turned && scrollOn_; }

private:
    Power power_    = Power::Off;
    Pose  pose_     = Pose::Point;
    bool  scrollOn_ = false;
};
