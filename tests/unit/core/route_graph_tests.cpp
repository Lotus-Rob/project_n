#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/jobs.hpp"
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
    bool blocked = false;
    for (std::size_t step = 1; step < way.size(); ++step) {
        const u32 cost = map.StepCost(way[step - 1], way[step], core::WALKING);
        blocked = blocked || cost == core::IMPASSABLE_COST;
        total += cost;
    }
    REQUIRE(!blocked);
    return total;
}

/// The hand-drawn map's routing rules: sectors of 8, so it is 5 sectors across and 3 down.
constexpr core::RoutingRules HAND_RULES{ .sectorSize = 8, .gateSpacing = 4 };
/// The hand-drawn map's wary profile, which pays for walking near the danger.
constexpr core::ProfileIndex WARY = 1;
/// What the wary profile adds to a tile near the danger.
constexpr u16 WARY_DANGER_COST = 40;
/// The hand-drawn map's danger tile, in its corner sector; its reach of 2 just touches row 16, the ring of the sector above.
constexpr core::TilePoint DANGER_TILE{ .column = 37, .row = 18 };
/// The sector above the danger's: alike inside for both profiles, unlike on its ring.
constexpr u32 BESIDE_DANGER = 9;
/// The profile a test adds after the hand-drawn map's two.
constexpr core::ProfileIndex ADDED = 2;

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
core::WorldMap HandMap() {
    return MustBuild(HandSource());
}

/// How many sectors the hand-drawn map has.
u32 HandSectors(const core::WorldMap& map) {
    return core::SectorColumns(map, HAND_RULES) * core::SectorRows(map, HAND_RULES);
}

/// The hand-drawn map's graphs built with `options`.
core::RouteGraphs HandGraphs(const core::WorldMap& map, core::BuildOptions options = {}) {
    return core::RouteGraphs::Build(map, HAND_RULES, options);
}

/// Whether every profile's graph of `first` is the same as `second`'s.
bool AllSame(const core::WorldMap& map, const core::RouteGraphs& first, const core::RouteGraphs& second) {
    bool same = true;
    for (std::size_t profile = 0; profile < map.Profiles().size(); ++profile) {
        const auto index = static_cast<core::ProfileIndex>(profile);
        same = same && first.Of(index).SameAs(second.Of(index));
    }
    return same;
}

/// The hand-drawn map's graphs built on a job system of `workers` workers, two sectors a chunk so they spread.
core::RouteGraphs HandGraphsOn(const core::WorldMap& map, u32 workers) {
    core::Jobs jobs{ workers };
    return HandGraphs(map, { .jobs = &jobs, .sectorsPerChunk = 2, .share = true });
}

