#pragma once
#include <Arduino.h>
#include "config.h"

class Haptic {
public:
    void begin() {
        pinMode(cfg::HAPTIC_PIN, OUTPUT);
        digitalWrite(cfg::HAPTIC_PIN, LOW);
    }
    // Sperrfrist gegen Dauerbrummen. Ereignisse fallen oft dicht zusammen: ein
    // Doppel-Pinch meldet Klick und Drag-Start kurz nacheinander, ein Wechsel
    // durch mehrere Haltungen ebenso. Ohne Sperre setzt jeder Ausloeser tStart_
    // neu, der Motor laeuft durch und der Nutzer spuert einen einzigen langen
    // Brumm statt einer abzaehlbaren Rueckmeldung - genau das, was eine
    // haptische Bestaetigung nicht sein darf.
    void trigger(uint32_t now_ms) {
        if (used_ && now_ms - tStart_ < cfg::HAPTIC_COOLDOWN_MS) return;
        used_   = true;
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
    // used_ nur, damit die Sperre nicht schon beim ersten Ausloeser greift:
    // kurz nach dem Start ist now_ms klein und tStart_ noch null.
    bool     used_   = false;
    bool     active_ = false;
    uint32_t tStart_ = 0;
};