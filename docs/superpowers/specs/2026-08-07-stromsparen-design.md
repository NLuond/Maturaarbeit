# Air Mouse – Stromsparen (Design)

**Datum:** 2026-08-07
**Umfang:** Drei Betriebszustände mit hardwaregestütztem Aufwachen durch Bewegung,
schlafende CPU zwischen den Takten, abgeschaltetes BLE im Ruhezustand, DC/DC-Regler,
stillgelegte Peripherie, und ein Batteriemodul, mit dem sich die Ersparnis am Gerät
belegen lässt.

Zielbetrieb ist **Akku über BLE**. Über USB hängt das Gerät ohnehin an einer Stromquelle,
und ein Ruhezustand würde die Enumeration abwerfen — ein Teil der Massnahmen wirkt dort
trotzdem, der grosse Hebel nicht.

---

## 1. Ausgangslage

Es gibt heute keinerlei Stromsparmassnahme. Zwei Befunde bestimmen den Entwurf:

**Die Schleife läuft leer durch.** `src/main.cpp` kehrt aus `loop()` sofort zurück, wenn
der Takt noch nicht fällig ist. Der Kern der Adafruit-Firmware ruft `loop()` daraufhin
sofort wieder auf (`while (1) { loop(); yield(); }`), der Cortex-M4F läuft also
durchgehend mit 64 MHz und tut in rund 97 % der Zeit nichts. Das ist der grösste einzelne
Verbraucher im Betrieb.

**Im ausgeschalteten Zustand läuft alles weiter.** Ist die Maus über die Drehgeste
ausgeschaltet, tastet die IMU weiter mit 208 Hz ab, Madgwick rechnet weiter, und BLE bleibt
verbunden — nur die Wirkung fällt weg. Ein Gerät, das man aus der Hand legt, verbraucht
damit fast so viel wie eines, das benutzt wird.

### Was die Hardware hergibt (am Variant-Header geprüft)

| Pin | Bedeutung |
|---|---|
| `PIN_LSM6DS3TR_C_INT1` = P18 | IMU-Interrupt auf einem GPIO — Aufwecken durch Bewegung möglich |
| `PIN_LSM6DS3TR_C_POWER` = P15 | IMU vollständig abschaltbar |
| `PIN_VBAT` = P32, `VBAT_ENABLE` = P14 | Akkuspannung messbar |
| `PIN_PDM_PWR` = P19 | Mikrofon-Versorgung |

Der LSM6DS3TR-C bringt drei einschlägige Funktionen mit, alle über `writeRegister()` der
Seeed-Bibliothek erreichbar:

| Funktion | Register | Eignung |
|---|---|---|
| **Wake-Up** | `WAKE_UP_THS`, `WAKE_UP_DUR`, `MD1_CFG` | direkt passend: Beschleunigungsänderung über Schwelle → INT1 |
| **Activity/Inactivity** | `TAP_CFG.INACT_EN` | die IMU senkt bei Ruhe selbst ihre Rate und schaltet das Gyroskop ab |
| Significant Motion | `CTRL10_C`, `INT1_SIGN_MOT` | zu träge fürs Armheben, nicht verwendet |

Verwendet wird **Wake-Up zusammen mit Activity/Inactivity**: die IMU verwaltet ihren
eigenen Stromzustand und meldet Bewegung per Pin. Der Mikrocontroller muss nichts pollen.

### Was der Arduino-Kern hergibt (am Framework geprüft)

| Zweck | API | Befund |
|---|---|---|
| CPU zwischen Takten schlafen | `delay()` | ruft `vTaskDelay`; FreeRTOS läuft mit `configUSE_TICKLESS_IDLE = 1`, der Kern schläft also wirklich |
| Ruhezustand | `suspendLoop()` / `resumeLoop()` | suspendiert die `loop()`-Task; ohne lauffähige Task schläft die CPU bis zum Interrupt |
| einzelner Schlafschritt | `waitForEvent()` | in `cores/nRF5/wiring.h` |

Alles Nötige ist damit vorhanden; es muss nichts nachgebaut werden.

---

## 2. Drei Betriebszustände

```
   AKTIV                    BEREIT                      SCHLAF
   FSM eingeschaltet        FSM aus, Arm bewegt         60 s ohne Bewegung
                            sich noch
   IMU 208 Hz               IMU 52 Hz                   IMU Low-Power + INT1
   Madgwick, ML, HID        Madgwick, Drehgeste         nichts laeuft
   BLE verbunden            BLE verbunden               BLE AUS
   CPU schlaeft im Takt     CPU schlaeft im Takt        loop() suspendiert
   ~2-3 mA                  ~1 mA                       ~0.03-0.05 mA
       ▲                        │  ▲                        │  ▲
       └──── Drehgeste ─────────┘  └──── INT1 (Bewegung) ───┘  │
                                                               │
                                      SLEEP_AFTER_MS = 60 s ───┘
```

