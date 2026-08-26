# Der Programmcode der Air Mouse

Beschreibung des Aufbaus und der Funktionsweise der Firmware.

Dieses Dokument erklärt, *wie* der Code aufgebaut ist und *warum* er so aufgebaut ist. Die
Begründungen stehen bewusst dabei — bei mehreren Entscheidungen war die naheliegende
Lösung die falsche, und das ist der interessantere Teil.

---

## 1. Was das Gerät tut

Eine am Unterarm getragene Platine steuert den Mauszeiger eines Computers. Die
Drehbewegung des Arms bewegt den Cursor, ein Zusammendrücken von Daumen und Zeigefinger
(ein *Pinch*) löst einen Klick aus, und eine Drehung des Unterarms um seine eigene Achse
wählt zwischen den Betriebsarten. Ausgegeben wird das als gewöhnliche USB- oder
Bluetooth-Maus, das Betriebssystem braucht also keinen Treiber.

**Hardware:** Seeed XIAO nRF52840 Sense — ein Mikrocontroller (ARM Cortex-M4F, 64 MHz)
mit aufgelöteter LSM6DS3-IMU (Beschleunigungssensor und Gyroskop in einem Gehäuse, über
I²C angebunden). Dazu ein kleiner Vibrationsmotor an Pin D1 für die Rückmeldung.

**Toolchain:** PlatformIO mit dem Arduino-Framework. Das Klassifikationsmodell kommt als
generierter C++-Code aus Edge Impulse.

---

## 2. Verzeichnisaufbau

```
src/main.cpp              Einstiegspunkt: Takt und Betriebsart-Weiche
include/config.h          Alle Einstellwerte und Compile-Time-Schalter
lib/<Modulname>/          Je ein Modul, header-only
test/test_<name>/         Je ein PC-Test (laeuft ohne Hardware)
tools/                    PC-seitige Hilfsprogramme (BLE-Bruecke fuer die Aufnahme)
docs/                     Diese Datei, die Anleitung zum Modelltraining und der
                          Spickzettel fuer den Aufnahmetag
platformio.ini            Build-Konfiguration und Include-Pfade
```

Jedes Modul ist **eine header-only Klasse in einem eigenen Ordner** unter `lib/`, ohne
zugehörige `.cpp`-Datei. Eingebunden werden sie über `-I`-Einträge in `platformio.ini`.
Der Grund ist pragmatisch: bei Klassen dieser Grösse spart die Aufteilung in Header und
Implementierung nichts, und der Compiler kann alles inline setzen, was auf einem
Mikrocontroller mit fester Taktrate zählt.

Ein neues Modul braucht deshalb immer zwei Schritte: den Ordner **und** den
`-I`-Eintrag. Vergisst man den zweiten, findet der Compiler den Header nicht.

---

## 3. Das Grundprinzip: fester Takt

`src/main.cpp` ist bewusst dünn. Die gesamte Schleife:

```cpp
void loop() {
    // Bis zum naechsten Takt schlafen statt leer durchlaufen (Abschnitt 9.1)
    while ((int32_t)(nextSample_us - micros()) > 0) delay(1);

    const uint32_t now_us = micros();

    // Taktlaenge folgt dem Betriebszustand: AKTIV 208 Hz, BEREIT 52 Hz
    const bool     active = app.wantsActiveRate();
    const uint32_t tickUs = active ? cfg::SAMPLE_INTERVAL_US : cfg::READY_INTERVAL_US;
    const float    tickDt = active ? cfg::DT                 : cfg::READY_DT;

    nextSample_us += tickUs;                             // naechster Takt
    if (Takt verpasst) { neu ausrichten; overruns++; }

    const ImuSample s = imu.read(tickDt);

    app.update(s, tickDt, now_us);                       // alles Weitere

    if (app.wantsSleep()) { /* Interrupt scharf, suspendLoop(), aufwachen */ }
}
```

Entscheidend ist, dass die Verarbeitung mit einer **festen Schrittweite** rechnet und
nicht mit der tatsächlich verstrichenen Zeit. Alle Filter und das Zeitfenster des
Klassifikators setzen eine konstante Abtastrate voraus. Nach einer Stockung wird der Takt
deshalb *neu ausgerichtet*, statt die Rückstände nachzuholen — ein nachgeholter
Doppelschritt würde jeden Filter kurzzeitig verfälschen.

„Fest" heisst dabei nicht „eine einzige": es gibt zwei Schrittweiten, `cfg::DT` in AKTIV
und `cfg::READY_DT` in BEREIT (Abschnitt 9.2). Innerhalb eines Zustands ist die
Schrittweite konstant, und der ML-Pfad läuft ausschliesslich in AKTIV, wo weiterhin exakt
`cfg::SAMPLE_INTERVAL_US` gilt.

Die Schrittweite ist dort nicht frei wählbar, sondern von **zwei** Seiten gebunden.
Massgeblich ist der Sensor: `cfg::SAMPLE_INTERVAL_US = 4808 µs` ist exakt 1/208 s und
damit die Zeit, in der der LSM6DS3 bei `ACCEL_ODR_HZ = 208` einen neuen Messwert
liefert. Auf jeden Sensorwert kommt so genau ein Schleifendurchlauf.

Vorher stand hier 4785 µs, also 209 Hz — die Rate, mit der das Edge-Impulse-Projekt
hinterlegt ist. Die Schleife lief damit rund 0,5 % **schneller als der Sensor**, und
etwa einmal pro Sekunde las sie denselben Messwert zweimal. Ein doppelter Wert ist für
den 30-Hz-Hochpass der Hüllkurve eine Stufe mit Ableitung null, also genau in dem
Frequenzband eine Störung, aus dem die Klickerkennung ihr Signal zieht.

Die zweite Bindung ist das Modell: läuft die Firmware schneller oder langsamer als beim
Training, sieht der Klassifikator ein zeitlich gestauchtes oder gedehntes Fenster und
wird still schlechter, ohne dass ein Fehler auftritt. Ein `static_assert` in
`PinchClassifier.h` hält beide Zahlen zusammen, seit dieser Änderung mit einer relativen
Toleranz von 1 % statt ±25 µs. Die verbleibende Abweichung 208 gegen 209 Hz beträgt
über das 40er-Fenster rund 0,9 ms auf 191 ms — weit unter der Streuung zweier Pinches
derselben Hand. Wird das Modell einmal aus 208-Hz-Daten neu exportiert, fällt auch
dieser Rest weg.

Der Zähler `overruns` zählt verpasste Takte. Er ist im Teleplot als Kanal `ovr` sichtbar
und im Aufnahmemodus zusätzlich an die eingebaute LED gekoppelt (Abschnitt 11). Er hat
allerdings einen blinden Fleck von zwei Takten und wird deshalb vom Kanal `late` ergänzt,
der die Verspätung jedes einzelnen Takts in µs ausgibt — siehe Abschnitt 9.1.

---

## 4. Die sechs Rollen

Der Code ist in sechs Rollen aufgeteilt, und die Trennung wird strikt eingehalten:

| Schicht | Rolle | Module |
|---|---|---|
| **Treiber** | Hardware ansprechen | `ImuReader`, `MouseHID`, `Haptic`, `PinchClassifier`, `Battery` |
| **Ableitung** | Messwerte in Grössen umrechnen | `MadgwickAHRS`, `ArmOrientation`, `Filters/` |
| **Erkenner** | aus Grössen Ereignisse machen | `PoseDetector`, `TwistToggle`, `TwistGuard`, `PinchDetector`, `SleepPolicy` |
| **Zustand** | entscheiden, was ein Ereignis bedeutet | `AirMouseState` |
| **Ausführung** | Entscheidungen umsetzen | `AirMouseController`, `OrientationPointer`, `MotionPipeline`, `ScrollWheel` |
| **Beobachtung** | Zustand sichtbar machen, ohne einzugreifen | `Telemetry` |

Die wichtigste Regel: **`AirMouseState` hält den gesamten Zustand und führt selbst nichts
aus.** Jedes Ereignis liefert ein `Actions`-Struct zurück, das beschreibt, *was zu tun
ist*; ausgeführt wird es an genau einer Stelle, in `AirMouseController::apply()`.

Der Nutzen zeigt sich beim Suchen von Fehlern. Verhält sich das Gerät falsch, gibt es nur
zwei Möglichkeiten: entweder liefert ein Erkenner das falsche Ereignis, oder der Automat
zieht daraus den falschen Schluss. Der zweite Fall lässt sich auf dem PC in Sekunden
prüfen (Abschnitt 10), der erste am Gerät messen. Vorher lagen Zustandsbits wie
`airmouseOn_` und `lastMode_` im Controller verstreut und mussten von Hand synchron
gehalten werden — jede Änderung konnte sie auseinanderlaufen lassen.

Die zweite Regel: **Erkenner und Zustand kennen keine Hardware.** In
`AirMouseState`, `ArmOrientation`, `TwistToggle`, `TwistGuard`, `PinchDetector`,
`PoseDetector`, `ScrollWheel`, `MotionPipeline`, `SleepPolicy`, `MadgwickAHRS` und
`Filters/OneEuro.h` steht kein `#include <Arduino.h>`, kein `config.h`, kein Bluetooth
und nichts aus dem Edge-Impulse-SDK. Genau das macht sie auf dem PC testbar (Abschnitt 10).

Zwei Module halten diese Regel bewusst *nicht* ein und stehen deshalb hier: `Filters/`
`LowPass`/`HighPass` benutzen die Arduino-Konstante `PI`, und `OrientationPointer` zieht
`config.h` für die Vorgaben in `PointerTuning`. Beide enthalten keine
Entscheidungslogik — sie rechnen — und werden über den 1-Euro-Test und über Messungen am
Gerät abgedeckt.

---

## 5. Der Ablauf eines Takts

`AirMouseController::update()` ist das Bindeglied. Pro Takt läuft:

```
   IMU lesen  (ImuReader: ein I2C-Burst, 12 Bytes)
        │
        ├──► MadgwickAHRS ──► ArmOrientation ──► twist_ , elev_
        │        (Lage)         (benannte Winkel)
        │
        ├──► VibrationEnvelope ──► env      (Erschuetterungs-Huellkurve)
        │
        ▼
   1. TwistGuard.update(twist_)   ──► twistGain_   (0…1, bremst den Cursor)
        │
   2. PoseDetector.update(twist_, elev_, gyroSum)  ──► Haltung
        │
   3. env > TWIST_CANCEL_ENV ──► TwistToggle.reportShock()  (nur im Ein-Zustand)
        │
   4. TwistToggle.tick(s.gy, level)      (Ausschlag durch Integration)
        │      └── Toggle ──► fsm_.onPower()       ──┐
        │                                            │
   5. wenn eingeschaltet:                            ├──► apply()
        │      fsm_.onPose(Haltung)                ──┤     (einzige Stelle
        │      PinchDetector ──► fsm_.onPinch()    ──┘      mit Wirkung)
        │
   6. runMotion(): EINE Bewegung, zwei Ziele
        │      OrientationPointer ──► px, py
        │           ├── scrolling() ──► ScrollWheel ──► HID-Scroll
        │           └── pointing()  ──► HID-Bewegung
        │
   7. SleepPolicy.tick(on, gyroSum) ──► PowerOff / GoToSleep
```

Zwei Details in dieser Reihenfolge sind nicht beliebig:

**Die Haltung wird vor dem Pinch verarbeitet.** Welche Taste ein Pinch auslöst, hängt an
der Haltung — die soll im selben Takt schon die aktuelle sein.

**Die Haltung wird frisch aus `pose_.pose()` gelesen, nicht oben gemerkt.** Schaltet
`onPower()` in diesem Takt gerade ein, setzt `apply()` über `resetPose` die
Haltungserkennung zurück. Ein oben gemerkter Wert wäre dann für einen Takt veraltet und
die Maus startete kurz in der alten Haltung.

---

## 6. Die Module im Einzelnen

### 6.1 Sensorik — `ImuReader`, `ImuSample`

`ImuSample` ist die Grenzstruktur zwischen Treiber und Verarbeitung. Sie enthält bewusst
keine Sensortypen, damit ein Modul, das nur Messwerte braucht, nicht das halbe
LSM6DS3-Interface mitkompilieren muss:

```cpp
struct ImuSample {
    float ax, ay, az;      // roh, mit Erdbeschleunigung   -> Madgwick
    float lax, lay, laz;   // linear, ohne Erdbeschleunigung -> ML-Modell
    float gx, gy, gz;      // Drehraten, Nullpunkt korrigiert
    float accMag;          // Betrag der rohen Beschleunigung -> Huellkurve
    float gyroSum;         // |gx| + |gy| + |gz|, Mass fuer "wie bewegt"
};
```

`ImuReader::read()` liest alle sechs Rohwerte in **einer** I²C-Transaktion: 12 Bytes ab
Register `0x22`, wo Gyro X/Y/Z und Beschleunigung X/Y/Z lückenlos aufeinanderfolgen.
Sechs Einzelzugriffe kosteten bei 100 kHz rund 2 ms — bei 4.785 ms Schrittweite mehr als
ein Drittel des Budgets — und die sechs Werte stammten aus verschiedenen Zeitpunkten, die
bis zu 2 ms auseinanderlagen. Der Bus läuft auf 400 kHz; die Rate muss **nach**
`imu_.begin()` gesetzt werden, weil `Wire.begin()` sie dort auf die Arduino-Vorgabe
zurückstellt.

