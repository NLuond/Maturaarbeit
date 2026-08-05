# TODO – Air Mouse Firmware

## ENTSCHEID: finales ML-Modell (aktuell nur Probemodell vorhanden)
**Stand:** Probemodell = Spectral-Analysis-FFT-Merkmale (5 Kanäle) + Netz, 3 Klassen,
int8, ~196-ms-Fenster (41 Werte @ 209 Hz), K-Means-Anomalie aktiv.

**Empfehlung (Literatur, Consensus Juli 2026):**
- **LSTM ausschliessen** – zu schwer/langsam für den nRF52840 (Saha & Samanta 2026).
- **Kleines 1D-CNN** als Ziel – passt zum Inhaltsverzeichnis, feldüblich für IMU,
  ressourcen-tauglich mit int8 (Daghero et al. 2022).
- **MLP ist ein legitimer, teils sparsamerer Konkurrent** (Lattanzi et al. 2022: 4x weniger
  Speicher/36x weniger Energie bei gleicher Genauigkeit) → NICHT als Verlierer behandeln,
  sondern als Vergleichspunkt in Kap. 2.4 nutzen.
- **Der Haupthebel bei kleinem Ein-Personen-Datensatz ist die Kapazität, nicht CNN-vs-MLP.**

**Offene Bau-Entscheidung:**
- [ ] Rohsignal-Fenster (kanonisches 1D-CNN) ODER Spektral-Merkmale + Netz (aktuelles
  Probemodell)? Für das saubere «1D-CNN»-Narrativ ist das Rohsignal stimmiger. Bewusst
  entscheiden und begründen können.
- [ ] Overfitting-Rezept anwenden: kleines Netz, Dropout oder L2, Datenaugmentation/
  Rebalancing, Early Stopping. NICHT überregularisieren (sonst Underfitting)
  (Santos & Papa 2022).
- [ ] Nach dem finalen Training: echte ROM/RAM/Genauigkeit aus dem EON Tuner notieren
  (für Kap. 5.1 und 5.4) und mir sagen, welche Variante es wurde -> dann schreiben wir 2.4.

## Fehlauslösungen (False Positives) reduzieren – Hard Negatives
**Kontext:** Die env-Schwelle reagiert nur auf die *Stärke* der Erschütterung, nicht auf
ihre *Form*. Alltags-Impulse (Tastaturtippen, Klopfen auf den Tisch, Klatschen) können sie
täuschen. Die Literatur bestätigt das: Xu et al. 2022 (CHI) sammeln genau solche
Alltagssignale ausdrücklich als Negativklasse.

- [ ] **Hard-Negative-Datensatz prüfen/erweitern:** Tastaturtippen, Klopfen, Klatschen,
  Türklinke/Griff, Gehen explizit als Negativbeispiele aufnehmen, damit der ML-Klassifikator
  lernt, sie abzulehnen. (Betrifft die Datenerfassung, Kap. 5.1 – prüfen, ob im aktuellen
  Datensatz schon abgedeckt.)

## ERLEDIGT: vertauschte Lagewinkel (Ursache der schlechten Haltungserkennung)

**Befund.** Am Gerät abgelesen: flach auf dem Tisch `az = +1`, um 90 ° um die
Unterarmachse verdreht `ax = +1`. Ausmultipliziert ist die Madgwick-Standardformel
`rollDeg() = atan2(uy, uz)` und `pitchDeg() = asin(-ux)`. Bei dieser Einbaulage ist das
erste also die **Armneigung** und das zweite die **Handverdrehung** – genau vertauscht
gegenüber dem, was die Namen nahelegen. Folgen:

- `PoseDetector` bekam die Armneigung statt der Verdrehung → Verdrehen wurde nie erkannt,
  dafür sprang die Haltung auf `Idle`, sobald man die Hand hoch/runter neigte.
- `ScrollJoystick` bekam die Verdrehung statt der Neigung – die steht im Scroll-Modus
  konstant bei ~90 °, der Joystick war also wirkungslos.
- Die Y-Ausblendung im `OrientationPointer` bekam ebenfalls die Verdrehung und schnitt
  die senkrechte Bewegung schon bei ganz normal gehaltener Hand weg. Das war der Grund
  für „ich muss mich viel zu weit bewegen", nicht die Verstärkung.

**Behoben.** `rollDeg()`/`pitchDeg()` sind aus `MadgwickAHRS` entfernt (sie laden zum
selben Fehler ein), stattdessen `upX/upY/upZ`. `lib/ArmOrientation/` benennt daraus
`twistDeg` und `elevDeg`. PC-Test: `test/test_arm_orientation.cpp`, 281 Prüfungen.

