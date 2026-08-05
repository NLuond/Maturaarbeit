#pragma once
// Selbst tragend inkludieren: cfg:: benutzt uint32_t und den Pin-Namen D1.
// Ohne diese beiden Zeilen kompiliert die Datei nur, wenn der Includer vorher
// zufaellig <Arduino.h> gezogen hat.
#include <Arduino.h>
#include <stdint.h>

#define USE_ML_PINCH    true
#define DEBUG_TELEPLOT  true

// Welche Teleplot-Kanaele gesendet werden. Alle 24 gleichzeitig sind rund
// 17 kB/s - das kostet Rechenzeit in der Schleife, und eine zu langsame
// Schleife dehnt genau das ML-Fenster, das man gerade untersucht. Fuer eine
// gezielte Messung deshalb auf die passende Gruppe stellen.
#define DEBUG_ALL       0
#define DEBUG_PINCH     1   // env, gate, p_ml, click, gsum, nDeb, nGyro, drag
#define DEBUG_POINT     2   // gx/gy/gz, rx, ry, pacc, accx, mvfail, twist, elev,
                            // rtwist, level, srate
// DEBUG_ORIENT prueft die Einbaulage nach und steht bewusst neben DEBUG_ALL,
// nicht darin: er teilt gx/gy/gz und twist/elev mit DEBUG_POINT.
#define DEBUG_ORIENT    3   // ax/ay/az roh + gvx/gvy/gvz geglaettet, angX/angY/angZ,
                            // gx/gy/gz, twist, elev, rtwist, level
#define DEBUG_SET       DEBUG_POINT
// Voruebergehend auf USB-HID (TinyUSB), weil sich das Geraet gerade nicht per
// Bluetooth koppeln laesst. Fuer die Arbeit bleibt BLE das Ziel - zum Einstellen
// von Kennlinie und Haltungen ist der Uebertragungsweg aber egal, und ueber USB
// faellt das Verbindungsintervall als Stoergroesse weg.
#define USE_BLE_HID     false
#define COLLECT_MODE    false

// Haltungserkennung (Zeigen / Idle / Scrollen). Auf false meldet der Detektor
// dauerhaft die Zeige-Haltung - zum Eingrenzen bei Cursor-Problemen.
#define USE_POSE_MODE   true

// Cursor-Glaettung: 1-Euro-Filter (true) gegen den festen Tiefpass mit
// SMOOTH_TAU (false). Umschaltbar, damit sich der Unterschied messen laesst.
#define USE_ONE_EURO    true

// Roll-Kompensation: rechnet die Handverdrehung aus der Cursorbewegung heraus,
// damit eine waagerechte Handbewegung bei verdrehter Hand nicht schraeg laeuft.
// Auf false laufen die Achsen ungedreht wie zuvor - das ist der A/B-Vergleich
// fuer die Evaluation. In der Grundhaltung (twist = 0) sind beide identisch,
// der Unterschied zeigt sich erst bei verdrehter Hand.
#define USE_ROLL_COMP   true

namespace cfg {
    // --- Sensor ---------------------------------------------------------
    constexpr int ACCEL_RANGE_G  = 4;
    constexpr int ACCEL_ODR_HZ   = 208;
    constexpr int ACCEL_BW_HZ    = 100;
    constexpr int GYRO_RANGE_DPS = 500;
    constexpr int GYRO_ODR_HZ    = 208;

    // Der LSM6DS3 kann 400 kHz. Bei sechs Werten je Takt ist das der
    // Unterschied zwischen rund 2 ms und rund 0.5 ms Schleifenzeit - gesetzt
    // wird die Rate erst nach imu_.begin(), weil Wire.begin() sie dort auf die
    // Arduino-Vorgabe von 100 kHz zuruecksetzt.
    constexpr uint32_t I2C_CLOCK_HZ = 400000;

    // Feste Schrittweite der ganzen Verarbeitung. Muss zur Abtastrate des
    // Modells passen (209 Hz), sonst sieht der Klassifikator ein zeitlich
    // gestauchtes Fenster. PinchDetector prueft das beim Kompilieren.
    constexpr uint32_t SAMPLE_INTERVAL_US = 4785;
    constexpr float    DT                 = SAMPLE_INTERVAL_US * 1e-6f;

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
    // Die zweite Bedingung schliesst aus, dass eine gleichfoermige Drehung
    // ohne Drehratenanteil durchrutscht: ein ruhendes Board misst genau 1 g.
    // BIAS_TAU laenger, weil echter Temperaturdrift langsam ist.
    constexpr float BIAS_STILL_DPS = 3.f;
    constexpr float BIAS_ACC_TOL   = 0.05f;   // erlaubte Abweichung von 1 g
    constexpr float BIAS_TAU       = 5.0f;