**Nullpunktkorrektur des Gyroskops.** Ein MEMS-Gyroskop zeigt auch im Stillstand nicht
exakt null, und der Fehler wandert mit der Temperatur. Ein Tiefpass entfernt ihn nicht —
er ist keine Schwankung, sondern ein Versatz. Er wird deshalb gelernt, solange das Gerät
ruht, und abgezogen. Die Bedingung ist zweiteilig:

```cpp
if (s.gyroSum < cfg::BIAS_STILL_DPS &&               // 3 Grad/s
    fabsf(s.accMag - 1.f) < cfg::BIAS_ACC_TOL) {     // 0.05 g um 1 g
```

Die Schwelle lag zunächst bei 15 °/s und damit **über** der langsamsten gemeinten
Bewegung: langsames, gezieltes Zeigen liegt bei 5–10 °/s und fiel mitten ins Lernfenster.
Der Schätzer übernahm die gewollte Drehrate als vermeintlichen Nullpunkt; der Cursor wurde
beim langsamen Ziehen immer langsamer und driftete beim Anhalten zurück. Das Symptom sieht
aus wie zu starke Glättung und ist keine.

**Gravitationsfreie Achsen.** Drei Tiefpässe bei 0.8 Hz schätzen die Erdbeschleunigung;
`lax = ax − lowpass(ax)` ist das, was übrig bleibt. Wozu das dient, steht in Abschnitt 6.7.

### 6.2 Lage — `MadgwickAHRS`, `ArmOrientation`

Der Madgwick-Filter fusioniert Gyroskop und Beschleunigungssensor zu einer Schätzung der
räumlichen Lage. Das Gyroskop ist kurzfristig genau, driftet aber; der
Beschleunigungssensor kennt langfristig die Richtung "unten", ist aber bei Bewegung
gestört. Der Filter kombiniert beides und liefert `upX/upY/upZ` — die Richtung von "oben"
im Koordinatensystem der Platine.

`ArmOrientation` benennt daraus zwei Winkel:

- **`twistDeg`** — die Verdrehung um die Unterarmachse (Supination/Pronation).
- **`elevDeg`** — die Neigung des Unterarms aus der Waagerechten.

Diese Umbenennung ist kein Kosmetikschritt, sondern die Reparatur eines teuren Fehlers.
Der Madgwick-Filter bietet üblicherweise `rollDeg()` und `pitchDeg()` an. Ausmultipliziert
sind das `atan2(uy, uz)` und `asin(−ux)`. Bei der hier verwendeten Einbaulage — flach
`az = +1`, um 90° verdreht `ax = +1` — ist das erste die **Armneigung** und das zweite die
**Handverdrehung**, also genau vertauscht gegenüber dem, was die Namen nahelegen. Zwei
Module bekamen dadurch den falschen Winkel: die Haltungserkennung reagierte auf Heben
statt auf Drehen, und die Ausblendung im Zeiger schnitt die senkrechte Bewegung schon bei
gerader Hand weg. Die beiden Methoden gibt es deshalb nicht mehr; Winkel werden
ausschliesslich über `arm::twistDeg()` und `arm::elevDeg()` benannt.

`ArmOrientation` ist hardwarefrei und wendet `cfg::ELEV_SIGN` **nicht** selbst an — das
tut der Controller. Nur so bleibt das Modul ohne `config.h` übersetzbar und damit auf dem
PC testbar.

### 6.3 Filter — `lib/Filters/`

| Datei | Funktion |
|---|---|
| `LowPass.h` | Tiefpass erster Ordnung, Grenzfrequenz im Konstruktor |
| `HighPass.h` | Hochpass erster Ordnung |
| `VibrationEnvelope.h` | Hochpass 30 Hz → Betrag → Tiefpass 15 Hz |
| `OneEuro.h` | 1-Euro-Filter nach Casiez et al. 2012 |

Alle nehmen ihre Parameter über den Konstruktor entgegen, mit `cfg::` nur als Vorgabe.
Damit lassen sich zwei verschieden eingestellte Filter im selben Programm gegeneinander
laufen lassen — für Vergleichsmessungen, ohne für jede Variante neu zu flashen.

**Der 1-Euro-Filter** ist ein Tiefpass, dessen Grenzfrequenz mit der
Bewegungsgeschwindigkeit mitwächst:

```
cutoff = MIN_CUTOFF + BETA · geglaettete_Drehrate
```

Steht die Hand still, filtert er stark und unterdrückt das Zittern; bewegt sie sich,
öffnet er und erzeugt kaum Verzögerung. Ein fester Tiefpass muss sich zwischen diesen
beiden Fällen entscheiden, dieser nicht.

Eine Abweichung vom Original ist beabsichtigt: dort wird die Geschwindigkeit aus dem
verrauschten Positionssignal geschätzt, hier liefert das Gyroskop sie direkt, die
Ableitungsstufe entfällt also. Eine zweite Abweichung war **nicht** beabsichtigt und
verursachte ein deutlich sichtbares Zittern des Cursors: mit der Ableitungsstufe war auch
der Tiefpass auf der Geschwindigkeit (`dcutoff`, im Original 1 Hz) verschwunden. Ohne ihn
folgt die Grenzfrequenz dem Betrag des Signals und steht bei Handzittern ausgerechnet auf
den Spitzen am weitesten offen. Wiederhergestellt und isoliert gemessen senkt der
Geschwindigkeits-Tiefpass den Effektivwert eines simulierten 10-Hz-Tremors um
**15.2–15.5 %**; der Spitzenwert trennt mit 10.3–10.6 % schlechter, die naheliegende
Vermutung "der Effekt zeigt sich in den Spitzen" ist also widerlegt. Diese Messung entstand
mit `dcutoff` auf dem Originalwert 1 Hz.

Der eingestellte Wert liegt inzwischen bei **3 Hz**, und zwar wegen derselben Abweichung,
die auch die Ableitungsstufe erspart: 1 Hz entspricht einer Zeitkonstante von 159 ms, so
lange braucht die Geschätzung der Geschwindigkeit, um einer begonnenen Bewegung zu folgen.
Bis dahin steht die Grenzfrequenz noch nahe `MIN_CUTOFF`, und genau das war am Gerät als
träger Bewegungsbeginn spürbar. Die 1 Hz des Originals sind dort nötig, weil die
Geschwindigkeit aus einem verrauschten Positionssignal differenziert wird; hier liefert sie
das Gyroskop direkt, und die Deadzone von 3.5 °/s entfernt das Kleinzittern bereits vor dem
Filter. Der Zweck des Tiefpasses bleibt auch bei 3 Hz erhalten — die Welligkeit eines
10-Hz-Tremors erscheint in `|Geschwindigkeit|` bei 20 Hz und wird dort immer noch um rund
85 % gedämpft, die Grenzfrequenz reitet also weiterhin nicht auf den Tremorspitzen.

Simuliert man einen Drehratensprung auf 30 °/s bei 208 Hz durch die Filterlogik, ergibt
sich die Anstiegszeit des Ausgangs:

| `MIN_CUTOFF` / `BETA` / `DCUTOFF` | 63 % | 90 % |
|---|---|---|
| 1.0 / 0.20 / 1.0 | 77 ms | 135 ms |
| 0.6 / 0.12 / 1.0 | 106 ms | 183 ms |
| **0.6 / 0.20 / 3.0** (eingestellt) | **58 ms** | **101 ms** |

Die mittlere Zeile war am Gerät als träger Bewegungsbeginn deutlich spürbar. Bemerkenswert
ist die dritte: sie ist schneller als die erste und dämpft langsame Bewegung trotzdem
stärker, weil die beiden Eigenschaften an verschiedenen Parametern hängen — `MIN_CUTOFF` an
der Ruhe, `DCUTOFF` am Bewegungsbeginn.

Der grössere Hebel gegen das Zittern ist allerdings `BETA`. Physiologischer Tremor liegt
bei 8–12 Hz mit 0.1–0.5° Amplitude, also rund 12 °/s Drehrate; der Mittelwert von
`|12·sin(2π·10t)|` ist 7.6 °/s, ob geglättet oder nicht. Mit `BETA = 0.55` ergab das eine
mittlere Grenzfrequenz von 5.1 Hz und damit kaum Dämpfung bei 10 Hz, mit `BETA = 0.2` sind
es 2.5 Hz (beides bei `MIN_CUTOFF = 1.0`; mit den heutigen 0.6 sind es 4.8 bzw. 2.1 Hz).

### 6.4 Zeiger — `OrientationPointer`

Rechnet Drehraten in Pixel um. Der Weg eines Werts:

```
gx, gz  ──► arm::rates()  ──► Gieren, Nicken   (Roll-Kompensation)
        ──► Deadzone      ──► kleine Reste weg
        ──► 1-Euro-Filter ──► geglaettet
        ──► Beschleunigung──► Kennlinie (derzeit aus)
        ──► · SENS · dt   ──► Pixel
```

**Roll-Kompensation.** `arm::rates()` zerlegt die Drehrate bezogen auf den *Raum* statt
auf die Platine. Ohne sie läuft der Cursor bei verdrehter Hand schräg: dreht man die Hand
um 45°, wird aus einer waagerechten Handbewegung eine diagonale Cursorbewegung. Bei
`twist = 0` sind beide Pfade identisch, der Unterschied zeigt sich erst bei verdrehter
Hand. Über `USE_ROLL_COMP` abschaltbar.

Der dafür eingesetzte Verdrehungswinkel ist **nicht** derselbe, mit dem die
Haltungserkennung arbeitet. `PoseDetector` führt zwei geglättete Fassungen desselben
Winkels: eine schnelle (`MODE_TAU = 0.10 s`) für Haltung und Geste, und eine langsame
(`ROLLCOMP_TAU = 0.25 s`) für die Roll-Kompensation. Der Grund: hier steht der Winkel in
einer Drehmatrix, und deren Rauschen landet unmittelbar als Zittern im Cursor. Die
schnellere Glättung, die die Drehgeste braucht, wäre an dieser Stelle ein Rückschritt.

**Deadzone.** Weich statt hart: unterhalb der Schwelle null, darüber wird die Schwelle
abgezogen, damit die Bewegung stetig bei null beginnt. Sie fängt den Rest-Nullpunktfehler
ab, den die Bias-Korrektur übriglässt. Gegen das Zittern hilft sie nur begrenzt — Tremor
liegt bei rund 12 °/s und damit weit über jeder vertretbaren Totzone. Sie weiter
anzuheben kostet feine Bewegung direkt: 2.5 °/s entsprechen bei `SENS_X = 165` schon
412 px/s, die stufenlos abgezogen werden. Mit der angehobenen Verstärkung ist sie
umgekehrt wichtiger geworden: was sie durchlässt, wird mitverstärkt, ein von selbst
wandernder Cursor fällt jetzt eher auf.

**Verstärkung.** `SENS_X`/`SENS_Y` stehen auf 165 px/° — rund 12° Armdrehung für die volle
Breite eines 1920 px breiten Schirms. Casiez et al. 2008 finden zu niedrige Verstärkung
klar schädlich und zu hohe kaum, im Zweifel also eher höher. Der Wert hat eine nicht
offensichtliche Nebenwirkung: dieselben Pixel speisen über `MotionPipeline` auch das
Scrollrad, das sie durch `SCROLL_PX_PER_STEP` teilt. Die gemeinte Grösse ist dort ein
*Winkel* je Radschritt, nicht eine Pixelzahl — beim Ändern von `SENS_Y` gehört
`SCROLL_PX_PER_STEP` deshalb im selben Verhältnis mitgezogen, sonst ändert sich das
Scrolltempo unbeabsichtigt mit dem Cursortempo. 60 px bei 165 px/° sind wie zuvor rund ein
Drittel Grad je Schritt.

**Beschleunigung.** `ACCEL_K` steht auf 0, die Kennlinie ist also linear. Sie greift
**nach** dem Filter und multipliziert deshalb auch das Restzittern — bis zum Vierfachen.
Scotto et al. 2020 fanden eine linear steigende Verstärkung ohnehin schlechter als eine
gute konstante. Die Konstante bleibt stehen, damit sich die Gegenprobe über
`PointerTuning` ohne Neuflashen fahren lässt.

**Ausblendung nach oben.** Ab `ELEV_LIMIT − ELEV_FADE` wird die Aufwärtsbewegung stetig
ausgeblendet, bevor der Arm an seine anatomische Reichweite läuft.

### 6.5 Verdrehungsbremse — `TwistGuard`

Friert die Cursorbewegung ein, während sich der Unterarm um seine eigene Achse dreht.

Ohne das läuft der Zeiger beim Hindrehen in die abgedrehte Haltung quer über den Schirm,
und der Rechtsklick ist unbrauchbar: bis die Hand die Haltung erreicht hat, steht der
Cursor nicht mehr auf dem Ziel.

Die naheliegende Lösung — die Achse `gy` verwerfen — genügt nicht, und der Zeiger tut das
ohnehin schon. Die Unterarmachse fällt nicht exakt mit einer Platinenachse zusammen; das
Board sitzt am Arm und nicht im Gelenk. Eine Verdrehung leckt deshalb immer auch in `gx`
und `gz`, also genau in die beiden Achsen, aus denen der Zeiger seine Bewegung zieht. Der
Guard leitet seine Rate darum aus der **Lageschätzung** ab: die Ableitung von
`arm::twistDeg()` misst die Verdrehung selbst, unabhängig von der Einbaulage.

```
gain
 1.0 │────────────────┐
     │                 ╲
 0.0 │                  └──────────────────
     └──────────────────────────────────────►  |Verdrehungsrate|
     0          25 °/s      70 °/s
```

