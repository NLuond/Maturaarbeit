#pragma once
#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "ImuSample.h"
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
class AirMouseController {
public:
    explicit AirMouseController(MouseHID& mouse)
        : mouse_(mouse), ahrs_(cfg::MADGWICK_BETA) {}

    void begin() { haptic_.begin(); }

    void update(const ImuSample& s, float dt, uint32_t now_us) {
        const uint32_t now_ms = now_us / 1000;

        haptic_.update(now_ms);
        ahrs_.update(s.gx, s.gy, s.gz, s.ax, s.ay, s.az, dt);
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
        // wenn die Maus noch aus ist.
        const Pose posed = pose_.update(twist_, elev_, s.gyroSum, dt, now_ms);

        // Eine Erschuetterung ueber der env-Schwelle verbraucht die laufende
        // Ausdrehung: sie war ein Pinch und keine Schaltgeste. Absichtlich an
        // der Schwelle und nicht am erkannten Klick - verpasst der
        // Klassifikator den Pinch, wuerde das Zurueckdrehen sonst die Maus
        // abschalten statt rechtszuklicken. Nur im eingeschalteten Zustand:
        // dort laeuft der Pinch-Pfad, und eine Erschuetterung soll die
        // Einschalt-Geste nicht abbrechen.
        if (fsm_.on() && env > cfg::ENV_ON) twistToggle_.cancel();

        switch (twistToggle_.tick(pose_.relTwistDeg(), pose_.level(), now_ms)) {
            case TwistEvent::Toggle: apply(fsm_.onPower(),     now_ms); break;
            case TwistEvent::Held:   apply(fsm_.onTwistHeld(), now_ms); break;
            case TwistEvent::None:   break;
        }

        if (fsm_.on()) {
            // Haltung vor Pinch: welche Taste ein Pinch ausloest, haengt an der
            // Haltung, und die soll im selben Takt schon die aktuelle sein.
            apply(fsm_.onPose(posed), now_ms);
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
    }

private:
    MouseHID&          mouse_;
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

    bool     clickPulse_ = false;
    float    twist_ = 0.f, elev_ = 0.f;
    float    twistGain_ = 1.f;
    float    accumX_ = 0.f, accumY_ = 0.f;
    uint32_t lastMove_ = 0, lastDbg_ = 0;
    uint16_t moveFail_ = 0;

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

    // Die einzige Stelle, an der Aktionen des Automaten Wirkung entfalten.
    void apply(const Actions& a, uint32_t now_ms) {
        if (a.click)      { mouse_.click();      clickPulse_ = true; }
        if (a.rightClick) { mouse_.rightClick(); clickPulse_ = true; }

        if (a.resetPose)     pose_.reset();
        if (a.enterScroll)   scroll_.enter(elev_);
        if (a.resetPointer) { accumX_ = accumY_ = 0.f; pointer_.reset(); twistGuard_.reset(); }
        if (a.hapticPulses)  haptic_.trigger(now_ms, a.hapticPulses);
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

        if (pinched) apply(fsm_.onPinch(scroll_.inDeadzone()), now_ms);
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
