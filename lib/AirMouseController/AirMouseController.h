#pragma once
#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "ImuSample.h"
#include "ImuReader.h"
#include "MouseHID.h"
#include "MadgwickAHRS.h"
#include "VibrationEnvelope.h"
#include "PinchClassifier.h"
#include "PinchDetector.h"
#include "OrientationPointer.h"
#include "AirMouseState.h"
#include "PoseDetector.h"
#include "MotionPipeline.h"
#include "TwistToggle.h"
#include "Haptic.h"
#include "ArmOrientation.h"
#include "TwistGuard.h"
#include "Battery.h"
#include "SleepPolicy.h"
#include "Telemetry.h"

// Verdrahtet Sensorik, Erkenner und Zustandsautomat:
// Ereignisse einsammeln -> fsm_ fragen -> apply() ausfuehren.

// Die hardwarefreien Module halten dieselben Zahlen wie cfg:: in eigenen
// Tuning-Structs. Ein Auseinanderdriften waere sonst kein Compilerfehler,
// sondern stilles Fehlverhalten - bei jedem neuen Tuning-Feld erweitern.
constexpr TwistTuning  kTwistDefaults{};
constexpr PoseTuning   kPoseDefaults{};
constexpr PinchTuning  kPinchDefaults{};
constexpr ScrollTuning kScrollDefaults{};
constexpr MotionTuning kMotionDefaults{};

static_assert(kTwistDefaults.onDeg     == cfg::TURN_ON_DEG      &&
              kTwistDefaults.backDeg   == cfg::TWIST_BACK_DEG   &&
              kTwistDefaults.maxMs     == cfg::TWIST_MAX_MS     &&
              kTwistDefaults.lockoutMs == cfg::TWIST_LOCKOUT_MS &&
              kTwistDefaults.stillDps  == cfg::TWIST_STILL_DPS  &&
              kTwistDefaults.stillMs   == cfg::TWIST_STILL_MS,
              "TwistTuning und die cfg::-Werte der Ein/Aus-Geste sind auseinandergelaufen");
static_assert(kTwistDefaults.backDeg < cfg::TURN_OFF_DEG &&
              cfg::TURN_OFF_DEG < cfg::TURN_ON_DEG,
              "Reihenfolge backDeg < TURN_OFF_DEG < TURN_ON_DEG verletzt");

// Die Geste erschuettert das Board selbst; ihre Drehrate muss den Klick also
// sperren, bevor sie unter der Schwelle liegt, ab der eine Erschuetterung als
// Pinch zaehlen darf.
static_assert(cfg::TWIST_STILL_DPS <= cfg::PINCH_TWIST_GUARD,
              "Der Dreh-Guard des Klicks liegt unter der Ruheschwelle der Geste");

static_assert(kPoseDefaults.twistNeutralDeg == cfg::TWIST_NEUTRAL_DEG &&
              kPoseDefaults.turnOnDeg       == cfg::TURN_ON_DEG       &&
              kPoseDefaults.turnOffDeg      == cfg::TURN_OFF_DEG      &&
              kPoseDefaults.levelMaxDeg     == cfg::LEVEL_MAX_DEG     &&
              kPoseDefaults.levelHystDeg    == cfg::LEVEL_HYST_DEG    &&
              kPoseDefaults.poseUpMaxDeg    == cfg::POSE_UP_MAX_DEG   &&
              kPoseDefaults.poseDownMaxDeg  == cfg::POSE_DOWN_MAX_DEG &&
              kPoseDefaults.poseHoldMaxDeg  == cfg::POSE_HOLD_MAX_DEG &&
              kPoseDefaults.modeTau         == cfg::MODE_TAU          &&
              kPoseDefaults.rollCompTau     == cfg::ROLLCOMP_TAU      &&
              kPoseDefaults.dwellMs         == cfg::MODE_DWELL_MS     &&
              kPoseDefaults.stillDps        == cfg::POSE_STILL_DPS    &&
              kPoseDefaults.calmMs          == cfg::POSE_CALM_MS,
              "PoseTuning und die cfg::-Werte der Handhaltung sind auseinandergelaufen");

