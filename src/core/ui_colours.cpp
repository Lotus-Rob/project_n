#include "core/ui_colours.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/math.hpp"

namespace core {

namespace {

constexpr f64 CHANNEL_MAX = 255.0;
constexpr u32 CHANNEL_MASK = 0xFFU;
constexpr u32 ALPHA_SHIFT = 24;
constexpr u32 GREEN_SHIFT = 8;
constexpr u32 BLUE_SHIFT = 16;
constexpr f64 SRGB_LINEAR_END = 0.04045;
constexpr f64 SRGB_SLOPE = 12.92;
constexpr f64 SRGB_OFFSET = 0.055;
constexpr f64 SRGB_SCALE = 1.055;
constexpr f64 SRGB_GAMMA = 2.4;
constexpr f64 LINEAR_END = 0.0031308;

f64 ToLinear(f64 srgb) {
    return srgb <= SRGB_LINEAR_END ? srgb / SRGB_SLOPE : math::Pow((srgb + SRGB_OFFSET) / SRGB_SCALE, SRGB_GAMMA);
}

f64 ToEncoded(f64 linear) {
    return linear <= LINEAR_END ? linear * SRGB_SLOPE : (SRGB_SCALE * math::Pow(linear, 1.0 / SRGB_GAMMA)) - SRGB_OFFSET;
}

u32 Byte(f64 unit) {
    return static_cast<u32>(std::lround(std::clamp(unit, 0.0, 1.0) * CHANNEL_MAX));
}

} // namespace

u32 FromSrgb(u8 red, u8 green, u8 blue, u8 alpha) {
    const f64 opacity = static_cast<f64>(alpha) / CHANNEL_MAX;
    const auto premultiplied = [opacity](u8 channel) {
        return Byte(ToLinear(static_cast<f64>(channel) / CHANNEL_MAX) * opacity);
    };
    return premultiplied(red) | (premultiplied(green) << GREEN_SHIFT) | (premultiplied(blue) << BLUE_SHIFT) |
           (static_cast<u32>(alpha) << ALPHA_SHIFT);
}

Srgb ToSrgb(u32 packed) {
    const u32 alpha = packed >> ALPHA_SHIFT;
    if (alpha == 0) {
        return { .red = 0, .green = 0, .blue = 0, .alpha = 0 };
    }
    const f64 opacity = static_cast<f64>(alpha) / CHANNEL_MAX;
    const auto straight = [packed, opacity](u32 shift) {
        const f64 linear = static_cast<f64>((packed >> shift) & CHANNEL_MASK) / CHANNEL_MAX / opacity;
        return static_cast<u8>(Byte(ToEncoded(linear)));
    };
    return {
        .red = straight(0), .green = straight(GREEN_SHIFT), .blue = straight(BLUE_SHIFT), .alpha = static_cast<u8>(alpha)
    };
}

u32 ScaledColour(u32 packed, f32 factor) {
    constexpr u32 CHANNEL_BITS = 8;
    constexpr u32 CHANNELS = 4;
    constexpr auto BYTE_MAX = static_cast<f32>(CHANNEL_MAX);
    u32 scaled = 0;
    for (u32 channel = 0; channel < CHANNELS; ++channel) {
        const u32 shift = channel * CHANNEL_BITS;
        const auto value = static_cast<f32>((packed >> shift) & CHANNEL_MASK);
        scaled |= static_cast<u32>(std::lround(std::clamp(value * factor, 0.0F, BYTE_MAX))) << shift;
    }
    return scaled;
}

f32 OpacityOf(u32 packed) {
    return static_cast<f32>(packed >> ALPHA_SHIFT) / static_cast<f32>(CHANNEL_MAX);
}

} // namespace core