Zwei Eigenschaften sind wesentlich:

- **Sofort zu, langsam wieder auf.** Am Ende einer Drehung klingt die Rate aus und kreuzt
  die Schwelle mehrfach; ohne begrenzte Rückkehr (`releaseS = 0.20 s`) zuckte der Cursor
  dabei wiederholt an.
- **Der Faktor greift auf die fertigen Pixel**, nicht auf die Rate vor dem 1-Euro-Filter.
  So läuft der Filter während der Drehung weiter mit und bleibt eingeschwungen —
  andernfalls käme nach jeder Drehung eine Anfahrverzögerung obendrauf.

Aus demselben Grund verwirft `reset()` nur den letzten Winkel und **nicht** die geschätzte
Rate: der Guard ist ein durchgehend laufender Schätzer, dessen Zustand beim Zurücksetzen
gültig ist. Die Bremse darf nicht mitten in genau der Drehung aufgehen, die das
Zurücksetzen ausgelöst hat.

### 6.6 Haltungserkennung — `PoseDetector`

Bildet Verdrehung und Armneigung auf eine von drei Haltungen ab:

| Haltung | Bedeutung |
|---|---|
| `Point` | Hand gerade — Cursor folgt der Bewegung, Pinch = Linksklick |
| `Idle` | Arm zu steil — nichts passiert |
| `Turned` | Hand abgedreht — Pinch = Rechtsklick, nach einer Sekunde zusätzlich Scroll |

Wichtig: **`Idle` ist keine Zone der Verdrehung.** Es ist ausschliesslich das Ergebnis des
Neigungs-Gates. Diese Bedingung wird absolut gegen die Schwerkraft gemessen und nicht
relativ zum Einschalten: "waagrecht" soll waagrecht heissen, sonst kalibriert man sich die
Bedingung durch Einschalten in schiefer Haltung gleich weg.

Es gibt **zwei** solche Gates mit verschiedenen Aufgaben. Beobachtbar ist die Verdrehung,
solange die Schwerkraft eine Komponente quer zur Unterarmachse hat; deren Betrag ist
`sqrt(ux²+uz²) = cos(elev)`. Bei 35° sind das noch 82 % des Signals, erst jenseits von
rund 70° wird `atan2f(ux, uz)` wirklich schlecht konditioniert.

- **eng und symmetrisch** (`LEVEL_MAX_DEG`, ±30°, `level()`): Startbedingung der
  Ein/Aus-Drehgeste, dort nur beim Anlaufen geprüft. Es beantwortet nicht mehr „ist der
  Winkel verlässlich" (das braucht die integrierte Fassung nicht), sondern „wird die Maus
  gerade benutzt" — und ist damit die wichtigste Bremse gegen Fehlschaltungen. Eine
  Fehlschaltung kostet mehr als eine verpasste Geste, deshalb eng.
- **weit und asymmetrisch** (`POSE_UP_MAX_DEG` +65°, `POSE_DOWN_MAX_DEG` −35°,
  `poseGate()`): entscheidet über `Idle`. Die beiden Richtungen bedeuten Verschiedenes —
  nach oben zeigt und scrollt man, nach unten hängt der Arm im Ruhezustand.

Läuft der Scroll-Modus, gilt das weite Gate **gar nicht** (Parameter `holdTurned`).
Scrollen heisst den Arm zu neigen; würde das Gate hier greifen, beendete das Scrollen sich
selbst — `Idle` verlässt `Turned`. Der einzige Weg heraus ist deshalb das Zurückdrehen,
und das funktioniert auch bei erhobener Hand, ohne den Arm erst senken zu müssen. Nur
jenseits von
`POSE_HOLD_MAX_DEG` (80°) steht der Unterarm so steil, dass die Verdrehung nicht mehr zu
sehen ist; dort bleibt die Haltung stehen, statt auf einem Rauschwert umzuspringen.

Innerhalb des Gates hat die Verdrehachse nur noch **eine** Schwelle, mit Hysterese:

```cpp
Pose classify() const {
    const float tilt = fabsf(rel_);
    if (pose_ == Pose::Turned) return (tilt < cfg::TURN_OFF_DEG) ? Point : Turned;  // 55
    return (tilt > cfg::TURN_ON_DEG) ? Turned : Point;                              // 70
}
```

Verglichen wird der **Betrag**. Aus der Zeige-Haltung heraus lässt sich der Unterarm rund
90° supinieren, aber nur 10–30° pronieren; die Schwelle ist also anatomisch nur in einer
Richtung erreichbar, und welche das ist, muss der Code nicht wissen. Eine frühere
Richtungskonstante liess sich nur durch Ausprobieren bestimmen und machte bei falscher
Einstellung den Scroll-Modus unerreichbar, ohne dass man es der Konfiguration ansah.

Drei Mechanismen stabilisieren die Entscheidung: Glättung (`MODE_TAU`), eine Haltezeit
(`MODE_DWELL_MS`, ein Wechsel zählt erst, wenn er kurz stabil anliegt) und eine
**Bewegungssperre** — während `gyroSum` über `POSE_STILL_DPS` liegt und für `POSE_CALM_MS`
danach wird gar nicht erst entschieden. Eine gehaltene Haltung ist per Definition nichts,
was man mitten im Schwung einnimmt. Die Winkel laufen dabei weiter mit, nur die
Entscheidung ruht.

### 6.7 Klickerkennung — `VibrationEnvelope`, `PinchDetector`, `PinchClassifier`

Ein Pinch erzeugt eine kurze, hochfrequente Erschütterung, die sich über die Hand bis zur
Platine fortpflanzt. Die Kette:

```
accMag ──► Hochpass 30 Hz ──► Betrag ──► Tiefpass 15 Hz ──► env
                                                             │
                                        ┌────────────────────┘
                                        ▼
                        bi-level Gate (ENV_ON 0.035 / ENV_OFF 0.020)
                                        │  nur wenn offen:
                                        ▼
                        ML-Klassifikator ──► Pinch ja/nein
                                        │
                                        ▼
                Entprellung + Gyro-Guard + Dreh-Guard ──► fsm_.onPinch()
```

**Die bi-level Schwelle** (öffnet bei `ENV_ON`, schliesst erst unter `ENV_OFF`) verhindert
Flattern genau an der Schwelle. Die Werte stammen aus einer Messung: Untergrund 0.005–0.02,
echte Pinches 0.045–0.125.

**Der Klassifikator wird nur bei offenem Gate befragt.** `PinchDetector::tick()` bekommt
ihn als aufrufbares Objekt übergeben und ruft ihn per Kurzschlussauswertung:

```cpp
const bool hot = envGate_ && ml();
```

Das hat zwei Wirkungen: die Inferenz läuft nicht in jedem Takt, und `PinchDetector` kennt
das Edge-Impulse-SDK nicht — es bleibt damit hardwarefrei.

**Der Gyro-Guard** verwirft Erschütterungen, die während einer heftigen Bewegung
auftreten. **Die Entprellung** (`DEBOUNCE_MS`) verhindert Mehrfachauslösungen. Alle
Bremsen zählen ihre Ablehnungen mit (`nDeb`, `nGyro`, `nTwist` im Teleplot) — sonst wäre
nicht zu unterscheiden, ob eine Flanke gar nicht erkannt oder erkannt und danach verworfen
wurde.

**Der Dreh-Guard** (`PINCH_TWIST_GUARD = 60 °/s`) ist der jüngste der drei und sieht
allein auf die Verdrehung des Unterarms. Er ist nötig, weil die Ein/Aus-Geste sich genau
auf dieser Achse abspielt und dabei das Board erschüttert: in der abgedrehten Haltung
(Scroll-Modus) wurde daraus zuverlässig ein **ungewollter Rechtsklick**, denn dort ist der
Gyro-Guard bewusst ausgesetzt und die Hüllkurve entscheidet allein.

Er ist deshalb der einzige Guard, den der Scroll-Modus **nicht** aufhebt. Das kostet
nichts: gescrollt wird durch Neigen und Schwenken des Arms, nicht durch Verdrehen — der
Rechtsklick im Scroll-Modus fällt also nie in eine Unterarmdrehung, die Schaltgeste
dagegen immer. Die beiden Bewegungen sind auf getrennten Achsen unterscheidbar, und genau
das macht den Guard trennscharf statt bloss restriktiv.

**Nach einem Rechtsklick sperrt der Controller zusätzlich** (`holdOff()`,
`RIGHT_CLICK_HOLDOFF_MS = 700 ms`). Dort lösten bisher fast sicher zwei Klicks aus, und
der zweite schloss das eben geöffnete Kontextmenü wieder: die zwei Haptikpulse dauern
160 ms und das Lösen des Pinch erzeugt einen eigenen Impuls — beides fällt genau ans Ende
der 180 ms Entprellung. Der Linksklick behält die reine Entprellung, damit ein bewusster
Doppelklick aus zwei Pinches möglich bleibt. Die Sperre verlängert das Fenster, sie
verschiebt es nicht: `tLastPinch_` bleibt unberührt, und eine über die ganze Sperre
anhaltende Erschütterung zündet danach nicht nach, weil `wasHot_` durchgehend wahr bleibt.

**Das Einfrieren des Cursors** nach einem Klick ist an das offene `env`-Gate gebunden und
nicht an eine feste Zeit. Der Zeiger ruht also nur, solange die Erschütterung wirklich
anliegt; `FREEZE_MAX_MS = 60` ist nur die Notbremse für ein hängendes Gate. Vorher waren
es pauschal 120 ms nach *jedem* Klick, was sich als "der Cursor ist kurz tot" anfühlte.

### 6.8 Das Modell und seine Kanäle — `PinchFeatures`, `lib/ei-model/`

Das Klassifikationsmodell kommt aus Edge Impulse: 2 Klassen (`non_pinch`, `pinch`),
5 Kanäle, Fenster 40 Werte bei 209 Hz (≈ 191 ms), Rohsignal + 1D-CNN, int8-quantisiert.
Die 209 Hz sind die im Studio hinterlegte Rate des Modells; die Firmware taktet mit
208 Hz, weil sie dem Sensor folgt (Abschnitt 3).

Der Vorgänger arbeitete mit drei Klassen und Spectral-Analysis-Merkmalen (FFT) statt dem
Rohfenster. Der Wechsel senkte den Flash-Bedarf von 18.1 % auf 14.4 %, weil mit den
Spektralmerkmalen auch der FFT-Teil des SDK entfällt — die Wahl des Verarbeitungsblocks
kostet also nicht nur Genauigkeit, sondern auch Programmspeicher.

Die Kanalbelegung steht an **genau einer Stelle** im ganzen Projekt:

```cpp
namespace feat {
    constexpr int CHANNELS = 5;

    inline void pack(const ImuSample& s, float env, float* out) {
        out[0] = env;
        out[1] = s.gyroSum / 100.f;   // auf eine aehnliche Groessenordnung wie g
        out[2] = s.lax;
        out[3] = s.lay;
        out[4] = s.laz;
    }
}
```

Sowohl der Aufnahmemodus in `main.cpp` als auch das Inferenzfenster in `PinchClassifier`
gehen durch diese Funktion. Das ist kein Zufall: stünde die Reihenfolge an zwei Orten,
gäbe eine dort vertauschte Achse weder einen Compiler- noch einen Laufzeitfehler. Das
Modell würde nur still schlechter, und in der Auswertung wäre das von einem Modellproblem
nicht zu unterscheiden.

**Warum die Kanäle 2–4 die lineare und nicht die rohe Beschleunigung führen.** Die rohen
Achsen tragen die Erdbeschleunigung und damit die Handhaltung als Gleichanteil mit sich.
In der Zeige-Haltung liegt sie auf `az ≈ +1`, in der um 90° abgedrehten Haltung auf
`ax ≈ +1`. Drei von fünf Kanälen hätten in der Rechtsklick-Haltung also einen völlig
anderen Gleichanteil als im Training — ein nur in Zeige-Haltung aufgenommenes Modell kann
den Pinch dort praktisch nicht wiedererkennen, und mehr Daten *einer* Haltung hilft
dagegen nicht. Ohne den Gleichanteil ist die Haltung für das Modell unsichtbar, und ein
Datensatz deckt beide ab.

Der generierte Code unter `lib/ei-model/` wird **nicht von Hand editiert**; bei einem
Modell-Update wird der ganze Ordner ersetzt.

### 6.9 Ein/Aus — `TwistToggle`

Ein- und Ausschalten geschieht über eine bewusste Drehung des Unterarms: zügig um rund 45°
drehen und binnen `maxMs` wieder zurück. Das Fenster gilt für die **ganze Bewegung**, ab
dem Beginn der Drehung — wer langsam ausdreht, hat für die Rückkehr weniger Zeit.

**Gemessen wird der Ausschlag, nicht ein Winkel.** `TwistToggle` bekommt die rohe Drehrate
`gy` und integriert sie ab dem Beginn der Bewegung. Der Ausschlag ist damit immer relativ
zu der Haltung, in der der Unterarm gerade ruhte.

Dieselbe Ausdrehung trägt **drei** Bedeutungen, unterschieden allein durch das, was danach
passiert:

```
twist
 90° │      ┌───┐                ┌────────┐              ┌──────────────┐
     │     ╱     ╲              ╱          ╲            ╱                ╲
 70° ├────╱───────╲────────────╱────────────╲──────────╱──────────────────╲───
     │   ╱         ╲          ╱              ╲        ╱                    ╲
 30° ├──╯───────────╰────────╯────────────────╰──────╯──────────────────────╰──
     └──────────────────────────────────────────────────────────────────────────
        │< 1200 ms >│         │  Pinch dazwischen │    │   > 1200 ms (halten)  │
         EIN / AUS              RECHTSKLICK              SCROLL-MODUS
```

