#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/journey.hpp"
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
constexpr std::array<u32, 7> ROAD_ROUTE{ 0, 1, 2, 3, 4, 5, 6 };

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
    const core::StableId follower = Traveller(state);
    game::Travel travel;
    Journeys(travel, state, map, alone, 1);
    travel.Start(VisitOf(map, alone, DAYS_APART * DAY));
    core::Journey pair = VisitOf(map, leader, 0);
    pair.members.push_back(follower);
    travel.Start(pair);

    travel.Forget(alone);
    CHECK(travel.JournalOf(alone).empty());
    CHECK_FALSE(travel.Travelling(alone));
    REQUIRE(travel.Journeys().size() == 1);

    // A journey goes on with the members left, led by the next.
    travel.Forget(leader);
    CHECK_FALSE(travel.Travelling(leader));
    REQUIRE(travel.Journeys().size() == 1);
    const bool followerLeft = travel.Journeys().front().members == std::vector{ follower };
    CHECK(followerLeft);
    CHECK(travel.Travelling(follower));
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

    // Without the world map, only an empty table loads.
    CHECK_FALSE(Loads(bytes, state, nullptr));
    CHECK(Loads(Bytes(game::Travel{}), state, nullptr));
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
    CHECK_FALSE(Loads(BytesOf(VisitOf(map, homeless, 0)), activated, &map));
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

    // Two journeys of one NPC, then no journals, as Save() lays a table out.
    constexpr u64 NO_JOURNALS = 0;
    std::vector<std::byte> twice;
    core::Writer writer{ twice };
    writer(std::vector{ sound, sound }, NO_JOURNALS);
    CHECK_FALSE(Loads(twice, state, &map));
}

} // namespace
