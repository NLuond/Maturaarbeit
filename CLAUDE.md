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
getestet, nicht auf dem Chip. Alle zwoelf auf einmal:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" test -e native
```

Erwartet: `12 test cases: 12 succeeded`. Das laeuft in gut zehn Sekunden und ist der
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

Die PC-seitige BLE-Bruecke bringt ihren eigenen Selbsttest im selben Format mit — ohne
Geraet, ohne COM-Port, ohne bleak:

```powershell
py tools/ble_collect_bridge.py --selftest
```

Alles andere wird weiterhin ueber Kompilieren + Messen am Geraet (Teleplot) verifiziert.

Alle zwoelf Testdateien haengen daran, dass der jeweilige Header hardwarefrei bleibt — weder
`Arduino.h` noch `config.h`: `AirMouseState.h`, `ArmOrientation.h`, `TwistToggle.h`,
`TwistGuard.h`, `SleepPolicy.h`, `PoseDetector.h`, `PinchDetector.h`, `ScrollWheel.h`,
`MotionPipeline.h`, `OneEuro.h`, `PinchFeatures.h`/`ImuSample.h` und `MadgwickAHRS.h`. Die `-I`-Liste in
`[env:native]` bricht den Build, wenn eines davon abdriftet — das ist Absicht.

Parameter, die sonst aus `cfg::` kaemen, stecken deshalb in eigenen Tuning-Structs:
`TwistTuning`, `TwistGuardTuning`, `SleepTuning`, `PoseTuning`, `PinchTuning`,
`ScrollTuning`, `MotionTuning` und `PointerTuning`. **Ein Block von `static_assert`s am Kopf von
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
| `COLLECT_OVER_BLE` | Die Aufnahme laeuft ueber BLE-UART (Nordic UART Service) statt ueber USB-Serial; jede Zeile traegt dann einen 16-Bit-Zaehler. Gegenstueck auf dem PC ist `tools/ble_collect_bridge.py`. Nur mit `COLLECT_MODE` wirksam. |
| `DEBUG_TELEPLOT` | Teleplot-Kanaele (`>name:wert`) aus `Telemetry`. Kostet Serial-Bandbreite und bremst die Schleife — fuer echte Nutzungstests aus. |
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
Begruendung, aber hoechstens einen Satz** — die Datei ist als Spezifikation gedacht, nicht
als Werteliste und nicht als Aenderungsprotokoll. Neue Werte in die passende Gruppe
einsortieren und im selben Stil kommentieren; Gruppen, die ein Tuning-Struct spiegeln,
sind mit `[auch in <X>Tuning]` ueberschrieben.

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
| AKTIV | Maus eingeschaltet (`fsm_.on()`) | 208 Hz | an | `cfg::SAMPLE_INTERVAL_US` (208 Hz) |
| BEREIT | ausgeschaltet, aber innerhalb `SleepTuning::sleepAfterMs` (60 s) bewegt | 52 Hz | an | `cfg::READY_INTERVAL_US` (52 Hz) |
| SCHLAF | 60 s ohne Bewegung (`gyroSum < SleepTuning::stillDps`) | nur Beschleunigungssensor, Wake-on-Motion auf INT1 | aus | `loop()` suspendiert (`suspendLoop()`), IMU weckt per Interrupt |

Aus AKTIV fuehrt der Weg nach 5 min Ruhe (`SleepTuning::offAfterMs`) ueber ein
Selbst-Abschalten nach BEREIT, von dort nach weiteren 60 s in den SCHLAF.

Eingeschlafen wird nur aus BEREIT, nie direkt aus AKTIV — sonst verschwaende die Maus
mitten im Gebrauch, waehrend man den Cursor nur ruhig auf einem Ziel haelt. Nach
`SleepTuning::offAfterMs` (5 min) ohne Bewegung schaltet sie sich aus AKTIV aber selbst ab
(`SleepEvent::PowerOff` → `fsm_.onPower()`, also derselbe Weg wie die Drehgeste samt langem
Brummer) und faellt damit nach BEREIT, wo die kurze Ruhezeit uebernimmt. Ohne das laeuft
eine eingeschaltet abgelegte Maus die ganze Nacht mit vollem Takt und Funk: `tQuiet_` wurde
frueher bei jedem Takt zurueckgesetzt, solange `mouseOn` galt.

Beim Abschalten laeuft die Ruhezeit **neu** an. Das ist keine Kosmetik: `Haptic` schaltet
den Motor in `trigger()` auf HIGH und erst in `update()` wieder LOW — ginge das Geraet im
selben Takt schlafen, bliebe der Motor des langen Ein/Aus-Impulses an.

`BLE_ALWAYS_ON` gehoert dazu: auf `true` ist `radioOff()` eine leere Huelle, das Geraet
wirbt also auch im Schlaf weiter (sichtbar an der blinkenden blauen `LED_CONN`) und der
Funk bleibt der groesste Verbraucher. Vorgabe ist `false`; auffindbar bleibt das Geraet
ueber Wake-on-Motion, eine Bewegung genuegt.

Es gibt bewusst kein
System OFF: das RAM bleibt erhalten und damit der ueber Minuten gelernte Gyro-Nullpunkt
und die BLE-Verbindung, ein Reset wuerde beides kosten.

## Architektur

`src/main.cpp` ist bewusst duenn: fester Takt + IMU lesen, dann entweder die CSV-Zeile an
`CollectLink` geben (`COLLECT_MODE`) oder `AirMouseController::update()` aufrufen. Die Schleife arbeitet mit
**fester Schrittweite**, nicht mit der gemessenen Zeitdifferenz — Filter und ML-Fenster
brauchen eine konstante Abtastrate; nach einer Stockung wird neu ausgerichtet statt
nachgeholt. `cfg::DT` ist dabei nicht mehr die einzige Schrittweite: sie gilt nur in
AKTIV, in BEREIT laeuft die Schleife mit `cfg::READY_DT` (52 Hz statt 208 Hz) — der
ML-Pfad braucht die volle Rate ohnehin nur, solange die Maus eingeschaltet ist, und laeuft
ausschliesslich in AKTIV.

Jedes Modul ist eine header-only Klasse in einem eigenen `lib/<Name>/` (kein `.cpp`),
per `-I` in `platformio.ini` eingebunden. Neues Modul → Ordner anlegen **und** einen
`-I`-Eintrag ergaenzen: in `[env:xiaoblesense]` immer, in `[env:native]` zusaetzlich, wenn
das Modul hardwarefrei ist und einen PC-Test bekommt.

Die Policy-Module (`AirMouseState`, `ArmOrientation`, `PinchDetector`, `TwistToggle`,
`TwistGuard`, `PoseDetector`, `ScrollWheel`, `MotionPipeline`, `OrientationPointer`,
`SleepPolicy`, `Filters/`) kennen weder Hardware noch das EI-SDK. Beides ist auf
`ImuReader`, `MouseHID`, `CollectLink`, `Haptic`, `Battery`, `PinchClassifier` und
`Telemetry` beschraenkt. Diese Richtung beim Erweitern
beibehalten — nichts aus `lib/ei-model` oder `bluefruit` gehoert in ein Policy-Modul.

Streng hardwarefrei (weder `Arduino.h` noch `config.h`) sind davon alle ausser
`OrientationPointer` und `Filters/LowPass`+`HighPass`; jene beiden enthalten keine
Entscheidungslogik, sondern rechnen nur.

**`AirMouseState` (`lib/AirMouseState/`) haelt den gesamten Zustand** in zwei Achsen
(`Power` / `Pose`) und fuehrt selbst nichts aus: jedes Ereignis (`onPower`, `onPose`,
`onPinch`) liefert ein `Actions`-Struct zurueck, das der Controller in `apply()` — der
einzigen Stelle mit Seiteneffekten — ausfuehrt. Neue Zustandslogik gehoert hierhin und
braucht einen Test, kein zusaetzliches Flag im Controller.

**Die Haltung bestimmt, wohin die Bewegung geht; der Pinch klickt.**

| | Bewegung | Pinch |
|---|---|---|
| **Arm gerade** (`Point`) | Cursor | Linksklick |
| **Arm gedreht** (`Turned`) | Scrollen | Rechtsklick |

`Idle` (Arm zu steil) bleibt wirkungslos. **Scrollen braucht keinen Griff** — Abdrehen
genuegt, ohne Haltezeit, ohne Eintrittsgeste. Der Pinch bedeutet in beiden Haltungen
dasselbe („hier klicken"), nur die Taste wechselt, und **beide loesen sofort aus**. Der
Pinch ist reine Ausgabe: er veraendert keine Achse.

`Idle` ist **keine Zone der Verdrehung**, sondern ausschliesslich das Ergebnis des
Neigungs-Gates. Das stand frueher falsch im Klassenkommentar von `PoseDetector` und ist
jetzt durch `test_pose_detector` festgenagelt.

Die **gehaltene Ausdrehung** ist der Modus-Waehler und traegt in `TwistToggle` keine
Bedeutung mehr: sie ist einfach die Haltung `Turned`. Ein `TwistEvent::Held` fuer den
Scroll-Modus und ein tieferer Scheitelwinkel fuer das Ziehen sind beide erprobt und wieder
ausgebaut worden — beide lagen auf derselben Achse wie Ein/Aus und machten die Schaltgeste
unzuverlaessig.

**Es gibt bewusst kein Ziehen, und der Klick ist unteilbar.** Der Automat hat nur zwei
Achsen (`Power`, `Pose`); `scrolling()` ist reine Ableitung (`on() && pose == Turned`).

Drei Anlaeufe zum Ziehen sind gebaut, am Geraet erprobt und wieder entfernt worden:

- **Loslassen als Ende** (Apples „pinch and move"): ein *gehaltener* Pinch ist mit einer
  IMU grundsaetzlich unsichtbar (Zustand, keine Beschleunigung) — Doublepoint verbaut
  dafuer einen optischen Sensor. Der Loese-*Impuls* waere ein Ereignis und damit denkbar;
  am Geraet gemessen ist er aber nur teilweise vorhanden, bei kurzem Pinch gar nicht, und
  kaum ueber dem Rauschen.
- **Doppel-Pinch als Ausloeser**: sein Zeitband (200–350 ms) war in der Praxis nicht zu
  treffen.
- **Bewegung als Ausloeser**: dafuer musste die Taste ein Fenster lang unten bleiben, und
  das verzoegerte **jeden** Klick — ueber BLE, wo ein Bericht je Verbindungsintervall
  durchgeht, deutlich spuerbar.

Ebenfalls ausgebaut: ein Ausloeser ueber einen *tieferen* Scheitelwinkel der Ausdrehung.
Er lag auf derselben Achse wie Ein/Aus, und eine etwas zu weit geratene Schaltgeste wurde
dadurch stillschweigend zum Ziehen — Ein/Aus verlor messbar an Zuverlaessigkeit. Der
aeltere Anlauf Pinch-plus-Abdrehen kollidierte mit dem Rechtsklick auf derselben Geste.

**Die Vibration kommt sofort beim Pinch**, nicht erst bei einer Entscheidung — ein um ein
Fenster verzoegerter Puls fuehlt sich an, als gaebe es gar keinen. Jede laengere Vibration
sperrt anschliessend kurz die Klickerkennung (`pinch_.holdOff()`), sonst laese sie die
eigene Erschuetterung als Pinch.

**Ein/Aus brummt lang** (`Actions::hapticLong`, `cfg::HAPTIC_LONG_MS`), alles andere kurz:
1 = Klick oder Haltungswechsel, 2 = Rechtsklick. Ein/Aus ist das einzige Ereignis, nach
dem gar nichts mehr geht, und ein langer Puls hebt sich sauberer ab als eine dritte Anzahl
kurzer, die ohnehin zu einem Brummen verschmelzen wuerde.

`AirMouseController` verdrahtet nur noch: Ereignisse einsammeln → FSM fragen → `apply()`.
Ablauf pro Tick:

1. `TwistToggle` — die Ein/Aus-Geste: Unterarm zuegig um `onDeg` (45 Grad) drehen
   und binnen `maxMs` (1200 ms) zurueck schaltet ein oder aus (`TwistEvent::Toggle`).
   **Gemessen wird der AUSSCHLAG, integriert aus der rohen Drehrate `s.gy` ab dem
   Beginn der Bewegung — kein Winkel gegen `TWIST_NEUTRAL_DEG`.** Damit haengt die
   Geste weder an der Schwerkraft noch an einer festen Ruhelage, braucht keine
   Einschwingzeit und kennt keine Singularitaet bei steilem Unterarm; vorher waren das
   vier Wege, auf denen sie lautlos scheiterte. Integriert wird nur *waehrend* der
   Geste, ein Gyro-Nullpunktfehler summiert sich also nicht auf (3 Grad/s ueber 1.2 s
   sind 3.6 Grad gegen 45).
   Bewusst **nicht** `TwistGuard::rateDps()`: jene Rate soll die Verdrehung
   *vollstaendig* erfassen und holt sich dafuer die Lage dazu, hier zaehlt nur der
   Ausschlag. Der Schiefstand zwischen Unterarm- und Platinenachse kostet cos(Winkel),
   bei 15 Grad rund drei Prozent — bezahlbar gegen vier Totalausfaelle.
   Der Ausschlag ist deshalb **von der Haltung entkoppelt**: `TWIST_ON_DEG` (45,
   relativ) und `TURN_ON_DEG` (70, absolut) sind zwei verschiedene Groessen, ein
   `static_assert` haelt die Geste unter der Haltungsschwelle.
   **Der Ausschlag allein traegt die Geste nicht** — 45 Grad Unterarmdrehung kommen im
   Alltag staendig vor. Was sie zur Geste macht, sind **zwei Bedingungen am Start**, und
   beide gelten NUR dort:
   - `level()` (`LEVEL_MAX_DEG`, 30 Grad): die Maus wird gerade waagrecht benutzt.
     Waehrend der Geste wird nicht mehr geprueft — die Drehung schwenkt `elev` selbst
     mit, weil die Platinenachse nicht exakt auf der anatomischen Drehachse liegt, und
     ein laufendes Gate wuergte die eigene Geste ab.
   - `armedMs` (200 ms): der Unterarm muss vorher geruht haben. Eine bewusste Geste
     beginnt aus der Ruhe, Alltagsbewegung ist durchgehend.
   Beide zaehlen ihre Ablehnungen (`nTwLvl`, `nTwMov`) — das sind **verhinderte
   Fehlauslösungen** und deshalb erwartungsgemaess grosse Zahlen. Verdaechtig werden sie
   erst, wenn eine gemeinte Geste ausbleibt und einer von beiden dabei hochgeht.
   `outMaxMs` (600 ms) verlangt zusaetzlich einen **zuegigen Hinweg**.
   Das ist die **einzige** Bedeutung der Geste: wer laenger draussen bleibt, ist einfach
   in der Haltung `Turned`, und wie weit gedreht wird, spielt keine Rolle.
   Vier Phasen, im Teleplot-Kanal `tw` ablesbar: 0 = Ruhe, 1 = dreht heraus,
   2 = Ausschlag erreicht, 3 = Lockout, 4 = durch einen Pinch verbraucht. `twexc` zeigt
   den laufenden Ausschlag in Grad — der Kanal, an dem `TWIST_ON_DEG` eingestellt wird.
   `Idle -> Out` zuendet nur auf der **Flanke** der Drehrate: nach einem Abbruch dreht
   sich der Unterarm noch, und ohne die Flanke begaenne die Rueckdrehung sofort eine
   neue Geste in der Gegenrichtung.
   **Die Erschuetterung wird nur gemeldet (`reportShock()`), nicht mehr gewertet.** Ob
   sie die Ausdrehung verbraucht, entscheidet `TwistToggle` an der Drehrate: nur wenn
   der Unterarm zuvor `stillMs` (150 ms) lang unter `stillDps` (40 Grad/s) blieb, war
   sie ein Pinch. Die Drehung erzeugt ihre Erschuetterung am Scheitel naemlich selbst,
   und weil der Controller nur im **eingeschalteten** Zustand meldet, verwarf genau das
   frueher das Ausschalten — die Maus ging an, aber nicht wieder aus. Eine Ruhezeit
   statt eines Momentanvergleichs, weil die Rate am Umkehrpunkt kurz durch null geht,
   genau dort, wo der Anschlag liegt.
   Drei Bremsen koennen die Geste verwerfen, und alle drei taten das frueher **lautlos**:
   die Abbruchschwelle (`cfg::TWIST_CANCEL_ENV` — eigene, hoehere Schwelle als das
   Klick-Gate, weil die Drehung selbst die Huellkurve ueber `ENV_ON` hebt), das
   Waagrecht-Gate (`LEVEL_MAX_DEG`) und das Fenster (`maxMs`). Jede zaehlt
   inzwischen mit (`nTwCan`, `nTwLvl`, `nTwSlow` im Teleplot) — ohne das ist eine
   verschluckte Geste von Unzuverlaessigkeit nicht zu unterscheiden.
   Beim Einschalten setzt `Actions::resetPose` die Haltung auf `Point` zurueck. Einen
   Nullpunkt braucht die **Geste** seit dem Umbau auf den Ausschlag gar nicht mehr;
   `cfg::TWIST_NEUTRAL_DEG` gehoert nur noch der **Haltung** (`PoseDetector`) und ist
   dort fest statt beim Einschalten kalibriert: das geschah frueher direkt nach der
   Schaltgeste, wo die Lageschaetzung am staerksten gestoert ist, und ergab jedes Mal
   einen anderen Bezug.
2. `MadgwickAHRS` — Lage aus Gyro+Accel; liefert `upX/upY/upZ`, die Richtung von "oben"
   im Koerperkoordinatensystem. Bewusst **kein** `rollDeg()`/`pitchDeg()` mehr, siehe
   unten.
   **Die Lage wird gesetzt, nicht eingeschwungen.** Beim Start und nach jedem Aufwachen
   steht `oriented_` auf false, und der Controller ruft jeden Takt
   `seedFromAccel()`: ein einzelner Messwert legt die Lage bis auf die (ohnehin nicht
   beobachtbare) Drehung um die Lotrechte fest. Sobald einer davon nach reiner
   Schwerkraft aussieht (`|accMag - 1 g| <= cfg::SEED_ACC_TOL`), rastet die Schaetzung in
   genau diesem Takt ein und `oriented_` bleibt stehen. Waehrend der Bewegung ist das
   Setzen nur eine grobe Schaetzung — aber immer noch besser als die Flach-Annahme der
   Einheitsquaternion, von der der Filter frueher aus losgelaufen ist.
   Das ersetzt das komplette Einschwing-Geruest: `MADGWICK_BETA_FAST`,
   `SleepTuning::settleMs`, `SleepEvent::Settled` und `SleepPolicy::settling()` gibt es
   nicht mehr. Die Drehgeste haengt jetzt an `oriented_` statt an einem Zeitfenster und
   ist nach dem Aufwachen sofort da, sobald der Arm einen Takt lang ruhig ist.
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
   dabei eine laufende Ausdrehung (`twistToggle_.reportShock()`) — sofern der Unterarm
   dabei ruhte, war sie ein Pinch und keine Schaltgeste, sonst wuerde ein verpasster
   Pinch beim Zurueckdrehen die Maus abschalten statt rechtszuklicken.
   `PinchDetector::tick()` bekommt den Klassifikator als Callable uebergeben und ruft
   ihn per Kurzschlussauswertung nur bei offenem Gate — deshalb kennt die Policy das
   EI-SDK nicht und die Inferenz laeuft nicht in jedem Takt.
   Nach einem **Rechtsklick** sperrt der Controller zusaetzlich
   (`pinch_.holdOff()`, `RIGHT_CLICK_HOLDOFF_MS`): der Impuls beim Loesen des Pinch
   schloesse sonst das eben geoeffnete Kontextmenue wieder.
   Im **Scroll-Modus** zaehlt fast allein die Huellkurve (`tick(..., relaxed)`): der
   Rechtsklick faellt dort per Definition in eine Armbewegung, und der Gyro-Guard
   verwuerfe ihn genau dann, wenn er gebraucht wird.
   Der **Dreh-Guard** (`cfg::PINCH_TWIST_GUARD`, 60 Grad/s auf `TwistGuard::rateDps()`)
   ist der einzige, den `relaxed` nicht aufhebt. Ohne ihn wurde die Ein/Aus-Geste im
   Scroll-Modus zum ungewollten Rechtsklick. Er kostet nichts, weil gescrollt wird,
   indem man den Arm neigt und schwenkt, und nicht, indem man ihn verdreht — die
   beiden Bewegungen liegen auf getrennten Achsen. Zaehler `nTwist`.
5. `OrientationPointer` und `MotionPipeline` — **eine Quelle, zwei Ziele.** Der Zeiger
   rechnet die Bewegung genau einmal in Pixel; `MotionPipeline` bringt sie ans Ziel, das
   `MotionTarget` benennt: an den Cursor (`Point`) oder ueber `ScrollWheel` ins Rad
   (`Turned`, senkrechte Komponente durch `SCROLL_PX_PER_STEP`). Deshalb muss der Nutzer
   nur eine Bewegungsart lernen. Der Vorgaenger war ein Joystick auf der *gehaltenen*
   Armneigung mit Eintrittswinkel, Totzone und einer Sekunde Haltezeit — eine zweite
   Bewegungsart mit eigenem Verhalten.
   `arm::rates()` zerlegt die Drehrate in Gieren und Nicken
   *bezogen auf den Raum* (Roll-Kompensation, `USE_ROLL_COMP`) → Deadzone → 1-Euro-Filter
   → Beschleunigung → Pixel; `MotionPipeline` akkumuliert sie und schickt alle
   `MOVE_INTERVAL_US` int8-Berichte los. Den Sendeweg bekommt sie als Callable
   uebergeben — wie `PinchDetector` den Klassifikator —, deshalb kennt sie das HID nicht
   und laeuft im PC-Test. Ohne die Kompensation laeuft der Cursor bei verdrehter Hand schraeg;
   bei `twist = 0` sind beide Pfade identisch. `TwistGuard` blendet die Bewegung
   waehrend einer Unterarmdrehung aus, damit die Drehgeste sich zielen laesst, ohne dass
   der Cursor dabei querlaeuft. Seine Rate kommt aus der Lageschaetzung
   (Differenz von `arm::twistDeg` zum Vortakt) und nicht aus `gy`: die Unterarmachse
   faellt nicht exakt mit einer Platinenachse zusammen, eine Verdrehung leckt deshalb
   auch in `gx` und `gz` und ein einzelner Gyro-Kanal wuerde sie nicht vollstaendig
   erfassen.
6. `Telemetry` — die Teleplot-Ausgabe haelt const-Referenzen auf alle Module und liest
   sie von aussen; im Controller steht dafuer nur noch `telemetry_.update()` und
   `telemetry_.emit()`. Statt `#if` im Rumpf entscheiden die Compile-Konstanten
   `kEnabled` (aus `DEBUG_TELEPLOT`) und `kSet` (aus `DEBUG_SET`) — der Optimierer wirft
   die abgeschalteten Zweige samt Serial-Aufrufen weg, gemessen rund 2.7 kB Flash.
