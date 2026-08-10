#pragma once
#include <stdint.h>
#include <math.h>

// Ein/Aus durch eine Drehgeste des Unterarms. Dieselbe Ausdrehung traegt drei
// Bedeutungen, unterschieden allein durch das, was danach passiert:
//
//   raus und binnen maxMs zurueck, nichts dazwischen  -> Toggle (Ein/Aus)
//   raus, Erschuetterung dazwischen (cancel())        -> nichts, das war ein Pinch
//   raus und laenger als maxMs gehalten               -> Held (Scroll-Modus)
//
// Eine vierte Bedeutung ueber einen tieferen Scheitelwinkel (Ziehen) ist wieder
// ausgebaut worden: sie lag auf derselben Achse wie Ein/Aus, und eine etwas zu
// weit geratene Schaltgeste wurde dadurch stillschweigend zum Ziehen. Das
// Ziehen haengt jetzt am Doppel-Pinch, siehe AirMouseState.
//
// Der Winkel kommt aus der Lageschaetzung und ist damit absolut; aus der
// Drehrate integriert wuerde die Referenz wegdriften. Kehrseite: bei senkrecht
// gehaltenem Unterarm ist die Verdrehung aus der Schwerkraft nicht beobachtbar,
// deshalb muss level durchgehend gelten.
//
// Ohne config.h und ohne Arduino.h, damit der PC-Test laeuft.
struct TwistTuning {
    float    onDeg     =  70.f;   // ab hier gilt der Arm als abgedreht
    float    backDeg   =  30.f;   // erst hier gilt er wieder als gerade
    // Laenger draussen = keine Schaltgeste mehr, sondern der Scroll-Modus.
    // 1000 ms waren zu knapp: eine bewusst gefuehrte Drehung raus UND zurueck
    // braucht mehr, und wer sie verfehlt, bekommt keinen Hinweis - die Geste
    // faellt lautlos aus.
    uint32_t maxMs     = 1300;
    uint32_t lockoutMs =  800;    // Ruhe nach einem Schaltvorgang
};

enum class TwistEvent : uint8_t {
    None,
    Toggle,   // raus und zurueck innerhalb maxMs, nicht abgebrochen
    Held      // maxMs ueberschritten, waehrend noch ausgedreht
};

class TwistToggle {
public:
    explicit TwistToggle(const TwistTuning& t = TwistTuning()) : t_(t) {}

    // relTwistDeg: geglaettete Verdrehung gegenueber der Zeige-Haltung.
    TwistEvent tick(float relTwistDeg, bool level, uint32_t now_ms) {
        // Betrag statt Vorzeichen: onDeg ist anatomisch nur in einer
        // Drehrichtung erreichbar, und welche das ist, muss der Code nicht
        // wissen.
        const float tilt = fabsf(relTwistDeg);

        if (locked_) {
            if (now_ms - tToggle_ < t_.lockoutMs) return TwistEvent::None;
            locked_ = false;
        }

        if (!level) {
            // Zaehlen, damit eine hier verschluckte Geste im Teleplot sichtbar
            // wird: sie scheitert sonst lautlos und sieht wie Unzuverlaessigkeit
            // aus. Gleiches Muster wie die Zaehler in PinchDetector.
            if (out_ && !used_) nNotLevel_++;
            out_  = false;
            used_ = true;
            return TwistEvent::None;
        }

        if (!out_) {
            if (tilt > t_.onDeg) {
                out_  = true;
                used_ = false;
                held_ = false;
                tOut_ = now_ms;
            }
            return TwistEvent::None;
        }

        if (tilt < t_.backDeg) {
            out_ = false;
            const bool quick = (now_ms - tOut_) <= t_.maxMs;
            if (quick && !used_ && !held_) {
                tToggle_ = now_ms;
                locked_  = true;
                return TwistEvent::Toggle;
            }
            if (used_)       nCancelled_++;
            else if (!quick && !held_) nTooSlow_++;
            return TwistEvent::None;
        }

        if (!held_ && (now_ms - tOut_) > t_.maxMs) {
            held_ = true;
            return TwistEvent::Held;
        }
        return TwistEvent::None;
    }

    // Die laufende Ausdrehung schaltet nicht mehr: waehrenddessen lag eine
    // Erschuetterung ueber der env-Schwelle, es war also ein Pinch.
    void cancel() { used_ = true; }

    // Fuer den Teleplot-Kanal tw: 0 = gerade, 1 = ausgedreht,
    // 2 = ausgedreht und verbraucht, 3 = Lockout.
    uint8_t state() const {
        if (locked_) return 3;
        if (!out_)   return 0;
        return used_ ? 2 : 1;
    }

    // Warum eine Ausdrehung nicht geschaltet hat. Ohne diese Zaehler scheitert
    // die Geste lautlos und ist von Unzuverlaessigkeit nicht zu unterscheiden.
    uint16_t rejectedByCancel() const { return nCancelled_; }
    uint16_t rejectedByLevel()  const { return nNotLevel_; }
    uint16_t rejectedByTime()   const { return nTooSlow_; }

private:
    TwistTuning t_;
    bool     out_     = false;   // gerade ausgedreht
    bool     used_    = false;   // diese Ausdrehung schaltet nicht mehr
    bool     held_    = false;   // Held wurde fuer diese Ausdrehung gemeldet
    bool     locked_  = false;
    uint32_t tOut_    = 0;
    uint32_t tToggle_ = 0;
    uint16_t nCancelled_ = 0;
    uint16_t nNotLevel_  = 0;
    uint16_t nTooSlow_   = 0;
};
