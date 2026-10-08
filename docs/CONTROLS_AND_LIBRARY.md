# Playing a game

Open **Add game folder**, choose the extracted game directory in Android’s Files
picker, and allow access. Keep the game’s archives, patches and support files
together. The app remembers the folder; tap **Play** on later visits.

The launcher recognizes `startup.tjs`, `data.xp3`, other XP3 archives and game
executables. If it cannot choose a startup archive, it asks you to select one.
Selecting a parent folder checks its immediate game subfolders too. ZIP and RAR
downloads must be extracted first. Compatibility patches are still needed for
some games; the library does not download games or change their contents.

Search, favorites and the sort menu help with larger libraries. Each game’s
options let you rename its shortcut, choose another launch file, reconnect a
moved folder, or remove the shortcut. Removing a shortcut preserves game files
and saves. **Play again** relaunches the most recently played game; load saves
from the game’s own menu.

The classic file browser remains available at the bottom of the library.

# Game controls

| Input | Result |
| --- | --- |
| Tap | Left click / advance dialogue |
| Move a finger after touching | Left-button drag |
| Hold a finger still | Right click |
| Two-finger tap | Right click |
| Three-finger tap | Middle click |
| Two-finger vertical slide | Mouse wheel, commonly used for the backlog |
| Physical mouse | Pointer movement, left/right/middle buttons and wheel |
| Physical keyboard | Letters, WASD, arrows, modifiers, numbers, navigation, F1–F12 and numpad |
| Android Back | Open Controls |
| Escape | Send Esc to the game |

**Controls** provides on-screen WASD or arrows, optional Enter/Space/Esc/Ctrl
buttons, and a larger keyboard. Buttons can be held and combined. **Type text**
opens the phone’s keyboard and sends Unicode text to the game’s focused field.

In **Touchpad mode**, sliding moves a visible pointer relative to your finger.
Tap to click, or hold the **Left** mouse button while sliding to drag. Pointer
speed is adjustable. Controls also offer button size, opacity, side placement,
screen-awake behavior and optional physical WASD-to-arrow mapping. Settings are
remembered between games. **Advanced engine options** opens the original engine
menu.

Place `Kirikiroid2Preference.xml` beside the game's `data.xp3` or `startup.tjs`.
The engine reloads it when the game starts, including when launching an archive.
A startup notification confirms the load; **Controls > Game preferences** shows
the file path, load status and effective startup options. Missing, unreadable or
invalid XML is reported there. Startup options take priority in this order:
explicit launch options, game preferences, then global preferences. Restart the
game after editing startup options. Saved touch and keyboard controls are
separate from these engine options.

Save in the game before choosing **Return to library**. The confirmation closes
the engine; unsaved game progress is not restored automatically. The library
uses a separate Android process so it survives the engine’s shutdown.

# Verification

Builds run in GitHub Actions. The Android matrix targets Android 16 (API 36),
with 4 KB and 16 KB page images and debug/release APKs, including the existing
engine regression fixtures. Document-provider preference files are served
through non-seekable pipes to exercise the same loading path as provider-backed
game folders.

Archive-launch fixtures check that `System.exePath` still identifies the game
folder, so preferences, plugins and sibling patch archives remain accessible
after the engine enters the startup archive.

- `GameInputTest` checks chords, repeat events, shared holds, focus loss, pointer
  IDs, cancellation, touchpad movement, mouse buttons, wheel coordinates and
  long press.
- `GameLibraryTest` checks startup discovery, ambiguous archives, stable document
  grants, search, persistence and preserving game files when removing a shortcut.
- `LibraryUiTest` adds a folder through the actual system picker, then searches,
  renames, favorites and removes it in the rendered launcher.
- `NativeControlsTest` injects keyboard, touch and mouse input into a running
  Kirikiri window. Its TJS event log verifies held keys, game coordinates,
  cancellation, Unicode input and release when the control panel opens.
  It also opens **Game preferences** and verifies the applied XML path and
  compatibility options in the rendered dialog.

The emulator artifacts contain `ui-evidence/*.png`, instrumentation results,
`input-events.raw` and the existing engine logs. The native input test runs in a
separate instrumentation session because engine shutdown intentionally ends the
Android process.

`library_return.py` verifies canceling and confirming a game switch, closing the
engine without closing the library, and relaunching with the saved controls.
