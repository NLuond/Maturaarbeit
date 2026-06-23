#pragma once
#include <Arduino.h>
#undef round
#include "config.h"
#if USE_ML_PINCH
#include "model-parameters/model_metadata.h"
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#endif
class PinchDetector {
public:
    bool tick(float env, float gyroSum, float ax, float ay, float az, uint32_t now) {
        bool ready = (now - tLastPinch_) >= cfg::DEBOUNCE_MS;
        bool calm  = (gyroSum < cfg::PINCH_GYRO_GUARD);

    #if USE_ML_PINCH
        pushSample(env, gyroSum / 100.f, ax, ay, az);

        bool hot = false;
        if (env > cfg::ENV_ABS && bufferFull()) {
            hot = runInference();
        }
    #else
        bool hot = (env > cfg::ENV_ABS);
        (void)gyroSum; (void)ax; (void)ay; (void)az;
    #endif

        bool fired = false;
        if (hot && !above_ && ready && calm) { tLastPinch_ = now; fired = true; }
        above_ = hot;
        return fired;
    }

    bool inFreeze(uint32_t now) const { return (now - tLastPinch_) < cfg::FREEZE_MS; }

private:
    bool     above_ = false;
    uint32_t tLastPinch_ = 0;

#if USE_ML_PINCH
    static constexpr int CHANNELS   = 5;
    static constexpr int FRAME_SIZE = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;

    float buffer_[FRAME_SIZE];
    float ordered_[FRAME_SIZE];
    int   head_  = 0; 
    int   count_ = 0;  

    void pushSample(float env, float gyro, float ax, float ay, float az) {
        buffer_[head_++] = env;
        buffer_[head_++] = gyro;
        buffer_[head_++] = ax;
        buffer_[head_++] = ay;
        buffer_[head_++] = az;
        if (head_ >= FRAME_SIZE) head_ = 0;
        if (count_ < FRAME_SIZE) count_ += CHANNELS;
    }

    bool bufferFull() const { return count_ >= FRAME_SIZE; }

    void buildOrdered() {
        for (int i = 0; i < FRAME_SIZE; i++) {
            ordered_[i] = buffer_[(head_ + i) % FRAME_SIZE];
        }
    }

    bool runInference() {
        buildOrdered();

        signal_t signal;
        numpy::signal_from_buffer(ordered_, FRAME_SIZE, &signal);

        ei_impulse_result_t result = { 0 };
        EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
        if (err != EI_IMPULSE_OK) {
        #if DEBUG_TELEPLOT
            Serial.print(">ei_err:"); Serial.println(err);
        #endif
            return false;
        }

        float pinchScore = 0.f;
        for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        #if DEBUG_TELEPLOT
            Serial.print(">");  Serial.print(result.classification[i].label);
            Serial.print(":");  Serial.println(result.classification[i].value, 3);
        #endif
            if (strcmp(result.classification[i].label, "pinch") == 0) {
                pinchScore = result.classification[i].value;
            }
        }
        return pinchScore > cfg::ML_CONFIDENCE;
    }
#endif
};