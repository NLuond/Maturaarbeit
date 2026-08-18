# Testrunner fuer `pio test -e native`.
#
# Die Tests benutzen keinen Rahmen: jede Datei unter test/ ist ein
# eigenstaendiges C++-Programm mit eigenem main(), das seine Pruefungen zaehlt
# und so ausgibt:
#
#     FEHLER Zeile 42: die Bremse oeffnet zu frueh
#     117 Pruefungen, 1 Fehler
#
# Damit bleiben es Programme, die sich auch mit einem einzigen g++-Aufruf
# uebersetzen lassen. Dieser Runner uebersetzt das Format in PlatformIO-
# Testfaelle: jede FEHLER-Zeile ein fehlgeschlagener, die Zusammenfassung ein
# bestandener Fall.

import re

from platformio.test.result import TestCase, TestCaseSource, TestStatus
from platformio.test.runners.base import TestRunnerBase

# "  FEHLER Zeile 42: die Bremse oeffnet zu frueh"
FAILURE_RE = re.compile(r"^\s*FEHLER Zeile (\d+):\s*(.+?)\s*$")

# "117 Pruefungen, 0 Fehler"
SUMMARY_RE = re.compile(r"^\s*(\d+) Pruefungen, (\d+) Fehler\s*$")


class CustomTestRunner(TestRunnerBase):
    def on_testing_line_output(self, line):
        if self.options.verbose:
            super().on_testing_line_output(line)

        failure = FAILURE_RE.match(line)
        if failure:
            self.test_suite.add_case(
                TestCase(
                    name=failure.group(2),
                    status=TestStatus.FAILED,
                    message=failure.group(2),
                    stdout=line,
                    source=TestCaseSource(
                        filename=self.test_suite.test_name,
                        line=int(failure.group(1)),
                    ),
                )
            )
            return None

        summary = SUMMARY_RE.match(line)
        if summary:
            checks, failures = int(summary.group(1)), int(summary.group(2))
            # Die einzelnen Fehlschlaege stehen schon als eigene Testfaelle
            # oben; hier kommt nur noch der Sammeleintrag dazu. Ein Testlauf
            # ganz ohne Pruefungen gilt als Fehler - dann stimmt etwas mit der
            # Datei nicht, und "keine Fehler" waere die falsche Auskunft.
            if failures == 0 and checks > 0:
                self.test_suite.add_case(
                    TestCase(
                        name="%d Pruefungen" % checks,
                        status=TestStatus.PASSED,
                        stdout=line,
                    )
                )
            elif checks == 0:
                self.test_suite.add_case(
                    TestCase(
                        name="keine Pruefungen ausgefuehrt",
                        status=TestStatus.FAILED,
                        message="der Test hat keine einzige Pruefung gemeldet",
                        stdout=line,
                    )
                )
            self.test_suite.on_finish()

        return None
