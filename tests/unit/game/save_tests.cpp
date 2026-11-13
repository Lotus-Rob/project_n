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
    REQUIRE(loaded.Load(reader, SAVED_TICK));
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
    REQUIRE(loaded.Load(reader, SAVED_TICK));
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
    REQUIRE(loaded.Load(reader, SAVED_TICK));
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
    game::Arena saved(test::TestManifest());
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

    game::Arena loaded(test::TestManifest(), game::Scene::Cells);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
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
    game::Arena saved(test::TestManifest());
    for (u64 tick = 1; tick <= SAVED_AT; ++tick) {
        Tick(saved, At(tick), {});
        if (tick == DEATH) {
            saved.Damage(Foxes(saved).at(0), FOX_HEALTH, tick);
        }
    }
    REQUIRE_FALSE(saved.State()[game::ACTIVE].Entities().view<const game::Arriving>().empty());
    const std::vector<std::byte> bytes = Saved(saved);

    game::Arena loaded(test::TestManifest(), game::Scene::Cells);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
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
    game::Arena saved(test::TestManifest(), game::Scene::Arena, data);
    Tick(saved, At(1), {});
    const core::Vec2 point =
        saved.State()[game::ACTIVE].Entities().get<game::Body>(saved.State()[game::ACTIVE].Find(Foxes(saved).front())).current;
    const std::array<core::Action, 1> cast{
        { { .kind = core::ActionKind::Cast, .slot = core::Slot::Special1, .point = point } }
    };
    Tick(saved, At(CAST), { .actions = cast, .held = {} });
    core::Jobs jobs{ 1 };
    core::Snapshot snapshot;
    saved.Publish(snapshot, jobs);
    REQUIRE(snapshot.volleys.size() == 1);
    const core::Volley volley = snapshot.volleys.front().volley;
    const u64 last = core::LastArrival(volley);
    const u64 savedAt = CAST + ((last - CAST) / 2);
    for (u64 tick = CAST + 1; tick <= savedAt; ++tick) {
        Tick(saved, At(tick), {});
    }
    const std::vector<std::byte> bytes = Saved(saved);

    game::Arena loaded(test::TestManifest(), game::Scene::Cells, data);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
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
    game::Arena saved(test::TestManifest(), game::Scene::Arena, data);
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
    saved.Publish(snapshot, jobs);
    REQUIRE(snapshot.volleys.size() == 1);
    const core::Volley bolt = snapshot.volleys.front().volley;
    const u64 savedAt = core::ArrivalTick(bolt, 0) + BETWEEN;
    for (u64 tick = CAST + 1; tick <= savedAt; ++tick) {
        Tick(saved, At(tick), {});
    }
    REQUIRE(saved.State()[game::ACTIVE].Entities().all_of<game::Afflictions>(saved.State()[game::ACTIVE].Find(fox)));
    const std::vector<std::byte> bytes = Saved(saved);

    game::Arena loaded(test::TestManifest(), game::Scene::Cells, data);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
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
    game::Arena saved(test::TestManifest(), game::Scene::Stress);
    const std::vector<std::byte> bytes = Saved(saved);

    game::Arena game(test::TestManifest(), game::Scene::Arena);
    const u64 before = game.StateHash();
    core::Reader reader{ std::span{ bytes }.first(bytes.size() - CUT) };
    CHECK_FALSE(game.Load(reader, SAVED_TICK));
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
    CHECK_FALSE(game.Load(longReader, SAVED_TICK));

    std::vector<std::byte> other = Saved(saved);
    const auto format = std::bit_cast<std::array<std::byte, sizeof(u32)>>(OTHER_FORMAT);
    std::ranges::copy(format, other.begin());
    core::Reader otherReader{ other };
    CHECK_FALSE(game.Load(otherReader, SAVED_TICK));
}

constexpr u32 FORMAT = 4;
constexpr u32 ACTIVE_VERSION = 4;
constexpr u32 WORLD_VERSION = 1;
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

