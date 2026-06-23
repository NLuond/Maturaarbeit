#pragma once
#define USE_ML_PINCH true
#define DEBUG_TELEPLOT true
#define USE_BLE_HID true
#define COLLECT_MODE false
namespace cfg {
    constexpr int   ACCEL_RANGE_G   = 4;
    constexpr int   ACCEL_ODR_HZ    = 208;
    constexpr int   ACCEL_BW_HZ     = 100; 
    constexpr int   GYRO_RANGE_DPS  = 500;
    constexpr int   GYRO_ODR_HZ     = 208;

    // Pinch Vibration
    constexpr float HP_CUTOFF_HZ    = 30.f;
    constexpr float ENV_LP_HZ       = 15.f;

    // Pinch
    constexpr float    ENV_ABS      = 0.018f;
    constexpr float    PINCH_GYRO_GUARD = 100.f;
    constexpr uint32_t DEBOUNCE_MS  = 300;
    constexpr uint32_t DOUBLE_MS    = 350;
    constexpr uint32_t FREEZE_MS    = 120;

    // Pointing
    constexpr float SENS_X      = 50.0f;
    constexpr float SENS_Y      = 50.0f;
    constexpr float DEADZONE    = 5.0f;
    constexpr float ACCEL_K     = 1.0f;
    constexpr float ACCEL_MAX   = 3.0f;
    constexpr float SMOOTH_TAU  = 0.024f;
    constexpr float PITCH_LIMIT = 45.f;
    constexpr float PITCH_FADE  = 12.f;
    constexpr uint32_t MOVE_INTERVAL_US = 16000;

    // Madgwick
    constexpr float MADGWICK_BETA = 0.033f;

    // Shake Toggle
    constexpr float SHAKE_ON   = 350.f;
    constexpr float SHAKE_OFF  = 180.f;
    constexpr uint32_t SHAKE_REFRACT_MS = 80;
    constexpr uint32_t SHAKE_GAP_MIN_MS = 100;
    constexpr uint32_t SHAKE_GAP_MAX_MS = 450;
    constexpr uint32_t SHAKE_LOCKOUT_MS = 800;

    // ML-Classification
    constexpr float ML_CONFIDENCE = 0.5f;
    // Haptik (Vibrationsmotor, PWM)
    constexpr int      HAPTIC_PIN        = D1;
    constexpr uint8_t  HAPTIC_PEAK       = 150;
    constexpr uint32_t HAPTIC_ATTACK_MS  = 10;
    constexpr uint32_t HAPTIC_MS         = 45;
}