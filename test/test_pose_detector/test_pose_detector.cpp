// Test der Haltungserkennung. Laeuft auf dem PC - PoseDetector haengt seit dem
// Umbau auf PoseTuning an keiner Hardware.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/PoseDetector -I lib/AirMouseState -o "$env:TEMP\pose.exe" test/test_pose_detector/test_pose_detector.cpp
//   & "$env:TEMP\pose.exe"
//
#include "PoseDetector.h"
#include <cstdio>
#include <cmath>

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

static const PoseTuning kT{};

static const float    kDt = 0.005f;   // s je Takt
static const uint32_t kMs = 5;        // dieselbe Zeit in ms

// Die Geste ist rund 90 Grad. Die Glaettung erreicht ihren Eingang nur
// asymptotisch - mit einem Eingang von exakt turnOnDeg wuerde die Schwelle nie
// ueberschritten. Deshalb wird hier mit der koerperlich echten Auslenkung
// gearbeitet und nicht mit der Schwelle selbst.
static const float kTurned = 90.f;
static const float kSteep  = 60.f;    // deutlich ausserhalb des Waagrecht-Gates

static Pose run(PoseDetector& p, float twist, float elev, float gyro,
                uint32_t& now, uint32_t ms) {
    Pose last = p.pose();
    for (uint32_t i = 0; i < ms; i += kMs) {
        last = p.update(twist, elev, gyro, kDt, now);
        now += kMs;
    }
    return last;
}

// --- Grundzustand -------------------------------------------------------

static void test_startsInPoint() {
    PoseDetector p;
    CHECK(p.pose() == Pose::Point, "startet nicht in der Zeige-Haltung");
    CHECK(p.level(), "startet ausserhalb des Waagrecht-Gates");
}

static void test_staysPointWhenHeldStraightAndLevel() {
    PoseDetector p;
    uint32_t now = 1000;
    CHECK(run(p, 0.f, 0.f, 0.f, now, 3000) == Pose::Point,
          "die Zeige-Haltung haelt bei ruhiger, gerader Hand nicht");
}

// --- Das Waagrecht-Gate -------------------------------------------------

// Idle ist ausschliesslich das Ergebnis des Gates. Genau das war der Fehler in
// der alten Beschreibung: Idle war einmal auch eine Zone der Verdrehung.
static void test_gateForcesIdle() {
    PoseDetector p;
    uint32_t now = 1000;
    CHECK(run(p, 0.f, kSteep, 0.f, now, 2000) == Pose::Idle,
          "ein angehobener Arm liefert nicht Idle");
    CHECK(!p.level(), "level() meldet den angehobenen Arm nicht");

    CHECK(run(p, 0.f, -kSteep, 0.f, now, 2000) == Pose::Idle,
          "ein haengender Arm liefert nicht Idle - das Gate rechnet mit Vorzeichen");
}

// Das Gate hat Vorrang. Abgedreht UND nicht waagrecht ist Idle, nicht Turned -
// sonst wuerde ein haengender Arm den Rechtsklick tragen, und die Verdrehung
// ist bei senkrechtem Unterarm ohnehin nicht beobachtbar.
static void test_gateBeatsTwist() {
    PoseDetector p;
    uint32_t now = 1000;
    CHECK(run(p, kTurned, kSteep, 0.f, now, 2000) == Pose::Idle,
          "abgedreht bei nicht waagrechtem Arm liefert nicht Idle");
}

static void test_gateHasHysteresis() {
    PoseDetector p;
    uint32_t now = 1000;
    run(p, 0.f, kSteep, 0.f, now, 2000);            // Gate offen -> zu
    CHECK(!p.level(), "das Gate hat gar nicht erst geschlossen");

    // Zwischen levelMaxDeg - levelHystDeg (27) und levelMaxDeg (35) darf sich
    // nichts ruehren: das ist das Hysteresefenster.
    const float inBand = kT.levelMaxDeg - 0.5f * kT.levelHystDeg;
    run(p, 0.f, inBand, 0.f, now, 2000);
    CHECK(!p.level(), "das Gate oeffnet schon im Hysteresefenster");

    run(p, 0.f, 0.f, 0.f, now, 2000);
    CHECK(p.level(), "das Gate oeffnet nach der Rueckkehr in die Waagerechte nicht");
}

// --- Die Verdrehachse ---------------------------------------------------

