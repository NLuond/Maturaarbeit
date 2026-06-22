#include <Arduino.h>
#include "config.h"
#include "ImuReader.h"
#include "MouseHID.h"
#include "AirMouseController.h"
#include "MadgwickAHRS.h"
#include "VibrationEnvelope.h"

ImuReader          imu;
MouseHID           mouse;
AirMouseController app(mouse);
uint32_t prev_us = 0;

void setup() {
    Serial.begin(115200);
#if !COLLECT_MODE
    mouse.begin();
    imu.begin();
    app.begin();
    while (!mouse.ready()) delay(1);
#else
    imu.begin();
#endif
    prev_us = micros();
}

void loop() {
    uint32_t now_us = micros();
    float dt = (now_us - prev_us) * 1e-6f;
    if (dt < 0.0002f) return;
    prev_us = now_us;

    ImuSample s = imu.read();

#if COLLECT_MODE
    static uint32_t lastSample_us = 0;
    static MadgwickAHRS  ahrsC(cfg::MADGWICK_BETA);
    static VibrationEnvelope envC;

    ahrsC.update(s.gx, s.gy, s.gz, s.ax, s.ay, s.az, dt);
    float env = envC.update(s.accMag, dt);

    if (now_us - lastSample_us >= 10000) {
        lastSample_us = now_us;
        float gyroScaled = s.gyroSum / 100.f;

        Serial.print(env, 4);        Serial.print(',');
        Serial.print(gyroScaled, 4); Serial.print(',');
        Serial.print(s.ax, 4);       Serial.print(',');
        Serial.print(s.ay, 4);       Serial.print(',');
        Serial.println(s.az, 4);
    }
#else
    app.update(s, dt, now_us);
#endif
}