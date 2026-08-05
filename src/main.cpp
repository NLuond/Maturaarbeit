#include <Arduino.h>
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

#if DEBUG_TELEPLOT && !COLLECT_MODE
// Zaehlt, wie oft die Schleife einen ganzen Abtastschritt verpasst hat. Jeder
// Zaehlschritt bedeutet ein zeitlich gedehntes ML-Fenster - der Klassifikator
// sieht dann etwas anderes als im Training. Steigt ovr waehrend einer Messung,
// misst man den Messaufbau und nicht mehr das System.
static uint16_t overruns   = 0;
static uint32_t lastOvrDbg = 0;
#endif

void setup() {
#if DEBUG_TELEPLOT || COLLECT_MODE
    Serial.begin(115200);
#endif
    imu.begin();
#if !COLLECT_MODE
    mouse.begin();
    app.begin();
#endif
    nextSample_us = micros();
}

void loop() {
    const uint32_t now_us = micros();
    if ((int32_t)(now_us - nextSample_us) < 0) return;

    // Feste Schrittweite statt der tatsaechlich verstrichenen Zeit: alle Filter
    // und das ML-Fenster brauchen eine konstante Abtastrate. Nach einer
    // Stockung wird neu ausgerichtet, statt die Rueckstaende nachzuholen.
    nextSample_us += cfg::SAMPLE_INTERVAL_US;
    if ((int32_t)(now_us - nextSample_us) > (int32_t)cfg::SAMPLE_INTERVAL_US) {
        nextSample_us = now_us + cfg::SAMPLE_INTERVAL_US;
    #if DEBUG_TELEPLOT && !COLLECT_MODE
        overruns++;
    #endif
    }

    const ImuSample s = imu.read(cfg::DT);

#if COLLECT_MODE
    // Muss dieselbe Rate und dieselben Kanaele liefern wie der Inferenz-Pfad,5
    // sonst lernt das Modell auf anderen Daten, als es spaeter sieht. Die
    // Kanaele kommen deshalb aus feat::pack() - derselben Funktion, die zur
    // Laufzeit das Fenster fuer den Klassifikator fuellt.
    static VibrationEnvelope envelope;

    const float env = envelope.update(s.accMag, cfg::DT);

    float f[feat::CHANNELS];
    feat::pack(s, env, f);

    for (int i = 0; i < feat::CHANNELS; i++) {
        if (i) Serial.print(',');
        Serial.print(f[i], 4);
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
