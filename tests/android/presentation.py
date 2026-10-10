"""Check actual Android framebuffer pixels before advancing a render fixture."""
from dataclasses import dataclass
from pathlib import Path
import re
import struct
import subprocess
import sys
import time
import zlib

PACKAGE = "com.yuri.kirikiri2"
MARKERS = ((255, 0, 255), (0, 255, 255), (255, 255, 0), (255, 128, 0))


@dataclass
class Screen:
    width: int
    height: int
    rgba: bytes

    def pixel(self, x, y):
        if not (0 <= x < self.width and 0 <= y < self.height):
            raise AssertionError(f"Sample outside framebuffer: {x},{y}")
        offset = (y * self.width + x) * 4
        return tuple(self.rgba[offset:offset + 3])


def decode_screencap(data):
    # AOSP screencap writes width, height, format, color space, then tightly
    # packed rows (older releases omit color space). RGBA_8888=1, RGBX_8888=2.
    if len(data) < 12:
        raise ValueError("Truncated screencap header")
    width, height, pixel_format = struct.unpack_from("<III", data)
    if not (0 < width <= 8192 and 0 < height <= 8192) or pixel_format not in (1, 2):
        raise ValueError(f"Unsupported framebuffer: {width}x{height} format={pixel_format}")
    header_size = len(data) - width * height * 4
    if header_size not in (12, 16):
        raise ValueError("Truncated or padded screencap payload")
    rgba = data[header_size:]
    if pixel_format == 2:
        rgba = bytearray(rgba)
        rgba[3::4] = b"\xff" * (width * height)
        rgba = bytes(rgba)
    return Screen(width, height, rgba)


