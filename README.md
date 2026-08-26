# Air Mouse

Firmware einer am Unterarm getragenen Maus. Die Drehbewegung des Arms steuert den Cursor,
ein Zusammendrücken von Daumen und Zeigefinger (ein *Pinch*) löst den Klick aus, und eine
Drehung des Unterarms um seine eigene Achse schaltet zwischen den Betriebsarten. Ausgegeben
wird das als gewöhnliche USB- oder Bluetooth-Maus — das Betriebssystem braucht keinen
Treiber.

Der Pinch wird nicht über einen Schalter erkannt, sondern über die Erschütterung, die er
in der Hand erzeugt: ein Klassifikationsmodell (Edge Impulse, auf dem Mikrocontroller
ausgeführt) entscheidet anhand von Beschleunigung und Drehrate, ob eine Erschütterung ein
Pinch war oder etwas anderes.

Entstanden als Maturaarbeit. Die ausführliche Beschreibung von Aufbau und Begründungen
steht in **[docs/Programmcode.md](docs/Programmcode.md)**.

---

## Aufbau

```
   LSM6DS3 (Accel + Gyro, I2C, 208 Hz)
        |
        v
   +--------------------------------------------------+
   | ImuReader                                        |
   | ein I2C-Burst, Gyro-Nullpunkt, Erdbeschleunigung |
   +--------------------------------------------------+
        |
        |  ImuSample  (ax/ay/az, lax/lay/laz, gx/gy/gz, accMag, gyroSum)
        |
        +----------------------+----------------------+
        v                      v                      v
   +---------------+   +------------------+   +------------------+
   | MadgwickAHRS  |   | VibrationEnvelope|   | Drehraten gx, gz |
   | ArmOrientation|   | 30 Hz -> |.| ->15|   |                  |
   +---------------+   +------------------+   +------------------+
        | twist, elev          | env                 |
        v                      v                     v
   +---------------+   +------------------+   +--------------------+
   | PoseDetector  |   | PinchDetector    |   | OrientationPointer |
   | TwistToggle   |   | + ML-Klassifik.  |   | + TwistGuard       |
   +---------------+   +------------------+   +--------------------+
        |   Ereignisse         |                     |
        +----------+-----------+                     |
                   v                                 |
        +---------------------------+                |
        | AirMouseState             |                |
        | haelt den Zustand und     |                |
        | entscheidet, was ein      |                |
        | Ereignis bedeutet         |                |
        +---------------------------+                |
                   |  Actions                        |
                   v                                 v
        +----------------------------------------------------+
        | AirMouseController::apply()                        |
        | die einzige Stelle mit Seiteneffekten              |
        +----------------------------------------------------+
                   |                        |
                   v                        v
        +---------------------+   +---------------------+
        | MouseHID (USB/BLE)  |   | Haptic (Motor D1)   |
        +---------------------+   +---------------------+
                   |
                   v
              Computer
```

Die wichtigste Regel dahinter: **`AirMouseState` hält den gesamten Zustand und führt selbst
nichts aus.** Jedes Ereignis liefert ein `Actions`-Struct zurück, das beschreibt, *was zu
tun ist*; ausgeführt wird es an genau einer Stelle. Verhält sich das Gerät falsch, gibt es
deshalb nur zwei Möglichkeiten — entweder liefert ein Erkenner das falsche Ereignis, oder
der Automat zieht daraus den falschen Schluss. Der zweite Fall lässt sich auf dem PC in
Sekunden prüfen, der erste am Gerät messen.

---

## Stückliste

| Teil | Anmerkung |
|---|---|
| Seeed XIAO nRF52840 Sense | ARM Cortex-M4F, 64 MHz; LSM6DS3-IMU ist aufgelötet |
| Vibrationsmotor | an Pin `D1`, rein digital geschaltet (kein PWM) |
| LiPo-Akku | am Akku-Anschluss der Platine; Spannung über den eingebauten Teiler messbar |
| Armband / Halterung | trägt die Platine am Unterarm |

Typen, Kapazität und Bezugsquellen sind hier bewusst nicht angegeben — es steht nur, was
sich aus der Firmware belegen lässt.

---

## Bauen und Flashen

