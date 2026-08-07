# Stromsparen — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Die Air Mouse laeuft im Akkubetrieb wochenlang statt stundenlang, ohne dass sich die Bedienung aendert.

**Architecture:** Drei Betriebszustaende (AKTIV 208 Hz / BEREIT 52 Hz / SCHLAF). Die Entscheidung, wann geschlafen wird, liegt in einem hardwarefreien, PC-getesteten Modul `lib/SleepPolicy/`; die Ausfuehrung verteilt sich auf `ImuReader` (Abtastrate, Wake-Up-Interrupt), `MouseHID` (Funk aus) und `src/main.cpp` (`suspendLoop()`). Geweckt wird ueber den Wake-Up-Interrupt der IMU auf INT1 — es wird nichts gepollt. Zusaetzlich schlaeft die CPU kuenftig auch zwischen den Takten, statt leer durchzulaufen; das ist der groesste Einzelposten.

**Tech Stack:** C++14, PlatformIO / Arduino (Adafruit nRF52 Core mit FreeRTOS, `configUSE_TICKLESS_IDLE = 1`), LSM6DS3TR-C ueber die Seeed-Bibliothek, bluefruit fuer BLE.

**Spec:** `docs/superpowers/specs/2026-08-07-stromsparen-design.md`

## Global Constraints

- **Kommentare auf Deutsch, ASCII ohne Umlaute** (`waehrend`, `Verzoegerung`, `Abtastrate`). Sie begruenden das *Warum* einer Entscheidung. Dieser Stil ist Teil der Maturaarbeit. Die Markdown-Dateien `TODO.md`, `CLAUDE.md` und `docs/*.md` behalten ihre echten Umlaute.
- Kein `new`/`malloc`, keine `String`, keine dynamischen Container im Hot Path. Feste Puffer und `float`.
- Alle Zahlenwerte als `constexpr` in `namespace cfg` (`include/config.h`) — **ausser** in den PC-testbaren Modulen, die ein eigenes Tuning-Struct tragen (`SleepTuning`, `TwistTuning`, `TwistGuardTuning`, `PointerTuning`) und weder `config.h` noch `<Arduino.h>` einbinden duerfen.
- **`cfg::SAMPLE_INTERVAL_US = 4785` bleibt unveraendert** und ist per `static_assert` in `PinchClassifier.h` an das ML-Modell gekoppelt. Der Klassifikator laeuft ausschliesslich im Zustand AKTIV, wo genau diese Rate gilt.
- Policy-Module kennen weder Hardware noch das EI-SDK.
- **Neues Modul → Ordner `lib/<Name>/` anlegen UND `-I lib/<Name>` in `platformio.ini` ergaenzen.**
- **Nie zwei `pio run` gleichzeitig** auf dasselbe `.pio/build`.
- **Nicht flashen.** Nie `pio run -t upload` — Nils flasht selbst. Tasks enden mit einem erfolgreichen Build.

### Befehle

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run 2>&1 | Select-Object -Last 20
```

```powershell
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/SleepPolicy -o "$env:TEMP\sleep.exe" test/test_sleep_policy.cpp
& "$env:TEMP\sleep.exe"
```

Ein PC-Test gilt als bestanden, wenn er `0 Fehler` meldet und mit Exit-Code 0 endet. Warnungen unter `-Wall -Wextra` sind Fehler im Sinne dieses Plans.

**Die sieben bestehenden PC-Tests muessen am Ende jedes Tasks gruen bleiben:**
`test_state_machine.cpp` (`-I lib/AirMouseState`), `test_arm_orientation.cpp` (`-I lib/ArmOrientation`), `test_twist_toggle.cpp` (`-I lib/TwistToggle`), `test_twist_guard.cpp` (`-I lib/TwistGuard`), `test_one_euro.cpp` (`-I lib/Filters`), `test_pinch_features.cpp` (`-I lib/ImuReader -I lib/PinchFeatures`), sowie ab Task 2 `test_sleep_policy.cpp`.

---

## File Structure

| Datei | Verantwortung | Task |
|---|---|---|
| `lib/Battery/Battery.h` | *neu* — Akkuspannung messen, mitteln | 1 |
| `lib/SleepPolicy/SleepPolicy.h` | *neu* — entscheidet *wann* geschlafen wird | 2 |
| `test/test_sleep_policy.cpp` | *neu* | 2 |
| `src/main.cpp` | Takt, Schlaf der Schleife, `suspendLoop()`, variables `dt` | 3, 4, 8 |
| `include/config.h` | Konstanten | 1, 3, 4, 7, 8 |
| `lib/ImuReader/ImuReader.h` | Abtastrate umschalten, Wake-Up-Interrupt | 5 |
| `lib/MouseHID/MouseHID.h` | Funk ab- und anschalten | 6 |
| `lib/MadgwickAHRS/MadgwickAHRS.h` | Beta zur Laufzeit setzbar | 7 |
| `lib/AirMouseController/AirMouseController.h` | orchestriert die Zustandswechsel | 1, 7, 8 |
| `platformio.ini` | `-I`-Eintraege, Build-Flags | 1, 2, 9 |
| `TODO.md`, `CLAUDE.md`, `docs/Programmcode.md` | Doku und Messplan | 10 |

**Reihenfolge und warum:** Task 1 (Batterie) steht bewusst **zuerst**, damit Nils die Entladekurve des *jetzigen* Standes aufnehmen kann, bevor irgendetwas optimiert ist — ohne diesen Vorher-Wert gibt es am Ende keinen Vergleich und keine Zahl fuer die Arbeit. Task 3 (Schleife) ist der groesste Einzelposten und steht deshalb frueh und allein, damit sein Effekt einzeln messbar ist. Die Tasks 5–7 bauen die Hardware-Fähigkeiten, Task 8 verdrahtet sie. **Nach jedem Task ist der Firmware-Build gruen.**

---

## Task 1: `lib/Battery/` — die Ersparnis messbar machen

Ohne Messung ist jede Ersparnis eine Behauptung. Die XIAO kann ihre eigene Akkuspannung lesen; damit nimmt die Firmware ihre Entladekurve selbst auf, ohne Zusatzgeraet. Dieser Task steht zuerst, damit der Vorher-Zustand messbar ist.

**Files:**
- Create: `lib/Battery/Battery.h`
- Modify: `platformio.ini`, `include/config.h`, `lib/AirMouseController/AirMouseController.h`

**Interfaces:**
- Produces: `class Battery { void begin(); void update(uint32_t now_ms); float volts() const; }`

- [ ] **Step 1: `Battery.h` schreiben**

```cpp
#pragma once
#include <Arduino.h>
#include "config.h"

