#pragma once
#include <Arduino.h>
#include <type_traits>
#include "config.h"
#include "PinchFeatures.h"

// Sendeweg der Trainingsdaten im COLLECT_MODE, umgeschaltet ueber
// COLLECT_OVER_BLE: USB-Serial oder BLE-UART (Nordic UART Service). Die Kanaele
// und ihre Reihenfolge kommen in beiden Faellen aus feat::pack().
//
// Ueber Funk traegt jede Zeile zusaetzlich einen 16-Bit-Zaehler vorweg, den die
// Bruecke (tools/ble_collect_bridge.py) prueft und wieder abstreift. Ohne ihn
// waere ein verlorener Brocken unsichtbar und dehnte den Datensatz still - der
// Fehler, der den ersten Datensatz unbrauchbar gemacht hat.
//
// send() meldet zurueck, ob die Zeile vollstaendig abgegeben wurde; der
// Aufrufer latcht daran die Warnleuchte. Der Zaehler laeuft auch ohne
// Verbindung weiter, damit ein Verbindungsabbruch bei der Bruecke als Luecke
// ankommt und nicht als saubere Aufnahme.

// Sammelt eine Zeile im RAM, damit sie in einem Stueck an den Sendeweg geht:
// BLEUart schickt sonst je Byte eine Benachrichtigung. Ueber Print kommt dabei
// dieselbe Zahlenformatierung heraus wie bisher aus Serial.print().
class CsvLine : public Print {
public:
    size_t write(uint8_t b) override {
        if (len_ >= CAPACITY) { over_ = true; return 0; }
        buf_[len_++] = b;
        return 1;
    }
    using Print::write;

    void clear() { len_ = 0; over_ = false; }

    const uint8_t* data() const { return buf_; }
    size_t         size() const { return len_; }
    // Eine abgeschnittene Zeile saehe vollstaendig gesendet aus - ohne diese
    // Frage waere genau das der eine stille Datenverlust.
    bool overflow() const { return over_; }

private:
    static constexpr size_t CAPACITY = 64;
    uint8_t buf_[CAPACITY];
    size_t  len_  = 0;
    bool    over_ = false;
};

namespace collect {
    // Kanal 0 ist die Huellkurve: sie bewegt sich zwischen 0.005 und 0.125, bei
    // drei Stellen bliebe am unteren Ende eine signifikante Ziffer.
    inline uint8_t digits(int channel) { return channel == 0 ? 4 : 3; }
}

#if COLLECT_OVER_BLE
#include <bluefruit.h>
#include <nrf_soc.h>

// bufferTXD() legt den Sendepuffer fest mit genau einer MTU an - 247 Bytes,
// rund sechs Zeilen. Weil BLEUart bei 244 Bytes selbst leert, ist er damit
// staendig fast voll: die naechste Zeile passt nur noch teilweise hinein, der
// Rest faehrt als eigenes Kleinpaket, und scheitert das, kommt eine halbe Zeile
// an. Gemessen war so jedes dritte Paket ein solcher Rest. Deshalb der
// Sendepuffer hier selbst und grosszuegig - dieselben drei Zeilen wie im Kern,
// nur mit eigener Groesse.
class CollectUart : public BLEUart {
public:
    void beginBuffered(uint16_t bytes) {
        if (!_tx_fifo) {
            _tx_fifo = new Adafruit_FIFO(1);   // einmalig beim Start, nicht im Takt
            _tx_fifo->begin(bytes);
        }
        _tx_buffered = true;
    }

    // Beim Trennen bleibt der Rest der letzten Sitzung im Puffer stehen und
    // faehrt beim naechsten Verbinden als erstes hinaus - Zeilen mit alten
    // Zaehlerwerten, die der Bruecke als Luecke erscheinen. Gemessen waren es
    // fuenf Zeilen und ein Sprung von 8566.
    void dropPending() {
        if (_tx_fifo) _tx_fifo->clear();
    }
};

static CollectUart bleuart;

