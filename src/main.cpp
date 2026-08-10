#include <Arduino.h>
#include <nrf_soc.h>
#include "config.h"
#include "ImuReader.h"
#include "MouseHID.h"
#include "AirMouseController.h"
#if COLLECT_MODE
#include "VibrationEnvelope.h"
#include "PinchFeatures.h"
#endif

ImuReader          imu;
MouseHID           mouse;
AirMouseController app(mouse, imu);

// Der einzige Zustand, den die Schleife ueber einen Takt hinaus traegt.
static uint32_t nextSample_us = 0;

#if DEBUG_TELEPLOT || COLLECT_MODE
// Verpasste Abtastschritte. Jeder bedeutet ein zeitlich gedehntes ML-Fenster.
static uint16_t overruns = 0;
#endif
#if DEBUG_TELEPLOT && !COLLECT_MODE
static uint32_t tOvrDbg = 0;
#endif

void setup() {
    // Das Mikrofon wird nie benutzt und bleibt aktiv abgeschaltet.
    pinMode(PIN_PDM_PWR, OUTPUT);
    digitalWrite(PIN_PDM_PWR, LOW);

#if DEBUG_TELEPLOT || COLLECT_MODE
    Serial.begin(115200);
#endif
    imu.begin();
#if !COLLECT_MODE
    mouse.begin();
    app.begin();
    pinMode(PIN_LSM6DS3TR_C_INT1, INPUT);

    // Erst hier, nicht am Anfang von setup(): die SoftDevice wird von
    // Bluefruit.begin() in mouse.begin() hochgefahren. Ein SVC ohne laufende
    // SoftDevice faellt im schlimmsten Fall in den voreingestellten
    // SVC_Handler des Kerns - und der ist eine Endlosschleife. Deshalb nach
    // Uebertragungsweg getrennt und innerhalb dieses !COLLECT_MODE-Zweigs.
#if USE_BLE_HID
    sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);
#else
    NRF_POWER->DCDCEN = 1;
#endif
#endif
#if COLLECT_MODE
    // Aufnahme-Warnleuchte, aktiv LOW. Sie geht an, sobald ein Abtastschritt
    // verpasst wurde, und bleibt bis zum Reset an: leuchtet sie nach der
    // Aufnahme, ist der Datensatz zeitlich gedehnt und wird verworfen - der
    // CSV selbst sieht man das nicht an.
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);
#endif
    nextSample_us = micros();
}

#if !COLLECT_MODE
// Tut absichtlich nur eines: I2C und BLE haben in einer ISR nichts verloren.
static void onMotion() { resumeLoop(); }

static void sleepUntilMotion() {
    attachInterrupt(digitalPinToInterrupt(PIN_LSM6DS3TR_C_INT1), onMotion, RISING);
    // INT1 haelt dank LIR eine Flanke, die zwischen dem Scharfstellen und hier
    // gefallen ist. Ohne die Pruefung ginge sie verloren: vTaskResume zaehlt
    // nicht, ein Resume vor dem Suspend ist weg. INT1 ist aktiv HIGH, LOW
    // heisst also "nichts steht an".
    if (digitalRead(PIN_LSM6DS3TR_C_INT1) == LOW) suspendLoop();
    detachInterrupt(digitalPinToInterrupt(PIN_LSM6DS3TR_C_INT1));

    // millis() und nicht micros()/1000: der Quotient laeuft schon bei
    // 4'294'967 ueber. Beide Uhren stammen aus demselben FreeRTOS-Tick.
    app.onWake(millis());
}
#endif

void loop() {
    // Warten statt leer durchlaufen: der Kern ruft loop() sonst in einer engen
    // Schleife mit 64 MHz auf. delay() ruft vTaskDelay, und mit
    // configUSE_TICKLESS_IDLE schlaeft der Kern dabei tatsaechlich.
    //
    // micros() ist auf diesem Kern aus dem FreeRTOS-Tick abgeleitet (1024 Hz,
    // rund 977 us je Schritt) - eine Warteschleife koennte den Takt gar nicht
    // feiner treffen als vTaskDelay, sie wuerde nur Strom verbrennen.
    while ((int32_t)(nextSample_us - micros()) > 0) delay(1);

    const uint32_t now_us = micros();

#if DEBUG_TELEPLOT && !COLLECT_MODE
    // Verspaetung dieses Takts, gemessen BEVOR nextSample_us weitergestellt
    // wird. ovr schlaegt erst bei einem ganzen verpassten Takt aus; dieser
    // Kanal zeigt auch kleineres Zittern.
    const int32_t lateUs = (int32_t)(now_us - nextSample_us);
#endif

#if COLLECT_MODE
    // Die Aufnahme braucht durchgehend die feste ML-Rate.
    const uint32_t tickUs = cfg::SAMPLE_INTERVAL_US;
    const float    tickDt = cfg::DT;
#else
    // AKTIV braucht die 209 Hz des ML-Modells, BEREIT nur die Drehgeste.
    const bool     active = app.wantsActiveRate();
    const uint32_t tickUs = active ? cfg::SAMPLE_INTERVAL_US : cfg::READY_INTERVAL_US;
    const float    tickDt = active ? cfg::DT                 : cfg::READY_DT;
#endif

    // Feste Schrittweite statt der tatsaechlich verstrichenen Zeit: alle Filter
    // und das ML-Fenster brauchen eine konstante Abtastrate. Nach einer
    // Stockung wird neu ausgerichtet, statt die Rueckstaende nachzuholen.
    nextSample_us += tickUs;
    if ((int32_t)(now_us - nextSample_us) > (int32_t)tickUs) {
        nextSample_us = now_us + tickUs;
    #if DEBUG_TELEPLOT || COLLECT_MODE
        overruns++;
    #endif
    #if COLLECT_MODE
        digitalWrite(LED_BUILTIN, LOW);   // aktiv LOW: an, gelatcht bis zum Reset
    #endif
    }

    const ImuSample s = imu.read(tickDt);

#if COLLECT_MODE
    // Dieselbe Rate und dieselben Kanaele wie der Inferenz-Pfad, sonst lernt
    // das Modell auf anderen Daten, als es spaeter sieht. Die Kanaele kommen
    // deshalb aus feat::pack().
    static VibrationEnvelope envelope;

    const float env = envelope.update(s.accMag, cfg::DT);

    float f[feat::CHANNELS];
    feat::pack(s, env, f);

    for (int i = 0; i < feat::CHANNELS; i++) {
        if (i) Serial.print(',');
        // Kanal 0 ist die Huellkurve: sie bewegt sich zwischen 0.005 und 0.125,
        // bei drei Stellen bliebe am unteren Ende eine signifikante Ziffer.
        Serial.print(f[i], i == 0 ? 4 : 3);
    }
    Serial.println();
#else
    app.update(s, tickDt, now_us);

    #if DEBUG_TELEPLOT
    // Seltener als der uebrige Debug-Takt: die Werte aendern sich langsam.
    if (now_us - tOvrDbg >= 200000) {
        tOvrDbg = now_us;
        Serial.print(">ovr:");  Serial.println(overruns);
        Serial.print(">late:"); Serial.println(lateUs);
    }
    #endif

    if (app.wantsSleep()) {
        sleepUntilMotion();
        // Nach dem Schlaf liegt nextSample_us beliebig weit in der
        // Vergangenheit; ohne Neuausrichtung liefe die Schleife erst tausende
        // Overrun-Korrekturen ab. tickUs und nicht SAMPLE_INTERVAL_US: der
        // Automat ist nach dem Aufwachen in BEREIT.
        nextSample_us = micros() + tickUs;
    }
#endif
}
