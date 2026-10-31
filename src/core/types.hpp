#pragma once

/// @file
/// Sized number types for all game and engine code. Use them instead of `int`, `unsigned`,
/// `float` and the like, so a value's size is the same on every platform and visible where it is
/// declared. The precompiled headers include this file; other headers include it themselves.

#include <cstdint>
#include <limits>

/// Signed 8-bit integer.
using i8 = std::int8_t;
/// Signed 16-bit integer.
using i16 = std::int16_t;
/// Signed 32-bit integer.
using i32 = std::int32_t;
/// Signed 64-bit integer.
using i64 = std::int64_t;
/// Unsigned 8-bit integer.
using u8 = std::uint8_t;
/// Unsigned 16-bit integer.
using u16 = std::uint16_t;
/// Unsigned 32-bit integer.
using u32 = std::uint32_t;
/// Unsigned 64-bit integer.
using u64 = std::uint64_t;
/// 32-bit IEEE 754 floating point.
using f32 = float;
/// 64-bit IEEE 754 floating point.
using f64 = double;

static_assert(sizeof(f32) == sizeof(u32) && std::numeric_limits<f32>::is_iec559, "f32 must be a 32-bit IEEE 754 float");
static_assert(sizeof(f64) == sizeof(u64) && std::numeric_limits<f64>::is_iec559, "f64 must be a 64-bit IEEE 754 float");
