#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/journey.hpp"
#include "core/route_graph.hpp"
#include "core/segments.hpp"
#include "core/world_map.hpp"

#include <doctest/doctest.h>

namespace {

constexpr f32 TILE = 256.0F;
constexpr u16 MEADOW_COST = 10;
constexpr u16 ROAD_COST = 4;
constexpr u8 MEADOW = 0;
constexpr u8 PEAKS = 1;

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

/// The hand-drawn map's routing rules: sectors of 8, so it is 5 sectors across and 3 down.
constexpr core::RoutingRules HAND_RULES{ .sectorSize = 8, .gateSpacing = 4 };
/// What the wary profile adds to a tile near the danger.
constexpr u16 WARY_DANGER_COST = 40;
/// The hand-drawn map's danger tile, in its corner sector; its reach of 2 just touches row 16, the ring of the sector above.
constexpr core::TilePoint DANGER_TILE{ .column = 37, .row = 18 };

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

/// The hand-drawn map's walking graph, which the tests read their edges from.
struct Fixture {
    core::WorldMap map = HandMap();
    core::RouteGraphs graphs = core::RouteGraphs::Build(map, HAND_RULES);

    /// The walking graph.
    [[nodiscard]] const core::RouteGraph& Graph() const {
        return graphs.Of(core::WALKING);
    }
};

/// Whether edge `edge` of `graph` is a step over a border rather than a way inside a sector.
bool IsCrossing(const core::RouteGraph& graph, u32 edge) {
    const core::GraphEdge& way = graph.Edges()[edge];
    return way.to - graph.FirstNode(way.sector) >= graph.SectorNodes(way.sector).size();
}

/// The first edge inside a sector whose cost differs by direction.
u32 AsymmetricEdge(const core::RouteGraph& graph) {
    for (u32 edge = 0; edge < graph.Edges().size(); ++edge) {
        const core::GraphEdge& way = graph.Edges()[edge];
        if (!IsCrossing(graph, edge) && way.cost != way.costBack) {
            return edge;
        }
    }
    FAIL("the map has no edge costing differently each way");
    return 0;
}

/// Eight edges to make together, unsorted with one repeated: three inside one sector sharing a `from` node, two
/// crossings and three more inside sectors.
std::vector<u32> NamedEdges(const core::RouteGraph& graph) {
    std::vector<u32> sharing;
    std::vector<u32> crossings;
    std::vector<u32> others;
    for (u32 edge = 0; edge < graph.Edges().size(); ++edge) {
        const core::GraphEdge& way = graph.Edges()[edge];
        if (IsCrossing(graph, edge)) {
            crossings.push_back(edge);
            continue;
        }
        const bool sameNode = !sharing.empty() && graph.Edges()[sharing.front()].sector == way.sector &&
                              graph.Edges()[sharing.front()].from == way.from;
        if (sharing.size() < 3 && (sharing.empty() || sameNode)) {
            sharing.push_back(edge);
        }
        else if (sharing.size() == 3 && edge % 29 == 0) {
            others.push_back(edge);
        }
    }
    REQUIRE(sharing.size() == 3);
    REQUIRE(others.size() >= 3);
    REQUIRE(crossings.size() >= 2);
    return { others[1], crossings[crossings.size() / 2], sharing[2], sharing[0], others[0], crossings[0], sharing[1], others[2],
             sharing[0] };
}

/// Whether the two walk tables hold the same marks, field by field, the numbers to the bit.
bool SameMarks(const std::vector<core::LegWalkMark>& first, const std::vector<core::LegWalkMark>& second) {
    return std::ranges::equal(first, second, [](const core::LegWalkMark& left, const core::LegWalkMark& right) {
        return std::bit_cast<u64>(left.walked) == std::bit_cast<u64>(right.walked) &&
               std::bit_cast<u64>(left.length) == std::bit_cast<u64>(right.length) && left.before == right.before &&
               left.after == right.after;
    });
}

/// Whether two segments hold the same tiles, costs and marks.
bool SameSegment(const core::Segment& first, const core::Segment& second) {
    return first.tiles == second.tiles && first.cost == second.cost && first.costBack == second.costBack &&
           SameMarks(first.walk, second.walk) && SameMarks(first.walkBack, second.walkBack);
}

TEST_CASE("a segment's costs are its edge's each way") {
    const Fixture fixture;
    const u32 edge = AsymmetricEdge(fixture.Graph());
    core::SectorWalk walk;
    const core::Segment segment = core::MakeSegment(fixture.map, fixture.Graph(), edge, walk);
    const core::GraphEdge& way = fixture.Graph().Edges()[edge];
    REQUIRE(segment.tiles.size() >= 2);
    REQUIRE(segment.cost.size() == segment.tiles.size());
    REQUIRE(segment.costBack.size() == segment.tiles.size());
    CHECK(segment.tiles.front() == fixture.Graph().NodeTile(way.from));
    CHECK(segment.tiles.back() == fixture.Graph().NodeTile(way.to));
    CHECK(segment.cost.front() == 0);
    CHECK(segment.cost.back() == way.cost);
    CHECK(segment.costBack.back() == 0);
    CHECK(segment.costBack.front() == way.costBack);
    u32 forward = 0;
    u32 back = 0;
    std::size_t wrong = 0;
    for (std::size_t index = 1; index < segment.tiles.size(); ++index) {
        forward += fixture.map.StepCost(segment.tiles[index - 1], segment.tiles[index], core::WALKING);
        wrong += segment.cost[index] == forward ? 0 : 1;
    }
    for (std::size_t index = segment.tiles.size() - 1; index-- > 0;) {
        back += fixture.map.StepCost(segment.tiles[index + 1], segment.tiles[index], core::WALKING);
        wrong += segment.costBack[index] == back ? 0 : 1;
    }
    CHECK(wrong == 0);
}

TEST_CASE("a segment's walk marks are a leg's over its tiles, each way") {
    const Fixture fixture;
    core::SectorWalk walk;
    const core::Segment segment = core::MakeSegment(fixture.map, fixture.Graph(), AsymmetricEdge(fixture.Graph()), walk);
    const core::Leg leg =
        core::MakeTileLeg(fixture.map, core::WALKING, 0, fixture.map.TileCentre(segment.tiles.front()), segment.tiles);
    CHECK(SameMarks(segment.walk, core::MakeLegWalk(fixture.map, leg).marks));
    const std::vector<u32> reversed(segment.tiles.rbegin(), segment.tiles.rend());
    const core::Leg back = core::MakeTileLeg(fixture.map, core::WALKING, 0, fixture.map.TileCentre(reversed.front()), reversed);
    CHECK(SameMarks(segment.walkBack, core::MakeLegWalk(fixture.map, back).marks));
    CHECK(segment.walk.size() == segment.tiles.size());
    CHECK(segment.WalkBytes() > 0);
    CHECK(segment.Bytes() > segment.WalkBytes());
}

TEST_CASE("a crossing's segment is its two tiles") {
    const Fixture fixture;
    u32 edge = 0;
    while (!IsCrossing(fixture.Graph(), edge)) {
        ++edge;
    }
    core::SectorWalk walk;
    const core::Segment segment = core::MakeSegment(fixture.map, fixture.Graph(), edge, walk);
    const core::GraphEdge& way = fixture.Graph().Edges()[edge];
    CHECK(segment.tiles == std::vector<u32>{ fixture.Graph().NodeTile(way.from), fixture.Graph().NodeTile(way.to) });
    CHECK(segment.cost == std::vector<u32>{ 0, way.cost });
    CHECK(segment.costBack == std::vector<u32>{ way.costBack, 0 });
}

TEST_CASE("segments made together are the segments made alone") {
    const Fixture fixture;
    const std::vector<u32> edges = NamedEdges(fixture.Graph());
    core::SegmentStore store(fixture.map, fixture.Graph());
    store.Add(edges);
    core::SectorWalk walk;
    std::size_t different = 0;
    for (const u32 edge : edges) {
        const core::Segment* found = store.Find(edge);
        REQUIRE(found != nullptr);
        different += SameSegment(*found, core::MakeSegment(fixture.map, fixture.Graph(), edge, walk)) ? 0 : 1;
    }
    CHECK(different == 0);
    CHECK(store.Made() == 8);
}

TEST_CASE("the store makes a segment once and never moves it") {
    const Fixture fixture;
    const std::vector<u32> edges = NamedEdges(fixture.Graph());
    core::SegmentStore store(fixture.map, fixture.Graph());
    CHECK(store.Find(edges.front()) == nullptr);
    CHECK(store.Made() == 0);
    store.Add(std::span<const u32>(edges).first(3));
    const core::Segment* first = store.Find(edges.front());
    REQUIRE(first != nullptr);
    store.Add(std::span<const u32>(edges).subspan(1));
    CHECK(store.Find(edges.front()) == first);
    CHECK(store.Made() == 8);
}

TEST_CASE("the store's bytes grow only with the segments made") {
    const Fixture fixture;
    const std::vector<u32> edges = NamedEdges(fixture.Graph());
    core::SegmentStore store(fixture.map, fixture.Graph());
    const std::size_t none = store.Bytes();
    store.Add(std::span<const u32>(edges).first(2));
    const std::size_t two = store.Bytes();
    store.Add(std::span<const u32>(edges).first(6));
    CHECK(none > 0);
    CHECK(two > none);
    CHECK(store.Bytes() > two);
    CHECK(store.WalkBytes() > 0);
    CHECK(store.WalkBytes() < store.Bytes());
}

} // namespace
