#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/delta.hpp"
#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/journey.hpp"
#include "core/manifest.hpp"
#include "core/paths.hpp"
#include "core/random.hpp"
#include "core/state.hpp"
#include "game/combat.hpp"
#include "game/components.hpp"
#include "game/host.hpp"
#include "game/npcs.hpp"
#include "game/scenes/world.hpp"
#include "game/travel.hpp"
#include "game/world_state.hpp"
#include "support/fog.hpp"
#include "support/host.hpp"
#include "support/manifest.hpp"
#include "support/saves.hpp"
#include "support/world.hpp"

#include <doctest/doctest.h>

namespace {

// The shipped combat rules (assets/data/abilities.yaml's `combat:` section, which a data test checks).
constexpr core::CombatRules RULES{};

// The simulation tick every save here is loaded as made at: past any tick they played to, so only a flash dated after it
// is refused.
constexpr u64 SAVED_TICK = 1'000;

using test::At;
using test::FOX_HEALTH;
using test::Foxes;
using test::Published;
using test::SameBytes;
using test::Saved;
using test::Tick;

TEST_CASE("a save made in world loads back into it") {
    constexpr core::HeldInput RIGHT{ .move = { .x = 1.0F, .y = 0.0F } };
    core::Jobs jobs{ 1 };
    game::Host saved(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    Tick(saved, At(1), { .actions = {}, .held = RIGHT }); // the travellers on their way, the cultivator off its start
    const std::vector<std::byte> bytes = Saved(saved);

    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, test::ShippedData());
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene();
    CHECK(loaded.Current() == saved.Current());
    CHECK(loaded.Current().key == game::WORLD_SCENE);
    CHECK(loaded.Player() == saved.Player());
    CHECK(loaded.StateHash() == saved.StateHash());

    // And it goes on as the game that saved.
    Tick(saved, At(2), { .actions = {}, .held = RIGHT });
    Tick(loaded, At(2), { .actions = {}, .held = RIGHT });
    CHECK(loaded.StateHash() == saved.StateHash());
    const std::vector<core::Instance> fromLoaded = Published(loaded, jobs);
    const std::vector<core::Instance> fromSaved = Published(saved, jobs);
    const bool same = SameBytes(fromLoaded, fromSaved);
    CHECK(same);
}

TEST_CASE("the world scene's NPCs number from 1") {
    const game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    const std::shared_ptr<const core::WorldMap> world = test::ShippedWorld();
    const core::WorldMap& map = *world;
    const auto city = std::ranges::find(map.Places(), core::PlaceKind::City, &core::Place::kind);
    REQUIRE(city != map.Places().end());
    const core::State& state = host.State();
    const core::Entity first = state[game::WORLD].Find(core::StableId{ 1 });
    REQUIRE(first != core::Entity{ entt::null });
    CHECK(state[game::WORLD].Entities().get<game::Npc>(first).home == std::distance(map.Places().begin(), city));
    CHECK(host.Player() == core::StableId{ core::SCENE_ID_BASE }); // the cultivator, spawned first, from the scene's range
}

TEST_CASE("a save keeps the scene counter") {
    game::Host saved(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    Tick(saved, At(1), {});
    const std::vector<std::byte> bytes = Saved(saved);

    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, test::ShippedData());
    REQUIRE(loaded.State().SceneIds().next != saved.State().SceneIds().next); // the arena spawns more of its own
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene();
    CHECK(loaded.State().SceneIds().next == saved.State().SceneIds().next);
    CHECK(loaded.State().Ids().next == saved.State().Ids().next);
}

TEST_CASE("a save made in a dev scene holds the game's world set aside, and where the cultivator comes back to") {
    constexpr core::HeldInput RIGHT{ .move = { .x = 1.0F, .y = 0.0F } };
    const std::array<core::Action, 1> cycle{ { { .kind = core::ActionKind::CycleScene } } };
    core::Jobs jobs{ 1 };
    game::Host saved(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    Tick(saved, At(1), { .actions = {}, .held = RIGHT });
    saved.AdvanceCalendar(At(2), jobs, {}); // a world that differs from a new game's
    const core::Vec2 foot = test::BodyOf(saved, saved.Player()).current;
    Tick(saved, At(3), { .actions = cycle, .held = {} });
    saved.EnterScene();
    REQUIRE(saved.Current().key == game::ARENA_SCENE);
    Tick(saved, At(4), {});
    const std::vector<std::byte> bytes = Saved(saved);

    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, test::ShippedData());
    REQUIRE(loaded.WorldHash() != saved.WorldHash());
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene();
    CHECK(loaded.StateHash() == saved.StateHash());
    CHECK(loaded.WorldHash() == saved.WorldHash());

    // Both back round to the world map: the same world, the cultivator where it left it.
    for (u64 tick = 5; saved.Current().key != game::WORLD_SCENE; ++tick) {
        Tick(saved, At(tick), { .actions = cycle, .held = {} });
        saved.EnterScene();
        Tick(loaded, At(tick), { .actions = cycle, .held = {} });
        loaded.EnterScene();
    }
    CHECK(loaded.Current().key == game::WORLD_SCENE);
    CHECK(loaded.StateHash() == saved.StateHash());
    CHECK(test::BodyOf(loaded, loaded.Player()).current.x == foot.x);
    CHECK(test::BodyOf(loaded, loaded.Player()).current.y == foot.y);
}

TEST_CASE("Load holds and changes nothing until EnterScene") {
    game::Host saved(test::TestManifest(), game::PAINTED_SCENE);
    Tick(saved, At(1), {});
    const std::vector<std::byte> bytes = Saved(saved);

    game::Host loaded(test::TestManifest(), game::ARENA_SCENE);
    const u64 before = loaded.StateHash();
    const core::SceneId shown = loaded.Scene();
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    CHECK(loaded.StateHash() == before);
    CHECK(loaded.Scene() == shown);
    CHECK(loaded.Current().key == game::ARENA_SCENE);
    const std::optional<core::SceneFacts> requested = loaded.Requested(); // the saved scene's facts
    REQUIRE(requested.has_value());
    CHECK(requested.value_or(core::SceneFacts{}) == saved.Current());

    loaded.EnterScene();
    CHECK(loaded.StateHash() == saved.StateHash());
    CHECK(loaded.Scene() == saved.Scene());
    CHECK(loaded.Current() == saved.Current());
    CHECK_FALSE(loaded.Requested().has_value());
}

TEST_CASE("the player, where it walked, and the selection survive a save") {
    constexpr core::HeldInput RIGHT{ .move = { .x = 1.0F, .y = 0.0F } };
    game::Host saved(test::TestManifest());
    const std::array<core::Action, 1> select{ { { .kind = core::ActionKind::Select, .target = saved.Player() } } };
    Tick(saved, At(1), { .actions = select, .held = RIGHT });
    const std::vector<std::byte> bytes = Saved(saved);

    game::Host loaded(test::TestManifest(), game::CELLS_SCENE);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene(); // as the logic does at the swap
    CHECK(loaded.Player() == saved.Player());
    CHECK(loaded.Selected() == saved.Player());
    Tick(loaded, At(2), { .actions = {}, .held = RIGHT });
    Tick(saved, At(2), { .actions = {}, .held = RIGHT });
    CHECK(loaded.StateHash() == saved.StateHash());
}

TEST_CASE("health, the dying and the respawns survive a save") {
    constexpr u64 FIRST_DEATH = 5;
    constexpr u64 SAVED_AT = 40;   // the first fox gone and its respawn waiting, the second mid-dissolve
    constexpr u64 PLAYED_TO = 400; // past both respawns and into their arrivals
    constexpr i64 WOUND = 300;
    game::Host saved(test::TestManifest());
    for (u64 tick = 1; tick <= SAVED_AT; ++tick) {
        Tick(saved, At(tick), {});
        if (tick == FIRST_DEATH) {
            saved.Damage(Foxes(saved).at(0), FOX_HEALTH, tick);
        }
    }
    const std::vector<core::StableId> foxes = Foxes(saved);
    saved.Damage(foxes.at(0), FOX_HEALTH, SAVED_AT);
    saved.Damage(foxes.at(1), WOUND, SAVED_AT);
    const std::vector<std::byte> bytes = Saved(saved);

    game::Host loaded(test::TestManifest(), game::CELLS_SCENE);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene(); // as the logic does at the swap
    CHECK(loaded.StateHash() == saved.StateHash());
    for (u64 tick = SAVED_AT + 1; tick <= PLAYED_TO; ++tick) {
        Tick(saved, At(tick), {});
        Tick(loaded, At(tick), {});
    }
    CHECK(loaded.StateHash() == saved.StateHash());
    CHECK(Foxes(loaded).size() == Foxes(saved).size());
}

TEST_CASE("Qi, cooldowns and a dash under way survive a save") {
    constexpr u64 SAVED_AT = 4; // mid-dash
    constexpr u64 PLAYED_TO = 30;
    constexpr core::Vec2 FAR_RIGHT{ .x = 5'000.0F, .y = 0.0F };
    game::Host saved(test::TestManifest());
    const std::array<core::Action, 2> casts{ {
        { .kind = core::ActionKind::Cast, .slot = core::Slot::Generator },
        { .kind = core::ActionKind::Cast, .slot = core::Slot::Movement, .point = FAR_RIGHT },
    } };
    Tick(saved, At(1), { .actions = casts, .held = {} });
    for (u64 tick = 2; tick <= SAVED_AT; ++tick) {
        Tick(saved, At(tick), {});
    }
    const core::Registry& entities = saved.State()[game::ACTIVE].Entities();
    const core::Entity player = saved.State()[game::ACTIVE].Find(saved.Player());
    REQUIRE(entities.all_of<game::Dashing>(player));
    const std::vector<std::byte> bytes = Saved(saved);

    game::Host loaded(test::TestManifest(), game::CELLS_SCENE);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene(); // as the logic does at the swap
    CHECK(loaded.StateHash() == saved.StateHash());
    const game::Caster& caster =
        loaded.State()[game::ACTIVE].Entities().get<game::Caster>(loaded.State()[game::ACTIVE].Find(loaded.Player()));
    CHECK(caster.qi == entities.get<game::Caster>(player).qi);
    for (u64 tick = SAVED_AT + 1; tick <= PLAYED_TO; ++tick) {
        Tick(saved, At(tick), {});
        Tick(loaded, At(tick), {});
    }
    CHECK(loaded.StateHash() == saved.StateHash());
}

// Whether two runs' events are the same, field by field.
bool SameEvents(const std::vector<core::Event>& lhs, const std::vector<core::Event>& rhs) {
    return std::ranges::equal(lhs, rhs, [](const core::Event& one, const core::Event& two) {
        return one.kind == two.kind && one.hit == two.hit && one.use == two.use && one.target == two.target &&
               one.value == two.value && std::bit_cast<u64>(one.position) == std::bit_cast<u64>(two.position);
    });
}

TEST_CASE("a save mid-arrival goes on as the game that saved, with the same events") {
    constexpr u64 DEATH = 5;
    constexpr u64 SAVED_AT = 300;  // the fox that respawned at tick 215 is easing in until tick 395
    constexpr u64 PLAYED_TO = 420; // past its arrival and two of the aura's hits
    game::Host saved(test::TestManifest());
    for (u64 tick = 1; tick <= SAVED_AT; ++tick) {
        Tick(saved, At(tick), {});
        if (tick == DEATH) {
            saved.Damage(Foxes(saved).at(0), FOX_HEALTH, tick);
        }
    }
    REQUIRE_FALSE(saved.State()[game::ACTIVE].Entities().view<const game::Arriving>().empty());
    const std::vector<std::byte> bytes = Saved(saved);

    game::Host loaded(test::TestManifest(), game::CELLS_SCENE);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene(); // as the logic does at the swap
    CHECK(loaded.StateHash() == saved.StateHash());
    std::vector<core::Event> savedEvents;
    std::vector<core::Event> loadedEvents;
    for (u64 tick = SAVED_AT + 1; tick <= PLAYED_TO; ++tick) {
        saved.Tick(At(tick), {}, savedEvents);
        loaded.Tick(At(tick), {}, loadedEvents);
    }
    CHECK_FALSE(savedEvents.empty());
    const bool same = SameEvents(loadedEvents, savedEvents);
    CHECK(same);
    CHECK(loaded.StateHash() == saved.StateHash());
}

TEST_CASE("the save round-trips mid-volley") {
    constexpr u64 CAST = 2;
    core::GameData data = game::BuiltInData();
    data.abilities.qiStart = data.abilities.qiMax; // enough for the swords
    game::Host saved(test::TestManifest(), game::ARENA_SCENE, data);
    Tick(saved, At(1), {});
    const core::Vec2 point =
        saved.State()[game::ACTIVE].Entities().get<game::Body>(saved.State()[game::ACTIVE].Find(Foxes(saved).front())).current;
    const std::array<core::Action, 1> cast{
        { { .kind = core::ActionKind::Cast, .slot = core::Slot::Special1, .point = point } }
    };
    Tick(saved, At(CAST), { .actions = cast, .held = {} });
    core::Jobs jobs{ 1 };
    core::Snapshot snapshot;
    saved.Publish(snapshot, jobs, std::nullopt);
    REQUIRE(snapshot.volleys.size() == 1);
    const core::Volley volley = snapshot.volleys.front().volley;
    const u64 last = core::LastArrival(volley);
    const u64 savedAt = CAST + ((last - CAST) / 2);
    for (u64 tick = CAST + 1; tick <= savedAt; ++tick) {
        Tick(saved, At(tick), {});
    }
    const std::vector<std::byte> bytes = Saved(saved);

    game::Host loaded(test::TestManifest(), game::CELLS_SCENE, data);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene(); // as the logic does at the swap
    CHECK(loaded.StateHash() == saved.StateHash());
    std::size_t hits = 0;
    u32 differentEvents = 0; // ticks whose events differ, counted so the test asserts the worst once
    u32 differentStates = 0;
    for (u64 tick = savedAt + 1; tick <= last + volley.shape.fadeTicks + 2; ++tick) {
        std::vector<core::Event> savedEvents;
        std::vector<core::Event> loadedEvents;
        saved.Tick(At(tick), {}, savedEvents);
        loaded.Tick(At(tick), {}, loadedEvents);
        differentEvents += SameEvents(loadedEvents, savedEvents) ? 0U : 1U;
        differentStates += loaded.StateHash() == saved.StateHash() ? 0U : 1U;
        hits += static_cast<std::size_t>(std::ranges::count(savedEvents, volley.use, &core::Event::use));
    }
    CHECK(differentEvents == 0);
    CHECK(differentStates == 0);
    CHECK(hits > 0);
}

TEST_CASE("the save round-trips mid-burn") {
    constexpr u64 CAST = 2;
    constexpr u64 BETWEEN = 30;        // after the bolt's hit, before the burn's first tick
    constexpr u32 BURN_TICKS = 3;      // the bolt's burn, all still to come
    constexpr f32 NEAR_ORBIT = 150.0F; // the foxes out of the crystal aura's reach
    core::GameData data = game::BuiltInData();
    data.fox.orbitRadius = NEAR_ORBIT;
    data.fox.orbitWobble = 0.0F;
    game::Host saved(test::TestManifest(), game::ARENA_SCENE, data);
    Tick(saved, At(1), {});
    const core::StableId fox = Foxes(saved).front();
    const core::Vec2 foot =
        saved.State()[game::ACTIVE].Entities().get<game::Body>(saved.State()[game::ACTIVE].Find(fox)).current;
    constexpr f32 HALF = 0.5F;
    const f32 height = static_cast<f32>(test::TestManifest()->Sprite(data.fox.sprite).footY);
    const core::Vec2 chest{ .x = foot.x, .y = foot.y - (height * HALF) }; // where the bolt aims: nearest this fox
    const std::array<core::Action, 1> cast{
        { { .kind = core::ActionKind::Cast, .slot = core::Slot::Generator, .point = chest } }
    };
    Tick(saved, At(CAST), { .actions = cast, .held = {} });
    core::Jobs jobs{ 1 };
    core::Snapshot snapshot;
    saved.Publish(snapshot, jobs, std::nullopt);
    REQUIRE(snapshot.volleys.size() == 1);
    const core::Volley bolt = snapshot.volleys.front().volley;
    const u64 savedAt = core::ArrivalTick(bolt, 0) + BETWEEN;
    for (u64 tick = CAST + 1; tick <= savedAt; ++tick) {
        Tick(saved, At(tick), {});
    }
    REQUIRE(saved.State()[game::ACTIVE].Entities().all_of<game::Afflictions>(saved.State()[game::ACTIVE].Find(fox)));
    const std::vector<std::byte> bytes = Saved(saved);

    game::Host loaded(test::TestManifest(), game::CELLS_SCENE, data);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene(); // as the logic does at the swap
    CHECK(loaded.StateHash() == saved.StateHash());
    std::vector<core::Event> used;
    u32 differentEvents = 0; // ticks whose events differ, counted so the test asserts the worst once
    u32 differentStates = 0;
    for (u64 tick = savedAt + 1; tick <= savedAt + (BURN_TICKS * RULES.afflictionPeriod) + 1; ++tick) {
        std::vector<core::Event> savedEvents;
        std::vector<core::Event> loadedEvents;
        saved.Tick(At(tick), {}, savedEvents);
        loaded.Tick(At(tick), {}, loadedEvents);
        differentEvents += SameEvents(loadedEvents, savedEvents) ? 0U : 1U;
        differentStates += loaded.StateHash() == saved.StateHash() ? 0U : 1U;
        std::ranges::copy_if(loadedEvents, std::back_inserter(used), [&bolt](const core::Event& event) {
            return event.use == bolt.use;
        });
    }
    CHECK(differentEvents == 0);
    CHECK(differentStates == 0);
    REQUIRE(used.size() == BURN_TICKS + 1);
    for (std::size_t index = 0; index < BURN_TICKS; ++index) { // the burn's three ticks, one by one
        CHECK(used.at(index).kind == core::EventKind::Hit);
        CHECK(used.at(index).hit == core::HitKind::Burn);
        CHECK(used.at(index).target == fox);
    }
    CHECK(used.back().kind == core::EventKind::UseEnded);
}

TEST_CASE("a truncated save is refused, and the game is unchanged") {
    constexpr std::size_t CUT = 4;
    game::Host saved(test::TestManifest(), game::STRESS_SCENE);
    const std::vector<std::byte> bytes = Saved(saved);

    game::Host game(test::TestManifest(), game::ARENA_SCENE);
    const u64 before = game.StateHash();
    core::Reader reader{ std::span{ bytes }.first(bytes.size() - CUT) };
    CHECK_FALSE(game.Load(reader, SAVED_TICK));
    CHECK(game.StateHash() == before);
}

TEST_CASE("a save with bytes after the state, or of another format, is refused") {
    constexpr std::byte EXTRA{ 1 };
    constexpr u32 OTHER_FORMAT = 99;
    game::Host saved(test::TestManifest(), game::ARENA_SCENE);
    std::vector<std::byte> longer = Saved(saved);
    longer.push_back(EXTRA);
    game::Host game(test::TestManifest(), game::ARENA_SCENE);
    core::Reader longReader{ longer };
    CHECK_FALSE(game.Load(longReader, SAVED_TICK));

    std::vector<std::byte> other = Saved(saved);
    const auto format = std::bit_cast<std::array<std::byte, sizeof(u32)>>(OTHER_FORMAT);
    std::ranges::copy(format, other.begin());
    core::Reader otherReader{ other };
    CHECK_FALSE(game.Load(otherReader, SAVED_TICK));
}

using test::ACTIVE_VERSION;
using test::ArenaSave;
using test::SavedVolley;
using test::WORLD_VERSION;
using test::WorldSaveAt;
using World = test::WorldComponents;

// Whether a fresh host loads `bytes`, saved at `savedTick`; CHECKs that its state hash is the same afterwards, and that
// it holds a state to enter exactly when it loaded one.
bool Loads(const std::vector<std::byte>& bytes, u64 savedTick = SAVED_TICK) {
    game::Host game(test::TestManifest(), game::ARENA_SCENE);
    const u64 before = game.StateHash();
    core::Reader reader{ bytes };
    const bool loaded = game.Load(reader, savedTick);
    const u64 after = game.StateHash();
    CHECK(after == before);
    const bool holds = game.Requested().has_value();
    CHECK(holds == loaded);
    return loaded;
}

// What `write` writes to a core::Writer, as bytes.
template<typename Write>
std::vector<std::byte> Written(const Write& write) {
    std::vector<std::byte> part;
    core::Writer writer{ part };
    write(writer);
    return part;
}

constexpr u64 STATE_SEED = 7;
constexpr u32 NPC_CELL = 3;

TEST_CASE("a sound save built as the refusals below build theirs is loaded") {
    core::State state{ game::DOMAINS, STATE_SEED };
    static_cast<void>(game::SpawnNpc(state, NPC_CELL));
    const core::Entity player = state.Create(game::ACTIVE, core::IdRange::Scene).second;
    state[game::ACTIVE].Entities().emplace<game::Body>(player);
    game::Host game(test::TestManifest(), game::ARENA_SCENE);
    const std::vector<std::byte> bytes = ArenaSave(state, state[game::ACTIVE].Entities().get<core::StableId>(player));
    core::Reader reader{ bytes };
    CHECK(game.Load(reader, SAVED_TICK));
}

TEST_CASE("a save whose ID counters leave their ranges is refused, and the game is unchanged") {
    // The scene counter follows the format and the scene; the game world's own counter, its seed, generator version and
    // base hash.
    constexpr std::size_t SCENE_AT = sizeof(u32) + sizeof(u8);
    constexpr std::size_t WORLD_AT = SCENE_AT + sizeof(u64) + sizeof(u64) + sizeof(u32) + sizeof(u64);
    const core::State state{ game::DOMAINS, STATE_SEED };
    const std::vector<std::byte> sound = ArenaSave(state);
    const auto with = [&sound](std::size_t offset, u64 next) {
        std::vector<std::byte> bytes = sound;
        std::ranges::copy(
            std::bit_cast<std::array<std::byte, sizeof(u64)>>(next), bytes.begin() + static_cast<std::ptrdiff_t>(offset)
        );
        return bytes;
    };
    CHECK(Loads(sound));
    CHECK(Loads(with(WORLD_AT, core::SCENE_ID_BASE - 1))); // one world ID left to hand out
    CHECK(Loads(with(SCENE_AT, core::SCENE_ID_END - 1)));  // one scene ID left to hand out
    CHECK_FALSE(Loads(with(WORLD_AT, 0)));
    CHECK_FALSE(Loads(with(WORLD_AT, core::SCENE_ID_BASE))); // its next ID would be the scene range's first
    CHECK_FALSE(Loads(with(WORLD_AT, core::SCENE_ID_BASE + 1)));
    CHECK_FALSE(Loads(with(SCENE_AT, core::SCENE_ID_BASE - 1)));
    CHECK_FALSE(Loads(with(SCENE_AT, core::SCENE_ID_END)));
    CHECK_FALSE(Loads(with(SCENE_AT, std::numeric_limits<u64>::max()))); // would wrap round to the world's IDs
}

TEST_CASE("a save whose player doesn't exist is refused, and the game is unchanged") {
    constexpr core::StableId NOBODY{ 12'345 };
    const core::State state{ game::DOMAINS, STATE_SEED };
    const bool loaded = Loads(ArenaSave(state, NOBODY));
    CHECK_FALSE(loaded);
}

TEST_CASE("a save whose player has no body is refused, and the game is unchanged") {
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId player = state.Create(game::ACTIVE, core::IdRange::Scene).first;
    const bool loaded = Loads(ArenaSave(state, player));
    CHECK_FALSE(loaded);
}

TEST_CASE("a save with a creature that has no look is refused, and the game is unchanged") {
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::Entity entity = state.Create(game::ACTIVE, core::IdRange::Scene).second;
    state[game::ACTIVE].Entities().emplace<game::Body>(entity);
    state[game::ACTIVE].Entities().emplace<game::Creature>(entity);
    const bool loaded = Loads(ArenaSave(state));
    CHECK_FALSE(loaded);
}

// A fox as the arena makes one, standing at the origin, with `health`.
core::Entity SpawnFox(core::State& state, game::Health health) {
    const core::Entity entity = state.Create(game::ACTIVE, core::IdRange::Scene).second;
    state[game::ACTIVE].Entities().emplace<game::Body>(entity);
    state[game::ACTIVE].Entities().emplace<game::Look>(entity);
    state[game::ACTIVE].Entities().emplace<game::Creature>(entity, game::CreatureKind::Fox);
    state[game::ACTIVE].Entities().emplace<game::Health>(entity, health);
    return entity;
}

TEST_CASE("a save with a fox as the arena makes one is loaded") {
    core::State state{ game::DOMAINS, STATE_SEED };
    SpawnFox(state, { .current = FOX_HEALTH, .max = FOX_HEALTH });
    const std::vector<std::byte> bytes = ArenaSave(state);
    core::Reader reader{ bytes };
    game::Host game(test::TestManifest(), game::ARENA_SCENE);
    CHECK(game.Load(reader, SAVED_TICK));
}

TEST_CASE("a save with health above its most is refused, and the game is unchanged") {
    core::State state{ game::DOMAINS, STATE_SEED };
    SpawnFox(state, { .current = FOX_HEALTH + 1, .max = FOX_HEALTH });
    const bool loaded = Loads(ArenaSave(state));
    CHECK_FALSE(loaded);
}

TEST_CASE("a save with a creature that has no health is refused, and the game is unchanged") {
    core::State state{ game::DOMAINS, STATE_SEED };
    SpawnFox(state, { .current = FOX_HEALTH, .max = FOX_HEALTH });
    state[game::ACTIVE].Entities().clear<game::Health>();
    const bool loaded = Loads(ArenaSave(state));
    CHECK_FALSE(loaded);
}

TEST_CASE("a save with a creature at no health that isn't dying, or dying with health left, is refused") {
    core::State dead{ game::DOMAINS, STATE_SEED };
    SpawnFox(dead, { .current = 0, .max = FOX_HEALTH });
    const bool deadLoaded = Loads(ArenaSave(dead));
    CHECK_FALSE(deadLoaded);

    core::State dying{ game::DOMAINS, STATE_SEED };
    SpawnFox(dying, { .current = FOX_HEALTH, .max = FOX_HEALTH });
    core::Registry& entities = dying[game::ACTIVE].Entities();
    entities.emplace<game::Dying>(entities.view<const game::Health>().front());
    const bool dyingLoaded = Loads(ArenaSave(dying));
    CHECK_FALSE(dyingLoaded);
}

TEST_CASE("a save with a caster that has no health, or a dash that has no body, is refused") {
    core::State caster{ game::DOMAINS, STATE_SEED };
    caster[game::ACTIVE].Entities().emplace<game::Caster>(caster.Create(game::ACTIVE, core::IdRange::Scene).second);
    const bool casterLoaded = Loads(ArenaSave(caster));
    CHECK_FALSE(casterLoaded);

    core::State dashing{ game::DOMAINS, STATE_SEED };
    dashing[game::ACTIVE].Entities().emplace<game::Dashing>(
        dashing.Create(game::ACTIVE, core::IdRange::Scene).second,
        game::Dashing{ .from = {}, .to = {}, .startTick = 0, .ticks = 1 }
    );
    const bool dashingLoaded = Loads(ArenaSave(dashing));
    CHECK_FALSE(dashingLoaded);
}

// A volley of ten swords at the point, as a cast resolves one.
SavedVolley PointVolley() {
    constexpr u64 START = 3;
    constexpr u32 COUNT = 10;
    constexpr u32 FLIGHT = 20;
    constexpr core::Vec2 POINT{ .x = 100.0F, .y = 50.0F };
    return {
        .volley = { .use = 1,
                    .startTick = START,
                    .seed = 1,
                    .origin = {},
                    .point = POINT,
                    .shape = { .count = COUNT, .flightTicks = FLIGHT, .commitTicks = core::MIN_COMMIT_TICKS } },
        .targets = { { .entity = {}, .previous = POINT, .current = POINT, .diedTick = START } },
    };
}

TEST_CASE("a save with a volley as a cast makes one is loaded") {
    const core::State state{ game::DOMAINS, STATE_SEED };
    const std::vector<std::byte> bytes = ArenaSave(state, {}, PointVolley());
    core::Reader reader{ bytes };
    game::Host game(test::TestManifest(), game::ARENA_SCENE);
    CHECK(game.Load(reader, SAVED_TICK));
}

TEST_CASE("a save with a volley whose commit window is unresolved, with no targets, or aiming at a living point, is refused") {
    const core::State state{ game::DOMAINS, STATE_SEED };
    SavedVolley unresolved = PointVolley();
    unresolved.volley.shape.commitTicks = 0;
    const bool unresolvedLoaded = Loads(ArenaSave(state, {}, unresolved));
    CHECK_FALSE(unresolvedLoaded);

    SavedVolley untargeted = PointVolley();
    untargeted.targets.clear();
    const bool untargetedLoaded = Loads(ArenaSave(state, {}, untargeted));
    CHECK_FALSE(untargetedLoaded);

    SavedVolley living = PointVolley();
    living.targets.front().diedTick = 0;
    const bool livingLoaded = Loads(ArenaSave(state, {}, living));
    CHECK_FALSE(livingLoaded);
}

TEST_CASE("a save with afflictions on something with no health is refused") {
    core::State state{ game::DOMAINS, STATE_SEED };
    state[game::ACTIVE].Entities().emplace<game::Afflictions>(state.Create(game::ACTIVE, core::IdRange::Scene).second);
    const bool loaded = Loads(ArenaSave(state));
    CHECK_FALSE(loaded);
}

// PointVolley() aiming instead at the living `target`.
SavedVolley AimedAt(core::StableId target) {
    SavedVolley volley = PointVolley();
    volley.targets.front() = {
        .entity = target, .previous = volley.volley.point, .current = volley.volley.point, .diedTick = 0
    };
    return volley;
}

TEST_CASE("a save with a volley aiming at a living target that is no creature here is refused") {
    constexpr core::StableId MISSING{ 12'345 };
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId bare = state.Create(game::ACTIVE, core::IdRange::Scene).first; // no body, no look: nothing to aim at
    const bool bareLoaded = Loads(ArenaSave(state, {}, AimedAt(bare)));
    CHECK_FALSE(bareLoaded);
    const bool missingLoaded = Loads(ArenaSave(state, {}, AimedAt(MISSING)));
    CHECK_FALSE(missingLoaded);

    const core::Entity fox = SpawnFox(state, { .current = FOX_HEALTH, .max = FOX_HEALTH });
    const std::vector<std::byte> bytes = ArenaSave(state, {}, AimedAt(state[game::ACTIVE].Entities().get<core::StableId>(fox)));
    core::Reader reader{ bytes };
    game::Host game(test::TestManifest(), game::ARENA_SCENE);
    REQUIRE(game.Load(reader, SAVED_TICK));
    game.EnterScene(); // as the logic does at the swap
    std::vector<core::Event> events;
    game.Tick(At(1), {}, events); // follows the fox's chest
}

TEST_CASE("a loaded volley whose arrivals are all overdue still ends its use, once") {
    constexpr u64 LATER = 100; // past every arrival: the volley starts at 3 and flies for 20
    const core::State state{ game::DOMAINS, STATE_SEED };
    const SavedVolley volley = PointVolley();
    const std::vector<std::byte> bytes = ArenaSave(state, {}, volley);
    core::Reader reader{ bytes };
    game::Host game(test::TestManifest(), game::ARENA_SCENE);
    REQUIRE(game.Load(reader, SAVED_TICK));
    game.EnterScene(); // as the logic does at the swap
    std::vector<core::Event> events;
    game.Tick(At(LATER), {}, events);
    game.Tick(At(LATER + 1), {}, events);
    const auto ended = [&volley](const core::Event& event) {
        return event.kind == core::EventKind::UseEnded && event.use == volley.volley.use;
    };
    CHECK(std::ranges::count_if(events, ended) == 1);
}

TEST_CASE("a save's Qi above the loading game's most is lowered to it") {
    constexpr i64 LOWER = 20;
    game::Host saved(test::TestManifest());
    Tick(saved, At(1), {});
    const std::vector<std::byte> bytes = Saved(saved);
    core::GameData data = game::BuiltInData();
    data.abilities.qiMax = LOWER;
    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, data);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene(); // as the logic does at the swap
    const core::Registry& entities = loaded.State()[game::ACTIVE].Entities();
    CHECK(entities.get<game::Caster>(loaded.State()[game::ACTIVE].Find(loaded.Player())).qi == LOWER);
}

// A save of one fox easing in from `arriving`.
std::vector<std::byte> ArrivingSave(game::Arriving arriving) {
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::Entity fox = SpawnFox(state, { .current = FOX_HEALTH, .max = FOX_HEALTH });
    state[game::ACTIVE].Entities().emplace<game::Orbit>(fox);
    state[game::ACTIVE].Entities().emplace<game::Arriving>(fox, arriving);
    return ArenaSave(state);
}

TEST_CASE("a save with an arrival of no ticks, or from NaN, is refused, and the game is unchanged") {
    constexpr u32 ARRIVAL = 180;
    constexpr f32 NOT_A_NUMBER = std::numeric_limits<f32>::quiet_NaN();
    const std::vector<std::byte> sound = ArrivingSave({ .from = {}, .startTick = 0, .ticks = ARRIVAL });
    core::Reader reader{ sound };
    game::Host game(test::TestManifest(), game::ARENA_SCENE);
    CHECK(game.Load(reader, SAVED_TICK));
    const bool noTicks = Loads(ArrivingSave({ .from = {}, .startTick = 0, .ticks = 0 }));
    CHECK_FALSE(noTicks);
    const bool fromNowhere =
        Loads(ArrivingSave({ .from = { .x = NOT_A_NUMBER, .y = 0.0F }, .startTick = 0, .ticks = ARRIVAL }));
    CHECK_FALSE(fromNowhere);
}

TEST_CASE("a save with a world NPC that has no needs is refused, and the game is unchanged") {
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId npc = game::SpawnNpc(state, NPC_CELL);
    state[game::WORLD].Entities().remove<game::Needs>(state[game::WORLD].Find(npc));

    const bool loaded = Loads(ArenaSave(state));
    CHECK_FALSE(loaded);
}

TEST_CASE("a save with a group of billions of members is refused, and the game is unchanged") {
    constexpr u32 MEMBERS = 4'000'000'000U;
    core::State state{ game::DOMAINS, STATE_SEED };
    game::SpawnGroup(state, NPC_CELL, MEMBERS);

    const bool loaded = Loads(ArenaSave(state));
    CHECK_FALSE(loaded);
}

TEST_CASE("a save with an active NPC standing at NaN is refused, and the game is unchanged") {
    constexpr f32 NOT_A_NUMBER = std::numeric_limits<f32>::quiet_NaN();
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId npc = game::SpawnNpc(state, NPC_CELL);
    game::Activate(state, npc);
    state[game::ACTIVE].Entities().get<game::Body>(state[game::ACTIVE].Find(npc)).current = { .x = NOT_A_NUMBER,
                                                                                              .y = NOT_A_NUMBER };

    const bool loaded = Loads(ArenaSave(state));
    CHECK_FALSE(loaded);
}

TEST_CASE("a save with an active NPC walking towards NaN is refused, and the game is unchanged") {
    constexpr f32 NOT_A_NUMBER = std::numeric_limits<f32>::quiet_NaN();
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId npc = game::SpawnNpc(state, NPC_CELL);
    game::Activate(state, npc);
    state[game::ACTIVE].Entities().get<game::Wander>(state[game::ACTIVE].Find(npc)).target = { .x = NOT_A_NUMBER, .y = 0.0F };

    const bool loaded = Loads(ArenaSave(state));
    CHECK_FALSE(loaded);
}

TEST_CASE("a save with needs outside 0 to 1 is refused, and the game is unchanged") {
    constexpr f32 TOO_LONELY = 2.0F;
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId npc = game::SpawnNpc(state, NPC_CELL);
    state[game::WORLD].Entities().get<game::Needs>(state[game::WORLD].Find(npc)).company = TOO_LONELY;

    const bool loaded = Loads(ArenaSave(state));
    CHECK_FALSE(loaded);
}

TEST_CASE("a save with something drawn but standing nowhere is refused, and the game is unchanged") {
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::Entity entity = state.Create(game::ACTIVE, core::IdRange::Scene).second;
    state[game::ACTIVE].Entities().emplace<game::Look>(entity);

    const bool loaded = Loads(ArenaSave(state));
    CHECK_FALSE(loaded);
}

TEST_CASE("a save with a flash that starts after the save's tick is refused") {
    constexpr u64 STARTED = 40;
    constexpr core::Vec2 POINT{ .x = 100.0F, .y = 50.0F };
    const core::State state{ game::DOMAINS, STATE_SEED };
    const std::vector<std::byte> bytes = ArenaSave(
        state,
        {},
        std::nullopt,
        core::Flash{ .point = POINT, .startTick = STARTED, .ticks = RULES.flashTicks, .element = core::HitKind::Poison }
    );
    game::Host game(test::TestManifest(), game::ARENA_SCENE);
    core::Reader reader{ bytes };
    CHECK(game.Load(reader, STARTED));
    const bool before = Loads(bytes, STARTED - 1);
    CHECK_FALSE(before);
}

TEST_CASE("a save with a seal of no size, ending before it starts, at NaN or of an unknown element is refused") {
    constexpr core::Seal SOUND{ .key = 1,
                                .point = { .x = 100.0F, .y = 50.0F },
                                .size = 420.0F,
                                .startTick = 10,
                                .endTick = 40,
                                .element = core::HitKind::Damage,
                                .hasElement = true };
    constexpr core::Seal NEVER_ENDING{ .key = 1, .size = 1.0F, .startTick = 10, .endTick = core::NEVER_ENDS };
    constexpr f32 NOT_A_NUMBER = std::numeric_limits<f32>::quiet_NaN();
    const core::State state{ game::DOMAINS, STATE_SEED };
    const auto saveOf = [&state](const core::Seal& seal) { return ArenaSave(state, {}, std::nullopt, std::nullopt, seal); };
    const auto loads = [&saveOf](const core::Seal& seal) { return Loads(saveOf(seal)); };
    for (const core::Seal& sound : { SOUND, NEVER_ENDING }) {
        game::Host game(test::TestManifest(), game::ARENA_SCENE);
        const std::vector<std::byte> bytes = saveOf(sound);
        core::Reader reader{ bytes };
        CHECK(game.Load(reader, SAVED_TICK));
    }
    core::Seal broken = SOUND;
    broken.size = 0.0F;
    CHECK_FALSE(loads(broken));
    broken = SOUND;
    broken.size = NOT_A_NUMBER;
    CHECK_FALSE(loads(broken));
    broken = SOUND;
    broken.endTick = broken.startTick - 1;
    CHECK_FALSE(loads(broken));
    broken = SOUND;
    broken.point.x = NOT_A_NUMBER;
    CHECK_FALSE(loads(broken));
    broken = SOUND;
    broken.element = static_cast<core::HitKind>(core::HIT_KINDS);
    CHECK_FALSE(loads(broken));
}

TEST_CASE("a save with a seal that starts after its tick is refused, and one its tick already ended is loaded") {
    constexpr core::Seal JUST_STARTED{ .key = 1, .size = 1.0F, .startTick = SAVED_TICK, .endTick = SAVED_TICK + 1 };
    constexpr core::Seal ENDED{ .key = 1, .size = 1.0F, .startTick = 10, .endTick = 40 }; // harmless: the next tick removes it
    constexpr core::Seal FROM_LATER{ .key = 1, .size = 1.0F, .startTick = SAVED_TICK + 1, .endTick = core::NEVER_ENDS };
    const core::State state{ game::DOMAINS, STATE_SEED };
    const auto saveOf = [&state](const core::Seal& seal) { return ArenaSave(state, {}, std::nullopt, std::nullopt, seal); };
    for (const core::Seal& sound : { JUST_STARTED, ENDED }) { // the boundary, started on the save's own tick, and the past
        game::Host game(test::TestManifest(), game::ARENA_SCENE);
        const std::vector<std::byte> bytes = saveOf(sound);
        core::Reader reader{ bytes };
        CHECK(game.Load(reader, SAVED_TICK));
    }
    const bool fromLater = Loads(saveOf(FROM_LATER));
    CHECK_FALSE(fromLater);
}

TEST_CASE("a save naming the world scene is refused by a host without the world map, and loaded by one with it") {
    const core::GameData builtIn = game::BuiltInData();
    const std::vector<std::byte> bytes = WorldSaveAt({}, &builtIn);
    game::Host without(test::TestManifest(), game::ARENA_SCENE, builtIn);
    core::Reader refused{ bytes };
    CHECK_FALSE(without.Load(refused, SAVED_TICK));
    CHECK_FALSE(without.Requested().has_value());
    const core::GameData withMap = test::SmallData();
    game::Host with(test::TestManifest(), game::ARENA_SCENE, withMap);
    const std::vector<std::byte> mapped = WorldSaveAt({}, &withMap);
    core::Reader accepted{ mapped };
    CHECK(with.Load(accepted, SAVED_TICK));
}

// The index of the first place of `kind` on `map`.
u16 FirstPlace(const core::WorldMap& map, core::PlaceKind kind) {
    const auto places = map.Places();
    const auto found = std::ranges::find(places, kind, &core::Place::kind);
    REQUIRE(found != places.end());
    return static_cast<u16>(found - places.begin());
}

// A save of the world scene holding one world NPC in `cell`, homed at `home`; `worldVersion` as it was written.
std::vector<std::byte> WorldSaveOf(u32 cell, u16 home, u32 worldVersion = WORLD_VERSION, u32 activeVersion = ACTIVE_VERSION) {
    core::State state{ game::DOMAINS, STATE_SEED };
    static_cast<void>(game::SpawnNpc(state, cell, home));
    return ArenaSave(state, {}, std::nullopt, std::nullopt, std::nullopt, game::WORLD_SCENE, worldVersion, activeVersion);
}

// Whether a host with the small test map loads `bytes`.
bool LoadsWithMap(const std::vector<std::byte>& bytes) {
    game::Host game(test::TestManifest(), game::ARENA_SCENE, test::SmallData());
    core::Reader reader{ bytes };
    return game.Load(reader, SAVED_TICK);
}

TEST_CASE("a world save round-trips the NPCs' homes") {
    game::Host saved(test::TestManifest(), game::WORLD_SCENE, test::SmallData());
    const std::vector<std::byte> bytes = Saved(saved);
    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, test::SmallData());
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene();
    CHECK(loaded.StateHash() == saved.StateHash());
    // The homes themselves, not only the hash: each NPC is homed and placed as it was.
    const core::Registry& was = saved.State()[game::WORLD].Entities();
    const core::Registry& now = loaded.State()[game::WORLD].Entities();
    bool same = was.view<const game::Npc>().size() == now.view<const game::Npc>().size();
    for (const auto [entity, stable, person] : was.view<const core::StableId, const game::Npc>().each()) {
        const game::Npc& other = now.get<game::Npc>(loaded.State()[game::WORLD].Find(stable));
        same = same && other.home == person.home && other.cell == person.cell && person.home != game::NO_HOME;
    }
    CHECK(same);
}

TEST_CASE("a version-1 to version-4 world save is refused") {
    const std::shared_ptr<const core::WorldMap> map = test::SmallWorld();
    const u16 city = FirstPlace(*map, core::PlaceKind::City);
    const u32 cell = map->TileIndex(map->Places()[city].tile);
    CHECK(LoadsWithMap(WorldSaveOf(cell, city)));
    constexpr u32 FIRST_VERSION = 1;
    constexpr u32 BEFORE_TRAVEL = 2;
    constexpr u32 WHOLE_WORLD = 3;
    constexpr u32 UNORDERED = 4;
    CHECK_FALSE(LoadsWithMap(WorldSaveOf(cell, city, FIRST_VERSION)));
    CHECK_FALSE(LoadsWithMap(WorldSaveOf(cell, city, BEFORE_TRAVEL)));
    CHECK_FALSE(LoadsWithMap(WorldSaveOf(cell, city, WHOLE_WORLD)));
    CHECK_FALSE(LoadsWithMap(WorldSaveOf(cell, city, UNORDERED)));
}

TEST_CASE("a version-4 active section is refused") {
    const std::shared_ptr<const core::WorldMap> map = test::SmallWorld();
    const u16 city = FirstPlace(*map, core::PlaceKind::City);
    const u32 cell = map->TileIndex(map->Places()[city].tile);
    constexpr u32 OLD_VERSION = 4;
    CHECK_FALSE(LoadsWithMap(WorldSaveOf(cell, city, WORLD_VERSION, OLD_VERSION)));
    const core::State state{ game::DOMAINS, STATE_SEED };
    CHECK_FALSE(
        Loads(ArenaSave(state, {}, std::nullopt, std::nullopt, std::nullopt, game::ARENA_SCENE, WORLD_VERSION, OLD_VERSION))
    );
}

TEST_CASE("a world save naming a home past the map's cities is refused") {
    const std::shared_ptr<const core::WorldMap> map = test::SmallWorld();
    const u16 city = FirstPlace(*map, core::PlaceKind::City);
    const u32 cell = map->TileIndex(map->Places()[city].tile);
    const u16 pastPlaces = static_cast<u16>(map->Places().size());
    const u16 danger = FirstPlace(*map, core::PlaceKind::Danger);
    CHECK_FALSE(LoadsWithMap(WorldSaveOf(cell, pastPlaces)));
    CHECK_FALSE(LoadsWithMap(WorldSaveOf(cell, danger)));
    CHECK_FALSE(LoadsWithMap(WorldSaveOf(cell, game::NO_HOME)));
}

TEST_CASE("a world save with an NPC on a tile past the map is refused") {
    const std::shared_ptr<const core::WorldMap> map = test::SmallWorld();
    const u16 city = FirstPlace(*map, core::PlaceKind::City);
    const u32 tiles = map->Columns() * map->Rows();
    CHECK(LoadsWithMap(WorldSaveOf(tiles - 1, city)));
    CHECK_FALSE(LoadsWithMap(WorldSaveOf(tiles, city)));
}

TEST_CASE("an arena save still loads, its NPCs homeless, and one with a home is refused") {
    core::State homeless{ game::DOMAINS, STATE_SEED };
    static_cast<void>(game::SpawnNpc(homeless, NPC_CELL));
    CHECK(Loads(ArenaSave(homeless)));
    core::State homed{ game::DOMAINS, STATE_SEED };
    constexpr u16 SOME_HOME = 0;
    static_cast<void>(game::SpawnNpc(homed, NPC_CELL, SOME_HOME));
    CHECK_FALSE(Loads(ArenaSave(homed)));
}

TEST_CASE("the clock and the pause are saved and loaded") {
    constexpr u64 WALKED = 7;
    core::Jobs jobs{ 1 };
    game::Host saved(test::TestManifest(), game::WORLD_SCENE, test::SmallData());
    saved.AdvanceCalendar(At(1), jobs, {});
    for (u64 tick = 2; tick < 2 + WALKED; ++tick) {
        Tick(saved, At(tick), {});
    }
    const std::array<core::Action, 1> pause{ { { .kind = core::ActionKind::ToggleWorldPause } } };
    Tick(saved, At(2 + WALKED), { .actions = pause, .held = {} });
    REQUIRE(saved.WorldPaused());
    const std::vector<std::byte> bytes = Saved(saved);

    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, test::SmallData());
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    CHECK(loaded.WorldTicks() == 0); // held until the swap
    loaded.EnterScene();
    CHECK(loaded.WorldTicks() == game::TICKS_PER_MONTH + WALKED);
    CHECK(loaded.Months() == 1);
    CHECK(loaded.WorldPaused());
    CHECK(loaded.StateHash() == saved.StateHash());

    // Resumed alike, both clocks run on alike.
    Tick(saved, At(3 + WALKED), { .actions = pause, .held = {} });
    Tick(loaded, At(3 + WALKED), { .actions = pause, .held = {} });
    Tick(saved, At(4 + WALKED), {});
    Tick(loaded, At(4 + WALKED), {});
    CHECK(loaded.WorldTicks() == game::TICKS_PER_MONTH + WALKED + 2); // from the resuming tick on
    CHECK(loaded.StateHash() == saved.StateHash());
}

TEST_CASE("a save whose clock doesn't fit its month steps is refused") {
    constexpr u64 MONTH = game::TICKS_PER_MONTH;
    CHECK(LoadsWithMap(WorldSaveAt({ .ticks = (2 * MONTH) + 1, .months = 2 })));
    CHECK(LoadsWithMap(WorldSaveAt({ .ticks = 2 * MONTH, .months = 1 })));             // its month due, saved before the step
    CHECK(LoadsWithMap(WorldSaveAt({ .ticks = 2 * MONTH, .months = 2 })));             // the step just run
    CHECK_FALSE(LoadsWithMap(WorldSaveAt({ .ticks = (2 * MONTH) + 1, .months = 1 }))); // past a month due
    CHECK_FALSE(LoadsWithMap(WorldSaveAt({ .ticks = (2 * MONTH) - 1, .months = 2 }))); // a step run too early
    CHECK_FALSE(LoadsWithMap(WorldSaveAt({ .ticks = 0, .months = ~u64{ 0 } })));       // the most steps
    CHECK_FALSE(LoadsWithMap(WorldSaveAt({ .ticks = ~u64{ 0 }, .months = 0 })));       // the most ticks
    // The calendar ends at its last month's first tick: a clock there loads, one past it doesn't.
    constexpr u64 CALENDAR_END = game::LAST_MONTH * MONTH;
    CHECK(LoadsWithMap(WorldSaveAt({ .ticks = CALENDAR_END, .months = game::LAST_MONTH })));
    CHECK(LoadsWithMap(WorldSaveAt({ .ticks = CALENDAR_END, .months = game::LAST_MONTH - 1 }))); // its last step due
    CHECK_FALSE(LoadsWithMap(WorldSaveAt({ .ticks = ~u64{ 0 }, .months = game::LAST_MONTH })));  // in the last month
}

TEST_CASE("a save paused off the world map is refused, and one paused on it is loaded") {
    const core::State state{ game::DOMAINS, STATE_SEED };
    CHECK(LoadsWithMap(WorldSaveAt({ .paused = true })));
    const std::vector<std::byte> arena = ArenaSave(
        state,
        {},
        std::nullopt,
        std::nullopt,
        std::nullopt,
        game::ARENA_SCENE,
        WORLD_VERSION,
        ACTIVE_VERSION,
        { .paused = true }
    );
    CHECK_FALSE(Loads(arena));
}

// A save of the arena whose game's world, set aside, is empty but for `setAside`, written against the world a game on
// `data` generates (on the built-in data when null).
std::vector<std::byte> ArenaSetAside(const test::SetAside& setAside, const core::GameData* data = nullptr) {
    const core::State state{ game::DOMAINS, STATE_SEED };
    return ArenaSave(
        state,
        {},
        std::nullopt,
        std::nullopt,
        std::nullopt,
        game::ARENA_SCENE,
        WORLD_VERSION,
        ACTIVE_VERSION,
        {},
        game::Travel{},
        std::nullopt,
        game::Relationships{},
        setAside,
        data
    );
}

TEST_CASE("a game's world set aside paused or keeping a place on the map is refused by a game without the map") {
    const core::GameData withMap = test::SmallData();
    const core::Vec2 east = withMap.world->StartCentre() + core::Vec2{ .x = 256.0F, .y = 0.0F };
    CHECK(LoadsWithMap(ArenaSetAside({ .paused = true }, &withMap)));
    CHECK_FALSE(Loads(ArenaSetAside({ .paused = true })));
    CHECK(LoadsWithMap(ArenaSetAside({ .place = east }, &withMap)));
    CHECK_FALSE(Loads(ArenaSetAside({ .place = east })));
}

TEST_CASE("a kept place on the map that isn't a finite point, or that lies on the portal, is refused") {
    const core::GameData withMap = test::SmallData();
    const core::Vec2 start = withMap.world->StartCentre();
    const core::Vec2 portal = start + game::WORLD_PORTAL_OFFSET;
    constexpr f32 NOT_A_NUMBER = std::numeric_limits<f32>::quiet_NaN();
    constexpr f32 INFINITE = std::numeric_limits<f32>::infinity();
    const auto keeping = [&withMap](core::Vec2 place) { return ArenaSetAside({ .place = place }, &withMap); };
    CHECK_FALSE(LoadsWithMap(keeping(core::Vec2{ .x = NOT_A_NUMBER, .y = start.y })));
    CHECK_FALSE(LoadsWithMap(keeping(core::Vec2{ .x = start.x, .y = INFINITE })));
    CHECK_FALSE(LoadsWithMap(keeping(portal)));
    // Either side of the step area's rim, across.
    CHECK_FALSE(LoadsWithMap(keeping(portal + core::Vec2{ .x = 0.9F * game::PORTAL_RADIUS, .y = 0.0F })));
    CHECK(LoadsWithMap(keeping(portal + core::Vec2{ .x = 1.1F * game::PORTAL_RADIUS, .y = 0.0F })));
}

TEST_CASE("a save whose relationship names a world NPC brought into the scene loads") {
    const std::shared_ptr<const core::WorldMap> map = test::SmallWorld();
    const u16 city = map->Start();
    const u32 tile = map->TileIndex(map->Places()[city].tile);
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId holder = game::SpawnNpc(state, tile, city);
    const core::StableId subject = game::SpawnNpc(state, tile, city);
    state.Move(
        subject, game::ACTIVE, [&](const core::Registry& from, core::Entity old, core::Registry& into, core::Entity moved) {
            into.emplace<game::Npc>(moved, from.get<game::Npc>(old));
            into.emplace<game::Needs>(moved, from.get<game::Needs>(old));
            const core::Vec2 foot = map->TileCentre(tile);
            into.emplace<game::Body>(moved, foot, foot);
        }
    );
    game::Relationships relationships;
    const std::array<std::pair<core::StableId, core::StableId>, 1> met{ { { holder, subject } } };
    relationships.Merge(met, 0);
    const std::vector<std::byte> bytes = ArenaSave(
        state,
        {},
        std::nullopt,
        std::nullopt,
        std::nullopt,
        game::WORLD_SCENE,
        WORLD_VERSION,
        ACTIVE_VERSION,
        {},
        game::Travel{},
        std::nullopt,
        relationships
    );
    CHECK(LoadsWithMap(bytes));
}

/// A save of the world scene holding `state` and `relationships`, as the small test map's game loads it.
std::vector<std::byte> WorldSceneSave(const core::State& state, const game::Relationships& relationships = {}) {
    return ArenaSave(
        state,
        {},
        std::nullopt,
        std::nullopt,
        std::nullopt,
        game::WORLD_SCENE,
        WORLD_VERSION,
        ACTIVE_VERSION,
        {},
        game::Travel{},
        std::nullopt,
        relationships
    );
}

TEST_CASE("a save whose scene holds a world ID that isn't an NPC is refused") {
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::Entity stray = state.Create(game::ACTIVE, core::IdRange::World).second;
    state[game::ACTIVE].Entities().emplace<game::Body>(stray);
    state[game::ACTIVE].Entities().emplace<game::Look>(stray);
    CHECK_FALSE(LoadsWithMap(WorldSceneSave(state)));
}

TEST_CASE("a save whose world holds a scene ID is refused") {
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::Entity stray = state.Create(game::WORLD, core::IdRange::Scene).second;
    state[game::WORLD].Entities().emplace<game::Body>(stray);
    CHECK_FALSE(LoadsWithMap(WorldSceneSave(state)));
}

TEST_CASE("a save whose relationship names a scene ID is refused") {
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId holder = game::SpawnNpc(state, NPC_CELL, FirstPlace(*test::SmallData().world, core::PlaceKind::City));
    const auto [cultivator, entity] = state.Create(game::ACTIVE, core::IdRange::Scene);
    state[game::ACTIVE].Entities().emplace<game::Body>(entity);
    game::Relationships relationships;
    const std::array<std::pair<core::StableId, core::StableId>, 1> met{ { { holder, cultivator } } };
    relationships.Merge(met, 0);
    CHECK_FALSE(LoadsWithMap(WorldSceneSave(state, relationships)));
}

// A day in a journey's signed ticks.
constexpr i64 DAY = static_cast<i64>(core::TICKS_PER_DAY);
// Stands in for the game's journey draws: a stay's seed is keyed on the state's seed, as every draw is.
constexpr u32 STAY_PURPOSE = 0x7e57;

/// `map`'s first city and the next city after it, and the walk from the first's tile to the next's.
struct Trip {
    u16 home = core::NO_PLACE;
    u16 destination = core::NO_PLACE;
    std::vector<u32> route;
};

Trip FirstTrip(const core::WorldMap& map) {
    Trip trip;
    trip.home = FirstPlace(map, core::PlaceKind::City);
    const auto places = map.Places();
    const auto next = std::find_if(places.begin() + trip.home + 1, places.end(), [](const core::Place& place) {
        return place.kind == core::PlaceKind::City;
    });
    REQUIRE(next != places.end());
    trip.destination = static_cast<u16>(next - places.begin());
    trip.route = core::FindRoute(
        map, core::WALKING, map.TileIndex(places[trip.home].tile), map.TileIndex(places[trip.destination].tile)
    );
    REQUIRE(trip.route.size() >= 3);
    return trip;
}

/// `member`'s visit along `trip`, leaving on `departTick` and staying `stayDays`.
core::Journey VisitOf(const core::WorldMap& map, const Trip& trip, core::StableId member, i64 departTick, u32 stayDays) {
    const auto seed =
        static_cast<u32>(core::Random(STATE_SEED, static_cast<u64>(departTick), std::to_underlying(member), STAY_PURPOSE, 0));
    return core::Visit(map, trip.route, member, trip.home, trip.destination, departTick, stayDays, seed);
}

/// A world NPC of `state` at home on `trip`'s first city, travelling.
core::StableId TravellerOn(core::State& state, const core::WorldMap& map, const Trip& trip) {
    const core::StableId npc = game::SpawnNpc(state, map.TileIndex(map.Places()[trip.home].tile), trip.home);
    state[game::WORLD].Entities().get<game::Npc>(state[game::WORLD].Find(npc)).action = game::Action::Travel;
    return npc;
}

/// A save of the world scene holding `state` and `travel`, its world clock at `ticks`.
std::vector<std::byte> TravelSave(const core::State& state, const game::Travel& travel, u64 ticks = 0) {
    return ArenaSave(
        state,
        {},
        std::nullopt,
        std::nullopt,
        std::nullopt,
        game::WORLD_SCENE,
        WORLD_VERSION,
        ACTIVE_VERSION,
        { .ticks = ticks },
        travel
    );
}

/// Whether `lhs` and `rhs` have the same bits, as determinism means.
bool SamePoint(core::Vec2 lhs, core::Vec2 rhs) {
    return std::bit_cast<u32>(lhs.x) == std::bit_cast<u32>(rhs.x) && std::bit_cast<u32>(lhs.y) == std::bit_cast<u32>(rhs.y);
}

std::vector<std::byte> TravelBytes(const game::Travel& travel) {
    return Written([&travel](core::Writer& writer) { travel.Save(writer); });
}

TEST_CASE("a world save round-trips travel and the journal, and hashes them") {
    constexpr u32 STAY_DAYS = 2;
    const std::shared_ptr<const core::WorldMap> map = test::SmallWorld();
    const Trip trip = FirstTrip(*map);
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId back = TravellerOn(state, *map, trip);
    const core::StableId away = TravellerOn(state, *map, trip);
    game::Travel travel;
    const core::Journey ended = VisitOf(*map, trip, back, 0, STAY_DAYS);
    travel.Start(ended);
    travel.Finish(core::EndTick(ended), state);
    travel.Start(VisitOf(*map, trip, away, DAY, STAY_DAYS));
    const std::vector<std::byte> bytes = TravelSave(state, travel);

    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, test::SmallData());
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene();
    const bool sameTravel = TravelBytes(loaded.NpcTravel()) == TravelBytes(travel);
    CHECK(sameTravel);
    CHECK(loaded.NpcTravel().JournalOf(back).size() == 1);
    CHECK(loaded.NpcTravel().Travelling(away));

