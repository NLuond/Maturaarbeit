// Test des Zustandsautomaten. Laeuft auf dem PC, nicht auf dem Chip - der
// Automat haengt bewusst an keiner Hardware.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/AirMouseState -o "$env:TEMP\fsm.exe" test/test_state_machine/test_state_machine.cpp
//   & "$env:TEMP\fsm.exe"
//
#include "AirMouseState.h"
#include <cstdio>
#include <initializer_list>

static int failures = 0;
static int checks   = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        checks++;                                                           \
        if (!(cond)) {                                                      \
            failures++;                                                     \
            std::printf("  FEHLER Zeile %d: %s\n", __LINE__, (msg));        \
        }                                                                   \
    } while (0)

// --- Hilfen ------------------------------------------------------------

// Ein Pinch darf nie beide Tasten zugleich melden - sonst kaeme im Controller
// ein Links- und ein Rechtsklick im selben Takt heraus.
static bool actionsConsistent(const Actions& a) {
    if (a.pressLeft && a.rightClick) return false;
    if (a.pressLeft && a.releaseLeft) return false;
    return true;
}

static AirMouseState turnedOn() {
    AirMouseState s;
    s.onPower();
    return s;
}

static AirMouseState inPose(Pose p) {
    AirMouseState s = turnedOn();
    s.onPose(p);
    return s;
}

// In der abgedrehten Haltung wird der Scroll-Joystick erst nach einer Sekunde
// zugeschaltet. Diese Hilfe stellt genau das her.
static AirMouseState scrolling() {
    AirMouseState s = inPose(Pose::Turned);
    s.onTwistHeld();
    return s;
}

// --- Einzelne Anforderungen -------------------------------------------

static void test_startsOff() {
    AirMouseState s;
    CHECK(!s.on(), "startet nicht ausgeschaltet");
    CHECK(s.pose() == Pose::Point, "startet nicht in der Zeige-Haltung");
}

static void test_offIgnoresEverything() {
    AirMouseState s;
    const Actions a1 = s.onPinch(true, false);
    const Actions a2 = s.onPose(Pose::Turned);
    const Actions a3 = s.onPose(Pose::Idle);

    CHECK(!a1.pressLeft, "ausgeschaltet geht die linke Taste runter");
    CHECK(!a1.rightClick, "ausgeschaltet wird rechts geklickt");
    CHECK(!a2.enterScroll, "ausgeschaltet wird gescrollt");
    CHECK(a3.hapticPulses == 0, "ausgeschaltet brummt es");
    CHECK(!s.on(), "ausgeschaltet nicht mehr aus");

    // Auch die Haltung darf im Ruhezustand nicht mitwandern, sonst steht der
    // Automat beim Einschalten in einer Haltung, die niemand eingenommen hat.
    CHECK(s.pose() == Pose::Point, "Haltung wandert im Ruhezustand mit");
}

static void test_twistTogglePowersOn() {
    AirMouseState s;
    const Actions on = s.onPower();
    CHECK(s.on(), "Drehgeste schaltet nicht ein");
    CHECK(on.resetPose, "Haltungserkennung wird beim Einschalten nicht zurueckgesetzt");
    CHECK(on.resetPointer, "Zeiger wird beim Einschalten nicht genullt");
    CHECK(s.pose() == Pose::Point, "startet nicht in der Zeige-Haltung");

    const Actions off = s.onPower();
    CHECK(!s.on(), "zweite Drehgeste schaltet nicht aus");
    CHECK(off.resetPointer, "Ausschalten nullt den Zeiger nicht");
}

// Beim Einschalten in verdrehter Hand darf nicht die alte Haltung gelten.
static void test_twistToggleResetsPose() {
    AirMouseState s = inPose(Pose::Turned);
    s.onPower();                       // aus
    const Actions on = s.onPower();    // wieder ein
    CHECK(s.pose() == Pose::Point, "Haltung nach dem Einschalten nicht zurueckgesetzt");
    CHECK(on.resetPose, "Einschalten meldet kein Zuruecksetzen der Erkennung");
    CHECK(s.pointing(), "nach dem Einschalten wird nicht gezeigt");
}

// --- Der Pinch, je nach Haltung ---------------------------------------

