# Air Mouse – UX-Überarbeitung (Design)

**Datum:** 2026-08-05
**Umfang:** Ein/Aus über eine Drehgeste statt über Schütteln, Rechtsklick in der
abgedrehten Haltung, Latenz des Cursors, gravitationsfreie ML-Kanäle, Aufräumen der
Drag-Reste.

Ziel der Sitzung ist die Bedienbarkeit, nicht die Funktionsmenge. Jede Änderung hat
entweder einen gemessenen oder einen rechnerisch belegten Grund; Werte, die erst am Gerät
entschieden werden können, sind als Startwerte markiert und in Abschnitt 9 mit einem
Messschritt hinterlegt.

---

## 1. Eine Schwelle, drei Bedeutungen

### Befund

Heute liegt der Rechtsklick im Band `Idle` (45–85° Verdrehung), das Scrollen darüber
(>85°). Die gewünschte Rechtsklick-Haltung – Arm um rund 90° abgedreht – fällt damit
genau auf die Scroll-Haltung. Das Idle-Band ist ausserdem schmal und muss beim Pinchen
gehalten werden, während der Unterarm ohnehin nahe am Anschlag seiner Supination
arbeitet. Ein/Aus lief bisher über zwei Gyro-Spitzen (`ShakeToggle`), deren Schwelle mit
350 °/s nur knapp über den ~250 °/s des normalen Gebrauchs liegt.

### Entscheid

Die Verdrehachse bekommt **eine** Schwelle, `TURN_ON_DEG ≈ 70°`. Was eine Ausdrehung
bedeutet, entscheidet sich erst beim Zurückdrehen:

```
twist
 90° │      ┌───┐                ┌────────┐              ┌──────────────┐
     │     ╱     ╲              ╱          ╲            ╱                ╲
 70° ├────╱───────╲────────────╱────────────╲──────────╱──────────────────╲───
     │   ╱         ╲          ╱              ╲        ╱                    ╲
 30° ├──╯───────────╰────────╯────────────────╰──────╯──────────────────────╰──
  0° │
     └──────────────────────────────────────────────────────────────────────────
        │< 1000 ms >│         │  Pinch dazwischen │    │   > 1000 ms (halten)  │
         EIN / AUS              RECHTSKLICK              SCROLL-MODUS
```

- **Unter 1 s raus und zurück, ohne Erschütterung dazwischen** → Ein/Aus.
- **Pinch während der Ausdrehung** → Rechtsklick; die Rückdrehung schaltet dann nicht.
- **Länger als 1 s gehalten** → Scroll-Joystick über die Armneigung; die spätere
  Rückdrehung schaltet ebenfalls nicht.

Damit ist die Ein/Aus-Geste kontrolliert *und* schnell: sie verlangt eine bewusste
90°-Drehung des Unterarms und ein Zurück innerhalb einer Sekunde. Das Schütteln entfällt
ersatzlos, `lib/ShakeToggle/` und alle `SHAKE_*`-Konstanten werden gelöscht.

### Warum Madgwick die richtige Quelle ist

`arm::twistDeg()` liest den Winkel aus dem Schwerkraftvektor der Lageschätzung – ein
**absoluter** Winkel. Aus `gy` integriert driftete die Referenz weg, und die Bedingung
„wieder zurück auf gerade" wäre nach einer Minute nicht mehr dieselbe wie am Anfang. Die
Geste ist deshalb nur mit dem Filter überhaupt zuverlässig darstellbar.

Die Kehrseite: bei senkrecht gehaltenem Unterarm ist die Verdrehung aus der Schwerkraft
**nicht beobachtbar**. Das Waagrecht-Gate (`LEVEL_MAX_DEG`, ±35°) hält davon fern – es
ist damit keine Bequemlichkeit mehr, sondern eine Voraussetzung der Geste. `TwistToggle`
verlangt `level` durchgehend, sonst würde ein hängender Arm zufällig schalten.

### Der abgesicherte Fehlermodus

Verpasst der Klassifikator einen Pinch bei 90°, dreht der Nutzer zurück – und die Maus
ginge **aus** statt rechtszuklicken. Ein verfehlter Klick würde zum Abschalten. Deshalb
bricht nicht der *erkannte Klick* die Geste ab, sondern das **geöffnete `env`-Gate**:
tritt während der Ausdrehung überhaupt eine Erschütterung über `ENV_ON` auf, ist die
Ausdrehung verbraucht und schaltet nicht mehr. Diese Bedingung ist vom Modell unabhängig
und fängt auch den verpassten Pinch ab.

Das ist keine theoretische Absicherung: der Rechtsklick funktioniert am Gerät derzeit
nicht, mit dem Modell als wahrscheinlicher Ursache (Abschnitt 5). Genau dieser Fall darf
nicht dazu führen, dass die Maus ausgeht.

Der Controller ruft dafür `twist_.cancel()`, wenn `fsm_.on() && env > cfg::ENV_ON`. Die
Bindung an `fsm_.on()` ist nötig: im Ruhezustand läuft der Pinch-Pfad nicht, und eine
Erschütterung soll die Einschalt-Geste nicht abbrechen.

### Haltungen

`Pose::Scroll` heisst künftig `Pose::Turned` – der Zustand trägt jetzt zwei Bedeutungen
(Rechtsklick *und*, nach einer Sekunde, Scrollen); ein Name, der nur eine davon nennt,
wäre dieselbe Falle wie seinerzeit `rollDeg()`/`pitchDeg()`. `Pose::Idle` bleibt im Enum,
bekommt aber eine andere Bedeutung: es ist ausschliesslich das Ergebnis des
Waagrecht-Gates, also „Arm nicht in Arbeitshaltung". Ohne diesen Zustand hätte das Gate
kein Ziel und der Cursor liefe mit hängendem Arm weiter.

