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
    std::size_t missing = 0;
    i64 worstCost = std::numeric_limits<i64>::min();
    i64 worstOffRoad = std::numeric_limits<i64>::min();
    std::pair<u32, u32> costliest{};
    std::pair<u32, u32> furthestOffRoad{};
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
                costliest = { route->cost, Walked(map, cheapest) };
            }
            // The cheapest route's off-road tiles plus 5% of its length, rounded up.
            const i64 allowed = i64{ OffRoad(map, cheapest) } + static_cast<i64>(((cheapest.size() * 5) + 99) / 100);
            const u32 offRoad = OffRoad(map, core::RouteTiles(map, graph, *route));
            if (i64{ offRoad } - allowed > worstOffRoad) {
                worstOffRoad = i64{ offRoad } - allowed;
                furthestOffRoad = { offRoad, OffRoad(map, cheapest) };
            }
        }
    }
    CAPTURE(costliest.first);
    CAPTURE(costliest.second);
    CAPTURE(furthestOffRoad.first);
    CAPTURE(furthestOffRoad.second);
    CHECK(missing == 0);
    CHECK(worstCost <= 0);
    CHECK(worstOffRoad <= 0);
}

} // namespace
