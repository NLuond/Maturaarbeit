#pragma once
// Selbst tragend: cfg:: benutzt uint32_t und den Pin-Namen D2.
#include <Arduino.h>
#include <stdint.h>

// =====================================================================
//  Konfiguration der Air Mouse
// =====================================================================
//
// Zwei Teile: #define-Schalter fuer die Betriebsarten (es gibt keine
// Laufzeit-Konfiguration - umstellen heisst neu bauen und flashen) und
// constexpr-Einstellwerte in namespace cfg, gruppiert in der Reihenfolge, in
// der ein Messwert sie durchlaeuft.
//
// Magic Numbers gehoeren hierhin, nicht in die Module. Ausnahme sind die
// hardwarefrei zu haltenden Module: sie tragen ihre Werte in einem eigenen
// Tuning-Struct, unten mit "[auch in <X>Tuning]" markiert. static_asserts in
// AirMouseController.h halten beide Seiten zusammen.

// =====================================================================
//  1. Betriebsarten
// =====================================================================

// Statt HID nur CSV ausgeben (env, gyro/100, lax, lay, laz) fuer die Aufnahme
// der Trainingsdaten; HID und Controller werden nicht initialisiert.
#define COLLECT_MODE    false

// Die Aufnahme ueber BLE-UART statt ueber USB-Serial, fuer die Bruecke in
// tools/ble_collect_bridge.py: ohne Kabel am Arm, dafuer ohne Teleplot. Nur
// zusammen mit COLLECT_MODE wirksam.
#define COLLECT_OVER_BLE true

// ML-Klassifikator gegen reine Schwellwert-Erkennung: auf false entscheidet
// allein das env-Gate.
#define USE_ML_PINCH    true

// BLE (bluefruit) gegen USB-HID (TinyUSB). Im USB-Zweig sind radioOff()/
// radioOn() leere Huellen - dort ist keine Strommessung sinnvoll.
#define USE_BLE_HID     true

// Funk bleibt auch im Schlaf erreichbar. Kostet den mit Abstand groessten
// Ruhestrom, denn geworben wird alle 20 bis 152 ms weiter - auf true wirbt das
// Geraet die ganze Nacht. Wake-on-Motion holt es ohnehin zurueck: eine Bewegung
// genuegt, damit es wieder auffindbar ist.
#define BLE_ALWAYS_ON   false

// Kurz halten: das Advertising-Paket hat 31 Bytes, wovon Flags, Appearance und
// die HID-UUID schon 11 belegen.
#define BLE_NAME        "Maturaarbeit"

// --- Die drei A/B-Vergleiche der Evaluation --------------------------
// Keine Aufraeum-Optionen: sie messen denselben Aufbau mit und ohne einen
// einzelnen Mechanismus.

// Haltungserkennung. Auf false meldet PoseDetector dauerhaft Point; die Winkel
// laufen weiter, die Ein/Aus-Geste liest sie direkt.
#define USE_POSE_MODE   true

// Cursor-Glaettung: 1-Euro-Filter (true) gegen festen Tiefpass mit SMOOTH_TAU.
#define USE_ONE_EURO    true

// Rechnet die Handverdrehung aus der Cursorbewegung heraus; bei twist = 0 sind
// beide Pfade identisch.
#define USE_ROLL_COMP   true

// =====================================================================
//  2. Debug-Ausgabe (Teleplot)
// =====================================================================

// Teleplot-Kanaele (">name:wert"). Kostet Serial-Bandbreite und bremst die
// Schleife - fuer Nutzungstests und jede Strommessung ausschalten.
#define DEBUG_TELEPLOT  true

// Welche Kanalgruppe gesendet wird. Alle gleichzeitig sind rund 17 kB/s, und
// eine zu langsame Schleife dehnt genau das ML-Fenster, das man untersucht.
// Immer gesendet: on, pose, dpose, tw, nClick, nTw*, vbat, ble, adv, ci.
#define DEBUG_ALL       0
#define DEBUG_PINCH     1   // env, envMax, gate, arm, p_ml, click, gsum, nDeb,
                            // nGyro, ei_err, ei_us
#define DEBUG_POINT     2   // gx/gy/gz, rx, ry, pacc, accx, mvfail, twist,
                            // elev, rtwist, rtwraw, level, pgate, sacc, tg, tgr