| Parameter (`TwistTuning`) | Wert | Bedeutung |
|---|---|---|
| `onDeg` | 45° | so weit muss die Drehung reichen — **ab der Ruhelage**, nicht absolut |
| `backDeg` | 20° | so nah wieder an den Ausgangspunkt |
| `outMaxMs` | 600 ms | so schnell muss der Hinweg gehen |
| `maxMs` | 1200 ms | Fenster für die ganze Bewegung, ab Beginn der Drehung |
| `lockoutMs` | 800 ms | Ruhe nach einem Schaltvorgang |
| `startDps` | 60 °/s | ab hier läuft eine Drehung; die Geste startet auf dieser Flanke |
| `armedMs` | 200 ms | so lange muss der Unterarm vorher geruht haben |
| `stillDps` | 40 °/s | darunter gilt der Unterarm als ruhend |
| `stillMs` | 150 ms | so lange Ruhe, bevor eine Erschütterung als Pinch zählt |

`onDeg` und die Haltungsschwelle `TURN_ON_DEG` (70°) sind **zwei verschiedene Grössen**:
die eine ist ein Ausschlag ab der Ruhelage, die andere ein absoluter Winkel gegen die
Schwerkraft. Ein `static_assert` hält die Geste unter der Haltungsschwelle, damit ein Flick
aus der Zeige-Haltung nicht nebenbei in den Scroll-Modus wechselt.

**Wie weit ausgedreht wird, spielt keine Rolle.** Ein tieferer Scheitelwinkel als vierte
Bedeutung derselben Bewegung — über etwa 150° hinaus als Auslöser für das Ziehen — ist
gebaut und wieder ausgebaut worden. Er funktionierte für sich genommen (`TwistToggle`
merkte sich den grössten erreichten Betrag und entschied erst beim Zurückkommen), aber er
lag auf derselben Achse wie Ein/Aus: eine etwas zu weit geratene Schaltgeste wurde
stillschweigend zum Ziehen, und Ein/Aus verlor am Gerät messbar an Zuverlässigkeit. Das
ist ein brauchbares Beispiel für die Arbeit — eine Geste kann korrekt implementiert und
trotzdem der falsche Entwurf sein, weil sie sich eine Achse mit einer wichtigeren teilt.

**Vom absoluten Winkel zur integrierten Drehrate — eine rückgängig gemachte
Entscheidung.** Ursprünglich las die Geste den Winkel aus dem Madgwick-Filter, mit dem
Argument, er sei *absolut*: aus `gy` integriert driftet die Referenz weg, und "wieder
zurück auf gerade" wäre nach einer Minute nicht mehr dieselbe Bedingung wie am Anfang. Das
Argument ist für sich genommen richtig — und trotzdem war es die falsche Wahl.

Denn der absolute Winkel bringt vier Voraussetzungen mit, und **jede einzelne lässt die
Geste lautlos scheitern**:

| Voraussetzung | Bricht, wenn |
|---|---|
| Schwerkraft als Bezug | der Unterarm steil steht — dann ist die Verdrehung nicht beobachtbar |
| durchgehend geprüftes Waagrecht-Gate | die Drehung `elev` selbst mitschwenkt, weil Board-Y ≠ anatomische Drehachse |
| fester Nullpunkt `TWIST_NEUTRAL_DEG` | die tatsächliche Ruhelage des Unterarms daneben liegt — kalibriert wird nie |
| Einschwingzeit der Lageschätzung | direkt nach dem Aufwachen geflickt wird (β = 0.033 korrigiert nur ~2 °/s) |

Am Gerät zeigte sich das als eine Geste, die "meistens, aber nicht verlässlich" ging. Drei
aufeinanderfolgende Korrekturen an den Verwerfungspfaden brachten jeweils eine Verbesserung
und deckten dabei die nächste Voraussetzung auf — das klassische Zeichen dafür, dass nicht
ein Schwellwert falsch steht, sondern die **Eingangsgrösse** falsch gewählt ist.

Die Integration nimmt alle vier auf einmal weg. Der Drift-Einwand entfällt dabei, weil
**nur während der Geste integriert wird**, also höchstens `maxMs` = 1,2 s lang: 3 °/s
Rest-Nullpunktfehler ergeben 3,6° gegen eine Schwelle von 45°. Ausserhalb der Geste wird
gar nicht integriert, es kann sich also nichts aufsummieren.

Bewusst genommen wird die **rohe** Rate `gy`, nicht die aus der Lageschätzung abgeleitete
Rate von `TwistGuard`. Die beiden Module fragen Verschiedenes: `TwistGuard` muss die
Verdrehung *vollständig* erfassen, weil ihr Rest sonst als Zittern im Cursor landet, und
holt sich dafür die Lage dazu. Die Geste braucht nur den *Ausschlag*; der Schiefstand
zwischen Unterarm- und Platinenachse kostet dort cos(Winkel), bei 15° rund drei Prozent.
Diese drei Prozent gegen vier Totalausfälle einzutauschen war der eigentliche Fehler der
ersten Fassung.

Das Waagrecht-Gate bleibt trotzdem bestehen, aber mit **geänderter Begründung**: es ist
keine Voraussetzung der Messung mehr, sondern nur noch ein Filter gegen Fehlauslösungen bei
hängendem oder senkrecht erhobenem Arm.

**Der Ausschlag allein trägt die Geste nicht.** Das ist die Lehre aus dem Umbau: die
Umstellung auf den relativen Ausschlag machte die Geste *auslösbar*, nahm ihr aber die
*Spezifität*. 70° absolut war eine unnatürliche Haltung, die im Alltag kaum vorkommt — 45°
Unterarmdrehung ab der jeweiligen Ruhelage dagegen ständig. Am Gerät zeigte sich das
prompt: die Maus schaltete sich immer wieder von selbst ein.

Getragen wird die Geste deshalb von **zwei Bedingungen am Start**, nicht von der Amplitude:

| Bedingung | Wert | Was sie ausschliesst |
|---|---|---|
| `level()` | ±30° | alles, was nicht mit waagrecht gehaltenem Arm passiert — hängender Arm, Greifen, Gestikulieren |
| `armedMs` | 200 ms Ruhe davor | Drehungen, die Teil einer grösseren, durchgehenden Bewegung sind |

Beide gelten **nur am Start**. Sie beantworten die Frage „war das überhaupt als Geste
gemeint", und die stellt sich einmal. Für `level()` ist das nicht nur Vereinfachung,
sondern notwendig: die Drehung schwenkt `elev` selbst mit, weil die Platinenachse nicht
exakt auf der anatomischen Drehachse liegt — bei 15° Schiefstand sind das über eine
45°-Drehung rund 11°. Ein durchgehend geprüftes enges Gate würde also ausgerechnet die
eigene Geste abwürgen, und je enger man es stellt, desto häufiger.

Damit hat `LEVEL_MAX_DEG` auch eine **neue Begründung**. Früher war es eine Voraussetzung
der *Messung* (bei steilem Arm ist die Verdrehung aus der Schwerkraft nicht beobachtbar);
seit der Integration ist es ein reiner *Spezifitätsfilter* und durfte deshalb von 50° auf
30° zusammengezogen werden.

Die Zähler `nTwLvl` und `nTwMov` zählen entsprechend **verhinderte Fehlauslösungen** und
sind erwartungsgemäss gross. Sie werden erst verdächtig, wenn eine gemeinte Geste ausbleibt
und einer von beiden dabei hochgeht.

**Der kleinere Ausschlag und was ihn bezahlt.** Ab der Ruhelage gemessen genügen 45° statt
70° — die Geste ermüdet damit spürbar weniger bei wiederholter Benutzung. Auf einem
*uncalibrierten absoluten* Winkel wäre diese Senkung gefährlich gewesen: ruht der Unterarm
bei 30°, läge die Auslöseschwelle nur noch 15° entfernt. Relativ gemessen sind 45° immer
echte 45°. Bezahlt wird die kleinere Bewegung mit `outMaxMs` = 600 ms: der Hinweg muss
zügig sein. Das trennt die Geste von einer beiläufigen Armdrehung, die denselben Winkel
über mehrere Sekunden erreicht — eine Unterscheidung, die der grosse Ausschlag vorher
allein über die Amplitude leistete.

**Der abgesicherte Fehlermodus — und was er zunächst kaputt machte.** Verpasst der
Klassifikator einen Pinch bei 90°, dreht der Nutzer zurück — und die Maus ginge *aus*
statt rechtszuklicken. Ein verfehlter Klick würde zum Abschalten. Deshalb bricht nicht der
*erkannte Klick* die Geste ab, sondern die Erschütterung selbst: das ist vom Modell
unabhängig und fängt auch den verpassten Pinch.

Genau diese Absicherung war aber der Grund, warum das **Ausschalten am Gerät nicht
funktionierte**, während das Einschalten ging. Zwei Asymmetrien wirkten zusammen:

1. Gemeldet wurde nur im **eingeschalteten** Zustand — im Aus-Zustand konnte die
   Absicherung die Einschaltgeste also gar nicht treffen.
2. Im Aus-Zustand (BEREIT) läuft die Schleife mit 52 Hz. Der 30-Hz-Hochpass der
   Hüllkurve liegt dort an der Nyquist-Grenze und liefert praktisch nichts mehr;
   eingeschaltet, mit 208 Hz, spricht er voll an.

Die Ausdrehung erzeugt ihre Erschütterung nämlich **selbst**: am Scheitel läuft der
Unterarm gegen seine anatomische Grenze, und dieser Anschlag hebt die Hüllkurve über
`TWIST_CANCEL_ENV`. Die Geste verwarf sich also im Ausschaltfall regelmässig selbst.

**Die Lösung ist eine zweite Grösse: die Drehrate.** Ein Pinch ist ein Fingerereignis —
der Unterarm steht dabei still. Der Anschlag einer Drehung dagegen fällt mitten in eine
Bewegung von mehreren hundert Grad pro Sekunde. `TwistToggle` bekommt die Rate deshalb
mit herein (dieselbe, die `TwistGuard` aus der Lageschätzung ableitet) und entscheidet
selbst, was eine gemeldete Erschütterung bedeutet:

> Eine Erschütterung verbraucht die laufende Ausdrehung nur, wenn der Unterarm zuvor
> `stillMs` lang unter `stillDps` geblieben ist.

Der Controller *meldet* also nur noch (`reportShock()`), er entscheidet nicht mehr. Beide
gewünschten Eigenschaften bleiben damit erhalten: der Anschlag der eigenen Drehung
zählt nicht, der bewusste Pinch im ausgedrehten Stand zählt weiterhin.

Die Ruhezeit ist nötig, weil die Rate am Umkehrpunkt der Drehung kurz durch null geht —
ein reiner Momentanvergleich würde ausgerechnet dort danebengreifen, wo der Anschlag
liegt. `stillMs` = 150 ms ist länger als dieses Fenster und kürzer als jede Pause, die
vor einem bewussten Rechtsklick liegt.

**Der Ablauf als Zustandsautomat.** Die Erkennung läuft in vier Phasen, im Teleplot-Kanal
`tw` direkt ablesbar:

| `tw` | Phase | Bedeutung |
|---|---|---|
| 0 | `Idle` | Unterarm ruht; es wird nicht integriert |
| 1 | `Out` | dreht heraus, Ausschlag wird mitgeschrieben (`twexc`) |
| 2 | `Back` | `onDeg` erreicht, Unterarm kommt zurück |
| 3 | `Lockout` | eben geschaltet, `lockoutMs` Ruhe |
| 4 | — | Drehung durch einen Pinch verbraucht |

Der Kanal `twexc` zeigt den laufenden Ausschlag in Grad. Er ist das Werkzeug, mit dem sich
`onDeg` einstellen lässt: man sieht damit, wie weit man tatsächlich dreht, statt es zu
schätzen.

`Idle → Out` zündet nur auf der **Flanke** der Drehrate, nicht solange sie über `startDps`
liegt. Ohne das begänne die Rückdrehung sofort eine neue Geste in der Gegenrichtung: nach
einem Abbruch mitten in der Bewegung dreht sich der Unterarm ja weiter. Genau dieser Fall
liess in der Testfassung zwei Prüfungen fehlschlagen — dass die Drehrate am Umkehrpunkt
physikalisch durch null gehen *muss*, ist der Grund, warum die Flanke immer wieder
scharf wird.

Diese Geste ersetzt ein früheres Schütteln, dessen Schwelle (350 °/s) nur knapp über den
rund 250 °/s des normalen Gebrauchs lag.

### 6.10 Scrollen — `ScrollWheel`

Gescrollt wird mit **derselben Armbewegung wie gezeigt**. `OrientationPointer` rechnet
die Bewegung genau einmal in Pixel; im Scroll-Modus geht ihre senkrechte Komponente ins
Rad statt an den Cursor, geteilt durch `SCROLL_PX_PER_STEP`. Das Modul selbst ist nur noch ein
Akkumulator mit Drosselung und Begrenzung — Bruchteile bleiben stehen und laufen auf, so
dass eine langsame Bewegung dieselbe Gesamtzahl Schritte ergibt wie eine schnelle.

