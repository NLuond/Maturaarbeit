#pragma once
#include <Arduino.h>
#include "config.h"

// Kleiner Impuls-Sequenzer: n Impulse à HAPTIC_MS mit HAPTIC_GAP_MS dazwischen.
// Ein Impuls bedeutet Linksklick, Haltungswechsel oder Ein/Aus, zwei bedeuten
// Rechtsklick.
//
// Es gibt bewusst KEINE feste Sperrfrist mehr. Eine solche muesste ueber der
// Dauer des Zwei-Impuls-Musters liegen (130 ms), cfg::DEBOUNCE_MS steht aber
// auf 180 ms - ein Doppelklick wuerde damit nur noch einmal brummen. Gesperrt
// ist stattdessen genau, solange ein Muster laeuft, plus eine Luecke danach.
// Das erfuellt denselben Zweck: dicht aufeinander folgende Ausloeser
// verschmelzen nicht zu einem langen Brummen, sondern bleiben abzaehlbar.
class Haptic {
public:
    void begin() {
        pinMode(cfg::HAPTIC_PIN, OUTPUT);
        digitalWrite(cfg::HAPTIC_PIN, LOW);
    }

    void trigger(uint32_t now_ms, uint8_t pulses = 1) {
        if (pulses == 0) return;
        if (busy_) return;                                        // laufendes Muster nicht stoeren
        if (used_ && now_ms - tFree_ < cfg::HAPTIC_GAP_MS) return; // Ruhe danach
        left_  = pulses;
        busy_  = true;
        used_  = true;
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
    // used_ nur, damit die Ruhezeit nicht schon beim ersten Ausloeser greift:
    // kurz nach dem Start ist now_ms klein und tFree_ noch null.
    bool     busy_  = false;
    bool     on_    = false;
    bool     used_  = false;
    uint8_t  left_  = 0;
    uint32_t tStep_ = 0;
    uint32_t tFree_ = 0;
};
