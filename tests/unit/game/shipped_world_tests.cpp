#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/delta.hpp"
#include "core/game_data.hpp"
#include "core/jobs.hpp"
#include "core/journey.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"
#include "core/rect.hpp"
#include "core/save_file.hpp"
#include "core/state.hpp"
#include "core/view_limits.hpp"
#include "core/world_map.hpp"
#include "game/host.hpp"
#include "game/npcs.hpp"
#include "game/scenes.hpp"
#include "game/scenes/world.hpp"
#include "game/travel.hpp"
#include "support/manifest.hpp"
#include "support/saves.hpp"
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

/// The index of the place keyed `key` on `map`, which has one.
u16 PlaceIndex(const core::WorldMap& map, std::string_view key) {
    const auto place = std::ranges::find(map.Places(), key, &core::Place::key);
    REQUIRE(place != map.Places().end());
    return static_cast<u16>(place - map.Places().begin());
}

/// What walking from city `from` to city `target` of `map` costs by `profile`, or core::UNREACHABLE.
u32 CityCost(const core::WorldMap& map, u16 from, u16 target, core::ProfileIndex profile) {
    const core::DistanceMap distances = core::DistanceMap::Build(map, map.TileIndex(map.Places()[target].tile), profile);
    return distances.CostFrom(map.TileIndex(map.Places()[from].tile));
}

// No city is cut off: whatever their power, travellers get from any city to any other.
TEST_CASE("every band's profile reaches every city from every city") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    REQUIRE_FALSE(map->Travel().bands.empty());
    for (const core::PowerBand& band : map->Travel().bands) {
        CAPTURE(band.profile);
        for (u16 target = 0; target < map->Places().size(); ++target) {
            if (map->Places()[target].kind != core::PlaceKind::City) {
                continue;
            }
            const core::DistanceMap distances =
                core::DistanceMap::Build(*map, map->TileIndex(map->Places()[target].tile), band.profile);
            const auto cut = std::ranges::find_if(map->Places(), [&](const core::Place& place) {
                return place.kind == core::PlaceKind::City &&
                       distances.CostFrom(map->TileIndex(place.tile)) == core::UNREACHABLE;
            });
            CAPTURE(map->Places()[target].key);
            CHECK_MESSAGE(cut == map->Places().end(), "a city this band can't walk from");
        }
    }
}

/// Fewest steps along road tiles, edge to edge as roads are rasterised, from `from` to `target` of `map` without entering
/// a tile `banned` marks; core::UNREACHABLE when no such way exists.
u32 RoadSteps(const core::WorldMap& map, u32 from, u32 target, const std::vector<bool>& banned) {
    constexpr u8 ROAD = std::to_underlying(core::TileFlag::Road);
    std::vector<u32> steps(banned.size(), core::UNREACHABLE);
    std::vector<u32> frontier{ from }; // breadth first: tiles in the order they were reached
    steps.at(from) = 0;
    for (std::size_t next = 0; next < frontier.size(); ++next) {
        const u32 tile = frontier[next];
        const core::TilePoint point = map.PointOf(tile);
        const std::array<core::TilePoint, 4> sides{ { { .column = point.column + 1, .row = point.row },
                                                      { .column = point.column - 1, .row = point.row },
                                                      { .column = point.column, .row = point.row + 1 },
                                                      { .column = point.column, .row = point.row - 1 } } };
        for (const core::TilePoint side : sides) {
            // A step off the map's first column or row wraps to a huge value, which this bound also refuses.
            if (side.column >= map.Columns() || side.row >= map.Rows()) {
                continue;
            }
            const u32 sideTile = map.TileIndex(side);
            if (steps.at(sideTile) == core::UNREACHABLE && !banned.at(sideTile) && (map.FlagsAt(sideTile) & ROAD) != 0) {
                steps.at(sideTile) = steps.at(tile) + 1;
                frontier.push_back(sideTile);
            }
        }
    }
    return steps.at(target);
}

