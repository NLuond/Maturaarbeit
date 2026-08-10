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
#include "ScrollJoystick.h"
#include "TwistToggle.h"
#include "Haptic.h"
#include "LowPass.h"
#include "ArmOrientation.h"
#include "TwistGuard.h"
#include "Battery.h"
#include "SleepPolicy.h"

// Verdrahtet Sensorik, Erkenner und Zustandsautomat:
// Ereignisse einsammeln -> fsm_ fragen -> apply() ausfuehren.

// Die hardwarefreien Module halten ihre Zahlen in einem eigenen Tuning-Struct
// statt in cfg::. Dieselbe Groesse steht damit an zwei Orten, und ein Drift
// waere kein Compilerfehler, sondern stilles Fehlverhalten. Hier wird er einer.
// Bei jedem neuen Tuning-Feld erweitern.
constexpr TwistTuning  kTwistDefaults{};
constexpr PoseTuning   kPoseDefaults{};
constexpr PinchTuning  kPinchDefaults{};
constexpr ScrollTuning kScrollDefaults{};

static_assert(kTwistDefaults.onDeg == cfg::TURN_ON_DEG,
              "TwistToggle::onDeg und cfg::TURN_ON_DEG meinen dieselbe Schwelle");
static_assert(kTwistDefaults.backDeg < cfg::TURN_OFF_DEG &&
              cfg::TURN_OFF_DEG < cfg::TURN_ON_DEG,
              "Reihenfolge backDeg < TURN_OFF_DEG < TURN_ON_DEG verletzt");

static_assert(kPoseDefaults.twistNeutralDeg == cfg::TWIST_NEUTRAL_DEG &&
              kPoseDefaults.turnOnDeg       == cfg::TURN_ON_DEG       &&
              kPoseDefaults.turnOffDeg      == cfg::TURN_OFF_DEG      &&
              kPoseDefaults.levelMaxDeg     == cfg::LEVEL_MAX_DEG     &&
              kPoseDefaults.levelHystDeg    == cfg::LEVEL_HYST_DEG    &&
              kPoseDefaults.modeTau         == cfg::MODE_TAU          &&
              kPoseDefaults.rollCompTau     == cfg::ROLLCOMP_TAU      &&
              kPoseDefaults.dwellMs         == cfg::MODE_DWELL_MS     &&
              kPoseDefaults.stillDps        == cfg::POSE_STILL_DPS    &&
              kPoseDefaults.calmMs          == cfg::POSE_CALM_MS,
              "PoseTuning und die cfg::-Werte der Handhaltung sind auseinandergelaufen");

static_assert(kPinchDefaults.envOn        == cfg::ENV_ON           &&
              kPinchDefaults.envOff       == cfg::ENV_OFF          &&
              kPinchDefaults.gyroGuardDps == cfg::PINCH_GYRO_GUARD &&
              kPinchDefaults.debounceMs   == cfg::DEBOUNCE_MS      &&
              kPinchDefaults.freezeMaxMs  == cfg::FREEZE_MAX_MS,
              "PinchTuning und die cfg::-Werte der Klickerkennung sind auseinandergelaufen");

static_assert(kScrollDefaults.deadDeg    == cfg::SCROLL_DEAD_DEG    &&
              kScrollDefaults.gain       == cfg::SCROLL_GAIN        &&
              kScrollDefaults.maxHz      == cfg::SCROLL_MAX_HZ      &&
              kScrollDefaults.invert     == cfg::SCROLL_INVERT      &&
              kScrollDefaults.intervalMs == cfg::SCROLL_INTERVAL_MS,
              "ScrollTuning und die cfg::-Werte des Scroll-Joysticks sind auseinandergelaufen");