```
elev ausserhalb LEVEL_MAX_DEG  ────────────►  Idle    (nichts, Pinch wirkungslos)

twist  0°───────────55°──[Hysterese]──70°───────────120°
       │ Point                        │ Turned
       │ Cursor + Linksklick          │ Rechtsklick beim Pinch
       │                              │ + Scroll-Joystick nach 1 s
```

**Rechtsklick nur bei ruhiger Neigung.** In `Turned` löst ein Pinch den Rechtsklick nur
aus, solange die Armneigung innerhalb der Scroll-Totzone um den Eintrittswinkel steht
(`|elev − elev0| < SCROLL_DEAD_DEG`). Wer gerade scrollt, kippt den Arm – dann wäre ein
Klick nicht vorhersehbar. Wer die Hand ruhig gedreht hält, scrollt nicht und meint den
Klick. Die Bedingung gilt zeitunabhängig, der Rechtsklick funktioniert also auch nach
Ablauf der Sekunde.

### Zwei Rückkehrschwellen, und warum

| Schwelle | Wert | Zweck |
|---|---|---|
| `TURN_ON_DEG` | 70° | Ausdrehung erkannt (Haltung *und* Geste) |
| `TURN_OFF_DEG` | 55° | Haltungs-Hysterese zurück nach `Point` |
| `TURN_BACK_DEG` | 30° | Ein/Aus-Geste: echte Rückkehr in die gerade Haltung |

Die Geste verlangt eine strengere Rückkehr als die Haltung. Sonst würde ein halbherziges
Zurückwackeln auf 54° die Maus abschalten.

### Der Nullpunkt bleibt fest

`cfg::TWIST_NEUTRAL_DEG` bleibt eine feste Konstante; es wird **nicht** bei jedem
Einschalten neu kalibriert. Eine Neukalibrierung wurde in dieser Sitzung entworfen und
wieder verworfen – Begründung in Abschnitt 10.

---

## 2. Module

### `lib/TwistToggle/` (neu, ersetzt `lib/ShakeToggle/`)

Reine Policy, hardwarefrei, auf dem PC prüfbar – wie `ArmOrientation` und
`AirMouseState`:

```cpp
// Kein #include "config.h" und kein <Arduino.h>: dieser Header muss sich ohne
// Toolchain uebersetzen lassen (test/test_twist_toggle.cpp).
struct TwistTuning {
    float    onDeg     =  70.f;   // Ausdrehung erkannt
    float    backDeg   =  30.f;   // zurueck in der geraden Haltung
    uint32_t maxMs     = 1000;    // laenger = keine Schaltgeste mehr
    uint32_t lockoutMs =  800;    // nach dem Schalten
};

enum class TwistEvent : uint8_t {
    None,
    Toggle,   // raus und zurueck innerhalb maxMs, nicht abgebrochen
    Held      // maxMs ueberschritten, waehrend noch ausgedreht -> Scroll
};

class TwistToggle {
public:
    explicit TwistToggle(const TwistTuning& t = TwistTuning());
    TwistEvent tick(float relTwistDeg, bool level, uint32_t now_ms);
    void cancel();   // env-Gate ging auf: diese Ausdrehung schaltet nicht mehr
};
```

`Held` wird je Ausdrehung **genau einmal** gemeldet. Die Zahlen leben in `TwistTuning`
und nicht doppelt in `cfg::` – das ist die dokumentierte Ausnahme zur Regel „alle
Zahlenwerte in `cfg::`", Begründung ist die PC-Testbarkeit.

Eingang ist `PoseDetector::relTwistDeg()`, also die **geglättete** Verdrehung gegenüber
`TWIST_NEUTRAL_DEG` – nicht der rohe Winkel, dessen Rauschen sonst an den Schwellen
flattern würde.

### `lib/AirMouseState/AirMouseState.h`

```cpp
enum class Pose : uint8_t { Point, Idle, Turned };

Actions onPower();               // war onShake()
Actions onPose(Pose next);
Actions onTwistHeld();           // Scroll-Joystick zuschalten
Actions onPinch(bool scrollIdle);
```

| Ereignis | `Off` | `On/Point` | `On/Idle` | `On/Turned` |
|---|---|---|---|---|
| `onPower` | → On | → Off | → Off | → Off |
| `onPose` | ignoriert | Zeiger zurück | ggf. Wechsel | – |
| `onTwistHeld` | ignoriert | ignoriert | ignoriert | `enterScroll`, Joystick an |
| `onPinch` | ignoriert | Linksklick, 1 Impuls | ignoriert | `scrollIdle` ? Rechtsklick, 2 Impulse : – |

`scrolling()` liefert nur dann `true`, wenn `pose_ == Turned` **und** der Joystick über
`onTwistHeld()` zugeschaltet wurde. Das Verlassen von `Turned` schaltet ihn wieder ab.
Damit scrollt die erste Sekunde einer Ausdrehung nicht – sonst würde jede Ein/Aus-Geste
nebenbei ein Stück weit scrollen.

### `lib/PoseDetector/PoseDetector.h`

`classify()` wird kürzer als heute:

```cpp
Pose classify() const {
    const float tilt = fabsf(rel_);
    if (pose_ == Pose::Turned) return (tilt < cfg::TURN_OFF_DEG) ? Pose::Point : Pose::Turned;
    // Deckt Point und Idle ab: aus beiden fuehrt derselbe Eintrittspunkt nach
    // Turned. Idle ist keine Zone der Verdrehung mehr, sondern das Ergebnis des
    // Waagrecht-Gates - es braucht deshalb keinen eigenen Zweig.
    return (tilt > cfg::TURN_ON_DEG) ? Pose::Turned : Pose::Point;
}
```

