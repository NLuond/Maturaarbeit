# Kurzanleitung: Trainingsdaten über Bluetooth aufnehmen

Der Spickzettel für den Aufnahmetag — nur der Ablauf. Warum es so läuft, steht in
[EdgeImpulse.md](EdgeImpulse.md) (Schritt 2b und 3b); wie die Brücke gebaut ist, in
[Programmcode.md](Programmcode.md), Abschnitt 11.

---

## Einmalig pro Rechner

### 1. Virtuelles COM-Paar

Der Forwarder kann nur einen echten COM-Port öffnen; BLE ist unter Windows keiner. com0com
legt ein Paar an: die Brücke schreibt in den einen, der Forwarder liest den anderen.

Erst nachsehen, ob es schon steht:

```powershell
py -c "import serial.tools.list_ports as l; print([p.device for p in l.comports()])"
```

Sind zwei zusätzliche COM-Nummern da — auf diesem Rechner **COM10** und **COM11** —, ist
dieser Schritt erledigt.

Sonst PowerShell **als Administrator** öffnen und den signierten Installer laufen lassen;
die unsignierte Version lehnt Windows 11 ab:

```powershell
& "$env:USERPROFILE\Downloads\com0com-signed\Setup_com0com_v3.0.0.0_W7_x64_signed.exe"
```

Vorgaben übernehmen, die Treiberfrage bestätigen. **Der Installer legt das Paar selbst an**,
mit automatisch vergebenen Nummern. Welche zusammengehören, zeigt:

```powershell
Get-CimInstance Win32_PnPEntity | Where-Object Name -match "serial port emulator" | Select-Object Name
```

`CNCA1` und `CNCB1` sind die beiden Enden desselben Paars. Nur falls gar keine COM-Nummern
auftauchen, sondern bloss `CNCA0`/`CNCB0`, von Hand nachlegen — `setupc.exe` liest seine
`.inf` aus dem **aktuellen** Verzeichnis, also erst dorthin wechseln:

```powershell
cd "C:\Program Files (x86)\com0com"
.\setupc.exe install PortName=COM10 PortName=COM11
```

### 2. Python-Pakete

```powershell
py -m pip install -r tools/requirements.txt
```

**Überall `py`, nicht `python`.** PlatformIO bringt einen eigenen Python 3.11 mit, der in
manchen Terminals vorne steht und die Pakete nicht hat.

---

## Vor jeder Aufnahmesitzung

Die Schalter in [config.h](../include/config.h) prüfen:

```cpp
#define COLLECT_MODE     true
#define COLLECT_OVER_BLE true
#define DEBUG_TELEPLOT   false   // sonst mischen sich ">name:wert"-Zeilen ins CSV
```

Bauen und flashen, **im Projektverzeichnis** — sonst findet PlatformIO die
`platformio.ini` nicht:

```powershell
cd c:\dev\Maturaarbeit
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -t upload
```

Das Gerät wirbt danach als serielles BLE-Gerät, nicht mehr als Maus. **Nicht in den
Windows-Bluetooth-Einstellungen koppeln** — die Brücke verbindet sich selbst.

---

## Aufnehmen

Zwei Fenster. **Beide bleiben offen, solange aufgenommen wird** — beendet man die Brücke,
liest der Forwarder ins Leere und meldet `0 samples`, ohne zu merken, dass die Quelle weg
ist.

**Fenster 1 — die Brücke.** Warten, bis „Verbunden mit …" erscheint:

```powershell
cd c:\dev\Maturaarbeit
py tools/ble_collect_bridge.py --port COM10
```

**Fenster 2 — der Forwarder.** Beim ersten Start fragt er nach E-Mail, Passwort und
Projekt:

```powershell
edge-impulse-data-forwarder --frequency 208
```

Seine Fragen:

- **Port:** COM11 — **nicht COM10.** Die Brücke schreibt in COM10 hinein, der Forwarder
  liest COM11 heraus. Ein COM-Port gehört immer nur einem Programm.
- **Achsennamen:** `env, gyro, lax, lay, laz` — Reihenfolge nicht vertauschen
- **Gerätename:** frei wählbar

Aufgenommen wird dann im Studio unter *Data acquisition*.

---

## Vier Kontrollen, bei jeder Aufnahme

| Wo | Muss | Sonst |
|---|---|---|
| Brücke, `Zeilen/s` **im Mittel** | ~208 | Takt stimmt nicht, nicht aufnehmen |
| Brücke, `>>> LUECKE`-Zeile | keine während der Aufnahme | diese eine Aufnahme verwerfen |
| Brücke, Spalte `verworfen` | 0, sobald der Forwarder liest | er liest nicht mit |
| LED am Gerät | aus | Aufnahme verwerfen |

