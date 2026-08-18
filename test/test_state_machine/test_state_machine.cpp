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
    return !(a.click && a.rightClick);
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

// --- Grundzustand ------------------------------------------------------

static void test_startsOff() {
    AirMouseState s;
    CHECK(!s.on(), "startet nicht ausgeschaltet");
    CHECK(s.pose() == Pose::Point, "startet nicht in der Zeige-Haltung");
    CHECK(!s.scrolling(), "startet im Scroll-Modus");
}

static void test_offIgnoresEverything() {
    AirMouseState s;
    const Actions a1 = s.onPinch(false);
    const Actions a2 = s.onPose(Pose::Turned);

    CHECK(!a1.click && !a1.rightClick, "ausgeschaltet wird geklickt");
    CHECK(a2.hapticPulses == 0, "ausgeschaltet brummt es");
    CHECK(!s.on(), "ausgeschaltet nicht mehr aus");
    CHECK(!s.scrolling(), "ausgeschaltet wird gescrollt");
}

static void test_twistTogglePowersOn() {
    AirMouseState s;
    const Actions on = s.onPower();
    CHECK(s.on(), "die Drehgeste schaltet nicht ein");
    CHECK(on.resetPose, "Einschalten meldet kein Zuruecksetzen der Erkennung");
    CHECK(on.hapticLong, "Einschalten brummt nicht lang");
    CHECK(s.pointing(), "nach dem Einschalten wird nicht gezeigt");

    const Actions off = s.onPower();
    CHECK(!s.on(), "die Drehgeste schaltet nicht aus");
    CHECK(off.hapticLong, "Ausschalten brummt nicht lang");
    CHECK(off.resetPointer, "Ausschalten verwirft den Rueckstau nicht");
}

// Ein/Aus ist das einzige Ereignis mit langem Puls - danach geht gar nichts
// mehr, das muss sich von jedem Klickmuster abheben.
static void test_onlyPowerUsesTheLongPulse() {
    AirMouseState s = turnedOn();
    CHECK(!s.onPose(Pose::Turned).hapticLong, "der Haltungswechsel brummt lang");
    CHECK(!s.onPinch(false).hapticLong,       "der Rechtsklick brummt lang");
}

// --- Die Matrix: Haltung waehlt, was Bewegung und Pinch bedeuten --------

// Der Klick ist unteilbar und kommt SOFORT. Ein frueherer Entwurf hielt die
// Taste ein Fenster lang unten, um daraus ein Ziehen machen zu koennen - das
// verzoegerte jeden Klick und war ueber BLE deutlich spuerbar.
static void test_pinchWhilePointingClicksLeft() {
    AirMouseState s = turnedOn();
    const Actions a = s.onPinch(false);
    CHECK(a.click, "Pinch beim Zeigen klickt nicht links");
    CHECK(!a.rightClick, "Pinch beim Zeigen klickt rechts");
    CHECK(a.hapticPulses == 1, "der Linksklick meldet nicht genau einen Impuls");
}

static void test_pinchWhileTurnedClicksRight() {
    AirMouseState s = inPose(Pose::Turned);
    const Actions a = s.onPinch(false);
    CHECK(a.rightClick, "Pinch bei gedrehter Hand klickt nicht rechts");
    CHECK(!a.click, "Pinch bei gedrehter Hand klickt links");
    CHECK(a.hapticPulses == 2, "der Rechtsklick meldet nicht zwei Impulse");
}

static void test_pinchInIdleDoesNothing() {
    AirMouseState s = inPose(Pose::Idle);
    const Actions a = s.onPinch(false);
    CHECK(!a.click && !a.rightClick, "Pinch bei zu steilem Arm klickt");
    CHECK(a.hapticPulses == 0, "Pinch in Idle brummt");
}

// Gescrollt wird, sobald der Arm abgedreht ist - ohne Griff, ohne Haltezeit.
static void test_turningAloneScrolls() {
    AirMouseState s = turnedOn();
    CHECK(!s.scrolling(), "in der Zeige-Haltung wird gescrollt");

    s.onPose(Pose::Turned);
    CHECK(s.scrolling(), "das Abdrehen allein scrollt nicht");
    CHECK(!s.pointing(), "in der abgedrehten Haltung laeuft auch der Cursor");

    s.onPose(Pose::Point);
    CHECK(!s.scrolling(), "das Zurueckdrehen beendet das Scrollen nicht");
    CHECK(s.pointing(), "das Zurueckdrehen gibt den Cursor nicht frei");
}

