#pragma once

/// @file
/// @ingroup core
/// Building sprite instances for the snapshot (design doc 0002 §8.5): the packed 16-bit formats
/// the shader unpacks, and an Instance from a sprite of the atlas manifest and its transform.

#include "core/frame.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"

#include "core/generated/atlas.hpp" // by `project_n atlas`

namespace core {

/// Opaque white, premultiplied: draws a texture's own colours.
inline constexpr u32 OPAQUE_WHITE = 0xFFFF'FFFFU;

/// `value` as an IEEE 754 half-precision float, rounded to nearest, ties to even: out of range
/// becomes infinity, NaN stays NaN, and below the smallest half becomes a subnormal or zero.
[[nodiscard]] u16 PackHalf(f32 value);

/// `value` clamped to [0, 1] as a 16-bit unsigned normalised integer, rounded to nearest.
[[nodiscard]] u16 PackUnorm16(f32 value);

/// Two 16-bit values in one u32: `low` in the low half, as the shader unpacks them.
[[nodiscard]] constexpr u32 PackPair(u16 low, u16 high) {
    constexpr u32 HIGH_HALF = 16;
    return static_cast<u32>(low) | (static_cast<u32>(high) << HIGH_HALF);
}

/// A straight colour and opacity, each channel from 0 to 1, as Instance::colour: premultiplied
/// and rounded to 8 bits.
[[nodiscard]] u32 PackColour(f32 red, f32 green, f32 blue, f32 alpha);

/// One sprite to draw this tick, as the publisher sees it.
struct SpriteDraw {
    /// Which sprite of the atlas manifest.
    atlas::SpriteId sprite = atlas::SpriteId{};
    /// Where its foot point (the manifest's) stood at the previous tick, in world units.
    Vec2 previous{};
    /// Where its foot point stands at this tick.
    Vec2 current{};
    /// How much larger than its image it is drawn.
    f32 scale = 1.0F;
    /// Clockwise rotation about its centre in radians at the previous tick.
    f32 previousRotation = 0.0F;
    /// Clockwise rotation at this tick.
    f32 currentRotation = 0.0F;
    /// Premultiplied colour (PackColour()) the image is multiplied by.
    u32 colour = OPAQUE_WHITE;
};

/// The instance that draws `draw`: its quad placed so the sprite's foot point lands on its
/// position, with the sprite's page and region from the manifest.
[[nodiscard]] Instance MakeInstance(const SpriteDraw& draw);

} // namespace core
