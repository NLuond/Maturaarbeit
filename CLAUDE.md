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
getestet, nicht auf dem Chip. Alle zehn auf einmal:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" test -e native
```

Erwartet: `10 test cases: 10 succeeded`. Das laeuft in gut zehn Sekunden und ist der
schnellste Weg, eine Aenderung an einem dieser Module zu pruefen — vor dem Firmware-Build,
nicht danach.

Einzeln geht es auch ohne PlatformIO (`g++` liegt unter `C:\Strawberry\c\bin`); die
passende Zeile steht im Kopf jeder Testdatei, z.B.:

```powershell
g++ -std=c++14 -Wall -Wextra -I lib/AirMouseState -o "$env:TEMP\fsm.exe" test/test_state_machine/test_state_machine.cpp
& "$env:TEMP\fsm.exe"
```

Erwartet: `0 Fehler`, Exit 0 — feste Pruefzahlen stehen bewusst nicht hier, sie liefen bei
jeder Umschreibung der Tests auseinander.

Die Tests benutzen **keinen Testrahmen**: jede Datei ist ein eigenstaendiges Programm mit
eigenem `main()`, das seine Pruefungen zaehlt und ueber den Exit-Code meldet, ob es
durchgelaufen ist. Genau deshalb funktioniert der g++-Einzeiler. Fuer `pio test` uebersetzt
`test/test_custom_runner.py` (`test_framework = custom`) die Ausgabe in PlatformIO-
Testfaelle; das Format `N Pruefungen, M Fehler` und `FEHLER Zeile N: ...` ist damit Teil
der Schnittstelle und darf nicht beilaeufig geaendert werden.

Alles andere wird weiterhin ueber Kompilieren + Messen am Geraet (Teleplot) verifiziert.

Alle zehn Testdateien haengen daran, dass der jeweilige Header hardwarefrei bleibt — weder
`Arduino.h` noch `config.h`: `AirMouseState.h`, `ArmOrientation.h`, `TwistToggle.h`,
`TwistGuard.h`, `SleepPolicy.h`, `PoseDetector.h`, `PinchDetector.h`, `ScrollJoystick.h`,
`OneEuro.h`, `PinchFeatures.h`/`ImuSample.h` und `MadgwickAHRS.h`. Die `-I`-Liste in
`[env:native]` bricht den Build, wenn eines davon abdriftet — das ist Absicht.

Parameter, die sonst aus `cfg::` kaemen, stecken deshalb in eigenen Tuning-Structs:
`TwistTuning`, `TwistGuardTuning`, `SleepTuning`, `PoseTuning`, `PinchTuning`,
`ScrollTuning` und `PointerTuning`. **Ein Block von `static_assert`s am Kopf von
`AirMouseController.h` haelt jedes dieser Felder mit seinem `cfg::`-Gegenstueck zusammen —
bei jedem neuen Tuning-Feld gehoert er erweitert**, sonst driften zwei Wahrheiten
lautlos auseinander. In `config.h` sind die betroffenen Gruppen mit `[auch in <X>Tuning]`
markiert.

Zwei Sonderfaelle: das Vorzeichen `cfg::ELEV_SIGN` wird erst im Controller angewandt, nicht
im `ArmOrientation`-Modul; und `USE_POSE_MODE` reist als Feld `PoseTuning::classify` in den
Konstruktor von `PoseDetector` (Compile-Wert, keine Laufzeit-Konfiguration).

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

`config.h` ist nach Themen gegliedert, in der Reihenfolge, in der ein Messwert sie
durchlaeuft: Betriebsarten → Debug → Sensor → Takt/Betriebszustaende → Lage → Handhaltung
→ Zeigen → Klick → Scrollen → Haptik → Akku. **Jede Konstante traegt Einheit und
Begruendung** — die Datei ist als Spezifikation gedacht, nicht als Werteliste. Neue Werte
in die passende Gruppe einsortieren und im selben Stil kommentieren; Gruppen, die ein
Tuning-Struct spiegeln, sind mit `[auch in <X>Tuning]` ueberschrieben.

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
| BEREIT | ausgeschaltet, aber innerhalb `SleepTuning::sleepAfterMs` (60 s) bewegt | 52 Hz | an | `cfg::READY_INTERVAL_US` (52 Hz) |
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
per `-I` in `platformio.ini` eingebunden. Neues Modul → Ordner anlegen **und** einen
`-I`-Eintrag ergaenzen: in `[env:xiaoblesense]` immer, in `[env:native]` zusaetzlich, wenn
das Modul hardwarefrei ist und einen PC-Test bekommt.

Die Policy-Module (`AirMouseState`, `ArmOrientation`, `PinchDetector`, `TwistToggle`,
`TwistGuard`, `PoseDetector`, `ScrollJoystick`, `OrientationPointer`, `SleepPolicy`,
`Filters/`) kennen weder Hardware noch das EI-SDK. Beides ist auf `ImuReader`, `MouseHID`,
`Haptic`, `Battery` und `PinchClassifier` beschraenkt. Diese Richtung beim Erweitern
beibehalten — nichts aus `lib/ei-model` oder `bluefruit` gehoert in ein Policy-Modul.

Streng hardwarefrei (weder `Arduino.h` noch `config.h`) sind davon alle ausser
`OrientationPointer` und `Filters/LowPass`+`HighPass`; jene beiden enthalten keine
Entscheidungslogik, sondern rechnen nur.

**`AirMouseState` (`lib/AirMouseState/`) haelt den gesamten Zustand** in zwei Achsen
(`Power` / `Pose`) und fuehrt selbst nichts aus: jedes Ereignis (`onPower`, `onPose`,
`onTwistHeld`, `onPinch`) liefert ein `Actions`-Struct zurueck, das der Controller in
`apply()` — der einzigen Stelle mit Seiteneffekten — ausfuehrt. Neue Zustandslogik
gehoert hierhin und braucht einen Test, kein zusaetzliches Flag im Controller.

**Die Haltung waehlt, was ein Pinch bedeutet:** `Point` → linke Taste runter (daraus wird
Klick oder Ziehen, siehe unten), `Idle` (nur das Waagrecht-Gate, keine Verdrehungs-
Bandbreite mehr) → nichts, `Turned` → Rechtsklick bei ruhig gehaltener Neigung. Power und
Pose laesst der Pinch unberuehrt; die einzige Achse, die er bewegt, ist `Grab` — auch das
ist getestet.

`Idle` ist **keine Zone der Verdrehung**, sondern ausschliesslich das Ergebnis des
Neigungs-Gates. Der Scroll-Joystick kommt **nicht** mit der Haltung `Turned`, sondern
erst ueber `onTwistHeld()` nach einer Sekunde gehaltener Ausdrehung — sonst wuerde jede
Ein/Aus-Geste nebenbei ein Stueck weit scrollen. Beides stand frueher falsch im
Klassenkommentar von `PoseDetector` und ist jetzt durch `test_pose_detector` festgenagelt.

**Die dritte Achse `Grab` (`Off` / `Pending` / `On`) traegt Klick und Ziehen gemeinsam.**
Es gibt **keinen** `Actions::click` mehr: der Pinch drueckt die Taste sofort
(`pressLeft`, `Grab::Pending`). Was daraus wird, entscheidet die **Bewegung**:

- Cursor bewegt sich ueber `cfg::DRAG_MOVE_PX` → `onDragMove()`, `Grab::On`, Ziehen
- Cursor bleibt stehen, `cfg::DRAG_WINDOW_MS` laeuft ab → `onClickWindow()`, Taste hoch,
  das war ein gewoehnlicher Klick
- Pinch waehrend `Grab::On` → Taste hoch, fallenlassen

Dasselbe Kriterium trennt auf jedem Touchscreen Tippen von Wischen. Der Druck kommt ohne
jede Verzoegerung; nur das Loslassen wartet.

**Warum nicht Apples „pinch and move" mit Loslassen als Ende:** ein *gehaltener* Pinch ist
mit einer IMU grundsaetzlich unsichtbar (Zustand, keine Beschleunigung) — Doublepoint
verbaut dafuer einen optischen Sensor und liest die Sehnen. Der Loese-*Impuls* waere ein
Ereignis und damit denkbar; am Geraet gemessen ist er aber nur teilweise vorhanden, bei
kurzem Pinch gar nicht, und kaum ueber dem Rauschen. Deshalb endet das Ziehen mit einem
bewussten zweiten Pinch. Ein frueherer Entwurf, der den Doppel-Pinch zum *Starten*
benutzte, ist daran gescheitert, dass sein Zeitband (200–350 ms) in der Praxis nicht zu
treffen war.

**Waehrend des Ziehens wird der Pinch allein an der Huellkurve gemessen**
(`PinchDetector::tick(..., relaxed)`): der Pinch zum Fallenlassen faellt per Definition in
eine Armbewegung, und der Gyro-Guard verwuerfe ihn sonst genau dann, wenn er gebraucht
wird.

**Die Vibration kommt sofort beim Pinch**, nicht erst bei der Entscheidung — ein um das
ganze Fenster verzoegerter Puls fuehlt sich an, als gaebe es gar keinen. Den zweiten Pinch
kann er nicht vortaeuschen: er dauert 40 ms, gezaehlt wird erst ab `DEBOUNCE_MS`.

Ein Ausloeser ueber einen *tieferen* Scheitelwinkel der Ausdrehung ist erprobt und wieder
ausgebaut worden: er lag auf derselben Achse wie Ein/Aus, und eine etwas zu weit geratene
Schaltgeste wurde dadurch stillschweigend zum Ziehen — Ein/Aus verlor messbar an
Zuverlaessigkeit. Der aeltere Anlauf Pinch-plus-Abdrehen kollidierte mit dem Rechtsklick
auf derselben Geste.

Der Preis der Achse ist die Invariante **„nie mit gedrueckter Taste abschalten oder die
Zeige-Haltung verlassen"** — sie gilt fuer `holding()`, nicht nur `dragging()`: auch das
unentschiedene `Pending` haelt die Taste koerperlich unten. `onPower()` und `onPose()`
geben sie von sich aus frei, und `AirMouseState::dropGrab()` ist die einzige Stelle, an
der das geschieht. Dazu kommen im Controller (`runDrag()`) eine Zwangsfreigabe nach
`cfg::DRAG_MAX_MS` und ein Erinnerungsimpuls alle `cfg::DRAG_REMIND_MS`, damit ein
laufendes Ziehen nie stillschweigend aktiv ist. Jede laengere Vibration sperrt
anschliessend kurz die Klickerkennung (`pinch_.holdOff()`) — sonst laese sie die eigene
Erschuetterung als Pinch.

**Ein/Aus brummt lang** (`Actions::hapticLong`, `cfg::HAPTIC_LONG_MS`), alles andere kurz:
1 = Klick oder Haltungswechsel, 2 = Rechtsklick, 3 = Ziehen an oder aus. Ein/Aus ist das
einzige Ereignis, nach dem gar nichts mehr geht, und ein langer Puls hebt sich sauberer ab
als eine vierte Anzahl kurzer, die ohnehin zu einem Brummen verschmelzen wuerde.

`AirMouseController` verdrahtet nur noch: Ereignisse einsammeln → FSM fragen → `apply()`.
Ablauf pro Tick:

1. `TwistToggle` — die Ein/Aus-Geste ersetzt das fruehere Schuetteln: Unterarm um rund
   90 Grad abdrehen und innerhalb einer Sekunde zurueck schaltet ein oder aus
   (`TwistEvent::Toggle`), laenger gehalten wird daraus der Scroll-Modus
   (`TwistEvent::Held`). Wie weit ausgedreht wird, spielt darueber hinaus keine Rolle —
   ein tieferer Scheitelwinkel als zweite Bedeutung ist erprobt und wieder ausgebaut.
   Drei Bremsen koennen die Geste verwerfen, und alle drei taten das frueher **lautlos**:
   die Abbruchschwelle (`cfg::TWIST_CANCEL_ENV` — eigene, hoehere Schwelle als das
   Klick-Gate, weil die Drehung selbst die Huellkurve ueber `ENV_ON` hebt), das
   Waagrecht-Gate (`LEVEL_MAX_DEG`) und das Rueckkehrfenster (`maxMs`). Jede zaehlt
   inzwischen mit (`nTwCan`, `nTwLvl`, `nTwSlow` im Teleplot) — ohne das ist eine
   verschluckte Geste von Unzuverlaessigkeit nicht zu unterscheiden.
   Beim Einschalten setzt `Actions::resetPose` die Haltung auf
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
3. `PoseDetector` — ein **Neigungs-Gate** entscheidet zuerst: ausserhalb gibt es nur
   `Idle`, egal wie die Hand steht. Erst innerhalb des Gates waehlt die geglaettete
   Verdrehung gegen `TWIST_NEUTRAL_DEG` zwischen `Point` und `Turned` — `Idle` ist also
   keine Zone der Verdrehung mehr, sondern allein das Ergebnis des Gates.
   Es gibt **zwei** Gates mit verschiedenen Aufgaben: das enge, symmetrische
   `LEVEL_MAX_DEG` (`level()`) ist Voraussetzung der Ein/Aus-Drehgeste, das weite und
   asymmetrische `POSE_UP_MAX_DEG`/`POSE_DOWN_MAX_DEG` (`poseGate()`) entscheidet ueber
   `Idle` — nach oben zeigt und scrollt man, nach unten haengt der Arm im Ruhezustand.
   Massgeblich ist die Beobachtbarkeit der Verdrehung, `sqrt(ux²+uz²) = cos(elev)`; bei
   35 Grad stehen davon noch 82 Prozent zur Verfuegung.
   Laeuft der Scroll-Modus (`holdTurned`, aus `fsm_.scrolling()`), gilt das weite Gate
   **gar nicht**: Scrollen heisst den Arm neigen, und mit Gate beendete das Scrollen sich
   selbst. Der einzige Weg heraus ist das Zurueckdrehen, auch bei erhobener Hand. Nur
   jenseits von `POSE_HOLD_MAX_DEG` bleibt die Haltung stehen. Geglaettet (`MODE_TAU`) + Haltezeit
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
   Nach einem **Rechtsklick** sperrt der Controller zusaetzlich
   (`pinch_.holdOff()`, `RIGHT_CLICK_HOLDOFF_MS`): der Impuls beim Loesen des Pinch
   schloesse sonst das eben geoeffnete Kontextmenue wieder. Der Rechtsklick bleibt ein
   ganzer Klick und nimmt am Doppel-Pinch nicht teil.
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
   `GoToSleep`, bereitet das **private** `prepareSleep()` nur Funk und IMU vor
   (`radioOff()`, IMU auf Sleep-Rate + Wake-on-Motion); das eigentliche Schlafenlegen
   (`suspendLoop()`) und Aufwecken fuehrt `main.cpp` aus, weil dort der Schleifentakt
   haengt, der danach neu ausgerichtet werden muss. Von aussen fragt man `wantsSleep()` ab
   — `prepareSleep()` direkt zu rufen wuerde die Sensorrate vom Zustand des Automaten
   abkoppeln.

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

- Kommentare auf **Deutsch, ASCII ohne Umlaute** (`waehrend`, `Verzoegerung`).

- **Sparsam kommentieren.** Der Code soll sich selbst erklaeren — sprechende Namen,
  kleine Funktionen mit einer Aufgabe. Ein Kommentar ist die zweitbeste Loesung: laesst
  sich dieselbe Auskunft durch einen besseren Bezeichner oder eine ausgelagerte Funktion
  geben, dann so. Richtwert ist die heutige Dichte in `lib/` und `src/` (rund 20 Prozent
  der Zeilen); wer deutlich darueber landet, hat vermutlich den Code erklaert statt ihn
  verstaendlich zu schreiben.

  Was bleiben darf: die Kurzbeschreibung am Kopf einer Datei, eine knappe Begruendung
  fuer etwas, das ohne sie wie ein Fehler aussieht (Reihenfolgen, `#if`-Zweige,
  Hardware-Eigenheiten wie „aktiv LOW"), Einheiten, und Literaturverweise dort, wo ein
  Verfahren aus einer Quelle stammt (Casiez et al. 2012, Madgwick 2010).

  Was **nicht** bleibt: was der Code schon sagt, ganze Absaetze Herleitung, und die
  Geschichte verworfener Ansaetze. Ausfuehrliche Begruendungen gehoeren nach
  `docs/Programmcode.md`, nicht in den Header.

