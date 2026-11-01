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
constexpr u32 BYTE = 8;
constexpr u32 UNORM16_MAX = 0xFFFFU;
constexpr f32 UNORM8_MAX = 255.0F;

// Ties to even, IEEE 754's default: a half packed here has the bits a hardware f32-to-f16 conversion gives.
constexpr u32 RoundShift(u32 value, u32 shift) {
    const u32 kept = value >> shift;
    const u32 rest = value & ((1U << shift) - 1U);
    const u32 halfway = 1U << (shift - 1U);
    return kept + static_cast<u32>(rest > halfway || (rest == halfway && (kept & 1U) != 0));
}

u32 PackUnorm8(f32 value) {
    return static_cast<u32>(std::lround(std::clamp(value, 0.0F, 1.0F) * UNORM8_MAX));
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

u16 PackUnorm16(f32 value) {
    return static_cast<u16>(std::lround(std::clamp(value, 0.0F, 1.0F) * static_cast<f32>(UNORM16_MAX)));
}

u32 PackColour(f32 red, f32 green, f32 blue, f32 alpha) {
    return PackUnorm8(red * alpha) | (PackUnorm8(green * alpha) << BYTE) | (PackUnorm8(blue * alpha) << (2 * BYTE)) |
           (PackUnorm8(alpha) << (3 * BYTE));
}

Instance MakeInstance(const SpriteDraw& draw) {
    const atlas::Sprite& sprite = atlas::SPRITES.at(static_cast<std::size_t>(draw.sprite));
    const Vec2 size{ .x = static_cast<f32>(sprite.width), .y = static_cast<f32>(sprite.height) };
    const Vec2 foot{ .x = static_cast<f32>(sprite.footX), .y = static_cast<f32>(sprite.footY) };
    const Vec2 toCentre = ((size * 0.5F) - foot) * draw.scale;
    constexpr auto PAGE = static_cast<f32>(atlas::PAGE_SIZE);
    return {
        .previous = draw.previous + toCentre,
        .current = draw.current + toCentre,
        .size = PackPair(PackHalf(size.x * draw.scale), PackHalf(size.y * draw.scale)),
        .rotation = PackPair(PackHalf(draw.previousRotation), PackHalf(draw.currentRotation)),
        .regionMin = PackPair(PackUnorm16(static_cast<f32>(sprite.x) / PAGE), PackUnorm16(static_cast<f32>(sprite.y) / PAGE)),
        .regionMax = PackPair(
            PackUnorm16(static_cast<f32>(sprite.x + sprite.width) / PAGE),
            PackUnorm16(static_cast<f32>(sprite.y + sprite.height) / PAGE)
        ),
        .layerFlags = PackPair(sprite.page, 0),
        .colour = draw.colour,
    };
}

} // namespace core