**`MODE_TAU` sinkt von 0.25 s auf 0.10 s.** Die Glättung verzögert Hin- und Rückflanke um
je eine Zeitkonstante; bei 0.25 s wäre das 1-s-Fenster der Geste um die Hälfte
verschmiert. Die Stabilität kommt weiterhin aus Haltezeit, Hysterese und der
Bewegungssperre.

**Dafür braucht es einen zweiten, langsameren Verdrehungswinkel.** Derselbe Wert geht
heute auch in die Roll-Kompensation des Zeigers (`arm::rates()`), und dort steht er in
einer Drehmatrix: weniger Glättung heisst dort direkt mehr Rauschen im Cursor – also
genau das Wackeln, das Abschnitt 3 beseitigen soll. `PoseDetector` führt deshalb zwei
geglättete Winkel:

| Methode | Zeitkonstante | Verwendung |
|---|---|---|
| `relTwistDeg()` | `MODE_TAU` = 0.10 s | Haltungserkennung, `TwistToggle` |
| `relTwistSlow()` | `ROLLCOMP_TAU` = 0.25 s | Roll-Kompensation im `OrientationPointer` |

Kosten sind eine zusätzliche Exponentialglättung pro Takt. Ohne die Trennung wäre die
schnellere Haltungserkennung ein Rückschritt beim Cursor.

**Die Bewegungssperre (`POSE_STILL_DPS`, `POSE_CALM_MS`) bleibt, mit neuer Begründung.**
Sie stand bisher da, weil das Einschalt-Schütteln die Haltungserkennung durch `Idle` bis
`Scroll` riss; das Schütteln gibt es nicht mehr. Sie bleibt trotzdem: eine gehaltene
Haltung ist per Definition nichts, was man mitten im Schwung einnimmt. Die Obergrenze
`SHAKE_ON`, gegen die 300 °/s gewählt wurde, existiert nicht mehr – der Wert steht jetzt
allein gegen die ~250 °/s des normalen Gebrauchs und ist am Gerät gegenzuprüfen.

Wichtig und leicht zu übersehen: eine zügige 90°-Drehung in 300 ms erzeugt rund 300 °/s
und friert damit die *Haltungsentscheidung* ein. `TwistToggle` arbeitet deshalb auf dem
**Winkel** und nicht auf `pose()` – die Winkel laufen während der Sperre weiter, nur die
Haltungsentscheidung ruht.

### `lib/ScrollJoystick/ScrollJoystick.h`

Neu `bool inDeadzone() const`, gesetzt in `update()` aus
`fabsf(dev) <= cfg::SCROLL_DEAD_DEG`; nach `enter()` auf `true`.

### config.h

| Konstante | alt | neu |
|---|---|---|
| `POINT_MAX_DEG` | 45 | entfällt |
| `SCROLL_ON_DEG` | 85 | entfällt |
| `MODE_HYST_DEG` | 10 | entfällt |
| `SHAKE_*` (6 Stück) | – | entfallen ersatzlos |
| `TURN_ON_DEG` | – | 70 (Startwert) |
| `TURN_OFF_DEG` | – | 55 (Startwert) |
| `MODE_TAU` | 0.25 | 0.10 |
| `TWIST_NEUTRAL_DEG` | 0 | unverändert |

`TURN_BACK_DEG`, `maxMs` und `lockoutMs` stehen in `TwistTuning`, nicht in `cfg::`.

---

## 3. Latenz und Cursor-Gefühl

### Befund: Latenzbudget (USB-Pfad)

| Posten | heute | Anmerkung |
|---|---|---|
| IMU-Lesen | 6 einzelne I2C-Transaktionen | bei 100 kHz ≈ 2 ms reine Schleifenzeit pro Takt |
| Abtastung | 4.785 ms | fest, an `EI_CLASSIFIER_INTERVAL_MS` gekoppelt – unantastbar |
| `MOVE_INTERVAL_US` | 10 ms | im Mittel +5 ms; begründet war das mit dem BLE-Intervall, über USB pollt der Host jede 1 ms |
| 1-Euro-Filter | Grenzfrequenz folgt dem Zittern | siehe eigenen Befund unten – hier liegt Verzögerung *und* Wackeln |
| `FREEZE_MS` | 120 ms | Cursor steht nach **jedem** Klick vollständig still |

Friston et al. 2016 sehen messbare Verschlechterung ab ~16 ms; Casiez et al. 2012 rechnen
mit 10–20 ms Budget allein fürs Filtern. Beides ist heute deutlich überschritten.

### Befund: der Bias-Schätzer frisst langsames Zeigen

`ImuReader` führt den Gyro-Nullpunkt nach, sobald `gyroSum < BIAS_STILL_DPS` (15 °/s).
Eine langsame, bewusste Zeigebewegung liegt bei 5–10 °/s und fällt damit **in** das
Lernfenster. Mit `BIAS_TAU = 2 s` übernimmt der Schätzer nach rund zwei Sekunden den
grössten Teil der gewollten Drehrate als vermeintlichen Nullpunkt: der Cursor wird beim
langsamen Ziehen immer langsamer und driftet beim Anhalten zurück. Das Symptom sieht aus
wie zu starke Glättung und ist keine.

Gegenmittel: Schwelle deutlich unter die langsamste gemeinte Bewegung, zusätzlich an
ruhige Beschleunigung koppeln (ein ruhendes Board misst `|accMag − 1| < 0.05`), und die
Zeitkonstante verlängern, weil echter Temperaturdrift langsam ist.

### Befund: der 1-Euro-Filter hält das Handzittern für gewollte Bewegung

