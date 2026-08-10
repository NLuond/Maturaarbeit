#pragma once
#include <Arduino.h>
#include "config.h"

// Misst die Akkuspannung ueber den eingebauten Spannungsteiler der XIAO.
// VBAT_ENABLE (aktiv LOW) schaltet den Teiler nur fuer die Messung zu: dauerhaft
// angeschlossen zoege er staendig Strom, und genau den wollen wir hier messen.
class Battery {
public:
    void begin() {
        pinMode(VBAT_ENABLE, OUTPUT);
        digitalWrite(VBAT_ENABLE, HIGH);      // aktiv LOW: Teiler aus
        analogReference(AR_INTERNAL_3_0);
        analogReadResolution(12);             // 0..4095
    }

    void update(uint32_t now_ms) {
        if (everRun_ && now_ms - tLast_ < cfg::BATTERY_INTERVAL_MS) return;
        everRun_ = true;
        tLast_   = now_ms;

        digitalWrite(VBAT_ENABLE, LOW);
        uint32_t sum = 0;
        for (int i = 0; i < kOversample; i++) sum += analogRead(PIN_VBAT);
        digitalWrite(VBAT_ENABLE, HIGH);

        volts_ = (sum / (float)kOversample) * cfg::BATTERY_VOLTS_PER_LSB;
    }

    float volts() const { return volts_; }

private:
    // Der ADC rauscht; gemittelt wird ueber mehrere Wandlungen. Als Konstante,
    // weil Schleife und Teiler zwingend dieselbe Zahl brauchen.
    static constexpr int kOversample = 8;

    bool     everRun_ = false;   // sonst wartet die erste Messung ein Intervall
    uint32_t tLast_   = 0;
    float    volts_   = 0.f;
};
