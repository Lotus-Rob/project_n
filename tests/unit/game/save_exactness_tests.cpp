// Saves a game runs on from exactly as one that never saved, on the world map and in the dev scenes, and from either kind
// of scene into the other; and the save fuzz target's seeds, each written by a case here and checked to load in its game.
#include "build_info.hpp"
#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/delta.hpp"
#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/journey.hpp"
#include "core/paths.hpp"
#include "core/state.hpp"
#include "game/components.hpp"
#include "game/host.hpp"
#include "game/travel.hpp"
#include "game/world_state.hpp"
#include "support/exactness.hpp"
#include "support/host.hpp"
#include "support/manifest.hpp"
#include "support/saves.hpp"
#include "support/world.hpp"

#include <doctest/doctest.h>

namespace {

using test::At;
using test::FOX_HEALTH;
using test::Foxes;
using test::Published;
using test::SameBytes;
using test::Saved;
using test::Tick;

constexpr u64 TICKS = 5;
constexpr u64 MONTHS = 12; // enough for relationships to form
// The simulation tick every save here is loaded as made at: past any tick they played to.
constexpr u64 SAVED_TICK = 1'000;

// The stress arena after some ticks and months: every component and the relationships in use.
void Play(game::Host& host, core::Jobs& jobs, u64 from) {
    for (u64 tick = from; tick < from + TICKS; ++tick) {
        Tick(host, At(tick), {});
    }
    for (u64 month = 0; month < MONTHS; ++month) {
        host.AdvanceCalendar(At(from + TICKS + month), jobs, {});
    }
}

TEST_CASE("a game loaded from a save goes on exactly as the game that saved") {
    core::Jobs jobs{ 1 };
    game::Host saved(test::TestManifest(), game::STRESS_SCENE);
    Play(saved, jobs, 1);
    const std::vector<std::byte> bytes = Saved(saved);
    REQUIRE_FALSE(saved.NpcRelationships().All().empty());

    game::Host loaded(test::TestManifest(), game::ARENA_SCENE); // on the data that made the saved game's world
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene(); // as the logic does at the swap
    CHECK(loaded.StateHash() == saved.StateHash());
    CHECK(loaded.Months() == saved.Months());
    CHECK(loaded.Scene() == saved.Scene());

    Play(saved, jobs, 1 + TICKS + MONTHS);
    Play(loaded, jobs, 1 + TICKS + MONTHS);
    CHECK(loaded.StateHash() == saved.StateHash());
    const std::vector<core::Instance> fromLoaded = Published(loaded, jobs);
    const std::vector<core::Instance> fromSaved = Published(saved, jobs);
    const bool same = SameBytes(fromLoaded, fromSaved);
    CHECK(same);
}

TEST_CASE("a loaded world equals the saved one") {
    const core::GameData data = test::ShippedData();
    core::Jobs jobs{ 1 };
    game::Host saved(test::TestManifest(), game::WORLD_SCENE, data);
    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, data);
    const auto roundTrips = [&] {
        const std::vector<std::byte> bytes = Saved(saved);
        core::Reader reader{ bytes };
        if (!loaded.Load(reader, SAVED_TICK)) {
            return false;
        }
        loaded.EnterScene();
        return loaded.StateHash() == saved.StateHash() && loaded.WorldHash() == saved.WorldHash() &&
               loaded.State().Ids().next == saved.State().Ids().next;
    };
    CHECK(roundTrips()); // tick 0

    // Mid-month, the cultivator walking a route three tiles east.
    const core::Vec2 east = data.world->StartCentre() + core::Vec2{ .x = 3.0F * data.world->TileSize(), .y = 0.0F };
    const std::array<core::Action, 1> walk{ { { .kind = core::ActionKind::WalkTo, .point = east } } };
    Tick(saved, At(1), { .actions = walk, .held = {} });
    for (u64 tick = 2; tick <= 40; ++tick) {
        Tick(saved, At(tick), {});
    }
    REQUIRE(saved.PlayerRoute().has_value());
    CHECK(roundTrips());

    // Mid-journey after a month step: the journeys the base seeded have ended, and new ones are under way.
    saved.AdvanceCalendar(At(41), jobs, {});
    for (u64 tick = 42; tick <= 50; ++tick) {
        Tick(saved, At(tick), {});
    }
    REQUIRE_FALSE(saved.NpcTravel().Journeys().empty());
    CHECK(roundTrips());

