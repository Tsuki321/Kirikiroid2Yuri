# Built-in plugin compatibility

`Plugins.link` resolves supported plugin names to implementations compiled into
the Android engine. Registration exposes the plugin's TJS classes, functions or
transition providers. Lookup accepts a filename or storage path, ignores filename
case and treats `.tpm` as the autoload spelling of `.dll`. Repeated links share one
registration, and `Plugins.getList()` reports loaded canonical module names.
Unknown names raise a script exception and remain absent from that list.

Built-in modules remain resident for the engine lifetime. `Plugins.unlink`
returns false for them and raises for an unloaded name, so scripts can correctly
detect that the APIs still exist. The engine's static-library link retains plugin
registration objects, including modules that are only referenced by script names.

| Module | Engine-visible behavior |
|---|---|
| `extrans.dll` | `mosaic`, `wave`, `ripple`, `rotateswap`, `turn`, `rotatezoom`, `rotatevanish` through `Layer.beginTransition`. |
| `extNagano.dll` | Twelve additional transition providers, including storage/layer rule images and triangle morphs. See [effect options and fidelity limits](EXTNAGANO.md). |
| `getLangName.dll` | `System.getCurrentUILangName` and `System.getCurrentLocaleName` return English language descriptions. Android locale/script information distinguishes Traditional and Simplified Chinese. |
| `wfBasicEffect.dll` | `WaveSoundBuffer.GainLimit`, `DelayEffect`, `GraphicEqualizer` and `StkFreeVerb` process decoded PCM through `WaveSoundBuffer.filters`. |
| `wfTypicalDSP.dll` | `WaveSoundBuffer.WaveDSPFilter` supplies the supported DSP filter families using the pinned DSPFilters implementation. |

See [audio effect APIs and limits](WAVE_EFFECTS.md) for constructor units,
supported DSP combinations and filter ownership behavior.

Transition sources are snapshotted through the renderer's RGBA texture API before
CPU effect rendering. GPU writes invalidate cached CPU pixels, so a subsequent
effect or screenshot reads the current frame. This path prioritizes compatible
pixel access across texture formats; large images can cost more CPU time than a
dedicated GPU effect.

The [validation suite](VALIDATION.md) checks real plugin registration, transition
pixels/completion and processed playback samples in both ordinary folders and
Android document trees. Host tests exercise pixel and DSP algorithms with GCC
and Clang sanitizers. These checks establish the covered interfaces; additional
Windows plugins and untested game-specific APIs still need separate ports.
