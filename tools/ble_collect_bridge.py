#!/usr/bin/env python3
"""BLE-Bruecke fuer den Edge-Impulse-Data-Forwarder.

Die Firmware sendet im COLLECT_MODE mit COLLECT_OVER_BLE dieselben CSV-Zeilen
wie ueber USB, nur mit einem 16-Bit-Zaehler davor. Dieses Skript liest sie ueber
den Nordic UART Service, prueft den Zaehler auf Lueckenlosigkeit, streift ihn ab
und schiebt den Rest in einen virtuellen COM-Port. Auf dessen Gegenstueck zeigt
der Forwarder:

    py tools/ble_collect_bridge.py --port COM10
    edge-impulse-data-forwarder --frequency 208     # dort COM11 waehlen

Selbsttest des Zeilenzusammenbaus - ohne Geraet, ohne COM-Port, ohne bleak:

    py tools/ble_collect_bridge.py --selftest
"""

import argparse
import asyncio
import queue
import sys
import threading
import time

# Nordic UART Service. Gesucht wird ueber die UUID und nicht ueber den Namen:
# die HID-Firmware wirbt unter demselben BLE_NAME.
NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX      = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # Geraet -> PC, notify

# feat::CHANNELS. Eine Zeile mit anderer Feldzahl ist ein zusammengeklebtes
# Bruchstueck und damit ein Datenverlust, den der Zaehler allein nicht sieht.
CHANNELS = 5

SEQ_MODULO = 1 << 16

LF   = b"\n"
CR   = b"\r"
CRLF = b"\r\n"


class LineAssembler:
    """Setzt CSV-Zeilen aus BLE-Brocken zusammen und prueft den Zaehler.

    feed() liefert die fertigen Nutzlasten ohne Zaehler und ohne Zeilenende.
    Die Zaehler lines/lost/gaps/bad sind das Urteil ueber die Aufnahme: alles
    ausser lines muss 0 bleiben.
    """

    def __init__(self):
        self._buf = bytearray()
        self._last_seq = None
        self._synced = False
        self.lines = 0
        self.lost  = 0
        self.gaps  = 0
        self.bad   = 0

    def feed(self, chunk):
        self._buf += chunk
        out = []
        while True:
            nl = self._buf.find(LF)
            if nl < 0:
                break
            raw = bytes(self._buf[:nl])
            del self._buf[:nl + 1]
            if not self._synced:
                # Der erste Brocken faengt fast immer mitten in einer Zeile an.
                # Ein Sample beim Verbinden zu verlieren ist harmlos, es als
                # kaputt zu melden waere es nicht.
                self._synced = True
                continue
            line = raw.rstrip(CR)
            if line:
                payload = self._accept(line)
                if payload is not None:
                    out.append(payload)
        return out

    def _accept(self, line):
        head, _, rest = line.partition(b",")
        # Die Feldzahl faengt den Fall ab, den der Zaehler allein nicht sieht:
        # fehlen Bytes mitten in einer Zeile, kleben zwei Haelften zusammen und
        # der Zaehler der zweiten verschwindet mit ihnen.
        if len(rest.split(b",")) != CHANNELS:
            self.bad += 1
            return None
        try:
            seq = int(head.decode("ascii"))
        except (ValueError, UnicodeDecodeError):
            self.bad += 1
            return None

        if self._last_seq is not None:
            missing = (seq - self._last_seq - 1) % SEQ_MODULO
            if missing:
                self.gaps += 1
                self.lost += missing
        self._last_seq = seq
        self.lines += 1
        return rest


def _summary(asm):
    """Bilanz der Sitzung, am Ende ausgegeben.

    Ein einzelner verlorener Brocken macht nicht die ganze Sitzung unbrauchbar,
    sondern die Aufnahme, die gerade lief - deshalb laeuft die Bruecke weiter
    und urteilt am Schluss, statt beim ersten Fehler abzubrechen.
    """
    total = asm.lines + asm.lost
    if not total:
        return "Keine Daten empfangen."
    if not (asm.gaps or asm.bad):
        return "Sauber: %d Zeilen, keine Luecke." % asm.lines
    return ("Verlust: %d Luecke(n), %d von %d Samples fehlen (%.2f %%), %d kaputte Zeilen.\n"
            "Nur die Aufnahmen verwerfen, die waehrend einer Luecke liefen."
            % (asm.gaps, asm.lost, total, 100.0 * asm.lost / total, asm.bad))


