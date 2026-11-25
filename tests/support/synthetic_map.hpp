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
#include <cstdio>
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

/// How a synthetic map's ground is made.
enum class SyntheticTerrain : u8 {
    Noise,  ///< Two octaves of value noise, with roads between the cities.
    Meadow, ///< Meadow everywhere and no roads.
    Coarse, ///< Only the coarse octave, so the biomes are wide, with roads, and every danger reaches 16 tiles.
};

/// What a synthetic map varies from its scale's default.
struct SyntheticOptions {
    /// How many places it holds, cities and dangers in the shares its scale has (at least 2 cities); 0 for the scale's.
    u32 places = 0;
    /// How its ground is made.
    SyntheticTerrain terrain = SyntheticTerrain::Noise;
};

/// The scales a synthetic map exists at, in times the shipped test map's area.
inline constexpr std::array<u32, 3> SYNTHETIC_SCALES{ { 1, 10, 50 } };

/// The size of the synthetic map `scale` times the shipped test map's area: only SYNTHETIC_SCALES exist. Any other scale
/// is an error in a constant expression and stops the program otherwise.
[[nodiscard]] constexpr SyntheticSize SyntheticSizeOf(u32 scale) {
    constexpr std::array<SyntheticSize, SYNTHETIC_SCALES.size()> SIZES{ {
        { .columns = 150, .rows = 85, .cities = 6, .dangers = 10 },
        { .columns = 474, .rows = 269, .cities = 60, .dangers = 100 },
        { .columns = 1061, .rows = 601, .cities = 300, .dangers = 500 },
    } };
    for (std::size_t i = 0; i < SYNTHETIC_SCALES.size(); ++i) {
        if (SYNTHETIC_SCALES.at(i) == scale) {
            return SIZES.at(i);
        }
    }
    // Not a constant expression, so a bad scale in one fails to compile.
    std::println(stderr, "no synthetic map of scale {}", scale);
    std::abort();
}

/// `size` with `places` places in its shares of cities and dangers, at least 2 of them cities; `size` itself for 0.
[[nodiscard]] constexpr SyntheticSize WithPlaces(SyntheticSize size, u32 places) {
    if (places == 0) {
        return size;
    }
    const u32 cities = std::clamp(places * size.cities / (size.cities + size.dangers), 2U, std::max(places, 2U));
    return { .columns = size.columns, .rows = size.rows, .cities = cities, .dangers = places > cities ? places - cities : 0 };
}

