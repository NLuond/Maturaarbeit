#pragma once
#include <stdint.h>
#include <math.h>

// Ein/Aus durch eine Drehgeste des Unterarms: aus der Ruhe heraus, bei
// waagrechtem Arm, zuegig um onDeg heraus und binnen maxMs wieder zurueck. Das
// ist die einzige Bedeutung der Ausdrehung - wer laenger draussen bleibt, ist in
// der Haltung Turned, und die traegt PoseDetector.
//
// Der Ausschlag allein traegt die Geste NICHT: 45 Grad Unterarmdrehung kommen im
// Alltag staendig vor. Was sie zur Geste macht, sind die beiden Bedingungen am
// Start - waagrechter Arm (die Maus wird gerade benutzt) und ein Unterarm, der
// vorher armedMs lang ruhte (die Bewegung war gemeint, nicht Teil einer
// groesseren). Beide gelten NUR am Start.
//
// Gemessen wird die DREHRATE, integriert ab dem Beginn der Bewegung, nicht ein
// Winkel gegen eine feste Nulllage. Der Ausschlag ist damit immer relativ zu
// der Haltung, in der der Unterarm gerade ruhte. Das nimmt der Geste vier
// Abhaengigkeiten auf einmal:
//
//   keine Schwerkraft als Bezug   - bei steilem Unterarm war die Verdrehung
//                                   aus der Lageschaetzung unbeobachtbar
//   kein fester Nullpunkt         - ruhte der Arm neben der gedachten
//                                   Neutrallage, war die Geste unerreichbar
//   keine Einschwingzeit          - der Gyro braucht keine Konvergenz
//   keine Singularitaet           - atan2 wird nahe senkrecht schlecht bedingt
//
// Integriert wird nur WAEHREND der Geste, also hoechstens maxMs lang. Ein
// Nullpunktfehler des Gyros summiert sich deshalb nicht auf: 3 Grad/s Rest
// ergeben ueber 1.2 s rund 3.6 Grad gegen eine Schwelle von 45.
//
// Ohne config.h und ohne Arduino.h, damit der PC-Test laeuft.
struct TwistTuning {
    // Ausschlag ab der Ruhelage, nicht gegen eine feste Nulllage - deshalb
    // deutlich kleiner als die Haltungsschwelle cfg::TURN_ON_DEG.
    float    onDeg      = 45.f;   // so weit muss die Drehung reichen
    float    backDeg    = 20.f;   // so nah wieder an den Ausgangspunkt

    // Der Hinweg muss zuegig sein. Diese Bedingung ist der Preis fuer den
    // kleinen Ausschlag: sie trennt die Geste von einer beilaeufigen Armdrehung,
    // die denselben Winkel ueber Sekunden erreicht.
    uint32_t outMaxMs   =  600;   // ms bis onDeg
    uint32_t maxMs      = 1200;   // ms fuer die ganze Bewegung
    uint32_t lockoutMs  =  800;   // Ruhe nach einem Schaltvorgang

    float    startDps   =  60.f;  // ab hier laeuft eine Drehung

    // So lange muss der Unterarm vor der Geste geruht haben. Eine bewusste
    // Geste beginnt aus der Ruhe; Alltagsbewegung ist durchgehend und kommt nie
    // so lange zur Ruhe. Zusammen mit dem Waagrecht-Gate ist das die Bremse
    // gegen Fehlausloesungen - der Ausschlag allein reicht dafuer nicht.
    uint32_t armedMs    =  200;   // ms Ruhe vor dem Start

    // Wann eine gemeldete Erschuetterung als Pinch zaehlt: nur wenn der
    // Unterarm stillMs lang unter stillDps geblieben ist.
    float    stillDps   =  40.f;  // Grad/s
    uint32_t stillMs    =  150;   // ms
    float    rateTau    = 0.03f;  // s, Glaettung NUR fuer die Ruhepruefung
};

