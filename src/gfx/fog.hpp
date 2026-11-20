#pragma once

/// @file
/// @ingroup gfx
/// The fog of war as the renderer sees it: a field of two values per world-map tile, and the sampling both the CPU and
/// the GPU's linear filter do over it. Pure maths, tested without a device.

#include "core/types.hpp"
#include "core/vec2.hpp"

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

} // namespace gfx
