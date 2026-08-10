# Altlastenliste

Bestandesaufnahme aller 18 Module, aufgenommen auf Commit `c2d5f18`. Zweck: die Reste
sichtbar machen, die sich über die drei Umbauten (UX-Überarbeitung, Stromsparen,
Zustandsautomat) angesammelt haben, und die Aufräumarbeit begründbar machen.

Startpunkte waren die drei im Auftrag genannten: die `minor (deferred)`-Einträge in
`.superpowers/sdd/2026-08-07-stromsparen/progress.md`, ungenutzte Konstanten in
`config.h`, und falsche Kommentare.

Status: **offen** = beim Aufnehmen noch da · **erledigt** = in dieser Runde behoben ·
**bereits erledigt** = war schon vor der Aufnahme behoben, der Eintrag in `progress.md`
ist veraltet.

---

## 1. Die aufgeschobenen Punkte aus `progress.md`

Sieben Einträge waren als `minor (deferred)` vermerkt. Sechs davon sind bereits in den
Nachfolge-Commits behoben worden, ohne dass die Liste nachgeführt wurde — das ist selbst
eine Altlast, weil die Liste sonst beim nächsten Lesen zu Doppelarbeit führt.

| # | Eintrag | Ort | Status |
|---|---|---|---|
| 1.1 | Oversampling-Anzahl `8` als Literal statt Konstante | `Battery.h` | bereits erledigt (`kOversample`) |
| 1.2 | toter Store `restUs = …` im `if`-Block | `main.cpp` | bereits erledigt (Busy-Wait durch `delay(1)`-Schlafschleife ersetzt) |
| 1.3 | Kommentar behauptet „FreeRTOS-Auflösung 1 ms", tatsächlich 1024 Hz | `main.cpp` | bereits erledigt (nennt jetzt 977 µs) |
| 1.4 | Registerreihenfolge in `disableWakeOnMotion()` unkommentiert | `ImuReader.h` | bereits erledigt |
| 1.5 | Flanken-Fenster zwischen `enableWakeOnMotion()` und `attachInterrupt()`, LIR aus | `ImuReader.h`, `main.cpp` | bereits erledigt (LIR `0x81` + Pegelprüfung) |
| 1.6 | `prepareSleep()` ist `public` bei genau einem Aufrufer | `AirMouseController.h` | bereits erledigt (`private`) |
| 1.7 | **`tickUs` ist `file-static`, eine lokale Variable würde reichen** | `main.cpp:15` | **offen → erledigt** |

Zu 1.7: `tickUs` wird in jedem `loop()`-Durchlauf vor der ersten Lesestelle neu
zugewiesen. Der Initialwert `cfg::READY_INTERVAL_US` und der Kommentar „startet
ausgeschaltet" täuschen einen Zustand vor, den die Variable nicht trägt — sie ist eine
reine Rechengrösse innerhalb eines Takts.

---

## 2. Ungenutzte Konstanten und tote Zugriffsfunktionen

**Ungenutzte `cfg::`-Konstanten: keine.** Alle 63 `constexpr` in `namespace cfg` haben
mindestens eine Verwendung (maschinell geprüft). Was zunächst wie eine tote Konstante
aussieht, ist in drei Fällen bewusst so:

- `cfg::ACCEL_K = 0.0f` und `cfg::SMOOTH_TAU` liegen auf Vergleichspfaden, die über
  `PointerTuning` bzw. `USE_ONE_EURO` erreichbar sind — sie sind die Gegenprobe der
  Evaluation, nicht Reste.
- `cfg::ELEV_SIGN` wird bewusst nur im Controller angewandt, damit `ArmOrientation`
  hardwarefrei bleibt.

Tot sind dagegen drei **Zugriffsfunktionen**, die niemand ruft:

| # | Fund | Ort | Status |
|---|---|---|---|
| 2.1 | `PoseDetector::elevDeg()` — kein Aufrufer; der Controller führt `elev_` selbst | `PoseDetector.h:103` | offen → erledigt (entfernt) |
| 2.2 | `AirMouseState::power()` — kein Aufrufer, auch nicht im Test; `on()` deckt alles ab | `AirMouseState.h:164` | offen → erledigt (entfernt) |
| 2.3 | `MadgwickAHRS::q[4]` und `beta` sind `public`, werden aber nur intern und über `setBeta()` benutzt | `MadgwickAHRS.h:7-8` | offen → erledigt (privat) |

`TwistGuard::gain()` sieht ebenfalls tot aus, wird aber von `test_twist_guard.cpp`
gebraucht und bleibt.

---

## 3. Falsche oder veraltete Kommentare

