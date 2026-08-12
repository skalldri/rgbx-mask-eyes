# rgbx-mask-eyes

Symbolic LED eyes with glitchy transitions, for the
[RGB Sunglasses](https://github.com/skalldri/rgb-sunglasses).

Modelled on Watch_Dogs 2's Wrench: a small set of deliberately-authored eye
symbols rather than a font, where the character comes from **how they change**
rather than from how many there are. Neutral is `X` — what that mask idles on —
and everything else is a departure that returns to it.

## Expressions

Four, selected by button (proto0: 0=Up, 1=Left, 2=Right, 3=Down):

| Button | Expression | Left / right eye |
| --- | --- | --- |
| Up | neutral | `X` `X` |
| Left | pleased | `^` `^` |
| Right | alert | `!` `!` |
| Down | angry | `>` `<` — pointing inward, which is what reads as a scowl |

Angry is the one asymmetric expression, which is deliberate: it proves the
per-eye glyph model rather than assuming both eyes always match.

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

There is also a low-probability idle glitch (~1 tear every few seconds) so a
held expression never reads as a frozen image.

## Parameters

| Name | Type | Default | Notes |
| --- | --- | --- | --- |
| Color | COLOR | white | Full-scale on purpose: the panel renders at ~2% global brightness, so a "dim" colour is invisible |
| Glitch Ms | UINT32 | 160 | 0 = instant cut, no transition effect. Clamped to 2000 |
| Idle Glitch | BOOL | on | The occasional tear while holding an expression |

Colour modes (spectrum sweep, random-on-beat, …) work on `Color` for free —
the host resolves the mode byte before the extension sees it.

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

**Randomness is a self-contained xorshift32.** The SDK exposes no RNG, and a
self-seeded one keeps the simulator's output reproducible run to run.

## Status

First cut. Buttons drive the expression so the look can be judged; the intended
end state is autonomous selection from IMU and audio (head bob → pleased, tilt →
inquisitive, transient → alert) with the app able to override. That slots in
above the expression table without changing the rendering or transition code.

Tracked as [rgb-sunglasses#53](https://github.com/skalldri/rgb-sunglasses/issues/53).

## Building

```bash
./build.sh          # -> build/arm/mask_eyes.llext and build/wasm/mask_eyes.wasm
```

Drag the `.wasm` onto the [hosted simulator](https://rgb-sunglasses.autom8ed.com/sim/),
or run it headlessly from a firmware checkout — `buttons-tour` presses all five
buttons 600 ms apart, which walks the whole expression set:

```bash
fw/sim/rgbx-sim run path/to/mask_eyes.wasm --scenario buttons-tour --ascii 1
```

Copy the `.llext` to `/NAND:/ext/` on a device to run it on hardware.
