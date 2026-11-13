#include "core/glow.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/math.hpp"
#include "core/tick_clock.hpp"

namespace core {

namespace {

constexpr f64 TICKS_A_SECOND = static_cast<f64>(TICKS_PER_SECOND);
constexpr f32 GLOW_HALF = 0.5F;
constexpr f32 TWO = 2.0F;
constexpr f32 THREE = 3.0F;
constexpr f32 ONE_THIRD = 1.0F / 3.0F;
constexpr f32 TWO_THIRDS = 2.0F / 3.0F;

// The mean of `CurveAt(curve, t)` over t from 0 to 1: a pulse's beat sweeps t evenly, so this is its average strength.
f32 CurveMean(GlowCurve curve) {
    if (curve == GlowCurve::EaseIn) {
        return ONE_THIRD;
    }
    if (curve == GlowCurve::EaseOut) {
        return TWO_THIRDS;
    }
    return GLOW_HALF;
}

} // namespace

f32 CurveAt(GlowCurve curve, f32 fraction) {
    const f32 held = std::clamp(fraction, 0.0F, 1.0F);
    if (curve == GlowCurve::EaseIn) {
        return held * held;
    }
    if (curve == GlowCurve::EaseOut) {
        return 1.0F - ((1.0F - held) * (1.0F - held));
    }
    if (curve == GlowCurve::Smooth) {
        return held * held * (THREE - (TWO * held));
    }
    if (curve == GlowCurve::Sine) {
        return (1.0F - math::Cos(std::numbers::pi_v<f32> * held)) * GLOW_HALF;
    }
    return held;
}

f32 GlowStrength(const GlowDrive& drive, f32 value, f64 ageTicks, u8 result, bool released, bool reduceFlashing) {
    if (drive.kind == DriveKind::Pulse) {
        if (reduceFlashing) {
            return 1.0F - (drive.depth * (1.0F - CurveMean(drive.curve)));
        }
        // The phase is taken in f64 so a start tick far back loses nothing.
        const f64 beats = ageTicks / (static_cast<f64>(drive.periodSeconds) * TICKS_A_SECOND);
        const f32 phase = static_cast<f32>(beats - std::floor(beats));
        const f32 away = std::abs((TWO * phase) - 1.0F);
        return 1.0F - (drive.depth * (1.0F - CurveAt(drive.curve, away)));
    }
    if (drive.kind == DriveKind::Charge) {
        const f32 charged = CurveAt(drive.curve, value);
        if (!released) {
            return charged;
        }
        const f64 faded = std::min(ageTicks / (static_cast<f64>(drive.fadeSeconds) * TICKS_A_SECOND), 1.0);
        return charged * (1.0F - CurveAt(drive.fadeCurve, static_cast<f32>(faded)));
    }
    if (drive.kind == DriveKind::Progress) {
        return result == 0 ? CurveAt(drive.curve, value) : 1.0F;
    }
    return 1.0F;
}

} // namespace core
