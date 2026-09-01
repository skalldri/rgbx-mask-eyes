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
#include <rgbx/rgbx_sys.h>

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

/* Tilt uses an ASYMMETRIC pair — one eye wide, one squinting — because that is
 * what reads as quizzical. Mirroring it also encodes WHICH way the head went,
 * which a symmetric glyph could not. */
const char *const kGlyphWide[kGlyphH] = {
    "...#####...",  //
    "..##...##..",  //
    ".##.....##.",  //
    "##.......##",  //
    "##.......##",  //
    "##.......##",  //
    "##.......##",  //
    "##.......##",  //
    ".##.....##.",  //
    "..##...##..",  //
    "...#####...",  //
};

const char *const kGlyphSquint[kGlyphH] = {
    "...........",  //
    "...........",  //
    "...........",  //
    "...........",  //
    ".#########.",  //
    ".#########.",  //
    ".#########.",  //
    "...........",  //
    "...........",  //
    "...........",  //
    "...........",  //
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
    {kGlyphX, kGlyphX},              /* 0 neutral   — button Up */
    {kGlyphCaret, kGlyphCaret},      /* 1 pleased   — button Left */
    {kGlyphBang, kGlyphBang},        /* 2 alert     — button Right */
    {kGlyphGt, kGlyphLt},            /* 3 angry     — button Down */
    {kGlyphSquint, kGlyphWide},      /* 4 tilt left  */
    {kGlyphWide, kGlyphSquint},      /* 5 tilt right */
};
constexpr size_t kNumExpressions = sizeof(kExpressions) / sizeof(kExpressions[0]);

enum : size_t {
    kExprNeutral = 0,
    kExprPleased = 1,
    kExprAlert = 2,
    kExprAngry = 3,
    kExprTiltLeft = 4,
    kExprTiltRight = 5,
};

/* Only the first four are reachable by button; the tilt pair is autonomous-only.
 * Angry is deliberately the other way round — see the selector. */
constexpr size_t kNumButtonExpressions = 4;

/* Head tilt (ear toward shoulder) is roll about Z, which shows up on accel.Y:
 * +X is the crown, +Y the LEFT temple, +Z out the back of the head
 * (fw/docs/imu-coordinate-frame.md). Tilting left puts world-up partly along
 * -Y, so negative accel.Y is a left tilt.
 *
 * That doc explicitly has NOT bench-verified polarity, so if the tilt glyphs
 * come out mirrored on a real head, flip this one constant rather than editing
 * the selector. */
constexpr float kTiltLeftSign = -1.0f;

constexpr size_t kParamColor = 0;
constexpr size_t kParamGlitchMs = 1;
constexpr size_t kParamIdleGlitch = 2;
constexpr size_t kParamJitterPct = 3;
constexpr size_t kParamDoubleTakePct = 4;
constexpr size_t kParamAuto = 5;
constexpr size_t kParamLively = 6;
/* Numeric overrides on top of the profiles: 0 means "use the profile's value",
 * which is unambiguous because 0 is never a legal live value for any of them.
 * The x10/x1000 scaling exists because the param ABI has no float type;
 * "Bob Enter x10"=85 therefore means 8.5 m/s^2-equivalent units. */
constexpr size_t kParamBobEnterX10 = 7;
constexpr size_t kParamTiltEnterX10 = 8;
constexpr size_t kParamTiltSpeed = 9; /* fast-tilt filter alpha x1000 */
constexpr size_t kParamSustainMs = 10;
constexpr size_t kParamDwellMs = 11;

/* Two coherent constant sets rather than two builds, so the comparison is a
 * switch you flip on the phone WHILE WEARING the glasses — instantly, even
 * mid-song. Reflashing two variants and remembering how the first one felt is
 * not a comparison anyone can make honestly. */
struct Profile {
    uint32_t minDwellMs;   /* floor on how long any expression is held */
    uint32_t sustainMs;    /* how long a challenger must persist to win */
    uint32_t alertHoldMs;  /* how long a startle interrupt holds */
    float tiltEnter;       /* |accel.Y| m/s^2 to enter a tilt */
    float tiltExit;        /* and to leave it — the gap IS the hysteresis */
    float bobEnter;        /* smoothed motion to read as "moving" */
    float bobExit;
    float startleExcess;   /* rise ABOVE the slow baseline that counts as a startle */
    float quietCeiling;    /* baseline above this = an already-loud room, no startles */
    uint32_t startleRefractoryMs;
};

