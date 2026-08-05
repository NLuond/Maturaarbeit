#pragma once
#include <LSM6DS3.h>
#include <math.h>
#include "config.h"
#include "ImuSample.h"
#include "LowPass.h"

class ImuReader {
public:
    ImuReader() : imu_(I2C_MODE, 0x6A) {}

    void begin() {
        imu_.settings.accelRange      = cfg::ACCEL_RANGE_G;
        imu_.settings.accelSampleRate = cfg::ACCEL_ODR_HZ;
        imu_.settings.accelBandWidth  = cfg::ACCEL_BW_HZ;
        imu_.settings.gyroRange       = cfg::GYRO_RANGE_DPS;
        imu_.settings.gyroSampleRate  = cfg::GYRO_ODR_HZ;
        imu_.begin();
    }

    ImuSample read(float dt) {
        const float rawX = imu_.readFloatGyroX();
        const float rawY = imu_.readFloatGyroY();
        const float rawZ = imu_.readFloatGyroZ();

        ImuSample s;
        s.ax = imu_.readFloatAccelX();
        s.ay = imu_.readFloatAccelY();
        s.az = imu_.readFloatAccelZ();

        // Erdbeschleunigung schaetzen und abziehen. Eine gehaltene Haltung
        // aendert sich im Bereich unter 1 Hz, die Beschleunigung beim Zeigen
        // und beim Pinchen deutlich darueber - 0.8 Hz trennt beides. LowPass
        // setzt sich beim ersten Sample auf den Eingang, es gibt also keinen
        // Einschwinger beim Start.
        s.lax = s.ax - lpGx_.run(s.ax, dt);
        s.lay = s.ay - lpGy_.run(s.ay, dt);
        s.laz = s.az - lpGz_.run(s.az, dt);

        s.gx = rawX - bx_;
        s.gy = rawY - by_;
        s.gz = rawZ - bz_;

        s.accMag  = sqrtf(s.ax*s.ax + s.ay*s.ay + s.az*s.az);
        s.gyroSum = fabsf(s.gx) + fabsf(s.gy) + fabsf(s.gz);

        // Ein MEMS-Gyroskop zeigt auch im Stillstand nicht exakt null, und der
        // Fehler wandert mit der Temperatur. Ein Tiefpass entfernt ihn nicht -
        // er ist ja keine Schwankung, sondern ein Versatz. Deshalb wird er
        // gelernt, solange das Geraet ruhig liegt, und danach abgezogen. Ohne
        // das wandert der Cursor von allein.
        if (s.gyroSum < cfg::BIAS_STILL_DPS &&
            fabsf(s.accMag - 1.f) < cfg::BIAS_ACC_TOL) {
            const float a = 1.f - expf(-dt / cfg::BIAS_TAU);
            bx_ += a * (rawX - bx_);
            by_ += a * (rawY - by_);
            bz_ += a * (rawZ - bz_);
        }
        return s;
    }

private:
    LSM6DS3 imu_;
    float   bx_ = 0.f, by_ = 0.f, bz_ = 0.f;
    LowPass lpGx_{cfg::GRAVITY_LP_HZ}, lpGy_{cfg::GRAVITY_LP_HZ}, lpGz_{cfg::GRAVITY_LP_HZ};
};
