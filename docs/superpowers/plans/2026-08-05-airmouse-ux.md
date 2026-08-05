# Air Mouse UX-Ueberarbeitung — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ein/Aus laeuft ueber eine Drehgeste statt ueber Schuetteln, der Rechtsklick funktioniert in der abgedrehten Haltung, und der Cursor wackelt nicht mehr.

**Architecture:** Die Firmware behaelt ihren Aufbau: header-only Policy-Module in `lib/<Name>/`, ein duenner `main.cpp`, `AirMouseState` als einzige Zustandsablage und `AirMouseController::apply()` als einzige Stelle mit Seiteneffekten. Neu sind zwei Dinge: `lib/TwistToggle/` ersetzt `lib/ShakeToggle/`, und `ImuSample` fuehrt zusaetzlich die gravitationsfreie Beschleunigung, damit das ML-Modell die Handhaltung nicht mehr sieht.

**Tech Stack:** C++14, PlatformIO / Arduino (Seeed XIAO nRF52840 Sense), Edge Impulse SDK, Adafruit TinyUSB (USB-HID) bzw. bluefruit (BLE-HID), g++ fuer die PC-Tests.

**Spec:** `docs/superpowers/specs/2026-08-05-airmouse-ux-design.md`

## Global Constraints

- **Kommentare auf Deutsch, ASCII ohne Umlaute** (`waehrend`, `Verzoegerung`, `Rueckstau`). Sie begruenden das *Warum* einer Entscheidung, oft mit Literaturverweis. Dieser Stil ist Teil der Maturaarbeit und beim Aendern von Code beizubehalten.
- **Kein `new`/`malloc`, keine `String`, keine dynamischen Container im Hot Path.** Feste Puffer und `float`.
- **Alle Zahlenwerte als `constexpr` in `namespace cfg`** (`include/config.h`). Ausnahme: Module, die auf dem PC testbar sein muessen, duerfen `config.h` nicht einbinden und tragen ihre Werte in einem Tuning-Struct (`PointerTuning`, `TwistTuning`). Diese Ausnahme ist in der jeweiligen Datei zu begruenden.
- **Policy-Module kennen weder Hardware noch das EI-SDK.** Nichts aus `lib/ei-model` oder `bluefruit` gehoert in `AirMouseState`, `ArmOrientation`, `PinchDetector`, `PoseDetector`, `ScrollJoystick`, `TwistToggle`, `OrientationPointer` oder `Filters/`.
- **Neues Modul → Ordner `lib/<Name>/` anlegen UND `-I lib/<Name>` in `platformio.ini` ergaenzen.** Ohne den zweiten Schritt findet der Compiler den Header nicht.
- **Die Kanal-Reihenfolge des Modells steht ausschliesslich in `feat::pack()`** (`lib/PinchFeatures/PinchFeatures.h`). Nur Task 1 darf sie anfassen.
- **Feste Schrittweite `cfg::DT`** — die Verarbeitung rechnet nie mit der gemessenen Zeitdifferenz. `cfg::SAMPLE_INTERVAL_US = 4785` ist an `EI_CLASSIFIER_INTERVAL_MS` gekoppelt und per `static_assert` gesichert. **Nicht aendern.**
- **Nie zwei `pio run` gleichzeitig** auf dasselbe `.pio/build`. Der Abbruch `Fatal error: can't create ... .o: No such file or directory` sieht nach einem Codefehler aus und ist nur die Kollision.
- **Nicht flashen.** `pio run -t upload` wird nicht ausgefuehrt — Nils flasht selbst. Tasks enden mit einem erfolgreichen Build, nicht mit einem Upload.

### Befehle

Bauen (Ausgabe kuerzen, interessant sind nur die RAM/Flash-Zeile und `SUCCESS`):

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run 2>&1 | Select-Object -Last 20
```

PC-Test (Beispiel; `g++` liegt unter `C:\Strawberry\c\bin`):

```powershell
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/AirMouseState -o "$env:TEMP\fsm.exe" test/test_state_machine.cpp
& "$env:TEMP\fsm.exe"
```

Ein PC-Test gilt als bestanden, wenn er `0 Fehler` meldet und mit Exit-Code 0 endet. Die Zahl der Pruefungen aendert sich beim Umschreiben von Tests — sie ist kein Abnahmekriterium, `0 Fehler` schon.

---

## File Structure

| Datei | Verantwortung | Task |
|---|---|---|
| `lib/ImuReader/ImuSample.h` | Grenzstruktur Treiber → Verarbeitung; fuehrt neu `lax/lay/laz` | 1 |
| `lib/ImuReader/ImuReader.h` | Sensortreiber, Gyro-Bias, Gravitationsabzug, Burst-Read | 1, 5, 6 |
| `lib/PinchFeatures/PinchFeatures.h` | einzige Stelle mit der Kanal-Reihenfolge des Modells | 1 |
| `test/test_pinch_features.cpp` | *neu* — sichert Kanalzahl und -reihenfolge | 1 |
| `src/main.cpp` | Takt, `COLLECT_MODE`, Overrun-Anzeige | 2 |
| `lib/Filters/OneEuro.h` | 1-Euro-Filter, neu mit Geschwindigkeits-Tiefpass | 3 |
| `test/test_one_euro.cpp` | *neu* — Tremor-Daempfung und Sprungantwort | 3 |
| `include/config.h` | alle `cfg::`-Konstanten und Compile-Schalter | 4, 5, 7, 8, 10, 12, 15 |
| `lib/OrientationPointer/OrientationPointer.h` | Cursor-Kennlinie, `PointerTuning` | 4 |
| `lib/AirMouseController/AirMouseController.h` | Verdrahtung, `apply()`, Teleplot | 6, 7, 9, 12, 14 |
| `lib/PinchDetector/PinchDetector.h` | env-Gate, Entprellung, Freeze | 8 |
| `lib/TwistGuard/TwistGuard.h` | *neu* — bremst den Cursor waehrend der Unterarmdrehung | 9 |
| `test/test_twist_guard.cpp` | *neu* | 9 |
| `lib/Haptic/Haptic.h` | Impuls-Sequenzer | 10 |
| `lib/TwistToggle/TwistToggle.h` | *neu* — Ein/Aus-Drehgeste, ersetzt `ShakeToggle` | 11 |
| `test/test_twist_toggle.cpp` | *neu* | 11 |
| `lib/PoseDetector/PoseDetector.h` | Haltung aus Verdrehung + Waagrecht-Gate, zwei geglaettete Winkel | 12 |
| `lib/ScrollJoystick/ScrollJoystick.h` | Scroll aus Armneigung, neu `inDeadzone()` | 13 |
| `lib/AirMouseState/AirMouseState.h` | Zustandsautomat, `Pose`, `Actions` | 12, 14 |
| `test/test_state_machine.cpp` | Test des Automaten | 12, 14 |
| `platformio.ini` | `-I`-Eintraege | 9, 11, 15 |
| `lib/PinchGesture/`, `lib/ShakeToggle/` | *geloescht* | 15 |
| `TODO.md`, `CLAUDE.md` | Arbeitsjournal, Projektanleitung | 15 |

**Reihenfolge und warum:** Tasks 1–2 zuerst, damit Nils mit der Datenaufnahme beginnen kann, sobald sie fertig sind — alles Weitere blockiert das nicht. Tasks 3–9 sind der Cursor (das spuerbarste Problem) und beruehren den Zustandsautomaten nicht. Tasks 10–14 bauen Geste und Haltungen um. Task 15 raeumt auf.

Task 9 (`TwistGuard`) steht bewusst **vor** der Drehgeste: ohne ihn reisst jede Ausdrehung den Cursor mit, und die Gesten aus Tasks 11–14 waeren am Geraet nicht sinnvoll zu beurteilen.

**Nach jedem Task ist der Firmware-Build gruen** — das ist der Grund fuer diesen Zuschnitt. Genau eine Ausnahme: **nach Task 3 ist er rot**, weil `OrientationPointer` den alten Zwei-Parameter-Konstruktor des Filters ruft. Task 4 stellt ihn im naechsten Schritt wieder her. Die Trennung ist gewollt: die Filteraenderung und ihre Verdrahtung sollen einzeln nachvollziehbar bleiben. Tasks 3 und 4 gehoeren deshalb zusammen ausgefuehrt und nicht ueber eine Pause getrennt.

---

## Task 1: Gravitationsfreie Kanaele fuers Modell

Das Modell sieht heute rohes `ax/ay/az` inklusive Erdbeschleunigung. In der Zeige-Haltung liegt die auf `az ≈ +1`, in der um 90 Grad abgedrehten Haltung auf `ax ≈ +1` — drei von fuenf Kanaelen haben dort einen ganz anderen Gleichanteil als im Training. Deshalb funktioniert der Rechtsklick am Geraet nicht. Der Abzug passiert in `ImuReader`, weil `COLLECT_MODE` und Inferenzpfad beide durch `ImuSample` gehen und so nicht auseinanderlaufen koennen.

**Files:**
- Modify: `lib/ImuReader/ImuSample.h`
- Modify: `lib/ImuReader/ImuReader.h`
- Modify: `lib/PinchFeatures/PinchFeatures.h`
- Create: `test/test_pinch_features.cpp`

**Interfaces:**
- Produces: `ImuSample` mit den zusaetzlichen Feldern `float lax, lay, laz`. Alle spaeteren Tasks lesen `s.lax` usw.; `s.ax/ay/az` bleiben roh und gehen unveraendert an Madgwick.
- Produces: `feat::pack(const ImuSample& s, float env, float* out)` fuellt `out[0..4]` mit `env, gyroSum/100, lax, lay, laz`.

- [ ] **Step 1: Den Test schreiben, der die Kanalbelegung festnagelt**

Neue Datei `test/test_pinch_features.cpp`:

```cpp
// Test der Kanalbelegung des Modells. Laeuft auf dem PC - PinchFeatures.h und
// ImuSample.h haengen bewusst an keiner Hardware.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/ImuReader -I lib/PinchFeatures \
//       -o build/feat.exe test/test_pinch_features.cpp && ./build/feat.exe
//
// Warum es diesen Test gibt: die Kanal-Reihenfolge steht nur an dieser einen
// Stelle, und eine dort vertauschte Achse gibt weder Compiler- noch
// Laufzeitfehler. Das Modell wird nur still schlechter, und in der Auswertung
// ist das von einem Modellproblem nicht zu unterscheiden.
#include "PinchFeatures.h"
#include <cstdio>
#include <cmath>

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

// Nicht "near" nennen: das ist auf Windows-Toolchains stellenweise ein Makro.
static bool istGleich(float a, float b) { return std::fabs(a - b) < 1e-5f; }

static void test_channelCount() {
    // Das Modell erwartet genau so viele Kanaele je Zeitschritt. Aendert sich
    // die Zahl, ist jedes trainierte Modell ungueltig.
    CHECK(feat::CHANNELS == 5, "Kanalzahl ist nicht mehr 5");
}

static void test_channelOrder() {
    ImuSample s{};
    s.ax  = 9.f;  s.ay  = 8.f;  s.az  = 7.f;    // roh: darf NICHT im Fenster landen
    s.lax = 1.f;  s.lay = 2.f;  s.laz = 3.f;    // linear: gehoert hinein
    s.gyroSum = 250.f;

    float f[feat::CHANNELS];
    feat::pack(s, 0.042f, f);

    CHECK(istGleich(f[0], 0.042f), "Kanal 0 ist nicht die Huellkurve");
    CHECK(istGleich(f[1], 2.5f),   "Kanal 1 ist nicht gyroSum/100");
    CHECK(istGleich(f[2], 1.f),    "Kanal 2 ist nicht lax");
    CHECK(istGleich(f[3], 2.f),    "Kanal 3 ist nicht lay");
    CHECK(istGleich(f[4], 3.f),    "Kanal 4 ist nicht laz");
}

static void test_rawAccelIsNotUsed() {
    // Der Kern der Aenderung: die Handhaltung darf im Fenster nicht sichtbar
    // sein. Dieselbe Bewegung, aber um 90 Grad verdrehte Erdbeschleunigung,
    // muss dasselbe Fenster ergeben.
    ImuSample flach{}, gedreht{};
    flach.lax = gedreht.lax = 0.10f;
    flach.lay = gedreht.lay = -0.05f;
    flach.laz = gedreht.laz = 0.02f;
    flach.gyroSum = gedreht.gyroSum = 30.f;
    flach.az   = 1.f;    // Hand gerade:   Erdbeschleunigung auf Z
    gedreht.ax = 1.f;    // Hand gedreht:  Erdbeschleunigung auf X

    float a[feat::CHANNELS], b[feat::CHANNELS];
    feat::pack(flach,   0.05f, a);
    feat::pack(gedreht, 0.05f, b);

    for (int i = 0; i < feat::CHANNELS; i++) {
        CHECK(istGleich(a[i], b[i]), "Handhaltung ist im Modell-Fenster sichtbar");
    }
}