- **`include/config.h` ist von dieser Regel ausgenommen.** Dort ist der Kommentar der
  Inhalt: `3.5f` allein sagt nichts, Einheit und Begruendung schon. Siehe unten.
- Kein `new`/`malloc`, keine `String`, keine dynamischen Container im Hot Path; feste
  Puffer und `float`.
- Namensgebung, ueber alle Module hinweg einheitlich zu halten:
  Zeitstempel `t<Ereignis>_` (`tQuiet_`, `tMove_`, `tLastPinch_`), Zaehler `n<Grund>_`
  (`nDebounce_`, `nMoveFail_`), Winkel mit Suffix `Deg`, Raten mit `Dps` oder `Hz`,
  Zeitparameter `now_ms` bzw. `now_us`, Tuning-Felder mit Einheit im Namen (`settleMs`,
  `lowDps`, `sleepAfterMs`), Abfragen nach einem Wunsch des Controllers als
  `wants...()` (`wantsSleep()`, `wantsActiveRate()`).

- `TODO.md` ist das laufende Arbeitsjournal (offene Messungen, Einstellwerte, Entscheide
  fuer die schriftliche Arbeit). Es ist stellenweise aelter als der Code — z.B. nennt es
  `USE_TWIST_GUARD`, das es in `config.h` nicht mehr gibt. Immer gegen den Code pruefen,
  bevor daraus etwas uebernommen wird.

- `docs/Programmcode.md` ist die ausfuehrliche Beschreibung fuer die schriftliche Arbeit,
  `docs/Altlasten.md` die Bestandesaufnahme der Aufraeumrunde. `README.md` ist die
  Einstiegsseite des oeffentlichen Repositoriums und bewusst kurz — Einzelheiten gehoeren
  nach `docs/`, nicht dorthin.
