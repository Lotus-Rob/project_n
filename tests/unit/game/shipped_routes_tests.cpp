#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/paths.hpp"
#include "core/route_graph.hpp"
#include "core/route_search.hpp"
#include "core/world_map.hpp"
#include "support/shipped_world.hpp"

#include <doctest/doctest.h>

namespace {

/// How many of `tiles` no road crosses.
u32 OffRoad(const core::WorldMap& map, const std::vector<u32>& tiles) {
    return static_cast<u32>(std::ranges::count_if(tiles, [&](u32 tile) {
        return (map.FlagsAt(tile) & std::to_underlying(core::TileFlag::Road)) == 0;
    }));
}

/// What walking `tiles` costs on foot, step by step.
u32 Walked(const core::WorldMap& map, const std::vector<u32>& tiles) {
    u32 total = 0;
    for (std::size_t step = 1; step < tiles.size(); ++step) {
        total += map.StepCost(tiles[step - 1], tiles[step], core::WALKING);
    }
    return total;
}

/// The pair that came off worst on one bound: its tiles, and the measure it was judged by for the graph's route and for
/// the cheapest.
struct WorstPair {
    /// The start's tile.
    u32 from = 0;
    /// The end's tile.
    u32 target = 0;
    /// The graph's route's measure.
    u32 route = 0;
    /// The cheapest route's measure.
    u32 cheapest = 0;
};

TEST_CASE("routes between the shipped map's cities keep to its roads") {
    const std::shared_ptr<const core::WorldMap> shipped = test::ShippedWorld();
    const core::WorldMap& map = *shipped;
    const core::RouteGraphs graphs = core::RouteGraphs::Build(map);
    const core::RouteGraph& graph = graphs.Of(core::WALKING);
    std::vector<u32> cities;
    for (const core::Place& place : map.Places()) {
        if (place.kind == core::PlaceKind::City) {
            cities.push_back(map.TileIndex(place.tile));
        }
    }
    REQUIRE(cities.size() == 6);

    core::RouteScratch scratch;
    core::SectorWalk walk;
    std::size_t missing = 0;
    i64 worstCost = std::numeric_limits<i64>::min();
    i64 worstOffRoad = std::numeric_limits<i64>::min();
    WorstPair costliest{};
    WorstPair furthestOffRoad{};
    for (const u32 from : cities) {
        for (const u32 target : cities) {
            if (from == target) {
                continue;
            }
            const std::vector<u32> cheapest = core::FindRoute(map, core::WALKING, from, target);
            const std::optional<core::GraphRoute> route = core::FindGraphRoute(map, graph, from, target, scratch);
            if (cheapest.empty() || !route) {
                ++missing;
                continue;
            }
            const i64 over = (i64{ route->cost } * 100) - (i64{ Walked(map, cheapest) } * 105);
            if (over > worstCost) {
                worstCost = over;
                costliest = { .from = from, .target = target, .route = route->cost, .cheapest = Walked(map, cheapest) };
            }
            // The cheapest route's off-road tiles plus 5% of its length, rounded up.
            const i64 allowed = i64{ OffRoad(map, cheapest) } + static_cast<i64>(((cheapest.size() * 5) + 99) / 100);
            const u32 offRoad = OffRoad(map, core::RouteTiles(map, graph, *route, walk));
            if (i64{ offRoad } - allowed > worstOffRoad) {
                worstOffRoad = i64{ offRoad } - allowed;
                furthestOffRoad = { .from = from, .target = target, .route = offRoad, .cheapest = OffRoad(map, cheapest) };
            }
        }
    }
    CAPTURE(costliest.from);
    CAPTURE(costliest.target);
    CAPTURE(costliest.route);
    CAPTURE(costliest.cheapest);
    CAPTURE(furthestOffRoad.from);
    CAPTURE(furthestOffRoad.target);
    CAPTURE(furthestOffRoad.route);
    CAPTURE(furthestOffRoad.cheapest);
    CHECK(missing == 0);
    CHECK(worstCost <= 0);
    CHECK(worstOffRoad <= 0);
}

} // namespace