    // Saved again and loaded, it hashes the same.
    game::Host reloaded(test::TestManifest(), game::ARENA_SCENE, test::SmallData());
    const std::vector<std::byte> again = Saved(loaded);
    core::Reader againReader{ again };
    REQUIRE(reloaded.Load(againReader, SAVED_TICK));
    reloaded.EnterScene();
    CHECK(reloaded.StateHash() == loaded.StateHash());

    // The travel is hashed: the same save without the journal entry hashes otherwise. Its journey stays, as a traveller
    // without one is refused.
    game::Host without(test::TestManifest(), game::ARENA_SCENE, test::SmallData());
    game::Travel unwritten;
    unwritten.Start(VisitOf(*map, trip, away, DAY, STAY_DAYS));
    const std::vector<std::byte> bare = TravelSave(state, unwritten);
    core::Reader bareReader{ bare };
    REQUIRE(without.Load(bareReader, SAVED_TICK));
    without.EnterScene();
    CHECK(without.StateHash() != loaded.StateHash());

    // And with the same journal and the journey leaving a day later, otherwise too.
    game::Host later(test::TestManifest(), game::ARENA_SCENE, test::SmallData());
    game::Travel moved;
    const core::Journey sameEnded = VisitOf(*map, trip, back, 0, STAY_DAYS);
    moved.Start(sameEnded);
    moved.Finish(core::EndTick(sameEnded), state);
    moved.Start(VisitOf(*map, trip, away, 2 * DAY, STAY_DAYS));
    const std::vector<std::byte> movedBytes = TravelSave(state, moved);
    core::Reader movedReader{ movedBytes };
    REQUIRE(later.Load(movedReader, SAVED_TICK));
    later.EnterScene();
    CHECK(later.StateHash() != loaded.StateHash());
}