// Misst die Akkuspannung ueber den eingebauten Spannungsteiler der XIAO.
//
// VBAT_ENABLE (P14) schaltet den Teiler zu und ist aktiv LOW. Er wird nur
// fuer die Messung eingeschaltet und danach wieder freigegeben: ein
// dauerhaft angeschlossener Teiler zieht staendig Strom, und genau den
// wollen wir hier ja messen.
//
// Gemessen wird selten (BATTERY_INTERVAL_MS) und ueber mehrere Wandlungen
// gemittelt - der ADC rauscht, und die Spannung aendert sich ueber Stunden.
class Battery {
public:
    void begin() {
        pinMode(VBAT_ENABLE, OUTPUT);
        digitalWrite(VBAT_ENABLE, HIGH);      // aktiv LOW: Teiler aus
        analogReference(AR_INTERNAL_3_0);     // 3.0 V Referenz
        analogReadResolution(12);             // 0..4095
    }

    void update(uint32_t now_ms) {
        if (used_ && now_ms - tLast_ < cfg::BATTERY_INTERVAL_MS) return;
        used_  = true;
        tLast_ = now_ms;

        digitalWrite(VBAT_ENABLE, LOW);       // Teiler zu
        uint32_t sum = 0;
        for (int i = 0; i < 8; i++) sum += analogRead(PIN_VBAT);
        digitalWrite(VBAT_ENABLE, HIGH);      // und wieder weg

        volts_ = (sum / 8.f) * cfg::BATTERY_VOLTS_PER_LSB;
    }

    float volts() const { return volts_; }

private:
    // used_ nur, damit die erste Messung nicht bis BATTERY_INTERVAL_MS
    // wartet: kurz nach dem Start ist now_ms klein und tLast_ noch null.
    bool     used_  = false;
    uint32_t tLast_ = 0;
    float    volts_ = 0.f;
};
```

- [ ] **Step 2: Konstanten in `config.h` ergaenzen**

Am Ende von `namespace cfg`, vor der schliessenden Klammer:

```cpp
    // --- Akku ------------------------------------------------------------
    // Selten genug, dass die Messung selbst nichts kostet - die Spannung
    // aendert sich ueber Stunden, nicht ueber Sekunden.
    constexpr uint32_t BATTERY_INTERVAL_MS = 30000;

    // Volt je ADC-Schritt: 3.0 V Referenz / 4096 Schritte, multipliziert mit
    // dem Teilerverhaeltnis der XIAO (ueblicherweise 1 M / 510 k, also
    // (1000+510)/510 = 2.961).
    //
    // Der Wert ist ein Startwert und gehoert kalibriert: eine bekannte
    // Akkuspannung mit dem Multimeter messen und gegen den Kanal vbat
    // halten, dann den Faktor nachziehen. Die Angaben zum Teiler sind in der
    // Literatur uneinheitlich, und eine Laufzeitangabe ist nur so gut wie
    // die Spannungsmessung, auf der sie beruht.
    constexpr float BATTERY_VOLTS_PER_LSB = (3.0f / 4096.f) * 2.961f;
```

- [ ] **Step 3: `-I lib/Battery` in `platformio.ini` ergaenzen**

Nach `-I lib/ImuReader`:

```
    -I lib/Battery
```

- [ ] **Step 4: Im Controller verdrahten**

`#include "Battery.h"` ergaenzen, Member `Battery battery_;` anlegen. In `begin()`:

```cpp
    void begin() { haptic_.begin(); battery_.begin(); }
```

In `update()`, direkt nach `haptic_.update(now_ms);`:

```cpp
        battery_.update(now_ms);
```

Im Teleplot-Block bei den immer gesendeten Kanaelen (neben `>on:`):

```cpp
        Serial.print(">vbat:");   Serial.println(battery_.volts(), 3);
```

- [ ] **Step 5: Firmware bauen**

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run 2>&1 | Select-Object -Last 20
```

Erwartet: `SUCCESS`.

- [ ] **Step 6: Commit**

```bash
git add lib/Battery/Battery.h include/config.h platformio.ini lib/AirMouseController/AirMouseController.h
git commit -m "$(cat <<'EOF'
feat: Akkuspannung messen und als Teleplot-Kanal ausgeben

Steht bewusst vor den Stromsparmassnahmen: ohne eine Entladekurve des
jetzigen Standes gibt es am Ende keinen Vergleichswert. Der Spannungsteiler
wird nur fuer die Messung zugeschaltet, sonst zoege er dauerhaft Strom.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: `lib/SleepPolicy/` — wann geschlafen wird

Hardwarefrei und auf dem PC pruefbar, nach dem Muster von `TwistToggle` und `TwistGuard`. Das Modul entscheidet *wann*, nicht *wie*. Es wird hier gebaut und getestet, aber noch **nicht verdrahtet** — das ist Task 8.

**Files:**
- Create: `lib/SleepPolicy/SleepPolicy.h`, `test/test_sleep_policy.cpp`
- Modify: `platformio.ini`

**Interfaces:**
- Produces: `struct SleepTuning { float stillDps; uint32_t sleepAfter; uint32_t settleMs; }`
- Produces: `enum class SleepEvent : uint8_t { None, GoToSleep, Settled }`
- Produces: `class SleepPolicy { explicit SleepPolicy(const SleepTuning& = SleepTuning()); SleepEvent tick(bool mouseOn, float gyroSum, uint32_t now_ms); void wake(uint32_t now_ms); bool settling() const; bool wantsSleep() const; }`

- [ ] **Step 1: Den Test schreiben**

Neue Datei `test/test_sleep_policy.cpp`:

```cpp
// Test der Schlaf-Entscheidung. Laeuft auf dem PC - SleepPolicy haengt
// bewusst an keiner Hardware.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/SleepPolicy -o build/sleep.exe test/test_sleep_policy.cpp && ./build/sleep.exe
//
#include "SleepPolicy.h"
#include <cstdio>

static int failures = 0;
static int checks   = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        checks++;                                                           \
        if (!(cond)) {                                                      \
            failures++;                                                     \
            std::printf("  FEHLER Zeile %d: %s\n", __LINE__, (msg));        \
        }                                                                   \
    } while (0)

// Laesst die Zeit in 10-ms-Schritten laufen und meldet, welches Ereignis
// dabei kam.
static SleepEvent run(SleepPolicy& p, bool mouseOn, float gyroSum,
                      uint32_t& now, uint32_t ms) {
    SleepEvent seen = SleepEvent::None;
    for (uint32_t i = 0; i < ms; i += 10) {
        const SleepEvent e = p.tick(mouseOn, gyroSum, now);
        if (e != SleepEvent::None) seen = e;
        now += 10;
    }
    return seen;
}

static void test_sleepsAfterQuietTime() {
    SleepPolicy p;
    uint32_t now = 1000;
    CHECK(run(p, false, 0.f, now, 30000) == SleepEvent::None,
          "schlaeft schon nach 30 s");
    CHECK(run(p, false, 0.f, now, 35000) == SleepEvent::GoToSleep,
          "schlaeft nicht nach 60 s Ruhe");
}

// Die wichtigste Eigenschaft: waehrend die Maus benutzt wird, darf sie nicht
// verschwinden - auch nicht, wenn man den Cursor minutenlang ruhig auf einem
// Ziel haelt.
static void test_neverSleepsWhileMouseOn() {
    SleepPolicy p;
    uint32_t now = 1000;
    CHECK(run(p, true, 0.f, now, 300000) == SleepEvent::None,
          "schlaeft ein, obwohl die Maus eingeschaltet ist");
}

static void test_motionResetsTheTimer() {
    SleepPolicy p;
    uint32_t now = 1000;
    run(p, false, 0.f,   now, 50000);   // fast eingeschlafen
    run(p, false, 200.f, now,   500);   // Bewegung
    CHECK(run(p, false, 0.f, now, 30000) == SleepEvent::None,
          "die Bewegung hat den Zeitgeber nicht zurueckgesetzt");
    CHECK(run(p, false, 0.f, now, 35000) == SleepEvent::GoToSleep,
          "schlaeft danach gar nicht mehr");
}

// Eine ruhig gehaltene, aber getragene Hand zaehlt nicht als Ruhe.
static void test_smallMotionCountsAsQuiet() {
    SleepPolicy p;
    uint32_t now = 1000;
    // 5 Grad/s liegt unter stillDps (20) und darf den Zeitgeber nicht halten.
    CHECK(run(p, false, 5.f, now, 65000) == SleepEvent::GoToSleep,
          "kleines Rauschen verhindert das Einschlafen");
}

static void test_wantsSleepLatchesUntilWake() {
    SleepPolicy p;
    uint32_t now = 1000;
    CHECK(!p.wantsSleep(), "will schon vor dem Ereignis schlafen");
    run(p, false, 0.f, now, 65000);
    CHECK(p.wantsSleep(), "meldet den Schlafwunsch nicht");
    p.wake(now);
    CHECK(!p.wantsSleep(), "der Schlafwunsch bleibt nach dem Aufwachen stehen");
}

static void test_goToSleepComesOnce() {
    SleepPolicy p;
    uint32_t now = 1000;
    CHECK(run(p, false, 0.f, now, 65000) == SleepEvent::GoToSleep, "kein GoToSleep");
    CHECK(run(p, false, 0.f, now, 65000) == SleepEvent::None,
          "GoToSleep kommt mehrfach ohne zwischenzeitliches wake()");
}

static void test_settlingWindowAfterWake() {
    SleepPolicy p;
    uint32_t now = 1000;
    run(p, false, 0.f, now, 65000);
    p.wake(now);
    CHECK(p.settling(), "nach dem Aufwachen wird nicht eingeschwungen");
    CHECK(run(p, false, 100.f, now, 200) == SleepEvent::None,
          "Settled kommt zu frueh");
    CHECK(p.settling(), "das Einschwingfenster endet zu frueh");
    CHECK(run(p, false, 100.f, now, 200) == SleepEvent::Settled,
          "Settled kommt gar nicht");
    CHECK(!p.settling(), "settling() bleibt nach Settled stehen");
}

static void test_settledComesOnce() {
    SleepPolicy p;
    uint32_t now = 1000;
    run(p, false, 0.f, now, 65000);
    p.wake(now);
    run(p, false, 100.f, now, 400);                       // Settled
    CHECK(run(p, false, 100.f, now, 400) == SleepEvent::None,
          "Settled kommt mehrfach");
}

// Nach dem Aufwachen laeuft der Zeitgeber neu an - sonst schliefe das Geraet
// unmittelbar nach dem Wecken wieder ein.
//
// Geprueft wird "kein GoToSleep" und nicht "None": in den ersten 300 ms nach
// dem Aufwachen kommt zwangslaeufig das Settled-Ereignis, und run() liefert
// das zuletzt gesehene zurueck.
static void test_timerRestartsAfterWake() {
    SleepPolicy p;
    uint32_t now = 1000;
    run(p, false, 0.f, now, 65000);
    p.wake(now);
    CHECK(run(p, false, 0.f, now, 30000) != SleepEvent::GoToSleep,
          "schlaeft direkt nach dem Aufwachen wieder ein");
}

int main() {
    test_sleepsAfterQuietTime();
    test_neverSleepsWhileMouseOn();
    test_motionResetsTheTimer();
    test_smallMotionCountsAsQuiet();
    test_wantsSleepLatchesUntilWake();
    test_goToSleepComesOnce();
    test_settlingWindowAfterWake();
    test_settledComesOnce();
    test_timerRestartsAfterWake();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
```

- [ ] **Step 2: Test laufen lassen und Fehlschlag bestaetigen**

```powershell
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/SleepPolicy -o "$env:TEMP\sleep.exe" test/test_sleep_policy.cpp
```

Erwartet: **Compilerfehler**, `SleepPolicy.h: No such file or directory`.

- [ ] **Step 3: `SleepPolicy.h` schreiben**

Neue Datei `lib/SleepPolicy/SleepPolicy.h`:

```cpp
#pragma once
#include <stdint.h>

// Entscheidet, wann das Geraet in den Ruhezustand geht - nicht, wie das
// ausgefuehrt wird. Die Hardware-Seite (IMU-Rate, Funk, suspendLoop) liegt
// im Controller und in main.cpp.
//
// Kein #include "config.h" und kein <Arduino.h>: dieser Header muss sich
// ohne Toolchain uebersetzen lassen (test/test_sleep_policy.cpp). Die Werte
// stehen deshalb in SleepTuning - dieselbe bewusste Ausnahme wie bei
// TwistTuning und TwistGuardTuning.
struct SleepTuning {
    // Bewegung wird an der Drehrate gemessen und nicht an der
    // Beschleunigung: eine ruhig gehaltene, aber getragene Hand soll nicht
    // als Ruhe zaehlen, und die Erdbeschleunigung liegt immer an.
    float    stillDps   = 20.f;
    uint32_t sleepAfter = 60000;   // ms Ruhe bis zum Schlaf
    uint32_t settleMs   = 300;     // Sperre nach dem Aufwachen
};

enum class SleepEvent : uint8_t {
    None,
    GoToSleep,   // Ruhezeit erreicht - Hardware herunterfahren
    Settled      // Einschwingfenster vorbei - Madgwick-Beta zurueckstellen
};

class SleepPolicy {
public:
    explicit SleepPolicy(const SleepTuning& t = SleepTuning()) : t_(t) {}

    SleepEvent tick(bool mouseOn, float gyroSum, uint32_t now_ms) {
        // Eingeschaltet wird nie geschlafen. Sonst verschwaende die Maus
        // mitten im Gebrauch, waehrend man den Cursor nur ruhig auf einem
        // Ziel haelt - dort ist gyroSum naemlich klein.
        if (mouseOn || gyroSum >= t_.stillDps) tQuiet_ = now_ms;

        if (settling_ && (now_ms - tWake_) >= t_.settleMs) {
            settling_ = false;
            return SleepEvent::Settled;
        }

        if (!wants_ && !settling_ && (now_ms - tQuiet_) >= t_.sleepAfter) {
            wants_ = true;
            return SleepEvent::GoToSleep;
        }
        return SleepEvent::None;
    }

    // Nach dem Aufwachen aufrufen. Startet das Einschwingfenster und laesst
    // den Ruhe-Zeitgeber neu anlaufen - ohne das schliefe das Geraet
    // unmittelbar wieder ein, weil tQuiet_ noch aus der Zeit vor dem Schlaf
    // stammt.
    void wake(uint32_t now_ms) {
        wants_    = false;
        settling_ = true;
        tWake_    = now_ms;
        tQuiet_   = now_ms;
    }

    // Waehrend des Einschwingens ist die Lageschaetzung noch nicht wieder
    // eingerastet; die Drehgeste haengt an ihr und bleibt so lange gesperrt.
    bool settling()   const { return settling_; }
    bool wantsSleep() const { return wants_; }

private:
    SleepTuning t_;
    uint32_t tQuiet_   = 0;
    uint32_t tWake_    = 0;
    bool     wants_    = false;
    bool     settling_ = false;
};
```