static_assert(kPinchDefaults.envOff < kPinchDefaults.envOn,
              "Bi-Level-Schwelle verkehrt herum: ENV_OFF muss unter ENV_ON liegen");

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
        updateAngles(s, dt);
        pose_.update(twist_, elev_, s.gyroSum, dt, now_ms);

        handleTwistGesture(env, now_ms);

        if (fsm_.on()) {
            // Haltung vor Pinch: sie entscheidet, welche Taste er ausloest.
            // Frisch gelesen, weil handleTwistGesture() sie gerade
            // zurueckgesetzt haben kann.
            apply(fsm_.onPose(pose_.pose()), now_ms);
            handlePinch(s, env, now_ms);
        }

        runPointer(s, dt, now_us, now_ms);
        runScroll(dt, now_ms);

        debug(s, env, now_us);
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
    ScrollJoystick     scroll_;
    TwistToggle        twistToggle_;
    Haptic             haptic_;
    TwistGuard         twistGuard_;
    Battery            battery_;
    SleepPolicy        sleep_;

    bool     clickPulse_ = false;
    float    twist_ = 0.f, elev_ = 0.f;
    float    twistGain_ = 1.f;
    float    accumX_ = 0.f, accumY_ = 0.f;
    uint32_t tMove_ = 0, tDbg_ = 0;
    uint16_t nMoveFail_ = 0;
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
        updateGravityEstimate(s, dt);
    }

    // --- Ereignisse -----------------------------------------------------

    void handleTwistGesture(float env, uint32_t now_ms) {
        // Eine Erschuetterung verbraucht die laufende Ausdrehung - sie war ein
        // Pinch, keine Schaltgeste. An der Schwelle und nicht am erkannten
        // Klick, damit ein vom Modell verpasster Pinch die Maus nicht abschaltet.
        if (fsm_.on() && env > cfg::ENV_ON) twistToggle_.cancel();

        // settling(): nach dem Aufwachen ist der Winkel noch nicht verlaesslich.
        const bool levelOk = pose_.level() && !sleep_.settling();
        switch (twistToggle_.tick(pose_.relTwistDeg(), levelOk, now_ms)) {
            case TwistEvent::Toggle: apply(fsm_.onPower(),     now_ms); break;
            case TwistEvent::Held:   apply(fsm_.onTwistHeld(), now_ms); break;
            case TwistEvent::None:   break;
        }
    }

    void handlePinch(const ImuSample& s, float env, uint32_t now_ms) {
        ml_.push(s, env);

        const bool pinched = pinch_.tick(env, s.gyroSum, now_ms,
                                         [this] { return ml_.ready() && ml_.isPinch(); });
        if (!pinched) return;

        // TwistToggle sieht den Arm koerperlich frueher als draussen als die
        // FSM-Haltung; 1 = ausgedreht, 2 = ausgedreht und verbraucht.
        const uint8_t tw = twistToggle_.state();
        const bool armOut = (tw == 1) || (tw == 2);
        apply(fsm_.onPinch(scroll_.inDeadzone(), armOut), now_ms);
    }

    // Die einzige Stelle, an der Aktionen des Automaten Wirkung entfalten.
    void apply(const Actions& a, uint32_t now_ms) {
        if (a.click)      { mouse_.click();      clickPulse_ = true; }
        if (a.rightClick) { mouse_.rightClick(); clickPulse_ = true; }

        if (a.resetPose)     pose_.reset();
        if (a.enterScroll)   scroll_.enter(elev_);
        if (a.resetPointer) { accumX_ = accumY_ = 0.f; pointer_.reset(); twistGuard_.reset(); }
        if (a.hapticPulses)  haptic_.trigger(now_ms, a.hapticPulses);

        syncImuRate();
    }

    // --- Laufende Taetigkeit --------------------------------------------

    void runPointer(const ImuSample& s, float dt, uint32_t now_us, uint32_t now_ms) {
        if (!fsm_.pointing()) { accumX_ = accumY_ = 0.f; return; }
        if (pinch_.inFreeze(now_ms)) return;

        float px, py;
        pointer_.update(s.gx, s.gz, pose_.relTwistSlowDeg(), elev_, dt, px, py);

        // Die Bremse greift auf die fertigen Pixel, damit der 1-Euro-Filter
        // waehrend einer Drehung eingeschwungen bleibt.
        accumX_ += px * twistGain_;
        accumY_ += py * twistGain_;

        if (now_us - tMove_ < cfg::MOVE_INTERVAL_US) return;
        tMove_ = now_us;
        sendAccumulatedMove();
    }

    // Ein Bericht traegt hoechstens 127 px je Achse; bei schneller Bewegung
    // laeuft mehr auf, deshalb mehrere Berichte im selben Takt.
    void sendAccumulatedMove() {
        for (int i = 0; i < cfg::MOVE_MAX_REPORTS; i++) {
            const int8_t mx = (int8_t)constrain(accumX_, -127.f, 127.f);
            const int8_t my = (int8_t)constrain(accumY_, -127.f, 127.f);
            if (!mx && !my) break;

            // Erst abziehen, wenn das Paket angenommen wurde - sonst geht
            // Bewegung bei voller Warteschlange verloren.
            if (mouse_.move(mx, my)) { accumX_ -= mx; accumY_ -= my; }
            else {
                // Nimmt die Gegenstelle laenger gar nichts an, liefe der
                // Rueckstau sonst minutenlang weiter und der Cursor schoesse
                // beim Verbinden quer ueber den Schirm.
                nMoveFail_++;
                accumX_ = constrain(accumX_, -cfg::MOVE_BACKLOG_MAX, cfg::MOVE_BACKLOG_MAX);
                accumY_ = constrain(accumY_, -cfg::MOVE_BACKLOG_MAX, cfg::MOVE_BACKLOG_MAX);
                break;
            }
        }
    }

    void runScroll(float dt, uint32_t now_ms) {
        if (!fsm_.scrolling()) return;
        const int8_t ticks = scroll_.update(elev_, dt, now_ms);
        if (ticks) mouse_.scroll(ticks);
    }

    // --- Betriebszustaende ----------------------------------------------

    void handleSleep(float gyroSum, uint32_t now_ms) {
        switch (sleep_.tick(fsm_.on(), gyroSum, now_ms)) {
            case SleepEvent::GoToSleep: prepareSleep(); break;
            case SleepEvent::Settled:   ahrs_.setBeta(cfg::MADGWICK_BETA); break;
            case SleepEvent::None:      break;
        }
    }

    void setImuRate() {
        imuRateActive_ = fsm_.on();
        imu_.setRate(imuRateActive_ ? ImuRate::Active : ImuRate::Ready);
    }

    // Nur beim Wechsel schreiben: ein I2C-Zugriff je Takt waere das Gegenteil
    // von sparsam.
    void syncImuRate() {
        if (fsm_.on() != imuRateActive_) setImuRate();
    }

    // Privat: Wer schlafen legen will, fragt wantsSleep() ab. Das eigentliche
    // suspendLoop() gehoert main.cpp, wo der Schleifentakt haengt.
    // Reihenfolge: erst der Funk, dann die IMU.
    void prepareSleep() {
        mouse_.radioOff();
        imu_.setRate(ImuRate::Sleep);
        imu_.enableWakeOnMotion();
    }

    // --- Teleplot -------------------------------------------------------

