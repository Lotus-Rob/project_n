#include "core/instances.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace core {

namespace {

constexpr u32 SIGN_MASK = 0x8000'0000U;
constexpr u32 INFINITY_BITS = 0x7F80'0000U;
constexpr u32 HALF_INFINITY = 0x7C00U;
constexpr u32 HALF_QUIET_NAN = 0x7E00U;
// The first magnitude that rounds past the largest half (65504) to infinity: 65520.
constexpr u32 HALF_OVERFLOW = 0x477F'F000U;
// 2^-14, the smallest normal half; below it halves are subnormal.
constexpr u32 HALF_MIN_NORMAL = 0x3880'0000U;
// 2^-25: half the smallest subnormal half, which rounds (to even) to zero, like everything below.
constexpr u32 HALF_UNDERFLOW = 0x3300'0000U;
// Rebiases a normal float's exponent (127) to a half's (15), in place in the float's bits.
constexpr u32 EXPONENT_REBIAS = 0x3800'0000U;
constexpr u32 MANTISSA_SHIFT = 13; // 23 mantissa bits down to 10
constexpr u32 FLOAT_MANTISSA = 0x007F'FFFFU;
constexpr u32 IMPLICIT_ONE = 0x0080'0000U;
constexpr u32 FLOAT_EXPONENT_SHIFT = 23;
constexpr u32 SUBNORMAL_BIAS = 126; // a float of biased exponent e is M * 2^(e - 126) smallest subnormals
constexpr u32 UNORM16_MAX = 0xFFFFU;

// Ties to even, IEEE 754's default: a half packed here has the bits a hardware f32-to-f16 conversion gives.
constexpr u32 RoundShift(u32 value, u32 shift) {
    const u32 kept = value >> shift;
    const u32 rest = value & ((1U << shift) - 1U);
    const u32 halfway = 1U << (shift - 1U);
    return kept + static_cast<u32>(rest > halfway || (rest == halfway && (kept & 1U) != 0));
}

} // namespace

u16 PackHalf(f32 value) {
    const u32 bits = std::bit_cast<u32>(value);
    const u32 sign = (bits & SIGN_MASK) >> 16U;
    const u32 magnitude = bits & ~SIGN_MASK;
    if (magnitude >= INFINITY_BITS) {
        return static_cast<u16>(sign | (magnitude > INFINITY_BITS ? HALF_QUIET_NAN : HALF_INFINITY));
    }
    if (magnitude >= HALF_OVERFLOW) {
        return static_cast<u16>(sign | HALF_INFINITY);
    }
    if (magnitude >= HALF_MIN_NORMAL) {
        // A carry out of the mantissa raises the exponent, which is the correct rounding.
        return static_cast<u16>(sign | RoundShift(magnitude - EXPONENT_REBIAS, MANTISSA_SHIFT));
    }
    if (magnitude <= HALF_UNDERFLOW) {
        return static_cast<u16>(sign);
    }
    const u32 mantissa = (magnitude & FLOAT_MANTISSA) | IMPLICIT_ONE;
    return static_cast<u16>(sign | RoundShift(mantissa, SUBNORMAL_BIAS - (magnitude >> FLOAT_EXPONENT_SHIFT)));
}

f32 UnpackHalf(u16 half) {
    constexpr u32 HALF_SIGN = 0x8000U;
    constexpr u32 HALF_EXPONENT_SHIFT = 10;
    constexpr u32 HALF_EXPONENT_MAX = 0x1FU;
    constexpr u32 HALF_MANTISSA = 0x3FFU;
    constexpr i32 SUBNORMAL_EXPONENT = -24; // a subnormal half is its mantissa times 2^-24
    const u32 sign = (half & HALF_SIGN) << 16U;
    const u32 exponent = (half >> HALF_EXPONENT_SHIFT) & HALF_EXPONENT_MAX;
    const u32 mantissa = half & HALF_MANTISSA;
    if (exponent == 0) {
        const f32 magnitude = std::ldexp(static_cast<f32>(mantissa), SUBNORMAL_EXPONENT);
        return sign != 0 ? -magnitude : magnitude;
    }
    if (exponent == HALF_EXPONENT_MAX) {
        return std::bit_cast<f32>(sign | INFINITY_BITS | (mantissa << MANTISSA_SHIFT));
    }
    return std::bit_cast<f32>(sign | ((exponent << FLOAT_EXPONENT_SHIFT) + EXPONENT_REBIAS) | (mantissa << MANTISSA_SHIFT));
}

u16 PackUnorm16(f32 value) {
    return static_cast<u16>(std::lround(std::clamp(value, 0.0F, 1.0F) * static_cast<f32>(UNORM16_MAX)));
}

Instance MakeInstance(const Manifest& manifest, const SpriteDraw& draw) {
    const SpriteInfo& sprite = manifest.Sprite(draw.sprite);
    const Vec2 size{ .x = static_cast<f32>(sprite.width), .y = static_cast<f32>(sprite.height) };
    const Vec2 foot{ .x = static_cast<f32>(sprite.footX), .y = static_cast<f32>(sprite.footY) };
    const Vec2 toCentre = ((size * 0.5F) - foot) * draw.scale;
    const auto page = static_cast<f32>(manifest.pageSize);
    // Within half a turn of zero, and the previous within half a turn of it: the shader's lerp then takes the shorter
    // arc, and the halves keep their precision.
    constexpr f64 TURN = 2 * std::numbers::pi;
    const f64 current = std::remainder(static_cast<f64>(draw.currentRotation), TURN);
    const f64 previous =
        current - std::remainder(static_cast<f64>(draw.currentRotation) - static_cast<f64>(draw.previousRotation), TURN);
    return {
        .previous = draw.previous + toCentre,
        .current = draw.current + toCentre,
        .size = PackPair(PackHalf(size.x * draw.scale), PackHalf(size.y * draw.scale)),
        .rotation = PackPair(PackHalf(static_cast<f32>(previous)), PackHalf(static_cast<f32>(current))),
        .regionMin = PackPair(PackUnorm16(static_cast<f32>(sprite.x) / page), PackUnorm16(static_cast<f32>(sprite.y) / page)),
        .regionMax = PackPair(
            PackUnorm16(static_cast<f32>(sprite.x + sprite.width) / page),
            PackUnorm16(static_cast<f32>(sprite.y + sprite.height) / page)
        ),
        .layerFlags = PackPair(sprite.page, 0),
        .colour = draw.colour,
    };
}

} // namespace core
