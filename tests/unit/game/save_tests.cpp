#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/state.hpp"
#include "core/tick_clock.hpp"
#include "game/arena.hpp"
#include "game/combat.hpp"
#include "game/components.hpp"
#include "game/npcs.hpp"
#include "support/manifest.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 TICKS = 5;
constexpr u64 MONTHS = 12; // enough for relationships to form

core::TickCounters At(u64 tick) {
    return { .wall = tick, .simulation = tick, .state = tick };
}

// Ticks `arena`, ignoring its events.
void Tick(game::Arena& arena, const core::TickCounters& counters, const core::TickInput& input) {
    std::vector<core::Event> events;
    arena.Tick(counters, input, events);
}

// The stress arena after some ticks and months: every component and the relationships in use.
void Play(game::Arena& arena, core::Jobs& jobs, u64 from) {
    for (u64 tick = from; tick < from + TICKS; ++tick) {
        Tick(arena, At(tick), {});
    }
    for (u64 month = 0; month < MONTHS; ++month) {
        arena.AdvanceCalendar(At(from + TICKS + month), jobs, {});
    }
}

constexpr i64 FOX_HEALTH = 1'200;

// The arena's foxes, in ID order.
std::vector<core::StableId> Foxes(const game::Arena& arena) {
    std::vector<core::StableId> foxes;
    const core::Registry& entities = arena.State()[game::ACTIVE].Entities();
    for (auto [entity, creature, stable] : entities.view<const game::Creature, const core::StableId>().each()) {
        if (creature.kind == game::CreatureKind::Fox) {
            foxes.push_back(stable);
        }
    }
    std::ranges::sort(foxes);
    return foxes;
}

std::vector<std::byte> Saved(game::Arena& arena) {
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    arena.Save(writer);
    return bytes;
}

std::vector<core::Instance> Published(game::Arena& arena, core::Jobs& jobs) {
    core::Snapshot snapshot;
    arena.Publish(snapshot, jobs);
    return snapshot.instances;
}

bool SameBytes(std::span<const core::Instance> lhs, std::span<const core::Instance> rhs) {
    return std::ranges::equal(std::as_bytes(lhs), std::as_bytes(rhs));
}

TEST_CASE("a game loaded from a save goes on exactly as the game that saved") {
    core::Jobs jobs{ 1 };
    game::Arena saved(test::TestManifest(), game::Scene::Stress);
    Play(saved, jobs, 1);
    const std::vector<std::byte> bytes = Saved(saved);
    REQUIRE_FALSE(saved.NpcRelationships().All().empty());

    game::Arena loaded(test::TestManifest(), game::Scene::Arena);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader));
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

TEST_CASE("the player, where it walked, and the selection survive a save") {
    constexpr core::HeldInput RIGHT{ .move = { .x = 1.0F, .y = 0.0F } };
    game::Arena saved(test::TestManifest());
    const std::array<core::Action, 1> select{ { { .kind = core::ActionKind::Select, .target = saved.Player() } } };
    Tick(saved, At(1), { .actions = select, .held = RIGHT });
    const std::vector<std::byte> bytes = Saved(saved);

    game::Arena loaded(test::TestManifest(), game::Scene::Cells);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader));
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
    game::Arena saved(test::TestManifest());
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

    game::Arena loaded(test::TestManifest(), game::Scene::Cells);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader));
    CHECK(loaded.StateHash() == saved.StateHash());
    for (u64 tick = SAVED_AT + 1; tick <= PLAYED_TO; ++tick) {
        Tick(saved, At(tick), {});
        Tick(loaded, At(tick), {});
    }
    CHECK(loaded.StateHash() == saved.StateHash());
    CHECK(Foxes(loaded).size() == Foxes(saved).size());
}

TEST_CASE("a truncated save is refused, and the game is unchanged") {
    constexpr std::size_t CUT = 4;
    game::Arena saved(test::TestManifest(), game::Scene::Stress);
    const std::vector<std::byte> bytes = Saved(saved);

    game::Arena game(test::TestManifest(), game::Scene::Arena);
    const u64 before = game.StateHash();
    core::Reader reader{ std::span{ bytes }.first(bytes.size() - CUT) };
    CHECK_FALSE(game.Load(reader));
    CHECK(game.StateHash() == before);
}

TEST_CASE("a save with bytes after the state, or of another format, is refused") {
    constexpr std::byte EXTRA{ 1 };
    constexpr u32 OTHER_FORMAT = 99;
    game::Arena saved(test::TestManifest(), game::Scene::Arena);
    std::vector<std::byte> longer = Saved(saved);
    longer.push_back(EXTRA);
    game::Arena game(test::TestManifest(), game::Scene::Arena);
    core::Reader longReader{ longer };
    CHECK_FALSE(game.Load(longReader));

    std::vector<std::byte> other = Saved(saved);
    const auto format = std::bit_cast<std::array<std::byte, sizeof(u32)>>(OTHER_FORMAT);
    std::ranges::copy(format, other.begin());
    core::Reader otherReader{ other };
    CHECK_FALSE(game.Load(otherReader));
}