- [ ] **Step 4: Test laufen lassen und Bestehen bestaetigen**

```powershell
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/SleepPolicy -o "$env:TEMP\sleep.exe" test/test_sleep_policy.cpp
& "$env:TEMP\sleep.exe"
```

Erwartet: `... Pruefungen, 0 Fehler`, Exit 0.

- [ ] **Step 5: `-I lib/SleepPolicy` in `platformio.ini` ergaenzen**

Nach `-I lib/TwistGuard`:

```
    -I lib/SleepPolicy
```

- [ ] **Step 6: Firmware bauen**

Erwartet: `SUCCESS`. Das Modul ist noch nicht verdrahtet, es wird nur gefunden.

- [ ] **Step 7: Commit**

```bash
git add lib/SleepPolicy/SleepPolicy.h test/test_sleep_policy.cpp platformio.ini
git commit -m "$(cat <<'EOF'
feat: SleepPolicy - entscheidet, wann das Geraet ruht

Hardwarefrei und auf dem PC geprueft. Wichtigste Eigenschaft: im
eingeschalteten Zustand wird nie geschlafen - beim ruhigen Zielen ist die
Drehrate klein, und die Maus duerfte dabei nicht verschwinden.

Noch nicht verdrahtet.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Die Schleife schlaeft zwischen den Takten

Der groesste Einzelposten des ganzen Plans, und er kostet nichts an Verhalten.

`loop()` kehrt heute sofort zurueck, wenn der Takt noch nicht faellig ist. Der Kern ruft es daraufhin sofort wieder auf (`while (1) { loop(); yield(); }`), der Cortex-M4F laeuft also durchgehend mit 64 MHz und tut in rund 97 % der Zeit nichts.

`delay()` ruft in diesem Kern `vTaskDelay`, und FreeRTOS laeuft mit `configUSE_TICKLESS_IDLE = 1` — der Kern schlaeft waehrend dieser Zeit also wirklich.

**Files:**
- Modify: `src/main.cpp`, `include/config.h`

**Interfaces:**
- Produces: nichts nach aussen.

- [ ] **Step 1: Konstante in `config.h` ergaenzen**

Direkt nach `constexpr float DT = ...`:

```cpp
    // Ab welchem Rest die Schleife schlafen legt statt zu warten. Die
    // FreeRTOS-Aufloesung betraegt 1 ms, der Takt muss aber auf wenige
    // Mikrosekunden genau bleiben - deshalb wird die letzte Millisekunde
    // bewusst abgewartet und nicht verschlafen.
    constexpr int32_t SLEEP_MIN_REST_US = 1500;
