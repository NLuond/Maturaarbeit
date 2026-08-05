#pragma once

// Grenzstruktur zwischen Treiber und Verarbeitung. Bewusst ohne Sensor-Typen
// und ohne Treiber-Header: wer nur die Messwerte braucht, soll nicht das
// halbe LSM6DS3-Interface mitkompilieren muessen.
struct ImuSample {
    // Roh, mit Erdbeschleunigung. Fuer Madgwick - ihm ist die
    // Erdbeschleunigung das Signal, aus dem er die Lage schaetzt.
    float ax, ay, az;

    // Linear: Erdbeschleunigung abgezogen. Fuer das ML-Fenster. Die rohen
    // Achsen tragen die Handhaltung als Gleichanteil mit sich - in der
    // Zeige-Haltung liegt die Erdbeschleunigung auf az, in der um 90 Grad
    // abgedrehten auf ax. Ein Modell, das mit den rohen Achsen trainiert
    // wurde, erkennt denselben Pinch in der anderen Haltung deshalb nicht
    // wieder. Ohne den Gleichanteil ist die Haltung fuer das Modell
    // unsichtbar, und ein Datensatz deckt beide ab.
    float lax, lay, laz;

    float gx, gy, gz;

    // Betrag der ROHEN Beschleunigung. Die Huellkurve arbeitet mit einem
    // Hochpass bei 30 Hz und entfernt den Gleichanteil selbst.
    float accMag;
    float gyroSum;
};
