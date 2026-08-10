#pragma once
// Selbst tragend inkludieren: cfg:: benutzt uint32_t und den Pin-Namen D1.
// Ohne diese beiden Zeilen kompiliert die Datei nur, wenn der Includer vorher
// zufaellig <Arduino.h> gezogen hat.
#include <Arduino.h>
#include <stdint.h>

// =====================================================================
//  Konfiguration der Air Mouse
// =====================================================================
//
// Diese Datei ist die Spezifikation des Geraets in Zahlen. Sie hat zwei Teile:
//
//   1. Betriebsarten - #define-Schalter. Es gibt KEINE Laufzeit-Konfiguration;
//      umstellen heisst neu bauen und flashen.
//   2. Einstellwerte - constexpr in namespace cfg, nach Themen gruppiert in der
//      Reihenfolge, in der ein Messwert sie durchlaeuft: Sensor -> Takt -> Lage
//      -> Handhaltung -> Zeigen -> Klick -> Scrollen -> Rueckmeldung.
//
// Jeder Wert traegt seine Einheit und seine Begruendung. Wo eine Begruendung
// fehlt, weil der Wert noch nicht am Geraet gemessen ist, steht das ausdruecklich
// dabei - die Einstellwerte sind begruendete Ausgangspunkte, keine Messergebnisse
// (Messplan: TODO.md).
//
// Magic Numbers gehoeren hierhin, nicht in die Module. Eine bewusste Ausnahme
// gibt es: fuenf Module muessen sich ohne Toolchain uebersetzen lassen, damit
// ihre PC-Tests laufen (TwistToggle, TwistGuard, SleepPolicy, PoseDetector,
// PinchDetector, ScrollJoystick, OrientationPointer). Sie halten ihre Zahlen in
// einem eigenen Tuning-Struct. Die betroffenen Werte sind unten mit
// "[auch in <X>Tuning]" markiert; static_asserts in AirMouseController.h halten
// beide Seiten zusammen, ein Auseinanderdriften ist ein Uebersetzungsfehler.

// =====================================================================
//  1. Betriebsarten
// =====================================================================

// Statt HID nur CSV ausgeben (env, gyro/100, lax, lay, laz) fuer die Aufnahme
// der Trainingsdaten mit dem edge-impulse-data-forwarder. HID und Controller
// werden dann gar nicht erst initialisiert.
#define COLLECT_MODE    false

// ML-Klassifikator gegen reine Schwellwert-Erkennung. Auf false entscheidet
// allein das env-Gate - der schnellste Weg, ein Fehlverhalten vom Modell zu
// trennen, das gar nicht die Ursache ist.
#define USE_ML_PINCH    true

// BLE (bluefruit) gegen USB-HID (TinyUSB) in MouseHID.h.
//
// Steht voruebergehend auf USB: das Geraet liess sich zuletzt nicht per
// Bluetooth koppeln. Fuer die Arbeit bleibt BLE das Ziel - zum Einstellen von
// Kennlinie und Haltungen ist der Uebertragungsweg egal, und ueber USB faellt
// das Verbindungsintervall als Stoergroesse weg. ACHTUNG bei Strommessungen:
// im USB-Zweig sind radioOff()/radioOn() leere Huellen, dort zu messen hiesse
// eine Funktion zu messen, die es nicht gibt.
#define USE_BLE_HID     false

// --- Die drei A/B-Vergleiche der Evaluation --------------------------
// Diese drei sind keine Aufraeum-Optionen, sondern erlauben, denselben Aufbau
// mit und ohne einen einzelnen Mechanismus gegen dieselbe Aufgabe zu messen.

// Haltungserkennung. Auf false meldet PoseDetector dauerhaft Point - zum
// Eingrenzen von Cursor-Problemen. Die Winkel laufen dabei weiter mit, nur die
// Klassifikation steht still: die Ein/Aus-Drehgeste liest den Winkel direkt und
// muss auch dann funktionieren.
#define USE_POSE_MODE   true

// Cursor-Glaettung: 1-Euro-Filter (true) gegen den festen Tiefpass mit
// SMOOTH_TAU (false).
#define USE_ONE_EURO    true

// Roll-Kompensation: rechnet die Handverdrehung aus der Cursorbewegung heraus,
// damit eine waagerechte Handbewegung bei verdrehter Hand nicht schraeg laeuft.
// In der Grundhaltung (twist = 0) sind beide Pfade identisch, der Unterschied
// zeigt sich erst bei verdrehter Hand.
#define USE_ROLL_COMP   true

// =====================================================================
//  2. Debug-Ausgabe (Teleplot)
// =====================================================================

// Teleplot-Kanaele (">name:wert") aus AirMouseController::debug(). Kostet
// Serial-Bandbreite und bremst die Schleife - fuer echte Nutzungstests und fuer
// JEDE Strommessung ausschalten, sonst misst man den Messaufbau.
#define DEBUG_TELEPLOT  true