def save_png(screen, path):
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
    rows = b"".join(b"\0" + screen.rgba[y * screen.width * 4:(y + 1) * screen.width * 4]
                    for y in range(screen.height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" +
                     chunk(b"IHDR", struct.pack(">IIBBBBB", screen.width, screen.height, 8, 6, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def scene(stage):
    if not 1 <= stage <= 9:
        raise ValueError("Invalid presentation stage")
    width, height, background = ((321, 241, (32, 48, 64)) if stage <= 4 else
                                 (353, 277, (24, 56, 40)) if stage <= 6 else
                                 (319, 223, (56, 32, 24)))
    rectangles = [(80, 80, width - 160, height - 160, (112, 64, 144))]
    for (left, top), color in zip(((24, 40), (width - 40, 40),
                                  (24, height - 56), (width - 40, height - 56)), MARKERS):
        rectangles.append((left, top, 16, 16, color))
    if stage in (2, 3, 4, 6):
        rectangles += [(64, 0, width - 128, 24, (224, 48, 48)),
                       (64, height - 24, width - 128, 24, (48, 96, 224))]
    if stage in (3, 4, 8, 9):
        rectangles.append((64, height // 2 - 8, width - 128, 16, (48, 208, 96)))
    if stage == 4:
        rectangles += [(64, 70, width - 128, 8, (224, 144, 32)),
                       (64, height - 78, width - 128, 8, (32, 128, 160))]
    return width, height, background, rectangles


def expected_pixel(stage, x, y):
    _, _, color, rectangles = scene(stage)
    for left, top, width, height, value in rectangles:
        if left <= x < left + width and top <= y < top + height:
            color = value
    return color


def marker_centers(screen):
    bounds = [[screen.width, screen.height, -1, -1, 0] for _ in MARKERS]
    data = screen.rgba
    for offset in range(0, len(data), 4):
        red, green, blue = data[offset:offset + 3]
        marker = (-1 if red < 230 and green < 230 else
                  0 if red > 230 and blue > 230 and green < 25 else
                  1 if green > 230 and blue > 230 and red < 25 else
                  2 if red > 230 and green > 230 and blue < 25 else
                  3 if red > 230 and 105 < green < 150 and blue < 25 else -1)
        if marker < 0:
            continue
        y, x = divmod(offset // 4, screen.width)
        box = bounds[marker]
        box[0] = min(box[0], x); box[1] = min(box[1], y)
        box[2] = max(box[2], x); box[3] = max(box[3], y); box[4] += 1
    centers = []
    for index, (left, top, right, bottom, count) in enumerate(bounds):
        if count < 9 or count < (right - left + 1) * (bottom - top + 1) // 2:
            raise AssertionError(f"Presentation marker {index} missing or obscured")
        centers.append(((left + right) / 2, (top + bottom) / 2))
    return centers


def validate(screen, stage):
    width, height, _, _ = scene(stage)
    p0, p1, p2, p3 = marker_centers(screen)
    if any(abs(p3[axis] - p1[axis] - p2[axis] + p0[axis]) > 3 for axis in (0, 1)):
        raise AssertionError("Presentation markers do not describe one rectangular image")
    vx = tuple((p1[axis] - p0[axis]) / (width - 64) for axis in (0, 1))
    vy = tuple((p2[axis] - p0[axis]) / (height - 96) for axis in (0, 1))
    if abs(vx[0] * vy[1] - vx[1] * vy[0]) < 0.04:
        raise AssertionError("Presentation markers are degenerate")
    # Sample changed and untouched rows, both sides of the dirty rectangles,
    # and right/bottom edges where pitch or resize mistakes appear.
    xs = (12, 52, 88, width // 2, width - 88, width - 52, width - 12)
    ys = (6, 32, 64, 74, 90, height // 2, height - 90, height - 74, height - 64, height - 32, height - 6)
    for y in ys:
        for x in xs:
            actual_at = tuple(round(p0[axis] + (x - 31.5) * vx[axis] + (y - 47.5) * vy[axis])
                              for axis in (0, 1))
            actual = screen.pixel(*actual_at)
            expected = expected_pixel(stage, x, y)
            if any(abs(actual[channel] - expected[channel]) > 20 for channel in range(3)):
                raise AssertionError(f"Stage {stage} source({x},{y}) display{actual_at}: expected {expected}, got {actual}")
    return len(xs) * len(ys)


def parse_stage(data):
    try:
        text = data.decode("utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig")
    except UnicodeDecodeError:
        return None
    match = re.fullmatch(r"([1-9]) ([0-9]+) ([0-9]+)\r?\n", text)
    return tuple(map(int, match.groups())) if match else None


def adb(*args, check=True):
    return subprocess.run(["adb", *args], check=check, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=25)


def drive(name, output):
    diagnostics = Path("test-results/android")
    diagnostics.mkdir(parents=True, exist_ok=True)
    ack = diagnostics / f"{name}-presentation-ack.txt"
    for stage in range(1, 10):
        deadline = time.monotonic() + 45
        last_screen, problem = None, "stage marker not ready"
        while time.monotonic() < deadline:
            marker = adb("shell", "-T", "cat", output + "/presentation-stage.txt", check=False)
            current = parse_stage(marker.stdout) if marker.returncode == 0 else None
            if current and current[0] > stage:
                raise AssertionError(f"Presentation advanced without acknowledgement: {current[0]} > {stage}")
            if current and current[0] == stage:
                if current[1:] != scene(stage)[:2]:
                    raise AssertionError(f"Unexpected source dimensions: {current}")
                last_screen = decode_screencap(adb("exec-out", "screencap").stdout)
                try:
                    samples = validate(last_screen, stage)
                except AssertionError as error:
                    problem = str(error)
                else:
                    save_png(last_screen, diagnostics / f"{name}-presentation-{stage}.png")
                    print(f"PASS presentation stage {stage}: {samples} actual framebuffer samples", flush=True)
                    ack.write_text(str(stage) + "\n", encoding="utf-8")
                    adb("push", str(ack), output + "/presentation-ack.txt")
                    break
            if adb("shell", "pidof", PACKAGE, check=False).returncode:
                raise AssertionError("Engine exited before presentation completed")
            time.sleep(0.2)
        else:
            if last_screen is not None:
                save_png(last_screen, diagnostics / f"{name}-presentation-{stage}-failed.png")
            raise AssertionError(f"Presentation stage {stage} timed out: {problem}")


if __name__ == "__main__":
    drive(sys.argv[1], sys.argv[2])
