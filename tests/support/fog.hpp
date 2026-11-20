#pragma once

/// @file
/// Reading what a game::Host has explored of the world map, and the explored bits tests expect, worked out tile by tile
/// rather than by the fog's own rules (core/fog.hpp), so an expectation never repeats the code it checks.

#include "core/fog.hpp"
#include "core/types.hpp"
#include "core/world_map.hpp"
#include "game/host.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <span>
#include <vector>

namespace test {

/// The explored tiles of `host`'s game world, copied.
[[nodiscard]] inline std::vector<u64> ExploredOf(const game::Host& host) {
    const std::span<const u64> explored = host.ExploredTiles();
    return { explored.begin(), explored.end() };
}

/// How many tiles `bits` holds explored.
[[nodiscard]] inline u32 ExploredCount(std::span<const u64> bits) {
    u32 count = 0;
    for (const u64 word : bits) {
        count += static_cast<u32>(std::popcount(word));
    }
    return count;
}

/// The bits of every tile of `map` within `radius` tiles of any of `centres`, by Manhattan distance.
[[nodiscard]] inline std::vector<u64> Diamonds(const core::WorldMap& map, std::span<const u32> centres, u32 radius) {
    constexpr u32 WORD_BITS = 64;
    const u32 tiles = map.Columns() * map.Rows();
    std::vector<u64> bits(core::FogWords(tiles), 0);
    const auto gap = [](u32 first, u32 second) { return first > second ? first - second : second - first; };
    for (u32 tile = 0; tile < tiles; ++tile) {
        const core::TilePoint point = map.PointOf(tile);
        const bool seen = std::ranges::any_of(centres, [&](u32 centre) {
            const core::TilePoint from = map.PointOf(centre);
            return gap(from.column, point.column) + gap(from.row, point.row) <= radius;
        });
        if (seen) {
            bits[tile / WORD_BITS] |= u64{ 1 } << (tile % WORD_BITS);
        }
    }
    return bits;
}

} // namespace test
