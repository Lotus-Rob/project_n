#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/instances.hpp"
#include "core/jobs.hpp"
#include "core/state.hpp"
#include "game/arena.hpp"
#include "game/combat.hpp"
#include "game/components.hpp"
#include "game/npcs.hpp"
#include "support/manifest.hpp"

#include <doctest/doctest.h>

namespace {

using core::atlas::SpriteId;

constexpr std::size_t FOXES = 12;
constexpr i64 CULTIVATOR_HEALTH = 1'000;
constexpr i64 FOX_HEALTH = 1'200;
constexpr u64 AURA_TICK = 60;
constexpr core::Rect GROUND{ .min = {}, .max = { .x = 1'920.0F, .y = 1'080.0F } };
constexpr f32 ON_THE_EDGE = 1e-3F;
constexpr u32 DISSOLVED = 0xFFFFU;
constexpr u32 HALF_BITS = 16;

core::TickCounters At(u64 tick) {
    return { .wall = tick, .simulation = tick, .state = tick };
}

// Ticks `arena` from `first` to `last`, holding nothing, ignoring its events.
void Run(game::Arena& arena, u64 first, u64 last) {
    std::vector<core::Event> events;
    for (u64 tick = first; tick <= last; ++tick) {
        events.clear();
        arena.Tick(At(tick), {}, events);
    }
}

// What `arena` publishes at simulation tick `tick`.
core::Snapshot Published(game::Arena& arena, u64 tick) {
    core::Jobs jobs{ 1 };
    core::Snapshot snapshot;
    snapshot.simulationTick = tick;
    arena.Publish(snapshot, jobs);
    return snapshot;
}

const core::Registry& Entities(const game::Arena& arena) {
    return arena.State()[game::ACTIVE].Entities();
}

core::Entity EntityOf(const game::Arena& arena, core::StableId stableId) {
    return arena.State()[game::ACTIVE].Find(stableId);
}

std::vector<core::StableId> Foxes(const game::Arena& arena) {
    std::vector<core::StableId> foxes;
    for (auto [entity, creature, stable] : Entities(arena).view<const game::Creature, const core::StableId>().each()) {
        if (creature.kind == game::CreatureKind::Fox) {
            foxes.push_back(stable);
        }
    }
    std::ranges::sort(foxes);
    return foxes;
}

game::Health HealthOf(const game::Arena& arena, core::StableId stableId) {
    return Entities(arena).get<game::Health>(EntityOf(arena, stableId));
}

core::Vec2 FootOf(const game::Arena& arena, core::StableId stableId) {
    return Entities(arena).get<game::Body>(EntityOf(arena, stableId)).current;
}

f32 Distance(core::Vec2 start, core::Vec2 end) {
    return std::hypot(end.x - start.x, end.y - start.y);
}

// Game data whose foxes circle 1,000 units round the player, where the crystal's aura never reaches them.
core::GameData FarFromTheCrystal() {
    constexpr f32 FAR_ORBIT = 1'000.0F;
    core::GameData data = game::BuiltInData();
    data.fox.orbitRadius = FAR_ORBIT;
    data.fox.orbitWobble = 0.0F;
    return data;
}

TEST_CASE("every creature starts at its kind's health") {
    const game::Arena arena(test::TestManifest());
    CHECK(HealthOf(arena, arena.Player()).current == CULTIVATOR_HEALTH);
    CHECK(HealthOf(arena, arena.Player()).max == CULTIVATOR_HEALTH);
    const std::vector<core::StableId> foxes = Foxes(arena);
    REQUIRE(foxes.size() == FOXES);
    for (const core::StableId fox : foxes) {
        CHECK(HealthOf(arena, fox).current == FOX_HEALTH);
        CHECK(HealthOf(arena, fox).max == FOX_HEALTH);
    }
}

// Game data that walks the cultivator to 100 units from the crystal in one tick and circles the foxes 300 units round
// it, which puts some of them in the aura's reach (as the arena's aura tests have it).
core::GameData NearTheCrystal() {
    constexpr f32 WALK_SPEED = 440.0F;
    constexpr f32 FOX_ORBIT = 300.0F;
    core::GameData data = game::BuiltInData();
    data.cultivator.walkSpeed = WALK_SPEED;
    data.fox.orbitRadius = FOX_ORBIT;
    data.fox.orbitWobble = 0.0F;
    return data;
}

// The arena near the crystal, the player walked to it on tick 1 and damaged by `wound` at tick 2, ticked to the aura's
// first hit; the hits of that tick.
std::vector<core::Event> AuraAfterWound(game::Arena& arena, i64 wound) {
    constexpr core::HeldInput LEFT{ .move = { .x = -1.0F, .y = 0.0F } };
    std::vector<core::Event> events;
    arena.Tick(At(1), { .actions = {}, .held = LEFT }, events);
    arena.Damage(arena.Player(), wound, 2);
    Run(arena, 2, AURA_TICK - 1);
    events.clear();
    arena.Tick(At(AURA_TICK), {}, events);
    return events;
}

TEST_CASE("the aura's damage lowers a fox's health and heals the player up to its most") {
    constexpr i64 DEEP = 500;
    constexpr i64 SCRATCH = 10; // less than the least healing
    game::Arena arena(test::TestManifest(), game::Scene::Arena, NearTheCrystal());
    const std::vector<core::Event> events = AuraAfterWound(arena, DEEP);
    std::size_t damaged = 0;
    for (const core::Event& event : events) {
        if (event.kind != core::EventKind::Hit || event.target == arena.Player()) {
            continue;
        }
        ++damaged;
        CHECK(event.hit == core::HitKind::Damage);
        CHECK(HealthOf(arena, event.target).current == FOX_HEALTH - event.value);
    }
    CHECK(damaged > 0);
    const auto heal = std::ranges::find(events, arena.Player(), &core::Event::target);
    REQUIRE(heal != events.end());
    CHECK(heal->hit == core::HitKind::Healing);
    CHECK(HealthOf(arena, arena.Player()).current == CULTIVATOR_HEALTH - DEEP + heal->value);

    game::Arena scratched(test::TestManifest(), game::Scene::Arena, NearTheCrystal());
    const std::vector<core::Event> again = AuraAfterWound(scratched, SCRATCH);
    REQUIRE(std::ranges::contains(again, scratched.Player(), &core::Event::target));
    CHECK(HealthOf(scratched, scratched.Player()).current == CULTIVATOR_HEALTH);
}

TEST_CASE("health at zero gives a dying state, then removal, then a respawn on the edge") {
    constexpr u64 DEATH = 5;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, FarFromTheCrystal());
    Run(arena, 1, DEATH);
    const std::vector<core::StableId> before = Foxes(arena);
    const core::StableId fox = before.front();
    arena.Damage(fox, FOX_HEALTH, DEATH);
    CHECK(HealthOf(arena, fox).current == 0);
    const game::Dying* const dying = Entities(arena).try_get<game::Dying>(EntityOf(arena, fox));
    REQUIRE(dying != nullptr);
    CHECK(dying->startTick == DEATH);

    Run(arena, DEATH + 1, DEATH + game::DYING_TICKS - 1);
    CHECK(arena.State().DomainOf(fox).has_value());
    Run(arena, DEATH + game::DYING_TICKS, DEATH + game::DYING_TICKS);
    CHECK_FALSE(arena.State().DomainOf(fox).has_value());
    CHECK(Foxes(arena).size() == FOXES - 1);

    const u64 respawn = DEATH + game::DYING_TICKS + game::RESPAWN_TICKS;
    Run(arena, DEATH + game::DYING_TICKS + 1, respawn - 1);
    CHECK(Foxes(arena).size() == FOXES - 1);
    Run(arena, respawn, respawn);
    const std::vector<core::StableId> after = Foxes(arena);
    REQUIRE(after.size() == FOXES);
    std::vector<core::StableId> added;
    std::ranges::set_difference(after, before, std::back_inserter(added));
    REQUIRE(added.size() == 1);
    const core::Vec2 foot = FootOf(arena, added.front());
    INFO("the new fox stands at ", foot.x, ", ", foot.y);
    const bool onEdge = std::abs(foot.x - GROUND.min.x) < ON_THE_EDGE || std::abs(foot.x - GROUND.max.x) < ON_THE_EDGE ||
                        std::abs(foot.y - GROUND.min.y) < ON_THE_EDGE || std::abs(foot.y - GROUND.max.y) < ON_THE_EDGE;
    CHECK(onEdge);
    CHECK(foot.x >= GROUND.min.x - ON_THE_EDGE);
    CHECK(foot.x <= GROUND.max.x + ON_THE_EDGE);
    CHECK(foot.y >= GROUND.min.y - ON_THE_EDGE);
    CHECK(foot.y <= GROUND.max.y + ON_THE_EDGE);
    CHECK(HealthOf(arena, added.front()).current == FOX_HEALTH);
}