Der Vorgänger war ein Joystick auf der *gehaltenen* Armneigung: er brauchte einen
Eintrittswinkel, eine Totzone und eine Sekunde Haltezeit, um überhaupt zu starten. Das war
eine **zweite Bewegungsart** mit eigenem Verhalten, die man zusätzlich lernen musste — und
sie war der Grund, warum das Neigen des Arms den Scroll-Modus beenden konnte, den es
steuern sollte. Der Wechsel auf die Zeigerbewegung ist deshalb keine Vereinfachung der
Implementierung, sondern eine der Bedienung.

`reset()` verwirft den aufgelaufenen Rest ausserhalb der abgedrehten Haltung: er darf nicht
in die nächste Scroll-Sitzung überschwappen und dort sofort einen Schritt auslösen.

### 6.11 Rückstau der Bewegung — `MotionPipeline`

Zwischen dem Zeiger und dem HID liegt ein Puffer, und zwar aus zwei Gründen: ein
HID-Bericht trägt nur ±127 Pixel je Achse, und über BLE geht nur **ein** Bericht je
Verbindungsintervall durch. `MotionPipeline` hält diesen Rückstau.

Sie bekommt pro Takt die fertigen Pixel und ein `MotionTarget` — `Cursor`, `Wheel` oder
`None` — und entscheidet daraus, wohin die Bewegung geht. Für den Cursor summiert sie
auf, schickt alle `MOVE_INTERVAL_US` bis zu `MOVE_MAX_REPORTS` Berichte los und zieht
eine gesendete Strecke **erst dann** ab, wenn das Paket angenommen wurde. Lehnt die
Gegenstelle ab, wird der Rückstau bei `MOVE_BACKLOG_MAX` gekappt: nähme sie minutenlang
nichts an, schösse der Cursor beim Verbinden sonst quer über den Schirm.

Den Sendeweg bekommt sie als **Callable** übergeben, genau wie `PinchDetector` seinen
Klassifikator. Deshalb kennt sie das HID nicht, bindet weder `Arduino.h` noch `config.h`
ein und läuft im PC-Test (`test_motion_pipeline`) — dort ist der Empfänger eine Attrappe,
die sich auf Kommando als „Warteschlange voll" meldet. Genau dieser Fall ist am Gerät
kaum reproduzierbar und war zugleich der, in dem früher Bewegung verlorenging.

### 6.12 Ausgabe — `MouseHID`, `Haptic`

`MouseHID` kapselt die beiden Übertragungswege hinter einer gemeinsamen Schnittstelle:
TinyUSB für USB-HID, bluefruit für BLE-HID, umgeschaltet über `USE_BLE_HID`. Es wird immer
nur einer der beiden Zweige kompiliert — der andere kann unbemerkt abdriften, bis jemand
umschaltet. Sieben `static_assert`s auf die Methodensignaturen fangen das ab.

Besonders heikel ist der Rückgabewert von `move()`: er meldet, ob das Paket angenommen
wurde. Der Aufrufer darf die gesendete Strecke **erst dann** von seinem Rest abziehen,
sonst geht Bewegung verloren, wenn die Warteschlange voll ist.

`Haptic` ist ein kleiner Impuls-Sequenzer: *n* Impulse à `HAPTIC_MS` mit `HAPTIC_GAP_MS`
dazwischen. Ein Impuls bedeutet Linksklick, Haltungswechsel oder Ein/Aus; zwei bedeuten
Rechtsklick. Der Rechtsklick passiert in einer Haltung, in der man den Cursor nicht
beobachtet — er muss unterscheidbar sein.

Der Motor wird **rein digital** geschaltet (`digitalWrite` HIGH/LOW), nicht per PWM. Er
kennt also nur an und aus, und es gibt keine Intensitätsstufe: die gesamte Information
steckt in der *Anzahl* der Impulse und in ihrer Länge. Das ist kein Provisorium — eine
über PWM abgestufte Stärke ist am Unterarm durch die Kleidung hindurch kaum
unterscheidbar, ein zweiter Impuls dagegen zuverlässig.

```
1 Impuls:   ████                 40 ms
2 Impulse:  ████░░░░░░████      130 ms
```

Eine feste Sperrfrist gibt es bewusst nicht. Sie müsste über der Musterdauer von 130 ms
liegen, `DEBOUNCE_MS` steht aber auf 180 ms — ein Doppelklick würde damit nur noch einmal
brummen. Gesperrt ist stattdessen genau, solange ein Muster läuft, plus `HAPTIC_REST_MS`
danach.

### 6.13 Beobachtung — `Telemetry`

Die Teleplot-Ausgabe (`>name:wert`, eine Zeile je Kanal) liegt in einem eigenen Modul.
Es hält `const`-Referenzen auf alle übrigen Module und liest sie von aussen; im Controller
stehen dafür nur noch zwei Zeilen. `Telemetry` ist damit das einzige Modul, das quer durch
das ganze System liest — vertretbar, weil es nirgends eingreift und niemand von ihm
abhängt.

Es enthält bewusst **kein** `#if`. Die Schalter `DEBUG_TELEPLOT` und `DEBUG_SET` werden zu
den Compile-Konstanten `kEnabled` und `kSet`, und die Kanalgruppen hängen an gewöhnlichen
`if`-Abfragen darauf. Der Optimierer entfernt die abgeschalteten Zweige samt ihrer
`Serial`-Aufrufe: gemessen 188 388 gegen 185 620 Bytes Flash, also rund 2.7 kB, die im
Auslieferungsbuild verschwinden.

Vorher standen dieselben 150 Zeilen im Controller, mit sechzehn `#if`-Zweigen und zwei
leeren Zwillingsfunktionen (`void trackEnvPeak(float) {}`), damit der Code ohne
Debug-Schalter überhaupt übersetzt. Das zerriss den Lesefluss genau in den Methoden, die
man als Erstes ansieht.

---

## 7. Die Bedienung als Zustandsautomat

`AirMouseState` hält zwei Achsen: `Power` (Off/On) und `Pose` (Point/Idle/Turned). Eine
dritte für einen Griff gibt es bewusst nicht — siehe unten.

**Die Haltung bestimmt, wohin die Bewegung geht; der Pinch klickt.**

| | Bewegung | Pinch |
|---|---|---|
| **Arm gerade** (`Point`) | Cursor | Linksklick |
| **Arm gedreht** (`Turned`) | Scrollen | Rechtsklick |

**Scrollen braucht keinen Griff.** Abdrehen genügt, ohne Haltezeit und ohne Eintrittsgeste;
`scrolling()` ist reine Ableitung aus der Haltung. Der Pinch bedeutet in beiden Haltungen
dasselbe — "hier klicken", nur die Taste wechselt — und **beide lösen sofort aus**, ohne
Fenster.

| Ereignis | `Off` | `On/Point` | `On/Idle` | `On/Turned` |
|---|---|---|---|---|
| `onPower()` | → On | → Off | → Off | → Off |
| `onPose(p)` | ignoriert | Zeiger zurücksetzen | ggf. Wechsel | ggf. Wechsel |
| `onPinch(armOut)` | ignoriert | Linksklick¹ | ignoriert | Rechtsklick |

¹ nur wenn der Arm **nicht** physisch abgedreht ist — siehe unten.

Die Rückgabe ist immer ein `Actions`-Struct:

```cpp
struct Actions {
    bool click, rightClick;   // beide als ganzer, unteilbarer Klick
    bool resetPointer;        // aufgelaufene Bewegung verwerfen
    bool resetPose;           // Haltungserkennung zuruecksetzen
    uint8_t hapticPulses;     // 0 still, 1 Klick/Haltungswechsel, 2 Rechtsklick
    bool hapticLong;          // Ein/Aus
};
```

**Warum keine Taste einen Zustand braucht.** Beide Klicks sind ganze Klicks: Druck und
Freigabe im selben Aufruf. Damit gibt es keine Tastenmaske, die zwischen zwei Takten
auseinanderlaufen könnte, und keinen Fehlerfall "klebende Taste" — der einzige, der einen
Rechner wirklich unbenutzbar macht.

**Warum es kein Ziehen gibt.** Ohne gedrückt gehaltene Taste fehlt einer Maus mehr, als es
zunächst scheint: Text markieren, ein Fenster verschieben, einen Schieberegler bedienen.
Drei Anläufe sind trotzdem gebaut, am Gerät erprobt und wieder entfernt worden.

Der naheliegende Weg — Kontakt = Taste runter, Lösen = Taste hoch — scheitert am Sensor.
Ein *gehaltener* Pinch ist mit einer IMU grundsätzlich unsichtbar: ein Zustand erzeugt
keine Beschleunigung, und die Hüllkurve ist ein Hochpass ab 30 Hz, der Kontakt und Lösen
als zwei Impulse sieht, aber nichts dazwischen. Der Löse-*Impuls* wäre ein Ereignis und
damit denkbar; am Gerät gemessen ist er aber nur teilweise vorhanden, bei kurzem Pinch gar
nicht, und kaum über dem Rauschen. Ein Tastenzustand, dessen Ende in der Hälfte der Fälle
ausbleibt, ergibt genau die klebende Taste. (Doublepoint verbaut für dieselbe Funktion
einen optischen Sensor und liest damit die Sehnen.)

Der zweite Anlauf, ein **Doppel-Pinch** als Auslöser, brauchte ein Zeitband von 200–350 ms,
das in der Praxis nicht zu treffen war. Der dritte, **Bewegung** als Auslöser, war der
lehrreichste: er funktionierte, verlangte aber, dass die Taste ein Fenster lang unten
bleibt, bevor entschieden wird — und legte damit die Fensterlänge als Verzögerung auf
*jeden* gewöhnlichen Klick. Über BLE, wo ein Bericht je Verbindungsintervall durchgeht,
war das deutlich spürbar. Ein viertes Mal wurde ein tieferer Scheitelwinkel der Ausdrehung
probiert; er lag auf derselben Achse wie Ein/Aus und machte die Schaltgeste unzuverlässig.

Der Klick ist deshalb wieder unteilbar und sofort. Das ist ein brauchbares Beispiel für
die Arbeit: eine Geste kann korrekt implementiert sein und trotzdem der falsche Entwurf,
weil ihr Preis auf einer Funktion liegt, die tausendmal häufiger gebraucht wird.

**`armOut` — kein Linksklick bei abgedrehtem Arm.** `TwistToggle` reagiert sofort auf den
Winkel; die Haltung im Automaten kommt erst durch Glättung, Haltezeit und Bewegungssperre
hindurch, also 400–700 ms später. In diesem Fenster ist der Arm physisch abgedreht,
während `pose()` noch `Point` meldet — ein Pinch dort löste einen **Linksklick** an einer
Cursorposition aus, die der Nutzer nicht sieht. Der Automat unterdrückt den Klick jetzt,
solange der Arm draussen ist. Bewusst unterdrücken und nicht auf Rechtsklick umleiten: gar
nichts zu tun ist wiederherstellbar — man pinscht eine halbe Sekunde später nochmal —, ein
Klick auf der falschen Taste an unsichtbarer Position nicht.

**Zwei Sperren nach einem Klick.** Nach einem Rechtsklick sperrt der Controller die
Klickerkennung für `RIGHT_CLICK_HOLDOFF_MS` — der Impuls beim Lösen der Finger schlösse
sonst das eben geöffnete Kontextmenü wieder. Nach dem langen Ein/Aus-Puls gilt dasselbe:
er dauert mit 200 ms länger als die Entprellung, und ohne die Sperre läse die
Klickerkennung die eigene Vibration als Pinch.

---

## 8. Konfiguration

Es gibt **keine Laufzeit-Konfiguration**. Alle Betriebsarten sind `#define`s am Anfang von
`include/config.h`; umstellen heisst neu bauen und flashen.

| Schalter | Wirkung |
|---|---|
| `COLLECT_MODE` | statt HID nur CSV ausgeben, für die Datenaufnahme |
| `DEBUG_TELEPLOT` | Teleplot-Kanäle senden; kostet Serial-Bandbreite und bremst die Schleife |
| `DEBUG_SET` | welche Kanalgruppe (`DEBUG_ALL`, `DEBUG_PINCH`, `DEBUG_POINT`, `DEBUG_ORIENT`, `DEBUG_ENV`) |
| `USE_ML_PINCH` | ML-Klassifikator gegen reine Schwellwerterkennung |
| `USE_BLE_HID` | BLE gegen USB |
| `BLE_ALWAYS_ON` | Funk auch im Schlaf erreichbar; auf `true` wirbt das Gerät durchgehend weiter und `radioOff()` ist eine leere Hülle — Vorgabe `false`, auffindbar bleibt es über Wake-on-Motion |
| `USE_POSE_MODE` | Haltungserkennung aus → dauerhaft `Point` |
| `USE_ONE_EURO` | 1-Euro-Filter gegen festen Tiefpass |
| `USE_ROLL_COMP` | Roll-Kompensation ein/aus |

Die letzten drei sind nicht bloss Aufräum-Optionen, sondern die **A/B-Vergleiche für die
Evaluation**: sie erlauben, denselben Aufbau mit und ohne einen einzelnen Mechanismus zu
messen.

Alle Zahlenwerte sind `constexpr` in `namespace cfg`. Magic Numbers gehören dorthin, nicht
in die Module.

Die Datei ist nach Themen gegliedert, in der Reihenfolge, in der ein Messwert sie
durchläuft: Sensor → Takt und Betriebszustände → Lage → Handhaltung → Zeigen → Klick →
Scrollen → Rückmeldung → Akku. Jeder Wert trägt seine Einheit und seine Begründung; wo
eine Begründung fehlt, weil der Wert noch nicht am Gerät gemessen ist, steht das
ausdrücklich dabei.