// Druck UND Rueckmeldung kommen sofort - das ist der ganze Unterschied zu den
// frueheren Doppel-Pinch-Entwuerfen, die den Klick als Ganzes verzoegerten. Nur
// das Loslassen wartet auf das Fenster.
static void test_pinchWhilePointingPressesImmediately() {
    AirMouseState s = turnedOn();
    const Actions a = s.onPinch(true, false);
    CHECK(a.pressLeft, "Pinch beim Zeigen drueckt die linke Taste nicht");
    CHECK(!a.rightClick, "Pinch beim Zeigen klickt rechts");
    CHECK(a.hapticPulses == 1, "der Pinch meldet sich nicht sofort an der Hand");
    CHECK(s.holding(), "die Taste gilt nach dem Pinch nicht als unten");
    CHECK(!s.dragging(), "der erste Pinch verriegelt schon das Ziehen");
}

// Kein zweiter Pinch: Taste hoch. Ohne weitere Vibration - die kam schon beim
// Pinch, und ein zweiter Puls hier waere vom Rechtsklick nicht zu unterscheiden.
static void test_clickWindowCompletesTheClick() {
    AirMouseState s = turnedOn();
    s.onPinch(true, false);
    const Actions a = s.onClickWindow();
    CHECK(a.releaseLeft, "das Fensterende gibt die Taste nicht frei");
    CHECK(a.hapticPulses == 0, "das Fensterende brummt ein zweites Mal");
    CHECK(!s.holding(), "die Taste bleibt nach dem Klick unten");

    // Ohne gedrueckte Taste ist das Fensterende ein No-Op.
    CHECK(!s.onClickWindow().releaseLeft, "das Fensterende gibt ins Leere frei");
}

// Die BEWEGUNG macht aus dem Klick ein Ziehen - kein zweiter Pinch, kein
// Zeitband. Die Taste ist seit dem Pinch unten und bleibt es jetzt nur laenger.
static void test_movementTurnsTheClickIntoADrag() {
    AirMouseState s = turnedOn();
    s.onPinch(true, false);
    const Actions a = s.onDragMove();
    CHECK(!a.pressLeft, "die Taste wird ein zweites Mal gedrueckt");
    CHECK(!a.releaseLeft, "die Bewegung gibt die Taste frei");
    CHECK(a.hapticPulses == 3, "das Ziehen meldet nicht drei Impulse");
    CHECK(s.dragging(), "die Bewegung verriegelt das Ziehen nicht");

    // Und danach laeuft das Fenster ins Leere - das Ziehen endet nicht von
    // selbst, sondern nur durch Pinch, Haltungswechsel oder Zwangsfreigabe.
    CHECK(!s.onClickWindow().releaseLeft, "das Fensterende beendet das Ziehen");
    CHECK(s.dragging(), "das Ziehen endet mit dem Fenster");
}

// Ohne gedrueckte Taste ist Bewegung nur Bewegung.
static void test_movementWithoutPinchDoesNothing() {
    AirMouseState s = turnedOn();
    const Actions a = s.onDragMove();
    CHECK(!a.pressLeft && !a.releaseLeft, "Bewegung allein greift zu");
    CHECK(!s.holding(), "Bewegung allein haelt die Taste");
    CHECK(a.hapticPulses == 0, "Bewegung allein brummt");
}

// Der Loese-Impuls des laufenden Pinch faellt genau hierhin. Er darf nichts
// ausloesen - weder ein zweites Druecken noch ein Ziehen.
static void test_pinchDuringOpenWindowIsIgnored() {
    AirMouseState s = turnedOn();
    s.onPinch(true, false);
    const Actions a = s.onPinch(true, false);
    CHECK(!a.pressLeft, "der zweite Pinch drueckt erneut");
    CHECK(!a.releaseLeft, "der zweite Pinch gibt frei");
    CHECK(a.hapticPulses == 0, "der zweite Pinch brummt");
    CHECK(!s.dragging(), "ein zweiter Pinch verriegelt noch immer das Ziehen");
    CHECK(s.holding(), "der zweite Pinch verwirft den laufenden Klick");
}

