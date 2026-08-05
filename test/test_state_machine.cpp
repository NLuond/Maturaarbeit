// Test des Zustandsautomaten. Laeuft auf dem PC, nicht auf dem Chip - der
// Automat haengt bewusst an keiner Hardware.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/AirMouseState -o build/fsm.exe test/test_state_machine.cpp && ./build/fsm.exe
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
    if (a.click && a.rightClick) return false;
    return true;
}

static AirMouseState turnedOn() {
    AirMouseState s;
    s.onShake();
    return s;
}

static AirMouseState inPose(Pose p) {
    AirMouseState s = turnedOn();
    s.onPose(p);
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
    const Actions a1 = s.onPinch();
    const Actions a2 = s.onPose(Pose::Turned);
    const Actions a3 = s.onPose(Pose::Idle);

    CHECK(!a1.click, "ausgeschaltet wird links geklickt");
    CHECK(!a1.rightClick, "ausgeschaltet wird rechts geklickt");
    CHECK(!a2.enterScroll, "ausgeschaltet wird gescrollt");
    CHECK(!a3.haptic, "ausgeschaltet brummt es");
    CHECK(!s.on(), "ausgeschaltet nicht mehr aus");

    // Auch die Haltung darf im Ruhezustand nicht mitwandern, sonst steht der
    // Automat beim Einschalten in einer Haltung, die niemand eingenommen hat.
    CHECK(s.pose() == Pose::Point, "Haltung wandert im Ruhezustand mit");
}

static void test_shakeToggles() {
    AirMouseState s;
    const Actions on = s.onShake();
    CHECK(s.on(), "Schuetteln schaltet nicht ein");
    CHECK(on.resetPose, "Haltungserkennung wird beim Einschalten nicht zurueckgesetzt");
    CHECK(on.resetPointer, "Zeiger wird beim Einschalten nicht genullt");
    CHECK(s.pose() == Pose::Point, "startet nicht in der Zeige-Haltung");

    const Actions off = s.onShake();
    CHECK(!s.on(), "zweites Schuetteln schaltet nicht aus");
    CHECK(off.resetPointer, "Ausschalten nullt den Zeiger nicht");
}

// Beim Einschalten in verdrehter Hand darf nicht die alte Haltung gelten.
static void test_shakeResetsPose() {
    AirMouseState s = inPose(Pose::Turned);
    s.onShake();                       // aus
    const Actions on = s.onShake();    // wieder ein
    CHECK(s.pose() == Pose::Point, "Haltung nach dem Einschalten nicht zurueckgesetzt");
    CHECK(on.resetPose, "Einschalten meldet kein Zuruecksetzen der Erkennung");
    CHECK(s.pointing(), "nach dem Einschalten wird nicht gezeigt");
}

// --- Der Pinch, je nach Haltung ---------------------------------------

static void test_pinchWhilePointingClicksLeft() {
    const Actions a = turnedOn().onPinch();
    CHECK(a.click, "Pinch beim Zeigen klickt nicht links");
    CHECK(!a.rightClick, "Pinch beim Zeigen klickt rechts");
    CHECK(a.haptic, "Linksklick meldet sich nicht haptisch");
}

static void test_pinchWhileTurnedClicksRight() {
    AirMouseState s = inPose(Pose::Idle);
    const Actions a = s.onPinch();
    CHECK(a.rightClick, "Pinch bei gedrehter Hand klickt nicht rechts");
    CHECK(!a.click, "Pinch bei gedrehter Hand klickt links");
    CHECK(a.haptic, "Rechtsklick meldet sich nicht haptisch");
}

static void test_pinchWhileScrollingDoesNothing() {
    AirMouseState s = inPose(Pose::Turned);
    const Actions a = s.onPinch();
    CHECK(!a.click, "Pinch in der Scroll-Haltung klickt links");
    CHECK(!a.rightClick, "Pinch in der Scroll-Haltung klickt rechts");
    CHECK(!a.haptic, "Pinch in der Scroll-Haltung brummt");
}

// Genau eine Haltung je Taste - sonst laesst sich aus dem Cursorverhalten nicht
// mehr ablesen, welche Taste ein Pinch gerade ausloesen wuerde.
static void test_exactlyOneButtonPerPose() {
    int left = 0, right = 0, none = 0;
    for (Pose p : {Pose::Point, Pose::Idle, Pose::Turned}) {
        AirMouseState s = inPose(p);
        const Actions a = s.onPinch();
        CHECK(actionsConsistent(a), "Pinch meldet beide Tasten zugleich");
        if (a.click)           left++;
        else if (a.rightClick) right++;
        else                   none++;
    }
    CHECK(left == 1,  "nicht genau eine Haltung klickt links");
    CHECK(right == 1, "nicht genau eine Haltung klickt rechts");
    CHECK(none == 1,  "nicht genau eine Haltung bleibt wirkungslos");
}

