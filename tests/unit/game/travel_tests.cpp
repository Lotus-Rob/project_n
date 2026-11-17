#include "core/archive.hpp"
#include "core/calendar.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/game_data.hpp"
#include "core/jobs.hpp"
#include "core/journey.hpp"
#include "core/paths.hpp"
#include "core/random.hpp"
#include "core/state.hpp"
#include "core/world_map.hpp"
#include "game/npcs.hpp"
#include "game/travel.hpp"

#include <doctest/doctest.h>

namespace {

constexpr f32 TILE = 256.0F;
constexpr u16 ROAD_COST = 4;
constexpr u16 FORD_COST = 30;
constexpr u16 MEADOW_COST = 10;
constexpr u8 MEADOW = 0;
constexpr u32 PEOPLE = 100;
constexpr u32 COLUMNS = 7;
constexpr u32 ROWS = 3;
constexpr u32 TILES = COLUMNS * ROWS;
constexpr u16 HOME = 0;
constexpr u16 DESTINATION = 1;
constexpr u32 ROAD_END = 5;
constexpr u64 STATE_SEED = 7;
// Stands in for the game's journey draws: a stay's seed is keyed on the game's seed, as every draw is.
constexpr u64 GAME_SEED = 0x5eed;
constexpr u32 STAY_PURPOSE = 0x7e57;
constexpr u32 STAY_DAYS = 2;
constexpr i64 DAY = static_cast<i64>(core::TICKS_PER_DAY);
constexpr i64 THIRD_DAY = 3 * DAY;
// A visit along the test map's road, home to the destination, two days there and back: 1,384 ticks, from day 3's start
// to tick 1,924, in day 10.
constexpr u8 DAYS_AWAY = 7;
// Days between the departures of journeys one NPC makes in turn: each is over before the next sets out.
constexpr i64 DAYS_APART = 10;

// The test map, 7 by 3 tiles of meadow: city 0 (the home) at tile 0, a road over tiles 1 to 5 of the top row, and city
// 1 (the destination) at tile 6, its right end. Its profile is the default on_foot at pace 16.
core::WorldMap TestMap() {
    core::WorldMapSource source;
    source.columns = COLUMNS;
    source.rows = ROWS;
    source.tileSize = TILE;
    source.roadCost = ROAD_COST;
    source.fordCost = FORD_COST;
    source.kinds = { { .name = "meadow", .symbol = '.', .cost = MEADOW_COST } };
    source.grid.assign(static_cast<std::size_t>(TILES), MEADOW);
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

// The top row from the home's tile to the destination's.
constexpr std::array<u32, 7> ROAD_ROUTE{ { 0, 1, 2, 3, 4, 5, 6 } };

/// `member`'s visit from the home to the destination and back along the road, leaving on `departTick`, staying `stayDays`.
core::Journey VisitOf(const core::WorldMap& map, core::StableId member, i64 departTick, u32 stayDays = STAY_DAYS) {
    const auto seed =
        static_cast<u32>(core::Random(GAME_SEED, static_cast<u64>(departTick), std::to_underlying(member), STAY_PURPOSE, 0));
    return core::Visit(map, ROAD_ROUTE, member, HOME, DESTINATION, departTick, stayDays, seed);
}

/// A world NPC homed in the home city, on its tile, travelling.
core::StableId Traveller(core::State& state) {
    const core::StableId npc = game::SpawnNpc(state, 0, HOME);
    state[game::WORLD].Entities().get<game::Npc>(state[game::WORLD].Find(npc)).action = game::Action::Travel;
    return npc;
}

game::Action ActionOf(const core::State& state, core::StableId npc) {
    return state[game::WORLD].Entities().get<game::Npc>(state[game::WORLD].Find(npc)).action;
}

/// `travel`'s journeys and journals as Save() writes them.
std::vector<std::byte> Bytes(const game::Travel& travel) {
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    travel.Save(writer);
    return bytes;
}

u64 Hash(const game::Travel& travel) {
    core::Hasher hasher;
    travel.Save(hasher);
    return hasher.Value();
}

/// Whether a fresh table loads `bytes` against `state` and `map`, the reader failing exactly when it doesn't.
bool Loads(const std::vector<std::byte>& bytes, const core::State& state, const core::WorldMap* map) {
    game::Travel travel;
    core::Reader reader{ bytes };
    const bool loaded = travel.Load(reader, state, map);
    CHECK(loaded != reader.Failed());
    return loaded;
}

/// A table holding just `journey`.
std::vector<std::byte> BytesOf(core::Journey journey) {
    game::Travel travel;
    travel.Start(std::move(journey));
    return Bytes(travel);
}

TEST_CASE("an ended journey adds a journal entry and ends") {
    const core::WorldMap map = TestMap();
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId npc = Traveller(state);
    const core::Journey journey = VisitOf(map, npc, THIRD_DAY);
    game::Travel travel;
    travel.Start(journey);
    CHECK(travel.Travelling(npc));

    travel.Finish(core::EndTick(journey), state);
    CHECK(travel.Journeys().empty());
    CHECK_FALSE(travel.Travelling(npc));
    CHECK(ActionOf(state, npc) == game::Action::Rest);
    REQUIRE(travel.JournalOf(npc).size() == 1);
    const game::JournalEntry& entry = travel.JournalOf(npc).front();
    CHECK(entry.destination == DESTINATION);
    CHECK(entry.departureDay == 3);
    CHECK(entry.daysAway == DAYS_AWAY);
    CHECK(entry.profile == core::WALKING);
}

TEST_CASE("a journey that left before tick 0 counts its departure day back from it") {
    constexpr i64 BEFORE_ZERO = -200; // in day -2, which runs from tick -360 to -181
    const core::WorldMap map = TestMap();
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId npc = Traveller(state);
    const core::Journey journey = VisitOf(map, npc, BEFORE_ZERO);
    game::Travel travel;
    travel.Start(journey);
    travel.Finish(core::EndTick(journey), state);
    REQUIRE(travel.JournalOf(npc).size() == 1);
    CHECK(travel.JournalOf(npc).front().departureDay == -2);
    constexpr u8 DAYS_FROM_BEFORE_ZERO = 8; // ends on tick 1,184, in day 6
    CHECK(travel.JournalOf(npc).front().daysAway == DAYS_FROM_BEFORE_ZERO);
}

/// Sends `npc` on `count` visits in turn, the first leaving on day 0, the next ones DAYS_APART days after each other, and
/// ends each.
void Journeys(game::Travel& travel, core::State& state, const core::WorldMap& map, core::StableId npc, i64 count) {
    for (i64 visit = 0; visit < count; ++visit) {
        const core::Journey journey = VisitOf(map, npc, visit * DAYS_APART * DAY);
        travel.Start(journey);
        travel.Finish(core::EndTick(journey), state);
    }
}

TEST_CASE("the eleventh journey drops the oldest") {
    const core::WorldMap map = TestMap();
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId npc = Traveller(state);
    game::Travel travel;
    Journeys(travel, state, map, npc, game::JOURNAL_LENGTH);
    CHECK(travel.JournalOf(npc).size() == game::JOURNAL_LENGTH);
    CHECK(travel.JournalOf(npc).front().departureDay == 0);

    Journeys(travel, state, map, npc, game::JOURNAL_LENGTH + 1);
    REQUIRE(travel.JournalOf(npc).size() == game::JOURNAL_LENGTH);
    CHECK(travel.JournalOf(npc).front().departureDay == DAYS_APART); // the second of the last run's eleven
    CHECK(travel.JournalOf(npc).back().departureDay == game::JOURNAL_LENGTH * DAYS_APART);
}

TEST_CASE("an NPC the player has met keeps fifty") {
    const core::WorldMap map = TestMap();
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId met = Traveller(state);
    state[game::WORLD].Entities().emplace<game::Met>(state[game::WORLD].Find(met));
    game::Travel travel;
    Journeys(travel, state, map, met, game::MET_JOURNAL_LENGTH + 1);
    REQUIRE(travel.JournalOf(met).size() == game::MET_JOURNAL_LENGTH);
    CHECK(travel.JournalOf(met).front().departureDay == DAYS_APART);
    CHECK(travel.JournalOf(met).back().departureDay == game::MET_JOURNAL_LENGTH * DAYS_APART);
}

TEST_CASE("a journey not yet over stays") {
    const core::WorldMap map = TestMap();
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId first = Traveller(state);
    const core::StableId later = Traveller(state);
    const core::Journey early = VisitOf(map, first, 0);
    game::Travel travel;
    travel.Start(early);
    travel.Start(VisitOf(map, later, DAY));

    travel.Finish(core::EndTick(early) - 1, state);
    CHECK(travel.Journeys().size() == 2);
    CHECK(travel.JournalOf(first).empty());
    CHECK(ActionOf(state, first) == game::Action::Travel);

    travel.Finish(core::EndTick(early), state);
    REQUIRE(travel.Journeys().size() == 1);
    CHECK(travel.Journeys().front().members.front() == later);
    CHECK(travel.Travelling(later));
    CHECK_FALSE(travel.Travelling(first));
    CHECK(travel.JournalOf(later).empty());
    CHECK(ActionOf(state, later) == game::Action::Travel);
}

TEST_CASE("journeys are kept in their first members' order") {
    const core::WorldMap map = TestMap();
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId lower = Traveller(state);
    const core::StableId higher = Traveller(state);
    game::Travel travel;
    travel.Start(VisitOf(map, higher, 0));
    travel.Start(VisitOf(map, lower, DAY));
    REQUIRE(travel.Journeys().size() == 2);
    CHECK(travel.Journeys()[0].members.front() == lower);
    CHECK(travel.Journeys()[1].members.front() == higher);
}

TEST_CASE("a forgotten NPC leaves its journey and its journal") {
    const core::WorldMap map = TestMap();
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId alone = Traveller(state);
    const core::StableId leader = Traveller(state);
    const core::StableId middle = Traveller(state);
    const core::StableId between = Traveller(state);
    const core::StableId last = Traveller(state);
    game::Travel travel;
    Journeys(travel, state, map, alone, 1);
    travel.Start(VisitOf(map, alone, DAYS_APART * DAY));
    core::Journey group = VisitOf(map, leader, 0);
    group.members.push_back(middle);
    group.members.push_back(last);
    travel.Start(group);
    travel.Start(VisitOf(map, between, DAY));

    travel.Forget(alone);
    CHECK(travel.JournalOf(alone).empty());
    CHECK_FALSE(travel.Travelling(alone));
    REQUIRE(travel.Journeys().size() == 2);

    // A member who doesn't lead leaves the journey as it is.
    travel.Forget(middle);
    CHECK_FALSE(travel.Travelling(middle));
    const bool middleLeft = travel.Journeys().front().members == std::vector{ leader, last };
    CHECK(middleLeft);

    // Without its leader a journey goes on, led by the next member and in that member's place among the journeys.
    travel.Forget(leader);
    CHECK_FALSE(travel.Travelling(leader));
    REQUIRE(travel.Journeys().size() == 2);
    CHECK(travel.Journeys()[0].members.front() == between);
    const bool lastLeads = travel.Journeys()[1].members == std::vector{ last };
    CHECK(lastLeads);
    CHECK(travel.Travelling(last));

    // A journey left without members is dropped.
    travel.Forget(last);
    REQUIRE(travel.Journeys().size() == 1);
    CHECK(travel.Journeys().front().members.front() == between);
}

TEST_CASE("an ended journey of two writes both members' journals") {
    const core::WorldMap map = TestMap();
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId leader = Traveller(state);
    const core::StableId follower = Traveller(state);
    core::Journey pair = VisitOf(map, leader, THIRD_DAY);
    pair.members.push_back(follower);
    game::Travel travel;
    travel.Start(pair);
    travel.Finish(core::EndTick(pair), state);
    CHECK(travel.Journeys().empty());
    for (const core::StableId member : { leader, follower }) {
        CAPTURE(std::to_underlying(member));
        CHECK_FALSE(travel.Travelling(member));
        CHECK(ActionOf(state, member) == game::Action::Rest);
        REQUIRE(travel.JournalOf(member).size() == 1);
        CHECK(travel.JournalOf(member).front().departureDay == 3);
        CHECK(travel.JournalOf(member).front().daysAway == DAYS_AWAY);
    }
}

TEST_CASE("travel and the journal are saved, loaded and hashed") {
    const core::WorldMap map = TestMap();
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId home = Traveller(state);
    const core::StableId away = Traveller(state);
    game::Travel travel;
    Journeys(travel, state, map, home, 2);
    travel.Start(VisitOf(map, away, DAY));
    const std::vector<std::byte> bytes = Bytes(travel);

    game::Travel loaded;
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, state, &map));
    CHECK(reader.Remaining() == 0);
    const bool sameBytes = Bytes(loaded) == bytes;
    CHECK(sameBytes);
    CHECK(Hash(loaded) == Hash(travel));
    CHECK(loaded.JournalOf(home).size() == 2);
    CHECK(loaded.Travelling(away));
    CHECK_FALSE(loaded.Travelling(home));