// Welche Kanalgruppe gesendet wird. Alle gleichzeitig sind rund 17 kB/s; das
// kostet Rechenzeit, und eine zu langsame Schleife dehnt genau das ML-Fenster,
// das man gerade untersucht. Fuer eine gezielte Messung deshalb auf die
// passende Gruppe stellen.
//
// Immer gesendet, unabhaengig von der Gruppe: on, pose, dpose, tw, vbat.
#define DEBUG_ALL       0
#define DEBUG_PINCH     1   // env, envMax, gate, p_ml, click, gsum, nDeb,
                            // nGyro, ei_err, ei_us
#define DEBUG_POINT     2   // gx/gy/gz, rx, ry, pacc, accx, mvfail, twist,
                            // elev, rtwist, level, pgate, srate, tg, tgr
// DEBUG_ORIENT prueft die Einbaulage nach und steht bewusst neben DEBUG_ALL,
// nicht darin: er teilt gx/gy/gz und twist/elev mit DEBUG_POINT, zusammen
// kaemen diese Kanaele doppelt heraus.
#define DEBUG_ORIENT    3   // ax/ay/az roh + gvx/gvy/gvz geglaettet,
                            // angX/angY/angZ, gx/gy/gz, twist, elev, rtwist,
                            // level

// Ebenfalls neben DEBUG_ALL, weil er env/gate/click mit DEBUG_PINCH teilt.
// Der schlanke Satz fuer die Messung des LOESE-Impulses: nur vier Kanaele,
// damit die Serial-Last die Schleife nicht bremst - eine gedehnte Schleife
// dehnt genau die Huellkurve, die gemessen werden soll.
//
// envMax ist der Spitzenwert seit der letzten Ausgabe. Ohne ihn waere die
// Messung wertlos: die Schleife laeuft mit 209 Hz, die Ausgabe mit 50 Hz, und
// ein env-Impuls (Zeitkonstante rund 10 ms) wuerde nur zufaellig auf seinem
// Scheitel getroffen - die abgelesene Amplitude waere systematisch zu klein.
#define DEBUG_ENV       4   // env, envMax, gate, click
#define DEBUG_SET       DEBUG_ENV

namespace cfg {

// =====================================================================
//  3. Sensor (LSM6DS3, on-board)
// =====================================================================

    // Messbereiche. +-4 g deckt die Beschleunigungsspitzen eines Pinches ab,
    // ohne die Aufloesung um die Erdbeschleunigung herum unnoetig zu verlieren;
    // +-500 Grad/s liegt ueber den rund 250 Grad/s des normalen Gebrauchs und
    // ueber den rund 300 Grad/s einer zuegigen Ein/Aus-Drehung.
    constexpr int ACCEL_RANGE_G  = 4;      // g
    constexpr int GYRO_RANGE_DPS = 500;    // Grad/s

    // Abtastrate des Sensors im Zustand AKTIV. 208 Hz ist die naechstliegende
    // Stufe des LSM6DS3 zu den 209 Hz, mit denen das Modell trainiert wurde.
    constexpr int ACCEL_ODR_HZ   = 208;    // Hz
    constexpr int GYRO_ODR_HZ    = 208;    // Hz

    // Analoge Bandbreite des Beschleunigungssensors. Rund die halbe Abtastrate:
    // hoeher hiesse Aliasing, deutlich niedriger wuerde die hochfrequente
    // Erschuetterung wegdaempfen, aus der die Klickerkennung lebt.
    constexpr int ACCEL_BW_HZ    = 100;    // Hz

    // Der LSM6DS3 kann 400 kHz. Bei sechs Werten je Takt ist das der
    // Unterschied zwischen rund 2 ms und rund 0.5 ms Schleifenzeit - gesetzt
    // wird die Rate erst nach imu_.begin(), weil Wire.begin() sie dort auf die
    // Arduino-Vorgabe von 100 kHz zuruecksetzt.
    constexpr uint32_t I2C_CLOCK_HZ = 400000;   // Hz

    // Nullpunkt des Gyroskops driftet mit der Temperatur. Er wird nur
    // nachgefuehrt, solange das Geraet wirklich ruhig liegt.
    //
    // Die Schwelle lag bei 15 Grad/s und damit ueber der langsamsten gemeinten
    // Bewegung: langsames, gezieltes Zeigen liegt bei 5 bis 10 Grad/s und fiel
    // mitten ins Lernfenster. Der Schaetzer uebernahm die gewollte Drehrate als
    // Nullpunkt, der Cursor wurde beim langsamen Ziehen immer langsamer und
    // driftete beim Anhalten zurueck - ein Symptom, das man einer zu starken
    // Glaettung zuschreiben wuerde und das keine war.
    //
    // Die zweite Bedingung schliesst aus, dass eine gleichfoermige Drehung ohne
    // Drehratenanteil durchrutscht: ein ruhendes Board misst genau 1 g.
    // BIAS_TAU ist lang, weil echter Temperaturdrift langsam ist.
    constexpr float BIAS_STILL_DPS = 3.f;     // Grad/s
    constexpr float BIAS_ACC_TOL   = 0.05f;   // g, erlaubte Abweichung von 1 g
    constexpr float BIAS_TAU       = 5.0f;    // s

// =====================================================================
//  4. Takt und Betriebszustaende
// =====================================================================
//
// Drei Hardware-Zustaende ueber der Ein/Aus-Achse des Zustandsautomaten:
//
//   AKTIV   Maus eingeschaltet        IMU 208 Hz   Funk an   209 Hz Takt
//   BEREIT  aus, aber bewegt          IMU  52 Hz   Funk an    52 Hz Takt
//   SCHLAF  60 s ohne Bewegung        nur Accel    Funk aus  loop() suspendiert
//
// Eingeschlafen wird nur aus BEREIT, nie aus AKTIV - sonst verschwaende die
// Maus mitten im Gebrauch, waehrend man den Cursor nur ruhig auf einem Ziel
// haelt. Die Zeitschwellen dazu stehen in SleepTuning (lib/SleepPolicy/).

