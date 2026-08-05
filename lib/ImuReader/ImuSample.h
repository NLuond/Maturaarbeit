#pragma once

// Grenzstruktur zwischen Treiber und Verarbeitung. Bewusst ohne Sensor-Typen
// und ohne Treiber-Header: wer nur die Messwerte braucht, soll nicht das
// halbe LSM6DS3-Interface mitkompilieren muessen.
struct ImuSample {
    float ax, ay, az, gx, gy, gz;
    float accMag;
    float gyroSum;
};
