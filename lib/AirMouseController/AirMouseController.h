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

// Bindeglied zwischen Sensorik und Zustandsautomat. Die Aufgabenteilung:
//
//   Erkenner (TwistToggle, PoseDetector, PinchDetector) machen aus
//   Messwerten Ereignisse.
//   AirMouseState entscheidet allein, was diese Ereignisse bedeuten, und
//   liefert die auszufuehrenden Aktionen zurueck.
//   Dieser Controller fuehrt sie aus und betreibt die laufende Taetigkeit
//   (Cursor bewegen, scrollen) passend zum Zustand.
//
// Kein Zustandsbit liegt ausserhalb von AirMouseState. Das war vorher anders -
// airmouseOn_ und lastMode_ lagen hier verstreut und mussten von Hand
// synchron gehalten werden.

// TwistToggle und PoseDetector entscheiden ueber dieselbe koerperliche
// Schwelle, halten ihre Werte aber getrennt: TwistTuning muss ohne config.h
// uebersetzbar bleiben, damit der PC-Test laeuft. Ein Auseinanderdriften gaebe
// keinen Compilerfehler, sondern eine Maus, die in der abgedrehten Haltung
// links klickt - deshalb hier festgenagelt.
constexpr TwistTuning kTwistDefaults{};
static_assert(kTwistDefaults.onDeg == cfg::TURN_ON_DEG,
              "TwistToggle::onDeg und cfg::TURN_ON_DEG meinen dieselbe Schwelle");
static_assert(kTwistDefaults.backDeg < cfg::TURN_OFF_DEG &&
              cfg::TURN_OFF_DEG < cfg::TURN_ON_DEG,
              "Reihenfolge backDeg < TURN_OFF_DEG < TURN_ON_DEG verletzt");

class AirMouseController {
public:
    AirMouseController(MouseHID& mouse, ImuReader& imu)
        : mouse_(mouse), imu_(imu), ahrs_(cfg::MADGWICK_BETA) {}

    void begin() {
        haptic_.begin();
        battery_.begin();
        // Der Sensor kommt aus ImuReader::begin() mit 208 Hz, der Automat
        // startet aber ausgeschaltet - also in BEREIT. Ohne diese Zeile liefen
        // die beiden bis zum ersten Einschalten auseinander.
        imu_.setRate(imuRateActive_ ? ImuRate::Active : ImuRate::Ready);
    }

