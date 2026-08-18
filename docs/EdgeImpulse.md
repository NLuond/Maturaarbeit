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
doppelt ungültig: mit 100 Hz statt 209 Hz aufgenommen **und** mit den rohen statt den
linearen Beschleunigungsachsen.

## Was die Firmware fest vorgibt

Drei Dinge sind nicht verhandelbar, weil `static_assert`s in
[PinchClassifier.h](../lib/PinchClassifier/PinchClassifier.h) sie erzwingen — weicht das
Modell ab, **bricht der Build**, und zwar mit einer klaren Meldung statt mit stillem
Fehlverhalten:

| | Wert | Warum |
|---|---|---|
| Abtastrate | **209 Hz** | `cfg::SAMPLE_INTERVAL_US` = 4785 µs, Toleranz ±25 µs (also 208–210 Hz) |
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
#define COLLECT_MODE    true
#define DEBUG_TELEPLOT  false   // sonst mischen sich ">name:wert"-Zeilen ins CSV
```

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
```

Dann flashen. `COLLECT_MODE` hält den festen 209-Hz-Takt und initialisiert weder HID noch
Controller — die IMU bleibt auf den 208 Hz aus `imu.begin()`.

## Schritt 2 — Forwarder verbinden

```powershell
edge-impulse-data-forwarder
```

Er fragt der Reihe nach:

- **Achsennamen:** `env, gyro, lax, lay, laz` — die Reihenfolge ist die von `feat::pack()`
  und darf nicht vertauscht werden.
- **Gerätename:** frei wählbar.

**Die gemeldete Frequenz muss 209 Hz sein** (208–210 sind toleriert). Meldet er etwas
anderes, nicht aufnehmen — dann stimmt der Schleifentakt nicht, und das ist genau der
Fehler, der den alten Datensatz unbrauchbar gemacht hat.

**Zweite Kontrolle: die eingebaute LED.** Sie geht an, sobald ein Abtastschritt verpasst
wurde, und bleibt bis zum Reset an. Leuchtet sie während oder nach einer Aufnahme, ist der
Datensatz zeitlich gedehnt — verwerfen und neu machen. Dem CSV sieht man das nicht an.

## Schritt 3 — Daten aufnehmen

Klassen — Namen exakt so. **Nur `pinch` ist Schnittstelle** (`strcmp` in der Firmware), die
Gegenklassen darf man frei benennen und zusammenfassen. Das eingesetzte Modell 1084395
benutzt zwei Klassen (`non_pinch`, `pinch`); drei Klassen haben denselben F1-Wert
geliefert, dafuer aber den diagnostisch wertvollen Einzelwert `negative → pinch` sichtbar
gemacht. Fuer die schriftliche Arbeit lohnt sich deshalb ein zusaetzlicher Lauf mit drei
Klassen, auch wenn eingesetzt wird, was besser abschneidet.

| Klasse | Aufnahme | Menge |
|---|---|---|
| `pinch` | zügige Pinches, Arm ruhig gehalten | ~40 Ereignisse |
| `idle` | Hand entspannt, Arm ruhig, nichts tun | ~2 Minuten |
| `negative` | Tippen, Klopfen, Klatschen, Türklinke, Gehen — **und Armbewegung ohne Pinch** | ~3 Minuten |

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

## Schritt 4 — Impulse bauen

*Create impulse*:

| Feld | Wert |
|---|---|
| Window size | **196 ms** |
| Window increase | **98 ms** (50 % Überlappung) |
| Frequency | **209 Hz** (kommt aus den Daten, hier nur prüfen) |
| Zero-pad data | an |

196 ms ergeben bei 209 Hz genau 41 Samples — dieselbe Fensterlänge wie beim bisherigen
Modell. Die Literatur findet 200 ms mit 50 % Überlappung optimal; kleinere Fenster
verschlechtern den F1-Wert.

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
- `ei_us`: wie lange dauert eine Inferenz? Muss klar unter 4785 µs bleiben, sonst reisst
  der Takt.
- `ei_err`: muss 0 bleiben.
- `nGyro`: steigt er beim Pinchen, blockiert der Gyro-Guard (`PINCH_GYRO_GUARD` = 100 °/s)
  die Klicks, die er durchlassen soll — das ist ein bekannter offener Punkt.
- **Fehlauslösungen pro Stunde:** eine Stunde normal am Rechner arbeiten, nicht bewusst
  pinchen, danach `nClick` ablesen. Diese Zahl berichtet keine der recherchierten Studien,
  und für eine Maus ist sie die entscheidende.
