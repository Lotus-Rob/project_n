#pragma once

/// @file
/// @ingroup gfx
/// The ground field the world map's ground shader reads: per tile, its terrain kind and how far it lies from the shore.
/// Pure maths, tested without a device.

#include "core/types.hpp"
#include "core/world_map.hpp"

#include <span>
#include <vector>

namespace gfx {

/// The farthest a shore distance is told apart, in tiles; farther reads as this.
inline constexpr f32 MAX_SHORE_TILES = 4.0F;

/// The byte for a shore distance of `tiles` (negative in water): `clamp(round(128 + 32 * tiles), 0, 255)`.
[[nodiscard]] u8 ShoreByte(f32 tiles);

/// Fills `into` with two bytes per tile of `map`, row by row: the tile's kind index, then ShoreByte of its signed shore
/// distance. The distance runs between tile centres to the nearest tile of the other wetness, less half a tile, so a
/// shore's two sides read -0.5 (water) and +0.5 (land); it is negative in water and clamped to MAX_SHORE_TILES. A tile
/// is water when its kind index is in `waterKinds`; with no water kind, or no tile of the other wetness, every distance
/// is the clamp.
void GroundTexels(const core::WorldMap& map, std::span<const u8> waterKinds, std::vector<u8>& into);

} // namespace gfx
