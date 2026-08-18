#pragma once
#include <type_traits>
#include "config.h"

// Kapselt USB-HID (TinyUSB) und BLE-HID (bluefruit) hinter einer gemeinsamen
// Schnittstelle, umgeschaltet ueber USE_BLE_HID.
//
// move() meldet zurueck, ob das Paket angenommen wurde. Der Aufrufer darf die
// gesendete Strecke erst dann von seinem Rest abziehen - sonst geht Bewegung
// verloren, wenn die Warteschlange voll ist.

#if USE_BLE_HID
#include <bluefruit.h>

static BLEDis bledis;
static BLEHidAdafruit blehid;

class MouseHID {
public:
    // Bit je fehlgeschlagenem Schritt der Inbetriebnahme. Ohne die Pruefung
    // scheitert der Funk lautlos - ununterscheidbar von "wird nicht gefunden".
    enum : uint8_t {
        ErrStack = 1 << 0,   // Bluefruit.begin()
        ErrDis   = 1 << 1,   // Device Information Service
        ErrHid   = 1 << 2,   // HID-Service
        ErrAdv   = 1 << 3,   // Advertising liess sich nicht starten
        ErrPkt   = 1 << 4,   // ein Feld passte nicht ins Advertising-Paket
        ErrName  = 1 << 5    // der Name passte weder in Paket noch Scan Response
    };
    uint8_t initError() const { return initErr_; }

    // Wirbt das Geraet gerade wirklich? Der Startaufruf kann gelingen und das
    // Advertising trotzdem spaeter stehen - dieser Wert ist die Wahrheit.
    bool advertising() const { return Bluefruit.Advertising.isRunning(); }

    void begin() {
        if (!Bluefruit.begin()) initErr_ |= ErrStack;
        Bluefruit.setName(BLE_NAME);
        Bluefruit.setTxPower(4);

        // Kurzes Verbindungsintervall anfragen (Einheit 1.25 ms, also 7.5-15 ms).
        // Ohne Anfrage handelt der Stack teils 30 ms aus.
        Bluefruit.Periph.setConnInterval(6, 12);

        bledis.setManufacturer("Nils");
        bledis.setModel("AirMouse");
        if (bledis.begin() != ERROR_NONE) initErr_ |= ErrDis;

        if (blehid.begin() != ERROR_NONE) initErr_ |= ErrHid;
        startAdvertising();
    }

    void click()      { blehid.mouseButtonPress(MOUSE_BUTTON_LEFT);
                        blehid.mouseButtonRelease(); }
    void rightClick() { blehid.mouseButtonPress(MOUSE_BUTTON_RIGHT);
                        blehid.mouseButtonRelease(); }
    void scroll(int8_t ticks)       { blehid.mouseScroll(ticks); }
    bool move(int8_t dx, int8_t dy) { return blehid.mouseMove(dx, dy); }


    bool connected() const { return Bluefruit.connected(); }

    // Jeden Takt aufrufen: wer nicht verbunden ist, muss werben. Bewusst eine
    // LAUFENDE Bedingung, damit jeder Pfad, der das Advertising je gestoppt hat,
    // hier wieder eingefangen wird.
    //
    // Hoechstens einmal je CHECK_MS: meldet isRunning() nur kurz false, wuerde
    // das Advertising sonst 209-mal je Sekunde neu gestartet und kaeme nie zur
    // Ruhe.
    void ensureAdvertising(uint32_t now_ms) {
        if (now_ms - tCheck_ < CHECK_MS) return;
        tCheck_ = now_ms;

        if (Bluefruit.connected()) {
            // Die Vorgabe aus begin() ist nur ein Wunsch; Windows vergibt
            // HID-Geraeten oft 30 ms und mehr. Nochmal fragen hilft haeufig -
            // aber nur ein paar Mal, sonst stellt man die Verbindung zu.
            if (nParamReq_ < 3 && connIntervalUs() > cfg::MOVE_INTERVAL_US) {
                BLEConnection* c = Bluefruit.Connection(Bluefruit.connHandle());
                if (c) { c->requestConnectionParameter(6); nParamReq_++; }   // 6 * 1.25 ms
            }
            return;
        }
        nParamReq_ = 0;   // naechste Verbindung darf wieder fragen
        if (Bluefruit.Advertising.isRunning()) return;

        nAdvStart_++;   // sichtbar machen, wie oft nachgestartet werden musste
        if (!Bluefruit.Advertising.start(0)) initErr_ |= ErrAdv;
    }

    // Zusammen trennen die beiden Zahlen die Faelle: adv = 0 kann "wirbt nicht"
    // ODER "ist verbunden" heissen - eine Verbindung setzt in der Bibliothek
    // dasselbe Flag zurueck (BLEAdvertising.cpp, BLE_GAP_EVT_CONNECTED).
    uint16_t advRestarts() const { return nAdvStart_; }
    uint8_t  connCount()   const { return Bluefruit.Periph.connected(); }

    // Ausgehandeltes Verbindungsintervall in Mikrosekunden, 0 wenn nicht
    // verbunden (getConnectionInterval() liefert Einheiten zu 1.25 ms). Das ist
    // die eigentliche Taktgrenze der Maus: schneller als ein Bericht je
    // Intervall kommt nichts durch.
    uint32_t connIntervalUs() const {
        if (!Bluefruit.connected()) return 0;
        BLEConnection* c = Bluefruit.Connection(Bluefruit.connHandle());
        if (!c) return 0;
        return (uint32_t)c->getConnectionInterval() * 1250u;
    }

