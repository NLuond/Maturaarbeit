#pragma once
#include <stdint.h>
#include <math.h>

// Ein/Aus durch eine Drehgeste des Unterarms: raus und zurueck, die ganze
// Bewegung binnen maxMs. Das ist die einzige Bedeutung der Ausdrehung - wer
// laenger draussen bleibt, ist in der Haltung Turned, und die traegt
// PoseDetector.
//
// Zwei Groessen kommen herein, beide aus derselben Lageschaetzung. Der Winkel
// ist absolut und driftet nicht weg; aus der Drehrate integriert wanderte die
// Referenz. Die Rate trennt die Erschuetterung der Geste selbst - den Anschlag
// am Scheitel - von der eines Pinch. Kehrseite des absoluten Winkels: bei
// senkrechtem Unterarm ist die Verdrehung aus der Schwerkraft nicht
// beobachtbar, deshalb muss level durchgehend gelten.
//
// Ohne config.h und ohne Arduino.h, damit der PC-Test laeuft.
struct TwistTuning {
    float    onDeg     =  70.f;   // so weit muss die Ausdrehung reichen
    float    backDeg   =  30.f;   // darunter gilt der Unterarm wieder als gerade

    // Fenster fuer die ganze Bewegung, gemessen ab dem Verlassen der
    // Neutralzone - nicht erst ab onDeg, sonst waere langsames Ausdrehen gratis.
    uint32_t maxMs     = 1400;
    uint32_t lockoutMs =  800;    // Ruhe nach einem Schaltvorgang

    // Wann eine gemeldete Erschuetterung als Pinch zaehlt: nur wenn der
    // Unterarm stillMs lang unter stillDps geblieben ist.
    float    stillDps  =  40.f;   // Grad/s
    uint32_t stillMs   =  150;    // ms
};

enum class TwistEvent : uint8_t {
    None,
    Toggle    // raus und zurueck innerhalb maxMs, nicht abgebrochen
};

class TwistToggle {
public:
    explicit TwistToggle(const TwistTuning& t = TwistTuning()) : t_(t) {}

    // relTwistDeg:  Verdrehung gegenueber der Zeige-Haltung, roh.
    // twistRateDps: Betrag der Drehrate um die Unterarmachse (TwistGuard).
    TwistEvent tick(float relTwistDeg, float twistRateDps, bool level, uint32_t now_ms) {
        if (twistRateDps > t_.stillDps) tTurning_ = now_ms;

        // Betrag statt Vorzeichen: onDeg ist anatomisch nur in einer
        // Drehrichtung erreichbar.
        const float tilt = fabsf(relTwistDeg);
        const bool  home = tilt <= t_.backDeg;

        const TwistEvent e = step(tilt, home, level, now_ms);
        wasHome_ = home;
        return e;
    }

    // Der Controller meldet eine Erschuetterung ueber der Abbruchschwelle; ob
    // sie die laufende Ausdrehung verbraucht, entscheidet dieses Modul. Nur ein
    // ruhender Unterarm kann gepincht haben - ein drehender erschuettert sich
    // selbst.
    void reportShock(uint32_t now_ms) {
        if (phase_ != Phase::Out && phase_ != Phase::Back) return;
        if (now_ms - tTurning_ >= t_.stillMs) used_ = true;
    }

    // Fuer den Teleplot-Kanal tw: 0 = Ruhe, 1 = ausgedreht, 2 = auf dem
    // Rueckweg, 3 = Lockout, 4 = verbraucht.
    uint8_t state() const {
        switch (phase_) {
            case Phase::Idle:    return 0;
            case Phase::Lockout: return 3;
            default: return used_ ? 4 : (phase_ == Phase::Out ? 1 : 2);
        }
    }

    // Warum eine Ausdrehung nicht geschaltet hat. Ohne diese Zaehler scheitert
    // die Geste lautlos und ist von Unzuverlaessigkeit nicht zu unterscheiden.
    uint16_t rejectedByCancel() const { return nCancelled_; }
    uint16_t rejectedByLevel()  const { return nNotLevel_; }
    uint16_t rejectedByTime()   const { return nTooSlow_; }

private:
    enum class Phase : uint8_t { Idle, Out, Back, Lockout };

    TwistEvent step(float tilt, bool home, bool level, uint32_t now_ms) {
        if (phase_ == Phase::Lockout) {
            if (now_ms - tToggle_ < t_.lockoutMs) return TwistEvent::None;
            phase_ = Phase::Idle;
        }

        if (phase_ == Phase::Idle) {
            // Nur auf der Flanke aus der Neutralzone heraus, sonst begaenne
            // eine abgebrochene Geste im ausgedrehten Stand einfach neu.
            if (level && wasHome_ && !home) {
                phase_  = Phase::Out;
                peak_   = tilt;
                tStart_ = now_ms;
                used_   = false;
            }
            return TwistEvent::None;
        }

        if (!level)                      return abort(nNotLevel_);
        if (now_ms - tStart_ > t_.maxMs) return abort(nTooSlow_);

        if (tilt > peak_) peak_ = tilt;

        if (phase_ == Phase::Out) {
            if (peak_ < t_.onDeg) {
                // Zu flach und schon wieder daheim: Alltagsbewegung. Zurueck auf
                // Anfang, damit die naechste Geste ihr volles Fenster bekommt.
                if (home) phase_ = Phase::Idle;
            } else if (tilt < t_.onDeg) {
                phase_ = Phase::Back;
            }
            return TwistEvent::None;
        }

        if (tilt > t_.onDeg) { phase_ = Phase::Out; return TwistEvent::None; }
        if (!home)             return TwistEvent::None;

        if (used_) return abort(nCancelled_);

        phase_   = Phase::Lockout;
        tToggle_ = now_ms;
        return TwistEvent::Toggle;
    }

    // Gezaehlt wird nur, was auch ein Versuch war: eine Ausdrehung unter onDeg
    // ist Alltagsbewegung und kein verschluckter Schaltvorgang.
    TwistEvent abort(uint16_t& counter) {
        if (peak_ >= t_.onDeg) counter++;
        phase_ = Phase::Idle;
        return TwistEvent::None;
    }

    TwistTuning t_;
    Phase    phase_    = Phase::Idle;
    bool     used_     = false;   // Pinch dazwischen, diese Ausdrehung schaltet nicht
    bool     wasHome_  = true;
    float    peak_     = 0.f;     // groesste Auslenkung dieser Ausdrehung, Grad
    uint32_t tStart_   = 0;
    uint32_t tToggle_  = 0;
    uint32_t tTurning_ = 0;       // zuletzt gedreht
    uint16_t nCancelled_ = 0;
    uint16_t nNotLevel_  = 0;
    uint16_t nTooSlow_   = 0;
};