    // --- Pinch ----------------------------------------------------------
    constexpr float HP_CUTOFF_HZ = 30.f;
    constexpr float ENV_LP_HZ    = 15.f;

    // Bi-Level-Schwelle: oeffnet bei ENV_ON, schliesst erst unter ENV_OFF.
    // Verhindert Flattern des Gates an der Schwelle (Katsuragawa et al. 2019).
    //
    // Aus der ersten Aufnahme: Untergrund 0.005 bis 0.02, echte Pinches 0.045
    // bis 0.125. Die bisherigen 0.018/0.012 lagen also mitten im Untergrund -
    // das Gate oeffnete staendig, und jede Fehlauslösung musste danach der
    // Klassifikator abfangen. Jetzt klar oberhalb des Untergrunds und weit
    // unterhalb der schwaechsten echten Pinches.
    constexpr float ENV_ON  = 0.035f;
    constexpr float ENV_OFF = 0.020f;

    constexpr float    PINCH_GYRO_GUARD = 100.f;

    // Obergrenze fuer das Einfrieren des Cursors nach einem Klick. Frueher war
    // das eine feste Zeit von 120 ms - der Cursor stand also nach jedem Klick,
    // auch wenn die Erschuetterung laengst vorbei war. Jetzt friert er nur,
    // solange das env-Gate offen ist; diese Zahl ist nur noch die Notbremse
    // fuer den Fall, dass das Gate haengt.
    constexpr uint32_t FREEZE_MAX_MS = 60;

    constexpr uint32_t DEBOUNCE_MS = 180;

    // Zusammen mit der angehobenen env-Schwelle die zweite Bremse gegen
    // Fehlauslösungen. 0.5 hiess: alles, was eher Pinch als nicht ist.
    constexpr float    ML_CONFIDENCE = 0.65f;

    // --- Zeigen ---------------------------------------------------------
    // Pixel pro Grad Drehung: stepX = rate[Grad/s] * SENS_X * dt[s], und rate*dt
    // sind genau die in diesem Takt gedrehten Grad.
    constexpr float SENS_X    = 110.0f;
    constexpr float SENS_Y    = 110.0f;

    // Beschleunigung aus. Sie greift NACH dem 1-Euro-Filter und multipliziert
    // deshalb auch das Restzittern - bis zum Vierfachen. Scotto et al. 2020
    // fanden eine linear steigende Verstaerkung ohnehin schlechter als eine
    // gute konstante. ACCEL_K bleibt als Konstante stehen, damit sich die
    // Gegenprobe ohne Reflash fahren laesst:
    //
    //   PointerTuning t;  t.accelK = 2.f;
    //   OrientationPointer mitBeschleunigung(t);
    constexpr float ACCEL_K   = 0.0f;
    constexpr float ACCEL_MAX = 4.0f;

    // Faengt den Rest-Nullpunktfehler ab, den die Bias-Korrektur uebriglaesst.
    // Gegen das Wackeln hilft sie nur begrenzt - Tremor erzeugt rund 12 Grad/s
    // und liegt weit ueber jeder vertretbaren Totzone. Weiter anzuheben kostet
    // feine Bewegung direkt: 2.5 Grad/s sind bei SENS_X = 110 schon 275 px/s,
    // die stufenlos abgezogen werden.
    constexpr float DEADZONE = 3.5f;

    // 1-Euro-Filter (Casiez et al. 2012). Grenzfrequenz waechst mit der
    // Geschwindigkeit: cutoff = MIN_CUTOFF + BETA * geglaettete Drehrate.
    //
    // EURO_DCUTOFF ist der Tiefpass auf der Geschwindigkeit selbst, aus dem
    // Original. Ohne ihn folgt die Grenzfrequenz dem Betrag des Signals und
    // steht bei Handzittern genau auf den Spitzen am weitesten offen.
    //
    // Der wirksame Hebel gegen das Wackeln ist aber BETA: der Mittelwert des
    // Tremors liegt bei rund 7.6 Grad/s, geglaettet wie ungeglaettet. Mit 0.55
    // ergab das eine mittlere Grenzfrequenz von 5.1 Hz und damit kaum
    // Daempfung bei 10 Hz; mit 0.2 sind es 2.5 Hz.
    //
    // Einstellen in dieser Reihenfolge (Casiez et al. 2012): erst BETA auf 0
    // und MIN_CUTOFF senken, bis die ruhig gehaltene Hand einen ruhigen Cursor
    // ergibt, dann BETA anheben, bis die Verzoegerung beim Zeigen verschwindet.
    constexpr float EURO_DCUTOFF    = 1.0f;   // Hz
    constexpr float EURO_MIN_CUTOFF = 1.0f;   // Hz
    constexpr float EURO_BETA       = 0.2f;   // Hz pro Grad/s

