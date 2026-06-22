#pragma once
#include <Arduino.h>
#include <math.h>
#include "config.h"

class OrientationPointer {
public:
    void update(float gX, float gY, float gZ, float pitchDeg, float dt,
                float& outX, float& outY) {

        float rateX = -gZ;
        float rateY =  gX;


        if (fabsf(rateX) < cfg::DEADZONE) rateX = 0.f;
        if (fabsf(rateY) < cfg::DEADZONE) rateY = 0.f;


        float speed = sqrtf(rateX * rateX + rateY * rateY);
        float accel = 1.f + cfg::ACCEL_K * (speed / 200.f);
        accel = constrain(accel, 1.f, cfg::ACCEL_MAX);


        float gainUp = 1.f;
        float over = pitchDeg - (cfg::PITCH_LIMIT - cfg::PITCH_FADE);
        if (over > 0.f) gainUp = constrain(1.f - over / cfg::PITCH_FADE, 0.f, 1.f);
        if (rateY > 0.f) rateY *= gainUp;


        float targetX = rateX * cfg::SENS_X * accel * dt;
        float targetY = rateY * cfg::SENS_Y * accel * dt;


        smX_ += cfg::SMOOTH * (targetX - smX_);
        smY_ += cfg::SMOOTH * (targetY - smY_);
        outX = smX_;
        outY = smY_;
    }

private:
    float smX_ = 0.f;
    float smY_ = 0.f;
};