Vorausgesetzt wird [PlatformIO](https://platformio.org/). Der PlatformIO-CLI liegt unter
Windows üblicherweise nicht im `PATH`:

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe"

& $pio run                          # bauen
& $pio run -t upload                # flashen
& $pio device monitor -b 115200     # serielle Ausgabe ansehen
```

Der Build dauert wegen des Edge-Impulse-SDK ein bis zwei Minuten und ist sehr gesprächig;
interessant sind nur die RAM/Flash-Zeile und `SUCCESS`.

Flashen über den seriellen Bootloader: die RESET-Taste der Platine zweimal kurz drücken,
bis die LED langsam pulsiert.

### Tests

Alle hardwarefreien Module werden auf dem PC geprüft, ohne Board, in gut zehn Sekunden:

```powershell
& $pio test -e native               # erwartet: 11 test cases: 11 succeeded
```

Das ist der schnellste Weg, eine Änderung zu prüfen — **vor** dem Firmware-Build, nicht
danach. Die Tests benutzen keinen Testrahmen: jede Datei ist ein eigenständiges Programm
mit eigenem `main()` und lässt sich auch einzeln mit einem g++-Aufruf übersetzen (die Zeile
dafür steht im Kopf jeder Testdatei).

### Betriebsarten

Es gibt keine Laufzeit-Konfiguration. Alle Betriebsarten sind `#define`s am Anfang von
[`include/config.h`](include/config.h); umstellen heisst neu bauen und flashen.

| Schalter | Wirkung |
|---|---|
| `COLLECT_MODE` | statt HID nur CSV ausgeben, für die Aufnahme der Trainingsdaten |
| `DEBUG_TELEPLOT`, `DEBUG_SET` | Teleplot-Kanäle senden, gruppenweise wählbar |
| `USE_ML_PINCH` | ML-Klassifikator gegen reine Schwellwerterkennung |
| `USE_BLE_HID` | BLE gegen USB |
| `USE_POSE_MODE` | Haltungserkennung aus → dauerhaft Zeige-Haltung |
| `USE_ONE_EURO` | 1-Euro-Filter gegen festen Tiefpass |
| `USE_ROLL_COMP` | Roll-Kompensation ein/aus |

Die letzten drei sind nicht bloss Aufräum-Optionen, sondern die A/B-Vergleiche für die
Auswertung: sie erlauben, denselben Aufbau mit und ohne einen einzelnen Mechanismus gegen
dieselbe Aufgabe zu messen.

---

## Module

Jedes Modul ist eine header-only Klasse in einem eigenen Ordner unter `lib/`.
**Hardwarefrei** heisst: weder `<Arduino.h>` noch `config.h` noch das Edge-Impulse-SDK —
und damit auf dem PC testbar.

| Modul | Aufgabe | Hardwarefrei | Test |
|---|---|:---:|:---:|
| `ImuReader` | I²C-Burst, Gyro-Nullpunkt, Erdbeschleunigung abziehen | – | – |
| `MadgwickAHRS` | Lageschätzung aus Gyro + Beschleunigung | ✓ | – |
| `ArmOrientation` | daraus die benannten Winkel `twist` und `elev` | ✓ | ✓ |
| `Filters/` | Tief-, Hochpass, Hüllkurve, 1-Euro-Filter | teilweise | ✓ |
| `PoseDetector` | Winkel → Haltung (Zeigen / Idle / Abgedreht) | ✓ | ✓ |
| `TwistToggle` | Ein/Aus-Geste aus der Unterarmdrehung | ✓ | ✓ |
| `TwistGuard` | bremst den Cursor während einer Unterarmdrehung | ✓ | ✓ |
| `PinchDetector` | Schwelle, Entprellung, Gyro-Guard → Klick ja/nein | ✓ | ✓ |
| `PinchClassifier` | kapselt das Edge-Impulse-Modell | – | – |
| `PinchFeatures` | einzige Stelle, an der die Modellkanäle festliegen | ✓ | ✓ |
| `AirMouseState` | Zustandsautomat; entscheidet, was ein Ereignis bedeutet | ✓ | ✓ |
| `OrientationPointer` | Drehraten → Pixel (Deadzone, Filter, Kennlinie) | – | – |
| `ScrollWheel` | Zeigerbewegung → Scroll-Schritte | ✓ | ✓ |
| `MotionPipeline` | Rückstau zum HID: Pakete aufteilen, Weiche Cursor/Rad | ✓ | ✓ |
| `SleepPolicy` | entscheidet, wann das Gerät schlafen geht | ✓ | ✓ |
| `MouseHID` | USB-HID oder BLE-HID hinter einer Schnittstelle | – | – |
| `Haptic` | Impuls-Sequenzer für den Vibrationsmotor | – | – |
| `Battery` | Akkuspannung über den eingebauten Teiler | – | – |
| `AirMouseController` | verdrahtet alles; einzige Stelle mit Seiteneffekten | – | – |
| `Telemetry` | Teleplot-Ausgabe; beobachtet die Module, greift nicht ein | – | – |
| `ei-model/` | generierter Code aus Edge Impulse, nicht von Hand ändern | – | – |

---

## Weiterlesen

| Datei | Inhalt |
|---|---|
| **[docs/Programmcode.md](docs/Programmcode.md)** | Aufbau und Funktionsweise ausführlich, mit den Begründungen hinter den Entscheidungen |
| [docs/EdgeImpulse.md](docs/EdgeImpulse.md) | Anleitung, wie das Klassifikationsmodell aufgenommen und trainiert wird |
| [docs/Aufnehmen.md](docs/Aufnehmen.md) | Kurzanleitung für den Aufnahmetag: Aufnahme über Bluetooth, Schritt für Schritt |
| [include/config.h](include/config.h) | alle Einstellwerte, nach Themen gruppiert, mit Einheit und Begründung |
| [TODO.md](TODO.md) | laufendes Arbeitsjournal: offene Messungen und Entscheide |
| [CLAUDE.md](CLAUDE.md) | Kurzfassung der Projektregeln für die Arbeit am Code |

Die Einstellwerte im Code sind **begründete Ausgangspunkte, keine Messergebnisse.** Der
Messplan dazu steht in `TODO.md`, in der Reihenfolge, in der die Schritte aufeinander
aufbauen.

---

## Lizenz

Noch nicht festgelegt. Ohne ausdrückliche Lizenz gilt das volle Urheberrecht: eine Nutzung,
Weitergabe oder Bearbeitung des Codes ist damit nicht gestattet. Der generierte Code unter
`lib/ei-model/` stammt aus Edge Impulse und steht unter dessen eigenen Bedingungen.
