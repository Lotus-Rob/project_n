#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/jobs.hpp"
#include "core/journey.hpp"
#include "core/state.hpp"
#include "game/components.hpp"
#include "game/host.hpp"
#include "game/npcs.hpp"
#include "game/scenes.hpp"
#include "game/travel.hpp"
#include "support/manifest.hpp"
#include "support/saves.hpp"
#include "support/world.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 SEED = 7;
constexpr u32 CELL = 130;
constexpr u32 MANY_WORKERS = 7;
constexpr u64 FEW = 5;
constexpr u64 MANY = 20;

using Pair = std::pair<core::StableId, core::StableId>;

// Exact equality, as determinism means: the same bits.
bool Same(f32 lhs, f32 rhs) {
    return std::bit_cast<u32>(lhs) == std::bit_cast<u32>(rhs);
}

bool Same(core::Vec2 lhs, core::Vec2 rhs) {
    return Same(lhs.x, rhs.x) && Same(lhs.y, rhs.y);
}

constexpr u32 STAY_DAYS = 2;
constexpr u32 STAY_SEED = 0x7e57;
constexpr i64 LATER = 100'000;

/// A world with an NPC on a journey under way after one finished, so it has a journal entry: `travel` holds both.
struct Traveller {
    core::State state{ game::DOMAINS, SEED };
    std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    game::Travel travel;
    core::StableId npc{};
    core::StableId group{};

    Traveller() {
        std::vector<u16> cities;
        for (std::size_t index = 0; index < map->Places().size() && cities.size() < 2; ++index) {
            if (map->Places()[index].kind == core::PlaceKind::City) {
                cities.push_back(static_cast<u16>(index));
            }
        }
        REQUIRE(cities.size() == 2);
        const std::vector<u32> tiles = core::FindRoute(
            *map, core::WALKING, map->TileIndex(map->Places()[cities[0]].tile), map->TileIndex(map->Places()[cities[1]].tile)
        );
        REQUIRE(tiles.size() >= 3);
        npc = game::SpawnNpc(state, tiles.front(), cities[0]);
        group = game::SpawnGroup(state, tiles.front(), 0);
        const core::Journey done = core::Visit(*map, tiles, npc, cities[0], cities[1], 0, STAY_DAYS, STAY_SEED);
        travel.Start(done);
        travel.Finish(core::EndTick(done), state);
        travel.Start(core::Visit(*map, tiles, npc, cities[0], cities[1], LATER, STAY_DAYS, STAY_SEED));
        state[game::WORLD].Entities().get<game::Npc>(state[game::WORLD].Find(npc)).action = game::Action::Travel;
    }
};

TEST_CASE("a cell's centre lies in that cell") {
    CHECK(game::CellAt(game::CellCentre(CELL)) == CELL);
    CHECK(game::CellAt(game::CellCentre(0)) == 0);
}

TEST_CASE("an NPC activated and deactivated keeps its stable ID, state and cell") {
    constexpr f32 POWER = 3.0F;
    core::State state{ game::DOMAINS, SEED };
    const core::StableId npc = game::SpawnNpc(state, CELL);
    state[game::WORLD].Entities().get<game::Npc>(state[game::WORLD].Find(npc)).power = POWER;
    state[game::WORLD].Entities().emplace<game::Met>(state[game::WORLD].Find(npc));

    game::Activate(state, npc);
    REQUIRE(state.DomainOf(npc) == game::ACTIVE);
    const core::Entity active = state[game::ACTIVE].Find(npc);
    CHECK(state[game::ACTIVE].Entities().get<game::Body>(active).current.x == game::CellCentre(CELL).x);
    CHECK(state[game::ACTIVE].Entities().all_of<game::Met>(active));

    game::Deactivate(state, npc);
    REQUIRE(state.DomainOf(npc) == game::WORLD);
    const core::Entity back = state[game::WORLD].Find(npc);
    CHECK(state[game::WORLD].Entities().get<game::Npc>(back).cell == CELL);
    CHECK(state[game::WORLD].Entities().get<game::Npc>(back).power == POWER);
    CHECK(state[game::WORLD].Entities().all_of<game::Met>(back));
    CHECK_FALSE(state[game::WORLD].Entities().all_of<game::Body>(back));
}

TEST_CASE("an active NPC deactivates into the cell it walked to") {
    core::State state{ game::DOMAINS, SEED };
    const core::StableId npc = game::SpawnNpc(state, CELL);
    game::Activate(state, npc);
    state[game::ACTIVE].Entities().get<game::Body>(state[game::ACTIVE].Find(npc)).current = game::CellCentre(CELL + 1);

    game::Deactivate(state, npc);
    CHECK(state[game::WORLD].Entities().get<game::Npc>(state[game::WORLD].Find(npc)).cell == CELL + 1);
}