    void update(const ImuSample& s, float dt, uint32_t now_us) {
        // millis() statt now_us/1000: micros() laeuft bei 2^32 ueber, der
        // Quotient daraus also schon bei 4'294'967 - und dann ist die
        // vorzeichenlose Differenzarithmetik ungueltig, auf der jeder
        // Zeitgeber hier beruht. SleepPolicy saehe in dem einen Takt eine
        // Luecke von rund 4.29e9 ms und legte das Geraet sofort schlafen,
        // TwistToggle koennte ein Held melden, PoseDetector eine wartende
        // Haltung augenblicklich uebernehmen.
        //
        // millis() kommt aus derselben Tickquelle wie micros() (FreeRTOS,
        // 1024 Hz), laeuft also mit ihr im Gleichtakt. Es springt erst beim
        // Ueberlauf des 32-Bit-Ticks zurueck, nach rund 48.5 Tagen. Auch das
        // ist kein sauberer 2^32-Ueberlauf - der Sprung liegt bei
        // 4'194'304'000 ms -, aber statt alle 71.58 Minuten nur noch alle
        // sieben Wochen.
        const uint32_t now_ms = millis();

        haptic_.update(now_ms);
        battery_.update(now_ms);
        ahrs_.update(s.gx, s.gy, s.gz, s.ax, s.ay, s.az, dt);
        // Laeuft auch in BEREIT, dort mit 52 Hz - also oberhalb der
        // Nyquist-Frequenz des eigenen 30-Hz-Hochpasses. Das ist wissentlich
        // hingenommen und harmlos: env wird ausschliesslich unter fsm_.on()
        // ausgewertet, und dort taktet die Schleife mit 209 Hz. Der
        // nachgeschaltete 15-Hz-Tiefpass ist nach dem Wechsel nach AKTIV
        // innerhalb von rund 30 ms (drei Zeitkonstanten) wieder eingelaufen,
        // lange bevor ein Pinch bewertet wird. Den Filter in BEREIT
        // anzuhalten waere also kein Gewinn, sondern nur eine Fallunterscheidung
        // mehr.
        const float env = envelope_.update(s.accMag, dt);

    #if DEBUG_TELEPLOT
        // Muss in jedem Takt laufen, nicht erst im gedrosselten debug():
        // ein Tiefpass, der nur jedes n-te Sample sieht, hat eine andere
        // Zeitkonstante als die eingestellte.
        gvx_ = lpX_.run(s.ax, dt);
        gvy_ = lpY_.run(s.ay, dt);
        gvz_ = lpZ_.run(s.az, dt);
    #endif

        // Einmal pro Takt aus der Lage ableiten und ueberall dasselbe benutzen.
        // Vorher zogen sich PoseDetector, ScrollJoystick und der Zeiger ihre
        // Winkel einzeln aus rollDeg()/pitchDeg() - und zwei davon den falschen.
        twist_ = arm::twistDeg(ahrs_.upX(), ahrs_.upZ());
        elev_  = arm::elevDeg(ahrs_.upX(), ahrs_.upY(), ahrs_.upZ()) * cfg::ELEV_SIGN;

        // Muss in jedem Takt laufen, auch wenn gerade nicht gezeigt wird:
        // die Bremse leitet ihre Rate aus der Differenz zum letzten Winkel ab,
        // und ein ausgelassener Takt waere ein Sprung.
        twistGain_ = twistGuard_.update(twist_, dt);

        // --- Ereignisse einsammeln und dem Automaten geben ---------------
        // Der Haltungs-Detektor laeuft immer, auch im Ruhezustand: seine
        // Glaettung ist beim Einschalten dann schon eingeschwungen statt bei
        // null, und die Drehgeste braucht den geglaetteten Winkel gerade dann,
        // wenn die Maus noch aus ist. Der Rueckgabewert wird nicht hier
        // festgehalten, sondern unten frisch ueber pose_.pose() gelesen -
        // siehe Kommentar dort.
        pose_.update(twist_, elev_, s.gyroSum, dt, now_ms);

        // Eine Erschuetterung ueber der env-Schwelle verbraucht die laufende
        // Ausdrehung: sie war ein Pinch und keine Schaltgeste. Absichtlich an
        // der Schwelle und nicht am erkannten Klick - verpasst der
        // Klassifikator den Pinch, wuerde das Zurueckdrehen sonst die Maus
        // abschalten statt rechtszuklicken. Nur im eingeschalteten Zustand:
        // dort laeuft der Pinch-Pfad, und eine Erschuetterung soll die
        // Einschalt-Geste nicht abbrechen.
        if (fsm_.on() && env > cfg::ENV_ON) twistToggle_.cancel();

        // Waehrend des Einschwingens ist der Verdrehungswinkel noch nicht
        // verlaesslich. Ueber level = false verwirft TwistToggle eine
        // laufende Ausdrehung ohnehin - es braucht dafuer keinen neuen
        // Mechanismus im Modul.
        const bool levelOk = pose_.level() && !sleep_.settling();
        switch (twistToggle_.tick(pose_.relTwistDeg(), levelOk, now_ms)) {
            case TwistEvent::Toggle: apply(fsm_.onPower(),     now_ms); break;
            case TwistEvent::Held:   apply(fsm_.onTwistHeld(), now_ms); break;
            case TwistEvent::None:   break;
        }

        if (fsm_.on()) {
            // Haltung vor Pinch: welche Taste ein Pinch ausloest, haengt an der
            // Haltung, und die soll im selben Takt schon die aktuelle sein.
            // pose_.pose() statt eines oben gemerkten Werts: onPower() kann in
            // diesem Takt schon vor uns gelaufen sein (Zeile weiter oben) und
            // ueber apply()/a.resetPose die Haltung zurueckgesetzt haben - ein
            // gemerkter Wert waere dann fuer einen Takt falsch und liesse die
            // Maus kurz in der alten Haltung starten.
            apply(fsm_.onPose(pose_.pose()), now_ms);
            handlePinch(s, env, now_ms);
        }

        // --- Laufende Taetigkeit zum aktuellen Zustand -------------------
        if (fsm_.pointing()) {
            if (!pinch_.inFreeze(now_ms)) handlePointing(s, dt, now_us);
        } else {
            accumX_ = accumY_ = 0.f;
        }

        if (fsm_.scrolling()) {
            // Der Joystick zaehlt die gehaltene Armneigung, nicht die
            // Handverdrehung - die hat den Scroll-Modus ja gerade ausgewaehlt
            // und stuende waehrend des Scrollens konstant bei rund 90 Grad.
            const int8_t ticks = scroll_.update(elev_, dt, now_ms);
            if (ticks) mouse_.scroll(ticks);
        }

        debug(s, env, now_us);

        switch (sleep_.tick(fsm_.on(), s.gyroSum, now_ms)) {
            case SleepEvent::GoToSleep:
                // Nur vorbereiten. Das Schlafenlegen selbst gehoert
                // main.cpp - dort haengt der Takt dran, der danach neu
                // ausgerichtet werden muss.
                prepareSleep();
                break;
            case SleepEvent::Settled:
                ahrs_.setBeta(cfg::MADGWICK_BETA);
                break;
            case SleepEvent::None:
                break;
        }
    }

