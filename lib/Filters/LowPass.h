#pragma once
#include <Arduino.h>
#include <math.h>

class LowPass {
public:
    explicit LowPass(float cutoffHz) : fc(cutoffHz) {}
    float run(float x, float dt) {
        if (first) { y = x; first = false; return x; }
        float a = 1.f - expf(-2.f * PI * fc * dt);
        y += a * (x - y);
        return y;
    }
private:
    float fc, y = 0;
    bool first = true;
};