TEST_CASE("a world save naming a traveller who isn't there is refused, and so is travel off the world map") {
    constexpr u32 STAY_DAYS = 2;
    const std::shared_ptr<const core::WorldMap> map = test::SmallWorld();
    const Trip trip = FirstTrip(*map);
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId npc = TravellerOn(state, *map, trip);
    game::Travel travel;
    travel.Start(VisitOf(*map, trip, npc, 0, STAY_DAYS));
    CHECK(LoadsWithMap(TravelSave(state, travel)));

    state.Destroy(npc);
    CHECK_FALSE(LoadsWithMap(TravelSave(state, travel)));

    // The arena shows no world map, so it holds no travel.
    const core::State arena{ game::DOMAINS, STATE_SEED };
    const std::vector<std::byte> arenaBytes = ArenaSave(
        arena, {}, std::nullopt, std::nullopt, std::nullopt, game::ARENA_SCENE, WORLD_VERSION, ACTIVE_VERSION, {}, travel
    );
    CHECK_FALSE(Loads(arenaBytes));
}

TEST_CASE("a save made mid-journey loads to the same positions on the next tick") {
    const std::shared_ptr<const core::WorldMap> map = test::SmallWorld();
    const Trip trip = FirstTrip(*map);
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId walking = TravellerOn(state, *map, trip);
    const core::StableId staying = TravellerOn(state, *map, trip);
    // The walker leaves on tick 0 and is between its second and third tiles at the save; the other set out early enough to
    // be a day and a third into its stay of three.
    constexpr u32 TWO_DAYS = 2;
    constexpr u32 THREE_DAYS = 3;
    const core::Journey walk = VisitOf(*map, trip, walking, 0, TWO_DAYS);
    const core::Leg& out = walk.parts.front().leg;
    const u64 savedAt = (u64{ out.arrive[1] } + u64{ out.arrive[2] }) / 2;
    const i64 outTicks = core::EndTick(out);
    const i64 earlier = static_cast<i64>(savedAt) - outTicks - DAY - (DAY / 3);
    const core::Journey stay = VisitOf(*map, trip, staying, earlier, THREE_DAYS);
    REQUIRE(stay.parts[1].stay);
    REQUIRE(stay.parts[1].rest.startTick < static_cast<i64>(savedAt));
    game::Travel travel;
    travel.Start(walk);
    travel.Start(stay);

    game::Host saved(test::TestManifest(), game::ARENA_SCENE, test::SmallData());
    const std::vector<std::byte> first = TravelSave(state, travel, savedAt);
    core::Reader firstReader{ first };
    REQUIRE(saved.Load(firstReader, SAVED_TICK));
    saved.EnterScene();
    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, test::SmallData());
    const std::vector<std::byte> again = Saved(saved);
    core::Reader reader{ again };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene();

    Tick(saved, At(1), {});
    Tick(loaded, At(1), {});
    REQUIRE(loaded.WorldTicks() == savedAt + 1);
    REQUIRE(saved.WorldTicks() == savedAt + 1);
    const auto next = static_cast<i64>(savedAt + 1);
    REQUIRE(loaded.NpcTravel().Journeys().size() == 2);
    // A road step, the quickest, crosses a 256-unit tile in 64 ticks: no traveller goes further in a tick.
    constexpr f32 ONE_TICK_MOST = 4.0F;
    f32 farthest = 0.0F;
    bool same = true;
    for (std::size_t index = 0; index < 2; ++index) {
        const core::Journey& journey = loaded.NpcTravel().Journeys()[index];
        const core::Journey& original = journey.members.front() == walking ? walk : stay;
        const core::Vec2 now = core::Where(*map, journey, next);
        const core::Vec2 before = core::Where(*map, original, next - 1);
        same = same && SamePoint(core::Where(*map, saved.NpcTravel().Journeys()[index], next), now) &&
               SamePoint(core::Where(*map, original, next), now);
        const core::Vec2 step = now - before;
        farthest = std::max(farthest, std::sqrt((step.x * step.x) + (step.y * step.y)));
    }
    CHECK(same);
    CHECK(farthest <= ONE_TICK_MOST);
}

