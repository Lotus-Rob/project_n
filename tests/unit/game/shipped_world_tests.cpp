#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/game_data.hpp"
#include "core/jobs.hpp"
#include "core/journey.hpp"
#include "core/paths.hpp"
#include "core/rect.hpp"
#include "core/view_limits.hpp"
#include "core/world_map.hpp"
#include "game/host.hpp"
#include "game/npcs.hpp"
#include "game/scenes.hpp"
#include "game/scenes/world.hpp"
#include "game/travel.hpp"
#include "support/manifest.hpp"
#include "support/world.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u32 SHIPPED_PEOPLE = 50'000;

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

// The measurement path (app/camera_path) flies over WORLD_BUSIEST, where the travellers crowd round the port: moving the
// port off it would measure an empty sea.
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

/// Two cities by place index: where a route starts and where it ends.
struct CityPair {
    u16 from = 0;
    u16 target = 0;
};

// The search and the distance map are two programs for one answer; the shipped map's roads, river, fords and sand are
// what could make them differ (the heuristic's floor against seven-fifths diagonals).
TEST_CASE("the search costs the same as the distance map on the shipped map") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    // Pairs of cities by place index: the capital, port, sect, oasis, marsh town.
    constexpr std::array<CityPair, 8> PAIRS{ { { .from = 0, .target = 1 },
                                               { .from = 1, .target = 0 },
                                               { .from = 0, .target = 2 },
                                               { .from = 2, .target = 3 },
                                               { .from = 3, .target = 4 },
                                               { .from = 4, .target = 0 },
                                               { .from = 1, .target = 4 },
                                               { .from = 2, .target = 0 } } };
    for (const auto& [from, target] : PAIRS) {
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

/// How many members of `host`'s journeys under way at world tick `tick` stand within `area`, on the road or in a stay.
std::size_t TravellersIn(const game::Host& host, const core::WorldMap& map, const core::Rect& area, i64 tick) {
    std::size_t inside = 0;
    for (const core::Journey& journey : host.NpcTravel().Journeys()) {
        if (!core::Underway(journey, tick)) {
            continue;
        }
        for (std::size_t member = 0; member < journey.members.size(); ++member) {
            inside += core::Contains(area, core::Where(map, journey, tick, member)) ? 1U : 0U;
        }
    }
    return inside;
}

// `measure`'s world run looks over WORLD_BUSIEST at the widest view: the travellers there are its load, kept at 2,000 or
// more so its runs stay comparable over time. The shipped share, 60 per thousand, puts 650 to 884 there over the first half
// month, so the run raises it with `--travel-share`, through what main applies it with: at 200 the fewest is 2,304, at its
// second tick, rising to 2,813.
TEST_CASE("the busiest view holds at least 2,000 travellers") {
    constexpr std::size_t FEWEST = 2'000;
    constexpr u16 MEASURED_SHARE = 200; // `--travel-share` in measure's world run (MEASURE_RUNS in tools/project_n.py)
    constexpr auto DAY = static_cast<i64>(game::TICKS_PER_DAY);
    core::GameData data = test::ShippedData();
    REQUIRE(game::SetTravelShare(data, MEASURED_SHARE).has_value());
    const std::shared_ptr<const core::WorldMap> map = data.world;
    const game::Host host(test::TestManifest(), game::WORLD_SCENE, data);
    const core::Rect widest{ .min = game::WORLD_BUSIEST - core::WIDEST_HALF_VIEW,
                             .max = game::WORLD_BUSIEST + core::WIDEST_HALF_VIEW };
    struct Sample {
        std::string_view why;
        i64 tick;
    };
    // The first half month, all of it the travel the world's build seeds: its first ticks, a day's boundaries and middles,
    // and its last tick.
    const std::array<Sample, 20> samples{ {
        { .why = "the new world's first tick", .tick = 0 },
        { .why = "its second tick", .tick = 1 },
        { .why = "the first day's middle", .tick = DAY / 2 },
        { .why = "the first day's last tick", .tick = DAY - 1 },
        { .why = "the second day's first tick", .tick = DAY },
        { .why = "day 3", .tick = 2 * DAY },
        { .why = "day 3's middle", .tick = (2 * DAY) + (DAY / 2) },
        { .why = "day 4", .tick = 3 * DAY },
        { .why = "day 5", .tick = 4 * DAY },
        { .why = "day 5's last tick", .tick = (5 * DAY) - 1 },
        { .why = "day 6", .tick = 5 * DAY },
        { .why = "day 7's middle", .tick = (6 * DAY) + (DAY / 2) },
        { .why = "day 8", .tick = 7 * DAY },
        { .why = "day 9", .tick = 8 * DAY },
        { .why = "day 10's middle", .tick = (9 * DAY) + (DAY / 2) },
        { .why = "day 11", .tick = 10 * DAY },
        { .why = "day 12", .tick = 11 * DAY },
        { .why = "day 13's middle", .tick = (12 * DAY) + (DAY / 2) },
        { .why = "day 14", .tick = 13 * DAY },
        { .why = "the half month's last tick", .tick = (15 * DAY) - 1 },
    } };
    std::size_t lowest = std::numeric_limits<std::size_t>::max();
    std::string_view lowestWhy;
    for (const Sample& sample : samples) {
        const std::size_t count = TravellersIn(host, *map, widest, sample.tick);
        if (count < lowest) {
            lowest = count;
            lowestWhy = sample.why;
        }
    }
    CAPTURE(lowestWhy);
    CHECK(lowest >= FEWEST);
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

/// Whether two texts are alike in every field, their numbers to the bit.
bool SameText(const core::TextItem& lhs, const core::TextItem& rhs) {
    const auto point = [](core::Vec2 vector) { return std::bit_cast<u64>(vector); };
    const auto number = [](f32 value) { return std::bit_cast<u32>(value); };
    return lhs.kind == rhs.kind && lhs.string == rhs.string && lhs.npcName == rhs.npcName && lhs.value == rhs.value &&
           lhs.sign == rhs.sign && point(lhs.previous) == point(rhs.previous) && point(lhs.current) == point(rhs.current) &&
           point(lhs.offset) == point(rhs.offset) && point(lhs.scatter) == point(rhs.scatter) &&
           number(lhs.lift) == number(rhs.lift) && number(lhs.opacity) == number(rhs.opacity) && lhs.entity == rhs.entity &&
           number(lhs.height) == number(rhs.height) && lhs.nameKey == rhs.nameKey && lhs.priority == rhs.priority &&
           lhs.colour == rhs.colour && lhs.align == rhs.align && lhs.direction == rhs.direction;
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
    const auto named = [](const core::TextItem& text) { return text.npcName.has_value(); };
    REQUIRE(std::ranges::any_of(published[0].texts, named));
    CHECK(
        std::ranges::equal(
            std::as_bytes(std::span{ published[0].instances }), std::as_bytes(std::span{ published[1].instances })
        )
    );
    CHECK(published[0].ids == published[1].ids);
    CHECK(std::ranges::equal(published[0].texts, published[1].texts, SameText));
}

} // namespace
