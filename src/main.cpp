/*
 * Mask Eyes — symbolic LED eyes with glitchy transitions.
 *
 * Modelled on Watch_Dogs 2's Wrench: a small set of deliberately-authored eye
 * symbols rather than a font, with the character coming from HOW they change
 * rather than from how many there are. Neutral is X, which is what that mask
 * idles on; everything else is a departure that returns to it.
 *
 * Why authored bitmaps and not text: the extension ABI has no font or glyph
 * API (an extension gets params, inputs and a raw framebuffer, nothing else),
 * and a text glyph would be ~5x7 in a space that can hold 11x11. Drawing the
 * shapes directly also means the eye set is not limited to characters that
 * happen to exist in the firmware's font.
 */

#include <rgbx/rgbx_animation.h>

namespace {

/* Glyph canvas, one per eye.
 *
 * The panel is 40x12 with a 10x6 nose cutout at bottom-centre (x 15..24,
 * y 6..11) where no LEDs exist. Two 11x11 glyphs at x 2 and x 27 sit entirely
 * clear of it, with a margin either side. Row 11 is left dark: pushing the
 * glyphs down to use it would put the inner edges into the cutout. */
constexpr size_t kGlyphW = 11;
constexpr size_t kGlyphH = 11;
constexpr size_t kLeftEyeX = 2;
constexpr size_t kRightEyeX = 27;

/* Glyphs are string art on purpose. This is an aesthetic that gets iterated by
 * eye, and a packed bitmask is unreadable to edit — '#' lights a pixel, any
 * other character is dark. The cost is ~1 KB of rodata against a 24 KB llext
 * heap, which is not a constraint worth optimising against here. */
const char *const kGlyphX[kGlyphH] = {
    "##.......##",  //
    ".##.....##.",  //
    "..##...##..",  //
    "...##.##...",  //
    "....###....",  //
    "....###....",  //
    "....###....",  //
    "...##.##...",  //
    "..##...##..",  //
    ".##.....##.",  //
    "##.......##",  //
};

const char *const kGlyphCaret[kGlyphH] = {
    "...........",  //
    "...........",  //
    ".....#.....",  //
    "....###....",  //
    "...##.##...",  //
    "..##...##..",  //
    ".##.....##.",  //
    "##.......##",  //
    "...........",  //
    "...........",  //
    "...........",  //
};

const char *const kGlyphBang[kGlyphH] = {
    "....###....",  //
    "....###....",  //
    "....###....",  //
    "....###....",  //
    "....###....",  //
    "....###....",  //
    "....###....",  //
    "...........",  //
    "....###....",  //
    "....###....",  //
    "...........",  //
};

/* Angry is the one asymmetric expression: the eyes point INWARD at each other,
 * which is what reads as a scowl. Left shows '>', right shows '<'. */
const char *const kGlyphGt[kGlyphH] = {
    "##.........",  //
    ".##........",  //
    "..##.......",  //
    "...##......",  //
    "....##.....",  //
    ".....##....",  //
    "....##.....",  //
    "...##......",  //
    "..##.......",  //
    ".##........",  //
    "##.........",  //
};

const char *const kGlyphLt[kGlyphH] = {
    ".........##",  //
    "........##.",  //
    ".......##..",  //
    "......##...",  //
    ".....##....",  //
    "....##.....",  //
    ".....##....",  //
    "......##...",  //
    ".......##..",  //
    "........##.",  //
    ".........##",  //
};

struct Expression {
    const char *const *left;
    const char *const *right;
};

/* Button id selects the expression directly (proto0: 0=Up, 1=Left, 2=Right,
 * 3=Down), which is the whole input model for this first cut. Autonomous
 * selection from IMU/audio is the next step and slots in above this table
 * without changing anything below it. */
const Expression kExpressions[] = {
    {kGlyphX, kGlyphX},          /* 0 Up    — neutral */
    {kGlyphCaret, kGlyphCaret},  /* 1 Left  — pleased */
    {kGlyphBang, kGlyphBang},    /* 2 Right — alert */
    {kGlyphGt, kGlyphLt},        /* 3 Down  — angry */
};
constexpr size_t kNumExpressions = sizeof(kExpressions) / sizeof(kExpressions[0]);

constexpr size_t kParamColor = 0;
constexpr size_t kParamGlitchMs = 1;
constexpr size_t kParamIdleGlitch = 2;

/* Idle glitch: roughly one micro-tear every few seconds so a held expression
 * never reads as a frozen image. Expressed as a 1-in-N chance per tick at the
 * nominal ~90 Hz tick rate. */
constexpr uint32_t kIdleGlitchOdds = 260;
constexpr uint32_t kIdleGlitchMs = 45;

/* Per-row transition effects. Computed once per frame and applied to BOTH eyes
 * so they tear together — they are one display on one face, and tearing them
 * independently immediately reads as two unrelated panels. */
struct RowFx {
    bool useIncoming;
    int8_t shift;
    bool blank;
};

class MaskEyes : public rgbx::Animation {
   public:
    void tick(uint32_t dt_ms) override {
        readButtons();
        advanceGlitch(dt_ms);

        const uint32_t color = paramColor(kParamColor);
        const uint8_t r = (color >> 16) & 0xFF;
        const uint8_t g = (color >> 8) & 0xFF;
        const uint8_t b = color & 0xFF;

        RowFx fx[kGlyphH];
        buildRowFx(fx);

        fill(0, 0, 0);
        const Expression &out = kExpressions[previous_];
        const Expression &in = kExpressions[current_];
        drawEye(out.left, in.left, kLeftEyeX, fx, r, g, b);
        drawEye(out.right, in.right, kRightEyeX, fx, r, g, b);
    }