static void test_twistSelectsTurned() {
    PoseDetector p;
    uint32_t now = 1000;
    CHECK(run(p, kTurned, 0.f, 0.f, now, 2000) == Pose::Turned,
          "die abgedrehte Haltung wird nicht erkannt");
}

// Der Betrag entscheidet, nicht das Vorzeichen: aus der Zeige-Haltung heraus
// ist die Schwelle anatomisch ohnehin nur in einer Richtung erreichbar, und
// welche das ist, muss der Code nicht wissen.
static void test_twistUsesMagnitude() {
    PoseDetector p;
    uint32_t now = 1000;
    CHECK(run(p, -kTurned, 0.f, 0.f, now, 2000) == Pose::Turned,
          "die Gegenrichtung wird nicht als abgedreht erkannt");
}

static void test_twistHasHysteresis() {
    PoseDetector p;
    uint32_t now = 1000;
    run(p, kTurned, 0.f, 0.f, now, 2000);
    CHECK(p.pose() == Pose::Turned, "gar nicht erst abgedreht");

    // Zwischen turnOffDeg (55) und turnOnDeg (70) bleibt die Haltung stehen.
    const float inBand = 0.5f * (kT.turnOffDeg + kT.turnOnDeg);
    CHECK(run(p, inBand, 0.f, 0.f, now, 2000) == Pose::Turned,
          "halbes Zurueckdrehen faellt schon aus der abgedrehten Haltung");

    CHECK(run(p, 0.f, 0.f, 0.f, now, 2000) == Pose::Point,
          "ganzes Zurueckdrehen kehrt nicht in die Zeige-Haltung zurueck");
}

// --- Haltezeit und Bewegungssperre -------------------------------------

// Ein Wechsel zaehlt erst, wenn er stabil anliegt. Geprueft wird beides: dass
// er kurz vorher noch nicht gilt und kurz danach schon.
static void test_dwellTimeDelaysTheChange() {
    PoseDetector p;
    uint32_t now = 1000;
    run(p, 0.f, 0.f, 0.f, now, 2000);               // eingeschwungen in Point

    // Erst so lange fahren, bis der geglaettete Winkel sicher ueber der
    // Schwelle liegt, aber ohne die Haltezeit abzuwarten. Dass die Haltezeit
    // ueberhaupt wirkt, zeigt der Vergleich der beiden Zeitpunkte unten.
    PoseDetector q;
    uint32_t nowQ = 1000;
    run(q, 0.f, 0.f, 0.f, nowQ, 2000);
    const Pose early = run(q, kTurned, 0.f, 0.f, nowQ, kT.dwellMs - 5);
    CHECK(early == Pose::Point, "der Wechsel gilt schon vor Ablauf der Haltezeit");
    CHECK(run(q, kTurned, 0.f, 0.f, nowQ, 2 * kT.dwellMs) == Pose::Turned,
          "der Wechsel gilt auch nach der Haltezeit nicht");
}

// Waehrend einer heftigen Bewegung wird gar nicht erst entschieden. Ohne das
// reisst schon eine zuegige 90-Grad-Drehung die Erkennung durch Idle bis
// Turned, samt Haptik und Zeiger-Reset.
static void test_motionFreezesTheDecision() {
    PoseDetector p;
    uint32_t now = 1000;
    run(p, 0.f, 0.f, 0.f, now, 2000);
    CHECK(run(p, kTurned, 0.f, kT.stillDps + 100.f, now, 3000) == Pose::Point,
          "die Haltung wechselt mitten in einer heftigen Bewegung");
}

// Die Winkel laufen waehrend der Sperre weiter - nur die ENTSCHEIDUNG ruht.
// Daran haengt die Ein/Aus-Drehgeste: TwistToggle liest relTwistDeg() direkt,
// und die Drehung selbst erzeugt rund 300 Grad/s.
static void test_anglesKeepRunningWhileFrozen() {
    PoseDetector p;
    uint32_t now = 1000;
    run(p, kTurned, 0.f, kT.stillDps + 100.f, now, 2000);
    CHECK(p.pose() == Pose::Point, "die Entscheidung ist trotz Bewegung gefallen");
    CHECK(p.relTwistDeg() > kT.turnOnDeg,
          "der Winkel steht waehrend der Bewegungssperre still");
}

