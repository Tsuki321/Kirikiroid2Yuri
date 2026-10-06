# Kirikiroid2-Yuri roadmap (implementation notes)

## Phase 0 — Android beta stability

- Global preferences: `PreferenceConfig.h` persists on each change; `TVPWriteDataToFile` replaces local preferences using a temporary file and rename, with a document-provider fallback.
- Title path list: `FileSelectorForm.cpp` keeps `ListItem.csb` cell wrapper for correct hit targets.
- In-game menu: nested menus use `eEnterAniOverFromRight`.
- Message box: `setSwallowTouches(true)` on dialog buttons.

## Phase 1 — Plugins

| Plugin | Status |
|--------|--------|
| scriptsEx | Ported (`scriptsEx.cpp`, `bitap_fuzzy.hpp`) |
| TJS DataPack | Bounded `Scripts.loadDataPack` reader for stored/LZ4 metadata, cipher modes 1–6, both byte orders and external IVs. See `DATAPACK.md`. |
| System / win32dialog | Native yes/no confirmation and desktop message-box button/result contracts. |
| MenuItem controls | Window-control icon constants/properties; Android menus label icon-only controls. Custom bitmap rendering and horizontal menu-bar layout remain separate. |
| windowEx | Virtual-window minimize, maximize and restore; other Windows APIs remain outside the Android implementation. |
| layerExDraw / GdiPlus | Android raster backend for text, metrics, paths, transforms, basic brushes and images. Advanced modes report errors. See `VALIDATION.md`. |
| layerEx base | In-tree (`LayerExBase.cpp`, `layerExBase.hpp`) |
| layerExImage | Portable brightness/contrast, HSL effects, noise and Gaussian blur, with clipping and alpha preservation. |
| extrans | Portable mosaic, wave, ripple and rotateswap handlers with bounded options and renderer-independent pixel tests. Uses CPU effect rendering with RGBA texture snapshots. The original turn, rotatezoom and rotatevanish effects remain unimplemented. |
| textrender | Portable `TextRenderBase` layout, formatting, ruby, links and dialogue timing. Font and image callbacks use the framework's raster backend. |
| fstat / dirlist | Directory enumeration and file operations for local storage, document trees and readable archive metadata. Empty-directory removal preserves contents. |

## Phase 2 — CX / XP3

- Pipeline: `xp3filter.cpp` + per-game `xp3filter.tjs` beside archives.
- Native `cxdec_decode` remains Win32-only reference; add decoders per title as needed.

## Phase 3 — Storage

- Target SDK29 retains legacy access. Granted document trees have native open/seek/list/create/rename/delete support via `StorageAccess.java` and `AndroidStorage.cpp`.
- SAF: `KR2Activity.java` picker (`triggerStorageAccessFramework` / `onActivityResult` requestCode 3) persists the document-tree URI in Android `SharedPreferences["URI"]` and now mirrors it into the engine via `nativeSetSafTreeUri` → `GlobalConfigManager` (`<Item key="saf_tree_uri" value="..."/>` in `GlobalPreference.xml`). Symmetric `nativeGetSafTreeUri` lets the engine read the URI back. In-engine preference item `preference_android_fetch_sdcard_permission` (`tTVPPreferenceInfoFetchSDCardPermission` in `PreferenceConfig.h`) re-triggers the SAF picker; locale strings exist in en/ja/zh_cn/zh_tw.

## Phase 4 — Config / CLI

- `GlobalPreference.xml` via `GlobalConfigManager`.
- Android `TVPCheckStartupArg` (`AndroidUtils.cpp`): runs the Breakpad dump check, then consumes launch args stashed by `nativeSetStartupArgs`. `KR2Activity.onCreate` reads intent extras (`startupPath` String → `.xp3`/bootable folder; `args` String[] of `-key=value`/`-flag`) and forwards them to native globals (`g_AndroidStartupPath` / `g_AndroidStartupArgs` in `krkr2_android.cpp`). `TVPCheckStartupArg` parses options into `TVPProgramArguments` via `TVPSetCommandLine` (exposed to TJS2 as `System.commandLineArgument`) and dispatches `startupFrom(path)` when the path is a bootable archive (`TVPCheckArchive == 1`) or directory containing `startup.tjs`; otherwise falls back to the file selector. Mirrors Win32 `Platform.cpp:91-134`.

## Phase 5 — Desktop

- **Out of scope.** This fork is Android-only (arm64-v8a, SDK 22+). Root `CMakeLists.txt:18-23` stubs Windows/Linux with `not support yet`. No plans to build desktop targets.

## Phase 6 — SDL2 (optional, lighter backend)

- **Optional.** Not required for the engine to work on Android; cocos2d-x remains the active backend. This phase is a future optimization to drop the heavy cocos2d-x dependency for a leaner binary.
- Replace cocos2d-x with SDL2 in: `MainScene.cpp`, `AppDelegate.cpp`, `YUVSprite.cpp`, `environ/ui/*`, Gradle `:cocos2dx` module.
- Note: `src/core/visual/RenderManager.cpp` is the engine's software renderer (used by some paths), not cocos rendering. It may need adaptation but is separate from the cocos migration.

## Phase 7 - Regression tests

GitHub Actions builds both Android APK variants and host tests. Host tests run with GCC and Clang under address/undefined-behavior sanitizers. Android instrumentation checks real persisted document grants; separate native TJS fixtures exercise local files and document trees after process restart.

Debug and optimized/minified release APKs are checked on API30 and API35 (16 KB) emulators. ELF segments and APK packing are checked for 16 KB alignment. Release publication depends on the host tests and entire emulator matrix.

See [VALIDATION.md](VALIDATION.md) for coverage, artifacts, signing, dependency reproducibility and known gaps.