Der **Einzelwert** bei `Zeilen/s` schwankt um ±6 — BLE liefert in Schüben, da rutschen
Zeilen über die Sekundengrenze. Das ist Messrauschen, kein Taktfehler; massgeblich ist der
Mittelwert über eine halbe Minute. Die Abtastung selbst läuft mit fester Schrittweite und
driftet nicht.

Die LED geht bei einem verpassten Takt oder einer nicht abgegebenen Zeile an und bleibt bis
zum Reset an.

Verliert die Funkstrecke Samples, **läuft die Brücke weiter** und schreibt eine auffällige
Zeile mit Uhrzeit dazwischen:

```
  >>> 14:32:07 LUECKE: 7 Samples fehlen, 1 kaputte Zeilen
```

Sie bricht nicht ab, weil ein verlorener Brocken nicht die Sitzung unbrauchbar macht,
sondern nur die Aufnahme, die gerade lief. Die Uhrzeit sagt, welche das war. Am Ende
(Ctrl-C) zieht sie Bilanz:

```
Sauber: 4604 Zeilen, keine Luecke.
```

oder mit Verlust die Zahl der fehlenden Samples und ihren Anteil. Nichts davon sieht man
dem CSV an — deshalb die Kontrollen.

---

## Was aufnehmen

Gerechnet wird in **Fenstern**, nicht in Minuten: eine Sekunde ergibt rund 10 Fenster, ein
einzelner Pinch von ~300 ms aber nur zwei bis drei. Deshalb ist `pinch` der Engpass.

| Klasse | Menge | Was |
|---|---|---|
| `pinch` | ~150 Ereignisse | zügige Pinches, Arm ruhig gehalten |
| `idle` | ~1 min | Hand entspannt, Arm ruhig, nichts tun |
| `negative` | ~2 min | Tippen, Klopfen, Klatschen, Gehen — **und Armbewegung ohne Pinch** |

`pinch` in 10-Sekunden-Häppchen mit je 8–10 Pinches aufnehmen und im Studio per
*Split sample* (~300 ms) zerlegen. Ohne das sind die meisten Fenster darin Ruhe mit dem
Etikett `pinch`, und das Modell lernt, Ruhe sei ein Pinch.

Ein Satz Pinches in **abgedrehter** Haltung zusätzlich, im Studio ins Test-Set verschieben
— das belegt die Haltungsunabhängigkeit.

Für den Vergleich eigener gegen gemischten Datensatz siehe
[EdgeImpulse.md, Schritt 3b](EdgeImpulse.md). Die Regel dort in einem Satz: beim gemischten
Datensatz gehört **eine Person vollständig ins Test-Set**, nie zufällig aufgeteilt.

---

## Wenn etwas klemmt

| Symptom | Ursache |
|---|---|
| `ModuleNotFoundError: No module named 'bleak'` | `python` zeigt auf PlatformIOs Python 3.11. `py` benutzen |
| `NotPlatformIOProjectError` | PowerShell steht nicht im Projektordner. `cd c:\dev\Maturaarbeit` |
| Brücke findet kein Gerät | Firmware ohne `COLLECT_OVER_BLE` geflasht; oder die vorige Verbindung wird noch abgebaut, dann 15 s warten; oder das Gerät ist in den Windows-Bluetooth-Einstellungen gekoppelt und dort zu entfernen |
| `>>> LUECKE` mit tausenden Samples direkt nach dem Verbinden | Firmware älter als die Puffer-Räumung beim Verbindungsaufbau. Neu flashen |
| Vereinzelte kleine Lücken (unter ~50 Samples) | Funkstrecke. Gerät näher an den Rechner, freie Sichtlinie; die betroffene Aufnahme wiederholen |
| Forwarder: „Opening COM10: Access denied" | COM10 statt COM11 gewählt. COM10 gehört der Brücke |
| Forwarder: „Reading data from device OK (0 samples)" + 400 Bad Request | Die Brücke läuft nicht. Prüfen: ist COM10 belegt? Frei heisst, niemand schreibt hinein |
| Forwarder: „Failed to get information off device" | Brücke lief noch nicht oder war nicht verbunden. Erst Fenster 1, dann Fenster 2 |
| Dauerhaft deutlich unter 208 `Zeilen/s` | Näher an den Rechner. Bleibt es dabei, trägt die Funkstrecke das Textformat nicht und es muss auf binär umgestellt werden (~22 statt ~38 Bytes je Sample) |
| `setupc.exe`: „ERROR: 2 — The system cannot find the file specified" | Falsches Arbeitsverzeichnis, siehe oben |

Den Zeilenzusammenbau der Brücke prüft ein eigener Selbsttest — ohne Gerät, ohne COM-Port,
ohne bleak:

```powershell
py tools/ble_collect_bridge.py --selftest
```

---

Ab hier weiter in [EdgeImpulse.md](EdgeImpulse.md), Schritt 3b und 4: Datenmenge, Impulse
bauen, trainieren, exportieren.
