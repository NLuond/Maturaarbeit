# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Projekt

Firmware einer Air Mouse (Maturaarbeit) fuer ein **Seeed XIAO nRF52840 Sense** (LSM6DS3 IMU
on-board). Die Hand steuert per Lage/Drehrate den Cursor, ein Pinch (Daumen-Zeigefinger)
loest den Klick aus, erkannt von einem Edge-Impulse-Modell. Ausgabe als BLE-HID-Maus.

## Build / Flash

PlatformIO CLI liegt nicht im PATH — immer ueber den vollen Pfad aufrufen:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run                # bauen
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -t upload      # flashen
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" device monitor -b 115200
```

Der Build ist wegen des Edge-Impulse-SDK langsam und **sehr** gespraechig; Ausgabe kuerzen
(`2>&1 | Select-Object -Last 20`). Interessant ist nur die RAM/Flash-Zeile und `SUCCESS`.

Zustandsautomat, Winkel-Ableitung und die anderen hardwarefreien Module werden auf dem PC
getestet, nicht auf dem Chip (`g++` liegt unter `C:\Strawberry\c\bin`):

```powershell
g++ -std=c++14 -Wall -Wextra -I lib/AirMouseState -o "$env:TEMP\fsm.exe" test/test_state_machine.cpp
& "$env:TEMP\fsm.exe"

g++ -std=c++14 -Wall -Wextra -I lib/ArmOrientation -o "$env:TEMP\arm.exe" test/test_arm_orientation.cpp
& "$env:TEMP\arm.exe"

g++ -std=c++14 -Wall -Wextra -I lib/TwistToggle -o "$env:TEMP\twist.exe" test/test_twist_toggle.cpp
& "$env:TEMP\twist.exe"

g++ -std=c++14 -Wall -Wextra -I lib/TwistGuard -o "$env:TEMP\guard.exe" test/test_twist_guard.cpp
& "$env:TEMP\guard.exe"

g++ -std=c++14 -Wall -Wextra -I lib/Filters -o "$env:TEMP\euro.exe" test/test_one_euro.cpp
& "$env:TEMP\euro.exe"

g++ -std=c++14 -Wall -Wextra -I lib/ImuReader -I lib/PinchFeatures -o "$env:TEMP\feat.exe" test/test_pinch_features.cpp
& "$env:TEMP\feat.exe"

g++ -std=c++14 -Wall -Wextra -I lib/SleepPolicy -o "$env:TEMP\sleep.exe" test/test_sleep_policy.cpp
& "$env:TEMP\sleep.exe"
```

Erwartet fuer alle sieben: `0 Fehler`, Exit 0 — feste Pruefzahlen stehen bewusst nicht mehr
hier, sie liefen bei jeder Umschreibung der Tests auseinander.

Das laeuft in Sekunden und ist der schnellste Weg, eine Aenderung an einem dieser Module
zu pruefen — vor dem Firmware-Build, nicht danach. Es gibt keine `[env:native]`-Sektion,
ein `pio test` wuerde also aufs Board wollen. Alles andere wird weiterhin ueber
Kompilieren + Messen am Geraet (Teleplot) verifiziert.

Alle sieben Testdateien haengen daran, dass der jeweilige Header hardwarefrei bleibt:
`ArmOrientation.h` inkludiert bewusst weder `Arduino.h` noch `config.h`, ebenso
`TwistToggle.h`, `TwistGuard.h`, `OneEuro.h`, `PinchFeatures.h` und `SleepPolicy.h`.
Parameter, die sonst aus `cfg::` kaemen, stecken deshalb in eigenen Tuning-Structs
(`TwistTuning`, `TwistGuardTuning`, `PointerTuning`, `SleepTuning`); das Vorzeichen
`cfg::ELEV_SIGN` wird erst im Controller angewandt, nicht im `ArmOrientation`-Modul.

Beim Aendern von Schaltern in `config.h` **nie** parallel zu einem laufenden Build: zwei
gleichzeitige `pio run` auf dasselbe `.pio/build` brechen mit
`Fatal error: can't create ... .o: No such file or directory` ab — das sieht nach einem
Codefehler aus, ist aber nur die Kollision.

## Compile-Time-Schalter (`include/config.h`)

Es gibt keine Laufzeit-Konfiguration. Alle Betriebsarten sind `#define`s ganz oben in
`include/config.h`; umstellen heisst neu bauen und flashen.

