# TODO – Air Mouse Firmware

## ERLEDIGT: Modellentscheid — Rohsignal + 1D-CNN

**Stand:** Studio-Projekt **1084395**, Export **v3**, liegt in `lib/ei-model/`.
2 Klassen (`non_pinch`, `pinch`), 5 Kanäle, Fenster 40 Samples @ 209 Hz (191 ms),
**Rohsignal + 1D-CNN**, int8. Struktur unverändert gegenüber v1, nur mehr Trainingsdaten —
der Tausch brauchte keine Codeänderung.

- [ ] **Die Kennzahlen unten stammen noch von v1.** Nach dem Test von v3 die Werte aus
  *Model testing* hier ersetzen (Recall, Precision, F1, AUC, Falsch-Positive) — sonst
  stehen in der Arbeit die Zahlen des falschen Modells.

| Kennzahl | Wert |
|---|---|
| Pinch-Recall / Precision / F1 | 87.5 % / ~94.8 % / **0.91** |
| Falsch-Positive (`non_pinch → pinch`) | 0.1 % |
| AUC | 0.94 |
| Inferenz / RAM / Flash (EI-Schätzung) | 3 ms / 4 KB / 32 KB |
| Flash gesamt | 18.1 % → **14.4 %** |

`cfg::ML_CONFIDENCE` wurde deswegen von 0.65 auf **0.50** gesenkt: AUC 0.94 bei 0.1 %
Falsch-Positiven heisst, die Trennung ist besser als die alte Schwelle ausnutzte.

Der Vorgänger (1036761, 3 Klassen, Spectral Analysis + MLP) liegt in
`archive/ei-model-1036761/` — ausserhalb von `lib/`, wird also nicht mehr gebaut.

**Noch offen:**
- [ ] `ei_us` am Gerät messen. Die 3 ms sind eine Schätzung; prüfen, gegen welchen
  Takt Edge Impulse sie gerechnet hat (80 MHz statt 64 MHz wären Faktor 1.25). Bei
  4785 µs Taktbudget sind 3 ms bereits 63 %.
- [ ] `ovr` beim Pinchen beobachten — bleibt er 0, hält die Schleife den Takt.
- [ ] `nClick` nach einer Stunde normaler Arbeit: hält die gesenkte Schwelle?
- [ ] Ein Vergleichslauf mit 3 Klassen für Kapitel 2.4 (`negative → pinch` sichtbar
  machen) und einer mit Spectral Analysis + MLP auf denselben Daten.
- [x] **Im Binary nachgewiesen:** 14 `arm_convolve_*`/`arm_depthwise_*`-Kernel,
  2 `arm_fully_connected_*`, **0** `arm_rfft_*`/`arm_cfft_*`. Beim alten Modell war es
  genau umgekehrt (nur fully_connected und FFT) — das Binary belegt also unabhängig vom
  Studio, dass jetzt ein CNN auf dem Rohsignal läuft und der Spektralpfad weg ist.
  Nachprüfen mit `arm-none-eabi-nm .pio/build/xiaoblesense/firmware.elf`.

### Alter Stand (erledigt, zur Nachvollziehbarkeit)
Probemodell = Spectral-Analysis-FFT-Merkmale (5 Kanäle) + Netz, 3 Klassen,
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
`twistDeg` und `elevDeg`. PC-Test: `test/test_arm_orientation/test_arm_orientation.cpp`, 281 Prüfungen.

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
  nur 10–30 ° Pronation möglich, `TURN_ON_DEG` ist also anatomisch ohnehin nur in einer
  Richtung erreichbar.
- [ ] **`TURN_ON_DEG` (70):** erreichbar, da über 100 gemessen. Prüfen, ob 70 bequem
  oder anstrengend ist.
- [ ] **`LEVEL_MAX_DEG` (35 °) einstellen:** `level` muss beim normalen Zeigen dauerhaft
  1 sein und erst bei hängendem oder angehobenem Arm auf 0 fallen. Zu eng = die Maus
  fällt beim Zeigen aus, zu weit = die Bedingung greift nie.
- [ ] **`SENS_X/Y` gegenprüfen:** von 100 auf 110 px/Grad angehoben. Nach dem Wegfall
  der falschen Y-Ausblendung kann das jetzt zu viel sein – erst so messen, dann
  entscheiden (Casiez et al. 2008: zu niedrig schadet klar, zu hoch kaum).
- [ ] **`mvfail`** beobachten – steigt der Zähler, gehen Pakete an BLE verloren.
- [ ] **`PINCH_GYRO_GUARD` (100 °/s):** bei jedem Pinch schiesst `gyroSum` auf 200
  bis 250. Am Gerät prüfen, ob die Spitze zum `env`-Maximum wieder abgeklungen ist
  – sonst verwirft der Guard genau die Klicks, die er durchlassen soll.
- [ ] **`ML_CONFIDENCE` (0.50):** Kontrolle ist `nClick` nach einer Stunde Arbeit
  ohne bewusstes Pinchen. Bleibt der Zähler stehen, ist die Schwelle tragfähig.

## Versuch 0 — der ML-Weg (vor allen Sensor-Experimenten)

Die bisherige Messung hat einen **Schwellwert auf einem abgeleiteten Kanal** widerlegt
(`env`, Hochpass ab 30 Hz, alles auf einen Betrag heruntergerechnet). Sie hat *nicht*
geprüft, ob ein Klassifikator auf den fünf Rohkanälen das Lösen findet — der sieht die
zeitliche Form und die Korrelation zwischen den Achsen, nicht nur eine Amplitude.
Doublepoint erreicht mit genau diesem Ansatz 94–97 % auf dem Kontakt, ohne Kalibrierung.

