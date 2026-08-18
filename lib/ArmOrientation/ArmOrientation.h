#pragma once
#include <math.h>

// Macht aus der Richtung von "oben" (MadgwickAHRS::upX/upY/upZ) zwei Winkel,
// die etwas ueber den Arm aussagen statt ueber die Platine.
//
// Einbaulage, rechter Unterarm, Chip oben auf: +Y entlang des Unterarms und
// damit Achse der Verdrehung, +Z senkrecht auf der Platine (flach az = +1),
// +X quer zum Arm (um 90 Grad verdreht ax = +1). Deshalb gibt es hier kein
// rollDeg()/pitchDeg(): bei dieser Lage waeren die beiden Standardformeln
// gegenueber ihren Namen vertauscht.
//
// Ohne config.h und ohne Arduino.h, damit der PC-Test laeuft. cfg::ELEV_SIGN
// wendet deshalb der Controller an, nicht dieses Modul.
namespace arm {

constexpr float R2D = 57.29578f;

// Verdrehung um die Unterarmachse. atan2 statt asin: der Winkel muss ueber
// 90 Grad hinaus eindeutig bleiben, die Scroll-Haltung liegt bei rund 90.
inline float twistDeg(float ux, float uz) {
    if (ux == 0.f && uz == 0.f) return 0.f;   // Arm senkrecht, Verdrehung undefiniert
    return atan2f(ux, uz) * R2D;
}

// Neigung des Unterarms aus der Waagerechten, positiv wenn die Hand steigt.
// Der ganze Vektor im Nenner haelt den Betrag richtig, auch bei verdrehter
// Hand.
inline float elevDeg(float ux, float uy, float uz) {
    const float n = sqrtf(ux*ux + uy*uy + uz*uz);
    if (n < 1e-4f) return 0.f;
    float s = uy / n;
    if (s >  1.f) s =  1.f;
    if (s < -1.f) s = -1.f;
    return asinf(s) * R2D;
}

// Verdrehung gegenueber der Zeige-Haltung, auf +-180 Grad umgeschlagen - sonst
// waere die Differenz am Sprung eine scheinbare Auslenkung von hunderten Grad.
inline float relDeg(float twistDeg, float neutralDeg) {
    float d = twistDeg - neutralDeg;
    while (d >  180.f) d -= 360.f;
    while (d < -180.f) d += 360.f;
    return d;
}

// Zerlegt die Drehrate in Gieren und Nicken bezogen auf den Raum statt auf die
// Platine, sonst laeuft der Cursor bei verdrehter Hand schraeg. Bei t = 0 faellt
// das auf yaw = -gz und nick = gx zurueck. gy geht nicht ein: das ist die
// Drehung um die Unterarmachse selbst, sie waehlt die Haltung.
inline void rates(float gx, float gz, float twistDeg, float& yaw, float& nick) {
    const float t = twistDeg / R2D;
    const float c = cosf(t), s = sinf(t);
    yaw  = -(gx * s + gz * c);
    nick =   gx * c - gz * s;
}

}  // namespace arm
