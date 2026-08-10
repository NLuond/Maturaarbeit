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
// Der Winkel kommt aus der Lageschaetzung und ist damit absolut; aus der
// Drehrate integriert wuerde die Referenz wegdriften. Kehrseite: bei senkrecht
// gehaltenem Unterarm ist die Verdrehung aus der Schwerkraft nicht beobachtbar,
// deshalb muss level durchgehend gelten.
//
// Ohne config.h und ohne Arduino.h, damit der PC-Test laeuft.
struct TwistTuning {
    float    onDeg     =  70.f;   // ab hier gilt der Arm als abgedreht
    float    backDeg   =  30.f;   // erst hier gilt er wieder als gerade
    uint32_t maxMs     = 1000;    // laenger draussen = keine Schaltgeste mehr
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

private:
    TwistTuning t_;
    bool     out_     = false;   // gerade ausgedreht
    bool     used_    = false;   // diese Ausdrehung schaltet nicht mehr
    bool     held_    = false;   // Held wurde fuer diese Ausdrehung gemeldet
    bool     locked_  = false;
    uint32_t tOut_    = 0;
    uint32_t tToggle_ = 0;
};