    // The journal is hashed: one more entry changes the hash.
    Journeys(loaded, state, map, home, 1);
    CHECK(Hash(loaded) != Hash(travel));

    // Without the world map, only an empty table loads, and only where nobody is travelling.
    CHECK_FALSE(Loads(bytes, state, nullptr));
    const core::State resting{ game::DOMAINS, STATE_SEED };
    CHECK(Loads(Bytes(game::Travel{}), resting, nullptr));
}

TEST_CASE("a save naming a traveller who isn't there is refused") {
    const core::WorldMap map = TestMap();
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId npc = Traveller(state);
    const std::vector<std::byte> bytes = BytesOf(VisitOf(map, npc, 0));
    CHECK(Loads(bytes, state, &map));

    const core::State empty{ game::DOMAINS, STATE_SEED };
    game::Travel kept;
    kept.Start(VisitOf(map, npc, 0));
    const std::vector<std::byte> before = Bytes(kept);
    core::Reader reader{ bytes };
    CHECK_FALSE(kept.Load(reader, empty, &map));
    const bool unchanged = Bytes(kept) == before;
    CHECK(unchanged);

    // A traveller in the active tier isn't a world NPC.
    core::State activated{ game::DOMAINS, STATE_SEED };
    const core::StableId homeless = game::SpawnNpc(activated, 0);
    game::Activate(activated, homeless);
    const bool homelessLoads = Loads(BytesOf(VisitOf(map, homeless, 0)), activated, &map);
    CHECK_FALSE(homelessLoads);
}

