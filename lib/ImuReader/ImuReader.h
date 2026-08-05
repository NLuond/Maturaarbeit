#pragma once
#include <LSM6DS3.h>
#include <math.h>
#include "config.h"
#include "ImuSample.h"

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
        if (s.gyroSum < cfg::BIAS_STILL_DPS) {
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
};
