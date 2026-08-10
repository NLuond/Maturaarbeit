# Bedienkonzept — von der Expertenmaus zur Alltagsmaus

Strategiepapier. Ausgangslage: der Cursor funktioniert einwandfrei, der Pinch brauchbar,
alles andere beherrscht nur, wer die Firmware kennt. Schon das Einschalten scheitert bei
neuen Nutzern. Dieses Dokument sagt, woran das liegt, was Apple und andere an derselben
Stelle tun, und in welcher Reihenfolge die Bedienung umgebaut werden sollte.

## 1. Die Diagnose: nicht die Gesten sind zu schwer, es sind zu viele

Heute muss ein Nutzer fünf Dinge lernen, und jedes davon ist **modal** (die Bedeutung
hängt von einem Zustand ab, den er nicht sehen kann) und **zeitbasiert** (er muss ein
unsichtbares Fenster treffen):

| Funktion | Geste | versteckte Bedingung |
|---|---|---|
| Ein/Aus | ausdrehen, < 1300 ms zurück | Zeitfenster, Waagrecht-Gate, Abbruchschwelle, Lockout |
| Scrollen | ausdrehen, > 1300 ms halten | zusätzlich Haltung `Turned`, Haltezeit, Totzone |
| Linksklick | Pinch | Haltung muss `Point` sein |
| Rechtsklick | Pinch | Haltung muss `Turned` sein |
| Ziehen | Pinch, zweiter binnen 350 ms | Band 200–350 ms |

Drei Eigenschaften machen das für Ungeübte unbedienbar:

**Zeitfenster sind unsichtbar.** Der Nutzer kann nicht wissen, ob er 1200 oder 1400 ms
gebraucht hat. Verfehlt er das Fenster, passiert nichts — ohne Hinweis, was falsch war.

**Zustände sind unsichtbar.** `Power`, `Pose`, `Grab`, `scrollOn` ergeben zusammen
Dutzende Kombinationen, und das Gerät hat keine Anzeige. Der Nutzer rät, in welchem
Zustand er ist, und die Geste bedeutet je nachdem etwas anderes.

**Fehlschläge sind stumm.** Sechs Bremsen (Abbruchschwelle, Waagrecht-Gate,
Rückkehrfenster, Gyro-Guard, Entprellung, ML-Schwelle) verwerfen Gesten lautlos. Für den
Nutzer ist „die Geste war zu langsam" nicht von „das Gerät ist kaputt" zu unterscheiden.
Genau das erzeugt den Eindruck von Unzuverlässigkeit.

Die HCI-Literatur benennt den Haupthebel: der stärkste Einzelfaktor für Auffindbarkeit ist
**Einfachheit der Handlung**, nicht besseres Training oder bessere Erkennung. Und weil
diesem Gerät ein Bildschirm fehlt, fällt die zweitwirksamste Massnahme — visuelles
Feedforward, das Erfolgsquoten auf über 80 % hebt — praktisch aus. Es bleibt nur, die
Handlungen selbst zu vereinfachen.

## 2. Wie Apple dasselbe Problem löst

**Vision Pro: ein Primitiv, der Kontext liefert die Bedeutung.** Das gesamte Vokabular
besteht aus dem Pinch: Pinch = tippen, Pinch + Bewegung = ziehen, Pinch + Bewegung =
scrollen. Drei Funktionen, **eine** zu lernende Handbewegung. Nicht drei Gesten, die man
auseinanderhalten muss, sondern eine Geste, deren Bedeutung sich aus dem ergibt, was
gleichzeitig passiert.

**Apple Watch AssistiveTouch: seltene Funktionen kommen in ein Menü, nicht in eine
Geste.** Es gibt genau vier Gesten (Pinch, Doppel-Pinch, Faust, Doppel-Faust). Alles
Weitere steckt im *Action Menu*, das eine dieser Gesten öffnet und das die Optionen
durchläuft. Man muss nichts auswendig können — man wartet, bis das Gewünschte kommt.