**Eine bewusste Ausnahme:** Module, die auf dem PC testbar sein müssen, dürfen `config.h`
nicht einbinden — die Datei zieht `<Arduino.h>` nach. Ihre Werte stehen deshalb in eigenen
Tuning-Structs: `TwistTuning`, `TwistGuardTuning`, `SleepTuning`, `PoseTuning`,
`PinchTuning`, `ScrollTuning` und `PointerTuning`. Das birgt die Gefahr, dass zwei Orte
dieselbe physikalische Grösse beschreiben und auseinanderdriften — und ein
Auseinanderdriften gäbe keinen Compilerfehler, sondern stilles Fehlverhalten: eine Maus,
die in der abgedrehten Haltung links klickt, ein Gate, das im Untergrund öffnet, ein
Scroll-Modus, der nicht anspringt.

Ein Block von `static_assert`s in `AirMouseController.h` macht daraus einen
Übersetzungsfehler:

```cpp
constexpr TwistTuning kTwistDefaults{};
constexpr PoseTuning  kPoseDefaults{};
static_assert(kTwistDefaults.onDeg == cfg::TURN_ON_DEG, "…dieselbe Schwelle");
static_assert(kTwistDefaults.backDeg < cfg::TURN_OFF_DEG &&
              cfg::TURN_OFF_DEG < cfg::TURN_ON_DEG,     "…Reihenfolge verletzt");
static_assert(kPoseDefaults.turnOnDeg    == cfg::TURN_ON_DEG    &&
              kPoseDefaults.levelMaxDeg  == cfg::LEVEL_MAX_DEG  && /* … */ , "…");
```

Das ist der Preis für die PC-Testbarkeit, und er ist bewusst so bezahlt: die Doppelung ist
sichtbar, überwacht und in `config.h` an jeder betroffenen Gruppe mit
`[auch in <X>Tuning]` markiert. Bei jedem neuen Tuning-Feld gehört die Sperre erweitert.

Ein Sonderfall ist `USE_POSE_MODE`. Er ist ein `#define`, `PoseDetector` kennt `config.h`
aber nicht; der Schalter kommt deshalb als Feld `PoseTuning::classify` in den Konstruktor.
Das ist trotzdem keine Laufzeit-Konfiguration — der Wert steht beim Übersetzen fest und
reist nur einen Konstruktor weit. Der Gewinn: „Haltungserkennung aus" ist damit selbst
prüfbar geworden, und die Prüfung deckt genau die Feinheit ab, die man dabei falsch machen
kann (die Winkel müssen weiterlaufen, nur die Klassifikation steht still — sonst sähe die
Ein/Aus-Drehgeste die Drehung nie).

---

## 9. Betriebszustände und Stromsparen

Bis hierhin war die Firmware immer "an", solange sie lief: Ein/Aus war eine Eigenschaft
der Bedienung (`AirMouseState`), keine der Hardware. Legte man die Maus über die Drehgeste
ab, tastete die IMU trotzdem mit 208 Hz weiter, Madgwick rechnete weiter, und BLE blieb
verbunden — nur die Wirkung fiel weg. Ein abgelegtes Gerät verbrauchte damit fast so viel
wie eines in Gebrauch. Dieser Abschnitt beschreibt, was dagegen eingebaut wurde, und warum
in dieser Reihenfolge.

### 9.1 Der grösste Hebel: die Schleife schläft zwischen den Takten

Vor dieser Änderung kehrte `loop()` einfach zurück, wenn der nächste Takt noch nicht fällig
war. Der Arduino-Kern ruft `loop()` dann sofort wieder auf — der Cortex-M4F lief also
durchgehend mit 64 MHz und tat in rund 97 % der Zeit nichts. Das war der grösste einzelne
Verbraucher im ganzen Gerät, unabhängig vom Betriebszustand.

```cpp
while ((int32_t)(nextSample_us - micros()) > 0) delay(1);
```

`delay()` ruft intern `vTaskDelay`, und mit `configUSE_TICKLESS_IDLE` schläft der
FreeRTOS-Kern für diese Zeit tatsächlich, statt nur den Aufrufer zu blockieren.

Die erste Fassung dieser Schleife wartete die letzte Millisekunde noch in einer leeren
`while`-Schleife ab, mit der Begründung, die FreeRTOS-Auflösung von 1 ms sei für einen
Takt von 4808 µs zu grob. **Diese Begründung war falsch**, und der Whole-Branch-Review hat
sie aufgedeckt. `micros()` ist auf diesem Kern gar keine Mikrosekunden-Uhr: `delay.h`
liefert `dwt_enabled() ? (DWT->CYCCNT / 64) : tick2us(xTaskGetTickCount())`, und
`dwt_enable()` wird nirgends aufgerufen — weder vom Kern, noch von `SystemInit` (das
setzt `TRCENA` nur unter `ENABLE_SWO`/`ENABLE_TRACE`, beide undefiniert), noch von diesem
Projekt. `micros()` steppt also im FreeRTOS-Tick von 1024 Hz, in Schritten von rund
977 µs. Die Warteschleife pollte damit **dieselbe Uhr, auf die `vTaskDelay` schläft** —
sie konnte gar nichts feiner auflösen und hat nur 1–2 ms je Takt mit 64 MHz verbrannt.
Beide Fassungen treffen exakt denselben Tick; die neue schläft dabei.

Diese Änderung wirkt in **jedem** Zustand, in dem das Gerät tatsächlich läuft (AKTIV wie
BEREIT), und kostet nichts am Verhalten. Zur Kontrolle dienen zwei Teleplot-Kanäle: `ovr`
zählt ganz verpasste Takte, und `late` gibt die tatsächliche Verspätung jedes Takts in µs
aus. `late` ist der aussagekräftigere von beiden — `ovr` schlägt erst bei **zwei**
verpassten Takten an (9616 µs in AKTIV, 38462 µs in BEREIT), weil `nextSample_us` beim
Überlauftest bereits weitergestellt ist. Die Werte von `late` kommen in Stufen von rund
977 µs statt in glatten Mikrosekunden — diese Quantisierung ist zugleich der direkte
Messbeleg für die eben beschriebene Tick-Auflösung.

**Die Compiler-Flags dagegen sind der kleinste Posten.** `-O2` (statt `-O1`), `-DNDEBUG`
und `-ffunction-sections`/`-fdata-sections`/`-Wl,--gc-sections` in `platformio.ini` stehen
zwar unter denselben Stromspar-Änderungen, wirken aber anders: `-O2` verkürzt die Wachzeit
pro Takt etwas, weil schnellerer Code schneller wieder schlafen geht, aber der Effekt ist
klein gegen eine Schleife, die vorher gar nicht schlief. `--gc-sections` wirft ungenutzten
Code aus dem Binary — das spart Flash, nicht Strom im Betrieb, und wird deshalb auch nicht
als Stromsparmassnahme geführt. `-Ofast` oder `-flto` wären hier falsch: Ersteres bricht
mit `-ffast-math` die IEEE-Semantik, auf die sich die Quaternion-Normierung im
Madgwick-Filter verlässt, Letzteres wäre bei einem grossen, generierten SDK ein Fehlerort,
der sich kaum zuordnen liesse.

### 9.2 Drei Zustände: AKTIV, BEREIT, SCHLAF

Zusätzlich zur Ein/Aus-Achse in `AirMouseState` gibt es drei Hardware-Zustände. Sie liegen
darüber, nicht darin — der Automat weiss nichts von IMU-Rate oder Funk, das verwaltet
`SleepPolicy` zusammen mit dem Controller:

| Zustand | Bedingung | IMU | Funk | Schleifentakt |
|---|---|---|---|---|
| **AKTIV** | Maus eingeschaltet | 208 Hz, Madgwick, ML | verbunden | `cfg::SAMPLE_INTERVAL_US` (208 Hz) |
| **BEREIT** | ausgeschaltet, aber bewegt | 52 Hz, Madgwick, Drehgeste | verbunden | `cfg::READY_INTERVAL_US` (52 Hz) |
| **SCHLAF** | 60 s ohne Bewegung | nur Beschleunigungssensor, Wake-on-Motion | aus | `loop()` suspendiert |

`SleepPolicy` ist nach demselben Muster gebaut wie die übrigen Erkenner: hardwarefrei,
ohne `config.h` und `<Arduino.h>`, ihre Parameter stehen in `SleepTuning` und sie liefert
nur ein Ereignis (`SleepEvent::PowerOff`/`GoToSleep`) zurück, statt selbst etwas
abzuschalten:

```cpp
SleepEvent tick(bool mouseOn, float gyroSum, uint32_t now_ms) {
    if (gyroSum >= t_.stillDps) tQuiet_ = now_ms;
    ...
    if (mouseOn) {
        if ((now_ms - tQuiet_) < t_.offAfterMs) return SleepEvent::None;
        tQuiet_ = now_ms;
        return SleepEvent::PowerOff;
    }
    if (!wants_ && (now_ms - tQuiet_) >= t_.sleepAfterMs) { ... }
}
```

**Bewegung heisst hier `gyroSum` über einer Schwelle, nicht Beschleunigung.** Eine ruhig
gehaltene, aber getragene Hand liefert konstant 1 g Erdbeschleunigung und sähe für einen
Beschleunigungs-Schwellwert aus wie Stillstand — genau das soll aber nicht als Ruhe zählen.

Aus demselben Grund schläft das Gerät **nie direkt aus AKTIV ein**: sonst könnte die Maus
mitten im Gebrauch verschwinden, während man den Cursor nur ruhig auf einem Ziel hält —
dort ist `gyroSum` nämlich klein. Diese Begründung trägt aber nur über Sekunden, nicht über
Stunden. In einer ersten Fassung setzte `tick()` den Ruhe-Zeitpunkt `tQuiet_` zurück,
solange `mouseOn` wahr war; damit war der eingeschaltete Zustand eine Sackgasse, aus der
das Gerät von allein nie wieder herausfand. Wer die Maus eingeschaltet ablegte, liess sie
mit 208 Hz Takt und laufendem Funk durchlaufen, bis der Akku leer war.

Jetzt folgt `tQuiet_` allein der Drehrate, und der eingeschaltete Zustand hat eine zweite,
deutlich längere Schwelle: nach `offAfterMs` (5 min) ohne Bewegung meldet die Politik
`PowerOff`. Der Controller schaltet daraufhin über `fsm_.onPower()` ab — derselbe Weg wie
die Drehgeste, mit langem Brummer, zurückgesetzter Haltung und IMU auf 52 Hz. Von dort
übernimmt die kurze Ruhezeit, und weitere 60 s später geht das Gerät schlafen.

Dass `tQuiet_` beim Abschalten **neu** anläuft, ist dabei keine Kosmetik. `Haptic` schaltet
den Motor in `trigger()` auf HIGH und erst in `update()` wieder auf LOW; ginge das Gerät im
selben Takt schlafen, würde `update()` nie wieder aufgerufen und der Motor des langen
Ein/Aus-Impulses liefe weiter. Die 60 s BEREIT geben ihm reichlich Takte, um fertig zu
werden.

**Das Aufwecken übernimmt die IMU selbst**, ohne dass der Mikrocontroller pollen muss. Vor
dem Schlaf konfiguriert `ImuReader::enableWakeOnMotion()` den LSM6DS3 so, dass eine
Beschleunigungsänderung über `WAKE_UP_THS` (6 Bit, ein Schritt entspricht bei ±4 g rund
62 mg; Startwert 2 ≈ 125 mg) den Pin `PIN_LSM6DS3TR_C_INT1` auf High zieht. `main.cpp`
hängt daran nur eine minimale ISR:

```cpp
static void onMotion() { resumeLoop(); }
```

Sie tut absichtlich nur eines. I²C-Zugriffe und BLE haben in einer Interrupt-Routine
nichts verloren — alles Weitere geschieht erst in der Task, sobald sie nach
`suspendLoop()` wieder läuft: `AirMouseController::onWake()` konfiguriert die IMU neu,
schaltet den Funk wieder an und erhöht kurzzeitig das Madgwick-Beta. Die Neuausrichtung
des Schleifentakts folgt erst danach, in `main.cpp` selbst — siehe Abschnitt 9.3.

**Zwischen dem Scharfstellen und dem Einschlafen liegt ein Rennen**, das der
Whole-Branch-Review gefunden hat. `attachInterrupt()` und `suspendLoop()` sind zwei
getrennte Anweisungen. Fällt die INT1-Flanke dazwischen, ruft die ISR
`xTaskResumeFromISR` auf eine Task auf, die noch gar nicht suspendiert ist — und
`vTaskSuspend`/`vTaskResume` **zählen nicht**. Das Resume verpufft, die Task suspendiert
sich unmittelbar danach trotzdem, und die Bewegung, die hätte wecken sollen, ist verloren.
Mit `RISING` und ohne Verriegelung gibt es auch keine zweite Flanke, auf die man hoffen
könnte: das Gerät bliebe schlafen, bis es zufällig erneut bewegt wird.

Der Ausweg sind zwei zusammengehörende Änderungen. `enableWakeOnMotion()` setzt in
`TAP_CFG1` zusätzlich das LIR-Bit (`0x81` statt `0x80`), womit INT1 stehen bleibt, bis
`WAKE_UP_SRC` gelesen wird — aus der flüchtigen Flanke wird ein gehaltener Pegel. Und
`main.cpp` prüft diesen Pegel, bevor es sich schlafen legt:

```cpp
attachInterrupt(digitalPinToInterrupt(PIN_LSM6DS3TR_C_INT1), onMotion, RISING);
if (digitalRead(PIN_LSM6DS3TR_C_INT1) == LOW) suspendLoop();
```

INT1 ist bei diesem Baustein aktiv High (`CTRL3_C.H_LACTIVE` bleibt auf der Vorgabe 0),
`LOW` heisst also zuverlässig „es steht nichts an". Steht doch etwas an, wird der
Schlafschritt einfach übersprungen und der Weck-Pfad läuft sofort durch. Damit ist auch
die Reihenfolge in `disableWakeOnMotion()` tragend geworden: erst die Wegleitung auf INT1
kappen, dann `WAKE_UP_SRC` lesen, um die Verriegelung zu lösen. Umgekehrt könnte zwischen
Lesen und Abschalten ein neues Ereignis den Pegel erneut setzen und stehen lassen — INT1
läge dann dauerhaft hoch, und die nächste Pegelprüfung sähe ein Ereignis, das keines ist.

### 9.3 Die Lage nach dem Aufwachen

Zwei Dinge sind beim Aufwachen kurzzeitig falsch, und beide hängen an derselben Ursache:
Während des Schlafs bekommt nichts neue Samples.

**Die Lageschätzung ist veraltet.** Madgwick lief zuletzt vor bis zu 60 Sekunden, und der
Filter startet ohnehin bei der Einheitsquaternion — also der Annahme, das Board läge flach.
Die Ein/Aus-Drehgeste hängt über das Waagrecht-Gate an diesem Winkel und könnte
danebengreifen oder von allein auslösen.

Die erste Fassung liess den Filter dorthin *konvergieren*: `onWake()` setzte ein stark
erhöhtes Beta (`MADGWICK_BETA_FAST = 0.5` statt `0.033`, rund 28,6 °/s Nachführung), und
nach `settleMs` stellte `SleepEvent::Settled` das normale Beta zurück. Das Fenster musste
mit 1500 ms grosszügig bemessen sein, um auch eine Lage einzufangen, die nach einem Schlaf
mit langsamer Armdrehung um ein Vielfaches danebenliegt — und solange es lief, blieb
`TwistToggle` gesperrt. Die Maus war nach dem Aufwecken also anderthalb Sekunden lang nicht
einschaltbar.

Das war von Anfang an der Umweg. Die Schwerkraft ist keine Grösse, die man einlaufen lassen
muss: **ein einziger Messwert des Beschleunigungssensors sagt, wo unten ist.** Er legt die
Lage bis auf die Drehung um die Lotrechte fest, und die ist ohne Magnetometer weder
beobachtbar noch wird sie von `upX/upY/upZ` gelesen. `MadgwickAHRS::seedFromAccel()` setzt
die Quaternion deshalb direkt:

```cpp
// Kürzeste Drehung, die "oben" von der Z-Achse auf den gemessenen Vektor bringt.
float q0 = 1.f + az, q1 = ay, q2 = -ax;
```

Für normierte Eingaben hat dieser Vektor die Länge `2·(1+az)`; die Drehachse ist damit nur
bei `az = -1` unbestimmt — beim exakten Kopfstand, wo dann jede Achse quer dazu taugt.

Bleibt die Frage, welchen Messwert man nimmt. Aufgeweckt wird das Gerät durch *Bewegung*,
und während einer Bewegung misst der Sensor Schwerkraft **plus** Beschleunigung; der Vektor
zeigt dann nicht nach unten. Der Controller löst das ohne Wartezeit: solange kein Messwert
brauchbar aussieht, wird die Lage in **jedem** Takt neu gesetzt.

```cpp
void seedOrientation(const ImuSample& s) {
    ahrs_.seedFromAccel(s.ax, s.ay, s.az);
    oriented_ = fabsf(s.accMag - 1.f) <= cfg::SEED_ACC_TOL;
}
```

Während der Bewegung ist das nur eine grobe Schätzung — aber immer noch näher an der
Wahrheit als die Flach-Annahme, von der der Filter sonst losläuft. Und in dem Takt, in dem
der Arm ruhig wird, rastet die Schätzung exakt ein und `oriented_` bleibt stehen. Ein
Zeitfenster gibt es nicht mehr: die Drehgeste hängt jetzt an `oriented_`, ist also verfügbar,
sobald ein einziger Messwert nach Schwerkraft aussieht.

Damit entfallen `MADGWICK_BETA_FAST`, `SleepTuning::settleMs`, `SleepEvent::Settled` und
`SleepPolicy::settling()` ersatzlos — ein ganzer Mechanismus, der nur existierte, weil die
Lage eingeschwungen statt gesetzt wurde.

Die Quaternion-Konvention ist dabei die Stelle, an der man sich lautlos vertut: ein
Vorzeichenfehler ergibt eine plausibel aussehende, aber gespiegelte Lage. `test_madgwick_seed`
prüft deshalb alle sechs Achsenlagen einzeln gegen `upX/upY/upZ`, dazu eine schräge, den
Nullvektor und die Zusicherung, dass der gesetzte Wert unter dem laufenden Filter stehen
bleibt.

**Der Schleifentakt liegt beliebig weit in der Vergangenheit.** `nextSample_us` wurde vor
dem Schlaf zuletzt gesetzt; ohne Korrektur müsste die Schleife nach dem Aufwachen erst
tausende Overrun-Korrekturen abarbeiten, bevor sie wieder im Takt ist. `main.cpp` setzt
`nextSample_us` deshalb direkt auf `micros() + tickUs` — mit der *aktuellen* Taktlänge,
nicht mit `cfg::SAMPLE_INTERVAL_US`: der Automat ist nach dem Aufwachen in BEREIT, der IMU
läuft also schon auf 52 Hz, und mit der falschen Konstante läge der nächste erwartete
Zeitpunkt vor dem nächsten tatsächlichen Sample.

Eine Nebenbedingung dabei betrifft die Millisekunde, mit der `SleepPolicy` und der Rest
des Controllers rechnen. Sie kam ursprünglich aus `micros() / 1000`, und das war ein
Fehler, den erst der Whole-Branch-Review sichtbar gemacht hat: `micros()` läuft bei 2³²
über, der **Quotient** daraus also schon bei 4 294 967. Vorzeichenlose
Differenzarithmetik (`now_ms - tPrev`) ist aber nur dann gültig, wenn der Zähler an der
Breite seines Typs überläuft — und genau diese Differenz bildet jeder Zeitgeber im
Projekt. Alle 71,58 Minuten hätte `SleepPolicy` in einem einzigen Takt eine Lücke von
rund 4,29 · 10⁹ ms gesehen und das Gerät sofort schlafen gelegt; `TwistToggle` hätte ein
falsches `Held` melden, `PoseDetector` eine wartende Haltung augenblicklich übernehmen
können.

Der Controller rechnet deshalb mit `millis()`. Beide Uhren stammen aus derselben Quelle,
dem FreeRTOS-Tick mit 1024 Hz, laufen also im Gleichtakt — gemischt wird nichts, und
`now_us` behält seine eigenen Verwendungen (`nextSample_us`, das Berichtsintervall zum
Host, das Debug-Intervall). `millis()` springt erst beim Überlauf des 32-Bit-Ticks
zurück, nach rund 48,5 Tagen. Streng genommen ist auch das kein sauberer 2³²-Überlauf —
der Sprung liegt bei 4 194 304 000 ms —, aber statt alle 71,58 Minuten nur noch alle
sieben Wochen, und das ist für ein Akkugerät mit Schlafzyklen ohne praktische Bedeutung.

### 9.4 Warum kein System OFF

Der nRF52840 kennt einen noch tieferen Ruhezustand, System OFF, der praktisch keinen Strom
zieht. Er wurde bewusst nicht verwendet: System OFF verwirft das RAM, ein Aufwachen daraus
ist ein Neustart. Das kostet zwei Dinge, die über Minuten aufgebaut wurden — den gelernten
Gyro-Nullpunkt (`ImuReader`s Bias-Schätzer müsste wieder von vorn einschwingen) und die
bestehende BLE-Verbindung (die Gegenstelle müsste neu koppeln). Der Gewinn ist dabei nach
der Schätzung im Entwurf klein: System OFF zieht rund 0.4 µA gegenüber rund 30 µA im
gewählten Schlafzustand — knapp 30 µA Unterschied, ungemessen wie alle Stromwerte an
dieser Stelle (Abschnitt 13). Bei einem Gerät, das ohnehin wöchentlich geladen wird,
rechtfertigt das diesen Preis nicht.

**Ohne Messung ist jede Ersparnis eine Behauptung.** `lib/Battery/` liest die Akkuspannung
über den eingebauten Spannungsteiler der XIAO (`VBAT_ENABLE` schaltet ihn nur für die
Messung zu, sonst zöge er dauerhaft Strom) und macht sie als Teleplot-Kanal `vbat`
sichtbar. Damit lässt sich die Entladekurve — vor und nach diesem Umbau, unter gleichem
Nutzungsmuster — direkt am Gerät aufnehmen, ohne Zusatzmessgerät. Der Umrechnungsfaktor
(`BATTERY_VOLTS_PER_LSB`) ist ein Startwert aus dem typischen Teilerverhältnis der Platine
und gehört gegen eine Multimetermessung kalibriert, bevor die Kurve etwas beweist —
Einzelheiten dazu im Messplan in `TODO.md`.

---

## 10. Testbarkeit

Elf Testdateien laufen **auf dem PC**, ohne Mikrocontroller, ohne Board, in gut zehn
Sekunden:

| Test | prüft | Include-Pfad |
|---|---|---|
| `test_state_machine` | Zustandsautomat | `-I lib/AirMouseState` |
| `test_arm_orientation` | Winkelableitung | `-I lib/ArmOrientation` |
| `test_pose_detector` | Haltungserkennung | `-I lib/PoseDetector -I lib/AirMouseState` |
| `test_twist_toggle` | Ein/Aus-Drehgeste | `-I lib/TwistToggle` |
| `test_twist_guard` | Verdrehungsbremse | `-I lib/TwistGuard` |
| `test_pinch_detector` | Klick-Entscheidung | `-I lib/PinchDetector` |
| `test_scroll_wheel` | Rad-Ausgabe | `-I lib/ScrollWheel` |
| `test_motion_pipeline` | Rückstau zum HID | `-I lib/MotionPipeline -I lib/ScrollWheel` |
| `test_one_euro` | 1-Euro-Filter | `-I lib/Filters` |
| `test_pinch_features` | Kanalbelegung des Modells | `-I lib/ImuReader -I lib/PinchFeatures` |
| `test_sleep_policy` | Schlaf-Entscheidung | `-I lib/SleepPolicy` |
| `test_madgwick_seed` | Lage aus der Schwerkraft setzen | `-I lib/MadgwickAHRS` |

Alle auf einmal:

```powershell
pio test -e native        # erwartet: "12 test cases: 12 succeeded"
```

Einzeln, ohne PlatformIO — die Zeile dafür steht im Kopf jeder Testdatei:

```powershell
g++ -std=c++14 -Wall -Wextra -I lib/AirMouseState -o "$env:TEMP\fsm.exe" test/test_state_machine/test_state_machine.cpp
& "$env:TEMP\fsm.exe"        # erwartet: "… Pruefungen, 0 Fehler", Exit 0
```

Das ist der schnellste Weg, eine Änderung zu prüfen — **vor** dem Firmware-Build, nicht
danach. Der Firmware-Build dauert wegen des Edge-Impulse-SDK ein bis zwei Minuten.

Die Tests benutzen **keinen Testrahmen**. Jede Datei ist ein eigenständiges Programm mit
eigenem `main()`, das seine Prüfungen zählt und über den Exit-Code meldet, ob es
durchgelaufen ist. Genau deshalb funktioniert der g++-Einzeiler oben: es gibt keine zweite,
an PlatformIO gebundene Fassung derselben Prüfungen. Für `pio test` übersetzt ein kleiner
eigener Runner (`test/test_custom_runner.py`, `test_framework = custom`) die Ausgabe in
PlatformIO-Testfälle.

Alle elf hängen daran, dass der jeweilige Header **hardwarefrei** bleibt. Das ist keine
Nebenbedingung, sondern der Grund für den Zuschnitt der Module. Fällt ein `#include
<Arduino.h>` hinein, ist der Test weg — und die `-I`-Liste in `[env:native]` bricht den
Build, was genau die Absicht ist.

Die Tests prüfen bewusst *Eigenschaften* statt Implementierungsdetails.
`test_pinch_features` etwa stellt dieselbe Bewegung mit zwei um 90° verdrehten
Gravitationslagen nebeneinander und verlangt ein identisches Modellfenster — es testet
also genau die Eigenschaft, um deretwillen die Kanäle gravitationsfrei sind.
`test_pinch_detector` zählt mit, wie oft der Klassifikator befragt wurde, und prüft damit
die Kurzschlussauswertung, die verhindert, dass in jedem Takt eine Inferenz läuft.

`PoseDetector`, `PinchDetector` und `ScrollWheel` sind erst in der Aufräumrunde dazu
gekommen. Sie zogen vorher `config.h` und damit `<Arduino.h>` herein; ihre Werte stehen
jetzt in `PoseTuning`/`PinchTuning`/`ScrollTuning` (Abschnitt 8). Damit hat **jedes Modul
mit Entscheidungslogik einen Test.**