**Wichtig: als eigenes Edge-Impulse-Projekt, nicht im produktiven Modell.** Die Frage darf
das laufende Klick-Modell nicht gefährden.

### Sondierung (zwei Klassen, eine Aufnahmesitzung)

- [ ] Projekt mit genau zwei Klassen: `hold` (Pinch geschlossen halten, Arm ruhig) gegen
  `idle` (Hand entspannt, Arm ruhig). Je rund 40 Wiederholungen — das ist die
  Grössenordnung aus putEMG, eine belegte Untergrenze gibt es in der Literatur nicht.
- [ ] **Kriterium:** trennt das Modell die beiden? Nein → die statische Klasse kollabiert
  in Idle, der ML-Weg zum *Halten* ist tot, und nur noch das *Lösen* ist interessant.
- [ ] Zweite Sondierung mit `release` gegen `negative`: das Lösen ist ein Ereignis, kein
  Zustand, und hat damit bessere Aussichten als `hold`.

### Wenn es trägt: ins produktive Modell

- [ ] Verarbeitungsblock von **Spectral Analysis auf rohe Zeitreihen** umstellen und ein
  1D-CNN trainieren. Damit ist die seit langem offene Bau-Entscheidung oben entschieden,
  und zwar belegt: rohe Zeitreihen generalisieren bei kleinen Einzelnutzer-Datensätzen
  besser als handgebaute Spektralmerkmale (Jaén-Vargas et al. 2022, Startsev et al. 2018);
  LSTM bleibt wegen des nRF52840 draussen (Saha & Samanta 2026).
- [ ] Fensterlänge bleibt bei 196 ms (41 Samples @ 209 Hz) — die Literatur findet 200 ms
  mit 50 % Überlappung optimal, kleinere Fenster verschlechtern den F1-Wert.
- [ ] Negativklasse bewusst entwerfen: **Armbewegung ohne Pinch** ist der wichtigste Fall
  und in der Literatur nirgends behandelt. Dazu die Hard Negatives von oben (Tippen,
  Klopfen, Klatschen, Türklinke, Gehen).

### Die Messgrösse, die in keiner Studie steht

- [ ] **Fehlauslösungen pro Stunde normaler Nutzung.** Die Literatur berichtet
  ausschliesslich Klassifikationsgenauigkeit auf isolierten Gesten, nie
  Falsch-Positiv-Raten im Dauerbetrieb. Eine Geste mit 97 % Trefferquote kann unbrauchbar
  sein, wenn sie zehnmal pro Stunde von allein auslöst. Der Zähler `nClick` im Teleplot
  liefert das direkt: eine Stunde normal am Rechner arbeiten, ohne bewusst zu pinchen.

**Für die Arbeit:** die Recherche meldet vier offene Punkte — Onset gegen Release als
getrennte Klassen, Fenster unter 100 ms, Tragfähigkeit einer statischen Klasse, und
Falsch-Positiv-Raten im Dauerbetrieb. Alle vier sind mit diesem Aufbau messbar, und die
209 Hz liegen über den 100 Hz der meisten Studien, die selbst anmerken, dass kurze
Transienten höhere Raten brauchen könnten. Das ist der eigenständige Beitrag der Arbeit.

## Versuchsreihe: lässt sich der GEHALTENE Pinch doch erkennen?

Die eine Frage, an der alles hängt: **kann das Ziehen durch Öffnen der Finger enden statt
durch einen zweiten Pinch?** Drei Versuche, nach Aufwand geordnet. Jeder hat ein
Abbruchkriterium — fällt er negativ aus, ist der nächste dran, nicht eine Verfeinerung.

Randbedingung: das PDM-Mikrofon wird aus **Datenschutzgründen bewusst nicht benutzt**,
obwohl es verbaut ist. Der Preis dafür ist belegbar (Posatskiy et al. 2012: Mikrofone sind
weniger bewegungsartefaktanfällig als Beschleunigungssensoren) und gehört so in die Arbeit.

### Versuch 1 — MMG-Plateau (kein neuer Code, keine Hardware)

Ein kontrahierender Muskel vibriert im Band 30–90 Hz (Abbas et al. 2025, 200 Hz Abtastung
— dein Gerät läuft mit 208 Hz, und der Hochpass der Hüllkurve liegt bei 30 Hz, also genau
an der unteren Bandgrenze).

`DEBUG_SET = DEBUG_ENV`, Kanäle `env`/`envMax`:

- [ ] Pinch schliessen, **3 s halten**, Arm ruhig. Dann entspannen, 3 s ruhig halten.
- [ ] **Kriterium:** liegt `env` während des Haltens auf einem sichtbar erhöhten Plateau
  gegenüber der Ruhe? Der Kontakt-Impuls ist nur die Spitze am Anfang — es zählt, worauf
  die Kurve danach zurückfällt.
- [ ] Einmal mit **fest angezogenem**, einmal mit lockerem Band. Wächst der Unterschied mit
  dem Anzug, ist die Hautkopplung der Hebel und nicht der Sensor (Siddiqui & Chan 2020).
- [ ] Falls positiv: vierte Klasse `hold` in Edge Impulse aufnehmen. `COLLECT_MODE`
  streamt bereits die richtigen Kanäle, `env` ist Kanal 0 — kein Firmware-Umbau nötig.

