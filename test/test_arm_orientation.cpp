// Test der Winkel-Ableitung aus der Lage. Laeuft auf dem PC - ArmOrientation.h
// haengt bewusst weder an Arduino.h noch an config.h.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/ArmOrientation -o build/arm.exe test/test_arm_orientation.cpp && ./build/arm.exe
//
// Geprueft wird gegen die am Geraet abgelesene Einbaulage:
//   flach auf dem Tisch  -> az = +1, twist = 0, elev = 0
//   um 90 Grad verdreht  -> ax = +1, twist = +90
//
#include "ArmOrientation.h"
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

// 0.05 Grad Toleranz: die Konstante R2D ist auf sieben Stellen gerundet, ein
// exakter Vergleich wuerde daran scheitern und nicht an der Rechnung.
static void near(float got, float want, const char* msg) {
    checks++;
    if (std::fabs(got - want) > 0.05f) {
        failures++;
        std::printf("  FEHLER: %s (erhalten %.3f, erwartet %.3f)\n", msg, got, want);
    }
}

static const float D2R = 3.14159265f / 180.f;

// "Oben" in Koerperkoordinaten fuer eine Lage aus Verdrehung und Armneigung.
// Die Kugelparametrisierung entspricht: erst um die Unterarmachse verdrehen,
// dann den Arm heben.
static void up(float twist, float elev, float& ux, float& uy, float& uz) {
    const float t = twist * D2R, e = elev * D2R;
    ux = std::sin(t) * std::cos(e);
    uy = std::sin(e);
    uz = std::cos(t) * std::cos(e);
}