| Schalter | Wirkung |
|---|---|
| `COLLECT_MODE` | `main.cpp` sendet statt HID nur CSV (`env,gyro/100,lax,lay,laz`) fuer Edge Impulse. HID/Controller werden gar nicht erst initialisiert. |
| `DEBUG_TELEPLOT` | Teleplot-Kanaele (`>name:wert`) aus `AirMouseController::debug()`. Kostet Serial-Bandbreite und bremst die Schleife — fuer echte Nutzungstests aus. |
| `USE_ML_PINCH` | ML-Klassifikator gegen reine Schwellwert-Erkennung (`envGate`). |
| `USE_BLE_HID` | BLE (bluefruit) gegen USB-HID (TinyUSB) in `MouseHID.h`. |
| `USE_POSE_MODE` | Haltungserkennung aus → `PoseDetector` meldet dauerhaft `Point`, zum Eingrenzen von Cursor-Problemen. |
| `USE_ONE_EURO` | 1-Euro-Filter gegen festen Tiefpass (`SMOOTH_TAU`) — das ist der A/B-Vergleich fuer die Evaluation, nicht bloss eine Aufraeum-Option. |
| `USE_ROLL_COMP` | Rechnet die Handverdrehung aus der Cursorbewegung heraus. Zweiter A/B-Vergleich; bei `twist = 0` sind beide Pfade identisch, der Unterschied zeigt sich erst bei verdrehter Hand. |

Alle Zahlenwerte (Schwellen, Gains, Zeitkonstanten) sind `constexpr` in `namespace cfg`.
Magic Numbers gehoeren dorthin, nicht in die Module.

Filter und Kennlinien nehmen ihre Parameter aber ueber den Konstruktor entgegen
(`HighPass`, `LowPass`, `MadgwickAHRS`, `OneEuroFilter`, `OrientationPointer` via
`PointerTuning`), mit `cfg::` nur als Vorgabe. Damit laufen zwei Einstellungen im selben
Binary gegeneinander — fuer die A/B-Messungen aus `TODO.md` ohne Reflash pro Variante:

```cpp
PointerTuning t;  t.accelK = 0.f;
OrientationPointer ohneAccel(t);
```

## Betriebszustaende

Zusaetzlich zur Ein/Aus-Achse in `AirMouseState` gibt es drei Hardware-Zustaende, die
`SleepPolicy` und der Controller gemeinsam verwalten. main.cpp richtet danach den
Schleifentakt aus, der Controller die IMU-Rate:

| Zustand | Bedingung | IMU-Rate | Funk | Schleifentakt |
|---|---|---|---|---|
| AKTIV | Maus eingeschaltet (`fsm_.on()`) | 208 Hz | an | `cfg::SAMPLE_INTERVAL_US` (209 Hz) |
| BEREIT | ausgeschaltet, aber innerhalb `SleepTuning::sleepAfter` (60 s) bewegt | 52 Hz | an | `cfg::READY_INTERVAL_US` (52 Hz) |
| SCHLAF | 60 s ohne Bewegung (`gyroSum < SleepTuning::stillDps`) | nur Beschleunigungssensor, Wake-on-Motion auf INT1 | aus | `loop()` suspendiert (`suspendLoop()`), IMU weckt per Interrupt |

Eingeschlafen wird nur aus BEREIT, nie aus AKTIV — sonst verschwaende die Maus mitten im
Gebrauch, waehrend man den Cursor nur ruhig auf einem Ziel haelt. Es gibt bewusst kein
System OFF: das RAM bleibt erhalten und damit der ueber Minuten gelernte Gyro-Nullpunkt
und die BLE-Verbindung, ein Reset wuerde beides kosten.

## Architektur

`src/main.cpp` ist bewusst duenn: fester Takt + IMU lesen, dann entweder CSV ausgeben
(`COLLECT_MODE`) oder `AirMouseController::update()` aufrufen. Die Schleife arbeitet mit
**fester Schrittweite**, nicht mit der gemessenen Zeitdifferenz — Filter und ML-Fenster
brauchen eine konstante Abtastrate; nach einer Stockung wird neu ausgerichtet statt
nachgeholt. `cfg::DT` ist dabei nicht mehr die einzige Schrittweite: sie gilt nur in
AKTIV, in BEREIT laeuft die Schleife mit `cfg::READY_DT` (52 Hz statt 209 Hz) — der
ML-Pfad braucht die volle Rate ohnehin nur, solange die Maus eingeschaltet ist, und laeuft
ausschliesslich in AKTIV.

