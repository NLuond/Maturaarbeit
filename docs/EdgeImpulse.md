# Das Modell in Edge Impulse trainieren

> **Stand:** Das erste Modell nach dieser Anleitung ist gebaut und eingesetzt —
> Studio-Projekt **1084395**, 2 Klassen (`non_pinch`, `pinch`), Fenster **40 Samples**
> (191 ms), Rohsignal + 1D-CNN, int8. Validierung: Pinch-Recall 87.5 %, Precision ~94.8 %,
> F1 0.91, AUC 0.94, Falsch-Positive 0.1 %. Auf dem Geraet 3 ms Inferenz, 4 KB RAM,
> 32 KB Flash.
>
> Die Anleitung unten bleibt gueltig — die einzige Abweichung ist die Fensterlaenge
> (191 statt 196 ms, also 40 statt 41 Samples) und dass zwei statt drei Klassen benutzt
> wurden. Zu beidem stehen unten Anmerkungen.

Schritt-für-Schritt-Anleitung für das erste Modell des neuen Datensatzes. Der alte ist
doppelt ungültig: mit 100 Hz statt der vollen Sensorrate aufgenommen **und** mit den
rohen statt den linearen Beschleunigungsachsen.

## Was die Firmware fest vorgibt

Drei Dinge sind nicht verhandelbar, weil `static_assert`s in
[PinchClassifier.h](../lib/PinchClassifier/PinchClassifier.h) sie erzwingen — weicht das
Modell ab, **bricht der Build**, und zwar mit einer klaren Meldung statt mit stillem
Fehlverhalten:

| | Wert | Warum |
|---|---|---|
| Abtastrate | **208 Hz** | `cfg::SAMPLE_INTERVAL_US` = 4808 µs = 1/`ACCEL_ODR_HZ`, Toleranz ±1 % |
| Kanäle | **genau 5** | `feat::CHANNELS`, Reihenfolge aus `feat::pack()` |
| Klassenname | **`pinch`**, klein | `strcmp(label, "pinch")` in `isPinch()` |

Die Fensterlänge ist dagegen frei — die Puffer richten sich danach. 196 ms sind trotzdem
die richtige Wahl (siehe Schritt 4).

Die Entscheidungsschwelle liegt **nicht** in Edge Impulse, sondern in der Firmware
(`cfg::ML_CONFIDENCE` = 0.65). Was du im Studio als Confidence-Threshold einstellst, ist
für das Gerät ohne Belang.

## Die fünf Kanäle — und warum du an ihnen nichts rechnen musst

Alles, was man üblicherweise vor dem Training von Hand macht, passiert bereits in der
Firmware, in `feat::pack()` ([PinchFeatures.h](../lib/PinchFeatures/PinchFeatures.h)):

| # | Kanal | Was er ist | Erdbeschleunigung? |
|---|---|---|---|
| 0 | `env` | Hüllkurve der Erschütterung, Hochpass ab 30 Hz → Betrag → Tiefpass 15 Hz | durch den Hochpass **entfernt** |
| 1 | `gyro` | `gyroSum / 100` | nicht betroffen (Drehrate) |
| 2–4 | `lax, lay, laz` | **lineare** Beschleunigung | in `ImuReader` per Tiefpass **abgezogen** |

**Die Erdbeschleunigung ist schon weg — rechne sie nicht noch einmal heraus.** Genau das
ist der Grund, warum ein einziger Datensatz beide Handhaltungen abdeckt: für das Modell
sieht ein Pinch in der Zeige-Haltung gleich aus wie in der abgedrehten. Die alten Daten
benutzten die *rohen* Achsen und waren auch deshalb unbrauchbar.

**Normieren musst du ebenfalls nichts, und du darfst es nicht ausserhalb tun.** Der
Verarbeitungsblock läuft auf dem Gerät genauso wie beim Training — was du im Studio
einstellst, ist automatisch konsistent. Würdest du die CSV dagegen vorher in einem Skript
normieren (z. B. z-Standardisierung je Aufnahme), sähe das Gerät später andere Zahlen als
das Modell gelernt hat. Also: **Scale axes = 1.0** im `Raw Data`-Block, sonst nichts.

Die Grössenordnungen sind bereits absichtlich angeglichen — `gyroSum` wird in
`feat::pack()` durch 100 geteilt, „auf eine aehnliche Groessenordnung wie g".

