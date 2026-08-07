# Der Programmcode der Air Mouse

Beschreibung des Aufbaus und der Funktionsweise der Firmware. Stand: Commit `a3a848c`.

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
test/                     PC-Tests (laufen ohne Hardware)
docs/                     Diese Datei, Entwurfs- und Planungsdokumente
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
    const uint32_t now_us = micros();
    if ((int32_t)(now_us - nextSample_us) < 0) return;   // noch nicht dran

    nextSample_us += cfg::SAMPLE_INTERVAL_US;            // naechster Takt
    if (Takt verpasst) { neu ausrichten; overruns++; }

    const ImuSample s = imu.read(cfg::DT);

    app.update(s, cfg::DT, now_us);                      // alles Weitere
}
```

Entscheidend ist, dass die Verarbeitung mit einer **festen Schrittweite** `cfg::DT`
rechnet und nicht mit der tatsächlich verstrichenen Zeit. Alle Filter und das Zeitfenster
des Klassifikators setzen eine konstante Abtastrate voraus. Nach einer Stockung wird der
Takt deshalb *neu ausgerichtet*, statt die Rückstände nachzuholen — ein nachgeholter
Doppelschritt würde jeden Filter kurzzeitig verfälschen.

Die Schrittweite ist nicht frei wählbar: `cfg::SAMPLE_INTERVAL_US = 4785 µs` entspricht
209 Hz und ist die Abtastrate, mit der das Edge-Impulse-Modell trainiert wurde. Ein
`static_assert` in `PinchClassifier.h` erzwingt beim Kompilieren, dass beide Zahlen
übereinstimmen. Läuft die Firmware schneller oder langsamer, sieht der Klassifikator ein
zeitlich gestauchtes oder gedehntes Fenster und wird still schlechter, ohne dass ein
Fehler auftritt.

Der Zähler `overruns` zählt verpasste Takte. Er ist im Teleplot als Kanal `ovr` sichtbar
und im Aufnahmemodus zusätzlich an die eingebaute LED gekoppelt (Abschnitt 11).

---

## 4. Die vier Schichten

Der Code ist in vier Rollen aufgeteilt, und die Trennung wird strikt eingehalten:

| Schicht | Rolle | Module |
|---|---|---|
| **Treiber** | Hardware ansprechen | `ImuReader`, `MouseHID`, `Haptic`, `PinchClassifier`, `Battery` |
| **Ableitung** | Messwerte in Grössen umrechnen | `MadgwickAHRS`, `ArmOrientation`, `Filters/` |
| **Erkenner** | aus Grössen Ereignisse machen | `PoseDetector`, `TwistToggle`, `TwistGuard`, `PinchDetector`, `SleepPolicy` |
| **Zustand** | entscheiden, was ein Ereignis bedeutet | `AirMouseState` |
| **Ausführung** | Entscheidungen umsetzen | `AirMouseController`, `OrientationPointer`, `ScrollJoystick` |

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
`PoseDetector`, `ScrollJoystick`, `OrientationPointer`, `SleepPolicy` und `Filters/` steht
kein `#include <Arduino.h>`, kein Bluetooth und nichts aus dem Edge-Impulse-SDK. Genau das
macht sie auf dem PC testbar.

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
   3. env > ENV_ON  ──► TwistToggle.cancel()       (nur im Ein-Zustand)
        │
   4. TwistToggle.tick(relTwist, level)
        │      ├── Toggle ──► fsm_.onPower()       ──┐
        │      └── Held   ──► fsm_.onTwistHeld()   ──┤
        │                                            │
   5. wenn eingeschaltet:                            ├──► apply()
        │      fsm_.onPose(Haltung)                ──┤     (einzige Stelle
        │      PinchDetector ──► fsm_.onPinch()    ──┘      mit Wirkung)
        │
   6. laufende Taetigkeit:
           pointing()  ──► OrientationPointer ──► HID-Bewegung
           scrolling() ──► ScrollJoystick     ──► HID-Scroll
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
Vermutung "der Effekt zeigt sich in den Spitzen" ist also widerlegt.