// DEBUG_ORIENT und DEBUG_ENV stehen bewusst NEBEN DEBUG_ALL, nicht darin: sie
// teilen Kanaele mit DEBUG_POINT bzw. DEBUG_PINCH und kaemen sonst doppelt.
#define DEBUG_ORIENT    3   // Einbaulage: ax/ay/az, gvx/gvy/gvz, angX/angY/angZ
#define DEBUG_ENV       4   // schlanker Satz zur Impulsmessung: env, envMax,
                            // gate, click
#define DEBUG_SET       DEBUG_PINCH

// Misst beim Start die reine Rechenzeit einer Inferenz und gibt eine Zeile aus.
// Noetig, weil der Kanal ei_us im Betrieb nur eine OBERE Schranke liefert: Die
// Messausgabe fuellt den USB-Sendepuffer, und die Aufgabe, die ihn leert, hat
// Vorrang vor der Schleife und unterbricht dabei die laufende Inferenz. Hier
// laeuft nichts nebenher, und der Mittelwert ueber viele Durchlaeufe hebt
// zugleich die Aufloesung von micros() (rund 977 us) auf.
// Nur mit USE_ML_PINCH sinnvoll; DEBUG_TELEPLOT dabei auf false.
#define BENCH_INFERENCE false

namespace cfg {

// =====================================================================
//  3. Sensor (LSM6DS3, on-board)
// =====================================================================

    constexpr int ACCEL_RANGE_G  = 4;      // g, deckt die Spitzen eines Pinches ab
    constexpr int GYRO_RANGE_DPS = 500;    // Grad/s, ueber den ~300 einer zuegigen Drehung
    // Der Schleifentakt folgt DIESEM Wert, nicht umgekehrt: laeuft die Schleife
    // schneller, liest sie denselben Messwert zweimal.
    constexpr int ACCEL_ODR_HZ   = 208;    // Hz
    constexpr int GYRO_ODR_HZ    = 208;    // Hz
    constexpr int ACCEL_BW_HZ    = 100;    // Hz, rund die halbe Abtastrate (Aliasing)

    // Erst nach imu_.begin() setzen: Wire.begin() faellt sonst auf 100 kHz zurueck.
    constexpr uint32_t I2C_CLOCK_HZ = 400000;   // Hz

    // Gyro-Nullpunkt wird nur im Stillstand nachgefuehrt. Die Schwelle muss
    // unter der langsamsten gemeinten Bewegung liegen (Zeigen: 5 bis 10 Grad/s),
    // sonst lernt der Schaetzer die gewollte Drehrate als Nullpunkt.
    constexpr float BIAS_STILL_DPS = 3.f;     // Grad/s
    constexpr float BIAS_ACC_TOL   = 0.05f;   // g, erlaubte Abweichung von 1 g
    constexpr float BIAS_TAU       = 5.0f;    // s, Temperaturdrift ist langsam

// =====================================================================
//  4. Takt und Betriebszustaende
// =====================================================================
//
//   AKTIV   Maus eingeschaltet    IMU 208 Hz   Funk an   208 Hz Takt
//   BEREIT  aus, aber bewegt      IMU  52 Hz   Funk an    52 Hz Takt
//   SCHLAF  60 s ohne Bewegung    nur Accel    Funk aus  loop() suspendiert
//
// Eingeschlafen wird nur aus BEREIT: haelt man den Cursor ruhig auf einem Ziel,
// ist die Drehrate klein, und die Maus verschwaende mitten im Gebrauch. Nach
// 5 min ohne Bewegung schaltet sie sich aus AKTIV aber selbst ab und faellt
// damit nach BEREIT, sonst liefe eine abgelegte Maus die ganze Nacht mit vollem
// Takt und Funk. Die Zeitschwellen stehen in SleepTuning (lib/SleepPolicy/).