TEST_CASE("a travel table with a city off the map, a tile off it, or arrive ticks not rising is refused") {
    const core::WorldMap map = TestMap();
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId npc = Traveller(state);
    const core::Journey sound = VisitOf(map, npc, 0);
    REQUIRE(sound.parts.size() == 3);
    REQUIRE_FALSE(sound.parts[0].stay);
    const auto refused = [&](auto change) {
        core::Journey journey = sound;
        change(journey);
        return !Loads(BytesOf(std::move(journey)), state, &map);
    };
    const auto pastPlaces = static_cast<u16>(map.Places().size());
    CHECK(refused([&](core::Journey& journey) { journey.destination = pastPlaces; }));
    CHECK(refused([&](core::Journey& journey) { journey.home = pastPlaces; }));
    CHECK(refused([&](core::Journey& journey) { journey.parts[1].rest.city = pastPlaces; }));
    CHECK(refused([](core::Journey& journey) { journey.parts[0].leg.tiles.back() = TILES; }));
    CHECK(refused([](core::Journey& journey) { journey.parts[0].leg.arrive[2] = journey.parts[0].leg.arrive[1]; }));
    CHECK(refused([](core::Journey& journey) { journey.parts[0].leg.arrive.pop_back(); }));  // one short of the tiles
    CHECK(refused([](core::Journey& journey) { journey.parts[0].leg.arrive.front() = 1; })); // not from 0
    CHECK(refused([](core::Journey& journey) { journey.parts[1].rest.startTick += 1; }));    // not where the leg ends
    CHECK(refused([](core::Journey& journey) { journey.parts[0].leg.origin.x = std::numeric_limits<f32>::quiet_NaN(); }));
    CHECK(refused([](core::Journey& journey) { journey.parts[0].leg.profile = 1; })); // the map has one profile
    CHECK(refused([](core::Journey& journey) { journey.members.push_back(journey.members.front()); })); // twice
    CHECK(refused([](core::Journey& journey) { journey.parts[0].leg.shape = std::bit_cast<core::LegShape>(u8{ 1 }); }));

    // Two journeys of one NPC, then no journals, as Save() lays a table out.
    constexpr u64 NO_JOURNALS = 0;
    std::vector<std::byte> twice;
    core::Writer writer{ twice };
    writer(std::vector{ sound, sound }, NO_JOURNALS);
    CHECK_FALSE(Loads(twice, state, &map));
}