// Kills the first fox at tick 1 and ticks to its respawn; the new fox.
core::StableId Respawned(game::Arena& arena) {
    Run(arena, 1, 1);
    const std::vector<core::StableId> before = Foxes(arena);
    arena.Damage(before.front(), FOX_HEALTH, 1);
    Run(arena, 2, 1 + game::DYING_TICKS + game::RESPAWN_TICKS);
    std::vector<core::StableId> added;
    std::ranges::set_difference(Foxes(arena), before, std::back_inserter(added));
    REQUIRE(added.size() == 1);
    return added.front();
}

TEST_CASE("a respawned fox eases into its orbit") {
    constexpr u64 AFTERWARDS = 60;
    // An eased arrival's fastest step is 1.5 times its mean: twice leaves room without allowing a snap.
    constexpr f32 TWICE = 2.0F;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, FarFromTheCrystal());
    const core::StableId fox = Respawned(arena);
    const u64 arrived = 1 + game::DYING_TICKS + game::RESPAWN_TICKS + game::ARRIVE_TICKS;
    const core::StableId other = Foxes(arena).front(); // an orbiting fox: the respawned one has the newest ID
    REQUIRE(other != fox);
    const core::Vec2 from = FootOf(arena, fox);
    const game::Orbit orbit = Entities(arena).get<game::Orbit>(EntityOf(arena, fox));
    REQUIRE(Entities(arena).all_of<game::Arriving>(EntityOf(arena, fox)));

    std::vector<core::Vec2> path{ from };
    std::vector<core::Vec2> otherPath{ FootOf(arena, other) };
    f32 farthest = 0.0F;
    for (u64 tick = arrived - game::ARRIVE_TICKS + 1; tick < arrived; ++tick) {
        Run(arena, tick, tick);
        path.push_back(FootOf(arena, fox));
        otherPath.push_back(FootOf(arena, other));
        farthest = std::max(farthest, Distance(from, game::OrbitPoint(orbit, tick)));
        CHECK(Entities(arena).all_of<game::Arriving>(EntityOf(arena, fox)));
    }
    CHECK(Distance(path.back(), game::OrbitPoint(orbit, arrived - 1)) > 0.0F); // still on its way
    f32 orbitStep = 0.0F;
    for (std::size_t index = 1; index < otherPath.size(); ++index) {
        orbitStep = std::max(orbitStep, Distance(otherPath.at(index - 1), otherPath.at(index)));
    }
    const f32 approach = TWICE * farthest / static_cast<f32>(game::ARRIVE_TICKS);
    for (std::size_t index = 1; index < path.size(); ++index) {
        INFO("tick ", index, " of its arrival");
        CHECK(Distance(path.at(index - 1), path.at(index)) <= (TWICE * orbitStep) + approach);
    }
    for (u64 tick = arrived; tick < arrived + AFTERWARDS; ++tick) {
        Run(arena, tick, tick);
        const core::Vec2 foot = FootOf(arena, fox);
        const core::Vec2 circled = game::OrbitPoint(orbit, tick);
        CHECK(foot.x == circled.x);
        CHECK(foot.y == circled.y);
    }
    CHECK_FALSE(Entities(arena).all_of<game::Arriving>(EntityOf(arena, fox)));
}

// The index in `snapshot` of the instance drawing a fox whose foot stands at `foot`.
std::optional<std::size_t> FoxInstanceAt(const core::Snapshot& snapshot, core::Vec2 foot) {
    const core::Instance expected = core::MakeInstance(*test::TestManifest(), { .sprite = SpriteId::Fox, .current = foot });
    for (std::size_t index = 0; index < snapshot.instances.size(); ++index) {
        const core::Instance& instance = snapshot.instances.at(index);
        if (instance.regionMin == expected.regionMin &&
            std::bit_cast<u64>(instance.current) == std::bit_cast<u64>(expected.current)) {
            return index;
        }
    }
    return std::nullopt;
}

TEST_CASE("a dying fox carries no ID, label or health bar") {
    constexpr u64 DEATH = 3;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, FarFromTheCrystal());
    Run(arena, 1, DEATH);
    const core::StableId fox = Foxes(arena).front();
    arena.Damage(fox, FOX_HEALTH, DEATH);
    u32 last = 0;
    for (u64 tick = DEATH; tick < DEATH + game::DYING_TICKS; ++tick) {
        if (tick > DEATH) {
            Run(arena, tick, tick);
        }
        INFO("tick ", tick);
        const core::Snapshot snapshot = Published(arena, tick);
        const std::optional<std::size_t> drawn = FoxInstanceAt(snapshot, FootOf(arena, fox));
        REQUIRE(drawn.has_value());
        CHECK(snapshot.ids.at(drawn.value_or(0)) == core::StableId{});
        CHECK_FALSE(std::ranges::contains(snapshot.ids, fox));
        const u32 dissolve = snapshot.instances.at(drawn.value_or(0)).layerFlags >> HALF_BITS;
        if (tick == DEATH) {
            CHECK(dissolve == 0);
        }
        else {
            CHECK(dissolve > last);
        }
        last = dissolve;
        CHECK_FALSE(std::ranges::contains(snapshot.texts, fox, &core::TextItem::entity));
        CHECK_FALSE(std::ranges::contains(snapshot.healthBars, fox, &core::HealthBar::entity));
    }
    CHECK(last == DISSOLVED);
}

TEST_CASE("the selection clears when the selected fox is removed") {
    constexpr u64 DEATH = 1;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, FarFromTheCrystal());
    const core::StableId fox = Foxes(arena).front();
    const std::array<core::Action, 1> select{ { { .kind = core::ActionKind::Select, .target = fox } } };
    std::vector<core::Event> events;
    arena.Tick(At(DEATH), { .actions = select, .held = {} }, events);
    REQUIRE(arena.Selected() == fox);
    arena.Damage(fox, FOX_HEALTH, DEATH);
    Run(arena, DEATH + 1, DEATH + game::DYING_TICKS - 1);
    CHECK(arena.Selected() == fox);
    CHECK_FALSE(Published(arena, DEATH + game::DYING_TICKS - 1).selected.has_value()); // a dying fox isn't outlined
    Run(arena, DEATH + game::DYING_TICKS, DEATH + game::DYING_TICKS);
    CHECK(arena.Selected() == core::StableId{});
}

TEST_CASE("a dying fox can't be selected") {
    constexpr u64 DEATH = 1;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, FarFromTheCrystal());
    const core::StableId fox = Foxes(arena).front();
    arena.Damage(fox, FOX_HEALTH, 0);
    const std::array<core::Action, 1> select{ { { .kind = core::ActionKind::Select, .target = fox } } };
    std::vector<core::Event> events;
    arena.Tick(At(DEATH), { .actions = select, .held = {} }, events);
    CHECK(arena.Selected() == core::StableId{});
}

