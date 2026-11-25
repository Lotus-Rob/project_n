#pragma once

/// @file
/// Deterministic test maps of 1, 10 and 50 times the shipped test map's area, for tests and benchmarks that need a map
/// of a known size without reading data. Terrain is whole-number value noise, places are drawn on open ground, and each
/// city is joined to its nearest neighbours by a straight road that bridges closed ground with meadow, so every city is
/// joined to the first. Whole numbers and core::Random only: the same map on every machine.

#include "core/random.hpp"
#include "core/types.hpp"
#include "core/world_map.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <expected>
#include <format>
#include <print>
#include <string>
#include <utility>
#include <vector>

namespace test {

/// The size of a synthetic map: its tiles and how many cities and dangers stand on it.
struct SyntheticSize {
    /// The map's width in tiles.
    u32 columns;
    /// The map's height in tiles.
    u32 rows;
    /// How many cities it holds; the first is the start.
    u32 cities;
    /// How many dangers it holds.
    u32 dangers;
};

/// The size of the synthetic map `scale` times the shipped test map's area: only 1, 10 and 50 exist. Any other scale is
/// an error in a constant expression and stops the program otherwise.
[[nodiscard]] constexpr SyntheticSize SyntheticSizeOf(u32 scale) {
    switch (scale) {
        case 1:
            return { .columns = 150, .rows = 85, .cities = 6, .dangers = 10 };
        case 10:
            return { .columns = 474, .rows = 269, .cities = 60, .dangers = 100 };
        case 50:
            return { .columns = 1061, .rows = 601, .cities = 300, .dangers = 500 };
        default:
            std::abort(); // not a constant expression, so a bad scale in one fails to compile
    }
}

namespace synthetic {

// Terrain kinds, by index: the shipped map's names and costs.
constexpr u8 MEADOW = 4;
constexpr std::array<std::pair<const char*, u16>, 9> KINDS{ {
    { "deep_water", core::IMPASSABLE_COST },
    { "shallows", 30 },
    { "sand", 14 },
    { "marsh", 25 },
    { "meadow", 10 },
    { "forest", 16 },
    { "hills", 20 },
    { "rock", 40 },
    { "peaks", core::IMPASSABLE_COST },
} };

// One purpose per use of core::Random, so no two uses share numbers.
constexpr u32 PURPOSE_COARSE_NOISE = 1;
constexpr u32 PURPOSE_FINE_NOISE = 2;
constexpr u32 PURPOSE_CITY_COLUMN = 3;
constexpr u32 PURPOSE_CITY_ROW = 4;
constexpr u32 PURPOSE_DANGER_COLUMN = 5;
constexpr u32 PURPOSE_DANGER_ROW = 6;

constexpr u32 COARSE_CELL = 24; // lattice cell side, in tiles
constexpr u32 FINE_CELL = 6;
constexpr u32 COARSE_WEIGHT = 3;
constexpr u32 FINE_WEIGHT = 1;
constexpr u32 LATTICE_LEVELS = 256; // a lattice value is below this

// Each kind's noise sum is below its bound (the last kind takes the rest), cut from the sums' measured spread so the
// kinds come in about these shares: 12% deep water, 5% shallows, 8% sand, 35% meadow, 20% forest, 10% hills, 5% rock,
// 5% peaks. Marsh has none.
constexpr std::array<u32, 8> KIND_BOUNDS{ 335, 365, 405, 405, 555, 659, 720, 760 };

constexpr u32 DANGER_REACH = 3;
constexpr u32 PEOPLE = 100;
constexpr u16 DANGER_COST = 40;
constexpr f32 TILE_SIZE = 256.0F;

/// Value noise of one octave at tile (`column`, `row`): whole-number lattice values drawn per corner, blended bilinearly
/// in fixed point. The result is below LATTICE_LEVELS.
[[nodiscard]] inline u32 Octave(u64 seed, u32 purpose, u32 cell, u32 columns, u32 column, u32 row) {
    const u32 latticeColumns = (columns / cell) + 2;
    const auto corner = [&](u32 x, u32 y) {
        return core::ToBelow(core::Random(seed, 0, (u64{ y } * latticeColumns) + x, purpose, 0), LATTICE_LEVELS);
    };
    const u32 x = column / cell;
    const u32 y = row / cell;
    const u32 across = column % cell;
    const u32 down = row % cell;
    const u32 top = (corner(x, y) * (cell - across)) + (corner(x + 1, y) * across);
    const u32 bottom = (corner(x, y + 1) * (cell - across)) + (corner(x + 1, y + 1) * across);
    return ((top * (cell - down)) + (bottom * down)) / (cell * cell);
}

/// The kind index a tile's noise sum picks.
[[nodiscard]] inline u8 KindOf(u32 sum) {
    for (u8 kind = 0; kind < KIND_BOUNDS.size(); ++kind) {
        if (sum < KIND_BOUNDS[kind]) {
            return kind;
        }
    }
    return static_cast<u8>(KIND_BOUNDS.size());
}

/// Whether `kind` can be walked.
[[nodiscard]] inline bool Open(u8 kind) {
    return KINDS[kind].second != core::IMPASSABLE_COST;
}

/// The squared straight-line distance between two tiles.
[[nodiscard]] inline u64 Distance(core::TilePoint first, core::TilePoint second) {
    const i64 across = static_cast<i64>(first.column) - static_cast<i64>(second.column);
    const i64 down = static_cast<i64>(first.row) - static_cast<i64>(second.row);
    return static_cast<u64>((across * across) + (down * down));
}

/// Calls `mark` with each tile the road between `first` and `second` covers, as WorldMap::Build marks a two-point
/// polyline: from the endpoint with the lower (row, column), 4-connected, stepping along the axis whose next tile
/// boundary the segment crosses first, the column's on a tie. The two must agree, or the road would cross closed ground
/// the carving missed and Build would refuse the map.
template<typename Mark>
void WalkRoad(core::TilePoint first, core::TilePoint second, const Mark& mark) {
    const bool swap = std::pair{ second.row, second.column } < std::pair{ first.row, first.column };
    const core::TilePoint from = swap ? second : first;
    const core::TilePoint last = swap ? first : second;
    const i64 deltaX = static_cast<i64>(last.column) - static_cast<i64>(from.column);
    const i64 deltaY = static_cast<i64>(last.row) - static_cast<i64>(from.row);
    const i64 stepX = deltaX < 0 ? -1 : 1;
    const i64 stepY = deltaY < 0 ? -1 : 1;
    const i64 lengthX = deltaX < 0 ? -deltaX : deltaX;
    const i64 lengthY = deltaY < 0 ? -deltaY : deltaY;
    i64 column = from.column;
    i64 row = from.row;
    i64 doneX = 0;
    i64 doneY = 0;
    mark(core::TilePoint{ .column = static_cast<u32>(column), .row = static_cast<u32>(row) });
    while (doneX < lengthX || doneY < lengthY) {
        const i64 acrossX = ((2 * doneX) + 1) * lengthY;
        const i64 acrossY = ((2 * doneY) + 1) * lengthX;
        if (doneY >= lengthY || (doneX < lengthX && acrossX <= acrossY)) {
            column += stepX;
            ++doneX;
        }
        else {
            row += stepY;
            ++doneY;
        }
        mark(core::TilePoint{ .column = static_cast<u32>(column), .row = static_cast<u32>(row) });
    }
}

/// A source before it is built, and how many closed tiles its roads turned to meadow.
struct Draft {
    /// What Build takes.
    core::WorldMapSource source;
    /// The closed tiles under a road that became meadow.
    u32 carved = 0;
};

/// The movement profile `key`, walking every kind at its cost and adding `danger` near a danger.
[[nodiscard]] inline core::MovementProfile Profile(std::string key, u16 danger) {
    std::vector<u16> costs;
    for (const auto& kind : KINDS) {
        costs.push_back(kind.second);
    }
    return { .key = std::move(key),
             .ticksPerCost = core::DEFAULT_TICKS_PER_COST,
             .kindCosts = std::move(costs),
             .features = { .danger = danger } };
}

/// The source of the synthetic map: terrain, places, roads, then the carving of closed tiles under the roads.
[[nodiscard]] inline Draft Source(u32 scale, u64 seed) {
    const SyntheticSize size = SyntheticSizeOf(scale);
    Draft draft;
    core::WorldMapSource& source = draft.source;
    source.columns = size.columns;
    source.rows = size.rows;
    source.tileSize = TILE_SIZE;
    source.dangerReach = DANGER_REACH;
    for (const auto& kind : KINDS) {
        source.kinds.push_back({ .name = kind.first, .symbol = ' ', .cost = kind.second });
    }
    source.grid.resize(static_cast<std::size_t>(size.columns) * size.rows);
    for (u32 row = 0; row < size.rows; ++row) {
        for (u32 column = 0; column < size.columns; ++column) {
            const u32 sum = (COARSE_WEIGHT * Octave(seed, PURPOSE_COARSE_NOISE, COARSE_CELL, size.columns, column, row)) +
                            (FINE_WEIGHT * Octave(seed, PURPOSE_FINE_NOISE, FINE_CELL, size.columns, column, row));
            source.grid[(static_cast<std::size_t>(row) * size.columns) + column] = KindOf(sum);
        }
    }

    // A place redraws with the next draw index until its tile is open and free.
    std::vector<bool> taken(source.grid.size(), false);
    const u32 places = size.cities + size.dangers;
    for (u32 index = 0; index < places; ++index) {
        const bool city = index < size.cities;
        for (u32 draw = 0;; ++draw) {
            const core::TilePoint tile{
                .column = core::ToBelow(
                    core::Random(seed, 0, index, city ? PURPOSE_CITY_COLUMN : PURPOSE_DANGER_COLUMN, draw), size.columns
                ),
                .row =
                    core::ToBelow(core::Random(seed, 0, index, city ? PURPOSE_CITY_ROW : PURPOSE_DANGER_ROW, draw), size.rows),
            };
            const std::size_t at = (static_cast<std::size_t>(tile.row) * size.columns) + tile.column;
            if (taken[at] || !Open(source.grid[at])) {
                continue;
            }
            taken[at] = true;
            const std::string key = std::format("{}_{}", city ? "city" : "danger", city ? index : index - size.cities);
            source.places.push_back(
                { .key = key,
                  .kind = city ? core::PlaceKind::City : core::PlaceKind::Danger,
                  .sort = city ? "" : "beast",
                  .tile = tile,
                  .nameKey = key,
                  .name = {},
                  .people = city ? PEOPLE : 0,
                  .start = index == 0 }
            );
            break;
        }
    }

    // Each city to its nearest lower-indexed city and to its nearest city, a road once however many ask for it; the lower
    // index wins a tie, so the first city is joined to all.
    const auto nearest = [&](u32 city, u32 last) {
        u32 best = city == 0 ? 1 : 0;
        for (u32 other = 0; other < last; ++other) {
            if (other != city && Distance(source.places[city].tile, source.places[other].tile) <
                                     Distance(source.places[city].tile, source.places[best].tile)) {
                best = other;
            }
        }
        return best;
    };
    std::vector<std::pair<u32, u32>> joins;
    const auto join = [&](u32 first, u32 second) {
        const std::pair<u32, u32> pair = std::minmax(first, second);
        if (std::ranges::find(joins, pair) == joins.end()) {
            joins.push_back(pair);
        }
    };
    for (u32 city = 0; city < size.cities; ++city) {
        if (city > 0) {
            join(city, nearest(city, city));
        }
        join(city, nearest(city, size.cities));
    }
    for (const auto& [first, second] : joins) {
        const core::TilePoint from = source.places[first].tile;
        const core::TilePoint to = source.places[second].tile;
        source.roads.push_back({ from, to });
        WalkRoad(from, to, [&](core::TilePoint tile) {
            u8& kind = source.grid[(static_cast<std::size_t>(tile.row) * size.columns) + tile.column];
            if (!Open(kind)) {
                kind = MEADOW;
                ++draft.carved;
            }
        });
    }

    source.profiles.push_back(Profile("on_foot", 0));
    source.profiles.push_back(Profile("on_foot_wary", DANGER_COST));
    source.travel.reachBase = 300;
    source.travel.reachPerPower = 20;
    source.travel.bands = { { .fromPower = 0.0F, .profile = 1 }, { .fromPower = 5.0F, .profile = 0 } };
    return draft;
}

} // namespace synthetic

/// The synthetic map of `scale` times the test map's area (1, 10 or 50) for `seed`: the same map for the same pair.
/// Stops the program if it can't be built, a fault of this support code, as ShippedWorld() does.
[[nodiscard]] inline core::WorldMap SyntheticMap(u32 scale, u64 seed) {
    std::expected<core::WorldMap, core::Error> built = core::WorldMap::Build(synthetic::Source(scale, seed).source);
    if (!built) {
        std::println(stderr, "synthetic map refused: {}", built.error().message);
        std::abort();
    }
    return std::move(*built);
}

} // namespace test