    // After three month steps.
    saved.AdvanceCalendar(At(51), jobs, {});
    saved.AdvanceCalendar(At(52), jobs, {});
    REQUIRE(saved.Months() == 3);
    CHECK(roundTrips());
}

TEST_CASE("a loaded world carries on as the saved one") {
    constexpr core::HeldInput RIGHT{ .move = { .x = 1.0F, .y = 0.0F } };
    constexpr u64 BETWEEN = 3; // ticks between month steps
    const core::GameData data = test::ShippedData();
    for (const u32 workers : { 1U, 4U }) {
        CAPTURE(workers);
        core::Jobs jobs{ workers };
        game::Host saved(test::TestManifest(), game::WORLD_SCENE, data);
        game::Host neverSaved(test::TestManifest(), game::WORLD_SCENE, data);
        for (game::Host* host : { &saved, &neverSaved }) {
            Tick(*host, At(1), { .actions = {}, .held = RIGHT });
            host->AdvanceCalendar(At(2), jobs, {});
            Tick(*host, At(3), { .actions = {}, .held = RIGHT });
        }
        u64 tick = 4;
        const std::vector<std::byte> bytes = Saved(saved);
        game::Host loaded(test::TestManifest(), game::ARENA_SCENE, data);
        core::Reader reader{ bytes };
        REQUIRE(loaded.Load(reader, SAVED_TICK));
        loaded.EnterScene();

        // Counted rather than checked at each step, so the worst is asserted once.
        u32 unlikeSaved = 0;
        u32 unlikeNeverSaved = 0;
        const auto compare = [&] {
            unlikeSaved += loaded.StateHash() == saved.StateHash() ? 0U : 1U;
            unlikeNeverSaved += loaded.StateHash() == neverSaved.StateHash() ? 0U : 1U;
        };
        compare();
        for (u32 month = 0; month < 2; ++month) {
            for (u64 step = 0; step < BETWEEN; ++step, ++tick) {
                for (game::Host* host : { &saved, &neverSaved, &loaded }) {
                    Tick(*host, At(tick), { .actions = {}, .held = RIGHT });
                }
                compare();
            }
            for (game::Host* host : { &saved, &neverSaved, &loaded }) {
                host->AdvanceCalendar(At(tick), jobs, {});
            }
            ++tick;
            compare();
        }
        CHECK(loaded.Months() == 3);
        CHECK(unlikeSaved == 0);
        CHECK(unlikeNeverSaved == 0);
    }
}

// Writes `bytes` as the save fuzz target's seed `name` (tests/fuzz/corpus/save/), CHECKing that the file was written.
void WriteSeed(std::string_view name, std::span<const std::byte> bytes) {
    const std::filesystem::path path = std::filesystem::path{ PN_FUZZ_CORPUS } / "save" / name;
    std::ofstream file{ path, std::ios::binary };
    std::ranges::transform(bytes, std::ostreambuf_iterator<char>{ file }, [](std::byte byte) {
        return static_cast<char>(std::to_integer<u8>(byte));
    });
    file.close();
    CHECK(file.good());
}

// Whether the save fuzz target's game (tests/fuzz/save.cpp: the arena on the small test map) loads `bytes`, as a seed must
// for the fuzzer to start from a save the game enters.
bool FuzzGameLoads(std::span<const std::byte> bytes) {
    game::Host game(test::TestManifest(), game::ARENA_SCENE, test::TravellingSmallData());
    core::Reader reader{ bytes };
    return game.Load(reader, SAVED_TICK);
}

// The arena data of the mid-fight save: Qi full, enough for the swords and the lance, on the small test map, as the save
// fuzz target's game runs on it.
core::GameData FightData() {
    core::GameData data = test::TravellingSmallData();
    data.abilities.qiStart = data.abilities.qiMax;
    return data;
}