| # | Fund | Ort | Status |
|---|---|---|---|
| 3.1 | Klassenkommentar: „`Idle` — abgedreht **oder** Arm nicht waagrecht". `Idle` ist seit dem Umbau **allein** das Ergebnis des Waagrecht-Gates; der Kommentar 100 Zeilen tiefer in derselben Datei sagt das Gegenteil. | `PoseDetector.h:11` | offen → erledigt |
| 3.2 | Klassenkommentar: „`Turned` — Neigung wirkt wie ein Joystick". Der Joystick kommt erst über `onTwistHeld()` nach einer Sekunde, nicht mit der Haltung. | `PoseDetector.h:12` | offen → erledigt |
| 3.3 | `USE_POSE_MODE` ist mit „Zeigen / Idle / **Scrollen**" kommentiert. Die Haltung heisst seit dem Umbau `Turned` und trägt zwei Bedeutungen — genau deshalb wurde sie umbenannt. | `config.h:31` | offen → erledigt |
| 3.4 | `DEBUG_PINCH` listet sieben Kanäle, ausgegeben werden neun (`ei_err`, `ei_us` fehlen). Die Zahl „alle 24 gleichzeitig" stimmt ebenfalls nicht mehr. | `config.h:11-16` | offen → erledigt |
| 3.5 | `Haptic` beschreibt einen „Impuls-Sequenzer", sagt aber nirgends, dass der Ausgang **rein digital** geschaltet wird (`digitalWrite` HIGH/LOW). Es gibt keine PWM und damit keine Intensitätsstufe — nur Anzahl und Länge der Impulse tragen Information. | `Haptic.h:5` | offen → erledigt |
| 3.6 | `docs/Programmcode.md` Abschnitt 3 zeigt eine `loop()`, die es so nicht mehr gibt: ohne Schlafschleife, mit festem `cfg::DT` statt `tickDt`. Abschnitt 9.1 desselben Dokuments beschreibt den heutigen Stand — die beiden widersprechen sich. | `docs/Programmcode.md:54` | offen → erledigt |
| 3.7 | `docs/Programmcode.md`: „Stand: Commit `a3a848c`" — vier Commits alt. | `docs/Programmcode.md:3` | offen → erledigt |
| 3.8 | `docs/Programmcode.md`: „Fünf `static_assert`s auf die Methodensignaturen" — es sind sieben (`radioOn`/`radioOff` kamen dazu). | `docs/Programmcode.md:542` | offen → erledigt |
| 3.9 | `docs/Programmcode.md`: Überschrift „Die vier Schichten" über einer Tabelle mit fünf Zeilen. | `docs/Programmcode.md:88` | offen → erledigt |
| 3.10 | `docs/Programmcode.md` Abschnitt 10 nennt `PoseDetector`, `ScrollJoystick` und `PinchDetector` als „nicht auf dem PC prüfbar" — nach dem Umbau in dieser Runde sind sie es. | `docs/Programmcode.md:906` | offen → erledigt |

**Nicht gefunden: der Kommentar „(Vibrationsmotor, PWM)".** Er kommt im ganzen
Arbeitsverzeichnis nicht vor (nur in `.pio/build/` als Framework-Symbol `HardwarePWM`,
das nichts mit diesem Projekt zu tun hat). Der sachliche Punkt dahinter stimmt aber und
ist als 3.5 aufgenommen: der Haptik-Code schaltet rein digital, und das stand nirgends.

---

## 4. Sichtbarkeiten

| # | Fund | Ort | Status |
|---|---|---|---|
| 4.1 | `prepareSleep()` `public` | `AirMouseController.h` | bereits erledigt |
| 4.2 | `MadgwickAHRS::q[4]`, `MadgwickAHRS::beta` `public` | `MadgwickAHRS.h` | offen → erledigt |
| 4.3 | `Battery::kOversample` ist `public static`, wird aber nur in zwei Zeilen derselben Klasse gebraucht | `Battery.h:21` | offen → erledigt |

---

## 5. Namensgebung über die Module hinweg

Die einzelnen Module sind für sich konsistent, untereinander aber nicht. Drei Muster
laufen auseinander:

| # | Muster | Abweichungen | Status |
|---|---|---|---|
| 5.1 | Zeitstempel heissen `t<Ereignis>_` (`tQuiet_`, `tWake_`, `tOut_`, `tPending_`, `tMoving_`, `tStep_`, `tFree_`, `tLastPinch_`) | Der Controller weicht als einziger ab: `lastMove_`, `lastDbg_`; `main.cpp` mit `lastOvrDbg` | offen → erledigt |
| 5.2 | Zähler heissen `n<Grund>_` (`nDebounce_`, `nGyro_`) | `moveFail_` im Controller | offen → erledigt (`nMoveFail_`) |
| 5.3 | Winkel-liefernde Funktionen tragen das Suffix `Deg` (`twistDeg`, `elevDeg`, `relTwistDeg`), Raten das Suffix `Dps`/`Hz` (`rateDps`) | `PoseDetector::relTwistSlow()` (Winkel ohne `Deg`), `ScrollJoystick::rate()` (Rate ohne Einheit, liefert Schritte/s) | offen → erledigt |
| 5.4 | Zeitparameter heissen `now_ms` bzw. `now_us` | `PinchDetector::tick(…, uint32_t now, …)` und `inFreeze(uint32_t now)` — es sind Millisekunden | offen → erledigt |
| 5.5 | Tuning-Felder tragen die Einheit im Namen (`settleMs`, `lowDps`, `releaseS`, `maxMs`, `lockoutMs`) | `SleepTuning::sleepAfter` — Millisekunden, ohne Suffix | offen → erledigt (`sleepAfterMs`) |
| 5.6 | Abfragen nach einem Wunsch des Controllers heissen `wants…()` (`wantsSleep()`) | `activeRate()` liefert `bool`, liest sich aber wie ein Ratenwert | offen → erledigt (`wantsActiveRate()`) |