Jedes Modul ist eine header-only Klasse in einem eigenen `lib/<Name>/` (kein `.cpp`),
per `-I` in `platformio.ini` eingebunden. Neues Modul → Ordner anlegen **und** dort einen
`-I`-Eintrag ergaenzen.

Die Policy-Module (`AirMouseState`, `ArmOrientation`, `PinchDetector`, `TwistToggle`,
`TwistGuard`, `PoseDetector`, `ScrollJoystick`, `OrientationPointer`, `SleepPolicy`,
`Filters/`) kennen weder Hardware noch das EI-SDK. Beides ist auf `ImuReader`, `MouseHID`,
`Haptic`, `Battery` und `PinchClassifier` beschraenkt. Diese Richtung beim Erweitern
beibehalten — nichts aus `lib/ei-model` oder `bluefruit` gehoert in ein Policy-Modul.

**`AirMouseState` (`lib/AirMouseState/`) haelt den gesamten Zustand** in zwei Achsen
(`Power` / `Pose`) und fuehrt selbst nichts aus: jedes Ereignis (`onPower`, `onPose`,
`onTwistHeld`, `onPinch`) liefert ein `Actions`-Struct zurueck, das der Controller in
`apply()` — der einzigen Stelle mit Seiteneffekten — ausfuehrt. Neue Zustandslogik
gehoert hierhin und braucht einen Test, kein zusaetzliches Flag im Controller.

**Die Haltung waehlt, was ein Pinch bedeutet:** `Point` → Linksklick, `Idle` (nur das
Waagrecht-Gate, keine Verdrehungs-Bandbreite mehr) → nichts, `Turned` → Rechtsklick bei
ruhig gehaltener Neigung. Der Pinch selbst ist zustandslos, er veraendert im Automaten
nichts — auch das ist getestet.

**Es gibt bewusst keine `Grab`-Achse und kein Ziehen.** Zwei Anlaeufe dazu sind wieder
ausgebaut worden: der Doppel-Pinch brauchte ein Wartefenster, das auf *jedem* gewoehnlichen
Klick lag; Pinch-plus-Abdrehen kollidierte mit dem Rechtsklick auf derselben Geste.
Solange nichts eine Taste gedrueckt haelt, waere ein Zustand dafuer nur Ballast — und die
Invariante „nie mit gedrueckter Taste abschalten" waere leer. Kommt das Ziehen spaeter
ueber eine 180-Grad-Drehung zurueck, gehoert dazu wieder eine eigene Achse mit dieser
Invariante und einem Test dafuer.

`AirMouseController` verdrahtet nur noch: Ereignisse einsammeln → FSM fragen → `apply()`.
Ablauf pro Tick:

1. `TwistToggle` — die Ein/Aus-Geste ersetzt das fruehere Schuetteln: Unterarm um rund
   90 Grad abdrehen und innerhalb einer Sekunde zurueck schaltet ein oder aus
   (`TwistEvent::Toggle`), laenger gehalten wird daraus der Scroll-Modus
   (`TwistEvent::Held`). Beim Einschalten setzt `Actions::resetPose` die Haltung auf
   `Point` zurueck — der Nullpunkt der Verdrehung ist dagegen fest
   (`cfg::TWIST_NEUTRAL_DEG`) und wird *nicht* beim Einschalten kalibriert: das geschah
   frueher direkt nach dem Schuetteln, wo die Lageschaetzung am staerksten gestoert war,
   und ergab jedes Mal einen anderen Bezug.
2. `MadgwickAHRS` — Lage aus Gyro+Accel; liefert `upX/upY/upZ`, die Richtung von "oben"
   im Koerperkoordinatensystem. Bewusst **kein** `rollDeg()`/`pitchDeg()` mehr, siehe
   unten.
   `ArmOrientation` macht daraus die beiden benannten Winkel: `twistDeg` (Verdrehung um
   die Unterarmachse) und `elevDeg` (Neigung des Unterarms aus der Waagerechten). Der
   Controller rechnet sie einmal pro Tick und verteilt sie — kein Modul holt sich seinen
   Winkel mehr selbst.
