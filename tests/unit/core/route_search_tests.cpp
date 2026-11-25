#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/jobs.hpp"
#include "core/paths.hpp"
#include "core/random.hpp"
#include "core/route_graph.hpp"
#include "core/route_search.hpp"
#include "core/world_map.hpp"
#include "support/synthetic_map.hpp"

#include <doctest/doctest.h>

namespace {

constexpr f32 TILE = 256.0F;
constexpr u16 MEADOW_COST = 10;
constexpr u16 ROAD_COST = 4;
constexpr u8 MEADOW = 0;
constexpr u8 PEAKS = 1;
/// The hand-drawn map's routing rules: sectors of 8, so it is 5 sectors across and 3 down.
constexpr core::RoutingRules HAND_RULES{ .sectorSize = 8, .gateSpacing = 4 };
/// The small maps' routing rules: sectors of 4.
constexpr core::RoutingRules SMALL_RULES{ .sectorSize = 4, .gateSpacing = 2 };
/// The hand-drawn map's wary profile, which pays for walking near the danger.
constexpr core::ProfileIndex WARY = 1;
/// The profile a test adds after the hand-drawn map's two.
constexpr core::ProfileIndex ADDED = 2;
/// What the wary profile adds to a tile near the danger.
constexpr u16 WARY_DANGER_COST = 40;

/// A map from rows of symbols: '.' meadow (10), '^' peaks (impassable), '=' a road on meadow (4), and a digit for a city
/// on meadow whose place index is the digit, city 0 the start.
core::WorldMapSource Source(const std::vector<std::string>& rows) {
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
core::WorldMap MustBuild(core::WorldMapSource source) {
    auto built = core::WorldMap::Build(std::move(source));
    REQUIRE(built.has_value());
    return *std::move(built);
}

/// The index of the tile at `column`, `row` of `map`.
u32 At(const core::WorldMap& map, u32 column, u32 row) {
    return map.TileIndex({ .column = column, .row = row });
}

/// The index of `point` on `map`.
u32 At(const core::WorldMap& map, core::TilePoint point) {
    return map.TileIndex(point);
}

/// A 40 x 24 map: a road from city 0 east along row 2 and down column 37 into the danger's reach, a ridge down column 12
/// with a gap, peaks splitting the borders at column 16 and row 8, three cities and a danger of reach 2 in the
/// south-east corner; profiles `on_foot` and `on_foot_wary`.
core::WorldMapSource HandSource() {
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
          .tile = { .column = 37, .row = 18 },
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
core::WorldMap HandMap() {
    return MustBuild(HandSource());
}

/// The hand-drawn map's graphs, built on the calling thread.
core::RouteGraphs HandGraphs(const core::WorldMap& map) {
    return core::RouteGraphs::Build(map, HAND_RULES);
}

/// The tiles of the hand-drawn map's places.
constexpr core::TilePoint CITY_0{ .column = 2, .row = 2 };
constexpr core::TilePoint CITY_1{ .column = 20, .row = 12 };
constexpr core::TilePoint CITY_2{ .column = 34, .row = 4 };
constexpr core::TilePoint DANGER{ .column = 37, .row = 18 };

/// A pair of tiles to route between, named for why it is there.
struct Pair {
    /// Why the pair is a sample.
    const char* name = "";
    /// Where the route starts.
    core::TilePoint from;
    /// Where it ends.
    core::TilePoint to;
};

/// Six routes over the hand-drawn map, each showing the search something of its own.
constexpr std::array<Pair, 6> HAND_PAIRS{ {
    { .name = "along the road", .from = CITY_0, .to = CITY_2 },
    { .name = "back along the road", .from = CITY_2, .to = CITY_0 },
    { .name = "through the ridge's gap", .from = { .column = 2, .row = 16 }, .to = CITY_1 },
    { .name = "round the peaks on a sector border", .from = { .column = 27, .row = 6 }, .to = { .column = 27, .row = 10 } },
    { .name = "corner to corner", .from = { .column = 0, .row = 23 }, .to = { .column = 39, .row = 0 } },
    { .name = "inside one sector", .from = { .column = 0, .row = 5 }, .to = { .column = 4, .row = 5 } },
} };

/// What walking `tiles` costs by `profile`, step by step; UNREACHABLE if a step can't be taken.
u32 Cost(const core::WorldMap& map, core::ProfileIndex profile, const std::vector<u32>& tiles) {
    u32 total = 0;
    for (std::size_t step = 1; step < tiles.size(); ++step) {
        const u32 cost = map.StepCost(tiles[step - 1], tiles[step], profile);
        if (cost == core::IMPASSABLE_COST) {
            return core::UNREACHABLE;
        }
        total += cost;
    }
    return total;
}

/// Whether `route`'s tiles start on `from`, end on `target`, can each be stepped onto and cost what it says.
bool Walkable(const core::WorldMap& map, const core::RouteGraph& graph, const core::GraphRoute& route, u32 from, u32 target) {
    const std::vector<u32> tiles = core::RouteTiles(map, graph, route);
    return !tiles.empty() && tiles.front() == from && tiles.back() == target && Cost(map, graph.Profile(), tiles) == route.cost;
}

/// The route from `from` to `target` over `graph`, found with a workspace of its own.
std::optional<core::GraphRoute> Fresh(const core::WorldMap& map, const core::RouteGraph& graph, u32 from, u32 target) {
    core::RouteScratch scratch;
    return core::FindGraphRoute(map, graph, from, target, scratch);
}

/// The route between `pair`'s tiles on the hand-drawn map's `graph`, with a workspace of its own.
std::optional<core::GraphRoute> Fresh(const core::WorldMap& map, const core::RouteGraph& graph, const Pair& pair) {
    return Fresh(map, graph, At(map, pair.from), At(map, pair.to));
}

/// The route `found` holds; the test fails if there is none.
core::GraphRoute Must(const std::optional<core::GraphRoute>& found) {
    REQUIRE(found.has_value());
    return found.value_or(core::GraphRoute{});
}

TEST_CASE("a route's steps are walkable and cost what the route says") {
    const core::WorldMap map = HandMap();
    const core::RouteGraphs graphs = HandGraphs(map);
    const core::RouteGraph& graph = graphs.Of(core::WALKING);
    std::size_t missing = 0;
    std::size_t unwalkable = 0;
    for (const Pair& pair : HAND_PAIRS) {
        CAPTURE(pair.name);
        const std::optional<core::GraphRoute> route = Fresh(map, graph, pair);
        missing += route ? 0 : 1;
        unwalkable += route && Walkable(map, graph, *route, At(map, pair.from), At(map, pair.to)) ? 0 : 1;
    }
    CHECK(missing == 0);
    CHECK(unwalkable == 0);
}

TEST_CASE("a route between two places uses only the graph's edges") {
    const core::WorldMap map = HandMap();
    const core::RouteGraphs graphs = HandGraphs(map);
    const core::RouteGraph& graph = graphs.Of(core::WALKING);
    const std::array<Pair, 3> pairs{ {
        { .name = "city to city", .from = CITY_0, .to = CITY_1 },
        { .name = "city to a danger", .from = CITY_1, .to = DANGER },
        { .name = "city to the start", .from = CITY_2, .to = CITY_0 },
    } };
    std::size_t ownStretches = 0;
    std::size_t stepless = 0;
    for (const Pair& pair : pairs) {
        const core::GraphRoute route = Must(Fresh(map, graph, pair));
        ownStretches += route.head.empty() && route.tail.empty() ? 0 : 1;
        stepless += route.steps.empty() ? 1 : 0;
    }
    CHECK(ownStretches == 0);
    CHECK(stepless == 0);
}

TEST_CASE("a route from open ground has a stretch of its own at that end only") {
    const core::WorldMap map = HandMap();
    const core::RouteGraphs graphs = HandGraphs(map);
    const core::RouteGraph& graph = graphs.Of(core::WALKING);
    const u32 open = At(map, 1, 6);
    REQUIRE(graph.NodeAt(open) == core::NO_NODE);

    const core::GraphRoute out = Must(Fresh(map, graph, open, At(map, CITY_2)));
    REQUIRE(out.head.size() >= 2);
    CHECK(out.head.front() == open);
    CHECK(graph.NodeAt(out.head.back()) != core::NO_NODE);
    CHECK(!out.steps.empty());
    CHECK(out.tail.empty());

    const core::GraphRoute back = Must(Fresh(map, graph, At(map, CITY_2), open));
    CHECK(back.head.empty());
    CHECK(!back.steps.empty());
    REQUIRE(back.tail.size() >= 2);
    CHECK(graph.NodeAt(back.tail.front()) != core::NO_NODE);
    CHECK(back.tail.back() == open);
}

TEST_CASE("a route is the same whatever was asked before it") {
    const core::WorldMap map = HandMap();
    const core::RouteGraphs graphs = HandGraphs(map);
    const core::RouteGraph& graph = graphs.Of(core::WALKING);
    const Pair& asked = HAND_PAIRS[4];
    const core::GraphRoute fresh = Must(Fresh(map, graph, asked));

    core::RouteScratch scratch;
    for (const Pair& pair : HAND_PAIRS) {
        if (&pair != &asked) {
            static_cast<void>(core::FindGraphRoute(map, graph, At(map, pair.from), At(map, pair.to), scratch));
        }
    }
    const core::GraphRoute used = Must(core::FindGraphRoute(map, graph, At(map, asked.from), At(map, asked.to), scratch));
    CHECK(used.steps == fresh.steps);
    CHECK(used.head == fresh.head);
    CHECK(used.tail == fresh.tail);
    CHECK(used.cost == fresh.cost);

    core::Jobs jobs{ 4 };
    const core::RouteGraphs spread = core::RouteGraphs::Build(map, HAND_RULES, { .jobs = &jobs, .sectorsPerChunk = 2 });
    CHECK(Fresh(map, spread.Of(core::WALKING), asked) == fresh);
}

TEST_CASE("a workspace serves a larger graph, a smaller one and the first again") {
    const core::WorldMap hand = HandMap();
    const core::RouteGraphs handGraphs = HandGraphs(hand);
    const core::RouteGraph& small = handGraphs.Of(core::WALKING);
    const core::WorldMap synthetic = test::SyntheticMap(1, 1);
    const core::RouteGraphs syntheticGraphs = core::RouteGraphs::Build(synthetic);
    const core::RouteGraph& large = syntheticGraphs.Of(core::WALKING);
    REQUIRE(large.Nodes() > small.Nodes());
    const u32 smallFrom = At(hand, HAND_PAIRS[4].from);
    const u32 smallTo = At(hand, HAND_PAIRS[4].to);
    const u32 largeFrom = synthetic.TileIndex(synthetic.Places()[0].tile);
    const u32 largeTo = synthetic.TileIndex(synthetic.Places()[1].tile);

    core::RouteScratch scratch;
    const core::GraphRoute first = Must(core::FindGraphRoute(hand, small, smallFrom, smallTo, scratch));
    const std::optional<core::GraphRoute> larger = core::FindGraphRoute(synthetic, large, largeFrom, largeTo, scratch);
    const std::optional<core::GraphRoute> again = core::FindGraphRoute(hand, small, smallFrom, smallTo, scratch);
    REQUIRE(larger.has_value());
    CHECK(first == Fresh(hand, small, smallFrom, smallTo));
    CHECK(larger == Fresh(synthetic, large, largeFrom, largeTo));
    CHECK(again == first);
}

TEST_CASE("a search counts what it popped and relaxed") {
    const core::WorldMap map = HandMap();
    const core::RouteGraphs graphs = HandGraphs(map);
    const core::RouteGraph& graph = graphs.Of(core::WALKING);
    core::RouteScratch scratch;
    // City 0's sector, the sector of the ridge's gap and city 1's.
    const core::GraphRoute route = Must(core::FindGraphRoute(map, graph, At(map, CITY_0), At(map, CITY_1), scratch));
    const core::SearchCounts across = scratch.Counts();
    CHECK(across.popped >= route.steps.size() + 1);
    CHECK(across.popped <= graph.Nodes());
    CHECK(across.relaxed >= route.steps.size());

    const u32 tile = At(map, CITY_1);
    REQUIRE(core::FindGraphRoute(map, graph, tile, tile, scratch).has_value());
    CHECK(scratch.Counts().popped == 0);
    CHECK(scratch.Counts().relaxed == 0);
}

TEST_CASE("a start and an end on one tile, in one sector and on a gate work") {
    const core::WorldMap map = HandMap();
    const core::RouteGraphs graphs = HandGraphs(map);
    const core::RouteGraph& graph = graphs.Of(core::WALKING);

    const u32 tile = At(map, 5, 5);
    const core::GraphRoute still = Must(Fresh(map, graph, tile, tile));
    CHECK(still.head == std::vector<u32>{ tile });
    CHECK(still.steps.empty());
    CHECK(still.tail.empty());
    CHECK(still.cost == 0);

    // Four meadow steps east, with no node on the way.
    const core::GraphRoute inside = Must(Fresh(map, graph, At(map, 0, 5), At(map, 4, 5)));
    CHECK(inside.head == std::vector<u32>{ At(map, 0, 5), At(map, 1, 5), At(map, 2, 5), At(map, 3, 5), At(map, 4, 5) });
    CHECK(inside.steps.empty());
    CHECK(inside.tail.empty());
    CHECK(inside.cost == 4 * MEADOW_COST);

    // The road's gate onto sector 1 at (8, 2): five road steps to the border and one over it.
    const u32 gate = At(map, 8, 2);
    REQUIRE(graph.NodeAt(gate) != core::NO_NODE);
    const core::GraphRoute onGate = Must(Fresh(map, graph, At(map, CITY_0), gate));
    CHECK(onGate.head.empty());
    CHECK(onGate.steps.size() == 2);
    CHECK(onGate.tail.empty());
    CHECK(onGate.cost == 6 * ROAD_COST);
}

TEST_CASE("a route between the halves of a split sector leaves it and comes back") {
    // Peaks down column 2 of sectors 0 and 2 part sector 0's west from its east; the gap is in sector 4, beyond the block
    // of sectors round sector 0, so only the graph's edges reach round.
    const core::WorldMap map = MustBuild(Source(
        { "0.^.....",
          "..^.....",
          "..^.....",
          "..^.....",
          "..^.....",
          "..^.....",
          "..^.....",
          "..^.....",
          "........",
          "........",
          "........",
          "........" }
    ));
    const core::RouteGraphs graphs = core::RouteGraphs::Build(map, SMALL_RULES);
    const core::RouteGraph& graph = graphs.Of(core::WALKING);
    const u32 from = At(map, 1, 1);
    const u32 target = At(map, 3, 1);
    REQUIRE(core::SectorOf(map, SMALL_RULES, from) == core::SectorOf(map, SMALL_RULES, target));
    const core::GraphRoute route = Must(Fresh(map, graph, from, target));
    CHECK(!route.steps.empty());
    CHECK(Walkable(map, graph, route, from, target));
}

TEST_CASE("an end that can't be entered or reached gives no route") {
    const core::WorldMap map = HandMap();
    const core::RouteGraphs graphs = HandGraphs(map);
    CHECK_FALSE(Fresh(map, graphs.Of(core::WALKING), At(map, CITY_0), At(map, 12, 5)).has_value());

    const core::WorldMap island =
        MustBuild(Source({ "0.......", "........", "...^^^..", "...^.^..", "...^^^..", "........", "........", "........" }));
    const core::RouteGraphs islandGraphs = core::RouteGraphs::Build(island, SMALL_RULES);
    CHECK_FALSE(Fresh(island, islandGraphs.Of(core::WALKING), At(island, 0, 0), At(island, 4, 3)).has_value());

    core::WorldMapSource source = HandSource();
    // A cart keeps to the roads, so city 1 on open meadow is out of its reach.
    source.profiles.push_back(
        { .key = "cart", .ticksPerCost = 1, .kindCosts = { core::IMPASSABLE_COST, core::IMPASSABLE_COST }, .features = {} }
    );
    const core::WorldMap carts = MustBuild(std::move(source));
    const core::RouteGraphs cartGraphs = HandGraphs(carts);
    REQUIRE(cartGraphs.Of(ADDED).PlaceNode(1) == core::NO_NODE);
    CHECK_FALSE(Fresh(carts, cartGraphs.Of(ADDED), At(carts, CITY_0), At(carts, CITY_1)).has_value());
}

TEST_CASE("a route from a tile that can't be entered leaves it") {
    const core::WorldMap map = HandMap();
    const core::RouteGraphs graphs = HandGraphs(map);
    const core::RouteGraph& graph = graphs.Of(core::WALKING);
    const u32 peak = At(map, 12, 5);
    REQUIRE(map.CostAt(peak, core::WALKING) == core::IMPASSABLE_COST);
    const core::GraphRoute route = Must(Fresh(map, graph, peak, At(map, CITY_1)));
    CHECK(Walkable(map, graph, route, peak, At(map, CITY_1)));
}

TEST_CASE("the way there and the way back each cost their own steps") {
    const core::WorldMap map = HandMap();
    const core::RouteGraphs graphs = HandGraphs(map);
    const core::RouteGraph& graph = graphs.Of(core::WALKING);
    // City 2 stands on the road, the other end on meadow, so the last step differs each way.
    const u32 city = At(map, CITY_2);
    const u32 open = At(map, 30, 12);
    const core::GraphRoute there = Must(Fresh(map, graph, city, open));
    const core::GraphRoute back = Must(Fresh(map, graph, open, city));
    CHECK(Walkable(map, graph, there, city, open));
    CHECK(Walkable(map, graph, back, open, city));
    CHECK(there.cost != back.cost);
}

TEST_CASE("an end one tile from a sector border routes no dearer than through the neighbouring sector's gate") {
    // Sectors of 4 in one row; every border's gates are on rows 0 and 2, and the end on row 3 stands on sector 3's west
    // edge.
    const core::WorldMap map =
        MustBuild(Source({ "...................0", "....................", "....................", "...................." }));
    const core::RouteGraphs graphs = core::RouteGraphs::Build(map, SMALL_RULES);
    const core::RouteGraph& graph = graphs.Of(core::WALKING);
    const u32 from = At(map, 0, 3);
    const u32 target = At(map, 12, 3);
    REQUIRE(graph.NodeAt(target) == core::NO_NODE);
    const core::GraphRoute route = Must(Fresh(map, graph, from, target));
    CHECK(Walkable(map, graph, route, from, target));
    // Onto sector 1's gate (7, 2) (6 straight and a diagonal, 74), over to (8, 2) (10), then from there straight to the
    // end (3 straight and a diagonal, 44), rather than through sector 3's own gate at (12, 2) (134).
    CHECK(route.cost == 128);

    // The synthetic map's wary pair that paid most for ends reached only through their own sectors' gates.
    const core::WorldMap synthetic = test::SyntheticMap(1, 1);
    const core::RouteGraphs syntheticGraphs = core::RouteGraphs::Build(synthetic);
    constexpr u32 SYNTHETIC_FROM = 12427;
    constexpr u32 SYNTHETIC_TO = 2074;
    const core::GraphRoute dearest = Must(Fresh(synthetic, syntheticGraphs.Of(WARY), SYNTHETIC_FROM, SYNTHETIC_TO));
    const u32 cheapest = Cost(synthetic, WARY, core::FindRoute(synthetic, WARY, SYNTHETIC_FROM, SYNTHETIC_TO));
    CAPTURE(dearest.cost);
    CAPTURE(cheapest);
    CHECK(u64{ dearest.cost } * 100 <= u64{ cheapest } * 105);
}

/// The purposes of the synthetic routes' draws: each end's tile and place.
constexpr u32 PURPOSE_FROM = 1;
constexpr u32 PURPOSE_TO = 2;
/// How many pairs the synthetic map's routes are drawn for, each routed both ways.
constexpr u32 SYNTHETIC_PAIRS = 10;

/// End `end` (0 the start, 1 the target) of pair `pair` on `map`: pairs alternate between places and tiles `profile` can
/// enter, redrawn until one is.
u32 DrawEnd(const core::WorldMap& map, core::ProfileIndex profile, u32 pair, u32 end) {
    const u32 purpose = end == 0 ? PURPOSE_FROM : PURPOSE_TO;
    const bool place = (pair + end) % 2 == 0;
    const auto bound = static_cast<u32>(place ? map.Places().size() : u64{ map.Columns() } * map.Rows());
    for (u32 draw = 0;; ++draw) {
        const u32 picked = core::ToBelow(core::Random(1, pair, profile, purpose, draw), bound);
        const u32 tile = place ? map.TileIndex(map.Places()[picked].tile) : picked;
        if (map.CostAt(tile, profile) != core::IMPASSABLE_COST) {
            return tile;
        }
    }
}

TEST_CASE("routes on a synthetic map cost within 5% of the cheapest, each way") {
    const core::WorldMap map = test::SyntheticMap(1, 1);
    const core::RouteGraphs graphs = core::RouteGraphs::Build(map);
    core::RouteScratch scratch;
    for (const core::ProfileIndex profile : { core::WALKING, WARY }) {
        CAPTURE(profile);
        const core::RouteGraph& graph = graphs.Of(profile);
        i64 worst = std::numeric_limits<i64>::min();
        u32 worstFrom = 0;
        u32 worstTo = 0;
        u32 worstCost = 0;
        u32 worstCheapest = 0;
        std::size_t missing = 0;
        for (u32 pair = 0; pair < SYNTHETIC_PAIRS; ++pair) {
            const u32 first = DrawEnd(map, profile, pair, 0);
            const u32 second = DrawEnd(map, profile, pair, 1);
            for (const auto& [from, target] : { std::pair{ first, second }, std::pair{ second, first } }) {
                const std::vector<u32> cheapest = core::FindRoute(map, profile, from, target);
                const std::optional<core::GraphRoute> route = core::FindGraphRoute(map, graph, from, target, scratch);
                missing += cheapest.empty() || route ? 0 : 1;
                if (cheapest.empty() || !route) {
                    continue;
                }
                const u32 best = Cost(map, profile, cheapest);
                const i64 over = (i64{ route->cost } * 100) - (i64{ best } * 105);
                if (over > worst) {
                    worst = over;
                    worstFrom = from;
                    worstTo = target;
                    worstCost = route->cost;
                    worstCheapest = best;
                }
            }
        }
        CAPTURE(worstFrom);
        CAPTURE(worstTo);
        CAPTURE(worstCost);
        CAPTURE(worstCheapest);
        CHECK(missing == 0);
        CHECK(worst <= 0);
    }
}

} // namespace