// How many tiles of the first trip the player's routes below walk.
constexpr std::size_t ROUTE_TILES = 4;

/// The player's route along the first `tiles` tiles of `trip`, set out on world tick `startTick` from its first tile's
/// centre: a journey of `player` alone with one leg.
core::Journey RouteAlong(
    const core::WorldMap& map,
    const Trip& trip,
    core::StableId player,
    std::size_t tiles,
    i64 startTick = 0
) {
    REQUIRE(trip.route.size() >= tiles);
    const std::span<const u32> walked = std::span{ trip.route }.first(tiles);
    core::Journey route;
    route.members = { player };
    route.parts.push_back(
        { .stay = false, .leg = core::MakeTileLeg(map, core::WALKING, startTick, map.TileCentre(walked.front()), walked) }
    );
    return route;
}

/// Whether a host on `data` loads `bytes`.
bool LoadsWith(const core::GameData& data, const std::vector<std::byte>& bytes) {
    game::Host game(test::TestManifest(), game::ARENA_SCENE, data);
    core::Reader reader{ bytes };
    return game.Load(reader, SAVED_TICK);
}

std::vector<std::byte> JourneyBytes(const core::Journey& journey) {
    return Written([&journey](core::Writer& writer) { writer(journey); });
}

TEST_CASE("a world save round-trips the player's route and hashes it, and one no player can walk is refused") {
    const std::shared_ptr<const core::WorldMap> map = test::SmallWorld();
    const Trip trip = FirstTrip(*map);
    REQUIRE(trip.route.size() >= ROUTE_TILES);
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::Vec2 home = map->TileCentre(trip.route.front());
    const auto [player, entity] = state.Create(game::ACTIVE, core::IdRange::Scene);
    state[game::ACTIVE].Entities().emplace<game::Body>(entity, home, home);
    const auto [other, otherEntity] = state.Create(game::ACTIVE, core::IdRange::Scene);
    state[game::ACTIVE].Entities().emplace<game::Body>(otherEntity, home, home);
    const auto save = [&state](
                          const std::optional<core::Journey>& route,
                          core::StableId walker,
                          core::SceneKey scene = game::WORLD_SCENE,
                          const core::GameData* data = nullptr
                      ) {
        return ArenaSave(
            state,
            walker,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            scene,
            WORLD_VERSION,
            ACTIVE_VERSION,
            {},
            {},
            route,
            {},
            {},
            data
        );
    };
    const core::Journey route = RouteAlong(*map, trip, player, ROUTE_TILES);

    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, test::SmallData());
    const std::vector<std::byte> bytes = save(route, player);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene();
    REQUIRE(loaded.PlayerRoute().has_value());
    const bool sameRoute = JourneyBytes(loaded.PlayerRoute().value_or(core::Journey{})) == JourneyBytes(route);
    CHECK(sameRoute);

    // Hashed: the same save without the route, or with it set out a tick later, hashes otherwise.
    game::Host bare(test::TestManifest(), game::ARENA_SCENE, test::SmallData());
    const std::vector<std::byte> bareBytes = save(std::nullopt, player);
    core::Reader bareReader{ bareBytes };
    REQUIRE(bare.Load(bareReader, SAVED_TICK));
    bare.EnterScene();
    CHECK(bare.StateHash() != loaded.StateHash());
    game::Host later(test::TestManifest(), game::ARENA_SCENE, test::SmallData());
    const std::vector<std::byte> laterBytes = save(RouteAlong(*map, trip, player, ROUTE_TILES, 1), player);
    core::Reader laterReader{ laterBytes };
    REQUIRE(later.Load(laterReader, SAVED_TICK));
    later.EnterScene();
    CHECK(later.StateHash() != loaded.StateHash());

    // Refused: off the world map, with no player, someone else's, of no step, a stay, or onto a tile past the map.
    CHECK_FALSE(Loads(save(route, player, game::ARENA_SCENE)));
    CHECK_FALSE(LoadsWithMap(save(route, {})));
    CHECK_FALSE(LoadsWithMap(save(RouteAlong(*map, trip, other, ROUTE_TILES), player)));
    CHECK_FALSE(LoadsWithMap(save(RouteAlong(*map, trip, player, 1), player)));
    core::Journey stay = route;
    stay.parts.front().stay = true;
    CHECK_FALSE(LoadsWithMap(save(stay, player)));
    core::Journey past = route;
    past.parts.front().leg.tiles.back() = map->Columns() * map->Rows();
    CHECK_FALSE(LoadsWithMap(save(past, player)));

    // On a map with a second profile, a route by it is refused where the same route on foot loads.
    const core::GameData twoProfiles = test::SmallData(true);
    REQUIRE(twoProfiles.world->Profiles().size() == 2);
    core::Journey ridden = route;
    ridden.parts.front().leg.profile = core::ProfileIndex{ 1 };
    CHECK(LoadsWith(twoProfiles, save(route, player, game::WORLD_SCENE, &twoProfiles)));
    CHECK_FALSE(LoadsWith(twoProfiles, save(ridden, player, game::WORLD_SCENE, &twoProfiles)));
}