TEST_CASE("the player removed on the tick a fox respawns leaves no player, and the fox circles the start") {
    constexpr u64 FOX_DEATH = 1;
    constexpr u64 PLAYER_DEATH = FOX_DEATH + game::RESPAWN_TICKS; // both removed on the respawn's tick
    constexpr core::Vec2 START{ .x = 960.0F, .y = 620.0F };       // where the cultivator starts
    game::Arena arena(test::TestManifest(), game::Scene::Arena, FarFromTheCrystal());
    const std::vector<core::StableId> before = Foxes(arena);
    const core::StableId player = arena.Player();
    Run(arena, 1, FOX_DEATH);
    arena.Damage(before.front(), FOX_HEALTH, FOX_DEATH);
    Run(arena, FOX_DEATH + 1, PLAYER_DEATH);
    arena.Damage(player, CULTIVATOR_HEALTH, PLAYER_DEATH);
    const u64 respawn = FOX_DEATH + game::DYING_TICKS + game::RESPAWN_TICKS;
    REQUIRE(respawn == PLAYER_DEATH + game::DYING_TICKS);
    Run(arena, PLAYER_DEATH + 1, respawn);
    CHECK(arena.Player() == core::StableId{});
    CHECK_FALSE(arena.State().DomainOf(player).has_value());
    std::vector<core::StableId> added;
    std::ranges::set_difference(Foxes(arena), before, std::back_inserter(added));
    REQUIRE(added.size() == 1);
    const game::Orbit orbit = Entities(arena).get<game::Orbit>(EntityOf(arena, added.front()));
    CHECK(orbit.centre.x == START.x);
    CHECK(orbit.centre.y == START.y);
}

TEST_CASE("each living enemy in a combat scene reports its health over its head") {
    constexpr i64 WOUND = 300;
    constexpr f32 THREE_QUARTERS = 0.75F;
    constexpr u64 TICK = 2;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, FarFromTheCrystal());
    Run(arena, 1, TICK);
    const std::vector<core::StableId> foxes = Foxes(arena);
    arena.Damage(foxes.at(0), WOUND, TICK);
    arena.Damage(foxes.at(1), FOX_HEALTH, TICK);
    const core::Snapshot snapshot = Published(arena, TICK);
    CHECK(snapshot.healthBars.size() == FOXES - 1);
    CHECK_FALSE(std::ranges::contains(snapshot.healthBars, arena.Player(), &core::HealthBar::entity));
    CHECK_FALSE(std::ranges::contains(snapshot.healthBars, foxes.at(1), &core::HealthBar::entity));
    for (const core::HealthBar& bar : snapshot.healthBars) {
        INFO("fox ", std::to_underlying(bar.entity));
        CHECK(std::ranges::contains(foxes, bar.entity));
        const auto label = std::ranges::find(snapshot.texts, bar.entity, &core::TextItem::entity);
        REQUIRE(label != snapshot.texts.end());
        CHECK(bar.previous.x == label->previous.x);
        CHECK(bar.previous.y == label->previous.y);
        CHECK(bar.current.x == label->current.x);
        CHECK(bar.current.y == label->current.y);
        CHECK(bar.fraction == (bar.entity == foxes.at(0) ? THREE_QUARTERS : 1.0F));
    }

    game::Arena stress(test::TestManifest(), game::Scene::Stress);
    CHECK(Published(stress, 0).healthBars.empty());
}

TEST_CASE("new game data sets a creature's most health, keeping what it has up to it") {
    constexpr i64 WOUND = 300;
    constexpr i64 LOWER_MOST = 500;
    constexpr i64 HIGHER_MOST = 2'000;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, FarFromTheCrystal());
    const std::vector<core::StableId> foxes = Foxes(arena);
    arena.Damage(foxes.at(0), WOUND, 0);
    core::GameData data = FarFromTheCrystal();
    data.fox.health = HIGHER_MOST;
    arena.SetData(data);
    CHECK(HealthOf(arena, foxes.at(0)).current == FOX_HEALTH - WOUND);
    CHECK(HealthOf(arena, foxes.at(0)).max == HIGHER_MOST);
    data.fox.health = LOWER_MOST;
    arena.SetData(data);
    CHECK(HealthOf(arena, foxes.at(0)).current == LOWER_MOST);
    CHECK(HealthOf(arena, foxes.at(1)).current == LOWER_MOST);
    CHECK(HealthOf(arena, foxes.at(1)).max == LOWER_MOST);
}

TEST_CASE("damage to a dying or removed creature does nothing") {
    constexpr u64 DEATH = 1;
    constexpr i64 MORE = 100;
    constexpr i64 HEALING = -500;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, FarFromTheCrystal());
    Run(arena, 1, DEATH);
    const core::StableId fox = Foxes(arena).front();
    arena.Damage(fox, FOX_HEALTH, DEATH);
    arena.Damage(fox, MORE, DEATH + 1);
    arena.Damage(fox, HEALING, DEATH + 1);
    CHECK(HealthOf(arena, fox).current == 0);
    CHECK(Entities(arena).get<game::Dying>(EntityOf(arena, fox)).startTick == DEATH);
    Run(arena, DEATH + 1, DEATH + game::DYING_TICKS);
    REQUIRE_FALSE(arena.State().DomainOf(fox).has_value());
    const u64 before = arena.StateHash();
    arena.Damage(fox, MORE, DEATH + game::DYING_TICKS);
    CHECK(arena.StateHash() == before);
}

TEST_CASE("an aura amount passes through the one function every hit's amount does") {
    constexpr i64 BASE = 345;
    CHECK(game::HitAmount(BASE) == BASE);
    CHECK(game::HitAmount(core::AbilityData{}, BASE) == BASE);
}

TEST_CASE("health is valid from nothing to its most") {
    constexpr i64 MOST = 1'200;
    CHECK(game::Health{ .current = 0, .max = MOST }.Valid());
    CHECK(game::Health{ .current = MOST, .max = MOST }.Valid());
    CHECK_FALSE(game::Health{ .current = MOST + 1, .max = MOST }.Valid());
    CHECK_FALSE(game::Health{ .current = -1, .max = MOST }.Valid());
    CHECK_FALSE(game::Health{ .current = 0, .max = 0 }.Valid());
}

constexpr i64 QI_START = 50;
constexpr i64 QI_MAX = 100;
constexpr i64 BOLT_GAIN = 10;
constexpr u64 BOLT_COOLDOWN = 15;
constexpr i64 LANCE_COST = 25;
constexpr i64 SWORDS_COST = 60;
constexpr f32 DASH_DISTANCE = 260.0F;
constexpr u64 DASH_TICKS = 9;
constexpr u64 DASH_COOLDOWN = 60;
constexpr core::Vec2 FAR_RIGHT{ .x = 5'000.0F, .y = 0.0F };

// Ticks `arena` once at `tick`, casting the ability in `slot` at `point` and holding `held`; the tick's events.
std::vector<core::Event> CastAt(
    game::Arena& arena,
    u64 tick,
    core::Slot slot,
    core::Vec2 point = {},
    core::HeldInput held = {}
) {
    const std::array<core::Action, 1> cast{ { { .kind = core::ActionKind::Cast, .slot = slot, .point = point } } };
    std::vector<core::Event> events;
    arena.Tick(At(tick), { .actions = cast, .held = held }, events);
    return events;
}

const game::Caster& CasterOf(const game::Arena& arena) {
    return Entities(arena).get<game::Caster>(EntityOf(arena, arena.Player()));
}

TEST_CASE("the player starts with the data's starting Qi") {
    const game::Arena arena(test::TestManifest());
    CHECK(CasterOf(arena).qi == QI_START);
    CHECK(CasterOf(arena).qi == game::BuiltInData().abilities.qiStart);
}

TEST_CASE("a cast in the combat scene has an effect: Qi changes and the slot's cooldown starts") {
    constexpr u64 TICK = 7;
    game::Arena casting(test::TestManifest());
    game::Arena idle(test::TestManifest());
    CastAt(casting, TICK, core::Slot::Generator);
    Run(idle, TICK, TICK);
    CHECK(CasterOf(casting).qi == QI_START + BOLT_GAIN);
    CHECK(CasterOf(casting).readyTick.at(std::to_underlying(core::Slot::Generator)) == TICK + BOLT_COOLDOWN);
    CHECK(casting.StateHash() != idle.StateHash());
}

TEST_CASE("the generator gains Qi up to the most; the spender and a special cost it") {
    game::Arena arena(test::TestManifest());
    u64 tick = 1;
    for (i64 expected = QI_START + BOLT_GAIN; expected < QI_MAX; expected += BOLT_GAIN) {
        CastAt(arena, tick, core::Slot::Generator);
        CHECK(CasterOf(arena).qi == expected);
        tick += BOLT_COOLDOWN;
    }
    CastAt(arena, tick, core::Slot::Generator);
    CHECK(CasterOf(arena).qi == QI_MAX);
    tick += BOLT_COOLDOWN;
    CastAt(arena, tick, core::Slot::Generator);
    CHECK(CasterOf(arena).qi == QI_MAX);

    CastAt(arena, tick, core::Slot::Spender);
    CHECK(CasterOf(arena).qi == QI_MAX - LANCE_COST);
    CastAt(arena, tick, core::Slot::Special1);
    CHECK(CasterOf(arena).qi == QI_MAX - LANCE_COST - SWORDS_COST);
}

TEST_CASE("a cooldown blocks a recast until it's over") {
    constexpr u64 FIRST = 10;
    game::Arena arena(test::TestManifest());
    CastAt(arena, FIRST, core::Slot::Generator);
    CastAt(arena, FIRST + BOLT_COOLDOWN - 1, core::Slot::Generator);
    CHECK(CasterOf(arena).qi == QI_START + BOLT_GAIN);
    Run(arena, FIRST + BOLT_COOLDOWN, FIRST + (2 * BOLT_COOLDOWN));
    CHECK(CasterOf(arena).qi == QI_START + BOLT_GAIN); // the refused cast wasn't queued
    CastAt(arena, FIRST + (2 * BOLT_COOLDOWN), core::Slot::Generator);
    CHECK(CasterOf(arena).qi == QI_START + (2 * BOLT_GAIN));
}

TEST_CASE("a cast at the tick the cooldown ends goes off") {
    constexpr u64 FIRST = 10;
    game::Arena arena(test::TestManifest());
    CastAt(arena, FIRST, core::Slot::Generator);
    CastAt(arena, FIRST + BOLT_COOLDOWN, core::Slot::Generator);
    CHECK(CasterOf(arena).qi == QI_START + (2 * BOLT_GAIN));
}

TEST_CASE("a cast without enough Qi does nothing but report it") {
    constexpr i64 SHORT = 10;
    core::GameData data = game::BuiltInData();
    data.abilities.qiStart = SHORT;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, data);
    const std::vector<core::Event> events = CastAt(arena, 1, core::Slot::Spender);
    REQUIRE(events.size() == 1);
    CHECK(events.front().kind == core::EventKind::NotEnoughQi);
    CHECK(events.front().value == static_cast<i64>(std::to_underlying(core::Slot::Spender)));
    CHECK(CasterOf(arena).qi == SHORT);
    CHECK(CasterOf(arena).readyTick.at(std::to_underlying(core::Slot::Spender)) == 0);
}