    constexpr float SMOOTH_TAU = 0.024f;      // nur fuer den Vergleichspfad

    // Ausblendung nach oben, bevor der Arm an seine Reichweite laeuft. Bezug ist
    // die Armneigung (arm::elevDeg), nicht mehr irgendein Pitch der Platine -
    // die alte Benennung war genau der Grund, warum hier die Handverdrehung
    // ankam und die senkrechte Bewegung schon bei gerader Hand wegschnitt.
    constexpr float ELEV_LIMIT = 45.f;
    constexpr float ELEV_FADE  = 12.f;

    // Berichtsintervall zum Host. Ueber BLE bringt es nichts, kuerzer als das
    // Verbindungsintervall zu senden - die Pakete warten dann nur in der
    // Warteschlange. Ueber USB pollt der Host jede Millisekunde, dort ist die
    // Halbierung ein direkter Latenzgewinn und verdoppelt zugleich die
    // Obergrenze der uebertragbaren Geschwindigkeit (127 px je Bericht).
#if USE_BLE_HID
    constexpr uint32_t MOVE_INTERVAL_US = 10000;
#else
    constexpr uint32_t MOVE_INTERVAL_US = 5000;
#endif

    // Wie viele Berichte hoechstens im selben Takt hintereinander gehen, um
    // einen Rueckstau abzubauen. Ohne das braucht ein Rueckstau von 300 px drei
    // Intervalle, bis er draussen ist. Die Obergrenze verhindert, dass eine
    // haengende Gegenstelle die Schleife blockiert.
    constexpr int MOVE_MAX_REPORTS = 3;

    // Obergrenze des Bewegungs-Rueckstaus in Pixeln je Achse. Zwei Pakete
    // (2 x 127) federn eine kurzzeitig volle Warteschlange ab; alles darueber
    // ist kein Rueckstau mehr, sondern eine fehlende Verbindung.
    constexpr float MOVE_BACKLOG_MAX = 254.f;

    // --- Handhaltung ----------------------------------------------------
    // Verdrehung um die Unterarmachse (arm::twistDeg) in der Zeige-Haltung.
    // Fester Bezugspunkt statt einer Kalibrierung beim Einschalten: das Board
    // sitzt immer gleich am Arm, der Nullpunkt ist also eine Eigenschaft der
    // Bauform und keine der einzelnen Sitzung. Kalibriert wurde bisher direkt
    // nach dem Schuetteln - die Lageschaetzung ist dann noch von der
    // Schuettelbewegung gestoert, und der Bezugspunkt fiel bei jedem
    // Einschalten anders aus. Am Teleplot-Kanal "twist" ablesen: was dort in
    // ruhiger Zeige-Haltung steht, gehoert hierhin.
    constexpr float TWIST_NEUTRAL_DEG = 0.f;

    // Die Verdrehachse hat nur noch eine Schwelle. Was eine Ausdrehung
    // bedeutet, entscheidet sich erst beim Zurueckdrehen (TwistToggle):
    // schnell zurueck schaltet ein oder aus, gehalten wird daraus der
    // Scroll-Modus, mit einem Pinch dazwischen war es ein Rechtsklick.
    //
    // Verglichen wird der Betrag der Verdrehung. Eine Richtungskonstante gibt
    // es bewusst nicht: aus der Zeige-Haltung heraus ist TURN_ON_DEG
    // anatomisch nur in einer Richtung erreichbar (Supination ~90 Grad,
    // Pronation nur 10 bis 30), also muss der Code die Richtung nicht kennen.
    constexpr float TURN_ON_DEG  = 70.f;
    constexpr float TURN_OFF_DEG = 55.f;

    // Waagrecht-Bedingung. Haengt der Arm herunter oder ist er angehoben, ist
    // keine der drei Haltungen gemeint - der Zustandsautomat bekommt dann Idle,
    // egal wie die Hand verdreht ist. Absolut gemessen und nicht relativ zum
    // Einschalten: "waagrecht" soll waagrecht heissen, sonst kalibriert man sich
    // die Bedingung beim Einschalten in einer schiefen Haltung gleich weg.
    constexpr float LEVEL_MAX_DEG  = 35.f;
    constexpr float LEVEL_HYST_DEG = 8.f;

    // Vorzeichen der Unterarmachse. +Y zeigt zur Hand oder zum Ellbogen - das
    // haengt daran, wie herum das Board am Arm sitzt, und dreht elev um.
    // Am Geraet gemessen: Arm heben ergab negatives elev, also -1.
    constexpr float ELEV_SIGN = -1.f;

