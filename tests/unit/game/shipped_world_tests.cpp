#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/game_data.hpp"
#include "core/jobs.hpp"
#include "core/journey.hpp"
#include "core/names.hpp"
#include "core/paths.hpp"
#include "core/rect.hpp"
#include "core/world_map.hpp"
#include "game/host.hpp"
#include "game/npcs.hpp"
#include "game/scenes.hpp"
#include "game/scenes/world.hpp"
#include "support/manifest.hpp"
#include "support/world.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u32 SHIPPED_PEOPLE = 50'000;

// The shipped map's size stays in code (WORLD_COLUMNS, WORLD_ROWS, WORLD_TILE: the stand-in walkers' spread
// and gatherings are laid out on them), so the map must match it.
TEST_CASE("the shipped world map is the size the world scene's code assumes") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    CHECK(map->Columns() == game::WORLD_COLUMNS);
    CHECK(map->Rows() == game::WORLD_ROWS);
    CHECK(map->TileSize() == doctest::Approx(game::WORLD_TILE));
}

// `measure` counts the world's NPCs: the cities' shares are its load, which stays comparable at 50,000.
TEST_CASE("the shipped cities' shares sum to 50,000") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    u32 people = 0;
    u32 capital = 0;
    u32 largest = 0;
    for (const core::Place& place : map->Places()) {
        people += place.kind == core::PlaceKind::City ? place.people : 0;
        capital = place.key == "capital" ? place.people : capital;
        largest = std::max(largest, place.people);
    }
    CHECK(people == SHIPPED_PEOPLE);
    CHECK(capital == largest);
}

// The measurement path (app/camera_path) flies over WORLD_BUSIEST, where the busiest stand-in gathering sits round the
// port: moving the port off it would measure an empty sea.
TEST_CASE("WORLD_BUSIEST lies in the port's tile") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    const auto port = std::ranges::find(map->Places(), std::string_view{ "port" }, &core::Place::key);
    REQUIRE(port != map->Places().end());
    CHECK(port->kind == core::PlaceKind::City);
    CHECK(port->tile.column == static_cast<u32>(game::WORLD_BUSIEST.x / map->TileSize()));
    CHECK(port->tile.row == static_cast<u32>(game::WORLD_BUSIEST.y / map->TileSize()));
}

// ShippedData() is what every host that builds the world scene takes: the built-in data with the shipped map.
TEST_CASE("the shipped data carries the shipped world map") {
    const core::GameData data = test::ShippedData();
    REQUIRE(data.world != nullptr);
    CHECK(data.world->Places()[data.world->Start()].key == "capital");
    const u64 builtIn = game::BuiltInData().Hash(); // named first: a temporary inside CHECK trips MSVC's C4866
    CHECK(data.Hash() != builtIn);
}

// 80% mortals (below 5, the cultivator threshold), 18% cultivators (5 to 50), 2% strong (50 and up), within 2 points.
TEST_CASE("the shipped world's people are mostly mortals, some cultivators, a rare few strong") {
    constexpr f32 CULTIVATOR = 5.0F;
    constexpr f32 STRONG = 50.0F;
    constexpr f64 TOLERANCE = 2.0;
    constexpr f64 MORTAL_SHARE = 80.0;
    constexpr f64 CULTIVATOR_SHARE = 18.0;
    constexpr f64 STRONG_SHARE = 2.0;
    game::Host const host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    u32 mortals = 0;
    u32 cultivators = 0;
    u32 strong = 0;
    for (const auto [entity, person] : host.State()[game::WORLD].Entities().view<const game::Npc>().each()) {
        mortals += person.power < CULTIVATOR ? 1U : 0U;
        cultivators += person.power >= CULTIVATOR && person.power < STRONG ? 1U : 0U;
        strong += person.power >= STRONG ? 1U : 0U;
    }
    const auto share = [](u32 count) { return 100.0 * count / SHIPPED_PEOPLE; };
    CHECK(mortals + cultivators + strong == SHIPPED_PEOPLE);
    CHECK(std::abs(share(mortals) - MORTAL_SHARE) <= TOLERANCE);
    CHECK(std::abs(share(cultivators) - CULTIVATOR_SHARE) <= TOLERANCE);
    CHECK(std::abs(share(strong) - STRONG_SHARE) <= TOLERANCE);
}

// The search and the distance map are two programs for one answer; the shipped map's roads, river, fords and sand are
// what could make them differ (the heuristic's floor against seven-fifths diagonals).
TEST_CASE("the search costs the same as the distance map on the shipped map") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    // Pairs of cities by place index: the capital, port, sect, oasis, marsh town.
    const std::vector<std::pair<u16, u16>> pairs = { { 0, 1 }, { 1, 0 }, { 0, 2 }, { 2, 3 },
                                                     { 3, 4 }, { 4, 0 }, { 1, 4 }, { 2, 0 } };
    for (const auto& [from, target] : pairs) {
        CAPTURE(from);
        CAPTURE(target);
        const u32 start = map->TileIndex(map->Places()[from].tile);
        const u32 goal = map->TileIndex(map->Places()[target].tile);
        const core::DistanceMap distances = core::DistanceMap::Build(*map, goal, core::WALKING);
        REQUIRE(distances.CostFrom(start) != core::UNREACHABLE);
        const std::vector<u32> route = core::FindRoute(*map, core::WALKING, start, goal);
        REQUIRE_FALSE(route.empty());
        u32 walked = 0;
        for (std::size_t step = 1; step < route.size(); ++step) {
            walked += map->StepCost(route[step - 1], route[step], core::WALKING);
        }
        CHECK(walked == distances.CostFrom(start));
    }
}