TEST_CASE("an empty slot casts nothing") {
    game::Arena casting(test::TestManifest());
    game::Arena idle(test::TestManifest());
    const std::vector<core::Event> events = CastAt(casting, 1, core::Slot::Special2);
    Run(idle, 1, 1);
    CHECK(events.empty());
    CHECK(casting.StateHash() == idle.StateHash());
    CHECK(CasterOf(casting).readyTick.at(std::to_underlying(core::Slot::Special2)) == 0); // no cooldown started
}

TEST_CASE("the dash moves the player toward the point, by its distance, over its duration") {
    constexpr core::HeldInput LEFT{ .move = { .x = -1.0F, .y = 0.0F } };
    constexpr u64 START = 1;
    constexpr u64 LAST = START + DASH_TICKS - 1;
    game::Arena arena(test::TestManifest());
    const core::StableId player = arena.Player();
    const core::Vec2 from = FootOf(arena, player);
    CastAt(arena, START, core::Slot::Movement, from + FAR_RIGHT, LEFT);
    // The cast's own tick takes the first step, eased: less than a linear ninth of the way.
    const f32 first = FootOf(arena, player).x - from.x;
    CHECK(first > 0.0F);
    CHECK(first < DASH_DISTANCE / static_cast<f32>(DASH_TICKS));
    f32 previous = first;
    for (u64 tick = START + 1; tick < LAST; ++tick) {
        std::vector<core::Event> events;
        arena.Tick(At(tick), { .actions = {}, .held = LEFT }, events);
        const f32 moved = FootOf(arena, player).x - from.x;
        CHECK(moved > previous);
        CHECK(moved < DASH_DISTANCE);
        previous = moved;
    }
    CHECK(Entities(arena).all_of<game::Dashing>(EntityOf(arena, player)));
    std::vector<core::Event> events;
    arena.Tick(At(LAST), { .actions = {}, .held = LEFT }, events);
    CHECK(FootOf(arena, player).x == from.x + DASH_DISTANCE);
    CHECK(FootOf(arena, player).y == from.y);
    CHECK_FALSE(Entities(arena).all_of<game::Dashing>(EntityOf(arena, player)));
    for (const core::StableId fox : Foxes(arena)) {
        const core::Vec2 centre = Entities(arena).get<game::Orbit>(EntityOf(arena, fox)).centre;
        CHECK(centre.x == FootOf(arena, player).x);
        CHECK(centre.y == FootOf(arena, player).y);
    }
    arena.Tick(At(LAST + 1), { .actions = {}, .held = LEFT }, events);
    CHECK(FootOf(arena, player).x < from.x + DASH_DISTANCE); // walking again
}

TEST_CASE("the dash stops at a point nearer than its distance") {
    constexpr f32 NEAR_BY = 100.0F;
    game::Arena arena(test::TestManifest());
    const core::Vec2 from = FootOf(arena, arena.Player());
    CastAt(arena, 1, core::Slot::Movement, from + core::Vec2{ .x = 0.0F, .y = NEAR_BY });
    Run(arena, 2, DASH_TICKS);
    CHECK(FootOf(arena, arena.Player()).x == from.x);
    CHECK(FootOf(arena, arena.Player()).y == from.y + NEAR_BY);
}

TEST_CASE("a dash toward the player's own foot goes nowhere, finite, and ends on time") {
    constexpr u64 START = 1;
    game::Arena arena(test::TestManifest());
    const core::Vec2 from = FootOf(arena, arena.Player());
    CastAt(arena, START, core::Slot::Movement, from);
    CHECK(FootOf(arena, arena.Player()).x == from.x);
    CHECK(FootOf(arena, arena.Player()).y == from.y);
    Run(arena, START + 1, START + DASH_TICKS - 1);
    CHECK(FootOf(arena, arena.Player()).x == from.x);
    CHECK(FootOf(arena, arena.Player()).y == from.y);
    CHECK_FALSE(Entities(arena).all_of<game::Dashing>(EntityOf(arena, arena.Player())));
    CHECK(CasterOf(arena).readyTick.at(std::to_underlying(core::Slot::Movement)) == START + DASH_COOLDOWN);
}

TEST_CASE("a cast during a dash is ignored") {
    game::Arena arena(test::TestManifest());
    CastAt(arena, 1, core::Slot::Movement, FAR_RIGHT);
    const std::vector<core::Event> events = CastAt(arena, 2, core::Slot::Generator);
    CHECK(events.empty());
    CHECK(CasterOf(arena).qi == QI_START);
    CHECK(CasterOf(arena).readyTick.at(std::to_underlying(core::Slot::Generator)) == 0);
}

TEST_CASE("a cast by a dying player is ignored") {
    game::Arena arena(test::TestManifest());
    Run(arena, 1, 1);
    arena.Damage(arena.Player(), CULTIVATOR_HEALTH, 1);
    REQUIRE(Entities(arena).all_of<game::Dying>(EntityOf(arena, arena.Player())));
    CastAt(arena, 2, core::Slot::Generator);
    CHECK(CasterOf(arena).qi == QI_START);
    CHECK(CasterOf(arena).readyTick.at(std::to_underlying(core::Slot::Generator)) == 0);
}

TEST_CASE("a cast at a point that isn't finite is ignored") {
    constexpr f32 NOT_A_NUMBER = std::numeric_limits<f32>::quiet_NaN();
    constexpr f32 INFINITE = std::numeric_limits<f32>::infinity();
    game::Arena arena(test::TestManifest());
    CastAt(arena, 1, core::Slot::Generator, { .x = NOT_A_NUMBER, .y = 0.0F });
    CastAt(arena, 2, core::Slot::Movement, { .x = 0.0F, .y = INFINITE });
    CHECK(CasterOf(arena).qi == QI_START);
    CHECK(CasterOf(arena).readyTick == std::array<u64, core::SLOTS>{});
    CHECK_FALSE(Entities(arena).all_of<game::Dashing>(EntityOf(arena, arena.Player())));
}

