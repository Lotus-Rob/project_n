#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/glow.hpp"

#include <doctest/doctest.h>

namespace {

constexpr f32 QUARTER = 0.25F;
constexpr f32 HALF = 0.5F;
constexpr f32 THREE_QUARTERS = 0.75F;
constexpr f32 PULSE_AT_QUARTER_BEAT = 0.55F;
constexpr f32 PULSE_AT_HALF_BEAT = 0.1F;
// Depth 0.9 held at the beat's average: 1 - 0.9 * (1 - mean), the mean of ease in 1/3 and of ease out 2/3.
constexpr f32 EASE_IN_AVERAGE = 0.4F;
constexpr f32 EASE_OUT_AVERAGE = 0.7F;
constexpr f32 CHARGE_FADING = 0.0625F;
constexpr f32 THREE_TENTHS = 0.3F;
constexpr f32 TWO = 2.0F;
constexpr f32 TOO_BIG = 17.0F;
constexpr f32 TOO_FAST = 0.2F;
constexpr f32 FAST_ENOUGH = 0.34F;
constexpr f64 QUARTER_BEAT_TICKS = 24.0;
constexpr f64 HALF_BEAT_TICKS = 48.0;
constexpr f64 FULL_BEAT_TICKS = 96.0;
constexpr f64 HALF_FADE_TICKS = 12.0;
constexpr f64 PAST_FADE_TICKS = 30.0;
constexpr f64 BEATS_IN_A_LONG_SESSION = 4'294'967'296.0;
// 2^32 beats of the pulse's own period (as the f32 1.6 is stored) and a quarter beat: a long session's phase.
constexpr f64 FAR_TICKS = (BEATS_IN_A_LONG_SESSION * static_cast<f64>(1.6F) * 60.0) + QUARTER_BEAT_TICKS;

constexpr core::GlowDrive PULSE{ .kind = core::DriveKind::Pulse,
                                 .periodSeconds = 1.6F,
                                 .depth = 0.9F,
                                 .curve = core::GlowCurve::Sine };
constexpr core::GlowDrive CHARGE{ .kind = core::DriveKind::Charge,
                                  .curve = core::GlowCurve::EaseIn,
                                  .fadeSeconds = 0.4F,
                                  .fadeCurve = core::GlowCurve::EaseOut };
constexpr core::GlowDrive PROGRESS{ .kind = core::DriveKind::Progress, .curve = core::GlowCurve::Linear };

f32 Strength(const core::GlowDrive& drive, f32 value, f64 age, u8 result = 0, bool released = false, bool reduce = false) {
    return core::GlowStrength(drive, value, age, result, released, reduce);
}

TEST_CASE("a curve maps 0 to 0 and 1 to 1, and its middle as its shape says") {
    constexpr std::array<core::GlowCurve, 5> CURVES{ { core::GlowCurve::Linear,
                                                       core::GlowCurve::EaseIn,
                                                       core::GlowCurve::EaseOut,
                                                       core::GlowCurve::Smooth,
                                                       core::GlowCurve::Sine } };
    for (const core::GlowCurve curve : CURVES) {
        CHECK(core::CurveAt(curve, 0.0F) == doctest::Approx(0.0F));
        CHECK(core::CurveAt(curve, 1.0F) == doctest::Approx(1.0F));
    }
    CHECK(core::CurveAt(core::GlowCurve::Linear, QUARTER) == QUARTER);
    CHECK(core::CurveAt(core::GlowCurve::EaseIn, HALF) == QUARTER);
    CHECK(core::CurveAt(core::GlowCurve::EaseOut, HALF) == THREE_QUARTERS);
    CHECK(core::CurveAt(core::GlowCurve::Smooth, HALF) == HALF);
    CHECK(core::CurveAt(core::GlowCurve::Sine, HALF) == doctest::Approx(HALF));
}

TEST_CASE("a curve holds outside 0 to 1 at its ends") {
    CHECK(core::CurveAt(core::GlowCurve::Linear, -1.0F) == 0.0F);
    CHECK(core::CurveAt(core::GlowCurve::EaseIn, TWO) == 1.0F);
}

TEST_CASE("a steady glow is at full strength whatever its inputs") {
    const core::GlowDrive steady{};
    CHECK(Strength(steady, 0.0F, 0.0) == 1.0F);
    CHECK(Strength(steady, THREE_TENTHS, HALF_BEAT_TICKS, 2, true) == 1.0F);
}

TEST_CASE("a pulse starts at full strength, dips to its depth at half a beat and is back after a beat") {
    CHECK(Strength(PULSE, 0.0F, 0.0) == doctest::Approx(1.0F));
    CHECK(Strength(PULSE, 0.0F, QUARTER_BEAT_TICKS) == doctest::Approx(PULSE_AT_QUARTER_BEAT));
    CHECK(Strength(PULSE, 0.0F, HALF_BEAT_TICKS) == doctest::Approx(PULSE_AT_HALF_BEAT));
    CHECK(Strength(PULSE, 0.0F, FULL_BEAT_TICKS) == doctest::Approx(1.0F));
}

TEST_CASE("a pulse far into a long session has the phase it would near tick 0") {
    CHECK(Strength(PULSE, 0.0F, FAR_TICKS) == doctest::Approx(PULSE_AT_QUARTER_BEAT));
}

TEST_CASE("reducing flashing holds a pulse at its average strength at every age") {
    CHECK(Strength(PULSE, 0.0F, 0.0, 0, false, true) == doctest::Approx(PULSE_AT_QUARTER_BEAT));
    CHECK(Strength(PULSE, 0.0F, HALF_BEAT_TICKS, 0, false, true) == doctest::Approx(PULSE_AT_QUARTER_BEAT));
}

TEST_CASE("reducing flashing holds an eased pulse at its own curve's average, not halfway") {
    constexpr core::GlowDrive EASE_IN{
        .kind = core::DriveKind::Pulse, .periodSeconds = 1.6F, .depth = 0.9F, .curve = core::GlowCurve::EaseIn
    };
    constexpr core::GlowDrive EASE_OUT{
        .kind = core::DriveKind::Pulse, .periodSeconds = 1.6F, .depth = 0.9F, .curve = core::GlowCurve::EaseOut
    };
    CHECK(Strength(EASE_IN, 0.0F, 0.0, 0, false, true) == doctest::Approx(EASE_IN_AVERAGE));
    CHECK(Strength(EASE_OUT, 0.0F, HALF_BEAT_TICKS, 0, false, true) == doctest::Approx(EASE_OUT_AVERAGE));
}

TEST_CASE("reducing flashing leaves a steady glow alone") {
    CHECK(Strength(core::GlowDrive{}, 0.0F, 0.0, 0, false, true) == 1.0F);
}

TEST_CASE("a charge while charging is the value through its curve, and at release it starts from there") {
    CHECK(Strength(CHARGE, HALF, 0.0) == QUARTER);
    CHECK(Strength(CHARGE, HALF, 0.0, 0, true) == QUARTER);
}

TEST_CASE("a charge fades to nothing over its fade after release") {
    CHECK(Strength(CHARGE, HALF, HALF_FADE_TICKS, 0, true) == doctest::Approx(CHARGE_FADING));
    CHECK(Strength(CHARGE, HALF, PAST_FADE_TICKS, 0, true) == doctest::Approx(0.0F));
}

TEST_CASE("a progress is the value through its curve until a result, then full strength") {
    CHECK(Strength(PROGRESS, THREE_TENTHS, 0.0) == THREE_TENTHS);
    CHECK(Strength(PROGRESS, THREE_TENTHS, 0.0, 2) == 1.0F);
}

TEST_CASE("the glow roles name every role but none, and a look is valid only within its ranges") {
    CHECK(core::GLOW_ROLE_KEYS.at(std::to_underlying(core::GlowRole::None)).empty());
    CHECK(core::GLOW_ROLE_KEYS.at(std::to_underlying(core::GlowRole::FireBolt)) == "fire_bolt");
    core::GlowLook look{};
    CHECK(look.Valid());
    look.size = TOO_BIG;
    CHECK_FALSE(look.Valid());
    look = core::GlowLook{};
    look.drive.periodSeconds = TOO_FAST;
    CHECK_FALSE(look.Valid());
    look.drive.periodSeconds = FAST_ENOUGH;
    CHECK(look.Valid());
}

} // namespace