**Apple Watch: die beste Einschaltgeste ist gar keine.** Die Uhr wacht beim Heben des
Handgelenks auf. Kein Zeitfenster, nichts zu lernen, nur eine einstellbare Empfindlichkeit.
Das Einschalten ist keine Geste, sondern eine Folge der Haltung.

### Und wo der Vergleich aufhört

Apples Vokabular lässt sich nicht eins zu eins übernehmen, weil das Ziel ein anderes ist.
**visionOS ist um Gaze und Pinch herum entworfen worden**: die Bedienelemente sind gross,
es gibt keine Scrollbalken-Griffe, keinen Doppelklick, kein Kontextmenü als Grundbaustein.
Drei Gesten genügen dort, weil das Betriebssystem für drei Gesten gebaut ist.

**Windows ist um eine präzise Maus mit echten Tasten entworfen worden** und verlangt
entsprechend mehr: gedrückt halten und ziehen, Doppelklick, Rechtsklick, feines Zielen auf
kleine Flächen. Ein Gestengerät, das Windows bedienen soll, kann sich sein Vokabular also
nicht frei wählen — die untere Grenze gibt das Betriebssystem vor.

Die Folgerung ist nicht, Apples Prinzipien zu verwerfen, sondern zu wissen, wofür sie
gelten: **die Prinzipien sind übertragbar, die Gestenzahl nicht.** Wo visionOS mit drei
Gesten auskommt, sind es hier realistisch fünf Funktionen (Zeigen, Links, Rechts,
Scrollen, Ziehen) — aber sie dürfen aus möglichst wenigen *Handlungen* entstehen, und
genau darum geht es in den Stufen 2 und 2b.

Daraus die drei Leitsätze für dieses Gerät:

1. **Was man aus der Haltung ablesen kann, darf keine Geste sein.**
2. **Zeitfenster durch gehaltene Zustände ersetzen** — die eigene Armstellung ist für den
   Nutzer spürbar, ein 350-ms-Fenster nicht.
3. **Ein Primitiv (der Pinch), modifiziert durch die Haltung.**

## 3. Der Umbau

### Stufe 1 — Das Einschalten abschaffen (grösster Effekt, mittlerer Aufwand)

Die Ein/Aus-Drehgeste entfällt als *primärer* Weg. Die Maus ist an, solange der Unterarm
in Arbeitshaltung ist:

```
an   := Arm waagrecht (Waagrecht-Gate) UND in den letzten N Sekunden bewegt
aus  := Arm ausserhalb des Gates (hängt, abgelegt) ODER M Sekunden still
```

Das ist Apples *Raise to Wake*, und die Bausteine liegen bereits alle im Code:
`PoseDetector::level()`, `SleepPolicy` mit `stillDps`/`sleepAfterMs`, `gyroSum`. Es ist
keine neue Erkennung nötig, nur eine neue Verdrahtung: `Power` wird aus einem
Geste-Ereignis zu einer Funktion der Haltung.

Beide Übergänge brummen, damit der Zustand nie stillschweigend wechselt. Die Drehgeste
bleibt als *Notausschalter* für den Fall erhalten, dass jemand die Maus im Arbeitsposition
bewusst stillstellen will — aber niemand muss sie kennen.

**Hauptrisiko** ist die Fehlaktivierung: der Cursor läuft beim Gestikulieren oder Reden.
Dagegen: Hysterese am Gate, eine Mindest-Haltezeit vor dem Einschalten, und die bereits
vorhandene Ruhe-Erkennung. Der Wert von `M` ist der eigentliche Einstellparameter und
gehört gemessen, nicht geraten.

### Stufe 2 — Die Drehung wird ein gehaltener Modifikator (grösster Effekt auf das Vokabular)

Erst wenn Stufe 1 die Ein/Aus-Bedeutung von der Drehachse nimmt, wird die Drehung frei für
genau eine, durchgehend gültige Bedeutung:

```
Arm gerade      →  zeigen.  Pinch = Linksklick.  Pinch + Bewegung = ziehen (Stufe 2b).
Arm ausgedreht  →  scrollen (Armneigung).  Pinch = Rechtsklick.
```

