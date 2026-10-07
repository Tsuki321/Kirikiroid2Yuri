# Runtime fixes and validation

Hxv4 companion preparation and reader coverage are described in [HXV4.md](HXV4.md).
PSB scene data and PIMG resource support are described in [PSB.md](PSB.md).
Game files, recovered keys, and companions are excluded from CI; its Hxv4 inputs
are synthetic.

The runtime fixes cover storage failures, archive extraction, bytecode loading,
plugin diagnostics, drawing, text layout, and movie controls. Builds and execution checks for
these changes run in GitHub Actions.

## Test coverage

| Check | Coverage |
|---|---|
| GCC and Clang host tests | MD5, fuzzy matching, file append/update/truncate, atomic replacement, extraction path boundaries, symlink/hardlink rejection, worker startup/join, bytecode structure and operands, XP3 index bounds and segment validation, alpha-blending endpoints |
| Address/undefined-behavior sanitizers | Host tests, including 20,000 deterministic bytecode mutations and truncated input prefixes; sanitizer findings fail the test |
| Optimized TJS runtime | Production interpreter compiled with `-O2`: empty-string conversion and serialization, closures across array growth, inheritance, exception-stack recovery, garbage collection with nested active frames and reentry, aligned hash storage, bounded string formatting, and compilation/bytecode validation of every engine fixture |
| Offline package inspector | Bounded XP3 metadata inspection, protected/obscured entry reporting, and checks that archive member payloads are never read |
| Android instrumentation | Existing-directory creation, write failures, local replacement, real persisted document grants, Unicode/case handling, read/write/list/rename/delete, subtree and startup-permission boundaries |
| Native TJS fixtures | Text/binary/compressed dictionary and empty-string serialization, literal-only loading, bytecode compilation/loading, malformed bytecode, missing/repeated plugins, actual text/shape/image pixels, alpha endpoints, transforms, window controls, valid and malformed XP3 archives through local and document-tree storage |
| Media fixtures | Generated PCM audio and MPEG-4 video, metadata, playback status/rate, pause/resume and stop frames |
| Plugin regressions | Seven extrans and twelve extNagano pixel algorithms, option bounds, rule images, morph meshes, locale naming and DSP filter responses under host sanitizers; actual TJS registration, transition completion and filtered PCM playback on Android |
| Android lifecycle | Backgrounding and returning to the existing activity |
| APK verification | 16 KB ELF load/RELRO alignment and uncompressed library packing |

The emulator matrix uses API 30 and API 35 with Google's 16 KB system image.
Both debug and optimized/minified release builds are exercised. The images run
on x86-64 hosts and execute the ARM64 APK through Android's native bridge; this
does not replace testing on physical ARM64 devices and their GPU drivers.

