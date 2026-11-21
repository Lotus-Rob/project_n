#pragma once

/// @file
/// @ingroup gfx
/// The fog of war as the renderer sees it: a field of two values per world-map tile, and the sampling both the CPU and
/// the GPU's linear filter do over it, and the uniforms the world map's shaders shade by it. Pure maths, tested without a
/// device.

#include "core/types.hpp"
#include "core/vec2.hpp"
#include "core/world_look.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace gfx {

/// The fog's values for one frame: per tile an explored and an in-sight value, eased by app::FogFade.
struct FogFrame {
    /// The map's tiles across; 0 means no fog, which nothing fades or shades.
    u32 columns = 0;
    /// The map's tiles down.
    u32 rows = 0;
    /// A tile's side in world units.
    f32 tileSize = 0.0F;
    /// Two values per tile in tile order, row by row: explored, then in sight; each 0 to 255.
    // NOLINTNEXTLINE(readability-redundant-member-init): designated initializers omit it; Clang warns without one
    std::span<const u8> values{};
    /// Changes whenever any value does, so a changed field is uploaded once and an unchanged one never.
    u64 revision = 0;
};

/// The channel of FogFrame::values holding whether a tile is explored.
inline constexpr u32 FOG_EXPLORED = 0;
/// The channel of FogFrame::values holding whether a tile is in sight.
inline constexpr u32 FOG_IN_SIGHT = 1;

/// The `channel`'s value at `world`, 0 to 1: bilinear between the tile centres and clamped at the map's edge, exactly as
/// the GPU's linear filter with clamp-to-edge samples a texture with a texel per tile. 1 with no fog.
[[nodiscard]] f32 FogSample(const FogFrame& fog, core::Vec2 world, u32 channel);

/// How far `world` shows through sight, 0 to 1: the in-sight sample eased from nothing at half to full.
[[nodiscard]] f32 SightFade(const FogFrame& fog, core::Vec2 world);

/// The explored value of the tile under `world` alone, 0 to 1, whatever its neighbours; 1 with no fog or off the map.
[[nodiscard]] f32 TileExplored(const FogFrame& fog, core::Vec2 world);

/// What the fragment shaders shade and fade by the fog with: `Fog` in shaders/fog.hlsli, rows of 16 bytes.
struct FogUniforms {
    /// The field's tiles across, then down.
    std::array<f32, 2> mapSize{};
    /// A tile's side in world units.
    f32 tileSize = 0.0F;
    /// 1 while the view has fog; 0 leaves every sprite as it is.
    u32 on = 0;
    /// Linear red, green and blue of the sepia colour over its own luminance, then how far the colour drains.
    std::array<f32, 4> sepia{};
    /// The mist's linear red, green and blue, then how much of it shows.
    std::array<f32, 4> mist{};
    /// The linear red, green and blue under the clouds, then an unused 0.
    std::array<f32, 4> shade{};
};
/// The bytes of FogUniforms, as the shader's constant buffer holds them.
inline constexpr std::size_t FOG_UNIFORM_BYTES = 64;
static_assert(sizeof(FogUniforms) == FOG_UNIFORM_BYTES, "the shader's fog constant buffer is 64 bytes");
/// A row of the shader's constant buffer, where each of FogUniforms' colours starts.
inline constexpr std::size_t FOG_ROW_BYTES = 16;
static_assert(
    offsetof(FogUniforms, sepia) == FOG_ROW_BYTES && offsetof(FogUniforms, mist) == 2 * FOG_ROW_BYTES &&
        offsetof(FogUniforms, shade) == 3 * FOG_ROW_BYTES,
    "the shader's fog constant buffer starts each colour on its own 16-byte row"
);

/// The linear red, green and blue of the sRGB colour `srgb`, each as the 8-bit linear byte core::FromSrgb() gives over 255,
/// as every world map colour reaches its shader.
[[nodiscard]] std::array<f32, 3> LinearOf(const std::array<u8, 3>& srgb);

/// The uniforms that shade by `fog` in `look`'s colours and strengths; off when `fog` has no tiles.
[[nodiscard]] FogUniforms FogUniformsOf(const FogFrame& fog, const core::FogLook& look);

} // namespace gfx