#if DEBUG_TELEPLOT
    LowPass lpX_{cfg::GRAVITY_LP_HZ}, lpY_{cfg::GRAVITY_LP_HZ}, lpZ_{cfg::GRAVITY_LP_HZ};
    float   gvx_ = 0.f, gvy_ = 0.f, gvz_ = 1.f;

    // Muss in jedem Takt laufen, nicht erst im gedrosselten debug(): ein
    // Tiefpass, der nur jedes n-te Sample sieht, hat eine andere Zeitkonstante.
    void updateGravityEstimate(const ImuSample& s, float dt) {
        gvx_ = lpX_.run(s.ax, dt);
        gvy_ = lpY_.run(s.ay, dt);
        gvz_ = lpZ_.run(s.az, dt);
    }

    // Winkel einer Achse ueber der Waagerechten.
    static float axisTiltDeg(float comp, float mag) {
        if (mag < 1e-3f) return 0.f;
        return asinf(constrain(comp / mag, -1.f, 1.f)) * 57.29578f;
    }
#else
    void updateGravityEstimate(const ImuSample&, float) {}
#endif

    // Eine Zeile ">name:wert" pro Kanal.
    void debug(const ImuSample& s, float env, uint32_t now_us) {
    #if DEBUG_TELEPLOT
        if (now_us - tDbg_ < cfg::DEBUG_INTERVAL_US) return;
        tDbg_ = now_us;

        // Immer. pose ist die Haltung im Automaten, dpose die des Detektors -
        // laufen sie auseinander, liegt der Fehler in der Uebergabe.
        // pose: 0 = Point, 1 = Idle, 2 = Turned.
        // tw:   0 = gerade, 1 = ausgedreht, 2 = verbraucht, 3 = Lockout.
        Serial.print(">on:");     Serial.println(fsm_.on() ? 1 : 0);
        Serial.print(">pose:");   Serial.println((int)fsm_.pose());
        Serial.print(">dpose:");  Serial.println((int)pose_.pose());
        Serial.print(">tw:");     Serial.println(twistToggle_.state());
        Serial.print(">vbat:");   Serial.println(battery_.volts(), 3);

    #if DEBUG_SET == DEBUG_ALL || DEBUG_SET == DEBUG_PINCH
        // Klick-Kette: env -> gate -> p_ml -> click. Steigt stattdessen nDeb
        // oder nGyro, wurde die Flanke erkannt und erst danach verworfen.
        Serial.print(">env:");    Serial.println(env, 4);
        Serial.print(">gate:");   Serial.println(pinch_.envGate() ? 1 : 0);
        Serial.print(">p_ml:");   Serial.println(ml_.score(), 3);
        Serial.print(">click:");  Serial.println(clickPulse_ ? 1 : 0);
        Serial.print(">gsum:");   Serial.println(s.gyroSum, 1);
        Serial.print(">nDeb:");   Serial.println(pinch_.blockedByDebounce());
        Serial.print(">nGyro:");  Serial.println(pinch_.blockedByGyro());
        Serial.print(">ei_err:"); Serial.println(ml_.error());
        Serial.print(">ei_us:");  Serial.println(ml_.lastUs());
    #endif

    #if DEBUG_SET == DEBUG_ALL || DEBUG_SET == DEBUG_POINT
        // Zeigen: rx/ry gegen gx/gz zeigt die Daempfung des Filters, accx den
        // Rueckstau, mvfail abgelehnte Pakete.
        Serial.print(">gx:");     Serial.println(s.gx, 2);
        Serial.print(">gy:");     Serial.println(s.gy, 2);
        Serial.print(">gz:");     Serial.println(s.gz, 2);
        Serial.print(">rx:");     Serial.println(pointer_.rateX(), 2);
        Serial.print(">ry:");     Serial.println(pointer_.rateY(), 2);
        Serial.print(">pacc:");   Serial.println(pointer_.accel(), 2);
        Serial.print(">accx:");   Serial.println(accumX_, 1);
        Serial.print(">mvfail:"); Serial.println(nMoveFail_);
        Serial.print(">twist:");  Serial.println(twist_, 1);
        Serial.print(">elev:");   Serial.println(elev_, 1);
        Serial.print(">rtwist:"); Serial.println(pose_.relTwistDeg(), 1);
        Serial.print(">level:");  Serial.println(pose_.level() ? 1 : 0);
        Serial.print(">srate:");  Serial.println(scroll_.rateHz(), 2);
        Serial.print(">tg:");     Serial.println(twistGain_, 2);
        Serial.print(">tgr:");    Serial.println(twistGuard_.rateDps(), 1);
    #endif

    // Nicht Teil von DEBUG_ALL: teilt gx/gy/gz mit DEBUG_POINT.
    #if DEBUG_SET == DEBUG_ORIENT
        // Einbaulage nachpruefen: flach az = +1, um 90 Grad verdreht ax = +1.
        // twist/elev muessen sich mit angX/angZ bzw. angY decken.
        const float amag = sqrtf(gvx_*gvx_ + gvy_*gvy_ + gvz_*gvz_);
        Serial.print(">ax:");     Serial.println(s.ax, 3);
        Serial.print(">ay:");     Serial.println(s.ay, 3);
        Serial.print(">az:");     Serial.println(s.az, 3);
        Serial.print(">amraw:");  Serial.println(s.accMag, 3);
        Serial.print(">gvx:");    Serial.println(gvx_, 3);
        Serial.print(">gvy:");    Serial.println(gvy_, 3);
        Serial.print(">gvz:");    Serial.println(gvz_, 3);
        Serial.print(">amag:");   Serial.println(amag, 3);
        Serial.print(">angX:");   Serial.println(axisTiltDeg(gvx_, amag), 1);
        Serial.print(">angY:");   Serial.println(axisTiltDeg(gvy_, amag), 1);
        Serial.print(">angZ:");   Serial.println(axisTiltDeg(gvz_, amag), 1);
        Serial.print(">gx:");     Serial.println(s.gx, 1);
        Serial.print(">gy:");     Serial.println(s.gy, 1);
        Serial.print(">gz:");     Serial.println(s.gz, 1);
        Serial.print(">twist:");  Serial.println(twist_, 1);
        Serial.print(">elev:");   Serial.println(elev_, 1);
        Serial.print(">rtwist:"); Serial.println(pose_.relTwistDeg(), 1);
        Serial.print(">level:");  Serial.println(pose_.level() ? 1 : 0);
    #endif

        clickPulse_ = false;
    #else
        (void)s; (void)env; (void)now_us;
    #endif
    }
};