Der grössere Hebel gegen das Zittern ist allerdings `BETA`. Physiologischer Tremor liegt
bei 8–12 Hz mit 0.1–0.5° Amplitude, also rund 12 °/s Drehrate; der Mittelwert von
`|12·sin(2π·10t)|` ist 7.6 °/s, ob geglättet oder nicht. Mit `BETA = 0.55` ergab das eine
mittlere Grenzfrequenz von 5.1 Hz und damit kaum Dämpfung bei 10 Hz, mit `BETA = 0.2` sind
es 2.5 Hz.

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
anzuheben kostet feine Bewegung direkt: 2.5 °/s entsprechen bei `SENS_X = 110` schon
275 px/s, die stufenlos abgezogen werden.

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
| `Idle` | Arm nicht waagrecht — nichts passiert |
| `Turned` | Hand abgedreht — Pinch = Rechtsklick, nach einer Sekunde zusätzlich Scroll |

Wichtig: **`Idle` ist keine Zone der Verdrehung.** Es ist ausschliesslich das Ergebnis des
Waagrecht-Gates. Hängt der Arm herunter oder ist er angehoben, ist keine der Haltungen
gemeint — dann liefert der Detektor `Idle`, egal wie die Hand verdreht ist. Diese
Bedingung wird absolut gegen die Schwerkraft gemessen und nicht relativ zum Einschalten:
"waagrecht" soll waagrecht heissen, sonst kalibriert man sich die Bedingung durch
Einschalten in schiefer Haltung gleich weg.

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
                        Entprellung + Gyro-Guard ──► fsm_.onPinch()
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
auftreten. **Die Entprellung** (`DEBOUNCE_MS`) verhindert Mehrfachauslösungen. Beide
zählen ihre Ablehnungen mit (`nDeb`, `nGyro` im Teleplot) — sonst wäre nicht zu
unterscheiden, ob eine Flanke gar nicht erkannt oder erkannt und danach verworfen wurde.

**Das Einfrieren des Cursors** nach einem Klick ist an das offene `env`-Gate gebunden und
nicht an eine feste Zeit. Der Zeiger ruht also nur, solange die Erschütterung wirklich
anliegt; `FREEZE_MAX_MS = 60` ist nur die Notbremse für ein hängendes Gate. Vorher waren
es pauschal 120 ms nach *jedem* Klick, was sich als "der Cursor ist kurz tot" anfühlte.

### 6.8 Das Modell und seine Kanäle — `PinchFeatures`, `lib/ei-model/`

Das Klassifikationsmodell kommt aus Edge Impulse: 3 Klassen (`idle`, `negative`, `pinch`),
5 Kanäle, Fenster 41 Werte bei 209 Hz (≈ 196 ms), int8-quantisiert.

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

Ein- und Ausschalten geschieht über eine bewusste Drehung des Unterarms: gerade halten,
um rund 90° abdrehen, innerhalb einer Sekunde zurück.

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
        │< 1000 ms >│         │  Pinch dazwischen │    │   > 1000 ms (halten)  │
         EIN / AUS              RECHTSKLICK              SCROLL-MODUS
