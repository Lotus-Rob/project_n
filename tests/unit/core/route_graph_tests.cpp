#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/paths.hpp"
#include "core/route_graph.hpp"
#include "core/world_map.hpp"

#include <doctest/doctest.h>

namespace {

constexpr f32 TILE = 256.0F;
constexpr u16 MEADOW_COST = 10;
constexpr u16 ROAD_COST = 4;
constexpr u8 MEADOW = 0;
constexpr u8 PEAKS = 1;
constexpr core::ProfileIndex CART = 1;
constexpr core::RoutingRules RULES{ .sectorSize = 4, .gateSpacing = 2 };

/// A map from rows of symbols: '.' meadow (10), '^' peaks (impassable), '=' a road on meadow (4), and a digit for a city
/// on meadow whose place index is the digit, city 0 the start.
core::WorldMapSource Source(const std::vector<std::string>& rows) {
    for (const std::string& row : rows) {
        REQUIRE(row.size() == rows.front().size());
    }
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

core::WorldMap MustBuild(core::WorldMapSource source) {
    auto built = core::WorldMap::Build(std::move(source));
    REQUIRE(built.has_value());
    return *std::move(built);
}

/// The index of the tile at `column`, `row` of `map`.
u32 At(const core::WorldMap& map, u32 column, u32 row) {
    return map.TileIndex({ .column = column, .row = row });
}

/// `sector`'s graph by walking, built with a workspace of its own.
core::SectorGraph Build(const core::WorldMap& map, u32 sector, core::RoutingRules rules = RULES) {
    core::SectorWalk walk;
    return core::BuildSector(map, core::WALKING, rules, sector, walk);
}

/// The tiles of `graph`'s crossings' nodes, in the crossings' order.
std::vector<u32> CrossingTiles(const core::SectorGraph& graph) {
    std::vector<u32> tiles;
    tiles.reserve(graph.crossings.size());
    for (const core::SectorCrossing& crossing : graph.crossings) {
        tiles.push_back(graph.nodes.at(crossing.node));
    }
    return tiles;
}

/// What walking `way` costs step by step.
u32 Walked(const core::WorldMap& map, const std::vector<u32>& way) {
    u32 total = 0;
    for (std::size_t step = 1; step < way.size(); ++step) {
        const u32 cost = map.StepCost(way[step - 1], way[step], core::WALKING);
        REQUIRE(cost != core::IMPASSABLE_COST);
        total += cost;
    }
    return total;
}

TEST_CASE("a map smaller than a sector is one sector with only its places") {
    const core::WorldMap map = MustBuild(Source({ "0......", ".....1.", "......." }));
    const core::RoutingRules rules{};
    CHECK(core::SectorColumns(map, rules) == 1);
    CHECK(core::SectorRows(map, rules) == 1);
    const core::SectorGraph graph = Build(map, 0, rules);
    CHECK(graph.nodes == std::vector<u32>{ At(map, 0, 0), At(map, 5, 1) });
    CHECK(graph.crossings.empty());
}

TEST_CASE("sectors at the map's edge are short") {
    const core::WorldMap map =
        MustBuild(Source({ "0.........", "..........", "..........", "..........", "..........", ".........." }));
    CHECK(core::SectorColumns(map, RULES) == 3);
    CHECK(core::SectorRows(map, RULES) == 2);
    const core::TileSpan edge = core::SectorSpan(map, RULES, 2);
    CHECK(edge.firstColumn == 8);
    CHECK(edge.lastColumn == 9);
    CHECK(edge.firstRow == 0);
    CHECK(edge.lastRow == 3);
    const core::TileSpan corner = core::SectorSpan(map, RULES, 5);
    CHECK(corner.firstColumn == 8);
    CHECK(corner.lastRow == 5);
    CHECK(core::SectorOf(map, RULES, At(map, 9, 5)) == 5);
    CHECK(core::SectorOf(map, RULES, At(map, 4, 3)) == 1);
}

TEST_CASE("an open border gets a gate for every stretch of the gate spacing") {
    const core::WorldMap map = MustBuild(Source({ "........", "........", "........", ".......0" }));
    const core::SectorGraph graph = Build(map, 0);
    CHECK(CrossingTiles(graph) == std::vector<u32>{ At(map, 3, 0), At(map, 3, 2) });
    CHECK(std::ranges::none_of(graph.crossings, &core::SectorCrossing::south));
}

TEST_CASE("a road crossing a border is a gate") {
    const core::WorldMap map = MustBuild(Source({ ".......0", "........", "........", "========" }));
    CHECK(CrossingTiles(Build(map, 0)) == std::vector<u32>{ At(map, 3, 0), At(map, 3, 3) });
}

TEST_CASE("impassable ground splits a border into runs") {
    const core::WorldMap map = MustBuild(Source({ "........", "....^...", "........", ".......0" }));
    CHECK(CrossingTiles(Build(map, 0)) == std::vector<u32>{ At(map, 3, 0), At(map, 3, 2) });
}

TEST_CASE("a place on a gate's tile and a corner tile serving two borders are one node each") {
    // The road makes (3, 3) the cheapest pair of the lower piece of both sector 0's east and south borders.
    const core::WorldMap map =
        MustBuild(Source({ "...0....", "........", "........", "...==...", "...=....", "........", "........", "........" }));
    const core::SectorGraph graph = Build(map, 0);
    CHECK(graph.nodes == std::vector<u32>{ At(map, 3, 0), At(map, 0, 3), At(map, 3, 3) });
    CHECK(
        graph.crossings == std::vector<core::SectorCrossing>{
                               { .node = 0, .south = false, .cost = MEADOW_COST, .costBack = MEADOW_COST },
                               { .node = 1, .south = true, .cost = MEADOW_COST, .costBack = MEADOW_COST },
                               { .node = 2, .south = false, .cost = ROAD_COST, .costBack = ROAD_COST },
                               { .node = 2, .south = true, .cost = ROAD_COST, .costBack = ROAD_COST },
                           }
    );
}

TEST_CASE("an edge is the cheapest way inside the sector from its lower node, and costs its own tiles back") {
    // Sector 1's nodes are its west gates (4, 0) and (4, 2) and the city on the road's end (7, 3).
    core::WorldMapSource whole = Source({ "........", "........", "........", ".....==0" });
    whole.roads.push_back({ { .column = 7, .row = 3 } });
    const core::WorldMap map = MustBuild(std::move(whole));
    core::WorldMapSource alone = Source({ "....", "....", "....", ".==0" });
    alone.roads.push_back({ { .column = 3, .row = 3 } });
    const core::WorldMap sector = MustBuild(std::move(alone));

    const core::SectorGraph graph = Build(map, 1);
    REQUIRE(graph.nodes == std::vector<u32>{ At(map, 4, 0), At(map, 4, 2), At(map, 7, 3) });
    const auto edge =
        std::ranges::find_if(graph.edges, [](const core::SectorEdge& found) { return found.from == 1 && found.to == 2; });
    REQUIRE(edge != graph.edges.end());
    const std::vector<u32> cheapest = core::FindRoute(sector, core::WALKING, At(sector, 0, 2), At(sector, 3, 3));
    CHECK(edge->cost == Walked(sector, cheapest));

    core::SectorWalk walk;
    walk.WalkFrom(map, core::WALKING, core::SectorSpan(map, RULES, 1), At(map, 4, 2));
    std::vector<u32> way;
    walk.Way(At(map, 7, 3), way);
    CHECK(way == std::vector<u32>{ At(map, 4, 2), At(map, 5, 3), At(map, 6, 3), At(map, 7, 3) });
    std::vector<u32> back = way;
    std::ranges::reverse(back);
    CHECK(edge->costBack == Walked(map, back));
    // Onto the road diagonally (4 x 7 / 5 = 5) and along it (4 + 4); back along it and off onto meadow (10 x 7 / 5 = 14).
    CHECK(edge->cost == 13);
    CHECK(edge->costBack == 22);
}

TEST_CASE("a walk to a tile prices each way in and gives its tiles in walking order") {
    core::WorldMapSource source = Source({ "........", "........", "........", ".....==0" });
    source.roads.push_back({ { .column = 7, .row = 3 } });
    const core::WorldMap map = MustBuild(std::move(source));
    core::SectorWalk walk;
    walk.WalkTo(map, core::WALKING, core::SectorSpan(map, RULES, 1), At(map, 7, 3));
    CHECK(walk.Cost(At(map, 4, 2)) == 13);
    CHECK(walk.Cost(At(map, 7, 3)) == 0);
    CHECK(walk.Cost(At(map, 3, 3)) == core::UNREACHABLE); // the sector to the west
    std::vector<u32> way{ 99 };
    walk.Way(At(map, 4, 2), way);
    CHECK(way == std::vector<u32>{ At(map, 4, 2), At(map, 5, 3), At(map, 6, 3), At(map, 7, 3) });
    walk.Way(At(map, 3, 3), way);
    CHECK(way.empty());
}

TEST_CASE("nodes a sector's ground keeps apart share no edge") {
    // Peaks down column 2 of sector 0 leave the city and the south gate at (0, 3) west of them, the other gates east; a
    // way round runs through sector 2.
    const core::WorldMap map =
        MustBuild(Source({ "0.^.....", "..^.....", "..^.....", "..^.....", "........", "........", "........", "........" }));
    REQUIRE(!core::FindRoute(map, core::WALKING, At(map, 0, 0), At(map, 3, 0)).empty());
    const core::SectorGraph graph = Build(map, 0);
    REQUIRE(graph.nodes == std::vector<u32>{ At(map, 0, 0), At(map, 3, 0), At(map, 3, 2), At(map, 0, 3), At(map, 3, 3) });
    CHECK(
        graph.edges == std::vector<core::SectorEdge>{
                           { .from = 0, .to = 3, .cost = 30, .costBack = 30 },
                           { .from = 1, .to = 2, .cost = 20, .costBack = 20 },
                           { .from = 1, .to = 4, .cost = 30, .costBack = 30 },
                           { .from = 2, .to = 4, .cost = 10, .costBack = 10 },
                       }
    );
}

TEST_CASE("a crossing costs the tile it enters each way") {
    const core::WorldMap map = MustBuild(Source({ "....=...", "....=...", "....=...", "....=..0" }));
    CHECK(
        Build(map, 0).crossings == std::vector<core::SectorCrossing>{
                                       { .node = 0, .south = false, .cost = ROAD_COST, .costBack = MEADOW_COST },
                                       { .node = 1, .south = false, .cost = ROAD_COST, .costBack = MEADOW_COST },
                                   }
    );
}

TEST_CASE("a sector builds the same with a fresh workspace and a used one") {
    // Peaks in sector 1 keep the city at (7, 0) and the gate at (7, 3) from its other nodes, which the whole map's single
    // sector joins round them.
    const core::WorldMap map =
        MustBuild(Source({ "......^0", "......^.", "......^.", "......^.", "........", "........", "........", "........" }));
    const core::SectorGraph fresh = Build(map, 1);
    REQUIRE(fresh.nodes.size() == 5);
    REQUIRE(fresh.edges.size() == 4);

    core::SectorWalk walk;
    static_cast<void>(core::BuildSector(map, core::WALKING, RULES, 0, walk));
    static_cast<void>(core::BuildSector(map, core::WALKING, core::RoutingRules{}, 0, walk));
    CHECK(core::BuildSector(map, core::WALKING, RULES, 1, walk) == fresh);
}

TEST_CASE("a place on ground its profile can't enter is no node") {
    core::WorldMapSource source = Source({ "0......", ".....1.", "......." });
    source.roads.push_back({ { .column = 0, .row = 0 }, { .column = 2, .row = 0 } });
    // A cart keeps to the roads, so city 1 on open meadow is out of its reach.
    source.profiles = {
        { .key = "on_foot", .ticksPerCost = 1, .kindCosts = { MEADOW_COST, core::IMPASSABLE_COST }, .features = {} },
        { .key = "cart", .ticksPerCost = 1, .kindCosts = { core::IMPASSABLE_COST, core::IMPASSABLE_COST }, .features = {} }
    };
    const core::WorldMap map = MustBuild(std::move(source));
    core::SectorWalk walk;
    const core::RoutingRules rules{};
    CHECK(core::BuildSector(map, core::WALKING, rules, 0, walk).nodes == std::vector<u32>{ At(map, 0, 0), At(map, 5, 1) });
    CHECK(core::BuildSector(map, CART, rules, 0, walk).nodes == std::vector<u32>{ At(map, 0, 0) });
}

} // namespace