3. `PoseDetector` — das **Waagrecht-Gate** (`LEVEL_MAX_DEG`, Hysterese `LEVEL_HYST_DEG`)
   entscheidet zuerst: ausserhalb gibt es nur `Idle`, egal wie die Hand steht. Erst
   innerhalb des Gates waehlt die geglaettete Verdrehung gegen `TWIST_NEUTRAL_DEG`
   zwischen `Point` und `Turned` — `Idle` ist also keine Zone der Verdrehung mehr,
   sondern allein das Ergebnis des Gates. Geglaettet (`MODE_TAU`) + Haltezeit
   (`MODE_DWELL_MS`) + Hysterese (auch auf dem Gate), sonst springt die Haltung mitten in
   einer schnellen Bewegung um. Zusaetzlich wird waehrend heftiger Bewegung
   (`POSE_STILL_DPS`, plus `POSE_CALM_MS` Ruhezeit danach) gar nicht erst entschieden —
   ohne das reisst schon eine zuegige 90-Grad-Drehung die Erkennung durch `Idle` bis
   `Turned`, samt Haptik und Zeiger-Reset. Die Winkel laufen dabei weiter mit, nur die
   Entscheidung ruht.
4. Klick-Pfad: `VibrationEnvelope` (Hochpass 30 Hz → Betrag → Tiefpass 15 Hz) → bi-level
   Gate (`ENV_ON`/`ENV_OFF`) in `PinchDetector` → bei offenem Gate ML-Inferenz in
   `PinchClassifier` → direkt `fsm_.onPinch()`, ohne Zwischenstufe; welche Taste daraus
   wird, entscheidet dort die Haltung. Eine Erschuetterung ueber `ENV_ON` verbraucht
   dabei auch eine laufende Ausdrehung (`twistToggle_.cancel()`) — sie war ein Pinch und
   keine Schaltgeste, sonst wuerde ein verpasster Pinch beim Zurueckdrehen die Maus
   abschalten statt rechtszuklicken.
   `PinchDetector::tick()` bekommt den Klassifikator als Callable uebergeben und ruft
   ihn per Kurzschlussauswertung nur bei offenem Gate — deshalb kennt die Policy das
   EI-SDK nicht und die Inferenz laeuft nicht in jedem Takt.
5. `OrientationPointer` — `arm::rates()` zerlegt die Drehrate in Gieren und Nicken
   *bezogen auf den Raum* (Roll-Kompensation, `USE_ROLL_COMP`) → Deadzone → 1-Euro-Filter
   → Beschleunigung → Pixel; im Controller akkumuliert und alle `MOVE_INTERVAL_US` als
   int8 verschickt. Ohne die Kompensation laeuft der Cursor bei verdrehter Hand schraeg;
   bei `twist = 0` sind beide Pfade identisch. `TwistGuard` blendet die Bewegung
   waehrend einer Unterarmdrehung aus, damit die Drehgeste sich zielen laesst, ohne dass
   der Cursor dabei querlaeuft. Seine Rate kommt aus der Lageschaetzung
   (Differenz von `arm::twistDeg` zum Vortakt) und nicht aus `gy`: die Unterarmachse
   faellt nicht exakt mit einer Platinenachse zusammen, eine Verdrehung leckt deshalb
   auch in `gx` und `gz` und ein einzelner Gyro-Kanal wuerde sie nicht vollstaendig
   erfassen.
6. `ScrollJoystick` — im Scroll-Modus zaehlt die *gehaltene* Armneigung relativ zum
   Eintrittswinkel (Positionssignal, driftet nicht), nicht die Drehrate.
7. `SleepPolicy` — am Ende jedes Takts befragt, mit `fsm_.on()` und `gyroSum`. Liefert sie
   `GoToSleep`, bereitet `prepareSleep()` nur Funk und IMU vor (`radioOff()`, IMU auf
   Sleep-Rate + Wake-on-Motion); das eigentliche Schlafenlegen (`suspendLoop()`) und
   Aufwecken fuehrt `main.cpp` aus, weil dort der Schleifentakt haengt, der danach neu
   ausgerichtet werden muss.

Wichtige Eigenheiten, die man sonst kaputt macht:

- **Roll und Pitch heissen hier nicht, was sie bei Madgwick heissen.** Ausmultipliziert
  ist die Standardformel `rollDeg() = atan2(uy, uz)` und `pitchDeg() = asin(-ux)`. Bei
  dieser Einbaulage (flach `az = +1`, um 90 Grad verdreht `ax = +1`) ist das erste die
  Armneigung und das zweite die Handverdrehung — also genau vertauscht gegenueber dem,
  was die Namen nahelegen. Genau daran hing der Fehler, dass `PoseDetector` die Neigung
  statt der Verdrehung bekam und die Y-Ausblendung im Zeiger schon bei gerader Hand
  zuschlug. Deshalb gibt es die beiden Methoden nicht mehr: Winkel werden ausschliesslich
  ueber `arm::twistDeg()` / `arm::elevDeg()` benannt.

- **`MouseHID::move()` gibt zurueck, ob das Paket angenommen wurde.** Der Rest darf erst
  danach abgezogen werden, sonst geht Bewegung bei voller BLE-Warteschlange verloren.
- **Der Klassifikator wird in jeder Haltung gefuettert, aber nur im eingeschalteten
  Zustand** (`handlePinch` haengt an `fsm_.on()`). Ueber die Haltungen hinweg zu fuettern
  haelt das Fenster beim Zurueckdrehen aktuell; im Off-Zustand waeren es Inferenzen,
  deren Ergebnis ohnehin verworfen wird.
- **Der Pinch wird sofort gemeldet, ohne jedes Wartefenster.** Jede Gestenerkennung, die
  auf ein zweites Ereignis wartet, legt ihre Fensterlaenge als Verzoegerung auf *jeden*
  gewoehnlichen Klick. Deshalb unterscheiden sich Links- und Rechtsklick ueber die
  gehaltene Haltung und nicht ueber die Anzahl der Pinches.
- **Gyro-Bias wird nur im Stillstand nachgefuehrt** (`ImuReader`) — ein Tiefpass kann
  einen Offset nicht entfernen; ohne die Korrektur wandert der Cursor von allein.

## Edge-Impulse-Modell (`lib/ei-model/`)

Generierter Code aus Studio-Projekt 1036761 — **nicht von Hand editieren**, beim Update
den ganzen Ordner ersetzen. Aktuelles Modell: 3 Klassen (`idle`, `negative`, `pinch`),
Sensor Fusion ueber 5 Kanaele, Fenster 41 Samples @ 209 Hz (~196 ms), int8.

Die Abtastrate ist die kritische Kopplung: `PinchClassifier.h` haelt per `static_assert`
`cfg::SAMPLE_INTERVAL_US` (4785 µs) mit `EI_CLASSIFIER_INTERVAL_MS` zusammen, ebenso die
Kanalzahl mit `EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME`.

Die Kanal-Reihenfolge steht **nur** in `feat::pack()` (`lib/PinchFeatures/`). Sowohl der
`COLLECT_MODE` in `main.cpp` als auch das Inferenz-Fenster in `PinchClassifier` gehen
durch diese Funktion. Eine Aenderung dort macht jedes bisher trainierte Modell ungueltig
— und sie ist der einzige Ort, an dem das passieren kann.

Die Kanaele 2–4 fuehren die **lineare** Beschleunigung (`lax/lay/laz`, Erdbeschleunigung
in `ImuReader` per Tiefpass herausgerechnet), nicht die rohen Achsen. Damit ist die
Handhaltung fuer das Modell unsichtbar — der Pinch sieht in Zeige- und in abgedrehter
Haltung dasselbe Signal, und ein einziger Datensatz deckt beide Haltungen ab.

## Konventionen

- Kommentare auf **Deutsch, ASCII ohne Umlaute** (`waehrend`, `Verzoegerung`). Sie
  begruenden das *Warum* einer Entscheidung, oft mit Literaturverweis
  (z.B. Casiez et al. 2012 fuer den 1-Euro-Filter) — dieser Stil ist Teil der Arbeit
  und beim Aendern von Code beizubehalten.
- Kein `new`/`malloc`, keine `String`, keine dynamischen Container im Hot Path; feste
  Puffer und `float`.
- `TODO.md` ist das laufende Arbeitsjournal (offene Messungen, Einstellwerte, Entscheide
  fuer die schriftliche Arbeit). Es ist stellenweise aelter als der Code — z.B. nennt es
  `USE_TWIST_GUARD`, das es in `config.h` nicht mehr gibt. Immer gegen den Code pruefen,
  bevor daraus etwas uebernommen wird.
