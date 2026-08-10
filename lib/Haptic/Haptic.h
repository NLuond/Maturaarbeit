#pragma once
#include <Arduino.h>
#include "config.h"

// Impuls-Sequenzer: n Impulse a HAPTIC_MS mit HAPTIC_GAP_MS dazwischen.
// Ein Impuls bedeutet Linksklick, Haltungswechsel oder Ein/Aus, zwei bedeuten
// Rechtsklick.
//
// Der Ausgang wird rein digital geschaltet, nicht per PWM: es gibt keine
// Intensitaetsstufe, die Information steckt in der Anzahl der Impulse.
//
// Gesperrt ist genau, solange ein Muster laeuft, plus HAPTIC_REST_MS danach -
// eine feste Sperrfrist muesste ueber der Musterdauer liegen und wuerde einen
// Doppelklick nur noch einmal brummen lassen.
class Haptic {
public:
    void begin() {
        pinMode(cfg::HAPTIC_PIN, OUTPUT);
        digitalWrite(cfg::HAPTIC_PIN, LOW);
    }

    void trigger(uint32_t now_ms, uint8_t pulses = 1) {
        if (pulses == 0) return;
        if (busy_) return;
        if (everRun_ && now_ms - tFree_ < cfg::HAPTIC_REST_MS) return;
        left_  = pulses;
        busy_  = true;
        everRun_ = true;
        on_    = true;
        tStep_ = now_ms;
        digitalWrite(cfg::HAPTIC_PIN, HIGH);
    }

    void update(uint32_t now_ms) {
        if (!busy_) return;
        if (on_) {
            if (now_ms - tStep_ < cfg::HAPTIC_MS) return;
            digitalWrite(cfg::HAPTIC_PIN, LOW);
            on_    = false;
            tStep_ = now_ms;
            if (--left_ == 0) { busy_ = false; tFree_ = now_ms; }
        } else {
            if (now_ms - tStep_ < cfg::HAPTIC_GAP_MS) return;
            digitalWrite(cfg::HAPTIC_PIN, HIGH);
            on_    = true;
            tStep_ = now_ms;
        }
    }

private:
    bool     busy_    = false;
    bool     on_      = false;
    bool     everRun_ = false;   // sonst greift die Ruhezeit schon beim ersten Mal
    uint8_t  left_    = 0;
    uint32_t tStep_   = 0;
    uint32_t tFree_   = 0;
};