Am Gerät wackelt der Cursor deutlich zu stark. Die Ursache ist nicht der Zahlenwert der
Grenzfrequenz, sondern die Rückkopplung über die Geschwindigkeit.

Physiologischer Tremor liegt bei 8–12 Hz mit 0.1–0.5° Amplitude – das entspricht rund
**12 °/s Drehrate**. Die Grenzfrequenz des Filters ist
`cutoff = MIN_CUTOFF + BETA · |Drehrate|`, mit den aktuellen Werten also
0.9 + 0.55 · 12 = **7.5 Hz**. Bei 7.5 Hz passiert 10-Hz-Tremor praktisch ungedämpft. Der
Filter öffnet genau dann, wenn er schliessen müsste. Bei `SENS = 110 px/°` werden aus
0.2° Zittern ±22 px Cursorwackeln.

Dazu kommt eine unbeabsichtigte Abweichung vom Original. Casiez et al. 2012 filtern die
Geschwindigkeit vor der Verwendung mit einem eigenen Tiefpass (`dcutoff`, Vorgabe 1 Hz)
und setzen *erst dessen Ausgang* in die Grenzfrequenz ein. `lib/Filters/OneEuro.h` hat
mit der Ableitungsstufe auch diesen Tiefpass gestrichen und reicht die rohe, zitternde
Drehrate direkt durch. Der Kommentar im Header begründet nur den Wegfall der Ableitung –
der Wegfall der Glättung darauf war nicht beabsichtigt.

Verstärkt wird beides durch die Beschleunigungskennlinie: sie multipliziert das
Restzittern mit bis zu `ACCEL_MAX = 4`, und sie greift **nach** dem Filter, kann also von
ihm nicht mehr aufgefangen werden.

### Befund: die Ausdrehung reisst den Cursor mit

Ohne Gegenmassnahme läuft der Zeiger beim Hindrehen in die abgedrehte Haltung quer über
den Schirm. Damit ist der Rechtsklick unbrauchbar: bis die Hand die Haltung erreicht hat,
steht der Cursor nicht mehr auf dem Ziel. Dasselbe gilt für die Ein/Aus-Geste.

Der Zeiger verwirft `gy` — die Platinenachse, die der Unterarmachse am nächsten kommt —
heute schon ersatzlos. Das genügt nicht: die Unterarmachse fällt nicht exakt mit einer
Platinenachse zusammen (das Board sitzt am Arm, nicht im Gelenk), und eine Verdrehung
leckt deshalb immer auch in `gx` und `gz`. Dazu kommt, dass die Roll-Kompensation den
Verdrehungswinkel in eine Drehmatrix einsetzt: ändert er sich *während* der Bewegung,
werden `gx` und `gz` mitten im Zug umgerechnet.

### Entscheid: `lib/TwistGuard/`

Die Drehrate um die Unterarmachse wird aus der **Lageschätzung** abgeleitet, nicht aus
`gy`: die Ableitung von `arm::twistDeg()` misst die Verdrehung selbst und ist damit
unabhängig davon, wie das Board am Arm sitzt. Genau dafür ist der Madgwick-Filter da.

Aus der geglätteten Rate wird ein Faktor 0…1, mit dem der Controller `px`/`py` vor dem
Aufsummieren multipliziert:

```
gain
 1.0 │────────────────┐
     │                 ╲
     │                  ╲
 0.0 │                   └──────────────────
     └────────────────────────────────────────  |Verdrehungsrate|
     0            LOW_DPS      HIGH_DPS
```

Zwei Eigenschaften, die nicht verhandelbar sind:

- **Sofort zu, langsam wieder auf.** Am Ende einer Drehung klingt die Rate aus und
  kreuzt die Schwelle mehrfach; ohne begrenzte Rückkehr zuckte der Cursor dabei
  wiederholt an. Das Aufmachen ist deshalb auf `RELEASE_S` gedehnt.
- **Der Faktor greift auf `px`/`py`, nicht auf die Rate vor dem Filter.** Der
  1-Euro-Filter läuft weiter mit und bleibt eingeschwungen — sonst käme nach jeder
  Drehung eine Anfahrverzögerung obendrauf.

Die Werte stehen in `TwistGuardTuning` und nicht in `cfg::` — der Header muss ohne
Toolchain übersetzbar bleiben, wie `PointerTuning` und `TwistTuning`.

| `TwistGuardTuning` | Startwert | Bedeutung |
|---|---|---|
| `lowDps` | 25 | darunter volle Bewegung |
| `highDps` | 70 | darüber gar keine |
| `rateTau` | 0.03 s | Glättung der abgeleiteten Rate |
| `releaseS` | 0.20 s | Zeit für die volle Rückkehr auf 1.0 |

