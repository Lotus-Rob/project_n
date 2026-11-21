#pragma once

/// @file
/// @ingroup gfx
/// The draws that fix how a tile of the world map looks: a hash of its column and row, so every view that finds the tile
/// draws it alike and nothing reshuffles as the camera moves.

#include "core/random.hpp"
#include "core/types.hpp"

namespace gfx {

/// The seed of the map's tile draws: that of game/scenes/world.cpp's old noise (game can't be seen from gfx).
inline constexpr u64 TERRAIN_SEED = 0x5eed;
/// The purpose of the map's tile draws (game/random_purposes.hpp keeps the number, 9, retired).
inline constexpr u32 TERRAIN_PURPOSE = 9;
/// The draw of a tile's variant: the first draw a tile ever had.
inline constexpr u32 VARIANT_DRAW = 2;

/// The key of the tile at `column` and `row` in a draw.
[[nodiscard]] constexpr u64 TileKey(u32 column, u32 row) {
    constexpr u32 COLUMN_SHIFT = 32;
    return (u64{ column } << COLUMN_SHIFT) | row;
}

/// The tile's `draw`th number as a fraction from 0 up to but never 1: the same for the tile whichever view finds it.
[[nodiscard]] constexpr f32 TileUnit(u32 column, u32 row, u32 draw) {
    constexpr u32 UNUSED_BITS = 64 - 24;
    constexpr f32 TWO_TO_MINUS_24 = 0x1.0p-24F;
    const u64 random = core::Random(TERRAIN_SEED, 0, TileKey(column, row), TERRAIN_PURPOSE, draw);
    return static_cast<f32>(random >> UNUSED_BITS) * TWO_TO_MINUS_24;
}

} // namespace gfx