**Kein System OFF.** Das RAM bleibt erhalten, und damit der über Minuten gelernte
Gyro-Nullpunkt — ein realer Vorteil gegenüber System OFF, wo die Firmware neu startet und
alles von vorn einschwingen müsste. Die absolute Ersparnis von System OFF (~0.4 µA gegen
~30 µA) rechtfertigt diesen Preis bei einem Gerät, das wöchentlich geladen wird, nicht.

**Eingeschlafen wird nie im Zustand AKTIV.** Der Zeitgeber läuft ausschliesslich in
BEREIT. Sonst könnte die Maus mitten im Gebrauch verschwinden, während man den Cursor nur
ruhig auf einem Ziel hält.

**Bewegung heisst `gyroSum` über einer Schwelle**, nicht "irgendein Ereignis". Eine ruhig
gehaltene, aber getragene Hand soll nicht als Ruhe zählen; deshalb die Drehrate und nicht
die Beschleunigung.

---

## 3. Wo die Entscheidung lebt: `lib/SleepPolicy/`

Nach dem Muster der übrigen Erkenner: hardwarefrei, ohne `config.h` und `<Arduino.h>`,
auf dem PC prüfbar. Es entscheidet *wann*, nicht *wie*.

```cpp
struct SleepTuning {
    float    stillDps   = 20.f;    // darunter gilt der Arm als ruhig
    uint32_t sleepAfter = 60000;   // ms Ruhe bis zum Schlaf
    uint32_t settleMs   = 300;     // Sperre nach dem Aufwachen
};

enum class SleepEvent : uint8_t { None, GoToSleep, Settled };

class SleepPolicy {
public:
    explicit SleepPolicy(const SleepTuning& t = SleepTuning());

    // mouseOn: FSM ist eingeschaltet -> nie einschlafen.
    SleepEvent tick(bool mouseOn, float gyroSum, uint32_t now_ms);

    // Nach dem Aufwachen aufrufen: startet das Einschwingfenster.
    void wake(uint32_t now_ms);

    // Waehrend des Einschwingens bleibt die Drehgeste gesperrt.
    bool settling() const;

    // Steht seit dem GoToSleep an und wird von wake() geloescht. Damit haelt
    // die Policy diesen Zustand selbst, statt ihn im Controller zu spiegeln.
    bool wantsSleep() const;
};
```

Beide Ereignisse werden je Übergang **genau einmal** gemeldet und haben je einen
Verbraucher:

- **`GoToSleep`** — der Controller fährt die Hardware herunter (`radioOff()`,
  `setRate(Sleep)`, `enableWakeOnMotion()`). `main.cpp` fragt danach `wantsSleep()` ab und
  legt die Schleife schlafen.
- **`Settled`** — das Einschwingfenster ist vorbei: der Controller stellt
  `MADGWICK_BETA_FAST` wieder auf `cfg::MADGWICK_BETA` zurück. Ohne diesen Verbraucher
  bliebe der Filter dauerhaft auf dem hohen Beta und würde bei jeder Handbewegung von der
  Linearbeschleunigung mitgerissen.

---

## 4. Wo die Ausführung lebt

Die Zustandswechsel berühren Hardware und verteilen sich auf drei Stellen. Der
`AirMouseController` orchestriert, weil er beides sieht; `main.cpp` führt den Schlaf selbst
aus, weil ihm der Takt gehört.

### `ImuReader` — Abtastrate und Wake-Up

```cpp
enum class ImuRate : uint8_t { Active, Ready, Sleep };   // 208 / 52 / 26 Hz
void setRate(ImuRate r);
void enableWakeOnMotion();    // Wake-Up + Inactivity scharf, INT1 aktiv
void disableWakeOnMotion();
```

`setRate(Sleep)` schaltet zusätzlich das Gyroskop ab (`CTRL2_G` auf Power-Down) — es ist
der grössere Verbraucher der beiden Sensoren und wird zum Aufwecken nicht gebraucht.