Kein Zeitfenster, kein `Toggle` gegen `Held`, kein `scrollOn_`-Flag, keine Haltezeit von
einer Sekunde, kein Lockout. Die Drehung wirkt wie eine gehaltene Umschalttaste: solange
sie anliegt, gilt die zweite Bedeutung — und der Nutzer *spürt* seine eigene Armstellung,
im Gegensatz zu jedem Zeitfenster.

Das entfernt `TwistToggle` samt `cancel()`, Lockout und den drei Ablehnungszählern
vollständig, und mit ihnen die drei häufigsten stillen Fehlschläge. Die Anzahl der zu
lernenden Dinge sinkt von fünf auf **drei**: pinchen, doppelt pinchen, Arm abdrehen.

### Stufe 2b — Ziehen: Apples Modell ohne Apples Sensor

Apples Modell ist „pinch and move": zusammenkneifen, bewegen, loslassen. Es ist dem
Doppel-Pinch klar überlegen, weil es **nichts zu zählen und kein Fenster zu treffen** gibt
— man tut einfach, was man auch mit einer echten Maustaste täte.

**Was eine IMU sehen kann und was nicht — die drei Fälle sind zu trennen:**

| | Physik | mit dieser IMU? |
|---|---|---|
| **Kontakt** (Finger treffen sich) | Stoss: breitbandiger Impuls | **ja**, das ist die heutige Erkennung |
| **Lösen** (Finger trennen sich) | ebenfalls ein Impuls, aber schwächer — kein Stoss, nur elastisches Zurückschnellen | **im Prinzip ja**, Amplitude offen |
| **Halten** (Finger bleiben zusammen) | keine Bewegung, also kein Signal | **nein**, grundsätzlich nicht |

Der entscheidende Unterschied ist nicht schwach gegen stark, sondern **Ereignis gegen
Zustand**. Eine IMU misst Beschleunigung; ein statischer Zustand erzeugt keine. Deshalb
greift Doublepoint für „pinch and hold" zum optischen Herzfrequenzsensor, und deshalb
untersucht die Forschung PPG für Fingergesten: der Sensor liest **Blutfluss-Störungen
durch Muskel- und Sehnenbewegung** und sieht damit den Zustand, den die IMU prinzipiell
nicht sehen kann. Den Pinch-*Moment* dagegen erkennt Doublepoint sehr wohl aus der IMU
allein, mit 94 bis 97 Prozent — und die Literatur arbeitet dafür mit exakt der Auslegung
dieses Geräts: rund 200 Hz Abtastrate, ±4 g, ±500 °/s.

Das Halten ist also ausgeschlossen. Beim **Lösen** blieb die Frage offen — theoretisch ein
Ereignis wie der Kontakt, nur schwächer.

#### Gemessen: das Lösen taugt nicht

Der Versuch (siehe unten) ist durchgeführt worden. Ergebnis:

> Eine zweite Spitze gibt es **teilweise**, bei **kurzem Pinch gar nicht**, und sie liegt
> **kaum über dem Rauschen**.

Damit ist „pinch and move" im Sinne Apples — greifen, ziehen, loslassen — auf dieser
Hardware nicht umsetzbar. Ein Tastenzustand, dessen Ende in der Hälfte der Fälle ausbleibt,
führt zu einer klebenden Taste, und das ist der schlechteste Fehlerfall überhaupt.

Das ist ein sauberes negatives Ergebnis und für die Arbeit wertvoller als ein weiterer
Umbau: eine Entwurfsfrage, entschieden durch eine einzige isolierte Messung, und zugleich
die Erklärung, warum eine spezialisierte Firma wie Doublepoint für dieselbe Funktion einen
zweiten Sensortyp verbaut.

#### Was davon bleibt: der Anfang der Geste

Das Lösen wird nur für das **Ende** gebraucht. Der Anfang kommt ohne aus:

```
Pinch                        →  Taste runter (sofort)
Cursor bewegt sich > 12 px   →  es ist ein Ziehen, Taste bleibt unten
Cursor bleibt stehen         →  es war ein Klick, Taste hoch nach 350 ms
Pinch                        →  fallengelassen
```

Der **Start** ist damit genau Apples Geste: zusammenkneifen und bewegen, nichts zu zählen,
kein Zeitband zu treffen. Unterschieden wird über die Bewegung — dasselbe Kriterium, mit
dem jeder Touchscreen Tippen von Wischen trennt, und selbsterklärend, weil man das Objekt
am Cursor hängen sieht. Nur das **Ende** ist ein bewusster zweiter Pinch statt des
Loslassens, weil der Sensor das Loslassen nicht sieht.

Für den Nutzer: „kneifen und ziehen zum Greifen, kneifen zum Fallenlassen."

Damit entfällt der Doppel-Pinch als Auslöser vollständig — mit ihm das Zeitband von
200–350 ms, das in der Praxis nicht zu treffen war, und die Gefahr, dass der Löse-Impuls
des ersten Pinch ein ungewolltes Ziehen verriegelt.

```
Pinch-Impuls                 →  Taste runter
Cursor bewegt sich > S px    →  es ist ein Ziehen, Taste bleibt unten
Cursor bleibt stehen         →  es war ein Klick, Taste hoch am Fensterende
naechster Impuls (Loesen)    →  Taste hoch, fallengelassen
```

Der entscheidende Unterschied zum Doppel-Pinch: **unterschieden wird über die Bewegung,
nicht über eine zweite Geste und nicht über ein Zeitfenster, das der Nutzer treffen muss.**
Das ist dasselbe Kriterium, mit dem jeder Touchscreen Tippen von Wischen trennt — die
vertrauteste Unterscheidung, die es in der Bedienung überhaupt gibt. Und sie ist
selbsterklärend: man *sieht* das Objekt am Cursor hängen.

Für den Nutzer bleibt: „zusammenkneifen, ziehen, loslassen." Kein Zählen, kein Timing.

**Das Restrisiko** bleibt das verpasste Lösen — die Taste klebt. Die Sicherungen dagegen
sind bereits gebaut (Zwangsfreigabe, Freigabe beim Verlassen der Zeige-Haltung, Freigabe
beim Ausschalten, Erinnerungsimpuls) und behalten hier ihre Berechtigung.

#### Das entscheidende Experiment (durchgeführt, Ergebnis oben)

Der bisherige „Beleg" für den Löse-Impuls ist nicht sauber: die zweite Auslösung beim
Rechtsklick könnte ebenso gut die **Haptik** gewesen sein (zwei Pulse, 160 ms). Beides
fiel ans Ende der damaligen 180 ms Entprellung und ist daran nicht zu unterscheiden.

Ein Versuch trennt es eindeutig — **den Pinch bewusst eine volle Sekunde geschlossen
halten und dann die Finger öffnen.** Die Haptik ist nach spätestens 160 ms vorbei. Jede
`env`-Spitze bei t ≈ 1000 ms kann deshalb **nur** das Lösen sein.

Abzulesen (`DEBUG_SET = DEBUG_PINCH`, Kanäle `env`, `gate`, `click`):

1. **Gibt es die zweite Spitze überhaupt?** Nein → das ganze Modell fällt, und das Ziehen
   braucht wieder eine zweite Geste.
2. **Wie hoch ist sie?** Der Kontakt liegt bei 0.045–0.125. Liegt das Lösen darunter,
   braucht es eine **eigene, tiefere Schwelle** (`RELEASE_ENV`) — mit `ENV_ON` = 0.035
   würde es sonst zum Teil übersehen. Auswertbar ist sie nur, wenn sie klar über dem
   Untergrund von 0.005–0.02 liegt.
3. **Wie streut sie über 20 Wiederholungen?** Das ist die Zahl, an der zugleich der lange
   Pinch als Rechtsklick hängt (Stufe 2c, Variante b).