    // Feste Schrittweite der ganzen Verarbeitung in AKTIV. Muss zur Abtastrate
    // des Modells passen (209 Hz), sonst sieht der Klassifikator ein zeitlich
    // gestauchtes Fenster; PinchClassifier prueft das per static_assert.
    constexpr uint32_t SAMPLE_INTERVAL_US = 4785;                  // us
    constexpr float    DT                 = SAMPLE_INTERVAL_US * 1e-6f;   // s

    // Schrittweite in BEREIT. Dort laeuft nur noch die Drehgeste, und die
    // dauert rund eine Sekunde - 52 Hz sind dafuer reichlich, und es ist genau
    // die naechste ODR-Stufe des Sensors nach unten.
    constexpr uint32_t READY_INTERVAL_US = 19230;                  // us (52 Hz)
    constexpr float    READY_DT          = READY_INTERVAL_US * 1e-6f;     // s

    // Schwelle der Wake-Up-Funktion im Schlaf, 6 Bit. Ein Schritt entspricht
    // einem Vierundsechzigstel des Messbereichs, bei +-4 g also rund 62 mg.
    // Startwert 2 = rund 125 mg: Armheben soll wecken, ein Klopfen auf den
    // Tisch moeglichst nicht. Am Geraet nachziehen (Messplan Schritt 4).
    constexpr uint8_t WAKE_UP_THRESHOLD = 2;   // Schritte a ~62 mg

// =====================================================================
//  5. Lageschaetzung
// =====================================================================

    // Schrittweite der Schwerkraft-Korrektur im Madgwick-Filter. Klein, weil
    // der Beschleunigungssensor bei jeder Handbewegung von der
    // Linearbeschleunigung gestoert ist und der Filter ihm nur langsam folgen
    // soll; das Gyroskop traegt die kurze Zeitskala.
    constexpr float MADGWICK_BETA = 0.033f;    // dimensionslos

    // Nur fuer das Einschwingfenster nach dem Aufwachen. Waehrend des Schlafs
    // bekommt der Filter keine Samples; mit dem normalen Beta brauchte er
    // Sekunden, bis die Lage wieder stimmt - und die Drehgeste haengt an genau
    // diesem Winkel. Dauerhaft waere dieser Wert falsch: der Filter wuerde dann
    // bei jeder Handbewegung von der Linearbeschleunigung mitgerissen.
    //
    // 0.5 entspricht rund 28.6 Grad/s Korrekturgeschwindigkeit; zusammen mit
    // SleepTuning::settleMs (1500 ms) ergibt das rund 43 Grad Nachfuehrung. Die
    // beiden Zahlen gehoeren miteinander gerechnet, nicht einzeln gewaehlt.
    constexpr float MADGWICK_BETA_FAST = 0.5f;   // dimensionslos

    // Grenzfrequenz, mit der die Erdbeschleunigung aus dem Accelerometer
    // herausgefiltert wird (ergibt lax/lay/laz fuer das ML-Fenster). Eine
    // gehaltene Handhaltung aendert sich im Bereich unter 1 Hz, die
    // Linearbeschleunigung beim Zeigen deutlich darueber - 0.8 Hz trennt
    // beides, ohne die Anzeige traege zu machen.
    constexpr float GRAVITY_LP_HZ = 0.8f;      // Hz

    // Vorzeichen der Unterarmachse. +Y zeigt zur Hand oder zum Ellbogen - das
    // haengt daran, wie herum das Board am Arm sitzt, und dreht elev um.
    // Am Geraet gemessen: Arm heben ergab negatives elev, also -1.
    //
    // Wird bewusst erst im Controller angewandt, nicht in ArmOrientation: nur
    // so bleibt jenes Modul ohne config.h uebersetzbar und PC-testbar.
    constexpr float ELEV_SIGN = -1.f;          // +1 oder -1

// =====================================================================
//  6. Handhaltung  [auch in PoseTuning, lib/PoseDetector/]
// =====================================================================
//
// Ein Neigungs-Gate entscheidet zuerst: ausserhalb gibt es nur Idle, egal wie
// die Hand steht. Erst innerhalb waehlt die geglaettete Verdrehung zwischen
// Point und Turned. Idle ist also KEINE Zone der Verdrehung, sondern allein das
// Ergebnis des Gates.
//
// Es gibt zwei solche Gates mit verschiedenen Aufgaben: das enge, symmetrische
// LEVEL_MAX_DEG ist Voraussetzung der Ein/Aus-Drehgeste, das weite,
// asymmetrische POSE_UP/DOWN entscheidet ueber Idle.