TEST_CASE("a cast of a slot out of range is ignored") {
    game::Arena casting(test::TestManifest());
    game::Arena idle(test::TestManifest());
    const std::vector<core::Event> events = CastAt(casting, 1, static_cast<core::Slot>(core::SLOTS));
    Run(idle, 1, 1);
    CHECK(events.empty());
    CHECK(casting.StateHash() == idle.StateHash());
}

TEST_CASE("new game data with a lower most Qi lowers the Qi held to it") {
    constexpr i64 LOWER = 20;
    game::Arena arena(test::TestManifest());
    core::GameData data = game::BuiltInData();
    data.abilities.qiMax = LOWER;
    arena.SetData(data);
    CHECK(CasterOf(arena).qi == LOWER);
}

TEST_CASE("a casting player's state is published in a combat scene only") {
    game::Arena arena(test::TestManifest());
    CastAt(arena, 1, core::Slot::Generator);
    const core::Snapshot snapshot = Published(arena, 1);
    REQUIRE(snapshot.caster.has_value());
    const core::CasterView view = snapshot.caster.value_or(core::CasterView{});
    CHECK(view.qi == QI_START + BOLT_GAIN);
    CHECK(view.qiMax == QI_MAX);
    CHECK(view.health == CULTIVATOR_HEALTH);
    CHECK(view.healthMax == CULTIVATOR_HEALTH);
    CHECK(view.readyTick.at(std::to_underlying(core::Slot::Generator)) == 1 + BOLT_COOLDOWN);
    CHECK(view.loadout.at(std::to_underlying(core::Slot::Spender)) == core::AbilityKind::PoisonLance);
    game::Arena painted(test::TestManifest(), game::Scene::Painted);
    CHECK_FALSE(Published(painted, 1).caster.has_value());
}

TEST_CASE("a cast's use ID carries the slot") {
    constexpr u64 TICK = 9;
    CHECK(game::CastUse(TICK, core::Slot::Generator) == core::AbilityUse(TICK, game::CAST_SOURCE));
    CHECK(game::CastUse(TICK, core::Slot::Movement) == core::AbilityUse(TICK, game::CAST_SOURCE + 2));
}

TEST_CASE("a caster's Qi and a dash are valid when not negative, finite and at least a tick long") {
    CHECK(game::Caster{ .qi = 0 }.Valid());
    CHECK_FALSE(game::Caster{ .qi = -1 }.Valid());
    constexpr f32 NOT_A_NUMBER = std::numeric_limits<f32>::quiet_NaN();
    CHECK(game::Dashing{ .from = {}, .to = {}, .startTick = 0, .ticks = 1 }.Valid());
    CHECK_FALSE(game::Dashing{ .from = {}, .to = {}, .startTick = 0, .ticks = 0 }.Valid());
    CHECK_FALSE(game::Dashing{ .from = {}, .to = { .x = NOT_A_NUMBER, .y = 0.0F }, .startTick = 0, .ticks = 1 }.Valid());
}

// The volleys: the flying swords and the fire bolt.

constexpr i64 AMPLE = 1'000'000; // more health than any volley takes
constexpr i64 SWORD_DAMAGE = 20;
constexpr i64 BOLT_DAMAGE = 140;
constexpr i64 BURN_DAMAGE = 40;
constexpr u32 BURN_TICKS = 3;
constexpr u32 SWORDS = 1'000;
constexpr u64 CAST = 2; // the first cast's tick: the foxes have stood where they circle since tick 1

constexpr std::size_t FLYING_SWORDS = std::to_underlying(core::AbilityKind::FlyingSwords);
constexpr std::size_t FIRE_BOLT = std::to_underlying(core::AbilityKind::FireBolt);

// Game data whose foxes, each with `health`, circle 150 units round the player, out of the crystal aura's reach, and
// whose player starts with the Qi for the swords.
core::GameData Gathered(i64 health) {
    constexpr f32 NEAR_ORBIT = 150.0F;
    core::GameData data = game::BuiltInData();
    data.abilities.qiStart = data.abilities.qiMax;
    data.fox.orbitRadius = NEAR_ORBIT;
    data.fox.orbitWobble = 0.0F;
    data.fox.health = health;
    return data;
}

// Off the line through the player and a fox, whose neighbours on either side are as far from it, so the foxes nearest a
// point moved by it are at distinct distances.
constexpr core::Vec2 OFF_AXIS{ .x = 12.0F, .y = 5.0F };

// Where the volleys aim at a creature: its foot raised by half its sprite's foot height.
core::Vec2 ChestOf(const game::Arena& arena, core::StableId stableId) {
    const core::Entity entity = EntityOf(arena, stableId);
    const game::Look& look = Entities(arena).get<game::Look>(entity);
    const f32 height = static_cast<f32>(test::TestManifest()->Sprite(look.sprite).footY) * look.scale;
    const core::Vec2 foot = Entities(arena).get<game::Body>(entity).current;
    constexpr f32 HALF = 0.5F;
    return { .x = foot.x, .y = foot.y - (height * HALF) };
}

// The foxes in ID order nearest `point`, `count` of them, and a radius round `point` that tags exactly those.
struct Tagging {
    std::vector<core::StableId> foxes;
    f32 radius = 0.0F;
};

Tagging Nearest(const game::Arena& arena, core::Vec2 point, std::size_t count) {
    std::vector<std::pair<f32, core::StableId>> away;
    for (const core::StableId fox : Foxes(arena)) {
        away.emplace_back(Distance(point, ChestOf(arena, fox)), fox);
    }
    std::ranges::sort(away);
    REQUIRE(away.size() > count);
    constexpr f32 GAP = 2.0F; // the radius stays half this from every fox, so rounding never tags another
    REQUIRE(away.at(count).first - away.at(count - 1).first > GAP);
    Tagging tagging{ .foxes = {}, .radius = std::midpoint(away.at(count - 1).first, away.at(count).first) };
    for (std::size_t index = 0; index < count; ++index) {
        tagging.foxes.push_back(away.at(index).second);
    }
    std::ranges::sort(tagging.foxes);
    return tagging;
}

// `data` with the flying swords' radius set to `radius` and their hits dealing `damage`.
core::GameData WithSwords(core::GameData data, f32 radius, i64 damage = SWORD_DAMAGE) {
    data.abilities.table.at(FLYING_SWORDS).radius = radius;
    data.abilities.table.at(FLYING_SWORDS).damage = damage;
    return data;
}

// Ticks `arena` from `first` to `last`, holding nothing; its events, each stamped with its tick.
std::vector<core::Event> Play(game::Arena& arena, u64 first, u64 last) {
    std::vector<core::Event> events;
    for (u64 tick = first; tick <= last; ++tick) {
        const std::size_t before = events.size();
        arena.Tick(At(tick), {}, events);
        for (core::Event& event : std::span{ events }.subspan(before)) {
            event.tick = tick;
        }
    }
    return events;
}

// The events of `use` among `events`.
std::vector<core::Event> OfUse(const std::vector<core::Event>& events, u64 use) {
    std::vector<core::Event> found;
    std::ranges::copy_if(events, std::back_inserter(found), [use](const core::Event& event) { return event.use == use; });
    return found;
}

std::size_t CountKind(const std::vector<core::Event>& events, core::EventKind kind) {
    return static_cast<std::size_t>(std::ranges::count(events, kind, &core::Event::kind));
}

// Casts `slot` at `point` on `tick`, stamping the cast tick's events with it.
std::vector<core::Event> StampedCast(game::Arena& arena, u64 tick, core::Slot slot, core::Vec2 point) {
    std::vector<core::Event> events = CastAt(arena, tick, slot, point);
    for (core::Event& event : events) {
        event.tick = tick;
    }
    return events;
}

// The only volley `arena` publishes at `tick`.
core::VolleyView OnlyVolley(game::Arena& arena, u64 tick) {
    const core::Snapshot snapshot = Published(arena, tick);
    REQUIRE(snapshot.volleys.size() == 1);
    return snapshot.volleys.empty() ? core::VolleyView{} : snapshot.volleys.front();
}

// Ticks `arena`, whose foxes have `health`, to before CAST and gives its swords a radius that tags the `count` foxes
// nearest the first fox's chest, dealing `damage`; those foxes and that chest.
struct Aimed {
    std::vector<core::StableId> tagged;
    core::Vec2 point{};
};

