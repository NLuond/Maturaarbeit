#pragma once

// Grenzstruktur zwischen Treiber und Verarbeitung. Bewusst ohne Sensor-Typen:
// wer nur die Messwerte braucht, soll nicht das halbe LSM6DS3-Interface
// mitkompilieren muessen.
struct ImuSample {
    float ax, ay, az;      // roh, mit Erdbeschleunigung -> Madgwick

    // Linear, Erdbeschleunigung abgezogen -> ML-Fenster. Die rohen Achsen
    // tragen die Handhaltung als Gleichanteil mit sich; ohne ihn ist die
    // Haltung fuer das Modell unsichtbar und ein Datensatz deckt beide ab.
    float lax, lay, laz;

    float gx, gy, gz;      // Drehraten, Nullpunkt korrigiert

    float accMag;          // Betrag der ROHEN Beschleunigung -> Huellkurve
    float gyroSum;         // |gx| + |gy| + |gz|, Mass fuer "wie bewegt"
};
