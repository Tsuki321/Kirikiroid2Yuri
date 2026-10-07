"""Drive only the synthetic dialog fixture and verify every native wait."""
from pathlib import Path
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

PACKAGE = "com.yuri.kirikiri2"
BUTTONS = ("button1", "button3", "button2", "button1", "button1", "button1", "button3", "button1")
INITIAL_TEXT = {5: "first", 6: "", 7: "cancelled"}


def adb(*args, check=True):
    return subprocess.run(["adb", *args], check=check, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=25)


def stage_text(output):
    result = adb("exec-out", "cat", output + "/dialog-stage.txt", check=False)
    return result.stdout.decode("utf-8-sig").strip() if result.returncode == 0 else ""


def drive(name, output):
    diagnostics = Path("test-results/android")
    diagnostics.mkdir(parents=True, exist_ok=True)
    for stage, button in enumerate(BUTTONS, 1):
        caption = "CI_DIALOG_" + str(stage)
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            if adb("shell", "pidof", PACKAGE, check=False).returncode:
                raise RuntimeError("engine exited before " + caption)
            current = stage_text(output)
            if current and int(current) > stage:
                raise RuntimeError("native dialog returned before the response: " + caption)
            dumped = adb("shell", "uiautomator", "dump", "--compressed",
                         "/data/local/tmp/ci-dialog.xml", check=False)
            if dumped.returncode:
                time.sleep(0.3)
                continue
            xml = adb("exec-out", "cat", "/data/local/tmp/ci-dialog.xml").stdout
            (diagnostics / (name + "-dialog-" + str(stage) + ".xml")).write_bytes(xml)
            nodes = [node for node in ET.fromstring(xml).iter("node")
                     if node.get("package") == PACKAGE]
            if not any(node.get("text") == caption for node in nodes):
                time.sleep(0.3)
                continue
            # Leave a visible dialog unanswered, then prove the script still
            # waits at that request. This detects stale result reuse directly.
            time.sleep(0.35)
            if stage_text(output) != str(stage):
                raise RuntimeError("script advanced with an unanswered dialog: " + caption)
            if stage in INITIAL_TEXT:
                editors = [node for node in nodes if node.get("class") == "android.widget.EditText"]
                if len(editors) != 1 or editors[0].get("text") != INITIAL_TEXT[stage]:
                    raise RuntimeError("input dialog has stale initial text: " + caption)
            controls = [node for node in nodes
                        if node.get("resource-id") == "android:id/" + button
                        and node.get("enabled") == "true"]
            if len(controls) != 1:
                raise RuntimeError("expected dialog button is absent: " + caption)
            bounds = re.fullmatch(r"\[(\d+),(\d+)\]\[(\d+),(\d+)\]", controls[0].get("bounds", ""))
            if not bounds:
                raise RuntimeError("invalid dialog button bounds: " + caption)
            left, top, right, bottom = map(int, bounds.groups())
            if right <= left or bottom <= top:
                raise RuntimeError("dialog button has no visible area: " + caption)
            adb("shell", "input", "tap", str((left + right) // 2), str((top + bottom) // 2))
            print(caption + " was blocked until its explicit response", flush=True)
            break
        else:
            raise RuntimeError("timed out waiting for " + caption)


if __name__ == "__main__":
    drive(*sys.argv[1:3])
