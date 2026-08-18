#pragma once
#include <stdint.h>
#include "ScrollWheel.h"

// Nimmt die fertigen Pixel eines Takts entgegen und bringt sie ans Ziel:
// Cursor oder Rad. Haelt dafuer den Rueckstau, der entsteht, weil ein
// HID-Bericht nur +-127 px je Achse traegt und ueber BLE nur einer je
// Verbindungsintervall durchgeht.
//
// Ohne config.h und ohne Arduino.h, damit der PC-Test laeuft; die Werte stehen
// in MotionTuning und sind per static_assert an cfg:: gebunden.

// Wohin die Bewegung dieses Takts geht.
enum class MotionTarget : uint8_t { None, Cursor, Wheel };

struct MotionTuning {
    uint8_t maxReports = 3;      // Pakete je Takt
    float   backlogMax = 900.f;  // px, Deckel des Rueckstaus
};

class MotionPipeline {
public:
    explicit MotionPipeline(const MotionTuning& t = MotionTuning(),
                            const ScrollTuning& s = ScrollTuning())
        : t_(t), wheel_(s) {}

    // send(dx, dy) meldet zurueck, ob das Paket angenommen wurde; scroll(ticks)
    // gibt Radschritte aus. Beide als Callable, damit dieses Modul das HID nicht
    // kennt - wie der Klassifikator in PinchDetector.
    template <class Send, class Scroll>
    void run(MotionTarget target, float dxPx, float dyPx,
             uint32_t now_us, uint32_t now_ms, uint32_t intervalUs,
             Send send, Scroll scroll) {
        if (target == MotionTarget::Wheel) {
            const int8_t ticks = wheel_.update(dyPx, now_ms);
            if (ticks) scroll(ticks);
            return;
        }
        // Kein Rest darf stehenbleiben, der beim naechsten Abdrehen sofort einen
        // Schritt ausloest.
        wheel_.reset();

        if (target == MotionTarget::None) { accumX_ = accumY_ = 0.f; return; }

        accumX_ += dxPx;
        accumY_ += dyPx;

        if (now_us - tSend_ < intervalUs) return;
        tSend_ = now_us;
        flush(send);
    }

    void reset() { accumX_ = accumY_ = 0.f; wheel_.reset(); }

    float    pendingX()     const { return accumX_; }
    float    pendingY()     const { return accumY_; }
    float    pendingWheel() const { return wheel_.pending(); }
    uint16_t rejected()     const { return nRejected_; }

private:
    MotionTuning t_;
    ScrollWheel  wheel_;
    float        accumX_ = 0.f, accumY_ = 0.f;
    uint32_t     tSend_ = 0;
    uint16_t     nRejected_ = 0;

    template <class Send>
    void flush(Send send) {
        for (uint8_t i = 0; i < t_.maxReports; i++) {
            const int8_t dx = clamp8(accumX_);
            const int8_t dy = clamp8(accumY_);
            if (!dx && !dy) break;

            // Erst abziehen, wenn das Paket angenommen wurde - sonst geht
            // Bewegung bei voller Warteschlange verloren.
            if (send(dx, dy)) {
                accumX_ -= dx;
                accumY_ -= dy;
                continue;
            }
            // Nimmt die Gegenstelle laenger nichts an, liefe der Rueckstau
            // minutenlang weiter und der Cursor schoesse beim Verbinden quer
            // ueber den Schirm.
            nRejected_++;
            accumX_ = cap(accumX_);
            accumY_ = cap(accumY_);
            break;
        }
    }

    static int8_t clamp8(float v) {
        if (v >  127.f) return  127;
        if (v < -127.f) return -127;
        return (int8_t)v;
    }

    float cap(float v) const {
        if (v >  t_.backlogMax) return  t_.backlogMax;
        if (v < -t_.backlogMax) return -t_.backlogMax;
        return v;
    }
};