    // Feste Schrittweite in AKTIV: exakt 1/ACCEL_ODR_HZ, damit auf jeden
    // Sensorwert genau ein Schleifendurchlauf kommt. Ein schnellerer Takt liest
    // denselben Messwert doppelt und schiebt dem Hochpass eine Stufe unter, ein
    // langsamerer laesst Werte fallen. PinchClassifier haelt den Wert per
    // static_assert an der Rate des Modells.
    constexpr uint32_t SAMPLE_INTERVAL_US = 4808;                  // us (208 Hz)
    constexpr float    DT                 = SAMPLE_INTERVAL_US * 1e-6f;   // s

    // Schrittweite in BEREIT: dort laeuft nur die Drehgeste, und 52 Hz ist die
    // naechste ODR-Stufe des Sensors nach unten. Wieder exakt 1/ODR.
    constexpr uint32_t READY_INTERVAL_US = 19231;                  // us (52 Hz)
    constexpr float    READY_DT          = READY_INTERVAL_US * 1e-6f;     // s

    // Weckschwelle im Schlaf, 6 Bit a ~62 mg: Armheben soll wecken, ein Klopfen
    // auf den Tisch moeglichst nicht.
    constexpr uint8_t WAKE_UP_THRESHOLD = 2;   // Schritte a ~62 mg (~125 mg)

// =====================================================================
//  5. Lageschaetzung
// =====================================================================

    // Schrittweite der Schwerkraft-Korrektur im Madgwick-Filter. Klein, weil der
    // Beschleunigungssensor bei jeder Handbewegung gestoert ist.
    constexpr float MADGWICK_BETA = 0.033f;    // dimensionslos

    // Erlaubte Abweichung von 1 g, damit ein Messwert als reine Schwerkraft
    // durchgeht und die Lage daraus gesetzt werden darf. Bis dahin wird jeden
    // Takt neu gesetzt - ein Einschwingfenster braucht es dadurch nicht.
    constexpr float SEED_ACC_TOL = 0.08f;      // g

    // Grenzfrequenz, mit der die Erdbeschleunigung herausgefiltert wird: trennt
    // die gehaltene Haltung (< 1 Hz) von der Bewegung beim Zeigen.
    constexpr float GRAVITY_LP_HZ = 0.8f;      // Hz

    // Vorzeichen der Unterarmachse; am Geraet gemessen (Arm heben ergab
    // negatives elev). Wird erst im Controller angewandt, damit ArmOrientation
    // ohne config.h uebersetzbar bleibt.
    constexpr float ELEV_SIGN = -1.f;          // +1 oder -1

// =====================================================================
//  6. Handhaltung  [auch in PoseTuning, lib/PoseDetector/]
// =====================================================================
//
// Ein Neigungs-Gate entscheidet zuerst: ausserhalb gibt es nur Idle. Erst
// innerhalb waehlt die geglaettete Verdrehung zwischen Point und Turned.

    // Bezugspunkt der Verdrehung in der Zeige-Haltung. Fest statt beim
    // Einschalten kalibriert: das Board sitzt immer gleich am Arm, und nach der
    // Einschaltgeste ist die Lageschaetzung am staerksten gestoert.
    constexpr float TWIST_NEUTRAL_DEG = 0.f;   // Grad

    // Eine Schwelle mit Hysterese, verglichen wird der Betrag: aus der
    // Zeige-Haltung ist TURN_ON_DEG anatomisch nur in einer Richtung erreichbar.
    constexpr float TURN_ON_DEG  = 70.f;       // Grad
    constexpr float TURN_OFF_DEG = 55.f;       // Grad

    // Waagrecht-Bedingung der Ein/Aus-Geste, absolut gegen die Schwerkraft.
    // Gelesen nur beim START der Geste, und dort beantwortet sie eine einzige
    // Frage: wird die Maus gerade benutzt? Eng, weil genau das die Bremse gegen
    // Fehlausloesungen ist - 45 Grad Unterarmdrehung kommen im Alltag staendig
    // vor, aber kaum mit waagrecht gehaltenem Arm. Zaehler nTwLvl zaehlt die
    // dadurch verhinderten Fehlausloesungen.
    constexpr float LEVEL_MAX_DEG  = 30.f;     // Grad
    constexpr float LEVEL_HYST_DEG =  8.f;     // Grad

    // Neigungs-Gate der Haltung, weiter und asymmetrisch: nach oben zeigt und
    // scrollt man, nach unten haengt der Arm im Ruhezustand.
    constexpr float POSE_UP_MAX_DEG   = 65.f;  // Grad
    constexpr float POSE_DOWN_MAX_DEG = 35.f;  // Grad