    bool wantsSleep() const { return sleep_.wantsSleep(); }

    void onWake(uint32_t now_ms) {
        imu_.disableWakeOnMotion();
        // Aufgewacht heisst BEREIT, nicht AKTIV: bewegt wurde der Arm, die
        // Einschalt-Drehgeste kommt erst noch. Die Rate folgt deshalb dem
        // Zustand des Automaten, und imuRateActive_ wird mitgefuehrt - sonst
        // liefe der Sensor mit 208 Hz weiter, waehrend die Schleife mit 52 Hz
        // taktet, und apply() saehe keinen Wechsel mehr.
        imuRateActive_ = fsm_.on();
        imu_.setRate(imuRateActive_ ? ImuRate::Active : ImuRate::Ready);
        mouse_.radioOn();
        // Erhoehtes Beta, damit die Lage nach dem Schlaf schnell wieder auf
        // die Schwerkraft einrastet. SleepEvent::Settled stellt es zurueck.
        ahrs_.setBeta(cfg::MADGWICK_BETA_FAST);
        sleep_.wake(now_ms);
    }

    // AKTIV laeuft mit voller Rate, BEREIT gedrosselt. main.cpp richtet den
    // Takt danach aus.
    bool activeRate() const { return fsm_.on(); }

private:
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
    uint32_t lastMove_ = 0, lastDbg_ = 0;
    uint16_t moveFail_ = 0;
    // Startzustand ist ausgeschaltet, also BEREIT.
    bool     imuRateActive_ = false;

#if DEBUG_TELEPLOT
    // Nur zur Achsen-Bestimmung: die geglaettete Erdbeschleunigung sagt
    // unabhaengig von Madgwick, wie das Board wirklich im Raum liegt.
    LowPass lpX_{cfg::GRAVITY_LP_HZ}, lpY_{cfg::GRAVITY_LP_HZ}, lpZ_{cfg::GRAVITY_LP_HZ};
    float   gvx_ = 0.f, gvy_ = 0.f, gvz_ = 1.f;