`TODO.md` nennt einen früheren `USE_TWIST_GUARD` mit `TWIST_K` / `TWIST_MIN_DPS`, den es
in `config.h` nicht mehr gibt. Der neue unterscheidet sich in der Quelle: er misst die
Verdrehung aus der Lageschätzung statt aus einer Gyro-Achse. Der alte Eintrag in
`TODO.md` („hinfällig, das erledigt jetzt der Haltungs-Modus") war falsch — der
Haltungs-Modus greift erst nach `MODE_DWELL_MS`, also nachdem der Cursor bereits
weggelaufen ist.

### Befund: die int8-Grenze des HID-Berichts bremst schnelle Bewegungen

Pro Bericht sind maximal 127 px je Achse übertragbar, bei 10 ms Intervall also
12 700 px/s. Mit `SENS = 110 px/°` und aktiver Beschleunigung wird das schon bei rund
50 °/s erreicht – also in jeder zügigen Zeigebewegung. Der Überhang bleibt in `accumX_`
liegen und wird verzögert nachgeliefert: der Cursor hinkt hinterher und läuft nach dem
Anhalten nach. Das 5-ms-Intervall verdoppelt die Grenze; zusätzlich werden bei einem
Rückstau über 127 px mehrere Berichte im selben Takt hintereinander gesendet (höchstens
drei, damit ein hängender Host die Schleife nicht blockiert).

### Entscheid

| Änderung | von | auf |
|---|---|---|
| `MOVE_INTERVAL_US` | 10 000 | 5 000 über USB; 10 000 bleibt für BLE (`#if USE_BLE_HID`) |
| Rückstau > 127 px | wartet auf den nächsten Takt | bis zu 3 Berichte hintereinander |
| I2C | 6 Einzeltransaktionen, 100 kHz | 400 kHz + ein Burst-Read über alle 12 Bytes |
| `BIAS_STILL_DPS` | 15 | 3, zusätzlich `\|accMag − 1\| < 0.05` |
| `BIAS_TAU` | 2.0 | 5.0 |
| `OneEuroFilter` | Geschwindigkeit roh | Geschwindigkeit über `EURO_DCUTOFF` = 1 Hz geglättet (Original wiederhergestellt) |
| `EURO_MIN_CUTOFF` | 0.9 Hz | 1.0 Hz |
| `EURO_BETA` | 0.55 | **0.2** (Startwert, Messreihe 0.1 / 0.2 / 0.4) |
| `ACCEL_K` | 2.0 | **0** (konstante Verstärkung; A/B über `PointerTuning` bleibt möglich) |
| `DEADZONE` | 2.5 | 3.5 |
| `FREEZE_MS` | 120 fest | an das offene `env`-Gate gebunden, Obergrenze 60 ms |

**Zum 1-Euro-Filter: der Hebel ist `EURO_BETA`.** Der Mittelwert von `|12 · sin(2π·10t)|`
ist 7.64 °/s — mit oder ohne Geschwindigkeits-Tiefpass. Die *mittlere* Grenzfrequenz
ändert `EURO_DCUTOFF` also kaum; was er ändert, ist ihre Modulation. Die Dämpfung kommt
aus `BETA`:

| `MIN_CUTOFF` / `BETA` | mittlere Grenzfrequenz | Dämpfung bei 10 Hz | Restwackeln bei 0.2° Tremor |
|---|---|---|---|
| 0.9 / 0.55 (heute) | 5.1 Hz | 0.45 | ±10 px |
| 1.0 / 0.2 (neu) | 2.5 Hz | 0.25 | ±5 px |
| 1.0 / 0.1 | 1.8 Hz | 0.17 | ±4 px |

`EURO_DCUTOFF` bleibt trotzdem drin, aus zwei Gründen: er stellt das Original wieder her,
und ohne ihn folgt die Grenzfrequenz dem Betrag des Signals — sie steht dann genau auf
den Tremorspitzen am weitesten offen. Der Spitzenwert des Wackelns ist damit schlechter,
als der Mittelwert oben vermuten lässt. Es ist eine Korrektheitsreparatur, kein Hebel.

Zahlen zur Einordnung, mit `MIN_CUTOFF = 1.0` und `BETA = 0.2`:

| Situation | Drehrate | Grenzfrequenz | τ |
|---|---|---|---|
| Tremor (wird weggeglättet) | 12 °/s → ~1 °/s nach `dcutoff` | 1.2 Hz | 133 ms |
| langsames, gezieltes Zeigen | 10 °/s | 3.0 Hz | 53 ms |
| zügige Bewegung | 100 °/s | 21 Hz | 7.6 ms |
| schneller Schwenk | 300 °/s | 61 Hz | 2.6 ms |

Das ist genau die Trennung, für die der Filter gebaut wurde – sie funktionierte bisher
nur nicht, weil das Zittern selbst als Geschwindigkeit gezählt wurde.

**Falls das nicht reicht: 3-Tap-Median vor der Deadzone.** Der Median entfernt einzelne
Ausreisser, ohne wie ein Tiefpass die Flanken zu verschleifen; er kostet ein Sample
Verzögerung (4.8 ms). Er kommt **nur** hinzu, wenn die drei Massnahmen oben am Gerät
gemessen nicht genügen – sonst ist nachher nicht zuordenbar, was gewirkt hat.

`PointerTuning` bekommt `euroDCutoff` als weiteres Feld, damit zwei verschieden
eingestellte Zeiger wie bisher im selben Binary gegeneinander laufen können.

**Vor allem anderen prüfen: läuft `DEBUG_TELEPLOT` beim Test?** Der Serial-Verkehr
blockiert die Schleife, der Takt rutscht, und die feste Schrittweite `cfg::DT` stimmt
dann nicht mehr mit der tatsächlich verstrichenen Zeit überein. Der Cursor bewegt sich
dadurch in Schüben – das sieht aus wie Wackeln und ist keins. Kanal `ovr` gegenprüfen.

**Zur Deadzone.** Sie sitzt *vor* dem 1-Euro-Filter; im absoluten Stillstand ist der
Filtereingang damit exakt null. Gegen das Wackeln hilft sie trotzdem nur begrenzt: der
Tremor erzeugt rund 12 °/s und liegt damit weit über jeder vertretbaren Deadzone. Sie
weiter anzuheben kostet feine Bewegung direkt – 2.5 °/s entsprechen bei `SENS = 110`
bereits 275 px/s, die stufenlos abgezogen werden. 3.5 ist deshalb die Obergrenze; das
Wackeln muss aus dem Filter kommen, die Ruhe im echten Stillstand aus dem Bias-Fix.

**Zum Freeze.** Statt einer festen Zeit nach dem Klick friert der Zeiger nur, solange das
`env`-Gate offen ist – mit 60 ms als Obergrenze für den Fall, dass das Gate hängt.

---

## 4. Haptik: Rechtsklick als Doppelimpuls

`Actions::haptic` (bool) wird zu `Actions::hapticPulses` (uint8_t). `Haptic` wird vom
Einzelschuss zu einem kleinen Sequenzer: n Impulse à `HAPTIC_MS` mit `HAPTIC_GAP_MS`
dazwischen.

```
1 Impuls:   ████                 40 ms   Linksklick, Haltungswechsel, Ein/Aus, Scroll an
2 Impulse:  ████░░░░░░████      130 ms   Rechtsklick
```

**Die feste Sperrfrist entfällt.** `HAPTIC_COOLDOWN_MS` (150 ms) wird ersetzt durch
„solange ein Muster läuft, plus `HAPTIC_GAP_MS` Ruhe danach". Eine feste Sperre über der
Musterdauer wäre ein Rückschritt: sie müsste über 130 ms liegen, `DEBOUNCE_MS` steht aber
auf 180 ms – ein Doppelklick würde damit nur noch einmal brummen. Die laufzeitabhängige
Sperre erfüllt denselben Zweck (kein Verschmelzen zu einem langen Brummen) und lässt den
Doppelklick spürbar: 90 ms Sperre nach einem Linksklick, 180 ms nach einem Rechtsklick.

| Konstante | alt | neu |
|---|---|---|
| `HAPTIC_MS` | 45 | 40 |
| `HAPTIC_GAP_MS` | – | 50 |
| `HAPTIC_COOLDOWN_MS` | 150 | entfällt |

Der Kommentar in `Haptic.h` begründet die Sperre heute mit dem Doppel-Pinch, den es nicht
mehr gibt. Die Begründung dreht sich um: die Sperre schützt jetzt die Abzählbarkeit des
Musters.

---

## 5. Edge Impulse: gravitationsfreie Kanäle

### Befund

Der Rechtsklick funktioniert am Gerät nicht. Wahrscheinliche Ursache: `feat::pack()`
reicht rohes `ax/ay/az` durch, inklusive Erdbeschleunigung. In der Zeige-Haltung liegt
die auf `az ≈ +1`, in der um 90° abgedrehten Haltung auf `ax ≈ +1`. Drei von fünf Kanälen
haben in der Rechtsklick-Haltung also einen völlig anderen Gleichanteil als im Training.
Ein nur in Zeige-Haltung aufgenommenes Modell kann den Pinch dort praktisch nicht
wiedererkennen – und mehr Daten *einer* Haltung hilft nicht.

### Entscheid

Die Erdbeschleunigung wird vor dem Packen abgezogen. Ort ist `ImuReader`, weil sowohl
`COLLECT_MODE` als auch der Inferenzpfad durch `ImuSample` gehen und damit nicht
auseinanderlaufen können:

```cpp
struct ImuSample {
    float ax, ay, az;        // roh, mit Erdbeschleunigung - fuer Madgwick
    float lax, lay, laz;     // linear, Erdbeschleunigung abgezogen - fuers Modell
    float gx, gy, gz;
    float accMag;            // Betrag der rohen Beschleunigung - fuer die Huellkurve
    float gyroSum;
};
```

`ImuReader` hält dafür drei `LowPass{cfg::GRAVITY_LP_HZ}` (0.8 Hz, der Wert existiert
bereits) und rechnet `lax = ax − lpX.run(ax, dt)`. `LowPass` setzt sich beim ersten
Sample auf den Eingang, es gibt also keine Einschwing-Artefakte beim Start.

`feat::pack()` reicht `lax/lay/laz` statt `ax/ay/az` durch. Kanalzahl und Reihenfolge
bleiben (5), das Eingangsformat des Modells ändert sich nicht.

Madgwick bekommt weiterhin die rohen Achsen – ihm ist die Erdbeschleunigung das Signal.
`VibrationEnvelope` bekommt weiterhin `accMag` aus den rohen Achsen.

### Warum die Änderung jetzt nichts kostet

`feat::pack()` zu ändern macht laut Konvention jedes trainierte Modell ungültig. Hier
kostet das nichts: die vorhandenen CSV wurden laut `TODO.md` mit 100 Hz aufgenommen, das
Modell erwartet 209 Hz. Die Daten sind bereits ungültig und müssen ohnehin neu
aufgenommen werden. Nach der Neuaufnahme wird diese Änderung wieder teuer – sie gehört
also **vor** die Aufnahme.

### Die UX-Arbeit wartet nicht auf das Modell

Der Rechtsklick lässt sich erst abnehmen, wenn der neue Datensatz aufgenommen und das
Modell neu trainiert ist. Damit die übrige Arbeit nicht blockiert: mit
`USE_ML_PINCH false` läuft die reine Schwellwert-Erkennung (`envGate`), die keine Haltung
kennt und deshalb in beiden Haltungen gleich funktioniert. Haltungen, Drehgeste,
Rechtsklick-Verdrahtung, Scroll und die gesamte Latenzarbeit sind damit vollständig
prüfbar, bevor das Modell fertig ist. Danach `USE_ML_PINCH true` und **nur noch die
Erkennung** bewerten – ein Unterschied, der dann eindeutig dem Modell zuzuordnen ist.

---

## 6. Datenaufnahme vorbereiten

Die Aufnahme macht Nils; die Firmware muss so vorbereitet sein, dass eine missglückte
Aufnahme *auffällt*, statt still ein schlechtes Modell zu erzeugen.

**Overrun-Anzeige im `COLLECT_MODE`.** Der Takt-Zähler ist heute per `#if DEBUG_TELEPLOT
&& !COLLECT_MODE` aus der Aufnahme herausdefiniert. Verpasst die Schleife während der
Aufnahme Abtastschritte – etwa weil der USB-CDC-Puffer volläuft –, ist das Fenster
zeitlich gedehnt und der Datensatz unbrauchbar, ohne dass man es der CSV ansieht. Die
Ausgabe darf den CSV-Strom nicht stören, also geht die Meldung auf die eingebaute LED:
ein Overrun schaltet sie **dauerhaft** ein (gelatcht bis zum Reset). Leuchtet sie nach
der Aufnahme, wird die Aufnahme verworfen. Achtung beim Bauen: `LED_BUILTIN` des XIAO
nRF52840 ist **aktiv LOW** – „an" ist `digitalWrite(LED_BUILTIN, LOW)`.

**Bytes sparen.** `Serial.print(f[i], 4)` bei 5 Kanälen und 209 Hz sind rund 8.4 kB/s.
Für `lax/lay/laz` und `gyro` reichen drei Nachkommastellen, die Sensorauflösung liegt
darunter. **`env` behält vier**: der Kanal bewegt sich zwischen 0.005 (Untergrund) und
0.125 (kräftiger Pinch) – bei drei Stellen bliebe am unteren Ende eine einzige
signifikante Ziffer, und genau dort liegt die Schwelle `ENV_ON = 0.035`.

**Aufnahmeprotokoll** (als eigener Abschnitt in `TODO.md`, damit es beim Schreiben der
Arbeit auffindbar bleibt):

- Werkzeug: `edge-impulse-data-forwarder`, 115200 Baud, fünf Achsen in der Reihenfolge
  von `feat::pack()`: `env, gyro, lax, lay, laz`.
- Die gemessene Frequenz, die der Forwarder meldet, gegen 209 Hz prüfen. Weicht sie ab
  oder leuchtet die LED, nicht aufnehmen.
- Klassen wie gehabt: `pinch`, `idle`, `negative`.
- Hard Negatives ausdrücklich mitnehmen (Xu et al. 2022): Tastaturtippen, Klopfen auf den
  Tisch, Klatschen, Türklinke, Gehen.
- **Validierung der Haltungsunabhängigkeit:** der Grossteil der `pinch`-Daten wird in der
  Zeige-Haltung aufgenommen; zusätzlich ein Satz Pinches in der abgedrehten Haltung, der
  **nur ins Test-Set** kommt. Fällt die Genauigkeit dort nicht ab, ist die
  Gravitationsfreiheit der Kanäle belegt – eine belastbare Zahl für Kapitel 5 und zugleich
  die Erklärung dafür, warum der Rechtsklick vorher nicht funktionierte.

---

## 7. Aufräumen

**Drag.** Im Code ist das Ziehen bereits vollständig entfernt (keine `Grab`-Achse, kein
`pressButton`/`releaseButton`). Übrig sind Reste in Nebendateien:

- `lib/PinchGesture/` löschen (nicht im Build, kein `-I`-Eintrag).
- `include/config.h:16` – `drag` aus der Kanalliste des `DEBUG_PINCH`-Kommentars.
- `lib/Haptic/Haptic.h` – Doppel-Pinch-Begründung ersetzen (Abschnitt 4).
- `lib/AirMouseController/AirMouseController.h:30` – Verweis auf `dragging_`.
- `TODO.md` – Doppel-Pinch-Fenster (157–159) und „Drag and Drop (offen, Idee)" (210–212).

**Schütteln.** `lib/ShakeToggle/` löschen, `-I`-Eintrag aus `platformio.ini`, alle
`SHAKE_*` aus `config.h`, die Schüttel-Punkte aus `TODO.md` (Zeile 126–127) und die
Erwähnungen in `CLAUDE.md`.

**`CLAUDE.md`** wird auf den neuen Stand gebracht: Ablauf pro Tick (Punkt 1 ist jetzt
`TwistToggle` statt `ShakeToggle`), die Haltungstabelle, und der Absatz über
`PinchGesture`/Grab-Achse.

---

## 8. Tests

**Auf dem PC, vor jedem Firmware-Build:**

| Test | Zustand |
|---|---|
| `test/test_state_machine.cpp` | umschreiben: `Pose::Turned`, `onPower`, `onTwistHeld`, `onPinch(bool)`, `hapticPulses` |
| `test/test_arm_orientation.cpp` | unverändert, muss grün bleiben |
| `test/test_twist_toggle.cpp` | neu |

`test_twist_toggle.cpp` deckt ab:

- Schaltet bei sauberem raus/zurück innerhalb `maxMs`.
- Schaltet **nicht**, wenn die Rückkehr später als `maxMs` kommt.
- Schaltet **nicht**, wenn `cancel()` während der Ausdrehung gerufen wurde.
- Schaltet **nicht** ohne echte Rückkehr (Zwischenlage über `backDeg`).
- Schaltet **nicht**, wenn `level` während der Ausdrehung wegfällt.
- `Held` kommt genau einmal je Ausdrehung, und nur wenn nicht vorher geschaltet wurde.
- Nach `Held` löst die Rückkehr keinen `Toggle` mehr aus.
- Lockout: unmittelbar nach einem `Toggle` schaltet eine zweite Geste nicht.

**Am Gerät (Teleplot), in dieser Reihenfolge** – jede Stufe setzt die vorherige voraus.
Schritte 1–7 laufen mit `USE_ML_PINCH false`, damit ein Fehlverhalten nicht dem Modell
zugeschrieben wird, das gar nicht die Ursache ist:

1. `ovr` – hält die Schleife den Takt? Steigt der Zähler, misst man den Messaufbau.
2. `gx/gy/gz` im Stillstand über 30 s, danach eine bewusst langsame Zeigebewegung über
   ~10 s: die Rate darf während der Bewegung nicht wegsacken (Bias-Fix).
3. `accx`, `mvfail` bei schneller Bewegung: der Rückstau muss nach dem Anhalten sofort
   auf null gehen, `mvfail` darf nicht steigen.
4. `rx`/`ry` gegen `gx`/`gz`, Hand ruhig gehalten: die Restamplitude in `rx`/`ry` ist das
   Wackeln. Reihenfolge nach Casiez – erst `EURO_BETA = 0` setzen und `MIN_CUTOFF`
   senken, bis die ruhig gehaltene Hand einen ruhigen Cursor ergibt, dann `BETA` über die
   Leiter 0.1 / 0.2 / 0.4 anheben, bis die Verzögerung beim Zeigen verschwindet. Zuletzt
   `ACCEL_K` 0 gegen 2.0 halten – die Beschleunigung wirkt nach dem Filter und bringt das
   Wackeln sonst zurück. Der 3-Tap-Median kommt nur hinzu, wenn hier etwas übrig bleibt.
5. `rtwist` in ruhiger Zeige-Haltung: steht es bei 0? Sonst `TWIST_NEUTRAL_DEG` nachziehen
   – alle folgenden Schwellen hängen daran.
6. `rtwist`, `on`, `tw`, `tg`, `pose`, `dpose` bei der Ein/Aus-Geste: schaltet sie
   zuverlässig? Schaltet sie **nicht** beim Rechtsklick und **nicht** nach dem Scrollen?
   Steht der Cursor während der ganzen Drehung still (`tg` muss auf 0 gehen, bevor
   `rtwist` nennenswert läuft)? `TURN_ON_DEG` / `TURN_BACK_DEG` / `maxMs` und die
   `TWIST_GUARD_*` nachziehen.
7. `srate` und `click` in `Turned`: Rechtsklick bei ruhig gehaltener Neigung, kein Klick
   während des Scrollens, Scroll erst nach einer Sekunde.
8. Erst jetzt `USE_ML_PINCH true`, nach der Neuaufnahme des Datensatzes: `env`, `gate`,
   `p_ml` beim Pinchen in beiden Haltungen.

**Neuer Teleplot-Kanal:** `tw` (Zustand von `TwistToggle`). Ohne ihn ist in Schritt 6
nicht zu unterscheiden, ob die Geste nicht erkannt oder nur verworfen wurde.

---

## 9. Startwerte, die am Gerät entschieden werden

Diese Zahlen sind begründete Ausgangspunkte, keine Ergebnisse. Sie gehören nach der
Messung mit dem gemessenen Wert und einer Notiz in `TODO.md` ersetzt.

| Konstante | Startwert | Messschritt (Abschnitt 8, „Am Gerät") |
|---|---|---|
| `TWIST_NEUTRAL_DEG` | 0 | Schritt 5 |
| `TURN_ON_DEG` / `TURN_OFF_DEG` | 70 / 55 | Schritt 6 |
| `TwistTuning::backDeg` | 30 | Schritt 6 |
| `TwistTuning::maxMs` | 1000 | Schritt 6 |
| `MODE_TAU` | 0.10 s | Schritt 6 |
| `EURO_DCUTOFF` | 1.0 Hz | Schritt 4 |
| `EURO_MIN_CUTOFF` | 1.0 Hz | Schritt 4 |
| `EURO_BETA` | 0.2 | Schritt 4 (Leiter 0.1 / 0.2 / 0.4) |
| `ACCEL_K` | 0 | Schritt 4 (gegen 2.0 halten) |
| `DEADZONE` | 3.5 | Schritt 2 und 4 |
| `ROLLCOMP_TAU` | 0.25 s | Schritt 4 |
| `TwistGuardTuning::lowDps` / `highDps` | 25 / 70 | Schritt 6 (Kanal `tg` gegen `rtwist`) |
| `TwistGuardTuning::releaseS` | 0.20 s | Schritt 6 |
| `BIAS_STILL_DPS` | 3 | Schritt 2 |
| `POSE_STILL_DPS` | 300 | Schritt 6 (Obergrenze `SHAKE_ON` entfällt) |

---

## 10. Nicht im Umfang

- **Neukalibrierung des Verdrehungs-Nullpunkts beim Einschalten.** In dieser Sitzung
  entworfen und wieder verworfen. Sie macht das Verhalten von Sitzung zu Sitzung
  unvorhersehbar: alle Schwellen (`TURN_ON_DEG`, `TURN_BACK_DEG`, `LEVEL_MAX_DEG`)
  beziehen sich auf den Nullpunkt, und wenn der bei jedem Einschalten wandert, wandern
  sie mit. `TODO.md` hält denselben Befund schon einmal fest – damals kam er von der
  Störung durch das Schütteln, diesmal ist er grundsätzlicher. Der feste
  `cfg::TWIST_NEUTRAL_DEG` bleibt, weil das Board immer gleich am Arm sitzt: der
  Nullpunkt ist eine Eigenschaft der Bauform, keine der einzelnen Sitzung. **Das ist
  ein brauchbares Beispiel für Kapitel 6** – zweimal unabhängig auf dieselbe Erkenntnis
  gestossen.
- **Ziehen (Drag & Drop).** Bleibt entfernt. Kommt es später zurück, gehört dazu wieder
  eine eigene Zustandsachse mit der Invariante „`Power::Off` ⟹ Taste frei" und ein Test.
- **Modellarchitektur (CNN vs. MLP).** Offene Entscheidung aus `TODO.md`, unabhängig von
  diesem Design. Die gravitationsfreien Kanäle gelten für jede Architektur.
- **BLE-Verbindungsintervall.** `USE_BLE_HID` ist derzeit `false`; der BLE-Pfad behält
  sein 10-ms-Berichtsintervall und wird hier nicht angefasst.
