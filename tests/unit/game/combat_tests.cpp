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

} // namespace
