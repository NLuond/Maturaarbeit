#pragma once
#include <Arduino.h>
#undef round                 // das EI-SDK kollidiert sonst mit dem Arduino-Makro
#include "config.h"
#include "ImuSample.h"
#include "PinchFeatures.h"

// Kapselt das Edge-Impulse-SDK: gleitendes Fenster, Kanal-Packung, Inferenz.
// Die Entscheidungslogik (Schwelle, Entprellung, Gyro-Guard) liegt bewusst
// nicht hier, sondern in PinchDetector - das SDK bleibt damit auf diese eine
// Datei beschraenkt und ein Modellwechsel beruehrt keine Ablauflogik.
//
// Die Klasse gibt selbst nichts aus. Wer die Zwischenwerte sehen will, holt
// sie ueber score()/error() ab; die Ausgabe passiert gebuendelt in
// AirMouseController::debug().

#if USE_ML_PINCH
#include "model-parameters/model_metadata.h"
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

// Das Modell wurde auf einer festen Abtastrate trainiert. Laeuft die Schleife
// schneller oder langsamer, sieht der Klassifikator ein zeitlich gestauchtes
// oder gedehntes Fenster und die gelernten Frequenzmerkmale stimmen nicht mehr.
static_assert(cfg::SAMPLE_INTERVAL_US > EI_CLASSIFIER_INTERVAL_MS * 1000.0 - 25.0 &&
              cfg::SAMPLE_INTERVAL_US < EI_CLASSIFIER_INTERVAL_MS * 1000.0 + 25.0,
              "cfg::SAMPLE_INTERVAL_US passt nicht zur Abtastrate des Modells");

static_assert(EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME == feat::CHANNELS,
              "feat::pack() liefert eine andere Kanalzahl als das Modell erwartet");

class PinchClassifier {
public:
    // Jeden Takt aufrufen, auch wenn gerade nicht klassifiziert wird - sonst
    // ist das Fenster veraltet, sobald es gebraucht wird.
    void push(const ImuSample& s, float env) {
        feat::pack(s, env, buffer_ + head_);
        head_ += feat::CHANNELS;
        if (head_ >= FRAME_SIZE) head_ = 0;
        if (count_ < FRAME_SIZE)  count_ += feat::CHANNELS;
    }

    bool ready() const { return count_ >= FRAME_SIZE; }

    // Fuehrt die Inferenz aus. Kostet Rechenzeit, deshalb nur bei offenem Gate
    // aufrufen (PinchDetector erledigt das ueber Kurzschlussauswertung).
    bool isPinch() {
        buildOrdered();

        signal_t signal;
        numpy::signal_from_buffer(ordered_, FRAME_SIZE, &signal);

        ei_impulse_result_t result = { 0 };
        // Selbst gestoppt statt result.timing: dessen Felder sind auf ganze
        // Millisekunden gerundet, was fuer eine Inferenz in dieser
        // Groessenordnung zu grob ist, um Optimierungen zu vergleichen.
        const uint32_t t0 = micros();
        err_ = run_classifier(&signal, &result, false);
        lastUs_ = micros() - t0;
        if (err_ != EI_IMPULSE_OK) { score_ = 0.f; return false; }

        score_ = 0.f;
        for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
            if (strcmp(result.classification[i].label, "pinch") == 0) {
                score_ = result.classification[i].value;
            }
        }
        return score_ > cfg::ML_CONFIDENCE;
    }

    float    score()  const { return score_; }
    int      error()  const { return (int)err_; }
    // Dauer der letzten Inferenz. Der Wert ist der Massstab dafuer, ob die
    // CMSIS-Beschleunigung im Build tatsaechlich greift.
    uint32_t lastUs() const { return lastUs_; }

private:
    static constexpr int FRAME_SIZE = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;

    static_assert(FRAME_SIZE % feat::CHANNELS == 0,
                  "Fenstergroesse muss ein Vielfaches der Kanalzahl sein");

    float buffer_[FRAME_SIZE];
    float ordered_[FRAME_SIZE];
    int   head_  = 0;
    int   count_ = 0;

    float            score_  = 0.f;
    uint32_t         lastUs_ = 0;
    EI_IMPULSE_ERROR err_    = EI_IMPULSE_OK;

    // Der Ringpuffer beginnt beim aeltesten Wert; das SDK erwartet das Fenster
    // in zeitlicher Reihenfolge.
    void buildOrdered() {
        for (int i = 0; i < FRAME_SIZE; i++) {
            ordered_[i] = buffer_[(head_ + i) % FRAME_SIZE];
        }
    }
};

#else

// Ersatz ohne Modell: meldet sich immer bereit und immer positiv, damit in
// PinchDetector allein das Schwellwert-Gate entscheidet.
class PinchClassifier {
public:
    void     push(const ImuSample&, float) {}
    bool     ready()   const { return true; }
    bool     isPinch()       { return true; }
    float    score()   const { return 0.f; }
    int      error()   const { return 0; }
    uint32_t lastUs()  const { return 0; }
};

#endif
