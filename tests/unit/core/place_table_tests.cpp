#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/jobs.hpp"
#include "core/paths.hpp"
#include "core/place_table.hpp"
#include "core/route_graph.hpp"
#include "core/route_search.hpp"
#include "core/world_map.hpp"
#include "support/hand_map.hpp"
#include "support/synthetic_map.hpp"

#include <doctest/doctest.h>

namespace {

using test::hand::HAND_RULES;
using test::hand::HandSource;
using test::hand::MustBuild;

/// The synthetic map's first danger: its cities come first.
constexpr u16 FIRST_DANGER = 6;
/// How many places the synthetic map has.
constexpr u16 PLACES = 16;

/// The places the first synthetic map's city `city` is asked about: the next city, its own danger and a danger further on.
std::array<u16, 3> NamedPlaces(u16 city) {
    return { static_cast<u16>((city + 1) % FIRST_DANGER),
             static_cast<u16>(FIRST_DANGER + city),
             static_cast<u16>(12 + (city % 4)) };
}

/// The shipped travel rules' reach: base 300, 20 a power, the bands at powers 0 and 5.
core::TravelRules ShippedRules() {
    core::TravelRules rules;
    rules.reachBase = 300;
    rules.reachPerPower = 20;
    rules.bands = { { .fromPower = 0.0F, .profile = 0 }, { .fromPower = 5.0F, .profile = 1 } };
    return rules;
}

/// The table of `graph`, built on the calling thread.
core::PlaceTable TableOf(const core::WorldMap& map, const core::RouteGraph& graph, u32 bound = core::UNREACHABLE) {
    return core::PlaceTable::Build(map, graph, bound);
}

TEST_CASE("the place table's cost is the cost of the route found for the same pair") {
    const core::WorldMap map = test::SyntheticMap(1, 1);
    REQUIRE(map.Places().size() == PLACES);
    const core::RouteGraphs graphs = core::RouteGraphs::Build(map);
    std::size_t pairs = 0;
    std::size_t differing = 0;
    for (std::size_t profile = 0; profile < map.Profiles().size(); ++profile) {
        const core::RouteGraph& graph = graphs.Of(static_cast<core::ProfileIndex>(profile));
        const core::PlaceTable table = TableOf(map, graph);
        CHECK(table.Bytes() >= std::size_t{ FIRST_DANGER } * PLACES * sizeof(u32));
        core::RouteScratch scratch;
        for (u16 city = 0; city < FIRST_DANGER; ++city) {
            for (const u16 place : NamedPlaces(city)) {
                const u32 from = map.TileIndex(map.Places()[city].tile);
                const u32 target = map.TileIndex(map.Places()[place].tile);
                const std::optional<core::GraphRoute> route = core::FindGraphRoute(map, graph, from, target, scratch);
                const u32 expected = route ? route->cost : core::UNREACHABLE;
                differing += table.Cost(city, place) == expected ? 0 : 1;
                ++pairs;
            }
        }
    }
    CHECK(pairs == 36);
    CHECK(differing == 0);
}

TEST_CASE("a city costs nothing to itself") {
    const core::WorldMap map = test::SyntheticMap(1, 1);
    const core::RouteGraphs graphs = core::RouteGraphs::Build(map);
    const core::PlaceTable table = TableOf(map, graphs.Of(core::WALKING));
    std::size_t costly = 0;
    for (u16 city = 0; city < FIRST_DANGER; ++city) {
        costly += table.Cost(city, city) == 0 ? 0 : 1;
    }
    CHECK(costly == 0);
}

TEST_CASE("a danger has no row") {
    const core::WorldMap map = test::SyntheticMap(1, 1);
    const core::RouteGraphs graphs = core::RouteGraphs::Build(map);
    const core::PlaceTable table = TableOf(map, graphs.Of(core::WALKING));
    CHECK(table.Cost(FIRST_DANGER, 0) == core::UNREACHABLE);
    CHECK(table.Cost(FIRST_DANGER, FIRST_DANGER) == core::UNREACHABLE);
    CHECK(table.Cost(PLACES, 0) == core::UNREACHABLE);
    CHECK(table.Cost(0, PLACES) == core::UNREACHABLE);
}

TEST_CASE("a place past the bound is unreachable in the table") {
    const core::WorldMap map = test::SyntheticMap(1, 1);
    const core::RouteGraphs graphs = core::RouteGraphs::Build(map);
    const core::RouteGraph& graph = graphs.Of(core::WALKING);
    const core::PlaceTable whole = TableOf(map, graph);
    const u32 near = whole.Cost(0, 1);
    const u32 far = whole.Cost(0, 2);
    REQUIRE(near != core::UNREACHABLE);
    REQUIRE(far != core::UNREACHABLE);
    REQUIRE(near != far);
    const u32 low = std::min(near, far);
    const u32 high = std::max(near, far);
    const u16 nearer = near < far ? 1 : 2;
    const u16 further = near < far ? 2 : 1;

    const core::PlaceTable bounded = TableOf(map, graph, low + ((high - low) / 2));
    CHECK(bounded.Cost(0, nearer) == low);
    CHECK(bounded.Cost(0, further) == core::UNREACHABLE);
    CHECK(TableOf(map, graph, low).Cost(0, nearer) == low);
    CHECK(TableOf(map, graph, low - 1).Cost(0, nearer) == core::UNREACHABLE);
}

TEST_CASE("a place its profile can't enter is unreachable in the table") {
    core::WorldMapSource source = HandSource();
    // A cart keeps to the roads, so city 1 on open meadow is out of its reach.
    source.profiles.push_back(
        { .key = "cart", .ticksPerCost = 1, .kindCosts = { core::IMPASSABLE_COST, core::IMPASSABLE_COST }, .features = {} }
    );
    const core::WorldMap map = MustBuild(std::move(source));
    const core::RouteGraphs graphs = core::RouteGraphs::Build(map, HAND_RULES);
    const core::RouteGraph& cart = graphs.Of(2);
    REQUIRE(cart.PlaceNode(1) == core::NO_NODE);
    const core::PlaceTable table = TableOf(map, cart);
    CHECK(table.Cost(0, 1) == core::UNREACHABLE);
    CHECK(table.Cost(1, 1) == core::UNREACHABLE);
    CHECK(table.Cost(1, 0) == core::UNREACHABLE);
    CHECK(table.Cost(0, 2) != core::UNREACHABLE);
}

TEST_CASE("the table is the same on 1 and 4 workers and without a job system") {
    const core::WorldMap map = test::SyntheticMap(1, 1);
    const core::RouteGraphs graphs = core::RouteGraphs::Build(map);
    std::size_t differing = 0;
    for (std::size_t profile = 0; profile < map.Profiles().size(); ++profile) {
        const core::RouteGraph& graph = graphs.Of(static_cast<core::ProfileIndex>(profile));
        const core::PlaceTable alone = TableOf(map, graph);
        core::Jobs one{ 1 };
        core::Jobs four{ 4 };
        differing += alone == core::PlaceTable::Build(map, graph, core::UNREACHABLE, &one) ? 0 : 1;
        differing += alone == core::PlaceTable::Build(map, graph, core::UNREACHABLE, &four) ? 0 : 1;
    }
    CHECK(differing == 0);
}

TEST_CASE("the reach bound follows the bands") {
    const core::TravelRules shipped = ShippedRules();
    CHECK(core::ReachBound(shipped, 0) == 400);
    CHECK(core::ReachBound(shipped, 1) == core::UNREACHABLE);
    CHECK(core::ReachBound(shipped, 2) == 0);

    core::TravelRules none = shipped;
    none.bands.clear();
    CHECK(core::ReachBound(none, core::WALKING) == core::UNREACHABLE);
    CHECK(core::ReachBound(none, 1) == 0);
}

TEST_CASE("a reach too large for a cost saturates below no bound") {
    core::TravelRules rules;
    rules.reachBase = core::UNREACHABLE;
    rules.reachPerPower = core::UNREACHABLE;
    rules.bands = { { .fromPower = 0.0F, .profile = 0 }, { .fromPower = 1.0e12F, .profile = 1 } };
    CHECK(core::ReachBound(rules, 0) == core::UNREACHABLE - 1);
}

} // namespace