// An arena save of `state` with `player`, laid out as Arena::Save writes one: format, scene, seed,
// stable-ID counter, months, the player and the selection (none), then each domain after its version,
// then no relationships, no respawns waiting, `volley` if any (the flying swords' hits of 20, none resolved yet),
// `flash` if any and `seal` if any.
// Its versions must follow arena.cpp's, or every refusal below is the version's.
std::vector<std::byte> ArenaSave(
    const core::State& state,
    core::StableId player = {},
    const std::optional<SavedVolley>& volley = std::nullopt,
    const std::optional<core::Flash>& flash = std::nullopt,
    const std::optional<core::Seal>& seal = std::nullopt
) {
    constexpr u64 MONTHS_PASSED = 0;
    constexpr u64 RELATIONSHIPS = 0;
    constexpr u64 RESPAWNS = 0;
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    writer(FORMAT, game::Scene::Arena, state.Seed(), state.Ids().next, MONTHS_PASSED, player, core::StableId{});
    writer(ACTIVE_VERSION);
    core::SaveDomain(writer, state[game::ACTIVE], Active{});
    writer(WORLD_VERSION);
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

// Whether a fresh arena loads `bytes`, saved at `savedTick`; CHECKs that its state hash is the same afterwards.
bool Loads(const std::vector<std::byte>& bytes, u64 savedTick = SAVED_TICK) {
    game::Arena game(test::TestManifest(), game::Scene::Arena);
    const u64 before = game.StateHash();
    core::Reader reader{ bytes };
    const bool loaded = game.Load(reader, savedTick);
    const u64 after = game.StateHash();
    CHECK(after == before);
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
void PlayFight(game::Arena& saved) {
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
std::vector<std::byte> WithoutWorldTier(const game::Arena& saved, const std::vector<std::byte>& full) {
    const core::State& state = saved.State();
    const core::State bare{ game::DOMAINS, state.Seed() };
    const auto head = Written([&](core::Writer& writer) {
        writer(FORMAT, game::Scene::Arena, state.Seed(), state.Ids().next, saved.Months(), saved.Player(), saved.Selected());
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
    game::Arena saved(test::TestManifest(), game::Scene::Arena, data);
    PlayFight(saved);
    const core::Registry& entities = saved.State()[game::ACTIVE].Entities();
    core::Jobs jobs{ 1 };
    core::Snapshot snapshot;
    saved.Publish(snapshot, jobs);
    REQUIRE(snapshot.volleys.size() >= 1);
    REQUIRE(snapshot.flashes.size() >= 1);
    CHECK_FALSE(entities.view<const game::Afflictions>().empty());
    CHECK_FALSE(entities.view<const game::Dying>().empty());
    CHECK_FALSE(entities.view<const game::Arriving>().empty());
    CHECK_FALSE(entities.view<const game::Dashing>().empty());
    const std::vector<std::byte> full = Saved(saved);
    game::Arena loadedFull(test::TestManifest(), game::Scene::Cells, data);
    core::Reader fullReader{ full };
    CHECK(loadedFull.Load(fullReader, SAVED_TICK));
    CHECK(loadedFull.StateHash() == saved.StateHash());

    const std::vector<std::byte> seed = WithoutWorldTier(saved, full);
    constexpr std::size_t SMALL = 16'384;
    REQUIRE_FALSE(seed.empty());
    CHECK(seed.size() < SMALL);
    game::Arena loaded(test::TestManifest(), game::Scene::Cells, data);
    core::Reader reader{ seed };
    CHECK(loaded.Load(reader, SAVED_TICK));
}

// Not part of a run: `--test-case='write the fight save seed' --no-skip` (tests/AGENTS.md) writes the fuzz seed.
TEST_CASE("write the fight save seed" * doctest::skip()) {
    game::Arena saved(test::TestManifest(), game::Scene::Arena, FightData());
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
    game::Arena game(test::TestManifest(), game::Scene::Arena);
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
    game::Arena game(test::TestManifest(), game::Scene::Arena);
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
    game::Arena game(test::TestManifest(), game::Scene::Arena);
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
    game::Arena game(test::TestManifest(), game::Scene::Arena);
    REQUIRE(game.Load(reader, SAVED_TICK));
    std::vector<core::Event> events;
    game.Tick(At(1), {}, events); // follows the fox's chest
}

TEST_CASE("a loaded volley whose arrivals are all overdue still ends its use, once") {
    constexpr u64 LATER = 100; // past every arrival: the volley starts at 3 and flies for 20
    const core::State state{ game::DOMAINS, STATE_SEED };
    const SavedVolley volley = PointVolley();
    const std::vector<std::byte> bytes = ArenaSave(state, {}, volley);
    core::Reader reader{ bytes };
    game::Arena game(test::TestManifest(), game::Scene::Arena);
    REQUIRE(game.Load(reader, SAVED_TICK));
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
    game::Arena saved(test::TestManifest());
    Tick(saved, At(1), {});
    const std::vector<std::byte> bytes = Saved(saved);
    core::GameData data = game::BuiltInData();
    data.abilities.qiMax = LOWER;
    game::Arena loaded(test::TestManifest(), game::Scene::Arena, data);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
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
    game::Arena game(test::TestManifest(), game::Scene::Arena);
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
    game::Arena game(test::TestManifest(), game::Scene::Arena);
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
        game::Arena game(test::TestManifest(), game::Scene::Arena);
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
        game::Arena game(test::TestManifest(), game::Scene::Arena);
        const std::vector<std::byte> bytes = saveOf(sound);
        core::Reader reader{ bytes };
        CHECK(game.Load(reader, SAVED_TICK));
    }
    const bool fromLater = Loads(saveOf(FROM_LATER));
    CHECK_FALSE(fromLater);
}

} // namespace
