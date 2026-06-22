#pragma once
#include <Arduino.h>
#include "config.h"

/* ================================================================
 * Haptic  —  Klick-Impuls als kurzer Vibrationsstoß (digital)
 * ----------------------------------------------------------------
 * Hartes An/Aus über digitalWrite. Bewusst KEIN analogWrite/PWM,
 * da der PWM-Hardware-Block unter BLE (SoftDevice) belegt sein
 * kann. Nicht-blockierend: trigger() startet, update() beendet.
 * ================================================================ */
class Haptic {
public:
    void begin() {
        pinMode(cfg::HAPTIC_PIN, OUTPUT);
        digitalWrite(cfg::HAPTIC_PIN, LOW);   // aus, KEIN analogWrite
    }

    // startet einen Vibrationsimpuls
    void trigger(uint32_t now_ms) {
        active_ = true;
        tStart_ = now_ms;
        digitalWrite(cfg::HAPTIC_PIN, HIGH);
    }

    // jeden Loop aufrufen: beendet den Impuls nach HAPTIC_MS
    void update(uint32_t now_ms) {
        if (!active_) return;
        if (now_ms - tStart_ >= cfg::HAPTIC_MS) {
            digitalWrite(cfg::HAPTIC_PIN, LOW);
            active_ = false;
        }
    }

private:
    bool     active_ = false;
    uint32_t tStart_ = 0;
};