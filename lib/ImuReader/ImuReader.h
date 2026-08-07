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

        // Muss NACH imu_.begin() stehen: dort laeuft Wire.begin() und setzt
        // die Taktrate zurueck. Begruendung des Werts bei cfg::I2C_CLOCK_HZ.
        Wire.setClock(cfg::I2C_CLOCK_HZ);
    }

    ImuSample read(float dt) {
        // Ein Burst statt sechs Einzeltransaktionen. Ab OUTX_L_G (0x22) folgen
        // luecklos Gyro X/Y/Z und Accel X/Y/Z, je zwei Bytes little-endian -
        // 0x28 (OUTX_L_XL) liegt genau hinter dem Gyro-Block. Neben der
        // gesparten Zeit hat das einen zweiten Vorteil: alle sechs Werte
        // stammen aus demselben Abtastzeitpunkt. Bei Einzelzugriffen lagen
        // zwischen dem ersten und dem letzten rund 2 ms, in denen sich die
        // Hand weiterbewegt hat.
        // Initialisiert, weil der Rueckgabestatus von readRegisterRegion()
        // verworfen wird: schlaegt die Bus-Transaktion fehl, blieben sonst
        // undefinierte Werte stehen statt eines erkennbar falschen Samples
        // (alles null). Bei 400 kHz ist eine fehlgeschlagene Transaktion
        // wahrscheinlicher als bei den zuvor genutzten 100 kHz.
        uint8_t raw[12] = {0};
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

    // Nur das ODR-Nibble wird veraendert, der Rest der Register bleibt
    // stehen: dort sitzen Messbereich und Bandbreite, die begin() gesetzt
    // hat und die sich nicht mit der Rate aendern sollen.
    void setRate(ImuRate r) {
        uint8_t odrXl = 0x50;   // 208 Hz
        uint8_t odrG  = 0x50;   // 208 Hz
        if (r == ImuRate::Ready) { odrXl = 0x30; odrG = 0x30; }   // 52 Hz
        // Im Schlaf laeuft nur der Beschleunigungssensor weiter, und der
        // nur fuer die Wake-Up-Funktion. Das Gyroskop ist der groessere
        // Verbraucher der beiden und wird zum Wecken nicht gebraucht.
        if (r == ImuRate::Sleep)  { odrXl = 0x20; odrG = 0x00; }   // 26 Hz / aus

        setOdr(LSM6DS3_ACC_GYRO_CTRL1_XL, odrXl);
        setOdr(LSM6DS3_ACC_GYRO_CTRL2_G,  odrG);
    }

    // Weckt ueber INT1, sobald sich die Beschleunigung um mehr als die
    // Schwelle aendert. TAP_CFG1 Bit 7 gibt die einfachen Interrupts
    // ueberhaupt erst frei; ohne dieses Bit bleibt INT1 stumm.
    void enableWakeOnMotion() {
        imu_.writeRegister(LSM6DS3_ACC_GYRO_WAKE_UP_DUR, 0x00);
        imu_.writeRegister(LSM6DS3_ACC_GYRO_WAKE_UP_THS, cfg::WAKE_UP_THRESHOLD);
        imu_.writeRegister(LSM6DS3_ACC_GYRO_MD1_CFG,     0x20);   // INT1_WU
        // Bit 0 ist LIR: INT1 bleibt stehen, bis WAKE_UP_SRC gelesen wird.
        // Ohne die Verriegelung gibt es nur eine kurze Flanke, und faellt die
        // zwischen attachInterrupt() und suspendLoop(), ist sie verloren -
        // vTaskResume zaehlt nicht. main.cpp prueft den Pegel deshalb vor dem
        // Suspend, und das setzt einen stehenden Pegel voraus.
        imu_.writeRegister(LSM6DS3_ACC_GYRO_TAP_CFG1,    0x81);   // Interrupts frei + LIR
    }

    void disableWakeOnMotion() {
        // Die Reihenfolge traegt seit LIR: erst die Wegleitung auf INT1
        // kappen und die Interrupts sperren, danach WAKE_UP_SRC lesen, um die
        // Verriegelung zu loesen. Umgekehrt koennte zwischen dem Lesen und dem
        // Abschalten ein neues Ereignis den Pegel erneut setzen und stehen
        // lassen - INT1 laege dann dauerhaft hoch, und die naechste
        // Pegelpruefung vor dem Schlafenlegen saehe ein Ereignis, das keines
        // ist.
        imu_.writeRegister(LSM6DS3_ACC_GYRO_MD1_CFG,  0x00);
        imu_.writeRegister(LSM6DS3_ACC_GYRO_TAP_CFG1, 0x00);
        // Die Quelle einmal lesen, damit ein noch anstehendes Ereignis
        // geloescht ist und INT1 nicht gleich wieder ausloest.
        uint8_t dummy = 0;
        imu_.readRegister(&dummy, LSM6DS3_ACC_GYRO_WAKE_UP_SRC);
        (void)dummy;
    }

private:
    void setOdr(uint8_t reg, uint8_t odrBits) {
        uint8_t v = 0;
        imu_.readRegister(&v, reg);
        imu_.writeRegister(reg, (uint8_t)((v & 0x0F) | odrBits));
    }

    LSM6DS3 imu_;
    float   bx_ = 0.f, by_ = 0.f, bz_ = 0.f;
    LowPass lpGx_{cfg::GRAVITY_LP_HZ}, lpGy_{cfg::GRAVITY_LP_HZ}, lpGz_{cfg::GRAVITY_LP_HZ};
};