// Kurz nach einer zuegigen Ausdrehung zeigt die FSM-Pose noch Point (siehe
// Kommentar bei AirMouseState::onPinch: POSE_CALM_MS + MODE_DWELL_MS +
// MODE_TAU Verzoegerung), obwohl der Arm koerperlich schon draussen ist. Ein
// Pinch in diesem Fenster darf weder links noch rechts klicken - Rechtsklick
// waere hier ebenso falsch, weil die FSM noch gar nicht weiss, dass die Hand
// dreht.
static void test_pinchSuppressedWhenArmPhysicallyOut() {
    const Actions a = turnedOn().onPinch(true, true);
    CHECK(!a.pressLeft, "Pinch drueckt links, obwohl der Arm ausgedreht ist");
    CHECK(!a.rightClick, "Pinch klickt rechts, obwohl die FSM-Pose noch Point ist");
    CHECK(a.hapticPulses == 0, "unterdrueckter Pinch brummt trotzdem");
}

static void test_pinchWhileTurnedClicksRight() {
    AirMouseState s = inPose(Pose::Turned);
    const Actions a = s.onPinch(true, false);
    CHECK(a.rightClick, "Pinch bei gedrehter Hand klickt nicht rechts");
    CHECK(!a.pressLeft, "Pinch bei gedrehter Hand drueckt links");
    CHECK(a.hapticPulses == 2, "Rechtsklick meldet nicht zwei Impulse");
}

// Wer den Arm kippt, scrollt gerade - ein Klick mitten im Lauf waere fuer den
// Nutzer nicht vorhersehbar.
static void test_pinchWhileActuallyScrollingDoesNothing() {
    AirMouseState s = scrolling();
    const Actions a = s.onPinch(false, false);
    CHECK(!a.pressLeft, "Pinch beim Scrollen drueckt links");
    CHECK(!a.rightClick, "Pinch beim Scrollen klickt rechts");
    CHECK(a.hapticPulses == 0, "Pinch beim Scrollen brummt");
}

// Auch waehrend der Scroll-Joystick zugeschaltet ist, gilt der Rechtsklick -
// solange die Neigung ruhig gehalten wird. Sonst waere er nach einer Sekunde
// unerreichbar.
static void test_rightClickWorksWhileScrollJoystickIsOn() {
    AirMouseState s = scrolling();
    const Actions a = s.onPinch(true, false);
    CHECK(a.rightClick, "Rechtsklick faellt weg, sobald der Joystick an ist");
}

// ScrollJoystick::dead_ ist eine Verriegelung: sie wird nur waehrend
// scroll_.update() nachgefuehrt, und das laeuft im Controller nur, solange
// scrolling() wahr ist. Verlaesst man die abgedrehte Haltung, friert dead_
// auf seinem letzten Wert ein - haengengeblieben bei false, wuerde ein
// erneutes Ausdrehen und sofortiges Pinchen (innerhalb der ersten Sekunde,
// bevor onTwistHeld den Joystick ueberhaupt wieder zuschaltet) faelschlich
// als "Neigung nicht ruhig" gelten und den Rechtsklick verschlucken. Ohne
// laufenden Joystick darf scrollIdle deshalb gar nicht erst gefragt werden.
static void test_rightClickIgnoresStaleScrollIdleWhenJoystickIsOff() {
    AirMouseState s = inPose(Pose::Turned);
    const Actions a1 = s.onPinch(true, false);
    CHECK(a1.rightClick, "Rechtsklick faellt aus, obwohl der Joystick noch aus ist");
    CHECK(a1.hapticPulses == 2, "Rechtsklick meldet nicht zwei Impulse");

    AirMouseState s2 = inPose(Pose::Turned);
    const Actions a2 = s2.onPinch(false, false);
    CHECK(a2.rightClick, "eine veraltete scrollIdle==false unterdrueckt den Rechtsklick");
    CHECK(a2.hapticPulses == 2, "Rechtsklick meldet nicht zwei Impulse");
}

static void test_pinchInIdleDoesNothing() {
    AirMouseState s = inPose(Pose::Idle);
    const Actions a = s.onPinch(true, false);
    CHECK(!a.pressLeft && !a.rightClick, "Pinch bei nicht waagrechtem Arm klickt");
    CHECK(a.hapticPulses == 0, "Pinch in Idle brummt");
}

