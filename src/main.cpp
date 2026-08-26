#include <Arduino.h>
#include <nrf_soc.h>
#include "config.h"
#include "ImuReader.h"
#include "MouseHID.h"
#include "AirMouseController.h"
#if COLLECT_MODE
#include "VibrationEnvelope.h"
#include "PinchFeatures.h"
#include "CollectLink.h"
#endif
#if BENCH_INFERENCE
#include "PinchClassifier.h"
#if !USE_ML_PINCH
#error "BENCH_INFERENCE braucht USE_ML_PINCH"
#endif
#endif

ImuReader          imu;
MouseHID           mouse;
AirMouseController app(mouse, imu);
#if COLLECT_MODE
CollectLink        link;
#endif

// Der einzige Zustand, den die Schleife ueber einen Takt hinaus traegt.
static uint32_t nextSample_us = 0;

#if DEBUG_TELEPLOT || COLLECT_MODE
// Verpasste Abtastschritte. Jeder bedeutet ein zeitlich gedehntes ML-Fenster.
static uint16_t overruns = 0;
#endif
#if DEBUG_TELEPLOT && !COLLECT_MODE
static uint32_t tOvrDbg = 0;
#endif

#if BENCH_INFERENCE
// Statisch, nicht auf dem Stapel: die beiden Fensterpuffer sind zusammen rund
// anderthalb Kilobyte.
static PinchClassifier bench;

// Ein voll gefuelltes Fenster mit plausiblen Werten. Die Rechenzeit eines
// Faltungsnetzes haengt nicht vom Inhalt ab, aber lauter Nullen koennen in der
// Gleitkomma-Vorverarbeitung einen Sonderfall treffen.
static void benchInference() {
    Serial.begin(115200);
    const uint32_t tWait = millis();
    while (!Serial && millis() - tWait < 5000) delay(10);

    ImuSample s{};
    s.ax  = 0.02f;  s.ay  = -0.01f; s.az  = 1.00f;
    s.lax = 0.03f;  s.lay = -0.02f; s.laz = 0.01f;
    s.gx  = 4.0f;   s.gy  = -3.0f;  s.gz  = 2.0f;
    s.accMag = 1.0f; s.gyroSum = 9.0f;
    while (!bench.ready()) bench.push(s, 0.05f);

    constexpr int kRuns = 100;
    const uint32_t t0 = micros();
    for (int i = 0; i < kRuns; i++) (void)bench.isPinch();
    const uint32_t dt = micros() - t0;

    Serial.print("inferenz_us: ");   Serial.println(dt / kRuns);
    Serial.print("davon_dsp_us: ");  Serial.println(bench.lastDspUs());
    Serial.print("davon_netz_us: "); Serial.println(bench.lastNnUs());
    Serial.print("takt_budget_us: ");Serial.println(cfg::SAMPLE_INTERVAL_US);
}
#endif

void setup() {
    // Das Mikrofon wird nie benutzt und bleibt aktiv abgeschaltet.
    pinMode(PIN_PDM_PWR, OUTPUT);
    digitalWrite(PIN_PDM_PWR, LOW);

#if DEBUG_TELEPLOT && !COLLECT_MODE
    Serial.begin(115200);
#endif
    imu.begin();
#if !COLLECT_MODE
    mouse.begin();
    app.begin();
    pinMode(PIN_LSM6DS3TR_C_INT1, INPUT);

    // Erst hier, nicht am Anfang von setup(): die SoftDevice wird von
    // Bluefruit.begin() in mouse.begin() hochgefahren, und ein SVC ohne laufende
    // SoftDevice faellt in den SVC_Handler des Kerns - eine Endlosschleife.
#if USE_BLE_HID
    sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);
#else
    NRF_POWER->DCDCEN = 1;
