#pragma once
#include <stdint.h>

// Zustandsautomat der Air Mouse. Drei getrennte Achsen statt eines flachen
// Zustands:
//
//   Power - Off / On, umgeschaltet durch die Drehgeste (TwistToggle).
//   Pose  - Point / Idle / Turned, aus der Haltung (PoseDetector).
//   Grab  - Off / Pending / On, die Linkstaste (Pinch).
//
//   Ereignis        | Off    | On/Point       | On/Idle        | On/Turned
//   ----------------|--------|----------------|----------------|----------------
//   onPower         | -> On  | -> Off         | -> Off         | -> Off
//   onPose          | ignor. | Zeiger zurueck | ggf. Wechsel   | Joystick aus
//   onTwistHeld     | ignor. | ignor.         | ignor.         | Scroll-Start
//   onPinch         | ignor. | Taste runter** | ignoriert      | Rechtsklick*
//   onClickWindow   | ignor. | Taste hoch     | Taste hoch     | Taste hoch
//
//   *  ausser der Joystick laeuft UND die Neigung steht ausserhalb der Totzone
//   ** ausser armOut: der Arm ist koerperlich schon draussen, siehe onPinch()
//
// Der Automat fuehrt selbst nichts aus: jedes Ereignis liefert ein Actions-
// Struct zurueck, das der Aufrufer erledigt. Damit haengt er an keiner Hardware
// und alle Seiteneffekte stehen an genau einer Stelle im Controller.
//
// KLICK UND ZIEHEN TEILEN SICH EINEN PFAD. Der Pinch drueckt die Taste sofort
// (Grab::Pending) - ohne jede Verzoegerung. Was daraus wird, entscheidet die
// BEWEGUNG, nicht eine zweite Geste und kein Zeitfenster, das man treffen muss:
//
//   Cursor bewegt sich > DRAG_MOVE_PX  -> Taste bleibt unten = Ziehen (Grab::On)
//   Cursor bleibt stehen, Fenster aus  -> Taste hoch          = Klick
//   Pinch waehrend Grab::On            -> Taste hoch          = fallenlassen
//
// Das ist dasselbe Kriterium, mit dem jeder Touchscreen Tippen von Wischen
// trennt. Der Anfang ist damit Apples Geste ("pinch and move"); nur das Ende
// ist ein zweiter Pinch statt des Loslassens - der LOESE-Impuls ist am Geraet
// gemessen worden und taugt nicht: nur teilweise vorhanden, bei kurzem Pinch
// gar nicht, kaum ueber dem Rauschen.
//
// Die Vibration kommt SOFORT beim Pinch, nicht erst bei der Entscheidung: ein
// um das ganze Fenster verzoegerter Puls fuehlt sich an, als gaebe es gar
// keinen. Den zweiten Pinch kann er nicht vortaeuschen - er dauert 40 ms,
// gezaehlt wird erst ab DEBOUNCE_MS (200 ms).
//
// Die Invariante der Grab-Achse: NIE mit gedrueckter Taste abschalten oder die
// Zeige-Haltung verlassen. Eine haengende Taste, die der Rechner nicht mehr
// loswird, ist der einzige Fehlerfall hier, der ihn unbenutzbar macht - deshalb
// geben onPower() und onPose() sie von sich aus frei.

enum class Power : uint8_t { Off, On };
enum class Pose  : uint8_t { Point, Idle, Turned };
enum class Grab  : uint8_t { Off, Pending, On };

struct Actions {
    bool rightClick   = false;  // rechts, weiterhin als ganzer Klick
    bool pressLeft    = false;  // links druecken und gedrueckt lassen
    bool releaseLeft  = false;
    bool resetPointer = false;
    bool enterScroll  = false;  // Scroll-Joystick auf aktuelle Neigung nullen
    bool resetPose    = false;  // Haltungserkennung auf Point zuruecksetzen

    // 0 = still, 1 = Linksklick / Haltungswechsel, 2 = Rechtsklick,
    // 3 = Ziehen an oder aus. Der Rechtsklick passiert in einer Haltung, in der
    // man den Cursor nicht sieht, und beim Ziehen sieht man zwar den Cursor,
    // aber nicht den Tastenzustand - beides muss an der Hand unterscheidbar sein.
    uint8_t hapticPulses = 0;

