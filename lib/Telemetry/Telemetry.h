#pragma once
#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "ImuSample.h"
#include "LowPass.h"
#include "ArmOrientation.h"
#include "AirMouseState.h"
#include "PoseDetector.h"
#include "TwistToggle.h"
#include "TwistGuard.h"
#include "PinchDetector.h"
#include "PinchClassifier.h"
#include "MotionPipeline.h"
#include "OrientationPointer.h"
#include "Battery.h"
#include "MouseHID.h"

// Teleplot-Ausgabe, eine Zeile ">name:wert" je Kanal. Beobachtet die Module von
// aussen und greift nirgends ein - deshalb darf sie als einzige quer durch das
// ganze System lesen und haelt dafuer const-Referenzen auf alles.
//
// Bewusst kein #if im Rumpf: kEnabled und kSet sind Compile-Konstanten, der
// Optimierer wirft die abgeschalteten Zweige samt ihrer Serial-Aufrufe weg. So
// bleibt der Regelpfad im Controller frei von Debug-Verzweigungen.
class Telemetry {
public:
    Telemetry(const AirMouseState& fsm, const PoseDetector& pose,
              const TwistToggle& twist, const TwistGuard& guard,
              const PinchDetector& pinch, const PinchClassifier& ml,
              const MotionPipeline& motion, const OrientationPointer& pointer,
              const Battery& battery, const MouseHID& mouse)
        : fsm_(fsm), pose_(pose), twist_(twist), guard_(guard), pinch_(pinch),
          ml_(ml), motion_(motion), pointer_(pointer), battery_(battery),
          mouse_(mouse) {}

    // Jeden Takt, nicht erst im gedrosselten emit(): ein Tiefpass, der nur jedes
    // n-te Sample sieht, haette eine andere Zeitkonstante. envPeak haelt den
    // groessten Wert seit der letzten Ausgabe fest - die Schleife laeuft mit
    // 209 Hz, die Ausgabe mit 50 Hz, ein Impuls von rund 10 ms waere sonst nur
    // zufaellig auf seinem Scheitel getroffen.
    void update(const ImuSample& s, float env, float dt) {
        if (!kEnabled) return;
        if (env > envPeak_) envPeak_ = env;
        gvx_ = lpX_.run(s.ax, dt);
        gvy_ = lpY_.run(s.ay, dt);
        gvz_ = lpZ_.run(s.az, dt);
    }

    // Ein Klick latcht bis zur naechsten Ausgabe; zwei Klicks darin trennt nur
    // der Zaehler.
    void onClick() {
        if (!kEnabled) return;
        clicked_ = true;
        nClick_++;
    }

    void emit(const ImuSample& s, float env, uint32_t now_us,
              float twistDeg, float elevDeg, float twistGain) {
        if (!kEnabled) return;
        if (now_us - tLast_ < cfg::DEBUG_INTERVAL_US) return;
        tLast_ = now_us;

        emitAlways();
        if (kSet == DEBUG_ALL || kSet == DEBUG_PINCH) emitPinch(s, env);
        if (kSet == DEBUG_ALL || kSet == DEBUG_POINT) emitPointing(s, twistDeg, elevDeg, twistGain);
        if (kSet == DEBUG_ENV)                        emitEnvelope(env);
        if (kSet == DEBUG_ORIENT)                     emitOrientation(s, twistDeg, elevDeg);

        clicked_ = false;
        envPeak_ = 0.f;   // erst nach allen Gruppen, sie lesen ihn alle
    }

private:
    static constexpr bool kEnabled = DEBUG_TELEPLOT;
    static constexpr int  kSet     = DEBUG_SET;

    const AirMouseState&      fsm_;
    const PoseDetector&       pose_;
    const TwistToggle&        twist_;
    const TwistGuard&         guard_;
    const PinchDetector&      pinch_;
    const PinchClassifier&    ml_;
    const MotionPipeline&     motion_;
    const OrientationPointer& pointer_;
    const Battery&            battery_;
    const MouseHID&           mouse_;

    LowPass  lpX_{cfg::GRAVITY_LP_HZ}, lpY_{cfg::GRAVITY_LP_HZ}, lpZ_{cfg::GRAVITY_LP_HZ};
    float    gvx_ = 0.f, gvy_ = 0.f, gvz_ = 1.f;
    float    envPeak_ = 0.f;
    uint32_t tLast_ = 0;
    uint16_t nClick_ = 0;
    bool     clicked_ = false;

    static void ch(const char* name, long v) {
        Serial.print('>'); Serial.print(name); Serial.print(':'); Serial.println(v);
    }
    static void ch(const char* name, float v, int digits) {
        Serial.print('>'); Serial.print(name); Serial.print(':'); Serial.println(v, digits);
    }