**Die Schrittweite folgt der Rate.** `cfg::DT` ist heute eine Konstante; in BEREIT läuft
die Verarbeitung mit 52 Hz und braucht das passende `dt`, sonst rechnen Madgwick und alle
Glättungen mit einer falschen Zeitbasis. `AirMouseController::update(s, dt, now_us)` nimmt
`dt` bereits als Parameter entgegen — es ändert sich also nur, **welchen Wert `main.cpp`
übergibt**, und ebenso die Taktlänge, gegen die die Schleife wartet. An den Modulen ändert
sich nichts. **Der ML-Pfad bleibt davon unberührt:** der Klassifikator läuft nur im Zustand
AKTIV, und dort gilt weiterhin exakt `cfg::SAMPLE_INTERVAL_US`. Der `static_assert` in
`PinchClassifier.h` bleibt unverändert gültig.

### `MouseHID` — Funk abschalten

```cpp
void radioOff();   // Verbindung trennen, Advertising stoppen
void radioOn();    // Advertising wieder starten
```

Im USB-Zweig sind beide leer — dort gibt es nichts abzuschalten, und ein `#if` an der
Aufrufstelle wäre die schlechtere Lösung.

### `src/main.cpp` — der Schlaf selbst

`app.wantsSleep()` reicht dabei nur `SleepPolicy::wantsSleep()` durch — es entsteht kein
zweites Zustandsbit im Controller.

```cpp
if (app.wantsSleep()) {
    attachInterrupt(digitalPinToInterrupt(PIN_LSM6DS3TR_C_INT1), onMotion, RISING);
    suspendLoop();                       // hier bleibt die Task stehen
    detachInterrupt(digitalPinToInterrupt(PIN_LSM6DS3TR_C_INT1));
    app.onWake(millis());
    nextSample_us = micros() + cfg::SAMPLE_INTERVAL_US;   // Takt neu ausrichten
}
```

Die letzte Zeile ist nicht optional: nach dem Schlaf liegt `nextSample_us` beliebig weit
in der Vergangenheit. Ohne Neuausrichtung liefe die Schleife erst einige tausend
Overrun-Korrekturen ab, bevor sie wieder im Takt ist.

Die ISR tut nur eines: `resumeLoop()`. Alles Weitere geschieht in der Task.

---

## 5. Der Takt schläft: `delay()` statt Leerlauf

Statt sofort zurückzukehren wartet die Schleife aktiv:

```cpp
const int32_t restUs = (int32_t)(nextSample_us - micros());
if (restUs > 1500) delay((restUs - 1000) / 1000);   // FreeRTOS schlaeft
while ((int32_t)(nextSample_us - micros()) > 0) { }  // Rest genau abwarten
```

`delay()` ruft `vTaskDelay`; mit `configUSE_TICKLESS_IDLE` schläft der Kern für diese
Zeit tatsächlich. Die letzte Millisekunde wird bewusst nicht verschlafen: die
FreeRTOS-Auflösung beträgt 1 ms, und der Takt von 4785 µs muss auf wenige Mikrosekunden
genau bleiben. Die verbleibende Wartezeit ist mit unter einer Millisekunde kurz genug, um
sie abzuwarten.

**Diese eine Änderung ist der grösste Posten des ganzen Entwurfs.** Sie wirkt in AKTIV und
BEREIT, also in den Zuständen, in denen das Gerät tatsächlich benutzt wird — und sie
kostet nichts an Verhalten.

Der Overrun-Zähler bleibt die Kontrolle: schläft die Schleife zu lange, steigt `ovr`.

---

## 6. Wiedereinschwingen nach dem Aufwachen

Während des Schlafs bekommt der Madgwick-Filter keine Samples; seine Lageschätzung ist
beim Aufwachen veraltet. Die Ein/Aus-Drehgeste hängt genau an diesem Winkel — sie könnte
danebengreifen oder von allein auslösen.

Nach dem Aufwachen läuft Madgwick deshalb für `settleMs` (300 ms) mit stark erhöhtem Beta
(`MADGWICK_BETA_FAST = 0.5`), damit die Lage auf die Schwerkraft einrastet, und
`TwistToggle` bleibt in dieser Zeit gesperrt. Der Arm bewegt sich in diesem Moment ohnehin
— die Sperre ist unsichtbar.

Gesperrt wird über den bestehenden Weg: `TwistToggle::tick()` bekommt `level = false`
übergeben, was die laufende Ausdrehung ohnehin verwirft. Es braucht dafür keinen neuen
Mechanismus im Modul.

---

## 7. Regler, Peripherie, Funk

