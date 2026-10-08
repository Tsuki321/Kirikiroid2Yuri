"""Verify real engine shutdown, return to the launcher, and a second launch."""
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET

PACKAGE = "com.yuri.kirikiri2"
OUTPUT = Path("test-results/android")


def adb(*args, check=True):
    return subprocess.run(["adb", *args], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=check, timeout=25)


def tree():
    result = adb("shell", "uiautomator", "dump", "/sdcard/krkr-library-ui.xml", check=False)
    if result.returncode:
        return None
    data = adb("exec-out", "cat", "/sdcard/krkr-library-ui.xml", check=False)
    try:
        return ET.fromstring(data.stdout)
    except ET.ParseError:
        return None


def locate(name, seconds=40):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        root = tree()
        if root is not None:
            for node in root.iter("node"):
                if name.casefold() in (node.get("text", "").casefold(), node.get("content-desc", "").casefold()):
                    bounds = re.fullmatch(r"\[(\d+),(\d+)\]\[(\d+),(\d+)\]", node.get("bounds", ""))
                    if bounds:
                        left, top, right, bottom = map(int, bounds.groups())
                        if right > left and bottom > top:
                            return (left + right) // 2, (top + bottom) // 2
        time.sleep(0.2)
    raise AssertionError("Visible UI element: " + name)


def click(name):
    x, y = locate(name)
    adb("shell", "input", "tap", str(x), str(y))


def screenshot(name):
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / (name + ".png")).write_bytes(adb("exec-out", "screencap", "-p").stdout)


def scroll_to(name):
    for _ in range(10):
        try:
            return locate(name, seconds=1)
        except AssertionError:
            root = tree()
            scroll = next((node for node in root.iter("node") if node.get("scrollable") == "true"), None) if root is not None else None
            if scroll is None:
                raise AssertionError("No scrollable view while looking for " + name)
            left, top, right, bottom = map(int, re.findall(r"\d+", scroll.get("bounds", "")))
            middle = (left + right) // 2
            adb("shell", "input", "swipe", str(middle), str(bottom - 30), str(middle), str(top + 30), "300")
    raise AssertionError("Could not reach " + name)


def open_library():
    adb("shell", "am", "start", "-W", "-n", PACKAGE + "/.LibraryActivity")
    locate("Your library")


def return_to_library():
    click("Open game controls")
    # Scroll the dialog without relying on a device-specific pixel height.
    for _ in range(8):
        root = tree()
        if root is not None and any(n.get("text") == "Return to library" for n in root.iter("node")):
            try:
                click("Return to library")
                break
            except AssertionError:
                pass
        scroll = next((n for n in root.iter("node") if n.get("scrollable") == "true"), None) if root is not None else None
        if scroll is None:
            raise AssertionError("Controls panel must have a scrollable body")
        left, top, right, bottom = map(int, re.findall(r"\d+", scroll.get("bounds", "")))
        mid = (left + right) // 2
        adb("shell", "input", "swipe", str(mid), str(bottom - 30), str(mid), str(top + 30), "300")
    else:
        raise AssertionError("Return to library action was not reachable")
    locate("Return to your library?")
    screenshot("library-return-confirmation")
    click("Return to library")
    locate("Your library")
    deadline = time.monotonic() + 10
    while adb("shell", "pidof", PACKAGE, check=False).returncode == 0 and time.monotonic() < deadline:
        time.sleep(0.2)
    assert adb("shell", "pidof", PACKAGE, check=False).returncode != 0, "Engine process must stop on return"
    assert adb("shell", "pidof", PACKAGE + ":library", check=False).returncode == 0, "Library must survive engine shutdown"


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / "before-library-return-logcat.txt").write_bytes(adb("logcat", "-d").stdout)
    adb("logcat", "-c")
    open_library()
    screenshot("library-last-played")
    scroll_to("+  Add game folder"); click("+  Add game folder")
    locate("Use this folder"); click("Show roots"); click("Kirikiri test games")
    scroll_to("Second controls playground"); click("Second controls playground")
    click("Use this folder"); click("Allow")
    locate("Your library")
    click("Play again")
    locate("Open game controls")
    original_pid = adb("shell", "pidof", PACKAGE).stdout.strip()
    adb("shell", "input", "keyevent", "KEYCODE_HOME")
    open_library()
    scroll_to("Play Second controls playground"); click("Play Second controls playground")
    locate("Switch to another game?"); screenshot("library-switch-confirmation")
    click("Keep playing"); locate("Open game controls")
    assert original_pid == adb("shell", "pidof", PACKAGE).stdout.strip(), "Canceling a switch must preserve the running game"
    adb("shell", "input", "keyevent", "KEYCODE_HOME")
    open_library()
    scroll_to("Play Second controls playground"); click("Play Second controls playground")
    click("Switch game"); locate("Open game controls")
    assert original_pid != adb("shell", "pidof", PACKAGE).stdout.strip(), "Switching games must use a fresh engine process"
    locate("Hold left mouse button")
    return_to_library()
    for attempt in range(2):
        click("Play again")
        locate("Open game controls")
        locate("Hold left mouse button")  # Touchpad choice survives a fresh engine process.
        screenshot("library-relaunch-" + str(attempt + 1))
        return_to_library()
    screenshot("library-returned")
    logs = adb("logcat", "-d").stdout.decode("utf-8", errors="replace")
    (OUTPUT / "library-return-logcat.txt").write_text(logs, encoding="utf-8")
    assert "Process: com.yuri.kirikiri2, PID:" not in logs, "Returning or switching games must not crash the app"
    assert ">>> com.yuri.kirikiri2 <<<" not in logs, "Returning or switching games must not crash the native engine"
    print("PASS: cancel/switch games, two library relaunches, preserved controls and clean engine shutdown", flush=True)


if __name__ == "__main__":
    main()