namespace synthetic {

/// The kind index of meadow, which closed ground under a road becomes.
constexpr u8 MEADOW = 4;
/// A terrain kind's name and the cost of entering its tiles.
struct KindSpec {
    /// Its name.
    const char* name;
    /// Its cost, or core::IMPASSABLE_COST.
    u16 cost;
};
/// The terrain kinds, by index: the shipped map's names and costs.
constexpr std::array<KindSpec, 9> KINDS{ {
    { .name = "deep_water", .cost = core::IMPASSABLE_COST },
    { .name = "shallows", .cost = 30 },
    { .name = "sand", .cost = 14 },
    { .name = "marsh", .cost = 25 },
    { .name = "meadow", .cost = 10 },
    { .name = "forest", .cost = 16 },
    { .name = "hills", .cost = 20 },
    { .name = "rock", .cost = 40 },
    { .name = "peaks", .cost = core::IMPASSABLE_COST },
} };

/// The purpose of the draws of the coarse noise octave's lattice; each use of core::Random has a purpose of its own, so no
/// two uses share numbers.
constexpr u32 PURPOSE_COARSE_NOISE = 1;
/// The purpose of the draws of the fine noise octave's lattice.
constexpr u32 PURPOSE_FINE_NOISE = 2;
/// The purpose of the draws of a city's column.
constexpr u32 PURPOSE_CITY_COLUMN = 3;
/// The purpose of the draws of a city's row.
constexpr u32 PURPOSE_CITY_ROW = 4;
/// The purpose of the draws of a danger's column.
constexpr u32 PURPOSE_DANGER_COLUMN = 5;
/// The purpose of the draws of a danger's row.
constexpr u32 PURPOSE_DANGER_ROW = 6;

/// The side, in tiles, of the coarse octave's lattice cell.
constexpr u32 COARSE_CELL = 24;
/// The side, in tiles, of the fine octave's lattice cell.
constexpr u32 FINE_CELL = 6;
/// What the coarse octave adds to a tile's noise sum, per lattice level.
constexpr u32 COARSE_WEIGHT = 3;
/// What the fine octave adds to a tile's noise sum, per lattice level.
constexpr u32 FINE_WEIGHT = 1;
/// The number of values a lattice corner may take: each is below this.
constexpr u32 LATTICE_LEVELS = 256;

/// Each kind's noise sum is below its bound (the last kind takes the rest), cut from the sums' measured spread so the
/// kinds come in about these shares: 12% deep water, 5% shallows, 8% sand, 35% meadow, 20% forest, 10% hills, 5% rock,
/// 5% peaks. Marsh has none.
constexpr std::array<u32, 8> KIND_BOUNDS{ { 335, 365, 405, 405, 555, 659, 720, 760 } };

/// How far, in tiles, a danger's reach flags tiles around it.
constexpr u32 DANGER_REACH = 3;
/// How far a danger reaches on a map of wide biomes.
constexpr u32 WIDE_DANGER_REACH = 16;
/// How many people a city homes.
constexpr u32 PEOPLE = 100;
/// What a tile near a danger adds to its cost for the wary profile.
constexpr u16 DANGER_COST = 40;
/// A tile's side in world units.
constexpr f32 TILE_SIZE = 256.0F;

/// Value noise of one octave at tile (`column`, `row`): whole-number lattice values drawn per corner, blended bilinearly
/// in fixed point. The result is below LATTICE_LEVELS.
[[nodiscard]] inline u32 Octave(u64 seed, u32 purpose, u32 cell, u32 columns, u32 column, u32 row) {
    const u32 latticeColumns = (columns / cell) + 2;
    const auto corner = [&](u32 latticeColumn, u32 latticeRow) {
        return core::ToBelow(
            core::Random(seed, 0, (u64{ latticeRow } * latticeColumns) + latticeColumn, purpose, 0), LATTICE_LEVELS
        );
    };
    const u32 cellColumn = column / cell;
    const u32 cellRow = row / cell;
    const u32 across = column % cell;
    const u32 down = row % cell;
    const u32 top = (corner(cellColumn, cellRow) * (cell - across)) + (corner(cellColumn + 1, cellRow) * across);
    const u32 bottom = (corner(cellColumn, cellRow + 1) * (cell - across)) + (corner(cellColumn + 1, cellRow + 1) * across);
    return ((top * (cell - down)) + (bottom * down)) / (cell * cell);
}

/// The kind index a tile's noise sum picks.
[[nodiscard]] inline u8 KindOf(u32 sum) {
    std::size_t kind = 0;
    while (kind < KIND_BOUNDS.size() && sum >= KIND_BOUNDS[kind]) {
        ++kind;
    }
    return static_cast<u8>(kind);
}

/// Whether `kind` can be walked.
[[nodiscard]] inline bool Open(u8 kind) {
    return KINDS[kind].cost != core::IMPASSABLE_COST;
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

/// The index in the source's grid of `tile`.
[[nodiscard]] inline std::size_t GridIndex(const core::WorldMapSource& source, core::TilePoint tile) {
    return (static_cast<std::size_t>(tile.row) * source.columns) + tile.column;
}

/// The movement profile `key`, walking every kind at its cost and adding `danger` near a danger.
[[nodiscard]] inline core::MovementProfile Profile(std::string key, u16 danger) {
    std::vector<u16> costs(KINDS.size());
    std::ranges::transform(KINDS, costs.begin(), &KindSpec::cost);
    return { .key = std::move(key),
             .ticksPerCost = core::DEFAULT_TICKS_PER_COST,
             .kindCosts = std::move(costs),
             .features = { .danger = danger } };
}

/// The kind index of the tile (`column`, `row`) of `terrain`.
[[nodiscard]] inline u8 TileKind(SyntheticTerrain terrain, u64 seed, const SyntheticSize& size, u32 column, u32 row) {
    if (terrain == SyntheticTerrain::Meadow) {
        return MEADOW;
    }
    const u32 coarse = Octave(seed, PURPOSE_COARSE_NOISE, COARSE_CELL, size.columns, column, row);
    if (terrain == SyntheticTerrain::Coarse) {
        // The kind bounds are cut for a sum of weights COARSE_WEIGHT + FINE_WEIGHT over two octaves of the same range.
        return KindOf((COARSE_WEIGHT + FINE_WEIGHT) * coarse);
    }
    return KindOf(
        (COARSE_WEIGHT * coarse) + (FINE_WEIGHT * Octave(seed, PURPOSE_FINE_NOISE, FINE_CELL, size.columns, column, row))
    );
}

/// Fills `source`'s size, kinds and grid: each tile's kind is picked as `terrain` says.
inline void MakeTerrain(core::WorldMapSource& source, const SyntheticSize& size, u64 seed, SyntheticTerrain terrain) {
    source.columns = size.columns;
    source.rows = size.rows;
    source.tileSize = TILE_SIZE;
    source.dangerReach = terrain == SyntheticTerrain::Coarse ? WIDE_DANGER_REACH : DANGER_REACH;
    for (const auto& kind : KINDS) {
        source.kinds.push_back({ .name = kind.name, .symbol = ' ', .cost = kind.cost });
    }
    source.grid.resize(static_cast<std::size_t>(size.columns) * size.rows);
    for (u32 row = 0; row < size.rows; ++row) {
        for (u32 column = 0; column < size.columns; ++column) {
            source.grid[GridIndex(source, { .column = column, .row = row })] = TileKind(terrain, seed, size, column, row);
        }
    }
}

/// What differs between a city and a danger when one is placed.
struct PlaceSpec {
    /// The place's kind.
    core::PlaceKind kind;
    /// The start of its key, before its number.
    const char* keyPrefix;
    /// A danger's sort; empty for a city.
    const char* sort;
    /// How many people it homes.
    u32 people;
    /// The purpose of the draws of its column.
    u32 columnPurpose;
    /// The purpose of the draws of its row.
    u32 rowPurpose;
};

/// A city.
constexpr PlaceSpec CITY{ .kind = core::PlaceKind::City,
                          .keyPrefix = "city",
                          .sort = "",
                          .people = PEOPLE,
                          .columnPurpose = PURPOSE_CITY_COLUMN,
                          .rowPurpose = PURPOSE_CITY_ROW };
/// A danger.
constexpr PlaceSpec DANGER{ .kind = core::PlaceKind::Danger,
                            .keyPrefix = "danger",
                            .sort = "beast",
                            .people = 0,
                            .columnPurpose = PURPOSE_DANGER_COLUMN,
                            .rowPurpose = PURPOSE_DANGER_ROW };

/// Adds place number `number` of its kind, drawn by `index` among all the places, to `source`: it redraws with the next
/// draw index until its tile is open and no other place holds it (`taken`).
inline void PlaceOne(
    core::WorldMapSource& source,
    std::vector<bool>& taken,
    const PlaceSpec& spec,
    u64 seed,
    u32 index,
    u32 number
) {
    for (u32 draw = 0;; ++draw) {
        const core::TilePoint tile{
            .column = core::ToBelow(core::Random(seed, 0, index, spec.columnPurpose, draw), source.columns),
            .row = core::ToBelow(core::Random(seed, 0, index, spec.rowPurpose, draw), source.rows),
        };
        const std::size_t tileIndex = GridIndex(source, tile);
        if (taken[tileIndex] || !Open(source.grid[tileIndex])) {
            continue;
        }
        taken[tileIndex] = true;
        const std::string key = std::format("{}_{}", spec.keyPrefix, number);
        source.places.push_back(
            { .key = key,
              .kind = spec.kind,
              .sort = spec.sort,
              .tile = tile,
              .nameKey = key,
              .name = {},
              .people = spec.people,
              .start = index == 0 }
        );
        return;
    }
}

/// Adds the cities, then the dangers, to `source`.
inline void MakePlaces(core::WorldMapSource& source, const SyntheticSize& size, u64 seed) {
    std::vector<bool> taken(source.grid.size(), false);
    for (u32 city = 0; city < size.cities; ++city) {
        PlaceOne(source, taken, CITY, seed, city, city);
    }
    for (u32 danger = 0; danger < size.dangers; ++danger) {
        PlaceOne(source, taken, DANGER, seed, size.cities + danger, danger);
    }
}

/// The city among the first `last` cities nearest to `city` (other than itself), the lower index on a tie.
[[nodiscard]] inline u32 NearestCity(const core::WorldMapSource& source, u32 city, u32 last) {
    u32 best = city == 0 ? 1 : 0;
    for (u32 other = 0; other < last; ++other) {
        if (other != city && Distance(source.places[city].tile, source.places[other].tile) <
                                 Distance(source.places[city].tile, source.places[best].tile)) {
            best = other;
        }
    }
    return best;
}

/// The pairs of cities a road joins: each city to its nearest lower-indexed city and to its nearest city, each pair once
/// however many ask for it, so the first city is joined to all.
[[nodiscard]] inline std::vector<std::pair<u32, u32>> RoadPairs(const core::WorldMapSource& source, u32 cities) {
    std::vector<std::pair<u32, u32>> pairs;
    const auto add = [&](u32 first, u32 second) {
        const std::pair<u32, u32> pair = std::minmax(first, second);
        if (std::ranges::find(pairs, pair) == pairs.end()) {
            pairs.push_back(pair);
        }
    };
    for (u32 city = 0; city < cities; ++city) {
        if (city > 0) {
            add(city, NearestCity(source, city, city));
        }
        add(city, NearestCity(source, city, cities));
    }
    return pairs;
}

/// Adds the roads, one straight line of two points each, and turns the closed tiles under them to meadow; returns how
/// many it turned.
inline u32 MakeRoads(core::WorldMapSource& source, u32 cities) {
    u32 carved = 0;
    for (const auto& [first, second] : RoadPairs(source, cities)) {
        const core::TilePoint start = source.places[first].tile;
        const core::TilePoint end = source.places[second].tile;
        source.roads.push_back({ start, end });
        WalkRoad(start, end, [&](core::TilePoint tile) {
            u8& kind = source.grid[GridIndex(source, tile)];
            if (!Open(kind)) {
                kind = MEADOW;
                ++carved;
            }
        });
    }
    return carved;
}

/// The source of the synthetic map: terrain, places, roads with the carving under them, profiles and travel rules.
[[nodiscard]] inline Draft Source(u32 scale, u64 seed, const SyntheticOptions& options = {}) {
    const SyntheticSize size = WithPlaces(SyntheticSizeOf(scale), options.places);
    Draft draft;
    core::WorldMapSource& source = draft.source;
    MakeTerrain(source, size, seed, options.terrain);
    MakePlaces(source, size, seed);
    if (options.terrain != SyntheticTerrain::Meadow) {
        draft.carved = MakeRoads(source, size.cities);
    }
    source.profiles.push_back(Profile("on_foot", 0));
    source.profiles.push_back(Profile("on_foot_wary", DANGER_COST));
    source.travel.reachBase = 300;
    source.travel.reachPerPower = 20;
    source.travel.bands = { { .fromPower = 0.0F, .profile = 1 }, { .fromPower = 5.0F, .profile = 0 } };
    return draft;
}

} // namespace synthetic

/// How many closed tiles under the roads of the synthetic map of `scale` and `seed` were turned to meadow so the roads can
/// cross them.
[[nodiscard]] inline u32 SyntheticCarvedTiles(u32 scale, u64 seed) {
    return synthetic::Source(scale, seed).carved;
}

/// The synthetic map of `scale` times the test map's area (1, 10 or 50) for `seed` and `options`: the same map for the
/// same arguments. Stops the program if it can't be built, a fault of this support code, as ShippedWorld() does.
[[nodiscard]] inline core::WorldMap SyntheticMap(u32 scale, u64 seed, const SyntheticOptions& options = {}) {
    std::expected<core::WorldMap, core::Error> built = core::WorldMap::Build(synthetic::Source(scale, seed, options).source);
    if (!built) {
        std::println(stderr, "synthetic map refused: {}", built.error().message);
        std::abort();
    }
    return std::move(*built);
}

} // namespace test