> **Der eine offene Punkt dazu:** `env` bewegt sich zwischen 0.005 und 0.125, `lax/lay/laz`
> dagegen um ±2. Der informativste Kanal ist damit rund zwanzigmal kleiner als die
> anderen. Der `Raw Data`-Block kennt nur *einen* Faktor für alle Achsen — eine
> kanalweise Anpassung ginge ausschliesslich in `feat::pack()`, und die macht **jedes**
> bisher trainierte Modell ungültig. Weil ohnehin neu aufgenommen wird, wäre jetzt der
> einzige billige Moment dafür. Empfehlung trotzdem: **erst so lassen.** Die erste Schicht
> des CNN lernt eine kanalweise Gewichtung von selbst, und eine Variable nach der anderen
> zu ändern ist mehr wert als ein vermuteter Vorteil. Enttäuscht das Modell, ist `env`
> hochskalieren der erste gezielte Versuch.

---

## Schritt 1 — Gerät auf Aufnahme umstellen

In [config.h](../include/config.h):

```cpp
#define COLLECT_MODE     true
#define DEBUG_TELEPLOT   false   // sonst mischen sich ">name:wert"-Zeilen ins CSV
#define COLLECT_OVER_BLE false   // true nimmt kabellos auf, siehe Schritt 2b
```

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
```

Dann flashen. `COLLECT_MODE` hält den festen 208-Hz-Takt und initialisiert weder HID noch
Controller — die IMU bleibt auf den 208 Hz aus `imu.begin()`.

## Schritt 2 — Forwarder verbinden

```powershell
edge-impulse-data-forwarder
```

Er fragt der Reihe nach:

- **Achsennamen:** `env, gyro, lax, lay, laz` — die Reihenfolge ist die von `feat::pack()`
  und darf nicht vertauscht werden.
- **Gerätename:** frei wählbar.

**Die gemeldete Frequenz muss 208 Hz sein.** Meldet er etwas anderes, nicht aufnehmen —
dann stimmt der Schleifentakt nicht, und das ist genau der Fehler, der den alten
Datensatz unbrauchbar gemacht hat.

> **Änderung gegenüber dem bisherigen Datensatz:** der Takt lag früher bei 209 Hz
> (4785 µs) und damit rund 0,5 % über der Rate des Sensors, der jede Handbewegung nur
> alle 4808 µs neu abtastet. Die Schleife las dadurch etwa einmal pro Sekunde denselben
> Messwert zweimal. Das aktuell eingebaute Modell ist mit 209 Hz im Studio hinterlegt
> und läuft trotzdem: 0,5 % sind über ein 40er-Fenster rund 0,9 ms. Sobald ein Modell
> aus 208-Hz-Daten trainiert wird, stimmen Takt und Modell exakt überein.

**Zweite Kontrolle: die eingebaute LED.** Sie geht an, sobald ein Abtastschritt verpasst
wurde — über Funk auch, wenn eine Zeile nicht abgegeben werden konnte — und bleibt bis
zum Reset an. Leuchtet sie während oder nach einer Aufnahme, ist der Datensatz gedehnt
oder lückenhaft — verwerfen und neu machen. Dem CSV sieht man das nicht an.

## Schritt 2b — Aufnahme über Bluetooth, ohne Kabel

Am Kabel zieht jede Armbewegung, und für die `pinch`-Aufnahmen ist genau das eine
Störung, die man nicht im Datensatz haben will. Über BLE geht dasselbe CSV kabellos.

**Der Ablauf mit allen Befehlen steht in [Aufnehmen.md](Aufnehmen.md).** Hier nur, was
sich gegenüber Schritt 2 ändert — und warum.

### Der Umweg über einen virtuellen COM-Port

Der Forwarder kann ausschliesslich einen **echten** COM-Port öffnen (Node-`serialport`,
kein TCP, keine Pipes), und BLE taucht unter Windows nicht als COM-Port auf. Dazwischen
steht deshalb eine Brücke: `tools/ble_collect_bridge.py` liest den Nordic UART Service und
schreibt in ein virtuelles COM-Paar (com0com), aus dessen anderem Ende der Forwarder liest.
Für ihn sieht das aus wie ein serielles Gerät.

Auf der Firmware-Seite ist es ein zweiter Schalter:

```cpp
#define COLLECT_MODE     true
#define COLLECT_OVER_BLE true
```

### Der Zähler vor jeder Zeile

Über Funk trägt jede Zeile zusätzlich einen 16-Bit-Zähler, den die Brücke prüft und
wieder abstreift. Ohne ihn wäre ein verlorener BLE-Brocken unsichtbar und dehnte den
Datensatz still — derselbe Fehler wie damals bei 209 Hz, nur schwerer zu finden.

Bei einer Lücke meldet die Brücke Uhrzeit und Zahl der fehlenden Samples und **läuft
weiter**; am Ende zieht sie Bilanz. Sie bricht nicht ab, weil ein verlorener Brocken nicht
die Sitzung unbrauchbar macht, sondern nur die Aufnahme, die gerade lief — und die sagt
die Uhrzeit.

Wie die Funkstrecke ausgemessen wurde und warum der Sendepuffer des Geräts vergrössert
werden musste: [Programmcode.md](Programmcode.md), Abschnitt 11.

### Die Frequenz wird vorgegeben, nicht erkannt

```powershell
edge-impulse-data-forwarder --frequency 208
```

Über BLE kommen die Zeilen in Schüben alle ~8 ms an; die Schätzung des Forwarders schwankt
entsprechend und legte sonst womöglich 205 oder 211 Hz im Studio ab. Die wahre Rate ist
ohnehin eine Konstante der Firmware (`cfg::SAMPLE_INTERVAL_US` = 4808 µs), keine
Messgrösse.

**Die 208-Hz-Kontrolle aus Schritt 2 wandert damit zur Brücke:** ihr Mittelwert bei
`Zeilen/s` muss ~208 sein, und es darf keine Lückenmeldung kommen. Der Einzelwert schwankt
um ±6 — das ist das Sekundenfenster der Anzeige, kein Taktfehler.

---

## Schritt 3 — Daten aufnehmen

Klassen — Namen exakt so. **Nur `pinch` ist Schnittstelle** (`strcmp` in der Firmware), die
Gegenklassen darf man frei benennen und zusammenfassen. Das eingesetzte Modell 1084395
benutzt zwei Klassen (`non_pinch`, `pinch`); drei Klassen haben denselben F1-Wert
geliefert, dafuer aber den diagnostisch wertvollen Einzelwert `negative → pinch` sichtbar
gemacht. Fuer die schriftliche Arbeit lohnt sich deshalb ein zusaetzlicher Lauf mit drei
Klassen, auch wenn eingesetzt wird, was besser abschneidet.

| Klasse | Aufnahme | Menge |
|---|---|---|
| `pinch` | zügige Pinches, Arm ruhig gehalten | ~150 Ereignisse |
| `idle` | Hand entspannt, Arm ruhig, nichts tun | ~1 Minute |
| `negative` | Tippen, Klopfen, Klatschen, Türklinke, Gehen — **und Armbewegung ohne Pinch** | ~2 Minuten |

Diese Mengen sind gegenüber dem ersten Datensatz verschoben — deutlich mehr `pinch`,
etwas weniger vom Rest. Warum, steht in Schritt 3b: gerechnet wird in Fenstern, und ein
einzelner Pinch liefert nur zwei bis drei davon.

**Der wichtigste Negativfall ist die Armbewegung ohne Pinch.** Er steht in keiner der
recherchierten Studien, und ohne ihn lernt das Modell, jede Bewegung sei ein Kandidat.
Nimm ihn ausdrücklich auf: zeigen, herumfahren, Arm heben und senken — alles ohne zu
pinchen.

### Kurze Ereignisse richtig labeln

Ein Pinch dauert rund 200 ms, eine Aufnahme mehrere Sekunden. Würdest du zehn Sekunden mit
acht Pinches als `pinch` labeln, wären die meisten Fenster darin **Ruhe mit falschem
Etikett** — das Modell lernte dann, Ruhe sei ein Pinch.

Deshalb: 10-Sekunden-Aufnahmen mit acht bis zehn Pinches machen, danach im Studio unter
*Data acquisition* die Aufnahme anklicken → **Split sample**. Segmentlänge auf rund
**300 ms** stellen, die vorgeschlagenen Segmente auf die Impulse ausrichten, übernehmen.
Daraus werden einzelne, sauber etikettierte Proben.

Für `idle` und `negative` ist das nicht nötig — dort ist jedes Fenster repräsentativ, also
einfach lange Aufnahmen am Stück.

### Haltungsunabhängigkeit belegen

Der Grossteil der `pinch`-Daten in der Zeige-Haltung. Zusätzlich ein Satz Pinches in der
**abgedrehten** Haltung, den du im Studio ausdrücklich ins **Test-Set** verschiebst (Sample
anklicken → *Move to test set*). Fällt die Genauigkeit dort nicht ab, ist belegt, dass die
gravitationsfreien Kanäle `lax/lay/laz` die Handhaltung unsichtbar machen — eine
belastbare Zahl für die Arbeit und zugleich die Erklärung, warum der Rechtsklick früher
nicht funktionierte.

Am Ende unter *Data acquisition* die Verteilung prüfen: Train/Test rund 80/20, und die drei
Klassen sollten ähnlich viele **Fenster** ergeben (nicht Sekunden).

## Schritt 3b — Wie viel, und warum zwei Datensätze

### Gerechnet wird in Fenstern, nicht in Sekunden

Bei 196 ms Fenster und 98 ms Schritt ergibt **eine Sekunde Aufnahme rund 10 Fenster**. Ein
einzelner Pinch von ~300 ms ergibt nach dem *Split sample* aber nur **zwei bis drei**.

Damit ist sichtbar, wo der Engpass liegt: 40 Pinches sind rund 100 Fenster und stehen drei
Minuten `negative` mit rund 1800 gegenüber. Das eingesetzte Modell (F1 0.91, Recall 87.5 %)
ist mit dieser Schieflage entstanden. Die nächstliegende Verbesserung ist deshalb **mehr
`pinch`**, nicht mehr von allem.

| Klasse | bisher | Ziel | ergibt rund |
|---|---|---|---|
| `pinch` | ~40 Ereignisse | **~150 Ereignisse** | 350–450 Fenster |
| `idle` | ~2 min | ~1 min | ~600 Fenster |
| `negative` | ~3 min | ~2 min | ~1200 Fenster |

`negative` darf grösser bleiben — die Klasse ist die vielfältigste. Massgeblich ist am Ende
die **Fensterzahl** unter *Data acquisition*, nicht die Minutenzahl.

150 Pinches sind rund 15 Aufnahmen zu 10 Sekunden, mit der BLE-Brücke also etwa eine
Viertelstunde.

### Zwei Projekte, nicht zwei Ordner

Edge Impulse trainiert immer auf dem **ganzen** Trainingsset eines Projekts; einen Teil
davon auszuschliessen geht nicht. Wer einen eigenen und einen gemischten Datensatz
vergleichen will, braucht deshalb **zwei Projekte**:

| Projekt | Inhalt |
|---|---|
| `AirMouse-eigen` | nur die eigenen Aufnahmen |
| `AirMouse-gemischt` | dieselben Aufnahmen **plus** die anderer Personen |

Der Weg von einem ins andere geht über den Export: *Dashboard → Export data* lädt die
Rohdaten als ZIP; entpackt gehen sie mit dem Uploader ins zweite Projekt. Der API-Key des
Zielprojekts steht dort unter *Dashboard → Keys*:

```powershell
edge-impulse-uploader --api-key ei_... --category split --directory pfad\zum\export
```

Wer später noch wissen will, von wem eine Probe stammt, gibt das beim Hochladen mit:

```powershell
edge-impulse-uploader --api-key ei_... --metadata person=lena --directory pfad\zu\lenas\daten
```

Pro zusätzlicher Person reicht **weniger als bei einem selbst** — gebraucht wird Vielfalt,
nicht Tiefe: rund 50 Pinches, 30 s `idle`, 60 s `negative`. Vier Personen verdoppeln den
Datensatz und bringen vier verschiedene Handgrössen, Pinch-Stärken und Armhaltungen hinein.

### Die Regel, an der die Zahlen hängen

**Beim gemischten Datensatz wird das Test-Set nach Person getrennt, nicht zufällig.**

Teilt Edge Impulse 80/20 zufällig auf, landen Fenster **derselben** Aufnahme in Training
und Test. Weil sich benachbarte Fenster zu 50 % überlappen, prüft das Modell dann faktisch
an Daten, die es kennt — die Genauigkeit sieht hervorragend aus und sagt nichts darüber,
ob es bei einer neuen Person funktioniert.

Richtig ist: **eine Person vollständig ins Test-Set**, alle anderen ins Training
(*Data acquisition* → Probe anklicken → *Move to test set*, oder gleich mit
`--category testing` hochladen).

Damit werden aus den zwei Projekten drei Zahlen, und die sind eine fertige Auswertung:

| Was | Modell | Test an | Beantwortet |
|---|---|---|---|
| persönlich | `eigen` | eigenen Daten | Wie gut geht es für den Träger? |
| gemischt, vertraut | `gemischt` | eigenen Daten | Schaden fremde Daten dem Träger? |
| gemischt, fremd | `gemischt` | zurückgehaltener Person | Funktioniert es bei einer **neuen** Person? |

Die dritte Zeile ist die interessanteste und in den recherchierten Studien meist die
schwächste — genau deshalb lohnt es sich, sie selbst zu messen.

---

## Schritt 4 — Impulse bauen

*Create impulse*:

| Feld | Wert |
|---|---|
| Window size | **196 ms** |
| Window increase | **98 ms** (50 % Überlappung) |
| Frequency | **208 Hz** (kommt aus den Daten, hier nur prüfen) |
| Zero-pad data | an |

196 ms ergeben bei 208 Hz 40 Samples — dieselbe Fensterlänge wie beim bisherigen
Modell, das bei 209 Hz auf 191 ms kam. Die Literatur findet 200 ms mit 50 %
Überlappung optimal; kleinere Fenster verschlechtern den F1-Wert.

**Processing block: `Raw Data`** — nicht Spectral Analysis. Alle fünf Achsen auswählen,
Scaling 1.0. Das ergibt 41 × 5 = **205 Merkmale**.

Warum roh und nicht spektral: rohe Zeitreihen generalisieren bei kleinen
Einzelnutzer-Datensätzen besser als handgebaute Spektralmerkmale (Jaén-Vargas et al. 2022;
Startsev et al. 2018). Das bisherige Modell benutzte Spectral Analysis + MLP — mit rohen
Daten wird daraus ein echtes 1D-CNN, was zugleich das saubere Narrativ für Kapitel 2.4 ist.

**Learning block: `Classification`.**

## Schritt 5 — Trainieren

Im Learning block auf die Expertenansicht (Keras) umstellen und die **1D-Convolution**-
Architektur wählen. Startwerte:

| Parameter | Wert |
|---|---|
| Training cycles | 100 |
| Learning rate | 0.005 |
| Validation set size | 20 % |
| Data augmentation | aus (Zeitreihen, nicht Bilder) |

Gegen Overfitting bei kleinem Datensatz: kleines Netz, Dropout **oder** L2 — nicht beides
und nicht zu stark, sonst kippt es in Underfitting (Santos & Papa 2022).

Ziel ist nicht die höchste Zahl, sondern eine **belastbare** Confusion-Matrix. Achte
besonders darauf, wie oft `negative` als `pinch` durchgeht — das sind die Fehlauslösungen,
die man später am Gerät spürt.

## Schritt 6 — Bewerten

*Model testing* gegen das Test-Set laufen lassen. Für die Arbeit notieren:

- Genauigkeit gesamt und je Klasse
- die Confusion-Matrix, besonders `negative` → `pinch`
- Genauigkeit auf den Pinches in **abgedrehter** Haltung (Haltungsunabhängigkeit)
- RAM und Flash aus dem EON Tuner bzw. der Deployment-Seite
- Inferenzzeit — am Gerät später am Teleplot-Kanal `ei_us` gegenprüfen

Lohnend als A/B für Kapitel 2.4: dasselbe Datenmaterial einmal mit `Raw Data` + 1D-CNN und
einmal mit `Spectral Analysis` + MLP trainieren und beide Zahlen gegenüberstellen. Das
kostet zehn Minuten und ist ein fertiger Vergleichsabschnitt.

## Schritt 7 — Exportieren und einbauen

*Deployment* → **C++ library** → Quantisierung **int8** → *Build*.

Das ZIP enthält `edge-impulse-sdk/`, `model-parameters/` und `tflite-model/`.

```powershell
Remove-Item -Recurse -Force c:\dev\Maturaarbeit\lib\ei-model\*
# den Inhalt des ZIP nach lib/ei-model/ entpacken
```

Den Ordner **vollständig ersetzen**, nicht mischen — und nichts darin von Hand editieren.

## Schritt 8 — Verifizieren

```cpp
#define COLLECT_MODE    false
#define DEBUG_TELEPLOT  true
#define DEBUG_SET       DEBUG_PINCH
```

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
```

Kompiliert es, stimmen Abtastrate und Kanalzahl — dafür sind die `static_assert`s da.
Bricht es ab, sagt die Meldung welches von beidem.

Am Gerät dann:

- `p_ml` beim Pinchen: steigt der Wert über 0.65 (`cfg::ML_CONFIDENCE`)?
- `ei_us`: wie lange dauert eine Inferenz? Muss klar unter 4808 µs bleiben, sonst reisst
  der Takt.
- `ei_err`: muss 0 bleiben.
- `nGyro`: steigt er beim Pinchen, blockiert der Gyro-Guard (`PINCH_GYRO_GUARD` = 100 °/s)
  die Klicks, die er durchlassen soll — das ist ein bekannter offener Punkt.
- **Fehlauslösungen pro Stunde:** eine Stunde normal am Rechner arbeiten, nicht bewusst
  pinchen, danach `nClick` ablesen. Diese Zahl berichtet keine der recherchierten Studien,
  und für eine Maus ist sie die entscheidende.
