# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Kirikiroid2-Yuri is a fork of Kirikiroid2 — a cross-platform port of the **Kirikiri2/KirikiriZ** Japanese visual-novel engine. The engine runs games written in the **TJS2** scripting language with the **KAG** ADV system. This fork targets newer Android devices (SDK 22+) and more file formats. Android (arm64-v8a) is the only platform currently buildable; Windows/Linux are stubbed out in CMake and not yet supported.

**Roadmap and implementation status:** see [`docs/ROADMAP.md`](docs/ROADMAP.md) (phases 0–6, what is done vs TODO). User-facing checklist remains in `readme.md`. Long-term goal: replace cocos2d-x rendering with SDL2 (phase 6 in `docs/ROADMAP.md`).

The original engine code is largely Win32-derived; much of `src/core` carries `win32/` subfolders that are selectively excluded per-platform in CMake.

## Build

Two-stage build: third-party dependencies are cross-compiled first, then the APK is assembled by Gradle (which drives the project's CMake via NDK externalNativeBuild).

```sh
# 1. Dependencies (from script/). Fetches sources and cross-compiles ~25 libs.
cd script
bash cross_androida64.sh                       # full: fetch + build ports, then build .so
SKIP_PORTS=yes bash cross_androida64.sh        # skip ports if already built
# Outputs into thirdparty/build/arch_androida64/ (lib + include)

# 2. APK
cd project/android
./gradlew assembleDebug                          # -> ../../../build_android/outputs/apk/debug/krkr2yuri_v<ver>.apk (configured by build.gradle)
./gradlew assembleRelease                        # requires signing env vars / sign.properties
```

Prerequisites: Android SDK with `ANDROID_HOME` set, **NDK 25.2.9519653**, and the host tools `wget 7z git make cmake` (plus python2 for cocos2d-x v3, msys2 on Windows for the ffmpeg port).

**Shortcut:** prebuilt dependency tarballs (`thirdparty_build.tar.gz`, `thirdparty_port.tar.gz`) are published under the repo's `deps` release and can be extracted into `thirdparty/build` and `thirdparty/port` to skip stage 1 entirely. The CI (`.github/workflows/build_android.yml`) does exactly this — it caches those tarballs and only runs `./gradlew assembleDebug`.

**CI triggers:** push of `v*` tags, or manual **workflow_dispatch** (`gh workflow run build_android.yml --ref <branch>`). Prefer GitHub Actions for builds when local compilation is not desired.

There is no test suite. Validation is manual: build the APK and run a game.

## Dependency layout (not in git)

These paths are gitignored and must exist before a build succeeds:

- `thirdparty/port/` — extracted third-party **source** (notably `thirdparty/port/cocos2d-x`, referenced throughout CMake as `COCOS2DX_PATH`).
- `thirdparty/build/arch_androida64/` — **compiled** static libs + headers (`PORTBUILD_PATH`). All audio/video/image/archive libs link from here.
- `assets/` — game engine assets, extracted from a prebuilt reference APK (e.g. `Kirikiroid2_yuri_1.3.9.apk`). Gradle pulls these from root `assets/` via `assets.srcDirs = ["../../../assets"]` (relative to `project/android/app/`).

## Architecture

The build produces one native shared library, `libkrkr2yuri.so`, composed of three CMake targets plus a JNI shim, loaded by a thin Java/Cocos Android app.

### CMake target graph

- **Root `CMakeLists.txt`** — builds the final `krkr2yuri` shared lib from `project/android/app/cpp/krkr2_android.cpp`. Links `krkr2core` and `krkr2plugin`. Note the `--whole-archive` wrapper around `cpp_android_spec` and the plugins: it forces JNI symbols and self-registering plugins to survive dead-code stripping — **do not remove it** or JNI entry points / plugins silently vanish.
- **`src/core/CMakeLists.txt` → `krkr2core`** (static) — the engine. Globs sources across `tjs2/ visual/ sound/ movie/ base/ environ/ msg/ utils/ extension/`. Uses an explicit `REMOVE_ITEM` blocklist to drop Win32-only files (GDI font rasterizer, SSE resamplers, JXR/BPG loaders, etc.). Appends `environ/android/`, `sound/ARM/`, `visual/ARM/` only when targeting Android. Links every third-party lib.
- **`src/plugins/CMakeLists.txt` → `krkr2plugin`** (static) — TJS2-callable native plugins, bound to the engine via `src/plugins/ncbind/`. All `src/plugins/*.cpp` are globbed in; new plugins only need a new `.cpp` with `NCB_MODULE_NAME` and ncbind registration.

**Plugins (recent):** `scriptsEx` ported (`scriptsEx.cpp`, `bitap_fuzzy.hpp`, MD5 via `src/core/utils/md5`). Android stubs: `windowEx_stub.cpp`, `layerExDraw_stub.cpp` (registers `GdiPlus` TJS class; real GDI+ only viable on future Win32 build). Existing: `xp3filter`, `layerExMovie`, `perspective`, etc. See plugin table in [`docs/ROADMAP.md`](docs/ROADMAP.md).

When adding/removing engine source files, remember the CMake globs require a re-CMake to pick them up, and check the `REMOVE_ITEM` blocklist if a file fails to compile on Android.

### Core engine (`src/core/`)

- `tjs2/` — the TJS2 language VM (lexer, bytecode interpreter, intrinsic objects). The scripting heart.
- `visual/` — rendering, layers, image codecs, real-time texture compression (`ARM/`, `gl/`, `ogl/`). Win32 rasterizer in `win32/` is excluded on Android.
- `sound/` — audio (ogg/opus/vorbis via OpenAL/oboe); `ARM/` holds NEON paths.
- `movie/` — video playback (ffmpeg-backed, originally from kodi).
- `base/` — file system, XP3 archive handling, storage abstraction.
- `msg/` — engine messages / localized strings.
- `environ/` — **platform integration layer**, the most fork-relevant area:
  - `environ/cocos2d/` — the cocos2d-x application: `AppDelegate`, `MainScene` (`TVPMainScene`), `CustomFileUtils`, `YUVSprite`. This is the bridge between the engine and the cocos rendering/event loop.
  - `environ/ui/` — all in-engine UI forms (file selector, preferences, in-game menu, message box) built as cocos `BaseForm` subclasses.
  - `environ/android/` — `AndroidUtils`, Android-specific storage/JNI helpers.
  - `environ/ConfigManager/` — `GlobalConfigManager` for persisted preferences (`GlobalPreference.xml` under `TVPGetInternalPreferencePath()` → `writablePath/.preference/`). Saves on preference UI exit and on each change via `PreferenceConfig.h` → `SaveToFile()`. Android writes through `TVPWriteDataToFile` in `environ/android/AndroidUtils.cpp` (`fopen` then JNI fallback).
  - `environ/ui/` beta touch fixes (see `docs/ROADMAP.md` phase 0): title path list cells in `FileSelectorForm.cpp`, `MessageBox.cpp` button swallow touches, `InGameMenuForm.cpp` nested menu push animation.

### Android app & JNI bridge (`project/android/`)

- Java side: `org.tvp.kirikiri2.KR2Activity` is the base activity (input, IME, message boxes, storage); `com.yuri.kirikiri2.MainActivity` extends it and is the launcher entry. `MediaStoreHack` implements the scoped-storage bypass.
- Native bridge: `project/android/app/cpp/krkr2_android.cpp` holds all `JNIEXPORT Java_org_tvp_kirikiri2_KR2Activity_*` functions. The pattern throughout is to marshal Java events (touch, key, IME, mouse, low-memory) and dispatch them onto the cocos thread via `Android_PushEvents(...)` or `performFunctionInCocosThread(...)` — **never call cocos/engine objects directly from a JNI thread**; always hop to the cocos thread. `cocos_android_app_init` constructs the `TVPAppDelegate`.
- Crash reporting via Google Breakpad is wired through `initDump` (minidump descriptor + exception handler), gated to skip during intentional shutdown (`TVPSystemUninitCalled`).
- `project/android/settings.gradle` includes two modules: `:cocos2dx` (sourced from `thirdparty/port/cocos2d-x/.../libcocos2dx`) and `:krkr2yuri` (`app/`). Build knobs live in `gradle.properties` (`PROP_APP_ABI`, `PROP_*_SDK_VERSION`, `PROP_BUILD_TYPE=cmake`).

### UI assets (`project/ui/`)

CocosStudio project (`.cocos-project.json`, `cocosstudio/`) holding image assets and the localization XMLs in `project/ui/Resources/res/locale/` (en/ja/zh_cn/zh_tw).

## Conventions

- Engine classes and globals use the upstream `TVP*` prefix (`TVPMainScene`, `TVPAppDelegate`, `TVPSystemUninitCalled`).
- The codebase mixes tabs and the original Win32 source style; match the style of the file you are editing rather than imposing a global standard.
- Platform-specific code is segregated into `win32/` / `android/` / `ARM/` subfolders and selected in CMake, not via `#ifdef` sprinkled everywhere — follow that pattern when adding platform code.

## Documentation

| Doc | Purpose |
|-----|---------|
| [`docs/ROADMAP.md`](docs/ROADMAP.md) | Phased roadmap, implementation notes, file pointers, CI |
| `readme.md` | Usage, build steps, compatibility table, issue list |
| `CLAUDE.md` | Agent-oriented architecture and conventions (this file) |

When extending the fork, update `docs/ROADMAP.md` for phase/status changes and keep `readme.md` roadmap checkboxes in sync where user-visible.