**DC/DC-Regler.** Der nRF52840 startet mit dem LDO; der DC/DC-Wandler spart bei Last bis
zu etwa 30 %. Der ursprüngliche Plan — `sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE)` mit
Rückfall auf den direkten Registerzugriff, solange keine SoftDevice läuft — war so nicht
haltbar: `Bluefruit.begin()` ist der einzige Aufruf, der die SoftDevice überhaupt startet,
und er läuft in `setup()` erst über `mouse.begin()`. Ein SVC-Aufruf an früherer Stelle in
`setup()` liefe also immer ohne resident laufende SoftDevice, und der voreingestellte
`SVC_Handler` des Kerns ist eine Endlosschleife — ein SVC ohne SoftDevice könnte das Gerät
im schlimmsten Fall aufhängen statt nur wirkungslos zu bleiben.

Gebaut wurde deshalb: der Block sitzt **nach** `mouse.begin()`, nach Übertragungsweg
getrennt statt mit Rückfall — `#if USE_BLE_HID` ruft `sd_power_dcdc_mode_set(...)`,
`#else` schreibt `NRF_POWER->DCDCEN = 1` direkt, weil dort nie eine SoftDevice läuft.
Beides steckt zusätzlich im bestehenden `#if !COLLECT_MODE`-Zweig, weil `mouse.begin()`
selbst im `COLLECT_MODE` nicht aufgerufen wird — ohne diese Schachtelung liefe der
SVC-Pfad dort in denselben Blockierfehler, unabhängig von `USE_BLE_HID`.

**Mikrofon.** `PIN_PDM_PWR` (P19) wird nie benutzt und bleibt aktiv abgeschaltet.

**Teleplot.** `DEBUG_TELEPLOT` kostet über USB-CDC reale Energie und bremst zusätzlich die
Schleife. Für den Akkubetrieb gehört es aus. Das steht bereits in `CLAUDE.md`, wird hier
aber zur Messbedingung: jede Stromangabe gilt mit `DEBUG_TELEPLOT false`.

**BLE-Parameter.** Hier liegt ein echter Zielkonflikt mit der Latenzarbeit der letzten
Runde: `Bluefruit.Periph.setConnInterval(6, 12)` fordert 7.5–15 ms an, was gut für die
Reaktionszeit und teuer für den Strom ist. Der Entwurf lässt das im Zustand AKTIV
**unverändert** — die Latenz ist die Kerneigenschaft des Geräts. In BEREIT wäre ein
längeres Intervall vertretbar, aber ein Wechsel des Verbindungsintervalls im laufenden
Betrieb ist eine Verhandlung mit der Gegenstelle, die auch scheitern kann. Das bleibt
**ausserhalb dieses Entwurfs** und wird als Messpunkt notiert; der grosse Gewinn kommt aus
dem Schlaf, in dem der Funk ganz aus ist.

**Sendeleistung.** `setTxPower(4)` ist das Maximum. Ein niedrigerer Wert spart beim Senden,
kostet Reichweite. Ebenfalls ein Messpunkt, keine Vorabentscheidung.

---

## 8. Compiler und Build

Ehrlich eingeordnet: **das ist der kleinste Posten.** Die Zahlen entstehen bei Schleife,
IMU-Rate und Funk, nicht beim Optimierer.

- **`-O2` bleibt.** Schneller Code heisst mehr Schlafzeit. `-Os` wäre hier
  kontraproduktiv: es könnte die Inferenz verlangsamen und damit die Wachzeit verlängern.
  Flash ist zu 18 % belegt, Platz ist kein Argument.
- **`-ffunction-sections -fdata-sections` mit `-Wl,--gc-sections`** entfernt ungenutzten
  Code aus dem Binary. Wird eingebaut, aber **nicht als Stromsparmassnahme ausgewiesen** —
  ein kleineres Binary verbraucht im Betrieb nicht weniger.
- **`-DNDEBUG`** deaktiviert `assert()` im Edge-Impulse-SDK.

Kein `-flto`: das SDK ist gross, generiert und nicht darauf getestet; ein Fehler dort wäre
schwer zuzuordnen und der Gewinn unbelegt.

---

## 9. `lib/Battery/` — die Ersparnis belegbar machen

Ohne Messung ist jede Ersparnis eine Behauptung. Die XIAO kann ihre eigene Akkuspannung
lesen; damit nimmt die Firmware ihre Entladekurve selbst auf, ohne Zusatzgerät.

```cpp
class Battery {
public:
    void  begin();                     // VBAT_ENABLE, ADC-Aufloesung, Referenz
    float update(uint32_t now_ms);     // gedrosselt gemessen, gemittelt
    float volts()   const;
    uint8_t percent() const;           // grobe Schaetzung aus der LiPo-Kennlinie
};
```