Each configuration runs sixteen engine cases: local and document-tree storage,
their cold launches from compiled `startup.tjs`, two movie cases, two archive
cases, four transition cases, two audio-filter cases and two modal-dialog cases. Transition cases run
with both software and OpenGL rendering; the harness checks the selected renderer
in the process log. Compiled startup must preserve its bootstrap globals before entering the
synthetic framework. The storage cases cover `fstat` directory listings, copying,
timestamps, renaming and removal; removal must reject the wrong entry type and
preserve nonempty directories. The drawing cases exercise `layerExImage` clipping,
color effects, blur and noise, and `textrender` through the Android font backend.
TLG5 and TLG6 saves convert the renderer's RGBA channels to the file's BGRA
channel order. A 9-by-10 pixel pattern checks 24-bit and 32-bit save/load paths,
alpha preservation, scanline and block boundaries, and an unchanged source image.
The synthetic PSB TLG resource follows the plane order in the
[KiriKiri reference decoder](https://github.com/krkrz/krkrz/blob/master/visual/LoadTLG.cpp).

GCC and Clang sanitizer tests cover the image-effect algorithms and compile the
production `TextRenderBase` script into the real TJS interpreter. Its regressions
cover font callbacks, wrapping, formatting, ruby, links, Unicode clusters,
vertical layout, language options, character timing, click-wait records and
malformed/recursive controls.

The transition fixtures call the native `Layer.beginTransition` API, capture
intermediate and final pixels with and without child layers, and verify option
errors explicitly. Rule images come from synthetic BMP data through both storage
backends. Repeated GPU writes and reads exercise cached-pixel invalidation.
Audio fixtures decode a constant PCM WAV through real `WaveSoundBuffer` objects,
compare the visualization samples from one -12 dB gain filter and two serial
-6 dB filters, and repeat after stop/open/play. This checks that script filter
objects reach the engine's decoder pipeline. Repeated immediate playback, stop,
reopen and invalidation exercise decoder shutdown before filter and sample-buffer
release. Pure DSP tests check the individual algorithms independently of Android
playback scheduling.

The modal-dialog driver inspects the real Android UI and answers eight successive
dialogs. Before every tap it confirms the script is still waiting at that request.
This checks fresh Yes/No and button-index results, accepted and empty input,
cancellation, and a message after an input dialog through both storage backends.
Each Java dialog owns its editor and callbacks; native results are reset before
dispatch and synchronized with the UI response. Progress reads wait for complete
UTF-16 or UTF-8 stage records, and a completion marker checks the final dialog.
Host tests cover partial records, transient ADB/XML read failures and early returns.

Tests use synthetic data and a test-only document provider in a separate APK.
The provider grants a subtree from its own UID; the application then uses normal
persisted URI permissions. Root ADB is used for deployment and collecting results,
not for granting the application unrestricted filesystem access. No commercial
game data is included in the test APK or uploaded by the workflow.

## CI artifacts and signing

`build_android.yml` runs on pull requests, `yuri`/`fix/**` pushes, version tags,
and manual dispatch. Artifacts include native test reports, debug and release test
APKs, the instrumentation APK, logcat, fixture results, and screenshots.

The release test APK contains the optimized/minified release code, signed with the
CI development identity. Each app variant has its own matching instrumentation
APK. Production signing is separate. `CI_DEBUG_KEYSTORE_B64` supplies the persistent PKCS#12
development keystore; its public certificate fingerprint is checked in CI. Fork
pull requests use a disposable test identity when that secret is unavailable.

Version-tag publication requires `RELEASE_KEYSTORE_B64`, `SIGN_KEY_ALIAS`,
`SIGN_KEY_PASS`, and `SIGN_STORE_PASS`. The publication job depends on successful
native tests, APK validation, and the entire emulator matrix. It publishes the
production-signed release APK. Ordinary branch builds do not publish releases.

## Dependency reproducibility

`script/dependencies.lock.json` pins the source, binary, and UI asset bundles by
URL, SHA-256, and size. Cache keys include the lock and fetch script. Extraction
occurs only after verification. A digest of the native dependency build scripts
and patches prevents silently reusing binaries after their inputs change.

These are verified legacy prebuilt bundles, not newly rebuilt dependencies. To
change native dependencies, rebuild them in GitHub Actions using the port scripts,
publish replacement bundles, and update the lock and native input digest together.
Do not simply update the digest to bypass an input mismatch.

## Compatibility limits

- Legacy class bytecode that undercounts the `extends global.Base` temporary
  receives a matching register-allocation correction in the validator and
  loader. A sanitizer test compiles a synthetic class, reproduces the old frame
  declaration, loads it and constructs an instance. New compilation records the
  temporary correctly; other out-of-range registers remain rejected.

- Packed-metadata fixtures exercise `Scripts.loadDataPack`, including binary
  octets, Unicode, endian handling and encrypted LZ4 containers. See `DATAPACK.md`.
- Dialog fixtures check the desktop `WIN32Dialog.messageBox` button/result
  contract without opening modal dialogs. Native `System.confirm` uses the
  Android activity's modal UI. Window-control menus retain icon/style properties
  and show text labels for icon-only controls; arbitrary Windows bitmap handles
  and horizontal menu-bar layout are not rendered.

- This remains an Android ARM64 Kirikiri/TJS runtime. Desktop ports and the SDL
  rendering migration are separate work.
- The target SDK remains 29. Document-tree storage works independently of legacy
  path access. App-private and document-mount startup paths do not request legacy
  storage permission; raising the target SDK and replacing the remaining legacy
  file-browser permission UI requires its own Android migration and device tests.
- Document providers must supply seekable descriptors for engine random access.
  Providers that supply only streams are not yet supported by this bridge.
- `layerExDraw` implements text, metrics, paths/curves, transforms, solid and linear
  gradient brushes, basic pens, image drawing, PNG/JPEG/WebP export, and color-region
  queries. Texture/path-gradient/hatch brushes, dashed/custom pens, and the full
  Windows GDI+ API remain incomplete. Unsupported implemented entry points report
  errors instead of silently drawing nothing. Font hinting can differ from Windows.
- `windowEx` provides virtual-window minimize/maximize/restore. Windows handles,
  styles, and other platform-specific functions are not emulated.
- Movie stop frames and basic controls are implemented. Advanced mixing and color
  adjustment APIs still require backend work.
- Publisher-specific archive encryption, hashed names, and native Windows plugins
  require separate compatibility work and appropriate fixtures. Recognizing XP3
  does not establish support for every game's archive variant.

The test workflow records what was exercised; a successful build alone is not a
claim that every Kirikiri game or every plugin API is compatible.
