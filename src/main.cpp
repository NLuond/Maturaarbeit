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
AirMouseController app(mouse, imu);

static uint32_t nextSample_us = 0;
static uint32_t tickUs = cfg::READY_INTERVAL_US;   // startet ausgeschaltet

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

    // Nur hier: der Wake-Up-Interrupt und die Schlaflogik existieren nur in
    // diesem Zweig, im COLLECT_MODE laeuft app.update() ohnehin nie.
    pinMode(PIN_LSM6DS3TR_C_INT1, INPUT);

    // Erst hier, nicht am Anfang von setup(): die SoftDevice wird von
    // Bluefruit.begin() hochgefahren, und das geschieht in mouse.begin().
    // Vorher waere der SVC-Aufruf wirkungslos, und der direkte
    // Registerzugriff wuerde von der spaeter startenden SoftDevice
    // ueberschrieben.
    //
    // Nach Uebertragungsweg getrennt statt mit Rueckfall: ein SVC ohne
    // laufende SoftDevice ist nicht nur wirkungslos, sondern faellt im
    // schlimmsten Fall in den voreingestellten SVC_Handler des Kerns - und
    // der ist eine Endlosschleife.
    //
    // Deshalb auch innerhalb dieses !COLLECT_MODE-Zweigs: im COLLECT_MODE
    // laeuft mouse.begin() nie, also auch keine SoftDevice - der SVC-Pfad
    // waere dort derselbe Blockierfehler, unabhaengig von USE_BLE_HID.
#if USE_BLE_HID
    sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);
#else
    NRF_POWER->DCDCEN = 1;
#endif
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

#if !COLLECT_MODE
// Tut absichtlich nur eines. Alles Weitere geschieht in der Task, sobald sie
// wieder laeuft - I2C und BLE haben in einer ISR nichts verloren.
static void onMotion() { resumeLoop(); }
#endif

void loop() {
    // Warten statt leer durchlaufen. Der Kern ruft loop() in einer engen
    // Schleife auf; ein sofortiges return hiesse 64 MHz Volllast fuer
    // nichts. delay() ruft vTaskDelay, und mit configUSE_TICKLESS_IDLE
    // schlaeft der Kern dabei tatsaechlich.
    //
    // micros() ist auf diesem Kern aus dem FreeRTOS-Tick abgeleitet (1024 Hz,
    // rund 977 us je Schritt) - dwt_enable() wird nirgends gerufen. Eine
    // Warteschleife kann den Takt also gar nicht feiner treffen als
    // vTaskDelay, sie wuerde nur mit 64 MHz Strom verbrennen. Deshalb wird
    // bis zum Takt geschlafen und nicht gewartet.
    while ((int32_t)(nextSample_us - micros()) > 0) delay(1);

    const uint32_t now_us = micros();

#if DEBUG_TELEPLOT && !COLLECT_MODE
    // Verspaetung dieses Takts, gemessen BEVOR nextSample_us weitergestellt
    // wird - danach waere der Wert um tickUs zu klein. ovr schlaegt erst bei
    // einem ganzen verpassten Takt aus und sieht kleineres Zittern nicht;
    // genau dieses Zittern soll der Kanal zeigen. Die Werte kommen in Stufen
    // von rund 977 us, weil micros() aus dem 1024-Hz-Tick stammt - diese
    // Quantisierung ist selbst der Beleg fuer die grobe Zeitaufloesung.
    const int32_t lateUs = (int32_t)(now_us - nextSample_us);
#endif

#if COLLECT_MODE
    // Die Aufnahme braucht durchgehend die feste ML-Rate. Es gibt hier
    // keinen BEREIT-Zustand und keinen Aufrufer von app.activeRate() -
    // app.update() laeuft im COLLECT_MODE nie, siehe unten.
    tickUs = cfg::SAMPLE_INTERVAL_US;
    const float tickDt = cfg::DT;
#else
    // Die Taktlaenge folgt dem Zustand: AKTIV braucht die 209 Hz des
    // ML-Modells, BEREIT nur die Drehgeste. Der Klassifikator laeuft
    // ausschliesslich in AKTIV, wo weiterhin exakt SAMPLE_INTERVAL_US gilt.
    tickUs = app.activeRate() ? cfg::SAMPLE_INTERVAL_US : cfg::READY_INTERVAL_US;
    const float tickDt = app.activeRate() ? cfg::DT : cfg::READY_DT;
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
    app.update(s, tickDt, now_us);

    #if DEBUG_TELEPLOT
    // Seltener als der uebrige Debug-Takt: der Wert aendert sich langsam und
    // soll die Schleife nicht zusaetzlich belasten.
    if (now_us - lastOvrDbg >= 200000) {
        lastOvrDbg = now_us;
        Serial.print(">ovr:");  Serial.println(overruns);
        Serial.print(">late:"); Serial.println(lateUs);
    }
    #endif

    if (app.wantsSleep()) {
        attachInterrupt(digitalPinToInterrupt(PIN_LSM6DS3TR_C_INT1), onMotion, RISING);
        // Mit LIR haelt INT1 eine Flanke, die zwischen dem Scharfstellen und
        // hier gefallen ist. Ohne diese Pruefung ginge sie verloren:
        // vTaskResume zaehlt nicht, ein Resume vor dem Suspend ist weg - und
        // die Maus schliefe bis zur naechsten Bewegung weiter. INT1 ist aktiv
        // HIGH (CTRL3_C.H_LACTIVE bleibt auf der Vorgabe 0), LOW heisst also
        // "nichts steht an".
        if (digitalRead(PIN_LSM6DS3TR_C_INT1) == LOW) suspendLoop();
        detachInterrupt(digitalPinToInterrupt(PIN_LSM6DS3TR_C_INT1));
        // millis() und nicht micros()/1000: der Quotient aus micros() laeuft
        // schon bei 4'294'967 ueber, und dann ist die vorzeichenlose
        // Differenzarithmetik im Controller ungueltig. Beide Uhren stammen aus
        // demselben FreeRTOS-Tick, laufen also im Gleichtakt - der Controller
        // rechnet seit dieser Aenderung ebenfalls mit millis(), gemischt wird
        // nichts.
        app.onWake(millis());
        // Nach dem Schlaf liegt nextSample_us beliebig weit in der
        // Vergangenheit. Ohne Neuausrichtung liefe die Schleife erst
        // tausende Overrun-Korrekturen ab, bevor sie wieder im Takt ist.
        // tickUs statt SAMPLE_INTERVAL_US: nach dem Aufwachen ist der
        // Automat in BEREIT, der naechste Takt laeuft also mit READY_DT -
        // mit der falschen Konstante haette die IMU (gerade auf 52 Hz
        // gestellt) beim ersten Tick noch kein neues Sample.
        nextSample_us = micros() + tickUs;
    }
#endif
}
