#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/journey.hpp"
#include "core/route_graph.hpp"
#include "core/segments.hpp"
#include "core/world_map.hpp"
#include "support/hand_map.hpp"

#include <doctest/doctest.h>

namespace {

using test::hand::HAND_RULES;
using test::hand::HandMap;

/// The hand-drawn map's walking graph, which the tests read their edges from.
struct Fixture {
    core::WorldMap map = HandMap();
    core::RouteGraphs graphs = core::RouteGraphs::Build(map, HAND_RULES);

    /// The walking graph.
    [[nodiscard]] const core::RouteGraph& Graph() const {
        return graphs.Of(core::WALKING);
    }
};

/// The first edge inside a sector whose cost differs by direction.
u32 AsymmetricEdge(const core::RouteGraph& graph) {
    for (u32 edge = 0; edge < graph.Edges().size(); ++edge) {
        const core::GraphEdge& way = graph.Edges()[edge];
        if (!graph.IsCrossing(edge) && way.cost != way.costBack) {
            return edge;
        }
    }
    FAIL("the map has no edge costing differently each way");
    return 0;
}

/// Eight edges to make together, unsorted with one repeated: three inside one sector sharing a `from` node, two
/// crossings and three more inside other sectors, each in a sector the one before isn't in.
std::vector<u32> NamedEdges(const core::RouteGraph& graph) {
    std::vector<u32> sharing;
    std::vector<u32> crossings;
    std::vector<u32> others;
    for (u32 edge = 0; edge < graph.Edges().size(); ++edge) {
        const core::GraphEdge& way = graph.Edges()[edge];
        if (graph.IsCrossing(edge)) {
            crossings.push_back(edge);
            continue;
        }
        const bool sameNode = !sharing.empty() && graph.Edges()[sharing.front()].sector == way.sector &&
                              graph.Edges()[sharing.front()].from == way.from;
        if (sharing.size() < 3 && (sharing.empty() || sameNode)) {
            sharing.push_back(edge);
        }
        else if (
            sharing.size() == 3 && others.size() < 3 && way.sector != graph.Edges()[sharing.front()].sector &&
            (others.empty() || way.sector != graph.Edges()[others.back()].sector)
        ) {
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
    while (edge < fixture.Graph().Edges().size() && !fixture.Graph().IsCrossing(edge)) {
        ++edge;
    }
    REQUIRE(edge < fixture.Graph().Edges().size());
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
    std::vector<const core::Segment*> before;
    for (const u32 edge : std::span<const u32>(edges).first(3)) {
        before.push_back(store.Find(edge));
        REQUIRE(before.back() != nullptr);
    }
    store.Add(edges);
    std::size_t moved = 0;
    for (std::size_t index = 0; index < before.size(); ++index) {
        moved += store.Find(edges[index]) == before[index] ? 0 : 1;
    }
    CHECK(moved == 0);
    CHECK(store.Made() == 8);
}

TEST_CASE("a place's ways inside its sector and over its block make the segments their own walks give") {
    const Fixture fixture;
    const core::RouteGraph& graph = fixture.Graph();
    const u32 city = graph.PlaceNode(0);
    // City 0's way inside its sector and two of its ways to the sectors round it: one node, two walks over other tiles.
    std::vector<u32> inside;
    std::vector<u32> block;
    for (u32 edge = 0; edge < graph.Edges().size(); ++edge) {
        if (graph.Edges()[edge].from != city || graph.IsCrossing(edge)) {
            continue;
        }
        (graph.IsBlockEdge(edge) ? block : inside).push_back(edge);
    }
    REQUIRE(!inside.empty());
    REQUIRE(block.size() >= 2);
    const std::vector<u32> edges{ inside.front(), block.front(), block.back() };
    core::SegmentStore store(fixture.map, graph);
    store.Add(edges);
    core::SectorWalk walk;
    std::size_t different = 0;
    for (const u32 edge : edges) {
        const core::Segment* found = store.Find(edge);
        REQUIRE(found != nullptr);
        different += SameSegment(*found, core::MakeSegment(fixture.map, graph, edge, walk)) ? 0 : 1;
    }
    CHECK(different == 0);
    CHECK(store.Made() == 3);
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