    // Im Scroll-Modus gilt das Gate nicht - sonst beendete das Scrollen sich
    // selbst. Nur hier bleibt die Haltung stehen: so steil ist die Verdrehung
    // nicht mehr beobachtbar.
    constexpr float POSE_HOLD_MAX_DEG = 80.f;  // Grad

    // Der Modus folgt der gehaltenen Haltung. MODE_TAU bleibt kurz, weil die
    // Glaettung sonst das 1-s-Fenster der Ein/Aus-Geste verschmiert.
    constexpr float    MODE_TAU      = 0.10f;  // s
    constexpr uint32_t MODE_DWELL_MS = 150;    // ms Haltezeit vor dem Wechsel

    // Zweite, langsamere Glaettung nur fuer die Roll-Kompensation: dort steht
    // der Winkel in einer Drehmatrix, deren Rauschen als Zittern im Cursor landet.
    constexpr float    ROLLCOMP_TAU  = 0.25f;  // s

    // Waehrend heftiger Bewegung wird die Haltung gar nicht erst gewechselt -
    // eine gehaltene Haltung nimmt man nicht mitten im Schwung ein. Gehoert am
    // Geraet gegengeprueft.
    constexpr float    POSE_STILL_DPS = 300.f; // Grad/s
    constexpr uint32_t POSE_CALM_MS   = 250;   // ms Ruhe nach der Bewegung

// ---------------------------------------------------------------------
//  6b. Ein/Aus-Drehgeste  [auch in TwistTuning, lib/TwistToggle/]
// ---------------------------------------------------------------------
//
// Aus der Ruhe heraus (TWIST_ARMED_MS) bei waagrechtem Arm (LEVEL_MAX_DEG)
// zuegig um TWIST_ON_DEG heraus und binnen TWIST_MAX_MS zurueck. Sonst nichts.
//
// Der Ausschlag allein traegt die Geste NICHT - 45 Grad Unterarmdrehung kommen
// im Alltag staendig vor. Erst die beiden Startbedingungen machen sie zur
// Geste, und beide gelten nur am Start.
//
// Gemessen wird der AUSSCHLAG ab dem Beginn der Bewegung, integriert aus der
// Drehrate - kein Winkel gegen TWIST_NEUTRAL_DEG. Die Geste ist damit von der
// Haltung entkoppelt und funktioniert aus jeder Ruhelage heraus.

    // Ausschlag ab der Ruhelage, nicht absolut - deshalb deutlich kleiner als
    // die Haltungsschwelle TURN_ON_DEG. Kleiner heisst weniger ermuedend; wie
    // weit tatsaechlich gedreht wird, zeigt der Teleplot-Kanal twexc.
    constexpr float TWIST_ON_DEG   = 45.f;     // Grad
    constexpr float TWIST_BACK_DEG = 20.f;     // Grad, so nah wieder zurueck

    // Der Hinweg muss zuegig sein - das ist der Preis fuer den kleinen
    // Ausschlag: nur so bleibt die Geste von einer beilaeufigen Armdrehung
    // unterscheidbar, die denselben Winkel ueber Sekunden erreicht.
    constexpr uint32_t TWIST_OUT_MAX_MS =  600;   // ms bis TWIST_ON_DEG
    constexpr uint32_t TWIST_MAX_MS     = 1200;   // ms fuer die ganze Bewegung
    constexpr uint32_t TWIST_LOCKOUT_MS =  800;   // ms Ruhe nach dem Schalten

    // Ab hier laeuft eine Drehung; die Geste startet auf der Flanke dieser
    // Schwelle. Weit ueber dem Gyro-Rauschen und unter der Rate einer
    // absichtlichen Drehung (45 Grad in 350 ms sind im Mittel 129 Grad/s).
    constexpr float TWIST_START_DPS = 60.f;    // Grad/s

    // So lange muss der Unterarm vor der Geste geruht haben. Zweite Bremse
    // gegen Fehlausloesungen neben LEVEL_MAX_DEG: eine bewusste Geste beginnt
    // aus der Ruhe, Alltagsbewegung ist durchgehend. Zaehler nTwMov.
    constexpr uint32_t TWIST_ARMED_MS = 200;   // ms