TEST_CASE("a stay ending before it starts, longer than a leg can be, or within a day of the last tick is refused") {
    const core::WorldMap map = TestMap();
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId npc = Traveller(state);
    // A journey of one stay in the destination, from `start` to `end`.
    const auto loads = [&](i64 start, i64 end) {
        core::Journey journey{ .members = { npc }, .home = HOME, .destination = DESTINATION, .parts = {} };
        journey.parts.push_back(
            { .stay = true, .leg = {}, .rest = { .city = DESTINATION, .startTick = start, .endTick = end, .seed = 0 } }
        );
        return Loads(BytesOf(std::move(journey)), state, &map);
    };
    constexpr i64 LONGEST = std::numeric_limits<u32>::max();
    constexpr i64 LATEST_END = std::numeric_limits<i64>::max() - DAY;
    CHECK(loads(DAY, DAY)); // no ticks long
    CHECK_FALSE(loads(DAY, DAY - 1));
    CHECK(loads(0, LONGEST));
    CHECK_FALSE(loads(0, LONGEST + 1));
    CHECK_FALSE(loads(std::numeric_limits<i64>::min(), 0)); // the earliest start: far too long
    CHECK(loads(LATEST_END - DAY, LATEST_END));
    CHECK_FALSE(loads(LATEST_END - DAY + 1, LATEST_END + 1));
}

TEST_CASE("a journal out of order, empty, of an NPC not there, too long, or naming what the map lacks is refused") {
    const core::WorldMap map = TestMap();
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId first = game::SpawnNpc(state, 0, HOME);
    const core::StableId second = game::SpawnNpc(state, 0, HOME);
    const core::StableId met = game::SpawnNpc(state, 0, HOME);
    state[game::WORLD].Entities().emplace<game::Met>(state[game::WORLD].Find(met));
    constexpr core::StableId NOBODY{ 999 };
    using Entries = std::vector<game::JournalEntry>;
    // A table of no journeys and `table`'s journals, as Save() lays one out.
    const auto loads = [&](const std::vector<std::pair<core::StableId, Entries>>& table) {
        constexpr u64 NO_JOURNEYS = 0;
        std::vector<std::byte> bytes;
        core::Writer writer{ bytes };
        writer(NO_JOURNEYS, static_cast<u64>(table.size()));
        for (const auto& [npc, entries] : table) {
            writer(npc, entries);
        }
        return Loads(bytes, state, &map);
    };
    const game::JournalEntry entry{ .destination = DESTINATION, .departureDay = 0, .daysAway = 1, .profile = core::WALKING };
    const Entries one{ entry };
    const auto pastPlaces = static_cast<u16>(map.Places().size());
    CHECK(loads({ { first, one }, { second, one } }));
    CHECK_FALSE(loads({ { second, one }, { first, one } }));
    CHECK_FALSE(loads({ { first, one }, { first, one } })); // repeated
    CHECK_FALSE(loads({ { first, {} } }));
    CHECK_FALSE(loads({ { NOBODY, one } }));
    CHECK(loads({ { first, Entries(game::JOURNAL_LENGTH, entry) } }));
    CHECK_FALSE(loads({ { first, Entries(game::JOURNAL_LENGTH + 1, entry) } }));
    CHECK(loads({ { met, Entries(game::MET_JOURNAL_LENGTH, entry) } }));
    CHECK_FALSE(loads({ { met, Entries(game::MET_JOURNAL_LENGTH + 1, entry) } }));
    CHECK_FALSE(
        loads({ { first, { { .destination = pastPlaces, .departureDay = 0, .daysAway = 1, .profile = core::WALKING } } } })
    );
    CHECK_FALSE(loads({ { first, { { .destination = DESTINATION, .departureDay = 0, .daysAway = 1, .profile = 1 } } } }));
}

TEST_CASE("a loaded journey's members travel, and nobody else does") {
    const core::WorldMap map = TestMap();
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId npc = Traveller(state);
    const std::vector<std::byte> bytes = BytesOf(VisitOf(map, npc, 0));
    REQUIRE(Loads(bytes, state, &map));
    game::Npc& person = state[game::WORLD].Entities().get<game::Npc>(state[game::WORLD].Find(npc));

    // A member resting would take a month's action while away.
    person.action = game::Action::Rest;
    CHECK_FALSE(Loads(bytes, state, &map));

    // Someone travelling on no journey would never come back to the month's actions.
    person.action = game::Action::Travel;
    static_cast<void>(Traveller(state));
    CHECK_FALSE(Loads(bytes, state, &map));
    CHECK_FALSE(Loads(Bytes(game::Travel{}), state, nullptr));
}