```

- [ ] **Step 2: Den Leerlauf durch Schlaf ersetzen**

In `src/main.cpp` den Kopf von `loop()` ersetzen:

```cpp
void loop() {
    // Warten statt leer durchlaufen. Der Kern ruft loop() in einer engen
    // Schleife auf; ein sofortiges return hiesse 64 MHz Volllast fuer
    // nichts. delay() ruft vTaskDelay, und mit configUSE_TICKLESS_IDLE
    // schlaeft der Kern dabei tatsaechlich.
    int32_t restUs = (int32_t)(nextSample_us - micros());
    if (restUs > cfg::SLEEP_MIN_REST_US) {
        delay((restUs - 1000) / 1000);
        restUs = (int32_t)(nextSample_us - micros());
    }
    // Die letzte Millisekunde genau abwarten - die FreeRTOS-Aufloesung
    // reicht dafuer nicht.
    while ((int32_t)(nextSample_us - micros()) > 0) { }

    const uint32_t now_us = micros();
```

Die bisherige Zeile `if ((int32_t)(now_us - nextSample_us) < 0) return;` entfaellt — sie ist durch das Warten ersetzt. Der Rest von `loop()` bleibt unveraendert, einschliesslich der Overrun-Korrektur.

- [ ] **Step 3: Firmware bauen**

Erwartet: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp include/config.h
git commit -m "$(cat <<'EOF'
perf: die Schleife schlaeft zwischen den Takten statt leer durchzulaufen

Der Kern rief loop() in einer engen Schleife auf; ein sofortiges return hiess
64 MHz Volllast fuer nichts - rund 97 Prozent der Zeit. delay() ruft
vTaskDelay, und mit configUSE_TICKLESS_IDLE schlaeft der Kern dabei wirklich.
Die letzte Millisekunde wird weiter abgewartet, weil die FreeRTOS-Aufloesung
fuer einen 4785-us-Takt nicht reicht.

Groesster Einzelposten der Stromsparmassnahmen, ohne Aenderung am Verhalten.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

**Nach diesem Task ist der erste Messschritt fällig** (`ovr` muss weiterhin bei 0 bleiben) — siehe Task 10.

---

## Task 4: DC/DC-Regler und stillgelegte Peripherie

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Produces: nichts nach aussen.

- [ ] **Step 1: Regler und Mikrofon in `setup()`**

In `src/main.cpp` ganz oben `#include <nrf_soc.h>` ergaenzen, und in `setup()` als erstes:

```cpp
    // Der nRF52840 startet mit dem LDO; der DC/DC-Wandler spart bei Last bis
    // zu etwa 30 Prozent. Bei aktiver SoftDevice darf das Register nicht
    // direkt beschrieben werden - dort ist sd_power_dcdc_mode_set der
    // richtige Weg. Der Rueckfall auf den Registerzugriff greift, solange
    // die SoftDevice nicht laeuft (USB-Zweig).
    if (sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE) != NRF_SUCCESS) {
        NRF_POWER->DCDCEN = 1;
    }

    // Das Mikrofon wird nie benutzt. Seine Versorgung liegt auf einem
    // eigenen Pin und bleibt aktiv abgeschaltet.
    pinMode(PIN_PDM_PWR, OUTPUT);
    digitalWrite(PIN_PDM_PWR, LOW);
```

- [ ] **Step 2: Firmware in beiden HID-Zweigen bauen**

Zuerst wie committet (`USE_BLE_HID false`), dann `USE_BLE_HID` auf `true` setzen, erneut bauen, **und wieder auf `false` zuruecksetzen**. Der `sd_power_dcdc_mode_set`-Pfad verhaelt sich in beiden Zweigen verschieden: ohne SoftDevice liefert der SVC-Aufruf einen Fehler und der Rueckfall greift, mit SoftDevice greift der erste Pfad. Beide muessen uebersetzen.

Erwartet: zweimal `SUCCESS`.

Faellt `sd_power_dcdc_mode_set` im USB-Zweig nicht durch den Compiler (Symbol nicht gefunden), **stoppen und melden** — dann ist der SoftDevice-Header dort nicht verfuegbar und der Aufruf muss hinter `#if USE_BLE_HID`.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "$(cat <<'EOF'
perf: DC/DC-Regler einschalten, Mikrofon-Versorgung abschalten

Der DC/DC-Wandler spart bei Last bis zu 30 Prozent gegenueber dem LDO. Bei
aktiver SoftDevice muss er ueber sd_power_dcdc_mode_set gesetzt werden, ein
direkter Registerzugriff waere dort wirkungslos.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: `ImuReader` — Abtastrate umschalten und Wake-Up scharfstellen

**Files:**
- Modify: `lib/ImuReader/ImuReader.h`, `include/config.h`

**Interfaces:**
- Produces: `enum class ImuRate : uint8_t { Active, Ready, Sleep }`
- Produces: `void ImuReader::setRate(ImuRate r)`, `void ImuReader::enableWakeOnMotion()`, `void ImuReader::disableWakeOnMotion()`

- [ ] **Step 1: Konstanten in `config.h` ergaenzen**

Im Sensor-Block, nach `GYRO_ODR_HZ`:

```cpp
    // Abtastraten der drei Betriebszustaende. AKTIV muss 208 Hz sein, weil
    // die Verarbeitung mit SAMPLE_INTERVAL_US laeuft und das ML-Fenster
    // daran haengt. BEREIT braucht nur die Drehgeste, die ueber rund eine
    // Sekunde laeuft - 52 Hz sind dafuer reichlich. Im Schlaf laeuft der
    // Beschleunigungssensor nur noch fuer die Wake-Up-Funktion.
    constexpr uint32_t READY_INTERVAL_US = 19230;   // 52 Hz
    constexpr float    READY_DT          = READY_INTERVAL_US * 1e-6f;

    // Schwelle der Wake-Up-Funktion, 6 Bit. Ein Schritt entspricht einem
    // Vierundsechzigstel des Messbereichs, bei +-4 g also rund 62 mg.
    // Startwert 2 = rund 125 mg: Armheben weckt, ein Klopfen auf den Tisch
    // moeglichst nicht. Am Geraet nachziehen.
    constexpr uint8_t WAKE_UP_THRESHOLD = 2;
```

- [ ] **Step 2: `setRate()` einbauen**

In `lib/ImuReader/ImuReader.h`, oberhalb der Klasse:

```cpp
enum class ImuRate : uint8_t { Active, Ready, Sleep };
```

Und als oeffentliche Methoden:

```cpp
    // Nur das ODR-Nibble wird veraendert, der Rest der Register bleibt
    // stehen: dort sitzen Messbereich und Bandbreite, die begin() gesetzt
    // hat und die sich nicht mit der Rate aendern sollen.
    void setRate(ImuRate r) {
        uint8_t odrXl = 0x50;   // 208 Hz
        uint8_t odrG  = 0x50;   // 208 Hz
        if (r == ImuRate::Ready) { odrXl = 0x30; odrG = 0x30; }   // 52 Hz
        // Im Schlaf laeuft nur der Beschleunigungssensor weiter, und der
        // nur fuer die Wake-Up-Funktion. Das Gyroskop ist der groessere
        // Verbraucher der beiden und wird zum Wecken nicht gebraucht.
        if (r == ImuRate::Sleep)  { odrXl = 0x20; odrG = 0x00; }   // 26 Hz / aus

        setOdr(LSM6DS3_ACC_GYRO_CTRL1_XL, odrXl);
        setOdr(LSM6DS3_ACC_GYRO_CTRL2_G,  odrG);
    }
```

Und als private Hilfe:

```cpp
    void setOdr(uint8_t reg, uint8_t odrBits) {
        uint8_t v = 0;
        imu_.readRegister(&v, reg);
        imu_.writeRegister(reg, (uint8_t)((v & 0x0F) | odrBits));
    }
```

- [ ] **Step 3: Wake-Up einbauen**

Als oeffentliche Methoden. Die Registerfolge ist dem Beispiel `FreeFallDetect` der Bibliothek nachgebildet, das dasselbe Interrupt-Muster benutzt — dort `MD1_CFG = 0x10` fuer Free-Fall, hier `0x20` fuer Wake-Up:

```cpp
    // Weckt ueber INT1, sobald sich die Beschleunigung um mehr als die
    // Schwelle aendert. TAP_CFG1 Bit 7 gibt die einfachen Interrupts
    // ueberhaupt erst frei; ohne dieses Bit bleibt INT1 stumm.
    void enableWakeOnMotion() {
        imu_.writeRegister(LSM6DS3_ACC_GYRO_WAKE_UP_DUR, 0x00);
        imu_.writeRegister(LSM6DS3_ACC_GYRO_WAKE_UP_THS, cfg::WAKE_UP_THRESHOLD);
        imu_.writeRegister(LSM6DS3_ACC_GYRO_MD1_CFG,     0x20);   // INT1_WU
        imu_.writeRegister(LSM6DS3_ACC_GYRO_TAP_CFG1,    0x80);   // Interrupts frei
    }

    void disableWakeOnMotion() {
        imu_.writeRegister(LSM6DS3_ACC_GYRO_MD1_CFG,  0x00);
        imu_.writeRegister(LSM6DS3_ACC_GYRO_TAP_CFG1, 0x00);
        // Die Quelle einmal lesen, damit ein noch anstehendes Ereignis
        // geloescht ist und INT1 nicht gleich wieder ausloest.
        uint8_t dummy = 0;
        imu_.readRegister(&dummy, LSM6DS3_ACC_GYRO_WAKE_UP_SRC);
        (void)dummy;
    }
```

- [ ] **Step 4: Firmware bauen**

Erwartet: `SUCCESS`. Die neuen Methoden sind noch nicht aufgerufen.

Sind `LSM6DS3_ACC_GYRO_CTRL1_XL` oder `LSM6DS3_ACC_GYRO_CTRL2_G` nicht definiert, **stoppen und melden** statt einen Namen zu raten — die vorhandenen Namen stehen in `.pio/libdeps/xiaoblesense/Seeed Arduino LSM6DS3/LSM6DS3.h`.

- [ ] **Step 5: Commit**

```bash
git add lib/ImuReader/ImuReader.h include/config.h
git commit -m "$(cat <<'EOF'
feat: IMU-Abtastrate umschaltbar, Wake-Up-Interrupt auf INT1

setRate() aendert nur das ODR-Nibble und laesst Messbereich und Bandbreite
stehen. Im Schlaf laeuft nur der Beschleunigungssensor mit 26 Hz weiter, das
Gyroskop ist abgeschaltet - es wird zum Wecken nicht gebraucht und ist der
groessere Verbraucher.

Registerfolge nach dem FreeFallDetect-Beispiel der Bibliothek.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: `MouseHID` — Funk ab- und anschalten

**Files:**
- Modify: `lib/MouseHID/MouseHID.h`

**Interfaces:**
- Produces: `void MouseHID::radioOff()`, `void MouseHID::radioOn()`

- [ ] **Step 1: Im BLE-Zweig**

```cpp
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
```

- [ ] **Step 2: Im USB-Zweig**

```cpp
    // Ueber USB gibt es keinen Funk und keinen Ruhezustand - das Geraet
    // haengt an einer Stromquelle, und ein Abschalten wuerde die
    // Enumeration abwerfen. Leer statt eines #if an der Aufrufstelle.
    void radioOff() {}
    void radioOn()  {}
```

- [ ] **Step 3: Die `static_assert`-Liste ergaenzen**

Bei den bestehenden Signaturpruefungen am Dateiende:

```cpp
static_assert(std::is_same<decltype(&MouseHID::radioOff), void (MouseHID::*)()>::value,
              "MouseHID::radioOff() hat die falsche Signatur");
static_assert(std::is_same<decltype(&MouseHID::radioOn),  void (MouseHID::*)()>::value,
              "MouseHID::radioOn() hat die falsche Signatur");
```

Diese Pruefungen sind hier nicht Zierde: es wird immer nur einer der beiden Zweige kompiliert, der andere kann unbemerkt abdriften.

- [ ] **Step 4: Firmware in beiden HID-Zweigen bauen**

`USE_BLE_HID false` bauen, dann auf `true` bauen, dann **wieder auf `false` zuruecksetzen**. Beide Zweige muessen uebersetzen.

- [ ] **Step 5: Commit**

```bash
git add lib/MouseHID/MouseHID.h
git commit -m "$(cat <<'EOF'
feat: MouseHID kann den Funk ab- und anschalten

Im BLE-Zweig Verbindung trennen und Advertising stoppen, im USB-Zweig leer -
dort gibt es nichts abzuschalten. Signaturen per static_assert gesichert, weil
immer nur ein Zweig kompiliert wird.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Madgwick-Beta zur Laufzeit

Nach dem Schlaf ist die Lageschaetzung veraltet, und die Drehgeste haengt genau an ihr. Ein kurzzeitig erhoehtes Beta laesst den Filter auf die Schwerkraft einrasten.

**Files:**
- Modify: `lib/MadgwickAHRS/MadgwickAHRS.h`, `include/config.h`

**Interfaces:**
- Produces: `void MadgwickAHRS::setBeta(float b)`

- [ ] **Step 1: Konstante in `config.h`**

Neben `MADGWICK_BETA`:

```cpp
    // Nur fuer das Einschwingfenster nach dem Aufwachen. Waehrend des
    // Schlafs bekommt der Filter keine Samples; mit dem normalen Beta
    // brauchte er Sekunden, bis die Lage wieder stimmt - und die Drehgeste
    // haengt an genau diesem Winkel. Dauerhaft waere dieser Wert falsch: der
    // Filter wuerde dann bei jeder Handbewegung von der
    // Linearbeschleunigung mitgerissen.
    constexpr float MADGWICK_BETA_FAST = 0.5f;
```

- [ ] **Step 2: Setter einbauen**

In `lib/MadgwickAHRS/MadgwickAHRS.h` als oeffentliche Methode. Der Konstruktor setzt das Beta bereits; hier wird nur derselbe Member nachtraeglich setzbar:

```cpp
    void setBeta(float b) { beta = b; }
```

Heisst der Member anders als `beta`, den vorhandenen Namen verwenden — **nicht umbenennen**.

- [ ] **Step 3: Firmware bauen**

Erwartet: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add lib/MadgwickAHRS/MadgwickAHRS.h include/config.h
git commit -m "$(cat <<'EOF'
feat: Madgwick-Beta zur Laufzeit setzbar

Fuer das Einschwingfenster nach dem Aufwachen: waehrend des Schlafs bekommt
der Filter keine Samples, und die Drehgeste haengt an der Lageschaetzung.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Verdrahtung der drei Zustaende

Der Kern. Hier laufen `SleepPolicy`, die IMU-Rate, der Funk und der Schlaf der Schleife zusammen.

**Files:**
- Modify: `lib/AirMouseController/AirMouseController.h`, `src/main.cpp`

**Interfaces:**
- Consumes: `SleepPolicy` (Task 2), `ImuReader::setRate/enableWakeOnMotion/disableWakeOnMotion` (Task 5), `MouseHID::radioOff/radioOn` (Task 6), `MadgwickAHRS::setBeta` (Task 7).
- Produces: `bool AirMouseController::wantsSleep() const`, `void AirMouseController::prepareSleep()`, `void AirMouseController::onWake(uint32_t now_ms)`, `bool AirMouseController::activeRate() const`

- [ ] **Step 1: Der Controller bekommt die Policy**

`#include "SleepPolicy.h"` ergaenzen, Member `SleepPolicy sleep_;` anlegen. Der Controller braucht ausserdem eine Referenz auf den `ImuReader`, den er bisher nicht kennt — der Konstruktor wird erweitert:

```cpp
    AirMouseController(MouseHID& mouse, ImuReader& imu)
        : mouse_(mouse), imu_(imu), ahrs_(cfg::MADGWICK_BETA) {}
```

Member `ImuReader& imu_;` neben `MouseHID& mouse_;`. `#include "ImuReader.h"` ergaenzen.

- [ ] **Step 2: Die Policy im Takt bedienen**

In `update()`, am Ende — nach `debug(...)`, damit der Schlafwunsch erst nach der vollstaendigen Verarbeitung des Takts entsteht:

```cpp
        switch (sleep_.tick(fsm_.on(), s.gyroSum, now_ms)) {
            case SleepEvent::GoToSleep:
                // Nur vorbereiten. Das Schlafenlegen selbst gehoert
                // main.cpp - dort haengt der Takt dran, der danach neu
                // ausgerichtet werden muss.
                prepareSleep();
                break;
            case SleepEvent::Settled:
                ahrs_.setBeta(cfg::MADGWICK_BETA);
                break;
            case SleepEvent::None:
                break;
        }
```

- [ ] **Step 3: Die Drehgeste waehrend des Einschwingens sperren**

Der Aufruf von `twistToggle_.tick(...)` bekommt das `level`-Argument entzogen, solange eingeschwungen wird:

```cpp
        // Waehrend des Einschwingens ist der Verdrehungswinkel noch nicht
        // verlaesslich. Ueber level = false verwirft TwistToggle eine
        // laufende Ausdrehung ohnehin - es braucht dafuer keinen neuen
        // Mechanismus im Modul.
        const bool levelOk = pose_.level() && !sleep_.settling();
        switch (twistToggle_.tick(pose_.relTwistDeg(), levelOk, now_ms)) {
```

- [ ] **Step 4: Die drei neuen Methoden**

Oeffentlich:

```cpp
    bool wantsSleep() const { return sleep_.wantsSleep(); }

    // Reihenfolge ist wichtig: erst der Funk, dann die IMU. Umgekehrt liefe
    // der Funk noch, waehrend die IMU schon nichts mehr meldet.
    void prepareSleep() {
        mouse_.radioOff();
        imu_.setRate(ImuRate::Sleep);
        imu_.enableWakeOnMotion();
    }

    void onWake(uint32_t now_ms) {
        imu_.disableWakeOnMotion();
        imu_.setRate(ImuRate::Active);
        mouse_.radioOn();
        // Erhoehtes Beta, damit die Lage nach dem Schlaf schnell wieder auf
        // die Schwerkraft einrastet. SleepEvent::Settled stellt es zurueck.
        ahrs_.setBeta(cfg::MADGWICK_BETA_FAST);
        sleep_.wake(now_ms);
    }

    // AKTIV laeuft mit voller Rate, BEREIT gedrosselt. main.cpp richtet den
    // Takt danach aus.
    bool activeRate() const { return fsm_.on(); }
```

- [ ] **Step 5: Die Rate dem Zustand folgen lassen**

Der Wechsel zwischen AKTIV und BEREIT geschieht beim Ein- und Ausschalten. In `apply()`, am Ende:

```cpp
        // Die Abtastrate folgt dem Ein/Aus-Zustand. Nur beim Wechsel
        // schreiben, nicht in jedem Takt - ein I2C-Zugriff je Takt waere
        // genau das Gegenteil von sparsam.
        if (fsm_.on() != imuRateActive_) {
            imuRateActive_ = fsm_.on();
            imu_.setRate(imuRateActive_ ? ImuRate::Active : ImuRate::Ready);
        }
```

Member `bool imuRateActive_ = false;` — der Startzustand ist ausgeschaltet, also BEREIT.

**Name bewusst nicht `rateActive_`:** er stuende zu nah an `activeRate()` aus Step 4, und die beiden bedeuten Verschiedenes. `activeRate()` sagt, welche Taktlaenge `main.cpp` waehlen soll; `imuRateActive_` merkt sich, was der IMU zuletzt gesagt wurde.

**Ein Takt Versatz ist eingeplant:** `main.cpp` liest `activeRate()` am Anfang des Takts, die IMU-Rate wechselt aber mitten drin in `apply()`. Beim Ein- oder Ausschalten passen Taktlaenge und Sensorrate deshalb fuer genau einen Durchlauf nicht zusammen. Das korrigiert sich im naechsten Takt von selbst und ist billiger, als beides kuenstlich zu synchronisieren.

- [ ] **Step 6: `main.cpp` — variables `dt`, Schlaf, Taktausrichtung**

Die globale Instanz erhaelt die IMU:

```cpp
AirMouseController app(mouse, imu);
```

Eine Datei-statische Variable fuer die aktuelle Taktlaenge:

```cpp
static uint32_t tickUs = cfg::READY_INTERVAL_US;   // startet ausgeschaltet
```

Im `loop()` die feste Schrittweite durch die variable ersetzen — `cfg::SAMPLE_INTERVAL_US` wird zu `tickUs` und `cfg::DT` zu `tickDt`, wobei:

```cpp
    // Die Taktlaenge folgt dem Zustand: AKTIV braucht die 209 Hz des
    // ML-Modells, BEREIT nur die Drehgeste. Der Klassifikator laeuft
    // ausschliesslich in AKTIV, wo weiterhin exakt SAMPLE_INTERVAL_US gilt.
    tickUs = app.activeRate() ? cfg::SAMPLE_INTERVAL_US : cfg::READY_INTERVAL_US;
    const float tickDt = app.activeRate() ? cfg::DT : cfg::READY_DT;
```

Diese beiden Zeilen stehen **vor** `nextSample_us += tickUs;`. `imu.read(...)` und `app.update(...)` bekommen `tickDt` statt `cfg::DT`.

Am Ende von `loop()`, nach `app.update(...)`:

```cpp
    if (app.wantsSleep()) {
        attachInterrupt(digitalPinToInterrupt(PIN_LSM6DS3TR_C_INT1), onMotion, RISING);
        suspendLoop();                 // hier bleibt die Task stehen
        detachInterrupt(digitalPinToInterrupt(PIN_LSM6DS3TR_C_INT1));
        app.onWake(millis());
        // Nach dem Schlaf liegt nextSample_us beliebig weit in der
        // Vergangenheit. Ohne Neuausrichtung liefe die Schleife erst
        // tausende Overrun-Korrekturen ab, bevor sie wieder im Takt ist.
        nextSample_us = micros() + cfg::SAMPLE_INTERVAL_US;
    }
```

Und die ISR, oberhalb von `loop()`:

```cpp
// Tut absichtlich nur eines. Alles Weitere geschieht in der Task, sobald sie
// wieder laeuft - I2C und BLE haben in einer ISR nichts verloren.
static void onMotion() { resumeLoop(); }
```

`pinMode(PIN_LSM6DS3TR_C_INT1, INPUT)` gehoert in `setup()`.

- [ ] **Step 7: Alle sieben PC-Tests und die Firmware bauen**

Erwartet: siebenmal `0 Fehler` und `SUCCESS`. Danach `USE_BLE_HID true` bauen, `SUCCESS` bestaetigen, **und zurueckstellen**.

- [ ] **Step 8: Commit**

```bash
git add lib/AirMouseController/AirMouseController.h src/main.cpp
git commit -m "$(cat <<'EOF'
feat: drei Betriebszustaende - aktiv, bereit, schlafend

Die Abtastrate folgt dem Ein/Aus-Zustand (208 / 52 Hz), und nach 60 s Ruhe
legt sich die Schleife ueber suspendLoop() schlafen; geweckt wird ueber den
Wake-Up-Interrupt der IMU auf INT1. Es wird nichts gepollt.

Nie geschlafen wird im eingeschalteten Zustand. Nach dem Aufwachen laeuft
Madgwick kurz mit erhoehtem Beta, und die Drehgeste bleibt so lange gesperrt -
sie haengt an einer Lageschaetzung, die im Schlaf keine Samples bekam.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: Build-Flags

Ehrlich eingeordnet: der kleinste Posten des Plans. Die Zahlen entstehen bei Schleife, IMU-Rate und Funk, nicht beim Optimierer.

**Files:**
- Modify: `platformio.ini`

- [ ] **Step 1: Flags ergaenzen**

Nach dem `-O2`-Block:

```
    ; Ungenutzten Code aus dem Binary werfen. Das schrumpft den Flash-Bedarf,
    ; ist aber ausdruecklich KEINE Stromsparmassnahme - ein kleineres Binary
    ; verbraucht im Betrieb nicht weniger.
    ;
    ; -Os waere hier falsch: langsamerer Code hiesse mehr Wachzeit, und die
    ; Wachzeit ist der Posten, um den es geht. Kein -flto, weil das EI-SDK
    ; gross und generiert ist und ein Fehler dort schwer zuzuordnen waere.
    -ffunction-sections
    -fdata-sections
    -Wl,--gc-sections
    ; Schaltet assert() im Edge-Impulse-SDK ab.
    -DNDEBUG
```

- [ ] **Step 2: Firmware bauen und die Groesse vergleichen**

Erwartet: `SUCCESS`. Die Flash-Zeile mit dem Wert vor dieser Aenderung vergleichen und beide im Report festhalten — die Ersparnis ist die einzige belegbare Wirkung dieses Tasks.

- [ ] **Step 3: Alle sieben PC-Tests laufen lassen**

Die Flags betreffen nur die Firmware, aber `-DNDEBUG` kann Verhalten aendern, wenn irgendwo ein `assert()` mit Nebenwirkung steht. Erwartet: siebenmal `0 Fehler`.

- [ ] **Step 4: Commit**

```bash
git add platformio.ini
git commit -m "$(cat <<'EOF'
build: gc-sections und NDEBUG

Schrumpft das Binary. Ausdruecklich keine Stromsparmassnahme - -O2 bleibt
bewusst stehen, weil schnellerer Code mehr Schlafzeit bedeutet.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: Dokumentation und Messplan

**Files:**
- Modify: `CLAUDE.md`, `TODO.md`, `docs/Programmcode.md`

- [ ] **Step 1: `CLAUDE.md`**

- Bei den Policy-Modulen `SleepPolicy` ergaenzen, bei den hardwarefreien PC-Tests `test_sleep_policy.cpp` mit `-I lib/SleepPolicy` und `SleepTuning` bei den Tuning-Structs.
- Im Ablauf pro Tick ergaenzen, dass am Ende `SleepPolicy` befragt wird und `main.cpp` den Schlaf ausfuehrt.
- Einen kurzen Abschnitt „Betriebszustaende" mit der Tabelle AKTIV / BEREIT / SCHLAF.
- Den Hinweis, dass `cfg::DT` nicht mehr die einzige Schrittweite ist: in BEREIT gilt `READY_DT`, und der ML-Pfad laeuft ausschliesslich in AKTIV.

- [ ] **Step 2: `docs/Programmcode.md`**

Einen neuen Abschnitt zwischen „Konfiguration" und „Testbarkeit" einfuegen, der die drei Betriebszustaende, das Aufwachen ueber INT1 und das Einschwingen nach dem Schlaf erklaert — im selben erklaerenden Ton wie der Rest der Datei, mit dem *Warum*: warum kein System OFF (Reset kostet den gelernten Gyro-Nullpunkt und die BLE-Verbindung), warum die Schleife der groesste Posten war, und warum die Compiler-Flags es nicht sind. `lib/Battery/` und `lib/SleepPolicy/` in der Modulliste ergaenzen.

- [ ] **Step 3: `TODO.md` — der Messplan**

Am Ende anfuegen:

```markdown
## Messplan Stromsparen

Alle Messungen mit `DEBUG_TELEPLOT false`, sonst misst man den Messaufbau.
Der Kanal `vbat` steht dafür auch ohne Teleplot zur Verfügung, wenn man ihn
einzeln einschaltet.

1. **`BATTERY_VOLTS_PER_LSB` kalibrieren.** Akkuspannung mit dem Multimeter
   messen und gegen `vbat` halten, Faktor nachziehen. Alles Weitere hängt an
   dieser Zahl.
2. **`ovr` nach dem Schleifen-Umbau.** Muss bei 0 bleiben. Steigt er, schläft
   die Schleife zu lange und die feste Schrittweite stimmt nicht mehr.
3. **Stromaufnahme je Zustand**, Multimeter in Serie: AKTIV, BEREIT, SCHLAF.
   Gegen die Schätzwerte im Design halten (~2–3 mA / ~1 mA / ~0.03–0.05 mA).
4. **`WAKE_UP_THRESHOLD` einstellen.** Armheben muss wecken, ein Klopfen auf
   den Tisch nicht. Startwert 2.
5. **Aufwachen prüfen.** Arm ablegen, 60 s warten, Arm heben: Wacht es auf?
   Wie lange bis BLE wieder steht? Funktioniert die Drehgeste unmittelbar
   danach, oder schlägt das Einschwingen durch (`rtwist` beobachten)?
6. **Falsches Einschlafen ausschliessen.** Maus einschalten, Cursor zwei
   Minuten ruhig auf einem Ziel halten. Sie darf nicht verschwinden.
7. **Entladekurve**, je einmal für den Stand vor und nach diesem Plan, unter
   gleichem Nutzungsmuster. Das ist die belastbare Zahl für die Arbeit —
   Laufzeit vorher gegen nachher.
8. **Offene Messpunkte, bewusst nicht entschieden:** BLE-Verbindungsintervall
   (7.5–15 ms, teuer aber latenzentscheidend) und Sendeleistung
   (`setTxPower(4)`, Maximum). Beide erst messen, dann entscheiden.
```

- [ ] **Step 4: Alle sieben PC-Tests und die Firmware bauen**

Erwartet: siebenmal `0 Fehler` und `SUCCESS`.

- [ ] **Step 5: Commit**

```bash
git add CLAUDE.md TODO.md docs/Programmcode.md
git commit -m "$(cat <<'EOF'
docs: Betriebszustaende dokumentiert, Messplan Stromsparen

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Nach dem Plan

Der Plan endet mit gruenen Tests und einem gebauten Binary — **nicht** mit einer belegten Ersparnis. Die Zahlen entstehen erst am Geraet, und der Vergleich braucht die Entladekurve des alten Standes: **die sollte aufgenommen sein, bevor Task 3 geflasht wird.** Task 1 steht genau deshalb an erster Stelle.
