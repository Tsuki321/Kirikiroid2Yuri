"""Host-side regressions for the synthetic Android dialog driver."""
import importlib.util
from itertools import repeat
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock
import xml.etree.ElementTree as ET


spec = importlib.util.spec_from_file_location(
    "android_dialogs", Path(__file__).parent / "android" / "dialogs.py")
dialogs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dialogs)

OUTPUT = "/data/user/0/com.yuri.kirikiri2/files/engine-ci/dialogs-output"
STAGE_PATH = OUTPUT + "/dialog-stage.txt"
XML_PATH = "/data/local/tmp/ci-dialog.xml"
ENCODINGS = (("utf-8", b""), ("utf-8", b"\xef\xbb\xbf"),
             ("utf-16-le", b"\xff\xfe"), ("utf-16-be", b"\xfe\xff"))


def result(stdout=b"", returncode=0):
    return subprocess.CompletedProcess(["adb"], returncode, stdout, b"")


def stage(number):
    return result(b"\xff\xfe" + (str(number) + "\n").encode("utf-16-le"))


def hierarchy(number):
    root = ET.Element("hierarchy")
    ET.SubElement(root, "node", {"package": dialogs.PACKAGE,
                                "text": "CI_DIALOG_" + str(number)})
    for button, left in (("button1", 10), ("button2", 110), ("button3", 210)):
        ET.SubElement(root, "node", {
            "package": dialogs.PACKAGE, "resource-id": "android:id/" + button,
            "enabled": "true", "bounds": f"[{left},100][{left + 80},140]"})
    if number in (5, 6, 7):
        ET.SubElement(root, "node", {
            "package": dialogs.PACKAGE, "class": "android.widget.EditText",
            "text": {5: "first", 6: "", 7: "cancelled"}[number]})
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


class FakeClock:
    def __init__(self):
        self.now = 0

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class FakeDevice:
    def __init__(self, stages, xml=(), dumps=None, pids=None):
        self.stages = iter(stages)
        self.xml = iter(xml)
        self.dumps = iter(dumps) if dumps is not None else repeat(result())
        self.pids = iter(pids) if pids is not None else repeat(result(b"123\n"))
        self.taps = []

    def adb(self, *args, check=True):
        if args == ("shell", "pidof", dialogs.PACKAGE):
            response = next(self.pids)
        elif args == ("shell", "-T", "cat", STAGE_PATH):
            response = next(self.stages)
        elif args == ("shell", "uiautomator", "dump", "--compressed", XML_PATH):
            response = next(self.dumps)
        elif args == ("shell", "-T", "cat", XML_PATH):
            response = next(self.xml)
        elif args[:3] == ("shell", "input", "tap"):
            self.taps.append(args[3:])
            response = result()
        else:
            raise AssertionError("unexpected adb command: " + repr(args))
        if isinstance(response, Exception):
            raise response
        return response


class StageReaderTest(unittest.TestCase):
    def test_complete_numeric_records(self):
        for encoding, bom in ENCODINGS:
            for newline in ("\n", "\r\n"):
                for number in (1, 8, 9, 12):
                    data = bom + (str(number) + newline).encode(encoding)
                    with self.subTest(data=data), mock.patch.object(
                            dialogs, "adb", return_value=result(data)):
                        self.assertEqual(dialogs.stage_text(OUTPUT), str(number))

    def test_incomplete_bom_value_or_newline_is_retried(self):
        for encoding, bom in ENCODINGS:
            data = bom + "12\r\n".encode(encoding)
            for length in range(len(data)):
                with self.subTest(data=data[:length]), mock.patch.object(
                        dialogs, "adb", return_value=result(data[:length])):
                    self.assertEqual(dialogs.stage_text(OUTPUT), "")

    def test_missing_file_and_failed_reads_are_retried(self):
        missing = b"cat: " + STAGE_PATH.encode() + b": No such file or directory\n"
        for response in (result(missing), result(missing, 1), result(b"1\n", 1)):
            with self.subTest(response=response), mock.patch.object(
                    dialogs, "adb", return_value=response) as adb:
                self.assertEqual(dialogs.stage_text(OUTPUT), "")
                adb.assert_called_once_with("shell", "-T", "cat", STAGE_PATH, check=False)

    def test_malformed_records_are_retried(self):
        for data in (b"0\n", b"01\n", b"-1\n", b" 1\n", b"1 \n", b"1\n2\n",
                     b"1\n\n", b"one\n", b"\xff\xfe\x00\xd8\n\x00"):
            with self.subTest(data=data), mock.patch.object(
                    dialogs, "adb", return_value=result(data)):
                self.assertEqual(dialogs.stage_text(OUTPUT), "")


class DialogDriverTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="android-dialog-tests-")
        self.addCleanup(directory.cleanup)
        self.diagnostics = Path(directory.name) / "diagnostics"
        self.clock = FakeClock()

    def drive(self, device, count=1):
        with mock.patch.object(dialogs, "adb", side_effect=device.adb), \
                mock.patch.object(dialogs, "time", self.clock), \
                mock.patch.object(dialogs, "Path", return_value=self.diagnostics), \
                mock.patch.object(dialogs, "BUTTONS", dialogs.BUTTONS[:count]), \
                mock.patch("builtins.print"):
            dialogs.drive("synthetic", OUTPUT)

    def test_transient_reads_recover_before_a_single_tap(self):
        xml = hierarchy(1)
        device = FakeDevice(
            stages=[result(b"cat: missing file\n"), result(b"\xff\xfe"),
                    stage(1), stage(1), stage(1), stage(1), result(b"\xff"),
                    stage(1), stage(1)],
            dumps=[result(returncode=1), result(), result(), result(), result()],
            xml=[result(returncode=1), result(b"<hierarchy>"), result(xml), result(xml)])
        self.drive(device)
        self.assertEqual(device.taps, [("50", "120")])
        self.assertEqual((self.diagnostics / "synthetic-dialog-1.xml").read_bytes(), xml)

    def test_all_eight_dialogs_get_their_own_response(self):
        device = FakeDevice(
            stages=[stage(number) for number in range(1, 9) for _ in range(2)],
            xml=[result(hierarchy(number)) for number in range(1, 9)])
        self.drive(device, count=8)
        self.assertEqual(device.taps, [(str(x), "120")
                                     for x in (50, 250, 150, 50, 50, 50, 250, 50)])

    def test_advance_before_visibility_fails_without_tapping(self):
        device = FakeDevice(stages=[stage(2)])
        with self.assertRaisesRegex(RuntimeError, "returned before the response: CI_DIALOG_1"):
            self.drive(device)
        self.assertEqual(device.taps, [])

    def test_advance_while_visible_fails_without_tapping(self):
        device = FakeDevice(stages=[stage(1), stage(2)], xml=[result(hierarchy(1))])
        with self.assertRaisesRegex(RuntimeError, "unanswered dialog: CI_DIALOG_1"):
            self.drive(device)
        self.assertEqual(device.taps, [])

    def test_completion_marker_detects_final_dialog_returning_early(self):
        device = FakeDevice(
            stages=[stage(number) for number in range(1, 8) for _ in range(2)]
                   + [stage(8), stage(9)],
            xml=[result(hierarchy(number)) for number in range(1, 9)])
        with self.assertRaisesRegex(RuntimeError, "unanswered dialog: CI_DIALOG_8"):
            self.drive(device, count=8)
        self.assertEqual(len(device.taps), 7)

    def test_missing_stage_expires_without_tapping(self):
        device = FakeDevice(stages=repeat(result(returncode=1)))
        with self.assertRaisesRegex(RuntimeError, "timed out waiting for CI_DIALOG_1"):
            self.drive(device)
        self.assertEqual(device.taps, [])
        self.assertGreaterEqual(self.clock.now, 45)
        self.assertLess(self.clock.now, 45.5)

    def test_process_exit_fails_without_tapping(self):
        device = FakeDevice(stages=[], pids=[result(returncode=1)])
        with self.assertRaisesRegex(RuntimeError, "engine exited before CI_DIALOG_1"):
            self.drive(device)
        self.assertEqual(device.taps, [])

    def test_adb_timeout_remains_a_transport_failure(self):
        device = FakeDevice(stages=[stage(1)], dumps=[subprocess.TimeoutExpired("adb", 25)])
        with self.assertRaises(subprocess.TimeoutExpired):
            self.drive(device)
        self.assertEqual(device.taps, [])


if __name__ == "__main__":
    unittest.main()