// Der Pinch veraendert den Zustand nicht - er ist reine Ausgabe. Sonst haette
// die Klickrate einen Einfluss darauf, was die naechste Geste bedeutet.
static void test_pinchIsStateless() {
    for (Pose p : {Pose::Point, Pose::Idle, Pose::Turned}) {
        AirMouseState s = inPose(p);
        const Actions first = s.onPinch();
        for (int i = 0; i < 20; i++) {
            const Actions again = s.onPinch();
            CHECK(again.click      == first.click,      "Linksklick haengt an der Vorgeschichte");
            CHECK(again.rightClick == first.rightClick, "Rechtsklick haengt an der Vorgeschichte");
            CHECK(s.pose() == p, "Pinch veraendert die Haltung");
            CHECK(s.on(), "Pinch schaltet ab");
        }
    }
}

// --- Haltungswechsel ---------------------------------------------------

static void test_samePoseIsNoOp() {
    for (Pose p : {Pose::Point, Pose::Idle, Pose::Turned}) {
        AirMouseState s = inPose(p);
        const Actions a = s.onPose(p);
        CHECK(!a.haptic && !a.resetPointer && !a.enterScroll,
              "unveraenderte Haltung loest Aktionen aus");
    }
}

static void test_scrollEntryResetsJoystick() {
    for (Pose from : {Pose::Point, Pose::Idle}) {
        AirMouseState s = inPose(from);
        const Actions a = s.onPose(Pose::Turned);
        CHECK(a.enterScroll, "Scroll-Haltung nullt den Joystick nicht");
        CHECK(s.scrolling(), "Scroll-Haltung kommt nicht an");
    }
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

// Auf dem Weg vom Zeigen in die Scroll-Haltung liegt Idle. Es darf dabei nicht
// zusaetzlich brummen, sonst kommen zwei Impulse fuer eine Bewegung.
static void test_idleIsSilent() {
    AirMouseState s = turnedOn();
    const Actions a = s.onPose(Pose::Idle);
    CHECK(!a.haptic, "der Weg nach Idle brummt");
    CHECK(!a.enterScroll, "Idle startet den Scroll-Joystick");
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
            for (int ev = 0; ev < 5; ev++) {
                AirMouseState s;
                if (pw == Power::On) { s.onShake(); s.onPose(po); }
                else if (po != Pose::Point) continue;   // aus gibt es nur Point

                Actions a;
                switch (ev) {
                    case 0: a = s.onShake();            break;
                    case 1: a = s.onPinch();            break;
                    case 2: a = s.onPose(Pose::Point);  break;
                    case 3: a = s.onPose(Pose::Idle);   break;
                    case 4: a = s.onPose(Pose::Turned); break;
                }
                visited++;

                CHECK(actionsConsistent(a), "widerspruechliche Aktionen");
                if (!s.on()) {
                    CHECK(!a.click && !a.rightClick, "geklickt, obwohl ausgeschaltet");
                    CHECK(!a.enterScroll, "gescrollt, obwohl ausgeschaltet");
                }
            }
        }
    }
    // Aus: nur Point -> 1 Gruppe. Ein: 3 Haltungen. Zusammen 4 mal 5 Ereignisse.
    CHECK(visited == 20, "nicht alle Kombinationen durchlaufen");
}

// Zufaellige, lange Ereignisfolge - findet Reihenfolgen, an die man beim
// Schreiben der Einzeltests nicht denkt.
static void test_randomWalkKeepsInvariants() {
    AirMouseState s;
    uint32_t rng = 12345;
    for (int i = 0; i < 20000; i++) {
        rng = rng * 1664525u + 1013904223u;
        Actions a;
        switch ((rng >> 16) % 5) {
            case 0: a = s.onShake();            break;
            case 1: a = s.onPinch();            break;
            case 2: a = s.onPose(Pose::Point);  break;
            case 3: a = s.onPose(Pose::Idle);   break;
            case 4: a = s.onPose(Pose::Turned); break;
        }
        if (!actionsConsistent(a)) { CHECK(false, "widerspruechliche Aktionen im Zufallslauf"); return; }

        // Jede Wirkung setzt den eingeschalteten Zustand voraus.
        if (a.click || a.rightClick || a.enterScroll) {
            if (!s.on()) { CHECK(false, "Wirkung im Ruhezustand"); return; }
        }
        // Und jede Taste die Haltung, die zu ihr gehoert.
        if (a.click      && !s.pointing()) { CHECK(false, "Linksklick ausserhalb der Zeige-Haltung"); return; }
        if (a.rightClick && s.pose() != Pose::Idle) {
            CHECK(false, "Rechtsklick ausserhalb der gedrehten Haltung"); return;
        }
    }
}

int main() {
    test_startsOff();
    test_offIgnoresEverything();
    test_shakeToggles();
    test_shakeResetsPose();
    test_pinchWhilePointingClicksLeft();
    test_pinchWhileTurnedClicksRight();
    test_pinchWhileScrollingDoesNothing();
    test_exactlyOneButtonPerPose();
    test_pinchIsStateless();
    test_samePoseIsNoOp();
    test_scrollEntryResetsJoystick();
    test_leavingPointResetsPointer();
    test_enteringPointResetsPointer();
    test_idleIsSilent();
    test_allStatesAllEvents();
    test_randomWalkKeepsInvariants();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