// Genau eine Haltung je Taste - sonst laesst sich aus dem Cursorverhalten nicht
// mehr ablesen, welche Taste ein Pinch gerade ausloesen wuerde.
static void test_exactlyOneButtonPerPose() {
    int left = 0, right = 0, none = 0;
    for (Pose p : {Pose::Point, Pose::Idle, Pose::Turned}) {
        AirMouseState s = inPose(p);
        const Actions a = s.onPinch(true, false);
        CHECK(actionsConsistent(a), "Pinch meldet beide Tasten zugleich");
        if (a.pressLeft)       left++;
        else if (a.rightClick) right++;
        else                   none++;
    }
    CHECK(left == 1,  "nicht genau eine Haltung klickt links");
    CHECK(right == 1, "nicht genau eine Haltung klickt rechts");
    CHECK(none == 1,  "nicht genau eine Haltung bleibt wirkungslos");
}

// Der Pinch laesst Power und Pose unberuehrt. Die Grab-Achse ist die einzige,
// die er bewegt - und zwar absichtlich: darauf beruht das Ziehen. Ausserhalb
// von Point beruehrt er auch die gar nicht.
static void test_pinchLeavesPowerAndPoseAlone() {
    for (Pose p : {Pose::Point, Pose::Idle, Pose::Turned}) {
        AirMouseState s = inPose(p);
        for (int i = 0; i < 20; i++) {
            const Actions a = s.onPinch(true, false);
            CHECK(s.pose() == p, "Pinch veraendert die Haltung");
            CHECK(s.on(), "Pinch schaltet ab");
            CHECK(actionsConsistent(a), "widerspruechliche Aktionen");
            if (p != Pose::Point) CHECK(!s.holding(), "Pinch haelt die Taste ausserhalb von Point");
        }
    }
}

// In Turned bleibt der Rechtsklick zustandslos: beliebig viele Pinches ergeben
// beliebig viele gleiche Rechtsklicks, ohne Vorgeschichte.
static void test_rightClickIsStateless() {
    AirMouseState s = inPose(Pose::Turned);
    const Actions first = s.onPinch(true, false);
    for (int i = 0; i < 20; i++) {
        const Actions again = s.onPinch(true, false);
        CHECK(again.rightClick == first.rightClick, "Rechtsklick haengt an der Vorgeschichte");
    }
}

// --- Haltungswechsel ---------------------------------------------------

static void test_samePoseIsNoOp() {
    for (Pose p : {Pose::Point, Pose::Idle, Pose::Turned}) {
        AirMouseState s = inPose(p);
        const Actions a = s.onPose(p);
        CHECK(a.hapticPulses == 0 && !a.resetPointer && !a.enterScroll,
              "unveraenderte Haltung loest Aktionen aus");
    }
}

// Der Scroll-Joystick kommt nicht mit der Haltung, sondern erst mit onTwistHeld.
// Sonst wuerde jede Ein/Aus-Geste nebenbei ein Stueck weit scrollen.
static void test_turnedAloneDoesNotScroll() {
    AirMouseState s = inPose(Pose::Turned);
    CHECK(!s.scrolling(), "die Haltung allein schaltet schon den Joystick zu");
    const Actions a = s.onTwistHeld();
    CHECK(a.enterScroll, "onTwistHeld nullt den Joystick nicht");
    CHECK(s.scrolling(), "onTwistHeld schaltet den Joystick nicht zu");
}

// Beim Verlassen der Zeige-Haltung muss der aufgelaufene Rest verfallen, sonst
// laeuft er beim Zurueckdrehen als Sprung in den Cursor.
static void test_leavingPointResetsPointer() {
    for (Pose to : {Pose::Idle, Pose::Turned}) {
        AirMouseState s = turnedOn();
        const Actions a = s.onPose(to);
        CHECK(a.resetPointer, "Verlassen der Zeige-Haltung nullt den Zeiger nicht");
    }
}

static void test_enteringPointResetsPointer() {
    for (Pose from : {Pose::Idle, Pose::Turned}) {
        AirMouseState s = inPose(from);
        const Actions a = s.onPose(Pose::Point);
        CHECK(a.resetPointer, "Betreten der Zeige-Haltung nullt den Zeiger nicht");
        CHECK(s.pointing(), "Zeige-Haltung kommt nicht an");
    }
}