// Genau eine Bedeutung je Feld der Matrix.
static void test_matrixIsComplete() {
    int left = 0, right = 0, scroll = 0, none = 0;
    for (Pose p : {Pose::Point, Pose::Idle, Pose::Turned}) {
        AirMouseState s = inPose(p);
        const Actions a = s.onPinch(false);
        CHECK(actionsConsistent(a), "Pinch meldet beide Tasten zugleich");

        if      (a.click)      left++;
        else if (a.rightClick) right++;
        else                   none++;

        if (s.scrolling()) scroll++;
    }
    CHECK(left == 1,   "nicht genau eine Haltung klickt links");
    CHECK(right == 1,  "nicht genau eine Haltung klickt rechts");
    CHECK(scroll == 1, "nicht genau eine Haltung scrollt");
    CHECK(none == 1,   "nicht genau eine Haltung bleibt wirkungslos");
}

// Kurz nach einer zuegigen Ausdrehung zeigt die FSM-Pose noch Point, obwohl der
// Arm koerperlich schon draussen ist. Ein Pinch in diesem Fenster darf weder
// links noch rechts wirken - die FSM weiss noch gar nicht, dass die Hand dreht.
static void test_pinchSuppressedWhenArmPhysicallyOut() {
    AirMouseState s = turnedOn();
    const Actions a = s.onPinch(true);
    CHECK(!a.click, "Pinch klickt links, obwohl der Arm ausgedreht ist");
    CHECK(!a.rightClick, "Pinch klickt rechts, obwohl die FSM-Pose noch Point ist");
    CHECK(a.hapticPulses == 0, "unterdrueckter Pinch brummt trotzdem");
}

// armOut unterdrueckt NUR den Linksklick. In der abgedrehten Haltung ist der
// Arm ja per Definition draussen - dort waere die Unterdrueckung sinnlos und
// der Rechtsklick nie erreichbar.
static void test_armOutDoesNotBlockTheRightClick() {
    AirMouseState s = inPose(Pose::Turned);
    CHECK(s.onPinch(true).rightClick, "armOut verschluckt den Rechtsklick");
}

// Der Pinch ist reine Ausgabe: er veraendert keine Achse. Sonst haette die
// Klickrate einen Einfluss darauf, was die naechste Geste bedeutet.
static void test_pinchIsStateless() {
    for (Pose p : {Pose::Point, Pose::Idle, Pose::Turned}) {
        AirMouseState s = inPose(p);
        const Actions first = s.onPinch(false);
        for (int i = 0; i < 20; i++) {
            const Actions again = s.onPinch(false);
            CHECK(again.click      == first.click,      "Linksklick haengt an der Vorgeschichte");
            CHECK(again.rightClick == first.rightClick, "Rechtsklick haengt an der Vorgeschichte");
            CHECK(s.pose() == p, "Pinch veraendert die Haltung");
            CHECK(s.on(), "Pinch schaltet ab");
        }
    }
}

// --- Haltungswechsel ---------------------------------------------------

static void test_poseChangeResetsPointer() {
    AirMouseState s = turnedOn();
    CHECK(s.onPose(Pose::Turned).resetPointer, "das Verlassen von Point setzt den Zeiger nicht zurueck");
    CHECK(s.onPose(Pose::Point).resetPointer,  "das Betreten von Point setzt den Zeiger nicht zurueck");
}

static void test_samePoseIsNoOp() {
    AirMouseState s = turnedOn();
    const Actions a = s.onPose(Pose::Point);
    CHECK(a.hapticPulses == 0 && !a.resetPointer, "dieselbe Haltung wirkt trotzdem");
}

// Idle bleibt still, sonst brummt es zweimal auf dem Weg vom Zeigen in die
// abgedrehte Haltung.
static void test_idleIsSilent() {
    AirMouseState s = turnedOn();
    CHECK(s.onPose(Pose::Idle).hapticPulses == 0, "der Weg nach Idle brummt");
}

static void test_powerOnResetsPose() {
    AirMouseState s = turnedOn();
    s.onPose(Pose::Turned);
    s.onPower();                       // aus
    s.onPower();                       // wieder ein
    CHECK(s.pose() == Pose::Point, "das Einschalten setzt die Haltung nicht zurueck");
    CHECK(!s.scrolling(), "nach dem Einschalten wird gescrollt");
}

