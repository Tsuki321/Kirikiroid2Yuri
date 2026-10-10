"""Independent checks that movie CI cannot accept stale or missing video frames."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

android = Path(__file__).parent / "android"
spec = importlib.util.spec_from_file_location("android_movie", android / "movie.py")
movie = importlib.util.module_from_spec(spec)
with mock.patch.object(sys, "path", [str(android), *sys.path]):
    spec.loader.exec_module(movie)


def frame(stage, *, rectangle=(80, 80, 160, 80), color=None):
    width, height = 320, 240
    pixels = bytearray(b"\x10\x10\x10\xff" * width * height)
    rectangles = [(24, 40, 16, 16, (255, 0, 255)),
                  (280, 40, 16, 16, (0, 255, 255)),
                  (24, 184, 16, 16, (255, 255, 0)),
                  (280, 184, 16, 16, (255, 128, 0))]
    colors = ((255, 0, 0), (0, 255, 0), (0, 0, 255))
    rectangles.append((*rectangle, color or colors[stage - 1]))
    for left, top, w, h, rgb in rectangles:
        row = bytes((*rgb, 255)) * w
        for y in range(top, top + h):
            at = (y * width + left) * 4
            pixels[at:at + len(row)] = row
    return struct.pack("<IIII", width, height, 1, 1) + pixels


class OverlayPixelsTests(unittest.TestCase):
    def test_red_green_and_blue_at_requested_bounds(self):
        for stage in range(1, 4):
            self.assertEqual(19, movie.validate(movie.decode_screencap(frame(stage)), stage))

    def test_stale_color_is_rejected(self):
        with self.assertRaisesRegex(AssertionError, "expected"):
            movie.validate(movie.decode_screencap(frame(1)), 2)

    def test_black_missing_and_small_movie_are_rejected(self):
        for options in ({"color": (0, 0, 0)}, {"color": (16, 16, 16)},
                        {"rectangle": (80, 80, 64, 64)}):
            with self.subTest(options=options), self.assertRaisesRegex(AssertionError, "expected"):
                movie.validate(movie.decode_screencap(frame(1, **options)), 1)

    def test_movie_overwriting_surrounding_scene_is_rejected(self):
        with self.assertRaisesRegex(AssertionError, "expected"):
            movie.validate(movie.decode_screencap(frame(1, rectangle=(64, 64, 192, 112))), 1)


class OverlayHandshakeTests(unittest.TestCase):
    def test_only_verified_frames_are_acknowledged_and_saved(self):
        current, acknowledgements = 1, []

        def adb(*args, **kwargs):
            nonlocal current
            if args[:3] == ("shell", "-T", "cat"):
                data = f"{current} 320 240\n".encode("utf-16")
            elif args == ("exec-out", "screencap"):
                data = frame(current)
            elif args[0] == "push":
                acknowledgements.append(Path(args[1]).read_text().strip())
                current += 1
                data = b""
            else:
                raise AssertionError(f"Unexpected adb call: {args}")
            return subprocess.CompletedProcess(args, 0, data, b"")

        with tempfile.TemporaryDirectory() as directory:
            with mock.patch.object(movie, "Path", side_effect=lambda path: Path(directory) / path), \
                    mock.patch.object(movie, "adb", side_effect=adb), mock.patch("builtins.print"):
                movie.drive("movie-test", "/fixture-output")
            self.assertEqual(3, len(list(Path(directory).rglob("*.png"))))
        self.assertEqual(["1", "2", "3"], acknowledgements)

    def test_invalid_frame_times_out_without_acknowledging(self):
        calls = []

        def adb(*args, **kwargs):
            calls.append(args)
            if args == ("exec-out", "screencap"):
                return subprocess.CompletedProcess(args, 0, frame(1, color=(0, 0, 0)), b"")
            if args[-1].endswith("movie-stage.txt"):
                return subprocess.CompletedProcess(args, 0, "1 320 240\n".encode("utf-16"), b"")
            return subprocess.CompletedProcess(args, 1, b"", b"")

        with tempfile.TemporaryDirectory() as directory:
            with mock.patch.object(movie, "Path", side_effect=lambda path: Path(directory) / path), \
                    mock.patch.object(movie, "adb", side_effect=adb), \
                    mock.patch.object(movie.time, "monotonic", side_effect=(0, 0, 46)), \
                    mock.patch.object(movie.time, "sleep"):
                with self.assertRaisesRegex(AssertionError, "timed out"):
                    movie.drive("movie-test", "/fixture-output")
        self.assertFalse(any(call[0] == "push" for call in calls))


if __name__ == "__main__":
    unittest.main()