// The visit map, 21 by 3 tiles of meadow under a road along the top row: the home city at its left end (tile 0), the near
// city 5 road tiles along (a walk costing 20 from the home's tile) and the far city at the row's right end (costing 80);
// the isle city stands in the bottom right corner behind shallows on both its sides and its corner, which a boat could
// cross but walking can't, so nobody walks to or from it.
constexpr u32 VISIT_COLUMNS = 21;
constexpr u32 VISIT_ROWS = 3;
constexpr u8 SHALLOWS = 1;
constexpr u16 SHALLOWS_COST = 30;
constexpr u16 NEAR = 1;
constexpr u16 FAR = 2;
constexpr u16 ISLE = 3;
constexpr u32 NEAR_COLUMN = 5;
constexpr u32 NEAR_COST = 20;
constexpr u32 FAR_COST = 80;
constexpr u32 NEAR_PEOPLE = 100;
constexpr u32 FAR_PEOPLE = 400;
constexpr u32 ISLE_PEOPLE = 5'000;
constexpr u32 ISLE_TILE = (VISIT_COLUMNS * VISIT_ROWS) - 1;
constexpr std::array<u16, core::VISITOR_KINDS> EVERYONE{ { 1, 1 } };
// Draws by core::VisitorKind: cultivators, then traders.
constexpr std::array<u16, core::VISITOR_KINDS> SECT_DRAWS{ { 6, 0 } };
constexpr u8 STAY_MIN = 2;
constexpr u8 STAY_MAX = 5;
// Everyone sets out, and the reach covers the whole road whatever the power.
constexpr core::TravelRules ALL_GO{ .sharePerMille = 1'000,
                                    .stayMinDays = STAY_MIN,
                                    .stayMaxDays = STAY_MAX,
                                    .reachBase = 1'000,
                                    .reachPerPower = 0,
                                    .cultivatorPower = 5.0F };
// A mortal's reach ends between the near city and the far one; each whole point of power adds 10.
constexpr core::TravelRules SHORT_REACH{ .sharePerMille = 1'000,
                                         .stayMinDays = STAY_MIN,
                                         .stayMaxDays = STAY_MAX,
                                         .reachBase = 50,
                                         .reachPerPower = 10,
                                         .cultivatorPower = 5.0F };
constexpr f32 MORTAL = 0.0F;
// Either side of the reach's next whole point, and of the cultivator's threshold.
constexpr f32 JUST_BELOW_FOUR = 3.9F;
constexpr f32 FOUR = 4.0F;
constexpr f32 STRONGEST_TRADER = 4.9F;
constexpr f32 WEAKEST_CULTIVATOR = 5.0F;
constexpr f32 CULTIVATOR = 10.0F;
constexpr f32 STRONG = 100.0F;
constexpr u32 VISITORS = 200;
constexpr u64 VISIT_COUNTER = 1;
constexpr i64 MONTH = static_cast<i64>(core::TICKS_PER_MONTH);

/// A city of the visit map.
core::Place City(const std::string& key, core::TilePoint tile, u32 people, std::array<u16, core::VISITOR_KINDS> draws) {
    return { .key = key,
             .kind = core::PlaceKind::City,
             .sort = "",
             .tile = tile,
             .nameKey = key,
             .name = {},
             .people = people,
             .start = key == "home",
             .draws = draws };
}

/// The visit map under `rules`, its far city drawing visitors by `farDraws`.
std::shared_ptr<const core::WorldMap> VisitMap(
    const core::TravelRules& rules = ALL_GO,
    std::array<u16, core::VISITOR_KINDS> farDraws = EVERYONE
) {
    core::WorldMapSource source;
    source.columns = VISIT_COLUMNS;
    source.rows = VISIT_ROWS;
    source.tileSize = TILE;
    source.roadCost = ROAD_COST;
    source.fordCost = FORD_COST;
    source.kinds = { { .name = "meadow", .symbol = '.', .cost = MEADOW_COST },
                     { .name = "shallows", .symbol = '-', .cost = SHALLOWS_COST } };
    source.grid.assign(static_cast<std::size_t>(VISIT_COLUMNS) * VISIT_ROWS, MEADOW);
    for (const u32 water : { (VISIT_COLUMNS * 2) - 2, (VISIT_COLUMNS * 2) - 1, ISLE_TILE - 1 }) {
        source.grid[water] = SHALLOWS;
    }
    source.profiles = { { .key = "on_foot",
                          .ticksPerCost = core::DEFAULT_TICKS_PER_COST,
                          .kindCosts = { MEADOW_COST, core::IMPASSABLE_COST },
                          .features = {} } };
    source.roads = { { { .column = 0, .row = 0 }, { .column = VISIT_COLUMNS - 1, .row = 0 } } };
    source.places = {
        City("home", { .column = 0, .row = 0 }, PEOPLE, EVERYONE),
        City("near", { .column = NEAR_COLUMN, .row = 0 }, NEAR_PEOPLE, EVERYONE),
        City("far", { .column = VISIT_COLUMNS - 1, .row = 0 }, FAR_PEOPLE, farDraws),
        City("isle", { .column = VISIT_COLUMNS - 1, .row = VISIT_ROWS - 1 }, ISLE_PEOPLE, EVERYONE),
    };
    source.travel = rules;
    auto built = core::WorldMap::Build(std::move(source));
    INFO((built ? std::string{} : built.error().message));
    REQUIRE(built.has_value());
    return std::make_shared<const core::WorldMap>(*std::move(built));
}