// Verlaesst man die abgedrehte Haltung, muss der Joystick wieder aus sein -
// sonst scrollte die naechste Ausdrehung ohne Haltezeit sofort los.
static void test_leavingTurnedStopsScrolling() {
    AirMouseState s = scrolling();
    s.onPose(Pose::Point);
    CHECK(!s.scrolling(), "der Joystick bleibt nach dem Zurueckdrehen an");
    s.onPose(Pose::Turned);
    CHECK(!s.scrolling(), "der Joystick kommt ohne Haltezeit zurueck");
}

// Auf dem Weg vom Zeigen in die Scroll-Haltung liegt Idle. Es darf dabei nicht
// zusaetzlich brummen, sonst kommen zwei Impulse fuer eine Bewegung.
static void test_idleIsSilent() {
    AirMouseState s = turnedOn();
    const Actions a = s.onPose(Pose::Idle);
    CHECK(a.hapticPulses == 0, "der Weg nach Idle brummt");
    CHECK(!a.enterScroll, "Idle startet den Scroll-Joystick");
}

static void test_onTwistHeldOnlyInTurned() {
    for (Pose p : {Pose::Point, Pose::Idle}) {
        AirMouseState s = inPose(p);
        const Actions a = s.onTwistHeld();
        CHECK(!a.enterScroll, "onTwistHeld wirkt ausserhalb der abgedrehten Haltung");
        CHECK(!s.scrolling(), "gescrollt ausserhalb der abgedrehten Haltung");
    }
    AirMouseState off;
    const Actions a = off.onTwistHeld();
    CHECK(!a.enterScroll, "onTwistHeld wirkt im Ruhezustand");
}

// --- Die Grab-Achse ----------------------------------------------------

// Hilfe: verriegeltes Ziehen ueber den Doppel-Pinch.
static AirMouseState dragging() {
    AirMouseState s = turnedOn();
    s.onPinch(true, false);
    s.onDragMove();
    return s;
}

// DIE Invariante: nie mit gedrueckter Taste abschalten. Eine haengende Taste,
// die der Rechner nicht mehr loswird, ist der einzige Fehlerfall hier, der ihn
// unbenutzbar macht. Gilt in BEIDEN Zustaenden der Achse - auch das noch
// unentschiedene Pending haelt die Taste koerperlich unten.
static void test_powerOffReleasesTheButton() {
    AirMouseState s = dragging();
    const Actions a = s.onPower();
    CHECK(a.releaseLeft, "das Abschalten gibt die Taste nicht frei");
    CHECK(!s.holding(), "ausgeschaltet, aber die Taste ist noch unten");

    AirMouseState p = turnedOn();
    p.onPinch(true, false);                 // Pending, Fenster laeuft noch
    CHECK(p.onPower().releaseLeft, "das Abschalten im offenen Fenster laesst die Taste unten");
}

// Zweite Fassung derselben Invariante: Arm abgelegt oder abgedreht heisst
// fallenlassen.
static void test_leavingPointReleasesTheButton() {
    for (Pose p : { Pose::Idle, Pose::Turned }) {
        AirMouseState s = dragging();
        const Actions a = s.onPose(p);
        CHECK(a.releaseLeft, "das Verlassen der Zeige-Haltung gibt die Taste nicht frei");
        CHECK(!s.holding(), "die Grab-Achse ueberlebt den Haltungswechsel");
    }
}

// Der zweite, schnellere Ausstieg - und zugleich der Grund, warum der Automat
// waehrend des Ziehens nie eine Taste druecken kann.
static void test_pinchWhileDraggingReleasesInsteadOfPressing() {
    AirMouseState s = dragging();
    const Actions a = s.onPinch(true, false);
    CHECK(a.releaseLeft, "der Pinch laesst waehrend des Ziehens nicht los");
    CHECK(!a.pressLeft, "der Pinch drueckt waehrend des Ziehens erneut");
    CHECK(a.hapticPulses == 3, "das Fallenlassen meldet nicht drei Impulse");
    CHECK(!s.holding(), "das Ziehen laeuft nach dem Pinch weiter");

    // Danach ist alles wieder normal: der naechste Pinch beginnt einen Klick.
    CHECK(s.onPinch(true, false).pressLeft, "nach dem Loslassen drueckt der Pinch nicht mehr");
}