**Neu: Waagrecht-Bedingung.** `PoseDetector` liefert nur noch `Idle`, solange
`|elev| > LEVEL_MAX_DEG` (35 °, Hysterese 8 °). Absolut gemessen, nicht relativ zum
Einschalten – sonst kalibriert man sich die Bedingung in schiefer Haltung gleich weg.

## ERLEDIGT: zufälliger Nullpunkt der Verdrehung

`PoseDetector` kalibrierte den Bezugspunkt beim Einschalten – also unmittelbar nach dem
Schütteln, wo die Lageschätzung von der Schüttelbewegung am stärksten gestört ist. Jedes
Einschalten ergab damit einen anderen Nullpunkt; `rtwist` war „zufällig" und dauerhaft
negativ, und **alle vorher aufgenommenen `rtwist`-Zahlen waren untereinander nicht
vergleichbar** (u.a. der Bereich −120…−60, der wie eine um 90 ° verdrehte Einbaulage
aussah und keine war).

Behoben durch einen festen Nullpunkt `cfg::TWIST_NEUTRAL_DEG`. Am Gerät bestätigt: 0 ist
richtig, in Zeige-Haltung steht `rtwist` bei 0, beim Drehen läuft es über 100.
`Actions::calibratePose` heisst jetzt `resetPose` und setzt nur noch die Haltung zurück.

**Lehre für die Arbeit:** Ein Bezugswert, der über Minuten gilt, darf nicht im
unruhigsten Moment genommen werden. Taugt als Beispiel für Kap. 6.

## Offen: Einstellwerte am Gerät

(`DEBUG_SET = DEBUG_POINT`, Kanäle `twist`, `elev`, `rtwist`, `level`, `pose`)

- [x] **Vorzeichen von `elev`** – gemessen negativ beim Heben, `ELEV_SIGN = -1.f` gesetzt.
- [x] **Nullpunkt `TWIST_NEUTRAL_DEG`** – 0 bestätigt.
- [x] **`SCROLL_DIR` entfernt.** Die Konstante liess sich nur durch Ausprobieren am
  Handgelenk bestimmen und machte bei falscher Einstellung den Scroll-Modus
  unerreichbar, ohne dass man es der Konfiguration ansah. `classify()` vergleicht jetzt
  den **Betrag** der Verdrehung: aus der Zeige-Haltung heraus sind ~90 ° Supination, aber
  nur 10–30 ° Pronation möglich, `SCROLL_ON_DEG` ist also anatomisch ohnehin nur in einer
  Richtung erreichbar.
- [ ] **`SCROLL_ON_DEG` (70):** erreichbar, da über 100 gemessen. Prüfen, ob 70 bequem
  oder anstrengend ist.
- [ ] **`POINT_MAX_DEG` (45):** Streuung beim *normalen* Zeigen dagegen halten – Zeigen
  darf nicht abbrechen.
- [ ] **`LEVEL_MAX_DEG` (35 °) einstellen:** `level` muss beim normalen Zeigen dauerhaft
  1 sein und erst bei hängendem oder angehobenem Arm auf 0 fallen. Zu eng = die Maus
  fällt beim Zeigen aus, zu weit = die Bedingung greift nie.
- [ ] **`SENS_X/Y` gegenprüfen:** von 100 auf 160 px/Grad angehoben. Nach dem Wegfall
  der falschen Y-Ausblendung kann das jetzt zu viel sein – erst so messen, dann
  entscheiden (Casiez et al. 2008: zu niedrig schadet klar, zu hoch kaum).
- [ ] **`mvfail`** beobachten – steigt der Zähler, gehen Pakete an BLE verloren.

## Messungen am Gerät (Firmware ist dafür bereit)
Die Teleplot-Ausgabe in `AirMouseController::debug()` ist in Sätze aufgeteilt
(`DEBUG_SET` in `config.h`): immer `on`/`pose`/`tw`, dazu `DEBUG_PINCH`
(`env`, `gate`, `p_ml`, `click`, `gsum`, `nDeb`, `nGyro`, `ei_err`, `ei_us`),
`DEBUG_POINT` (`gx/gy/gz`, `rx`, `ry`, `pacc`, `accx`, `mvfail`, `twist`, `elev`,
`rtwist`, `level`, `srate`) oder `DEBUG_ORIENT` (zusätzlich `ax/ay/az` roh,
`gvx/gvy/gvz` geglättet, `angX/angY/angZ`) zum Nachprüfen der Einbaulage.

- [ ] **Kontakt-Impuls-Dauer messen:** Im Teleplot `env` zusammen mit dem `click`-Puls
  aufzeichnen und die Dauer des Pinch-*Kontakts* (nicht der ganzen Greifbewegung) ablesen,
  um die ML-Fenstergrösse sauber zu begründen. Literatur gibt nur die volle Pinch-Geste
  (~550 ms), nicht den kurzen Kontakt-Impuls.
