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
        root = ET.fromstring(data.stdout)
    except ET.ParseError:
        return None
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / "library-return-ui.xml").write_bytes(data.stdout)
    return root


def visible_nodes(root, clip=None):
    if root is None:
        return
    bounds = re.fullmatch(r"\[(-?\d+),(-?\d+)\]\[(-?\d+),(-?\d+)\]", root.get("bounds", ""))
    if bounds:
        left, top, right, bottom = map(int, bounds.groups())
        left, top = max(left, 0), max(top, 0)
        if clip is not None:
            left, top = max(left, clip[0]), max(top, clip[1])
            right, bottom = min(right, clip[2]), min(bottom, clip[3])
        if right <= left or bottom <= top:
            return
        clip = left, top, right, bottom
        yield root, clip
    for child in root:
        yield from visible_nodes(child, clip)


def find_bounds(root, name, within_class=None, clip=None):
    for node, bounds in visible_nodes(root, clip):
        if within_class is not None:
            if node.get("class") == within_class:
                found = find_bounds(node, name, clip=bounds)
                if found is not None:
                    return found
        elif name.casefold() in (node.get("text", "").casefold(), node.get("content-desc", "").casefold()):
            return bounds
    return None


def locate(name, seconds=40, within_class=None):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        bounds = find_bounds(tree(), name, within_class)
        if bounds is not None:
            left, top, right, bottom = bounds
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
    for forward in (True, False):
        previous = None
        for _ in range(12):
            root = tree()
            bounds = find_bounds(root, name)
            if bounds is not None:
                left, top, right, bottom = bounds
                return (left + right) // 2, (top + bottom) // 2
            scroll = next(((node, bounds) for node, bounds in visible_nodes(root)
                           if node.get("scrollable") == "true"), None)
            if scroll is None:
                time.sleep(0.2)
                continue
            scroll_node, scroll_bounds = scroll
            signature = tuple((node.get("text"), node.get("content-desc"), bounds)
                              for node, bounds in visible_nodes(scroll_node, scroll_bounds))
            if signature == previous:
                break
            previous = signature
            left, top, right, bottom = scroll_bounds
            middle = (left + right) // 2
            # Stay inside the content: a swipe near the bottom screen edge
            # invokes Android's Home gesture instead of scrolling DocumentsUI.
            start, end = top + (bottom - top) * 3 // 4, top + (bottom - top) // 4
            if not forward:
                start, end = end, start
            adb("shell", "input", "swipe", str(middle), str(start), str(middle), str(end), "300")
    raise AssertionError("Could not reach " + name)


def select_picker_root(name):
    click("Show roots")
    # The same label can also occur in the breadcrumb behind the drawer.
    x, y = locate(name, within_class="android.widget.ListView")
    adb("shell", "input", "tap", str(x), str(y))
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        root = tree()
        if (find_bounds(root, name, "android.widget.ListView") is None
                and find_bounds(root, "Use this folder") is not None):
            return
        time.sleep(0.2)
    raise AssertionError("Selected folder-picker root: " + name)


def add_game(name):
    scroll_to("+  Add game folder"); click("+  Add game folder")
    locate("Use this folder"); select_picker_root("Kirikiri test games")
    scroll_to(name); click(name)
    click("Use this folder")
    deadline = time.monotonic() + 40
    while time.monotonic() < deadline:
        root = tree()
        if find_bounds(root, "Allow") is not None:
            click("Allow")
        elif find_bounds(root, "Choose a game folder") is not None:
            scroll_to(name); click(name)
        elif (find_bounds(root, "Your library") is not None
              and find_bounds(root, "Looking for games…") is None):
            scroll_to("Options for " + name)
            return
        time.sleep(0.2)
    raise AssertionError("Added library game: " + name)


def open_library():
    adb("shell", "am", "start", "-W", "-n", PACKAGE + "/.LibraryActivity")
    locate("Your library")


def return_to_library():
    click("Open game controls")
    locate("Game controls")
    scroll_to("Return to library"); click("Return to library")
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
    add_game("Second controls playground")
    scroll_to("Play again"); click("Play again")
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
        scroll_to("Play again"); click("Play again")
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