7. `SleepPolicy` — am Ende jedes Takts befragt, mit `fsm_.on()` und `gyroSum`. Liefert sie
   `PowerOff`, schaltet der Controller die Maus ueber `fsm_.onPower()` aus; liefert sie
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

Generierter Code aus Studio-Projekt **1084395**, Export **v3** — **nicht von Hand
editieren**, beim Update den ganzen Ordner ersetzen. Aktuelles Modell: **2 Klassen**
(`non_pinch`, `pinch`), 5 Kanaele, Fenster **40 Samples** @ 209 Hz (~191 ms),
**Achtung: die 209 Hz sind die im Studio hinterlegte Rate des Modells, nicht der
Schleifentakt** — der folgt dem Sensor mit 208 Hz (siehe unten),
**Rohsignal + 1D-CNN**, int8.

Struktur und Kopplung sind seit v1 unveraendert (gleiches Projekt, gleiche Fenster- und
Kanalzahl) — nur die Gewichte kommen aus einem groesseren Datensatz. Ein Wechsel zwischen
solchen Exporten braucht deshalb **keine** Codeaenderung; die `static_assert`s in
`PinchClassifier.h` bestaetigen das beim Uebersetzen.

Der Vorgaenger (Projekt 1036761, 3 Klassen, Spectral Analysis + MLP) liegt unter
`archive/ei-model-1036761/` — dort nur `model-parameters/` und `tflite-model/`, weil das
SDK bei jedem Export identisch mitkommt und 26 MB gross ist. **Ausserhalb von `lib/`, damit
PlatformIO es nicht findet.** Der Wechsel hat den Flash-Bedarf von 18.1 auf 14.4 Prozent
gesenkt: mit den Spektralmerkmalen faellt auch der FFT-Teil des SDK weg.

