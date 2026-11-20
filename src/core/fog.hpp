#pragma once

/// @file
/// @ingroup core
/// The fog of war's rules on the world map: which tiles the player has seen (a bitset, one bit per tile, row by row) and
/// which lie in sight of a tile. Distance is Manhattan: a radius covers a diamond.

#include "core/types.hpp"
#include "core/world_map.hpp"

#include <cstddef>
#include <span>

namespace core {

/// The words of 64 bits a bitset of `tiles` tiles needs.
[[nodiscard]] constexpr std::size_t FogWords(std::size_t tiles) {
    constexpr std::size_t WORD_BITS = 64;
    return (tiles + WORD_BITS - 1) / WORD_BITS;
}

/// Whether `tile`'s bit is set in `bits`; false for a tile past the bitset.
[[nodiscard]] bool IsExplored(std::span<const u64> bits, u32 tile);

/// The Manhattan distance between two tiles, in tiles.
[[nodiscard]] u32 TileDistance(TilePoint first, TilePoint second);

/// Whether `tile` lies within `radius` tiles of `from`, by TileDistance().
[[nodiscard]] bool InSight(const WorldMap& map, u32 from, u32 tile, u32 radius);

/// Sets the bit of every tile within `radius` of `centre`, clipped to the map, and returns how many were not set before.
/// `bits` must hold exactly the map's tiles' words.
u32 ExploreAround(std::span<u64> bits, const WorldMap& map, u32 centre, u32 radius);

/// Whether `bits` could be a map of `tiles` tiles's fog: the right number of words, and no bit set past the last tile.
[[nodiscard]] bool FogBitsFit(std::span<const u64> bits, std::size_t tiles);

} // namespace core