enum class TwistEvent : uint8_t {
    None,
    Toggle    // raus und zurueck innerhalb maxMs, nicht abgebrochen
};

class TwistToggle {
public:
    explicit TwistToggle(const TwistTuning& t = TwistTuning()) : t_(t) {}

    // twistRateDps: vorzeichenbehaftete Drehrate um die Unterarmachse, roh
    // (ImuSample::gy). Bewusst nicht die Rate aus der Lageschaetzung: die soll
    // die Verdrehung VOLLSTAENDIG erfassen und braucht dafuer die Schwerkraft,
    // hier genuegt der Ausschlag. Der Schiefstand zwischen Unterarm- und
    // Platinenachse kostet nur cos(Winkel), bei 15 Grad rund drei Prozent.
    TwistEvent tick(float twistRateDps, bool level, float dt, uint32_t now_ms) {
        // Eine Geste beginnt nur auf der FLANKE der Drehrate. Nach einem
        // Abbruch dreht sich der Unterarm noch, und ohne die Flanke begaenne
        // die Rueckdrehung sofort eine neue Geste in der Gegenrichtung.
        const bool fast   = fabsf(twistRateDps) > t_.startDps;
        const bool rising = fast && !wasFast_;
        wasFast_ = fast;

        const TwistEvent e = step(twistRateDps, rising, level, dt, now_ms);

        // Erst NACH step(): tTurning_ soll sagen, wie lange der Unterarm VOR
        // dieser Bewegung geruht hat. Der Tiefpass steigt bei einer zuegigen
        // Drehung schon im ersten Takt ueber stillDps - vorgezogen setzte die
        // Geste ihre eigene Ruhezeit zurueck und koennte nie starten.
        //
        // Geglaettet, weil der kurze Impuls eines Pinch den Unterarm sonst als
        // "dreht gerade" erscheinen liesse und reportShock() aushebelte.
        const float a = 1.f - expf(-dt / t_.rateTau);
        rateLp_ += a * (twistRateDps - rateLp_);
        if (fabsf(rateLp_) > t_.stillDps) tTurning_ = now_ms;

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

    // Fuer den Teleplot-Kanal tw: 0 = Ruhe, 1 = dreht heraus, 2 = Ausschlag
    // erreicht, 3 = Lockout, 4 = verbraucht.
    uint8_t state() const {
        switch (phase_) {
            case Phase::Idle:    return 0;
            case Phase::Lockout: return 3;
            default: return used_ ? 4 : (phase_ == Phase::Out ? 1 : 2);
        }
    }

    // Ausschlag der laufenden Drehung in Grad. Gehoert nach aussen, weil sich
    // onDeg nur einstellen laesst, wenn man am Geraet sieht, wie weit man
    // tatsaechlich dreht.
    float excursionDeg() const { return exc_; }

    // Warum eine Ausdrehung nicht geschaltet hat. Ohne diese Zaehler scheitert
    // die Geste lautlos und ist von Unzuverlaessigkeit nicht zu unterscheiden.
    // rejectedByTime deckt beide Fenster ab, den Hinweg und die ganze Bewegung.
    uint16_t rejectedByCancel() const { return nCancelled_; }
    uint16_t rejectedByTime()   const { return nTooSlow_; }

    // Die beiden Startbedingungen. Sie zaehlen verhinderte Fehlausloesungen, sind
    // also erwartungsgemaess gross - und werden erst dann verdaechtig, wenn eine
    // gemeinte Geste ausbleibt und einer von beiden dabei hochgeht.
    uint16_t rejectedByLevel()  const { return nNotLevel_; }
    uint16_t rejectedByMotion() const { return nMoving_; }

private:
    enum class Phase : uint8_t { Idle, Out, Back, Lockout };

    TwistEvent step(float rate, bool rising, bool level, float dt, uint32_t now_ms) {
        if (phase_ == Phase::Lockout) {
            if (now_ms - tToggle_ < t_.lockoutMs) return TwistEvent::None;
            phase_ = Phase::Idle;
        }

        if (phase_ == Phase::Idle) {
            if (!rising) return TwistEvent::None;

            // Beide Bedingungen gelten nur HIER, am Start. Sie beantworten die
            // Frage "war das ueberhaupt als Geste gemeint", und die stellt sich
            // einmal. Gezaehlt wird jede Ablehnung: sie ist eine verhinderte
            // Fehlausloesung und damit die interessanteste Zahl am Geraet.
            if (!level)                              { nNotLevel_++; return TwistEvent::None; }
            if (now_ms - tTurning_ < t_.armedMs)     { nMoving_++;   return TwistEvent::None; }

            phase_  = Phase::Out;
            dir_    = (rate > 0.f) ? 1.f : -1.f;
            exc_    = 0.f;
            peak_   = 0.f;
            tStart_ = now_ms;
            used_   = false;
            return TwistEvent::None;
        }

        // Roh integriert und nicht geglaettet: ein Tiefpass verschoebe den
        // Ausschlag um seine Zeitkonstante mal die Rate.
        exc_ += dir_ * rate * dt;
        if (exc_ > peak_) peak_ = exc_;

        // Kein Waagrecht-Gate mehr waehrend der Geste: die Drehung schwenkt elev
        // selbst mit, weil die Platinenachse nicht exakt auf der anatomischen
        // Drehachse liegt. Ein laufendes Gate wuergte die eigene Geste ab.
        (void)level;
        if (now_ms - tStart_ > t_.maxMs) return abort(nTooSlow_, t_.onDeg);

        if (phase_ == Phase::Out) {
            if (peak_ >= t_.onDeg) { phase_ = Phase::Back; return TwistEvent::None; }
            // Schon wieder daheim, ohne je weit genug gewesen zu sein: zurueck
            // auf Anfang, damit ein sofort wiederholter Versuch sein volles
            // Fenster bekommt. Bei einer kleinen Geste ist das der Regelfall.
            if (peak_ >= t_.backDeg && exc_ < t_.backDeg) phase_ = Phase::Idle;
            else if (now_ms - tStart_ > t_.outMaxMs) return abort(nTooSlow_, t_.backDeg);
            return TwistEvent::None;
        }

        if (exc_ >= t_.backDeg) return TwistEvent::None;

        if (used_) return abort(nCancelled_, t_.onDeg);

        phase_   = Phase::Lockout;
        tToggle_ = now_ms;
        return TwistEvent::Toggle;
    }

    // Gezaehlt wird nur, was auch ein Versuch war: eine Drehung, die nicht
    // einmal minPeak erreicht hat, ist Alltagsbewegung.
    TwistEvent abort(uint16_t& counter, float minPeak) {
        if (peak_ >= minPeak) counter++;
        phase_ = Phase::Idle;
        return TwistEvent::None;
    }

    TwistTuning t_;
    Phase    phase_    = Phase::Idle;
    bool     used_     = false;   // Pinch dazwischen, diese Drehung schaltet nicht
    float    dir_      = 1.f;     // Drehrichtung dieser Geste
    float    exc_      = 0.f;     // Ausschlag seit Beginn der Bewegung, Grad
    float    peak_     = 0.f;     // groesster Ausschlag dieser Geste, Grad
    float    rateLp_   = 0.f;     // geglaettete Rate, nur fuer die Ruhepruefung
    bool     wasFast_  = false;   // fuer die Startflanke der Drehrate
    uint32_t tStart_   = 0;
    uint32_t tToggle_  = 0;
    uint32_t tTurning_ = 0;       // zuletzt gedreht
    uint16_t nCancelled_ = 0;
    uint16_t nNotLevel_  = 0;
    uint16_t nMoving_    = 0;
    uint16_t nTooSlow_   = 0;
};
