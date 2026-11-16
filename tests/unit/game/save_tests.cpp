#include "build_info.hpp"
#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/manifest.hpp"
#include "core/state.hpp"
#include "core/tick_clock.hpp"
#include "game/combat.hpp"
#include "game/components.hpp"
#include "game/host.hpp"
#include "game/npcs.hpp"
#include "support/manifest.hpp"
#include "support/world.hpp"

#include <doctest/doctest.h>

namespace {

// The shipped combat rules (assets/data/abilities.yaml's `combat:` section, which a data test checks).
constexpr core::CombatRules RULES{};

constexpr u64 TICKS = 5;
constexpr u64 MONTHS = 12; // enough for relationships to form
// The simulation tick every save here is loaded as made at: past any tick they played to, so only a flash dated after it
// is refused.
constexpr u64 SAVED_TICK = 1'000;

core::TickCounters At(u64 tick) {
    return { .wall = tick, .simulation = tick, .state = tick };
}

// Ticks `host`, ignoring its events.
void Tick(game::Host& host, const core::TickCounters& counters, const core::TickInput& input) {
    std::vector<core::Event> events;
    host.Tick(counters, input, events);
}

// The stress arena after some ticks and months: every component and the relationships in use.
void Play(game::Host& host, core::Jobs& jobs, u64 from) {
    for (u64 tick = from; tick < from + TICKS; ++tick) {
        Tick(host, At(tick), {});
    }
    for (u64 month = 0; month < MONTHS; ++month) {
        host.AdvanceCalendar(At(from + TICKS + month), jobs, {});
    }
}

constexpr i64 FOX_HEALTH = 1'200;

// The arena's foxes, in ID order.
std::vector<core::StableId> Foxes(const game::Host& host) {
    std::vector<core::StableId> foxes;
    const core::Registry& entities = host.State()[game::ACTIVE].Entities();
    for (auto [entity, creature, stable] : entities.view<const game::Creature, const core::StableId>().each()) {
        if (creature.kind == game::CreatureKind::Fox) {
            foxes.push_back(stable);
        }
    }
    std::ranges::sort(foxes);
    return foxes;
}

std::vector<std::byte> Saved(game::Host& host) {
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    host.Save(writer);
    return bytes;
}

std::vector<core::Instance> Published(game::Host& host, core::Jobs& jobs) {
    core::Snapshot snapshot;
    host.Publish(snapshot, jobs, std::nullopt);
    return snapshot.instances;
}

bool SameBytes(std::span<const core::Instance> lhs, std::span<const core::Instance> rhs) {
    return std::ranges::equal(std::as_bytes(lhs), std::as_bytes(rhs));
}

TEST_CASE("a game loaded from a save goes on exactly as the game that saved") {
    core::Jobs jobs{ 1 };
    game::Host saved(test::TestManifest(), game::STRESS_SCENE);
    Play(saved, jobs, 1);
    const std::vector<std::byte> bytes = Saved(saved);
    REQUIRE_FALSE(saved.NpcRelationships().All().empty());

    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, test::ShippedData());
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

TEST_CASE("a save made in world loads back into it") {
    constexpr core::HeldInput RIGHT{ .move = { .x = 1.0F, .y = 0.0F } };
    core::Jobs jobs{ 1 };
    game::Host saved(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    Tick(saved, At(1), { .actions = {}, .held = RIGHT }); // the walkers on their way, the cultivator off its start
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

TEST_CASE("Load holds and changes nothing until EnterScene") {
    game::Host saved(test::TestManifest(), game::PAINTED_SCENE);
    Tick(saved, At(1), {});
    const std::vector<std::byte> bytes = Saved(saved);

    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, test::ShippedData());
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

constexpr u32 FORMAT = 4;
constexpr u32 ACTIVE_VERSION = 5; // 5: the NPCs' homes
constexpr u32 WORLD_VERSION = 2;  // 2: the NPCs' homes
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
    game::Arriving,
    game::Caster,
    game::Dashing,
    game::Afflictions>;
using World = core::ComponentList<game::Npc, game::Needs, game::Met, game::Group>;

// A volley as game::Combat saves one: the cast, then its targets.
struct SavedVolley {
    core::Volley volley{};
    std::vector<core::VolleyTarget> targets;
};

// An arena save of `state` with `player`, laid out as Host::Save writes one: format, scene, seed,
// stable-ID counter, months, the player and the selection (none), then each domain after its version,
// then no relationships, no respawns waiting, `volley` if any (the flying swords' hits of 20, none resolved yet),
// `flash` if any and `seal` if any.
// Its versions must follow game/host.cpp's, or every refusal below is the version's.
std::vector<std::byte> ArenaSave(
    const core::State& state,
    core::StableId player = {},
    const std::optional<SavedVolley>& volley = std::nullopt,
    const std::optional<core::Flash>& flash = std::nullopt,
    const std::optional<core::Seal>& seal = std::nullopt,
    core::SceneKey scene = game::ARENA_SCENE,
    u32 worldVersion = WORLD_VERSION,
    u32 activeVersion = ACTIVE_VERSION
) {
    constexpr u64 MONTHS_PASSED = 0;
    constexpr u64 RELATIONSHIPS = 0;
    constexpr u64 RESPAWNS = 0;
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    writer(
        FORMAT,
        static_cast<u8>(std::to_underlying(scene)),
        state.Seed(),
        state.Ids().next,
        MONTHS_PASSED,
        player,
        core::StableId{}
    );
    writer(activeVersion);
    core::SaveDomain(writer, state[game::ACTIVE], Active{});
    writer(worldVersion);
    core::SaveDomain(writer, state[game::WORLD], World{});
    writer(RELATIONSHIPS);
    writer(RESPAWNS);
    writer(static_cast<u64>(volley ? 1 : 0));
    if (volley) {
        constexpr i64 DAMAGE = 20;
        constexpr u32 RESOLVED = 0;
        writer(volley->volley, core::AbilityKind::FlyingSwords, core::HitKind::Damage, DAMAGE, core::Affliction{}, RESOLVED);
        writer(static_cast<u64>(volley->targets.size()));
        for (const core::VolleyTarget& target : volley->targets) {
            writer(target);
        }
    }
    writer(static_cast<u64>(flash ? 1 : 0));
    if (flash) {
        writer(*flash);
    }
    writer(static_cast<u64>(seal ? 1 : 0));
    if (seal) {
        writer(*seal);
    }
    return bytes;
}

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

// The arena data of the mid-fight save: Qi full, enough for the swords and the lance.
core::GameData FightData() {
    core::GameData data = game::BuiltInData();
    data.abilities.qiStart = data.abilities.qiMax;
    return data;
}

// Plays `saved` (an arena scene made with FightData()) to the middle of a fight, as tests/fuzz/corpus/save/fight holds
// it: the arena scene has no world tier, so its save is small. Casts are at the chest of one fox, whose tick-by-tick
// history the numbers below arrange.
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

// `full`, the save of `saved`, with the world tier and the relationships left out (about 200 KB of NPCs): the bytes before
// the world tier, an empty one, no relationships, and what follows the relationships, which is all combat. Empty if
// `full` isn't laid out that way.
std::vector<std::byte> WithoutWorldTier(const game::Host& saved, const std::vector<std::byte>& full) {
    const core::State& state = saved.State();
    const core::State bare{ game::DOMAINS, state.Seed() };
    const auto head = Written([&](core::Writer& writer) {
        writer(
            FORMAT,
            static_cast<u8>(std::to_underlying(game::ARENA_SCENE)),
            state.Seed(),
            state.Ids().next,
            saved.Months(),
            saved.Player(),
            saved.Selected()
        );
        writer(ACTIVE_VERSION);
        core::SaveDomain(writer, state[game::ACTIVE], Active{});
    });
    const auto world = [](const core::State& source) {
        return Written([&source](core::Writer& writer) {
            writer(WORLD_VERSION);
            core::SaveDomain(writer, source[game::WORLD], World{});
        });
    };
    const auto relationships = [](const game::Relationships& source) {
        return Written([&source](core::Writer& writer) { source.Save(writer); });
    };
    const std::vector<std::byte> fullWorld = world(state);
    const std::vector<std::byte> fullRelationships = relationships(saved.NpcRelationships());
    const std::size_t before = head.size() + fullWorld.size() + fullRelationships.size();
    if (full.size() <= before || !std::equal(head.begin(), head.end(), full.begin())) {
        return {};
    }
    std::vector<std::byte> bytes = head;
    const std::vector<std::byte> bareWorld = world(bare);
    const std::vector<std::byte> noRelationships = relationships(game::Relationships{});
    bytes.insert(bytes.end(), bareWorld.begin(), bareWorld.end());
    bytes.insert(bytes.end(), noRelationships.begin(), noRelationships.end());
    bytes.insert(bytes.end(), full.begin() + static_cast<std::ptrdiff_t>(before), full.end());
    return bytes;
}

TEST_CASE("the mid-fight save loads, and so does the fuzz seed cut from it") {
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
    const std::vector<std::byte> full = Saved(saved);
    game::Host loadedFull(test::TestManifest(), game::CELLS_SCENE, data);
    core::Reader fullReader{ full };
    REQUIRE(loadedFull.Load(fullReader, SAVED_TICK));
    loadedFull.EnterScene(); // as the logic does at the swap
    CHECK(loadedFull.StateHash() == saved.StateHash());

    const std::vector<std::byte> seed = WithoutWorldTier(saved, full);
    constexpr std::size_t SMALL = 16'384;
    REQUIRE_FALSE(seed.empty());
    CHECK(seed.size() < SMALL);
    game::Host loaded(test::TestManifest(), game::CELLS_SCENE, data);
    core::Reader reader{ seed };
    CHECK(loaded.Load(reader, SAVED_TICK));
}

// Not part of a run: `--test-case='write the fight save seed' --no-skip` (tests/AGENTS.md) writes the fuzz seed.
TEST_CASE("write the fight save seed" * doctest::skip()) {
    game::Host saved(test::TestManifest(), game::ARENA_SCENE, FightData());
    PlayFight(saved);
    const std::vector<std::byte> seed = WithoutWorldTier(saved, Saved(saved));
    REQUIRE_FALSE(seed.empty());
    const std::filesystem::path path = std::filesystem::path{ PN_FUZZ_CORPUS } / "save" / "fight";
    std::ofstream file{ path, std::ios::binary };
    std::ranges::transform(seed, std::ostreambuf_iterator<char>{ file }, [](std::byte byte) {
        return static_cast<char>(std::to_integer<u8>(byte));
    });
    file.close();
    CHECK(file.good());
}

constexpr u64 STATE_SEED = 7;
constexpr u32 NPC_CELL = 3;

TEST_CASE("a sound save built as the refusals below build theirs is loaded") {
    core::State state{ game::DOMAINS, STATE_SEED };
    static_cast<void>(game::SpawnNpc(state, NPC_CELL));
    const core::Entity player = state.Create(game::ACTIVE).second;
    state[game::ACTIVE].Entities().emplace<game::Body>(player);
    game::Host game(test::TestManifest(), game::ARENA_SCENE);
    const std::vector<std::byte> bytes = ArenaSave(state, state[game::ACTIVE].Entities().get<core::StableId>(player));
    core::Reader reader{ bytes };
    CHECK(game.Load(reader, SAVED_TICK));
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
core::Entity SpawnFox(core::State& state, game::Health health) {
    const core::Entity entity = state.Create(game::ACTIVE).second;
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
    caster[game::ACTIVE].Entities().emplace<game::Caster>(caster.Create(game::ACTIVE).second);
    const bool casterLoaded = Loads(ArenaSave(caster));
    CHECK_FALSE(casterLoaded);

    core::State dashing{ game::DOMAINS, STATE_SEED };
    dashing[game::ACTIVE].Entities().emplace<game::Dashing>(
        dashing.Create(game::ACTIVE).second, game::Dashing{ .from = {}, .to = {}, .startTick = 0, .ticks = 1 }
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
    state[game::ACTIVE].Entities().emplace<game::Afflictions>(state.Create(game::ACTIVE).second);
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
    const core::StableId bare = state.Create(game::ACTIVE).first; // no body, no look: nothing to aim at
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
    const core::Entity entity = state.Create(game::ACTIVE).second;
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

// A save of the world scene with nothing in it: small, and it names a scene that needs the world map.
std::vector<std::byte> EmptyWorldSave() {
    const core::State state{ game::DOMAINS, STATE_SEED };
    return ArenaSave(state, {}, std::nullopt, std::nullopt, std::nullopt, game::WORLD_SCENE);
}

TEST_CASE("a save naming the world scene is refused by a host without the world map, and loaded by one with it") {
    const std::vector<std::byte> bytes = EmptyWorldSave();
    game::Host without(test::TestManifest(), game::ARENA_SCENE); // the built-in data has no map
    core::Reader refused{ bytes };
    CHECK_FALSE(without.Load(refused, SAVED_TICK));
    CHECK_FALSE(without.Requested().has_value());
    game::Host with(test::TestManifest(), game::ARENA_SCENE, test::ShippedData());
    core::Reader accepted{ bytes };
    CHECK(with.Load(accepted, SAVED_TICK));
}

// Not part of a run: `--test-case='write the world save seed' --no-skip` (tests/AGENTS.md) writes the fuzz seed that
// reached the save target's host, which has no map, with a save naming the world scene.
TEST_CASE("write the world save seed" * doctest::skip()) {
    const std::vector<std::byte> seed = EmptyWorldSave();
    const std::filesystem::path path = std::filesystem::path{ PN_FUZZ_CORPUS } / "save" / "world";
    std::ofstream file{ path, std::ios::binary };
    std::ranges::transform(seed, std::ostreambuf_iterator<char>{ file }, [](std::byte byte) {
        return static_cast<char>(std::to_integer<u8>(byte));
    });
    file.close();
    CHECK(file.good());
}

// The index of the first place of `kind` on the shipped map.
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

// Whether a host with the shipped map loads `bytes`.
bool LoadsWithMap(const std::vector<std::byte>& bytes) {
    game::Host game(test::TestManifest(), game::ARENA_SCENE, test::ShippedData());
    core::Reader reader{ bytes };
    return game.Load(reader, SAVED_TICK);
}

TEST_CASE("a world save round-trips the NPCs' homes") {
    game::Host saved(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    const std::vector<std::byte> bytes = Saved(saved);
    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, test::ShippedData());
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

TEST_CASE("a version-1 world save is refused") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    const u16 city = FirstPlace(*map, core::PlaceKind::City);
    const u32 cell = map->TileIndex(map->Places()[city].tile);
    CHECK(LoadsWithMap(WorldSaveOf(cell, city)));
    constexpr u32 OLD_VERSION = 1;
    CHECK_FALSE(LoadsWithMap(WorldSaveOf(cell, city, OLD_VERSION)));
}

TEST_CASE("a version-4 active section is refused") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
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
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    const u16 city = FirstPlace(*map, core::PlaceKind::City);
    const u32 cell = map->TileIndex(map->Places()[city].tile);
    const u16 pastPlaces = static_cast<u16>(map->Places().size());
    const u16 danger = FirstPlace(*map, core::PlaceKind::Danger);
    CHECK_FALSE(LoadsWithMap(WorldSaveOf(cell, pastPlaces)));
    CHECK_FALSE(LoadsWithMap(WorldSaveOf(cell, danger)));
    CHECK_FALSE(LoadsWithMap(WorldSaveOf(cell, game::NO_HOME)));
}

TEST_CASE("a world save with an NPC on a tile past the map is refused") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
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

} // namespace