Erst danach lohnt sich Code. Diese Messung ist zudem ein guter Abschnitt für die
schriftliche Arbeit: sie entscheidet eine Entwurfsfrage durch eine einzige, sauber
isolierte Beobachtung.

### Stufe 2c — Rechtsklick: die verbleibende offene Frage

Nach dem Umbau ist der Rechtsklick die einzige Funktion ohne offensichtliche Heimat. Drei
Möglichkeiten, in dieser Reihenfolge empfohlen:

**a) Pinch bei abgedrehtem Arm** (Empfehlung). Nutzt den Modifikator aus Stufe 2, der für
das Scrollen ohnehin gebraucht wird — keine neue Geste, nur eine zweite Bedeutung einer
Handhaltung, die der Nutzer spürt. Kostet das Abdrehen des Arms.

**b) Langer Pinch** — zusammenkneifen und ohne Bewegung halten. Das wäre das
Touchscreen-Idiom für Kontextmenüs und damit das vertrauteste überhaupt. **Durch die
Messung erledigt:** „gehalten" müsste aus dem *Ausbleiben* des Löse-Impulses erschlossen
werden, und der bleibt bei kurzem Pinch ohnehin aus. Jeder gewöhnliche Klick ergäbe einen
Rechtsklick. Fällt weg.

**c) Auf dem Gerät weglassen** und in das Action-Menü aus Stufe 4 verschieben.

Empfehlung: **(a)**. Nach dem Messergebnis ist es nicht mehr die vorläufige, sondern die
einzige tragfähige Lösung auf dieser Hardware — und sie kostet nichts Zusätzliches, weil
der Modifikator für das Scrollen ohnehin gebraucht wird.

### Stufe 3 — Den Pinch zuverlässiger machen (kein Bedienthema, aber Voraussetzung)

Nach Stufe 2 hängt fast alles am Pinch. Seine Trefferquote wird damit zur zentralen
Grösse. Die Arbeitspakete stehen bereits in `TODO.md` und ändern sich durch dieses Papier
nicht:

- Trainingsdaten neu aufnehmen (die alten sind mit 100 Hz statt 209 Hz aufgenommen)
- Hard Negatives (Tippen, Klopfen, Klatschen, Gehen) als eigene Klasse
- `PINCH_GYRO_GUARD` gegen die gemessenen 200–250 °/s prüfen

### Stufe 4 — Was nicht ins Vokabular passt, kommt in ein Menü (optional, grösster Aufwand)

Bleiben Funktionen übrig (Mittelklick, Doppelklick, Zurück/Vorwärts), gehören sie nach
Apples Vorbild **nicht** in weitere Gesten, sondern in ein Action-Menü. Der entscheidende
Unterschied zur Uhr: dieses Gerät hat keinen Bildschirm — wohl aber steuert es einen. Ein
kleines Overlay auf dem Rechner wäre das Menü und zugleich das fehlende visuelle
Feedforward.

Das ist ein eigenes Teilprojekt (Host-Software) und bewusst als Letztes eingeordnet. Für
die schriftliche Arbeit ist es auch ohne Umsetzung wertvoll: es benennt die Grenze, an die
ein bildschirmloses Gestengerät stösst.

### Stufe 5 — Die Haptik als Sprache aufräumen (klein, sofort machbar)

Heute gibt es vier Muster (1 kurz, 2 kurz, 3 kurz, 1 lang). Das ist mehr, als man ohne
Anleitung unterscheiden kann. Nach dem Umbau reichen zwei:

| Muster | Bedeutung |
|---|---|
| kurz | etwas hat ausgelöst (Klick, Rechtsklick) |
| lang | der Modus hat gewechselt (an/aus, ziehen an/aus) |

Welche Taste geklickt wurde, sagt bereits die Armhaltung — dafür braucht es kein zweites
Muster.

## 4. Reihenfolge und Begründung