static_assert(kPinchDefaults.envOn         == cfg::ENV_ON            &&
              kPinchDefaults.envOff        == cfg::ENV_OFF           &&
              kPinchDefaults.gyroGuardDps  == cfg::PINCH_GYRO_GUARD  &&
              kPinchDefaults.twistGuardDps == cfg::PINCH_TWIST_GUARD &&
              kPinchDefaults.debounceMs    == cfg::DEBOUNCE_MS       &&
              kPinchDefaults.freezeMaxMs   == cfg::FREEZE_MAX_MS     &&
              kPinchDefaults.armMs         == cfg::PINCH_ARM_MS      &&
              kPinchDefaults.mlStride      == cfg::PINCH_ML_STRIDE,
              "PinchTuning und die cfg::-Werte der Klickerkennung sind auseinandergelaufen");

static_assert(kScrollDefaults.pxPerStep  == cfg::SCROLL_PX_PER_STEP  &&
              kScrollDefaults.maxPerTick == cfg::SCROLL_MAX_PER_TICK &&
              kScrollDefaults.invert     == cfg::SCROLL_INVERT       &&
              kScrollDefaults.intervalMs == cfg::SCROLL_INTERVAL_MS,
              "ScrollTuning und die cfg::-Werte des Scrollens sind auseinandergelaufen");

static_assert(kMotionDefaults.maxReports == cfg::MOVE_MAX_REPORTS &&
              kMotionDefaults.backlogMax == cfg::MOVE_BACKLOG_MAX,
              "MotionTuning und die cfg::-Werte der Bewegungsausgabe sind auseinandergelaufen");

static_assert(kPinchDefaults.envOff < kPinchDefaults.envOn,
              "Bi-Level-Schwelle verkehrt herum: ENV_OFF muss unter ENV_ON liegen");

// Das Armierungsfenster muss laenger sein als das ML-Fenster, sonst ist der
// Impuls beim letzten Aufruf noch nicht hindurchgelaufen. Nur mit Modell: ohne
// USE_ML_PINCH bringt PinchClassifier die EI-Konstanten gar nicht mit.
#if USE_ML_PINCH
static_assert(cfg::PINCH_ARM_MS * 1000.0 > EI_CLASSIFIER_INTERVAL_MS * 1000.0 *
                                           EI_CLASSIFIER_RAW_SAMPLE_COUNT,
              "PINCH_ARM_MS ist kuerzer als das ML-Fenster");
#endif

class AirMouseController {
public:
    AirMouseController(MouseHID& mouse, ImuReader& imu)
        : mouse_(mouse), imu_(imu), ahrs_(cfg::MADGWICK_BETA), pose_(poseTuning()) {}

    void begin() {
        haptic_.begin();
        battery_.begin();
        setImuRate();
    }

    void update(const ImuSample& s, float dt, uint32_t now_us) {
        // millis(), nicht now_us/1000: der Quotient liefe schon bei 4'294'967
        // ueber und machte jede Differenz hier ungueltig.
        const uint32_t now_ms = millis();

        haptic_.update(now_ms);
        battery_.update(now_ms);

        const float env = envelope_.update(s.accMag, dt);
        telemetry_.update(s, env, dt);
        updateAngles(s, dt);
        // scrolling() stammt aus dem Vortakt (onPose() laeuft erst danach); bei
        // 209 Hz ist der eine Takt Verzug ohne Belang.
        pose_.update(twist_, elev_, s.gyroSum, dt, now_ms, fsm_.scrolling());

        handleTwistGesture(env, now_ms);

        if (fsm_.on()) {
            // Haltung vor Pinch: sie entscheidet, welche Taste er ausloest.
            apply(fsm_.onPose(pose_.pose()), now_ms);
            handlePinch(s, env, now_ms);
        }

        runMotion(s, dt, now_us, now_ms);

        handleLink(now_ms);
        telemetry_.emit(s, env, now_us, twist_, elev_, twistGain_);
        handleSleep(s.gyroSum, now_ms);
    }

    bool wantsSleep() const { return sleep_.wantsSleep(); }

    // main.cpp richtet den Schleifentakt danach aus.
    bool wantsActiveRate() const { return fsm_.on(); }

    void onWake(uint32_t now_ms) {
        imu_.disableWakeOnMotion();
        // Aufgewacht heisst BEREIT, nicht AKTIV: die Einschalt-Geste kommt erst.
        setImuRate();
        mouse_.radioOn();
        ahrs_.setBeta(cfg::MADGWICK_BETA_FAST);
        sleep_.wake(now_ms);
    }

private:
    // USE_POSE_MODE ist ein #define, PoseDetector kennt config.h aber nicht.
    static PoseTuning poseTuning() {
        PoseTuning t;
        t.classify = USE_POSE_MODE;
        return t;
    }