// An arena save of `state` with `player`, laid out as Arena::Save writes one: format, scene, seed,
// stable-ID counter, months, the player and the selection (none), then each domain after its version,
// then no relationships and no respawns waiting. Its versions must follow arena.cpp's, or every refusal below is the
// version's.
std::vector<std::byte> ArenaSave(const core::State& state, core::StableId player = {}) {
    constexpr u32 FORMAT = 3;
    constexpr u32 ACTIVE_VERSION = 4;
    constexpr u32 WORLD_VERSION = 1;
    constexpr u64 MONTHS_PASSED = 0;
    constexpr u64 RELATIONSHIPS = 0;
    constexpr u64 RESPAWNS = 0;
    using Active = core::ComponentList<
        game::Body,
        game::Look,
        game::Orbit,
        game::Spin,
        game::Creature,
        game::Npc,
        game::Needs,
        game::Met,
        game::Wander,
        game::Health,
        game::Dying,
        game::Arriving>;
    using World = core::ComponentList<game::Npc, game::Needs, game::Met, game::Group>;
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    writer(FORMAT, game::Scene::Arena, state.Seed(), state.Ids().next, MONTHS_PASSED, player, core::StableId{});
    writer(ACTIVE_VERSION);
    core::SaveDomain(writer, state[game::ACTIVE], Active{});
    writer(WORLD_VERSION);
    core::SaveDomain(writer, state[game::WORLD], World{});
    writer(RELATIONSHIPS);
    writer(RESPAWNS);
    return bytes;
}

// Whether a fresh arena loads `bytes`; CHECKs that its state hash is the same afterwards.
bool Loads(const std::vector<std::byte>& bytes) {
    game::Arena game(test::TestManifest(), game::Scene::Arena);
    const u64 before = game.StateHash();
    core::Reader reader{ bytes };
    const bool loaded = game.Load(reader);
    const u64 after = game.StateHash();
    CHECK(after == before);
    return loaded;
}

constexpr u64 STATE_SEED = 7;
constexpr u32 NPC_CELL = 3;

TEST_CASE("a sound save built as the refusals below build theirs is loaded") {
    core::State state{ game::DOMAINS, STATE_SEED };
    static_cast<void>(game::SpawnNpc(state, NPC_CELL));
    const core::Entity player = state.Create(game::ACTIVE).second;
    state[game::ACTIVE].Entities().emplace<game::Body>(player);
    game::Arena game(test::TestManifest(), game::Scene::Arena);
    const std::vector<std::byte> bytes = ArenaSave(state, state[game::ACTIVE].Entities().get<core::StableId>(player));
    core::Reader reader{ bytes };
    CHECK(game.Load(reader));
}

TEST_CASE("a save whose player doesn't exist is refused, and the game is unchanged") {
    constexpr core::StableId NOBODY{ 12'345 };
    const core::State state{ game::DOMAINS, STATE_SEED };
    const bool loaded = Loads(ArenaSave(state, NOBODY));
    CHECK_FALSE(loaded);
}

TEST_CASE("a save whose player has no body is refused, and the game is unchanged") {
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::StableId player = state.Create(game::ACTIVE).first;
    const bool loaded = Loads(ArenaSave(state, player));
    CHECK_FALSE(loaded);
}

TEST_CASE("a save with a creature that has no look is refused, and the game is unchanged") {
    core::State state{ game::DOMAINS, STATE_SEED };
    const core::Entity entity = state.Create(game::ACTIVE).second;
    state[game::ACTIVE].Entities().emplace<game::Body>(entity);
    state[game::ACTIVE].Entities().emplace<game::Creature>(entity);
    const bool loaded = Loads(ArenaSave(state));
    CHECK_FALSE(loaded);
}

// A fox as the arena makes one, standing at the origin, with `health`.
void SpawnFox(core::State& state, game::Health health) {
    const core::Entity entity = state.Create(game::ACTIVE).second;
    state[game::ACTIVE].Entities().emplace<game::Body>(entity);
    state[game::ACTIVE].Entities().emplace<game::Look>(entity);
    state[game::ACTIVE].Entities().emplace<game::Creature>(entity, game::CreatureKind::Fox);
    state[game::ACTIVE].Entities().emplace<game::Health>(entity, health);
}

TEST_CASE("a save with a fox as the arena makes one is loaded") {
    core::State state{ game::DOMAINS, STATE_SEED };
    SpawnFox(state, { .current = FOX_HEALTH, .max = FOX_HEALTH });
    const std::vector<std::byte> bytes = ArenaSave(state);
    core::Reader reader{ bytes };
    game::Arena game(test::TestManifest(), game::Scene::Arena);
    CHECK(game.Load(reader));
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
    const core::Entity entity = state.Create(game::ACTIVE).second;
    state[game::ACTIVE].Entities().emplace<game::Look>(entity);

    const bool loaded = Loads(ArenaSave(state));
    CHECK_FALSE(loaded);
}

} // namespace