    // Im Ruhezustand waere der Funk der groesste verbleibende Verbraucher -
    // abgeschaltet ist das Geraet aber unauffindbar. Mit BLE_ALWAYS_ON bleibt
    // er an.
    void radioOff() {
    #if !BLE_ALWAYS_ON
        Bluefruit.Advertising.restartOnDisconnect(false);
        Bluefruit.Advertising.stop();
        if (Bluefruit.connected()) Bluefruit.disconnect(Bluefruit.connHandle());
    #endif
    }

    // Symmetrisch zurueck, sonst meldet sich das Geraet nach einem spaeteren
    // Verbindungsabbruch nicht mehr von selbst zurueck.
    void radioOn() {
        Bluefruit.Advertising.restartOnDisconnect(true);
        tCheck_ = 0;                 // beim naechsten Takt sofort nachsehen
        ensureAdvertising(CHECK_MS);
    }

private:
    static constexpr uint32_t CHECK_MS = 1000;
    uint8_t  initErr_   = 0;
    uint32_t tCheck_    = 0;
    uint16_t nAdvStart_ = 0;
    uint8_t  nParamReq_ = 0;

    void startAdvertising() {
        // Das Advertising-Paket fasst 31 Bytes; hier belegt sind 25 (Flags 3 +
        // Appearance 4 + HID-Service 4 + Name 14). TxPower ist bewusst draussen,
        // der Name ist wichtiger: Windows blendet BLE-Geraete ohne Namen aus,
        // und die Scan Response wird nur bei einem aktiven Scan geholt.
        //
        // Jeder Aufruf meldet, ob das Feld noch hineinpasste - ohne die Pruefung
        // faellt eines lautlos weg.
        if (!Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE)) initErr_ |= ErrPkt;
        if (!Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_HID_MOUSE))               initErr_ |= ErrPkt;
        if (!Bluefruit.Advertising.addService(blehid))                                    initErr_ |= ErrPkt;
        if (!Bluefruit.Advertising.addName())                                             initErr_ |= ErrName;
        if (!Bluefruit.ScanResponse.addName())                                            initErr_ |= ErrName;

        Bluefruit.Advertising.restartOnDisconnect(true);
        Bluefruit.Advertising.setInterval(32, 244);

        // KEIN Umschalten von schnellem auf langsames Werben: start(0) uebergibt
        // in der Bibliothek nicht "unbegrenzt", sondern _fast_timeout als Dauer
        // an die SoftDevice, nach deren Ablauf das Werben stehen bleiben kann.
        // Mit 0 gibt es gar keine Dauer - das Geraet wirbt durchgehend schnell.
        Bluefruit.Advertising.setFastTimeout(0);
        if (!Bluefruit.Advertising.start(0)) initErr_ |= ErrAdv;
    }
};

#else
#include "Adafruit_TinyUSB.h"

static uint8_t const _mouse_report_desc[] = { TUD_HID_REPORT_DESC_MOUSE() };

class MouseHID {
public:
    MouseHID() : hid_(_mouse_report_desc, sizeof(_mouse_report_desc),
                      HID_ITF_PROTOCOL_MOUSE, 2, false) {}
    void begin() {
        USBDevice.setManufacturerDescriptor("Nils");
        hid_.begin();
    }

    void click()      { hid_.mouseButtonPress(0, MOUSE_BUTTON_LEFT);
                        hid_.mouseButtonRelease(0); }
    void rightClick() { hid_.mouseButtonPress(0, MOUSE_BUTTON_RIGHT);
                        hid_.mouseButtonRelease(0); }
    void scroll(int8_t ticks)       { hid_.mouseScroll(0, ticks, 0); }
    bool move(int8_t dx, int8_t dy) { return hid_.mouseMove(0, dx, dy); }


    // Ueber USB gibt es kein Koppeln: haengt das Kabel, ist die Verbindung da.
    bool     connected() const   { return USBDevice.mounted(); }
    bool     advertising() const { return false; }
    uint8_t  initError() const   { return 0; }
    uint16_t advRestarts() const { return 0; }
    uint8_t  connCount() const   { return USBDevice.mounted() ? 1 : 0; }
    // USB hat kein Verbindungsintervall - MOVE_INTERVAL_US gilt unveraendert.
    uint32_t connIntervalUs() const { return 0; }
    void ensureAdvertising(uint32_t) {}

    void radioOff() {}
    void radioOn()  {}

private:
    Adafruit_USBD_HID hid_;
};
#endif

// Es wird immer nur einer der beiden Zweige kompiliert - der andere kann
// unbemerkt abdriften, bis jemand umschaltet.
static_assert(std::is_same<decltype(&MouseHID::begin),      void (MouseHID::*)()>::value,
              "MouseHID::begin() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::click),      void (MouseHID::*)()>::value,
              "MouseHID::click() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::rightClick), void (MouseHID::*)()>::value,
              "MouseHID::rightClick() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::scroll),     void (MouseHID::*)(int8_t)>::value,
              "MouseHID::scroll() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::move),       bool (MouseHID::*)(int8_t, int8_t)>::value,
              "MouseHID::move() muss bool zurueckgeben - siehe Akkumulation im Controller");
static_assert(std::is_same<decltype(&MouseHID::connected),  bool (MouseHID::*)() const>::value,
              "MouseHID::connected() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::ensureAdvertising), void (MouseHID::*)(uint32_t)>::value,
              "MouseHID::ensureAdvertising() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::initError),  uint8_t (MouseHID::*)() const>::value,
              "MouseHID::initError() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::advertising), bool (MouseHID::*)() const>::value,
              "MouseHID::advertising() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::radioOff),   void (MouseHID::*)()>::value,
              "MouseHID::radioOff() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::radioOn),    void (MouseHID::*)()>::value,
              "MouseHID::radioOn() hat die falsche Signatur");