// --- Erschoepfender Durchlauf -----------------------------------------

static void test_allStatesAllEvents() {
    const Power powers[] = {Power::Off, Power::On};
    const Pose  poses[]  = {Pose::Point, Pose::Idle, Pose::Turned};

    int visited = 0;
    for (Power pw : powers) {
        for (Pose po : poses) {
            for (int ev = 0; ev < 6; ev++) {
                AirMouseState s;
                if (pw == Power::On) { s.onPower(); s.onPose(po); }
                else if (po != Pose::Point) continue;   // aus gibt es nur Point

                Actions a;
                bool armOut = false;
                switch (ev) {
                    case 0: a = s.onPower();            break;
                    case 1: a = s.onPinch(false);       break;
                    case 2: armOut = true;
                            a = s.onPinch(true);        break;
                    case 3: a = s.onPose(Pose::Point);  break;
                    case 4: a = s.onPose(Pose::Idle);   break;
                    case 5: a = s.onPose(Pose::Turned); break;
                }
                visited++;

                CHECK(actionsConsistent(a), "widerspruechliche Aktionen");
                if (!s.on()) {
                    CHECK(!a.click && !a.rightClick, "geklickt, obwohl ausgeschaltet");
                    CHECK(!s.scrolling(), "gescrollt, obwohl ausgeschaltet");
                }
                if (armOut && po == Pose::Point) {
                    CHECK(!a.click, "Pinch klickt links, obwohl der Arm ausgedreht ist");
                }
            }
        }
    }
    // Aus: nur Point -> 1 Gruppe. Ein: 3 Haltungen. Zusammen 4 mal 6 Ereignisse.
    CHECK(visited == 24, "nicht alle Kombinationen durchlaufen");
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

        switch ((rng >> 16) % 6) {
            case 0: a = s.onPower();            break;
            case 1: a = s.onPinch(false);       break;
            case 2: armOut = true;
                    a = s.onPinch(true);        break;
            case 3: a = s.onPose(Pose::Point);  break;
            case 4: a = s.onPose(Pose::Idle);   break;
            case 5: a = s.onPose(Pose::Turned); break;
        }

        if (!actionsConsistent(a)) { CHECK(false, "widerspruechliche Aktionen im Zufallslauf"); return; }

        // Jede Wirkung setzt den eingeschalteten Zustand voraus.
        if ((a.click || a.rightClick) && !s.on()) { CHECK(false, "Wirkung im Ruhezustand"); return; }

        // Und jede Taste die Haltung, die zu ihr gehoert.
        if (a.click      && !s.pointing())            { CHECK(false, "Linksklick ausserhalb der Zeige-Haltung"); return; }
        if (armOut && a.click)                        { CHECK(false, "Linksklick trotz ausgedrehtem Arm"); return; }
        if (a.rightClick && s.pose() != Pose::Turned) { CHECK(false, "Rechtsklick ausserhalb der abgedrehten Haltung"); return; }

        // Scrollen und Zeigen schliessen sich aus - sonst liefe der Cursor
        // waehrend des Scrollens mit.
        if (s.scrolling() && s.pointing())            { CHECK(false, "Zeigen und Scrollen zugleich"); return; }
        if (s.scrolling() && s.pose() != Pose::Turned){ CHECK(false, "gescrollt ausserhalb der abgedrehten Haltung"); return; }
        if (s.scrolling() && !s.on())                 { CHECK(false, "gescrollt im Ruhezustand"); return; }
    }
}

int main() {
    test_startsOff();
    test_offIgnoresEverything();
    test_twistTogglePowersOn();
    test_onlyPowerUsesTheLongPulse();
    test_pinchWhilePointingClicksLeft();
    test_pinchWhileTurnedClicksRight();
    test_pinchInIdleDoesNothing();
    test_turningAloneScrolls();
    test_matrixIsComplete();
    test_pinchSuppressedWhenArmPhysicallyOut();
    test_armOutDoesNotBlockTheRightClick();
    test_pinchIsStateless();
    test_poseChangeResetsPointer();
    test_samePoseIsNoOp();
    test_idleIsSilent();
    test_powerOnResetsPose();
    test_allStatesAllEvents();
    test_randomWalkKeepsInvariants();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