    MouseHID&          mouse_;
    ImuReader&         imu_;
    MadgwickAHRS       ahrs_;
    VibrationEnvelope  envelope_;
    PinchClassifier    ml_;
    PinchDetector      pinch_;
    OrientationPointer pointer_;
    AirMouseState      fsm_;
    PoseDetector       pose_;
    MotionPipeline     motion_;
    TwistToggle        twistToggle_;
    Haptic             haptic_;
    TwistGuard         twistGuard_;
    Battery            battery_;
    SleepPolicy        sleep_;

    Telemetry          telemetry_{fsm_, pose_, twistToggle_, twistGuard_, pinch_,
                                  ml_, motion_, pointer_, battery_, mouse_};

    float    twist_ = 0.f, elev_ = 0.f;
    float    twistGain_ = 1.f;
    bool     imuRateActive_ = false;   // Startzustand ausgeschaltet, also BEREIT

    // --- Lage -----------------------------------------------------------

    // Einmal pro Takt ableiten und ueberall dasselbe benutzen.
    void updateAngles(const ImuSample& s, float dt) {
        ahrs_.update(s.gx, s.gy, s.gz, s.ax, s.ay, s.az, dt);
        twist_ = arm::twistDeg(ahrs_.upX(), ahrs_.upZ());
        elev_  = arm::elevDeg(ahrs_.upX(), ahrs_.upY(), ahrs_.upZ()) * cfg::ELEV_SIGN;
        // Auch wenn gerade nicht gezeigt wird: die Bremse leitet ihre Rate aus
        // der Differenz zum Vortakt ab, ein ausgelassener Takt waere ein Sprung.
        twistGain_ = twistGuard_.update(twist_, dt);
    }

    // --- Ereignisse -----------------------------------------------------

    void handleTwistGesture(float env, uint32_t now_ms) {
        // Nur melden, nicht selbst verwerfen: ob die Erschuetterung ein Pinch
        // war, entscheidet TwistToggle an der eigenen Drehrate - die Geste
        // erschuettert das Board am Scheitel selbst. An der Schwelle und nicht
        // am erkannten Klick, damit ein vom Modell verpasster Pinch nicht
        // abschaltet.
        if (fsm_.on() && env > cfg::TWIST_CANCEL_ENV) twistToggle_.reportShock(now_ms);

        // Der rohe Winkel, nicht der geglaettete: ein Tiefpass verzoegert eine
        // Rampe um seine Zeitkonstante, und die Geste scheiterte damit
        // ausgerechnet bei zuegiger Ausfuehrung. settling(): nach dem Aufwachen
        // ist der Winkel noch nicht verlaesslich.
        const bool levelOk = pose_.level() && !sleep_.settling();
        const float relRaw = arm::relDeg(twist_, cfg::TWIST_NEUTRAL_DEG);
        switch (twistToggle_.tick(relRaw, twistGuard_.rateDps(), levelOk, now_ms)) {
            case TwistEvent::Toggle: apply(fsm_.onPower(), now_ms); break;
            case TwistEvent::None:   break;
        }
    }

    void handlePinch(const ImuSample& s, float env, uint32_t now_ms) {
        ml_.push(s, env);

        const bool pinched = pinch_.tick(env, s.gyroSum, twistGuard_.rateDps(), now_ms,
                                         [this] { return ml_.ready() && ml_.isPinch(); },
                                         fsm_.scrolling());
        if (!pinched) return;

        // Der geglaettete Winkel ueberschreitet TURN_ON_DEG frueher, als die
        // FSM-Haltung nachzieht - genau dieses Fenster deckt armOut ab. Nicht
        // twistToggle_.state(): das Modul liest den rohen Winkel, dessen
        // Zittern wuerde den Pinch unterdruecken.
        const bool armOut = fabsf(pose_.relTwistDeg()) > cfg::TURN_ON_DEG;
        apply(fsm_.onPinch(armOut), now_ms);
    }

