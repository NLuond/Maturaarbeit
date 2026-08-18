#pragma once
#include <LSM6DS3.h>
#include <Wire.h>
#include <math.h>
#include "config.h"
#include "ImuSample.h"
#include "LowPass.h"

enum class ImuRate : uint8_t { Active, Ready, Sleep };

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

        // Muss nach imu_.begin() stehen: dort laeuft Wire.begin() und setzt die
        // Taktrate auf die Arduino-Vorgabe zurueck.
        Wire.setClock(cfg::I2C_CLOCK_HZ);
    }

    ImuSample read(float dt) {
        // Ein Burst statt sechs Einzeltransaktionen: ab OUTX_L_G folgen luecklos
        // Gyro X/Y/Z und Accel X/Y/Z, alle aus demselben Abtastzeitpunkt (bei
        // Einzelzugriffen lagen rund 2 ms dazwischen). Initialisiert, damit eine
        // fehlgeschlagene Transaktion ein erkennbar falsches Sample liefert.
        uint8_t raw[12] = {0};
        imu_.readRegisterRegion(raw, LSM6DS3_ACC_GYRO_OUTX_L_G, 12);

        const float rawX = imu_.calcGyro(readS16(raw, 0));
        const float rawY = imu_.calcGyro(readS16(raw, 2));
        const float rawZ = imu_.calcGyro(readS16(raw, 4));

        ImuSample s;
        s.ax = imu_.calcAccel(readS16(raw,  6));
        s.ay = imu_.calcAccel(readS16(raw,  8));
        s.az = imu_.calcAccel(readS16(raw, 10));

        // Erdbeschleunigung schaetzen und abziehen: eine gehaltene Haltung
        // aendert sich unter 1 Hz, die Bewegung beim Zeigen und Pinchen darueber.
        s.lax = s.ax - lpGx_.run(s.ax, dt);
        s.lay = s.ay - lpGy_.run(s.ay, dt);
        s.laz = s.az - lpGz_.run(s.az, dt);

        s.gx = rawX - bx_;
        s.gy = rawY - by_;
        s.gz = rawZ - bz_;

        s.accMag  = sqrtf(s.ax*s.ax + s.ay*s.ay + s.az*s.az);
        s.gyroSum = fabsf(s.gx) + fabsf(s.gy) + fabsf(s.gz);

        updateBias(s, rawX, rawY, rawZ, dt);
        return s;
    }

    // Nur das ODR-Nibble aendern; im Rest der Register stehen Messbereich und
    // Bandbreite aus begin().
    void setRate(ImuRate r) {
        uint8_t odrXl = 0x50;   // 208 Hz
        uint8_t odrG  = 0x50;
        if (r == ImuRate::Ready) { odrXl = 0x30; odrG = 0x30; }   // 52 Hz
        // Im Schlaf laeuft nur der Beschleunigungssensor, und der nur fuer die
        // Wake-Up-Funktion. Das Gyroskop ist der groessere Verbraucher.
        if (r == ImuRate::Sleep)  { odrXl = 0x20; odrG = 0x00; }   // 26 Hz / aus

        setOdr(LSM6DS3_ACC_GYRO_CTRL1_XL, odrXl);
        setOdr(LSM6DS3_ACC_GYRO_CTRL2_G,  odrG);
    }

    // Weckt ueber INT1, sobald die Beschleunigung die Schwelle ueberschreitet.
    // TAP_CFG1 Bit 7 gibt die einfachen Interrupts frei, Bit 0 ist LIR: INT1
    // bleibt stehen, bis WAKE_UP_SRC gelesen wird. Ohne die Verriegelung ginge
    // eine Flanke zwischen attachInterrupt() und suspendLoop() verloren.
    void enableWakeOnMotion() {
        imu_.writeRegister(LSM6DS3_ACC_GYRO_WAKE_UP_DUR, 0x00);
        imu_.writeRegister(LSM6DS3_ACC_GYRO_WAKE_UP_THS, cfg::WAKE_UP_THRESHOLD);
        imu_.writeRegister(LSM6DS3_ACC_GYRO_MD1_CFG,     0x20);   // INT1_WU
        imu_.writeRegister(LSM6DS3_ACC_GYRO_TAP_CFG1,    0x81);   // frei + LIR
    }

    void disableWakeOnMotion() {
        // Reihenfolge traegt: erst INT1 kappen, dann WAKE_UP_SRC lesen, um die
        // Verriegelung zu loesen. Umgekehrt koennte dazwischen ein neues
        // Ereignis den Pegel erneut setzen und stehen lassen.
        imu_.writeRegister(LSM6DS3_ACC_GYRO_MD1_CFG,  0x00);
        imu_.writeRegister(LSM6DS3_ACC_GYRO_TAP_CFG1, 0x00);
        uint8_t dummy = 0;
        imu_.readRegister(&dummy, LSM6DS3_ACC_GYRO_WAKE_UP_SRC);
        (void)dummy;
    }

private:
    static int16_t readS16(const uint8_t* raw, int i) {
        return (int16_t)((uint16_t)raw[i + 1] << 8 | raw[i]);
    }

    // Der Nullpunkt eines MEMS-Gyroskops ist ein Versatz, kein Rauschen - ein
    // Tiefpass entfernt ihn nicht. Also im Stillstand lernen und abziehen, sonst
    // wandert der Cursor von allein. Die zweite Bedingung schliesst eine
    // gleichfoermige Drehung aus: ein ruhendes Board misst genau 1 g.
    void updateBias(const ImuSample& s, float rawX, float rawY, float rawZ, float dt) {
        if (s.gyroSum >= cfg::BIAS_STILL_DPS) return;
        if (fabsf(s.accMag - 1.f) >= cfg::BIAS_ACC_TOL) return;

        const float a = 1.f - expf(-dt / cfg::BIAS_TAU);
        bx_ += a * (rawX - bx_);
        by_ += a * (rawY - by_);
        bz_ += a * (rawZ - bz_);
    }

    void setOdr(uint8_t reg, uint8_t odrBits) {
        uint8_t v = 0;
        imu_.readRegister(&v, reg);
        imu_.writeRegister(reg, (uint8_t)((v & 0x0F) | odrBits));
    }

    LSM6DS3 imu_;
    float   bx_ = 0.f, by_ = 0.f, bz_ = 0.f;
    LowPass lpGx_{cfg::GRAVITY_LP_HZ}, lpGy_{cfg::GRAVITY_LP_HZ}, lpGz_{cfg::GRAVITY_LP_HZ};
};
