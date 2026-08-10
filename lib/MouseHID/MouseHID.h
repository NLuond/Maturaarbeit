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

static BLEDis         bledis;
static BLEHidAdafruit blehid;

class MouseHID {
public:
    void begin() {
        Bluefruit.begin();
        Bluefruit.setName("Maturaarbeit Nils Luond");
        Bluefruit.setTxPower(4);

        // Kurzes Verbindungsintervall anfragen (Einheit 1.25 ms, also 7.5-15 ms).
        // Ohne Anfrage handelt der Stack teils 30 ms aus - genug, um die
        // Zielgenauigkeit messbar zu verschlechtern.
        Bluefruit.Periph.setConnInterval(6, 12);

        bledis.setManufacturer("Nils");
        bledis.setModel("AirMouse");
        bledis.begin();

        blehid.begin();
        startAdvertising();
    }

    void click()      { blehid.mouseButtonPress(MOUSE_BUTTON_LEFT);
                        blehid.mouseButtonRelease(); }
    void rightClick() { blehid.mouseButtonPress(MOUSE_BUTTON_RIGHT);
                        blehid.mouseButtonRelease(); }
    void scroll(int8_t ticks)       { blehid.mouseScroll(ticks); }
    bool move(int8_t dx, int8_t dy) { return blehid.mouseMove(dx, dy); }

    // Fuer das Ziehen: die Bibliothek merkt sich die Tastenmaske und traegt sie
    // in jeden folgenden mouseMove-Bericht mit - mehr braucht es dafuer nicht.
    void pressLeft()  { blehid.mouseButtonPress(MOUSE_BUTTON_LEFT); }
    void releaseAll() { blehid.mouseButtonRelease(); }

    // Im Ruhezustand ist der Funk der groesste verbleibende Verbraucher.
    //
    // Zuerst die Selbstwiederbelebung abschalten: sonst startet die SoftDevice
    // das Advertising im Disconnect-Ereignis sofort wieder, und der Funk waere
    // weiter an, ohne dass man es sieht.
    void radioOff() {
        Bluefruit.Advertising.restartOnDisconnect(false);
        Bluefruit.Advertising.stop();
        if (Bluefruit.connected()) Bluefruit.disconnect(Bluefruit.connHandle());
    }

    // Symmetrisch zurueck, sonst meldet sich das Geraet nach einem spaeteren
    // Verbindungsabbruch nicht mehr von selbst zurueck.
    void radioOn() {
        Bluefruit.Advertising.restartOnDisconnect(true);
        Bluefruit.Advertising.start(0);
    }

private:
    void startAdvertising() {
        Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
        Bluefruit.Advertising.addTxPower();
        Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_HID_MOUSE);
        Bluefruit.Advertising.addService(blehid);
        Bluefruit.Advertising.addName();
        Bluefruit.Advertising.restartOnDisconnect(true);
        Bluefruit.Advertising.setInterval(32, 244);
        Bluefruit.Advertising.setFastTimeout(30);
        Bluefruit.Advertising.start(0);
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

    void pressLeft()  { hid_.mouseButtonPress(0, MOUSE_BUTTON_LEFT); }
    void releaseAll() { hid_.mouseButtonRelease(0); }

    // Ueber USB gibt es keinen Funk: das Geraet haengt an einer Stromquelle,
    // und ein Abschalten wuerde die Enumeration abwerfen. Leer statt eines #if
    // an der Aufrufstelle - bei Strommessungen aber beachten.
    void radioOff() {}
    void radioOn()  {}

private:
    Adafruit_USBD_HID hid_;
};
#endif

// Es wird immer nur einer der beiden Zweige kompiliert - der andere kann
// unbemerkt abdriften, bis jemand umschaltet.
static_assert(std::is_same<decltype(&MouseHID::begin),   void (MouseHID::*)()>::value,
              "MouseHID::begin() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::click),      void (MouseHID::*)()>::value,
              "MouseHID::click() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::rightClick), void (MouseHID::*)()>::value,
              "MouseHID::rightClick() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::pressLeft),  void (MouseHID::*)()>::value,
              "MouseHID::pressLeft() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::releaseAll), void (MouseHID::*)()>::value,
              "MouseHID::releaseAll() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::scroll),  void (MouseHID::*)(int8_t)>::value,
              "MouseHID::scroll() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::move),    bool (MouseHID::*)(int8_t, int8_t)>::value,
              "MouseHID::move() muss bool zurueckgeben - siehe Akkumulation im Controller");
static_assert(std::is_same<decltype(&MouseHID::radioOff), void (MouseHID::*)()>::value,
              "MouseHID::radioOff() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::radioOn),  void (MouseHID::*)()>::value,
              "MouseHID::radioOn() hat die falsche Signatur");