static void test_dragReleaseIsUnconditional() {
    AirMouseState s = dragging();
    CHECK(s.onDragRelease().releaseLeft, "die Zwangsfreigabe gibt die Taste nicht frei");
    CHECK(!s.holding(), "die Zwangsfreigabe wirkt nicht");
    CHECK(!s.onDragRelease().releaseLeft,
          "die Zwangsfreigabe meldet eine Freigabe, obwohl nichts gehalten wird");
}

// Ein/Aus ist das einzige Ereignis mit langem Puls - danach geht gar nichts
// mehr, das muss sich von jedem Klickmuster abheben.
static void test_powerUsesTheLongPulse() {
    AirMouseState s;
    CHECK(s.onPower().hapticLong, "das Einschalten brummt nicht lang");
    CHECK(s.onPower().hapticLong, "das Ausschalten brummt nicht lang");

    AirMouseState t = turnedOn();
    CHECK(!t.onPose(Pose::Turned).hapticLong, "der Haltungswechsel brummt lang");
    CHECK(!t.onPinch(true, false).hapticLong, "der Rechtsklick brummt lang");
}

// --- Erschoepfender Durchlauf -----------------------------------------

// Jeder erreichbare Zustand mal jedes Ereignis. Prueft nicht die Bedeutung,
// sondern dass nichts undefiniert bleibt und nichts im Ruhezustand wirkt.
static void test_allStatesAllEvents() {
    const Power powers[] = { Power::Off, Power::On };
    const Pose  poses[]  = { Pose::Point, Pose::Idle, Pose::Turned };

    int visited = 0;
    for (Power pw : powers) {
        for (Pose po : poses) {
            for (int ev = 0; ev < 11; ev++) {
                AirMouseState s;
                if (pw == Power::On) { s.onPower(); s.onPose(po); }
                else if (po != Pose::Point) continue;   // aus gibt es nur Point

                Actions a;
                bool armOut = false;   // nur bei Fall 3/4 gesetzt, siehe unten
                switch (ev) {
                    case 0: a = s.onPower();                   break;
                    case 1: a = s.onPinch(true, false);        break;
                    case 2: a = s.onPinch(false, false);       break;
                    // armOut == true: Arm koerperlich ausgedreht, waehrend die
                    // FSM-Pose (hier: po) noch nicht nachgezogen hat.
                    case 3: armOut = true; a = s.onPinch(true, true);  break;
                    case 4: armOut = true; a = s.onPinch(false, true); break;
                    case 5: a = s.onPose(Pose::Point);         break;
                    case 6: a = s.onPose(Pose::Idle);          break;
                    case 7: a = s.onPose(Pose::Turned);        break;
                    case 8: a = s.onTwistHeld();               break;
                    case 9:  a = s.onClickWindow();            break;
                    case 10: a = s.onDragMove();               break;
                }
                visited++;

                CHECK(actionsConsistent(a), "widerspruechliche Aktionen");
                if (!s.on()) {
                    CHECK(!a.pressLeft && !a.rightClick, "geklickt, obwohl ausgeschaltet");
                    CHECK(!a.enterScroll, "gescrollt, obwohl ausgeschaltet");
                    CHECK(!s.holding(), "Taste unten, obwohl ausgeschaltet");
                }
                // armOut unterdrueckt nur den Linksklick in Point - er darf
                // den Rechtsklick in Turned nicht mitreissen, sonst waere ein
                // ausgedrehter Arm in der abgedrehten Haltung nie klickbar.
                if (armOut && po == Pose::Point) {
                    CHECK(!a.pressLeft, "Pinch drueckt links, obwohl der Arm ausgedreht ist");
                }
            }
        }
    }
    // Aus: nur Point -> 1 Gruppe. Ein: 3 Haltungen. Zusammen 4 mal 11 Ereignisse.
    CHECK(visited == 44, "nicht alle Kombinationen durchlaufen");
}