/// How many of `host`'s journeys are under way at world tick `tick`.
std::size_t UnderwayAt(const game::Host& host, i64 tick) {
    return static_cast<std::size_t>(std::ranges::count_if(host.NpcTravel().Journeys(), [tick](const core::Journey& journey) {
        return core::Underway(journey, tick);
    }));
}

// A few thousand of the 50,000 on the roads, enough to make them busy without crowding every tile.
TEST_CASE("the shipped roads carry a few thousand at once") {
    constexpr std::size_t FEWEST = 2'000;
    constexpr std::size_t MOST = 6'000;
    constexpr auto MONTH = static_cast<i64>(game::TICKS_PER_MONTH);
    game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    // The new world's first tick and the middle of its first month, then the next month's first tick, middle and last.
    const std::size_t start = UnderwayAt(host, 0);
    const std::size_t firstMiddle = UnderwayAt(host, MONTH / 2);
    {
        core::Jobs jobs{ 1 };
        host.AdvanceCalendar({ .wall = 1, .simulation = 1, .state = 1 }, jobs, {});
    }
    REQUIRE(host.WorldTicks() == game::TICKS_PER_MONTH);
    const std::size_t afterStep = UnderwayAt(host, MONTH);
    const std::size_t secondMiddle = UnderwayAt(host, MONTH + (MONTH / 2));
    const std::size_t secondLast = UnderwayAt(host, (2 * MONTH) - 1);
    CAPTURE(start);
    CAPTURE(firstMiddle);
    CAPTURE(afterStep);
    CAPTURE(secondMiddle);
    CAPTURE(secondLast);
    const auto counts = std::to_array({ start, firstMiddle, afterStep, secondMiddle, secondLast });
    CHECK(std::ranges::min(counts) >= FEWEST);
    CHECK(std::ranges::max(counts) <= MOST);
}

TEST_CASE("the month step with visits is the same on 1 and 4 workers") {
    constexpr u32 MANY_WORKERS = 4;
    struct Stepped {
        u64 hash = 0;
        std::size_t departures = 0; // journeys setting out in the month the step begins
    };
    const auto stepped = [](u32 workers) {
        game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
        core::Jobs jobs{ workers };
        host.AdvanceCalendar({ .wall = 1, .simulation = 1, .state = 1 }, jobs, {});
        constexpr auto MONTH = static_cast<i64>(game::TICKS_PER_MONTH);
        return Stepped{ .hash = host.StateHash(),
                        .departures = static_cast<std::size_t>(
                            std::ranges::count_if(host.NpcTravel().Journeys(), [](const core::Journey& journey) {
                                return core::StartTick(journey) >= MONTH;
                            })
                        ) };
    };
    const Stepped one = stepped(1);
    const Stepped many = stepped(MANY_WORKERS);
    CHECK(one.departures > 0);
    CHECK(one.departures == many.departures);
    CHECK(one.hash == many.hash);
}

TEST_CASE("publishing travellers is the same on 1 and 4 workers") {
    constexpr u32 MANY_WORKERS = 4;
    // More journeys than one publish chunk takes, so the travellers are packed in several.
    constexpr std::size_t SEVERAL_CHUNKS = 4'097;
    game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    std::vector<core::Event> events;
    host.Tick({ .wall = 1, .simulation = 1, .state = 1 }, {}, events);
    REQUIRE(host.NpcTravel().Journeys().size() >= SEVERAL_CHUNKS);
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    const core::Rect everywhere{
        .min = {},
        .max = { .x = static_cast<f32>(map->Columns()) * map->TileSize(),
                 .y = static_cast<f32>(map->Rows()) * map->TileSize() },
    };
    std::vector<core::Snapshot> published;
    for (const u32 workers : { 1U, MANY_WORKERS }) {
        core::Jobs jobs{ workers };
        core::Snapshot snapshot;
        host.Publish(snapshot, jobs, everywhere);
        published.push_back(std::move(snapshot));
    }
    const auto named = [](const core::Snapshot& snapshot) {
        std::vector<std::tuple<core::StableId, u32, u64>> labels;
        for (const core::TextItem& text : snapshot.texts) {
            if (text.npcName) {
                labels.emplace_back(text.entity, core::NameKey(*text.npcName), std::bit_cast<u64>(text.current));
            }
        }
        return labels;
    };
    REQUIRE_FALSE(named(published[0]).empty());
    CHECK(
        std::ranges::equal(
            std::as_bytes(std::span{ published[0].instances }), std::as_bytes(std::span{ published[1].instances })
        )
    );
    CHECK(published[0].ids == published[1].ids);
    CHECK(named(published[0]) == named(published[1]));
}

} // namespace