    // Die einzige Stelle, an der Aktionen des Automaten Wirkung entfalten.
    void apply(const Actions& a, uint32_t now_ms) {
        if (a.click) { mouse_.click(); telemetry_.onClick(); }
        if (a.rightClick) {
            mouse_.rightClick();
            // Ohne die Sperre schliesst der Loese-Impuls des Fingers das eben
            // geoeffnete Kontextmenue sofort wieder.
            pinch_.holdOff(now_ms, cfg::RIGHT_CLICK_HOLDOFF_MS);
            // Gepincht wird im ausgedrehten Stand: das Zurueckdrehen danach ist
            // die Rueckkehr aus dem Rechtsklick und darf nicht abschalten. Ein
            // Pinch knapp unter TWIST_CANCEL_ENV meldet sich sonst nirgends.
            twistToggle_.reportShock(now_ms);
            telemetry_.onClick();
        }

        if (a.resetPose)     pose_.reset();
        if (a.resetPointer) { motion_.reset(); pointer_.reset(); twistGuard_.reset(); }
        if (a.hapticPulses) {
            const uint32_t ms = a.hapticLong ? cfg::HAPTIC_LONG_MS : cfg::HAPTIC_MS;
            haptic_.trigger(now_ms, a.hapticPulses, ms);
            // Der lange Puls dauert laenger als die Entprellung - ohne die
            // Sperre laese die Klickerkennung ihn als Pinch.
            if (a.hapticLong) pinch_.holdOff(now_ms, ms + cfg::HAPTIC_BLIND_MS);
        }

        syncImuRate();
    }

    // --- Laufende Taetigkeit --------------------------------------------

    // Zeigen und Scrollen ziehen ihre Bewegung aus derselben Quelle und
    // unterscheiden sich nur im Ziel - deshalb ist nur eine Bewegungsart zu
    // lernen.
    void runMotion(const ImuSample& s, float dt, uint32_t now_us, uint32_t now_ms) {
        if (!fsm_.on())              { motion_.reset(); return; }
        if (pinch_.inFreeze(now_ms)) return;

        float px, py;
        pointer_.update(s.gx, s.gz, pose_.relTwistSlowDeg(), elev_, dt, px, py);

        // Die Bremse greift auf die fertigen Pixel, damit der 1-Euro-Filter
        // waehrend einer Drehung eingeschwungen bleibt.
        px *= twistGain_;
        py *= twistGain_;

        motion_.run(motionTarget(), px, py, now_us, now_ms, moveIntervalUs(),
                    [this](int8_t dx, int8_t dy) { return mouse_.move(dx, dy); },
                    [this](int8_t ticks)         { mouse_.scroll(ticks); });
    }

    MotionTarget motionTarget() const {
        if (fsm_.scrolling()) return MotionTarget::Wheel;
        if (fsm_.pointing())  return MotionTarget::Cursor;
        return MotionTarget::None;
    }

    // Schneller als ein Bericht je Verbindungsintervall kommt ueber BLE nichts
    // durch; wer trotzdem sendet, fuellt die Warteschlange, und dann fallen auch
    // Tastenberichte heraus. Ueber USB liefert connIntervalUs() 0.
    uint32_t moveIntervalUs() const {
        const uint32_t ci = mouse_.connIntervalUs();
        return ci > cfg::MOVE_INTERVAL_US ? ci : cfg::MOVE_INTERVAL_US;
    }

    // --- Betriebszustaende ----------------------------------------------

    void handleSleep(float gyroSum, uint32_t now_ms) {
        switch (sleep_.tick(fsm_.on(), gyroSum, now_ms)) {
            case SleepEvent::GoToSleep: prepareSleep(); break;
            case SleepEvent::Settled:   ahrs_.setBeta(cfg::MADGWICK_BETA); break;
            case SleepEvent::None:      break;
        }
    }

    // Jeden Takt geprueft, damit kein Pfad das Advertising dauerhaft stehen
    // lassen kann; der Aufruf ist wirkungslos, sobald verbunden oder am Werben.
    void handleLink(uint32_t now_ms) { mouse_.ensureAdvertising(now_ms); }

    void setImuRate() {
        imuRateActive_ = fsm_.on();
        imu_.setRate(imuRateActive_ ? ImuRate::Active : ImuRate::Ready);
    }

    // Nur beim Wechsel schreiben: ein I2C-Zugriff je Takt waere das Gegenteil
    // von sparsam.
    void syncImuRate() {
        if (fsm_.on() != imuRateActive_) setImuRate();
    }

    // Privat: wer schlafen legen will, fragt wantsSleep() ab. Das eigentliche
    // suspendLoop() gehoert main.cpp, wo der Schleifentakt haengt.
    // Reihenfolge: erst der Funk, dann die IMU.
    void prepareSleep() {
        mouse_.radioOff();
        imu_.setRate(ImuRate::Sleep);
        imu_.enableWakeOnMotion();
    }
};