- [x] ~~**Twist-Achse verifizieren**~~ – erledigt, siehe Abschnitt „vertauschte
  Lagewinkel" oben. Die Unterarmachse ist **Y**, `gy` verwirft der Zeiger ersatzlos.
- [ ] ~~**Twist-Guard einstellen**~~ – hinfällig. `USE_TWIST_GUARD`, `USE_ROLL_COMP`,
  `TWIST_K` und `TWIST_MIN_DPS` gibt es in `config.h` nicht mehr; die Aufgabe erledigt
  jetzt der Haltungs-Modus.
- [ ] **Bi-Level-Schwelle einstellen:** `cfg::ENV_OFF` (aktuell 0.012) prüfen. Zu nah an
  `ENV_ON` → Hysterese wirkungslos; zu tief → zweiter Pinch wird verschluckt.
  Aus der ersten Aufnahme: Untergrund ~0.005–0.02, echte Pinches 0.045–0.125.
  Vorschlag `ENV_ON` 0.035 / `ENV_OFF` 0.020 – am Kanal `gate` gegenprüfen.
- [ ] **Gyro-Guard prüfen:** Bei jedem Pinch schiesst `gsum` auf 200–250, aber
  `cfg::PINCH_GYRO_GUARD` steht auf 100. Eingezoomt nachschauen, ob die `gsum`-Spitze
  zum Zeitpunkt der `env`-Spitze schon abgeklungen ist. Wenn nicht, blockiert der
  Guard genau die Klicks, die er durchlassen soll.
## DRINGEND: Abtastrate von Training und Inferenz stimmt nicht überein
Das Modell erwartet **209 Hz** (`EI_CLASSIFIER_INTERVAL_MS = 4.785`). Der Inferenz-Pfad
läuft jetzt genau darauf (`cfg::SAMPLE_INTERVAL_US = 4785`, per `static_assert` gesichert).
`COLLECT_MODE` hat aber bisher mit **100 Hz** aufgenommen (10 000 µs) – die vorhandenen
Trainingsdaten sind also gegenüber dem, was der Klassifikator zur Laufzeit sieht, um
Faktor ~2 zeitgedehnt.

- [ ] In Edge Impulse nachschauen, welche Frequenz für die hochgeladenen CSV-Daten
  deklariert wurde. Stand dort 209 Hz, sind die Trainingsdaten faktisch falsch etikettiert.
- [ ] Entweder neu aufnehmen (`COLLECT_MODE` sampelt jetzt mit `cfg::SAMPLE_INTERVAL_US`,
  also automatisch richtig) oder in Edge Impulse die Frequenz korrigieren und neu trainieren.
- [ ] Danach die Pinch-Erkennung neu bewerten – vorher sind alle Genauigkeitswerte
  aus Kap. 5 nicht belastbar.

## Zustandsautomat (neu, PC-getestet)
`lib/AirMouseState/AirMouseState.h` – drei Achsen (Power / Pose / Grab), alle
Übergänge in einer Tabelle, keine Zustandsbits mehr im Controller verstreut.
Test: `test/test_state_machine.cpp`, 1251 Prüfungen, läuft auf dem PC.

```bash
g++ -std=c++14 -Wall -Wextra -I lib/AirMouseState -o build/fsm.exe test/test_state_machine.cpp && ./build/fsm.exe
```

- [ ] **Am Gerät gegenprüfen:** Kanäle `on`, `pose` (0=Point, 1=Idle, 2=Scroll)
  und `drag` im Teleplot. Der Automat ist bewiesen korrekt – offen ist nur, ob
  die *Erkenner* die richtigen Ereignisse liefern.
- [ ] **Scroll-Schwelle:** `SCROLL_ON_DEG` steht auf 70°, die Geste ist 90°.
  Falls der Scroll-Modus zu früh anspringt, auf 75–80 anheben.

## Inferenzzeit messen (Compiler-Flags umgestellt)
`-O1` → `-O2`, CMSIS-DSP und CMSIS-NN eingeschaltet. Im Binary nachgewiesen:
`arm_rfft_fast_f32`/`arm_cfft_f32` (FFT der Spektralmerkmale) und
`arm_fully_connected_s8`/`arm_softmax_s8` (Netz-Kernel).

- [ ] **Vorher/Nachher messen:** Mit `DEBUG_TELEPLOT true` den neuen Kanal `ei_us`
  ablesen (Dauer einer Inferenz in µs). Alten Stand über `git stash`/Checkout der
  alten `platformio.ini` gegenmessen → eine belegte Zahl für Kap. 5.4.
