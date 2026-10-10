"""Regressions for the independent screenshot oracle used by Android CI."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location(
    "android_presentation", Path(__file__).parent / "android" / "presentation.py")
presentation = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = presentation
spec.loader.exec_module(presentation)


def painted(stage):
    width, height, background, rectangles = presentation.scene(stage)
    pixels = bytearray(bytes((*background, 255)) * width * height)
    for left, top, w, h, color in rectangles:
        row = bytes((*color, 255)) * w
        for y in range(top, top + h):
            at = (y * width + left) * 4
            pixels[at:at + len(row)] = row
    return presentation.Screen(width, height, bytes(pixels))


def change_rectangle(screen, left, top, width, height, color):
    pixels = bytearray(screen.rgba)
    row = bytes((*color, 255)) * width
    for y in range(top, top + height):
        at = (y * screen.width + left) * 4
        pixels[at:at + len(row)] = row
    return presentation.Screen(screen.width, screen.height, bytes(pixels))


class FramebufferFormatTests(unittest.TestCase):
    def test_current_rgba_header_and_colorspace(self):
        rgba = b"\x10\x20\x30\xff\x40\x50\x60\xff"
        screen = presentation.decode_screencap(struct.pack("<IIII", 2, 1, 1, 1) + rgba)
        self.assertEqual((2, 1), (screen.width, screen.height))
        self.assertEqual((64, 80, 96), screen.pixel(1, 0))

    def test_legacy_rgbx_header_makes_diagnostics_opaque(self):
        screen = presentation.decode_screencap(struct.pack("<III", 1, 1, 2) + b"\x10\x20\x30\x00")
        self.assertEqual(b"\x10\x20\x30\xff", screen.rgba)

    def test_malformed_or_unsupported_framebuffers_are_rejected(self):
        for raw in (b"short", struct.pack("<IIII", 2, 2, 1, 0) + b"x" * 3,
                    struct.pack("<IIII", 1, 1, 4, 0) + b"x" * 4,
                    struct.pack("<IIII", 0, 1, 1, 0)):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                presentation.decode_screencap(raw)

    def test_stage_marker_encodings_and_partial_writes(self):
        for encoding in ("utf-8", "utf-8-sig", "utf-16"):
            self.assertEqual((5, 353, 277), presentation.parse_stage("5 353 277\n".encode(encoding)))
        for raw in (b"", b"5 353 277", b"5 353 ", b"10 353 277\n", b"\xff\xfe\x31"):
            self.assertIsNone(presentation.parse_stage(raw))


class DisplayOracleTests(unittest.TestCase):
    def test_all_stages_accept_complete_displayed_images(self):
        for stage in range(1, 10):
            with self.subTest(stage=stage):
                self.assertEqual(77, presentation.validate(painted(stage), stage))

    def test_letterboxing_scaling_and_rotation_preserve_sampling(self):
        source = painted(3)
        width, height = source.height * 2 + 26, source.width * 2 + 22
        output = bytearray(b"\x08\x08\x08\xff" * width * height)
        for y in range(source.height):
            for x in range(source.width):
                color = bytes((*source.pixel(x, y), 255))
                target_x, target_y = 13 + (source.height - 1 - y) * 2, 11 + x * 2
                for yy in (target_y, target_y + 1):
                    at = (yy * width + target_x) * 4
                    output[at:at + 8] = color * 2
        self.assertEqual(77, presentation.validate(presentation.Screen(width, height, bytes(output)), 3))

    def test_advancing_script_stage_cannot_hide_a_stale_display(self):
        with self.assertRaises(AssertionError):
            presentation.validate(painted(1), 2)

    def test_both_disjoint_dirty_row_ranges_must_reach_the_display(self):
        screen = painted(2)
        screen = change_rectangle(screen, 64, 0, screen.width - 128, 24, (32, 48, 64))
        with self.assertRaises(AssertionError):
            presentation.validate(screen, 2)

    def test_partial_upload_preserves_untouched_pixels(self):
        screen = painted(3)
        screen = change_rectangle(screen, screen.width // 2 - 3, 87, 7, 7, (0, 0, 0))
        with self.assertRaises(AssertionError):
            presentation.validate(screen, 3)

    def test_top_and_bottom_upload_coordinates_cannot_be_inverted(self):
        screen = painted(2)
        screen = change_rectangle(screen, 64, 0, screen.width - 128, 24, (48, 96, 224))
        screen = change_rectangle(screen, 64, screen.height - 24, screen.width - 128, 24, (224, 48, 48))
        with self.assertRaises(AssertionError):
            presentation.validate(screen, 2)

    def test_growing_texture_requires_uploading_new_bottom_rows(self):
        screen = painted(5)
        screen = change_rectangle(screen, 0, screen.height - 20, screen.width, 20, (0, 0, 0))
        with self.assertRaises(AssertionError):
            presentation.validate(screen, 5)

    def test_resizing_cannot_reuse_the_old_display_contents(self):
        with self.assertRaises(AssertionError):
            presentation.validate(painted(4), 5)
        with self.assertRaises(AssertionError):
            presentation.validate(painted(6), 7)

    def test_padded_pitch_cannot_be_interpreted_as_logical_width(self):
        screen = painted(4)
        padded = b"".join(screen.rgba[y * screen.width * 4:(y + 1) * screen.width * 4] + b"\0" * 12
                          for y in range(screen.height))
        corrupted = presentation.Screen(screen.width, screen.height, padded[:len(screen.rgba)])
        with self.assertRaises(AssertionError):
            presentation.validate(corrupted, 4)


class DisplayHandshakeTests(unittest.TestCase):
    def test_acknowledgements_require_matching_framebuffer_pixels(self):
        calls = []
        screen = painted(2)  # The script claims stage 1, but these pixels differ.
        raw = struct.pack("<IIII", screen.width, screen.height, 1, 1) + screen.rgba

        def adb(*args, **kwargs):
            calls.append(args)
            data = b"1 321 241\n" if args[:3] == ("shell", "-T", "cat") else (
                raw if args == ("exec-out", "screencap") else b"123\n")
            return subprocess.CompletedProcess(args, 0, data, b"")

        with tempfile.TemporaryDirectory() as directory:
            with mock.patch.object(presentation, "Path", side_effect=lambda path: Path(directory) / path), \
                    mock.patch.object(presentation, "adb", side_effect=adb), \
                    mock.patch.object(presentation.time, "monotonic", side_effect=(0, 0, 46)), \
                    mock.patch.object(presentation.time, "sleep"):
                with self.assertRaisesRegex(AssertionError, "timed out"):
                    presentation.drive("presentation-test", "/fixture-output")
        self.assertFalse(any(call[0] == "push" for call in calls))

    def test_all_nine_verified_stages_are_acknowledged_and_saved(self):
        current, acknowledgements = 1, []

        def adb(*args, **kwargs):
            nonlocal current
            if args[:3] == ("shell", "-T", "cat"):
                width, height = presentation.scene(current)[:2]
                data = f"{current} {width} {height}\n".encode("utf-16")
            elif args == ("exec-out", "screencap"):
                screen = painted(current)
                data = struct.pack("<IIII", screen.width, screen.height, 1, 1) + screen.rgba
            elif args[0] == "push":
                acknowledgements.append(Path(args[1]).read_text().strip())
                current += 1
                data = b""
            else:
                raise AssertionError(f"Unexpected adb call: {args}")
            return subprocess.CompletedProcess(args, 0, data, b"")

        with tempfile.TemporaryDirectory() as directory:
            with mock.patch.object(presentation, "Path", side_effect=lambda path: Path(directory) / path), \
                    mock.patch.object(presentation, "adb", side_effect=adb), mock.patch("builtins.print"):
                presentation.drive("presentation-test", "/fixture-output")
            self.assertEqual(9, len(list(Path(directory).rglob("*.png"))))
        self.assertEqual([str(stage) for stage in range(1, 10)], acknowledgements)


if __name__ == "__main__":
    unittest.main()
