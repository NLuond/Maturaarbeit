#pragma once

// Grenzstruktur zwischen Treiber und Verarbeitung, bewusst ohne Sensor-Typen:
// wer nur die Messwerte braucht, soll nicht das halbe LSM6DS3-Interface
// mitkompilieren.
struct ImuSample {
    float ax, ay, az;      // roh, mit Erdbeschleunigung -> Madgwick

    // Linear, Erdbeschleunigung abgezogen -> ML-Fenster. Ohne den Gleichanteil
    // ist die Handhaltung fuer das Modell unsichtbar.
    float lax, lay, laz;

    float gx, gy, gz;      // Drehraten, Nullpunkt korrigiert

    float accMag;          // Betrag der rohen Beschleunigung -> Huellkurve
    float gyroSum;         // |gx| + |gy| + |gz|, Mass fuer "wie bewegt"
};
