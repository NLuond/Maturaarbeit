#pragma once
#include <type_traits>
#include "config.h"

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
        // Die Gegenstelle darf ablehnen, aber ohne Anfrage handelt der Stack
        // teils 30 ms aus - das liegt schon ueber der Schwelle, ab der
        // Verzoegerung die Zielgenauigkeit messbar verschlechtert.
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

    // Im Ruhezustand ist der Funk der groesste verbleibende Verbraucher.
    // Verbindung trennen und Advertising stoppen; beim Aufwachen wird neu
    // geworben. Die Neuverbindung versteckt sich hinter der Bewegung des
    // Nutzers: er hebt den Arm, waehrenddessen verbindet sich BLE, und erst
    // danach kommt die Drehgeste.
    void radioOff() {
        Bluefruit.Advertising.stop();
        if (Bluefruit.connected()) Bluefruit.disconnect(Bluefruit.connHandle());
    }

    void radioOn() { Bluefruit.Advertising.start(0); }

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

    // Ueber USB gibt es keinen Funk und keinen Ruhezustand - das Geraet
    // haengt an einer Stromquelle, und ein Abschalten wuerde die
    // Enumeration abwerfen. Leer statt eines #if an der Aufrufstelle.
    void radioOff() {}
    void radioOn()  {}

private:
    Adafruit_USBD_HID hid_;
};
#endif

// Es wird immer nur einer der beiden Zweige kompiliert - der andere kann
// unbemerkt abdriften, bis jemand umschaltet und der Aufrufer nicht mehr passt.
// Besonders heikel ist der bool-Rueckgabewert von move(): an ihm haengt die
// Bewegungsakkumulation in AirMouseController. Wuerde er zu void, ginge bei
// voller Warteschlange still Bewegung verloren.
static_assert(std::is_same<decltype(&MouseHID::begin),   void (MouseHID::*)()>::value,
              "MouseHID::begin() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::click),      void (MouseHID::*)()>::value,
              "MouseHID::click() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::rightClick), void (MouseHID::*)()>::value,
              "MouseHID::rightClick() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::scroll),  void (MouseHID::*)(int8_t)>::value,
              "MouseHID::scroll() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::move),    bool (MouseHID::*)(int8_t, int8_t)>::value,
              "MouseHID::move() muss bool zurueckgeben - siehe Akkumulation im Controller");
static_assert(std::is_same<decltype(&MouseHID::radioOff), void (MouseHID::*)()>::value,
              "MouseHID::radioOff() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::radioOn),  void (MouseHID::*)()>::value,
              "MouseHID::radioOn() hat die falsche Signatur");