/* Calm holds a face long enough that moving MEANS something. Lively reacts to
 * everything and feels alive. Neither is obviously right — that is the whole
 * reason this is a runtime switch. */
/* Thresholds are MEASURED, not guessed — read off the sim scenarios with a
 * temporary trace. The bob and tilt numbers were re-based on REAL captures
 * (walk-no-music-1, head-roll-no-music) after the synthetic scenarios turned
 * out to be unrealistically gentle: real walking runs bob ~4.0 mean / 6.75
 * max, which sat entirely above the old enter of 1.6, so the face lived in
 * "pleased" the moment its wearer moved.
 *
 *   silence         bob 0.00              baseline 0.00
 *   walking (real)  bob 4.0 avg, 6.75 max
 *   head rolls      bob 8-16
 *   dance/nod (synthetic, gentle) bob <= 3.7 — no longer reach pleased,
 *                   accepted: they under-shoot real motion by ~2x
 *   metronome       bob 0.00              baseline 0.03-0.92
 *   pink noise      bob 0.00              baseline 1.23-10.79
 *
 * bobEnter sits above walking's max so plain walking never triggers, with
 * bobExit above walking's p90 (5.4) so it always releases; pleased is for
 * vigorous, deliberate motion (dancing, head-banging). Tilt reads the FAST
 * tilt filter (see kTiltFastAlpha): walking's worst fast-tilt excursion is
 * 3.6, forward head rolls leak 2.8, real side rolls peak 5-8.9, so enter 4.0
 * separates them all. quietCeiling has to sit below ~0.9 or a click track
 * never raises the baseline out of startle range. */
constexpr Profile kCalm = {900u, 450u, 900u, 4.0f, 2.0f, 8.5f, 6.0f, 0.8f, 0.5f, 5000u};
constexpr Profile kLively = {350u, 140u, 550u, 4.0f, 2.5f, 7.0f, 5.0f, 0.5f, 0.5f, 3000u};

/* A manual press pins its expression and suspends autonomous selection, so the
 * button test path still works with Auto on. */
constexpr uint32_t kOverrideMs = 6000;

/* Exponential smoothing coefficients, per tick at the nominal ~30 Hz (the
 * host ticks every 33 ms; an earlier version of this comment claimed ~90 Hz,
 * which was never true).
 * Gravity is deliberately far slower than motion: the split between them is
 * what separates "which way is your head pointing" from "are you moving", and
 * a gravity estimate that tracks motion collapses the two. */
constexpr float kGravityAlpha = 0.02f;
/* Tilt gets its OWN, much faster low-pass. The gravity estimate above is the
 * right reference for the bob residual, but at tau ~1.7 s it is far too slow
 * to answer "is the head tilted right now": measured on head-roll-no-music,
 * raw |accel.Y| crossed 3.2 m/s^2 at t=1.1 s while the gravity estimate took
 * until t=13.4 s — a brief roll never registered at all. 0.12/tick (tau
 * ~260 ms) tracks a roll in a few hundred ms while still ironing out the
 * per-step spikes of a walk (0.15 pushed walking's excursions to 3.8, too
 * close to the 4.0 enter threshold). */
constexpr float kTiltFastAlpha = 0.12f;
constexpr float kBobAlpha = 0.10f;
constexpr float kEngageAlpha = 0.06f;
/* The startle baseline is far slower still — it is "how loud has this room been
 * lately", which is what makes a bang startling in a quiet room and unremarkable
 * at a gig. */
constexpr float kBaselineAlpha = 0.005f;
constexpr float kGyroWeight = 1.0f;

/* Jitter is capped below 100% so a transition can never come out zero-length:
 * at 100 the low end of the range would be an instant cut, which reads as the
 * effect having failed rather than as a fast glitch. */
constexpr uint32_t kMaxJitterPct = 90;

/* A double-take re-glitches IN PLACE on the expression that just landed, so it
 * reads as the eye snapping, hesitating, and re-settling. Shorter than the
 * transition it follows — at full length it reads as a second transition
 * instead of a stutter. */
constexpr uint32_t kDoubleTakeNumerator = 1;
constexpr uint32_t kDoubleTakeDenominator = 2;

