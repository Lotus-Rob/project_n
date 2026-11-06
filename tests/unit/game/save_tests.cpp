#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/state.hpp"
#include "core/tick_clock.hpp"
#include "game/arena.hpp"
#include "game/components.hpp"
#include "game/npcs.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 TICKS = 5;
constexpr u64 MONTHS = 12; // enough for relationships to form

core::TickCounters At(u64 tick) {
    return { .wall = tick, .simulation = tick, .state = tick };
}

// The stress arena after some ticks and months: every component and the relationships in use.
void Play(game::Arena& arena, core::Jobs& jobs, u64 from) {
    for (u64 tick = from; tick < from + TICKS; ++tick) {
        arena.Tick(At(tick));
    }
    for (u64 month = 0; month < MONTHS; ++month) {
        arena.AdvanceCalendar(At(from + TICKS + month), jobs, {});
    }
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
    game::Arena saved{ game::Scene::Stress };
    Play(saved, jobs, 1);
    const std::vector<std::byte> bytes = Saved(saved);
    REQUIRE_FALSE(saved.NpcRelationships().All().empty());

    game::Arena loaded{ game::Scene::Arena };
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader));
    CHECK(loaded.StateHash() == saved.StateHash());
    CHECK(loaded.Months() == saved.Months());

    Play(saved, jobs, 1 + TICKS + MONTHS);
    Play(loaded, jobs, 1 + TICKS + MONTHS);
    CHECK(loaded.StateHash() == saved.StateHash());
    const std::vector<core::Instance> fromLoaded = Published(loaded, jobs);
    const std::vector<core::Instance> fromSaved = Published(saved, jobs);
    const bool same = SameBytes(fromLoaded, fromSaved);
    CHECK(same);
}

TEST_CASE("a truncated save is refused, and the game is unchanged") {
    constexpr std::size_t CUT = 4;
    game::Arena saved{ game::Scene::Stress };
    const std::vector<std::byte> bytes = Saved(saved);

    game::Arena game{ game::Scene::Arena };
    const u64 before = game.StateHash();
    core::Reader reader{ std::span{ bytes }.first(bytes.size() - CUT) };
    CHECK_FALSE(game.Load(reader));
    CHECK(game.StateHash() == before);
}

TEST_CASE("a save with bytes after the state, or of another format, is refused") {
    constexpr std::byte EXTRA{ 1 };
    constexpr u32 OTHER_FORMAT = 99;
    game::Arena saved{ game::Scene::Arena };
    std::vector<std::byte> longer = Saved(saved);
    longer.push_back(EXTRA);
    game::Arena game{ game::Scene::Arena };
    core::Reader longReader{ longer };
    CHECK_FALSE(game.Load(longReader));

    std::vector<std::byte> other = Saved(saved);
    const auto format = std::bit_cast<std::array<std::byte, sizeof(u32)>>(OTHER_FORMAT);
    std::ranges::copy(format, other.begin());
    core::Reader otherReader{ other };
    CHECK_FALSE(game.Load(otherReader));
}

// An arena save of `state`, laid out as Arena::Save writes one: format, scene, seed, stable-ID
// counter, months, then each domain after its version, then no relationships.
std::vector<std::byte> ArenaSave(const core::State& state) {
    constexpr u32 FORMAT = 1;
    constexpr u32 VERSION = 1;
    constexpr u64 MONTHS_PASSED = 0;
    constexpr u64 RELATIONSHIPS = 0;
    using Active = core::ComponentList<game::Body, game::Look, game::Orbit, game::Npc, game::Needs, game::Met, game::Wander>;
    using World = core::ComponentList<game::Npc, game::Needs, game::Met, game::Group>;
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    writer(FORMAT, game::Scene::Arena, state.Seed(), state.Ids().next, MONTHS_PASSED);
    writer(VERSION);
    core::SaveDomain(writer, state[game::ACTIVE], Active{});
    writer(VERSION);
    core::SaveDomain(writer, state[game::WORLD], World{});
    writer(RELATIONSHIPS);
    return bytes;
}

// Whether a fresh arena loads `bytes`; CHECKs that its state hash is the same afterwards.
bool Loads(const std::vector<std::byte>& bytes) {
    game::Arena game{ game::Scene::Arena };
    const u64 before = game.StateHash();
    core::Reader reader{ bytes };
    const bool loaded = game.Load(reader);
    const u64 after = game.StateHash();
    CHECK(after == before);
    return loaded;
}

constexpr u64 STATE_SEED = 7;
constexpr u32 NPC_CELL = 3;

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