    // Winkel einer Achse ueber der Waagerechten. Liegt das Board still, zeigt
    // die Hochachse +-90 Grad und die beiden anderen ungefaehr null.
    static float axisTiltDeg(float comp, float mag) {
        if (mag < 1e-3f) return 0.f;
        return asinf(constrain(comp / mag, -1.f, 1.f)) * 57.29578f;
    }
#endif

    // Privat, obwohl es wie ein Bedienschritt aussieht: die einzige
    // Aufrufstelle ist SleepEvent::GoToSleep in update(). Diese Methode
    // stellt die IMU absichtlich auf ImuRate::Sleep und bricht damit die
    // dokumentierte Zusage, dass die Sensorrate dem Zustand des Automaten
    // folgt - von aussen gerufen liefe die Schleife mit 52 Hz weiter,
    // waehrend der Sensor nur noch 26 Hz liefert. Wer schlafen legen will,
    // fragt wantsSleep() ab.
    //
    // Reihenfolge ist wichtig: erst der Funk, dann die IMU. Umgekehrt liefe
    // der Funk noch, waehrend die IMU schon nichts mehr meldet.
    void prepareSleep() {
        mouse_.radioOff();
        imu_.setRate(ImuRate::Sleep);
        imu_.enableWakeOnMotion();
    }

    // Die einzige Stelle, an der Aktionen des Automaten Wirkung entfalten.
    void apply(const Actions& a, uint32_t now_ms) {
        if (a.click)      { mouse_.click();      clickPulse_ = true; }
        if (a.rightClick) { mouse_.rightClick(); clickPulse_ = true; }

        if (a.resetPose)     pose_.reset();
        if (a.enterScroll)   scroll_.enter(elev_);
        if (a.resetPointer) { accumX_ = accumY_ = 0.f; pointer_.reset(); twistGuard_.reset(); }
        if (a.hapticPulses)  haptic_.trigger(now_ms, a.hapticPulses);

        // Die Abtastrate folgt dem Ein/Aus-Zustand. Nur beim Wechsel
        // schreiben, nicht in jedem Takt - ein I2C-Zugriff je Takt waere
        // genau das Gegenteil von sparsam.
        if (fsm_.on() != imuRateActive_) {
            imuRateActive_ = fsm_.on();
            imu_.setRate(imuRateActive_ ? ImuRate::Active : ImuRate::Ready);
        }
    }

    // Der Klassifikator laeuft nur im eingeschalteten Zustand. Ihn auch im
    // Ruhezustand zu fuettern haette Inferenzen ausgeloest, deren Ergebnis
    // ohnehin verworfen wird - reine Rechenzeit und Strom.
    //
    // Der erkannte Pinch geht ohne Zwischenstufe an den Automaten; welche Taste
    // daraus wird, entscheidet dort die Haltung. Frueher lag hier ein
    // PinchGesture, das Einzel- von Doppel-Pinch trennte und den einzelnen
    // dafuer bis zum Ablauf des Doppel-Fensters zurueckhielt - diese
    // Verzoegerung lag auf jedem gewoehnlichen Klick.
    void handlePinch(const ImuSample& s, float env, uint32_t now_ms) {
        ml_.push(s, env);

        // Der Detektor entscheidet ueber die Schwelle und fragt den
        // Klassifikator per Kurzschlussauswertung nur bei offenem Gate.
        const bool pinched = pinch_.tick(env, s.gyroSum, now_ms,
                                         [this] { return ml_.ready() && ml_.isPinch(); });

        if (pinched) {
            // TwistToggle sieht den Arm koerperlich frueher als draussen als
            // die FSM-Pose (die erst nach POSE_CALM_MS + MODE_DWELL_MS +
            // MODE_TAU ankommt) - state() 1/2 heisst "gerade ausgedreht", auch
            // wenn schon verbraucht (2). Siehe AirMouseState::onPinch.
            const uint8_t tw = twistToggle_.state();
            const bool armOut = (tw == 1) || (tw == 2);
            apply(fsm_.onPinch(scroll_.inDeadzone(), armOut), now_ms);
        }
    }