TEST_CASE("a save made mid-route loads to the same position on the next tick") {
    constexpr u64 WALKED = 90; // past the first tile, between two others
    const std::shared_ptr<const core::WorldMap> map = test::SmallWorld();
    const Trip trip = FirstTrip(*map);
    REQUIRE(trip.route.size() >= ROUTE_TILES);
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::Vec2 home = map->TileCentre(trip.route.front());
    const auto [player, entity] = state.Create(game::ACTIVE, core::IdRange::Scene);
    state[game::ACTIVE].Entities().emplace<game::Body>(entity, home, home);
    const std::vector<std::byte> start =
        ArenaSave(state, player, std::nullopt, std::nullopt, std::nullopt, game::WORLD_SCENE, WORLD_VERSION, ACTIVE_VERSION);
    game::Host saved(test::TestManifest(), game::ARENA_SCENE, test::SmallData());
    core::Reader startReader{ start };
    REQUIRE(saved.Load(startReader, SAVED_TICK));
    saved.EnterScene();
    const std::array<core::Action, 1> walk{ { { .kind = core::ActionKind::WalkTo,
                                                .point = map->TileCentre(trip.route[ROUTE_TILES - 1]) } } };
    Tick(saved, At(1), { .actions = walk, .held = {} });
    for (u64 tick = 2; tick <= WALKED; ++tick) {
        Tick(saved, At(tick), {});
    }
    REQUIRE(saved.PlayerRoute().has_value());
    const core::Vec2 there = test::BodyOf(saved, player).current;

    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, test::SmallData());
    const std::vector<std::byte> bytes = Saved(saved);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene();
    CHECK(loaded.StateHash() == saved.StateHash());
    Tick(saved, At(WALKED + 1), {});
    Tick(loaded, At(WALKED + 1), {});
    CHECK(SamePoint(test::BodyOf(loaded, player).previous, there)); // no jump
    CHECK(SamePoint(test::BodyOf(loaded, player).current, test::BodyOf(saved, player).current));
    CHECK_FALSE(SamePoint(test::BodyOf(loaded, player).current, there)); // walking on
    CHECK(loaded.StateHash() == saved.StateHash());
}

