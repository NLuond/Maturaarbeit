#include <Arduino.h>
#include <nrf_soc.h>
#include "config.h"
#include "ImuReader.h"
#include "MouseHID.h"
#include "AirMouseController.h"
#include "VibrationEnvelope.h"
#include "PinchFeatures.h"

ImuReader          imu;
MouseHID           mouse;
AirMouseController app(mouse);

static uint32_t nextSample_us = 0;

#if DEBUG_TELEPLOT || COLLECT_MODE
// Zaehlt, wie oft die Schleife einen ganzen Abtastschritt verpasst hat. Jeder
// Zaehlschritt bedeutet ein zeitlich gedehntes ML-Fenster - der Klassifikator
// bzw. das Training sieht dann etwas anderes als vorgesehen.
static uint16_t overruns = 0;
#endif
#if DEBUG_TELEPLOT && !COLLECT_MODE
// Nur der Teleplot-Pfad drosselt seine Ausgabe. Im COLLECT_MODE waere die
// Variable definiert und ungenutzt - das gibt eine Compiler-Warnung.
static uint32_t lastOvrDbg = 0;
#endif

void setup() {
    // Der nRF52840 startet mit dem LDO; der DC/DC-Wandler spart bei Last bis
    // zu etwa 30 Prozent. Bei aktiver SoftDevice darf das Register nicht
    // direkt beschrieben werden - dort ist sd_power_dcdc_mode_set der
    // richtige Weg. Der Rueckfall auf den Registerzugriff greift, solange
    // die SoftDevice nicht laeuft (USB-Zweig).
    if (sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE) != NRF_SUCCESS) {
        NRF_POWER->DCDCEN = 1;
    }

    // Das Mikrofon wird nie benutzt. Seine Versorgung liegt auf einem
    // eigenen Pin und bleibt aktiv abgeschaltet.
    pinMode(PIN_PDM_PWR, OUTPUT);
    digitalWrite(PIN_PDM_PWR, LOW);

#if DEBUG_TELEPLOT || COLLECT_MODE
    Serial.begin(115200);
#endif
    imu.begin();
#if !COLLECT_MODE
    mouse.begin();
    app.begin();
#endif
#if COLLECT_MODE
    // Aufnahme-Warnleuchte. LED_BUILTIN des XIAO nRF52840 ist aktiv LOW:
    // HIGH ist aus. Sie geht an, sobald die Schleife einen Abtastschritt
    // verpasst hat, und bleibt bis zum Reset an. Leuchtet sie nach der
    // Aufnahme, ist der Datensatz zeitlich gedehnt und wird verworfen - der
    // CSV selbst sieht man das nicht an.
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);
#endif
    nextSample_us = micros();
}

void loop() {
    // Warten statt leer durchlaufen. Der Kern ruft loop() in einer engen
    // Schleife auf; ein sofortiges return hiesse 64 MHz Volllast fuer
    // nichts. delay() ruft vTaskDelay, und mit configUSE_TICKLESS_IDLE
    // schlaeft der Kern dabei tatsaechlich.
    int32_t restUs = (int32_t)(nextSample_us - micros());
    if (restUs > cfg::SLEEP_MIN_REST_US) {
        delay((restUs - 1000) / 1000);
        restUs = (int32_t)(nextSample_us - micros());
    }
    // Die letzte Millisekunde genau abwarten - die FreeRTOS-Aufloesung
    // reicht dafuer nicht.
    while ((int32_t)(nextSample_us - micros()) > 0) { }

    const uint32_t now_us = micros();

    // Feste Schrittweite statt der tatsaechlich verstrichenen Zeit: alle Filter
    // und das ML-Fenster brauchen eine konstante Abtastrate. Nach einer
    // Stockung wird neu ausgerichtet, statt die Rueckstaende nachzuholen.
    nextSample_us += cfg::SAMPLE_INTERVAL_US;
    if ((int32_t)(now_us - nextSample_us) > (int32_t)cfg::SAMPLE_INTERVAL_US) {
        nextSample_us = now_us + cfg::SAMPLE_INTERVAL_US;
    #if DEBUG_TELEPLOT || COLLECT_MODE
        overruns++;
    #endif
    #if COLLECT_MODE
        digitalWrite(LED_BUILTIN, LOW);   // aktiv LOW: an, gelatcht bis zum Reset
    #endif
    }

    const ImuSample s = imu.read(cfg::DT);

#if COLLECT_MODE
    // Muss dieselbe Rate und dieselben Kanaele liefern wie der Inferenz-Pfad
    // sonst lernt das Modell auf anderen Daten, als es spaeter sieht. Die
    // Kanaele kommen deshalb aus feat::pack() - derselben Funktion, die zur
    // Laufzeit das Fenster fuer den Klassifikator fuellt.
    static VibrationEnvelope envelope;

    const float env = envelope.update(s.accMag, cfg::DT);

    float f[feat::CHANNELS];
    feat::pack(s, env, f);

    for (int i = 0; i < feat::CHANNELS; i++) {
        if (i) Serial.print(',');
        // Kanal 0 ist die Huellkurve. Sie bewegt sich zwischen 0.005
        // (Untergrund) und 0.125 (kraeftiger Pinch); bei drei Stellen bliebe
        // am unteren Ende eine einzige signifikante Ziffer uebrig, und genau
        // dort liegt die Schwelle ENV_ON. Die uebrigen vier Kanaele liegen
        // unter der Sensoraufloesung, drei Stellen genuegen - das spart bei
        // 209 Hz rund ein Fuenftel der Serial-Last.
        Serial.print(f[i], i == 0 ? 4 : 3);
    }
    Serial.println();
#else
    app.update(s, cfg::DT, now_us);

    #if DEBUG_TELEPLOT
    // Seltener als der uebrige Debug-Takt: der Wert aendert sich langsam und
    // soll die Schleife nicht zusaetzlich belasten.
    if (now_us - lastOvrDbg >= 200000) {
        lastOvrDbg = now_us;
        Serial.print(">ovr:"); Serial.println(overruns);
    }
    #endif
#endif
}
