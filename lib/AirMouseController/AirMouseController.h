#pragma once
#include <Arduino.h>
#include "config.h"
#include "ImuReader.h"
#include "MouseHID.h"
#include "MadgwickAHRS.h"
#include "VibrationEnvelope.h"
#include "PinchDetector.h"
#include "OrientationPointer.h"
#include "ShakeToggle.h"
#include "Haptic.h"                       // <-- korrekt geschrieben

class AirMouseController {
public:
    explicit AirMouseController(MouseHID& mouse)
        : mouse_(mouse), ahrs_(cfg::MADGWICK_BETA) {}

    void begin() {
        haptic_.begin();
    }

    void update(const ImuSample& s, float dt, uint32_t now_us) {
        uint32_t now_ms = now_us / 1000;

        haptic_.update(now_ms);

        ahrs_.update(s.gx, s.gy, s.gz, s.ax, s.ay, s.az, dt);
        float env = envelope_.update(s.accMag, dt);

        if (shaker_.tick(s.gyroSum, now_ms)) airmouseOn_ = !airmouseOn_;

        if (!airmouseOn_) {
            accumX_ = accumY_ = 0.f;
            debug(env, s.gyroSum, now_us);
            return;
        }

        handleClick(env, s.gyroSum, now_ms);
        handlePointing(s, dt, now_us, now_ms);
        debug(env, s.gyroSum, now_us);
    }

private:
    MouseHID&          mouse_;
    MadgwickAHRS       ahrs_;
    VibrationEnvelope  envelope_;
    PinchDetector      pinch_;
    OrientationPointer pointer_;
    ShakeToggle        shaker_;
    Haptic             haptic_;           // <-- jetzt Member, wie die anderen

    bool     airmouseOn_ = false;
    float    accumX_ = 0.f, accumY_ = 0.f;
    uint32_t lastMove_ = 0, lastDbg_ = 0;

    void handleClick(float env, float gyroSum, uint32_t now_ms) {
        bool mlPinch = false;
        if (pinch_.tick(env, gyroSum, mlPinch, now_ms)) {
            mouse_.click();
            haptic_.trigger(now_ms);
        }
    }

    void handlePointing(const ImuSample& s, float dt, uint32_t now_us, uint32_t now_ms) {
        if (pinch_.inFreeze(now_ms)) return;
        float px, py;
        pointer_.update(s.gx, s.gy, s.gz, ahrs_.pitchDeg(), dt, px, py);
        accumX_ += px; accumY_ += py;
        if (now_us - lastMove_ >= cfg::MOVE_INTERVAL_US) {
            lastMove_ = now_us;
            int8_t mx = (int8_t)constrain(accumX_, -127.f, 127.f);
            int8_t my = (int8_t)constrain(accumY_, -127.f, 127.f);
            if (mx || my) { mouse_.move(mx, my); accumX_ -= mx; accumY_ -= my; }
        }
    }

    void debug(float env, float gyroSum, uint32_t now_us) {
    #if DEBUG_TELEPLOT
        if (now_us - lastDbg_ < 8000) return;
        lastDbg_ = now_us;
        Serial.print(">env:");     Serial.println(env, 4);
        Serial.print(">gyroSum:"); Serial.println(gyroSum, 1);
        Serial.print(">on:");      Serial.println(airmouseOn_ ? 1 : 0);
    #endif
    }
};