    // Ein/Aus bekommt einen langen Puls statt mehrerer kurzer: es ist das
    // einzige Ereignis, bei dem danach gar nichts mehr geht, und muss sich
    // deutlich von jedem Klickmuster abheben.
    bool hapticLong = false;
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
            a.hapticLong   = true;
            return a;
        }
        power_    = Power::Off;
        scrollOn_ = false;
        dropGrab(a);
        a.resetPointer = true;
        a.hapticPulses = 1;
        a.hapticLong   = true;
        return a;
    }

    Actions onPose(Pose next) {
        Actions a;
        if (power_ != Power::On || next == pose_) return a;

        if (pose_ == Pose::Point) { a.resetPointer = true; dropGrab(a); }  // verlassen
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

        // Die Taste ist schon unten und die Entscheidung noch offen - hier gibt
        // es nichts zu tun. Ein zweites pressLeft wuerde den HID-Zustand von der
        // Achse abkoppeln. Trifft meist den Loese-Impuls des laufenden Pinch.
        if (grab_ == Grab::Pending) return a;

        // Waehrend des Ziehens laesst der Pinch los. Fallenlassen ist dieselbe
        // Geste wie aufnehmen, nur im anderen Zustand.
        if (grab_ == Grab::On) {
            dropGrab(a);
            a.hapticPulses = 3;
            return a;
        }

        switch (pose_) {
            case Pose::Point:
                if (armOut) break;
                // Sofort runter, ohne Fenster. Hoch geht sie erst mit
                // onClickWindow() - bis dahin kann ein zweiter Pinch daraus ein
                // Ziehen machen. Die Vibration kommt deshalb auch erst dort.
                grab_          = Grab::Pending;
                a.pressLeft    = true;
                a.hapticPulses = 1;
                break;
            case Pose::Turned:
                if (!scrollOn_ || scrollIdle) { a.rightClick = true; a.hapticPulses = 2; }
                break;
            case Pose::Idle:
                break;
        }
        return a;
    }

    // Der Cursor hat sich mit gedrueckter Taste bewegt: aus dem Klick wird ein
    // Ziehen. Kein pressLeft - die Taste ist seit dem Pinch unten, sie bleibt es
    // jetzt nur laenger.
    Actions onDragMove() {
        Actions a;
        if (grab_ != Grab::Pending) return a;
        grab_ = Grab::On;
        a.hapticPulses = 3;
        return a;
    }

    // Das Fenster ist ohne Bewegung abgelaufen: es war ein gewoehnlicher Klick,
    // die Taste geht hoch.
    //
    // Ohne Vibration - die kam schon beim Pinch. Sie ans Fensterende zu legen
    // hiess, die Rueckmeldung um das ganze Fenster zu verzoegern, und das fuehlt
    // sich an, als gaebe es gar keine.
    Actions onClickWindow() {
        Actions a;
        if (grab_ != Grab::Pending) return a;
        dropGrab(a);
        return a;
    }

    // Bedingungslose Freigabe fuer die Zwangsfreigabe des Controllers.
    Actions onDragRelease() {
        Actions a;
        dropGrab(a);
        return a;
    }

    Pose pose() const { return pose_; }

    bool on()        const { return power_ == Power::On; }
    bool pointing()  const { return power_ == Power::On && pose_ == Pose::Point; }
    bool scrolling() const { return power_ == Power::On && pose_ == Pose::Turned && scrollOn_; }
    bool dragging()  const { return grab_ == Grab::On; }
    bool holding()   const { return grab_ != Grab::Off; }   // Taste unten, egal warum

private:
    Power power_    = Power::Off;
    Pose  pose_     = Pose::Point;
    Grab  grab_     = Grab::Off;
    bool  scrollOn_ = false;

    // Die eine Stelle, an der die Taste freigegeben wird. Alles, was die
    // Invariante zu wahren hat, geht hier durch.
    void dropGrab(Actions& a) {
        if (grab_ == Grab::Off) return;
        grab_ = Grab::Off;
        a.releaseLeft = true;
    }
};
