# rgbx-extension-template

Template for building **rgbx animation extensions** for the
[RGB Sunglasses](https://github.com/skalldri/rgb-sunglasses) — standalone, with
no firmware-repo checkout and no Zephyr toolchain. One C (or C++) file becomes
both a device-loadable `.llext` and a `.wasm` you can test instantly in the
hosted web simulator.

## Documentation

- **[Getting started: your first extension](https://rgb-sunglasses.autom8ed.com/api/getting-started.html)**
  — a start-to-finish walkthrough that builds a working extension and explains
  each concept as it uses it: declaring parameters, reading the IMU/audio/button
  inputs, drawing pixels, and the good-moment signal. Ends with complete C and
  C++ listings. **Start here if this is your first extension.**
- **[API reference](https://rgb-sunglasses.autom8ed.com/api/)** — every type,
  macro and function in the rgbx ABI, generated from the headers this template
  builds against. Handy jumping-off points:
  [`rgbx_api.h`](https://rgb-sunglasses.autom8ed.com/api/rgbx__api_8h.html)
  (the flat C ABI) and
  [`rgbx::Animation`](https://rgb-sunglasses.autom8ed.com/api/classrgbx_1_1Animation.html)
  (the C++ wrapper).

This README covers the template itself — building, publishing, and the sandbox
constraints the build gates enforce.

## Quick start

1. **Use this template** (or fork) → clone your new repo.
2. Build both artifacts (first run downloads the pinned SDK + toolchains,
   ~5 min; afterwards it's seconds):

   ```bash
   ./build.sh
   ```

   Prerequisites: bash, cmake ≥ 3.21, Node.js ≥ 20, curl, tar
   (Linux or macOS; on Windows use WSL).

3. **Test without hardware**: drag `build/wasm/my_extension.wasm` onto
   <https://rgb-sunglasses.autom8ed.com/sim/>. The simulator runs your real
   code against the firmware's tick semantics — parameters, IMU/audio/button
   inputs, brightness behavior and all.
4. Edit `src/main.c` (it's the kitchen-sink "hello" reference — every
   parameter type and every input source) and iterate. Prefer C++? See
   "Writing your extension in C++" below. If the ABI isn't obvious from the
   example, the
   [getting started guide](https://rgb-sunglasses.autom8ed.com/api/getting-started.html)
   walks through the same ground one concept at a time.
5. **Rename your extension**: change `project(my_extension ...)` in
   `CMakeLists.txt`. The name must match `^[a-z0-9_]{1,25}$` — it becomes the
   `.llext` filename on the device and must equal your future registry-entry
   name (below).

## Writing your extension in C++

Copy the C++ example over the C one — the build automatically prefers
`src/main.cpp` when it exists (you can delete `src/main.c`):

```bash
cp examples/cpp-waves/main.cpp src/main.cpp
./build.sh
```

The [`rgbx::Animation`](https://rgb-sunglasses.autom8ed.com/api/classrgbx_1_1Animation.html)
wrapper (in the SDK's `include/rgbx/rgbx_animation.h`)
replaces the raw ABI boilerplate: subclass it, override `tick(dt_ms)`, and
declare everything with one `RGBX_ANIMATION(Class, "Name", 40, 12, params...)`
macro — it emits and exports all the ABI symbols for you. You get typed
parameter accessors (`paramU32`/`paramColor`/`paramBool`/`paramString` — the
last one hides the string-parameter indexing trap), `setPixel()`, and an
optional `goodMoment()` override for shuffle-mode switch points.

C++-specific rules (the SDK toolchain enforces the first two): no exceptions,
no RTTI, and the class must be trivially destructible (a `static_assert` in
the macro checks this). Still one translation unit, still no heap.

## Testing on real hardware (optional)

Copy `build/arm/<name>.llext` onto the glasses' USB mass-storage disk under
`ext/`, then sync, eject, and reboot the board. The firmware discovers
extensions at boot; select yours from the companion app or the `ext` shell
command.

## Publishing your extension

When it's ready, submit a PR to the
[rgb-sunglasses](https://github.com/skalldri/rgb-sunglasses) repo adding one
entry to `extensions/registry.json`:

```json
{
  "name": "my_extension",
  "repo": "https://github.com/you/your-extension-repo",
  "rev": "<full 40-hex commit SHA to publish>",
  "description": "One line about what it looks like",
  "author": "you",
  "license": "MIT"
}
```

`name` must equal your CMake project name; `rev` is the exact commit the
maintainers review and build. Once merged, every firmware release rebuilds
your extension from that pinned commit and ships it as a release asset — the
companion app then installs it onto devices automatically. Your repo must
carry an OSI-approved license.

## Rules of the sandbox

Your extension runs in a memory-protected sandbox with a per-tick CPU budget.
The build gates enforce most of this, but know the constraints:

- **One translation unit** (a single `.c` or `.cpp`; the build prefers
  `src/main.cpp` over `src/main.c`).
- **40×12 framebuffer**, RGB8. Render near full-scale (255) channel values —
  the firmware applies a global brightness factor (default 0.02), so dim
  drawing is invisible on the panel.
- **≤ 16 parameters, ≤ 4 of them strings** (see the manifest in `src/main.c`,
  or [`rgbx_manifest`](https://rgb-sunglasses.autom8ed.com/api/structrgbx__manifest.html)
  in the API reference).
- **No heap, no exceptions, no RTTI.**
- **Single-precision math only.** Firmware v3.1.0+ exports a curated libm set
  (`sinf`, `cosf`, `tanf`, `atan2f`, `sqrtf`, `expf`, `logf`, `powf`, `fmodf`,
  `floorf`, `ceilf`, `roundf`), the 64-bit integer helpers, and `memmove` —
  so real trig works now. **Double precision does not**: `sin`, `pow`, or any
  expression that promotes to `double` fails the build gate (write float
  literals with the `f` suffix). The FPU handles float `+ - * /` inline.
- **≤ 24 KB** total loaded size (build gate checks this).
- **Globals reset on every activation** — the firmware reloads your extension
  each time it's selected; persist nothing.
- **Bound any phase accumulator you feed to `sinf`/`cosf`/`tanf`.** See below —
  this one is not caught by any gate and has bitten two shipped extensions.
- Callable firmware functions are exactly the SDK's `arm/allowed-symbols.txt`:
  the math set above plus `str*`/`mem*` and `printk` (output shows in the
  simulator's console and the device's debug shell).

## The trig cliff: bound your phase accumulators

The single easiest way to ship a slow extension, and the one gate that cannot
catch it. Both extensions currently in the community registry shipped with this
bug ([rgb-sunglasses#304](https://github.com/skalldri/rgb-sunglasses/issues/304)).

The device's `sinf`/`cosf`/`tanf` take a cheap argument reduction only while
`|x| <= 201.06`. Above that they fall into a multi-precision reduction that costs
several times more **and keeps getting more expensive as the argument grows**.

So this — the obvious way to write a moving animation — degrades over time:

```cpp
t_ms_ += dt_ms * paramU32(0) / 50u;              // free-running: BAD
const float t = static_cast<float>(t_ms_) * 0.001f;
... sinf(fx * kTau + t * 1.1f) ...               // |arg| crosses 201 after ~3 min
```

It is nasty because it is invisible to every quick check: the animation runs at
full speed for the first minute or two, and the accumulator resets every time the
extension is activated — so it looks perfect right after you select it, every time.

**Fix: wrap the accumulator at a period where every rate you use completes a whole
number of cycles.**

```cpp
/* Rates 1.1, 0.7, 1.7 rad/s are 11 : 7 : 17 on a 0.1 grid, so all three phases
 * return to their exact starting values after 20*pi s. The wrap is seamless, and
 * the argument is bounded for ANY speed value -- the bound is on the accumulator,
 * not the rate. */
constexpr uint32_t kPeriodMs = 62832u;           // 20*pi s
t_ms_ = (t_ms_ + dt_ms * paramU32(0) / 50u) % kPeriodMs;
```

Costs one multiply per tick, nothing per pixel. `examples/cpp-waves/main.cpp`
shows it in context.

If your rates share no common period, reduce each phase with `fmodf` instead — but
`fmodf` **keeps the sign of its dividend**, so a phase that can go negative lands in
`(-2*pi, 0]`. That is fine passed straight to `sinf`/`cosf`, which accept negative
arguments; it is *not* fine if you then index a lookup table with it. Add the period
back when the result is negative.

**How to check it on hardware:** leave the animation running for several minutes on
one uninterrupted activation, then read `ext stats` — switching animations in between
zeroes the counters. Watch the device console for `Render overran the tick interval`,
which shows up long before the per-tick CPU budget does (the budget sits well above
the render interval and will not catch this). Raising your speed parameter compresses
the timeline proportionally.

## Updating the SDK pin

`cmake/fw-release.cmake` pins the firmware release (and its `rgbx-sdk`
tarball sha256) this extension builds against. To move to a newer firmware
release, update both lines from the release page's `rgbx-sdk-*.tar.gz` asset
and rebuild. The ABI is append-only within a version, so newer SDKs build
older extension source unchanged; a genuine ABI version bump is announced in
the firmware release notes.
