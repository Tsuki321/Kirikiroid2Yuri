# Wave effects

`wfBasicEffect.dll` and `wfTypicalDSP.dll` are built-in plugins. Load them with
`Plugins.link`, then add their native objects to `WaveSoundBuffer.filters`
before opening the sound. The effects process decoded PCM in array order.

```tjs
Plugins.link("wfBasicEffect.dll");
Plugins.link("wfTypicalDSP.dll");
var sound = new WaveSoundBuffer(null);
var lowpass = new WaveSoundBuffer.WaveDSPFilter("LowPass", "Butterworth");
lowpass.setParams(void, 4, 1200); // source sample rate, order, cutoff in Hz
sound.filters.add(lowpass);
sound.filters.add(new WaveSoundBuffer.GainLimit(-6));
sound.open("voice.wav");
sound.play();
```

An effect instance belongs to one open buffer at a time. Using it twice in a
chain or attaching it to another buffer throws. A failed attachment releases
only the connections made by that attempt, leaving an existing owner intact.
Stopping a sound resets its processing state; reopening rebuilds the chain.
Release the buffer's native chain before manually finalizing or invalidating
its effects. Editing the `filters` array takes effect on the next `open`.

The adapter accepts mono/stereo integer PCM with 8, 16, 24 or 32 bits per
sample, and 32-bit float PCM, at 1,000–384,000 Hz. Effects exchange float PCM;
the engine converts the final result to 16-bit output. Existing labels and
sample positions pass through the chain. Appended reverb/delay tails advance
the position without creating labels. More channels and other sample formats
fail explicitly.

## Basic effects

All classes below are nested in `WaveSoundBuffer` and expose a native
`interface` property and a `finalize()` method.

| Class / script API | Behavior |
| --- | --- |
| `GainLimit(gainDb=0, threshold=1, mode=0)`; `init` has the same arguments | Gain in decibels, followed by optional limiting. Mode `-1` clips at the threshold; modes `0`–`3` provide soft curves above it when the threshold is below one. Gain is limited to ±120 dB and threshold to 0–1. |
| `DelayEffect()` or `DelayEffect(delayMs, feedback, wet=1, bufferMs=1000)`; `init(delayMs, feedback, wet=1, bufferMs=1000)` | Per-channel delay with feedback. The echo recurrence is `stored=(input+old)*feedback`, `output=input+old*wet`. A zero delay uses `bufferMs`. Delay/buffer durations are limited to 60 seconds, feedback magnitude stays below one, and wet gain is limited to ±16. Adds a one-second tail. |
| `GraphicEqualizer(g0=1, …, g9=1)`; `getGain(band)`, `setGain(band, gain)` | Ten octave bands centered at 31.25, 62.5, 125, 250, 500, 1000, 2000, 4000, 8000 and 16000 Hz. Gains are linear, with one neutral, and range from 0 to 64. A zero gain uses a finite −120 dB band design. Bands near or above Nyquist are bypassed. |
| `StkFreeVerb(effectMix=0.75)` | Stereo FreeVerb topology with per-instance comb/allpass buffers. `effectMix`, `roomSize`, `damping` and `width` range from 0 to 1; defaults are 0.75, 0.75, 0.25 and 1. `mode` selects freeze. `extend` controls the extra tail in milliseconds, defaults to 1000, and ranges from 0 to 60000. Set the tail before opening the sound. |

Parameter changes synchronize with decoded audio blocks. Delay changes reset
the delay line; equalizer changes preserve its current filter state. Seek,
stop and reopen clear history. These are portable implementations of the
script surface; bit-for-bit equality with every historical Windows plugin
release has not been established.

## Typical DSP

`WaveSoundBuffer.WaveDSPFilter(response="LowPass", family="RBJ",
state="DirectFormII")` exposes `setParams(...)` and `getParamInfo(index)`.
Names ignore case, spaces, underscores and hyphens.

| Family | Responses |
| --- | --- |
| RBJ | LowPass, HighPass, BandPass / BandPass1, BandPass2, BandStop, LowShelf, HighShelf, BandShelf, AllPass |
| Butterworth, ChebyshevI, ChebyshevII | LowPass, HighPass, BandPass, BandStop, LowShelf, HighShelf, BandShelf |
| Bessel | LowPass, HighPass, BandPass, BandStop, LowShelf |
| Elliptic, Legendre | LowPass, HighPass, BandPass, BandStop |
| Custom | OnePole, TwoPole |

State choices are `DirectFormI`, `DirectFormII`, `TransposedDirectFormI` and
`TransposedDirectFormII`; numeric suffixes `1`/`2` are also accepted. Unsupported
family/response/state combinations throw instead of returning a dummy filter.
The order limit is 50 where the underlying design has an order parameter.

`setParams` uses the chosen design's positional parameters and returns the
number of supplied slots, capped at eight. Omitted or `void` slots return to design
defaults. Slot zero is the design sample rate: omitting it tracks the source
rate, while an explicit value stays fixed. No resampling is performed.

`getParamInfo(index)` returns a dictionary with `name`, `label`, `defaultValue`,
`currentValue`, `min` and `max`, or `void` for an invalid index. Parameter order,
units and ranges come from DSPFilters itself. Frequency and bandwidth limits
also respect the selected sample rate. RBJ shelf slopes retain positive
damping, and wide RBJ bands near Nyquist are bounded to avoid overflowing the
coefficient calculation. `currentValue` reports the effective value; metadata
`min`/`max` describe the upstream parameter range before these joint bounds.
Non-finite parameters are rejected.

## Sources and validation

The 40 DSPFilters headers/source files are retained from
[Vinnie Falco's DSPFilters, revision acc49170](https://github.com/vinniefalco/DSPFilters/tree/acc49170e79a94fcb9c04b8a2116e9f8dffd1c7d/shared/DSPFilters).
Their Git blob hashes match that revision after accounting for line endings.
Each file keeps its MIT notice; algorithm credits remain in
`src/plugins/thirdparty/dspfilters/source/Documentation.cpp`.

FreeVerb uses Jezar's comb/allpass topology and the parameter conventions in
[STK FreeVerb](https://github.com/thestk/stk/blob/6aacd357d76250bb7da2b1ddf675651828784bbc/src/FreeVerb.cpp).
The STK notice is retained in `src/plugins/thirdparty/stk-LICENSE.txt`. This
adapter owns its tuning/buffer state per instance and normalizes the wet/dry
gains with a shared denominator.

`tests/test_wave_effects.cpp` covers decoded-sample gain, sign-preserving
limiting, echo timing, channel isolation, reset behavior, equalizer frequency
response, reverb decay, all supported filter families/state forms, parameter
metadata and RBJ coefficient edge cases. Android plugin fixtures exercise the
native TJS classes and filter-chain lifecycle. The audio fixture measures
actual PCM amplitude for one −12 dB filter and two −6 dB filters, repeats after
reopening, and rejects shared ownership before the original owner starts
decoding. Build and runtime validation run through GitHub Actions.