### Versuch 2 — Motor als Erreger, IMU als Empfänger (kleiner Code, keine Hardware)

Schliessen sich Daumen und Zeigefinger, bildet die Hand eine geschlossene mechanische
Schleife; ihre Übertragungsfunktion ändert sich. Gemessen wird nicht der Muskel, sondern
die **Mechanik der Hand** — damit unabhängig von der Hautkopplung, an der Versuch 1
scheitern kann.

Belegt ist das Prinzip nur mit Lautsprecher und Mikrofon (EchoWrist: 97.6 % über 12
Interaktionen; Amesaka et al.: 83.9 % F-Score über 11 Gesten mit Ultraschall-Sweep).
**Motor plus IMU steht so nicht in der Literatur** — der Versuch ist insofern eigenständig.

- [ ] Haptik-Pin per PWM ansteuern statt nur digital (nRF52840 kann das an D1). Die
  Drehzahl eines ERM-Motors hängt an der Spannung, damit ist ein grober Frequenz-Sweep
  möglich — ohne Sweep bleibt nur eine feste Anregungsfrequenz.
- [ ] Anregung 200 ms, dabei `accMag` aufzeichnen. Je 20 Durchgänge mit **offener** und mit
  **geschlossener** Hand, Arm ruhig.
- [ ] **Kriterium:** unterscheiden sich Amplitude oder spektraler Schwerpunkt der Antwort
  systematisch? Dann ist ein Klassifikator sinnvoll, sonst nicht.
- [ ] Nur bei Erfolg: dasselbe **während Armbewegung** wiederholen. Das ist die eigentliche
  Hürde — beim Ziehen bewegt sich der Arm per Definition.
- [ ] Nebengedanke, falls es trägt: der Erinnerungsimpuls des Ziehens könnte zugleich der
  Prüfimpuls sein. Zwei Funktionen, ein Brummen.

### Versuch 3 — Kraftsensor im Band (ein Bauteil, ein Analogpin)

Esposito et al. 2018: ein piezoresistiver Kraftsensor auf der Haut über dem Beuger
korreliert mit r > 0.9 gegen EMG. Ein FSR kostet wenige Franken und braucht einen der
freien Pins A0/A2/A3.

- [ ] Nur angehen, wenn 1 und 2 negativ ausfallen — es ist der einzige Weg mit neuer
  Hardware, dafür der mit der höchsten Erfolgswahrscheinlichkeit.
- [ ] Offene Frage ist die mechanische Integration ins Band, nicht die Elektronik.

### Was NICHT verfolgt wird, und warum

- **Magnet am Finger + Magnetometer:** 16–25 mm Positionsfehler bei Pinch (Yang et al.
  2021), und es verlangt ein Bauteil an der Hand — das widerspricht dem Grundgedanken,
  dass am Finger nichts sitzt.
- **Kapazitive Handgelenks-Topographie:** in der Literatur die beste Option ohne Bauteil am
  Finger (Rudolph et al. 2022: 81–85 % über 6 Griffe, 89–97 % über Zustände innerhalb einer
  Interaktion), braucht aber ein eigenes Elektrodenband. Zu grosser Umbau für den Rahmen
  der Arbeit; als Ausblick erwähnenswert.
- **Bioimpedanz:** in der Recherche keine belastbaren Zahlen für gehaltene Fingerposen.

### Und wenn alles negativ ausfällt

Dann bleibt das Ziehen bei „kneifen, ziehen, kneifen" — was bereits gebaut ist. Die
Versuchsreihe ist damit kein Risiko für den Zeitplan, sondern eine mögliche Verbesserung
mit klar begrenztem Aufwand. **Selbst bei Erfolg** sollte der zweite Pinch als Ende
erhalten bleiben: alle IMU-only-Zahlen der Literatur liegen bei 75–85 %, und eine Taste,
die bei jedem siebten Mal klebt, ist schlechter als eine, die einen bewussten zweiten Griff
verlangt. Das erkannte Loslassen wäre dann die *frühere*, bequemere Freigabe — und ein
verpasstes Loslassen kein Fehler, sondern nur ein späteres Ende.

## ERLEDIGT: Löse-Impuls gemessen — taugt nicht

**Befund:** eine zweite Spitze gibt es *teilweise*, bei **kurzem Pinch gar nicht**, und sie
liegt **kaum über dem Rauschen**.

**Folge:** Apples „pinch and move" mit dem Loslassen als Ende ist auf dieser Hardware nicht
umsetzbar — ein Tastenzustand, dessen Ende in der Hälfte der Fälle ausbleibt, ergibt eine
klebende Taste. Ebenso erledigt ist der lange Pinch als Rechtsklick (er müsste „gehalten"
aus dem Ausbleiben des Löse-Impulses erschliessen).

**Was daraus gebaut wurde:** der Anfang der Geste braucht das Lösen nicht. Pinch drückt die
Taste sofort; bewegt sich der Cursor um mehr als `DRAG_MOVE_PX`, wird daraus ein Ziehen,
sonst geht die Taste nach `DRAG_WINDOW_MS` wieder hoch. Beendet wird das Ziehen mit einem
zweiten Pinch. Der Doppel-Pinch als *Auslöser* ist damit weg, und mit ihm sein Zeitband.