    void handlePointing(const ImuSample& s, float dt, uint32_t now_us) {
        float px, py;
        // Die Verdrehung kommt aus dem langsamer geglaetteten Kanal, nicht aus
        // dem, mit dem die Haltung und die Drehgeste arbeiten: sie geht hier in
        // eine Drehmatrix ein, und deren Rauschen wuerde als Zittern im Cursor
        // landen.
        pointer_.update(s.gx, s.gz, pose_.relTwistSlow(), elev_, dt, px, py);

        // Waehrend sich der Unterarm dreht, laeuft der Cursor nicht mit. Der
        // Faktor greift hier und nicht vor dem 1-Euro-Filter, damit der Filter
        // eingeschwungen bleibt - sonst kaeme nach jeder Drehung eine
        // Anfahrverzoegerung obendrauf.
        accumX_ += px * twistGain_;
        accumY_ += py * twistGain_;

        if (now_us - lastMove_ < cfg::MOVE_INTERVAL_US) return;
        lastMove_ = now_us;

        // Mehrere Berichte im selben Takt, solange Rueckstau da ist. Ein
        // einzelner Bericht traegt hoechstens 127 px je Achse; bei schneller
        // Bewegung laeuft mehr auf, und der Rest kaeme sonst erst im naechsten
        // Intervall heraus - der Cursor laeuft dann nach dem Anhalten nach.
        for (int i = 0; i < cfg::MOVE_MAX_REPORTS; i++) {
            const int8_t mx = (int8_t)constrain(accumX_, -127.f, 127.f);
            const int8_t my = (int8_t)constrain(accumY_, -127.f, 127.f);
            if (!mx && !my) break;

            // Nur abziehen, wenn das Paket auch angenommen wurde, sonst geht
            // die Bewegung bei voller Warteschlange verloren.
            if (mouse_.move(mx, my)) { accumX_ -= mx; accumY_ -= my; }
            else {
                // Abgelehnte Pakete stauen sich in accumX_/accumY_ und gehen
                // beim naechsten Mal mit hinaus. Steigt moveFail_ im Betrieb,
                // wird schneller gemeldet als die Gegenstelle ausliefert.
                //
                // Der Rueckstau wird dabei begrenzt: nimmt die Gegenstelle
                // laenger gar nichts an - Host noch nicht aufgezaehlt, Kabel
                // raus, BLE nicht verbunden - liefe die Summe sonst minutenlang
                // weiter und der Cursor schoesse beim Verbinden quer ueber den
                // Schirm.
                moveFail_++;
                accumX_ = constrain(accumX_, -cfg::MOVE_BACKLOG_MAX, cfg::MOVE_BACKLOG_MAX);
                accumY_ = constrain(accumY_, -cfg::MOVE_BACKLOG_MAX, cfg::MOVE_BACKLOG_MAX);
                break;   // haengende Gegenstelle: nicht weiter nachschieben
            }
        }
    }

