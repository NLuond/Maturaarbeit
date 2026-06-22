#pragma once
#include <LSM6DS3.h>
#include <math.h>
#include "config.h"

struct ImuSample {
    float ax, ay, az, gx, gy, gz;
    float accMag;
    float gyroSum;   // |gx|+|gy|+|gz|
};

class ImuReader {
public:
    ImuReader() : imu(I2C_MODE, 0x6A) {}
    void begin() {
        imu.settings.accelRange      = cfg::ACCEL_RANGE_G;
        imu.settings.accelSampleRate = cfg::ACCEL_ODR_HZ;
        imu.settings.accelBandWidth  = cfg::ACCEL_BW_HZ;
        imu.settings.gyroRange       = cfg::GYRO_RANGE_DPS;
        imu.settings.gyroSampleRate  = cfg::GYRO_ODR_HZ;
        imu.begin();
    }
    ImuSample read() {
        ImuSample s;
        s.ax = imu.readFloatAccelX(); s.ay = imu.readFloatAccelY(); s.az = imu.readFloatAccelZ();
        s.gx = imu.readFloatGyroX();  s.gy = imu.readFloatGyroY();  s.gz = imu.readFloatGyroZ();
        s.accMag  = sqrtf(s.ax*s.ax + s.ay*s.ay + s.az*s.az);
        s.gyroSum = fabsf(s.gx) + fabsf(s.gy) + fabsf(s.gz);
        return s;
    }
private:
    LSM6DS3 imu;
};