    // pose ist die Haltung im Automaten, dpose die des Detektors - laufen sie
    // auseinander, liegt der Fehler in der Uebergabe.
    // pose: 0 = Point, 1 = Idle, 2 = Turned.
    // tw:   0 = Ruhe, 1 = ausgedreht, 2 = Rueckweg, 3 = Lockout, 4 = verbraucht.
    //
    // nTw*: warum eine Ausdrehung nicht geschaltet hat - Erschuetterung ueber
    // TWIST_CANCEL_ENV, Arm ausserhalb LEVEL_MAX_DEG, zu spaet zurueck. nTwist
    // sind Klicks, die der Dreh-Guard verworfen hat. Alle vier scheitern sonst
    // lautlos und sehen wie Unzuverlaessigkeit aus.
    //
    // ble: 1 = verbunden, 0 = wirbt. blerr sagt, warum nichts geht, bitweise:
    // 1 = Bluefruit.begin(), 2 = Device Information, 4 = HID, 8 = Advertising.
    // nconn trennt "wirbt nicht" von "ist verbunden" - beides setzt in der
    // Bibliothek dasselbe Flag zurueck. ci ist die eigentliche Taktgrenze.
    void emitAlways() {
        ch("on",      fsm_.on() ? 1 : 0);
        ch("pose",    (long)fsm_.pose());
        ch("dpose",   (long)pose_.pose());
        ch("tw",      twist_.state());
        ch("nClick",  nClick_);
        ch("nTwCan",  twist_.rejectedByCancel());
        ch("nTwLvl",  twist_.rejectedByLevel());
        ch("nTwSlow", twist_.rejectedByTime());
        ch("nTwist",  pinch_.blockedByTwist());
        ch("vbat",    battery_.volts(), 3);
        ch("ble",     mouse_.connected() ? 1 : 0);
        ch("adv",     mouse_.advertising() ? 1 : 0);
        ch("blerr",   mouse_.initError());
        ch("nconn",   mouse_.connCount());
        ch("nadv",    mouse_.advRestarts());
        ch("ci",      mouse_.connIntervalUs() / 1000.f, 2);
    }

    // Klick-Kette: env -> gate -> arm -> p_ml -> click. Steigt stattdessen nDeb
    // oder nGyro, wurde die Flanke erkannt und erst danach verworfen. arm laeuft
    // absichtlich laenger als gate: das ML-Fenster braucht Zeit, den Impuls
    // aufzunehmen.
    void emitPinch(const ImuSample& s, float env) {
        ch("env",    env, 4);
        ch("gate",   pinch_.envGate() ? 1 : 0);
        ch("arm",    pinch_.armed() ? 1 : 0);
        ch("p_ml",   ml_.score(), 3);
        ch("click",  clicked_ ? 1 : 0);
        ch("envMax", envPeak_, 4);
        ch("gsum",   s.gyroSum, 1);
        ch("nDeb",   pinch_.blockedByDebounce());
        ch("nGyro",  pinch_.blockedByGyro());
        ch("ei_err", ml_.error());
        ch("ei_us",  (long)ml_.lastUs());
    }

    // rx/ry gegen gx/gz zeigt die Daempfung des Filters, accx den Rueckstau,
    // mvfail abgelehnte Pakete. rtwist ist geglaettet (Haltung), rtwraw roh
    // (Geste) - ihre Differenz am Scheitel ist die Verzoegerung des Tiefpasses.
    void emitPointing(const ImuSample& s, float twistDeg, float elevDeg, float twistGain) {
        ch("gx",     s.gx, 2);
        ch("gy",     s.gy, 2);
        ch("gz",     s.gz, 2);
        ch("rx",     pointer_.rateX(), 2);
        ch("ry",     pointer_.rateY(), 2);
        ch("pacc",   pointer_.accel(), 2);
        ch("accx",   motion_.pendingX(), 1);
        ch("mvfail", motion_.rejected());
        ch("twist",  twistDeg, 1);
        ch("elev",   elevDeg, 1);
        ch("rtwist", pose_.relTwistDeg(), 1);
        ch("rtwraw", arm::relDeg(twistDeg, cfg::TWIST_NEUTRAL_DEG), 1);
        ch("level",  pose_.level() ? 1 : 0);
        ch("pgate",  pose_.poseGate() ? 1 : 0);
        ch("sacc",   motion_.pendingWheel(), 2);
        ch("tg",     twistGain, 2);
        ch("tgr",    guard_.rateDps(), 1);
    }

    void emitEnvelope(float env) {
        ch("env",    env, 4);
        ch("envMax", envPeak_, 4);
        ch("gate",   pinch_.envGate() ? 1 : 0);
        ch("click",  clicked_ ? 1 : 0);
    }

    // Einbaulage nachpruefen: flach az = +1, um 90 Grad verdreht ax = +1.
    // twist/elev muessen sich mit angX/angZ bzw. angY decken.
    void emitOrientation(const ImuSample& s, float twistDeg, float elevDeg) {
        const float amag = sqrtf(gvx_*gvx_ + gvy_*gvy_ + gvz_*gvz_);
        ch("ax",     s.ax, 3);
        ch("ay",     s.ay, 3);
        ch("az",     s.az, 3);
        ch("amraw",  s.accMag, 3);
        ch("gvx",    gvx_, 3);
        ch("gvy",    gvy_, 3);
        ch("gvz",    gvz_, 3);
        ch("amag",   amag, 3);
        ch("angX",   axisTiltDeg(gvx_, amag), 1);
        ch("angY",   axisTiltDeg(gvy_, amag), 1);
        ch("angZ",   axisTiltDeg(gvz_, amag), 1);
        ch("gx",     s.gx, 1);
        ch("gy",     s.gy, 1);
        ch("gz",     s.gz, 1);
        ch("twist",  twistDeg, 1);
        ch("elev",   elevDeg, 1);
        ch("rtwist", pose_.relTwistDeg(), 1);
        ch("level",  pose_.level() ? 1 : 0);
        ch("pgate",  pose_.poseGate() ? 1 : 0);
    }

    // Winkel einer Achse ueber der Waagerechten.
    static float axisTiltDeg(float comp, float mag) {
        if (mag < 1e-3f) return 0.f;
        return asinf(constrain(comp / mag, -1.f, 1.f)) * arm::R2D;
    }
};
