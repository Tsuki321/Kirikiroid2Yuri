# Kirikiroid2-Yuri roadmap (implementation notes)

## Phase 0 — Android beta stability

- Global preferences: `PreferenceConfig.h` persists on each change; `TVPWriteDataToFile` overwrites via `fopen(..., "wb")`.
- Title path list: `FileSelectorForm.cpp` keeps `ListItem.csb` cell wrapper for correct hit targets.
- In-game menu: nested menus use `eEnterAniOverFromRight`.
- Message box: `setSwallowTouches(true)` on dialog buttons.

## Phase 1 — Plugins

| Plugin | Status |
|--------|--------|
| scriptsEx | Ported (`scriptsEx.cpp`, `bitap_fuzzy.hpp`) |
| windowEx | Android stub (`windowEx_stub.cpp`); full port needs Win32 |
| layerExDraw / GdiPlus | Stub class + clear error on Android (`layerExDraw_stub.cpp`) |
| layerEx base | In-tree (`LayerExBase.cpp`, `layerExBase.hpp`) |

## Phase 2 — CX / XP3

- Pipeline: `xp3filter.cpp` + per-game `xp3filter.tjs` beside archives.
- Native `cxdec_decode` remains Win32-only reference; add decoders per title as needed.

## Phase 3 — Storage

- Scoped bypass: `MediaStoreHack.java`.
- SAF: `KR2Activity.java` picker (`triggerStorageAccessFramework` / `onActivityResult` requestCode 3) persists the document-tree URI in Android `SharedPreferences["URI"]` and now mirrors it into the engine via `nativeSetSafTreeUri` → `GlobalConfigManager` (`<Item key="saf_tree_uri" value="..."/>` in `GlobalPreference.xml`). Symmetric `nativeGetSafTreeUri` lets the engine read the URI back. In-engine preference item `preference_android_fetch_sdcard_permission` (`tTVPPreferenceInfoFetchSDCardPermission` in `PreferenceConfig.h`) re-triggers the SAF picker; locale strings exist in en/ja/zh_cn/zh_tw.

## Phase 4 — Config / CLI

- `GlobalPreference.xml` via `GlobalConfigManager`.
- Android `TVPCheckStartupArg` (`AndroidUtils.cpp`): runs the Breakpad dump check, then consumes launch args stashed by `nativeSetStartupArgs`. `KR2Activity.onCreate` reads intent extras (`startupPath` String → `.xp3`/bootable folder; `args` String[] of `-key=value`/`-flag`) and forwards them to native globals (`g_AndroidStartupPath` / `g_AndroidStartupArgs` in `krkr2_android.cpp`). `TVPCheckStartupArg` parses options into `TVPProgramArguments` via `TVPSetCommandLine` (exposed to TJS2 as `System.commandLineArgument`) and dispatches `startupFrom(path)` when the path is a bootable archive (`TVPCheckArchive == 1`) or directory containing `startup.tjs`; otherwise falls back to the file selector. Mirrors Win32 `Platform.cpp:91-134`.

## Phase 5 — Desktop

- Root `CMakeLists.txt`: Windows/Linux targets not built yet.

## Phase 6 — SDL2

- Replace cocos last: `MainScene.cpp`, `AppDelegate.cpp`, `YUVSprite.cpp`, `environ/ui/*`, Gradle `:cocos2dx`.
- Note: `src/core/visual/RenderManager.cpp` is the engine's software renderer (used by some paths), not cocos rendering. It may need adaptation but is separate from the cocos migration.

Build validation: GitHub Actions `.github/workflows/build_android.yml` on push of `v*` tags or **workflow_dispatch** (manual). No local compile required if CI deps tarballs are used (same as CI job).