#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/jobs.hpp"
#include "core/journey.hpp"
#include "core/leg_walk_cache.hpp"
#include "core/world_map.hpp"

#include "support/meadow.hpp"

#include <doctest/doctest.h>

namespace {

// A 3 by 3 meadow with the start city on tile 0 (place 0) and another on tile 2, the top row's end (place 1).
constexpr u32 SIDE = 3;
constexpr std::array<core::TilePoint, 1> CITIES{ { { .column = 2, .row = 0 } } };
constexpr u16 HOME = 0;
constexpr u16 DESTINATION = 1;
constexpr core::StableId WALKER{ 41 };
constexpr i64 DEPARTURE = 1'000;
constexpr u32 STAY_DAYS = 2;
constexpr u32 SEED = 0x5eed;
// Along the top row, and the same ends by the middle tile.
constexpr std::array<u32, 3> ROAD{ { 0, 1, 2 } };
constexpr std::array<u32, 3> DETOUR{ { 0, 4, 2 } };

core::WorldMap SmallMap() {
    return test::MeadowMap(SIDE, SIDE, {}, {}, CITIES);
}

core::Journey VisitAlong(const core::WorldMap& map, std::span<const u32> route, i64 departure) {
    return core::Visit(map, route, core::WALKING, WALKER, HOME, DESTINATION, departure, STAY_DAYS, SEED);
}

TEST_CASE("legs over the same points share one walk table, and others get their own") {
    const core::WorldMap map = SmallMap();
    core::LegWalkCache walks{ map };
    const core::Journey early = VisitAlong(map, ROAD, DEPARTURE);
    const core::Journey late = VisitAlong(map, ROAD, DEPARTURE + 500);
    const core::LegWalk& road = walks.Of(early.parts[0].leg);
    CHECK(&walks.Of(late.parts[0].leg) == &road);
    CHECK(walks.Size() == 1);

    // The way home walks the same tiles in the other order.
    CHECK(&walks.Of(early.parts[2].leg) != &road);
    CHECK(walks.Size() == 2);
    CHECK(&walks.Of(late.parts[2].leg) == &walks.Of(early.parts[2].leg));

    // The same ends and as many tiles, through another between.
    const core::Journey detour = VisitAlong(map, DETOUR, DEPARTURE);
    const core::LegWalk& around = walks.Of(detour.parts[0].leg);
    CHECK(&around != &road);
    CHECK(around.tiles == std::vector<u32>{ DETOUR.begin(), DETOUR.end() });
    CHECK(walks.Size() == 3);

    // The road's tiles from a foot off the first tile's centre, as the player's own route sets out.
    const core::Vec2 foot = map.TileCentre(ROAD.front()) + core::Vec2{ .x = 10.0F, .y = 5.0F };
    const core::Leg fromFoot = core::MakeTileLeg(map, core::WALKING, DEPARTURE, foot, ROAD);
    CHECK(&walks.Of(fromFoot) != &road);
    CHECK(walks.Size() == 4);

    walks.Clear();
    CHECK(walks.Size() == 0);
    CHECK(walks.Of(late.parts[0].leg).Fits(late.parts[0].leg));
    CHECK(walks.Size() == 1);
}

TEST_CASE("a walk table asked for from several workers at once is made once per leg") {
    constexpr u32 WORKERS = 4;
    constexpr u32 ASKS = 64;
    const core::WorldMap map = SmallMap();
    core::LegWalkCache walks{ map };
    const std::array<core::Journey, 2> visits{ { VisitAlong(map, ROAD, DEPARTURE), VisitAlong(map, DETOUR, DEPARTURE) } };
    std::array<const core::LegWalk*, ASKS> found{};
    core::Jobs jobs{ WORKERS };
    jobs.ParallelFor(ASKS, [&](u32 ask) { found.at(ask) = &walks.Of(visits.at(ask % 2).parts[0].leg); });
    CHECK(walks.Size() == 2);
    std::size_t strays = 0;
    for (u32 ask = 0; ask < ASKS; ++ask) {
        strays += found.at(ask) == found.at(ask % 2) ? 0 : 1;
    }
    CHECK(strays == 0);
    CHECK(found[0] != found[1]);
}

} // namespace