/// Per tile of `map`: whether it lies in the reach of danger place `site`, the danger tiles nearest to it (danger places
/// stand further apart than twice the reach, so no tile lies in two reaches).
std::vector<bool> ReachOf(const core::WorldMap& map, u16 site) {
    constexpr u8 DANGER = std::to_underlying(core::TileFlag::Danger);
    const auto distanceSquared = [&](u32 tile, const core::Place& place) {
        const core::TilePoint point = map.PointOf(tile);
        const i64 columns = static_cast<i64>(point.column) - place.tile.column;
        const i64 rows = static_cast<i64>(point.row) - place.tile.row;
        return (columns * columns) + (rows * rows);
    };
    std::vector<bool> reach(static_cast<std::size_t>(map.Columns()) * map.Rows(), false);
    for (u32 tile = 0; tile < reach.size(); ++tile) {
        if ((map.FlagsAt(tile) & DANGER) == 0) {
            continue;
        }
        const auto nearest = std::ranges::min_element(map.Places(), {}, [&](const core::Place& place) {
            return place.kind == core::PlaceKind::Danger ? distanceSquared(tile, place) : std::numeric_limits<i64>::max();
        });
        reach.at(tile) = nearest - map.Places().begin() == site;
    }
    return reach;
}

// Three danger sites stand on the roads' shortcuts: on each of their links the short road runs through the danger and a
// longer one goes round it, so the strong and the weak each have their way; every other site lies off the roads.
TEST_CASE("three links fork around a danger") {
    constexpr u8 ROAD = std::to_underlying(core::TileFlag::Road);
    struct Fork {
        std::string_view site;
        std::string_view from;
        std::string_view target;
    };
    constexpr std::array<Fork, 3> FORKS{ {
        { .site = "jade_cave", .from = "capital", .target = "oasis" },
        { .site = "bone_forest", .from = "capital", .target = "port" },
        { .site = "ghostlight_woods", .from = "port", .target = "isle" },
    } };
    constexpr u32 MORTAL_TO_OASIS = 173; // round Jade Cave
    constexpr u32 STRONG_TO_OASIS = 152; // through it
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    const core::ProfileIndex wary = game::ProfileFor(map->Travel(), 0.0F);
    const core::ProfileIndex strong = game::ProfileFor(map->Travel(), map->Travel().cultivatorPower);
    REQUIRE(wary != strong);
    const std::vector<bool> nothing(static_cast<std::size_t>(map->Columns()) * map->Rows(), false);
    const auto entered = [&](const std::vector<u32>& route, const std::vector<bool>& reach) {
        return std::ranges::count_if(route, [&](u32 tile) { return reach.at(tile); });
    };
    for (const Fork& fork : FORKS) {
        CAPTURE(fork.site);
        const std::vector<bool> reach = ReachOf(*map, PlaceIndex(*map, fork.site));
        const u32 from = map->TileIndex(map->Places()[PlaceIndex(*map, fork.from)].tile);
        const u32 target = map->TileIndex(map->Places()[PlaceIndex(*map, fork.target)].tile);
        const u32 shortest = RoadSteps(*map, from, target, nothing);
        const u32 around = RoadSteps(*map, from, target, reach);
        CAPTURE(shortest);
        CAPTURE(around);
        REQUIRE(shortest != core::UNREACHABLE);
        CHECK(around != core::UNREACHABLE);
        CHECK(around > shortest); // the short way is the one through the danger
        // The game's own search: a mortal takes the way round, a cultivator the way through.
        const std::vector<u32> mortalRoute = core::FindRoute(*map, wary, from, target);
        const std::vector<u32> strongRoute = core::FindRoute(*map, strong, from, target);
        REQUIRE_FALSE(mortalRoute.empty());
        REQUIRE_FALSE(strongRoute.empty());
        CHECK(entered(mortalRoute, reach) == 0);
        CHECK(entered(strongRoute, reach) > 0);
    }
    const u16 capital = PlaceIndex(*map, "capital");
    const u16 oasis = PlaceIndex(*map, "oasis");
    CHECK(CityCost(*map, capital, oasis, wary) == MORTAL_TO_OASIS);
    CHECK(CityCost(*map, capital, oasis, strong) == STRONG_TO_OASIS);
    std::vector<std::string_view> onRoads;
    for (u16 site = 0; site < map->Places().size(); ++site) {
        if (map->Places()[site].kind != core::PlaceKind::Danger) {
            continue;
        }
        const std::vector<bool> reach = ReachOf(*map, site);
        bool touched = false;
        for (u32 tile = 0; tile < reach.size(); ++tile) {
            touched = touched || (reach.at(tile) && (map->FlagsAt(tile) & ROAD) != 0);
        }
        if (touched) {
            onRoads.push_back(map->Places()[site].key);
        }
    }
    CHECK(onRoads == std::vector<std::string_view>{ "ghostlight_woods", "jade_cave", "bone_forest" });
}

