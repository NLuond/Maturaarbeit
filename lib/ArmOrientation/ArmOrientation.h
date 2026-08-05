#pragma once
#include <math.h>

// Macht aus der Richtung von "oben" (MadgwickAHRS::upX/upY/upZ) zwei Winkel,
// die etwas ueber den Arm aussagen statt ueber die Platine.
//
// Einbaulage, am Geraet abgelesen (rechter Unterarm, Chip oben auf, USB-Buchse
// quer nach aussen):
//
//   +Z steht senkrecht auf der Platine und zeigt bei waagrechtem Arm nach oben.
//      Flach auf dem Tisch liest der Sensor az = +1.
//   +X zeigt quer zum Arm. Um 90 Grad um die Unterarmachse verdreht liest der
//      Sensor ax = +1 - X kippt also nach oben.
//   +Y ergibt sich als Z x X und laeuft damit entlang des Unterarms. Darum
//      dreht sich die Verdrehung, und darum liest ay die Armneigung.
//
// Daraus:
//
//   twist - Verdrehung um die Unterarmachse. 0 = Platine waagrecht,
//           +90 = um 90 Grad gekippt (ax = +1). Waehlt die Haltung.
//   elev  - Neigung des Unterarms aus der Waagerechten. 0 = waagrecht.
//           Gate fuer "nur waagrecht" und Eingang des Scroll-Joysticks.
//
// Warum nicht MadgwickAHRS::rollDeg()/pitchDeg(): ausmultipliziert ist
// rollDeg() = atan2(uy, uz) und pitchDeg() = asin(-ux). Bei dieser Einbaulage
// ist das erste die Armneigung und das zweite die Verdrehung - also genau
// vertauscht gegenueber dem, was die Namen nahelegen. Wer die Winkel hier
// abholt, kann sich nicht mehr vertun.
//
// Header bewusst ohne Arduino.h und ohne config.h: so laeuft der Test dazu
// (test/test_arm_orientation.cpp) auf dem PC statt auf dem Chip.
namespace arm {

constexpr float R2D = 57.29578f;

// Verdrehung um die Unterarmachse (Y). atan2 statt asin, weil der Winkel ueber
// 90 Grad hinaus eindeutig bleiben muss - die Scroll-Haltung liegt bei rund 90.
inline float twistDeg(float ux, float uz) {
    if (ux == 0.f && uz == 0.f) return 0.f;   // Arm senkrecht, Verdrehung undefiniert
    return atan2f(ux, uz) * R2D;
}

// Neigung des Unterarms aus der Waagerechten, positiv wenn die Hand steigt.
//
// Herleitung des Vorzeichens: hebt sich die Achse +Y um den Winkel theta zur
// Senkrechten hin, dann bekommt "oben" in Koerperkoordinaten die Komponente
// uy = sin(theta). Also elev = asin(uy), ohne Minuszeichen.
//
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

// Zerlegt die Drehrate in Gieren (waagerechte Cursorbewegung) und Nicken
// (senkrechte) - bezogen auf den Raum statt auf die Platine.
//
// Ist die Hand um twist verdreht, dreht sich das Koerperkoordinatensystem mit:
// eine rein waagerechte Handbewegung erzeugt dann nicht mehr nur gz, sondern
// auch gx, und der Cursor laeuft schraeg. Bei 30 Grad Verdrehung sind das schon
// sin(30) = 50 Prozent Ueberkopplung - und so viel streut die Verdrehung im
// normalen Gebrauch.
//
// "Oben" hat in Koerperkoordinaten die Richtung (sin t, 0, cos t); die Querachse
// steht senkrecht darauf und auf der Unterarmachse: (-cos t, 0, sin t). Die
// Projektion der Drehrate auf diese beiden Achsen ergibt Gier- und Nickanteil.
// Bei t = 0 faellt das auf yaw = -gz und nick = gx zurueck, also genau auf die
// unkompensierte Zuordnung - die Kompensation kann in der Grundhaltung also
// nichts verschlechtern.
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
