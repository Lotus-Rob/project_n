#pragma once

/// @file
/// @ingroup core
/// The fog of war's rules on the world map: which tiles the player has seen (a bitset, one bit per tile, row by row) and
/// which lie in sight of a tile. Distance is Manhattan: a radius covers a diamond.

#include "core/types.hpp"
#include "core/world_map.hpp"

#include <algorithm>
#include <cstddef>
#include <span>

namespace core {

/// The farthest any sight reaches, in tiles: a cap that keeps a typo in the game data from uncovering most of the map at
/// once, and keeps a tile's row or column plus the radius far from wrapping in ExploreAround().
inline constexpr u32 MAX_SIGHT = 64;

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

/// Calls `visit(tile)` with the index of every tile within `radius` of `centre`, clipped to the map, row by row. The
/// diamond ExploreAround() marks and InSight() tests, walked without testing the tiles outside it.
template<typename Visit>
void ForEachInSight(const WorldMap& map, u32 centre, u32 radius, Visit visit) {
    const TilePoint middle = map.PointOf(centre);
    const u32 firstRow = middle.row > radius ? middle.row - radius : 0;
    const u32 lastRow = std::min(map.Rows() - 1, middle.row + radius);
    for (u32 row = firstRow; row <= lastRow; ++row) {
        const u32 reach = radius - (row > middle.row ? row - middle.row : middle.row - row);
        const u32 firstColumn = middle.column > reach ? middle.column - reach : 0;
        const u32 lastColumn = std::min(map.Columns() - 1, middle.column + reach);
        for (u32 column = firstColumn; column <= lastColumn; ++column) {
            visit(map.TileIndex({ .column = column, .row = row }));
        }
    }
}

/// Sets the bit of every tile within `radius` of `centre`, clipped to the map, and returns how many were not set before.
/// `bits` must hold exactly the map's tiles' words.
u32 ExploreAround(std::span<u64> bits, const WorldMap& map, u32 centre, u32 radius);

/// Whether `bits` could be a map of `tiles` tiles's fog: the right number of words, and no bit set past the last tile.
[[nodiscard]] bool FogBitsFit(std::span<const u64> bits, std::size_t tiles);

} // namespace core
