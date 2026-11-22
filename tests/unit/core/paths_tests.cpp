#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/paths.hpp"

#include <doctest/doctest.h>

namespace {

constexpr f32 TILE = 256.0F;
constexpr u16 ROAD_COST = 4;
constexpr u16 FORD_COST = 30;
constexpr u16 MEADOW_COST = 10;
constexpr u16 SAND_COST = 14;
constexpr u8 MEADOW = 0;
constexpr u8 DEEP = 1;
constexpr u8 SAND = 2;
constexpr u32 ROAD_FIRST_COLUMN = 5;
constexpr u32 LAST_COLUMN = 7;
constexpr u32 ROAD_ROW = 4;
constexpr u32 WIDE_LAST_COLUMN = 8;

// A map from rows of symbols: '.' meadow, ',' sand, '~' deep water, and a digit for a city on meadow, whose place index is
// the digit.
core::WorldMapSource Source(const std::vector<std::string>& rows) {
    for (const std::string& row : rows) {
        REQUIRE(row.size() == rows.front().size());
    }
    core::WorldMapSource source;
    source.columns = static_cast<u32>(rows.front().size());
    source.rows = static_cast<u32>(rows.size());
    source.tileSize = TILE;
    source.roadCost = ROAD_COST;
    source.fordCost = FORD_COST;
    source.kinds = { { .name = "meadow", .symbol = '.', .cost = MEADOW_COST },
                     { .name = "deep water", .symbol = '~', .cost = core::IMPASSABLE_COST },
                     { .name = "sand", .symbol = ',', .cost = SAND_COST } };
    for (u32 row = 0; row < source.rows; ++row) {
        for (u32 column = 0; column < source.columns; ++column) {
            const char symbol = rows[row][column];
            u8 kind = MEADOW;
            if (symbol == '~') {
                kind = DEEP;
            }
            else if (symbol == ',') {
                kind = SAND;
            }
            source.grid.push_back(kind);
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

// What walking `route` costs, step by step.
u32 Walked(const core::WorldMap& map, const std::vector<u32>& route) {
    u32 total = 0;
    for (std::size_t step = 1; step < route.size(); ++step) {
        const u32 cost = map.StepCost(route[step - 1], route[step], core::WALKING);
        REQUIRE(cost != core::IMPASSABLE_COST);
        total += cost;
    }
    return total;
}

// Columns 0 to 7, rows 0 to 4: a river down column 4 with a ford at row 2, sand beside the road, a road along row 4
// from column 5 to the city 1 in the corner.
core::WorldMap RiverMap() {
    core::WorldMapSource source = Source({ "0.......", "........", "....,,,.", "........", ".......1" });
    source.rivers = { { { .column = 4, .row = 0 }, { .column = 4, .row = 4 } } };
    source.fords = { { .column = 4, .row = 2 } };
    source.roads = { { { .column = ROAD_FIRST_COLUMN, .row = ROAD_ROW }, { .column = LAST_COLUMN, .row = ROAD_ROW } } };
    return MustBuild(std::move(source));
}

TEST_CASE("a route takes the road over a shorter line across open ground") {
    core::WorldMapSource source = Source({ "0........", ".........", "........1" });
    source.roads = { { { .column = 0, .row = 1 }, { .column = WIDE_LAST_COLUMN, .row = 1 } } };
    const core::WorldMap map = MustBuild(std::move(source));
    const core::DistanceMap distances = core::DistanceMap::Build(map, 8, core::WALKING);
    const std::vector<u32> route = core::RouteDown(map, distances, 0);
    // Diagonal onto the road (4 x 7 / 5 = 5), six road steps (24), diagonal off it (10 x 7 / 5 = 14); the straight line along
    // the top row costs 80.
    CHECK(distances.CostFrom(0) == 43);
    CHECK(Walked(map, route) == distances.CostFrom(0));
    CHECK(std::ranges::find(route, 13U) != route.end()); // the road's middle, (4, 1)
    CHECK(route.front() == 0);
    CHECK(route.back() == 8);
    CHECK(core::FindRoute(map, core::WALKING, 0, 8) == route);
}

TEST_CASE("a route never cuts a corner past impassable ground") {
    const core::WorldMap map = MustBuild(Source({ "0.~.", "...." }));
    const std::vector<u32> down = { 1, 5, 6, 7, 3 };
    const core::DistanceMap distances = core::DistanceMap::Build(map, 3, core::WALKING);
    CHECK(core::RouteDown(map, distances, 1) == down);
    CHECK(core::FindRoute(map, core::WALKING, 1, 3) == down);
    CHECK(distances.CostFrom(1) == 4 * MEADOW_COST);
}

TEST_CASE("the shortest route goes straight across open ground past a road") {
    core::WorldMapSource source = Source({ "0........", ".........", "........1" });
    source.roads = { { { .column = 0, .row = 1 }, { .column = WIDE_LAST_COLUMN, .row = 1 } } };
    const core::WorldMap map = MustBuild(std::move(source));
    const std::vector<u32> straight = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    CHECK(core::FindShortestRoute(map, 0, 8) == straight);
    CHECK(core::FindRoute(map, core::WALKING, 0, 8) != straight);
    CHECK(core::FindShortestRoute(map, 5, 5) == std::vector<u32>{ 5 });
}

TEST_CASE("the shortest route over open ground keeps close to the straight line") {
    // Six columns by seven rows of meadow: tile = row * 6 + column.
    const core::WorldMap map = MustBuild(Source({ "0.....", "......", "......", "......", "......", "......", "......" }));
    // Two across and six down: two diagonals among four straight steps, spread out rather than all straight steps first.
    CHECK(core::FindShortestRoute(map, 0, 38) == std::vector<u32>{ 0, 6, 13, 19, 25, 32, 38 });
    // Five across and four down: the one straight step in the middle of the diagonals, there and back.
    CHECK(core::FindShortestRoute(map, 0, 29) == std::vector<u32>{ 0, 7, 14, 15, 22, 29 });
    CHECK(core::FindShortestRoute(map, 29, 0) == std::vector<u32>{ 29, 22, 15, 14, 7, 0 });
}

TEST_CASE("the shortest route round an obstacle keeps close to the line and stays shortest") {
    const core::WorldMap map = MustBuild(Source({ "0..~...", "......." }));
    const std::vector<u32> round = core::FindShortestRoute(map, 0, 6);
    // Down to the second row only for the three tiles beside the water; on meadow alone each step costs its length, so the
    // cheapest route's cost is the shortest one's.
    CHECK(round == std::vector<u32>{ 0, 1, 9, 10, 11, 5, 6 });
    CHECK(Walked(map, round) == Walked(map, core::FindRoute(map, core::WALKING, 0, 6)));
}

TEST_CASE("the shortest route never cuts a corner past impassable ground") {
    const core::WorldMap map = MustBuild(Source({ "0.~.", "...." }));
    const std::vector<u32> round = { 1, 5, 6, 7, 3 };
    CHECK(core::FindShortestRoute(map, 1, 3) == round);
    CHECK(core::FindShortestRoute(map, 1, 3) == core::FindRoute(map, core::WALKING, 1, 3));
}

TEST_CASE("the shortest route never squeezes a diagonal between two impassable tiles") {
    const core::WorldMap map = MustBuild(Source({ "0~", "~." }));
    CHECK(core::FindShortestRoute(map, 0, 3).empty());
    CHECK(core::FindShortestRoute(map, 0, 3) == core::FindRoute(map, core::WALKING, 0, 3));
}

TEST_CASE("the shortest route to an impassable or unreachable tile is empty") {
    const core::WorldMap map = MustBuild(Source({ "0..~.", "...~.", "...~." }));
    CHECK(core::FindShortestRoute(map, 0, 3).empty()); // deep water
    CHECK(core::FindShortestRoute(map, 0, 4).empty()); // an island past the water
}

TEST_CASE("a distance map's cost is what walking its route costs") {
    const core::WorldMap map = RiverMap();
    const u32 goal = map.TileIndex({ .column = 7, .row = 4 });
    const core::DistanceMap distances = core::DistanceMap::Build(map, goal, core::WALKING);
    const std::vector<std::pair<std::string, core::TilePoint>> starts = { { "across the ford", { .column = 0, .row = 0 } },
                                                                          { "across the ford, low", { .column = 2, .row = 3 } },
                                                                          { "in the sand", { .column = 6, .row = 2 } },
                                                                          { "entering the road from meadow",
                                                                            { .column = 6, .row = 3 } },
                                                                          { "beside the goal", { .column = 6, .row = 4 } } };
    for (const auto& [name, point] : starts) {
        CAPTURE(name);
        const u32 start = map.TileIndex(point);
        const std::vector<u32> route = core::RouteDown(map, distances, start);
        REQUIRE_FALSE(route.empty());
        CHECK(Walked(map, route) == distances.CostFrom(start));
        CHECK(route.back() == goal);
    }
    // From the far corner: two diagonals and a step to the ford's west bank (38), the ford (30), the sand beyond (14), a
    // diagonal and a diagonal onto the road (14 + 5).
    CHECK(distances.CostFrom(map.TileIndex({ .column = 0, .row = 0 })) == 101);
}

TEST_CASE("a distance map searches backwards: entering costs differ each way") {
    const core::WorldMap map = RiverMap();
    const u32 road = map.TileIndex({ .column = 6, .row = 4 });
    const u32 sand = map.TileIndex({ .column = 6, .row = 2 });
    const core::DistanceMap toRoad = core::DistanceMap::Build(map, road, core::WALKING);
    const core::DistanceMap toSand = core::DistanceMap::Build(map, sand, core::WALKING);
    CHECK(toRoad.CostFrom(sand) == MEADOW_COST + ROAD_COST); // from the sand, a meadow tile, then the road
    CHECK(toSand.CostFrom(road) == MEADOW_COST + SAND_COST); // from the road, a meadow tile, then the sand
}

TEST_CASE("the search costs the same as the distance map") {
    const core::WorldMap lake = MustBuild(Source({ "0.......", "..~~....", "..~.....", "........", ".......1" }));
    const core::WorldMap river = RiverMap();
    struct Sample {
        std::string name;
        u32 from;
        u32 goal;
        bool goalOnRiver; // on the river map the goal is a river tile no one can enter
    };
    const std::vector<Sample> samples = {
        { .name = "corner to corner", .from = 0, .goal = 39, .goalOnRiver = false },
        { .name = "the way back", .from = 39, .goal = 0, .goalOnRiver = false },
        { .name = "onto the river's bank tile, a river on the river map", .from = 3, .goal = 36, .goalOnRiver = true },
        { .name = "round the lake, over the ford", .from = 8, .goal = 31, .goalOnRiver = false },
        { .name = "along the ford's row", .from = 16, .goal = 7, .goalOnRiver = false },
        { .name = "to the road's end from the west bank", .from = 24, .goal = 39, .goalOnRiver = false },
        { .name = "from the south-west to the north", .from = 32, .goal = 5, .goalOnRiver = false },
        { .name = "back over the ford, east to west", .from = 15, .goal = 26, .goalOnRiver = false }
    };
    for (const auto& [name, from, goal, goalOnRiver] : samples) {
        CAPTURE(name);
        const core::DistanceMap distances = core::DistanceMap::Build(lake, goal, core::WALKING);
        const std::vector<u32> route = core::FindRoute(lake, core::WALKING, from, goal);
        REQUIRE_FALSE(route.empty());
        CHECK(Walked(lake, route) == distances.CostFrom(from));
        const core::DistanceMap across = core::DistanceMap::Build(river, goal, core::WALKING);
        const std::vector<u32> crossing = core::FindRoute(river, core::WALKING, from, goal);
        if (goalOnRiver) {
            CHECK(crossing.empty());
            CHECK(across.CostFrom(from) == core::UNREACHABLE);
        }
        else {
            REQUIRE_FALSE(crossing.empty());
            CHECK(Walked(river, crossing) == across.CostFrom(from));
        }
    }
}

TEST_CASE("routes are the same every time") {
    const core::WorldMap map = RiverMap();
    const u32 goal = map.TileIndex({ .column = 7, .row = 4 });
    const core::DistanceMap first = core::DistanceMap::Build(map, goal, core::WALKING);
    const core::DistanceMap second = core::DistanceMap::Build(map, goal, core::WALKING);
    CHECK(core::RouteDown(map, first, 0) == core::RouteDown(map, second, 0));
    CHECK(core::FindRoute(map, core::WALKING, 0, goal) == core::FindRoute(map, core::WALKING, 0, goal));
}

TEST_CASE("an unreachable goal gives no route") {
    const core::WorldMap map = MustBuild(Source({ "0..~.", "...~.", "...~." }));
    const u32 island = 4;
    const u32 shore = 0;
    const core::DistanceMap distances = core::DistanceMap::Build(map, island, core::WALKING);
    CHECK(distances.CostFrom(shore) == core::UNREACHABLE);
    CHECK(core::RouteDown(map, distances, shore).empty());
    CHECK(core::FindRoute(map, core::WALKING, shore, island).empty());
    const u32 water = 3;
    CHECK(core::FindRoute(map, core::WALKING, shore, water).empty());
    CHECK(core::FindRoute(map, core::WALKING, shore, shore) == std::vector<u32>{ shore });
    CHECK(core::RouteDown(map, core::DistanceMap::Build(map, shore, core::WALKING), shore) == std::vector<u32>{ shore });
}

TEST_CASE("Paths builds a map once and keeps it") {
    auto shared = std::make_shared<const core::WorldMap>(RiverMap());
    core::Paths paths{ shared };
    const core::DistanceMap& first = paths.To(1, core::WALKING);
    CHECK(first.Goal() == shared->TileIndex({ .column = 7, .row = 4 }));
    CHECK(first.Profile() == core::WALKING);
    CHECK(&paths.To(1, core::WALKING) == &first);
    paths.Warm(core::WALKING);
    CHECK(&paths.To(1, core::WALKING) == &first);
    CHECK(paths.To(0, core::WALKING).Goal() == 0);
}

} // namespace