#endif
#endif
#if COLLECT_MODE
    // Der Sendeweg bringt seinen eigenen Anschluss mit: Serial im USB-Zweig,
    // die SoftDevice im BLE-Zweig.
    link.begin();

    // Aufnahme-Warnleuchte, aktiv LOW: sie geht bei einem verpassten
    // Abtastschritt oder einer nicht abgegebenen Zeile an und bleibt bis zum
    // Reset an. Leuchtet sie danach, ist der Datensatz gedehnt oder
    // lueckenhaft - dem CSV selbst sieht man das nicht an.
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);
#endif
#if BENCH_INFERENCE
    benchInference();
#endif
    nextSample_us = micros();
}

#if !COLLECT_MODE
// Tut absichtlich nur eines: I2C und BLE haben in einer ISR nichts verloren.
static void onMotion() { resumeLoop(); }

static void sleepUntilMotion() {
    attachInterrupt(digitalPinToInterrupt(PIN_LSM6DS3TR_C_INT1), onMotion, RISING);
    // INT1 haelt dank LIR eine Flanke, die zwischen dem Scharfstellen und hier
    // gefallen ist. Ohne die Pruefung ginge sie verloren: ein Resume vor dem
    // Suspend zaehlt nicht. INT1 ist aktiv HIGH.
    if (digitalRead(PIN_LSM6DS3TR_C_INT1) == LOW) suspendLoop();
    detachInterrupt(digitalPinToInterrupt(PIN_LSM6DS3TR_C_INT1));

    // millis() und nicht micros()/1000: der Quotient laeuft schon bei
    // 4'294'967 ueber.
    app.onWake(millis());
}
#endif

void loop() {
    // Warten statt leer durchlaufen: der Kern ruft loop() sonst in einer engen
    // Schleife mit 64 MHz auf. delay() ruft vTaskDelay, und mit
    // configUSE_TICKLESS_IDLE schlaeft der Kern dabei tatsaechlich. Feiner
    // treffen liesse sich der Takt ohnehin nicht - micros() stammt hier aus dem
    // FreeRTOS-Tick (1024 Hz, rund 977 us je Schritt).
    while ((int32_t)(nextSample_us - micros()) > 0) delay(1);

    const uint32_t now_us = micros();

#if DEBUG_TELEPLOT && !COLLECT_MODE
    // Verspaetung dieses Takts, gemessen vor dem Weiterstellen von
    // nextSample_us. ovr schlaegt erst bei einem ganzen verpassten Takt aus.
    const int32_t lateUs = (int32_t)(now_us - nextSample_us);
#endif

#if COLLECT_MODE
    // Die Aufnahme braucht durchgehend die feste ML-Rate.
    const uint32_t tickUs = cfg::SAMPLE_INTERVAL_US;
    const float    tickDt = cfg::DT;
#else
    // AKTIV braucht die volle Sensorrate fuer das ML-Fenster, BEREIT nur die
    // Drehgeste.
    const bool     active = app.wantsActiveRate();
    const uint32_t tickUs = active ? cfg::SAMPLE_INTERVAL_US : cfg::READY_INTERVAL_US;
    const float    tickDt = active ? cfg::DT                 : cfg::READY_DT;
#endif

    // Feste Schrittweite statt der tatsaechlich verstrichenen Zeit: alle Filter
    // und das ML-Fenster brauchen eine konstante Abtastrate. Nach einer Stockung
    // wird neu ausgerichtet, statt die Rueckstaende nachzuholen.
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
    // Dieselbe Rate und dieselben Kanaele wie der Inferenz-Pfad, sonst lernt das
    // Modell auf anderen Daten, als es spaeter sieht.
    static VibrationEnvelope envelope;

    const float env = envelope.update(s.accMag, cfg::DT);

    float f[feat::CHANNELS];
    feat::pack(s, env, f);

    if (!link.send(f)) digitalWrite(LED_BUILTIN, LOW);   // aktiv LOW, gelatcht
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
        // Vergangenheit; ohne Neuausrichtung liefe die Schleife tausende
        // Overrun-Korrekturen ab. tickUs, weil der Automat nach dem Aufwachen in
        // BEREIT ist.
        nextSample_us = micros() + tickUs;
    }
#endif
}