```

| Parameter (`TwistTuning`) | Wert | Bedeutung |
|---|---|---|
| `onDeg` | 70° | ab hier gilt der Arm als abgedreht |
| `backDeg` | 30° | erst hier gilt er wieder als gerade |
| `maxMs` | 1000 ms | länger draussen = keine Schaltgeste mehr |
| `lockoutMs` | 800 ms | Ruhe nach einem Schaltvorgang |

Die Rückkehrschwelle (30°) ist strenger als die Haltungs-Hysterese (55°). Sonst würde ein
halbherziges Zurückwackeln auf 54° die Maus abschalten.

**Warum die Lageschätzung und nicht die integrierte Drehrate.** Der Winkel aus dem
Madgwick-Filter ist *absolut*. Aus `gy` integriert driftete die Referenz weg, und die
Bedingung "wieder zurück auf gerade" wäre nach einer Minute nicht mehr dieselbe wie am
Anfang. Die Kehrseite: bei senkrecht gehaltenem Unterarm ist die Verdrehung aus der
Schwerkraft **nicht beobachtbar**. Das Waagrecht-Gate ist deshalb keine Bequemlichkeit
mehr, sondern eine Voraussetzung der Geste — `TwistToggle` verlangt `level` durchgehend.

**Der abgesicherte Fehlermodus.** Verpasst der Klassifikator einen Pinch bei 90°, dreht
der Nutzer zurück — und die Maus ginge *aus* statt rechtszuklicken. Ein verfehlter Klick
würde zum Abschalten. Deshalb bricht nicht der *erkannte Klick* die Geste ab, sondern das
**geöffnete `env`-Gate**: tritt während der Ausdrehung überhaupt eine Erschütterung über
`ENV_ON` auf, ist die Ausdrehung verbraucht und schaltet nicht mehr. Diese Bedingung ist
vom Modell unabhängig.

Diese Geste ersetzt ein früheres Schütteln, dessen Schwelle (350 °/s) nur knapp über den
rund 250 °/s des normalen Gebrauchs lag.

### 6.10 Scrollen — `ScrollJoystick`

Im Scroll-Modus zählt nicht die Drehrate, sondern der **gehaltene Neigungswinkel**
relativ zum Eintrittswinkel. Weiter geneigt heisst schneller scrollen, zurück in die Mitte
heisst Stopp. Das ist ein Positions- und kein Ratensignal und driftet deshalb nicht weg.

Eingang ist die Armneigung `elev`, nicht die Handverdrehung — die hat den Modus ja gerade
ausgewählt und stünde während des Scrollens konstant bei rund 90°.

`inDeadzone()` meldet, ob die Neigung innerhalb der Totzone steht, also gerade nicht
gescrollt wird. Der Zustandsautomat entscheidet daran, ob ein Pinch in der abgedrehten
Haltung als Rechtsklick gilt (Abschnitt 7).

### 6.11 Ausgabe — `MouseHID`, `Haptic`

`MouseHID` kapselt die beiden Übertragungswege hinter einer gemeinsamen Schnittstelle:
TinyUSB für USB-HID, bluefruit für BLE-HID, umgeschaltet über `USE_BLE_HID`. Es wird immer
nur einer der beiden Zweige kompiliert — der andere kann unbemerkt abdriften, bis jemand
umschaltet. Fünf `static_assert`s auf die Methodensignaturen fangen das ab.

Besonders heikel ist der Rückgabewert von `move()`: er meldet, ob das Paket angenommen
wurde. Der Aufrufer darf die gesendete Strecke **erst dann** von seinem Rest abziehen,
sonst geht Bewegung verloren, wenn die Warteschlange voll ist.

`Haptic` ist ein kleiner Impuls-Sequenzer: *n* Impulse à `HAPTIC_MS` mit `HAPTIC_GAP_MS`
dazwischen. Ein Impuls bedeutet Linksklick, Haltungswechsel oder Ein/Aus; zwei bedeuten
Rechtsklick. Der Rechtsklick passiert in einer Haltung, in der man den Cursor nicht
beobachtet — er muss unterscheidbar sein.

```
1 Impuls:   ████                 40 ms
2 Impulse:  ████░░░░░░████      130 ms
```

Eine feste Sperrfrist gibt es bewusst nicht. Sie müsste über der Musterdauer von 130 ms
liegen, `DEBOUNCE_MS` steht aber auf 180 ms — ein Doppelklick würde damit nur noch einmal
brummen. Gesperrt ist stattdessen genau, solange ein Muster läuft, plus `HAPTIC_REST_MS`
danach.

---

## 7. Die Bedienung als Zustandsautomat

`AirMouseState` hält zwei Achsen: `Power` (Off/On) und `Pose` (Point/Idle/Turned), dazu
ein Flag `scrollOn_`, ob der Scroll-Joystick zugeschaltet ist.

| Ereignis | `Off` | `On/Point` | `On/Idle` | `On/Turned` |
|---|---|---|---|---|
| `onPower()` | → On | → Off | → Off | → Off |
| `onPose(p)` | ignoriert | Zeiger zurücksetzen | ggf. Wechsel | Joystick aus |
| `onTwistHeld()` | ignoriert | ignoriert | ignoriert | Joystick an, Neigung nullen |
| `onPinch(…)` | ignoriert | Linksklick¹ | ignoriert | Rechtsklick² |

¹ nur wenn der Arm **nicht** physisch abgedreht ist — siehe unten.
² nur wenn nicht gerade gescrollt wird — siehe unten.

Die Rückgabe ist immer ein `Actions`-Struct:

```cpp
struct Actions {
    bool click, rightClick;      // Maustasten
    bool resetPointer;           // aufgelaufene Bewegung verwerfen
    bool enterScroll;            // Joystick auf aktuelle Neigung nullen
    bool resetPose;              // Haltungserkennung zuruecksetzen
    uint8_t hapticPulses;        // 0 = still, 1 = normal, 2 = Rechtsklick
};
```

Zwei Bedingungen in `onPinch(bool scrollIdle, bool armOut)` verdienen eine Erklärung, weil
beide aus Fehlern entstanden sind:

**`scrollIdle` — kein Klick mitten im Scrollen.** Wer scrollt, kippt den Arm; ein Klick im
Lauf wäre nicht vorhersehbar. Die Bedingung lautet aber `!scrollOn_ || scrollIdle`, nicht
einfach `scrollIdle`. Der Grund: `ScrollJoystick::dead_` wird nur berechnet, solange
gescrollt wird. Nach dem Zurückdrehen friert der Wert ein — ein veraltetes `false` hätte
den schnellen Rechtsklick nach *jeder* Scroll-Sitzung stillschweigend getötet, bis man die
Haltung eine volle Sekunde hält. Läuft der Joystick nicht, gibt es nichts, womit ein Klick
kollidieren könnte.

**`armOut` — kein Linksklick bei abgedrehtem Arm.** `TwistToggle` reagiert sofort auf den
Winkel; die Haltung im Automaten kommt erst durch Glättung, Haltezeit und Bewegungssperre
hindurch, also 400–700 ms später. In diesem Fenster ist der Arm physisch abgedreht,
während `pose()` noch `Point` meldet — ein Pinch dort löste einen **Linksklick** an einer
Cursorposition aus, die der Nutzer nicht sieht. Der Automat unterdrückt den Klick jetzt,
solange der Arm draussen ist. Bewusst unterdrücken und nicht auf Rechtsklick umleiten: gar
nichts zu tun ist wiederherstellbar — man pinscht eine halbe Sekunde später nochmal —, ein
Klick auf der falschen Taste an unsichtbarer Position nicht.

**Der Scroll-Joystick kommt nicht mit der Haltung**, sondern erst über `onTwistHeld()`
nach einer Sekunde. Sonst würde jede Ein/Aus-Geste nebenbei ein Stück weit scrollen.

**Es gibt bewusst keine Grab-Achse und kein Ziehen.** Zwei Anläufe dazu wurden wieder
ausgebaut: der Doppel-Pinch brauchte ein Wartefenster, das als Verzögerung auf *jedem*
gewöhnlichen Klick lag; Pinch-plus-Abdrehen kollidierte mit dem Rechtsklick auf derselben
Geste. Solange nichts eine Taste gedrückt hält, wäre ein Zustand dafür nur Ballast.

---

## 8. Konfiguration

Es gibt **keine Laufzeit-Konfiguration**. Alle Betriebsarten sind `#define`s am Anfang von
`include/config.h`; umstellen heisst neu bauen und flashen.