    // Verdrehung um die Unterarmachse (arm::twistDeg) in der Zeige-Haltung.
    // Fester Bezugspunkt statt einer Kalibrierung beim Einschalten: das Board
    // sitzt immer gleich am Arm, der Nullpunkt ist also eine Eigenschaft der
    // Bauform und keine der einzelnen Sitzung. Kalibriert wurde bisher direkt
    // nach der Einschaltgeste - die Lageschaetzung ist dann noch von der
    // Bewegung gestoert, und der Bezugspunkt fiel jedes Mal anders aus. Am
    // Teleplot-Kanal "twist" ablesen: was dort in ruhiger Zeige-Haltung steht,
    // gehoert hierhin. Am Geraet bestaetigt: 0 ist richtig.
    constexpr float TWIST_NEUTRAL_DEG = 0.f;   // Grad

    // Die Verdrehachse hat nur eine Schwelle, mit Hysterese. Was eine
    // Ausdrehung bedeutet, entscheidet sich erst beim Zurueckdrehen
    // (TwistToggle): schnell zurueck schaltet ein oder aus, gehalten wird
    // daraus der Scroll-Modus, mit einem Pinch dazwischen war es ein
    // Rechtsklick.
    //
    // Verglichen wird der BETRAG der Verdrehung. Eine Richtungskonstante gibt
    // es bewusst nicht: aus der Zeige-Haltung heraus ist TURN_ON_DEG anatomisch
    // nur in einer Richtung erreichbar (Supination ~90 Grad, Pronation nur 10
    // bis 30), also muss der Code die Richtung nicht kennen.
    constexpr float TURN_ON_DEG  = 70.f;       // Grad
    constexpr float TURN_OFF_DEG = 55.f;       // Grad

    // Ein tieferer Scheitelwinkel als zweite Bedeutung derselben Ausdrehung
    // (Ziehen) ist wieder ausgebaut: er lag auf derselben Achse wie Ein/Aus,
    // und eine etwas zu weit geratene Schaltgeste wurde dadurch stillschweigend
    // zum Ziehen - Ein/Aus verlor messbar an Zuverlaessigkeit. Das Ziehen haengt
    // jetzt am Doppel-Pinch (DRAG_WINDOW_MS).

    // Waagrecht-Bedingung der EIN/AUS-GESTE. Absolut gegen die Schwerkraft
    // gemessen und nicht relativ zum Einschalten: "waagrecht" soll waagrecht
    // heissen, sonst kalibriert man sich die Bedingung beim Einschalten in einer
    // schiefen Haltung gleich weg.
    //
    // Von 35 auf 50 Grad geweitet. 35 klang nach "waagrecht", war aber der
    // haeufigste stille Killer der Ein/Aus-Geste: beim weiten Ausdrehen kippt
    // der Unterarm mit, und reisst das Gate mitten in der Drehung, gilt die
    // ganze Ausdrehung als verbraucht - ohne jeden Hinweis. Bei 50 Grad stehen
    // immer noch 64 Prozent der Schwerkraft quer zur Armachse (cos 50 = 0.64),
    // die Verdrehung bleibt also gut beobachtbar.
    //
    // Zaehler nTwLvl im Teleplot zeigt, wie oft das Gate noch zuschlaegt.
    constexpr float LEVEL_MAX_DEG  = 50.f;     // Grad
    constexpr float LEVEL_HYST_DEG =  8.f;     // Grad

    // Neigungs-Gate der HALTUNG. Beobachtbar ist die Verdrehung, solange die
    // Schwerkraft eine Komponente quer zur Unterarmachse hat; deren Betrag ist
    // sqrt(ux^2+uz^2) = cos(elev). Bei 35 Grad sind das noch 82 Prozent des
    // Signals - viel zu frueh zum Aufgeben. Erst jenseits von rund 70 Grad wird
    // atan2f(ux, uz) wirklich schlecht konditioniert.
    //
    // Asymmetrisch, weil die beiden Richtungen verschiedene Dinge bedeuten:
    // nach oben zeigt und scrollt man (Scrollen HEISST den Arm neigen), nach
    // unten haengt der Arm im Ruhezustand.
    constexpr float POSE_UP_MAX_DEG   = 65.f;  // Grad
    constexpr float POSE_DOWN_MAX_DEG = 35.f;  // Grad

    // Im laufenden Scroll-Modus gilt das Gate nicht - sonst beendete das
    // Scrollen sich selbst. Diese Schranke bleibt: so steil steht der Unterarm
    // fast senkrecht, die Verdrehung ist nicht mehr beobachtbar, und die Haltung
    // bliebe lieber stehen als auf einem Rauschwert umzuspringen.
    constexpr float POSE_HOLD_MAX_DEG = 80.f;  // Grad

    // Der Modus folgt der gehaltenen Haltung, nicht den Ausschlaegen einer
    // schnellen Bewegung. MODE_TAU ist deutlich kuerzer als frueher (0.25 s):
    // die Glaettung verzoegert Hin- und Rueckflanke um je eine Zeitkonstante,
    // und das 1-s-Fenster der Ein/Aus-Geste waere damit um die Haelfte
    // verschmiert.
    constexpr float    MODE_TAU      = 0.10f;  // s
    constexpr uint32_t MODE_DWELL_MS = 150;    // ms Haltezeit vor dem Wechsel