TEST_CASE("an NPC with a home deactivates onto the tile it had, wherever it stood") {
    constexpr u16 HOME = 2;
    core::State state{ game::DOMAINS, SEED };
    const core::StableId npc = game::SpawnNpc(state, CELL, HOME);
    // Brought into a scene whose ground isn't the map's, standing far from any point of its tile.
    state.Move(npc, game::ACTIVE, [](const core::Registry& from, core::Entity old, core::Registry& into, core::Entity moved) {
        into.emplace<game::Npc>(moved, from.get<game::Npc>(old));
        into.emplace<game::Needs>(moved, from.get<game::Needs>(old));
        into.emplace<game::Body>(moved, game::CellCentre(CELL + 1), game::CellCentre(CELL + 1));
    });

    game::Deactivate(state, npc);
    REQUIRE(state.DomainOf(npc) == game::WORLD);
    const game::Npc& person = state[game::WORLD].Entities().get<game::Npc>(state[game::WORLD].Find(npc));
    CHECK(person.cell == CELL);
    CHECK(person.home == HOME);
}

TEST_CASE("a group materialises into one world NPC per member, and an unmet NPC folds back") {
    constexpr u32 MEMBERS = 3;
    core::State state{ game::DOMAINS, SEED };
    game::Relationships relationships;
    const core::StableId group = game::SpawnGroup(state, CELL, MEMBERS);

    const std::vector<core::StableId> npcs = game::Materialise(state, group);
    REQUIRE(npcs.size() == MEMBERS);
    CHECK_FALSE(state.DomainOf(group).has_value());
    for (const core::StableId npc : npcs) {
        CHECK(state[game::WORLD].Entities().get<game::Npc>(state[game::WORLD].Find(npc)).cell == CELL);
    }

    const core::StableId other = game::SpawnGroup(state, CELL, 0);
    const std::array<Pair, 2> knows{ { { npcs[0], npcs[1] }, { npcs[1], npcs[0] } } };
    relationships.Merge(knows);
    game::Travel travel;
    game::Fold(state, relationships, travel, npcs[0], other);
    CHECK_FALSE(state.DomainOf(npcs[0]).has_value());
    CHECK(state[game::WORLD].Entities().get<game::Group>(state[game::WORLD].Find(other)).members == 1);
    CHECK(relationships.All().empty());
}

TEST_CASE("a folded traveller leaves no journey or journal behind") {
    Traveller world;
    game::Relationships relationships;
    REQUIRE(world.travel.Travelling(world.npc));
    REQUIRE_FALSE(world.travel.JournalOf(world.npc).empty());

    game::Fold(world.state, relationships, world.travel, world.npc, world.group);
    CHECK_FALSE(world.travel.Travelling(world.npc));
    CHECK(world.travel.Journeys().empty());
    CHECK(world.travel.JournalOf(world.npc).empty());
}

TEST_CASE("a save after a fold loads") {
    Traveller world;
    game::Relationships relationships;
    game::Fold(world.state, relationships, world.travel, world.npc, world.group);

    // Built here rather than by Host::Save(): a Host exposes its state and travel only as const, so it can't fold an NPC.
    const core::GameData data = test::ShippedData();
    const std::vector<std::byte> bytes = test::ArenaSave(
        world.state,
        {},
        std::nullopt,
        std::nullopt,
        std::nullopt,
        game::WORLD_SCENE,
        test::WORLD_VERSION,
        test::ACTIVE_VERSION,
        {},
        world.travel,
        std::nullopt,
        game::Relationships{},
        {},
        &data
    );
    game::Host host(test::TestManifest(), game::ARENA_SCENE, data);
    core::Reader reader{ bytes };
    CHECK(host.Load(reader, 0));
}

TEST_CASE("relationships merge into one sorted edge per pair, adding once per change") {
    constexpr core::StableId FIRST{ 1 };
    constexpr core::StableId SECOND{ 2 };
    constexpr core::StableId THIRD{ 3 };
    game::Relationships relationships;
    const std::array<Pair, 2> before{ { { SECOND, FIRST }, { SECOND, THIRD } } };
    relationships.Merge(before);
    const std::array<Pair, 3> after{ { { FIRST, THIRD }, { SECOND, THIRD }, { SECOND, THIRD } } };
    relationships.Merge(after);

    REQUIRE(relationships.All().size() == 3);
    CHECK(relationships.Of(FIRST).size() == 1);
    REQUIRE(relationships.Of(SECOND).size() == 2);
    CHECK(relationships.Of(SECOND)[0].subject == FIRST);
    CHECK(relationships.Of(SECOND)[0].opinion == 1.0F);
    CHECK(relationships.Of(SECOND)[1].subject == THIRD);
    CHECK(relationships.Of(SECOND)[1].opinion == doctest::Approx(3));
}