Aimed ReadySwords(game::Arena& arena, i64 health, std::size_t count, i64 damage = SWORD_DAMAGE) {
    Run(arena, 1, CAST - 1);
    const core::Vec2 point = ChestOf(arena, Foxes(arena).front()) + OFF_AXIS;
    const Tagging tagging = Nearest(arena, point, count);
    arena.SetData(WithSwords(Gathered(health), tagging.radius, damage));
    return { .tagged = tagging.foxes, .point = point };
}

TEST_CASE("every sword's hit is scheduled once and the use ends with the last") {
    constexpr std::size_t TAGGED = 3;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, Gathered(AMPLE));
    const Aimed ready = ReadySwords(arena, AMPLE, TAGGED);
    std::vector<core::Event> events = StampedCast(arena, CAST, core::Slot::Special1, ready.point);
    const core::VolleyView view = OnlyVolley(arena, CAST);
    const u64 last = core::LastArrival(view.volley);
    std::ranges::move(Play(arena, CAST + 1, last + 1), std::back_inserter(events));
    const std::vector<core::Event> used = OfUse(events, game::CastUse(CAST, core::Slot::Special1));
    CHECK(CountKind(used, core::EventKind::Hit) == SWORDS);
    REQUIRE(CountKind(used, core::EventKind::UseEnded) == 1);
    CHECK(used.back().kind == core::EventKind::UseEnded);
    CHECK(used.back().tick == last);
    for (const core::Event& hit : std::span{ used }.first(used.size() - 1)) {
        CHECK(hit.kind == core::EventKind::Hit);
        CHECK(hit.hit == core::HitKind::Damage);
        CHECK(hit.value == SWORD_DAMAGE);
        CHECK(std::ranges::contains(ready.tagged, hit.target));
    }
}

TEST_CASE("hits split evenly over the tagged enemies") {
    constexpr i64 HEALTH = 10'000;
    constexpr std::size_t TAGGED = 4;
    constexpr std::size_t EACH = SWORDS / TAGGED;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, Gathered(HEALTH));
    const Aimed ready = ReadySwords(arena, HEALTH, TAGGED);
    StampedCast(arena, CAST, core::Slot::Special1, ready.point);
    const u64 last = core::LastArrival(OnlyVolley(arena, CAST).volley);
    const std::vector<core::Event> used = OfUse(Play(arena, CAST + 1, last), game::CastUse(CAST, core::Slot::Special1));
    for (const core::StableId fox : ready.tagged) {
        CAPTURE(std::to_underlying(fox));
        CHECK(std::ranges::count(used, fox, &core::Event::target) == EACH);
        CHECK(HealthOf(arena, fox).current == HEALTH - (static_cast<i64>(EACH) * SWORD_DAMAGE));
    }
}

TEST_CASE("a dead enemy's swords move to the living") {
    constexpr i64 FRAIL = 3; // three hits of 1
    game::Arena arena(test::TestManifest(), game::Scene::Arena, Gathered(AMPLE));
    const Aimed ready = ReadySwords(arena, AMPLE, 2, 1);
    const core::StableId frail = ready.tagged.front();
    const core::StableId other = ready.tagged.back();
    arena.Damage(frail, AMPLE - FRAIL, CAST - 1);
    StampedCast(arena, CAST, core::Slot::Special1, ready.point);
    const u64 last = core::LastArrival(OnlyVolley(arena, CAST).volley);
    const std::vector<core::Event> used = OfUse(Play(arena, CAST + 1, last), game::CastUse(CAST, core::Slot::Special1));
    const game::Dying* const dying = Entities(arena).try_get<game::Dying>(EntityOf(arena, frail));
    REQUIRE(dying != nullptr);
    const u64 death = dying == nullptr ? 0 : dying->startTick;
    for (const core::Event& event : used) {
        if (event.kind == core::EventKind::Hit && event.tick > death) {
            CHECK(event.target == other);
        }
    }
    // The other's own half, and swords moved from the frail one.
    CHECK(std::ranges::count(used, other, &core::Event::target) > SWORDS / 2);
}

TEST_CASE("with all tagged enemies dead the rest plunge and emit nothing") {
    constexpr i64 FRAIL = 3;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, Gathered(FRAIL));
    const Aimed ready = ReadySwords(arena, FRAIL, 2, 1);
    StampedCast(arena, CAST, core::Slot::Special1, ready.point);
    const u64 last = core::LastArrival(OnlyVolley(arena, CAST).volley);
    const std::vector<core::Event> used = OfUse(Play(arena, CAST + 1, last + 1), game::CastUse(CAST, core::Slot::Special1));
    // Each dies at its third hit (they are gone by now).
    u64 lastDeath = 0;
    for (const core::StableId fox : ready.tagged) {
        std::vector<core::Event> hits;
        std::ranges::copy_if(used, std::back_inserter(hits), [fox](const core::Event& event) { return event.target == fox; });
        REQUIRE(hits.size() >= static_cast<std::size_t>(FRAIL));
        lastDeath = std::max(lastDeath, hits.at(static_cast<std::size_t>(FRAIL) - 1).tick);
    }
    REQUIRE(lastDeath < last);
    for (const core::Event& event : used) {
        if (event.kind == core::EventKind::Hit) {
            CHECK(event.tick <= lastDeath);
        }
    }
    CHECK(CountKind(used, core::EventKind::Hit) < SWORDS);
    REQUIRE(CountKind(used, core::EventKind::UseEnded) == 1);
    CHECK(used.back().kind == core::EventKind::UseEnded);
    CHECK(used.back().tick == last);
}

TEST_CASE("each tick's hits are the swords arriving then whose target was alive as the tick began") {
    constexpr i64 FRAIL = 3;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, Gathered(FRAIL));
    const Aimed ready = ReadySwords(arena, FRAIL, 2, 1);
    StampedCast(arena, CAST, core::Slot::Special1, ready.point);
    core::Snapshot before = Published(arena, CAST);
    REQUIRE(before.volleys.size() == 1);
    const core::Volley volley = before.volleys.front().volley;
    const u64 last = core::LastArrival(volley);
    std::vector<std::vector<u32>> arriving(last + 1);
    for (u32 sword = 0; sword < volley.shape.count; ++sword) {
        arriving.at(core::ArrivalTick(volley, sword)).push_back(sword);
    }
    std::array<u64, 2> deaths{};
    std::array<i64, 2> hitsTaken{};
    for (u64 tick = CAST + 1; tick <= last; ++tick) {
        CAPTURE(tick);
        // The targets as the tick begins: as the last tick left them.
        const std::vector<core::VolleyTarget> targets = before.volleyTargets;
        std::array<i64, 2> expected{};
        for (const u32 sword : arriving.at(tick)) {
            const core::VolleyTarget& target = targets.at(core::SwordTarget(volley, sword, tick, targets));
            if (target.diedTick == 0) {
                ++expected.at(target.entity == ready.tagged.front() ? 0 : 1);
            }
        }
        const std::vector<core::Event> events = OfUse(Play(arena, tick, tick), volley.use);
        for (std::size_t index = 0; index < 2; ++index) {
            const i64 hits = std::ranges::count(events, ready.tagged.at(index), &core::Event::target);
            CHECK(hits == expected.at(index));
            if (hitsTaken.at(index) < FRAIL && hitsTaken.at(index) + hits >= FRAIL) {
                deaths.at(index) = tick;
            }
            hitsTaken.at(index) += hits;
        }
        before = Published(arena, tick);
        REQUIRE(before.volleyTargets.size() == 2);
        for (std::size_t index = 0; index < 2; ++index) {
            // Dead from the tick its health ran out, even when swords struck it on that tick.
            const core::VolleyTarget& target = before.volleyTargets.at(index);
            CHECK(target.diedTick == deaths.at(index));
            if (target.diedTick != 0 && target.diedTick < tick) {
                // Where it died, between ticks too, so the swords plunging there hold still.
                CHECK(std::bit_cast<u64>(target.previous) == std::bit_cast<u64>(target.current));
            }
        }
    }
    CHECK(deaths.at(0) > 0);
    CHECK(deaths.at(1) > 0);
}