    // Ab wann eine Erschuetterung als Pinch zaehlt und die Drehung verbraucht:
    // nur ein ruhender Unterarm kann gepincht haben - die Drehung erschuettert
    // das Board am Scheitel selbst. TWIST_RATE_TAU glaettet dafuer, damit der
    // kurze Impuls eines Pinch den Arm nicht als drehend erscheinen laesst.
    constexpr float    TWIST_STILL_DPS = 40.f;   // Grad/s
    constexpr uint32_t TWIST_STILL_MS  = 150;    // ms Ruhe davor
    constexpr float    TWIST_RATE_TAU  = 0.03f;  // s

// =====================================================================
//  7. Zeigen  [auch in PointerTuning, lib/OrientationPointer/]
// =====================================================================

    // Pixel pro Grad Drehung. Casiez et al. 2008: zu niedrige Verstaerkung
    // schadet klar, zu hohe kaum - im Zweifel also eher hoeher. 165 px/Grad
    // heisst rund 12 Grad Armdrehung fuer die volle Breite eines 1920er Schirms.
    //
    // ACHTUNG beim Aendern: dieselben Pixel speisen ueber MotionPipeline auch
    // das Scrollrad. SCROLL_PX_PER_STEP gehoert im selben Verhaeltnis
    // mitgezogen, sonst aendert sich das Scrolltempo unbeabsichtigt mit.
    constexpr float SENS_X    = 165.0f;        // px/Grad
    constexpr float SENS_Y    = 165.0f;        // px/Grad

    // Beschleunigung aus: sie greift nach dem 1-Euro-Filter und multipliziert
    // deshalb auch das Restzittern. Die Konstante bleibt fuer die Gegenprobe
    // ohne Reflash (PointerTuning t; t.accelK = 2.f;).
    constexpr float ACCEL_K   = 0.0f;          // Faktor pro 200 Grad/s
    constexpr float ACCEL_MAX = 4.0f;          // Obergrenze des Faktors

    // Faengt den Rest-Nullpunktfehler ab (BIAS_STILL_DPS = 3 Grad/s). Bleibt
    // trotz hoeherer Verstaerkung stehen: was die Deadzone durchlaesst, wird von
    // SENS_X mitverstaerkt, ein wandernder Cursor faellt jetzt eher auf. Hoeher
    // kostet feine Bewegung direkt - 2.5 Grad/s sind bei 165 px/Grad schon
    // 412 px/s. Abgezogen wird weich, es entsteht also kein Sprung an der Kante.
    constexpr float DEADZONE = 3.5f;           // Grad/s

    // 1-Euro-Filter (Casiez et al. 2012): cutoff = MIN_CUTOFF + BETA * Rate.
    // Einstellen in dieser Reihenfolge - erst BETA auf 0 und MIN_CUTOFF senken,
    // bis die ruhige Hand einen ruhigen Cursor gibt, dann BETA anheben, bis die
    // Verzoegerung verschwindet.
    // Hoeher als die 1 Hz des Originals: dort wird die Geschwindigkeit aus einem
    // verrauschten Positionssignal differenziert, hier kommt sie direkt aus dem
    // Gyro und die Deadzone raeumt das Kleinzittern schon davor weg. 1 Hz hiess
    // 159 ms, bis der Filter eine begonnene Bewegung ueberhaupt bemerkt.
    constexpr float EURO_DCUTOFF    = 3.0f;    // Hz, Tiefpass auf der Geschwindigkeit
    constexpr float EURO_MIN_CUTOFF = 0.6f;    // Hz
    constexpr float EURO_BETA       = 0.2f;    // Hz pro Grad/s

    // Zeitkonstante des festen Tiefpasses - nur der Vergleichspfad von
    // USE_ONE_EURO.
    constexpr float SMOOTH_TAU = 0.024f;       // s

    // Ausblendung der Aufwaertsbewegung, bevor der Arm an seine anatomische
    // Reichweite laeuft: ab ELEV_LIMIT - ELEV_FADE stetig herunter auf null.
    constexpr float ELEV_LIMIT = 45.f;         // Grad
    constexpr float ELEV_FADE  = 12.f;         // Grad