Zusätzlich: `PinchDetector::above_` ist nach dem Umbau falsch benannt — die Variable hält
nicht mehr „über der Schwelle", sondern „im letzten Takt als Pinch gewertet".
Umbenannt in `wasHot_`.

---

## 6. Testlücken

Sieben der 18 Module haben einen PC-Test. Von den elf übrigen ist der grösste Teil
zurecht ungetestet (Treiber, oder gar keine Entscheidungslogik). Drei sind es **nicht**
zurecht:

| Modul | Entscheidungslogik | Warum bisher kein Test |
|---|---|---|
| `PinchDetector` | bi-level Gate, Entprellung, Gyro-Guard, Freeze-Fenster | zieht `config.h` und damit `<Arduino.h>` |
| `ScrollJoystick` | Totzone, Kennlinie, Begrenzung, Schritt-Akkumulation | zieht `config.h` und `constrain()` |
| `PoseDetector` | Waagrecht-Gate mit Hysterese, Bewegungssperre, Haltezeit, Klassifikation | zieht `config.h` und `<Arduino.h>` |

Das ist derselbe Grund, aus dem `TwistToggle`, `TwistGuard` und `SleepPolicy` bereits
ihre eigenen Tuning-Structs haben. Die drei Module bekommen ihn ebenfalls, mit
`static_assert` gegen `cfg::` als Schutz vor Auseinanderdriften.

Bewusst weiterhin ohne Test bleiben: `ImuReader`, `MouseHID`, `Haptic`, `Battery`,
`PinchClassifier` (Hardware bzw. Edge-Impulse-SDK), `MadgwickAHRS` und `Filters/Low-` und
`HighPass` (keine Entscheidungslogik, reine Rechenschritte — `OneEuro` ist getestet, weil
dort eine Einstellungsentscheidung drinsteckt), `PinchFeatures` (getestet über
`test_pinch_features.cpp`), `ArmOrientation` (getestet) und `AirMouseController` (das ist
gerade die Verdrahtung, die alle anderen zusammensetzt).

**`pio test` lief bisher überhaupt nicht.** Es gab keine `[env:native]`-Sektion, ein
`pio test` hätte also aufs Board gewollt. Die Tests lagen flach in `test/` und liessen
sich damit auch gar nicht als PlatformIO-Testsuite bauen (mehrere `main()` in einem
Binary). Behoben: Unterordner je Test, `[env:native]` mit `test_framework = custom`.

---

## 7. Sonstiges

| # | Fund | Ort | Status |
|---|---|---|---|
| 7.1 | `main.cpp` bindet `VibrationEnvelope.h` und `PinchFeatures.h` unbedingt ein, gebraucht werden sie nur im `COLLECT_MODE` | `main.cpp:7-8` | offen → erledigt |
| 7.2 | `MadgwickAHRS.h` bindet `<Arduino.h>` ein, benutzt daraus aber nichts (nur `sqrtf` aus `<math.h>`) | `MadgwickAHRS.h:3` | offen → erledigt |
| 7.3 | `MadgwickAHRS` ist das einzige Modul ohne Kopfkommentar und ohne Literaturverweis | `MadgwickAHRS.h` | offen → erledigt (Madgwick 2010) |
| 7.4 | Es gibt keine `README.md` im öffentlichen Repository | Wurzel | offen → erledigt |
| 7.5 | `config.h` ist nach Themen nur grob sortiert; Betriebszustände und Wake-Up stehen unter „Sensor", die Lageschätzung steht hinter den Modulen, die sie benutzen. Rund ein Drittel der Konstanten hat keinen Kommentar mit Einheit oder Begründung. | `config.h` | offen → erledigt |

---

## 8. Bewusst nicht angefasst

- **`lib/ei-model/`** ist generierter Code aus Edge Impulse und wird bei einem
  Modell-Update als Ganzes ersetzt. Jede Handänderung dort wäre beim nächsten Export weg.
- **Die globalen `bledis`/`blehid`-Objekte in `MouseHID.h`.** Sie in die Klasse zu ziehen
  wäre sauberer, aber die bluefruit-Beispiele setzen durchgängig globale Objekte voraus,
  und der BLE-Zweig lässt sich nur am Gerät verifizieren. Ein Umbau ohne Gegenprobe wäre
  hier ein Risiko ohne Gegenwert.
- **`TODO.md`** bleibt Arbeitsjournal und wird bewusst nicht geglättet: es dokumentiert
  auch verworfene Wege, und die Einträge sind teils älter als der Code. Wer daraus etwas
  übernimmt, prüft gegen den Code — das steht so in `CLAUDE.md`.
