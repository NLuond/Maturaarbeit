#pragma once
#include <Arduino.h>
#include "config.h"

// Misst die Akkuspannung ueber den eingebauten Spannungsteiler der XIAO.
//
// VBAT_ENABLE (P14) schaltet den Teiler zu und ist aktiv LOW. Er wird nur
// fuer die Messung eingeschaltet und danach wieder freigegeben: ein
// dauerhaft angeschlossener Teiler zieht staendig Strom, und genau den
// wollen wir hier ja messen.
//
// Gemessen wird selten (BATTERY_INTERVAL_MS) und ueber mehrere Wandlungen
// gemittelt - der ADC rauscht, und die Spannung aendert sich ueber Stunden.
class Battery {
public:
    // Anzahl der gemittelten Wandlungen. Als Konstante und nicht zweimal als
    // Ziffer: Schleife und Teiler muessen zwingend dieselbe Zahl benutzen,
    // sonst ist das Ergebnis stillschweigend um den Faktor daneben.
    // Steht hier und nicht in cfg::, weil es keine Einstellgroesse ist,
    // sondern eine Eigenschaft dieser Mittelung.
    static constexpr int kOversample = 8;

    void begin() {
        pinMode(VBAT_ENABLE, OUTPUT);
        digitalWrite(VBAT_ENABLE, HIGH);      // aktiv LOW: Teiler aus
        analogReference(AR_INTERNAL_3_0);     // 3.0 V Referenz
        analogReadResolution(12);             // 0..4095
    }

    void update(uint32_t now_ms) {
        if (used_ && now_ms - tLast_ < cfg::BATTERY_INTERVAL_MS) return;
        used_  = true;
        tLast_ = now_ms;

        digitalWrite(VBAT_ENABLE, LOW);       // Teiler zu
        uint32_t sum = 0;
        for (int i = 0; i < kOversample; i++) sum += analogRead(PIN_VBAT);
        digitalWrite(VBAT_ENABLE, HIGH);      // und wieder weg

        volts_ = (sum / (float)kOversample) * cfg::BATTERY_VOLTS_PER_LSB;
    }

    float volts() const { return volts_; }

private:
    // used_ nur, damit die erste Messung nicht bis BATTERY_INTERVAL_MS
    // wartet: kurz nach dem Start ist now_ms klein und tLast_ noch null.
    bool     used_  = false;
    uint32_t tLast_ = 0;
    float    volts_ = 0.f;
};