    // Zweite, langsamere Glaettung derselben Verdrehung - nur fuer die
    // Roll-Kompensation im Zeiger. Dort steht der Winkel in einer Drehmatrix,
    // und deren Rauschen landet unmittelbar als Zittern im Cursor. Die
    // schnellere MODE_TAU waere an dieser Stelle ein Rueckschritt.
    constexpr float    ROLLCOMP_TAU  = 0.25f;  // s

    // Waehrend einer heftigen Bewegung wird die Haltung gar nicht erst
    // gewechselt. Glaettung, Haltezeit und Hysterese daempfen die Ausschlaege
    // nur; eine gehaltene Haltung ist aber per Definition nichts, was man
    // mitten im Schwung einnimmt.
    //
    // Eine zuegige 90-Grad-Drehung erzeugt rund 300 Grad/s und friert damit die
    // Haltungsentscheidung ein. TwistToggle arbeitet deshalb auf dem Winkel und
    // nicht auf pose() - die Winkel laufen waehrend der Sperre weiter, nur die
    // Entscheidung ruht.
    //
    // Der Wert stand frueher zwischen dem normalen Gebrauch (~250 Grad/s) und
    // der weggefallenen Schuettel-Schwelle. Seit dem Wegfall steht er nur noch
    // auf einem Bein und gehoert am Geraet gegengeprueft.
    constexpr float    POSE_STILL_DPS = 300.f; // Grad/s
    constexpr uint32_t POSE_CALM_MS   = 250;   // ms Ruhe nach der Bewegung

// =====================================================================
//  7. Zeigen  [auch in PointerTuning, lib/OrientationPointer/]
// =====================================================================

    // Pixel pro Grad Drehung: stepX = rate[Grad/s] * SENS_X * dt[s], und
    // rate*dt sind genau die in diesem Takt gedrehten Grad. Von 100 auf 110
    // angehoben; Casiez et al. 2008 zeigen, dass zu niedrige Verstaerkung klar
    // schadet und zu hohe kaum. Gegenpruefen steht noch aus.
    constexpr float SENS_X    = 110.0f;        // px/Grad
    constexpr float SENS_Y    = 110.0f;        // px/Grad

    // Beschleunigung AUS. Sie greift NACH dem 1-Euro-Filter und multipliziert
    // deshalb auch das Restzittern - bis zum Vierfachen. Scotto et al. 2020
    // fanden eine linear steigende Verstaerkung ohnehin schlechter als eine
    // gute konstante. Die Konstante bleibt stehen, damit sich die Gegenprobe
    // ohne Reflash fahren laesst:
    //
    //   PointerTuning t;  t.accelK = 2.f;
    //   OrientationPointer mitBeschleunigung(t);
    constexpr float ACCEL_K   = 0.0f;          // Faktor pro 200 Grad/s
    constexpr float ACCEL_MAX = 4.0f;          // Obergrenze des Faktors

    // Faengt den Rest-Nullpunktfehler ab, den die Bias-Korrektur uebriglaesst.
    // Gegen das Wackeln hilft sie nur begrenzt - Tremor erzeugt rund 12 Grad/s
    // und liegt weit ueber jeder vertretbaren Totzone. Weiter anzuheben kostet
    // feine Bewegung direkt: 2.5 Grad/s sind bei SENS_X = 110 schon 275 px/s,
    // die stufenlos abgezogen werden.
    constexpr float DEADZONE = 3.5f;           // Grad/s

    // 1-Euro-Filter (Casiez et al. 2012). Die Grenzfrequenz waechst mit der
    // Geschwindigkeit: cutoff = EURO_MIN_CUTOFF + EURO_BETA * geglaettete Rate.
    //
    // EURO_DCUTOFF ist der Tiefpass auf der Geschwindigkeit selbst, aus dem
    // Original. Ohne ihn folgt die Grenzfrequenz dem Betrag des Signals und
    // steht bei Handzittern genau auf den Spitzen am weitesten offen.
    //
    // Der wirksame Hebel gegen das Wackeln ist aber EURO_BETA: der Mittelwert
    // des Tremors liegt bei rund 7.6 Grad/s, geglaettet wie ungeglaettet. Mit
    // 0.55 ergab das eine mittlere Grenzfrequenz von 5.1 Hz und damit kaum
    // Daempfung bei 10 Hz; mit 0.2 sind es 2.5 Hz.
    //
    // Einstellen in dieser Reihenfolge (Casiez et al. 2012): erst EURO_BETA auf
    // 0 und EURO_MIN_CUTOFF senken, bis die ruhig gehaltene Hand einen ruhigen
    // Cursor ergibt, dann EURO_BETA anheben, bis die Verzoegerung beim Zeigen
    // verschwindet.
    constexpr float EURO_DCUTOFF    = 1.0f;    // Hz
    constexpr float EURO_MIN_CUTOFF = 1.0f;    // Hz
    constexpr float EURO_BETA       = 0.2f;    // Hz pro Grad/s

    // Zeitkonstante des festen Tiefpasses - nur der Vergleichspfad von
    // USE_ONE_EURO. Ein fester Wert muss sich zwischen Zittern und Verzoegerung
    // entscheiden; genau das ist der zu messende Unterschied.
    constexpr float SMOOTH_TAU = 0.024f;       // s