TEST_CASE("a new game's world delta changes no NPC") {
    const core::GameData data = test::ShippedData();
    const game::Host host(test::TestManifest(), game::WORLD_SCENE, data);
    core::Paths paths{ data.world };
    const game::WorldState base = test::GeneratedWorld(data, host.State().Seed(), &paths);
    const core::DeltaCounts counts = core::CountDelta(base.state[game::WORLD], host.State()[game::WORLD], World{});
    CHECK(counts.removed == 0);
    CHECK(counts.created == 0);
    CHECK(counts.changed == 0);
    REQUIRE_FALSE(base.travel.Journeys().empty());
    const std::vector<std::byte> journeys =
        Written([&](core::Writer& writer) { host.NpcTravel().SaveDelta(writer, base.travel, data.world.get(), &paths); });
    // The journals are written whole, as relationships are.
    std::vector<u64> gone;
    std::vector<core::VisitPlan> plans;
    std::vector<core::Journey> whole;
    core::Reader reader{ journeys };
    reader(gone, plans, whole);
    REQUIRE_FALSE(reader.Failed());
    CHECK(gone.empty());
    CHECK(plans.empty());
    CHECK(whole.empty());
}

TEST_CASE("a save from another generator or other world data is refused") {
    const core::GameData data = test::SmallData();
    game::Host saved(test::TestManifest(), game::WORLD_SCENE, data);
    const std::vector<std::byte> bytes = Saved(saved);
    // The generator version follows the format, the scene, the scene counter and the seed; the base hash follows it.
    constexpr std::size_t GENERATOR_AT = sizeof(u32) + sizeof(u8) + sizeof(u64) + sizeof(u64);
    constexpr std::size_t BASE_AT = GENERATOR_AT + sizeof(u32);
    const auto patched = [&bytes](std::size_t offset, std::span<const std::byte> patch) {
        std::vector<std::byte> copy = bytes;
        std::ranges::copy(patch, copy.begin() + static_cast<std::ptrdiff_t>(offset));
        return copy;
    };
    const auto generator = [](u32 version) { return std::bit_cast<std::array<std::byte, sizeof(u32)>>(version); };
    CHECK(LoadsWith(data, bytes));
    CHECK_FALSE(LoadsWith(data, patched(GENERATOR_AT, generator(game::GENERATOR_VERSION + 1))));
    CHECK_FALSE(LoadsWith(data, patched(GENERATOR_AT, generator(game::GENERATOR_VERSION - 1))));
    const auto otherBase = std::bit_cast<std::array<std::byte, sizeof(u64)>>(data.BaseHash() + 1);
    CHECK_FALSE(LoadsWith(data, patched(BASE_AT, otherBase)));

    // Another map or other names generate another world; other fights don't.
    core::GameData otherMap = data;
    otherMap.world = test::SmallWorld(true);
    CHECK_FALSE(LoadsWith(otherMap, bytes));
    core::Names names = *data.names;
    names.surnames.front().weight += 1;
    core::GameData otherNames = data;
    otherNames.names = std::make_shared<const core::Names>(std::move(names));
    CHECK_FALSE(LoadsWith(otherNames, bytes));
    core::GameData otherFights = data;
    otherFights.abilities.qiMax += 10;
    otherFights.fox.health += 100;
    CHECK(LoadsWith(otherFights, bytes));
}