| Stufe | Wirkung auf Einsteiger | Aufwand | Risiko |
|---|---|---|---|
| 1 Einschalten abschaffen | sehr hoch — beseitigt die Hürde Nummer eins | mittel | Fehlaktivierung |
| 2 Drehung als Modifikator | sehr hoch — 5 Lernpunkte auf 3 | mittel | Ergonomie beim längeren Scrollen |
| 2b Ziehen über Bewegung | hoch — ersetzt das Zählen durch ein vertrautes Kriterium | klein | verpasstes Lösen |
| 5 Haptik aufräumen | mittel | klein | keines |
| 3 Pinch zuverlässiger | mittel — betrifft alle, nicht nur Einsteiger | hoch (Daten) | Modellgüte |
| 4 Action-Menü | hoch für Zusatzfunktionen | sehr hoch | Projektumfang |

Stufe 1 und 2 bedingen einander: solange Ein/Aus auf der Drehachse liegt, kann die Drehung
kein sauberer Modifikator sein. **Zusammen sind sie der eigentliche Umbau**, alles andere
ist Feinschliff.

## 5. Wie sich der Erfolg belegen lässt

Ohne Messung bleibt „einfacher" eine Behauptung. Vorschlag für die Evaluation, zugleich
ein fertiger Abschnitt der schriftlichen Arbeit:

- **5 ungeübte Testpersonen**, je ein Durchgang vor und nach dem Umbau.
- **Aufgabenfolge:** Cursor auf ein Ziel bewegen → linksklicken → rechtsklicken → eine
  Seite scrollen → eine Datei ziehen.
- **Zwei Bedingungen:** ohne jede Erklärung (misst Auffindbarkeit), danach mit einer
  30-Sekunden-Erklärung (misst Erlernbarkeit).
- **Messgrössen:** Erfolgsquote je Aufgabe, Zeit bis zum ersten Erfolg, Anzahl
  Fehlversuche, und — der aussagekräftigste Wert — wie viele Personen die Maus ohne
  Hilfe überhaupt *einschalten*.

Die Zahl aus der letzten Zeile vor dem Umbau ist die Rechtfertigung für Stufe 1; dieselbe
Zahl danach ist ihr Beleg.

## Quellen

- [Use AssistiveTouch on Apple Watch — Apple Support](https://support.apple.com/en-us/111111)
- [Use gestures with Apple Vision Pro — Apple Support](https://support.apple.com/en-us/117741)
- [Turn on and wake Apple Watch — Apple Support](https://support.apple.com/guide/watch/turn-on-and-wake-apple-watch-apd748b87e2a/watchos)
- [Understanding Novice Users' Mental Models of Gesture Discoverability and Designing Effective Onboarding (UbiComp 2024)](https://dl.acm.org/doi/10.1145/3675094.3678370)
- [Iteratively Designing Gesture Vocabularies: A Survey and Analysis of Best Practices in the HCI Literature (ACM TOCHI)](https://dl.acm.org/doi/10.1145/3503537)
- [Design Principles & Issues for Gaze and Pinch Interaction — Ken Pfeuffer](https://medium.com/antaeus-ar/design-principles-issues-for-gaze-and-pinch-interaction-a95e251169ae)
- [Doublepoint — Gesture Recognition (Produktseite)](https://www.doublepoint.com/product)
- [Wear OS smartwatch gesture system — Digital Trends (Sehnen-Erkennung über den optischen Sensor)](https://www.digitaltrends.com/phones/wear-os-smartwatches-amazing-gesture-system-doublepoint-ces-2024/)
- [Doublepoint WowMouse für Apple Watch — Notebookcheck](https://www.notebookcheck.net/Doublepoint-debuts-WowMouse-for-Apple-Watch-to-control-Macs-with-gestures.941474.0.html)
- [OpenWatch: A Multimodal Benchmark for Hand Gesture Recognition on Smartwatches (IMU + PPG)](https://arxiv.org/html/2605.04791)
- [FinDroidHR: Smartwatch Gesture Input with Optical Heartrate Monitor](http://cluo29.github.io/publications/2018/YuZhang2018.pdf)
- [Counting Finger and Wrist Movements Using Only a Wrist-Worn IMU (PMC)](https://pmc.ncbi.nlm.nih.gov/articles/PMC10300978/)