class SerialSink:
    """Schreibt die Zeilen in den COM-Port, ohne den Aufrufer aufzuhalten.

    com0com blockiert write(), solange niemand das Gegenstueck des Paars liest -
    und das tut der Forwarder erst, wenn er gestartet ist. Direkt aus dem
    BLE-Callback geschrieben friert das die ganze asyncio-Schleife ein: keine
    Statuszeile mehr, und die Benachrichtigungen stauen sich unsichtbar im
    Speicher. Deshalb ein eigener Faden mit begrenzter Warteschlange - was nicht
    hineinpasst, wird verworfen und gezaehlt, statt alles anzuhalten.
    """

    def __init__(self, port, depth=64):
        self._port = port
        self._queue = queue.Queue(maxsize=depth)
        self.dropped = 0
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def write(self, data, lines):
        try:
            self._queue.put_nowait(data)
        except queue.Full:
            self.dropped += lines

    def close(self):
        self._queue.put(None)
        self._thread.join(timeout=2.0)

    def _run(self):
        while True:
            item = self._queue.get()
            if item is None:
                return
            try:
                self._port.write(item)
            except Exception:
                # Ein toter Port darf den Faden nicht beenden: sonst laeuft die
                # Warteschlange voll und alles Weitere zaehlt als verworfen,
                # ohne dass die Ursache noch sichtbar waere.
                self.dropped += 1


async def _bridge(args):
    # Erst hier, nicht am Kopf der Datei: --selftest laeuft damit auch dort, wo
    # die Pakete fehlen - und wo sie fehlen, ist eine Anweisung mehr wert als
    # ein Traceback. PlatformIO bringt einen eigenen Python 3.11 mit, der in
    # manchen Terminals vor dem System-Python steht.
    try:
        from bleak import BleakClient, BleakScanner
        import serial
    except ImportError as exc:
        print("Modul fehlt: %s" % (exc.name or exc))
        print("Dieser Python hat es nicht: %s" % sys.executable)
        print()
        print("Entweder mit 'py' statt 'python' starten, oder nachruesten:")
        print("    \"%s\" -m pip install -r tools/requirements.txt" % sys.executable)
        return 2

    print("Suche Geraet mit Nordic UART Service ...")
    wanted = NUS_SERVICE.lower()
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: wanted in [u.lower() for u in (ad.service_uuids or [])],
        timeout=args.scan_timeout)
    if dev is None:
        print("Kein Geraet gefunden. Laeuft die Firmware mit COLLECT_MODE = true")
        print("und COLLECT_OVER_BLE = true, und ist sie nicht schon anderweitig verbunden?")
        return 2

    asm = LineAssembler()
    port = serial.Serial(args.port, args.baud)
    sink = SerialSink(port)

    def on_notify(_handle, data):
        payloads = asm.feed(bytes(data))
        if payloads:
            sink.write(CRLF.join(payloads) + CRLF, len(payloads))

    try:
        async with BleakClient(dev) as client:
            await client.start_notify(NUS_TX, on_notify)
            # Nur fuer die Statuszeile: unter 100 kommt die Funkstrecke fuer
            # 208 Hz nicht mit. Nicht jede Plattform gibt den Wert her.
            try:
                mtu = str(client.mtu_size)
            except Exception:
                mtu = "?"
            print("Verbunden mit %s (%s), MTU %s -> %s"
                  % (dev.name or "?", dev.address, mtu, args.port))
            print("Jetzt den Forwarder auf das Gegenstueck des COM-Paars zeigen lassen.")
            print("Bis er liest, nimmt com0com nichts an - 'verworfen' steigt so lange")
            print("und faellt danach auf 0. Ctrl-C beendet.")

            seen, tossed, missed, broken = 0, 0, 0, 0
            t_last = time.monotonic()
            while client.is_connected:
                await asyncio.sleep(1.0)
                now = time.monotonic()
                # Alles je Sekunde, nicht kumuliert: nur so ist zu sehen, dass
                # das Verwerfen aufhoert, sobald der Forwarder liest, und wann
                # genau eine Luecke aufgetreten ist.
                rate = (asm.lines - seen) / (now - t_last)
                tossed_now = sink.dropped - tossed
                lost_now = asm.lost - missed
                bad_now = asm.bad - broken
                seen, tossed = asm.lines, sink.dropped
                missed, broken, t_last = asm.lost, asm.bad, now

                print("  %6.1f Zeilen/s   %8d gesamt   %d verworfen"
                      % (rate, asm.lines, tossed_now))
                # Eigene, auffaellige Zeile: sie soll im Rueckblick zeigen, WANN
                # es passiert ist - davon haengt ab, welche Aufnahme betroffen war.
                if lost_now or bad_now:
                    print("  >>> %s LUECKE: %d Samples fehlen, %d kaputte Zeilen"
                          % (time.strftime("%H:%M:%S"), lost_now, bad_now))
    finally:
        sink.close()
        port.close()
        print()
        print(_summary(asm))

    return 1 if (asm.gaps or asm.bad) else 0


