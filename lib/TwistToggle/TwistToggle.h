#pragma once
#include <stdint.h>
#include <math.h>

// Ein/Aus durch eine Drehgeste des Unterarms: gerade halten, um rund 90 Grad
// abdrehen, innerhalb einer Sekunde wieder zurueck. Ersetzt das fruehere
// Schuetteln, dessen Schwelle (350 Grad/s) nur knapp ueber den rund 250 Grad/s
// des normalen Gebrauchs lag.
//
// Dieselbe Ausdrehung traegt drei Bedeutungen, unterschieden allein durch das,
// was danach passiert:
//
//   raus und binnen maxMs zurueck, nichts dazwischen  -> Toggle (Ein/Aus)
//   raus, Erschuetterung dazwischen (cancel())        -> nichts, das war ein Pinch
//   raus und laenger als maxMs gehalten               -> Held (Scroll-Modus)
//
// Der Winkel kommt aus der Lageschaetzung (arm::twistDeg ueber Madgwick) und ist
// damit absolut. Aus der Drehrate integriert wuerde die Referenz wegdriften und
// die Bedingung "wieder zurueck auf gerade" waere nach einer Minute nicht mehr
// dieselbe wie am Anfang.
//
// level muss durchgehend gelten: bei senkrecht gehaltenem Unterarm ist die
// Verdrehung aus der Schwerkraft nicht beobachtbar, ein haengender Arm wuerde
// sonst zufaellig schalten.
//
// Kein #include "config.h" und kein <Arduino.h>: dieser Header muss sich ohne
// Toolchain uebersetzen lassen (test/test_twist_toggle.cpp). Die Zahlen stehen
// deshalb in TwistTuning und nicht in cfg:: - dieselbe bewusste Ausnahme wie
// bei ArmOrientation.
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
        // Betrag statt Vorzeichen: aus der Zeige-Haltung heraus laesst sich der
        // Unterarm rund 90 Grad supinieren, aber nur 10 bis 30 Grad pronieren.
        // onDeg ist damit anatomisch nur in einer Richtung erreichbar, und
        // welche das ist, muss der Code nicht wissen.
        const float tilt = fabsf(relTwistDeg);

        if (locked_) {
            if (now_ms - tToggle_ < t_.lockoutMs) return TwistEvent::None;
            locked_ = false;
        }

        if (!level) {
            // Waagrecht-Gate weg: die laufende Ausdrehung zaehlt nicht mehr.
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

        // ausgedreht
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

    // Die laufende Ausdrehung schaltet nicht mehr. Der Aufrufer meldet damit,
    // dass waehrenddessen eine Erschuetterung ueber der env-Schwelle lag - also
    // ein Pinch versucht wurde. Absichtlich an der Schwelle und nicht am
    // erkannten Klick: verpasst der Klassifikator den Pinch, wuerde das
    // Zurueckdrehen sonst die Maus abschalten statt rechtszuklicken.
    void cancel() { used_ = true; }

    // Nur fuer die Teleplot-Ausgabe: 0 = gerade, 1 = ausgedreht,
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