- [ ] `DRAG_MOVE_PX` (12 px) am Gerät einstellen: zu klein → ungewolltes Ziehen beim
  Klicken, zu gross → Ziehen kommt nicht zustande. Kanal `drag` beim gewöhnlichen Klicken
  beobachten, er muss auf 0 bleiben.
- [ ] Für die Arbeit festhalten: negatives Messergebnis, das eine Entwurfsfrage entschieden
  hat — und die Erklärung, warum Doublepoint für dieselbe Funktion PPG verbaut.

## Messprotokoll des Versuchs (zur Dokumentation)

`DEBUG_SET` steht auf **`DEBUG_ENV`** — vier Kanäle: `env`, `envMax`, `gate`, `click`.
`envMax` ist der Spitzenwert seit der letzten Ausgabe und der eigentliche Messwert: die
Schleife läuft mit 209 Hz, die Ausgabe mit 50 Hz, ein env-Impuls (Zeitkonstante ~10 ms)
träfe seinen Scheitel sonst nur zufällig.

**Versuch:** Pinch bewusst eine volle Sekunde geschlossen halten, dann die Finger öffnen.
Die Haptik ist nach spätestens 160 ms vorbei — jede Spitze bei t ≈ 1000 ms kann deshalb
nur das Lösen sein. Das trennt den Löse-Impuls sauber von der Vibration, die bisher als
Erklärung für den doppelten Rechtsklick ebenso in Frage kam.

- [ ] **Gibt es die zweite Spitze überhaupt?** Nein → „pinch and move" fällt, das Ziehen
  braucht wieder eine zweite Geste.
- [ ] **Wie hoch ist `envMax` beim Lösen?** Kontakt liegt bei 0.045–0.125, Untergrund bei
  0.005–0.02. Liegt das Lösen dazwischen, braucht es eine eigene, tiefere Schwelle
  (`RELEASE_ENV`) — mit `ENV_ON` = 0.035 würde es sonst teilweise übersehen.
- [ ] **Streuung über 20 Wiederholungen.** Daran hängt zugleich, ob der lange Pinch als
  eigene Geste taugt.
- [ ] Gegenprobe mit einem *kurzen*, normalen Klick: wo landet der Löse-Impuls zeitlich
  relativ zum Kontakt? Das ist die Zahl, die `DEBOUNCE_MS` festlegt.

## OFFEN: Scroll-Achse am Geraet nachpruefen (Achsen scheinen vertauscht)

