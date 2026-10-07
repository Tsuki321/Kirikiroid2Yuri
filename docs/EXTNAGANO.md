# extNagano compatibility

`extNagano.dll` is a portable built-in plugin. Linking it registers these twelve
`Layer.beginTransition` providers. No Windows DLL is executed or emulated.
The normal engine transition lifecycle also exposes the providers to KAG.

| Name | Implemented behavior and options |
| --- | --- |
| `3duniversal` | Scatter pixels according to a color rule. `rule` is an image storage path or native `Layer`; `type` is `RGB` (default) or `HSB`. `speed1/2`, `accel1/2`, aliases `s1/2`, `a1/2`, and `bound1/2` control the outgoing/incoming motion. Motion defaults to zero; bounds default to zero. |
| `blurfade` | Separable box blur (`type:0`) or coarse bilinear blur (`type:1`), followed by a crossfade. `blur1/2` set both axes, then `blur1x/y`, `blur2x/y` override individual axes. Blur defaults to zero; `exponent` defaults to one. `prerender:0/1/2` are accepted scheduling hints; all use the same bounded, on-demand frame cache. |
| `scanline` | Alternating scan lines push the incoming image from opposite sides. |
| `zoomfade` | Centred zoom and crossfade. `zoom1` is the outgoing target percentage (100 by default); `zoom2` is the incoming initial percentage (200 by default). |
| `rgbfade` | Separate `delayR`, `delayG`, `delayB`, `delayA` values, each 0–255 and defaulting to zero. The channel with the greatest delay finishes at the requested transition end. |
| `spin` | Perspective rotation about the centre or either vertical edge. `type1` defaults to zero, `type2` to one. Accepts -1–11: -1 is stationary, 4/5 use the left hinge, 6/7 the right hinge, other values use the centre, and parity chooses the rotation direction. |
| `flutter` | Diagonal reflected page fold with a backside color, opacity, highlight, contact shadow and displacement. `back` is a script `0xAARRGGBB` color (zero preserves the reflected page), `alpha` defaults to 255 and `slip` to 8. |
| `book` | Shaded folded-page sweep with quadratic timing. `dir:0/1` chooses the side; `dir:-1` (default) randomly chooses once at creation. |
| `imagewipe` | Sweep a **colored alpha strip**, including its decorative edge. `rule` is a path or native `Layer`, and `dir:0/1` chooses the direction. This is different from the engine's grayscale `universal` transition. |
| `honeyturn` | Staggered hexagonal tiles turn over in the requested order. `size` defaults to 40, `dir` to 6, `order` to 2, `twist` to zero. Direction/order use numeric-keypad directions 1–9; order 5 moves outward from the centre. |
| `morphing` | Rasterize interpolated triangles and crossfade their independently sampled source coordinates. `before` and `after` are equal-sized flat arrays: six coordinates per triangle, `[x1,y1,x2,y2,x3,y3, ...]`. Gaps retain a crossfaded background; triangles that collapse during the animation contribute no pixels for that frame. |
| `multiripple` | Several delayed expanding waves reveal and vertically distort the new image. Supports `count` (default 1), `wavecount` (2), `rwidth` (32), `maxdrift` (24), `roundness` (1), `delaylast` (1). The final source is centred; earlier source locations are randomized once. |

Every provider requires `time` in milliseconds. Zero/one is raised to two; invalid
negative, nonfinite and overflowing values are rejected. Endpoints return the
original source pixels exactly, including their alpha values. Intermediate
compositing supports opaque, straight alpha and additive alpha engine layers.

## Provenance and fidelity

