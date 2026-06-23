#pragma once
#include "config.h"

#if USE_BLE_HID
#include <bluefruit.h>

static BLEDis  bledis;
static BLEHidAdafruit blehid;

class MouseHID {
public:
    void begin() {
        Bluefruit.begin();
        Bluefruit.setName("Maturaarbeit Nils Luond");
        Bluefruit.setTxPower(4);

        bledis.setManufacturer("Nils");
        bledis.setModel("AirMouse");
        bledis.begin();

        blehid.begin();
        startAdvertising();
    }
    bool ready() { return Bluefruit.connected() > 0; }

    void click()   { blehid.mouseButtonPress(MOUSE_BUTTON_LEFT);
                     blehid.mouseButtonRelease(); }
    void press()   { blehid.mouseButtonPress(MOUSE_BUTTON_LEFT); }
    void release() { blehid.mouseButtonRelease(); }
    void move(int8_t dx, int8_t dy) { blehid.mouseMove(dx, dy); }

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
    MouseHID() : hid(_mouse_report_desc, sizeof(_mouse_report_desc),
                     HID_ITF_PROTOCOL_MOUSE, 2, false) {}
    void begin() {
        USBDevice.setManufacturerDescriptor("Nils");
        hid.begin();
    }
    bool ready() { return TinyUSBDevice.mounted(); }
    void click()   { hid.mouseButtonPress(0, MOUSE_BUTTON_LEFT); hid.mouseButtonRelease(0); }
    void press()   { hid.mouseButtonPress(0, MOUSE_BUTTON_LEFT); }
    void release() { hid.mouseButtonRelease(0); }
    void move(int8_t dx, int8_t dy) { hid.mouseMove(0, dx, dy); }
private:
    Adafruit_USBD_HID hid;
};
#endif