/* Idle glitch: roughly one micro-tear every several seconds so a held
 * expression never reads as a frozen image. Expressed as a 1-in-N chance per
 * tick at the nominal ~30 Hz tick rate (so ~8.7 s between tears on average). */
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
        sinceChangeMs_ += dt_ms;
        updateFeatures(dt_ms);
        readButtons();
        selectExpression(dt_ms);
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
        for (size_t i = 0; i < kNumButtonExpressions; i++) {
            if (!buttonWasPressed(i)) {
                continue;
            }
            /* Re-pressing the current expression re-glitches in place — it is
             * the natural "do it again" and costs nothing to support. */
            previous_ = current_;
            current_ = i;
            startGlitch(glitchBaseMs());
            /* Rolled at trigger, consumed when this transition ends. Rolling it
             * at the END instead would re-roll on every frame the transition
             * happens to finish on, which is the same thing but harder to
             * reason about. */
            pendingDoubleTake_ = rollPercent(paramU32(kParamDoubleTakePct));
            overrideLeftMs_ = kOverrideMs;
            sinceChangeMs_ = 0;
        }
    }

    const Profile &profile() const { return paramBool(kParamLively) ? kLively : kCalm; }

    /* Override accessors: a nonzero override param wins, 0 falls back to the
     * active profile. The paired exit thresholds derive from the profile's
     * exit:enter ratio, so one knob per axis moves the whole hysteresis band
     * without collapsing it. */
    float bobEnterEff(const Profile &p) const {
        const uint32_t o = paramU32(kParamBobEnterX10);
        return (o != 0u) ? static_cast<float>(o) * 0.1f : p.bobEnter;
    }
    float bobExitEff(const Profile &p) const {
        const uint32_t o = paramU32(kParamBobEnterX10);
        return (o != 0u) ? bobEnterEff(p) * (p.bobExit / p.bobEnter) : p.bobExit;
    }
    float tiltEnterEff(const Profile &p) const {
        const uint32_t o = paramU32(kParamTiltEnterX10);
        return (o != 0u) ? static_cast<float>(o) * 0.1f : p.tiltEnter;
    }
    float tiltExitEff(const Profile &p) const {
        const uint32_t o = paramU32(kParamTiltEnterX10);
        return (o != 0u) ? tiltEnterEff(p) * (p.tiltExit / p.tiltEnter) : p.tiltExit;
    }
    float tiltAlphaEff() const {
        uint32_t o = paramU32(kParamTiltSpeed);
        if (o == 0u) {
            return kTiltFastAlpha;
        }
        if (o > 500u) {
            o = 500u; /* alpha 0.5 already tracks within ~2 ticks; beyond it is raw accel */
        }
        return static_cast<float>(o) * 0.001f;
    }
    uint32_t sustainMsEff(const Profile &p) const {
        const uint32_t o = paramU32(kParamSustainMs);
        return (o != 0u) ? o : p.sustainMs;
    }
    uint32_t dwellMsEff(const Profile &p) const {
        const uint32_t o = paramU32(kParamDwellMs);
        return (o != 0u) ? o : p.minDwellMs;
    }

    static float absf(float v) { return v < 0.0f ? -v : v; }

    /* Turns raw inputs into a few stable scalars, each with its own time
     * constant. Everything downstream reads these rather than rgbx_inputs, so
     * the selector never sees a single noisy frame. */
    void updateFeatures(uint32_t dt_ms) {
        /* Gravity is the LOW-passed accelerometer; motion is what is left. The
         * accelerometer reads +g on whichever axis points up, so without this
         * split a lean and a nod are indistinguishable. */
        if (!gravitySeeded_) {
            /* Seed from the first sample rather than converging from zero. From
             * zero the estimate takes ~550 ms to reach real gravity, and until it
             * does `accel - gravity` is most of a g — measured as bob peaking at
             * 4.65 in a SILENT, motionless scenario, which is a spurious "you are
             * moving" every single startup. */
            gravitySeeded_ = true;
            gravityX_ = accelX();
            gravityY_ = accelY();
            gravityZ_ = accelZ();
            tiltFastY_ = accelY();
        }
        gravityX_ += (accelX() - gravityX_) * kGravityAlpha;
        gravityY_ += (accelY() - gravityY_) * kGravityAlpha;
        gravityZ_ += (accelZ() - gravityZ_) * kGravityAlpha;
        tiltFastY_ += (accelY() - tiltFastY_) * tiltAlphaEff();

        /* Gyro is included because a nod is a ROTATION: the `nod` scenario drives
         * gyro alone and measured bob = 0 with an accel-only feature, i.e. the
         * one motion the issue names as "happy" was invisible. Weighted 1:1 —
         * rad/s and m/s^2 are not commensurable, so the weight is a tuning
         * constant chosen to put a brisk nod (~3 rad/s) in the same range as a
         * head bob, not a unit conversion.
         *
         * L1 norm on purpose: this feeds a threshold, so the choice of norm is
         * arbitrary, and it avoids a libm call entirely. */
        const float motion = absf(accelX() - gravityX_) + absf(accelY() - gravityY_) +
                             absf(accelZ() - gravityZ_) +
                             kGyroWeight * (absf(gyroX()) + absf(gyroY()) + absf(gyroZ()));
        bob_ += (motion - bob_) * kBobAlpha;

        float energy = 0.0f;
        for (size_t band = 0; band < numBands(); band++) {
            energy += bandEnergy(band);
        }
        engage_ += (energy - engage_) * kEngageAlpha;
        baseline_ += (engage_ - baseline_) * kBaselineAlpha;

        /* Beat flags are STICKY for ~3 ticks (fw/sim/PARITY.md), so a level test
         * counts one beat three times. Rising edge only. */
        const bool beatNow = isBeat(0);
        beatEdge_ = beatNow && !beatWas_;
        beatWas_ = beatNow;

        (void)dt_ms;
    }

    /* The background mood: what the face settles to when nothing is happening.
     * Angry has no entry here on purpose — there is no head or audio gesture
     * that honestly means "angry", and inventing one would make the face lie.
     * It stays a manual expression. */
    size_t backgroundMood(const Profile &p) const {
        const float tilt = accelYTilt();
        const float enter = (current_ == kExprTiltLeft || current_ == kExprTiltRight)
                                ? tiltExitEff(p)
                                : tiltEnterEff(p);
        if (absf(tilt) >= enter) {
            return (tilt * kTiltLeftSign > 0.0f) ? kExprTiltLeft : kExprTiltRight;
        }

        /* Deliberately NOT gated on music. Moving is a robust signal on its own,
         * whereas requiring audio made this dead silent whenever the room was —
         * and keeping it audio-free means beat-detection quality (#264) cannot
         * take the baseline behaviour down with it. */
        const float bobGate = (current_ == kExprPleased) ? bobExitEff(p) : bobEnterEff(p);
        if (bob_ >= bobGate) {
            return kExprPleased;
        }
        return kExprNeutral;
    }

    float accelYTilt() const { return tiltFastY_; }

    void selectExpression(uint32_t dt_ms) {
        const Profile &p = profile();

        if (overrideLeftMs_ > 0) {
            overrideLeftMs_ = (overrideLeftMs_ > dt_ms) ? (overrideLeftMs_ - dt_ms) : 0;
            return;
        }
        if (!paramBool(kParamAuto)) {
            return;
        }

        /* Momentary interrupt. Alert is an EVENT, not a state — treating it as
         * something to arbitrate into means deciding when to leave it, which is
         * unanswerable. A one-shot with a hold timer answers it for free. */
        if (alertLeftMs_ > 0) {
            alertLeftMs_ = (alertLeftMs_ > dt_ms) ? (alertLeftMs_ - dt_ms) : 0;
            return;
        }
        if (startleRefractoryLeftMs_ > 0) {
            startleRefractoryLeftMs_ =
                (startleRefractoryLeftMs_ > dt_ms) ? (startleRefractoryLeftMs_ - dt_ms) : 0;
        } else if (baseline_ <= p.quietCeiling && (engage_ - baseline_) >= p.startleExcess) {
            /* Measured against the SLOW baseline, and only while that baseline is
             * low. A per-tick delta fired on every beat of a click track (11-17
             * expression changes in 10 s); sustained music simply raises the
             * baseline, so the excess stays small and nothing startles. The
             * refractory then stops one real event re-firing as several. */
            startleRefractoryLeftMs_ = p.startleRefractoryMs;
            alertLeftMs_ = p.alertHoldMs;
            commit(kExprAlert);
            return;
        }

        /* Background arbitration: a challenger must both differ AND persist,
         * and the incumbent gets a minimum dwell. Without these two the state
         * chatters every frame whenever a feature sits near its threshold —
         * which is the failure mode that makes a face look broken. */
        const size_t want = backgroundMood(p);
        if (want == current_) {
            candidateMs_ = 0;
            return;
        }
        if (want != candidate_) {
            candidate_ = want;
            candidateMs_ = 0;
        }
        candidateMs_ += dt_ms;
        if (candidateMs_ >= sustainMsEff(p) && sinceChangeMs_ >= dwellMsEff(p)) {
            commit(want);
        }
    }

    void commit(size_t expression) {
        if (expression == current_) {
            return;
        }
        previous_ = current_;
        current_ = expression;
        startGlitch(glitchBaseMs());
        pendingDoubleTake_ = rollPercent(paramU32(kParamDoubleTakePct));
        sinceChangeMs_ = 0;
        candidateMs_ = 0;
        /* One line per CHANGE, never per tick. This is the chatter metric: the
         * question that decides whether the tuning is right is "how many times
         * did the face change in 30 s", and counting these answers it. */
        printk("maskeyes expr=%u\n", (unsigned)expression);
    }

    void advanceGlitch(uint32_t dt_ms) {
        if (glitchLeftMs_ > 0) {
            glitchLeftMs_ = (glitchLeftMs_ > dt_ms) ? (glitchLeftMs_ - dt_ms) : 0;
            if (glitchLeftMs_ == 0 && pendingDoubleTake_) {
                pendingDoubleTake_ = false;
                /* previous_ == current_ means it tears without changing what is
                 * shown: the stutter lands back on the expression just reached. */
                previous_ = current_;
                startGlitch((glitchBaseMs() * kDoubleTakeNumerator) / kDoubleTakeDenominator);
            }
            return;
        }
        if (paramBool(kParamIdleGlitch) && (rand32() % kIdleGlitchOdds) == 0) {
            /* Same machinery as a transition, but into the SAME expression, so
             * it tears without changing what is shown. */
            previous_ = current_;
            startGlitch(kIdleGlitchMs);
        }
    }

    /* Every glitch length goes through here, so jitter applies uniformly to
     * transitions, double-takes and idle tears — a fixed-length idle tear among
     * jittered transitions would stand out as the one mechanical element. */
    void startGlitch(uint32_t baseMs) {
        glitchSpanMs_ = jitter(baseMs);
        glitchLeftMs_ = glitchSpanMs_;
    }

    /* Spreads a duration to base +/- (base * jitter%), so the parameter stays
     * the thing you tune and becomes a CENTRE rather than a constant. */
    uint32_t jitter(uint32_t baseMs) {
        uint32_t pct = paramU32(kParamJitterPct);
        if (pct > kMaxJitterPct) {
            pct = kMaxJitterPct;
        }
        const uint32_t span = (baseMs * pct) / 100u;
        if (span == 0) {
            return baseMs;
        }
        return baseMs + (rand32() % (2u * span + 1u)) - span;
    }

    bool rollPercent(uint32_t pct) {
        if (pct == 0) {
            return false;
        }
        return (rand32() % 100u) < ((pct > 100u) ? 100u : pct);
    }

    uint32_t glitchBaseMs() const {
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
    bool pendingDoubleTake_ = false;

    float gravityX_ = 0.0f, gravityY_ = 0.0f, gravityZ_ = 0.0f;
    float tiltFastY_ = 0.0f;
    float bob_ = 0.0f;
    float engage_ = 0.0f;
    float baseline_ = 0.0f;
    bool gravitySeeded_ = false;
    uint32_t startleRefractoryLeftMs_ = 0;
    bool beatWas_ = false;
    bool beatEdge_ = false;
    size_t candidate_ = 0;
    uint32_t candidateMs_ = 0;
    uint32_t sinceChangeMs_ = 0;
    uint32_t alertLeftMs_ = 0;
    uint32_t overrideLeftMs_ = 0;
    uint32_t rng_ = 0x9E3779B9u;
};

}  // namespace

RGBX_ANIMATION(MaskEyes, "Mask Eyes", 40, 12,
               RGBX_PARAM("Color", RGBX_PARAM_COLOR, 0x00FFFFFF),
               RGBX_PARAM("Glitch Ms", RGBX_PARAM_UINT32, 160),
               RGBX_PARAM("Idle Glitch", RGBX_PARAM_BOOL, 1),
               RGBX_PARAM("Glitch Jitter", RGBX_PARAM_UINT32, 40),
               RGBX_PARAM("Double Take", RGBX_PARAM_UINT32, 12),
               RGBX_PARAM("Auto", RGBX_PARAM_BOOL, 1),
               RGBX_PARAM("Lively", RGBX_PARAM_BOOL, 0),
               RGBX_PARAM("Bob Enter x10", RGBX_PARAM_UINT32, 0),
               RGBX_PARAM("Tilt Enter x10", RGBX_PARAM_UINT32, 0),
               RGBX_PARAM("Tilt Speed", RGBX_PARAM_UINT32, 0),
               RGBX_PARAM("Sustain Ms", RGBX_PARAM_UINT32, 0),
               RGBX_PARAM("Dwell Ms", RGBX_PARAM_UINT32, 0));