// The travel rules' stated targets: a mortal, who walks warily, reaches the next town, and a strong cultivator crosses the
// map, from the sect in the west to the isle in the east.
TEST_CASE("a mortal reaches the next town, a strong cultivator the far side") {
    constexpr f32 MORTAL = 0.0F;
    constexpr f32 STRONG = 50.0F;
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    const core::TravelRules& rules = map->Travel();
    const core::ProfileIndex wary = game::ProfileFor(rules, MORTAL);
    const core::ProfileIndex strong = game::ProfileFor(rules, STRONG);
    REQUIRE(map->Profiles()[wary].features.danger > 0);
    REQUIRE(map->Profiles()[strong].features.danger == 0);
    const auto nearestTown = [&](u16 from) {
        u32 nearest = core::UNREACHABLE;
        for (u16 city = 0; city < map->Places().size(); ++city) {
            if (city != from && map->Places()[city].kind == core::PlaceKind::City) {
                nearest = std::min(nearest, CityCost(*map, from, city, wary));
            }
        }
        return nearest;
    };
    const u32 nearest = nearestTown(PlaceIndex(*map, "capital"));
    const u32 sectNearest = nearestTown(PlaceIndex(*map, "sect"));
    const u32 farSide = CityCost(*map, PlaceIndex(*map, "sect"), PlaceIndex(*map, "isle"), strong);
    CAPTURE(nearest);
    CAPTURE(farSide);
    CAPTURE(sectNearest);
    CHECK(nearest == 126);
    CHECK(sectNearest == 162);
    CHECK(farSide == 562);
    CHECK(nearest < rules.reachBase);
    CHECK(farSide < rules.reachBase + (static_cast<u32>(STRONG) * rules.reachPerPower));
}

/// How many of `host`'s journeys are under way at world tick `tick`.
std::size_t UnderwayAt(const game::Host& host, i64 tick) {
    return static_cast<std::size_t>(std::ranges::count_if(host.NpcTravel().Journeys(), [tick](const core::Journey& journey) {
        return core::Underway(journey, tick);
    }));
}