TEST_CASE("an active NPC walks towards its target and stops there") {
    core::State state{ game::DOMAINS, SEED };
    const core::StableId npc = game::SpawnNpc(state, CELL);
    game::Activate(state, npc);
    core::Domain& active = state[game::ACTIVE];
    const core::Entity entity = active.Find(npc);
    const core::Vec2 start = active.Entities().get<game::Body>(entity).current;
    constexpr core::Vec2 STEP{ .x = 3.0F, .y = 0.0F };
    active.Entities().get<game::Wander>(entity).target = start + STEP;

    game::WalkActive(active);
    const game::Body& body = active.Entities().get<game::Body>(entity);
    CHECK(body.previous.x == start.x);
    CHECK(body.current.x > start.x);
    CHECK(body.current.x < start.x + STEP.x);
    game::WalkActive(active);
    CHECK(active.Entities().get<game::Body>(entity).current.x == start.x + STEP.x);
}

TEST_CASE("an active NPC decides once in every decision slice") {
    core::State state{ game::DOMAINS, SEED };
    const core::StableId npc = game::SpawnNpc(state, CELL);
    game::Activate(state, npc);
    core::Domain& active = state[game::ACTIVE];
    u32 decisions = 0;
    core::Vec2 target = active.Entities().get<game::Wander>(active.Find(npc)).target;
    for (u64 tick = 1; tick <= game::DECISION_TICKS; ++tick) {
        game::DecideActive(active, SEED, tick, tick);
        const core::Vec2 now = active.Entities().get<game::Wander>(active.Find(npc)).target;
        decisions += Same(now, target) ? 0U : 1U;
        target = now;
    }
    CHECK(decisions == 1);
    CHECK(game::CellAt(target) == CELL);
}

// A world of `npcs` NPCs over a few cells, stepped `months` times on `workers` workers.
std::pair<core::State, game::Relationships> Stepped(u32 npcs, u32 months, u32 workers) {
    constexpr u32 CELLS = 16;
    core::State state{ game::DOMAINS, SEED };
    for (u32 npc = 0; npc < npcs; ++npc) {
        game::SpawnNpc(state, npc % CELLS);
    }
    game::Relationships relationships;
    game::MonthStep step;
    core::Jobs jobs{ workers };
    for (u64 month = 1; month <= months; ++month) {
        step.Run(state, relationships, jobs, month);
    }
    return { std::move(state), std::move(relationships) };
}

TEST_CASE("NPCs who socialise build relationships with others in their cell") {
    constexpr u32 NPCS = 64;
    constexpr u32 MONTHS = 12;
    const auto [state, relationships] = Stepped(NPCS, MONTHS, 1);
    REQUIRE_FALSE(relationships.All().empty());
    const auto& world = state[game::WORLD];
    for (const game::Relationship& edge : relationships.All()) {
        CHECK(edge.holder != edge.subject);
        const auto cell = [&](core::StableId npc) { return world.Entities().get<game::Npc>(world.Find(npc)).cell; };
        CHECK(cell(edge.holder) == cell(edge.subject));
    }
}

TEST_CASE("a month step gives the same world with one worker and with many") {
    constexpr u32 NPCS = 5'000; // several chunks
    constexpr u32 MONTHS = 6;
    const auto [one, oneRelationships] = Stepped(NPCS, MONTHS, 1);
    const auto [many, manyRelationships] = Stepped(NPCS, MONTHS, MANY_WORKERS);
    const auto& oneWorld = one[game::WORLD].Entities();
    const auto& manyWorld = many[game::WORLD].Entities();
    // All 5,000 are the equivalence being tested, so each is compared and the verdict asserted once.
    bool same = true;
    for (const auto [entity, stableId, person, want] :
         oneWorld.view<const core::StableId, const game::Npc, const game::Needs>().each()) {
        const core::Entity other = many[game::WORLD].Find(stableId);
        same = same && Same(manyWorld.get<game::Npc>(other).power, person.power) &&
               manyWorld.get<game::Npc>(other).action == person.action &&
               Same(manyWorld.get<game::Needs>(other).company, want.company) &&
               Same(manyWorld.get<game::Needs>(other).rest, want.rest);
    }
    CHECK(same);
    REQUIRE(oneRelationships.All().size() == manyRelationships.All().size());
    CHECK(std::ranges::equal(oneRelationships.All(), manyRelationships.All(), [](const auto& lhs, const auto& rhs) {
        return lhs.holder == rhs.holder && lhs.subject == rhs.subject && Same(lhs.opinion, rhs.opinion);
    }));
}

