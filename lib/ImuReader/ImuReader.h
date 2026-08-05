#pragma once
#include <LSM6DS3.h>
#include <Wire.h>
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

        // Muss NACH imu_.begin() stehen: dort laeuft Wire.begin(), und das
        // setzt die Taktrate auf die Arduino-Vorgabe von 100 kHz zurueck.
        // Der LSM6DS3 kann 400 kHz - bei sechs Werten je Takt ist das der
        // Unterschied zwischen rund 2 ms und rund 0.5 ms Schleifenzeit.
        Wire.setClock(400000);
    }

    ImuSample read(float dt) {
        // Ein Burst statt sechs Einzeltransaktionen. Ab OUTX_L_G (0x22) folgen
        // luecklos Gyro X/Y/Z und Accel X/Y/Z, je zwei Bytes little-endian -
        // 0x28 (OUTX_L_XL) liegt genau hinter dem Gyro-Block. Neben der
        // gesparten Zeit hat das einen zweiten Vorteil: alle sechs Werte
        // stammen aus demselben Abtastzeitpunkt. Bei Einzelzugriffen lagen
        // zwischen dem ersten und dem letzten rund 2 ms, in denen sich die
        // Hand weiterbewegt hat.
        uint8_t raw[12];
        imu_.readRegisterRegion(raw, LSM6DS3_ACC_GYRO_OUTX_L_G, 12);

        const int16_t gxi = (int16_t)((uint16_t)raw[1]  << 8 | raw[0]);
        const int16_t gyi = (int16_t)((uint16_t)raw[3]  << 8 | raw[2]);
        const int16_t gzi = (int16_t)((uint16_t)raw[5]  << 8 | raw[4]);
        const int16_t axi = (int16_t)((uint16_t)raw[7]  << 8 | raw[6]);
        const int16_t ayi = (int16_t)((uint16_t)raw[9]  << 8 | raw[8]);
        const int16_t azi = (int16_t)((uint16_t)raw[11] << 8 | raw[10]);

        const float rawX = imu_.calcGyro(gxi);
        const float rawY = imu_.calcGyro(gyi);
        const float rawZ = imu_.calcGyro(gzi);

        ImuSample s;
        s.ax = imu_.calcAccel(axi);
        s.ay = imu_.calcAccel(ayi);
        s.az = imu_.calcAccel(azi);

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