    // Ausblendung der Aufwaertsbewegung, bevor der Arm an seine anatomische
    // Reichweite laeuft: ab ELEV_LIMIT - ELEV_FADE stetig herunter auf null.
    // Bezug ist die Armneigung (arm::elevDeg), nicht mehr irgendein Pitch der
    // Platine - die alte Benennung war genau der Grund, warum hier die
    // Handverdrehung ankam und die senkrechte Bewegung schon bei gerader Hand
    // wegschnitt.
    constexpr float ELEV_LIMIT = 45.f;         // Grad
    constexpr float ELEV_FADE  = 12.f;         // Grad

    // Berichtsintervall zum Host. Die Pruefung laeuft im Haupttakt
    // (now_us - tMove_ < MOVE_INTERVAL_US), nicht per eigenem Timer - ein
    // gewuenschter Wert rundet deshalb immer auf ein ganzzahliges Vielfaches
    // von SAMPLE_INTERVAL_US (4785 us) auf. 5000 ergab so real 9570 us (zwei
    // Takte), nicht die angenommenen 5000. Die Werte unten sind so gewaehlt,
    // dass die Aufrundung exakt aufgeht:
    //
    //   USB (kein Verbindungsintervall, Host pollt jede Millisekunde):
    //     4000 us -> ein Takt  -> real 4785 us, jeder Tick ein Bericht.
    //   BLE (Verbindungsintervall 7.5-15 ms, Gegenstelle darf ablehnen):
    //     7500 us -> zwei Takte -> real 9570 us, am kurzen Ende des Intervalls
    //     - kuerzer bringt nichts, die Pakete stauen sich dann nur in der
    //     Warteschlange.
#if USE_BLE_HID
    constexpr uint32_t MOVE_INTERVAL_US = 7500;   // us
#else
    constexpr uint32_t MOVE_INTERVAL_US = 4000;   // us
#endif

    // Wie viele Berichte hoechstens im selben Takt hintereinander gehen, um
    // einen Rueckstau abzubauen. Ein einzelner Bericht traegt hoechstens 127 px
    // je Achse; ohne Nachschieben braucht ein Rueckstau von 300 px drei
    // Intervalle. Die Obergrenze verhindert, dass eine haengende Gegenstelle
    // die Schleife blockiert.
    constexpr int MOVE_MAX_REPORTS = 3;        // Pakete je Takt

    // Obergrenze des Bewegungs-Rueckstaus je Achse. Zwei Pakete (2 x 127)
    // federn eine kurzzeitig volle Warteschlange ab; alles darueber ist kein
    // Rueckstau mehr, sondern eine fehlende Verbindung - und der Cursor schoesse
    // beim Verbinden quer ueber den Schirm.
    constexpr float MOVE_BACKLOG_MAX = 254.f;  // px

// =====================================================================
//  8. Klickerkennung  [auch in PinchTuning, lib/PinchDetector/]
// =====================================================================
//
// Kette: accMag -> Hochpass -> Betrag -> Tiefpass -> env -> bi-level Gate ->
// (nur bei offenem Gate) ML-Inferenz -> Entprellung + Gyro-Guard -> Klick.

    // Huellkurve der Erschuetterung. Ein Pinch erzeugt einen kurzen,
    // hochfrequenten Impuls; der Hochpass entfernt Erdbeschleunigung und
    // Handbewegung, der Tiefpass macht aus dem Impuls eine auswertbare
    // Einhuellende.
    constexpr float HP_CUTOFF_HZ = 30.f;       // Hz
    constexpr float ENV_LP_HZ    = 15.f;       // Hz

    // Bi-Level-Schwelle: oeffnet bei ENV_ON, schliesst erst unter ENV_OFF.
    // Verhindert Flattern des Gates an der Schwelle (Katsuragawa et al. 2019).
    //
    // Aus der ersten Aufnahme: Untergrund 0.005 bis 0.02, echte Pinches 0.045
    // bis 0.125. Die bisherigen 0.018/0.012 lagen also mitten im Untergrund -
    // das Gate oeffnete staendig, und jede Fehlausloesung musste danach der
    // Klassifikator abfangen. Jetzt klar oberhalb des Untergrunds und weit
    // unterhalb der schwaechsten echten Pinches.
    constexpr float ENV_ON  = 0.035f;          // g (Huellkurve)
    constexpr float ENV_OFF = 0.020f;          // g (Huellkurve)

    // Eigene, HOEHERE Schwelle fuer den Abbruch der Ein/Aus-Drehgeste.
    //
    // Bisher brach ENV_ON sie ab, und das war der zweite stille Killer: eine
    // zuegige 90-Grad-Drehung hebt die Huellkurve selbst ueber 0.035, ohne dass
    // ein Pinch stattgefunden haette. Die Geste galt dann als verbraucht und
    // schaltete nicht - ohne Hinweis. Echte Pinches liegen bei 0.045 bis 0.125,
    // eine Schwelle knapp darunter trennt beides.
    //
    // Der Zweck des Abbruchs bleibt: ein vom Modell VERPASSTER Pinch waehrend
    // der Ausdrehung darf beim Zurueckdrehen nicht die Maus abschalten. Deshalb
    // haengt er weiter an der Huellkurve und nicht am erkannten Klick.
    // Zaehler nTwCan im Teleplot zeigt, wie oft er noch greift.
    constexpr float TWIST_CANCEL_ENV = 0.050f; // g (Huellkurve)