Nicht auf dem PC prüfbar bleiben die Treiber und der Zusammenbau: `ImuReader`, `MouseHID`,
`CollectLink`, `Haptic`, `Battery`, `PinchClassifier` (Hardware bzw. Edge-Impulse-SDK) und
`AirMouseController` selbst, der gerade die Verdrahtung aller anderen ist. Diese werden
über Kompilieren und Messen am Gerät verifiziert.

Die PC-seitige BLE-Brücke (Abschnitt 11) folgt demselben Muster mit anderen Mitteln. Ihr
heikler Teil ist nicht das Funken, sondern das Zusammensetzen der Zeilen aus BLE-Brocken
und die Lückenerkennung — beides reine Bytelogik. Sie hat deshalb einen eingebauten
Selbsttest im selben Ausgabeformat wie die C++-Tests, der ohne Gerät, ohne COM-Port und
ohne die BLE-Bibliothek läuft:

```powershell
py tools/ble_collect_bridge.py --selftest      # erwartet: "... Pruefungen, 0 Fehler"
```

Er prüft unter anderem den Fall, der die Brücke im Betrieb einmal zum Einfrieren gebracht
hat: dass ein COM-Port, der nichts annimmt, den Aufrufer **nicht** aufhält.

---

## 11. Datenaufnahme für das Modell

Mit `COLLECT_MODE true` gibt `main.cpp` statt HID-Bewegungen nur CSV aus — fünf Werte pro
Zeile, in der Reihenfolge aus `feat::pack()`. **Wohin die Zeile geht, entscheidet
`CollectLink`** (`lib/CollectLink/`): über USB-Serial oder, mit `COLLECT_OVER_BLE`, über
den Nordic UART Service. Zwei Zweige hinter einer gemeinsamen Schnittstelle, wie bei
`MouseHID`, von `static_assert`s zusammengehalten. Das Zahlenformat ist in beiden Zweigen
dasselbe, damit Aufnahmen über Kabel und über Funk im selben Datensatz liegen dürfen.

### Warum kabellos

Am USB-Kabel zieht jede Armbewegung. Für `idle` und `negative` ist das gleichgültig, für
`pinch` nicht: das sind genau die Aufnahmen, in denen der Arm ruhig gehalten wird, und ein
Kabelzug erzeugt dort dieselbe Art kurzer Erschütterung, die das Modell lernen soll. Der
Datensatz bekäme eine Störgrösse, die es am fertigen Gerät nicht gibt.

### Warum eine Brücke auf dem PC

`edge-impulse-data-forwarder` öffnet ausschliesslich einen **echten** COM-Port — er benutzt
Node-`serialport`, kennt also weder TCP noch Named Pipes — und BLE erscheint unter Windows
nicht als COM-Port. Dazwischen steht deshalb `tools/ble_collect_bridge.py`: sie liest den
Nordic UART Service und schreibt in ein virtuelles COM-Paar (com0com), aus dessen anderem
Ende der Forwarder liest. Für ihn sieht das aus wie ein serielles Gerät; der Ablauf im
Studio bleibt unverändert.

Gesucht wird das Gerät über die **UUID** des Dienstes, nicht über den Namen. Das ist kein
Detail: die HID-Firmware wirbt unter demselben `BLE_NAME`, und im Werbepaket steht ohnehin
kein Name — er sitzt in der Scan Response, weil die 128-Bit-UUID allein schon 18 der 31
Bytes belegt.

### Zwei Vorkehrungen der Aufnahme

**Die LED warnt vor gedehnten Fenstern.** Verpasst die Schleife während der Aufnahme einen
Abtastschritt, ist das Fenster zeitlich gedehnt und der Datensatz unbrauchbar, ohne dass
man es der CSV ansieht. Eine Textmeldung würde den Datenstrom stören, also geht die Meldung
auf die eingebaute LED und bleibt bis zum Reset an. Über Funk kommt ein zweiter Grund dazu:
eine Zeile, die der Sendeweg nicht angenommen hat. (`LED_BUILTIN` ist beim XIAO aktiv LOW.)

**Der Hüllkurven-Kanal behält vier Nachkommastellen**, die übrigen vier nur drei. `env`
bewegt sich zwischen 0.005 und 0.125, und die Schwelle `ENV_ON = 0.035` liegt genau dort —
bei drei Stellen bliebe am unteren Ende eine einzige signifikante Ziffer. Die anderen
Kanäle liegen unter der Sensorauflösung; die eingesparten Bytes senken die Last auf dem
Sendeweg um rund ein Fünftel, was über Funk mehr zählt als über USB.

### Der Zähler vor jeder Zeile

Über Funk trägt jede Zeile zusätzlich einen 16-Bit-Zähler. Er ist das Gegenstück zur LED
für den Weg *nach* dem Gerät: geht ein BLE-Brocken verloren, fehlen Samples, und dem CSV
sieht man auch das nicht an. Die Brücke prüft die Lückenlosigkeit und streift den Zähler
wieder ab.

Sie **bricht dabei nicht ab**, sondern meldet Uhrzeit und Zahl der fehlenden Samples und
läuft weiter. Der erste Entwurf brach ab, und das war falsch herum gedacht: eine Lücke
macht nicht die Sitzung unbrauchbar, sondern die Aufnahme, die gerade lief. Ein Abbruch
kostet dagegen alles, was danach noch aufgenommen worden wäre. Die Uhrzeit in der Meldung
ist das, was man wirklich braucht — sie sagt, welche Aufnahme zu wiederholen ist. Am Ende
der Sitzung steht eine Bilanz mit Anteil der fehlenden Samples.

Der Zähler läuft **auch ohne Verbindung** weiter. Sonst wäre ein Verbindungsabbruch mitten
in der Aufnahme unsichtbar: die Zeilen davor und danach wären lückenlos durchnummeriert,
obwohl Sekunden fehlen.

### Was die Messung der Funkstrecke ergab

Erforderlich sind rund 8 kB/s (208 Zeilen zu ~38 Bytes). Eine Messung über 25 Sekunden
zeigte, dass das zunächst nicht hielt:

| | erste Messung | nach der Korrektur |
|---|---|---|
| Zeilen je Sekunde | 171 | **208** |
| zerrissene Zeilen | 930 | **0** |
| Lücken | ständig | **keine** |

Der Hinweis lag in den Paketgrössen: neben 744 vollen Paketen zu 244 Bytes kamen rund 500
**Kleinpakete zu 23–29 Bytes** an. Die Ursache steht im Kern der Bluefruit-Bibliothek.
`BLEUart::bufferTXD()` legt den Sendepuffer mit **genau einer MTU** an — 247 Bytes, rund
sechs Zeilen — und leert ihn selbst, sobald 244 Bytes darin stehen. Er ist dadurch dauernd
fast voll: die nächste Zeile passt nur noch teilweise hinein, der Rest fährt als eigenes
Kleinpaket, und scheitert dieses, kommt beim Empfänger eine **halbe Zeile** an. Jeder Rest
verbrennt zusätzlich einen Sendeplatz, weshalb die Strecke nur rund 6,5 statt möglicher
12 kB/s trug.

Dazu kommt eine zweite Wirkung: `notify()` wartet über ein Semaphor auf einen freien
Sendeplatz und hält dabei die 208-Hz-Schleife an. Ein blockierter Sendeaufruf äussert sich
deshalb nicht als Lücke, sondern als **verpasster Takt** — niedrigere Rate bei lückenlosem
Zähler. Die beiden Fehlerbilder auseinanderzuhalten ist der eigentliche Nutzen des Zählers.

`CollectLink` legt den Sendepuffer deshalb selbst an, mit **2048 statt 247 Bytes**. Damit
passt jede Zeile am Stück hinein, die Kleinpakete entfallen, und jedes Paket fährt voll.

### Der Rest der letzten Sitzung

Ein zweiter Befund kam erst mit dem grösseren Puffer zum Vorschein: beim Trennen bleibt sein
Inhalt stehen und fährt beim nächsten Verbinden als erstes hinaus — Zeilen mit
Zählerwerten der **vorigen** Sitzung. Gemessen waren es fünf Zeilen, gefolgt von einem
Sprung von 8566. Die Brücke meldete das als Lücke, gleich in der ersten Sekunde jeder Sitzung.

`CollectLink` räumt den Puffer deshalb auf der **Flanke** von "nicht verbunden" nach
"verbunden", nicht im Zustand: geräumt wird einmal je Sitzung, sonst wäre der eben
geschriebene Inhalt in jedem Takt wieder weg.

### Warum die Brücke einen eigenen Schreib-Faden hat

com0com blockiert `write()`, solange niemand das andere Ende des Paars liest — und das tut
der Forwarder erst, wenn er gestartet ist. Direkt aus dem BLE-Rückruf geschrieben friert das
die ganze Ereignisschleife ein: keine Statusausgabe mehr, und die Benachrichtigungen stauen
sich unsichtbar im Speicher. Das Schreiben läuft deshalb in einem eigenen Faden mit
begrenzter Warteschlange; was nicht hineinpasst, wird **verworfen und gezählt**, statt
alles anzuhalten. Die Anzeige `verworfen` steigt so lange, bis der Forwarder liest — und
ist danach die dritte Kontrolle neben Rate und Lücken.

### Die Frequenz wird vorgegeben

Der Forwarder schätzt die Abtastrate sonst aus dem Datenstrom. Über BLE kommen die Zeilen
in Schüben alle ~8 ms an, die Schätzung schwankt entsprechend, und ein im Studio
hinterlegtes 205 oder 211 Hz wäre derselbe Fehler wie die 209 Hz des ersten Datensatzes.
Die wahre Rate ist eine Konstante der Firmware, keine Messgrösse, also wird sie mit
`--frequency 208` gesetzt. Die Kontrolle wandert damit zur Brücke: ihr Mittelwert bei
`Zeilen/s` muss ~208 sein.

Die Mengen je Klasse und der Vergleich eigener gegen gemischten Datensatz stehen in
[EdgeImpulse.md](EdgeImpulse.md), der Ablauf am Aufnahmetag in [Aufnehmen.md](Aufnehmen.md).

---

## 12. Wiederkehrende Entwurfsentscheidungen

Fünf Muster ziehen sich durch den Code und erklären die meisten Einzelentscheidungen.

**Eine Grösse, ein Ort.** Die Kanalreihenfolge des Modells steht nur in `feat::pack()`.
Die Winkel werden einmal pro Takt im Controller berechnet und verteilt, statt dass jedes
Modul sie sich selbst holt. Doppelte Wahrheiten fallen nicht durch Compilerfehler auf,
sondern werden still falsch.

**Namen müssen halten, was sie versprechen.** `rollDeg`/`pitchDeg` hiessen nicht, was sie
bei dieser Einbaulage bedeuteten, und kosteten eine lange Fehlersuche. Sie wurden durch
`twistDeg`/`elevDeg` ersetzt und die alten Methoden ganz entfernt, damit niemand
zurückfällt. Aus demselben Grund heisst der abgedrehte Zustand `Turned` und nicht mehr
`Scroll`: er trägt inzwischen zwei Bedeutungen.

**Keine Geste wartet auf ein zweites Ereignis.** Jede Erkennung, die auf eine Bestätigung
wartet, legt ihre Fensterlänge als Verzögerung auf *jeden* gewöhnlichen Fall. Der Pinch
wird deshalb sofort gemeldet; Links- und Rechtsklick unterscheiden sich über die gehaltene
Haltung und nicht über die Anzahl der Pinches.

**Bezugswerte kommen nicht aus dem unruhigsten Moment.** Der Nullpunkt der Verdrehung
wurde früher beim Einschalten kalibriert — also direkt nach der Einschaltgeste, wenn die
Lageschätzung am stärksten gestört ist. Jedes Einschalten ergab einen anderen Bezug und
alle aufgezeichneten Werte waren untereinander unvergleichbar. Er ist jetzt fest
(`TWIST_NEUTRAL_DEG`): das Board sitzt immer gleich am Arm, der Nullpunkt ist eine
Eigenschaft der Bauform und keine der einzelnen Sitzung. Die **Geste** braucht seit dem
Umbau auf den integrierten Ausschlag überhaupt keinen Nullpunkt mehr — sie misst gegen die
Haltung, in der der Unterarm gerade ruhte. Übrig bleibt der feste Bezug nur für die
Haltungserkennung, wo er ohnehin nur zwischen zwei weit auseinanderliegenden Zonen
unterscheiden muss.

**Verworfenes bleibt begründet stehen — aber nicht im Code.** Ausgebaute Ansätze (das
Ziehen, das Schütteln, der Scroll-Joystick, die Einschalt-Kalibrierung) sind in diesem
Dokument mit ihrer Begründung vermerkt; ohne diese Notiz baut man sie beim nächsten Mal
wieder ein. Im Code selbst stehen sie nicht: dort erklärt ein Kommentar den heutigen
Stand, nicht den Weg dorthin.

---

## 13. Offene Punkte

Die Einstellwerte im Code sind **begründete Ausgangspunkte, keine Messergebnisse**.
`TODO.md` führt den Messplan in der Reihenfolge, in der die Schritte aufeinander aufbauen,
und das Protokoll für die Neuaufnahme des Trainingsdatensatzes.

Zwei Dinge sind erst nach dieser Neuaufnahme bewertbar: die Zuverlässigkeit des
Rechtsklicks, und die Haltungsunabhängigkeit der gravitationsfreien Kanäle. Bis dahin
lassen sich Haltungen, Drehgeste, Scroll und der gesamte Cursor-Pfad mit
`USE_ML_PINCH false` prüfen — die reine Schwellwerterkennung kennt keine Haltung und
funktioniert deshalb in beiden gleich.