TEST_CASE("a save made on the world map mid-journey and mid-stay, loaded after a dev scene, has every traveller in place") {
    constexpr u64 SAVED_AT = 10;
    const std::array<core::Action, 1> cycle{ { { .kind = core::ActionKind::CycleScene } } };
    const core::GameData data = test::ShippedData();
    const core::WorldMap& map = *data.world;
    core::Jobs jobs{ 1 };
    game::Host saved(test::TestManifest(), game::WORLD_SCENE, data);
    for (u64 tick = 1; tick <= SAVED_AT; ++tick) {
        Tick(saved, At(tick), {});
    }
    const auto clock = static_cast<i64>(saved.WorldTicks());
    const auto partOf = [clock](const core::Journey& journey) {
        return std::ranges::find_if(journey.parts, [clock](const core::JourneyPart& part) {
            return core::StartTick(part) <= clock && clock < core::EndTick(part);
        });
    };
    const std::span<const core::Journey> journeys = saved.NpcTravel().Journeys();
    const auto walking = std::ranges::count_if(journeys, [&](const core::Journey& journey) {
        const auto part = partOf(journey);
        return part != journey.parts.end() && !part->stay;
    });
    const auto staying = std::ranges::count_if(journeys, [&](const core::Journey& journey) {
        const auto part = partOf(journey);
        return part != journey.parts.end() && part->stay;
    });
    REQUIRE(walking > 0);
    REQUIRE(staying > 0);
    const std::vector<std::byte> bytes = Saved(saved);
    const u64 savedWorld = saved.WorldHash();

    // A game that went into the arena, where a month passed for its own world, loads the save there.
    game::Host loaded(test::TestManifest(), game::WORLD_SCENE, data);
    Tick(loaded, At(1), { .actions = cycle, .held = {} });
    loaded.EnterScene();
    REQUIRE(loaded.Current().key == game::ARENA_SCENE);
    Tick(loaded, At(2), {});
    loaded.AdvanceCalendar(At(3), jobs, {});
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene();
    CHECK(loaded.WorldHash() == savedWorld);
    CHECK(loaded.StateHash() == saved.StateHash());

    Tick(saved, At(SAVED_AT + 1), {});
    Tick(loaded, At(SAVED_AT + 1), {});
    const auto next = static_cast<i64>(loaded.WorldTicks());
    REQUIRE(loaded.NpcTravel().Journeys().size() == journeys.size());
    u32 misplaced = 0; // counted, so the worst is asserted once
    for (std::size_t index = 0; index < journeys.size(); ++index) {
        const core::Journey& was = saved.NpcTravel().Journeys()[index];
        const core::Journey& now = loaded.NpcTravel().Journeys()[index];
        for (std::size_t member = 0; member < now.members.size(); ++member) {
            const bool same = was.members.size() == now.members.size() && was.members[member] == now.members[member] &&
                              SamePoint(core::Where(map, was, next, member), core::Where(map, now, next, member));
            misplaced += same ? 0U : 1U;
        }
    }
    CHECK(misplaced == 0);
    const std::vector<core::Instance> fromLoaded = Published(loaded, jobs);
    const std::vector<core::Instance> fromSaved = Published(saved, jobs);
    const bool sameFrame = SameBytes(fromLoaded, fromSaved);
    CHECK(sameFrame);
}