- [ ] **Danach `-O3` testen:** Erst messen, wenn `-O2` bewertet ist – eine Änderung
  pro Messung, sonst ist nicht zuordenbar, was gewirkt hat.
- [ ] **Nebenbefund für Kap. 2.4:** Das Binary enthält `arm_fully_connected_s8`, aber
  keinen `arm_convolve_*`-Kernel. Das Probemodell ist also ein **MLP, kein CNN** –
  passt zur offenen Architektur-Entscheidung oben.

## Cursor-Kennlinie (Literatur-gestützt, muss eingestellt und gemessen werden)
- [ ] **1-Euro-Filter einstellen:** erst `EURO_BETA = 0` und `EURO_MIN_CUTOFF` senken,
  bis das Zittern im Stillstand weg ist, dann `EURO_BETA` anheben, bis die Verzögerung
  beim schnellen Zeigen verschwindet (Casiez et al. 2012).
- [ ] **A/B-Vergleich für die Evaluation:** `USE_ONE_EURO` schaltet zwischen dem
  1-Euro-Filter und dem festen Tiefpass (`SMOOTH_TAU`). Beide Varianten gegen dieselbe
  Aufgabe messen – das ist ein fertiger Messabschnitt für Kap. 6.
- [ ] **Verstärkung prüfen:** Casiez et al. 2008 zeigen, dass *zu niedrige* CD-Gain
  klar schadet (mehr Nachfassen), zu hohe kaum. `SENS_X/Y` steht jetzt auf 160 px/Grad.
- [ ] **Beschleunigung hinterfragen:** Der Gewinn ist laut Literatur klein (3–6 %), und
  Scotto et al. 2020 fanden linear steigende Verstärkung *schlechter* als eine gute
  konstante. `ACCEL_K = 0` gegen den jetzigen Wert testen, bevor weiter optimiert wird.
- [ ] **Latenz messen:** Degradation setzt schon ab ~16 ms ein (Friston et al. 2016).
  Anteile: `MOVE_INTERVAL_US` = 16 ms Berichtsintervall + BLE-Verbindungsintervall
  (neu auf 7.5–15 ms angefragt, die Gegenstelle darf ablehnen) + Filterverzögerung.
  Casiez et al. 2012 rechnen mit nur 10–20 ms Budget fürs Filtern – prüfen, ob das
  eingehalten wird.

## Haltungs-Modus und Scrollen (neu gebaut, muss eingestellt werden)
Der Rollwinkel relativ zur Haltung beim Einschalten wählt die Betriebsart:
gerade = zeigen, abgedreht = nichts, nach aussen gedreht = Scroll-Joystick.

- [x] ~~**Drehrichtung festlegen**~~ – hinfällig, `SCROLL_DIR` gibt es nicht mehr
  (Betragsvergleich, siehe oben).
- [ ] **Umschaltpunkte einstellen:** `POINT_MAX_DEG` (45), `SCROLL_ON_DEG` (70) und
  `MODE_HYST_DEG` (10) am Kanal `pose` nachziehen. Zeigen darf nicht abbrechen, wenn
  man die Hand normal bewegt; die Scroll-Haltung muss bequem erreichbar bleiben.
- [ ] **Scroll-Geschwindigkeit einstellen:** `SCROLL_GAIN` (0.45 Schritte/s pro Grad),
  `SCROLL_MAX_HZ` (15) und `SCROLL_DEAD_DEG` (8) am Kanal `srate`. `SCROLL_INVERT`
  auf `-1.f`, falls die Scroll-Richtung verkehrt herum ist.
- [x] ~~**Doppel-Pinch prüfen**~~ – entfallen, samt `DOUBLE_MS` und `lib/PinchGesture/`.
- [ ] **Rechtsklick testen:** Pinch in der Idle-Haltung (Hand gedreht) löst rechts aus,
  in der Zeige-Haltung links, in der Scroll-Haltung nichts. Prüfen, ob sich das Idle-Band
  beim Pinchen zuverlässig halten lässt – dafür wurde `SCROLL_ON_DEG` von 70 auf 85
  angehoben, das Band ist damit 40° statt 25° breit.
- [ ] **Twist-Guard gegen den Haltungs-Modus abwägen:** Beide unterdrücken Bewegung
  beim Drehen. Prüfen, ob der Guard beim normalen Zeigen fälschlich anschlägt
  (Kanal `twist`) – dann `TWIST_K` erhöhen oder `USE_TWIST_GUARD false`.

**Für die Arbeit:** Die Roll-Kompensation taugt als Ausblick (8.2), solange nur der
Twist-Guard aktiv ist – „das aktuelle System unterdrückt Unterarm-Rotation, kompensiert
sie aber noch nicht".

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