    // Teleplot-Ausgabe: eine Zeile ">name:wert" pro Kanal.
    // pose: 0 = Point, 1 = Idle, 2 = Turned (Reihenfolge von enum class Pose).
    void debug(const ImuSample& s, float env, uint32_t now_us) {
    #if DEBUG_TELEPLOT
        if (now_us - lastDbg_ < cfg::DEBUG_INTERVAL_US) return;
        lastDbg_ = now_us;

        // Immer: der Zustand des Automaten, wie er wirklich ist.
        //
        // pose und dpose sind absichtlich getrennt. dpose ist, was der Detektor
        // aus den Winkeln macht; pose ist, was im Automaten davon ankommt.
        // Dazwischen liegt die Uebergabe, die nur im eingeschalteten Zustand
        // stattfindet - laufen die beiden auseinander, liegt der Fehler dort und
        // nicht in der Haltungserkennung. Auf einem gemeinsamen Kanal waere
        // genau diese Unterscheidung nicht zu sehen.
        Serial.print(">on:");     Serial.println(fsm_.on() ? 1 : 0);
        Serial.print(">pose:");   Serial.println((int)fsm_.pose());
        Serial.print(">dpose:");  Serial.println((int)pose_.pose());
        // tw trennt "Geste nicht erkannt" von "erkannt, aber verworfen":
        // 0 = gerade, 1 = ausgedreht, 2 = ausgedreht und verbraucht,
        // 3 = Lockout nach dem Schalten.
        Serial.print(">tw:");     Serial.println(twistToggle_.state());
        Serial.print(">vbat:");   Serial.println(battery_.volts(), 3);

    #if DEBUG_SET == DEBUG_ALL || DEBUG_SET == DEBUG_PINCH
        // --- Klick-Kette: env -> gate -> p_ml -> click -------------------
        // Beim zweiten Impuls eines Doppeltipps liest man hier direkt ab, ob
        // das Gate oeffnete (env ueber ENV_ON) und was der Klassifikator sagte.
        // Steigt stattdessen nDeb oder nGyro, wurde die Flanke erkannt und
        // erst danach verworfen.
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
        // --- Zeigen: wo bleibt die Bewegung? -----------------------------
        // rx/ry gegen gz/gx gehalten zeigt die Daempfung des 1-Euro-Filters,
        // accx den Rueckstau und mvfail abgelehnte BLE-Pakete. gy gehoert
        // dazu: was dort landet, verwirft der Zeiger ersatzlos.
        Serial.print(">gx:");     Serial.println(s.gx, 2);
        Serial.print(">gy:");     Serial.println(s.gy, 2);
        Serial.print(">gz:");     Serial.println(s.gz, 2);
        Serial.print(">rx:");     Serial.println(pointer_.rateX(), 2);
        Serial.print(">ry:");     Serial.println(pointer_.rateY(), 2);
        Serial.print(">pacc:");   Serial.println(pointer_.accel(), 2);
        Serial.print(">accx:");   Serial.println(accumX_, 1);
        Serial.print(">mvfail:"); Serial.println(moveFail_);
        Serial.print(">twist:");  Serial.println(twist_, 1);
        Serial.print(">elev:");   Serial.println(elev_, 1);
        Serial.print(">rtwist:"); Serial.println(pose_.relTwistDeg(), 1);
        Serial.print(">level:");  Serial.println(pose_.level() ? 1 : 0);
        Serial.print(">srate:");  Serial.println(scroll_.rate(), 2);
        Serial.print(">tg:");     Serial.println(twistGain_, 2);
        Serial.print(">tgr:");    Serial.println(twistGuard_.rateDps(), 1);
    #endif

    // Absichtlich nicht Teil von DEBUG_ALL: der Satz teilt gx/gy/gz mit
    // DEBUG_POINT, zusammen kaemen die Kanaele doppelt heraus.
    #if DEBUG_SET == DEBUG_ORIENT
        // --- Lage nachpruefen --------------------------------------------
        // ax/ay/az roh und gvx/gvy/gvz geglaettet zeigen die Einbaulage direkt
        // am Sensor: flach auf dem Tisch az = +1, um 90 Grad verdreht ax = +1.
        // Daneben die Winkel, die daraus abgeleitet werden - twist/elev muessen
        // sich decken mit dem, was angX/angZ bzw. angY sagen. Tun sie es nicht,
        // stimmt die Einbaulage in ArmOrientation.h nicht mehr.
        const float amag = sqrtf(gvx_*gvx_ + gvy_*gvy_ + gvz_*gvz_);
        // Roh und geglaettet nebeneinander: gv* ist zum Ablesen einer gehaltenen
        // Haltung gedacht, a* zeigt ungefiltert, was der Sensor wirklich liefert
        // - Rauschband, Ausschlaege beim Bewegen, Saettigung am Messbereich.
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