TEST_CASE("a data reload mid-game keeps the world and its base, and a save made after it loads") {
    const core::GameData data = test::ShippedData();
    core::Jobs jobs{ 1 };
    game::Host saved(test::TestManifest(), game::WORLD_SCENE, data);
    Tick(saved, At(1), {});
    saved.AdvanceCalendar(At(2), jobs, {});
    Tick(saved, At(3), {});
    const u64 world = saved.WorldHash();

    // As the debug data watch hands over an edited abilities.yaml.
    core::GameData edited = data;
    edited.abilities.qiMax += 25;
    edited.abilities.qiStart += 5;
    REQUIRE(edited.Hash() != data.Hash());
    saved.SetData(edited);
    CHECK(saved.DataHash() == edited.Hash());
    CHECK(saved.WorldHash() == world); // not made again
    CHECK(edited.BaseHash() == data.BaseHash());

    const std::vector<std::byte> bytes = Saved(saved);
    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, edited);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene();
    CHECK(loaded.StateHash() == saved.StateHash());
    CHECK(loaded.WorldHash() == world);
    CHECK(LoadsWith(data, bytes)); // a game on the data before the edit makes the same base
}

TEST_CASE("a world save whose world ID counter is behind its base's is refused, and one at it loads") {
    // A new game's world counter is its base's: the next ID the world made from the seed hands out.
    const core::GameData data = test::SmallData();
    game::Host saved(test::TestManifest(), game::WORLD_SCENE, data);
    const u64 baseNext = saved.State().Ids().next;
    REQUIRE(baseNext > 1); // the base has NPCs, so a counter can fall behind it and still fit its range
    const std::vector<std::byte> sound = Saved(saved);
    // The game world's counter follows the format, the scene, the scene counter, the seed, the generator version and the
    // base hash.
    constexpr std::size_t WORLD_AT = sizeof(u32) + sizeof(u8) + sizeof(u64) + sizeof(u64) + sizeof(u32) + sizeof(u64);
    const auto with = [&sound](u64 next) {
        std::vector<std::byte> bytes = sound;
        std::ranges::copy(
            std::bit_cast<std::array<std::byte, sizeof(u64)>>(next), bytes.begin() + static_cast<std::ptrdiff_t>(WORLD_AT)
        );
        return bytes;
    };
    REQUIRE(with(baseNext) == sound);
    CHECK(LoadsWith(data, with(baseNext)));
    CHECK(LoadsWith(data, with(baseNext + 1)));       // an ID handed out since, its entity gone
    CHECK_FALSE(LoadsWith(data, with(baseNext - 1))); // the base's last NPC's ID would be handed out again
    CHECK_FALSE(LoadsWith(data, with(1)));
}

TEST_CASE("an arena save whose scratch world's ID counter is behind its entry build's is refused, and one at it loads") {
    const core::GameData data = game::BuiltInData(); // no map: the game's world is empty, its section's size known
    game::Host saved(test::TestManifest(), game::ARENA_SCENE, data);
    const u64 builtNext = saved.State().Ids().next; // the entry build's: its active NPCs, then its 5,000 world NPCs
    const std::vector<std::byte> sound = Saved(saved);
    // The scratch world's counter follows the format, the scene, the scene counter and the game's world's section.
    const core::State empty{ game::DOMAINS, saved.State().Seed() }; // the game's world, made from the scratch world's seed
    const std::size_t worldSection = Written([&](core::Writer& writer) {
                                         test::WriteGameWorldSection(writer, empty, data, WORLD_VERSION, {}, game::Travel{});
                                     }).size();
    const std::size_t scratchAt = sizeof(u32) + sizeof(u8) + sizeof(u64) + worldSection;
    const auto with = [&sound, scratchAt](u64 next) {
        std::vector<std::byte> bytes = sound;
        std::ranges::copy(
            std::bit_cast<std::array<std::byte, sizeof(u64)>>(next), bytes.begin() + static_cast<std::ptrdiff_t>(scratchAt)
        );
        return bytes;
    };
    const bool atItsPlace = with(builtNext) == sound;
    REQUIRE(atItsPlace);
    CHECK(LoadsWith(data, with(builtNext)));
    CHECK(LoadsWith(data, with(builtNext + 1)));       // an ID handed out since, its entity gone
    CHECK_FALSE(LoadsWith(data, with(builtNext - 1))); // the entry build's last NPC's ID would be handed out again
    CHECK_FALSE(LoadsWith(data, with(1)));
}

using test::ExploredOf;

TEST_CASE("explored tiles and the sight radius survive a save and load") {
    core::GameData data = test::SmallData();
    data.cultivator.sight = 1;
    const core::WorldMap& map = *data.world;
    game::Host saved(test::TestManifest(), game::WORLD_SCENE, data);
    saved.SetSightRadius(2);
    Tick(saved, At(1), {}); // round the start by the grown radius
    const core::Vec2 far = map.TileCentre(map.TileIndex({ .column = test::small::COLUMNS - 1, .row = 1 }));
    saved.Explore({ .min = far, .max = far }); // a tile out on its own
    const std::vector<std::byte> bytes = Saved(saved);

    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, data);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene();
    CHECK(ExploredOf(loaded) == ExploredOf(saved));
    CHECK(loaded.SightRadius() == 2);
    CHECK(loaded.WorldHash() == saved.WorldHash());

    // A dev scene's save holds them too, with the game's world set aside.
    const std::array<core::Action, 1> cycle{ { { .kind = core::ActionKind::CycleScene } } };
    Tick(saved, At(2), { .actions = cycle, .held = {} });
    saved.EnterScene();
    REQUIRE(saved.Current().key == game::ARENA_SCENE);
    const std::vector<std::byte> aside = Saved(saved);
    game::Host loadedAside(test::TestManifest(), game::ARENA_SCENE, data);
    core::Reader asideReader{ aside };
    REQUIRE(loadedAside.Load(asideReader, SAVED_TICK));
    loadedAside.EnterScene();
    CHECK(ExploredOf(loadedAside) == ExploredOf(saved));
    CHECK(loadedAside.SightRadius() == 2);
    CHECK(loadedAside.WorldHash() == saved.WorldHash());
}

TEST_CASE("a load refuses explored bits of the wrong size") {
    // The small map's 21 tiles fit one word.
    constexpr u32 TILES = test::small::TILES;
    const auto exploring = [](std::vector<u64> explored) { return WorldSaveAt({ .explored = std::move(explored) }); };
    CHECK(LoadsWithMap(exploring({ u64{ 1 } << (TILES - 1) }))); // the last tile
    CHECK_FALSE(LoadsWithMap(exploring({})));                    // a word short
    CHECK_FALSE(LoadsWithMap(exploring({ 0, 0 })));              // a word too many
    CHECK_FALSE(LoadsWithMap(exploring({ u64{ 1 } << TILES }))); // a bit past the last tile
    CHECK_FALSE(LoadsWithMap(exploring({ ~u64{ 0 } })));

    // A dev scene's scratch world never shows the map, so it has no tiles to explore and no sight.
    const core::GameData data = test::SmallData();
    const core::State state{ game::DOMAINS, STATE_SEED };
    const auto scratch = [&](const test::SavedClock& clock) {
        return ArenaSave(
            state,
            {},
            std::nullopt,
            std::nullopt,
            std::nullopt,
            game::ARENA_SCENE,
            WORLD_VERSION,
            ACTIVE_VERSION,
            clock,
            game::Travel{},
            std::nullopt,
            game::Relationships{},
            {},
            &data
        );
    };
    CHECK(LoadsWithMap(scratch({})));
    CHECK_FALSE(LoadsWithMap(scratch({ .explored = std::vector<u64>{ 0 } })));
    CHECK_FALSE(LoadsWithMap(scratch({ .sight = 3 })));
}

TEST_CASE("a load refuses a sight radius past the farthest any sight reaches") {
    CHECK(LoadsWithMap(WorldSaveAt({ .sight = 0 })));
    CHECK(LoadsWithMap(WorldSaveAt({ .sight = core::MAX_SIGHT })));
    CHECK_FALSE(LoadsWithMap(WorldSaveAt({ .sight = core::MAX_SIGHT + 1 })));
    CHECK_FALSE(LoadsWithMap(WorldSaveAt({ .sight = ~u32{ 0 } }))); // where a row plus the radius would wrap
}

} // namespace
