#pragma once

/// @file
/// A plain meadow map of any size for tests that only look at its tiles, which needs nothing but core (unlike
/// world.hpp's small map, which a host's data builds on).

#include "core/types.hpp"
#include "core/world_map.hpp"

#include <cstddef>
#include <cstdlib>
#include <expected>
#include <format>
#include <span>
#include <utility>
#include <vector>

namespace test {

/// A map of `columns` by `rows` tiles with one city (the start) at tile 0, `rivers` and `roads` running over it: all a map
/// needs to build. `grid` gives each tile's kind index (0 meadow, 1 lake), or is empty for all meadow; tile 0 should stay
/// meadow, which the city stands on. More cities stand on `cities`, place indices 1 on in that order. Stops at once if it
/// can't be built, as SmallWorldMap() does.
[[nodiscard]] inline core::WorldMap MapOfTiles(
    u32 columns,
    u32 rows,
    std::span<const u8> grid,
    std::vector<std::vector<core::TilePoint>> rivers = {},
    std::vector<std::vector<core::TilePoint>> roads = {},
    std::span<const core::TilePoint> cities = {}
) {
    constexpr f32 TILE_SIZE = 256.0F;
    constexpr u16 MEADOW_COST = 10;
    constexpr u16 LAKE_COST = 40;
    constexpr u32 PEOPLE = 100;
    core::WorldMapSource source;
    source.columns = columns;
    source.rows = rows;
    source.tileSize = TILE_SIZE;
    source.kinds = { { .name = "meadow", .symbol = '.', .cost = MEADOW_COST } };
    source.rivers = std::move(rivers);
    source.roads = std::move(roads);
    if (grid.empty()) {
        source.grid.assign(static_cast<std::size_t>(columns) * rows, 0);
    }
    else {
        source.kinds.push_back({ .name = "lake", .symbol = '~', .cost = LAKE_COST });
        source.grid.assign(grid.begin(), grid.end());
    }
    source.places.push_back(
        { .key = "city",
          .kind = core::PlaceKind::City,
          .sort = "",
          .tile = { .column = 0, .row = 0 },
          .nameKey = "city",
          .name = {},
          .people = PEOPLE,
          .start = true }
    );
    for (std::size_t index = 0; index < cities.size(); ++index) {
        source.places.push_back(
            { .key = std::format("city{}", index + 1),
              .kind = core::PlaceKind::City,
              .sort = "",
              .tile = cities[index],
              .nameKey = std::format("city_{}", index + 1),
              .name = {},
              .people = PEOPLE,
              .start = false }
        );
    }
    std::expected<core::WorldMap, core::Error> built = core::WorldMap::Build(std::move(source));
    if (!built) {
        std::abort(); // every test after would fail for the same reason
    }
    return *std::move(built);
}

/// A map of `columns` by `rows` tiles of meadow with one city (the start) at tile 0 and more on `cities`, `rivers` and
/// `roads` running over it.
[[nodiscard]] inline core::WorldMap MeadowMap(
    u32 columns,
    u32 rows,
    std::vector<std::vector<core::TilePoint>> rivers = {},
    std::vector<std::vector<core::TilePoint>> roads = {},
    std::span<const core::TilePoint> cities = {}
) {
    return MapOfTiles(columns, rows, {}, std::move(rivers), std::move(roads), cities);
}

} // namespace test