/// `count` world NPCs of `home` standing on `tile`, each with `power`.
std::vector<core::StableId> Residents(core::State& state, u32 count, u32 tile, u16 home, f32 power) {
    std::vector<core::StableId> npcs;
    npcs.reserve(count);
    for (u32 npc = 0; npc < count; ++npc) {
        npcs.push_back(game::SpawnNpc(state, tile, home, power));
    }
    return npcs;
}

/// Whether `lhs` and `rhs` have the same bits, as determinism means.
bool SamePoint(core::Vec2 lhs, core::Vec2 rhs) {
    return std::bit_cast<u32>(lhs.x) == std::bit_cast<u32>(rhs.x) && std::bit_cast<u32>(lhs.y) == std::bit_cast<u32>(rhs.y);
}

/// The month of visits from `monthStart`, on one worker.
void VisitMonth(game::Travel& travel, core::State& state, const core::WorldMap& map, core::Paths& paths, i64 monthStart) {
    core::Jobs jobs{ 1 };
    travel.Month(state, map, paths, jobs, GAME_SEED, VISIT_COUNTER, monthStart);
}

/// How many of `travel`'s journeys go to `city`, counting only those whose first member is one of `members` (sorted).
std::size_t JourneysTo(const game::Travel& travel, u16 city, std::span<const core::StableId> members) {
    return static_cast<std::size_t>(std::ranges::count_if(travel.Journeys(), [&](const core::Journey& journey) {
        return journey.destination == city && std::ranges::binary_search(members, journey.members.front());
    }));
}

