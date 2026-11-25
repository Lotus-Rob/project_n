#include "core/archive.hpp"
#include "core/calendar.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/journey.hpp"
#include "core/leg_walk_cache.hpp"
#include "core/paths.hpp"
#include "core/world_map.hpp"

#include "support/meadow.hpp"

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

// The test map's source, 7 by 3 tiles of meadow: city 0 (the home, on sand) at tile 0, a road over tiles 1 to 5 of the top
// row, and city 1 (the destination, on meadow) at tile 6, its right end. Its profile is the default on_foot at pace 16.
core::WorldMapSource TestSource() {
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
    return source;
}

core::WorldMap Built(core::WorldMapSource source) {
    auto built = core::WorldMap::Build(std::move(source));
    INFO((built ? std::string{} : built.error().message));
    REQUIRE(built.has_value());
    return *std::move(built);
}

// The test map (TestSource()).
core::WorldMap TestMap() {
    return Built(TestSource());
}

// A laden walker's profile on the test map: slower, dearer on every kind and helped by no road, so its routes and ticks
// differ from walking's.
constexpr core::ProfileIndex LADEN = 1;
constexpr u16 LADEN_TICKS_PER_COST = 24;
constexpr u16 LADEN_MEADOW_COST = 12;
constexpr u16 LADEN_SAND_COST = 40;