    // Erschuetterungen mitten in einer heftigen Bewegung sind kein Pinch,
    // sondern deren Nebenwirkung.
    //
    // OFFEN: bei jedem Pinch schiesst gyroSum auf 200 bis 250, der Guard steht
    // aber auf 100. Am Geraet eingezoomt nachpruefen, ob die gsum-Spitze zum
    // Zeitpunkt der env-Spitze schon abgeklungen ist - sonst blockiert der
    // Guard genau die Klicks, die er durchlassen soll.
    constexpr float    PINCH_GYRO_GUARD = 100.f;   // Grad/s

    // Kuerzester Abstand zweier gewerteter Pinches. Deckt das Nachschwingen
    // eines Kontakts ab und sperrt den LOESE-Impuls aus, der sonst als zweiter
    // Pinch gaelte - genau der war der Grund, warum frueher jeder Rechtsklick
    // doppelt ausloeste.
    constexpr uint32_t DEBOUNCE_MS = 200;      // ms

    // Zusaetzliche Sperre nach einem RECHTSKLICK - der bleibt ein ganzer Klick
    // und nimmt am Doppel-Pinch nicht teil. Ohne sie schliesst der Loese-Impuls
    // des Fingers das eben geoeffnete Kontextmenue wieder.
    constexpr uint32_t RIGHT_CLICK_HOLDOFF_MS = 700;   // ms

    // Klick oder Ziehen entscheidet die BEWEGUNG. Nach dem Pinch bleibt die
    // Taste unten; bewegt sich der Cursor um mehr als DRAG_MOVE_PX, ist es ein
    // Ziehen, sonst geht sie nach DRAG_WINDOW_MS wieder hoch.
    //
    // Der DRUCK kommt sofort - das Fenster verzoegert nur das Loslassen. Genau
    // daran scheiterten die frueheren Doppel-Pinch-Entwuerfe: sie verzoegerten
    // den ganzen Klick, und man musste zusaetzlich ein Zeitband treffen.
    //
    // Der Weg wird als Summe der Betraege gezaehlt, nicht als Verschiebung: ein
    // Hin und Her waere sonst null, obwohl die Hand deutlich gezogen hat.
    // Waehrend der Erschuetterung selbst steht der Zeiger ohnehin still
    // (PinchDetector::inFreeze), das Zittern des Klicks zaehlt also nicht mit.
    //
    // 12 px ist rund ein Zehntel Grad Armbewegung bei SENS_X = 110 px/Grad -
    // klar mehr als Handzittern, klar weniger als eine gemeinte Bewegung.
    constexpr float    DRAG_MOVE_PX   = 12.f;  // px, Summe der Betraege
    constexpr uint32_t DRAG_WINDOW_MS = 350;   // ms

    // Zwei Sicherungen dagegen, dass die Taste stillschweigend haengt.
    //
    // Die Zwangsfreigabe ist die Notbremse: eine gehaltene Taste, die nur der
    // Nutzer wieder loesen kann, macht den Rechner unbenutzbar, wenn die
    // Loesegeste einmal nicht erkannt wird. 30 s sind laenger als jedes
    // vernuenftige Ziehen und kurz genug, um nicht zu stoeren.
    constexpr uint32_t DRAG_MAX_MS    = 30000;  // ms

    // Erinnerungsimpuls, damit ein laufendes Ziehen nie unbemerkt bleibt.
    // Danach sperrt der Controller die Klickerkennung kurz - sonst laese sie die
    // eigene Vibration als Pinch und beendete das Ziehen, das sie meldet.
    constexpr uint32_t DRAG_REMIND_MS      = 2000;  // ms
    constexpr uint32_t DRAG_REMIND_BLIND_MS = 200;  // ms Sperre nach dem Impuls

    // Obergrenze fuer das Einfrieren des Cursors nach einem Klick. Frueher war
    // das eine feste Zeit von 120 ms - der Cursor stand also nach jedem Klick,
    // auch wenn die Erschuetterung laengst vorbei war. Jetzt friert er nur,
    // solange das env-Gate offen ist; diese Zahl ist nur noch die Notbremse
    // fuer den Fall, dass das Gate haengt.
    constexpr uint32_t FREEZE_MAX_MS = 60;     // ms

    // Mindestwahrscheinlichkeit der Klasse "pinch". Zusammen mit der
    // angehobenen env-Schwelle die zweite Bremse gegen Fehlausloesungen; 0.5
    // hiess: alles, was eher Pinch als nicht ist.
    constexpr float    ML_CONFIDENCE = 0.65f;  // 0..1

// =====================================================================
//  9. Scroll-Joystick  [auch in ScrollTuning, lib/ScrollJoystick/]
// =====================================================================
//
// Im Scroll-Modus zaehlt die gehaltene Armneigung relativ zum Eintrittswinkel
// (Positionssignal, driftet nicht), nicht die Drehrate.