// Nach der Bewegung muss zusaetzlich calmMs verstreichen, bevor wieder
// entschieden wird.
static void test_calmTimeAfterMotion() {
    PoseDetector p;
    uint32_t now = 1000;
    run(p, kTurned, 0.f, kT.stillDps + 100.f, now, 2000);
    CHECK(run(p, kTurned, 0.f, 0.f, now, kT.calmMs - 20) == Pose::Point,
          "es wird schon innerhalb der Ruhezeit wieder entschieden");
    CHECK(run(p, kTurned, 0.f, 0.f, now, kT.calmMs + 2 * kT.dwellMs) == Pose::Turned,
          "nach der Ruhezeit wird gar nicht mehr entschieden");
}

// --- Die zwei Glaettungen ----------------------------------------------

// Der Zeiger bekommt eine langsamer geglaettete Fassung desselben Winkels: er
// steht dort in einer Drehmatrix, deren Rauschen unmittelbar als Zittern im
// Cursor landet. Die schnelle Fassung waere an dieser Stelle ein Rueckschritt.
static void test_slowTwistLagsBehindTheFastOne() {
    PoseDetector p;
    uint32_t now = 1000;
    run(p, kTurned, 0.f, 0.f, now, 150);
    CHECK(p.relTwistDeg() > p.relTwistSlowDeg(),
          "die langsame Glaettung hinkt der schnellen nicht hinterher");
    CHECK(p.relTwistSlowDeg() > 0.f, "die langsame Glaettung laeuft gar nicht mit");

    // Auf lange Sicht laufen beide auf denselben Wert zu.
    run(p, kTurned, 0.f, 0.f, now, 4000);
    CHECK(std::fabs(p.relTwistDeg() - p.relTwistSlowDeg()) < 1.f,
          "die beiden Glaettungen laufen dauerhaft auseinander");
}

// --- reset() und der abgeschaltete Modus -------------------------------

static void test_resetReturnsToPoint() {
    PoseDetector p;
    uint32_t now = 1000;
    run(p, kTurned, 0.f, 0.f, now, 2000);
    CHECK(p.pose() == Pose::Turned, "gar nicht erst abgedreht");
    p.reset();
    CHECK(p.pose() == Pose::Point, "reset() setzt die Haltung nicht zurueck");
}

// reset() setzt bewusst NUR die Haltung zurueck. Der Nullpunkt der Verdrehung
// ist fest - frueher wurde er hier auf die aktuelle Lage kalibriert, und zwar
// im unruhigsten Moment ueberhaupt, direkt nach der Einschaltgeste.
static void test_resetDoesNotRecalibrateTheZero() {
    PoseDetector p;
    uint32_t now = 1000;
    run(p, kTurned, 0.f, 0.f, now, 2000);
    const float before = p.relTwistDeg();
    p.reset();
    CHECK(std::fabs(p.relTwistDeg() - before) < 0.001f,
          "reset() kalibriert den Nullpunkt der Verdrehung neu");
}

// USE_POSE_MODE aus heisst: die KLASSIFIKATION steht still, nicht die
// Winkelberechnung. Die Ein/Aus-Drehgeste liest relTwistDeg() und level()
// direkt und muss auch dann noch funktionieren.
static void test_classifyOffStillTracksAngles() {
    PoseTuning t;
    t.classify = false;
    PoseDetector p(t);
    uint32_t now = 1000;

    CHECK(run(p, kTurned, kSteep, 0.f, now, 3000) == Pose::Point,
          "meldet bei abgeschalteter Klassifikation nicht dauerhaft Point");
    CHECK(p.relTwistDeg() > kT.turnOnDeg,
          "der Verdrehungswinkel laeuft bei abgeschalteter Klassifikation nicht mit");
    CHECK(!p.level(),
          "das Waagrecht-Gate steht bei abgeschalteter Klassifikation still");
}

int main() {
    test_startsInPoint();
    test_staysPointWhenHeldStraightAndLevel();
    test_gateForcesIdle();
    test_gateBeatsTwist();
    test_gateHasHysteresis();
    test_twistSelectsTurned();
    test_twistUsesMagnitude();
    test_twistHasHysteresis();
    test_dwellTimeDelaysTheChange();
    test_motionFreezesTheDecision();
    test_anglesKeepRunningWhileFrozen();
    test_calmTimeAfterMotion();
    test_slowTwistLagsBehindTheFastOne();
    test_resetReturnsToPoint();
    test_resetDoesNotRecalibrateTheZero();
    test_classifyOffStillTracksAngles();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
