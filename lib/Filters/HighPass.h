#pragma once
#include <Arduino.h>
#include <math.h>

// Hochpass erster Ordnung. Der erste Aufruf liefert 0 statt x: ohne
// Vorgaengerwert waere jede Sprungantwort ein Scheinimpuls.
class HighPass {
public:
    explicit HighPass(float cutoffHz) : fc(cutoffHz) {}
    float run(float x, float dt) {
        if (first) { xPrev = x; first = false; return 0.f; }
        float rc = 1.f / (2.f * PI * fc);
        float a  = rc / (rc + dt);
        float y  = a * (yPrev + x - xPrev);
        yPrev = y; xPrev = x;
        return y;
    }
private:
    float fc, yPrev = 0, xPrev = 0;
    bool first = true;
};