Beobachtung: im Scroll-Modus reagiert es auf Bewegung nach links/rechts statt auf
hoch/runter. Die Rechnung sagt das Gegenteil — `arm::elevDeg` liest `uy`, die Komponente
entlang der Unterarmachse, und `test_arm_orientation` prüft ausdrücklich nach, dass diese
Zahl auch bei 90° verdrehter Hand die Armneigung bleibt (Prüfung „Kreuzprodukt beider
Winkel"). Eine Drehung um die Hochachse (links/rechts) lässt `uy` unverändert.

Die Achse deshalb **nicht** blind tauschen — das bräche eine geprüfte Eigenschaft. Zuerst
messen (`DEBUG_SET = DEBUG_POINT`, Kanäle `elev`, `srate`, `pose`, `dpose`):

- [ ] Scroll-Modus betreten (ausdrehen, halten). Steht `dpose` auf 2 und läuft `srate`
  überhaupt? Wird der Modus gar nicht betreten, ist das das eigentliche Problem und die
  „Achse" nur das, was der Cursor stattdessen tut.
- [ ] Im Scroll-Modus den Unterarm **hoch/runter** neigen: `elev` muss sich ändern,
  `srate` anlaufen. Tut es das nicht, stimmt die Achse wirklich nicht.
- [ ] Im Scroll-Modus den Arm **links/rechts** schwenken: `elev` muss stehen bleiben.
  Ändert es sich, ist die Einbaulage anders als angenommen — dann gehört
  `DEBUG_SET = DEBUG_ORIENT` dazu (`angX/angY/angZ` gegen `twist`/`elev` halten), und
  erst danach wird an `ArmOrientation` etwas geändert.

## DRINGEND: das Fenster des Doppel-Pinch einstellen

Ziehen und Klick teilen sich einen Pfad: der Pinch drückt sofort, ein zweiter innerhalb
`DRAG_WINDOW_MS` macht daraus ein Ziehen. Das nutzbare Band ist `DEBOUNCE_MS` (200) bis
`DRAG_WINDOW_MS` (350) — beide Zahlen gehören **zusammen** eingestellt.

- [ ] **Der kritische Fall: löst der eigene Löse-Impuls ein Ziehen aus?** Hält man den
  Pinch länger als 200 ms geschlossen, fällt der Impuls beim Öffnen der Finger ins Fenster
  und verriegelt ein Ziehen, das niemand wollte. Kanal `drag` beim **gewöhnlichen**
  Klicken beobachten: er muss auf 0 bleiben. Springt er, `DEBOUNCE_MS` anheben (und
  `DRAG_WINDOW_MS` mit, das Band muss ≥ 100 ms bleiben — ein `static_assert` wacht darüber).
- [ ] **Trifft man das Ziehen?** Zweimal zügig pinchen, `drag` muss auf 1 gehen. Klappt es
  selten, ist das Band zu schmal: `DRAG_WINDOW_MS` erhöhen. Preis ist, dass jeder
  gewöhnliche Klick später *loslässt* (der Druck kommt weiterhin sofort).
- [ ] **Fühlt sich der Klick noch richtig an?** 350 ms zwischen Druck und Loslassen sind
  spürbar. Falls störend: `DRAG_WINDOW_MS` senken und die Trefferquote gegenhalten.
- [ ] **Doppelklick:** er ist durch diesen Entwurf grösstenteils aufgegeben — zwei schnelle
  Pinches heissen jetzt Ziehen. Übrig bleibt das schmale Band 350–500 ms. Am Kanal
  `nClick` prüfen, wie zuverlässig das ist, und für die Arbeit als bewusster Zielkonflikt
  notieren.

## Offen: Ziehen und Haptik am Gerät prüfen

Neue Teleplot-Kanäle: `drag` (0/1) und `nClick` (Zähler). Der Zähler ist nötig, weil
`click` bis zur nächsten Debug-Ausgabe latcht.

- [ ] **Ziehen:** zweimal zügig pinchen (drei Haptik-Impulse, `drag` auf 1), Text
  markieren, einmal pinchen zum Fallenlassen. Der Cursor muss während des Ziehens normal
  laufen.
- [ ] **Ein/Aus ist wieder zuverlässig?** Der tiefe Scheitelwinkel ist ausgebaut, die
  Drehachse hat wieder nur eine Bedeutung über 70°. Das war die Ursache dafür, dass Ein/Aus
  zwischenzeitlich aussetzte — gegenprüfen, dass es weg ist.
- [ ] **Der lange Ein/Aus-Puls** (200 ms) muss sich klar von jedem Klickmuster abheben und
  darf **nicht** selbst als Pinch gelesen werden — nach dem Einschalten sperrt der
  Controller die Klickerkennung dafür kurz. Kanal `drag`/`nClick` direkt nach dem
  Einschalten beobachten.
- [ ] **Die drei Sicherungen einzeln auslösen** — Ausschalten während des Ziehens, Arm
  abdrehen während des Ziehens, Pinch während des Ziehens. In allen drei Fällen muss die
  Taste nachweislich frei sein (die Textmarkierung endet).
- [ ] **Zwangsfreigabe:** 30 s ziehen lassen, ohne die Maus zu bewegen (`DRAG_MAX_MS`).
- [ ] **Erinnerungsimpuls:** brummt alle 2 s und beendet dabei **nicht** das Ziehen — die
  Sperre `DRAG_REMIND_BLIND_MS` muss die eigene Vibration abfangen.
- [ ] Alle Messungen mit `USE_BLE_HID true`: die gehaltene Tastenmaske über BLE ist der
  Pfad, der wirklich benutzt wird.

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
- [ ] **Twist-Guard einstellen:** `TWIST_K` und `TWIST_MIN_DPS` gibt es nicht mehr, aber
  der Guard selbst ist zurück als eigenes Modul (`lib/TwistGuard/`, kein
  `USE_TWIST_GUARD`-Schalter – er läuft immer mit). Parameter stehen in
  `TwistGuardTuning` (`lowDps`/`highDps`/`rateTau`/`releaseS`), am Kanal `tg`/`tgr`
  gegenprüfen, ob er beim normalen Zeigen fälschlich anschlägt. `USE_ROLL_COMP` ist
  ein eigener, aktiver Schalter für die Roll-Kompensation im Zeiger (siehe
  `CLAUDE.md`) und unabhängig vom Guard – beide unterdrücken die Verdrehung im
  Zeiger, aber auf unterschiedliche Art.
- [x] **Bi-Level-Schwelle eingestellt:** `cfg::ENV_ON` = 0.035 / `cfg::ENV_OFF` = 0.020.
  Aus der ersten Aufnahme: Untergrund ~0.005–0.02, echte Pinches 0.045–0.125. Am Kanal
  `gate` gegenprüfen, ob die Hysterese noch stimmt.
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
`lib/AirMouseState/AirMouseState.h` – zwei Achsen (Power / Pose), alle Übergänge
in einer Tabelle, keine Zustandsbits mehr im Controller verstreut. Keine
`Grab`-Achse, siehe `CLAUDE.md`.
Test: `test/test_state_machine/test_state_machine.cpp`, läuft auf dem PC, Kriterium `0 Fehler`.

```bash
g++ -std=c++14 -Wall -Wextra -I lib/AirMouseState -o "$env:TEMP/fsm.exe" test/test_state_machine/test_state_machine.cpp
```

- [ ] **Am Gerät gegenprüfen:** Kanäle `on`, `pose` (0=Point, 1=Idle, 2=Turned)
  im Teleplot. Der Automat ist bewiesen korrekt – offen ist nur, ob
  die *Erkenner* die richtigen Ereignisse liefern.
- [ ] **Ausdreh-Schwelle:** `TURN_ON_DEG` steht auf 70°, die Geste ist ~90°.
  Falls die abgedrehte Haltung zu früh anspringt, auf 75–80 anheben.

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
  klar schadet (mehr Nachfassen), zu hohe kaum. `SENS_X/Y` steht jetzt auf 110 px/Grad.
- [ ] **Beschleunigung hinterfragen:** Der Gewinn ist laut Literatur klein (3–6 %), und
  Scotto et al. 2020 fanden linear steigende Verstärkung *schlechter* als eine gute
  konstante. `ACCEL_K = 0` gegen den jetzigen Wert testen, bevor weiter optimiert wird.
- [ ] **Latenz messen:** Degradation setzt schon ab ~16 ms ein (Friston et al. 2016).
  Anteile: `MOVE_INTERVAL_US` = 9.57 ms Berichtsintervall über BLE (quantisiert auf
  zwei Takte à 4.785 ms, siehe `config.h`) + BLE-Verbindungsintervall (7.5–15 ms
  angefragt, die Gegenstelle darf ablehnen) + Filterverzögerung. Casiez et al. 2012
  rechnen mit nur 10–20 ms Budget fürs Filtern – prüfen, ob das eingehalten wird.

## Haltungs-Modus und Scrollen (neu gebaut, muss eingestellt werden)
Die Verdrehung gegen den festen Bezugspunkt (`TWIST_NEUTRAL_DEG`) wählt die Haltung:
gerade = zeigen (`Point`), abgedreht = Rechtsklick (`Turned`), nach einer Sekunde
gehalten zusätzlich Scroll-Joystick über die Armneigung (`onTwistHeld`). Unabhängig
davon erzwingt „nicht waagrecht" (`LEVEL_MAX_DEG`) immer `Idle` – nichts passiert,
egal wie die Hand verdreht ist.

- [x] ~~**Drehrichtung festlegen**~~ – hinfällig, `SCROLL_DIR` gibt es nicht mehr
  (Betragsvergleich, siehe oben).
- [ ] **Umschaltpunkte einstellen:** `TURN_ON_DEG` (70) und `TURN_OFF_DEG` (55) am
  Kanal `rtwist`/`pose` nachziehen. Zeigen darf nicht abbrechen, wenn man die Hand
  normal bewegt; die abgedrehte Haltung muss bequem erreichbar bleiben.
- [ ] **Scroll-Geschwindigkeit einstellen:** `SCROLL_GAIN` (1.2 Schritte/s pro Grad),
  `SCROLL_MAX_HZ` (25) und `SCROLL_DEAD_DEG` (3) am Kanal `srate`. `SCROLL_INVERT`
  auf `-1.f`, falls die Scroll-Richtung verkehrt herum ist.
- [x] ~~**Doppel-Pinch prüfen**~~ – entfallen, samt `DOUBLE_MS` und `lib/PinchGesture/`.
- [ ] **Rechtsklick testen:** Pinch in der Zeige-Haltung (`Point`) löst links aus, in
  der abgedrehten Haltung (`Turned`) rechts, in `Idle` (Arm nicht waagrecht) nichts.
  Zusätzlich prüfen, ob ein Pinch kurz nach dem Ausdrehen zuverlässig unterdrückt statt
  fälschlich links geklickt wird (`armOut` in `AirMouseState::onPinch`, deckt das
  Wartefenster `POSE_CALM_MS` + `MODE_DWELL_MS` + `MODE_TAU` ab).
- [ ] **Twist-Guard gegen die Ein/Aus-Geste abwägen:** Beide unterdrücken Bewegung beim
  Drehen. Prüfen, ob `TwistGuard` (`lib/TwistGuard/`, Kanäle `tg`/`tgr`) beim normalen
  Zeigen fälschlich anschlägt – dann `TwistGuardTuning::lowDps` anheben.

**Für die Arbeit:** Roll-Kompensation (`USE_ROLL_COMP`) und Twist-Guard unterdrücken
beide die Verdrehung im Zeiger, aber auf unterschiedliche Art – der Guard bremst die
Bewegung während der Drehung ganz weg, die Kompensation rechnet die Verdrehung aus der
Drehmatrix heraus, ohne die Bewegung selbst zu bremsen. Beide gegeneinander messen ist
ein fertiger Abschnitt für Kap. 6, kein offener Ausblick mehr.

## HEUTE: Aufnahmesitzung — Ablauf

Die Firmware ist dafür fertig. `COLLECT_MODE` hält den festen 209-Hz-Takt (`app.begin()`
läuft dort nicht, die IMU bleibt auf den 208 Hz aus `imu.begin()`), die Kanäle kommen aus
`feat::pack()`, und die eingebaute LED latcht ab dem ersten verpassten Abtastschritt.

**Umschalten und flashen:**

```
include/config.h:  #define COLLECT_MODE  true
                   #define DEBUG_TELEPLOT false      // sonst mischt sich Teleplot ins CSV
```
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
# danach flashen, dann:
edge-impulse-data-forwarder --frequency 209
```

Der Forwarder fragt nach den Achsennamen. In der Reihenfolge von `feat::pack()`:
`env, gyro, lax, lay, laz`. **Weicht die gemeldete Frequenz von 209 ab oder leuchtet die
LED, nicht aufnehmen** — der Datensatz wäre zeitlich gedehnt, und dem CSV sieht man das
nicht an.

### Zwei Projekte, nicht eines

- [ ] **Projekt A (produktiv):** `idle`, `negative`, `pinch`. Ersetzt den alten Datensatz,
  der doppelt ungültig ist (100 Hz statt 209, rohe statt linearer Achsen).
- [ ] **Projekt B (Sondierung):** `idle`, `hold`. Beantwortet die offene Frage, ohne das
  produktive Modell zu gefährden. Falls B trägt, wandert `hold` später nach A.

### Klassen und wie sie aufgenommen werden

| Klasse | Projekt | Aufnahme | Zweck |
|---|---|---|---|
| `pinch` | A | kurze, zügige Pinches, Arm ruhig | der Klick |
| `idle` | A + B | Hand entspannt, Arm ruhig gehalten | Grundlinie |
| `negative` | A | Tippen, Klopfen, Klatschen, Türklinke, Gehen — **und Armbewegung ohne Pinch** | Fehlauslösungen |
| `hold` | B | Pinch schliessen und über die ganze Aufnahme geschlossen halten, Arm ruhig | ist der gehaltene Pinch überhaupt sichtbar? |

Rund 40 Wiederholungen je Klasse (Grössenordnung aus putEMG; eine belegte Untergrenze gibt
es in der Literatur nicht — das ist eine der Lücken, die diese Arbeit schliessen kann).

**Armbewegung ohne Pinch ist der wichtigste Negativfall** und in keiner der recherchierten
Studien behandelt. Ohne ihn lernt das Modell, jede Bewegung sei ein Kandidat.

### Validierung der Haltungsunabhängigkeit

- [ ] Der Grossteil der `pinch`-Daten in Zeige-Haltung; zusätzlich ein Satz Pinches in der
  **abgedrehten** Haltung, der **nur ins Test-Set** kommt. Fällt die Genauigkeit dort nicht
  ab, ist die Gravitationsfreiheit der Kanäle belegt — eine belastbare Zahl für Kapitel 5
  und zugleich die Erklärung, warum der Rechtsklick vorher nicht funktionierte.

### Modellwahl (durch die Recherche entschieden)

- [ ] Verarbeitungsblock **rohe Zeitreihen statt Spectral Analysis**, Lernblock **1D-CNN**.
  Rohe Zeitreihen generalisieren bei kleinen Einzelnutzer-Datensätzen besser
  (Jaén-Vargas et al. 2022, Startsev et al. 2018); LSTM bleibt wegen des nRF52840 draussen.
- [ ] Fenster **196 ms** (41 Samples @ 209 Hz) mit 50 % Überlappung beibehalten — die
  Literatur findet 200 ms optimal, kleinere Fenster verschlechtern den F1-Wert.
- [ ] Nach dem Training `COLLECT_MODE` zurück auf `false`, Modellordner `lib/ei-model/`
  komplett ersetzen (nicht von Hand editieren).

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

## Messplan Stromsparen

Alle Messungen mit `DEBUG_TELEPLOT false`, sonst misst man den Messaufbau.
Der Kanal `vbat` steht dafür auch ohne Teleplot zur Verfügung, wenn man ihn
einzeln einschaltet.

1. **`BATTERY_VOLTS_PER_LSB` kalibrieren.** Akkuspannung mit dem Multimeter
   messen und gegen `vbat` halten, Faktor nachziehen. Alles Weitere hängt an
   dieser Zahl.
2. **`ovr` und `late` nach dem Schleifen-Umbau.** `ovr` muss bei 0 bleiben.
   Steigt er, schläft die Schleife zu lange und die feste Schrittweite stimmt
   nicht mehr. `ovr` allein genügt aber nicht — er hat einen blinden Fleck von
   zwei Takten (siehe Nebenbedingungen unten). `late` zeigt die tatsächliche
   Verspätung jedes Takts und ist der eigentliche Messwert.
3. **Stromaufnahme je Zustand**, Multimeter in Serie: AKTIV, BEREIT, SCHLAF.
   Gegen die Schätzwerte im Design halten (~2–3 mA / ~1 mA / ~0.03–0.05 mA).
4. **`WAKE_UP_THRESHOLD` einstellen.** Armheben muss wecken, ein Klopfen auf
   den Tisch nicht. Startwert 2.
5. **Aufwachen prüfen.** Arm ablegen, 60 s warten, Arm heben: Wacht es auf?
   Wie lange bis BLE wieder steht? Funktioniert die Drehgeste unmittelbar
   danach, oder schlägt das Einschwingen durch (`rtwist` beobachten)?
6. **Falsches Einschlafen ausschliessen.** Maus einschalten, Cursor zwei
   Minuten ruhig auf einem Ziel halten. Sie darf nicht verschwinden.
7. **Entladekurve**, je einmal für den Stand vor und nach diesem Plan, unter
   gleichem Nutzungsmuster. Das ist die belastbare Zahl für die Arbeit —
   Laufzeit vorher gegen nachher.
8. **Offene Messpunkte, bewusst nicht entschieden:** BLE-Verbindungsintervall
   (7.5–15 ms, teuer aber latenzentscheidend) und Sendeleistung
   (`setTxPower(4)`, Maximum). Beide erst messen, dann entscheiden.

**Nebenbedingungen aus den Code-Reviews — vor der Messreihe lesen, nicht erst
danach:**

- **Jede Strommessung und jeder Reconnect-Test läuft mit `USE_BLE_HID true`.**
  Im committeten USB-Build sind `radioOff()`/`radioOn()` leere Hüllen
  (`MouseHID.h`) — dort zu messen heisst, eine Funktion zu messen, die es gar
  nicht gibt.
- **`ovr` schlägt erst bei ZWEI verpassten Takten aus** — in AKTIV also erst ab
  9570 µs Verspätung, in BEREIT erst ab 38460 µs. Grund: `nextSample_us` ist beim
  Überlauftest schon weitergestellt, die Bedingung
  `now_us - nextSample_us > tickUs` misst deshalb gegen `2 × tickUs` ab dem
  ursprünglich geplanten Zeitpunkt. Der blinde Fleck ist in BEREIT viermal so breit
  wie in AKTIV, ausgerechnet im Zustand, in dem meistens gemessen wird. Feinere
  Verschiebungen bleiben unsichtbar — „`ovr` bleibt 0" ist für sich allein also
  kein Beleg dafür, dass die schlafende Schleife den Takt wirklich hält. Dafür
  gibt es jetzt den Kanal **`late`** (Verspätung jedes Takts in µs, vor dem
  Weiterstellen gemessen). Erwartungswert: Stufen von rund 977 µs, nicht glatte
  Mikrosekunden — `micros()` kommt aus dem 1024-Hz-FreeRTOS-Tick, `dwt_enable()`
  wird nirgends gerufen. Genau diese Quantisierung ist der Beleg dafür.
- **Enttäuscht die SCHLAF-Zahl, zuerst den Interrupt verdächtigen, nicht die
  IMU-Konfiguration.** `attachInterrupt` läuft über den GPIOTE-Event-Modus,
  der seine Erkennungsschaltung getaktet hält und dadurch messbar mehr
  Ruhestrom kostet als der stromsparende SENSE/PORT-Mechanismus.

  **Der Wechsel auf SENSE ist aber teurer, als er klingt.** Der
  `GPIOTE_IRQHandler` des Adafruit-Kerns iteriert ausschliesslich über
  `EVENTS_IN[ch]` und hat **keinen PORT-Ereignispfad**; der Handler ist zudem
  nicht `weak`, lässt sich also nicht einfach überschreiben. SENSE zu benutzen
  hiesse, einen eigenen ISR gegen den des Kerns zu schreiben — für eine
  Maturaarbeit ein unverhältnismässiger Eingriff mit echtem Regressionsrisiko.

  **Die billige Alternative:** den ISR ganz weglassen und die Schleife INT1
  schlafend abfragen (`delay(250)` zwischen den Abfragen). Vier kurze
  Aufwachvorgänge pro Sekunde kosten weit weniger als der dauernd getaktete
  GPIOTE-Ereignismodus. Der Handel: man gibt die Eigenschaft „es wird gar nichts
  gepollt" auf — und die ist nichts wert, wenn der Mechanismus, der sie erhält,
  mehr kostet als das Pollen selbst.
- **`delay()` schläft in diesem Kern nicht immer.** Die Implementierung ruft
  zuerst `TinyUSB_Device_FlushCDC()` und kehrt **ohne jedes `vTaskDelay` zurück**,
  wenn das Leeren des Puffers das ganze angeforderte Intervall verbraucht hat
  (`cores/nRF5/delay.c`). Mit `DEBUG_TELEPLOT true` und vollem CDC-Puffer kann die
  Schleife dadurch einen kompletten Takt lang durchdrehen statt zu schlafen. Ein
  überraschend hoher Stromwert beim Einstellen gehört zuerst gegen diesen
  Mechanismus geprüft — und nicht der IMU-Konfiguration angelastet.
- **Nach dem Aufwachen braucht das Gyroskop seine Einlaufzeit.** Am Teleplot
  prüfen, dass in der ersten Sekunde nach dem Wecken kein falsches `Toggle`
  oder `Held` auftritt.
- **Der Gyro-Bias-Lerner sieht während dieser Einlaufzeit nahezu null** und
  verschiebt den Nullpunkt bei jedem Aufwachen ein kleines Stück. Nach vielen
  Weck-Zyklen einmal nachsehen, ob der Cursor davon merklich driftet.
- **Der 71.58-Minuten-Überlauf ist erledigt** (Whole-Branch-Review). Der
  Controller rechnet nicht mehr mit `now_us / 1000`, sondern mit `millis()`.
  `micros()` läuft bei 2³² über, der Quotient daraus also schon bei 4 294 967 —
  und dann ist die vorzeichenlose Differenzarithmetik ungültig, auf der jeder
  Zeitgeber beruht. Der Langzeit-Lauftest über 75 Minuten bleibt trotzdem
  sinnvoll, jetzt aber als Gegenprobe: es darf **kein** verirrtes Einschlafen
  mehr auftreten. (Auch `millis()` ist auf diesem Kern kein sauberer
  2³²-Zähler — es stammt aus dem 32-Bit-Tick bei 1024 Hz und springt bei
  4 194 304 000 ms, also nach rund 48.5 Tagen, zurück. Für dieses Gerät
  irrelevant, aber der Vollständigkeit halber notiert.)

- **~~`MadgwickAHRS::alignToGravity()`~~ — gebaut, heisst `seedFromAccel()`.**
  Erledigt. Die Lage wird beim Start und nach jedem Aufwachen direkt aus einem
  Messwert des Beschleunigungssensors gesetzt, statt dorthin zu konvergieren.
  Solange kein Messwert nach reiner Schwerkraft aussieht
  (`|accMag - 1 g| > cfg::SEED_ACC_TOL`), wird jeden Takt neu gesetzt; in dem
  Takt, in dem der Arm ruhig wird, rastet die Schaetzung ein.

  Damit sind `MADGWICK_BETA_FAST`, `SleepTuning::settleMs`, `SleepEvent::Settled`
  und `SleepPolicy::settling()` ersatzlos entfallen — genau die zwei gekoppelten
  Zahlen, die niemand miteinander multipliziert hatte. Die Drehgeste haengt jetzt
  an `oriented_` statt an einem Zeitfenster und ist nach dem Aufwachen sofort da.
  Geprueft von `test_madgwick_seed` (alle sechs Achsenlagen).

  **Offen bleibt der Abnahmetest am Geraet**: Schritt 5 des Messplans Stromsparen,
  `rtwist` unmittelbar nach dem Wecken.