TEST_CASE("with none tagged, the swords strike the point and emit nothing but the end") {
    constexpr core::Vec2 NOWHERE{ .x = 960.0F, .y = 1'100.0F }; // 260 below the lowest fox's chest
    game::Arena arena(test::TestManifest(), game::Scene::Arena, Gathered(AMPLE));
    Run(arena, 1, CAST - 1);
    std::vector<core::Event> events = StampedCast(arena, CAST, core::Slot::Special1, NOWHERE);
    const core::Snapshot snapshot = Published(arena, CAST);
    REQUIRE(snapshot.volleys.size() == 1);
    REQUIRE(snapshot.volleyTargets.size() == 1);
    CHECK(snapshot.volleyTargets.front().entity == core::StableId{});
    CHECK(snapshot.volleyTargets.front().current.x == NOWHERE.x);
    CHECK(snapshot.volleyTargets.front().current.y == NOWHERE.y);
    const u64 last = core::LastArrival(snapshot.volleys.front().volley);
    std::ranges::move(Play(arena, CAST + 1, last + 1), std::back_inserter(events));
    const std::vector<core::Event> used = OfUse(events, game::CastUse(CAST, core::Slot::Special1));
    REQUIRE(used.size() == 1);
    CHECK(used.front().kind == core::EventKind::UseEnded);
    CHECK(used.front().tick == last);
    for (const core::StableId fox : Foxes(arena)) {
        CHECK(HealthOf(arena, fox).current == AMPLE);
    }
}

// An arena of foxes with ample health, ticked to before CAST; the first fox's chest.
core::Vec2 ReadyBolt(game::Arena& arena) {
    Run(arena, 1, CAST - 1);
    return ChestOf(arena, Foxes(arena).front());
}

TEST_CASE("a burn ticks its count and stops, each tick a Burn hit of the bolt's use") {
    game::Arena arena(test::TestManifest(), game::Scene::Arena, Gathered(AMPLE));
    const core::Vec2 point = ReadyBolt(arena);
    const core::StableId fox = Foxes(arena).front();
    std::vector<core::Event> events = StampedCast(arena, CAST, core::Slot::Generator, point);
    const u64 arrival = core::ArrivalTick(OnlyVolley(arena, CAST).volley, 0);
    constexpr u64 AFTER = 10;
    const u64 lastTick = arrival + (BURN_TICKS * game::AFFLICTION_PERIOD);
    std::ranges::move(Play(arena, CAST + 1, lastTick + AFTER), std::back_inserter(events));
    const std::vector<core::Event> used = OfUse(events, game::CastUse(CAST, core::Slot::Generator));
    REQUIRE(used.size() == 1 + BURN_TICKS + 1);
    CHECK(used.front().kind == core::EventKind::Hit);
    CHECK(used.front().tick == arrival);
    CHECK(used.front().value == BOLT_DAMAGE);
    for (u32 index = 0; index <= BURN_TICKS; ++index) {
        const core::Event& hit = used.at(index);
        CAPTURE(index);
        CHECK(hit.kind == core::EventKind::Hit);
        CHECK(hit.hit == core::HitKind::Burn);
        CHECK(hit.target == fox);
        CHECK(hit.tick == arrival + (index * game::AFFLICTION_PERIOD));
        if (index > 0) {
            CHECK(hit.value == BURN_DAMAGE);
        }
    }
    CHECK(used.back().kind == core::EventKind::UseEnded);
    CHECK(used.back().tick == lastTick);
    CHECK(HealthOf(arena, fox).current == AMPLE - BOLT_DAMAGE - (BURN_TICKS * BURN_DAMAGE));
}

TEST_CASE("a second bolt refreshes the burn; its later ticks belong to the latest use, and the first use ends") {
    constexpr u64 SECOND = CAST + BOLT_COOLDOWN;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, Gathered(AMPLE));
    const core::Vec2 point = ReadyBolt(arena);
    const core::StableId fox = Foxes(arena).front();
    std::vector<core::Event> events = StampedCast(arena, CAST, core::Slot::Generator, point);
    std::ranges::move(Play(arena, CAST + 1, SECOND - 1), std::back_inserter(events));
    std::ranges::move(StampedCast(arena, SECOND, core::Slot::Generator, ChestOf(arena, fox)), std::back_inserter(events));
    const u64 arrival = core::ArrivalTick(OnlyVolley(arena, SECOND).volley, 0);
    REQUIRE(arrival < CAST + game::AFFLICTION_PERIOD); // the first burn hasn't ticked yet
    const u64 lastTick = arrival + (BURN_TICKS * game::AFFLICTION_PERIOD);
    std::ranges::move(Play(arena, SECOND + 1, lastTick + 1), std::back_inserter(events));

    const std::vector<core::Event> first = OfUse(events, game::CastUse(CAST, core::Slot::Generator));
    REQUIRE(first.size() == 2);
    CHECK(first.front().kind == core::EventKind::Hit);
    CHECK(first.back().kind == core::EventKind::UseEnded);
    CHECK(first.back().tick == arrival);

    const std::vector<core::Event> second = OfUse(events, game::CastUse(SECOND, core::Slot::Generator));
    REQUIRE(second.size() == 1 + BURN_TICKS + 1);
    for (u32 index = 1; index <= BURN_TICKS; ++index) {
        CHECK(second.at(index).hit == core::HitKind::Burn);
        CHECK(second.at(index).tick == arrival + (index * game::AFFLICTION_PERIOD));
    }
    CHECK(second.back().kind == core::EventKind::UseEnded);
    CHECK(second.back().tick == lastTick);
}

TEST_CASE("a burn ends with its creature's death, and its use with it") {
    game::Arena arena(test::TestManifest(), game::Scene::Arena, Gathered(AMPLE));
    const core::Vec2 point = ReadyBolt(arena);
    const core::StableId fox = Foxes(arena).front();
    std::vector<core::Event> events = StampedCast(arena, CAST, core::Slot::Generator, point);
    const u64 arrival = core::ArrivalTick(OnlyVolley(arena, CAST).volley, 0);
    const u64 death = arrival + game::AFFLICTION_PERIOD + 1; // after the first tick of the burn
    std::ranges::move(Play(arena, CAST + 1, death - 1), std::back_inserter(events));
    arena.Damage(fox, AMPLE, death);
    std::ranges::move(Play(arena, death, death + (BURN_TICKS * game::AFFLICTION_PERIOD)), std::back_inserter(events));
    const std::vector<core::Event> used = OfUse(events, game::CastUse(CAST, core::Slot::Generator));
    REQUIRE(used.size() == 3); // the hit, one tick, the end
    CHECK(used.back().kind == core::EventKind::UseEnded);
    CHECK(used.back().tick == death);
}

TEST_CASE("the bolt strikes the tagged enemy nearest the point") {
    constexpr f32 NEARER_THE_SECOND = 0.7F;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, Gathered(AMPLE));
    Run(arena, 1, CAST - 1);
    const core::StableId first = Foxes(arena).front();
    const core::Vec2 firstChest = ChestOf(arena, first);
    const Tagging pair = Nearest(arena, firstChest + OFF_AXIS, 2);
    REQUIRE(std::ranges::contains(pair.foxes, first));
    const core::StableId second = pair.foxes.front() == first ? pair.foxes.back() : pair.foxes.front();
    const core::Vec2 secondChest = ChestOf(arena, second);
    const core::Vec2 point = firstChest + ((secondChest - firstChest) * NEARER_THE_SECOND);
    const f32 bolt = game::BuiltInData().abilities.table.at(FIRE_BOLT).radius;
    REQUIRE(Distance(point, firstChest) < bolt); // both are tagged
    std::vector<core::Event> events = StampedCast(arena, CAST, core::Slot::Generator, point);
    const core::Snapshot snapshot = Published(arena, CAST);
    REQUIRE(snapshot.volleyTargets.size() == 1);
    CHECK(snapshot.volleyTargets.front().entity == second);
    const u64 arrival = core::ArrivalTick(OnlyVolley(arena, CAST).volley, 0);
    std::ranges::move(Play(arena, CAST + 1, arrival), std::back_inserter(events));
    const std::vector<core::Event> used = OfUse(events, game::CastUse(CAST, core::Slot::Generator));
    REQUIRE_FALSE(used.empty());
    CHECK(used.front().kind == core::EventKind::Hit);
    CHECK(used.front().target == second);
    CHECK(HealthOf(arena, first).current == AMPLE);
}