`VBAT_ENABLE` (P14) schaltet den Spannungsteiler zu; er wird nur für die Messung aktiviert
und danach wieder abgeschaltet, sonst zieht der Teiler dauerhaft Strom. Gemessen wird
selten (Vorschlag: alle 30 s) und über mehrere Wandlungen gemittelt.

**Der Teilerfaktor wird nicht aus dem Datenblatt übernommen, sondern kalibriert:** eine
bekannte Spannung mit dem Multimeter messen und gegen den ADC-Rohwert halten. Der
Board-spezifische Faktor ist in der Literatur uneinheitlich angegeben, und eine
Kalibriermessung ist ohnehin die sauberere Grundlage für eine Laufzeitangabe.

Neuer Teleplot-Kanal `vbat`.

---

## 10. Startwerte

Begründete Ausgangspunkte, keine Ergebnisse.

| Konstante | Startwert | Begründung |
|---|---|---|
| `SleepTuning::sleepAfter` | 60 000 ms | vom Nutzer gewählt |
| `SleepTuning::stillDps` | 20 °/s | über dem Rauschen, unter jeder gewollten Bewegung |
| `SleepTuning::settleMs` | 300 ms | Einschwingen von Madgwick mit erhöhtem Beta |
| `MADGWICK_BETA_FAST` | 0.5 | nur im Einschwingfenster |
| IMU-Rate BEREIT | 52 Hz | die Drehgeste dauert ~1 s, das reicht weit |
| IMU-Rate SCHLAF | 26 Hz Low-Power | nur für die Wake-Up-Funktion |
| `WAKE_UP_THS` | 2 (von 63) | bei ±4 g entspricht ein Schritt 4 g/64 ≈ 62 mg, also rund 125 mg Schwelle |
| `WAKE_UP_DUR` | 0 | sofort auslösen; die Schwelle allein filtert |
| `BATTERY_INTERVAL_MS` | 30 000 | selten genug, um selbst nichts zu kosten |

---

## 11. Tests

**Auf dem PC:**

| Test | Deckt ab |
|---|---|
| `test/test_sleep_policy.cpp` *(neu)* | schläft nach `sleepAfter` Ruhe; schläft **nie**, solange `mouseOn`; Bewegung setzt den Zeitgeber zurück; `wake()` startet das Einschwingfenster; `settling()` endet nach `settleMs`; `GoToSleep` und `Settled` kommen je einmal |

Die übrigen sechs PC-Tests müssen unverändert grün bleiben.

**Am Gerät** — und hier liegt der Ertrag für die Arbeit:

1. `ovr` nach dem Umbau der Schleife: der Takt muss weiterhin gehalten werden. Steigt der
   Zähler, schläft die Schleife zu lange.
2. Aufwachen: Arm ablegen, 60 s warten, Arm heben. Wacht es auf? Wie lange bis BLE wieder
   steht? Funktioniert die Drehgeste unmittelbar danach, oder schlägt das Einschwingen
   durch (`rtwist` beobachten)?
3. `WAKE_UP_THS` einstellen: Armheben weckt, Klopfen auf den Tisch nicht.
4. Falsches Einschlafen: Fällt das Gerät im Gebrauch in den Schlaf? Der Zeitgeber darf in
   AKTIV nie laufen.
5. **Entladekurve** über `vbat`, je einmal für den alten und den neuen Stand, unter
   gleichem Nutzungsmuster. Das ist die belastbare Zahl — Laufzeit vorher gegen nachher.
6. Einzelmessungen je Zustand mit einem Multimeter in Serie, gegen die geschätzten Werte
   aus Abschnitt 2 gehalten.

---

## 12. Nicht im Umfang

- **System OFF.** Bewusst verworfen: der Reset kostet den gelernten Gyro-Nullpunkt und die
  BLE-Verbindung, und im Schlaf könnte die Drehgeste gar nicht erkannt werden. Der Gewinn
  gegenüber dem gewählten Ruhezustand ist bei wöchentlichem Laden nicht spürbar.
- **BLE-Verbindungsintervall zur Laufzeit ändern.** Zielkonflikt mit der Latenz, und eine
  Verhandlung, die scheitern kann. Bleibt Messpunkt.
- **Sendeleistung senken.** Messpunkt, keine Vorabentscheidung.
- **`-flto`.** Unbelegter Gewinn bei generiertem SDK-Code.
- **Das BLE-Kopplungsproblem.** Eigenständig und unabhängig von diesem Entwurf.