    // Berichtsintervall zum Host. Geprueft wird im Haupttakt, der Wert rundet
    // deshalb auf ein Vielfaches von SAMPLE_INTERVAL_US auf: 4000 -> ein Takt,
    // 7500 -> zwei Takte (9616 us, am kurzen Ende des BLE-Intervalls).
#if USE_BLE_HID
    constexpr uint32_t MOVE_INTERVAL_US = 7500;   // us
#else
    constexpr uint32_t MOVE_INTERVAL_US = 4000;   // us
#endif

// --- Ausgabe ans HID  [auch in MotionTuning, lib/MotionPipeline/] ----

    // Wie viele Berichte hoechstens im selben Takt nachgeschoben werden; ein
    // Bericht traegt hoechstens 127 px je Achse.
    constexpr int MOVE_MAX_REPORTS = 3;        // Pakete je Takt

    // Deckel des Bewegungs-Rueckstaus je Achse: 900 px sind rund 5 Grad und
    // reiten eine Funkstockung aus, ohne dass der Cursor nach Minuten ohne
    // Verbindung quer ueber den Schirm schiesst.
    constexpr float MOVE_BACKLOG_MAX = 900.f;  // px

// =====================================================================
//  8. Klickerkennung  [auch in PinchTuning, lib/PinchDetector/]
// =====================================================================
//
// Kette: accMag -> Hochpass -> Betrag -> Tiefpass -> env -> bi-level Gate ->
// (nur im Armierungsfenster) ML-Inferenz -> Entprellung + Gyro-Guard -> Klick.

    constexpr float HP_CUTOFF_HZ = 30.f;       // Hz, entfernt Erdbeschleunigung und Handbewegung
    constexpr float ENV_LP_HZ    = 15.f;       // Hz, macht aus dem Impuls eine Einhuellende

    // Bi-Level-Schwelle (Katsuragawa et al. 2019). Gemessen: Untergrund 0.005
    // bis 0.02, echte Pinches 0.045 bis 0.125.
    constexpr float ENV_ON  = 0.035f;          // g (Huellkurve)
    constexpr float ENV_OFF = 0.020f;          // g (Huellkurve)

    // Eigene, hoehere Schwelle fuer die Meldung an die Drehgeste: eine zuegige
    // 90-Grad-Drehung hebt die Huellkurve selbst ueber ENV_ON. Ob die Meldung
    // die Ausdrehung verwirft, entscheidet TwistToggle an der Drehrate.
    constexpr float TWIST_CANCEL_ENV = 0.050f; // g (Huellkurve)

    // Wie lange nach der steigenden env-Flanke der Klassifikator befragt wird.
    // Muss den Durchlauf des Impulses durch das 191-ms-Fenster abdecken.
    constexpr uint32_t PINCH_ARM_MS = 250;     // ms

    // Nur jeder n-te Takt kostet eine Inferenz (rund 3 ms von 4808 us Budget).
    constexpr uint8_t  PINCH_ML_STRIDE = 2;

    // Erschuetterungen mitten in einer heftigen Bewegung sind kein Pinch.
    constexpr float    PINCH_GYRO_GUARD = 100.f;   // Grad/s

    // Zweiter Guard, allein auf der Verdrehung des Unterarms - der einzige, den
    // der Scroll-Modus nicht aussetzt: die Ein/Aus-Geste dreht um genau diese
    // Achse. Gescrollt wird durch Neigen und Schwenken, der Rechtsklick
    // verliert also nichts.
    constexpr float    PINCH_TWIST_GUARD = 60.f;   // Grad/s

    // Kuerzester Abstand zweier gewerteter Pinches; sperrt den Loese-Impuls aus,
    // der sonst als zweiter Pinch gaelte.
    constexpr uint32_t DEBOUNCE_MS = 200;      // ms

    // Zusaetzliche Sperre nach einem Rechtsklick: der Loese-Impuls schloesse
    // sonst das eben geoeffnete Kontextmenue wieder.
    constexpr uint32_t RIGHT_CLICK_HOLDOFF_MS = 700;   // ms

    // Sperre nach einer langen Vibration - sie dauert laenger als die
    // Entprellung und wuerde sonst als Pinch gelesen.
    constexpr uint32_t HAPTIC_BLIND_MS = 200;   // ms