    // Der Modus folgt der gehaltenen Haltung, nicht den Ausschlaegen einer
    // schnellen Bewegung. Deutlich kuerzer als frueher (0.25 s): die Glaettung
    // verzoegert Hin- und Rueckflanke um je eine Zeitkonstante, und das
    // 1-s-Fenster der Ein/Aus-Geste waere damit um die Haelfte verschmiert.
    constexpr float    MODE_TAU      = 0.10f;
    constexpr uint32_t MODE_DWELL_MS = 150;

    // Zweite, langsamere Glaettung derselben Verdrehung - nur fuer die
    // Roll-Kompensation im Zeiger. Dort steht der Winkel in einer Drehmatrix,
    // und deren Rauschen landet unmittelbar als Zittern im Cursor. Die
    // schnellere MODE_TAU waere an dieser Stelle ein Rueckschritt.
    constexpr float    ROLLCOMP_TAU  = 0.25f;

    // Waehrend einer heftigen Bewegung wird die Haltung gar nicht erst
    // gewechselt. Glaettung, Haltezeit und Hysterese daempfen die Ausschlaege
    // nur - beim Einschalt-Schuetteln reicht das nicht: gemessen laeuft die
    // Verdrehung dabei ueber 70 Grad, der Detektor durchlaeuft also Idle bis
    // Scroll, mit Haptik und Zeiger-Reset als Nebenwirkung.
    //
    // Die Schwelle liegt ueber den ~250 Grad/s des normalen Gebrauchs und unter
    // SHAKE_ON. Die Ruhezeit danach ist noetig, weil gyroSum zwischen den beiden
    // Schuettel-Spitzen kurz einbricht - ohne sie waere das Fenster dazwischen
    // wieder offen.
    constexpr float    POSE_STILL_DPS = 300.f;
    constexpr uint32_t POSE_CALM_MS   = 250;

    // --- Scroll-Joystick ------------------------------------------------
    // Die alten Werte (8 Grad Totzone, 0.45 Schritte/s pro Grad) ergaben bei
    // 20 Grad Neigung ganze 5 Schritte pro Sekunde - man neigte die Hand weit
    // und es passierte fast nichts. Totzone deutlich verkleinert, damit das
    // Scrollen frueh einsetzt, und die Verstaerkung fast verdreifacht: 10 Grad
    // ergeben jetzt rund 8 Schritte/s, 20 Grad laufen an die Obergrenze.
    //
    // Die Totzone darf trotzdem nicht auf null: der Eintrittswinkel wird beim
    // Moduswechsel gemerkt, und ohne Totzone wuerde schon das Zittern der
    // gehaltenen Hand langsam scrollen.
    constexpr float    SCROLL_DEAD_DEG    = 3.f;
    constexpr float    SCROLL_GAIN        = 1.2f;   // Schritte/s pro Grad
    constexpr float    SCROLL_MAX_HZ      = 25.f;   // Schritte/s Obergrenze
    constexpr float    SCROLL_INVERT      = 1.f;    // -1.f dreht die Richtung
    // Kuerzer getaktet, sonst kommen bei hoher Rate mehrere Schritte als ein
    // Sprung heraus statt als gleichmaessiger Lauf.
    constexpr uint32_t SCROLL_INTERVAL_MS = 40;

    // --- Lage -----------------------------------------------------------
    constexpr float MADGWICK_BETA = 0.033f;

    // Grenzfrequenz, mit der die Erdbeschleunigung aus dem Accelerometer
    // herausgefiltert wird. Eine gehaltene Handhaltung aendert sich im Bereich
    // unter 1 Hz, die Linearbeschleunigung beim Zeigen deutlich darueber -
    // 0.8 Hz trennt beides, ohne die Anzeige traege zu machen.
    constexpr float GRAVITY_LP_HZ = 0.8f;

    // --- Ein/Aus durch Schuetteln ---------------------------------------
    constexpr float    SHAKE_ON         = 350.f;
    constexpr float    SHAKE_OFF        = 180.f;
    constexpr uint32_t SHAKE_REFRACT_MS = 80;
    constexpr uint32_t SHAKE_GAP_MIN_MS = 100;
    constexpr uint32_t SHAKE_GAP_MAX_MS = 450;
    constexpr uint32_t SHAKE_LOCKOUT_MS = 800;

    // --- Haptik und Debug -----------------------------------------------
    constexpr int      HAPTIC_PIN = D1;
    constexpr uint32_t HAPTIC_MS  = 40;

    // Luecke zwischen zwei Impulsen desselben Musters. Sie ist zugleich die
    // Ruhezeit nach einem Muster, bevor das naechste starten darf.
    constexpr uint32_t HAPTIC_GAP_MS = 50;

    // Teleplot kostet Serial-Bandbreite und bremst die Schleife. Fuer echte
    // Nutzungstests DEBUG_TELEPLOT ganz ausschalten.
    constexpr uint32_t DEBUG_INTERVAL_US = 20000;
}



