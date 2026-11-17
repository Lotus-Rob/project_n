#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/journey.hpp"
#include "core/world_map.hpp"

#include <doctest/doctest.h>

namespace {

constexpr f32 TILE = 256.0F;
constexpr u16 ROAD_COST = 4;
constexpr u16 FORD_COST = 30;
constexpr u16 MEADOW_COST = 10;
constexpr u16 SAND_COST = 14;
constexpr u8 MEADOW = 0;
constexpr u8 SAND = 1;
constexpr u32 PEOPLE = 100;
constexpr u32 COLUMNS = 7;
constexpr u32 ROWS = 3;
constexpr u16 HOME = 0;
constexpr u16 DESTINATION = 1;
constexpr u32 SEED = 0x5eed;
constexpr u32 TWO_DAYS = 2;
constexpr u32 FIVE_DAYS = 5;
constexpr core::StableId WALKER{ 41 };
constexpr core::StableId SECOND{ 42 };
constexpr core::StableId THIRD{ 43 };
constexpr f32 MEMBER_SPACING = 24.0F;
constexpr f32 CLOSE = 0.001F;
// A day in a journey's signed ticks.
constexpr i64 DAY = static_cast<i64>(core::TICKS_PER_DAY);
// How far from its city's tile centre a staying walker may be, in tiles.
constexpr f32 STAY_REACH = 0.35F;
// The last road tile's column.
constexpr u32 ROAD_END = 5;
// A departure before tick 0.
constexpr i64 BEFORE_ZERO = -200;

// The test map, 7 by 3 tiles of meadow: city 0 (the home, on sand) at tile 0, a road over tiles 1 to 5 of the top row,
// and city 1 (the destination, on meadow) at tile 6, its right end. Its profile is the default on_foot at pace 16.
core::WorldMap TestMap() {
    core::WorldMapSource source;
    source.columns = COLUMNS;
    source.rows = ROWS;
    source.tileSize = TILE;
    source.roadCost = ROAD_COST;
    source.fordCost = FORD_COST;
    source.kinds = { { .name = "meadow", .symbol = '.', .cost = MEADOW_COST },
                     { .name = "sand", .symbol = ',', .cost = SAND_COST } };
    source.grid.assign(static_cast<std::size_t>(COLUMNS) * ROWS, MEADOW);
    source.grid[0] = SAND;
    source.roads = { { { .column = 1, .row = 0 }, { .column = ROAD_END, .row = 0 } } };
    for (const u16 city : { HOME, DESTINATION }) {
        source.places.push_back(
            { .key = std::format("city{}", city),
              .kind = core::PlaceKind::City,
              .sort = "",
              .tile = { .column = city == HOME ? 0U : COLUMNS - 1, .row = 0 },
              .nameKey = std::format("city_{}", city),
              .name = {},
              .people = PEOPLE,
              .start = city == HOME }
        );
    }
    auto built = core::WorldMap::Build(std::move(source));
    REQUIRE(built.has_value());
    return *std::move(built);
}

// The top row from the home's tile to the destination's: sand, five road tiles, then the destination's meadow.
constexpr std::array<u32, 7> ROAD_ROUTE{ { 0, 1, 2, 3, 4, 5, 6 } };

f32 Distance(core::Vec2 from, core::Vec2 target) {
    const core::Vec2 apart = target - from;
    return std::sqrt((apart.x * apart.x) + (apart.y * apart.y));
}

// The largest distance between the journey's positions (of `member`) one tick apart, over the ticks from `first` to
// `last` (both included) and the tick after each.
f32 LargestJump(const core::WorldMap& map, const core::Journey& journey, i64 first, i64 last) {
    f32 largest = 0.0F;
    for (i64 tick = first; tick <= last; ++tick) {
        largest = std::max(largest, Distance(core::Where(map, journey, tick), core::Where(map, journey, tick + 1)));
    }
    return largest;
}

TEST_CASE("a tile leg takes its cost in ticks") {
    const core::WorldMap map = TestMap();
    constexpr i64 START = 1'000;
    constexpr std::array<u32, 3> SAND_THEN_ROAD{ { 0, 1, 2 } };
    const core::Leg road = core::MakeTileLeg(map, core::WALKING, START, map.TileCentre(0), SAND_THEN_ROAD);
    CHECK(road.shape == core::LegShape::TileRoute);
    CHECK(road.profile == core::WALKING);
    CHECK(road.startTick == START);
    CHECK(road.tiles == std::vector<u32>{ 0, 1, 2 });
    CHECK(road.arrive == std::vector<u32>{ 0, 64, 128 });
    CHECK(core::EndTick(road) == START + 128);

    constexpr std::array<u32, 2> DIAGONAL_MEADOW{ { 0, COLUMNS + 1 } };
    const core::Leg diagonal = core::MakeTileLeg(map, core::WALKING, START, map.TileCentre(0), DIAGONAL_MEADOW);
    CHECK(diagonal.arrive == std::vector<u32>{ 0, 14 * 16 });
}

TEST_CASE("a walker is at its origin before the leg and its last tile after") {
    const core::WorldMap map = TestMap();
    constexpr i64 START = 100;
    // The player's foot, off its tile's centre: the leg starts from it, not from the centre.
    const core::Vec2 foot = map.TileCentre(0) + core::Vec2{ .x = 30.0F, .y = -20.0F };
    const core::Leg leg = core::MakeTileLeg(map, core::WALKING, START, foot, ROAD_ROUTE);
    const core::Vec2 last = map.TileCentre(ROAD_ROUTE.back());
    const core::Vec2 halfway = (foot + map.TileCentre(1)) * 0.5F;

    const core::Vec2 before = core::Where(map, leg, START - 50);
    const core::Vec2 atStart = core::Where(map, leg, START);
    const core::Vec2 firstHalf = core::Where(map, leg, START + 32);
    const core::Vec2 atEnd = core::Where(map, leg, core::EndTick(leg));
    const core::Vec2 after = core::Where(map, leg, core::EndTick(leg) + 1'000);
    CHECK(before.x == foot.x);
    CHECK(before.y == foot.y);
    CHECK(atStart.x == foot.x);
    CHECK(atStart.y == foot.y);
    CHECK(Distance(firstHalf, halfway) < CLOSE);
    CHECK(atEnd.x == last.x);
    CHECK(atEnd.y == last.y);
    CHECK(after.x == last.x);
    CHECK(after.y == last.y);
}

TEST_CASE("a walker moves without a jump") {
    const core::WorldMap map = TestMap();
    constexpr i64 DEPART = 0;
    const core::Journey visit = core::Visit(map, ROAD_ROUTE, WALKER, HOME, DESTINATION, DEPART, TWO_DAYS, SEED);
    REQUIRE(visit.parts.size() == 3);
    const core::Leg& out = visit.parts[0].leg;
    const core::Stay& stay = visit.parts[1].rest;
    const core::Leg& back = visit.parts[2].leg;
    // The fastest step: a road tile, a tile's side in 64 ticks.
    constexpr f32 FASTEST = TILE / 64.0F;
    constexpr f32 EPSILON = 0.01F;

    // Between two arrivals a walker keeps one speed, so a jump would show at an arrival: the ticks either side of each.
    f32 largest = LargestJump(map, visit, DEPART - 1, DEPART);
    for (const u32 arrive : out.arrive) {
        const i64 arrival = out.startTick + i64{ arrive };
        largest = std::max(largest, LargestJump(map, visit, arrival - 1, arrival));
    }
    CHECK(largest < FASTEST + EPSILON);
    CHECK(LargestJump(map, visit, core::EndTick(out) - 2, core::EndTick(out) - 1) < FASTEST + EPSILON);
    CHECK(LargestJump(map, visit, stay.startTick, stay.startTick + 1) < FASTEST + EPSILON);
    CHECK(LargestJump(map, visit, stay.endTick - 2, stay.endTick - 1) < FASTEST + EPSILON);
    CHECK(LargestJump(map, visit, back.startTick, back.startTick + 1) < FASTEST + EPSILON);
}

TEST_CASE("a visit comes home by its own road reversed") {
    const core::WorldMap map = TestMap();
    constexpr i64 DEPART = 500;
    const core::Journey visit = core::Visit(map, ROAD_ROUTE, WALKER, HOME, DESTINATION, DEPART, TWO_DAYS, SEED);
    CHECK(visit.members == std::vector<core::StableId>{ WALKER });
    CHECK(visit.home == HOME);
    CHECK(visit.destination == DESTINATION);
    REQUIRE(visit.parts.size() == 3);
    CHECK_FALSE(visit.parts[0].stay);
    CHECK(visit.parts[1].stay);
    CHECK_FALSE(visit.parts[2].stay);

    const core::Leg& out = visit.parts[0].leg;
    const core::Stay& stay = visit.parts[1].rest;
    const core::Leg& back = visit.parts[2].leg;
    CHECK(out.startTick == DEPART);
    CHECK(out.arrive == std::vector<u32>{ 0, 64, 128, 192, 256, 320, 480 });
    CHECK(stay.city == DESTINATION);
    CHECK(stay.seed == SEED);
    CHECK(stay.startTick == DEPART + 480);
    CHECK(stay.endTick == DEPART + 480 + (TWO_DAYS * DAY));
    CHECK(back.startTick == stay.endTick);
    CHECK(back.tiles == std::vector<u32>{ 6, 5, 4, 3, 2, 1, 0 });
    // Into the road tiles at 64 ticks each, then into the home's sand at 14 × 16.
    CHECK(back.arrive == std::vector<u32>{ 0, 64, 128, 192, 256, 320, 544 });
    const core::Vec2 backOrigin = map.TileCentre(6);
    CHECK(back.origin.x == backOrigin.x);
    CHECK(back.origin.y == backOrigin.y);
    CHECK(core::StartTick(visit) == DEPART);
    CHECK(core::EndTick(visit) == back.startTick + 544);
}

TEST_CASE("a stay keeps its walker near the city's tile") {
    const core::WorldMap map = TestMap();
    const core::Journey visit = core::Visit(map, ROAD_ROUTE, WALKER, HOME, DESTINATION, 0, FIVE_DAYS, SEED);
    const core::Stay& stay = visit.parts[1].rest;
    const core::Vec2 centre = map.TileCentre(ROAD_ROUTE.back());
    // The stay's ends, each day point and the ticks either side of it, and the middles of a few days.
    const std::array<i64, 20> ticks{ { 0,
                                       1,
                                       DAY / 2,
                                       DAY - 1,
                                       DAY,
                                       DAY + 1,
                                       (DAY * 3) / 2,
                                       (2 * DAY) - 1,
                                       2 * DAY,
                                       (2 * DAY) + 1,
                                       (3 * DAY) - 1,
                                       3 * DAY,
                                       (3 * DAY) + 1,
                                       (7 * DAY) / 2,
                                       (4 * DAY) - 1,
                                       4 * DAY,
                                       (4 * DAY) + 1,
                                       (9 * DAY) / 2,
                                       (5 * DAY) - 1,
                                       5 * DAY } };
    f32 farthest = 0.0F;
    for (const i64 tick : ticks) {
        farthest = std::max(farthest, Distance(core::Where(map, visit, stay.startTick + tick), centre));
    }
    CHECK(farthest <= STAY_REACH * TILE);
    CHECK(farthest > 0.0F); // it drifts: a walker standing on the centre would pass the first check alone
}

TEST_CASE("two members of one journey stand apart") {
    const core::WorldMap map = TestMap();
    core::Journey convoy = core::Visit(map, ROAD_ROUTE, WALKER, HOME, DESTINATION, 0, TWO_DAYS, SEED);
    convoy.members = { WALKER, SECOND, THIRD };
    constexpr i64 ON_THE_ROAD = 100;
    const i64 inTheCity = convoy.parts[1].rest.startTick + DAY + 30;
    for (const i64 tick : { ON_THE_ROAD, inTheCity }) {
        const core::Vec2 first = core::Where(map, convoy, tick, 0);
        const core::Vec2 second = core::Where(map, convoy, tick, 1);
        const core::Vec2 third = core::Where(map, convoy, tick, 2);
        const core::Vec2 onIt = core::Where(map, convoy, tick);
        CHECK(first.x == onIt.x);
        CHECK(first.y == onIt.y);
        CHECK(Distance(first, second) == doctest::Approx(MEMBER_SPACING).epsilon(CLOSE));
        CHECK(Distance(first, third) == doctest::Approx(MEMBER_SPACING).epsilon(CLOSE));
        CHECK(Distance(second, third) == doctest::Approx(MEMBER_SPACING).epsilon(CLOSE));
    }
}

TEST_CASE("a journey places its legs exactly as Where of the leg does") {
    const core::WorldMap map = TestMap();
    constexpr std::array<u32, 4> ALONG_THE_ROAD{ { 0, 1, 2, 3 } };
    constexpr std::array<u32, 3> DOWN_THE_MEADOW{ { 3, COLUMNS + 3, (2 * COLUMNS) + 3 } };
    const core::Leg first = core::MakeTileLeg(map, core::WALKING, 0, map.TileCentre(0), ALONG_THE_ROAD);
    const core::Leg second =
        core::MakeTileLeg(map, core::WALKING, core::EndTick(first), map.TileCentre(ALONG_THE_ROAD.back()), DOWN_THE_MEADOW);
    core::Journey journey;
    journey.members = { WALKER };
    journey.parts = { { .stay = false, .leg = first, .rest = {} }, { .stay = false, .leg = second, .rest = {} } };

    struct Sample {
        i64 tick;
        const core::Leg* leg;
    };
    const std::array<Sample, 5> samples{ { { .tick = -10, .leg = &first },
                                           { .tick = 100, .leg = &first },
                                           { .tick = core::EndTick(first), .leg = &second },
                                           { .tick = core::EndTick(first) + 77, .leg = &second },
                                           { .tick = core::EndTick(second) + 10, .leg = &second } } };
    for (const Sample& sample : samples) {
        CAPTURE(sample.tick);
        const core::Vec2 onJourney = core::Where(map, journey, sample.tick);
        const core::Vec2 onLeg = core::Where(map, *sample.leg, sample.tick);
        CHECK(onJourney.x == onLeg.x);
        CHECK(onJourney.y == onLeg.y);
    }
}

TEST_CASE("signed ticks place a journey that left before tick 0") {
    const core::WorldMap map = TestMap();
    constexpr i64 DEPART = BEFORE_ZERO;
    const core::Journey visit = core::Visit(map, ROAD_ROUTE, WALKER, HOME, DESTINATION, DEPART, TWO_DAYS, SEED);
    CHECK(core::StartTick(visit) == DEPART);
    CHECK(core::EndTick(visit) == DEPART + 480 + (TWO_DAYS * DAY) + 544);
    CHECK_FALSE(core::Underway(visit, DEPART - 1));
    CHECK(core::Underway(visit, DEPART));
    CHECK(core::Underway(visit, 0));
    CHECK(core::Underway(visit, core::EndTick(visit) - 1));
    CHECK_FALSE(core::Underway(visit, core::EndTick(visit)));
    // Tick 0 is 200 ticks in: past tile 3 (192 ticks), an eighth of the way into the 64-tick step to tile 4.
    const core::Vec2 expected = map.TileCentre(3) + ((map.TileCentre(4) - map.TileCentre(3)) * 0.125F);
    CHECK(Distance(core::Where(map, visit, 0), expected) < CLOSE);
}

TEST_CASE("a journey is written and read back whole") {
    const core::WorldMap map = TestMap();
    core::Journey visit = core::Visit(map, ROAD_ROUTE, WALKER, HOME, DESTINATION, BEFORE_ZERO, TWO_DAYS, SEED);
    visit.members.push_back(SECOND);
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    writer(visit);

    core::Reader reader{ bytes };
    core::Journey read;
    reader(read);
    CHECK_FALSE(reader.Failed());
    CHECK(reader.Remaining() == 0);
    CHECK(read.members == visit.members);
    CHECK(read.destination == DESTINATION);
    REQUIRE(read.parts.size() == 3);
    CHECK(read.parts[1].stay);
    CHECK(read.parts[1].rest.endTick == visit.parts[1].rest.endTick);
    CHECK(read.parts[2].leg.tiles == visit.parts[2].leg.tiles);
    CHECK(read.parts[2].leg.arrive == visit.parts[2].leg.arrive);
    const core::Vec2 before = core::Where(map, visit, 0, 1);
    const core::Vec2 after = core::Where(map, read, 0, 1);
    CHECK(after.x == before.x);
    CHECK(after.y == before.y);
}

} // namespace