Die Klassennamen sind Schnittstelle: `PinchClassifier::isPinch()` sucht per `strcmp` die
Klasse **`pinch`**. Wie die Gegenklasse heisst, ist gleichgueltig — sie wird nie gelesen.

Die Abtastrate ist die kritische Kopplung: `PinchClassifier.h` haelt per `static_assert`
`cfg::SAMPLE_INTERVAL_US` (4808 µs) mit `EI_CLASSIFIER_INTERVAL_MS` zusammen, ebenso die
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

- **`include/config.h` haelt sich an dieselbe Regel, nur mit anderem Zuschnitt**: dort ist
  der Kommentar der Inhalt (`3.5f` allein sagt nichts), aber je Konstante steht hoechstens
  ein Satz plus Einheit. Herleitungen und die Geschichte frueherer Werte gehoeren nach
  `docs/Programmcode.md`.
- Kein `new`/`malloc`, keine `String`, keine dynamischen Container im Hot Path; feste
  Puffer und `float`.
- Namensgebung, ueber alle Module hinweg einheitlich zu halten:
  Zeitstempel `t<Ereignis>_` (`tQuiet_`, `tMove_`, `tLastPinch_`), Zaehler `n<Grund>_`
  (`nDebounce_`, `nMoveFail_`), Winkel mit Suffix `Deg`, Raten mit `Dps` oder `Hz`,
  Zeitparameter `now_ms` bzw. `now_us`, Tuning-Felder mit Einheit im Namen (`offAfterMs`,
  `lowDps`, `sleepAfterMs`), Abfragen nach einem Wunsch des Controllers als
  `wants...()` (`wantsSleep()`, `wantsActiveRate()`).

- `TODO.md` ist das laufende Arbeitsjournal (offene Messungen, Einstellwerte, Entscheide
  fuer die schriftliche Arbeit). Es ist stellenweise aelter als der Code — z.B. nennt es
  `USE_TWIST_GUARD`, das es in `config.h` nicht mehr gibt. Immer gegen den Code pruefen,
  bevor daraus etwas uebernommen wird.

- `docs/Programmcode.md` ist die ausfuehrliche Beschreibung fuer die schriftliche Arbeit,
  `docs/EdgeImpulse.md` die Anleitung zum Training des Modells, `docs/Aufnehmen.md` der
  Spickzettel fuer den Aufnahmetag (Kurzfassung derselben Schritte). `README.md` ist die
  Einstiegsseite des oeffentlichen Repositoriums und bewusst kurz — Einzelheiten gehoeren
  nach `docs/`, nicht dorthin.
