/*
 * C++ example extension — the rgbx::Animation wrapper path.
 *
 * To use it: copy this file to src/main.cpp (the build prefers main.cpp
 * over main.c automatically; you can delete src/main.c). One translation
 * unit only, same as C.
 *
 * What the wrapper gives you over raw C (see src/main.c for that style):
 *  - subclass rgbx::Animation, override tick(dt_ms); the RGBX_ANIMATION()
 *    macro at the bottom declares the manifest AND emits/exports all the
 *    ABI symbols for you
 *  - typed parameter accessors (paramU32/paramColor/paramBool/paramString)
 *    that hide the string-parameter indexing trap
 *  - setPixel()/width()/height() instead of manual framebuffer indexing
 *  - optional: override goodMoment() to tell shuffle mode where your
 *    animation's natural switch boundaries are
 *
 * Rules the SDK toolchain enforces or the sandbox imposes:
 *  - no exceptions, no RTTI (compiled with -fno-exceptions -fno-rtti)
 *  - the class must be trivially destructible (static_assert in the macro)
 *  - no heap; globals reset on every activation — persist nothing
 *  - single-precision math only: sinf/cosf/atan2f/sqrtf/... are available
 *    (exported by firmware v3.1.0+), but double-precision (sin, pow, any
 *    expression that promotes to double) is not — the build gate rejects
 *    it. Write float literals with the f suffix.
 *  - keep any phase/angle accumulator BOUNDED — see kPeriodMs below. This
 *    one bites hard and late: an unwrapped accumulator runs at full speed
 *    for the first couple of minutes and then silently gets several times
 *    slower, which no short test catches.
 *
 * This example: three sine waves interfere across the panel, tinted by a
 * BLE-tunable color, with speed and inversion parameters — the classic
 * plasma, but using real sinf() now that the firmware exports libm.
 */

#include <rgbx/rgbx_animation.h>

#include <math.h>

namespace {

constexpr float kTau = 6.2831853f;

/* Wrap the phase accumulator, and pick the period so the wrap is seamless.
 *
 * picolibc's sinf() only takes its cheap argument-reduction path while
 * |x| <= 2^7*(pi/2) = 201.06; past that it falls back to a multi-precision
 * reduction that costs several times more and keeps growing with the
 * argument. So a free-running accumulator makes an animation quietly slow
 * down minutes after it starts — this really happened to the plasma
 * extension (skalldri/rgb-sunglasses#304).
 *
 * 20*pi s works here because the three rates below (1.0, 0.7, 1.3 rad/s) are
 * commensurate — 10 : 7 : 13 on a 0.1 rad/s grid — so all three phases land
 * back on their starting values together. Pick your own period the same way:
 * the smallest time at which every rate you use has completed a whole number
 * of cycles. If yours don't share one, fmodf(phase, kTau) per wave also works.
 */
constexpr uint32_t kPeriodMs = 62832u; /* 20*pi s, to whole ms */

class CppWaves : public rgbx::Animation {
   public:
    void tick(uint32_t dt_ms) override {
        /* Speed is a percentage of nominal (50 == 1x). */
        t_ms_ = (t_ms_ + dt_ms * paramU32(0) / 50u) % kPeriodMs;
        const float t = static_cast<float>(t_ms_) * 0.001f;

        const uint32_t color = paramColor(1);
        const uint8_t cr = (color >> 16) & 0xFF;
        const uint8_t cg = (color >> 8) & 0xFF;
        const uint8_t cb = color & 0xFF;
        const bool invert = paramBool(2);

        for (size_t y = 0; y < height(); y++) {
            for (size_t x = 0; x < width(); x++) {
                const float fx = static_cast<float>(x) / static_cast<float>(width());
                const float fy = static_cast<float>(y) / static_cast<float>(height());

                /* Three interfering waves, each in [-1, 1]. */
                const float w = sinf(fx * kTau + t) +
                                sinf((fy + fx) * kTau * 0.5f - t * 0.7f) +
                                sinf(sqrtf(fx * fx + fy * fy) * kTau - t * 1.3f);

                /* Map [-3, 3] -> [0, 255]. Render near full scale: the
                 * firmware multiplies every pixel by a global brightness
                 * factor (default 0.02), so dim drawing is invisible. */
                float v = (w + 3.0f) * (255.0f / 6.0f);
                if (invert) {
                    v = 255.0f - v;
                }
                const uint32_t vi = static_cast<uint32_t>(v);
                setPixel(x, y, static_cast<uint8_t>(cr * vi / 255u),
                         static_cast<uint8_t>(cg * vi / 255u),
                         static_cast<uint8_t>(cb * vi / 255u));
            }
        }
    }

    /* Shuffle's good-switch-point: once per full cycle of the first wave.
     * (kPeriodMs isn't a multiple of 1000, so this window shifts phase once
     * per wrap. Harmless — it still opens about once a second.) */
    bool goodMoment() const override { return (t_ms_ % 1000u) < 40u; }

   private:
    uint32_t t_ms_ = 0;
};

}  // namespace

RGBX_ANIMATION(CppWaves, "C++ Waves", 40, 12,
               RGBX_PARAM("Speed", RGBX_PARAM_UINT32, 50),
               RGBX_PARAM("Color", RGBX_PARAM_COLOR, 0x0040C0FF),
               RGBX_PARAM("Invert", RGBX_PARAM_BOOL, 0));
