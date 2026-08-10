#pragma once
#include <math.h>

// Macht aus der Richtung von "oben" (MadgwickAHRS::upX/upY/upZ) zwei Winkel,
// die etwas ueber den Arm aussagen statt ueber die Platine.
//
// Einbaulage (rechter Unterarm, Chip oben auf, USB-Buchse quer nach aussen):
//
//   +Z senkrecht auf der Platine, bei waagrechtem Arm nach oben (flach az = +1)
//   +X quer zum Arm (um 90 Grad verdreht ax = +1)
//   +Y entlang des Unterarms, Achse der Verdrehung
//
// MadgwickAHRS bietet bewusst kein rollDeg()/pitchDeg(): ausmultipliziert waere
// das erste bei dieser Einbaulage die Armneigung und das zweite die
// Handverdrehung - genau vertauscht gegenueber dem, was die Namen nahelegen.
//
// Ohne config.h und ohne Arduino.h, damit der PC-Test laeuft. cfg::ELEV_SIGN
// wendet deshalb der Controller an, nicht dieses Modul.
namespace arm {

constexpr float R2D = 57.29578f;

// Verdrehung um die Unterarmachse. atan2 statt asin, weil der Winkel ueber
// 90 Grad hinaus eindeutig bleiben muss - die Scroll-Haltung liegt bei rund 90.
inline float twistDeg(float ux, float uz) {
    if (ux == 0.f && uz == 0.f) return 0.f;   // Arm senkrecht, Verdrehung undefiniert
    return atan2f(ux, uz) * R2D;
}

// Neigung des Unterarms aus der Waagerechten, positiv wenn die Hand steigt.
// Der ganze Vektor im Nenner und nicht nur uz: so bleibt der Betrag richtig,
// egal wie stark die Hand dabei zusaetzlich verdreht ist.
inline float elevDeg(float ux, float uy, float uz) {
    const float n = sqrtf(ux*ux + uy*uy + uz*uz);
    if (n < 1e-4f) return 0.f;
    float s = uy / n;
    if (s >  1.f) s =  1.f;
    if (s < -1.f) s = -1.f;
    return asinf(s) * R2D;
}

// Zerlegt die Drehrate in Gieren und Nicken bezogen auf den Raum statt auf die
// Platine: bei verdrehter Hand erzeugt eine waagerechte Handbewegung sonst
// nicht nur gz, sondern auch gx, und der Cursor laeuft schraeg.
//
// "Oben" hat in Koerperkoordinaten die Richtung (sin t, 0, cos t), die Querachse
// steht senkrecht darauf und auf der Unterarmachse: (-cos t, 0, sin t). Bei
// t = 0 faellt das auf yaw = -gz und nick = gx zurueck.
//
// gy geht nicht ein: das ist die Drehung um die Unterarmachse selbst, die waehlt
// die Haltung und soll den Cursor nicht bewegen.
inline void rates(float gx, float gz, float twistDeg, float& yaw, float& nick) {
    const float t = twistDeg / R2D;
    const float c = cosf(t), s = sinf(t);
    yaw  = -(gx * s + gz * c);
    nick =   gx * c - gz * s;
}

}  // namespace arm