TEST_CASE("bigger and nearer cities draw more visitors") {
    const std::shared_ptr<const core::WorldMap> map = VisitMap();
    // Pull is the people times 1,000,000 over the cost squared, reach (1,000 - cost) * 1,000 / 1,000, every draw 1.
    CHECK(game::DestinationWeight(*map, ALL_GO, MORTAL, NEAR_COST, NEAR) == 245'000'000);    // 250,000 * 980
    CHECK(game::DestinationWeight(*map, ALL_GO, MORTAL, 2 * NEAR_COST, NEAR) == 60'000'000); // 62,500 * 960
    CHECK(game::DestinationWeight(*map, ALL_GO, MORTAL, NEAR_COST, FAR) == 980'000'000);     // 1,000,000 * 980
    CHECK(game::DestinationWeight(*map, ALL_GO, MORTAL, FAR_COST, FAR) == 57'500'000);       // 62,500 * 920
    CHECK(game::DestinationWeight(*map, ALL_GO, MORTAL, 0, NEAR) == 100'000'000'000);        // a cost of 0 counts as 1

    core::State state{ game::DOMAINS, STATE_SEED };
    const std::vector<core::StableId> visitors = Residents(state, VISITORS, 0, HOME, MORTAL);
    core::Paths paths{ map };
    game::Travel travel;
    VisitMonth(travel, state, *map, paths, 0);
    CHECK(travel.Journeys().size() == VISITORS);
    const std::size_t near = JourneysTo(travel, NEAR, visitors);
    const std::size_t far = JourneysTo(travel, FAR, visitors);
    CAPTURE(near);
    CAPTURE(far);
    CHECK(near > far * 2); // its weight is over four times the far city's
    CHECK(far > 0);
}

TEST_CASE("the strong reach far cities that mortals don't") {
    const std::shared_ptr<const core::WorldMap> map = VisitMap(SHORT_REACH);
    // The reach is 50 plus 10 for each whole point of power, and the far city costs 80.
    CHECK(game::DestinationWeight(*map, SHORT_REACH, MORTAL, NEAR_COST, NEAR) == 150'000'000); // 250,000 * 600
    CHECK(game::DestinationWeight(*map, SHORT_REACH, MORTAL, FAR_COST, FAR) == 0);
    CHECK(game::DestinationWeight(*map, SHORT_REACH, JUST_BELOW_FOUR, FAR_COST, FAR) == 0); // reach 80: none left at cost 80
    CHECK(game::DestinationWeight(*map, SHORT_REACH, FOUR, FAR_COST, FAR) == 6'937'500);    // 62,500 * 111
    CHECK(game::DestinationWeight(*map, SHORT_REACH, STRONG, FAR_COST, FAR) == 57'687'500); // 62,500 * 923

    core::State state{ game::DOMAINS, STATE_SEED };
    const std::vector<core::StableId> mortals = Residents(state, VISITORS / 2, 0, HOME, MORTAL);
    const std::vector<core::StableId> strong = Residents(state, VISITORS / 2, 0, HOME, STRONG);
    core::Paths paths{ map };
    game::Travel travel;
    VisitMonth(travel, state, *map, paths, 0);
    const std::size_t mortalsNear = JourneysTo(travel, NEAR, mortals);
    const std::size_t mortalsFar = JourneysTo(travel, FAR, mortals);
    const std::size_t strongFar = JourneysTo(travel, FAR, strong);
    CHECK(mortalsNear == mortals.size());
    CHECK(mortalsFar == 0);
    CHECK(strongFar > 0);
}

TEST_CASE("a city's draw follows the visitor's kind") {
    const std::shared_ptr<const core::WorldMap> map = VisitMap(ALL_GO, SECT_DRAWS);
    CHECK(game::DestinationWeight(*map, ALL_GO, STRONGEST_TRADER, NEAR_COST, FAR) == 0); // a trader
    CHECK(
        game::DestinationWeight(*map, ALL_GO, WEAKEST_CULTIVATOR, NEAR_COST, FAR) == 5'880'000'000
    ); // a cultivator: 6 * 980,000,000

    core::State state{ game::DOMAINS, STATE_SEED };
    const std::vector<core::StableId> traders = Residents(state, VISITORS / 2, 0, HOME, MORTAL);
    const std::vector<core::StableId> cultivators = Residents(state, VISITORS / 2, 0, HOME, CULTIVATOR);
    core::Paths paths{ map };
    game::Travel travel;
    VisitMonth(travel, state, *map, paths, 0);
    const std::size_t tradersFar = JourneysTo(travel, FAR, traders);
    const std::size_t cultivatorsFar = JourneysTo(travel, FAR, cultivators);
    CHECK(tradersFar == 0);
    CHECK(cultivatorsFar > 0);
}

TEST_CASE("nobody sets out twice at once") {
    const std::shared_ptr<const core::WorldMap> map = VisitMap();
    core::State state{ game::DOMAINS, STATE_SEED };
    const std::vector<core::StableId> visitors = Residents(state, VISITORS, 0, HOME, MORTAL);
    core::Paths paths{ map };
    game::Travel travel;
    VisitMonth(travel, state, *map, paths, 0);
    REQUIRE(travel.Journeys().size() == VISITORS);
    // Without the journeys ended, everyone is still away at the next month, though some are back by then.
    VisitMonth(travel, state, *map, paths, MONTH);
    CHECK(travel.Journeys().size() == VISITORS);
    std::vector<core::StableId> members;
    for (const core::Journey& journey : travel.Journeys()) {
        members.insert(members.end(), journey.members.begin(), journey.members.end());
    }
    std::ranges::sort(members);
    CHECK(std::ranges::adjacent_find(members) == members.end());
    const bool allTravel = std::ranges::all_of(visitors, [&](core::StableId npc) {
        return ActionOf(state, npc) == game::Action::Travel && travel.Travelling(npc);
    });
    CHECK(allTravel);
}

TEST_CASE("every visitor comes home") {
    const std::shared_ptr<const core::WorldMap> map = VisitMap();
    core::State state{ game::DOMAINS, STATE_SEED };
    // On the home city's tile, and on tiles round it off the road.
    constexpr std::array<u32, 4> TILES_LIVED_ON{ { 0, VISIT_COLUMNS + 1, 2 * VISIT_COLUMNS, (2 * VISIT_COLUMNS) + 3 } };
    constexpr auto PER_TILE = static_cast<u32>(VISITORS / TILES_LIVED_ON.size());
    for (const u32 tile : TILES_LIVED_ON) {
        static_cast<void>(Residents(state, PER_TILE, tile, HOME, MORTAL));
    }
    core::Paths paths{ map };
    game::Travel travel;
    VisitMonth(travel, state, *map, paths, 0);
    REQUIRE(travel.Journeys().size() == VISITORS);
    bool home = true;
    for (const core::Journey& journey : travel.Journeys()) {
        const u32 tile = state[game::WORLD].Entities().get<game::Npc>(state[game::WORLD].Find(journey.members.front())).cell;
        const core::Vec2 centre = map->TileCentre(tile);
        home = home && journey.home == HOME && journey.parts.back().leg.tiles.back() == tile &&
               SamePoint(core::Where(*map, journey, core::StartTick(journey)), centre) &&
               SamePoint(core::Where(*map, journey, core::EndTick(journey)), centre);
    }
    CHECK(home);
}

TEST_CASE("a visit across a month step continues") {
    const std::shared_ptr<const core::WorldMap> map = VisitMap();
    core::State state{ game::DOMAINS, STATE_SEED };
    static_cast<void>(Residents(state, VISITORS, 0, HOME, MORTAL));
    core::Paths paths{ map };
    game::Travel travel;
    VisitMonth(travel, state, *map, paths, 0);
    std::vector<core::Journey> over;
    std::ranges::copy_if(travel.Journeys(), std::back_inserter(over), [](const core::Journey& journey) {
        return core::EndTick(journey) > MONTH;
    });
    REQUIRE_FALSE(over.empty());

    travel.Finish(MONTH, state);
    VisitMonth(travel, state, *map, paths, MONTH);
    bool continued = true;
    for (const core::Journey& journey : over) {
        const core::StableId npc = journey.members.front();
        const auto led =
            std::ranges::count(travel.Journeys(), npc, [](const core::Journey& kept) { return kept.members.front(); });
        const auto kept =
            std::ranges::find(travel.Journeys(), npc, [](const core::Journey& found) { return found.members.front(); });
        continued = continued && led == 1 && core::StartTick(*kept) == core::StartTick(journey) &&
                    core::EndTick(*kept) == core::EndTick(journey) && ActionOf(state, npc) == game::Action::Travel;
    }
    CHECK(continued);
}

TEST_CASE("departures spread over the month") {
    const std::shared_ptr<const core::WorldMap> map = VisitMap();
    core::State state{ game::DOMAINS, STATE_SEED };
    static_cast<void>(Residents(state, VISITORS, 0, HOME, MORTAL));
    core::Paths paths{ map };
    game::Travel travel;
    VisitMonth(travel, state, *map, paths, MONTH);
    REQUIRE(travel.Journeys().size() == VISITORS);
    const auto [first, last] = std::ranges::minmax_element(travel.Journeys(), {}, [](const core::Journey& journey) {
        return core::StartTick(journey);
    });
    const i64 earliest = core::StartTick(*first);
    const i64 latest = core::StartTick(*last);
    // The month from tick 5,400 runs from day 30 to day 59.
    CHECK(earliest / DAY == 30);
    CHECK(latest / DAY == 59);
    CHECK(latest < 2 * MONTH);
}

TEST_CASE("an unreachable city is never chosen") {
    const std::shared_ptr<const core::WorldMap> map = VisitMap();
    core::Paths paths{ map };
    REQUIRE(paths.To(ISLE, core::WALKING).CostFrom(0) == core::UNREACHABLE);
    CHECK(game::DestinationWeight(*map, ALL_GO, STRONG, core::UNREACHABLE, ISLE) == 0);

    core::State state{ game::DOMAINS, STATE_SEED };
    const std::vector<core::StableId> visitors = Residents(state, VISITORS, 0, HOME, STRONG);
    const std::vector<core::StableId> islanders = Residents(state, VISITORS / 4, ISLE_TILE, ISLE, STRONG);
    const game::Npc islander = state[game::WORLD].Entities().get<game::Npc>(state[game::WORLD].Find(islanders.front()));
    constexpr u64 ANY_DRAW = 0x1234'5678'9abc'def0ULL;
    CHECK_FALSE(game::ChooseDestination(*map, ALL_GO, paths, islander, ANY_DRAW).has_value());

    game::Travel travel;
    VisitMonth(travel, state, *map, paths, 0);
    CHECK(travel.Journeys().size() == VISITORS); // the islanders, who reach nowhere, stay
    const std::size_t toTheIsle = JourneysTo(travel, ISLE, visitors);
    CHECK(toTheIsle == 0);
    CHECK_FALSE(travel.Travelling(islanders.front()));
}

TEST_CASE("the draw falls on the first city its share reaches") {
    const std::shared_ptr<const core::WorldMap> map = VisitMap();
    core::Paths paths{ map };
    const game::Npc resident{ .cell = 0, .home = HOME, .action = game::Action::Rest, .power = MORTAL };
    // Near weighs 245 million, far 57.5 million and the isle nothing: the lowest draw picks near, the highest far.
    CHECK(game::ChooseDestination(*map, ALL_GO, paths, resident, 0) == NEAR);
    CHECK(game::ChooseDestination(*map, ALL_GO, paths, resident, std::numeric_limits<u64>::max()) == FAR);
}

TEST_CASE("a visit to the next tile over goes there and comes back") {
    core::WorldMapSource source;
    source.columns = 2;
    source.rows = 1;
    source.tileSize = TILE;
    source.kinds = { { .name = "meadow", .symbol = '.', .cost = MEADOW_COST } };
    source.grid.assign(2, MEADOW);
    source.places = { City("home", { .column = 0, .row = 0 }, PEOPLE, EVERYONE),
                      City("next", { .column = 1, .row = 0 }, PEOPLE, EVERYONE) };
    source.travel = ALL_GO;
    auto built = core::WorldMap::Build(std::move(source));
    REQUIRE(built.has_value());
    const auto map = std::make_shared<const core::WorldMap>(*std::move(built));
    core::State state{ game::DOMAINS, STATE_SEED };
    static_cast<void>(Residents(state, 1, 0, HOME, MORTAL));
    core::Paths paths{ map };
    game::Travel travel;
    VisitMonth(travel, state, *map, paths, 0);
    REQUIRE(travel.Journeys().size() == 1);
    const core::Journey& journey = travel.Journeys().front();
    REQUIRE(journey.parts.size() == 3);
    const bool there = journey.parts[0].leg.tiles == std::vector<u32>{ 0, 1 };
    const bool back = journey.parts[2].leg.tiles == std::vector<u32>{ 1, 0 };
    CHECK(there);
    CHECK(back);
    // One meadow step at the default pace of 16 ticks a cost unit.
    CHECK(core::EndTick(journey.parts[0].leg) - core::StartTick(journey) == MEADOW_COST * 16);
    CHECK(SamePoint(core::Where(*map, journey, core::EndTick(journey)), map->TileCentre(0)));
}

TEST_CASE("a raised travel share replaces the map's and the data's hash; data without a map is refused") {
    constexpr u16 RAISED_SHARE = 200;
    core::GameData data{};
    data.world = std::make_shared<const core::WorldMap>(TestMap());
    const std::shared_ptr<const core::WorldMap> before = data.world;
    const u64 hashBefore = data.Hash();
    const std::expected<void, core::Error> raised = game::SetTravelShare(data, RAISED_SHARE);
    REQUIRE(raised.has_value());
    REQUIRE(data.world != nullptr);
    CHECK(data.world->Travel().sharePerMille == RAISED_SHARE);
    CHECK(data.world->Travel().stayMinDays == before->Travel().stayMinDays);
    CHECK(before->Travel().sharePerMille == core::DEFAULT_SHARE_PER_MILLE); // the map read stays as it was
    const u64 hashAfter = data.Hash();
    CHECK(hashAfter != hashBefore);
    core::GameData noMap{};
    const std::expected<void, core::Error> refused = game::SetTravelShare(noMap, RAISED_SHARE);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.contains("world map"));
}

} // namespace