// A few thousand of the 50,000 on the roads, enough to make them look busy and load the renderer and simulation for a demo,
// without crowding every tile. It is a demo load, not a balance target: the share was set to land in it, but an edit to the
// map that moves where people live may move the count a few dozen either way.
TEST_CASE("the shipped roads carry 2,400 to 3,300 at once") {
    constexpr std::size_t FEWEST = 2'400;
    constexpr std::size_t MOST = 3'300;
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
// more so its runs stay comparable over time. The shipped share, 58 per thousand, puts 576 to 804 there over the first half
// month, so the run raises it with `--travel-share`, through what main applies it with: at 200 the fewest is 2,083, at its
// first tick, rising to 2,648.
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

// A save writes a journey as its plan and rebuilds it: every journey a new game starts must come back exactly.
TEST_CASE("the shipped world's journeys rebuild exactly") {
    const game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    core::Paths paths{ map };
    const auto bytesOf = [](const core::Journey& journey) {
        std::vector<std::byte> bytes;
        core::Writer writer{ bytes };
        writer(journey);
        return bytes;
    };
    REQUIRE_FALSE(host.NpcTravel().Journeys().empty());
    std::size_t different = 0;
    std::size_t planless = 0;
    for (const core::Journey& journey : host.NpcTravel().Journeys()) {
        const std::optional<core::VisitPlan> plan = core::PlanOf(journey);
        if (!plan) {
            ++planless;
            continue;
        }
        const bool same = bytesOf(core::Rebuild(*map, paths, *plan)) == bytesOf(journey);
        different += same ? 0 : 1;
    }
    CHECK(planless == 0);
    CHECK(different == 0);
}

/// How many bytes `write` puts in a Writer.
template<typename Write>
std::size_t Written(const Write& write) {
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    write(writer);
    return bytes.size();
}

/// What a save of the shipped world holds, part by part, in bytes.
struct SaveParts {
    std::size_t total = 0;
    std::size_t domain = 0;        // the NPCs' changed fields, gone and created
    std::size_t order = 0;         // the storage-order record
    std::size_t relationships = 0; // whole
    std::size_t journeys = 0;      // the travel as its difference from the new game's, journals whole
};

/// The sizes of the parts of `host`'s save, measured by writing each as the save does against `base`, a new game's host.
SaveParts PartsOf(const game::Host& host, const game::Host& base, std::size_t total) {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    core::Paths paths{ map };
    return {
        .total = total,
        .domain = Written([&](core::Writer& writer) {
            core::SaveDomainDelta(writer, base.State()[game::WORLD], host.State()[game::WORLD], test::WorldComponents{});
        }),
        .order = Written([&](core::Writer& writer) {
            core::SaveDomainOrder(writer, base.State()[game::WORLD], host.State()[game::WORLD], test::WorldComponents{});
        }),
        .relationships = Written([&](core::Writer& writer) { host.NpcRelationships().Save(writer); }),
        .journeys =
            Written([&](core::Writer& writer) { host.NpcTravel().SaveDelta(writer, base.NpcTravel(), map.get(), &paths); }),
    };
}

// Not part of a run: `--test-case='measure the shipped world's saves' --no-skip` (tests/AGENTS.md) prints how big the
// shipped world's save is, raw and on disk, and how long it takes to save and load, at a new game and after one, two and
// three month steps. Release builds only say anything about the times.
TEST_CASE("measure the shipped world's saves" * doctest::skip()) {
    using Clock = std::chrono::steady_clock;
    constexpr u32 STEPS = 3;
    const auto milliseconds = [](Clock::duration span) { return std::chrono::duration<f64, std::milli>(span).count(); };
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "project_n_measure_save.bin";
    game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    const game::Host base(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    core::Jobs jobs{ 2 };
    PN_LOG_INFO("step  raw B  disk B  save ms  write ms  load ms  domain B  order B  relationships B  journeys B");
    for (u32 step = 0; step <= STEPS; ++step) {
        if (step > 0) {
            host.AdvanceCalendar({ .wall = step, .simulation = step, .state = step }, jobs, {});
        }
        std::vector<std::byte> bytes;
        const Clock::time_point serialiseStart = Clock::now();
        core::Writer writer{ bytes };
        host.Save(writer);
        const Clock::time_point serialised = Clock::now();
        REQUIRE(core::WriteSaveFile(path, bytes).has_value());
        const Clock::time_point written = Clock::now();
        const std::uintmax_t onDisk = std::filesystem::file_size(path);

        game::Host loaded(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
        const Clock::time_point loadStart = Clock::now();
        const auto read = core::ReadSaveFile(path);
        REQUIRE(read.has_value());
        core::Reader reader{ *read };
        REQUIRE(loaded.Load(reader, 1));
        loaded.EnterScene();
        const Clock::time_point loadEnd = Clock::now();
        CHECK(loaded.StateHash() == host.StateHash());

        const SaveParts parts = PartsOf(host, base, bytes.size());
        PN_LOG_INFO(
            "{}  {}  {}  {:.1f}  {:.1f}  {:.1f}  {}  {}  {}  {}",
            step,
            parts.total,
            onDisk,
            milliseconds(serialised - serialiseStart),
            milliseconds(written - serialised),
            milliseconds(loadEnd - loadStart),
            parts.domain,
            parts.order,
            parts.relationships,
            parts.journeys
        );
    }
    std::filesystem::remove(path);
}

/// What the shipped world's save holds after months of play, as the sizes of its parts and what is behind them.
struct LongSave {
    // Counts
    std::size_t npcs = 0;
    core::DeltaCounts delta;
    std::size_t journeys = 0;
    std::size_t journalEntries = 0;
    std::size_t relationships = 0;
    // Holders by how many edges they hold; nobody is met in this run, so the limit is the unmet one
    std::size_t holdersNone = 0;
    std::size_t holdersSome = 0;
    std::size_t holdersAtLimit = 0;
    std::size_t holdersOverLimit = 0;
    std::size_t goneJourneys = 0;
    std::size_t plannedJourneys = 0;
    std::size_t wholeJourneys = 0;
    // Bytes, raw
    std::size_t total = 0;
    std::size_t domain = 0;
    std::size_t order = 0;
    std::size_t journeyGone = 0;
    std::size_t journeyPlans = 0;
    std::size_t journeyWhole = 0;
    std::size_t journals = 0;
    std::size_t relationshipBytes = 0;
    std::vector<std::byte> wholeSave; // the same save with the domain, relationships and travel written whole
};

/// Measures `host`'s save against `base`, a new game's host, by writing each part as the save does.
LongSave MeasureLongSave(const game::Host& host, const game::Host& base, std::span<const std::byte> saved) {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    core::Paths paths{ map };
    const core::Domain& now = host.State()[game::WORLD];
    const core::Domain& before = base.State()[game::WORLD];
    LongSave save;
    save.total = saved.size();
    save.delta = core::CountDelta(before, now, test::WorldComponents{});
    save.relationships = host.NpcRelationships().All().size();
    // A run of the sorted edge table is one holder's edges; a holder with none has no run, so it counts as the rest.
    const std::span<const game::Relationship> edges = host.NpcRelationships().All();
    for (std::size_t first = 0; first < edges.size();) {
        std::size_t last = first;
        while (last < edges.size() && edges[last].holder == edges[first].holder) {
            ++last;
        }
        const std::size_t held = last - first;
        if (held < game::RELATIONSHIP_LENGTH) {
            ++save.holdersSome;
        }
        else if (held == game::RELATIONSHIP_LENGTH) {
            ++save.holdersAtLimit;
        }
        else {
            ++save.holdersOverLimit;
        }
        first = last;
    }
    save.journeys = host.NpcTravel().Journeys().size();
    save.domain = Written([&](core::Writer& writer) { core::SaveDomainDelta(writer, before, now, test::WorldComponents{}); });
    save.order = Written([&](core::Writer& writer) { core::SaveDomainOrder(writer, before, now, test::WorldComponents{}); });
    save.relationshipBytes = Written([&](core::Writer& writer) { host.NpcRelationships().Save(writer); });

    std::vector<std::byte> journals; // each NPC's id and entries, as Travel writes its journals
    core::Writer journalWriter{ journals };
    for (const auto [entity, stable, person] : now.Entities().view<const core::StableId, const game::Npc>().each()) {
        const std::span<const game::JournalEntry> entries = host.NpcTravel().JournalOf(stable);
        ++save.npcs;
        save.journalEntries += entries.size();
        if (!entries.empty()) {
            journalWriter(stable, std::vector<game::JournalEntry>{ entries.begin(), entries.end() });
        }
    }
    save.holdersNone = save.npcs - save.holdersSome - save.holdersAtLimit - save.holdersOverLimit;
    save.journals = journals.size() + sizeof(u64); // and the count of NPCs that Travel writes before them

    // The journeys as Travel::SaveDelta() sorts them: the base's that are gone, then the others by plan or whole.
    const auto bytesOf = [](const core::Journey& journey) {
        std::vector<std::byte> bytes;
        core::Writer{ bytes }(journey);
        return bytes;
    };
    std::unordered_map<u64, std::vector<std::byte>> current;
    for (const core::Journey& journey : host.NpcTravel().Journeys()) {
        current.emplace(std::to_underlying(journey.members.front()), bytesOf(journey));
    }
    std::unordered_set<u64> kept;
    std::vector<u64> gone;
    u64 place = 0;
    for (const core::Journey& journey : base.NpcTravel().Journeys()) {
        const auto found = current.find(std::to_underlying(journey.members.front()));
        if (found != current.end() && found->second == bytesOf(journey)) {
            kept.insert(std::to_underlying(journey.members.front()));
        }
        else {
            gone.push_back(place);
        }
        ++place;
    }
    std::vector<core::VisitPlan> plans;
    std::vector<core::Journey> whole;
    for (const core::Journey& journey : host.NpcTravel().Journeys()) {
        if (kept.contains(std::to_underlying(journey.members.front()))) {
            continue;
        }
        const std::optional<core::VisitPlan> plan = core::PlanOf(journey);
        if (plan && bytesOf(core::Rebuild(*map, paths, *plan)) == bytesOf(journey)) {
            plans.push_back(*plan);
        }
        else {
            whole.push_back(journey);
        }
    }
    save.goneJourneys = gone.size();
    save.plannedJourneys = plans.size();
    save.wholeJourneys = whole.size();
    save.journeyGone = Written([&](core::Writer& writer) { writer(gone); });
    save.journeyPlans = Written([&](core::Writer& writer) { writer(plans); });
    save.journeyWhole = Written([&](core::Writer& writer) { writer(whole); });

    // The same save with nothing as a difference: the domain whole, every journey whole, then as many of the real save's
    // last bytes as its header, clock and scene take. Only their size is matched: what stands in is not them.
    const std::size_t rest = save.total - save.domain - save.order - save.journeyGone - save.journeyPlans - save.journeyWhole -
                             save.journals - save.relationshipBytes;
    core::Writer wholeWriter{ save.wholeSave };
    core::SaveDomain(wholeWriter, now, test::WorldComponents{});
    host.NpcRelationships().Save(wholeWriter);
    host.NpcTravel().Save(wholeWriter);
    save.wholeSave.insert(save.wholeSave.end(), saved.last(rest).begin(), saved.last(rest).end());
    return save;
}

// Not part of a run: `--test-case="measure the shipped world's saves over hours of play" --no-skip` (tests/AGENTS.md)
// runs the shipped world on month by month, as M does (AdvanceCalendar() on 4 workers; each call moves the clock
// to the next month's first tick, which is the state the clock reaches by playing the month), and logs the save's parts, the
// counts behind them and the times at 1, 10, 50, 100 and 250 hours of unpaused play on the world map (a month is 90 s at 60
// ticks a second). It also steps a 1-worker host alongside and compares its hashes with the 4-worker host's at each
// checkpoint. Release builds only say anything about the times. It runs for a long while: watch its log.
TEST_CASE("measure the shipped world's saves over hours of play" * doctest::skip()) {
    using Clock = std::chrono::steady_clock;
    struct Checkpoint {
        u32 hours;
        u32 months;
    };
    constexpr std::array<Checkpoint, 5> CHECKPOINTS{ {
        { .hours = 1, .months = 40 },
        { .hours = 10, .months = 400 },
        { .hours = 50, .months = 2'000 },
        { .hours = 100, .months = 4'000 },
        { .hours = 250, .months = 10'000 },
    } };
    const auto milliseconds = [](Clock::duration span) { return std::chrono::duration<f64, std::milli>(span).count(); };
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "project_n_measure_hours.bin";
    const std::filesystem::path wholePath = std::filesystem::temp_directory_path() / "project_n_measure_hours_whole.bin";
    game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    const game::Host base(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    game::Host oneWorker(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    constexpr u32 MANY_WORKERS = 4;
    core::Jobs jobs{ MANY_WORKERS };
    core::Jobs oneJob{ 1 };
    u32 month = 0;
    for (const Checkpoint& checkpoint : CHECKPOINTS) {
        const Clock::time_point stepsStart = Clock::now();
        const u32 stretch = checkpoint.months - month;
        const u32 stretchStart = month;
        for (; month < checkpoint.months; ++month) {
            host.AdvanceCalendar({ .wall = month + 1, .simulation = month + 1, .state = month + 1 }, jobs, {});
        }
        const f64 stepMs = milliseconds(Clock::now() - stepsStart) / stretch;

        const Clock::time_point oneStart = Clock::now();
        for (u32 step = stretchStart; step < checkpoint.months; ++step) {
            oneWorker.AdvanceCalendar({ .wall = step + 1, .simulation = step + 1, .state = step + 1 }, oneJob, {});
        }
        const f64 oneStepMs = milliseconds(Clock::now() - oneStart) / stretch;
        const u64 state = host.StateHash();
        const u64 world = host.WorldHash();
        const u64 oneState = oneWorker.StateHash();
        const u64 oneWorld = oneWorker.WorldHash();
        const bool alike = oneState == state && oneWorld == world;
        CAPTURE(checkpoint.hours); // when the two runs parted
        CHECK(oneState == state);
        CHECK(oneWorld == world);
        PN_LOG_INFO(
            "{} h: StateHash {:016x} WorldHash {:016x} on {} and 1 workers{}, 1-worker month step {:.1f} ms",
            checkpoint.hours,
            state,
            world,
            MANY_WORKERS,
            alike ? " (alike)" : " (DIFFERENT)",
            oneStepMs
        );

        std::vector<std::byte> bytes;
        const Clock::time_point serialiseStart = Clock::now();
        core::Writer writer{ bytes };
        host.Save(writer);
        const Clock::time_point serialised = Clock::now();
        REQUIRE(core::WriteSaveFile(path, bytes).has_value());
        const Clock::time_point written = Clock::now();
        const std::uintmax_t onDisk = std::filesystem::file_size(path);

        game::Host loaded(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
        const Clock::time_point loadStart = Clock::now();
        const auto read = core::ReadSaveFile(path);
        REQUIRE(read.has_value());
        core::Reader reader{ *read };
        REQUIRE(loaded.Load(reader, 1));
        loaded.EnterScene();
        const f64 loadMs = milliseconds(Clock::now() - loadStart);
        CHECK(loaded.StateHash() == host.StateHash());
        CHECK(loaded.WorldHash() == host.WorldHash());

        const LongSave save = MeasureLongSave(host, base, bytes);
        REQUIRE(core::WriteSaveFile(wholePath, save.wholeSave).has_value());
        const std::uintmax_t wholeOnDisk = std::filesystem::file_size(wholePath);
        PN_LOG_INFO(
            "{} h = {} months: raw {} B, on disk {} B, save {:.1f} + {:.1f} ms, load {:.1f} ms, month step {:.1f} ms",
            checkpoint.hours,
            checkpoint.months,
            save.total,
            onDisk,
            milliseconds(serialised - serialiseStart),
            milliseconds(written - serialised),
            loadMs,
            stepMs
        );
        PN_LOG_INFO(
            "  bytes: domain {}, order {}, journeys gone {} planned {} whole {}, journals {}, relationships {}, rest {}",
            save.domain,
            save.order,
            save.journeyGone,
            save.journeyPlans,
            save.journeyWhole,
            save.journals,
            save.relationshipBytes,
            save.total - save.domain - save.order - save.journeyGone - save.journeyPlans - save.journeyWhole - save.journals -
                save.relationshipBytes
        );
        PN_LOG_INFO(
            "  counts: npcs {}, changed {}, created {}, gone {}, journeys {} (base gone {}, planned {}, whole {}), journal "
            "entries {}, relationships {}; whole save raw {} B, on disk {} B",
            save.npcs,
            save.delta.changed,
            save.delta.created,
            save.delta.removed,
            save.journeys,
            save.goneJourneys,
            save.plannedJourneys,
            save.wholeJourneys,
            save.journalEntries,
            save.relationships,
            save.wholeSave.size(),
            wholeOnDisk
        );
        PN_LOG_INFO(
            "  holders by edges: none {}, 1 to {} {}, exactly {} {}, more {}",
            save.holdersNone,
            game::RELATIONSHIP_LENGTH - 1,
            save.holdersSome,
            game::RELATIONSHIP_LENGTH,
            save.holdersAtLimit,
            save.holdersOverLimit
        );
    }
    std::filesystem::remove(path);
    std::filesystem::remove(wholePath);
}

} // namespace
