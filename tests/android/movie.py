"""Verify decoded overlay frames in Android's framebuffer before advancing CI."""
from pathlib import Path
import sys
import time

from presentation import adb, decode_screencap, marker_centers, parse_stage, save_png


def validate(screen, stage):
    if stage not in (1, 2, 3):
        raise ValueError("Invalid movie stage")
    p0, p1, p2, p3 = marker_centers(screen)
    if any(abs(p3[axis] - p1[axis] - p2[axis] + p0[axis]) > 3 for axis in (0, 1)):
        raise AssertionError("Movie markers do not describe one rectangular image")
    vx = tuple((p1[axis] - p0[axis]) / 256 for axis in (0, 1))
    vy = tuple((p2[axis] - p0[axis]) / 144 for axis in (0, 1))
    if abs(vx[0] * vy[1] - vx[1] * vy[0]) < 0.04:
        raise AssertionError("Movie markers are degenerate")
    color = ((255, 0, 0), (0, 255, 0), (0, 0, 255))[stage - 1]
    samples = [(x, y, color) for x in (86, 120, 160, 200, 234) for y in (86, 120, 154)]
    samples += [(x, y, (16, 16, 16)) for x, y in ((72, 120), (248, 120), (160, 72), (160, 168))]
    for x, y, expected in samples:
        actual_at = tuple(round(p0[axis] + (x - 31.5) * vx[axis] + (y - 47.5) * vy[axis])
                          for axis in (0, 1))
        actual = screen.pixel(*actual_at)
        if any(abs(actual[channel] - expected[channel]) > 25 for channel in range(3)):
            raise AssertionError(f"Movie stage {stage} source({x},{y}) display{actual_at}: expected {expected}, got {actual}")
    return len(samples)


def drive(name, output):
    diagnostics = Path("test-results/android")
    diagnostics.mkdir(parents=True, exist_ok=True)
    ack = diagnostics / f"{name}-movie-ack.txt"
    for stage in range(1, 4):
        deadline = time.monotonic() + 45
        last_screen, problem = None, "movie stage not ready"
        while time.monotonic() < deadline:
            marker = adb("shell", "-T", "cat", output + "/movie-stage.txt", check=False)
            current = parse_stage(marker.stdout) if marker.returncode == 0 else None
            if current and current[0] > stage:
                raise AssertionError(f"Movie advanced without acknowledgement: {current[0]} > {stage}")
            if current and current[0] == stage:
                try:
                    if current[1:] != (320, 240):
                        raise AssertionError(f"Unexpected movie dimensions: {current[1:]}")
                    last_screen = decode_screencap(adb("exec-out", "screencap").stdout)
                    samples = validate(last_screen, stage)
                    save_png(last_screen, diagnostics / f"{name}-overlay-{stage}.png")
                    print(f"PASS {name} overlay {stage}: {samples} framebuffer samples", flush=True)
                    ack.write_text(str(stage) + "\n", encoding="utf-8")
                    adb("push", str(ack), output + "/movie-ack.txt")
                    break
                except (AssertionError, ValueError) as error:
                    problem = str(error)
            result = adb("shell", "-T", "cat", output + "/result.txt", check=False)
            if result.returncode == 0:
                raise AssertionError(f"Movie finished before framebuffer check: {result.stdout!r}")
            time.sleep(0.25)
        else:
            if last_screen is not None:
                save_png(last_screen, diagnostics / f"{name}-overlay-{stage}-failure.png")
            raise AssertionError(f"{name} overlay {stage} timed out: {problem}")


if __name__ == "__main__":
    drive(*sys.argv[1:])
