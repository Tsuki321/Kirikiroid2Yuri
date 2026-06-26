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
- SAF: partial in `KR2Activity.java`; persist document-tree URI in `GlobalPreference.xml` (TODO).

## Phase 4 — Config / CLI

- `GlobalPreference.xml` via `GlobalConfigManager`.
- Android `TVPCheckStartupArg`: dump check only; intent extras → `_argv` (TODO).

## Phase 5 — Desktop

- Root `CMakeLists.txt`: Windows/Linux targets not built yet.

## Phase 6 — SDL2

- Replace cocos last: `MainScene.cpp`, `RenderManager.cpp`, `environ/ui/*`, Gradle `:cocos2dx`.

Build validation: GitHub Actions `build_android.yml` on `v*` tags only.