    /* Mid-glitch is the one moment a shuffle hop would look like a fault rather
     * than a transition, so it is the one moment that is not a good moment. */
    bool goodMoment() const override { return glitchLeftMs_ == 0; }

   private:
    void readButtons() {
        for (size_t i = 0; i < kNumExpressions; i++) {
            if (!buttonWasPressed(i)) {
                continue;
            }
            /* Re-pressing the current expression re-glitches in place — it is
             * the natural "do it again" and costs nothing to support. */
            previous_ = current_;
            current_ = i;
            glitchLeftMs_ = glitchTotalMs();
            glitchSpanMs_ = glitchLeftMs_;
        }
    }

    void advanceGlitch(uint32_t dt_ms) {
        if (glitchLeftMs_ > 0) {
            glitchLeftMs_ = (glitchLeftMs_ > dt_ms) ? (glitchLeftMs_ - dt_ms) : 0;
            return;
        }
        if (paramBool(kParamIdleGlitch) && (rand32() % kIdleGlitchOdds) == 0) {
            /* Same machinery as a transition, but into the SAME expression, so
             * it tears without changing what is shown. */
            previous_ = current_;
            glitchLeftMs_ = kIdleGlitchMs;
            glitchSpanMs_ = kIdleGlitchMs;
        }
    }

    uint32_t glitchTotalMs() const {
        const uint32_t ms = paramU32(kParamGlitchMs);
        /* 0 disables the effect entirely (instant cut); the upper bound keeps a
         * mistyped value from leaving the panel tearing for minutes. */
        return (ms > 2000u) ? 2000u : ms;
    }

    void buildRowFx(RowFx *fx) {
        if (glitchLeftMs_ == 0 || glitchSpanMs_ == 0) {
            for (size_t row = 0; row < kGlyphH; row++) {
                fx[row] = RowFx{true, 0, false};
            }
            return;
        }

        /* Rows resolve to the incoming glyph progressively as the transition
         * runs, so it reads as the new expression fighting its way in rather
         * than as uniform noise for the whole duration. */
        const uint32_t elapsed = glitchSpanMs_ - glitchLeftMs_;
        const uint32_t settled = (elapsed * 100u) / glitchSpanMs_;

        for (size_t row = 0; row < kGlyphH; row++) {
            const uint32_t roll = rand32();
            fx[row].useIncoming = ((roll % 100u) < settled);
            /* Tear amplitude decays with the transition so the shape steadies
             * as it lands. */
            const int32_t reach = static_cast<int32_t>(3u - (3u * settled) / 100u);
            fx[row].shift = static_cast<int8_t>(
                reach == 0 ? 0 : (static_cast<int32_t>((roll >> 8) % (2u * reach + 1u)) - reach));
            /* Dropouts only early on: a hole in the final frames looks broken
             * rather than glitchy. */
            fx[row].blank = (settled < 60u) && (((roll >> 16) % 100u) < 12u);
        }
    }

    void drawEye(const char *const *outgoing, const char *const *incoming, size_t originX,
                 const RowFx *fx, uint8_t r, uint8_t g, uint8_t b) {
        for (size_t row = 0; row < kGlyphH; row++) {
            if (fx[row].blank) {
                continue;
            }
            const char *src = fx[row].useIncoming ? incoming[row] : outgoing[row];
            for (size_t col = 0; col < kGlyphW; col++) {
                if (src[col] != '#') {
                    continue;
                }
                const int32_t x = static_cast<int32_t>(originX + col) + fx[row].shift;
                if (x < 0 || x >= static_cast<int32_t>(width())) {
                    continue;
                }
                setPixel(static_cast<size_t>(x), row, r, g, b);
            }
        }
    }

    /* Self-contained xorshift32: the SDK exposes no RNG, and a self-seeded one
     * keeps the simulator's frame output reproducible run to run. */
    uint32_t rand32() {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return rng_;
    }

    size_t current_ = 0;
    size_t previous_ = 0;
    uint32_t glitchLeftMs_ = 0;
    uint32_t glitchSpanMs_ = 0;
    uint32_t rng_ = 0x9E3779B9u;
};

}  // namespace

RGBX_ANIMATION(MaskEyes, "Mask Eyes", 40, 12,
               RGBX_PARAM("Color", RGBX_PARAM_COLOR, 0x00FFFFFF),
               RGBX_PARAM("Glitch Ms", RGBX_PARAM_UINT32, 160),
               RGBX_PARAM("Idle Glitch", RGBX_PARAM_BOOL, 1));