This implementation follows the publicly available
[wamsoft/extNagano recovery at commit 4b403e2f](https://github.com/wamsoft/extNagano/tree/4b403e2f684756c490975b9d1697289aa1f34240), including its
[manual](https://github.com/wamsoft/extNagano/blob/4b403e2f684756c490975b9d1697289aa1f34240/manual.tjs)
and individual effect source files. That project states that the original source was lost and
that its implementation was reconstructed from the 2006 x86 DLL with Ghidra and
the Kirikiri extrans provider structure. It is **not the original source**, and
pixel-identical rendering against that DLL has not been verified here.

The upstream [readme](https://github.com/wamsoft/extNagano/blob/4b403e2f684756c490975b9d1697289aa1f34240/readme.txt)
attributes the original plugin to Yamamoto, Shun, chiyoclone.net and Shinohara,
and the Kirikiri stub/common interfaces to W.Dee. It reports the original
Kirikiri/GNU GPL dual license and applies the engine's licensing to its recovery.
This port uses this repository's engine licensing and independently implements
the documented/recovered math with the current renderer interfaces.

Specific limitations are explicit:

- `3duniversal` retains the recovery's approximation for nonzero bounce bounds.
  It preserves the recovered 1024-scale direction table. In RGB mode the rule's
  red channel is start time, green is speed and blue is direction. Zero direction
  points right. HSB mode uses `255 - brightness` as start time, doubled saturation
  (clamped to 255) as speed, and hue as direction, preserving the recovery's
  integer rounding. These details follow the recovered implementation; the
  public manual contradicts it in places.
- `spin` uses the recovery's centre/door perspective model; distinct historical
  special modes among 0–11 are not reconstructed beyond the groups listed above.
- `flutter` provides a portable diagonal fold approximation. The exact original
  page curvature and shading are not reproduced. `book` follows the recovered
  fold geometry with renderer-aware alpha compositing.
- `honeyturn` was incomplete even in the original DLL according to upstream. This
  port provides a working hexagonal flip, keypad ordering and shear, not a claim
  of equivalence to unfinished original behavior.
- `multiripple` uses the recovered model of delayed radial waves and vertical
  displacement, including alternating image blends as wave phase advances.
  Wave scheduling is normalized so all waves finish within `time`;
  exact original random positions, waveform quantization and delays are not
  reproduced.
- `blurfade` uses one frame cache for all `prerender` values, as does the public
  recovery. It avoids allocating an entire precomputed animation. Blur filtering,
  resampling, and alpha rounding can differ from the original SIMD routines.

All twelve names invoke real renderers. Missing rules and invalid meshes produce
errors; they do not turn a failed effect into a successful link-only stub or a
replacement dissolve.

## Renderer and bounds

Rules load through the engine's image provider, so normal archive and Android
document-tree storage work. Layer rules are snapshotted while the script object
is retained. File rules tile to the engine's requested dimensions; an imagewipe
strip keeps its natural width. For a smaller native rule layer, 3duniversal and
imagewipe extend the final source row/column as appropriate to the recovered API.

Source textures are copied to RGBA before CPU access, covering compressed, RGB,
scaled and GPU textures. Each frame is computed once from full sources and sliced
into requested update rectangles, which keeps effects continuous across divided
updates and nonzero destination offsets. This correctness-first CPU path can be
slower than a native GPU implementation for large images or many ripple sources.

Image/rule dimensions are at most 16384 per side and 16,777,216 pixels. Blur radii
are limited to 4096 and use a linear-time separable filter. Morphing accepts at
most 4096 triangles, coordinates within ±32768 and at most 134,217,728 raster
candidate pixels per frame. Ripple source count is limited to 20; wavelength,
drift and ripple count are bounded. Nonfinite motion and division-by-zero cases
are rejected before rendering.

`tests/test_nagano_transitions.cpp` covers all provider algorithms, exact endpoints,
small dimensions, distinct midpoints, divided updates, negative strides, changed
sources at an equal clock tick, rule channel semantics, alpha, blur axes, triangle
warping, deterministic seeds and invalid input. Engine fixtures cover actual TJS
linking and `Layer.beginTransition`; builds and executable tests run in GitHub
Actions in accordance with the no-local-compilation workflow.