// The test map with LADEN after the default on_foot.
core::WorldMap TwoProfileMap() {
    core::WorldMapSource source = TestSource();
    const core::MovementProfile walking = TestMap().Profiles()[core::WALKING];
    core::MovementProfile laden = walking;
    laden.key = "laden";
    laden.ticksPerCost = LADEN_TICKS_PER_COST;
    laden.kindCosts = { LADEN_MEADOW_COST, LADEN_SAND_COST };
    laden.features.road = LADEN_MEADOW_COST;
    source.profiles = { walking, laden };
    return Built(std::move(source));
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

/// The leg core::MakePacedLeg() makes of these; REQUIREs one.
core::Leg PacedLeg(const core::WorldMap& map, i64 startTick, core::Vec2 origin, std::span<const u32> tiles, f32 unitsPerTick) {
    const std::optional<core::Leg> leg = core::MakePacedLeg(map, startTick, origin, tiles, unitsPerTick);
    REQUIRE(leg.has_value());
    return leg.value_or(core::Leg{});
}

TEST_CASE("a paced leg takes the same ticks a tile on every kind") {
    const core::WorldMap map = TestMap();
    constexpr f32 WALK_SPEED = 4.0F;
    constexpr std::array<u32, 4> MEADOW_RUN{ { COLUMNS + 1, COLUMNS + 2, COLUMNS + 3, COLUMNS + 4 } };
    constexpr std::array<u32, 4> ROAD_RUN{ { 1, 2, 3, 4 } };
    constexpr std::array<u32, 4> SAND_THEN_ROAD{ { 0, 1, 2, 3 } };
    for (const std::span<const u32> tiles :
         { std::span<const u32>{ MEADOW_RUN }, std::span<const u32>{ ROAD_RUN }, std::span<const u32>{ SAND_THEN_ROAD } }) {
        const core::Leg leg = PacedLeg(map, 0, map.TileCentre(tiles.front()), tiles, WALK_SPEED);
        CHECK(leg.profile == core::WALKING);
        CHECK(leg.arrive == std::vector<u32>{ 0, 64, 128, 192 });
    }
}

TEST_CASE("a paced leg's diagonal steps take their length's time") {
    const core::WorldMap map = TestMap();
    constexpr std::array<u32, 3> DIAGONALS{ { 0, COLUMNS + 1, (2 * COLUMNS) + 2 } };
    const core::Leg leg = PacedLeg(map, 0, map.TileCentre(0), DIAGONALS, 4.0F);
    CHECK(leg.arrive == std::vector<u32>{ 0, 91, 182 });
}

TEST_CASE("a paced leg's first step from a foot near the next centre takes at least a tick") {
    const core::WorldMap map = TestMap();
    constexpr std::array<u32, 3> NEXT{ { 0, 1, 2 } };
    const core::Vec2 foot = map.TileCentre(1) - core::Vec2{ .x = 1.0F, .y = 0.0F };
    const core::Leg leg = PacedLeg(map, 5, foot, NEXT, 4.0F);
    CHECK(leg.arrive == std::vector<u32>{ 0, 1, 65 });
}

TEST_CASE("a paced leg's first step from a foot already on the next centre takes a tick") {
    const core::WorldMap map = TestMap();
    constexpr std::array<u32, 2> NEXT{ { 0, 1 } };
    const core::Leg leg = PacedLeg(map, 5, map.TileCentre(1), NEXT, 4.0F);
    CHECK(leg.arrive == std::vector<u32>{ 0, 1 });
}

TEST_CASE("a paced leg whose ticks would not fit, or that never moves, is refused") {
    const core::WorldMap map = TestMap();
    constexpr std::array<u32, 2> NEXT{ { 0, 1 } };
    // A tile of 256 units at a billionth of a unit a tick is 2.56e11 ticks, past a leg's 32-bit arrive ticks.
    CHECK_FALSE(core::MakePacedLeg(map, 0, map.TileCentre(0), NEXT, 1e-9F).has_value());
    CHECK_FALSE(core::MakePacedLeg(map, 0, map.TileCentre(0), NEXT, 0.0F).has_value());
    CHECK_FALSE(core::MakePacedLeg(map, 0, map.TileCentre(0), NEXT, -4.0F).has_value());
    CHECK_FALSE(core::MakePacedLeg(map, 0, map.TileCentre(0), NEXT, std::numeric_limits<f32>::quiet_NaN()).has_value());
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
    const core::Journey visit = core::Visit(map, ROAD_ROUTE, core::WALKING, WALKER, HOME, DESTINATION, DEPART, TWO_DAYS, SEED);
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
    const core::Journey visit = core::Visit(map, ROAD_ROUTE, core::WALKING, WALKER, HOME, DESTINATION, DEPART, TWO_DAYS, SEED);
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
    const core::Journey visit = core::Visit(map, ROAD_ROUTE, core::WALKING, WALKER, HOME, DESTINATION, 0, FIVE_DAYS, SEED);
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
    core::Journey convoy = core::Visit(map, ROAD_ROUTE, core::WALKING, WALKER, HOME, DESTINATION, 0, TWO_DAYS, SEED);
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
    const core::Journey visit = core::Visit(map, ROAD_ROUTE, core::WALKING, WALKER, HOME, DESTINATION, DEPART, TWO_DAYS, SEED);
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
    core::Journey visit = core::Visit(map, ROAD_ROUTE, core::WALKING, WALKER, HOME, DESTINATION, BEFORE_ZERO, TWO_DAYS, SEED);
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

/// `journey` as an archive writes it: every field, so two journeys with equal bytes are equal field by field.
std::vector<std::byte> BytesOf(const core::Journey& journey) {
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    writer(journey);
    return bytes;
}

/// A visit's defining numbers, for a table of samples.
struct Sample {
    const char* why;
    u32 from;
    std::size_t members;
    u32 stayDays;
    i64 departTick;
};

TEST_CASE("a visit by any profile rebuilt from its plan equals the visit") {
    const auto map = std::make_shared<const core::WorldMap>(TwoProfileMap());
    core::Paths paths{ map };
    constexpr u32 LONG_STAY_DAYS = 40; // longer than a month
    constexpr u32 THREE_DAYS = 3;
    constexpr i64 MONTH = static_cast<i64>(core::TICKS_PER_MONTH);
    constexpr u32 BOTTOM_ROW = COLUMNS * 2; // the bottom row's first tile
    constexpr u32 NEXT_TO_DESTINATION = ROAD_END;
    constexpr u32 DESTINATION_TILE = COLUMNS - 1;
    const std::array<Sample, 20> samples{ {
        { .why = "one member, the whole road", .from = 0, .members = 1, .stayDays = TWO_DAYS, .departTick = 0 },
        { .why = "three members", .from = 0, .members = 3, .stayDays = TWO_DAYS, .departTick = 0 },
        { .why = "two members, a longer stay, leaving a tick in",
          .from = 0,
          .members = 2,
          .stayDays = FIVE_DAYS,
          .departTick = 1 },
        { .why = "a stay of no days", .from = 0, .members = 1, .stayDays = 0, .departTick = 1'000 },
        { .why = "a stay crossing a month", .from = 0, .members = 1, .stayDays = LONG_STAY_DAYS, .departTick = MONTH - 100 },
        { .why = "leaving on a month's first tick", .from = 0, .members = 2, .stayDays = LONG_STAY_DAYS, .departTick = MONTH },
        { .why = "leaving on a month's last tick", .from = 0, .members = 1, .stayDays = TWO_DAYS, .departTick = MONTH - 1 },
        { .why = "a stay ending on a month boundary",
          .from = DESTINATION_TILE,
          .members = 1,
          .stayDays = THREE_DAYS,
          .departTick = MONTH - (THREE_DAYS * DAY) },
        { .why = "a route of one tile, the destination's own",
          .from = DESTINATION_TILE,
          .members = 1,
          .stayDays = TWO_DAYS,
          .departTick = 0 },
        { .why = "a one-tile route with a stay of no days",
          .from = DESTINATION_TILE,
          .members = 2,
          .stayDays = 0,
          .departTick = 7 },
        { .why = "left shortly before tick 0", .from = 0, .members = 1, .stayDays = TWO_DAYS, .departTick = BEFORE_ZERO },
        { .why = "left a month before tick 0, three members",
          .from = 0,
          .members = 3,
          .stayDays = FIVE_DAYS,
          .departTick = -MONTH },
        { .why = "left on the tick before 0", .from = 0, .members = 1, .stayDays = THREE_DAYS, .departTick = -1 },
        { .why = "a two-tile route", .from = NEXT_TO_DESTINATION, .members = 1, .stayDays = TWO_DAYS, .departTick = 0 },
        { .why = "a two-tile route, a long stay, a month's last tick",
          .from = NEXT_TO_DESTINATION,
          .members = 2,
          .stayDays = LONG_STAY_DAYS,
          .departTick = MONTH - 1 },
        { .why = "from the bottom row's start, before tick 0",
          .from = BOTTOM_ROW,
          .members = 1,
          .stayDays = FIVE_DAYS,
          .departTick = BEFORE_ZERO },
        { .why = "from the bottom row's fourth tile, three members",
          .from = BOTTOM_ROW + 3,
          .members = 3,
          .stayDays = TWO_DAYS,
          .departTick = 12'345 },
        { .why = "from the sand of the home's tile, a day in", .from = 0, .members = 1, .stayDays = 1, .departTick = DAY },
        { .why = "from a meadow corner, a month in",
          .from = (COLUMNS * ROWS) - 1,
          .members = 2,
          .stayDays = TWO_DAYS,
          .departTick = MONTH },
        { .why = "from the middle row's start, a long stay",
          .from = COLUMNS,
          .members = 1,
          .stayDays = LONG_STAY_DAYS,
          .departTick = 99 },
    } };
    const std::array<core::StableId, 3> everyone{ WALKER, SECOND, THIRD };
    for (const core::ProfileIndex profile : { core::WALKING, LADEN }) {
        for (const Sample& sample : samples) {
            CAPTURE(profile);
            CAPTURE(sample.why);
            CAPTURE(sample.from);
            CAPTURE(sample.members);
            CAPTURE(sample.departTick);
            CAPTURE(sample.stayDays);
            const std::vector<u32> route = core::RouteDown(*map, paths.To(DESTINATION, profile), sample.from);
            REQUIRE_FALSE(route.empty());
            core::Journey visit =
                core::Visit(*map, route, profile, WALKER, HOME, DESTINATION, sample.departTick, sample.stayDays, SEED);
            visit.members.assign(everyone.begin(), std::next(everyone.begin(), static_cast<std::ptrdiff_t>(sample.members)));
            CHECK(visit.parts[0].leg.profile == profile);
            CHECK(visit.parts[2].leg.profile == profile);

            REQUIRE(core::PlanOf(visit).has_value());
            const core::VisitPlan plan = core::PlanOf(visit).value_or(core::VisitPlan{});
            CHECK(plan.profile == profile);
            CHECK(plan.members == visit.members);
            CHECK(plan.home == HOME);
            CHECK(plan.destination == DESTINATION);
            CHECK(plan.from == sample.from);
            CHECK(plan.departTick == sample.departTick);
            CHECK(plan.stayDays == sample.stayDays);
            CHECK(plan.seed == SEED);
            const core::Journey rebuilt = core::Rebuild(*map, paths, plan);
            REQUIRE(rebuilt.parts.size() == 3);
            CHECK(rebuilt.parts[0].leg.origin.x == visit.parts[0].leg.origin.x);
            CHECK(rebuilt.parts[0].leg.origin.y == visit.parts[0].leg.origin.y);
            CHECK(rebuilt.parts[2].leg.origin.x == visit.parts[2].leg.origin.x);
            CHECK(rebuilt.parts[2].leg.origin.y == visit.parts[2].leg.origin.y);
            const bool same = BytesOf(rebuilt) == BytesOf(visit);
            CHECK(same);
        }
    }
}

TEST_CASE("a journey whose legs differ in profile has no plan") {
    const core::WorldMap map = TwoProfileMap();
    // The road row is as walkable laden, so one route serves both profiles.
    const core::Journey laden = core::Visit(map, ROAD_ROUTE, LADEN, WALKER, HOME, DESTINATION, 0, TWO_DAYS, SEED);
    REQUIRE(core::PlanOf(laden).has_value());
    CHECK(core::PlanOf(laden).value_or(core::VisitPlan{}).profile == LADEN);

    core::Journey walkedHome = laden;
    walkedHome.parts[2].leg = core::MakeTileLeg(
        map, core::WALKING, laden.parts[2].leg.startTick, laden.parts[2].leg.origin, laden.parts[2].leg.tiles
    );
    CHECK_FALSE(core::PlanOf(walkedHome).has_value());

    core::Journey walkedOut = laden;
    walkedOut.parts[0].leg.profile = core::WALKING;
    CHECK_FALSE(core::PlanOf(walkedOut).has_value());
}

TEST_CASE("a journey a visit didn't make has no plan") {
    const auto map = std::make_shared<const core::WorldMap>(TestMap());
    const core::Journey visit = core::Visit(*map, ROAD_ROUTE, core::WALKING, WALKER, HOME, DESTINATION, 0, TWO_DAYS, SEED);
    REQUIRE(core::PlanOf(visit).has_value());

    core::Journey withoutParts = visit;
    withoutParts.parts.clear();
    CHECK_FALSE(core::PlanOf(withoutParts).has_value());

    core::Journey legOnly = visit;
    legOnly.parts.resize(1);
    CHECK_FALSE(core::PlanOf(legOnly).has_value());

    core::Journey otherWay = visit;
    std::ranges::reverse(otherWay.parts[2].leg.tiles);
    CHECK_FALSE(core::PlanOf(otherWay).has_value());

    core::Journey gap = visit;
    gap.parts[2].leg.startTick += 1;
    CHECK_FALSE(core::PlanOf(gap).has_value());

    core::Journey partialDay = visit;
    partialDay.parts[1].rest.endTick += 1;
    partialDay.parts[2].leg.startTick += 1;
    CHECK_FALSE(core::PlanOf(partialDay).has_value());

    core::Journey memberless = visit;
    memberless.members.clear();
    CHECK_FALSE(core::PlanOf(memberless).has_value());
}

TEST_CASE("a plan from where its profile can't step rebuilds to nothing") {
    constexpr u8 SHALLOWS = 1;
    constexpr u32 SHALLOWS_TILE = COLUMNS + 3;
    core::WorldMapSource source;
    source.columns = COLUMNS;
    source.rows = ROWS;
    source.tileSize = TILE;
    source.roadCost = ROAD_COST;
    source.fordCost = FORD_COST;
    source.kinds = { { .name = "meadow", .symbol = '.', .cost = MEADOW_COST },
                     { .name = "shallows", .symbol = '-', .cost = FORD_COST } };
    source.grid.assign(static_cast<std::size_t>(COLUMNS) * ROWS, MEADOW);
    source.grid[SHALLOWS_TILE] = SHALLOWS;
    // A wader crosses the shallows walkers can't.
    constexpr core::ProfileIndex WADING = 1;
    source.profiles = { { .key = "on_foot",
                          .ticksPerCost = core::DEFAULT_TICKS_PER_COST,
                          .kindCosts = { MEADOW_COST, core::IMPASSABLE_COST },
                          .features = {} },
                        { .key = "wading",
                          .ticksPerCost = core::DEFAULT_TICKS_PER_COST,
                          .kindCosts = { MEADOW_COST, FORD_COST },
                          .features = {} } };
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
    const auto map = std::make_shared<const core::WorldMap>(*std::move(built));
    REQUIRE(map->CostAt(SHALLOWS_TILE) != core::IMPASSABLE_COST);
    REQUIRE(map->CostAt(SHALLOWS_TILE, core::WALKING) == core::IMPASSABLE_COST);
    core::Paths paths{ map };
    core::VisitPlan plan{ .members = { WALKER },
                          .home = HOME,
                          .destination = DESTINATION,
                          .from = 0,
                          .departTick = 0,
                          .stayDays = TWO_DAYS,
                          .seed = SEED,
                          .profile = core::WALKING };
    CHECK_FALSE(core::Rebuild(*map, paths, plan).parts.empty());
    plan.from = SHALLOWS_TILE;
    CHECK(core::Rebuild(*map, paths, plan).parts.empty());
    // A wader may set out from the shallows: the tile is judged by the profile the journey moves by.
    plan.profile = WADING;
    const core::Journey waded = core::Rebuild(*map, paths, plan);
    REQUIRE_FALSE(waded.parts.empty());
    CHECK(waded.parts[0].leg.profile == WADING);
    CHECK(waded.parts[0].leg.tiles.front() == SHALLOWS_TILE);
}

// The walk tests' map: 3 by 3 tiles of meadow, the home city at tile 0 and the destination on tile 2, the top row's end.
constexpr u32 WALK_COLUMNS = 3;
// The walk map's tile side, as the distances walked are measured.
constexpr f64 WALK_TILE = 256.0;
constexpr std::array<core::TilePoint, 1> WALK_CITIES{ { { .column = 2, .row = 0 } } };
// The tiles of the walk map by column and row.
constexpr u32 TOP_LEFT = 0;
constexpr u32 TOP_MIDDLE = 1;
constexpr u32 MIDDLE_LEFT = WALK_COLUMNS;
constexpr u32 CENTRE = WALK_COLUMNS + 1;
// A departure tick far from 0, so a tick taken for a duration would show.
constexpr i64 WALK_START = 1'000;

core::WorldMap WalkMap() {
    return test::MeadowMap(WALK_COLUMNS, WALK_COLUMNS, {}, {}, WALK_CITIES);
}

/// A journey of WALKER alone along one tile leg over `tiles` from the first one's centre, setting out on WALK_START.
core::Journey OneLeg(const core::WorldMap& map, std::span<const u32> tiles) {
    core::Leg leg = core::MakeTileLeg(map, core::WALKING, WALK_START, map.TileCentre(tiles.front()), tiles);
    core::Journey journey{ .members = { WALKER }, .home = HOME, .destination = HOME, .parts = {} };
    journey.parts.push_back({ .stay = false, .leg = std::move(leg), .rest = {} });
    return journey;
}

/// The tick halfway through step `step` (from tile `step` to the next) of `journey`'s first part, a leg.
i64 HalfwayThrough(const core::Journey& journey, std::size_t step) {
    const core::Leg& leg = journey.parts.front().leg;
    return leg.startTick + ((i64{ leg.arrive[step] } + i64{ leg.arrive[step + 1] }) / 2);
}

TEST_CASE("a walk east then south counts its length and keeps facing right") {
    const core::WorldMap map = WalkMap();
    constexpr std::array<u32, 3> EAST_THEN_SOUTH{ { TOP_LEFT, TOP_MIDDLE, CENTRE } };
    const core::Journey journey = OneLeg(map, EAST_THEN_SOUTH);
    // Meadow steps of the same cost, so halfway in ticks is halfway along.
    REQUIRE(journey.parts.front().leg.arrive[2] == 2 * journey.parts.front().leg.arrive[1]);

    const core::Walked east = core::WalkedAt(map, journey, HalfwayThrough(journey, 0));
    CHECK(east.distance == 0.5 * WALK_TILE);
    CHECK(east.moving);
    CHECK_FALSE(east.facesLeft);

    // A step north or south keeps the facing the last step across left.
    const core::Walked south = core::WalkedAt(map, journey, HalfwayThrough(journey, 1));
    CHECK(south.distance == 1.5 * WALK_TILE);
    CHECK(south.moving);
    CHECK_FALSE(south.facesLeft);
}

TEST_CASE("a walk west faces left") {
    const core::WorldMap map = WalkMap();
    constexpr std::array<u32, 2> WEST{ { TOP_MIDDLE, TOP_LEFT } };
    const core::Journey journey = OneLeg(map, WEST);
    const core::Walked walked = core::WalkedAt(map, journey, HalfwayThrough(journey, 0));
    CHECK(walked.moving);
    CHECK(walked.facesLeft);
}

TEST_CASE("a walk stands before its journey and after its end") {
    const core::WorldMap map = WalkMap();
    constexpr std::array<u32, 3> EAST_THEN_SOUTH{ { TOP_LEFT, TOP_MIDDLE, CENTRE } };
    const core::Journey journey = OneLeg(map, EAST_THEN_SOUTH);

    const core::Walked first = core::WalkedAt(map, journey, core::StartTick(journey));
    CHECK(first.distance == 0.0);
    CHECK_FALSE(first.moving);

    // The end tick still finishes the last step.
    const core::Walked last = core::WalkedAt(map, journey, core::EndTick(journey));
    CHECK(last.distance == 2.0 * WALK_TILE);
    CHECK(last.moving);

    const core::Walked after = core::WalkedAt(map, journey, core::EndTick(journey) + 1);
    CHECK(after.distance == 2.0 * WALK_TILE);
    CHECK_FALSE(after.moving);
}

TEST_CASE("a walk whose first step is vertical faces its first step across") {
    const core::WorldMap map = WalkMap();
    constexpr std::array<u32, 3> SOUTH_THEN_WEST{ { TOP_MIDDLE, CENTRE, MIDDLE_LEFT } };
    const core::Journey journey = OneLeg(map, SOUTH_THEN_WEST);
    const core::Walked walked = core::WalkedAt(map, journey, HalfwayThrough(journey, 0));
    CHECK(walked.moving);
    CHECK(walked.facesLeft);
}

TEST_CASE("a walk that never steps across faces right") {
    const core::WorldMap map = WalkMap();
    constexpr std::array<u32, 2> SOUTH{ { TOP_LEFT, MIDDLE_LEFT } };
    const core::Journey journey = OneLeg(map, SOUTH);
    const core::Walked walked = core::WalkedAt(map, journey, HalfwayThrough(journey, 0));
    CHECK(walked.moving);
    CHECK_FALSE(walked.facesLeft);
}

TEST_CASE("a walk runs on from its leg into its stay without a jump") {
    const core::WorldMap map = WalkMap();
    constexpr std::array<u32, 3> OUT{ { TOP_LEFT, TOP_MIDDLE, 2 } };
    const core::Journey visit = core::Visit(map, OUT, core::WALKING, WALKER, HOME, DESTINATION, WALK_START, TWO_DAYS, SEED);
    const i64 arrival = core::EndTick(visit.parts[0]);
    const core::Walked arrived = core::WalkedAt(map, visit, arrival);
    CHECK(arrived.distance == 2.0 * WALK_TILE);

    // The stay's first day drifts from the tile centre to its first day point evenly over the day.
    const auto firstDrift = static_cast<f64>(Distance(map.TileCentre(OUT.back()), core::Where(map, visit, arrival + DAY)));
    REQUIRE(firstDrift > 0.0);
    const core::Walked staying = core::WalkedAt(map, visit, arrival + 1);
    CHECK(staying.distance - arrived.distance == doctest::Approx(firstDrift / static_cast<f64>(DAY)));
    CHECK(staying.moving);
}

TEST_CASE("a walk stays still through a stay standing on its tile centre") {
    const core::WorldMap map = WalkMap();
    constexpr std::array<u32, 3> OUT{ { TOP_LEFT, TOP_MIDDLE, 2 } };
    // A one-day stay runs from the tile centre to the tile centre.
    const core::Journey visit = core::Visit(map, OUT, core::WALKING, WALKER, HOME, DESTINATION, WALK_START, 1, SEED);
    const core::Stay& stay = visit.parts[1].rest;
    bool anyMoving = false;
    f64 farthest = 0.0;
    // Its first tick after arriving, its middle, its last and its end, where the leg home has yet to move.
    for (const i64 tick : { stay.startTick + 1, stay.startTick + (DAY / 2), stay.endTick - 1, stay.endTick }) {
        const core::Walked walked = core::WalkedAt(map, visit, tick);
        anyMoving = anyMoving || walked.moving;
        farthest = std::max(farthest, walked.distance);
    }
    CHECK_FALSE(anyMoving);
    CHECK(farthest == 2.0 * WALK_TILE);
}

TEST_CASE("a walk in a stay faces the way each day drifts across") {
    const core::WorldMap map = WalkMap();
    constexpr std::array<u32, 3> OUT{ { TOP_LEFT, TOP_MIDDLE, 2 } };
    const core::Journey visit = core::Visit(map, OUT, core::WALKING, WALKER, HOME, DESTINATION, WALK_START, FIVE_DAYS, SEED);
    const core::Stay& stay = visit.parts[1].rest;
    std::size_t leftward = 0;
    std::size_t rightward = 0;
    std::size_t wrong = 0;
    for (i64 dayStart = stay.startTick; dayStart < stay.endTick; dayStart += DAY) {
        // Where() stands exactly on each day's point at the day's ends, so its drift across is the day's own.
        const f32 across = core::Where(map, visit, dayStart + DAY).x - core::Where(map, visit, dayStart).x;
        if (across == 0.0F) {
            continue;
        }
        (across < 0.0F ? leftward : rightward) += 1;
        wrong += core::WalkedAt(map, visit, dayStart + (DAY / 2)).facesLeft == (across < 0.0F) ? 0 : 1;
    }
    // The seed's drifts go both ways, so a walk facing either way throughout would fail.
    REQUIRE(leftward > 0);
    REQUIRE(rightward > 0);
    CHECK(wrong == 0);
}

TEST_CASE("a walk keeps its leg's facing through a stay standing on its tile centre") {
    const core::WorldMap map = WalkMap();
    // West from the city on tile 2 (place 1) to the one on tile 0 (place 0), so the facing kept is left, never the default
    // right.
    constexpr std::array<u32, 3> WESTWARD{ { 2, TOP_MIDDLE, TOP_LEFT } };
    constexpr u16 EAST_CITY = 1;
    constexpr u16 WEST_CITY = 0;
    const core::Journey visit = core::Visit(map, WESTWARD, core::WALKING, WALKER, EAST_CITY, WEST_CITY, WALK_START, 1, SEED);
    const core::Stay& stay = visit.parts[1].rest;
    CHECK(core::WalkedAt(map, visit, stay.startTick + (DAY / 2)).facesLeft);
    // Its end, where the walk home east has yet to begin.
    CHECK(core::WalkedAt(map, visit, stay.endTick).facesLeft);
}

TEST_CASE("a walk is the same for the same journey rebuilt") {
    constexpr std::array<u32, 3> OUT{ { TOP_LEFT, TOP_MIDDLE, 2 } };
    const core::WorldMap map = WalkMap();
    const core::Journey visit = core::Visit(map, OUT, core::WALKING, WALKER, HOME, DESTINATION, WALK_START, TWO_DAYS, SEED);
    // A map and journey made again from the same inputs, as a load makes them.
    const core::WorldMap loadedMap = WalkMap();
    const core::Journey loaded =
        core::Visit(loadedMap, OUT, core::WALKING, WALKER, HOME, DESTINATION, WALK_START, TWO_DAYS, SEED);
    // Mid-leg, and mid-stay, where the distance sums drifts of uneven lengths; a paused world asks for one tick again and
    // again.
    for (const i64 tick : { WALK_START + 100, visit.parts[1].rest.startTick + DAY + (DAY / 3) }) {
        const core::Walked first = core::WalkedAt(map, visit, tick);
        const core::Walked again = core::WalkedAt(loadedMap, loaded, tick);
        CHECK(std::bit_cast<u64>(first.distance) == std::bit_cast<u64>(again.distance));
        CHECK(first.moving == again.moving);
        CHECK(first.facesLeft == again.facesLeft);
    }
}

/// Whether two walks are the same to the bit.
bool SameWalk(const core::Walked& lhs, const core::Walked& rhs) {
    return std::bit_cast<u64>(lhs.distance) == std::bit_cast<u64>(rhs.distance) && lhs.moving == rhs.moving &&
           lhs.facesLeft == rhs.facesLeft;
}

/// A visit on WalkMap() from the home city's tile to the destination's: south first, so it sets out looking ahead, then a
/// diagonal, a step west and back east; the way home walks it reversed.
core::Journey WindingVisit(const core::WorldMap& map) {
    constexpr std::array<u32, 6> WINDING{ { TOP_LEFT, MIDDLE_LEFT, 7, 5, CENTRE, 2 } };
    return core::Visit(map, WINDING, core::WALKING, WALKER, HOME, DESTINATION, WALK_START, FIVE_DAYS, SEED);
}

// Named, and its size checked, before a function returns it: MSVC warns at the first call of a function returning a
// std::array that nothing has instantiated yet.
using Samples = std::array<i64, 20>;
static_assert(sizeof(Samples) == 20 * sizeof(i64));

/// Ticks of WindingVisit() in order: before it, each part's ends and the ticks either side, and points within each part.
Samples WindingSamples(const core::Journey& visit) {
    const core::Leg& out = visit.parts[0].leg;
    const core::Stay& stay = visit.parts[1].rest;
    const core::Leg& back = visit.parts[2].leg;
    const i64 homeEnd = core::EndTick(back);
    return { {
        WALK_START - 1,                                                         // before it sets out
        WALK_START,                                                             // setting out
        WALK_START + 1,                                                         // its first tick under way
        HalfwayThrough(visit, 0),                                               // down its first step, not yet across
        out.startTick + i64{ out.arrive[1] },                                   // a step's end
        out.startTick + i64{ out.arrive[1] } + 1,                               // the next step's first tick
        out.startTick + ((i64{ out.arrive[3] } + i64{ out.arrive[4] }) / 2),    // halfway west
        stay.startTick,                                                         // arriving
        stay.startTick + 1,                                                     // the stay's first tick
        stay.startTick + (DAY / 2),                                             // mid-day
        stay.startTick + DAY,                                                   // a day's end
        stay.startTick + (2 * DAY) + (DAY / 3),                                 // into a later day
        stay.endTick - 1,                                                       // the stay's last tick
        stay.endTick,                                                           // setting out home
        back.startTick + 1,                                                     // the way home's first tick
        back.startTick + ((i64{ back.arrive[1] } + i64{ back.arrive[2] }) / 2), // halfway home's second step
        back.startTick + i64{ back.arrive[2] },                                 // a step's end on the way home
        homeEnd - 1,                                                            // its last tick
        homeEnd,                                                                // home
        homeEnd + 1,                                                            // after it
    } };
}

TEST_CASE("a walk from kept tables is the walk worked out afresh, to the bit") {
    const core::WorldMap map = WalkMap();
    const core::Journey visit = WindingVisit(map);
    const core::Stay& stay = visit.parts[1].rest;
    const i64 homeEnd = core::EndTick(visit.parts[2].leg);
    const Samples samples = WindingSamples(visit);
    core::LegWalkCache walks{ map };
    std::size_t differ = 0;
    for (const i64 tick : samples) {
        differ += SameWalk(core::WalkedAt(map, visit, tick, walks), core::WalkedAt(map, visit, tick)) ? 0 : 1;
    }
    CHECK(differ == 0);
    // The samples reach both ways of facing and both standing and walking, so a table read wrongly would show.
    CHECK_FALSE(core::WalkedAt(map, visit, HalfwayThrough(visit, 0), walks).facesLeft);
    CHECK(core::WalkedAt(map, visit, samples[6], walks).facesLeft);
    CHECK(core::WalkedAt(map, visit, stay.startTick + 1, walks).moving);
    CHECK_FALSE(core::WalkedAt(map, visit, homeEnd + 1, walks).moving);
}

TEST_CASE("a walk from kept tables looks ahead for its facing, into its next leg too") {
    const core::WorldMap map = WalkMap();
    constexpr std::array<u32, 3> SOUTH_THEN_WEST{ { TOP_MIDDLE, CENTRE, MIDDLE_LEFT } };
    const core::Journey journey = OneLeg(map, SOUTH_THEN_WEST);
    core::LegWalkCache walks{ map };
    CHECK(core::WalkedAt(map, journey, core::StartTick(journey), walks).facesLeft);
    CHECK(core::WalkedAt(map, journey, HalfwayThrough(journey, 0), walks).facesLeft);

    // A leg straight down, then one west: down the first, it faces the second's way.
    constexpr std::array<u32, 2> SOUTH{ { TOP_MIDDLE, CENTRE } };
    constexpr std::array<u32, 2> WEST{ { CENTRE, MIDDLE_LEFT } };
    core::Journey twoLegs = OneLeg(map, SOUTH);
    const i64 turn = core::EndTick(twoLegs);
    twoLegs.parts.push_back(
        { .stay = false, .leg = core::MakeTileLeg(map, core::WALKING, turn, map.TileCentre(CENTRE), WEST), .rest = {} }
    );
    const i64 down = HalfwayThrough(twoLegs, 0);
    CHECK(core::WalkedAt(map, twoLegs, down, walks).facesLeft);
    CHECK(SameWalk(core::WalkedAt(map, twoLegs, down, walks), core::WalkedAt(map, twoLegs, down)));
}

TEST_CASE("a kept walk is the walk worked out afresh, to the bit, whatever order its ticks come in") {
    const core::WorldMap map = WalkMap();
    const core::Journey visit = WindingVisit(map);
    const Samples samples = WindingSamples(visit);
    core::LegWalkCache walks{ map };
    std::size_t differ = 0;
    // As a clock runs, each part summed once it is behind the tick.
    core::KeptWalk kept;
    for (const i64 tick : samples) {
        differ += SameWalk(core::WalkedAt(map, visit, tick, kept, walks), core::WalkedAt(map, visit, tick)) ? 0 : 1;
    }
    // Then back to the start, each tick before parts it had counted behind it.
    Samples backwards = samples;
    std::ranges::reverse(backwards);
    for (const i64 tick : backwards) {
        differ += SameWalk(core::WalkedAt(map, visit, tick, kept, walks), core::WalkedAt(map, visit, tick)) ? 0 : 1;
    }
    // And each one first, as for a journey first drawn there.
    for (const i64 tick : samples) {
        core::KeptWalk fresh;
        differ += SameWalk(core::WalkedAt(map, visit, tick, fresh, walks), core::WalkedAt(map, visit, tick)) ? 0 : 1;
    }
    CHECK(differ == 0);
}

TEST_CASE("a kept walk looks ahead for its facing, into its next leg too") {
    const core::WorldMap map = WalkMap();
    // A leg straight down, then one west: down the first, it faces the second's way.
    constexpr std::array<u32, 2> SOUTH{ { TOP_MIDDLE, CENTRE } };
    constexpr std::array<u32, 2> WEST{ { CENTRE, MIDDLE_LEFT } };
    core::Journey twoLegs = OneLeg(map, SOUTH);
    const i64 turn = core::EndTick(twoLegs);
    twoLegs.parts.push_back(
        { .stay = false, .leg = core::MakeTileLeg(map, core::WALKING, turn, map.TileCentre(CENTRE), WEST), .rest = {} }
    );
    core::LegWalkCache walks{ map };
    core::KeptWalk kept;
    std::size_t differ = 0;
    for (const i64 tick : { HalfwayThrough(twoLegs, 0), turn, turn + 1, core::EndTick(twoLegs) + 1 }) {
        differ += SameWalk(core::WalkedAt(map, twoLegs, tick, kept, walks), core::WalkedAt(map, twoLegs, tick)) ? 0 : 1;
    }
    CHECK(differ == 0);
    CHECK(core::WalkedAt(map, twoLegs, HalfwayThrough(twoLegs, 0), kept, walks).facesLeft);
}

/// Walk tables made afresh, counting how many are asked for.
class CountedWalks final : public core::LegWalks {
public:
    /// Tables of legs on `map`, which outlives it.
    explicit CountedWalks(const core::WorldMap& map)
      : m_map{ &map } {
    }

    const core::LegWalk& Of(const core::Leg& leg) override {
        ++m_asked;
        m_made.push_back(std::make_unique<const core::LegWalk>(core::MakeLegWalk(*m_map, leg)));
        return *m_made.back();
    }

    /// How many tables it was asked for.
    [[nodiscard]] std::size_t Asked() const {
        return m_asked;
    }

private:
    const core::WorldMap* m_map;
    std::vector<std::unique_ptr<const core::LegWalk>> m_made;
    std::size_t m_asked = 0;
};

TEST_CASE("a kept walk asks for each leg's table once, whatever order its ticks come in") {
    const core::WorldMap map = WalkMap();
    const core::Journey visit = WindingVisit(map);
    const Samples samples = WindingSamples(visit);
    CountedWalks walks{ map };
    core::KeptWalk kept;
    // First walked on its way home: the way out's table once, for how far it went, and the way home's.
    std::ignore = core::WalkedAt(map, visit, samples[15], kept, walks);
    CHECK(walks.Asked() == 2);
    for (const i64 tick : samples) {
        std::ignore = core::WalkedAt(map, visit, tick, kept, walks);
    }
    Samples backwards = samples;
    std::ranges::reverse(backwards);
    for (const i64 tick : backwards) {
        std::ignore = core::WalkedAt(map, visit, tick, kept, walks);
    }
    CHECK(walks.Asked() == 2);
}

TEST_CASE("walk marks made from tiles are a leg's walk table") {
    const core::WorldMap map = WalkMap();
    // East then south, and a loop whose straight-down and straight-up stretches sit between stretches across, so the
    // marks' before and after come from the backward pass.
    constexpr std::array<u32, 3> EAST_THEN_SOUTH{ { TOP_LEFT, TOP_MIDDLE, CENTRE } };
    constexpr std::array<u32, 5> LOOP{ { TOP_LEFT, MIDDLE_LEFT, CENTRE, TOP_MIDDLE, TOP_LEFT } };
    for (const std::span<const u32> tiles : { std::span<const u32>{ EAST_THEN_SOUTH }, std::span<const u32>{ LOOP } }) {
        const core::Journey journey = OneLeg(map, tiles);
        const core::Leg& leg = journey.parts.front().leg;

        const std::vector<core::LegWalkMark> marks = core::MakeWalkMarks(map, leg.origin, leg.tiles);
        const core::LegWalk walk = core::MakeLegWalk(map, leg);
        REQUIRE(marks.size() == walk.marks.size());
        for (std::size_t index = 0; index < marks.size(); ++index) {
            CHECK(marks[index].walked == walk.marks[index].walked);
            CHECK(marks[index].length == walk.marks[index].length);
            CHECK(marks[index].before == walk.marks[index].before);
            CHECK(marks[index].after == walk.marks[index].after);
        }
    }
    // The loop's first stretch goes straight down, yet the way across to come is right.
    const core::Journey loop = OneLeg(map, LOOP);
    const std::vector<core::LegWalkMark> loopMarks =
        core::MakeWalkMarks(map, loop.parts.front().leg.origin, loop.parts.front().leg.tiles);
    CHECK(loopMarks.front().after == core::Across::Right);
    CHECK(loopMarks.back().before == core::Across::Left);
}

} // namespace