    // Die alten Werte (8 Grad Totzone, 0.45 Schritte/s pro Grad) ergaben bei
    // 20 Grad Neigung ganze 5 Schritte pro Sekunde - man neigte die Hand weit
    // und es passierte fast nichts. Totzone deutlich verkleinert, damit das
    // Scrollen frueh einsetzt, und die Verstaerkung fast verdreifacht: 10 Grad
    // ergeben jetzt rund 8 Schritte/s, 20 Grad laufen an die Obergrenze.
    //
    // Die Totzone darf trotzdem nicht auf null: der Eintrittswinkel wird beim
    // Moduswechsel gemerkt, und ohne Totzone wuerde schon das Zittern der
    // gehaltenen Hand langsam scrollen.
    constexpr float    SCROLL_DEAD_DEG    = 3.f;    // Grad
    constexpr float    SCROLL_GAIN        = 1.2f;   // Schritte/s pro Grad
    constexpr float    SCROLL_MAX_HZ      = 25.f;   // Schritte/s
    constexpr float    SCROLL_INVERT      = 1.f;    // -1.f dreht die Richtung

    // Ausgabetakt. Kurz genug, dass bei hoher Rate ein gleichmaessiger Lauf
    // herauskommt und kein Sprung aus mehreren Schritten auf einmal.
    constexpr uint32_t SCROLL_INTERVAL_MS = 40;     // ms

// =====================================================================
// 10. Haptische Rueckmeldung
// =====================================================================
//
// Der Vibrationsmotor wird REIN DIGITAL geschaltet, nicht per PWM: er kennt nur
// an und aus. Die Information steckt in der ANZAHL der Impulse und in ihrer
// DAUER:
//
//   1 kurz  Linksklick, Haltungswechsel
//   2 kurz  Rechtsklick - er passiert in einer Haltung, in der man den Cursor
//           nicht sieht
//   3 kurz  Ziehen an oder aus - der Cursor ist sichtbar, der Tastenzustand nicht
//   1 lang  Ein/Aus - das einzige Ereignis, nach dem gar nichts mehr geht.
//           Ein langer Puls hebt sich sauberer ab als jede Anzahl kurzer, die
//           bei vier Impulsen ohnehin zu einem Brummen verschmelzen.

    constexpr int      HAPTIC_PIN = D1;        // digitaler Ausgang, aktiv HIGH
    constexpr uint32_t HAPTIC_MS  = 40;        // ms Impulsdauer
    constexpr uint32_t HAPTIC_LONG_MS = 200;   // ms, Ein/Aus

    // Luecke zwischen zwei Impulsen DESSELBEN Musters. Perzeptiv begruendet:
    // zwei Buzz muessen als getrennt spuerbar bleiben, nicht als ein langer.
    constexpr uint32_t HAPTIC_GAP_MS  = 50;    // ms

    // Ruhezeit NACH einem Muster, bevor das naechste starten darf - eine andere
    // Rolle als HAPTIC_GAP_MS, obwohl beide frueher denselben Wert teilten.
    // Das Zwei-Impuls-Muster dauert 2*HAPTIC_MS + HAPTIC_GAP_MS = 130 ms; mit
    // HAPTIC_REST_MS = 50 laege das Fenster bei exakt 180 ms, genau auf
    // DEBOUNCE_MS, ohne jede Reserve. Da jeder der drei Phasenuebergaenge
    // (busy_ frei, used_-Fenster, Debounce) erst im naechsten ~4.785-ms-Takt
    // erkannt wird, addiert sich bis zu 3x Takt-Jitter (~14 ms) obendrauf - der
    // zweite Buzz eines schnellen Doppel-Rechtsklicks fiele bei genau 180 ms
    // also im schlechtesten Fall still aus. 30 ms Reserve deckt das ab.
    constexpr uint32_t HAPTIC_REST_MS = 30;    // ms

    // Taktbremse der Teleplot-Ausgabe. 50 Hz sind fuer jede Groesse hier
    // ausreichend fein und halten die Serial-Last in Grenzen.
    constexpr uint32_t DEBUG_INTERVAL_US = 20000;  // us

// =====================================================================
// 11. Akku
// =====================================================================

    // Selten genug, dass die Messung selbst nichts kostet - die Spannung
    // aendert sich ueber Stunden, nicht ueber Sekunden.
    constexpr uint32_t BATTERY_INTERVAL_MS = 30000;   // ms

    // Volt je ADC-Schritt: 3.0 V Referenz / 4096 Schritte, multipliziert mit
    // dem Teilerverhaeltnis der XIAO (ueblicherweise 1 M / 510 k, also
    // (1000+510)/510 = 2.961).
    //
    // Der Wert ist ein Startwert und gehoert kalibriert: eine bekannte
    // Akkuspannung mit dem Multimeter messen und gegen den Kanal vbat halten,
    // dann den Faktor nachziehen. Die Angaben zum Teiler sind in der Literatur
    // uneinheitlich, und eine Laufzeitangabe ist nur so gut wie die
    // Spannungsmessung, auf der sie beruht.
    constexpr float BATTERY_VOLTS_PER_LSB = (3.0f / 4096.f) * 2.961f;   // V/LSB

}   // namespace cfg