int main() {
    test_channelCount();
    test_channelOrder();
    test_rawAccelIsNotUsed();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
```

- [ ] **Step 2: Test laufen lassen und Fehlschlag bestaetigen**

```powershell
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/ImuReader -I lib/PinchFeatures -o "$env:TEMP\feat.exe" test/test_pinch_features.cpp
```

Erwartet: **Compilerfehler**, `'struct ImuSample' has no member named 'lax'`. Das ist der gewuenschte Fehlschlag — die Felder gibt es noch nicht.

- [ ] **Step 3: `ImuSample` um die linearen Achsen erweitern**

`lib/ImuReader/ImuSample.h` vollstaendig ersetzen:

```cpp
#pragma once

// Grenzstruktur zwischen Treiber und Verarbeitung. Bewusst ohne Sensor-Typen
// und ohne Treiber-Header: wer nur die Messwerte braucht, soll nicht das
// halbe LSM6DS3-Interface mitkompilieren muessen.
struct ImuSample {
    // Roh, mit Erdbeschleunigung. Fuer Madgwick - ihm ist die
    // Erdbeschleunigung das Signal, aus dem er die Lage schaetzt.
    float ax, ay, az;

    // Linear: Erdbeschleunigung abgezogen. Fuer das ML-Fenster. Die rohen
    // Achsen tragen die Handhaltung als Gleichanteil mit sich - in der
    // Zeige-Haltung liegt die Erdbeschleunigung auf az, in der um 90 Grad
    // abgedrehten auf ax. Ein Modell, das mit den rohen Achsen trainiert
    // wurde, erkennt denselben Pinch in der anderen Haltung deshalb nicht
    // wieder. Ohne den Gleichanteil ist die Haltung fuer das Modell
    // unsichtbar, und ein Datensatz deckt beide ab.
    float lax, lay, laz;

    float gx, gy, gz;

    // Betrag der ROHEN Beschleunigung. Die Huellkurve arbeitet mit einem
    // Hochpass bei 30 Hz und entfernt den Gleichanteil selbst.
    float accMag;
    float gyroSum;
};
```

- [ ] **Step 4: `feat::pack()` auf die linearen Achsen umstellen**

In `lib/PinchFeatures/PinchFeatures.h` den Rumpf von `pack()` ersetzen und den Kommentarblock darueber ergaenzen:

```cpp
    // Aenderungen hier machen jedes bisher trainierte Modell ungueltig.
    //
    // Kanaele 2 bis 4 sind bewusst die LINEARE Beschleunigung und nicht die
    // rohe: die rohe traegt die Erdbeschleunigung und damit die Handhaltung
    // als Gleichanteil mit sich. Das Modell haette sie sonst je Haltung
    // getrennt lernen muessen - siehe ImuSample.h.
    inline void pack(const ImuSample& s, float env, float* out) {
        out[0] = env;
        out[1] = s.gyroSum / 100.f;  // auf eine aehnliche Groessenordnung wie g
        out[2] = s.lax;
        out[3] = s.lay;
        out[4] = s.laz;
    }
```

- [ ] **Step 5: Test laufen lassen und Bestehen bestaetigen**

```powershell
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/ImuReader -I lib/PinchFeatures -o "$env:TEMP\feat.exe" test/test_pinch_features.cpp
& "$env:TEMP\feat.exe"
```

Erwartet: `... Pruefungen, 0 Fehler`, Exit 0.

- [ ] **Step 6: `ImuReader` fuellt die linearen Achsen**

In `lib/ImuReader/ImuReader.h`: `#include "LowPass.h"` ergaenzen, drei Tiefpaesse als Member anlegen und in `read()` die Felder fuellen. `read()` bekommt folgenden Block direkt nach dem Setzen von `s.ax/ay/az`:

```cpp
        // Erdbeschleunigung schaetzen und abziehen. Eine gehaltene Haltung
        // aendert sich im Bereich unter 1 Hz, die Beschleunigung beim Zeigen
        // und beim Pinchen deutlich darueber - 0.8 Hz trennt beides. LowPass
        // setzt sich beim ersten Sample auf den Eingang, es gibt also keinen
        // Einschwinger beim Start.
        s.lax = s.ax - lpGx_.run(s.ax, dt);
        s.lay = s.ay - lpGy_.run(s.ay, dt);
        s.laz = s.az - lpGz_.run(s.az, dt);
```

Und bei den Membern:

```cpp
    LowPass lpGx_{cfg::GRAVITY_LP_HZ}, lpGy_{cfg::GRAVITY_LP_HZ}, lpGz_{cfg::GRAVITY_LP_HZ};
```

- [ ] **Step 7: Firmware bauen**

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run 2>&1 | Select-Object -Last 20
```

Erwartet: `SUCCESS`. Die RAM/Flash-Zeile notieren — sie darf sich nur minimal bewegen (drei zusaetzliche `float`-Paare).

- [ ] **Step 8: Commit**

```bash
git add lib/ImuReader/ImuSample.h lib/ImuReader/ImuReader.h lib/PinchFeatures/PinchFeatures.h test/test_pinch_features.cpp
git commit -m "$(cat <<'EOF'
feat: gravitationsfreie Beschleunigung fuers ML-Fenster

Die rohen Achsen tragen die Handhaltung als Gleichanteil mit sich. Ein in
Zeige-Haltung trainiertes Modell erkennt denselben Pinch in der um 90 Grad
abgedrehten Haltung deshalb nicht - das ist die Ursache dafuer, dass der
Rechtsklick am Geraet nicht funktioniert.

Der Abzug sitzt in ImuReader, weil COLLECT_MODE und Inferenzpfad beide durch
ImuSample gehen und so nicht auseinanderlaufen koennen.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: `COLLECT_MODE` fuer die Aufnahme vorbereiten

Verpasst die Schleife waehrend der Aufnahme Abtastschritte, ist das Fenster zeitlich gedehnt und der Datensatz unbrauchbar — ohne dass man es der CSV ansieht. Der Overrun-Zaehler ist heute per `#if DEBUG_TELEPLOT && !COLLECT_MODE` genau aus der Aufnahme herausdefiniert. Eine Meldung darf den CSV-Strom nicht stoeren, also geht sie auf die LED.

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `feat::pack()` und `ImuSample` aus Task 1.
- Produces: nichts, was spaetere Tasks brauchen.

- [ ] **Step 1: Overrun-Zaehler auch im `COLLECT_MODE` scharfschalten**

In `src/main.cpp` den Bedingungsblock am Dateikopf ersetzen:

```cpp
#if DEBUG_TELEPLOT || COLLECT_MODE
// Zaehlt, wie oft die Schleife einen ganzen Abtastschritt verpasst hat. Jeder
// Zaehlschritt bedeutet ein zeitlich gedehntes ML-Fenster - der Klassifikator
// bzw. das Training sieht dann etwas anderes als vorgesehen.
static uint16_t overruns = 0;
#endif
#if DEBUG_TELEPLOT && !COLLECT_MODE
// Nur der Teleplot-Pfad drosselt seine Ausgabe. Im COLLECT_MODE waere die
// Variable definiert und ungenutzt - das gibt eine Compiler-Warnung.
static uint32_t lastOvrDbg = 0;
#endif
```

Und im `loop()` die Bedingung um den `overruns++` auf `#if DEBUG_TELEPLOT || COLLECT_MODE` erweitern.

- [ ] **Step 2: Die LED-Anzeige einbauen**

In `setup()`, vor `nextSample_us = micros();`:

```cpp
#if COLLECT_MODE
    // Aufnahme-Warnleuchte. LED_BUILTIN des XIAO nRF52840 ist aktiv LOW:
    // HIGH ist aus. Sie geht an, sobald die Schleife einen Abtastschritt
    // verpasst hat, und bleibt bis zum Reset an. Leuchtet sie nach der
    // Aufnahme, ist der Datensatz zeitlich gedehnt und wird verworfen - der
    // CSV selbst sieht man das nicht an.
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);
#endif
```

Im `loop()`, im Overrun-Zweig direkt nach `overruns++`:

```cpp
    #if COLLECT_MODE
        digitalWrite(LED_BUILTIN, LOW);   // aktiv LOW: an, gelatcht bis zum Reset
    #endif
```

- [ ] **Step 3: Nachkommastellen der CSV anpassen**

Im `COLLECT_MODE`-Zweig die Ausgabeschleife ersetzen:

```cpp
    for (int i = 0; i < feat::CHANNELS; i++) {
        if (i) Serial.print(',');
        // Kanal 0 ist die Huellkurve. Sie bewegt sich zwischen 0.005
        // (Untergrund) und 0.125 (kraeftiger Pinch); bei drei Stellen bliebe
        // am unteren Ende eine einzige signifikante Ziffer uebrig, und genau
        // dort liegt die Schwelle ENV_ON. Die uebrigen vier Kanaele liegen
        // unter der Sensoraufloesung, drei Stellen genuegen - das spart bei
        // 209 Hz rund ein Fuenftel der Serial-Last.
        Serial.print(f[i], i == 0 ? 4 : 3);
    }
    Serial.println();
```

- [ ] **Step 4: Firmware in beiden Betriebsarten bauen**

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run 2>&1 | Select-Object -Last 20
```

Erwartet: `SUCCESS`. Danach in `include/config.h` `COLLECT_MODE` auf `true` setzen, erneut bauen, `SUCCESS` bestaetigen, **und wieder auf `false` zuruecksetzen**. Beide Zweige muessen uebersetzen — es wird immer nur einer kompiliert, der andere kann sonst unbemerkt abdriften.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "$(cat <<'EOF'
feat: COLLECT_MODE meldet verpasste Abtastschritte ueber die LED

Ein gedehntes Fenster sieht man der CSV nicht an, und eine Textmeldung wuerde
den Datenstrom stoeren. Die eingebaute LED bleibt ab dem ersten Overrun bis
zum Reset an - leuchtet sie nach der Aufnahme, wird verworfen.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

**Ab hier kann Nils Daten aufnehmen.** Die restlichen Tasks blockieren das nicht.

---

## Task 3: 1-Euro-Filter — Geschwindigkeits-Tiefpass wiederherstellen

Der Filter haelt das Handzittern fuer gewollte Bewegung. Tremor liegt bei 8–12 Hz mit 0.1–0.5 Grad Amplitude, also rund 12 Grad/s Drehrate; mit `cutoff = MIN_CUTOFF + BETA * |rate|` ergibt das 0.9 + 0.55 * 12 = 7.5 Hz, und bei 7.5 Hz passiert 10-Hz-Tremor fast ungedaempft. Casiez et al. 2012 filtern die Geschwindigkeit vor der Verwendung mit einem eigenen Tiefpass (`dcutoff`, Vorgabe 1 Hz). Beim Streichen der Ableitungsstufe ist dieser Tiefpass versehentlich mit verschwunden.

**Files:**
- Modify: `lib/Filters/OneEuro.h`
- Create: `test/test_one_euro.cpp`

**Interfaces:**
- Produces: `OneEuroFilter(float minCutoff, float beta, float dCutoff)` — dritter Parameter neu, **ohne Vorgabewert**, damit kein Aufrufer ihn versehentlich uebergeht. `float update(float x, float speed, float dt)` und `void reset()` bleiben in der Signatur unveraendert.

- [ ] **Step 1: Den Test schreiben**

Neue Datei `test/test_one_euro.cpp`:

```cpp
// Test des 1-Euro-Filters. Laeuft auf dem PC - OneEuro.h braucht nur <math.h>.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/Filters -o build/euro.exe test/test_one_euro.cpp && ./build/euro.exe
//
// Geprueft wird die Eigenschaft, um die es geht: Handzittern darf die
// Grenzfrequenz NICHT aufreissen, eine gehaltene Bewegung schon.
#include "OneEuro.h"
#include <cstdio>
#include <cmath>

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

static const float DT = 1.f / 209.f;   // Abtastrate der Firmware

// Effektivwert der Filterausgabe fuer ein reines Zittersignal. Der Aufrufer
// gibt Frequenz und Amplitude in Grad/s vor; speed ist wie in
// OrientationPointer der Betrag der Rate.
static float tremorRms(OneEuroFilter& f, float hz, float amp, float seconds) {
    double sum = 0.0;
    int    n   = 0;
    const int steps = (int)(seconds / DT);
    for (int i = 0; i < steps; i++) {
        const float t = i * DT;
        const float x = amp * std::sin(2.f * 3.14159265f * hz * t);
        const float y = f.update(x, std::fabs(x), DT);
        // Erste halbe Sekunde ist Einschwingen und zaehlt nicht mit.
        if (t > 0.5f) { sum += (double)y * y; n++; }
    }
    return n ? (float)std::sqrt(sum / n) : 0.f;
}

// Tremor mit 10 Hz und 12 Grad/s muss deutlich gedaempft werden. Der
// Effektivwert eines Sinus ist Amplitude/sqrt(2) = 8.49.
static void test_tremorIsAttenuated() {
    OneEuroFilter f(1.0f, 0.2f, 1.0f);
    const float rms = tremorRms(f, 10.f, 12.f, 3.f);
    CHECK(rms < 0.45f * 8.49f, "Tremor wird kaum gedaempft");
}

// Die Gegenprobe gegen die bisherige Einstellung. Geprueft wird die
// Anforderung - deutlich weniger Wackeln - und nicht der Mechanismus: der
// wirksame Hebel ist beta, dcutoff ist die Korrektheitsreparatur daneben.
static void test_neueEinstellungDaempftStaerker() {
    OneEuroFilter alt(0.9f, 0.55f, 1000.0f);   // wie bisher, ungeglaettete Rate
    OneEuroFilter neu(1.0f, 0.20f,    1.0f);
    const float rmsAlt = tremorRms(alt, 10.f, 12.f, 3.f);
    const float rmsNeu = tremorRms(neu, 10.f, 12.f, 3.f);
    CHECK(rmsNeu < 0.7f * rmsAlt, "die neue Einstellung daempft nicht spuerbar staerker");
}

// Eine gehaltene Bewegung darf nicht traege werden: nach einem Sprung auf
// 100 Grad/s muss der Filter binnen 200 ms mindestens 90 Prozent erreichen.
static void test_stepIsFast() {
    OneEuroFilter f(1.0f, 0.2f, 1.0f);
    const int steps = (int)(0.200f / DT);
    float y = 0.f;
    for (int i = 0; i < steps; i++) y = f.update(100.f, 100.f, DT);
    CHECK(y > 90.f, "Sprungantwort zu langsam");
}

// Ein konstanter Eingang muss exakt erreicht werden, sonst bliebe ein
// dauerhafter Versatz im Cursor stehen.
static void test_convergesToConstant() {
    OneEuroFilter f(1.0f, 0.2f, 1.0f);
    float y = 0.f;
    for (int i = 0; i < 5000; i++) y = f.update(7.f, 7.f, DT);
    CHECK(std::fabs(y - 7.f) < 1e-3f, "konvergiert nicht auf den Eingang");
}

// Der erste Wert nach reset() wird uebernommen, nicht aus der Vorgeschichte
// hochgezogen - sonst laeuft beim Haltungswechsel ein Rest in den Cursor.
static void test_resetTakesFirstSample() {
    OneEuroFilter f(1.0f, 0.2f, 1.0f);
    for (int i = 0; i < 500; i++) f.update(50.f, 50.f, DT);
    f.reset();
    const float y = f.update(3.f, 3.f, DT);
    CHECK(std::fabs(y - 3.f) < 1e-6f, "reset uebernimmt den ersten Wert nicht");
}

// dt <= 0 darf den Filter nicht zerstoeren (Division durch null).
static void test_zeroDtIsSafe() {
    OneEuroFilter f(1.0f, 0.2f, 1.0f);
    f.update(5.f, 5.f, DT);
    const float y = f.update(9.f, 9.f, 0.f);
    CHECK(std::isfinite(y), "dt = 0 liefert keinen endlichen Wert");
}

int main() {
    test_tremorIsAttenuated();
    test_neueEinstellungDaempftStaerker();
    test_stepIsFast();
    test_convergesToConstant();
    test_resetTakesFirstSample();
    test_zeroDtIsSafe();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
```

- [ ] **Step 2: Test laufen lassen und Fehlschlag bestaetigen**

```powershell
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/Filters -o "$env:TEMP\euro.exe" test/test_one_euro.cpp
```

Erwartet: **Compilerfehler**, `no matching function for call to 'OneEuroFilter::OneEuroFilter(float, float, float)'` — der dritte Parameter existiert noch nicht.

- [ ] **Step 3: `OneEuro.h` umbauen**

`lib/Filters/OneEuro.h` vollstaendig ersetzen:

```cpp
#pragma once
#include <math.h>

// 1-Euro-Filter nach Casiez, Roussel und Vogel (CHI 2012): ein Tiefpass, dessen
// Grenzfrequenz mit der Bewegungsgeschwindigkeit mitwaechst. Steht die Hand
// still, filtert er stark und unterdrueckt das Zittern; bewegt sie sich, oeffnet
// er und erzeugt kaum Verzoegerung. Ein fester Tiefpass muss sich zwischen
// diesen beiden Faellen entscheiden, dieser nicht.
//
// Abweichung vom Original: dort wird die Geschwindigkeit aus dem verrauschten
// Positionssignal geschaetzt. Hier ist sie die Messgroesse selbst - das Gyroskop
// liefert die Drehrate direkt. Die Ableitungsstufe des Originals entfaellt
// deshalb ersatzlos.
//
// Was NICHT entfallen darf, ist der Tiefpass auf der Geschwindigkeit (dcutoff,
// im Original 1 Hz). Ohne ihn geht die rohe Rate in die Grenzfrequenz ein, und
// die folgt dann dem Betrag des Signals: bei physiologischem Tremor - 8 bis
// 12 Hz, rund 12 Grad/s - steht sie genau auf den Spitzen am weitesten offen,
// die Spitzen kommen also besser durch als der Mittelwert vermuten laesst.
//
// Der wirksame Hebel gegen das Wackeln ist trotzdem beta und nicht dcutoff:
// der Mittelwert von |12*sin(2*pi*10t)| ist 7.64 Grad/s, ob geglaettet oder
// nicht. Mit beta = 0.55 ergibt das eine mittlere Grenzfrequenz von 5.1 Hz und
// damit kaum Daempfung bei 10 Hz; mit beta = 0.2 sind es 2.5 Hz. dcutoff ist
// eine Korrektheitsreparatur, beta die Einstellung.
//
// Die Parameter kommen wie bei HighPass/LowPass ueber den Konstruktor und nicht
// aus cfg::. So lassen sich fuer die Evaluation zwei verschieden eingestellte
// Filter im selben Programm gegeneinander laufen lassen.
class OneEuroFilter {
public:
    // minCutoff in Hz, beta in Hz pro Einheit von speed, dCutoff in Hz.
    // dCutoff hat bewusst keinen Vorgabewert: er ist der wirksame Hebel gegen
    // das Zittern und soll an jeder Aufrufstelle sichtbar sein.
    OneEuroFilter(float minCutoff, float beta, float dCutoff)
        : minCutoff_(minCutoff), beta_(beta), dCutoff_(dCutoff) {}

    // speed = Betrag der Bewegung, steuert die Grenzfrequenz.
    float update(float x, float speed, float dt) {
        if (dt <= 0.f) return y_;

        const float s = fabsf(speed);

        if (!init_) { y_ = x; sp_ = s; init_ = true; return y_; }

        // Erst die Geschwindigkeit glaetten, dann daraus die Grenzfrequenz.
        sp_ += alphaFor(dCutoff_, dt) * (s - sp_);

        const float cutoff = minCutoff_ + beta_ * sp_;
        y_ += alphaFor(cutoff, dt) * (x - y_);
        return y_;
    }

    void reset() { init_ = false; y_ = 0.f; sp_ = 0.f; }

private:
    // Zeitkonstante eines Tiefpasses erster Ordnung, in den Schrittfaktor
    // umgerechnet. Aus dem Original uebernommen.
    static float alphaFor(float cutoffHz, float dt) {
        const float tau = 1.f / (2.f * 3.14159265f * cutoffHz);
        return 1.f / (1.f + tau / dt);
    }

    float minCutoff_;
    float beta_;
    float dCutoff_;
    float y_    = 0.f;
    float sp_   = 0.f;   // geglaettete Geschwindigkeit
    bool  init_ = false;
};
```

- [ ] **Step 4: Test laufen lassen und Bestehen bestaetigen**

```powershell
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/Filters -o "$env:TEMP\euro.exe" test/test_one_euro.cpp
& "$env:TEMP\euro.exe"
```

Erwartet: `... Pruefungen, 0 Fehler`, Exit 0.

- [ ] **Step 5: Commit**

Der Firmware-Build ist an dieser Stelle noch rot (`OrientationPointer` ruft den Zwei-Parameter-Konstruktor). Task 4 folgt unmittelbar und stellt ihn wieder her — deshalb wird hier trotzdem committet, damit die Filteraenderung und ihre Verdrahtung getrennt nachvollziehbar bleiben.

```bash
git add lib/Filters/OneEuro.h test/test_one_euro.cpp
git commit -m "$(cat <<'EOF'
fix: 1-Euro-Filter glaettet die Geschwindigkeit wieder (Casiez dcutoff)

Ohne diesen Tiefpass geht die rohe Drehrate in die Grenzfrequenz ein, und
Handzittern (8-12 Hz, ~12 Grad/s) reisst sie auf 7.5 Hz auf. Der Filter
oeffnete damit genau dann, wenn er schliessen muesste - das war die Ursache
des Cursorwackelns.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Cursor-Kennlinie neu einstellen

Mit dem geglaetteten Geschwindigkeitssignal darf `BETA` klein bleiben, ohne dass eine echte Bewegung traege wird. Die Beschleunigungskennlinie faellt weg: sie greift **nach** dem Filter und multipliziert Restzittern mit bis zu 4. Scotto et al. 2020 fanden linear steigende Verstaerkung ohnehin schlechter als eine gute konstante.

**Files:**
- Modify: `include/config.h`
- Modify: `lib/OrientationPointer/OrientationPointer.h`

**Interfaces:**
- Consumes: `OneEuroFilter(minCutoff, beta, dCutoff)` aus Task 3.
- Produces: `PointerTuning` mit dem zusaetzlichen Feld `float euroDCutoff = cfg::EURO_DCUTOFF`.

- [ ] **Step 1: Die Konstanten in `config.h` ersetzen**

Den Block ab `constexpr float SENS_X` bis `constexpr float SMOOTH_TAU` ersetzen:

```cpp
    // Pixel pro Grad Drehung: stepX = rate[Grad/s] * SENS_X * dt[s], und rate*dt
    // sind genau die in diesem Takt gedrehten Grad.
    constexpr float SENS_X    = 110.0f;
    constexpr float SENS_Y    = 110.0f;

    // Beschleunigung aus. Sie greift NACH dem 1-Euro-Filter und multipliziert
    // deshalb auch das Restzittern - bis zum Vierfachen. Scotto et al. 2020
    // fanden eine linear steigende Verstaerkung ohnehin schlechter als eine
    // gute konstante. ACCEL_K bleibt als Konstante stehen, damit sich die
    // Gegenprobe ohne Reflash fahren laesst:
    //
    //   PointerTuning t;  t.accelK = 2.f;
    //   OrientationPointer mitBeschleunigung(t);
    constexpr float ACCEL_K   = 0.0f;
    constexpr float ACCEL_MAX = 4.0f;

    // Faengt den Rest-Nullpunktfehler ab, den die Bias-Korrektur uebriglaesst.
    // Gegen das Wackeln hilft sie nur begrenzt - Tremor erzeugt rund 12 Grad/s
    // und liegt weit ueber jeder vertretbaren Totzone. Weiter anzuheben kostet
    // feine Bewegung direkt: 2.5 Grad/s sind bei SENS_X = 110 schon 275 px/s,
    // die stufenlos abgezogen werden.
    constexpr float DEADZONE = 3.5f;

    // 1-Euro-Filter (Casiez et al. 2012). Grenzfrequenz waechst mit der
    // Geschwindigkeit: cutoff = MIN_CUTOFF + BETA * geglaettete Drehrate.
    //
    // EURO_DCUTOFF ist der Tiefpass auf der Geschwindigkeit selbst, aus dem
    // Original. Ohne ihn folgt die Grenzfrequenz dem Betrag des Signals und
    // steht bei Handzittern genau auf den Spitzen am weitesten offen.
    //
    // Der wirksame Hebel gegen das Wackeln ist aber BETA: der Mittelwert des
    // Tremors liegt bei rund 7.6 Grad/s, geglaettet wie ungeglaettet. Mit 0.55
    // ergab das eine mittlere Grenzfrequenz von 5.1 Hz und damit kaum
    // Daempfung bei 10 Hz; mit 0.2 sind es 2.5 Hz.
    //
    // Einstellen in dieser Reihenfolge (Casiez et al. 2012): erst BETA auf 0
    // und MIN_CUTOFF senken, bis die ruhig gehaltene Hand einen ruhigen Cursor
    // ergibt, dann BETA anheben, bis die Verzoegerung beim Zeigen verschwindet.
    constexpr float EURO_DCUTOFF    = 1.0f;   // Hz
    constexpr float EURO_MIN_CUTOFF = 1.0f;   // Hz
    constexpr float EURO_BETA       = 0.2f;   // Hz pro Grad/s

    constexpr float SMOOTH_TAU = 0.024f;      // nur fuer den Vergleichspfad
```

- [ ] **Step 2: `PointerTuning` und die Filter-Konstruktion anpassen**

In `lib/OrientationPointer/OrientationPointer.h` das Feld ergaenzen (nach `euroBeta`):

```cpp
    float euroDCutoff   = cfg::EURO_DCUTOFF;
```

Und die Initialisiererliste des Konstruktors:

```cpp
    explicit OrientationPointer(const PointerTuning& t = PointerTuning())
        : t_(t),
          euroX_(t.euroMinCutoff, t.euroBeta, t.euroDCutoff),
          euroY_(t.euroMinCutoff, t.euroBeta, t.euroDCutoff) {}
```

- [ ] **Step 3: Firmware bauen**

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run 2>&1 | Select-Object -Last 20
```

Erwartet: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add include/config.h lib/OrientationPointer/OrientationPointer.h
git commit -m "$(cat <<'EOF'
feat: Cursor-Kennlinie auf den geglaetteten 1-Euro-Filter eingestellt

EURO_DCUTOFF verdrahtet, BETA auf 0.2 gesenkt, Deadzone auf 3.5. ACCEL_K auf 0:
die Beschleunigung greift nach dem Filter und multipliziert das Restzittern
mit bis zu 4 (Scotto et al. 2020). Die Konstante bleibt fuer die Gegenprobe
ueber PointerTuning erhalten.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Gyro-Bias frisst kein langsames Zeigen mehr

Der Nullpunkt wird nachgefuehrt, sobald `gyroSum < 15 Grad/s`. Eine langsame, bewusste Zeigebewegung liegt bei 5–10 Grad/s und faellt damit **in** das Lernfenster: nach rund zwei Sekunden hat der Schaetzer den grossen Teil der gewollten Drehrate als vermeintlichen Nullpunkt uebernommen. Der Cursor wird beim langsamen Ziehen langsamer und driftet beim Anhalten zurueck.

**Files:**
- Modify: `include/config.h`
- Modify: `lib/ImuReader/ImuReader.h`

**Interfaces:**
- Consumes: `ImuSample.accMag` (unveraendert aus Task 1).
- Produces: nichts Neues nach aussen.

- [ ] **Step 1: Konstanten anpassen**

In `include/config.h` den Bias-Block ersetzen:

```cpp
    // Nullpunkt des Gyroskops driftet mit der Temperatur. Er wird nur
    // nachgefuehrt, solange das Geraet wirklich ruhig liegt.
    //
    // Die Schwelle lag bei 15 Grad/s und damit ueber der langsamsten gemeinten
    // Bewegung: langsames, gezieltes Zeigen liegt bei 5 bis 10 Grad/s und fiel
    // mitten ins Lernfenster. Der Schaetzer uebernahm die gewollte Drehrate als
    // Nullpunkt, der Cursor wurde beim langsamen Ziehen immer langsamer und
    // driftete beim Anhalten zurueck - ein Symptom, das man einer zu starken
    // Glaettung zuschreiben wuerde und das keine war.
    //
    // Die zweite Bedingung schliesst aus, dass eine gleichfoermige Drehung
    // ohne Drehratenanteil durchrutscht: ein ruhendes Board misst genau 1 g.
    // BIAS_TAU laenger, weil echter Temperaturdrift langsam ist.
    constexpr float BIAS_STILL_DPS = 3.f;
    constexpr float BIAS_ACC_TOL   = 0.05f;   // erlaubte Abweichung von 1 g
    constexpr float BIAS_TAU       = 5.0f;
```

- [ ] **Step 2: Die Lernbedingung in `ImuReader::read()` erweitern**

```cpp
        if (s.gyroSum < cfg::BIAS_STILL_DPS &&
            fabsf(s.accMag - 1.f) < cfg::BIAS_ACC_TOL) {
            const float a = 1.f - expf(-dt / cfg::BIAS_TAU);
            bx_ += a * (rawX - bx_);
            by_ += a * (rawY - by_);
            bz_ += a * (rawZ - bz_);
        }
```

- [ ] **Step 3: Firmware bauen**

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run 2>&1 | Select-Object -Last 20
```

Erwartet: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add include/config.h lib/ImuReader/ImuReader.h
git commit -m "$(cat <<'EOF'
fix: Gyro-Bias lernt nicht mehr waehrend langsamer Zeigebewegungen

BIAS_STILL_DPS von 15 auf 3, zusaetzlich Bedingung auf ruhende Beschleunigung.
Bei 15 Grad/s fiel langsames Zeigen (5-10 Grad/s) ins Lernfenster: der Cursor
wurde dabei langsamer und driftete beim Anhalten zurueck.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: I2C beschleunigen und in einem Burst lesen

Sechs `readFloat*`-Aufrufe sind sechs I2C-Transaktionen. Bei 100 kHz kostet das rund 2 ms je Takt — bei 4.785 ms Schrittweite ist das mehr als ein Drittel des Budgets. Ein Burst ueber 12 Bytes ab `OUTX_L_G` liefert Gyro XYZ und Accel XYZ luecklos, und alle sechs Werte stammen dann aus demselben Abtastzeitpunkt.

**Files:**
- Modify: `lib/ImuReader/ImuReader.h`

**Interfaces:**
- Produces: `ImuReader::read(float dt)` unveraendert in der Signatur; nur die Beschaffung der Rohwerte aendert sich.

- [ ] **Step 1: `begin()` auf 400 kHz stellen**

`Wire.begin()` wird von `imu_.begin()` aufgerufen und setzt keine Taktrate, es bleibt also bei den 100 kHz der Arduino-Vorgabe. Deshalb danach setzen:

```cpp
    void begin() {
        imu_.settings.accelRange      = cfg::ACCEL_RANGE_G;
        imu_.settings.accelSampleRate = cfg::ACCEL_ODR_HZ;
        imu_.settings.accelBandWidth  = cfg::ACCEL_BW_HZ;
        imu_.settings.gyroRange       = cfg::GYRO_RANGE_DPS;
        imu_.settings.gyroSampleRate  = cfg::GYRO_ODR_HZ;
        imu_.begin();

        // Muss NACH imu_.begin() stehen: dort laeuft Wire.begin(), und das
        // setzt die Taktrate auf die Arduino-Vorgabe von 100 kHz zurueck.
        // Der LSM6DS3 kann 400 kHz - bei sechs Werten je Takt ist das der
        // Unterschied zwischen rund 2 ms und rund 0.5 ms Schleifenzeit.
        Wire.setClock(400000);
    }
```

`#include <Wire.h>` am Dateikopf ergaenzen.

- [ ] **Step 2: Den Burst-Read einbauen**

Die ersten Zeilen von `read()` ersetzen:

```cpp
    ImuSample read(float dt) {
        // Ein Burst statt sechs Einzeltransaktionen. Ab OUTX_L_G (0x22) folgen
        // luecklos Gyro X/Y/Z und Accel X/Y/Z, je zwei Bytes little-endian -
        // 0x28 (OUTX_L_XL) liegt genau hinter dem Gyro-Block. Neben der
        // gesparten Zeit hat das einen zweiten Vorteil: alle sechs Werte
        // stammen aus demselben Abtastzeitpunkt. Bei Einzelzugriffen lagen
        // zwischen dem ersten und dem letzten rund 2 ms, in denen sich die
        // Hand weiterbewegt hat.
        uint8_t raw[12];
        imu_.readRegisterRegion(raw, LSM6DS3_ACC_GYRO_OUTX_L_G, 12);

        const int16_t gxi = (int16_t)((uint16_t)raw[1]  << 8 | raw[0]);
        const int16_t gyi = (int16_t)((uint16_t)raw[3]  << 8 | raw[2]);
        const int16_t gzi = (int16_t)((uint16_t)raw[5]  << 8 | raw[4]);
        const int16_t axi = (int16_t)((uint16_t)raw[7]  << 8 | raw[6]);
        const int16_t ayi = (int16_t)((uint16_t)raw[9]  << 8 | raw[8]);
        const int16_t azi = (int16_t)((uint16_t)raw[11] << 8 | raw[10]);

        const float rawX = imu_.calcGyro(gxi);
        const float rawY = imu_.calcGyro(gyi);
        const float rawZ = imu_.calcGyro(gzi);

        ImuSample s;
        s.ax = imu_.calcAccel(axi);
        s.ay = imu_.calcAccel(ayi);
        s.az = imu_.calcAccel(azi);
        s.gx = rawX - bx_;
        s.gy = rawY - by_;
        s.gz = rawZ - bz_;
```

Der Rest von `read()` bleibt unveraendert.

- [ ] **Step 3: Firmware bauen**

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run 2>&1 | Select-Object -Last 20
```

Erwartet: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add lib/ImuReader/ImuReader.h
git commit -m "$(cat <<'EOF'
perf: IMU in einem Burst und mit 400 kHz lesen

Sechs Einzeltransaktionen bei 100 kHz kosteten rund 2 ms je Takt - bei 4.785 ms
Schrittweite mehr als ein Drittel des Budgets. Der Burst liefert zudem alle
sechs Werte aus demselben Abtastzeitpunkt.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Berichtsintervall und Rueckstau

Pro HID-Bericht sind hoechstens 127 px je Achse uebertragbar. Bei 10 ms sind das 12 700 px/s — mit `SENS = 110 px/Grad` schon bei rund 50 Grad/s erreicht, also in jeder zuegigen Bewegung. Der Ueberhang bleibt liegen und wird verzoegert nachgeliefert: der Cursor hinkt hinterher und laeuft nach dem Anhalten nach.

**Files:**
- Modify: `include/config.h`
- Modify: `lib/AirMouseController/AirMouseController.h`

**Interfaces:**
- Consumes: `MouseHID::move(int8_t, int8_t) -> bool` (unveraendert).

- [ ] **Step 1: Das Intervall vom Uebertragungsweg abhaengig machen**

In `include/config.h` ersetzen:

```cpp
    // Berichtsintervall zum Host. Ueber BLE bringt es nichts, kuerzer als das
    // Verbindungsintervall zu senden - die Pakete warten dann nur in der
    // Warteschlange. Ueber USB pollt der Host jede Millisekunde, dort ist die
    // Halbierung ein direkter Latenzgewinn und verdoppelt zugleich die
    // Obergrenze der uebertragbaren Geschwindigkeit (127 px je Bericht).
#if USE_BLE_HID
    constexpr uint32_t MOVE_INTERVAL_US = 10000;
#else
    constexpr uint32_t MOVE_INTERVAL_US = 5000;
#endif

    // Wie viele Berichte hoechstens im selben Takt hintereinander gehen, um
    // einen Rueckstau abzubauen. Ohne das braucht ein Rueckstau von 300 px drei
    // Intervalle, bis er draussen ist. Die Obergrenze verhindert, dass eine
    // haengende Gegenstelle die Schleife blockiert.
    constexpr int MOVE_MAX_REPORTS = 3;
```

- [ ] **Step 2: `handlePointing()` den Rueckstau abbauen lassen**

Den Block ab `const int8_t mx = ...` in `lib/AirMouseController/AirMouseController.h` ersetzen:

```cpp
        // Mehrere Berichte im selben Takt, solange Rueckstau da ist. Ein
        // einzelner Bericht traegt hoechstens 127 px je Achse; bei schneller
        // Bewegung laeuft mehr auf, und der Rest kaeme sonst erst im naechsten
        // Intervall heraus - der Cursor laeuft dann nach dem Anhalten nach.
        for (int i = 0; i < cfg::MOVE_MAX_REPORTS; i++) {
            const int8_t mx = (int8_t)constrain(accumX_, -127.f, 127.f);
            const int8_t my = (int8_t)constrain(accumY_, -127.f, 127.f);
            if (!mx && !my) break;

            // Nur abziehen, wenn das Paket auch angenommen wurde, sonst geht
            // die Bewegung bei voller Warteschlange verloren.
            if (mouse_.move(mx, my)) { accumX_ -= mx; accumY_ -= my; }
            else {
                // Abgelehnte Pakete stauen sich in accumX_/accumY_ und gehen
                // beim naechsten Mal mit hinaus. Steigt moveFail_ im Betrieb,
                // wird schneller gemeldet als die Gegenstelle ausliefert.
                //
                // Der Rueckstau wird dabei begrenzt: nimmt die Gegenstelle
                // laenger gar nichts an - Host noch nicht aufgezaehlt, Kabel
                // raus, BLE nicht verbunden - liefe die Summe sonst minutenlang
                // weiter und der Cursor schoesse beim Verbinden quer ueber den
                // Schirm.
                moveFail_++;
                accumX_ = constrain(accumX_, -cfg::MOVE_BACKLOG_MAX, cfg::MOVE_BACKLOG_MAX);
                accumY_ = constrain(accumY_, -cfg::MOVE_BACKLOG_MAX, cfg::MOVE_BACKLOG_MAX);
                break;   // haengende Gegenstelle: nicht weiter nachschieben
            }
        }
```

- [ ] **Step 3: Firmware in beiden HID-Zweigen bauen**

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run 2>&1 | Select-Object -Last 20
```

Erwartet: `SUCCESS`. Danach `USE_BLE_HID` in `include/config.h` auf `true` setzen, erneut bauen, `SUCCESS` bestaetigen, **und wieder auf `false` zuruecksetzen**. Das neue `#if` in `config.h` haengt an diesem Schalter — beide Zweige muessen uebersetzen.

- [ ] **Step 4: Commit**

```bash
git add include/config.h lib/AirMouseController/AirMouseController.h
git commit -m "$(cat <<'EOF'
perf: 5 ms Berichtsintervall ueber USB, Rueckstau in einem Takt abbauen

127 px je Bericht sind bei 10 ms nur 12 700 px/s und damit schon ab ~50 Grad/s
die Bremse. Ueber USB pollt der Host jede Millisekunde; BLE behaelt seine 10 ms.
Bis zu drei Berichte je Takt raeumen einen aufgelaufenen Rest sofort ab.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Cursor friert nur noch waehrend der Erschuetterung

Heute steht der Cursor nach **jedem** Klick 120 ms vollstaendig still. Sinnvoll ist das Einfrieren nur, solange die Erschuetterung des Pinches wirklich anliegt.

**Files:**
- Modify: `include/config.h`
- Modify: `lib/PinchDetector/PinchDetector.h`

**Interfaces:**
- Produces: `PinchDetector::inFreeze(uint32_t now)` — Signatur unveraendert, Bedingung neu.

- [ ] **Step 1: Konstante umbenennen**

In `include/config.h`:

```cpp
    constexpr float    PINCH_GYRO_GUARD = 100.f;

    // Obergrenze fuer das Einfrieren des Cursors nach einem Klick. Frueher war
    // das eine feste Zeit von 120 ms - der Cursor stand also nach jedem Klick,
    // auch wenn die Erschuetterung laengst vorbei war. Jetzt friert er nur,
    // solange das env-Gate offen ist; diese Zahl ist nur noch die Notbremse
    // fuer den Fall, dass das Gate haengt.
    constexpr uint32_t FREEZE_MAX_MS = 60;
```

`FREEZE_MS` entfaellt.

- [ ] **Step 2: `inFreeze()` an das Gate binden**

In `lib/PinchDetector/PinchDetector.h`:

```cpp
    // Der Zeiger ruht, solange die Erschuetterung des Pinches anliegt - nicht
    // eine feste Zeit lang. Ein kurzer, sauberer Pinch gibt den Cursor damit
    // sofort wieder frei, statt ihn pauschal auszubremsen.
    bool inFreeze(uint32_t now) const {
        return envGate_ && (now - tLastPinch_) < cfg::FREEZE_MAX_MS;
    }
```

- [ ] **Step 3: Firmware bauen**

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run 2>&1 | Select-Object -Last 20
```

Erwartet: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add include/config.h lib/PinchDetector/PinchDetector.h
git commit -m "$(cat <<'EOF'
feat: Cursor friert nur waehrend der Erschuetterung, nicht 120 ms pauschal

FREEZE_MS wird zu FREEZE_MAX_MS = 60 und ist nur noch Notbremse; die Bedingung
ist jetzt das offene env-Gate.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: `TwistGuard` — der Cursor steht waehrend der Verdrehung still

Ohne das laeuft der Zeiger beim Hindrehen in die abgedrehte Haltung quer ueber den Schirm, und der Rechtsklick ist unbrauchbar: bis die Hand die Haltung erreicht hat, steht der Cursor nicht mehr auf dem Ziel.

`gy` wegzulassen genuegt nicht — das tut der Zeiger heute schon. Die Unterarmachse faellt nicht exakt mit einer Platinenachse zusammen (das Board sitzt am Arm, nicht im Gelenk), eine Verdrehung leckt deshalb immer auch in `gx` und `gz`. Die Rate wird darum aus der **Lageschaetzung** abgeleitet: die Ableitung von `arm::twistDeg()` misst die Verdrehung selbst, unabhaengig von der Einbaulage.

**Files:**
- Create: `lib/TwistGuard/TwistGuard.h`
- Create: `test/test_twist_guard.cpp`
- Modify: `platformio.ini`
- Modify: `lib/AirMouseController/AirMouseController.h`

`include/config.h` wird **nicht** angefasst: die Einstellwerte stehen in `TwistGuardTuning`, weil der Header ohne Toolchain uebersetzbar bleiben muss — dieselbe Ausnahme wie bei `PointerTuning` und `TwistTuning`.

**Interfaces:**
- Produces: `struct TwistGuardTuning { float lowDps; float highDps; float rateTau; float releaseS; }`
- Produces: `class TwistGuard { explicit TwistGuard(const TwistGuardTuning& = TwistGuardTuning()); float update(float twistDeg, float dt); float gain() const; float rateDps() const; void reset(); }` — `update()` liefert den Faktor 0…1, mit dem der Controller `px`/`py` multipliziert.

- [ ] **Step 1: Den Test schreiben**

Neue Datei `test/test_twist_guard.cpp`:

```cpp
// Test der Verdrehungs-Bremse. Laeuft auf dem PC - TwistGuard braucht nur
// <math.h> und <stdint.h>.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/TwistGuard -o build/guard.exe test/test_twist_guard.cpp && ./build/guard.exe
//
#include "TwistGuard.h"
#include <cstdio>
#include <cmath>

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

static const float DT = 1.f / 209.f;

// Dreht mit konstanter Rate weiter und liefert den letzten Faktor.
static float turnAt(TwistGuard& g, float& deg, float dps, float seconds) {
    float out = 1.f;
    const int steps = (int)(seconds / DT);
    for (int i = 0; i < steps; i++) {
        deg += dps * DT;
        out  = g.update(deg, DT);
    }
    return out;
}

// Ruhig gehaltene Hand: der Zeiger muss voll durchkommen.
static void test_stillMeansFullGain() {
    TwistGuard g;
    float deg = 0.f;
    CHECK(turnAt(g, deg, 0.f, 1.f) > 0.99f, "ruhige Hand wird gebremst");
}

// Der allererste Aufruf darf nicht bremsen: ohne Vorwert waere die Ableitung
// sonst riesig und der Cursor beim Einschalten kurz tot.
static void test_firstSampleDoesNotBrake() {
    TwistGuard g;
    const float out = g.update(90.f, DT);
    CHECK(out > 0.99f, "der erste Aufruf bremst");
}

// Eine zuegige Verdrehung muss den Zeiger vollstaendig stilllegen.
static void test_fastTwistClosesFully() {
    TwistGuard g;
    float deg = 0.f;
    turnAt(g, deg, 0.f, 0.5f);
    CHECK(turnAt(g, deg, 200.f, 0.3f) < 0.01f, "schnelle Verdrehung bremst nicht");
}

// Eine langsame Verdrehung ist keine Geste, sondern Zeigen mit leicht
// mitdrehender Hand - sie darf den Cursor nicht abwuergen.
static void test_slowTwistPassesThrough() {
    TwistGuard g;
    float deg = 0.f;
    turnAt(g, deg, 0.f, 0.5f);
    CHECK(turnAt(g, deg, 10.f, 0.5f) > 0.9f, "langsame Verdrehung bremst zu stark");
}

// Das Vorzeichen darf keine Rolle spielen.
static void test_signDoesNotMatter() {
    TwistGuard g;
    float deg = 0.f;
    turnAt(g, deg, 0.f, 0.5f);
    CHECK(turnAt(g, deg, -200.f, 0.3f) < 0.01f, "Verdrehung nach der anderen Seite bremst nicht");
}

// Sofort zu, langsam wieder auf. Am Ende einer Drehung klingt die Rate aus und
// kreuzt die Schwelle mehrfach; ohne begrenzte Rueckkehr zuckte der Cursor
// dabei wiederholt an.
static void test_releaseIsSlewLimited() {
    TwistGuard g;
    float deg = 0.f;
    turnAt(g, deg, 0.f, 0.5f);
    turnAt(g, deg, 200.f, 0.3f);              // zu

    const float kurz = turnAt(g, deg, 0.f, 0.05f);
    CHECK(kurz < 0.6f, "die Bremse oeffnet zu schnell wieder");

    const float lang = turnAt(g, deg, 0.f, 0.5f);
    CHECK(lang > 0.99f, "die Bremse oeffnet gar nicht mehr");
}

// Der Faktor bleibt in seinen Grenzen - er multipliziert eine Pixelzahl.
static void test_gainStaysBounded() {
    TwistGuard g;
    float deg = 0.f;
    for (int i = 0; i < 2000; i++) {
        const float dps = (i % 7 == 0) ? 300.f : ((i % 3 == 0) ? -150.f : 5.f);
        deg += dps * DT;
        const float out = g.update(deg, DT);
        if (out < 0.f || out > 1.f) { CHECK(false, "Faktor ausserhalb 0..1"); return; }
    }
    CHECK(true, "Faktor bleibt in den Grenzen");
}

// Der Winkel springt bei plus/minus 180 Grad um. Ohne wrapDeg waere der Sprung
// eine Differenz von 358 statt 2 Grad, also eine scheinbare Rate von rund
// 75'000 Grad/s statt der tatsaechlichen ~420.
//
// Geprueft wird die Rate und nicht der Faktor: 2 Grad in einem Takt SIND eine
// schnelle Drehung, die Bremse darf und soll dabei zugehen. Falsch waere nur
// die Groessenordnung. Die Schranke liegt weit ueber dem richtigen Wert und
// weit unter dem falschen, trifft also keine Aussage ueber die Glaettung.
static void test_wrapAroundIsNotARate() {
    TwistGuard g;
    float deg = 179.f;
    turnAt(g, deg, 0.f, 0.5f);
    deg = -179.f;                              // Sprung ueber die Grenze
    g.update(deg, DT);
    CHECK(g.rateDps() < 1000.f, "der Umschlag bei 180 Grad wird als Drehung gelesen");
}

int main() {
    test_stillMeansFullGain();
    test_firstSampleDoesNotBrake();
    test_fastTwistClosesFully();
    test_slowTwistPassesThrough();
    test_signDoesNotMatter();
    test_releaseIsSlewLimited();
    test_gainStaysBounded();
    test_wrapAroundIsNotARate();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
```

- [ ] **Step 2: Test laufen lassen und Fehlschlag bestaetigen**

```powershell
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/TwistGuard -o "$env:TEMP\guard.exe" test/test_twist_guard.cpp
```

Erwartet: **Compilerfehler**, `TwistGuard.h: No such file or directory`.

- [ ] **Step 3: `TwistGuard.h` schreiben**

Neue Datei `lib/TwistGuard/TwistGuard.h`:

```cpp
#pragma once
#include <stdint.h>
#include <math.h>

// Bremst den Zeiger, waehrend sich der Unterarm um seine eigene Achse dreht.
//
// Warum es das braucht: die abgedrehte Haltung traegt den Rechtsklick, und die
// Ein/Aus-Geste geht durch dieselbe Bewegung. Liefe der Cursor dabei mit, waere
// er nach dem Hindrehen nicht mehr auf dem Ziel - der Rechtsklick waere
// praktisch nicht zielbar.
//
// Warum die Rate aus der Lageschaetzung kommt und nicht aus gy: die
// Unterarmachse faellt nicht exakt mit einer Platinenachse zusammen, das Board
// sitzt am Arm und nicht im Gelenk. Eine Verdrehung leckt deshalb immer auch in
// gx und gz - genau die beiden Achsen, aus denen der Zeiger seine Bewegung
// zieht. Die Ableitung von arm::twistDeg() misst dagegen die Verdrehung selbst,
// unabhaengig von der Einbaulage. Dass es diesen Winkel ueberhaupt gibt, ist
// der Verdienst des Madgwick-Filters.
//
// Der Faktor greift auf die fertigen Pixel und nicht auf die Rate vor dem
// 1-Euro-Filter: so laeuft der Filter waehrend der Drehung weiter mit und
// bleibt eingeschwungen. Andernfalls kaeme nach jeder Drehung eine
// Anfahrverzoegerung obendrauf.
//
// Kein #include "config.h" und kein <Arduino.h>: dieser Header muss sich ohne
// Toolchain uebersetzen lassen (test/test_twist_guard.cpp).
struct TwistGuardTuning {
    float lowDps   = 25.f;    // darunter volle Bewegung
    float highDps  = 70.f;    // darueber gar keine
    float rateTau  = 0.03f;   // Glaettung der abgeleiteten Rate, Sekunden
    float releaseS = 0.20f;   // Zeit fuer die volle Rueckkehr auf 1.0
};

class TwistGuard {
public:
    explicit TwistGuard(const TwistGuardTuning& t = TwistGuardTuning()) : t_(t) {}

    // twistDeg: Verdrehung aus der Lageschaetzung (arm::twistDeg).
    // Rueckgabe: Faktor 0..1 fuer die Cursorbewegung dieses Takts.
    float update(float twistDeg, float dt) {
        if (dt <= 0.f) return gain_;

        if (!init_) {
            // Ohne Vorwert waere die Ableitung im ersten Takt riesig und der
            // Cursor beim Einschalten kurz tot.
            prev_ = twistDeg;
            init_ = true;
            return gain_;
        }

        // wrapDeg, weil der Winkel bei +-180 Grad umschlaegt. Ohne das waere
        // der Umschlag eine scheinbare Rate von zehntausenden Grad pro Sekunde
        // und die Bremse bliebe danach eine halbe Sekunde zu.
        const float d = wrapDeg(twistDeg - prev_);
        prev_ = twistDeg;

        const float a = 1.f - expf(-dt / t_.rateTau);
        rate_ += a * (fabsf(d) / dt - rate_);

        float want = 1.f;
        if      (rate_ >= t_.highDps) want = 0.f;
        else if (rate_ >  t_.lowDps)  want = 1.f - (rate_ - t_.lowDps) / (t_.highDps - t_.lowDps);

        // Sofort zu, langsam wieder auf: am Ende einer Drehung klingt die Rate
        // aus und kreuzt die Schwelle mehrfach. Ohne die begrenzte Rueckkehr
        // zuckte der Cursor dabei wiederholt an.
        if (want < gain_) gain_ = want;
        else {
            gain_ += dt / t_.releaseS;
            if (gain_ > want) gain_ = want;
        }
        if (gain_ < 0.f) gain_ = 0.f;
        if (gain_ > 1.f) gain_ = 1.f;
        return gain_;
    }

    void reset() { init_ = false; rate_ = 0.f; gain_ = 1.f; }

    float gain()    const { return gain_; }
    float rateDps() const { return rate_; }

private:
    static float wrapDeg(float a) {
        while (a >  180.f) a -= 360.f;
        while (a < -180.f) a += 360.f;
        return a;
    }

    TwistGuardTuning t_;
    bool  init_ = false;
    float prev_ = 0.f;
    float rate_ = 0.f;
    float gain_ = 1.f;
};
```

- [ ] **Step 4: Test laufen lassen und Bestehen bestaetigen**

```powershell
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/TwistGuard -o "$env:TEMP\guard.exe" test/test_twist_guard.cpp
& "$env:TEMP\guard.exe"
```

Erwartet: `... Pruefungen, 0 Fehler`, Exit 0.

- [ ] **Step 5: `-I`-Eintrag ergaenzen**

In `platformio.ini` bei den `build_flags`, nach `-I lib/OrientationPointer`:

```
    -I lib/TwistGuard
```

- [ ] **Step 6: Im Controller verdrahten**

`#include "TwistGuard.h"` ergaenzen, Member `TwistGuard twistGuard_;` anlegen.

In `update()`, direkt nach der Berechnung von `twist_` und `elev_`:

```cpp
        // Muss in jedem Takt laufen, auch wenn gerade nicht gezeigt wird:
        // die Bremse leitet ihre Rate aus der Differenz zum letzten Winkel ab,
        // und ein ausgelassener Takt waere ein Sprung.
        twistGain_ = twistGuard_.update(twist_, dt);
```

In `handlePointing()`, beim Aufsummieren:

```cpp
        pointer_.update(s.gx, s.gz, pose_.relTwistSlow(), elev_, dt, px, py);

        // Waehrend sich der Unterarm dreht, laeuft der Cursor nicht mit. Der
        // Faktor greift hier und nicht vor dem 1-Euro-Filter, damit der Filter
        // eingeschwungen bleibt - sonst kaeme nach jeder Drehung eine
        // Anfahrverzoegerung obendrauf.
        accumX_ += px * twistGain_;
        accumY_ += py * twistGain_;
```

Beim `resetPointer` in `apply()` die Bremse mit zuruecksetzen:

```cpp
        if (a.resetPointer) { accumX_ = accumY_ = 0.f; pointer_.reset(); twistGuard_.reset(); }
```

Im Teleplot-Block bei `DEBUG_POINT`:

```cpp
        Serial.print(">tg:");     Serial.println(twistGain_, 2);
        Serial.print(">tgr:");    Serial.println(twistGuard_.rateDps(), 1);
```

Und das Member `float twistGain_ = 1.f;` zu den uebrigen `float`-Membern.

- [ ] **Step 7: Firmware bauen**

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run 2>&1 | Select-Object -Last 20
```

Erwartet: `SUCCESS`.

- [ ] **Step 8: Commit**

```bash
git add lib/TwistGuard/TwistGuard.h test/test_twist_guard.cpp platformio.ini lib/AirMouseController/AirMouseController.h
git commit -m "$(cat <<'EOF'
feat: TwistGuard - der Cursor steht waehrend der Unterarmdrehung still

Ohne das laeuft der Zeiger beim Hindrehen in die abgedrehte Haltung weg und der
Rechtsklick ist nicht zielbar. gy wegzulassen genuegt nicht: die Unterarmachse
faellt nicht exakt mit einer Platinenachse zusammen, eine Verdrehung leckt
deshalb auch in gx und gz. Die Rate kommt darum aus der Ableitung des
Verdrehungswinkels der Lageschaetzung.

Sofort zu, langsam wieder auf - am Ende einer Drehung kreuzt die Rate die
Schwelle mehrfach, und der Cursor zuckte sonst wiederholt an.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: Haptik als Impuls-Sequenzer

Der Rechtsklick soll sich vom Linksklick unterscheiden: zwei Impulse statt einem. Die feste Sperrfrist muss dafuer weg — sie muesste ueber der Musterdauer von 130 ms liegen, `DEBOUNCE_MS` steht aber auf 180 ms, ein Doppelklick wuerde damit nur noch einmal brummen.

**Files:**
- Modify: `include/config.h`
- Modify: `lib/Haptic/Haptic.h`

**Interfaces:**
- Produces: `Haptic::trigger(uint32_t now_ms, uint8_t pulses = 1)`. Der Vorgabewert haelt alle bestehenden Aufrufer gueltig; Task 14 stellt sie auf den expliziten Wert um.

- [ ] **Step 1: Konstanten anpassen**

In `include/config.h` den Haptik-Block ersetzen:

```cpp
    constexpr int      HAPTIC_PIN = D1;
    constexpr uint32_t HAPTIC_MS  = 40;

    // Luecke zwischen zwei Impulsen desselben Musters. Sie ist zugleich die
    // Ruhezeit nach einem Muster, bevor das naechste starten darf.
    constexpr uint32_t HAPTIC_GAP_MS = 50;
```

`HAPTIC_COOLDOWN_MS` entfaellt.

- [ ] **Step 2: `Haptic.h` vollstaendig ersetzen**

```cpp
#pragma once
#include <Arduino.h>
#include "config.h"

// Kleiner Impuls-Sequenzer: n Impulse à HAPTIC_MS mit HAPTIC_GAP_MS dazwischen.
// Ein Impuls bedeutet Linksklick, Haltungswechsel oder Ein/Aus, zwei bedeuten
// Rechtsklick.
//
// Es gibt bewusst KEINE feste Sperrfrist mehr. Eine solche muesste ueber der
// Dauer des Zwei-Impuls-Musters liegen (130 ms), cfg::DEBOUNCE_MS steht aber
// auf 180 ms - ein Doppelklick wuerde damit nur noch einmal brummen. Gesperrt
// ist stattdessen genau, solange ein Muster laeuft, plus eine Luecke danach.
// Das erfuellt denselben Zweck: dicht aufeinander folgende Ausloeser
// verschmelzen nicht zu einem langen Brummen, sondern bleiben abzaehlbar.
class Haptic {
public:
    void begin() {
        pinMode(cfg::HAPTIC_PIN, OUTPUT);
        digitalWrite(cfg::HAPTIC_PIN, LOW);
    }

    void trigger(uint32_t now_ms, uint8_t pulses = 1) {
        if (pulses == 0) return;
        if (busy_) return;                                        // laufendes Muster nicht stoeren
        if (used_ && now_ms - tFree_ < cfg::HAPTIC_GAP_MS) return; // Ruhe danach
        left_  = pulses;
        busy_  = true;
        used_  = true;
        on_    = true;
        tStep_ = now_ms;
        digitalWrite(cfg::HAPTIC_PIN, HIGH);
    }

    void update(uint32_t now_ms) {
        if (!busy_) return;
        if (on_) {
            if (now_ms - tStep_ < cfg::HAPTIC_MS) return;
            digitalWrite(cfg::HAPTIC_PIN, LOW);
            on_    = false;
            tStep_ = now_ms;
            if (--left_ == 0) { busy_ = false; tFree_ = now_ms; }
        } else {
            if (now_ms - tStep_ < cfg::HAPTIC_GAP_MS) return;
            digitalWrite(cfg::HAPTIC_PIN, HIGH);
            on_    = true;
            tStep_ = now_ms;
        }
    }

private:
    // used_ nur, damit die Ruhezeit nicht schon beim ersten Ausloeser greift:
    // kurz nach dem Start ist now_ms klein und tFree_ noch null.
    bool     busy_  = false;
    bool     on_    = false;
    bool     used_  = false;
    uint8_t  left_  = 0;
    uint32_t tStep_ = 0;
    uint32_t tFree_ = 0;
};
```

- [ ] **Step 3: Firmware bauen**

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run 2>&1 | Select-Object -Last 20
```

Erwartet: `SUCCESS`. Die bestehenden `haptic_.trigger(now_ms)`-Aufrufe im Controller bleiben durch den Vorgabewert gueltig.

- [ ] **Step 4: Commit**

```bash
git add include/config.h lib/Haptic/Haptic.h
git commit -m "$(cat <<'EOF'
feat: Haptik als Impuls-Sequenzer (Rechtsklick = zwei Impulse)

Die feste Sperrfrist entfaellt: sie muesste ueber der Musterdauer von 130 ms
liegen, DEBOUNCE_MS steht aber auf 180 ms - ein Doppelklick haette dann nur
noch einmal gebrummt. Gesperrt ist jetzt die Musterdauer plus eine Luecke.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: `TwistToggle` — die Ein/Aus-Drehgeste

Eine Schwelle, drei Bedeutungen: unter 1 s raus und zurueck schaltet ein oder aus; laenger gehalten wird daraus der Scroll-Modus; wurde dazwischen gepinch, schaltet die Rueckdrehung nicht. Das Modul wird hier gebaut und getestet, aber noch **nicht verdrahtet** — die Verdrahtung ist Task 14.

**Files:**
- Create: `lib/TwistToggle/TwistToggle.h`
- Create: `test/test_twist_toggle.cpp`
- Modify: `platformio.ini`

**Interfaces:**
- Produces: `enum class TwistEvent : uint8_t { None, Toggle, Held }`
- Produces: `struct TwistTuning { float onDeg; float backDeg; uint32_t maxMs; uint32_t lockoutMs; }`
- Produces: `class TwistToggle { explicit TwistToggle(const TwistTuning& = TwistTuning()); TwistEvent tick(float relTwistDeg, bool level, uint32_t now_ms); void cancel(); }`

- [ ] **Step 1: Den Test schreiben**

Neue Datei `test/test_twist_toggle.cpp`:

```cpp
// Test der Ein/Aus-Drehgeste. Laeuft auf dem PC - TwistToggle haengt bewusst
// an keiner Hardware und bindet weder Arduino.h noch config.h ein.
//
//   g++ -std=c++14 -Wall -Wextra -I lib/TwistToggle -o build/twist.exe test/test_twist_toggle.cpp && ./build/twist.exe
//
#include "TwistToggle.h"
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

// Haelt einen Winkel fuer eine Dauer und meldet, welches Ereignis dabei kam.
// Getaktet wie die Firmware: rund 209 Aufrufe je Sekunde.
static TwistEvent hold(TwistToggle& t, float deg, bool level,
                       uint32_t& now, uint32_t ms) {
    TwistEvent seen = TwistEvent::None;
    for (uint32_t i = 0; i < ms; i += 5) {
        const TwistEvent e = t.tick(deg, level, now);
        if (e != TwistEvent::None) seen = e;
        now += 5;
    }
    return seen;
}

static void test_quickOutAndBackToggles() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,  true, now, 200);
    CHECK(hold(t, 90.f, true, now, 300) == TwistEvent::None, "Ausdrehen allein schaltet schon");
    CHECK(hold(t, 0.f,  true, now, 100) == TwistEvent::Toggle, "raus und zurueck schaltet nicht");
}

static void test_slowReturnDoesNotToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,  true, now, 200);
    hold(t, 90.f, true, now, 1500);          // laenger als maxMs gehalten
    CHECK(hold(t, 0.f, true, now, 200) != TwistEvent::Toggle,
          "zu spaete Rueckkehr schaltet trotzdem");
}

static void test_heldComesOnceAfterMaxMs() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f, true, now, 200);
    CHECK(hold(t, 90.f, true, now, 1500) == TwistEvent::Held, "Held kommt nicht");
    // Ein zweites Mal darf es in derselben Ausdrehung nicht kommen.
    CHECK(hold(t, 90.f, true, now, 1500) == TwistEvent::None, "Held kommt mehrfach");
}

static void test_cancelBlocksToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,  true, now, 200);
    hold(t, 90.f, true, now, 300);
    t.cancel();                              // env-Gate ging auf: Pinch erkannt
    CHECK(hold(t, 0.f, true, now, 200) != TwistEvent::Toggle,
          "abgebrochene Ausdrehung schaltet trotzdem");
}

static void test_partialReturnDoesNotToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,  true, now, 200);
    hold(t, 90.f, true, now, 300);
    // 50 Grad liegt unter onDeg, aber ueber backDeg - keine echte Rueckkehr.
    CHECK(hold(t, 50.f, true, now, 400) != TwistEvent::Toggle,
          "halbe Rueckkehr schaltet");
}

static void test_levelLossBlocksToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,  true,  now, 200);
    hold(t, 90.f, true,  now, 200);
    hold(t, 90.f, false, now, 100);          // Arm nicht mehr waagrecht
    CHECK(hold(t, 0.f, true, now, 200) != TwistEvent::Toggle,
          "schaltet trotz weggefallenem Waagrecht-Gate");
}

static void test_lockoutBlocksSecondToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,  true, now, 200);
    hold(t, 90.f, true, now, 300);
    CHECK(hold(t, 0.f, true, now, 100) == TwistEvent::Toggle, "erste Geste schaltet nicht");
    // Sofort noch einmal, innerhalb von lockoutMs.
    hold(t, 90.f, true, now, 200);
    CHECK(hold(t, 0.f, true, now, 100) != TwistEvent::Toggle,
          "zweite Geste schaltet trotz Lockout");
}

static void test_toggleWorksAgainAfterLockout() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,  true, now, 200);
    hold(t, 90.f, true, now, 300);
    hold(t, 0.f,  true, now, 100);           // erster Toggle
    hold(t, 0.f,  true, now, 1000);          // Lockout abwarten
    hold(t, 90.f, true, now, 300);
    CHECK(hold(t, 0.f, true, now, 100) == TwistEvent::Toggle,
          "nach dem Lockout schaltet es nicht wieder");
}

static void test_afterHeldNoToggle() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,  true, now, 200);
    hold(t, 90.f, true, now, 1500);          // Held
    CHECK(hold(t, 0.f, true, now, 200) != TwistEvent::Toggle,
          "Rueckkehr nach dem Scroll-Modus schaltet ab");
}

// Das Vorzeichen darf keine Rolle spielen: aus der Zeige-Haltung heraus ist
// die Richtung anatomisch ohnehin festgelegt, und der Code muss sie nicht
// kennen. Dieselbe Geste nach der anderen Seite muss gleich wirken.
static void test_signDoesNotMatter() {
    TwistToggle t;
    uint32_t now = 1000;
    hold(t, 0.f,   true, now, 200);
    hold(t, -90.f, true, now, 300);
    CHECK(hold(t, 0.f, true, now, 100) == TwistEvent::Toggle,
          "negative Verdrehung schaltet nicht");
}

int main() {
    test_quickOutAndBackToggles();
    test_slowReturnDoesNotToggle();
    test_heldComesOnceAfterMaxMs();
    test_cancelBlocksToggle();
    test_partialReturnDoesNotToggle();
    test_levelLossBlocksToggle();
    test_lockoutBlocksSecondToggle();
    test_toggleWorksAgainAfterLockout();
    test_afterHeldNoToggle();
    test_signDoesNotMatter();

    std::printf("%d Pruefungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
```

- [ ] **Step 2: Test laufen lassen und Fehlschlag bestaetigen**

```powershell
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/TwistToggle -o "$env:TEMP\twist.exe" test/test_twist_toggle.cpp
```

Erwartet: **Compilerfehler**, `TwistToggle.h: No such file or directory`.

- [ ] **Step 3: `TwistToggle.h` schreiben**

Neue Datei `lib/TwistToggle/TwistToggle.h`:

```cpp
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
```

- [ ] **Step 4: Test laufen lassen und Bestehen bestaetigen**

```powershell
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/TwistToggle -o "$env:TEMP\twist.exe" test/test_twist_toggle.cpp
& "$env:TEMP\twist.exe"
```

Erwartet: `... Pruefungen, 0 Fehler`, Exit 0.

- [ ] **Step 5: `-I`-Eintrag ergaenzen**

In `platformio.ini` bei den `build_flags`, direkt nach `-I lib/ShakeToggle`:

```
    -I lib/TwistToggle
```

- [ ] **Step 6: Firmware bauen**

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run 2>&1 | Select-Object -Last 20
```

Erwartet: `SUCCESS`. Das Modul ist noch nicht verdrahtet, es wird also lediglich mitgefunden.

- [ ] **Step 7: Commit**

```bash
git add lib/TwistToggle/TwistToggle.h test/test_twist_toggle.cpp platformio.ini
git commit -m "$(cat <<'EOF'
feat: TwistToggle - Ein/Aus ueber eine Drehgeste des Unterarms

Gerade halten, um 90 Grad abdrehen, binnen einer Sekunde zurueck. Dieselbe
Ausdrehung traegt drei Bedeutungen, unterschieden durch Dauer und ob
dazwischen eine Erschuetterung lag. Noch nicht verdrahtet.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 12: `Pose::Turned` und zwei geglaettete Verdrehungswinkel

Die Verdrehachse bekommt nur noch eine Schwelle. `Pose::Idle` bleibt, ist aber ausschliesslich das Ergebnis des Waagrecht-Gates. Die Umbenennung `Scroll` → `Turned` ist noetig, weil der Zustand jetzt zwei Bedeutungen traegt — dieses Projekt hat mit `rollDeg`/`pitchDeg` schon einmal teuer gelernt, was ein irrefuehrender Name kostet.

Die schnellere Glaettung fuer die Geste darf den Cursor nicht verschlechtern: derselbe Winkel geht in die Roll-Kompensation und steht dort in einer Drehmatrix. Deshalb zwei Winkel.

**Files:**
- Modify: `lib/AirMouseState/AirMouseState.h` (nur die Umbenennung)
- Modify: `lib/PoseDetector/PoseDetector.h`
- Modify: `include/config.h`
- Modify: `lib/AirMouseController/AirMouseController.h`
- Modify: `test/test_state_machine.cpp` (nur die Umbenennung)

**Interfaces:**
- Produces: `enum class Pose : uint8_t { Point, Idle, Turned }`
- Produces: `PoseDetector::relTwistDeg()` (schnell, `MODE_TAU`) und `PoseDetector::relTwistSlow()` (langsam, `ROLLCOMP_TAU`)

- [ ] **Step 1: `Pose::Scroll` ueberall in `Pose::Turned` umbenennen**

Betroffen sind `lib/AirMouseState/AirMouseState.h`, `lib/PoseDetector/PoseDetector.h`, `lib/AirMouseController/AirMouseController.h` und `test/test_state_machine.cpp`. In `AirMouseState.h` zusaetzlich den Kopfkommentar anpassen:

```cpp
//   Power - Off / On, umgeschaltet durch die Drehgeste (TwistToggle).
//   Pose  - Point / Idle / Turned, kommt aus der Verdrehung (PoseDetector).
//
// Die Haltung waehlt, was ein Pinch bedeutet:
//
//   Point  - Hand gerade         -> Linksklick, Cursor folgt der Bewegung.
//   Idle   - Arm nicht waagrecht -> nichts. Idle ist KEINE Zone der
//            Verdrehung mehr, sondern allein das Ergebnis des Waagrecht-Gates.
//   Turned - Hand abgedreht      -> Rechtsklick; nach einer Sekunde zusaetzlich
//            Scroll-Joystick ueber die Armneigung.
```

- [ ] **Step 2: Test laufen lassen — muss weiter bestehen**

```powershell
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/AirMouseState -o "$env:TEMP\fsm.exe" test/test_state_machine.cpp
& "$env:TEMP\fsm.exe"
```

Erwartet: `... Pruefungen, 0 Fehler`. Eine reine Umbenennung darf nichts am Verhalten aendern.

- [ ] **Step 3: Konstanten in `config.h` ersetzen**

Den Block von `POINT_MAX_DEG` bis `MODE_HYST_DEG` ersetzen:

```cpp
    // Die Verdrehachse hat nur noch eine Schwelle. Was eine Ausdrehung
    // bedeutet, entscheidet sich erst beim Zurueckdrehen (TwistToggle):
    // schnell zurueck schaltet ein oder aus, gehalten wird daraus der
    // Scroll-Modus, mit einem Pinch dazwischen war es ein Rechtsklick.
    //
    // Verglichen wird der Betrag der Verdrehung. Eine Richtungskonstante gibt
    // es bewusst nicht: aus der Zeige-Haltung heraus ist TURN_ON_DEG
    // anatomisch nur in einer Richtung erreichbar (Supination ~90 Grad,
    // Pronation nur 10 bis 30), also muss der Code die Richtung nicht kennen.
    constexpr float TURN_ON_DEG  = 70.f;
    constexpr float TURN_OFF_DEG = 55.f;
```

Und den Glaettungsblock:

```cpp
    // Der Modus folgt der gehaltenen Haltung, nicht den Ausschlaegen einer
    // schnellen Bewegung. Deutlich kuerzer als frueher (0.25 s): die Glaettung
    // verzoegert Hin- und Rueckflanke um je eine Zeitkonstante, und das
    // 1-s-Fenster der Ein/Aus-Geste waere damit um die Haelfte verschmiert.
    constexpr float    MODE_TAU      = 0.10f;
    constexpr uint32_t MODE_DWELL_MS = 150;

    // Zweite, langsamere Glaettung derselben Verdrehung - nur fuer die
    // Roll-Kompensation im Zeiger. Dort steht der Winkel in einer Drehmatrix,
    // und deren Rauschen landet unmittelbar als Zittern im Cursor. Die
    // schnellere MODE_TAU waere an dieser Stelle ein Rueckschritt.
    constexpr float    ROLLCOMP_TAU  = 0.25f;
```

- [ ] **Step 4: `PoseDetector` umbauen**

`classify()` ersetzen:

```cpp
    // Nur noch zwei Zonen auf der Verdrehachse, mit Hysterese. Idle taucht
    // hier nicht auf: es ist keine Zone der Verdrehung mehr, sondern das
    // Ergebnis des Waagrecht-Gates in update().
    Pose classify() const {
        const float tilt = fabsf(rel_);
        if (pose_ == Pose::Turned) {
            return (tilt < cfg::TURN_OFF_DEG) ? Pose::Point : Pose::Turned;
        }
        // Deckt Point und Idle ab: aus beiden fuehrt derselbe Eintrittspunkt
        // nach Turned.
        return (tilt > cfg::TURN_ON_DEG) ? Pose::Turned : Pose::Point;
    }
```

Zweiten Winkel ergaenzen — in `update()` direkt nach der bestehenden Glaettung:

```cpp
        // Zweite, langsamere Glaettung fuer die Roll-Kompensation. Sie laeuft
        // auch waehrend der Bewegungssperre weiter, wie die uebrigen Winkel.
        const float aSlow = 1.f - expf(-dt / cfg::ROLLCOMP_TAU);
        fTwistSlow_ = wrapDeg(fTwistSlow_ + aSlow * wrapDeg(twistDeg - fTwistSlow_));
        relSlow_    = wrapDeg(fTwistSlow_ - cfg::TWIST_NEUTRAL_DEG);
```

Im `#if !USE_POSE_MODE`-Zweig `relSlow_ = 0.f;` mitsetzen. Neue Member und Zugriff:

```cpp
    float relTwistSlow() const { return relSlow_; }
```
```cpp
    float fTwistSlow_ = 0.f;
    float relSlow_    = 0.f;
```

- [ ] **Step 5: Der Zeiger bekommt den langsamen Winkel**

In `AirMouseController::handlePointing()`:

```cpp
        // Die Verdrehung kommt aus dem langsamer geglaetteten Kanal, nicht aus
        // dem, mit dem die Haltung und die Drehgeste arbeiten: sie geht hier in
        // eine Drehmatrix ein, und deren Rauschen wuerde als Zittern im Cursor
        // landen.
        pointer_.update(s.gx, s.gz, pose_.relTwistSlow(), elev_, dt, px, py);
```

- [ ] **Step 6: Beide PC-Tests laufen lassen**

```powershell
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/AirMouseState -o "$env:TEMP\fsm.exe" test/test_state_machine.cpp
& "$env:TEMP\fsm.exe"
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/ArmOrientation -o "$env:TEMP\arm.exe" test/test_arm_orientation.cpp
& "$env:TEMP\arm.exe"
```

Erwartet: beide `0 Fehler`.

- [ ] **Step 7: Firmware bauen**

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run 2>&1 | Select-Object -Last 20
```

Erwartet: `SUCCESS`.

- [ ] **Step 8: Commit**

```bash
git add lib/AirMouseState/AirMouseState.h lib/PoseDetector/PoseDetector.h include/config.h lib/AirMouseController/AirMouseController.h test/test_state_machine.cpp
git commit -m "$(cat <<'EOF'
feat: eine Schwelle auf der Verdrehachse, Pose::Scroll heisst Turned

Idle ist keine Zone der Verdrehung mehr, sondern allein das Ergebnis des
Waagrecht-Gates. MODE_TAU sinkt auf 0.10 s fuer die Drehgeste; die
Roll-Kompensation bekommt dafuer einen eigenen, langsamer geglaetteten Winkel,
sonst waere die schnellere Haltungserkennung ein Rueckschritt beim Cursor.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 13: `ScrollJoystick::inDeadzone()`

Der Rechtsklick darf nur ausloesen, solange die Armneigung ruhig gehalten wird — wer gerade scrollt, kippt den Arm, und ein Klick mitten im Lauf waere nicht vorhersehbar.

**Files:**
- Modify: `lib/ScrollJoystick/ScrollJoystick.h`

**Interfaces:**
- Produces: `bool ScrollJoystick::inDeadzone() const`

- [ ] **Step 1: Das Flag ergaenzen**

In `enter()` nach `rate_ = 0.f;`:

```cpp
        dead_ = true;   // frisch eingetreten: die Neigung IST der Nullpunkt
```

In `update()`, die Zeile mit `mag` und den fruehen Ausstieg:

```cpp
        const float mag = fabsf(dev) - cfg::SCROLL_DEAD_DEG;
        dead_ = (mag <= 0.f);

        if (dead_) { rate_ = 0.f; return 0; }
```

Zugriff und Member:

```cpp
    // Die Neigung steht in der Totzone, es wird also gerade nicht gescrollt.
    // Der Zustandsautomat entscheidet daran, ob ein Pinch in der abgedrehten
    // Haltung als Rechtsklick gilt.
    bool inDeadzone() const { return dead_; }
```
```cpp
    bool dead_ = true;
```

- [ ] **Step 2: Firmware bauen**

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run 2>&1 | Select-Object -Last 20
```

Erwartet: `SUCCESS`.

- [ ] **Step 3: Commit**

```bash
git add lib/ScrollJoystick/ScrollJoystick.h
git commit -m "$(cat <<'EOF'
feat: ScrollJoystick meldet, ob die Neigung in der Totzone steht

Grundlage fuer den Rechtsklick: er gilt nur, solange nicht gescrollt wird.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 14: Zustandsautomat und Verdrahtung

Der Kern. `AirMouseState` bekommt die neuen Ereignisse, der Controller verdrahtet `TwistToggle` an Stelle von `ShakeToggle`. Das ist ein Task, weil der Firmware-Build dazwischen nicht gruen sein kann.

**Files:**
- Modify: `lib/AirMouseState/AirMouseState.h`
- Modify: `test/test_state_machine.cpp`
- Modify: `lib/AirMouseController/AirMouseController.h`

**Interfaces:**
- Consumes: `TwistToggle`, `TwistEvent` (Task 11); `ScrollJoystick::inDeadzone()` (Task 13); `Haptic::trigger(now, pulses)` (Task 10); `PoseDetector::relTwistDeg()` und `level()` (Task 12).
- Produces: `Actions { bool click, rightClick, resetPointer, enterScroll, resetPose; uint8_t hapticPulses; }`
- Produces: `AirMouseState::onPower()`, `onPose(Pose)`, `onTwistHeld()`, `onPinch(bool scrollIdle)`, `on()`, `pointing()`, `scrolling()`, `pose()`, `power()`

- [ ] **Step 1: Den Test umschreiben**

`test/test_state_machine.cpp` — folgende Aenderungen. Zuerst die Hilfen:

```cpp
static AirMouseState turnedOn() {
    AirMouseState s;
    s.onPower();
    return s;
}

static AirMouseState inPose(Pose p) {
    AirMouseState s = turnedOn();
    s.onPose(p);
    return s;
}

// In der abgedrehten Haltung wird der Scroll-Joystick erst nach einer Sekunde
// zugeschaltet. Diese Hilfe stellt genau das her.
static AirMouseState scrolling() {
    AirMouseState s = inPose(Pose::Turned);
    s.onTwistHeld();
    return s;
}
```

Dann alle `s.onShake()` durch `s.onPower()` ersetzen und alle `s.onPinch()` durch `s.onPinch(true)` (Neigung ruhig), ausser wo ausdruecklich anders gemeint. `a.haptic` wird ueberall zu `a.hapticPulses > 0`. Diese Tests aendern sich inhaltlich:

```cpp
static void test_pinchWhilePointingClicksLeft() {
    const Actions a = turnedOn().onPinch(true);
    CHECK(a.click, "Pinch beim Zeigen klickt nicht links");
    CHECK(!a.rightClick, "Pinch beim Zeigen klickt rechts");
    CHECK(a.hapticPulses == 1, "Linksklick meldet nicht genau einen Impuls");
}

static void test_pinchWhileTurnedClicksRight() {
    AirMouseState s = inPose(Pose::Turned);
    const Actions a = s.onPinch(true);
    CHECK(a.rightClick, "Pinch bei gedrehter Hand klickt nicht rechts");
    CHECK(!a.click, "Pinch bei gedrehter Hand klickt links");
    CHECK(a.hapticPulses == 2, "Rechtsklick meldet nicht zwei Impulse");
}

// Wer den Arm kippt, scrollt gerade - ein Klick mitten im Lauf waere fuer den
// Nutzer nicht vorhersehbar.
static void test_pinchWhileActuallyScrollingDoesNothing() {
    AirMouseState s = scrolling();
    const Actions a = s.onPinch(false);
    CHECK(!a.click, "Pinch beim Scrollen klickt links");
    CHECK(!a.rightClick, "Pinch beim Scrollen klickt rechts");
    CHECK(a.hapticPulses == 0, "Pinch beim Scrollen brummt");
}

// Auch waehrend der Scroll-Joystick zugeschaltet ist, gilt der Rechtsklick -
// solange die Neigung ruhig gehalten wird. Sonst waere er nach einer Sekunde
// unerreichbar.
static void test_rightClickWorksWhileScrollJoystickIsOn() {
    AirMouseState s = scrolling();
    const Actions a = s.onPinch(true);
    CHECK(a.rightClick, "Rechtsklick faellt weg, sobald der Joystick an ist");
}

static void test_pinchInIdleDoesNothing() {
    AirMouseState s = inPose(Pose::Idle);
    const Actions a = s.onPinch(true);
    CHECK(!a.click && !a.rightClick, "Pinch bei nicht waagrechtem Arm klickt");
    CHECK(a.hapticPulses == 0, "Pinch in Idle brummt");
}

// Der Scroll-Joystick kommt nicht mit der Haltung, sondern erst mit onTwistHeld.
// Sonst wuerde jede Ein/Aus-Geste nebenbei ein Stueck weit scrollen.
static void test_turnedAloneDoesNotScroll() {
    AirMouseState s = inPose(Pose::Turned);
    CHECK(!s.scrolling(), "die Haltung allein schaltet schon den Joystick zu");
    const Actions a = s.onTwistHeld();
    CHECK(a.enterScroll, "onTwistHeld nullt den Joystick nicht");
    CHECK(s.scrolling(), "onTwistHeld schaltet den Joystick nicht zu");
}

// Verlaesst man die abgedrehte Haltung, muss der Joystick wieder aus sein -
// sonst scrollte die naechste Ausdrehung ohne Haltezeit sofort los.
static void test_leavingTurnedStopsScrolling() {
    AirMouseState s = scrolling();
    s.onPose(Pose::Point);
    CHECK(!s.scrolling(), "der Joystick bleibt nach dem Zurueckdrehen an");
    s.onPose(Pose::Turned);
    CHECK(!s.scrolling(), "der Joystick kommt ohne Haltezeit zurueck");
}

static void test_onTwistHeldOnlyInTurned() {
    for (Pose p : {Pose::Point, Pose::Idle}) {
        AirMouseState s = inPose(p);
        const Actions a = s.onTwistHeld();
        CHECK(!a.enterScroll, "onTwistHeld wirkt ausserhalb der abgedrehten Haltung");
        CHECK(!s.scrolling(), "gescrollt ausserhalb der abgedrehten Haltung");
    }
    AirMouseState off;
    const Actions a = off.onTwistHeld();
    CHECK(!a.enterScroll, "onTwistHeld wirkt im Ruhezustand");
}
```

`test_exactlyOneButtonPerPose` und `test_pinchIsStateless` auf `Pose::Turned` und `onPinch(true)` umstellen. Zwei alte Tests werden ersetzt und entfallen: `test_scrollEntryResetsJoystick` durch `test_turnedAloneDoesNotScroll`, und `test_pinchWhileScrollingDoesNothing` durch `test_pinchWhileActuallyScrollingDoesNothing` — die alte Fassung pruefte die Haltung, die neue prueft, ob tatsaechlich gescrollt wird. In `test_exactlyOneButtonPerPose` bleibt die Zaehlung gueltig: `Point` klickt links, `Turned` rechts, `Idle` gar nicht. In `test_allStatesAllEvents` und `test_randomWalkKeepsInvariants` die Ereignisliste erweitern:

```cpp
                Actions a;
                switch (ev) {
                    case 0: a = s.onPower();            break;
                    case 1: a = s.onPinch(true);        break;
                    case 2: a = s.onPinch(false);       break;
                    case 3: a = s.onPose(Pose::Point);  break;
                    case 4: a = s.onPose(Pose::Idle);   break;
                    case 5: a = s.onPose(Pose::Turned); break;
                    case 6: a = s.onTwistHeld();        break;
                }
```

Die Schleifengrenze auf `ev < 7` und die Erwartung auf `visited == 28` setzen (4 Gruppen mal 7 Ereignisse). Im Zufallslauf `% 7` statt `% 5`, und die Invariante am Ende anpassen:

```cpp
        if (a.rightClick && s.pose() != Pose::Turned) {
            CHECK(false, "Rechtsklick ausserhalb der abgedrehten Haltung"); return;
        }
        // Der Joystick darf nie ohne die passende Haltung laufen.
        if (s.scrolling() && s.pose() != Pose::Turned) {
            CHECK(false, "Joystick laeuft ausserhalb der abgedrehten Haltung"); return;
        }
        if (s.scrolling() && !s.on()) { CHECK(false, "Joystick laeuft im Ruhezustand"); return; }
```

Und die neuen Tests in `main()` eintragen.

- [ ] **Step 2: Test laufen lassen und Fehlschlag bestaetigen**

```powershell
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/AirMouseState -o "$env:TEMP\fsm.exe" test/test_state_machine.cpp
```

Erwartet: **Compilerfehler**, `'class AirMouseState' has no member named 'onPower'`.

- [ ] **Step 3: `AirMouseState` umbauen**

`Actions` und die Ereignisse ersetzen:

```cpp
struct Actions {
    bool click        = false;  // links
    bool rightClick   = false;
    bool resetPointer = false;
    bool enterScroll  = false;  // Scroll-Joystick auf aktuelle Neigung nullen
    bool resetPose    = false;  // Haltungserkennung auf Point zuruecksetzen

    // 0 = still, 1 = Linksklick / Haltungswechsel / Ein-Aus, 2 = Rechtsklick.
    // Der Rechtsklick ist an der Hand sonst nicht vom Linksklick zu
    // unterscheiden - und er passiert in einer Haltung, in der man den Cursor
    // nicht sieht.
    uint8_t hapticPulses = 0;
};

class AirMouseState {
public:
    // Die Drehgeste hat vollstaendig durchgelaufen (TwistEvent::Toggle).
    Actions onPower() {
        Actions a;
        if (power_ == Power::Off) {
            power_ = Power::On;
            pose_  = Pose::Point;
            scrollOn_      = false;
            a.resetPose    = true;
            a.resetPointer = true;
            a.hapticPulses = 1;
            return a;
        }
        power_    = Power::Off;
        scrollOn_ = false;
        a.resetPointer = true;
        a.hapticPulses = 1;
        return a;
    }

    Actions onPose(Pose next) {
        Actions a;
        if (power_ != Power::On || next == pose_) return a;

        if (pose_ == Pose::Point)  a.resetPointer = true;   // verlassen
        if (pose_ == Pose::Turned) scrollOn_ = false;       // Joystick aus

        pose_ = next;

        switch (next) {                                     // betreten
            // Turned schaltet den Joystick NICHT zu - das macht erst
            // onTwistHeld() nach einer Sekunde. Sonst wuerde jede Ein/Aus-Geste
            // nebenbei ein Stueck weit scrollen.
            case Pose::Turned: a.hapticPulses = 1; break;
            case Pose::Point:  a.resetPointer = true; a.hapticPulses = 1; break;
            // Idle bleibt bewusst still, sonst brummt es zweimal auf dem Weg
            // vom Zeigen in die abgedrehte Haltung.
            case Pose::Idle:   break;
        }
        return a;
    }

    // Die Ausdrehung wurde laenger als das Schaltfenster gehalten: aus der
    // Ein/Aus-Geste ist der Scroll-Modus geworden.
    Actions onTwistHeld() {
        Actions a;
        if (power_ != Power::On || pose_ != Pose::Turned || scrollOn_) return a;
        scrollOn_      = true;
        a.enterScroll  = true;
        a.hapticPulses = 1;
        return a;
    }

    // Die Haltung entscheidet ueber die Taste. scrollIdle sagt, ob die
    // Armneigung in der Totzone des Joysticks steht - wer gerade scrollt,
    // kippt den Arm, und ein Klick mitten im Lauf waere fuer den Nutzer nicht
    // vorhersehbar.
    Actions onPinch(bool scrollIdle) {
        Actions a;
        if (power_ != Power::On) return a;

        switch (pose_) {
            case Pose::Point:
                a.click = true; a.hapticPulses = 1;
                break;
            case Pose::Turned:
                if (scrollIdle) { a.rightClick = true; a.hapticPulses = 2; }
                break;
            case Pose::Idle:
                break;
        }
        return a;
    }

    Power power() const { return power_; }
    Pose  pose()  const { return pose_; }

    bool on()        const { return power_ == Power::On; }
    bool pointing()  const { return power_ == Power::On && pose_ == Pose::Point; }
    bool scrolling() const { return power_ == Power::On && pose_ == Pose::Turned && scrollOn_; }

private:
    Power power_    = Power::Off;
    Pose  pose_     = Pose::Point;
    bool  scrollOn_ = false;
};
```

Den Absatz im Kopfkommentar ueber die Uebergangstabelle auf die neuen Ereignisse bringen.

- [ ] **Step 4: Test laufen lassen und Bestehen bestaetigen**

```powershell
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/AirMouseState -o "$env:TEMP\fsm.exe" test/test_state_machine.cpp
& "$env:TEMP\fsm.exe"
```

Erwartet: `... Pruefungen, 0 Fehler`, Exit 0.

- [ ] **Step 5: Den Controller verdrahten**

In `lib/AirMouseController/AirMouseController.h`:

`#include "ShakeToggle.h"` durch `#include "TwistToggle.h"` ersetzen, Member `ShakeToggle shaker_;` durch `TwistToggle twist_;`.

`apply()` ersetzen:

```cpp
    // Die einzige Stelle, an der Aktionen des Automaten Wirkung entfalten.
    void apply(const Actions& a, uint32_t now_ms) {
        if (a.click)      { mouse_.click();      clickPulse_ = true; }
        if (a.rightClick) { mouse_.rightClick(); clickPulse_ = true; }

        if (a.resetPose)     pose_.reset();
        if (a.enterScroll)   scroll_.enter(elev_);
        if (a.resetPointer) { accumX_ = accumY_ = 0.f; pointer_.reset(); }
        if (a.hapticPulses)  haptic_.trigger(now_ms, a.hapticPulses);
    }
```

`handlePinch()`: den Aufruf des Automaten auf die neue Signatur bringen:

```cpp
        if (pinched) apply(fsm_.onPinch(scroll_.inDeadzone()), now_ms);
```

In `update()` den Ereignisblock ersetzen:

```cpp
        // --- Ereignisse einsammeln und dem Automaten geben ---------------
        // Der Haltungs-Detektor laeuft immer, auch im Ruhezustand: seine
        // Glaettung ist beim Einschalten dann schon eingeschwungen statt bei
        // null, und die Drehgeste braucht den geglaetteten Winkel gerade dann,
        // wenn die Maus noch aus ist.
        const Pose posed = pose_.update(twist_, elev_, s.gyroSum, dt, now_ms);

        // Eine Erschuetterung ueber der env-Schwelle verbraucht die laufende
        // Ausdrehung: sie war ein Pinch und keine Schaltgeste. Absichtlich an
        // der Schwelle und nicht am erkannten Klick - verpasst der
        // Klassifikator den Pinch, wuerde das Zurueckdrehen sonst die Maus
        // abschalten statt rechtszuklicken. Nur im eingeschalteten Zustand:
        // dort laeuft der Pinch-Pfad, und eine Erschuetterung soll die
        // Einschalt-Geste nicht abbrechen.
        if (fsm_.on() && env > cfg::ENV_ON) twist_.cancel();

        switch (twist_.tick(pose_.relTwistDeg(), pose_.level(), now_ms)) {
            case TwistEvent::Toggle: apply(fsm_.onPower(),     now_ms); break;
            case TwistEvent::Held:   apply(fsm_.onTwistHeld(), now_ms); break;
            case TwistEvent::None:   break;
        }

        if (fsm_.on()) {
            // Haltung vor Pinch: welche Taste ein Pinch ausloest, haengt an der
            // Haltung, und die soll im selben Takt schon die aktuelle sein.
            apply(fsm_.onPose(posed), now_ms);
            handlePinch(s, env, now_ms);
        }
```

Im Teleplot-Block, bei den immer gesendeten Kanaelen:

```cpp
        // tw trennt "Geste nicht erkannt" von "erkannt, aber verworfen":
        // 0 = gerade, 1 = ausgedreht, 2 = ausgedreht und verbraucht,
        // 3 = Lockout nach dem Schalten.
        Serial.print(">tw:");     Serial.println(twist_.state());
```

Den Kopfkommentar der Klasse anpassen: `ShakeToggle` heisst dort jetzt `TwistToggle`, und der Satz ueber `airmouseOn_, lastMode_ und dragging_` verliert `dragging_`.

- [ ] **Step 6: Firmware bauen**

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run 2>&1 | Select-Object -Last 20
```

Erwartet: `SUCCESS`.

- [ ] **Step 7: Commit**

```bash
git add lib/AirMouseState/AirMouseState.h test/test_state_machine.cpp lib/AirMouseController/AirMouseController.h
git commit -m "$(cat <<'EOF'
feat: Drehgeste schaltet ein und aus, Rechtsklick in der abgedrehten Haltung

onShake wird zu onPower, neu onTwistHeld fuer den Scroll-Joystick und
onPinch(scrollIdle) fuer den Rechtsklick. Actions::haptic wird zu
hapticPulses - Rechtsklick meldet sich mit zwei Impulsen.

Eine Erschuetterung ueber ENV_ON verbraucht die laufende Ausdrehung. Damit
schaltet ein vom Klassifikator verpasster Pinch die Maus nicht ab.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 15: Aufraeumen

`ShakeToggle` ist ersetzt, `PinchGesture` war schon vorher unbenutzt. Dazu die Dokumentation auf den neuen Stand.

**Files:**
- Delete: `lib/ShakeToggle/`, `lib/PinchGesture/`
- Modify: `platformio.ini`, `include/config.h`, `TODO.md`, `CLAUDE.md`

- [ ] **Step 1: Die beiden Ordner loeschen**

```powershell
Remove-Item -Recurse -Force lib/ShakeToggle
Remove-Item -Recurse -Force lib/PinchGesture
```

- [ ] **Step 2: `platformio.ini` bereinigen**

Die Zeile `-I lib/ShakeToggle` entfernen. `-I lib/TwistToggle` bleibt.

- [ ] **Step 3: `config.h` bereinigen**

Den ganzen Block `// --- Ein/Aus durch Schuetteln ---` mit `SHAKE_ON`, `SHAKE_OFF`, `SHAKE_REFRACT_MS`, `SHAKE_GAP_MIN_MS`, `SHAKE_GAP_MAX_MS` und `SHAKE_LOCKOUT_MS` ersatzlos loeschen. An seine Stelle:

```cpp
    // --- Ein/Aus durch die Drehgeste ------------------------------------
    // Die Parameter stehen in TwistTuning (lib/TwistToggle/TwistToggle.h) und
    // bewusst nicht hier: der Header muss sich ohne Toolchain uebersetzen
    // lassen, damit test/test_twist_toggle.cpp auf dem PC laeuft - dieselbe
    // Ausnahme wie bei ArmOrientation und PointerTuning.
```

Im `DEBUG_PINCH`-Kommentar (Zeile 16) `drag` aus der Kanalliste streichen, im `DEBUG_POINT`-Kommentar `tg` und `tgr` ergaenzen (`tw` steht bei den immer gesendeten Kanaelen). Ausserdem die Begruendung bei `POSE_STILL_DPS` korrigieren — der Bezug auf `SHAKE_ON` existiert nicht mehr:

```cpp
    // Waehrend einer heftigen Bewegung wird die Haltung gar nicht erst
    // gewechselt. Glaettung, Haltezeit und Hysterese daempfen die Ausschlaege
    // nur; eine gehaltene Haltung ist aber per Definition nichts, was man
    // mitten im Schwung einnimmt.
    //
    // Die Schwelle lag frueher zwischen dem normalen Gebrauch (~250 Grad/s) und
    // der Schuettel-Schwelle. Die Obergrenze ist mit dem Schuetteln
    // weggefallen, der Wert steht also nur noch auf einem Bein und gehoert am
    // Geraet gegengeprueft.
    //
    // Wichtig: eine zuegige 90-Grad-Drehung erzeugt rund 300 Grad/s und friert
    // damit die Haltungsentscheidung ein. TwistToggle arbeitet deshalb auf dem
    // Winkel und nicht auf pose() - die Winkel laufen waehrend der Sperre
    // weiter, nur die Entscheidung ruht.
    constexpr float    POSE_STILL_DPS = 300.f;
    constexpr uint32_t POSE_CALM_MS   = 250;
```

- [ ] **Step 4: `TODO.md` bereinigen**

Drei Punkte ersatzlos loeschen — **nach Inhalt suchen, nicht nach Zeilennummer**, die verschieben sich beim Loeschen:

- den Aufzaehlungspunkt, der mit `**Doppel-Pinch-Fenster:**` beginnt (`DOUBLE_MS` = 500 ms),
- den Aufzaehlungspunkt, der mit `**Drag and Drop (offen, Idee):**` beginnt,
- den Aufzaehlungspunkt, der mit `**Schüttel-Schwelle:**` beginnt (`cfg::SHAKE_ON` = 350).

Im Abschnitt „Messungen am Geraet" in der Kanalliste `drag` durch `tw` ersetzen. Am Ende der Datei einen neuen Abschnitt anfuegen:

```markdown
## Aufnahmeprotokoll für den neuen Datensatz

Gilt ab der gravitationsfreien Kanalbelegung (`lax/lay/laz` statt `ax/ay/az`).
Die alten CSV sind doppelt ungültig: mit 100 Hz statt 209 Hz aufgenommen **und**
mit den rohen Achsen.

- Werkzeug: `edge-impulse-data-forwarder`, 115200 Baud. Fünf Achsen in der
  Reihenfolge von `feat::pack()`: `env, gyro, lax, lay, laz`.
- Die vom Forwarder gemeldete Frequenz gegen 209 Hz prüfen. Weicht sie ab oder
  leuchtet die eingebaute LED, **nicht aufnehmen** – die LED bleibt ab dem
  ersten verpassten Abtastschritt bis zum Reset an.
- Klassen: `pinch`, `idle`, `negative`.
- Hard Negatives ausdrücklich mitnehmen (Xu et al. 2022): Tastaturtippen,
  Klopfen auf den Tisch, Klatschen, Türklinke, Gehen.
- **Validierung der Haltungsunabhängigkeit:** der Grossteil der `pinch`-Daten in
  Zeige-Haltung; zusätzlich ein Satz Pinches in der abgedrehten Haltung, der
  **nur ins Test-Set** kommt. Fällt die Genauigkeit dort nicht ab, ist die
  Gravitationsfreiheit belegt – eine belastbare Zahl für Kap. 5 und zugleich die
  Erklärung, warum der Rechtsklick vorher nicht funktionierte.

## Messplan am Gerät (nach der UX-Überarbeitung)

Schritte 1–7 mit `USE_ML_PINCH false`, damit ein Fehlverhalten nicht dem Modell
zugeschrieben wird, das gar nicht die Ursache ist.

1. `ovr` – hält die Schleife den Takt?
2. `gx/gy/gz` im Stillstand über 30 s, dann eine bewusst langsame Zeigebewegung
   über ~10 s: die Rate darf nicht wegsacken (Bias-Fix).
3. `accx`, `mvfail` bei schneller Bewegung: Rückstau muss nach dem Anhalten
   sofort auf null gehen.
4. `rx`/`ry` gegen `gx`/`gz`, Hand ruhig gehalten – die Restamplitude ist das
   Wackeln. Erst `EURO_BETA = 0` und `EURO_MIN_CUTOFF` senken, dann `BETA` über
   0.1 / 0.2 / 0.4 anheben, zuletzt `ACCEL_K` 0 gegen 2.0 halten. Ein 3-Tap-
   Median vor der Deadzone nur, falls danach etwas übrig bleibt.
5. `rtwist` in ruhiger Zeige-Haltung: steht es bei 0? Sonst `TWIST_NEUTRAL_DEG`
   nachziehen – alle folgenden Schwellen hängen daran.
6. `rtwist`, `on`, `tw`, `tg`, `tgr`, `pose`, `dpose` bei der Ein/Aus-Geste:
   schaltet sie zuverlässig? Schaltet sie **nicht** beim Rechtsklick und
   **nicht** nach dem Scrollen? Steht der Cursor während der ganzen Drehung
   still – `tg` muss auf 0 gehen, bevor `rtwist` nennenswert läuft?
   `TURN_ON_DEG`, `TwistTuning::backDeg`/`maxMs` und `TwistGuardTuning::lowDps`/
   `highDps`/`releaseS` nachziehen. Zu hohes `lowDps` = der Cursor läuft beim
   Hindrehen weg; zu niedriges = normales Zeigen mit leicht mitdrehender Hand
   wird abgewürgt.
7. `srate` und `click` in `Turned`: Rechtsklick bei ruhig gehaltener Neigung,
   kein Klick während des Scrollens, Joystick erst nach einer Sekunde.
8. Erst jetzt `USE_ML_PINCH true`, nach der Neuaufnahme: `env`, `gate`, `p_ml`
   beim Pinchen in beiden Haltungen.
```

- [ ] **Step 5: `CLAUDE.md` auf den neuen Stand bringen**

Zu aendern:

- Abschnitt „Build / Flash": die drei PC-Tests statt zwei nennen, plus die beiden neuen (`test_twist_toggle.cpp`, `test_one_euro.cpp`, `test_pinch_features.cpp`) mit ihren `-I`-Pfaden. Die festen Pruefzahlen („322 Pruefungen") entfernen — sie stimmen nach dem Umschreiben nicht mehr; Kriterium ist `0 Fehler`.
- Abschnitt „Architektur", Ablauf pro Tick: Punkt 1 ist jetzt `TwistToggle` (Drehgeste) statt `ShakeToggle`. Der Satz ueber den Nullpunkt der Verdrehung bleibt gueltig — er ist weiterhin fest.
- Der Absatz „Die Haltung waehlt, was ein Pinch bedeutet" auf `Point` → Linksklick, `Idle` (nur Waagrecht-Gate) → nichts, `Turned` → Rechtsklick bei ruhiger Neigung.
- Der Absatz ueber `lib/PinchGesture/` entfaellt; der Absatz ueber die fehlende `Grab`-Achse bleibt, ohne den Verweis auf den Ordner.
- Bei den Policy-Modulen `PinchGesture` durch `TwistToggle` und `TwistGuard` ersetzen.
- Im Ablauf pro Tick beim Zeiger ergaenzen, dass `TwistGuard` die Bewegung waehrend einer Unterarmdrehung ausblendet, und **warum die Rate aus der Lageschaetzung kommt und nicht aus `gy`**: die Unterarmachse faellt nicht exakt mit einer Platinenachse zusammen, eine Verdrehung leckt deshalb auch in `gx` und `gz`.
- Im Punkt zu `feat::pack()` ergaenzen, dass die Kanaele 2–4 die **lineare** Beschleunigung fuehren und warum.

- [ ] **Step 6: Alle vier PC-Tests und die Firmware bauen**

```powershell
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/AirMouseState -o "$env:TEMP\fsm.exe" test/test_state_machine.cpp;   & "$env:TEMP\fsm.exe"
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/ArmOrientation -o "$env:TEMP\arm.exe" test/test_arm_orientation.cpp; & "$env:TEMP\arm.exe"
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/TwistToggle -o "$env:TEMP\twist.exe" test/test_twist_toggle.cpp;     & "$env:TEMP\twist.exe"
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/Filters -o "$env:TEMP\euro.exe" test/test_one_euro.cpp;              & "$env:TEMP\euro.exe"
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/TwistGuard -o "$env:TEMP\guard.exe" test/test_twist_guard.cpp;      & "$env:TEMP\guard.exe"
& "C:\Strawberry\c\bin\g++.exe" -std=c++14 -Wall -Wextra -I lib/ImuReader -I lib/PinchFeatures -o "$env:TEMP\feat.exe" test/test_pinch_features.cpp; & "$env:TEMP\feat.exe"
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run 2>&1 | Select-Object -Last 20
```

Erwartet: sechs mal `0 Fehler` und `SUCCESS`.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
chore: ShakeToggle und PinchGesture entfernt, Doku auf den neuen Stand

Das Schuetteln ist durch die Drehgeste ersetzt, PinchGesture war seit dem
Ausbau des Doppel-Pinch unbenutzt. TODO.md bekommt Aufnahmeprotokoll und
Messplan, CLAUDE.md den neuen Ablauf pro Tick.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

## Nach dem Plan: was am Geraet passiert

Der Plan endet mit gruenen Tests und einem gebauten Binary — **nicht** mit einer abgenommenen Funktion. Die Startwerte in `config.h` und `TwistTuning` sind begruendete Ausgangspunkte, keine Ergebnisse. Der Messplan in `TODO.md` (Task 15, Step 4) ist die Fortsetzung, und er gehoert Nils: bauen ja, flashen nein.

Zwei Dinge sind erst nach der Neuaufnahme des Datensatzes bewertbar:

- **Der Rechtsklick.** Bis dahin mit `USE_ML_PINCH false` pruefen — die reine Schwellwert-Erkennung kennt keine Haltung.
- **Die Haltungsunabhaengigkeit der Kanaele.** Der Testsatz aus der abgedrehten Haltung ist die Abnahme fuer Task 1.
