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
- **Tilt has its own, faster filter.** The gravity estimate (τ ≈ 1.7 s) is the
  right reference for the motion residual but far too slow to answer "is the
  head tilted right now" — on the `head-roll-no-music` capture, raw accel.Y
  crossed the tilt threshold at t = 1.1 s while the gravity estimate took until
  t = 13.4 s, so brief rolls never registered at all. A separate τ ≈ 260 ms
  low-pass tracks a roll in a few hundred ms while still ironing out the
  per-step spikes of a walk.
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

Thresholds were read off the simulator scenarios with a temporary trace. The
motion thresholds were re-based on **real device captures** (`walk-no-music-1`,
`head-roll-no-music`) after the synthetic scenarios turned out to be
unrealistically gentle — real walking runs bob ~3.9 mean / 6.0 max, which sat
entirely above the original enter threshold of 1.6, so the face lived in
"pleased" whenever its wearer moved at all:

| scenario | bob | fast tilt (m/s²) | baseline |
| --- | --- | --- | --- |
| silence | 0.00 | 0 | 0.00 |
| walking (real capture) | 2.0–6.0, mean 3.9 | −2.8 … +2.6 | — |
| head rolls (real capture) | 4.6–17.1 | −8.7 … +3.8 (side rolls) | — |
| dance / nod (synthetic) | ≤ 3.7 | — | 0.03–0.92 |
| pink noise | 0.00 | — | 1.23–10.79 |

So `bobEnter` sits above walking's max (pleased is for vigorous, deliberate
motion — dancing, head-banging), `tiltEnter` = 4.0 (≈ 24° of roll) clears both
walking's worst excursion (2.8) and forward-roll leakage while sitting well
below real side-roll peaks, and the synthetic `dance`/`nod` scenarios no longer
reach pleased — accepted, since they under-shoot real motion by ~2×.

Expression changes over the full scenario — chatter is the failure mode, so it
is the metric (audio startles included; the audio path is unchanged):

| scenario | length | calm | lively |
| --- | --- | --- | --- |
| silence | 5 s | 0 | 0 |
| walk-no-music-1 | 30 s | 1 | 4 |
| head-roll-no-music | 30 s | 12 | 24 |
| nod | 5 s | 0 | 0 |
| dance | 8 s | 2 | 4 |
| head-roll | 8 s | 3 | 3 |
| metronome-120 | 8 s | 2 | 4 |
| pink-noise | 5 s | 2 | 2 |

Every change on `walk-no-music-1`, `dance`, `metronome-120` and `pink-noise` is
an audio startle (alert + return), not motion: walking never triggers pleased
or tilt. On `head-roll-no-music` the tilt pair tracks the side-to-side rolls —
first activation lands within ~0.6 s of the fast-tilt signal crossing
threshold, against 13 s-to-never with the old single slow filter.

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
| Bob Enter x10 | UINT32 | 0 | Pleased enter threshold, tenths (85 = 8.5). 0 = profile value |
| Tilt Enter x10 | UINT32 | 0 | Tilt enter threshold in m/s² tenths (40 = 4.0 ≈ 24° roll). 0 = profile value |
| Tilt Speed | UINT32 | 0 | Fast-tilt filter alpha ×1000 (120 = 0.12), capped at 500. 0 = default |
| Sustain Ms | UINT32 | 0 | How long a challenger must persist. 0 = profile value |
| Dwell Ms | UINT32 | 0 | Minimum hold on any expression. 0 = profile value |

**The overrides ride on top of the profiles**: 0 means "use the active
profile's value" (never a legal live value, so the sentinel is unambiguous),
and the paired exit thresholds derive from the profile's exit:enter ratio, so
one knob per axis moves the whole hysteresis band without collapsing it. The
×10/×1000 scaling exists because the param ABI has no float type.

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

Autonomous selection works and is tuned against **real IMU captures**
(`walk-no-music-1`, `head-roll-no-music`) replayed in the simulator. **Not yet
validated on a real head** — the reactivity profiles and the numeric override
params exist to be A/B'd on hardware, and tilt polarity needs confirming (see
`kTiltLeftSign`).

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