class CollectLink {
public:
    void begin() {
        // Vor Bluefruit.begin(), weil es die Puffergroessen festlegt: mit der
        // Vorgabe von 23 Bytes je Paket reicht die Funkstrecke fuer 208 Hz nicht.
        Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
        Bluefruit.begin();
        Bluefruit.setName(BLE_NAME);
        Bluefruit.setTxPower(4);
        Bluefruit.Periph.setConnInterval(6, 12);

        bleuart.begin();
        // Ohne Pufferung wird jedes einzelne Byte zu einer Benachrichtigung;
        // mit ihr sammelt BLEUart bis zur MTU und sendet von selbst.
        bleuart.beginBuffered(TX_BUFFER_BYTES);
        startAdvertising();

        // Erst nach Bluefruit.begin(): ein SVC ohne laufende SoftDevice faellt
        // in den SVC_Handler des Kerns - eine Endlosschleife.
        sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);
    }

    bool ready() { return Bluefruit.connected() && bleuart.notifyEnabled(); }

    bool send(const float* f) {
        const uint16_t seq = seq_++;

        // Flanke, nicht Zustand: nur beim Beginn einer Sitzung darf geraeumt
        // werden, sonst raeumte jeder Takt den eben geschriebenen Puffer weg.
        const bool live = ready();
        if (live && !wasLive_) bleuart.dropPending();
        wasLive_ = live;
        if (!live) return true;

        line_.clear();
        line_.print(seq);
        for (int i = 0; i < feat::CHANNELS; i++) {
            line_.print(',');
            line_.print(f[i], collect::digits(i));
        }
        line_.print('\n');
        if (line_.overflow()) return false;

        return bleuart.write(line_.data(), line_.size()) == line_.size();
    }

private:
    // Muss eine ganze Zeile ueber die laengste Funkpause tragen; gemessen waren
    // das 66 ms, also rund 600 Bytes. RAM ist hier der billigste Posten.
    static constexpr uint16_t TX_BUFFER_BYTES = 2048;

    CsvLine  line_;
    uint16_t seq_ = 0;
    bool     wasLive_ = false;

    void startAdvertising() {
        // Die 128-Bit-UUID des UART-Dienstes belegt allein 18 der 31 Bytes des
        // Advertising-Pakets, der Name passt daneben nicht mehr und geht in die
        // Scan Response. Die Bruecke sucht ohnehin ueber die UUID: die
        // HID-Firmware wirbt unter demselben BLE_NAME.
        Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
        Bluefruit.Advertising.addService(bleuart);
        Bluefruit.ScanResponse.addName();

        Bluefruit.Advertising.restartOnDisconnect(true);
        Bluefruit.Advertising.setInterval(32, 244);
        // Siehe MouseHID: start(0) uebergibt _fast_timeout als Dauer an die
        // SoftDevice, nach deren Ablauf das Werben stehen bleiben kann.
        Bluefruit.Advertising.setFastTimeout(0);
        Bluefruit.Advertising.start(0);
    }
};

#else

class CollectLink {
public:
    void begin() { Serial.begin(115200); }

    // Ueber USB gibt es kein Koppeln, und ein fehlender Empfaenger ist am
    // CDC-Port nicht zu sehen.
    bool ready() { return true; }

    bool send(const float* f) {
        line_.clear();
        for (int i = 0; i < feat::CHANNELS; i++) {
            if (i) line_.print(',');
            line_.print(f[i], collect::digits(i));
        }
        line_.print("\r\n");
        if (line_.overflow()) return false;

        return Serial.write(line_.data(), line_.size()) == line_.size();
    }

private:
    CsvLine line_;
};
#endif

// Es wird immer nur einer der beiden Zweige kompiliert - der andere kann
// unbemerkt abdriften, bis jemand umschaltet.
static_assert(std::is_same<decltype(&CollectLink::begin), void (CollectLink::*)()>::value,
              "CollectLink::begin() hat die falsche Signatur");
static_assert(std::is_same<decltype(&CollectLink::ready), bool (CollectLink::*)()>::value,
              "CollectLink::ready() hat die falsche Signatur");
static_assert(std::is_same<decltype(&CollectLink::send), bool (CollectLink::*)(const float*)>::value,
              "CollectLink::send() muss bool zurueckgeben - daran haengt die Warnleuchte");
