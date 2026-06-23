#pragma once
#include <Arduino.h>
#include "config.h"

class Haptic {
public:
    void begin() {
        pinMode(cfg::HAPTIC_PIN, OUTPUT);
        digitalWrite(cfg::HAPTIC_PIN, LOW);
    }
    void trigger(uint32_t now_ms) {
        active_ = true;
        tStart_ = now_ms;
        digitalWrite(cfg::HAPTIC_PIN, HIGH);
    }


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