/// What walking `tiles` costs by `profile`, step by step, front to back or (`back`) back to front; UNREACHABLE if a step
/// can't be taken.
u32 WalkedBy(const core::WorldMap& map, core::ProfileIndex profile, const std::vector<u32>& tiles, bool back) {
    u32 total = 0;
    for (std::size_t step = 1; step < tiles.size(); ++step) {
        const u32 cost =
            back ? map.StepCost(tiles[step], tiles[step - 1], profile) : map.StepCost(tiles[step - 1], tiles[step], profile);
        if (cost == core::IMPASSABLE_COST) {
            return core::UNREACHABLE;
        }
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

TEST_CASE("each crossing's far tile is a node of the sector it enters, short sectors included") {
    // 9 x 6 in sectors of 4 leaves a last column of sectors one tile wide and a last row two tiles high; the peaks split
    // borders into runs on both sides of that column.
    const core::WorldMap map =
        MustBuild(Source({ "0........", "...^.....", "........^", "....^....", ".........", "...^....." }));
    std::size_t crossings = 0;
    std::size_t intoLastColumn = 0;
    std::size_t missing = 0;
    for (u32 sector = 0; sector < core::SectorColumns(map, RULES) * core::SectorRows(map, RULES); ++sector) {
        const core::SectorGraph graph = Build(map, sector);
        for (const core::SectorCrossing& crossing : graph.crossings) {
            const u32 far = graph.nodes.at(crossing.node) + (crossing.south ? map.Columns() : 1);
            const core::SectorGraph entered = Build(map, core::SectorOf(map, RULES, far));
            missing += std::ranges::binary_search(entered.nodes, far) ? 0 : 1;
            intoLastColumn += map.PointOf(far).column == 8 ? 1 : 0;
            ++crossings;
        }
    }
    CHECK(missing == 0);
    // Rows 0 and 3 across into sector 2 (row 2 is peaks there), row 4 into sector 5, and sector 2's south gate.
    CHECK(intoLastColumn == 4);
    CHECK(crossings > intoLastColumn);
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

TEST_CASE("the graphs are the same on 1 and 4 workers and without a job system") {
    const core::WorldMap map = HandMap();
    const core::RouteGraphs alone = HandGraphs(map, { .jobs = nullptr, .sectorsPerChunk = 2, .share = true });
    REQUIRE(alone.Of(core::WALKING).Nodes() > 0);
    CHECK(AllSame(map, HandGraphsOn(map, 1), alone));
    CHECK(AllSame(map, HandGraphsOn(map, 4), alone));
}

TEST_CASE("the graphs are the same whatever the chunk size and shared or not") {
    const core::WorldMap map = HandMap();
    const core::RouteGraphs graphs = HandGraphs(map);
    REQUIRE(graphs.Of(core::WALKING).Nodes() > 0);
    CHECK(AllSame(map, HandGraphs(map, { .jobs = nullptr, .sectorsPerChunk = 1, .share = true }), graphs));
    CHECK(AllSame(map, HandGraphs(map, { .jobs = nullptr, .sectorsPerChunk = 100, .share = true }), graphs));
    CHECK(AllSame(map, HandGraphs(map, { .jobs = nullptr, .sectorsPerChunk = 2, .share = false }), graphs));
}

TEST_CASE("a graph assembled from sectors built in any order is the same") {
    const core::WorldMap map = HandMap();
    const core::RouteGraphs graphs = HandGraphs(map);
    core::SectorWalk walk;
    for (const core::ProfileIndex profile : { core::WALKING, WARY }) {
        std::vector<core::SectorGraph> sectors(HandSectors(map));
        for (u32 sector = HandSectors(map); sector-- > 0;) {
            sectors[sector] = core::BuildSector(map, profile, HAND_RULES, sector, walk);
        }
        const core::RouteGraph graph = core::RouteGraph::Assemble(map, profile, HAND_RULES, sectors);
        CHECK(graph.Profile() == profile);
        CHECK(graph.SameAs(graphs.Of(profile)));
    }
}

TEST_CASE("rebuilding one sector alone gives the same sector") {
    const core::WorldMap map = HandMap();
    const core::RouteGraphs graphs = HandGraphs(map);
    core::SectorWalk walk;
    std::vector<core::SectorGraph> sectors(HandSectors(map));
    for (u32 sector = 0; sector < sectors.size(); ++sector) {
        sectors[sector] = core::BuildSector(map, WARY, HAND_RULES, sector, walk);
    }
    // Sector 6 holds the ridge's gap, so its graph has nodes on both sides.
    constexpr u32 REBUILT = 6;
    core::SectorWalk fresh;
    sectors[REBUILT] = core::BuildSector(map, WARY, HAND_RULES, REBUILT, fresh);
    REQUIRE(!sectors[REBUILT].edges.empty());
    CHECK(core::RouteGraph::Assemble(map, WARY, HAND_RULES, sectors).SameAs(graphs.Of(WARY)));
}

TEST_CASE("profiles that cost the same on a sector share its build") {
    const core::WorldMap map = HandMap();
    const u32 sectors = HandSectors(map);
    REQUIRE(map.Profiles().size() == 2);
    // Only the danger's sector and the one whose ring its reach touches cost differently for the wary.
    CHECK(HandGraphs(map).SectorsBuilt() == sectors + 2);
    CHECK(HandGraphs(map, { .jobs = nullptr, .sectorsPerChunk = 2, .share = false }).SectorsBuilt() == sectors * 2);
}

TEST_CASE("a sector whose ring costs differently is not shared") {
    const core::WorldMap map = HandMap();
    const core::TileSpan span = core::SectorSpan(map, HAND_RULES, BESIDE_DANGER);
    u32 unlikeInside = 0;
    for (u32 row = span.firstRow; row <= span.lastRow; ++row) {
        for (u32 column = span.firstColumn; column <= span.lastColumn; ++column) {
            const u32 tile = At(map, column, row);
            unlikeInside += map.CostAt(tile, core::WALKING) == map.CostAt(tile, WARY) ? 0 : 1;
        }
    }
    REQUIRE(unlikeInside == 0);
    CHECK_FALSE(core::SectorCostsAgree(map, HAND_RULES, BESIDE_DANGER, core::WALKING, WARY));
    CHECK(core::SectorCostsAgree(map, HAND_RULES, 0, core::WALKING, WARY));
    // On foot the road is the cheapest gate south onto the reach's edge; the wary pays for the reach there and crosses
    // beside it, so a copy would be wrong.
    core::SectorWalk walk;
    CHECK(
        core::BuildSector(map, core::WALKING, HAND_RULES, BESIDE_DANGER, walk) !=
        core::BuildSector(map, WARY, HAND_RULES, BESIDE_DANGER, walk)
    );
    const core::RouteGraphs shared = HandGraphs(map);
    const core::RouteGraphs unshared = HandGraphs(map, { .jobs = nullptr, .sectorsPerChunk = 1, .share = false });
    CHECK(shared.Of(core::WALKING).SameAs(unshared.Of(core::WALKING)));
    CHECK(shared.Of(WARY).SameAs(unshared.Of(WARY)));
}

TEST_CASE("each crossing joins the nodes either side of its border") {
    const core::WorldMap map = HandMap();
    const core::RouteGraphs graphs = HandGraphs(map);
    core::SectorWalk walk;
    for (const core::ProfileIndex profile : { core::WALKING, WARY }) {
        const core::RouteGraph& graph = graphs.Of(profile);
        // Edge IDs run sector by sector, its own edges first, so each sector's crossings are found by position alone.
        std::size_t next = 0;
        std::size_t crossings = 0;
        std::size_t listed = 0;
        std::size_t apart = 0;
        for (u32 sector = 0; sector < HandSectors(map); ++sector) {
            const core::SectorGraph built = core::BuildSector(map, profile, HAND_RULES, sector, walk);
            next += built.edges.size();
            listed += built.crossings.size();
            for (std::size_t crossing = 0; crossing < built.crossings.size() && next < graph.Edges().size(); ++crossing) {
                const core::GraphEdge& edge = graph.Edges()[next++];
                const core::TilePoint near = map.PointOf(graph.NodeTile(edge.from));
                const core::TilePoint far = map.PointOf(graph.NodeTile(edge.to));
                const bool east = far.column == near.column + 1 && far.row == near.row;
                const bool south = far.column == near.column && far.row == near.row + 1;
                const bool leaves = edge.sector == sector &&
                                    core::SectorOf(map, HAND_RULES, graph.NodeTile(edge.from)) == sector &&
                                    core::SectorOf(map, HAND_RULES, graph.NodeTile(edge.to)) != sector;
                apart += (east || south) && leaves ? 0 : 1;
                ++crossings;
            }
        }
        CHECK(crossings > 0);
        CHECK(crossings == listed);
        CHECK(apart == 0);
        CHECK(next == graph.Edges().size());
    }
}

TEST_CASE("a node's links name each of its edges once, each way's cost") {
    const core::WorldMap map = HandMap();
    const core::RouteGraphs graphs = HandGraphs(map);
    const core::RouteGraph& graph = graphs.Of(WARY);
    // Two cities, the danger, the wary's gate south beside the danger's reach and the last node.
    const std::vector<u32> named{
        graph.PlaceNode(0), graph.PlaceNode(1), graph.PlaceNode(3), graph.NodeAt(At(map, 36, 15)), graph.Nodes() - 1
    };
    REQUIRE(std::ranges::all_of(named, [&](u32 node) { return node < graph.Nodes(); }));
    std::size_t linkless = 0;
    std::size_t wrong = 0;
    std::size_t unlisted = 0;
    for (const u32 node : named) {
        const std::span<const core::GraphLink> links = graph.LinksOf(node);
        std::vector<u32> touching;
        for (u32 edge = 0; edge < graph.Edges().size(); ++edge) {
            if (graph.Edges()[edge].from == node || graph.Edges()[edge].to == node) {
                touching.push_back(edge);
            }
        }
        std::vector<u32> listed;
        for (const core::GraphLink& link : links) {
            const core::GraphEdge& edge = graph.Edges()[link.edge];
            const bool out = !link.reversed && edge.from == node && edge.to == link.node && link.cost == edge.cost;
            const bool back = link.reversed && edge.to == node && edge.from == link.node && link.cost == edge.costBack;
            wrong += out || back ? 0 : 1;
            listed.push_back(link.edge);
        }
        linkless += links.empty() ? 1 : 0;
        unlisted += listed == touching ? 0 : 1;
    }
    CHECK(linkless == 0);
    CHECK(wrong == 0);
    CHECK(unlisted == 0);
}

TEST_CASE("an edge's tiles start and end on its nodes and cost what the edge says each way") {
    const core::WorldMap map = HandMap();
    const core::RouteGraphs graphs = HandGraphs(map);
    core::SectorWalk walk;
    std::vector<u32> tiles{ 99 };
    for (const core::ProfileIndex profile : { core::WALKING, WARY }) {
        const core::RouteGraph& graph = graphs.Of(profile);
        REQUIRE(!graph.Edges().empty());
        std::size_t badEnds = 0;
        u32 worstCost = 0;
        u32 worstBack = 0;
        for (u32 id = 0; id < graph.Edges().size(); ++id) {
            const core::GraphEdge& edge = graph.Edges()[id];
            core::EdgeTiles(map, graph, id, walk, tiles);
            const bool ends =
                tiles.size() >= 2 && tiles.front() == graph.NodeTile(edge.from) && tiles.back() == graph.NodeTile(edge.to);
            badEnds += ends ? 0 : 1;
            const u32 cost = WalkedBy(map, profile, tiles, false);
            const u32 back = WalkedBy(map, profile, tiles, true);
            worstCost = std::max(worstCost, cost > edge.cost ? cost - edge.cost : edge.cost - cost);
            worstBack = std::max(worstBack, back > edge.costBack ? back - edge.costBack : edge.costBack - back);
        }
        CHECK(badEnds == 0);
        CHECK(worstCost == 0);
        CHECK(worstBack == 0);
    }
}

TEST_CASE("a place's node is its tile's, and a place its profile can't enter has none") {
    core::WorldMapSource source = HandSource();
    // A cart keeps to the roads, so it reaches cities 0 and 2 on the road's ends but not city 1 or the danger.
    source.profiles.push_back(
        { .key = "cart", .ticksPerCost = 1, .kindCosts = { core::IMPASSABLE_COST, core::IMPASSABLE_COST }, .features = {} }
    );
    const core::WorldMap map = MustBuild(std::move(source));
    const core::RouteGraphs graphs = HandGraphs(map);
    const core::RouteGraph& walking = graphs.Of(core::WALKING);
    std::size_t misplaced = 0;
    for (u16 place = 0; place < map.Places().size(); ++place) {
        const u32 tile = map.TileIndex(map.Places()[place].tile);
        const u32 node = walking.PlaceNode(place);
        misplaced += node != core::NO_NODE && node == walking.NodeAt(tile) && walking.NodeTile(node) == tile ? 0 : 1;
    }
    CHECK(misplaced == 0);
    CHECK(walking.NodeAt(At(map, 12, 5)) == core::NO_NODE); // a peak
    CHECK(walking.NodeAt(At(map, 1, 1)) == core::NO_NODE);  // open meadow inside a sector

    const core::RouteGraph& cart = graphs.Of(ADDED);
    CHECK(cart.NodeTile(cart.PlaceNode(0)) == At(map, 2, 2));
    CHECK(cart.NodeTile(cart.PlaceNode(2)) == At(map, 34, 4));
    CHECK(cart.PlaceNode(1) == core::NO_NODE);
    CHECK(cart.PlaceNode(3) == core::NO_NODE);
}

TEST_CASE("a profile that can enter nothing has an empty graph") {
    core::WorldMapSource source = HandSource();
    source.profiles.push_back(
        { .key = "stone",
          .ticksPerCost = 1,
          .kindCosts = { core::IMPASSABLE_COST, core::IMPASSABLE_COST },
          .features = { .road = core::IMPASSABLE_COST,
                        .river = core::IMPASSABLE_COST,
                        .bridge = core::IMPASSABLE_COST,
                        .ford = core::IMPASSABLE_COST,
                        .danger = 0 } }
    );
    const core::WorldMap map = MustBuild(std::move(source));
    const core::RouteGraphs graphs = HandGraphs(map);
    const core::RouteGraph& stone = graphs.Of(ADDED);
    CHECK(stone.Nodes() == 0);
    CHECK(stone.Edges().empty());
    CHECK(stone.SectorNodes(0).empty());
    CHECK(stone.NodeAt(At(map, 2, 2)) == core::NO_NODE);
    CHECK(stone.PlaceNode(0) == core::NO_NODE);
}

} // namespace