    // Notbremse fuer das Einfrieren des Cursors: normalerweise friert er nur,
    // solange das env-Gate offen ist.
    constexpr uint32_t FREEZE_MAX_MS = 60;     // ms

    // Mindestwahrscheinlichkeit der Klasse "pinch"; 0.50 statt 0.65, weil die
    // Validierung von Modell 1084395 (AUC 0.94) Spielraum auf der Seite der
    // Empfindlichkeit zeigt.
    constexpr float    ML_CONFIDENCE = 0.50f;  // 0..1

// =====================================================================
//  9. Scrollen  [auch in ScrollTuning, lib/ScrollWheel/]
// =====================================================================
//
// Gescrollt wird mit derselben Armbewegung wie gezeigt: in der abgedrehten
// Haltung geht die senkrechte Zeigerbewegung ins Rad statt an den Cursor.

    // In Pixeln angegeben, weil das Rad die fertige Zeigerbewegung teilt - die
    // gemeinte Groesse ist aber ein WINKEL: 60 px bei SENS_Y = 165 px/Grad sind
    // rund ein Drittel Grad Armbewegung je Schritt. Wird SENS_Y geaendert,
    // gehoert dieser Wert im selben Verhaeltnis mit, sonst aendert sich das
    // Scrolltempo unbeabsichtigt mit dem Cursortempo.
    //
    // Muss gross genug bleiben, dass eine ruhig gehaltene Hand stillsteht - die
    // Totzone des Zeigers greift hier nicht mehr.
    constexpr float    SCROLL_PX_PER_STEP  = 60.f;  // px je Radschritt
    constexpr float    SCROLL_MAX_PER_TICK = 8.f;   // Schritte je Ausgabe
    constexpr float    SCROLL_INVERT       = 1.f;   // -1.f dreht die Richtung

    // Kurz genug, dass bei hoher Rate ein gleichmaessiger Lauf herauskommt.
    constexpr uint32_t SCROLL_INTERVAL_MS  = 40;    // ms

// =====================================================================
// 10. Haptische Rueckmeldung
// =====================================================================
//
// Rein digital geschaltet, ohne PWM: die Information steckt in der Anzahl der
// Impulse und in ihrer Dauer.
//
//   1 kurz  Linksklick, Haltungswechsel
//   2 kurz  Rechtsklick - er passiert in einer Haltung ohne sichtbaren Cursor
//   1 lang  Ein/Aus - das einzige Ereignis, nach dem gar nichts mehr geht

    constexpr int      HAPTIC_PIN     = D2;    // digitaler Ausgang, aktiv HIGH
    constexpr uint32_t HAPTIC_MS      = 40;    // ms Impulsdauer
    constexpr uint32_t HAPTIC_LONG_MS = 200;   // ms, Ein/Aus

    // Luecke zwischen zwei Impulsen desselben Musters: zwei Buzz muessen als
    // getrennt spuerbar bleiben.
    constexpr uint32_t HAPTIC_GAP_MS  = 50;    // ms

    // Ruhezeit nach einem Muster. Das Zwei-Impuls-Muster dauert 130 ms; mit
    // 30 ms Reserve bleibt der zweite Buzz eines schnellen Doppel-Rechtsklicks
    // auch bei drei Takten Jitter (~14 ms) unter DEBOUNCE_MS hoerbar.
    constexpr uint32_t HAPTIC_REST_MS = 30;    // ms

    // Taktbremse der Teleplot-Ausgabe.
    constexpr uint32_t DEBUG_INTERVAL_US = 20000;  // us (50 Hz)

// =====================================================================
// 11. Akku
// =====================================================================

    // Die Spannung aendert sich ueber Stunden, nicht ueber Sekunden.
    constexpr uint32_t BATTERY_INTERVAL_MS = 30000;   // ms

    // 3.0 V Referenz / 4096 Schritte mal Teilerverhaeltnis der XIAO
    // ((1000+510)/510). Startwert - gegen ein Multimeter kalibrieren.
    constexpr float BATTERY_VOLTS_PER_LSB = (3.0f / 4096.f) * 2.961f;   // V/LSB

}   // namespace cfg