| Schalter | Wirkung |
|---|---|
| `COLLECT_MODE` | statt HID nur CSV ausgeben, für die Datenaufnahme |
| `DEBUG_TELEPLOT` | Teleplot-Kanäle senden; kostet Serial-Bandbreite und bremst die Schleife |
| `DEBUG_SET` | welche Kanalgruppe (`DEBUG_PINCH`, `DEBUG_POINT`, `DEBUG_ORIENT`, `DEBUG_ALL`) |
| `USE_ML_PINCH` | ML-Klassifikator gegen reine Schwellwerterkennung |
| `USE_BLE_HID` | BLE gegen USB |
| `USE_POSE_MODE` | Haltungserkennung aus → dauerhaft `Point` |
| `USE_ONE_EURO` | 1-Euro-Filter gegen festen Tiefpass |
| `USE_ROLL_COMP` | Roll-Kompensation ein/aus |

Die letzten drei sind nicht bloss Aufräum-Optionen, sondern die **A/B-Vergleiche für die
Evaluation**: sie erlauben, denselben Aufbau mit und ohne einen einzelnen Mechanismus zu
messen.

Alle Zahlenwerte sind `constexpr` in `namespace cfg`. Magic Numbers gehören dorthin, nicht
in die Module.

**Eine bewusste Ausnahme:** Module, die auf dem PC testbar sein müssen, dürfen `config.h`
nicht einbinden — die Datei zieht `<Arduino.h>` nach. Ihre Werte stehen deshalb in eigenen
Tuning-Structs: `TwistTuning`, `TwistGuardTuning`, `PointerTuning`, `SleepTuning`. Das
birgt die Gefahr, dass zwei Orte dieselbe physikalische Grösse beschreiben und
auseinanderdriften. Für die
Verdrehungsschwelle, die sowohl `TwistToggle` als auch `PoseDetector` benutzen, fängt ein
`static_assert` in `AirMouseController.h` das ab:

```cpp
constexpr TwistTuning kTwistDefaults{};
static_assert(kTwistDefaults.onDeg == cfg::TURN_ON_DEG, "…dieselbe Schwelle");
static_assert(kTwistDefaults.backDeg < cfg::TURN_OFF_DEG &&
              cfg::TURN_OFF_DEG < cfg::TURN_ON_DEG,     "…Reihenfolge verletzt");
```

Ein Auseinanderdriften gäbe sonst keinen Compilerfehler, sondern eine Maus, die in der
abgedrehten Haltung links klickt.

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
int32_t restUs = (int32_t)(nextSample_us - micros());
if (restUs > cfg::SLEEP_MIN_REST_US) {
    delay((restUs - 1000) / 1000);
    restUs = (int32_t)(nextSample_us - micros());
}
while ((int32_t)(nextSample_us - micros()) > 0) { }   // letzte Millisekunde exakt
```

`delay()` ruft intern `vTaskDelay`, und mit `configUSE_TICKLESS_IDLE` schläft der
FreeRTOS-Kern für diese Zeit tatsächlich, statt nur den Aufrufer zu blockieren. Die letzte
Millisekunde wird bewusst *nicht* verschlafen: die FreeRTOS-Auflösung beträgt 1 ms, der
Takt von 4785 µs muss aber auf wenige Mikrosekunden genau bleiben — sonst driftet das
ML-Fenster genau wie bei einem verpassten Takt. Diese eine Änderung wirkt in **jedem**
Zustand, in dem das Gerät tatsächlich läuft (AKTIV wie BEREIT), und kostet nichts am
Verhalten — der Overrun-Zähler `ovr` bleibt die Kontrolle, ob der Takt trotzdem hält.

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
| **AKTIV** | Maus eingeschaltet | 208 Hz, Madgwick, ML | verbunden | `cfg::SAMPLE_INTERVAL_US` (209 Hz) |
| **BEREIT** | ausgeschaltet, aber bewegt | 52 Hz, Madgwick, Drehgeste | verbunden | `cfg::READY_INTERVAL_US` (52 Hz) |
| **SCHLAF** | 60 s ohne Bewegung | nur Beschleunigungssensor, Wake-on-Motion | aus | `loop()` suspendiert |

`SleepPolicy` ist nach demselben Muster gebaut wie die übrigen Erkenner: hardwarefrei,
ohne `config.h` und `<Arduino.h>`, ihre Parameter stehen in `SleepTuning` und sie liefert
nur ein Ereignis (`SleepEvent::GoToSleep`/`Settled`) zurück, statt selbst etwas
abzuschalten:

```cpp
SleepEvent tick(bool mouseOn, float gyroSum, uint32_t now_ms) {
    if (mouseOn || gyroSum >= t_.stillDps) tQuiet_ = now_ms;
    ...
    if (!wants_ && !settling_ && (now_ms - tQuiet_) >= t_.sleepAfter) { ... }
}
```

**Bewegung heisst hier `gyroSum` über einer Schwelle, nicht Beschleunigung.** Eine ruhig
gehaltene, aber getragene Hand liefert konstant 1 g Erdbeschleunigung und sähe für einen
Beschleunigungs-Schwellwert aus wie Stillstand — genau das soll aber nicht als Ruhe zählen.
Aus demselben Grund schläft das Gerät **nur aus BEREIT ein, nie aus AKTIV**: `tick()`
setzt den Ruhe-Zeitpunkt `tQuiet_` bereits zurück, solange `mouseOn` wahr ist, unabhängig
von `gyroSum`. Sonst könnte die Maus mitten im Gebrauch verschwinden, während man den
Cursor nur ruhig auf einem Ziel hält — dort ist `gyroSum` nämlich klein.

**Das Aufwecken übernimmt die IMU selbst**, ohne dass der Mikrocontroller pollen muss. Vor
dem Schlaf konfiguriert `ImuReader::enableWakeOnMotion()` den LSM6DS3 so, dass eine
Beschleunigungsänderung über `WAKE_UP_THS` (6 Bit, ein Schritt entspricht bei ±4 g rund
62 mg; Startwert 2 ≈ 125 mg) den Pin `PIN_LSM6DS3TR_C_INT1` auf High zieht. `main.cpp`
hängt daran nur eine minimale ISR:

```cpp
static void onMotion() { resumeLoop(); }
```

Sie tut absichtlich nur eines. I²C-Zugriffe und BLE haben in einer Interrupt-Routine
nichts verloren — alles Weitere (IMU neu konfigurieren, Funk wieder anschalten,
Takt neu ausrichten) geschieht in `AirMouseController::onWake()`, sobald die Task nach
`suspendLoop()` wieder läuft.

### 9.3 Einschwingen nach dem Aufwachen

Zwei Dinge sind beim Aufwachen kurzzeitig falsch, und beide hängen an derselben Ursache:
Während des Schlafs bekommt nichts neue Samples.

**Die Lageschätzung ist veraltet.** Madgwick lief zuletzt vor bis zu 60 Sekunden; die
Ein/Aus-Drehgeste hängt aber genau an diesem Winkel und könnte danebengreifen oder von
allein auslösen. `onWake()` setzt deshalb kurzzeitig ein stark erhöhtes Beta
(`MADGWICK_BETA_FAST = 0.5` statt `0.033`), bis die Lage nach `settleMs` (300 ms) wieder
auf die Schwerkraft eingerastet ist (`SleepEvent::Settled` stellt das normale Beta
zurück). Für dieses Fenster bleibt `TwistToggle` gesperrt — nicht über einen neuen
Mechanismus, sondern über denselben Weg, mit dem auch eine nicht-waagrechte Haltung die
Geste verwirft: `tick()` bekommt `level = false` übergeben, solange `sleep_.settling()`
wahr ist.

**Der Schleifentakt liegt beliebig weit in der Vergangenheit.** `nextSample_us` wurde vor
dem Schlaf zuletzt gesetzt; ohne Korrektur müsste die Schleife nach dem Aufwachen erst
tausende Overrun-Korrekturen abarbeiten, bevor sie wieder im Takt ist. `main.cpp` setzt
`nextSample_us` deshalb direkt auf `micros() + tickUs` — mit der *aktuellen* Taktlänge,
nicht mit `cfg::SAMPLE_INTERVAL_US`: der Automat ist nach dem Aufwachen in BEREIT, der IMU
läuft also schon auf 52 Hz, und mit der falschen Konstante läge der nächste erwartete
Zeitpunkt vor dem nächsten tatsächlichen Sample.

Eine Nebenbedingung dabei: Die Millisekunde, mit der `SleepPolicy` und der Rest des
Controllers rechnen, kommt aus `micros() / 1000` und nicht aus `millis()`. Beide Uhren
laufen unterschiedlich lange, bevor sie überlaufen — die aus `micros()` abgeleitete alle
71,58 Minuten, `millis()` erst nach 49,7 Tagen. Würde man beide mischen, sähe
`SleepPolicy` nach dem ersten Überlauf eine riesige statt einer kleinen Zeitdifferenz.

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

Sieben Testdateien laufen **auf dem PC**, ohne Mikrocontroller, ohne Board, in Sekunden:

| Test | prüft | Include-Pfad |
|---|---|---|
| `test_state_machine.cpp` | Zustandsautomat | `-I lib/AirMouseState` |
| `test_arm_orientation.cpp` | Winkelableitung | `-I lib/ArmOrientation` |
| `test_twist_toggle.cpp` | Ein/Aus-Drehgeste | `-I lib/TwistToggle` |
| `test_twist_guard.cpp` | Verdrehungsbremse | `-I lib/TwistGuard` |
| `test_one_euro.cpp` | 1-Euro-Filter | `-I lib/Filters` |
| `test_pinch_features.cpp` | Kanalbelegung des Modells | `-I lib/ImuReader -I lib/PinchFeatures` |
| `test_sleep_policy.cpp` | Schlaf-Entscheidung | `-I lib/SleepPolicy` |

```powershell
g++ -std=c++14 -Wall -Wextra -I lib/AirMouseState -o "$env:TEMP\fsm.exe" test/test_state_machine.cpp
& "$env:TEMP\fsm.exe"        # erwartet: "… Pruefungen, 0 Fehler", Exit 0
```

Das ist der schnellste Weg, eine Änderung zu prüfen — **vor** dem Firmware-Build, nicht
danach. Der Firmware-Build dauert wegen des Edge-Impulse-SDK mehrere Minuten.

Alle sieben hängen daran, dass der jeweilige Header **hardwarefrei** bleibt. Das ist keine
Nebenbedingung, sondern der Grund für den Zuschnitt der Module. Fällt ein `#include
<Arduino.h>` hinein, ist der Test weg.

Die Tests prüfen bewusst *Eigenschaften* statt Implementierungsdetails.
`test_pinch_features.cpp` etwa stellt dieselbe Bewegung mit zwei um 90° verdrehten
Gravitationslagen nebeneinander und verlangt ein identisches Modellfenster — es testet
also genau die Eigenschaft, um deretwillen die Kanäle gravitationsfrei sind.

Nicht auf dem PC prüfbar sind alle Module, die `<Arduino.h>`, `config.h` oder das
Edge-Impulse-SDK brauchen: `ImuReader`, `MouseHID`, `Haptic`, `PinchClassifier`, `Battery`,
`PoseDetector`, `ScrollJoystick`, `PinchDetector` und der Controller selbst. Diese werden
über Kompilieren und Messen am Gerät verifiziert.

---

## 11. Datenaufnahme für das Modell

Mit `COLLECT_MODE true` gibt `main.cpp` statt HID-Bewegungen nur CSV aus — fünf Werte pro
Zeile, in der Reihenfolge aus `feat::pack()`, mit `edge-impulse-data-forwarder`
eingelesen.

Zwei Vorkehrungen sichern die Aufnahme ab:

**Die LED warnt vor gedehnten Fenstern.** Verpasst die Schleife während der Aufnahme einen
Abtastschritt — etwa weil der USB-Puffer volläuft —, ist das Fenster zeitlich gedehnt und
der Datensatz unbrauchbar, ohne dass man es der CSV ansieht. Eine Textmeldung würde den
Datenstrom stören, also geht die Meldung auf die eingebaute LED und bleibt bis zum Reset
an. Leuchtet sie nach der Aufnahme, wird verworfen. (`LED_BUILTIN` ist beim XIAO aktiv
LOW: `digitalWrite(…, LOW)` schaltet ein.)

**Der Hüllkurven-Kanal behält vier Nachkommastellen**, die übrigen vier nur drei. `env`
bewegt sich zwischen 0.005 und 0.125, und die Schwelle `ENV_ON = 0.035` liegt genau dort —
bei drei Stellen bliebe am unteren Ende eine einzige signifikante Ziffer. Die anderen
Kanäle liegen unter der Sensorauflösung; die eingesparten Bytes senken die Serial-Last um
rund ein Fünftel.

Das vollständige Aufnahmeprotokoll steht in `TODO.md`.

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
Eigenschaft der Bauform und keine der einzelnen Sitzung.

**Verworfenes bleibt begründet stehen.** Ausgebaute Ansätze — das Ziehen, das Schütteln,
die Einschalt-Kalibrierung — sind im Code und in den Dokumenten mit ihrer Begründung
vermerkt. Ohne diese Notiz baut man sie beim nächsten Mal wieder ein.

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