// Zufaellige, lange Ereignisfolge - findet Reihenfolgen, an die man beim
// Schreiben der Einzeltests nicht denkt.
static void test_randomWalkKeepsInvariants() {
    AirMouseState s;
    uint32_t rng = 12345;
    for (int i = 0; i < 20000; i++) {
        rng = rng * 1664525u + 1013904223u;
        Actions a;
        bool armOut = false;
        const bool wasHolding = s.holding();
        switch ((rng >> 16) % 11) {
            case 0: a = s.onPower();                   break;
            case 1: a = s.onPinch(true, false);        break;
            case 2: a = s.onPinch(false, false);       break;
            case 3: armOut = true; a = s.onPinch(true, true);  break;
            case 4: armOut = true; a = s.onPinch(false, true); break;
            case 5: a = s.onPose(Pose::Point);         break;
            case 6: a = s.onPose(Pose::Idle);          break;
            case 7: a = s.onPose(Pose::Turned);        break;
            case 8: a = s.onTwistHeld();               break;
            case 9:  a = s.onClickWindow();            break;
            case 10: a = s.onDragMove();               break;
        }
        if (!actionsConsistent(a)) { CHECK(false, "widerspruechliche Aktionen im Zufallslauf"); return; }

        // Jede Wirkung setzt den eingeschalteten Zustand voraus.
        if (a.pressLeft || a.rightClick || a.enterScroll) {
            if (!s.on()) { CHECK(false, "Wirkung im Ruhezustand"); return; }
        }
        // Und jede Taste die Haltung, die zu ihr gehoert.
        if (a.pressLeft  && !s.pointing()) { CHECK(false, "Linksklick ausserhalb der Zeige-Haltung"); return; }
        if (armOut && a.pressLeft) { CHECK(false, "Linksklick trotz ausgedrehtem Arm"); return; }
        if (a.rightClick && s.pose() != Pose::Turned) {
            CHECK(false, "Rechtsklick ausserhalb der abgedrehten Haltung"); return;
        }
        // Der Joystick darf nie ohne die passende Haltung laufen.
        if (s.scrolling() && s.pose() != Pose::Turned) {
            CHECK(false, "Joystick laeuft ausserhalb der abgedrehten Haltung"); return;
        }
        if (s.scrolling() && !s.on()) { CHECK(false, "Joystick laeuft im Ruhezustand"); return; }

        // Die Invarianten der Grab-Achse. Sie gelten fuer holding(), nicht nur
        // fuer dragging(): auch das unentschiedene Pending haelt die Taste
        // koerperlich unten.
        if (s.holding() && !s.on()) {
            CHECK(false, "ausgeschaltet mit gedrueckter Taste"); return;
        }
        if (s.holding() && s.pose() != Pose::Point) {
            CHECK(false, "Taste unten ausserhalb der Zeige-Haltung"); return;
        }
        // Wer drueckt, hielt vorher nichts; wer loslaesst, hielt vorher etwas.
        // Sonst laufen HID-Zustand und Achse auseinander.
        if (a.pressLeft   &&  wasHolding) { CHECK(false, "zweimal gedrueckt");     return; }
        if (a.releaseLeft && !wasHolding) { CHECK(false, "ins Leere losgelassen"); return; }
    }
}

int main() {
    test_startsOff();
    test_offIgnoresEverything();
    test_twistTogglePowersOn();
    test_twistToggleResetsPose();
    test_pinchWhilePointingPressesImmediately();
    test_clickWindowCompletesTheClick();
    test_movementTurnsTheClickIntoADrag();
    test_movementWithoutPinchDoesNothing();
    test_pinchDuringOpenWindowIsIgnored();
    test_pinchSuppressedWhenArmPhysicallyOut();
    test_pinchWhileTurnedClicksRight();
    test_pinchWhileActuallyScrollingDoesNothing();
    test_rightClickWorksWhileScrollJoystickIsOn();
    test_rightClickIgnoresStaleScrollIdleWhenJoystickIsOff();
    test_pinchInIdleDoesNothing();
    test_exactlyOneButtonPerPose();
    test_pinchLeavesPowerAndPoseAlone();
    test_rightClickIsStateless();
    test_samePoseIsNoOp();
    test_turnedAloneDoesNotScroll();
    test_leavingPointResetsPointer();
    test_enteringPointResetsPointer();
    test_leavingTurnedStopsScrolling();
    test_idleIsSilent();
    test_onTwistHeldOnlyInTurned();
    test_powerOffReleasesTheButton();
    test_leavingPointReleasesTheButton();
    test_pinchWhileDraggingReleasesInsteadOfPressing();
    test_dragReleaseIsUnconditional();
    test_powerUsesTheLongPulse();
    test_allStatesAllEvents();
    test_randomWalkKeepsInvariants();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