int main() {
    float ux, uy, uz;

    // --- Die drei am Geraet abgelesenen Bezugslagen --------------------
    near(arm::twistDeg(0.f, 1.f),        0.f,  "flach auf dem Tisch: twist");
    near(arm::elevDeg(0.f, 0.f, 1.f),    0.f,  "flach auf dem Tisch: elev");
    near(arm::twistDeg(1.f, 0.f),       90.f,  "ax = +1: twist");
    near(arm::elevDeg(1.f, 0.f, 0.f),    0.f,  "ax = +1: elev bleibt null");
    near(arm::twistDeg(-1.f, 0.f),     -90.f,  "ax = -1: twist andersherum");

    // --- Verdrehung ueber den ganzen Bereich ---------------------------
    // atan2 statt asin, damit die Scroll-Haltung bei rund 90 Grad eindeutig
    // bleibt und darueber hinaus weiterzaehlt statt zurueckzulaufen.
    for (int d = -170; d <= 170; d += 10) {
        up((float)d, 0.f, ux, uy, uz);
        near(arm::twistDeg(ux, uz), (float)d, "reine Verdrehung");
        near(arm::elevDeg(ux, uy, uz), 0.f,   "reine Verdrehung laesst elev bei null");
    }

    // --- Armneigung ----------------------------------------------------
    // Positiv, wenn die Hand steigt - daran haengt die Waagrecht-Bedingung.
    for (int d = -80; d <= 80; d += 10) {
        up(0.f, (float)d, ux, uy, uz);
        near(arm::elevDeg(ux, uy, uz), (float)d, "reine Armneigung");
        near(arm::twistDeg(ux, uz),     0.f,     "reine Armneigung laesst twist bei null");
    }

    // --- Beides zugleich -----------------------------------------------
    // Der eigentliche Zweck der Trennung: der Scroll-Joystick liest die
    // Armneigung, waehrend die Hand um 90 Grad verdreht gehalten wird. Wuerden
    // sich die Winkel mischen, waere genau dort die Steuerung unbrauchbar.
    for (int t = -120; t <= 120; t += 30) {
        for (int e = -60; e <= 60; e += 15) {
            up((float)t, (float)e, ux, uy, uz);
            near(arm::twistDeg(ux, uz),     (float)t, "twist bleibt bei geneigtem Arm");
            near(arm::elevDeg(ux, uy, uz),  (float)e, "elev bleibt bei verdrehter Hand");
        }
    }

    // --- Betrag des Eingangs darf egal sein ----------------------------
    // Madgwick normiert die Quaternion, nicht zwingend den daraus gebildeten
    // Vektor. elev teilt deshalb selbst durch den Betrag.
    up(35.f, 25.f, ux, uy, uz);
    near(arm::elevDeg(ux*3.7f, uy*3.7f, uz*3.7f), 25.f, "elev unabhaengig vom Betrag");
    near(arm::twistDeg(ux*3.7f, uz*3.7f),         35.f, "twist unabhaengig vom Betrag");

    // --- Grenzfaelle ---------------------------------------------------
    near(arm::elevDeg(0.f, 1.f, 0.f),   90.f, "Arm senkrecht nach oben");
    near(arm::elevDeg(0.f, -1.f, 0.f), -90.f, "Arm senkrecht nach unten");
    // Bei senkrechtem Arm ist die Verdrehung nicht bestimmbar. Wichtig ist nur,
    // dass kein NaN herauskommt - das wuerde den Vergleich in PoseDetector
    // stillschweigend immer falsch machen.
    CHECK(!std::isnan(arm::twistDeg(0.f, 0.f)), "twist bei senkrechtem Arm ist kein NaN");
    near(arm::twistDeg(0.f, 0.f), 0.f, "twist bei senkrechtem Arm faellt auf null");
    CHECK(!std::isnan(arm::elevDeg(0.f, 0.f, 0.f)), "elev ohne Signal ist kein NaN");
    near(arm::elevDeg(0.f, 0.f, 0.f), 0.f, "elev ohne Signal faellt auf null");

    // asin darf nicht ueber den Definitionsbereich laufen, wenn der Vektor
    // durch Rundung minimal zu lang ist.
    CHECK(!std::isnan(arm::elevDeg(0.f, 1.0001f, 0.f)), "elev bei uebersteuertem Eingang");

    // --- Ueberschlag bei 180 Grad --------------------------------------
    near(std::fabs(arm::twistDeg(0.f, -1.f)), 180.f, "Platine auf dem Kopf");

    // --- Roll-Kompensation ---------------------------------------------
    float yaw, nick;

    // Bei nicht verdrehter Hand muss exakt die alte Zuordnung herauskommen.
    // Das ist die wichtigste Eigenschaft: in der Grundhaltung darf die
    // Kompensation nichts veraendern.
    for (int g = -200; g <= 200; g += 50) {
        arm::rates((float)g, 0.f, 0.f, yaw, nick);
        near(yaw,  0.f,       "twist 0: gx erzeugt kein Gieren");
        near(nick, (float)g,  "twist 0: nick = gx");
        arm::rates(0.f, (float)g, 0.f, yaw, nick);
        near(yaw,  (float)-g, "twist 0: yaw = -gz");
        near(nick, 0.f,       "twist 0: gz erzeugt kein Nicken");
    }

    // Bei 90 Grad Verdrehung sind die Achsen vertauscht: was vorher gz war,
    // wirkt jetzt als Nicken. Genau diese Ueberkopplung liess den Cursor bei
    // verdrehter Hand schraeg laufen.
    arm::rates(0.f, 100.f, 90.f, yaw, nick);
    near(yaw,    0.f, "twist 90: gz giert nicht mehr");
    near(nick, -100.f, "twist 90: gz wirkt als Nicken");
    arm::rates(100.f, 0.f, 90.f, yaw, nick);
    near(yaw,  -100.f, "twist 90: gx giert");
    near(nick,    0.f, "twist 90: gx nickt nicht mehr");

    // Der Betrag der Bewegung darf sich durch die Verdrehung nicht aendern -
    // es ist eine Drehung, keine Skalierung. Sonst haenge die Empfindlichkeit
    // an der Handhaltung.
    for (int t = -180; t <= 180; t += 15) {
        arm::rates(60.f, -80.f, (float)t, yaw, nick);
        near(std::sqrt(yaw*yaw + nick*nick), 100.f, "Betrag bleibt erhalten");
    }

    // Eine reine Verdrehung um die Unterarmachse darf den Cursor nicht bewegen.
    // gy geht in rates() gar nicht ein - hier festgehalten, damit es nicht
    // versehentlich wieder hineinwandert.
    arm::rates(0.f, 0.f, 40.f, yaw, nick);
    near(yaw,  0.f, "reine Verdrehung giert nicht");
    near(nick, 0.f, "reine Verdrehung nickt nicht");

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