TEST_CASE("the snapshot carries each volley and its targets") {
    constexpr std::size_t TAGGED = 3;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, Gathered(AMPLE));
    const Aimed ready = ReadySwords(arena, AMPLE, TAGGED);
    StampedCast(arena, CAST, core::Slot::Special1, ready.point);
    const core::Snapshot snapshot = Published(arena, CAST);
    REQUIRE(snapshot.volleys.size() == 1);
    const core::VolleyView& view = snapshot.volleys.front();
    CHECK(view.ability == core::AbilityKind::FlyingSwords);
    CHECK(view.element == core::HitKind::Damage);
    CHECK(view.firstTarget == 0);
    CHECK(view.targets == TAGGED);
    CHECK(view.volley.use == game::CastUse(CAST, core::Slot::Special1));
    CHECK(view.volley.startTick == CAST);
    CHECK(view.volley.shape.count == SWORDS);
    CHECK(view.volley.shape.commitTicks >= core::MIN_COMMIT_TICKS);
    CHECK(view.volley.point.x == ready.point.x);
    CHECK(view.volley.point.y == ready.point.y);
    REQUIRE(snapshot.volleyTargets.size() == TAGGED);
    for (std::size_t index = 0; index < TAGGED; ++index) {
        const core::VolleyTarget& target = snapshot.volleyTargets.at(index);
        CAPTURE(index);
        CHECK(target.entity == ready.tagged.at(index));
        CHECK(target.diedTick == 0);
        const core::Vec2 chest = ChestOf(arena, target.entity);
        CHECK(target.current.x == chest.x);
        CHECK(target.current.y == chest.y);
    }
}

TEST_CASE("a scene change mid-volley ends every open use") {
    constexpr u64 SWORDS_CAST = 100;
    constexpr u64 CHANGE = 150; // the swords in flight, the burn between its ticks
    game::Arena arena(test::TestManifest(), game::Scene::Arena, Gathered(AMPLE));
    const core::Vec2 point = ReadyBolt(arena);
    std::vector<core::Event> events = StampedCast(arena, CAST, core::Slot::Generator, point);
    std::ranges::move(Play(arena, CAST + 1, SWORDS_CAST - 1), std::back_inserter(events));
    const core::StableId fox = Foxes(arena).front();
    std::ranges::move(StampedCast(arena, SWORDS_CAST, core::Slot::Special1, ChestOf(arena, fox)), std::back_inserter(events));
    const core::Snapshot cast = Published(arena, SWORDS_CAST);
    REQUIRE_FALSE(cast.volleys.empty());
    REQUIRE(core::LastArrival(cast.volleys.back().volley) > CHANGE);
    std::ranges::move(Play(arena, SWORDS_CAST + 1, CHANGE - 1), std::back_inserter(events));
    const std::array<core::Action, 1> change{ { { .kind = core::ActionKind::CycleScene } } };
    std::vector<core::Event> ended;
    arena.Tick(At(CHANGE), { .actions = change, .held = {} }, ended);
    const u64 bolt = game::CastUse(CAST, core::Slot::Generator);
    const u64 swords = game::CastUse(SWORDS_CAST, core::Slot::Special1);
    REQUIRE(ended.size() == 2);
    CHECK(CountKind(ended, core::EventKind::UseEnded) == 2);
    CHECK(std::ranges::contains(ended, bolt, &core::Event::use));
    CHECK(std::ranges::contains(ended, swords, &core::Event::use));
    CHECK(CountKind(OfUse(events, bolt), core::EventKind::UseEnded) == 0);
    CHECK(CountKind(OfUse(events, swords), core::EventKind::UseEnded) == 0);
    const core::Snapshot snapshot = Published(arena, CHANGE);
    CHECK(snapshot.volleys.empty());
    CHECK(snapshot.volleyTargets.empty());
    const std::vector<core::Event> later = Play(arena, CHANGE + 1, CHANGE + (BURN_TICKS * game::AFFLICTION_PERIOD));
    CHECK(OfUse(later, bolt).empty());
    CHECK(OfUse(later, swords).empty());
}

// Whether two runs' events are the same, field by field.
bool SameEvents(const std::vector<core::Event>& lhs, const std::vector<core::Event>& rhs) {
    return std::ranges::equal(lhs, rhs, [](const core::Event& one, const core::Event& two) {
        return one.tick == two.tick && one.kind == two.kind && one.hit == two.hit && one.use == two.use &&
               one.target == two.target && one.value == two.value &&
               std::bit_cast<u64>(one.position) == std::bit_cast<u64>(two.position);
    });
}

TEST_CASE("new game data leaves a volley in flight as it was cast") {
    constexpr u64 RELOAD = CAST + 10;
    constexpr i64 NEW_DAMAGE = 999;
    constexpr u32 NEW_COUNT = 10;
    constexpr u32 NEW_FLIGHT = 5;
    constexpr f32 NEW_RADIUS = 1'000.0F;
    game::Arena keptArena(test::TestManifest(), game::Scene::Arena, Gathered(AMPLE));
    game::Arena reloadedArena(test::TestManifest(), game::Scene::Arena, Gathered(AMPLE));
    const Aimed kept = ReadySwords(keptArena, AMPLE, 2);
    const Aimed reloaded = ReadySwords(reloadedArena, AMPLE, 2);
    StampedCast(keptArena, CAST, core::Slot::Special1, kept.point);
    StampedCast(reloadedArena, CAST, core::Slot::Special1, reloaded.point);
    const core::VolleyView cast = OnlyVolley(keptArena, CAST);
    const u64 last = core::LastArrival(cast.volley);
    Run(keptArena, CAST + 1, RELOAD);
    Run(reloadedArena, CAST + 1, RELOAD);
    core::GameData data = Gathered(AMPLE);
    core::AbilityData& swords = data.abilities.table.at(FLYING_SWORDS);
    swords.damage = NEW_DAMAGE;
    swords.radius = NEW_RADIUS;
    swords.volley.count = NEW_COUNT;
    swords.volley.flightTicks = NEW_FLIGHT;
    data.abilities.table.at(FIRE_BOLT).damage = NEW_DAMAGE;
    reloadedArena.SetData(data);

    const core::VolleyView after = OnlyVolley(reloadedArena, RELOAD);
    CHECK(after.volley.shape.count == cast.volley.shape.count);
    CHECK(after.volley.shape.flightTicks == cast.volley.shape.flightTicks);
    CHECK(after.volley.shape.commitTicks == cast.volley.shape.commitTicks);
    const std::vector<core::Event> keptEvents = Play(keptArena, RELOAD + 1, last + 1);
    const std::vector<core::Event> reloadedEvents = Play(reloadedArena, RELOAD + 1, last + 1);
    CHECK(CountKind(reloadedEvents, core::EventKind::Hit) == SWORDS);
    const bool same = SameEvents(keptEvents, reloadedEvents);
    CHECK(same);

    // The next cast takes the new data.
    const core::StableId fox = reloaded.tagged.front();
    StampedCast(reloadedArena, last + 2, core::Slot::Generator, ChestOf(reloadedArena, fox));
    const core::Snapshot boltCast = Published(reloadedArena, last + 2);
    REQUIRE_FALSE(boltCast.volleys.empty());
    const u64 arrival = core::ArrivalTick(boltCast.volleys.back().volley, 0); // the swords still fading before it
    const std::vector<core::Event> hits =
        OfUse(Play(reloadedArena, last + 3, arrival), game::CastUse(last + 2, core::Slot::Generator));
    REQUIRE_FALSE(hits.empty());
    CHECK(hits.front().value == NEW_DAMAGE);
}

TEST_CASE("afflictions are valid with no negative damage") {
    constexpr i64 HARM = 40;
    CHECK(game::Afflictions{ .burn = { .damage = HARM, .ticksLeft = 1, .use = 1, .nextTick = 1 }, .poison = {} }.Valid());
    CHECK_FALSE(game::Afflictions{ .burn = {}, .poison = { .damage = -1, .ticksLeft = 1, .use = 1, .nextTick = 1 } }.Valid());
}

} // namespace