def _selftest():
    checks = [0]
    failures = [0]

    def check(cond, msg):
        checks[0] += 1
        if not cond:
            failures[0] += 1
            line = sys._getframe(1).f_lineno
            print("  FEHLER Zeile %d: %s" % (line, msg))

    def synced():
        """Assembler, der die Startsynchronisation schon hinter sich hat."""
        a = LineAssembler()
        a.feed(b"muell\n")
        return a

    def sample(seq, first=0.0123):
        return b"%d,%.4f,-1.234,0.512,-0.033,0.998\n" % (seq, first)

    # 1 - eine ganze Zeile in einem Brocken
    a = synced()
    out = a.feed(sample(1))
    check(out == [b"0.0123,-1.234,0.512,-0.033,0.998"],
          "ganze Zeile: Zaehler nicht abgestreift, out=%r" % (out,))
    check(a.lines == 1, "ganze Zeile: lines != 1")

    # 2 - eine Zeile ueber zwei Brocken verteilt
    a = synced()
    check(a.feed(b"7,0.0123,-1.2") == [], "halbe Zeile darf nichts liefern")
    out = a.feed(b"34,0.512,-0.033,0.998\n")
    check(out == [b"0.0123,-1.234,0.512,-0.033,0.998"],
          "geteilte Zeile falsch zusammengesetzt: %r" % (out,))

    # 3 - mehrere Zeilen in einem Brocken
    a = synced()
    out = a.feed(sample(10) + sample(11) + sample(12))
    check(len(out) == 3, "drei Zeilen in einem Brocken: %d geliefert" % len(out))
    check(a.lines == 3, "drei Zeilen: lines != 3")

    # 4 - der erste Brocken beginnt mitten in einer Zeile
    a = LineAssembler()
    out = a.feed(b"34,0.512,-0.033,0.998\n" + sample(9))
    check(len(out) == 1, "Bruchstueck vor der ersten Zeile nicht verworfen")
    check(a.bad == 0, "verworfenes Bruchstueck darf nicht als bad zaehlen")

    # 5 - CRLF wird zu einer sauberen Nutzlast
    a = synced()
    out = a.feed(b"3,0.0123,-1.234,0.512,-0.033,0.998\r\n")
    check(out == [b"0.0123,-1.234,0.512,-0.033,0.998"],
          "CR nicht entfernt: %r" % (out,))

    # 6 - eine Luecke im Zaehler wird erkannt und beziffert
    a = synced()
    a.feed(sample(100))
    a.feed(sample(104))
    check(a.gaps == 1, "Luecke nicht erkannt")
    check(a.lost == 3, "Luecke falsch beziffert: lost=%d, erwartet 3" % a.lost)

    # 7 - der Ueberlauf des 16-Bit-Zaehlers ist keine Luecke
    a = synced()
    a.feed(sample(65535))
    a.feed(sample(0))
    check(a.gaps == 0, "Ueberlauf faelschlich als Luecke gewertet")

    # 8 - eine Zeile ohne Zaehler
    a = synced()
    out = a.feed(b"0.0123,-1.234,0.512,-0.033\n")
    check(out == [], "kaputte Zeile darf nichts liefern")
    check(a.bad == 1, "kaputte Zeile nicht als bad gezaehlt")

    # 9 - ein Zaehler, der keine Zahl ist
    a = synced()
    a.feed(b"x,0.0123,-1.234,0.512,-0.033,0.998\n")
    check(a.bad == 1, "nichtnumerischer Zaehler nicht als bad gezaehlt")

    # 10 - zwei Zeilen zusammengeklebt, weil dazwischen Bytes fehlen
    a = synced()
    a.feed(b"100,0.0123,-1.234,0.512,-0.033,0.998,0.512,-0.033,0.998\n")
    check(a.bad == 1, "falsche Feldzahl nicht als bad gezaehlt")

    # 11 - eine leere Zeile ist kein Fehler
    a = synced()
    out = a.feed(b"\n" + sample(5))
    check(a.bad == 0, "leere Zeile faelschlich als bad gezaehlt")
    check(len(out) == 1, "leere Zeile hat die folgende Zeile verschluckt")

    # 12 - eine saubere Sitzung wird als sauber bilanziert
    a = synced()
    for i in range(200):
        a.feed(sample(i))
    s = _summary(a)
    check(s.startswith("Sauber"), "saubere Sitzung falsch bilanziert: %r" % s)
    check("200" in s, "Zeilenzahl fehlt in der Bilanz: %r" % s)

    # 12b - eine Luecke steht mit Zahl und Anteil in der Bilanz
    a = synced()
    a.feed(sample(1))
    a.feed(sample(100))     # 98 fehlen
    for i in range(101, 201):
        a.feed(sample(i))
    s = _summary(a)
    check(not s.startswith("Sauber"), "Luecke nicht beanstandet: %r" % s)
    check("98" in s, "Zahl der fehlenden Samples fehlt: %r" % s)
    check("%" in s, "Anteil fehlt in der Bilanz: %r" % s)

    # 12c - ohne Daten wird nichts beschoenigt
    check("Keine Daten" in _summary(LineAssembler()),
          "leere Sitzung nicht als solche gemeldet")

    # 13 - der Sink reicht durch, was der Port annimmt
    class FakePort(object):
        def __init__(self):
            self.got = []

        def write(self, data):
            self.got.append(data)

    p = FakePort()
    sink = SerialSink(p)
    for _ in range(5):
        sink.write(b"zeile\r\n", 1)
    sink.close()
    check(p.got == [b"zeile\r\n"] * 5, "Sink reicht nicht durch: %r" % (p.got,))
    check(sink.dropped == 0, "durchgereichte Zeilen faelschlich als verworfen gezaehlt")

    # 14 - ein Port, der nichts annimmt, darf den Aufrufer NICHT aufhalten. Das
    # ist der Alltagsfall: der Forwarder laeuft noch nicht, com0com blockiert,
    # und der Aufrufer ist der BLE-Callback der asyncio-Schleife.
    class StalledPort(object):
        def write(self, data):
            time.sleep(0.05)

    sink = SerialSink(StalledPort(), depth=4)
    t0 = time.monotonic()
    for _ in range(40):
        sink.write(b"zeile\r\n", 1)
    elapsed = time.monotonic() - t0
    check(elapsed < 0.5,
          "write() haelt am stehenden Port auf: %.2f s fuer 40 Zeilen" % elapsed)
    check(sink.dropped > 0, "verworfene Zeilen werden nicht gezaehlt")
    sink.close()

    print("%d Pruefungen, %d Fehler" % (checks[0], failures[0]))
    return 1 if failures[0] else 0


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port",
                   help="virtueller COM-Port, in den geschrieben wird (z.B. COM10)")
    p.add_argument("--baud", type=int, default=115200,
                   help="Baudrate des COM-Ports; com0com ignoriert sie, der Forwarder nicht")
    p.add_argument("--scan-timeout", type=float, default=15.0,
                   help="Sekunden, die nach dem Geraet gesucht wird")
    p.add_argument("--selftest", action="store_true",
                   help="nur den Zeilenzusammenbau pruefen, ohne Geraet")
    args = p.parse_args()

    if args.selftest:
        return _selftest()
    if not args.port:
        p.error("--port wird gebraucht (oder --selftest)")
    try:
        return asyncio.run(_bridge(args))
    except KeyboardInterrupt:
        print("\nBeendet.")
        return 0


if __name__ == "__main__":
    sys.exit(main())