TEST_CASE("a traveller sits the month out and nobody meets it") {
    constexpr u32 NPCS = 64;
    constexpr u64 MONTHS = 12;
    core::State state{ game::DOMAINS, SEED };
    std::vector<core::StableId> travellers;
    for (u32 npc = 0; npc < NPCS; ++npc) {
        const core::StableId spawned = game::SpawnNpc(state, CELL);
        if (npc % 2 == 0) {
            state[game::WORLD].Entities().get<game::Npc>(state[game::WORLD].Find(spawned)).action = game::Action::Travel;
            travellers.push_back(spawned);
        }
    }
    game::Relationships relationships;
    game::MonthStep step;
    core::Jobs jobs{ 1 };
    for (u64 month = 1; month <= MONTHS; ++month) {
        step.Run(state, relationships, jobs, month);
    }
    REQUIRE_FALSE(relationships.All().empty()); // those at home met each other
    const auto& world = state[game::WORLD].Entities();
    const bool untouched = std::ranges::all_of(travellers, [&](core::StableId npc) {
        const core::Entity entity = state[game::WORLD].Find(npc);
        const game::Needs& want = world.get<game::Needs>(entity);
        return world.get<game::Npc>(entity).action == game::Action::Travel && want.rest == 0.0F && want.company == 0.0F;
    });
    CHECK(untouched);
    const bool unmet = std::ranges::none_of(relationships.All(), [&](const game::Relationship& edge) {
        return std::ranges::contains(travellers, edge.holder) || std::ranges::contains(travellers, edge.subject);
    });
    CHECK(unmet);
}

TEST_CASE("a month step stopped before it starts changes nothing") {
    constexpr u32 NPCS = 64;
    core::State state{ game::DOMAINS, SEED };
    for (u32 npc = 0; npc < NPCS; ++npc) {
        game::SpawnNpc(state, 0);
    }
    game::Relationships relationships;
    game::MonthStep step;
    core::Jobs jobs{ 1 };
    std::stop_source stop;
    stop.request_stop();
    step.Run(state, relationships, jobs, 1, stop.get_token());
    CHECK(relationships.All().empty());
    for (const auto [entity, want] : state[game::WORLD].Entities().view<const game::Needs>().each()) {
        CHECK(want.company == 0.0F);
        CHECK(want.rest == 0.0F);
    }
}

TEST_CASE("starting power is the same for the same identity and seed") {
    for (u64 id = 1; id <= FEW; ++id) {
        CHECK(Same(game::StartingPower(SEED, core::StableId{ id }), game::StartingPower(SEED, core::StableId{ id })));
    }
    // a different seed gives a different spread: some of the ids differ
    u32 differing = 0;
    for (u64 id = 1; id <= FEW; ++id) {
        differing += Same(game::StartingPower(SEED, core::StableId{ id }), game::StartingPower(SEED + 1, core::StableId{ id }))
                         ? 0U
                         : 1U;
    }
    CHECK(differing > 0);
}

// Fixed draws under SEED, each the first stable ID whose draw lands in its band.
TEST_CASE("starting power for named identities under a fixed seed") {
    constexpr f32 TOLERANCE = 1e-3F;
    constexpr f32 MORTAL = 2.90823F;
    constexpr f32 CULTIVATOR = 25.0162F;
    constexpr f32 STRONG = 465.879F;
    CHECK(game::StartingPower(SEED, core::StableId{ 2 }) == doctest::Approx(MORTAL).epsilon(TOLERANCE));     // first mortal
    CHECK(game::StartingPower(SEED, core::StableId{ 1 }) == doctest::Approx(CULTIVATOR).epsilon(TOLERANCE)); // first cultivator
    CHECK(game::StartingPower(SEED, core::StableId{ 41 }) == doctest::Approx(STRONG).epsilon(TOLERANCE));    // first strong
}

TEST_CASE("starting power lies in its bands") {
    constexpr f32 STRONGEST = 500.0F;
    f32 lowest = STRONGEST;
    f32 highest = 0.0F;
    for (u64 id = 1; id <= MANY; ++id) {
        const f32 power = game::StartingPower(SEED, core::StableId{ id });
        lowest = std::min(lowest, power);
        highest = std::max(highest, power);
    }
    CHECK(lowest >= 0.0F);
    CHECK(highest < STRONGEST);
}

TEST_CASE("the arena's and stress's NPCs start without power") {
    for (const core::SceneKey scene : { game::ARENA_SCENE, game::STRESS_SCENE }) {
        game::Host const host(test::TestManifest(), scene);
        u32 people = 0;
        u32 powered = 0;
        for (const core::DomainIndex domain : { game::WORLD, game::ACTIVE }) {
            for (const auto [entity, person] : host.State()[domain].Entities().view<const game::Npc>().each()) {
                powered += person.power == 0.0F ? 0U : 1U;
                ++people;
            }
        }
        CHECK(people > 0);
        CHECK(powered == 0);
    }
}

} // namespace
