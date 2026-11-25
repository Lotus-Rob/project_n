#pragma once

/// @file
/// Small maps drawn by hand in rows of symbols for the routing tests, and the 40 x 24 hand-drawn map whose sectors,
/// gates, ridge, road and danger those tests route over.

#include "core/profile.hpp"
#include "core/route_graph.hpp"
#include "core/types.hpp"
#include "core/world_map.hpp"

#include <doctest/doctest.h>

namespace test::hand {

/// A tile's side in world units.
inline constexpr f32 TILE = 256.0F;
/// What entering meadow costs.
inline constexpr u16 MEADOW_COST = 10;
/// What entering a road costs.
inline constexpr u16 ROAD_COST = 4;
/// The meadow's kind index.
inline constexpr u8 MEADOW = 0;
/// The peaks' kind index.
inline constexpr u8 PEAKS = 1;
/// The hand-drawn map's routing rules: sectors of 8, so it is 5 sectors across and 3 down.
inline constexpr core::RoutingRules HAND_RULES{ .sectorSize = 8, .gateSpacing = 4 };
/// The hand-drawn map's wary profile, which pays for walking near the danger.
inline constexpr core::ProfileIndex WARY = 1;
/// What the wary profile adds to a tile near the danger.
inline constexpr u16 WARY_DANGER_COST = 40;
/// The hand-drawn map's danger tile, in its corner sector; its reach of 2 just touches row 16, the ring of the sector above.
inline constexpr core::TilePoint DANGER_TILE{ .column = 37, .row = 18 };

/// A map from rows of symbols: '.' meadow (10), '^' peaks (impassable), '=' a road on meadow (4), and a digit for a city
/// on meadow whose place index is the digit, city 0 the start.
[[nodiscard]] inline core::WorldMapSource Source(const std::vector<std::string>& rows) {
    REQUIRE(std::ranges::all_of(rows, [&](const std::string& row) { return row.size() == rows.front().size(); }));
    core::WorldMapSource source;
    source.columns = static_cast<u32>(rows.front().size());
    source.rows = static_cast<u32>(rows.size());
    source.tileSize = TILE;
    source.roadCost = ROAD_COST;
    source.kinds = { { .name = "meadow", .symbol = '.', .cost = MEADOW_COST },
                     { .name = "peaks", .symbol = '^', .cost = core::IMPASSABLE_COST } };
    for (u32 row = 0; row < source.rows; ++row) {
        for (u32 column = 0; column < source.columns; ++column) {
            const char symbol = rows[row][column];
            source.grid.push_back(symbol == '^' ? PEAKS : MEADOW);
            if (symbol == '=') {
                source.roads.push_back({ { .column = column, .row = row } });
            }
            if (symbol >= '0' && symbol <= '9') {
                const auto index = static_cast<std::size_t>(symbol - '0');
                source.places.resize(std::max(source.places.size(), index + 1));
                source.places[index] = { .key = std::format("city{}", index),
                                         .kind = core::PlaceKind::City,
                                         .sort = "",
                                         .tile = { .column = column, .row = row },
                                         .nameKey = std::format("city_{}", index),
                                         .name = {},
                                         .people = 1,
                                         .start = index == 0 };
            }
        }
    }
    return source;
}

/// The map `source` builds; the test fails if it is refused.
[[nodiscard]] inline core::WorldMap MustBuild(core::WorldMapSource source) {
    auto built = core::WorldMap::Build(std::move(source));
    REQUIRE(built.has_value());
    return *std::move(built);
}

/// The index of the tile at `column`, `row` of `map`.
[[nodiscard]] inline u32 At(const core::WorldMap& map, u32 column, u32 row) {
    return map.TileIndex({ .column = column, .row = row });
}

/// The index of `point` on `map`.
[[nodiscard]] inline u32 At(const core::WorldMap& map, core::TilePoint point) {
    return map.TileIndex(point);
}

/// A 40 x 24 map: a road from city 0 east along row 2 and down column 37 into the danger's reach, a ridge down column 12
/// with a gap, peaks splitting the borders at column 16 and row 8, three cities and a danger of reach 2 in the
/// south-east corner; profiles `on_foot` and `on_foot_wary`.
[[nodiscard]] inline core::WorldMapSource HandSource() {
    core::WorldMapSource source = Source(
        {
            "........................................", "........................................",
            "..0===================================..", "..................................=..=..",
            "............^.....................2..=..", "............^........................=..",
            "............^........................=..", "............^........................=..",
            "............^.............^^^^.......=..", "............^........................=..",
            "............^........................=..", ".....................................=..",
            "....................1................=..", "............^........................=..",
            "............^........................=..", "............^........................=..",
            "............^........................=..", "............^...^....................=..",
            "............^...^.......................", "............^...^.......................",
            "............^...^.......................", "................^.......................",
            "........................................", "........................................",
        }
    );
    // Cities 0 and 2 stand on the road's ends, city 1 on open meadow.
    source.roads.push_back({ { .column = 2, .row = 2 } });
    source.roads.push_back({ { .column = 34, .row = 4 } });
    source.dangerReach = 2;
    source.places.push_back(
        { .key = "pit",
          .kind = core::PlaceKind::Danger,
          .sort = "pit",
          .tile = DANGER_TILE,
          .nameKey = "pit",
          .name = {},
          .people = 0,
          .start = false }
    );
    source.profiles = {
        { .key = "on_foot", .ticksPerCost = 1, .kindCosts = { MEADOW_COST, core::IMPASSABLE_COST }, .features = {} },
        { .key = "on_foot_wary",
          .ticksPerCost = 1,
          .kindCosts = { MEADOW_COST, core::IMPASSABLE_COST },
          .features = { .danger = WARY_DANGER_COST } },
    };
    return source;
}

/// The hand-drawn map HandSource describes.
[[nodiscard]] inline core::WorldMap HandMap() {
    return MustBuild(HandSource());
}

} // namespace test::hand