// Plays `saved` (an arena scene made with FightData()) to the middle of a fight, as tests/fuzz/corpus/save/fight holds
// it. Casts are at the chest of one fox, whose tick-by-tick history the numbers below arrange.
void PlayFight(game::Host& saved) {
    constexpr u64 FIRST_DEATH = 5;    // its fox respawns 210 ticks later and is still easing in at SAVED_AT
    constexpr u64 SECOND_DEATH = 150; // removed by SAVED_AT, its respawn still waiting
    constexpr u64 SWORDS_AT = 285;
    constexpr u64 BOLT_AT = 270;
    constexpr u64 LANCE_AT = 295; // its flash is still showing
    constexpr u64 DASH_AT = 298;
    constexpr u64 SAVED_AT = 300; // a third fox dies on this tick, so is still dissolving
    constexpr std::size_t DYING = 5;
    constexpr core::Vec2 FAR_RIGHT{ .x = 5'000.0F, .y = 0.0F };
    const auto cast = [&saved](u64 tick, core::Slot slot, core::Vec2 point) {
        const std::array<core::Action, 1> actions{ { { .kind = core::ActionKind::Cast, .slot = slot, .point = point } } };
        Tick(saved, At(tick), { .actions = actions, .held = {} });
    };
    core::Vec2 chest{};
    for (u64 tick = 1; tick < SAVED_AT; ++tick) {
        if (tick == BOLT_AT) {
            const core::Registry& entities = saved.State()[game::ACTIVE].Entities();
            chest = entities.get<game::Body>(saved.State()[game::ACTIVE].Find(Foxes(saved).at(2))).current;
            cast(tick, core::Slot::Generator, chest);
        }
        else if (tick == LANCE_AT) {
            cast(tick, core::Slot::Spender, chest);
        }
        else if (tick == SWORDS_AT) {
            cast(tick, core::Slot::Special1, chest);
        }
        else if (tick == DASH_AT) {
            cast(tick, core::Slot::Movement, FAR_RIGHT);
        }
        else {
            Tick(saved, At(tick), {});
        }
        if (tick == FIRST_DEATH || tick == SECOND_DEATH) {
            saved.Damage(Foxes(saved).at(tick == FIRST_DEATH ? 0 : 1), FOX_HEALTH, tick);
        }
    }
    Tick(saved, At(SAVED_AT), {});
    saved.Damage(Foxes(saved).at(DYING), FOX_HEALTH, SAVED_AT);
}

TEST_CASE("the mid-fight save is small, and loads") {
    const core::GameData data = FightData();
    game::Host saved(test::TestManifest(), game::ARENA_SCENE, data);
    PlayFight(saved);
    const core::Registry& entities = saved.State()[game::ACTIVE].Entities();
    core::Jobs jobs{ 1 };
    core::Snapshot snapshot;
    saved.Publish(snapshot, jobs, std::nullopt);
    REQUIRE(snapshot.volleys.size() >= 1);
    REQUIRE(snapshot.flashes.size() >= 1);
    CHECK_FALSE(entities.view<const game::Afflictions>().empty());
    CHECK_FALSE(entities.view<const game::Dying>().empty());
    CHECK_FALSE(entities.view<const game::Arriving>().empty());
    CHECK_FALSE(entities.view<const game::Dashing>().empty());
    const std::vector<std::byte> bytes = Saved(saved);
    // The arena's 5,000 world NPCs as its entry build made them: its scratch world's difference from that is empty.
    constexpr std::size_t SMALL = 16'384;
    CHECK(bytes.size() < SMALL);
    game::Host loaded(test::TestManifest(), game::CELLS_SCENE, data);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene(); // as the logic does at the swap
    CHECK(loaded.StateHash() == saved.StateHash());
}

// Not part of a run: `--test-case='write the fight save seed' --no-skip` (tests/AGENTS.md) writes the fuzz seed.
TEST_CASE("write the fight save seed" * doctest::skip()) {
    game::Host saved(test::TestManifest(), game::ARENA_SCENE, FightData());
    PlayFight(saved);
    WriteSeed("fight", Saved(saved));
}

// Not part of a run: `--test-case='write the world save seed' --no-skip` (tests/AGENTS.md) writes the fuzz seed of the
// world scene with nothing in it, its NPCs as the small test map makes them, which the save target's game loads.
TEST_CASE("write the world save seed" * doctest::skip()) {
    const core::GameData data = test::TravellingSmallData();
    WriteSeed("world", test::WorldSaveAt({}, &data));
}

TEST_CASE("an arena save mid-fight loads and carries on as the saved one") {
    constexpr u64 SAVED_AT = 300; // where PlayFight() leaves the fight
    constexpr u64 TICKS_ON = 600; // past every death's dissolve, respawn and easing in of the fight
    const core::GameData data = FightData();
    game::Host saved(test::TestManifest(), game::ARENA_SCENE, data);
    game::Host neverSaved(test::TestManifest(), game::ARENA_SCENE, data);
    PlayFight(saved);
    PlayFight(neverSaved);
    const std::vector<std::byte> bytes = Saved(saved);
    game::Host loaded(test::TestManifest(), game::CELLS_SCENE, data);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene();
    CHECK(
        test::UnlikeOnBothWorkerCounts(
            test::Games{ .saved = &saved, .neverSaved = &neverSaved, .loaded = &loaded }, SAVED_AT + 1, TICKS_ON, 1
        ) == 0
    );
}

// Brings `host`, built on the world scene, to the arena at tick `tick` by F2.
void ToArena(game::Host& host, u64 tick) {
    const std::array<core::Action, 1> cycle{ { { .kind = core::ActionKind::CycleScene } } };
    Tick(host, At(tick), { .actions = cycle, .held = {} });
    host.EnterScene();
    REQUIRE(host.Current().key == game::ARENA_SCENE);
}

// A game on the small test map stepped a month on the world map, from tick 1, then (when `arena`) taken to the arena by F2
// at tick 3 to step a month of its own there at tick 5, so both its worlds differ from how they were made.
void PlayBothWorlds(game::Host& host, core::Jobs& jobs, bool arena) {
    constexpr core::HeldInput RIGHT{ .move = { .x = 1.0F, .y = 0.0F } };
    Tick(host, At(1), { .actions = {}, .held = RIGHT });
    host.AdvanceCalendar(At(2), jobs, {});
    if (arena) {
        ToArena(host, 3);
        Tick(host, At(4), {});
        host.AdvanceCalendar(At(5), jobs, {});
    }
}

TEST_CASE("a save made in a dev scene restores both worlds") {
    const core::GameData data = test::TravellingSmallData();
    core::Jobs jobs{ 1 };
    game::Host saved(test::TestManifest(), game::WORLD_SCENE, data);
    game::Host neverSaved(test::TestManifest(), game::WORLD_SCENE, data);
    PlayBothWorlds(saved, jobs, true);
    PlayBothWorlds(neverSaved, jobs, true);
    REQUIRE(saved.Months() == 1); // the arena's own
    const std::vector<std::byte> bytes = Saved(saved);
    game::Host loaded(test::TestManifest(), game::WORLD_SCENE, data);
    REQUIRE(loaded.WorldHash() != saved.WorldHash());
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene();
    CHECK(loaded.Current().key == game::ARENA_SCENE);
    CHECK(loaded.Months() == 1);
    CHECK(loaded.StateHash() == saved.StateHash()); // the scratch world and the scene
    CHECK(loaded.WorldHash() == saved.WorldHash());
    CHECK(neverSaved.StateHash() == saved.StateHash());
    const bool savesAlike = Saved(loaded) == bytes; // so its scratch world's base is the one the saved game was built as
    CHECK(savesAlike);

    // A month more of the arena's own: both worlds go on as the saved game's.
    for (game::Host* host : { &saved, &neverSaved, &loaded }) {
        Tick(*host, At(6), {});
        host->AdvanceCalendar(At(7), jobs, {});
    }
    CHECK(loaded.StateHash() == saved.StateHash());
    CHECK(neverSaved.StateHash() == saved.StateHash());
    CHECK(loaded.WorldHash() == saved.WorldHash());
}

TEST_CASE("a save made on the world map loads in a dev scene, and one made in a dev scene loads on the world map") {
    const core::GameData data = test::TravellingSmallData();
    core::Jobs jobs{ 1 };
    // Each loads the other's save; its twin never does, and shows where the save's game goes on to.
    game::Host onMap(test::TestManifest(), game::WORLD_SCENE, data);
    game::Host onMapTwin(test::TestManifest(), game::WORLD_SCENE, data);
    game::Host inArena(test::TestManifest(), game::WORLD_SCENE, data);
    game::Host inArenaTwin(test::TestManifest(), game::WORLD_SCENE, data);
    PlayBothWorlds(onMap, jobs, false);
    PlayBothWorlds(onMapTwin, jobs, false);
    PlayBothWorlds(inArena, jobs, true);
    PlayBothWorlds(inArenaTwin, jobs, true);
    Tick(onMap, At(3), {});
    Tick(onMapTwin, At(3), {}); // so the map's game world differs from the arena game's set-aside one
    REQUIRE(onMap.WorldHash() != inArena.WorldHash());
    const std::vector<std::byte> fromMap = Saved(onMap);
    const std::vector<std::byte> fromArena = Saved(inArena);

    core::Reader mapReader{ fromMap };
    REQUIRE(inArena.Load(mapReader, SAVED_TICK)); // the arena's scratch world goes
    inArena.EnterScene();
    core::Reader arenaReader{ fromArena };
    REQUIRE(onMap.Load(arenaReader, SAVED_TICK)); // a scratch world comes, made again from the save
    onMap.EnterScene();
    CHECK(inArena.Current().key == game::WORLD_SCENE);
    CHECK(onMap.Current().key == game::ARENA_SCENE);
    // Saving again writes what the twins write: no scratch world left over, and the one made again written against the
    // same base as the twin's own.
    const bool mapSavesAlike = Saved(inArena) == Saved(onMapTwin);
    const bool arenaSavesAlike = Saved(onMap) == Saved(inArenaTwin);
    CHECK(mapSavesAlike);
    CHECK(arenaSavesAlike);

    // Each now the other's twin, which a tick and a month later still holds.
    const auto unlike = [](const game::Host& host, const game::Host& twin) {
        return host.StateHash() != twin.StateHash() || host.WorldHash() != twin.WorldHash() || host.Months() != twin.Months();
    };
    u32 differences = 0;
    differences += unlike(inArena, onMapTwin) ? 1U : 0U;
    differences += unlike(onMap, inArenaTwin) ? 1U : 0U;
    for (game::Host* host : { &onMap, &onMapTwin, &inArena, &inArenaTwin }) {
        Tick(*host, At(6), {});
        host->AdvanceCalendar(At(7), jobs, {});
    }
    differences += unlike(inArena, onMapTwin) ? 1U : 0U;
    differences += unlike(onMap, inArenaTwin) ? 1U : 0U;
    CHECK(differences == 0);
}

// How many of the journeys `host`'s world (on the world map, on `data`) holds are the base's gone, and how many are new
// ones written by their plans, as a save writes them.
std::pair<std::size_t, std::size_t> GoneAndPlanned(const game::Host& host, const core::GameData& data) {
    core::Paths paths{ data.world };
    const game::WorldState base = test::GeneratedWorld(data, host.State().Seed(), &paths);
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    host.NpcTravel().SaveDelta(writer, base.travel, data.world.get(), &paths);
    std::vector<u64> gone;
    std::vector<core::VisitPlan> plans;
    core::Reader reader{ bytes };
    reader(gone, plans);
    REQUIRE_FALSE(reader.Failed());
    return { gone.size(), plans.size() };
}

// How many NPCs of `host`'s world (on the world map, on `data`) differ from the base's.
u64 ChangedNpcs(const game::Host& host, const core::GameData& data) {
    core::Paths paths{ data.world };
    const game::WorldState base = test::GeneratedWorld(data, host.State().Seed(), &paths);
    return core::CountDelta(base.state[game::WORLD], host.State()[game::WORLD], test::WorldComponents{}).changed;
}

// The save of the arena entered from a world a month on, a fight under way: the save target's seed of a dev scene with its
// game's world set aside unlike how it was made.
std::vector<std::byte> ScratchSeed() {
    constexpr u64 TO_MID_VOLLEY = 20;
    const core::GameData data = FightData();
    core::Jobs jobs{ 1 };
    game::Host saved(test::TestManifest(), game::WORLD_SCENE, data);
    PlayBothWorlds(saved, jobs, false);
    REQUIRE(ChangedNpcs(saved, data) > 0); // the game's world's section has a difference to fuzz
    ToArena(saved, 3);
    const std::array<core::Action, 1> swords{ { { .kind = core::ActionKind::Cast,
                                                  .slot = core::Slot::Special1,
                                                  .point = test::BodyOf(saved, Foxes(saved).front()).current } } };
    Tick(saved, At(4), { .actions = swords, .held = {} });
    for (u64 tick = 5; tick < 5 + TO_MID_VOLLEY; ++tick) {
        Tick(saved, At(tick), {});
    }
    return Saved(saved);
}

// The save of the world scene on the small test map, its travellers out, after a month step: its journeys partly the
// base's, partly ended and partly new, the save target's seed of a game's world unlike how it was made.
std::vector<std::byte> DeltaSeed() {
    constexpr u64 TICKS_AFTER = 10;
    const core::GameData data = test::TravellingSmallData();
    core::Jobs jobs{ 1 };
    game::Host saved(test::TestManifest(), game::WORLD_SCENE, data);
    PlayBothWorlds(saved, jobs, false);
    for (u64 tick = 3; tick < 3 + TICKS_AFTER; ++tick) {
        Tick(saved, At(tick), {});
    }
    // Both ways a journey is written, so the fuzzer starts from each.
    const auto [gone, planned] = GoneAndPlanned(saved, data);
    REQUIRE(gone > 0);
    REQUIRE(planned > 0);
    REQUIRE(ChangedNpcs(saved, data) > 0);
    return Saved(saved);
}

TEST_CASE("the save fuzz target's seeds load in its game") {
    game::Host fight(test::TestManifest(), game::ARENA_SCENE, FightData());
    PlayFight(fight);
    CHECK(FuzzGameLoads(Saved(fight)));
    const core::GameData small = test::TravellingSmallData();
    CHECK(FuzzGameLoads(test::WorldSaveAt({}, &small)));
    const std::vector<std::byte> delta = DeltaSeed();
    CHECK(FuzzGameLoads(delta));
    const std::vector<std::byte> scratch = ScratchSeed();
    CHECK(FuzzGameLoads(scratch));
    constexpr std::size_t SMALL = 16'384; // a seed the fuzzer mutates quickly
    CHECK(delta.size() < SMALL);
    CHECK(scratch.size() < SMALL);
}

// Not part of a run: `--test-case='write the delta save seed' --no-skip` (tests/AGENTS.md) writes the fuzz seed.
TEST_CASE("write the delta save seed" * doctest::skip()) {
    WriteSeed("delta", DeltaSeed());
}

// Not part of a run: `--test-case='write the scratch save seed' --no-skip` (tests/AGENTS.md) writes the fuzz seed.
TEST_CASE("write the scratch save seed" * doctest::skip()) {
    WriteSeed("scratch", ScratchSeed());
}

// `world`'s world-ID counter and world domain as a save writes it whole, each storage in its order: equal bytes, the
// same NPCs in the same order.
std::vector<std::byte> WorldTier(const game::WorldState& world) {
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    writer(world.state.Ids().next);
    core::SaveDomain(writer, world.state[game::WORLD], test::WorldComponents{});
    return bytes;
}

TEST_CASE("the world a dev scene's build made, kept after the scene is left, is the one any build of it makes") {
    // A save of a dev scene is written against, and loaded onto, the world the host kept from its build, rather than one
    // built again: that holds only while the build reads neither the scene counter nor the game data's creatures.
    constexpr u64 SCENE_IDS_ON = 1'000; // as if many scenes had run before
    const core::GameData data = test::SmallData();
    core::GameData edited = data;
    edited.fox.health = 7;
    edited.fox.orbitRadius *= 2.0F;
    edited.cultivator.health = 3;
    const std::vector<std::byte> mapSave = test::WorldSaveAt({}, &data);
    u32 unlike = 0;
    for (const core::SceneKey scene : { game::ARENA_SCENE, game::STRESS_SCENE, game::PAINTED_SCENE, game::CELLS_SCENE }) {
        game::Host host(test::TestManifest(), scene, data);
        core::Reader reader{ mapSave };
        REQUIRE(host.Load(reader, SAVED_TICK)); // to the world map, the dev scene left
        host.EnterScene();
        REQUIRE(host.Current().key == game::WORLD_SCENE);
        host.SetData(edited);
        const game::WorldState* kept = host.KeptScratchBase();
        REQUIRE(kept != nullptr);
        const game::WorldState fresh = test::ScratchBase(scene, edited, { .next = core::SCENE_ID_BASE + SCENE_IDS_ON });
        unlike += WorldTier(*kept) == WorldTier(fresh) ? 0U : 1U;
    }
    CHECK(unlike == 0);
}

} // namespace
