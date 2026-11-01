#pragma once

/// @file
/// @ingroup core
/// Deterministic maths for the simulation (design doc 0002 §7.4): transcendental functions computed
/// from IEEE 754's exactly rounded operations only, so one binary gives the same results on every
/// machine. The platform's maths libraries pick code paths by CPU and change with OS and library
/// versions; `lint` fails on `std::sin` and the like in `core` and `game`. `std::sqrt`, the basic
/// operations, `std::round`, `std::fmod`, `std::ldexp` and `std::frexp` are exact and stay. Also
/// the floating-point environment this depends on.
///
/// Accuracy is within a few units in the last place of `f64` (Pow(): see there); the `f32`
/// overloads compute in `f64` and round once.

#include "core/types.hpp"

namespace core::math {

/// The sine of `radians`, for up to 2^20 quarter turns (about 1.6 million radians) either way;
/// NaN beyond, where the argument reduction would lose its accuracy.
[[nodiscard]] f64 Sin(f64 radians);
/// The cosine of `radians`, for up to 2^20 quarter turns (about 1.6 million radians) either way;
/// NaN beyond, where the argument reduction would lose its accuracy.
[[nodiscard]] f64 Cos(f64 radians);
/// e raised to `power`; 0 below about -745, infinity above about 709.8.
[[nodiscard]] f64 Exp(f64 power);
/// The natural logarithm of `value`: NaN below 0, minus infinity at 0.
[[nodiscard]] f64 Log(f64 value);
/// `base` raised to `exponent`, as Exp(exponent × Log(base)), so its relative error grows with
/// |exponent × Log(base)| (about 1e-13 at 1e3). A negative `base` needs an integer `exponent`
/// (NaN otherwise).
[[nodiscard]] f64 Pow(f64 base, f64 exponent);
/// The angle in radians, in [-π, π], from the positive x axis to the point (`pointX`, `pointY`);
/// 0 at the origin.
[[nodiscard]] f64 Atan2(f64 pointY, f64 pointX);

/// Sin() in `f32`.
[[nodiscard]] inline f32 Sin(f32 radians) {
    return static_cast<f32>(Sin(static_cast<f64>(radians)));
}
/// Cos() in `f32`.
[[nodiscard]] inline f32 Cos(f32 radians) {
    return static_cast<f32>(Cos(static_cast<f64>(radians)));
}
/// Exp() in `f32`.
[[nodiscard]] inline f32 Exp(f32 power) {
    return static_cast<f32>(Exp(static_cast<f64>(power)));
}
/// Log() in `f32`.
[[nodiscard]] inline f32 Log(f32 value) {
    return static_cast<f32>(Log(static_cast<f64>(value)));
}
/// Pow() in `f32`.
[[nodiscard]] inline f32 Pow(f32 base, f32 exponent) {
    return static_cast<f32>(Pow(static_cast<f64>(base), static_cast<f64>(exponent)));
}
/// Atan2() in `f32`.
[[nodiscard]] inline f32 Atan2(f32 pointY, f32 pointX) {
    return static_cast<f32>(Atan2(static_cast<f64>(pointY), static_cast<f64>(pointX)));
}

/// Sets the calling thread's floating-point environment to what the simulation assumes: round to
/// nearest, subnormal numbers kept (no flush-to-zero or denormals-are-zero). The environment is
/// per thread; call this first on every thread that runs simulation code.
void SetFloatingPointEnvironment();

/// Whether the calling thread's floating-point environment is still the one
/// SetFloatingPointEnvironment() sets: a library may change it.
[[nodiscard]] bool FloatingPointEnvironmentIsSet();

} // namespace core::math
