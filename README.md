# rgbx-mask-eyes

Symbolic LED eyes with glitchy transitions, for the
[RGB Sunglasses](https://github.com/skalldri/rgb-sunglasses).

Modelled on Watch_Dogs 2's Wrench: a small set of deliberately-authored eye
symbols rather than a font, where the character comes from **how they change**
rather than from how many there are. Neutral is `X` — what that mask idles on —
and everything else is a departure that returns to it.

## Expressions

| Expression | Left / right eye | Driven by |
| --- | --- | --- |
| neutral | `X` `X` | the floor — what it settles to |
| pleased | `^` `^` | sustained head motion (bob or nod) |
| alert | `!` `!` | a startle: sound rising sharply in a quiet room |
| angry | `>` `<` | **manual only** |
| tilt left | `—` `O` | head tilted, ear toward shoulder |
| tilt right | `O` `—` | mirrored |

The tilt pair and angry are asymmetric on purpose. Tilt has to be, since a
symmetric glyph could not encode *which* way the head went; angry points the
eyes inward at each other, which is what reads as a scowl.

**Angry has no autonomous trigger, deliberately.** No head or audio gesture
honestly means "angry", and inventing one would make the face lie. It stays a
button/app expression.

Buttons 0-3 (Up/Left/Right/Down) select the first four directly and suspend
autonomous selection for 6 s, so the manual test path still works with `Auto` on.

## Autonomous selection

Raw inputs are continuous and noisy; expressions are discrete and few. Mapping
one to the other with plain thresholds makes the state **chatter** — it flips
every frame whenever a signal sits near a boundary, which on a face reads as
broken rather than expressive. So the design is about arbitration:

- **Features, not raw inputs.** Gravity is the low-passed accelerometer and
  motion is what is left over, because the accelerometer reads +g on whichever
  axis points up — without that split a lean and a nod are indistinguishable.
  Motion includes the gyro, since a nod is a rotation. Engagement is smoothed
  band energy, over a much slower "how loud has this room been lately" baseline.
- **Momentary vs sustained.** Alert is an *event*, not a state: a one-shot with
  a hold timer. Treating it as something to arbitrate into would mean deciding
  when to leave it, which is unanswerable.
- **Hysteresis and dwell.** A challenger must both differ *and* persist, the
  incumbent gets a minimum dwell, and enter/exit thresholds differ. These are
  what kill the chatter.

Head tilt is roll about Z, which shows up on **accel.Y** (+X is the crown, +Y
the left temple — `fw/docs/imu-coordinate-frame.md`). That doc has not
bench-verified polarity, so if the tilt glyphs come out mirrored on a real head,
flip `kTiltLeftSign` rather than editing the selector.

Startles are measured against the *slow* baseline and only fire while that
baseline is low. Sustained music simply raises it, so nothing startles at a gig —
and a refractory period stops one real event re-firing as several.

Nothing in the sustained path depends on beat detection, which keeps baseline
behaviour insulated from beat-detection quality
([#264](https://github.com/skalldri/rgb-sunglasses/issues/264)).

### Measured, not guessed

Thresholds were read off the simulator scenarios with a temporary trace:

| scenario | bob | baseline |
| --- | --- | --- |
| silence | 0.00 | 0.00 |
| dance | 1.80–2.52 | 0.03–0.92 |
| nod | ~1.9 (gyro) | 0.00 |
| pink noise | 0.00 | 1.23–10.79 |

Expression changes per 10 s — chatter is the failure mode, so it is the metric:

| scenario | calm | lively |
| --- | --- | --- |
| silence | 0 | 0 |
| nod | 0 | 1 |
| dance | 2 | 3 |
| head-roll | 3 | 3 |
| metronome-120 | 2 | 2 |
| pink-noise | 2 | 2 |

## The glitch

Changing expression runs a short transition (default 160 ms) in which each row
independently picks whether it shows the outgoing or the incoming glyph, gets a
horizontal tear of up to ±3 px, and has a small chance of dropping out.

Three details make it read as glitching rather than as noise:

- **Rows resolve progressively.** The chance of showing the incoming glyph rises
  with elapsed time, so the new expression fights its way in instead of the
  whole panel being random for a fixed duration.
- **Tear amplitude decays**, so the shape steadies as it lands.
- **Dropouts only happen early.** A hole in the final frames looks broken; a
  hole in the first frames looks like a display glitch.

Both eyes tear together, using one set of per-row effects. They are one display
on one face — tearing them independently immediately reads as two unrelated
panels.

**Durations are jittered, not fixed.** `Glitch Ms` is a centre, not a constant:
every glitch length is spread by ±`Glitch Jitter`%, so transitions, double-takes
and idle tears all vary. Without this the texture varied but the *rhythm* did
not — every expression change took exactly the same time to resolve, which over
a long session reads as mechanical in a way a real failing display would not.

**Double-take.** A transition has a `Double Take`% chance of re-glitching in
place the moment it lands, so the eye snaps to the new expression, hesitates,
and re-settles. It runs at half the transition length — at full length it reads
as a second transition rather than a stutter.

There is also a low-probability idle glitch (~1 tear every few seconds) so a
held expression never reads as a frozen image. Its interval is a per-tick
probability rather than a timer, so the gaps are geometrically distributed —
usually a few seconds, occasionally much shorter or longer.

## Parameters

| Name | Type | Default | Notes |
| --- | --- | --- | --- |
| Color | COLOR | white | Full-scale on purpose: the panel renders at ~2% global brightness, so a "dim" colour is invisible |
| Glitch Ms | UINT32 | 160 | 0 = instant cut, no transition effect. Clamped to 2000 |
| Idle Glitch | BOOL | on | The occasional tear while holding an expression |
| Glitch Jitter | UINT32 | 40 | ±% spread on every glitch duration. 0 = fixed. Capped at 90 so a transition can never come out zero-length |
| Double Take | UINT32 | 12 | % chance a transition stutters and re-lands. 0 = off |
| Auto | BOOL | on | Autonomous selection. Off = buttons/app only |
| Lively | BOOL | off | Reactivity profile — see below |

Colour modes (spectrum sweep, random-on-beat, …) work on `Color` for free —
the host resolves the mode byte before the extension sees it.

**`Lively` is a switch rather than two builds** so the comparison can be made
while wearing the glasses — instantly, even mid-song. Reflashing two variants and
trying to remember how the first one felt is not a comparison anyone can make
honestly. Calm holds a face long enough that moving means something; Lively
reacts to everything and feels alive. It scales dwell, sustain, hysteresis
margins and the startle threshold together.

## Design notes

**Why authored bitmaps and not text.** The extension ABI has no font or glyph
API — an extension gets parameters, inputs and a raw framebuffer, nothing else.
Even on the firmware side a text glyph is ~5×7 in a space that can hold 11×11,
so drawing the shapes directly buys both resolution and freedom from whatever
characters happen to exist in the firmware's font.

**Geometry.** The panel is 40×12 with a 10×6 nose cutout at bottom-centre
(x 15..24, y 6..11) where no LEDs exist. Two 11×11 glyphs at x 2 and x 27 sit
entirely clear of it with a margin either side. Row 11 is left dark on purpose:
pushing the glyphs down to use it would put their inner edges into the cutout.

**Glyphs are string art** (`'#'` lights a pixel). This is an aesthetic that gets
iterated by eye, and a packed bitmask is unreadable to edit. The cost is ~1 KB
of rodata against a 24 KB llext heap.

**`printk` comes from `<rgbx/rgbx_sys.h>` — never hand-write its prototype.**
`extern "C"` matches on the name alone, so a wrong signature links anyway and
then diverges by target: on ARM a wrong return type is usually survivable, while
in the simulator the same source traps `unreachable` on the first call, because
WebAssembly calls are typed by full signature. This extension used to declare
`extern "C" int printk(const char *, ...)` itself, which was correct only against
the pre-`fw-v3.3.0` simulator shim. `rgbx_sys.h` (added in
[#351](https://github.com/skalldri/rgb-sunglasses/issues/351)) now single-sources
the whole supported symbol surface, and the wasm link runs with
`--fatal-warnings`, so a mismatch is a build error rather than a runtime trap.

**Randomness is a self-contained xorshift32.** Not a preference — the SDK's
supported symbol surface is 32 symbols (string/memory, `printk`, single-precision
libm, 64-bit division helpers) and contains **no RNG at all**. An extension runs
in a `K_USER` sandbox and can only resolve symbols the host exports, and the
`.llext` build gates against that list, so calling into Zephyr for randomness
fails at build time rather than at runtime. A self-seeded PRNG is also the better
choice regardless: it keeps the simulator's frame output reproducible run to run,
which is what makes golden-frame comparison possible.

## Status

Autonomous selection works and is tuned against the simulator. **Not yet
validated on a real head** — the reactivity profiles exist to be A/B'd on
hardware, and tilt polarity needs confirming (see `kTiltLeftSign`).

Tracked as [rgb-sunglasses#53](https://github.com/skalldri/rgb-sunglasses/issues/53).

## Building

```bash
./build.sh          # -> build/arm/mask_eyes.llext and build/wasm/mask_eyes.wasm
```

Prerequisites: bash, cmake ≥ 3.21, Node.js ≥ 20, curl, tar. `build.sh` checks the
Node version before it configures anything — the SDK's wasm gate
(`check-wasm.mjs`) needs ≥ 20, and an older one fails the wasm link with a bare
`SyntaxError` from inside the SDK. If you upgrade Node after a build, re-run
`./build.sh -URGBX_NODE`: CMake cached the old interpreter's path at configure
time and keeps using it otherwise.

Drag the `.wasm` onto the [hosted simulator](https://rgb-sunglasses.autom8ed.com/sim/),
or run it headlessly from a firmware checkout — `buttons-tour` presses all five
buttons 600 ms apart, which walks the whole expression set:

```bash
fw/sim/rgbx-sim run path/to/mask_eyes.wasm --scenario buttons-tour --ascii 1
```

Copy the `.llext` to `/NAND:/ext/` on a device to run it on hardware.
