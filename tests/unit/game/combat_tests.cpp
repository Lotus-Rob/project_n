#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/instances.hpp"
#include "core/state.hpp"
#include "game/combat.hpp"
#include "game/components.hpp"
#include "game/host.hpp"
#include "game/npcs.hpp"
#include "support/host.hpp"
#include "support/manifest.hpp"
#include "support/same.hpp"
#include "support/seals.hpp"

#include <doctest/doctest.h>

namespace {

// The shipped combat rules (assets/data/abilities.yaml's `combat:` section, which a data test checks).
constexpr core::CombatRules RULES{};

using core::atlas::SpriteId;
using test::At;
using test::FootOf;
using test::FOX_HEALTH;
using test::Foxes;
using test::NearTheCrystal;
using test::PublishedAt;
using test::SameEvents;

constexpr std::size_t FOXES = 12;
constexpr i64 CULTIVATOR_HEALTH = 1'000;
constexpr u64 AURA_TICK = 60;
constexpr core::Rect GROUND{ .min = {}, .max = { .x = 1'920.0F, .y = 1'080.0F } };
constexpr f32 ON_THE_EDGE = 1e-3F;
constexpr u32 DISSOLVED = 0xFFFFU;
constexpr u32 HALF_BITS = 16;

// Ticks `host` from `first` to `last`, holding nothing, ignoring its events.
void Run(game::Host& host, u64 first, u64 last) {
    std::vector<core::Event> events;
    for (u64 tick = first; tick <= last; ++tick) {
        events.clear();
        host.Tick(At(tick), {}, events);
    }
}

const core::Registry& Entities(const game::Host& host) {
    return host.State()[game::ACTIVE].Entities();
}

core::Entity EntityOf(const game::Host& host, core::StableId stableId) {
    return host.State()[game::ACTIVE].Find(stableId);
}

game::Health HealthOf(const game::Host& host, core::StableId stableId) {
    return Entities(host).get<game::Health>(EntityOf(host, stableId));
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
    const game::Host host(test::TestManifest());
    CHECK(HealthOf(host, host.Player()).current == CULTIVATOR_HEALTH);
    CHECK(HealthOf(host, host.Player()).max == CULTIVATOR_HEALTH);
    const std::vector<core::StableId> foxes = Foxes(host);
    REQUIRE(foxes.size() == FOXES);
    for (const core::StableId fox : foxes) {
        CHECK(HealthOf(host, fox).current == FOX_HEALTH);
        CHECK(HealthOf(host, fox).max == FOX_HEALTH);
    }
}

// The arena near the crystal, the player walked to it on tick 1 and damaged by `wound` at tick 2, ticked to the aura's
// first hit; the hits of that tick.
std::vector<core::Event> AuraAfterWound(game::Host& host, i64 wound) {
    constexpr core::HeldInput LEFT{ .move = { .x = -1.0F, .y = 0.0F } };
    std::vector<core::Event> events;
    host.Tick(At(1), { .actions = {}, .held = LEFT }, events);
    host.Damage(host.Player(), wound, 2);
    Run(host, 2, AURA_TICK - 1);
    events.clear();
    host.Tick(At(AURA_TICK), {}, events);
    return events;
}

TEST_CASE("the aura's damage lowers a fox's health and heals the player up to its most") {
    constexpr i64 DEEP = 500;
    constexpr i64 SCRATCH = 10; // less than the least healing
    game::Host host(test::TestManifest(), game::ARENA_SCENE, NearTheCrystal());
    const std::vector<core::Event> events = AuraAfterWound(host, DEEP);
    std::size_t damaged = 0;
    for (const core::Event& event : events) {
        if (event.kind != core::EventKind::Hit || event.target == host.Player()) {
            continue;
        }
        ++damaged;
        CHECK(event.hit == core::HitKind::Damage);
        CHECK(HealthOf(host, event.target).current == FOX_HEALTH - event.value);
    }
    CHECK(damaged > 0);
    const auto heal = std::ranges::find(events, host.Player(), &core::Event::target);
    REQUIRE(heal != events.end());
    CHECK(heal->hit == core::HitKind::Healing);
    CHECK(HealthOf(host, host.Player()).current == CULTIVATOR_HEALTH - DEEP + heal->value);

    game::Host scratched(test::TestManifest(), game::ARENA_SCENE, NearTheCrystal());
    const std::vector<core::Event> again = AuraAfterWound(scratched, SCRATCH);
    REQUIRE(std::ranges::contains(again, scratched.Player(), &core::Event::target));
    CHECK(HealthOf(scratched, scratched.Player()).current == CULTIVATOR_HEALTH);
}

TEST_CASE("health at zero gives a dying state, then removal, then a respawn on the edge") {
    constexpr u64 DEATH = 5;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, FarFromTheCrystal());
    Run(host, 1, DEATH);
    const std::vector<core::StableId> before = Foxes(host);
    const core::StableId fox = before.front();
    host.Damage(fox, FOX_HEALTH, DEATH);
    CHECK(HealthOf(host, fox).current == 0);
    const game::Dying* const dying = Entities(host).try_get<game::Dying>(EntityOf(host, fox));
    REQUIRE(dying != nullptr);
    CHECK(dying->startTick == DEATH);

    Run(host, DEATH + 1, DEATH + RULES.dyingTicks - 1);
    CHECK(host.State().DomainOf(fox).has_value());
    Run(host, DEATH + RULES.dyingTicks, DEATH + RULES.dyingTicks);
    CHECK_FALSE(host.State().DomainOf(fox).has_value());
    CHECK(Foxes(host).size() == FOXES - 1);

    const u64 respawn = DEATH + RULES.dyingTicks + RULES.respawnTicks;
    Run(host, DEATH + RULES.dyingTicks + 1, respawn - 1);
    CHECK(Foxes(host).size() == FOXES - 1);
    Run(host, respawn, respawn);
    const std::vector<core::StableId> after = Foxes(host);
    REQUIRE(after.size() == FOXES);
    std::vector<core::StableId> added;
    std::ranges::set_difference(after, before, std::back_inserter(added));
    REQUIRE(added.size() == 1);
    const core::Vec2 foot = FootOf(host, added.front());
    INFO("the new fox stands at ", foot.x, ", ", foot.y);
    const bool onEdge = std::abs(foot.x - GROUND.min.x) < ON_THE_EDGE || std::abs(foot.x - GROUND.max.x) < ON_THE_EDGE ||
                        std::abs(foot.y - GROUND.min.y) < ON_THE_EDGE || std::abs(foot.y - GROUND.max.y) < ON_THE_EDGE;
    CHECK(onEdge);
    CHECK(foot.x >= GROUND.min.x - ON_THE_EDGE);
    CHECK(foot.x <= GROUND.max.x + ON_THE_EDGE);
    CHECK(foot.y >= GROUND.min.y - ON_THE_EDGE);
    CHECK(foot.y <= GROUND.max.y + ON_THE_EDGE);
    CHECK(HealthOf(host, added.front()).current == FOX_HEALTH);
}

// Kills the first fox at tick 1 and ticks to its respawn; the new fox.
core::StableId Respawned(game::Host& host) {
    Run(host, 1, 1);
    const std::vector<core::StableId> before = Foxes(host);
    host.Damage(before.front(), FOX_HEALTH, 1);
    Run(host, 2, 1 + RULES.dyingTicks + RULES.respawnTicks);
    std::vector<core::StableId> added;
    std::ranges::set_difference(Foxes(host), before, std::back_inserter(added));
    REQUIRE(added.size() == 1);
    return added.front();
}

TEST_CASE("a respawned fox eases into its orbit") {
    constexpr u64 AFTERWARDS = 60;
    // An eased arrival's fastest step is 1.5 times its mean: twice leaves room without allowing a snap.
    constexpr f32 TWICE = 2.0F;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, FarFromTheCrystal());
    const core::StableId fox = Respawned(host);
    const u64 arrived = 1 + RULES.dyingTicks + RULES.respawnTicks + RULES.arriveTicks;
    const core::StableId other = Foxes(host).front(); // an orbiting fox: the respawned one has the newest ID
    REQUIRE(other != fox);
    const core::Vec2 from = FootOf(host, fox);
    const game::Orbit orbit = Entities(host).get<game::Orbit>(EntityOf(host, fox));
    REQUIRE(Entities(host).all_of<game::Arriving>(EntityOf(host, fox)));

    std::vector<core::Vec2> path{ from };
    std::vector<core::Vec2> otherPath{ FootOf(host, other) };
    f32 farthest = 0.0F;
    for (u64 tick = arrived - RULES.arriveTicks + 1; tick < arrived; ++tick) {
        Run(host, tick, tick);
        path.push_back(FootOf(host, fox));
        otherPath.push_back(FootOf(host, other));
        farthest = std::max(farthest, Distance(from, game::OrbitPoint(orbit, tick)));
        CHECK(Entities(host).all_of<game::Arriving>(EntityOf(host, fox)));
    }
    CHECK(Distance(path.back(), game::OrbitPoint(orbit, arrived - 1)) > 0.0F); // still on its way
    f32 orbitStep = 0.0F;
    for (std::size_t index = 1; index < otherPath.size(); ++index) {
        orbitStep = std::max(orbitStep, Distance(otherPath.at(index - 1), otherPath.at(index)));
    }
    const f32 approach = TWICE * farthest / static_cast<f32>(RULES.arriveTicks);
    for (std::size_t index = 1; index < path.size(); ++index) {
        INFO("tick ", index, " of its arrival");
        CHECK(Distance(path.at(index - 1), path.at(index)) <= (TWICE * orbitStep) + approach);
    }
    for (u64 tick = arrived; tick < arrived + AFTERWARDS; ++tick) {
        Run(host, tick, tick);
        const core::Vec2 foot = FootOf(host, fox);
        const core::Vec2 circled = game::OrbitPoint(orbit, tick);
        CHECK(foot.x == circled.x);
        CHECK(foot.y == circled.y);
    }
    CHECK_FALSE(Entities(host).all_of<game::Arriving>(EntityOf(host, fox)));
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
    game::Host host(test::TestManifest(), game::ARENA_SCENE, FarFromTheCrystal());
    Run(host, 1, DEATH);
    const core::StableId fox = Foxes(host).front();
    host.Damage(fox, FOX_HEALTH, DEATH);
    u32 last = 0;
    for (u64 tick = DEATH; tick < DEATH + RULES.dyingTicks; ++tick) {
        if (tick > DEATH) {
            Run(host, tick, tick);
        }
        INFO("tick ", tick);
        const core::Snapshot snapshot = PublishedAt(host, tick);
        const std::optional<std::size_t> drawn = FoxInstanceAt(snapshot, FootOf(host, fox));
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
    game::Host host(test::TestManifest(), game::ARENA_SCENE, FarFromTheCrystal());
    const core::StableId fox = Foxes(host).front();
    const std::array<core::Action, 1> select{ { { .kind = core::ActionKind::Select, .target = fox } } };
    std::vector<core::Event> events;
    host.Tick(At(DEATH), { .actions = select, .held = {} }, events);
    REQUIRE(host.Selected() == fox);
    host.Damage(fox, FOX_HEALTH, DEATH);
    Run(host, DEATH + 1, DEATH + RULES.dyingTicks - 1);
    CHECK(host.Selected() == fox);
    CHECK_FALSE(PublishedAt(host, DEATH + RULES.dyingTicks - 1).selected.has_value()); // a dying fox isn't outlined
    Run(host, DEATH + RULES.dyingTicks, DEATH + RULES.dyingTicks);
    CHECK(host.Selected() == core::StableId{});
}

TEST_CASE("a dying fox can't be selected") {
    constexpr u64 DEATH = 1;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, FarFromTheCrystal());
    const core::StableId fox = Foxes(host).front();
    host.Damage(fox, FOX_HEALTH, 0);
    const std::array<core::Action, 1> select{ { { .kind = core::ActionKind::Select, .target = fox } } };
    std::vector<core::Event> events;
    host.Tick(At(DEATH), { .actions = select, .held = {} }, events);
    CHECK(host.Selected() == core::StableId{});
}

TEST_CASE("the player removed on the tick a fox respawns leaves no player, and the fox circles the start") {
    constexpr u64 FOX_DEATH = 1;
    constexpr u64 PLAYER_DEATH = FOX_DEATH + RULES.respawnTicks; // both removed on the respawn's tick
    constexpr core::Vec2 START{ .x = 960.0F, .y = 620.0F };      // where the cultivator starts
    game::Host host(test::TestManifest(), game::ARENA_SCENE, FarFromTheCrystal());
    const std::vector<core::StableId> before = Foxes(host);
    const core::StableId player = host.Player();
    Run(host, 1, FOX_DEATH);
    host.Damage(before.front(), FOX_HEALTH, FOX_DEATH);
    Run(host, FOX_DEATH + 1, PLAYER_DEATH);
    host.Damage(player, CULTIVATOR_HEALTH, PLAYER_DEATH);
    const u64 respawn = FOX_DEATH + RULES.dyingTicks + RULES.respawnTicks;
    REQUIRE(respawn == PLAYER_DEATH + RULES.dyingTicks);
    Run(host, PLAYER_DEATH + 1, respawn);
    CHECK(host.Player() == core::StableId{});
    CHECK_FALSE(host.State().DomainOf(player).has_value());
    std::vector<core::StableId> added;
    std::ranges::set_difference(Foxes(host), before, std::back_inserter(added));
    REQUIRE(added.size() == 1);
    const game::Orbit orbit = Entities(host).get<game::Orbit>(EntityOf(host, added.front()));
    CHECK(orbit.centre.x == START.x);
    CHECK(orbit.centre.y == START.y);
}

TEST_CASE("each living enemy in a combat scene reports its health over its head") {
    constexpr i64 WOUND = 300;
    constexpr f32 THREE_QUARTERS = 0.75F;
    constexpr u64 TICK = 2;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, FarFromTheCrystal());
    Run(host, 1, TICK);
    const std::vector<core::StableId> foxes = Foxes(host);
    host.Damage(foxes.at(0), WOUND, TICK);
    host.Damage(foxes.at(1), FOX_HEALTH, TICK);
    const core::Snapshot snapshot = PublishedAt(host, TICK);
    CHECK(snapshot.healthBars.size() == FOXES - 1);
    CHECK_FALSE(std::ranges::contains(snapshot.healthBars, host.Player(), &core::HealthBar::entity));
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

    game::Host stress(test::TestManifest(), game::STRESS_SCENE);
    CHECK(PublishedAt(stress, 0).healthBars.empty());
}

TEST_CASE("new game data sets a creature's most health, keeping what it has up to it") {
    constexpr i64 WOUND = 300;
    constexpr i64 LOWER_MOST = 500;
    constexpr i64 HIGHER_MOST = 2'000;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, FarFromTheCrystal());
    const std::vector<core::StableId> foxes = Foxes(host);
    host.Damage(foxes.at(0), WOUND, 0);
    core::GameData data = FarFromTheCrystal();
    data.fox.health = HIGHER_MOST;
    host.SetData(data);
    CHECK(HealthOf(host, foxes.at(0)).current == FOX_HEALTH - WOUND);
    CHECK(HealthOf(host, foxes.at(0)).max == HIGHER_MOST);
    data.fox.health = LOWER_MOST;
    host.SetData(data);
    CHECK(HealthOf(host, foxes.at(0)).current == LOWER_MOST);
    CHECK(HealthOf(host, foxes.at(1)).current == LOWER_MOST);
    CHECK(HealthOf(host, foxes.at(1)).max == LOWER_MOST);
}

TEST_CASE("damage to a dying or removed creature does nothing") {
    constexpr u64 DEATH = 1;
    constexpr i64 MORE = 100;
    constexpr i64 HEALING = -500;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, FarFromTheCrystal());
    Run(host, 1, DEATH);
    const core::StableId fox = Foxes(host).front();
    host.Damage(fox, FOX_HEALTH, DEATH);
    host.Damage(fox, MORE, DEATH + 1);
    host.Damage(fox, HEALING, DEATH + 1);
    CHECK(HealthOf(host, fox).current == 0);
    CHECK(Entities(host).get<game::Dying>(EntityOf(host, fox)).startTick == DEATH);
    Run(host, DEATH + 1, DEATH + RULES.dyingTicks);
    REQUIRE_FALSE(host.State().DomainOf(fox).has_value());
    const u64 before = host.StateHash();
    host.Damage(fox, MORE, DEATH + RULES.dyingTicks);
    CHECK(host.StateHash() == before);
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

// Ticks `host` once at `tick`, casting the ability in `slot` at `point` and holding `held`; the tick's events.
std::vector<core::Event> CastAt(game::Host& host, u64 tick, core::Slot slot, core::Vec2 point = {}, core::HeldInput held = {}) {
    const std::array<core::Action, 1> cast{ { { .kind = core::ActionKind::Cast, .slot = slot, .point = point } } };
    std::vector<core::Event> events;
    host.Tick(At(tick), { .actions = cast, .held = held }, events);
    return events;
}

const game::Caster& CasterOf(const game::Host& host) {
    return Entities(host).get<game::Caster>(EntityOf(host, host.Player()));
}

TEST_CASE("the player starts with the data's starting Qi") {
    const game::Host host(test::TestManifest());
    CHECK(CasterOf(host).qi == QI_START);
    CHECK(CasterOf(host).qi == game::BuiltInData().abilities.qiStart);
}

TEST_CASE("a cast in the combat scene has an effect: Qi changes and the slot's cooldown starts") {
    constexpr u64 TICK = 7;
    game::Host casting(test::TestManifest());
    game::Host idle(test::TestManifest());
    CastAt(casting, TICK, core::Slot::Generator);
    Run(idle, TICK, TICK);
    CHECK(CasterOf(casting).qi == QI_START + BOLT_GAIN);
    CHECK(CasterOf(casting).readyTick.at(std::to_underlying(core::Slot::Generator)) == TICK + BOLT_COOLDOWN);
    CHECK(casting.StateHash() != idle.StateHash());
}

TEST_CASE("the generator gains Qi up to the most; the spender and a special cost it") {
    game::Host host(test::TestManifest());
    u64 tick = 1;
    for (i64 expected = QI_START + BOLT_GAIN; expected < QI_MAX; expected += BOLT_GAIN) {
        CastAt(host, tick, core::Slot::Generator);
        CHECK(CasterOf(host).qi == expected);
        tick += BOLT_COOLDOWN;
    }
    CastAt(host, tick, core::Slot::Generator);
    CHECK(CasterOf(host).qi == QI_MAX);
    tick += BOLT_COOLDOWN;
    CastAt(host, tick, core::Slot::Generator);
    CHECK(CasterOf(host).qi == QI_MAX);

    CastAt(host, tick, core::Slot::Spender);
    CHECK(CasterOf(host).qi == QI_MAX - LANCE_COST);
    CastAt(host, tick, core::Slot::Special1);
    CHECK(CasterOf(host).qi == QI_MAX - LANCE_COST - SWORDS_COST);
}

TEST_CASE("a cooldown blocks a recast until it's over") {
    constexpr u64 FIRST = 10;
    game::Host host(test::TestManifest());
    CastAt(host, FIRST, core::Slot::Generator);
    CastAt(host, FIRST + BOLT_COOLDOWN - 1, core::Slot::Generator);
    CHECK(CasterOf(host).qi == QI_START + BOLT_GAIN);
    Run(host, FIRST + BOLT_COOLDOWN, FIRST + (2 * BOLT_COOLDOWN));
    CHECK(CasterOf(host).qi == QI_START + BOLT_GAIN); // the refused cast wasn't queued
    CastAt(host, FIRST + (2 * BOLT_COOLDOWN), core::Slot::Generator);
    CHECK(CasterOf(host).qi == QI_START + (2 * BOLT_GAIN));
}

TEST_CASE("a cast at the tick the cooldown ends goes off") {
    constexpr u64 FIRST = 10;
    game::Host host(test::TestManifest());
    CastAt(host, FIRST, core::Slot::Generator);
    CastAt(host, FIRST + BOLT_COOLDOWN, core::Slot::Generator);
    CHECK(CasterOf(host).qi == QI_START + (2 * BOLT_GAIN));
}

TEST_CASE("a cast without enough Qi does nothing but report it") {
    constexpr i64 SHORT = 10;
    core::GameData data = game::BuiltInData();
    data.abilities.qiStart = SHORT;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, data);
    const std::vector<core::Event> events = CastAt(host, 1, core::Slot::Spender);
    REQUIRE(events.size() == 1);
    CHECK(events.front().kind == core::EventKind::NotEnoughQi);
    CHECK(events.front().value == static_cast<i64>(std::to_underlying(core::Slot::Spender)));
    CHECK(CasterOf(host).qi == SHORT);
    CHECK(CasterOf(host).readyTick.at(std::to_underlying(core::Slot::Spender)) == 0);
}

TEST_CASE("an empty slot casts nothing") {
    game::Host casting(test::TestManifest());
    game::Host idle(test::TestManifest());
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
    game::Host host(test::TestManifest());
    const core::StableId player = host.Player();
    const core::Vec2 from = FootOf(host, player);
    CastAt(host, START, core::Slot::Movement, from + FAR_RIGHT, LEFT);
    // The cast's own tick takes the first step, eased: less than a linear ninth of the way.
    const f32 first = FootOf(host, player).x - from.x;
    CHECK(first > 0.0F);
    CHECK(first < DASH_DISTANCE / static_cast<f32>(DASH_TICKS));
    f32 previous = first;
    for (u64 tick = START + 1; tick < LAST; ++tick) {
        std::vector<core::Event> events;
        host.Tick(At(tick), { .actions = {}, .held = LEFT }, events);
        const f32 moved = FootOf(host, player).x - from.x;
        CHECK(moved > previous);
        CHECK(moved < DASH_DISTANCE);
        previous = moved;
    }
    CHECK(Entities(host).all_of<game::Dashing>(EntityOf(host, player)));
    std::vector<core::Event> events;
    host.Tick(At(LAST), { .actions = {}, .held = LEFT }, events);
    CHECK(FootOf(host, player).x == from.x + DASH_DISTANCE);
    CHECK(FootOf(host, player).y == from.y);
    CHECK_FALSE(Entities(host).all_of<game::Dashing>(EntityOf(host, player)));
    for (const core::StableId fox : Foxes(host)) {
        const core::Vec2 centre = Entities(host).get<game::Orbit>(EntityOf(host, fox)).centre;
        CHECK(centre.x == FootOf(host, player).x);
        CHECK(centre.y == FootOf(host, player).y);
    }
    host.Tick(At(LAST + 1), { .actions = {}, .held = LEFT }, events);
    CHECK(FootOf(host, player).x < from.x + DASH_DISTANCE); // walking again
}

TEST_CASE("the dash stops at a point nearer than its distance") {
    constexpr f32 NEAR_BY = 100.0F;
    game::Host host(test::TestManifest());
    const core::Vec2 from = FootOf(host, host.Player());
    CastAt(host, 1, core::Slot::Movement, from + core::Vec2{ .x = 0.0F, .y = NEAR_BY });
    Run(host, 2, DASH_TICKS);
    CHECK(FootOf(host, host.Player()).x == from.x);
    CHECK(FootOf(host, host.Player()).y == from.y + NEAR_BY);
}

TEST_CASE("a dash toward the player's own foot goes nowhere, finite, and ends on time") {
    constexpr u64 START = 1;
    game::Host host(test::TestManifest());
    const core::Vec2 from = FootOf(host, host.Player());
    CastAt(host, START, core::Slot::Movement, from);
    CHECK(FootOf(host, host.Player()).x == from.x);
    CHECK(FootOf(host, host.Player()).y == from.y);
    Run(host, START + 1, START + DASH_TICKS - 1);
    CHECK(FootOf(host, host.Player()).x == from.x);
    CHECK(FootOf(host, host.Player()).y == from.y);
    CHECK_FALSE(Entities(host).all_of<game::Dashing>(EntityOf(host, host.Player())));
    CHECK(CasterOf(host).readyTick.at(std::to_underlying(core::Slot::Movement)) == START + DASH_COOLDOWN);
}

TEST_CASE("a cast during a dash is ignored") {
    game::Host host(test::TestManifest());
    CastAt(host, 1, core::Slot::Movement, FAR_RIGHT);
    const std::vector<core::Event> events = CastAt(host, 2, core::Slot::Generator);
    CHECK(events.empty());
    CHECK(CasterOf(host).qi == QI_START);
    CHECK(CasterOf(host).readyTick.at(std::to_underlying(core::Slot::Generator)) == 0);
}

TEST_CASE("a cast by a dying player is ignored") {
    game::Host host(test::TestManifest());
    Run(host, 1, 1);
    host.Damage(host.Player(), CULTIVATOR_HEALTH, 1);
    REQUIRE(Entities(host).all_of<game::Dying>(EntityOf(host, host.Player())));
    CastAt(host, 2, core::Slot::Generator);
    CHECK(CasterOf(host).qi == QI_START);
    CHECK(CasterOf(host).readyTick.at(std::to_underlying(core::Slot::Generator)) == 0);
}

TEST_CASE("a cast at a point that isn't finite is ignored") {
    constexpr f32 NOT_A_NUMBER = std::numeric_limits<f32>::quiet_NaN();
    constexpr f32 INFINITE = std::numeric_limits<f32>::infinity();
    game::Host host(test::TestManifest());
    CastAt(host, 1, core::Slot::Generator, { .x = NOT_A_NUMBER, .y = 0.0F });
    CastAt(host, 2, core::Slot::Movement, { .x = 0.0F, .y = INFINITE });
    CHECK(CasterOf(host).qi == QI_START);
    CHECK(CasterOf(host).readyTick == std::array<u64, core::SLOTS>{});
    CHECK_FALSE(Entities(host).all_of<game::Dashing>(EntityOf(host, host.Player())));
}

TEST_CASE("a cast of a slot out of range is ignored") {
    game::Host casting(test::TestManifest());
    game::Host idle(test::TestManifest());
    const std::vector<core::Event> events = CastAt(casting, 1, static_cast<core::Slot>(core::SLOTS));
    Run(idle, 1, 1);
    CHECK(events.empty());
    CHECK(casting.StateHash() == idle.StateHash());
}

TEST_CASE("new game data with a lower most Qi lowers the Qi held to it") {
    constexpr i64 LOWER = 20;
    game::Host host(test::TestManifest());
    core::GameData data = game::BuiltInData();
    data.abilities.qiMax = LOWER;
    host.SetData(data);
    CHECK(CasterOf(host).qi == LOWER);
}

TEST_CASE("a casting player's state is published in a combat scene only") {
    game::Host host(test::TestManifest());
    CastAt(host, 1, core::Slot::Generator);
    const core::Snapshot snapshot = PublishedAt(host, 1);
    REQUIRE(snapshot.caster.has_value());
    const core::CasterView view = snapshot.caster.value_or(core::CasterView{});
    CHECK(view.qi == QI_START + BOLT_GAIN);
    CHECK(view.qiMax == QI_MAX);
    CHECK(view.health == CULTIVATOR_HEALTH);
    CHECK(view.healthMax == CULTIVATOR_HEALTH);
    CHECK(view.readyTick.at(std::to_underlying(core::Slot::Generator)) == 1 + BOLT_COOLDOWN);
    CHECK(view.loadout.at(std::to_underlying(core::Slot::Spender)) == core::AbilityKind::PoisonLance);
    game::Host painted(test::TestManifest(), game::PAINTED_SCENE);
    CHECK_FALSE(PublishedAt(painted, 1).caster.has_value());
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
core::Vec2 ChestOf(const game::Host& host, core::StableId stableId) {
    const core::Entity entity = EntityOf(host, stableId);
    const game::Look& look = Entities(host).get<game::Look>(entity);
    const f32 height = static_cast<f32>(test::TestManifest()->Sprite(look.sprite).footY) * look.scale;
    const core::Vec2 foot = Entities(host).get<game::Body>(entity).current;
    constexpr f32 HALF = 0.5F;
    return { .x = foot.x, .y = foot.y - (height * HALF) };
}

// The foxes in ID order nearest `point`, `count` of them, and a radius round `point` that tags exactly those.
struct Tagging {
    std::vector<core::StableId> foxes;
    f32 radius = 0.0F;
};

Tagging Nearest(const game::Host& host, core::Vec2 point, std::size_t count) {
    std::vector<std::pair<f32, core::StableId>> away;
    for (const core::StableId fox : Foxes(host)) {
        away.emplace_back(Distance(point, ChestOf(host, fox)), fox);
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

// Ticks `host` from `first` to `last`, holding nothing; its events, each stamped with its tick.
std::vector<core::Event> Play(game::Host& host, u64 first, u64 last) {
    std::vector<core::Event> events;
    for (u64 tick = first; tick <= last; ++tick) {
        const std::size_t before = events.size();
        host.Tick(At(tick), {}, events);
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
std::vector<core::Event> StampedCast(game::Host& host, u64 tick, core::Slot slot, core::Vec2 point) {
    std::vector<core::Event> events = CastAt(host, tick, slot, point);
    for (core::Event& event : events) {
        event.tick = tick;
    }
    return events;
}

// The only volley `host` publishes at `tick`.
core::VolleyView OnlyVolley(game::Host& host, u64 tick) {
    const core::Snapshot snapshot = PublishedAt(host, tick);
    REQUIRE(snapshot.volleys.size() == 1);
    return snapshot.volleys.empty() ? core::VolleyView{} : snapshot.volleys.front();
}

// Ticks `host`, whose foxes have `health`, to before CAST and gives its swords a radius that tags the `count` foxes
// nearest the first fox's chest, dealing `damage`; those foxes and that chest.
struct Aimed {
    std::vector<core::StableId> tagged;
    core::Vec2 point{};
};

Aimed ReadySwords(game::Host& host, i64 health, std::size_t count, i64 damage = SWORD_DAMAGE) {
    Run(host, 1, CAST - 1);
    const core::Vec2 point = ChestOf(host, Foxes(host).front()) + OFF_AXIS;
    const Tagging tagging = Nearest(host, point, count);
    host.SetData(WithSwords(Gathered(health), tagging.radius, damage));
    return { .tagged = tagging.foxes, .point = point };
}

TEST_CASE("every sword's hit is scheduled once and the use ends with the last") {
    constexpr std::size_t TAGGED = 3;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    const Aimed ready = ReadySwords(host, AMPLE, TAGGED);
    std::vector<core::Event> events = StampedCast(host, CAST, core::Slot::Special1, ready.point);
    const core::VolleyView view = OnlyVolley(host, CAST);
    const u64 last = core::LastArrival(view.volley);
    std::ranges::move(Play(host, CAST + 1, last + 1), std::back_inserter(events));
    const std::vector<core::Event> used = OfUse(events, game::CastUse(CAST, core::Slot::Special1));
    CHECK(CountKind(used, core::EventKind::Hit) == SWORDS);
    REQUIRE(CountKind(used, core::EventKind::UseEnded) == 1);
    CHECK(used.back().kind == core::EventKind::UseEnded);
    CHECK(used.back().tick == last);
    // Every event before the end is a hit of the sword's damage on a tagged enemy: the first, the last and the middle
    // one are named (the first and last by the checks above and below), and the rest by one flag each.
    bool allHits = true;
    bool allDamage = true;
    bool allSwordDamage = true;
    bool allTagged = true;
    for (const core::Event& hit : std::span{ used }.first(used.size() - 1)) {
        allHits = allHits && hit.kind == core::EventKind::Hit;
        allDamage = allDamage && hit.hit == core::HitKind::Damage;
        allSwordDamage = allSwordDamage && hit.value == SWORD_DAMAGE;
        allTagged = allTagged && std::ranges::contains(ready.tagged, hit.target);
    }
    CHECK(allHits);
    CHECK(allDamage);
    CHECK(allSwordDamage);
    CHECK(allTagged);
}

TEST_CASE("hits split evenly over the tagged enemies") {
    constexpr i64 HEALTH = 10'000;
    constexpr std::size_t TAGGED = 4;
    constexpr std::size_t EACH = SWORDS / TAGGED;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(HEALTH));
    const Aimed ready = ReadySwords(host, HEALTH, TAGGED);
    StampedCast(host, CAST, core::Slot::Special1, ready.point);
    const u64 last = core::LastArrival(OnlyVolley(host, CAST).volley);
    const std::vector<core::Event> used = OfUse(Play(host, CAST + 1, last), game::CastUse(CAST, core::Slot::Special1));
    for (const core::StableId fox : ready.tagged) {
        CAPTURE(std::to_underlying(fox));
        CHECK(std::ranges::count(used, fox, &core::Event::target) == EACH);
        CHECK(HealthOf(host, fox).current == HEALTH - (static_cast<i64>(EACH) * SWORD_DAMAGE));
    }
}

TEST_CASE("a dead enemy's swords move to the living") {
    constexpr i64 FRAIL = 3; // three hits of 1
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    const Aimed ready = ReadySwords(host, AMPLE, 2, 1);
    const core::StableId frail = ready.tagged.front();
    const core::StableId other = ready.tagged.back();
    host.Damage(frail, AMPLE - FRAIL, CAST - 1);
    StampedCast(host, CAST, core::Slot::Special1, ready.point);
    const u64 last = core::LastArrival(OnlyVolley(host, CAST).volley);
    const std::vector<core::Event> used = OfUse(Play(host, CAST + 1, last), game::CastUse(CAST, core::Slot::Special1));
    // It dies on the tick of its third hit (swords arriving on that tick still strike it); the volley outlasts its
    // dissolve, so by now it is gone.
    std::vector<core::Event> frailHits;
    std::ranges::copy_if(used, std::back_inserter(frailHits), [frail](const core::Event& event) {
        return event.target == frail;
    });
    REQUIRE(frailHits.size() >= static_cast<std::size_t>(FRAIL));
    const u64 death = frailHits.at(static_cast<std::size_t>(FRAIL) - 1).tick;
    bool onlyOther = true; // after the death, every hit is on the other
    for (const core::Event& event : used) {
        if (event.kind == core::EventKind::Hit && event.tick > death) {
            onlyOther = onlyOther && event.target == other;
        }
    }
    CHECK(onlyOther);
    // The other's own half, and swords moved from the frail one.
    CHECK(std::ranges::count(used, other, &core::Event::target) > SWORDS / 2);
}

TEST_CASE("with all tagged enemies dead the rest plunge and emit nothing") {
    constexpr i64 FRAIL = 3;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(FRAIL));
    const Aimed ready = ReadySwords(host, FRAIL, 2, 1);
    StampedCast(host, CAST, core::Slot::Special1, ready.point);
    const u64 last = core::LastArrival(OnlyVolley(host, CAST).volley);
    const std::vector<core::Event> used = OfUse(Play(host, CAST + 1, last + 1), game::CastUse(CAST, core::Slot::Special1));
    // Each dies at its third hit (they are gone by now).
    u64 lastDeath = 0;
    for (const core::StableId fox : ready.tagged) {
        std::vector<core::Event> hits;
        std::ranges::copy_if(used, std::back_inserter(hits), [fox](const core::Event& event) { return event.target == fox; });
        REQUIRE(hits.size() >= static_cast<std::size_t>(FRAIL));
        lastDeath = std::max(lastDeath, hits.at(static_cast<std::size_t>(FRAIL) - 1).tick);
    }
    REQUIRE(lastDeath < last);
    bool noneAfterLastDeath = true;
    for (const core::Event& event : used) {
        if (event.kind == core::EventKind::Hit) {
            noneAfterLastDeath = noneAfterLastDeath && event.tick <= lastDeath;
        }
    }
    CHECK(noneAfterLastDeath);
    CHECK(CountKind(used, core::EventKind::Hit) < SWORDS);
    REQUIRE(CountKind(used, core::EventKind::UseEnded) == 1);
    CHECK(used.back().kind == core::EventKind::UseEnded);
    CHECK(used.back().tick == last);
}

// 0 when `holds`, else 1: to count what went wrong in a loop that asserts the count once.
u32 Missed(bool holds) {
    return holds ? 0U : 1U;
}

TEST_CASE("each tick's hits are the swords arriving then whose target was alive as the tick began") {
    constexpr i64 FRAIL = 3;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(FRAIL));
    const Aimed ready = ReadySwords(host, FRAIL, 2, 1);
    StampedCast(host, CAST, core::Slot::Special1, ready.point);
    core::Snapshot before = PublishedAt(host, CAST);
    REQUIRE(before.volleys.size() == 1);
    const core::Volley volley = before.volleys.front().volley;
    const u64 last = core::LastArrival(volley);
    std::vector<std::vector<u32>> arriving(last + 1);
    for (u32 sword = 0; sword < volley.shape.count; ++sword) {
        arriving.at(core::ArrivalTick(volley, sword)).push_back(sword);
    }
    std::array<u64, 2> deaths{};
    std::array<i64, 2> hitsTaken{};
    // Every tick from the cast to the last arrival is played (the swords strike over many), and the worst of each
    // measure is asserted once: the ticks whose hits were not the expected ones, whose death tick was wrong, and whose
    // dead target moved.
    u32 wrongHits = 0;
    u32 wrongDeath = 0;
    u32 movedAfterDeath = 0;
    for (u64 tick = CAST + 1; tick <= last; ++tick) {
        // The targets as the tick begins: as the last tick left them.
        const std::vector<core::VolleyTarget> targets = before.volleyTargets;
        std::array<i64, 2> expected{};
        for (const u32 sword : arriving.at(tick)) {
            const core::VolleyTarget& target = targets.at(core::SwordTarget(volley, sword, tick, targets));
            if (target.diedTick == 0) {
                ++expected.at(target.entity == ready.tagged.front() ? 0 : 1);
            }
        }
        const std::vector<core::Event> events = OfUse(Play(host, tick, tick), volley.use);
        for (std::size_t index = 0; index < 2; ++index) {
            const i64 hits = std::ranges::count(events, ready.tagged.at(index), &core::Event::target);
            wrongHits += Missed(hits == expected.at(index));
            if (hitsTaken.at(index) < FRAIL && hitsTaken.at(index) + hits >= FRAIL) {
                deaths.at(index) = tick;
            }
            hitsTaken.at(index) += hits;
        }
        before = PublishedAt(host, tick);
        if (before.volleyTargets.size() != 2) {
            ++wrongDeath;
            continue;
        }
        for (std::size_t index = 0; index < 2; ++index) {
            // Dead from the tick its health ran out, even when swords struck it on that tick.
            const core::VolleyTarget& target = before.volleyTargets.at(index);
            wrongDeath += Missed(target.diedTick == deaths.at(index));
            if (target.diedTick != 0 && target.diedTick < tick) {
                // Where it died, between ticks too, so the swords plunging there hold still.
                movedAfterDeath += Missed(std::bit_cast<u64>(target.previous) == std::bit_cast<u64>(target.current));
            }
        }
    }
    CHECK(wrongHits == 0);
    CHECK(wrongDeath == 0);
    CHECK(movedAfterDeath == 0);
    CHECK(deaths.at(0) > 0);
    CHECK(deaths.at(1) > 0);
}

TEST_CASE("two volleys on the same targets each end once, at their own last arrival") {
    constexpr u64 SECOND = CAST + 5;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    Run(host, 1, CAST - 1);
    const core::Vec2 point = ChestOf(host, Foxes(host).front()) + OFF_AXIS;
    core::GameData data = WithSwords(Gathered(AMPLE), Nearest(host, point, 2).radius);
    data.abilities.table.at(FLYING_SWORDS).cooldownTicks = 0; // free and ready, so the second overlaps the first
    data.abilities.table.at(FLYING_SWORDS).qiCost = 0;
    host.SetData(data);
    std::vector<core::Event> events = StampedCast(host, CAST, core::Slot::Special1, point);
    std::ranges::move(Play(host, CAST + 1, SECOND - 1), std::back_inserter(events));
    std::ranges::move(StampedCast(host, SECOND, core::Slot::Special1, point), std::back_inserter(events));
    const core::Snapshot snapshot = PublishedAt(host, SECOND);
    REQUIRE(snapshot.volleys.size() == 2);
    REQUIRE(snapshot.volleys.front().volley.use != snapshot.volleys.back().volley.use);
    const u64 firstLast = core::LastArrival(snapshot.volleys.front().volley);
    const u64 secondLast = core::LastArrival(snapshot.volleys.back().volley);
    REQUIRE(core::LaunchTick(snapshot.volleys.back().volley, 0) < firstLast); // in flight together
    std::ranges::move(Play(host, SECOND + 1, secondLast + 1), std::back_inserter(events));
    for (const core::VolleyView& view : snapshot.volleys) {
        CAPTURE(view.volley.use);
        const std::vector<core::Event> used = OfUse(events, view.volley.use);
        CHECK(CountKind(used, core::EventKind::Hit) == SWORDS);
        REQUIRE(CountKind(used, core::EventKind::UseEnded) == 1);
        CHECK(used.back().kind == core::EventKind::UseEnded);
        CHECK(used.back().tick == core::LastArrival(view.volley));
    }
    CHECK(firstLast != secondLast);
}

TEST_CASE("with none tagged, the swords strike the point and emit nothing but the end") {
    constexpr core::Vec2 NOWHERE{ .x = 960.0F, .y = 1'100.0F }; // 260 below the lowest fox's chest
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    Run(host, 1, CAST - 1);
    std::vector<core::Event> events = StampedCast(host, CAST, core::Slot::Special1, NOWHERE);
    const core::Snapshot snapshot = PublishedAt(host, CAST);
    REQUIRE(snapshot.volleys.size() == 1);
    REQUIRE(snapshot.volleyTargets.size() == 1);
    CHECK(snapshot.volleyTargets.front().entity == core::StableId{});
    CHECK(snapshot.volleyTargets.front().current.x == NOWHERE.x);
    CHECK(snapshot.volleyTargets.front().current.y == NOWHERE.y);
    const u64 last = core::LastArrival(snapshot.volleys.front().volley);
    std::ranges::move(Play(host, CAST + 1, last + 1), std::back_inserter(events));
    const std::vector<core::Event> used = OfUse(events, game::CastUse(CAST, core::Slot::Special1));
    REQUIRE(used.size() == 1);
    CHECK(used.front().kind == core::EventKind::UseEnded);
    CHECK(used.front().tick == last);
    for (const core::StableId fox : Foxes(host)) {
        CHECK(HealthOf(host, fox).current == AMPLE);
    }
}

// An arena of foxes with ample health, ticked to before CAST; the first fox's chest.
core::Vec2 ReadyBolt(game::Host& host) {
    Run(host, 1, CAST - 1);
    return ChestOf(host, Foxes(host).front());
}

TEST_CASE("a burn ticks its count and stops, each tick a Burn hit of the bolt's use") {
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    const core::Vec2 point = ReadyBolt(host);
    const core::StableId fox = Foxes(host).front();
    std::vector<core::Event> events = StampedCast(host, CAST, core::Slot::Generator, point);
    const u64 arrival = core::ArrivalTick(OnlyVolley(host, CAST).volley, 0);
    constexpr u64 AFTER = 10;
    const u64 lastTick = arrival + (BURN_TICKS * RULES.afflictionPeriod);
    std::ranges::move(Play(host, CAST + 1, lastTick + AFTER), std::back_inserter(events));
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
        CHECK(hit.tick == arrival + (index * RULES.afflictionPeriod));
        if (index > 0) {
            CHECK(hit.value == BURN_DAMAGE);
        }
    }
    CHECK(used.back().kind == core::EventKind::UseEnded);
    CHECK(used.back().tick == lastTick);
    CHECK(HealthOf(host, fox).current == AMPLE - BOLT_DAMAGE - (BURN_TICKS * BURN_DAMAGE));
}

TEST_CASE("a second bolt refreshes the burn; its later ticks belong to the latest use, and the first use ends") {
    constexpr u64 SECOND = CAST + BOLT_COOLDOWN;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    const core::Vec2 point = ReadyBolt(host);
    const core::StableId fox = Foxes(host).front();
    std::vector<core::Event> events = StampedCast(host, CAST, core::Slot::Generator, point);
    std::ranges::move(Play(host, CAST + 1, SECOND - 1), std::back_inserter(events));
    std::ranges::move(StampedCast(host, SECOND, core::Slot::Generator, ChestOf(host, fox)), std::back_inserter(events));
    const u64 arrival = core::ArrivalTick(OnlyVolley(host, SECOND).volley, 0);
    REQUIRE(arrival < CAST + RULES.afflictionPeriod); // the first burn hasn't ticked yet
    const u64 lastTick = arrival + (BURN_TICKS * RULES.afflictionPeriod);
    std::ranges::move(Play(host, SECOND + 1, lastTick + 1), std::back_inserter(events));

    const std::vector<core::Event> first = OfUse(events, game::CastUse(CAST, core::Slot::Generator));
    REQUIRE(first.size() == 2);
    CHECK(first.front().kind == core::EventKind::Hit);
    CHECK(first.back().kind == core::EventKind::UseEnded);
    CHECK(first.back().tick == arrival);

    const std::vector<core::Event> second = OfUse(events, game::CastUse(SECOND, core::Slot::Generator));
    REQUIRE(second.size() == 1 + BURN_TICKS + 1);
    for (u32 index = 1; index <= BURN_TICKS; ++index) {
        CHECK(second.at(index).hit == core::HitKind::Burn);
        CHECK(second.at(index).tick == arrival + (index * RULES.afflictionPeriod));
    }
    CHECK(second.back().kind == core::EventKind::UseEnded);
    CHECK(second.back().tick == lastTick);
}

TEST_CASE("a burn ends with its creature's death, and its use with it") {
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    const core::Vec2 point = ReadyBolt(host);
    const core::StableId fox = Foxes(host).front();
    std::vector<core::Event> events = StampedCast(host, CAST, core::Slot::Generator, point);
    const u64 arrival = core::ArrivalTick(OnlyVolley(host, CAST).volley, 0);
    const u64 death = arrival + RULES.afflictionPeriod + 1; // after the first tick of the burn
    std::ranges::move(Play(host, CAST + 1, death - 1), std::back_inserter(events));
    host.Damage(fox, AMPLE, death);
    std::ranges::move(Play(host, death, death + (BURN_TICKS * RULES.afflictionPeriod)), std::back_inserter(events));
    const std::vector<core::Event> used = OfUse(events, game::CastUse(CAST, core::Slot::Generator));
    REQUIRE(used.size() == 3); // the hit, one tick, the end
    CHECK(used.back().kind == core::EventKind::UseEnded);
    CHECK(used.back().tick == death);
}

TEST_CASE("the bolt strikes the tagged enemy nearest the point") {
    constexpr f32 NEARER_THE_SECOND = 0.7F;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    Run(host, 1, CAST - 1);
    const core::StableId first = Foxes(host).front();
    const core::Vec2 firstChest = ChestOf(host, first);
    const Tagging pair = Nearest(host, firstChest + OFF_AXIS, 2);
    REQUIRE(std::ranges::contains(pair.foxes, first));
    const core::StableId second = pair.foxes.front() == first ? pair.foxes.back() : pair.foxes.front();
    const core::Vec2 secondChest = ChestOf(host, second);
    const core::Vec2 point = firstChest + ((secondChest - firstChest) * NEARER_THE_SECOND);
    const f32 bolt = game::BuiltInData().abilities.table.at(FIRE_BOLT).radius;
    REQUIRE(Distance(point, firstChest) < bolt); // both are tagged
    std::vector<core::Event> events = StampedCast(host, CAST, core::Slot::Generator, point);
    const core::Snapshot snapshot = PublishedAt(host, CAST);
    REQUIRE(snapshot.volleyTargets.size() == 1);
    CHECK(snapshot.volleyTargets.front().entity == second);
    const u64 arrival = core::ArrivalTick(OnlyVolley(host, CAST).volley, 0);
    std::ranges::move(Play(host, CAST + 1, arrival), std::back_inserter(events));
    const std::vector<core::Event> used = OfUse(events, game::CastUse(CAST, core::Slot::Generator));
    REQUIRE_FALSE(used.empty());
    CHECK(used.front().kind == core::EventKind::Hit);
    CHECK(used.front().target == second);
    CHECK(HealthOf(host, first).current == AMPLE);
}

TEST_CASE("the snapshot carries each volley and its targets") {
    constexpr std::size_t TAGGED = 3;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    const Aimed ready = ReadySwords(host, AMPLE, TAGGED);
    StampedCast(host, CAST, core::Slot::Special1, ready.point);
    const core::Snapshot snapshot = PublishedAt(host, CAST);
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
        const core::Vec2 chest = ChestOf(host, target.entity);
        CHECK(target.current.x == chest.x);
        CHECK(target.current.y == chest.y);
    }
}

// The tick the first sword of `volley`'s second-to-last wave arrives: until then, two waves' hits are still due.
u64 SecondToLastWaveArrival(const core::Volley& volley) {
    u64 first = std::numeric_limits<u64>::max();
    for (u32 sword = 0; sword < volley.shape.count; ++sword) {
        if (core::WaveOf(volley, sword) + 2 == volley.shape.waves) {
            first = std::min(first, core::ArrivalTick(volley, sword));
        }
    }
    return first;
}

// The waves of `volley`'s swords that arrive after `tick`, one per sword, first wave first.
std::vector<u32> WavesDueAfter(const core::Volley& volley, u64 tick) {
    std::vector<u32> due;
    for (u32 sword = 0; sword < volley.shape.count; ++sword) {
        if (core::ArrivalTick(volley, sword) > tick) {
            due.push_back(core::WaveOf(volley, sword));
        }
    }
    std::ranges::sort(due);
    return due;
}

TEST_CASE("a scene change mid-volley ends every open use") {
    constexpr u64 SWORDS_CAST = 100;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    Run(host, 1, SWORDS_CAST - 1);
    const core::StableId fox = Foxes(host).front();
    std::vector<core::Event> events = StampedCast(host, SWORDS_CAST, core::Slot::Special1, ChestOf(host, fox));
    const core::Snapshot cast = PublishedAt(host, SWORDS_CAST);
    REQUIRE_FALSE(cast.volleys.empty());
    const core::Volley& swordsCast = cast.volleys.back().volley;
    REQUIRE(swordsCast.shape.waves >= 2);
    const u64 swordsEnd = core::LastArrival(swordsCast);
    // The change late in the swords' flight, just before their second-to-last wave arrives, and the bolt cast a burn's
    // length before their end, so its burn (begun after the bolt's flight) is between its ticks then: the tail after
    // the change, in the slow stress scene, is about two waves long.
    REQUIRE(swordsEnd > SWORDS_CAST + (BURN_TICKS * RULES.afflictionPeriod)); // the bolt cast after the swords
    const u64 changeTick = SecondToLastWaveArrival(swordsCast) - 1;
    const u64 boltCast = swordsEnd - (BURN_TICKS * RULES.afflictionPeriod);
    std::ranges::move(Play(host, SWORDS_CAST + 1, boltCast - 1), std::back_inserter(events));
    std::ranges::move(StampedCast(host, boltCast, core::Slot::Generator, ChestOf(host, fox)), std::back_inserter(events));
    const core::Snapshot boltCastSnapshot = PublishedAt(host, boltCast);
    const auto boltVolley =
        std::ranges::find(boltCastSnapshot.volleys, core::AbilityKind::FireBolt, &core::VolleyView::ability);
    REQUIRE(boltVolley != boltCastSnapshot.volleys.end());
    const u64 arrival = core::ArrivalTick(boltVolley->volley, 0);
    const u64 burnEnd = arrival + (BURN_TICKS * RULES.afflictionPeriod);
    REQUIRE(arrival < changeTick);
    REQUIRE((changeTick - arrival) % RULES.afflictionPeriod != 0); // the burn between its ticks
    std::ranges::move(Play(host, boltCast + 1, changeTick - 1), std::back_inserter(events));
    const core::Snapshot before = PublishedAt(host, changeTick - 1);
    const auto inFlight = std::ranges::find(before.volleys, core::AbilityKind::FlyingSwords, &core::VolleyView::ability);
    REQUIRE(inFlight != before.volleys.end());
    std::vector<u32> due = WavesDueAfter(inFlight->volley, changeTick);
    CHECK(due.size() > swordsCast.shape.count / swordsCast.shape.waves); // more than a wave's hits still due
    const auto repeated = std::ranges::unique(due);
    due.erase(repeated.begin(), repeated.end());
    CHECK(due.size() >= 2); // of two waves or more
    const std::array<core::Action, 1> change{ { { .kind = core::ActionKind::CycleScene } } };
    std::vector<core::Event> ended;
    host.Tick(At(changeTick), { .actions = change, .held = {} }, ended);
    host.EnterScene(); // as the logic does after the tick
    const u64 bolt = game::CastUse(boltCast, core::Slot::Generator);
    const u64 swords = game::CastUse(SWORDS_CAST, core::Slot::Special1);
    REQUIRE(ended.size() == 2);
    CHECK(CountKind(ended, core::EventKind::UseEnded) == 2);
    CHECK(std::ranges::contains(ended, bolt, &core::Event::use));
    CHECK(std::ranges::contains(ended, swords, &core::Event::use));
    CHECK(CountKind(OfUse(events, bolt), core::EventKind::UseEnded) == 0);
    CHECK(CountKind(OfUse(events, swords), core::EventKind::UseEnded) == 0);
    const std::vector<core::Event> boltBefore = OfUse(events, bolt);
    CHECK(boltBefore.size() == BURN_TICKS); // its hit and every tick of its burn but the last
    const core::Snapshot snapshot = PublishedAt(host, changeTick);
    CHECK(snapshot.volleys.empty());
    CHECK(snapshot.volleyTargets.empty());
    // To the last tick either use had an event due on, had the scene stayed.
    const std::vector<core::Event> later = Play(host, changeTick + 1, std::max(burnEnd, swordsEnd));
    CHECK(OfUse(later, bolt).empty());
    CHECK(OfUse(later, swords).empty());
}

TEST_CASE("new game data leaves a volley in flight as it was cast") {
    constexpr u64 RELOAD = CAST + 10;
    constexpr i64 NEW_DAMAGE = 999;
    constexpr u32 NEW_COUNT = 10;
    constexpr u32 NEW_FLIGHT = 5;
    constexpr f32 NEW_RADIUS = 1'000.0F;
    game::Host keptHost(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    game::Host reloadedHost(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    const Aimed kept = ReadySwords(keptHost, AMPLE, 2);
    const Aimed reloaded = ReadySwords(reloadedHost, AMPLE, 2);
    StampedCast(keptHost, CAST, core::Slot::Special1, kept.point);
    StampedCast(reloadedHost, CAST, core::Slot::Special1, reloaded.point);
    const core::VolleyView cast = OnlyVolley(keptHost, CAST);
    const u64 last = core::LastArrival(cast.volley);
    Run(keptHost, CAST + 1, RELOAD);
    Run(reloadedHost, CAST + 1, RELOAD);
    core::GameData data = Gathered(AMPLE);
    core::AbilityData& swords = data.abilities.table.at(FLYING_SWORDS);
    swords.damage = NEW_DAMAGE;
    swords.radius = NEW_RADIUS;
    swords.volley.count = NEW_COUNT;
    swords.volley.flightTicks = NEW_FLIGHT;
    data.abilities.table.at(FIRE_BOLT).damage = NEW_DAMAGE;
    reloadedHost.SetData(data);

    const core::VolleyView after = OnlyVolley(reloadedHost, RELOAD);
    CHECK(after.volley.shape.count == cast.volley.shape.count);
    CHECK(after.volley.shape.flightTicks == cast.volley.shape.flightTicks);
    CHECK(after.volley.shape.commitTicks == cast.volley.shape.commitTicks);
    const std::vector<core::Event> keptEvents = Play(keptHost, RELOAD + 1, last + 1);
    const std::vector<core::Event> reloadedEvents = Play(reloadedHost, RELOAD + 1, last + 1);
    CHECK(CountKind(reloadedEvents, core::EventKind::Hit) == SWORDS);
    const bool same = SameEvents(keptEvents, reloadedEvents);
    CHECK(same);

    // The next cast takes the new data.
    const core::StableId fox = reloaded.tagged.front();
    StampedCast(reloadedHost, last + 2, core::Slot::Generator, ChestOf(reloadedHost, fox));
    const core::Snapshot boltCast = PublishedAt(reloadedHost, last + 2);
    REQUIRE_FALSE(boltCast.volleys.empty());
    const u64 arrival = core::ArrivalTick(boltCast.volleys.back().volley, 0); // the swords still fading before it
    const std::vector<core::Event> hits =
        OfUse(Play(reloadedHost, last + 3, arrival), game::CastUse(last + 2, core::Slot::Generator));
    REQUIRE_FALSE(hits.empty());
    CHECK(hits.front().value == NEW_DAMAGE);
}

// The poison lance: a strike at the point.

constexpr i64 LANCE_DAMAGE = 360;
constexpr i64 POISON_DAMAGE = 60;
constexpr u32 POISON_TICKS = 4;
constexpr u64 LANCE_COOLDOWN = 30;
constexpr std::size_t POISON_LANCE = std::to_underlying(core::AbilityKind::PoisonLance);

// Ticks `host`, whose foxes have AMPLE health, to before CAST and gives its lance a radius that tags the `count` foxes
// nearest the first fox's chest; those foxes and that chest.
Aimed ReadyLance(game::Host& host, std::size_t count) {
    Run(host, 1, CAST - 1);
    const core::Vec2 point = ChestOf(host, Foxes(host).front()) + OFF_AXIS;
    const Tagging tagging = Nearest(host, point, count);
    core::GameData data = Gathered(AMPLE);
    data.abilities.table.at(POISON_LANCE).radius = tagging.radius;
    host.SetData(data);
    return { .tagged = tagging.foxes, .point = point };
}

TEST_CASE("the lance strikes every enemy in its area with poison and ends after its last tick") {
    constexpr std::size_t TAGGED = 3;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    const Aimed ready = ReadyLance(host, TAGGED);
    const u64 use = game::CastUse(CAST, core::Slot::Spender);
    std::vector<core::Event> events = StampedCast(host, CAST, core::Slot::Spender, ready.point);

    const std::vector<core::Event> struck = OfUse(events, use);
    REQUIRE(struck.size() == TAGGED);
    for (std::size_t index = 0; index < TAGGED; ++index) {
        CAPTURE(index);
        CHECK(struck.at(index).kind == core::EventKind::Hit);
        CHECK(struck.at(index).hit == core::HitKind::Poison);
        CHECK(struck.at(index).target == ready.tagged.at(index));
        CHECK(struck.at(index).value == LANCE_DAMAGE);
    }

    const u64 lastTick = CAST + (POISON_TICKS * RULES.afflictionPeriod);
    std::ranges::move(Play(host, CAST + 1, lastTick + 1), std::back_inserter(events));
    const std::vector<core::Event> used = OfUse(events, use);
    REQUIRE(used.size() == TAGGED + (TAGGED * POISON_TICKS) + 1);
    for (u32 tick = 1; tick <= POISON_TICKS; ++tick) {
        CAPTURE(tick);
        std::vector<core::StableId> targets;
        for (std::size_t index = 0; index < TAGGED; ++index) {
            const core::Event& hit = used.at((tick * TAGGED) + index);
            CHECK(hit.kind == core::EventKind::Hit);
            CHECK(hit.hit == core::HitKind::Poison);
            CHECK(hit.tick == CAST + (tick * RULES.afflictionPeriod));
            CHECK(hit.value == POISON_DAMAGE);
            targets.push_back(hit.target);
        }
        std::ranges::sort(targets);
        CHECK(targets == ready.tagged);
    }
    CHECK(used.back().kind == core::EventKind::UseEnded);
    CHECK(used.back().tick == lastTick);
    for (const core::StableId fox : ready.tagged) {
        CHECK(HealthOf(host, fox).current == AMPLE - LANCE_DAMAGE - (POISON_TICKS * POISON_DAMAGE));
    }
    for (const core::StableId fox : Foxes(host)) {
        if (!std::ranges::contains(ready.tagged, fox)) {
            CHECK(HealthOf(host, fox).current == AMPLE);
        }
    }
}

TEST_CASE("a lance with no enemy in its area ends its use at once") {
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    Run(host, 1, CAST - 1);
    const std::vector<core::Event> events = StampedCast(host, CAST, core::Slot::Spender, FAR_RIGHT);
    const std::vector<core::Event> used = OfUse(events, game::CastUse(CAST, core::Slot::Spender));
    REQUIRE(used.size() == 1);
    CHECK(used.front().kind == core::EventKind::UseEnded);
}

TEST_CASE("a second lance refreshes the poison; its later ticks belong to the latest use, and the first use ends") {
    constexpr u64 SECOND = CAST + LANCE_COOLDOWN;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    const Aimed ready = ReadyLance(host, 1);
    std::vector<core::Event> events = StampedCast(host, CAST, core::Slot::Spender, ready.point);
    std::ranges::move(Play(host, CAST + 1, SECOND - 1), std::back_inserter(events));
    const core::Vec2 again = ChestOf(host, ready.tagged.front());
    std::ranges::move(StampedCast(host, SECOND, core::Slot::Spender, again), std::back_inserter(events));
    const u64 lastTick = SECOND + (POISON_TICKS * RULES.afflictionPeriod);
    std::ranges::move(Play(host, SECOND + 1, lastTick + 1), std::back_inserter(events));

    const std::vector<core::Event> first = OfUse(events, game::CastUse(CAST, core::Slot::Spender));
    REQUIRE(first.size() == 2);
    CHECK(first.front().kind == core::EventKind::Hit);
    CHECK(first.back().kind == core::EventKind::UseEnded);
    CHECK(first.back().tick == SECOND);

    const std::vector<core::Event> second = OfUse(events, game::CastUse(SECOND, core::Slot::Spender));
    REQUIRE(second.size() == 1 + POISON_TICKS + 1);
    CHECK(second.back().kind == core::EventKind::UseEnded);
    CHECK(second.back().tick == lastTick);
}

TEST_CASE("a lance on the tick an earlier poison is due deals that tick first, then refreshes it") {
    constexpr u64 DUE = CAST + RULES.afflictionPeriod;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    const Aimed ready = ReadyLance(host, 1);
    const core::StableId fox = ready.tagged.front();
    StampedCast(host, CAST, core::Slot::Spender, ready.point);
    Run(host, CAST + 1, DUE - 1);
    const std::vector<core::Event> due = StampedCast(host, DUE, core::Slot::Spender, ChestOf(host, fox));

    constexpr u64 FIRST = game::CastUse(CAST, core::Slot::Spender);
    constexpr u64 SECOND = game::CastUse(DUE, core::Slot::Spender);
    const auto ticked = std::ranges::find_if(due, [](const core::Event& event) {
        return event.kind == core::EventKind::Hit && event.use == FIRST;
    });
    const auto struck = std::ranges::find_if(due, [](const core::Event& event) {
        return event.kind == core::EventKind::Hit && event.use == SECOND;
    });
    REQUIRE(ticked != due.end());
    REQUIRE(struck != due.end());
    CHECK(ticked->hit == core::HitKind::Poison);
    CHECK(ticked->value == POISON_DAMAGE);
    CHECK(ticked < struck); // the due tick first, then the new strike
    const std::vector<core::Event> ended = OfUse(due, FIRST);
    REQUIRE_FALSE(ended.empty());
    CHECK(ended.back().kind == core::EventKind::UseEnded); // refreshed after its tick
    CHECK(HealthOf(host, fox).current == AMPLE - (2 * LANCE_DAMAGE) - POISON_DAMAGE);
}

TEST_CASE("the lance's flash carries its element and fades after its ticks") {
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    const Aimed ready = ReadyLance(host, 1);
    StampedCast(host, CAST, core::Slot::Spender, ready.point);
    const core::Snapshot cast = PublishedAt(host, CAST);
    REQUIRE(cast.flashes.size() == 1);
    CHECK(cast.flashes.front().point.x == ready.point.x);
    CHECK(cast.flashes.front().point.y == ready.point.y);
    CHECK(cast.flashes.front().startTick == CAST);
    CHECK(cast.flashes.front().element == core::HitKind::Poison);
    CHECK(cast.volleys.empty()); // a strike, not a volley

    Run(host, CAST + 1, CAST + RULES.flashTicks - 1);
    CHECK(PublishedAt(host, CAST + RULES.flashTicks - 1).flashes.size() == 1);
    Run(host, CAST + RULES.flashTicks, CAST + RULES.flashTicks);
    CHECK(PublishedAt(host, CAST + RULES.flashTicks).flashes.empty());
}

TEST_CASE("the lance's flash survives a save") {
    constexpr u64 SAVED_AT = CAST + 3;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    const Aimed ready = ReadyLance(host, 1);
    StampedCast(host, CAST, core::Slot::Spender, ready.point);
    Run(host, CAST + 1, SAVED_AT);
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    host.Save(writer);

    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_AT));
    loaded.EnterScene(); // as the logic does at the swap
    CHECK(loaded.StateHash() == host.StateHash());
    const core::Snapshot snapshot = PublishedAt(loaded, SAVED_AT);
    REQUIRE(snapshot.flashes.size() == 1);
    CHECK(snapshot.flashes.front().point.x == ready.point.x);
    CHECK(snapshot.flashes.front().point.y == ready.point.y);
    CHECK(snapshot.flashes.front().startTick == CAST);
    CHECK(snapshot.flashes.front().element == core::HitKind::Poison);
    Run(host, SAVED_AT + 1, CAST + RULES.flashTicks);
    Run(loaded, SAVED_AT + 1, CAST + RULES.flashTicks);
    CHECK(loaded.StateHash() == host.StateHash());
    CHECK(PublishedAt(loaded, CAST + RULES.flashTicks).flashes.empty());
}

// The seals: the flying swords raise one under their rings.

constexpr f32 SWORDS_SEAL_SIZE = 420.0F; // abilities.yaml's `seal: { name: sword_formation, size: 420 }`

// Whether two seals are the same record, bit for bit.
bool SameSeal(const core::Seal& lhs, const core::Seal& rhs) {
    return lhs.key == rhs.key && std::bit_cast<u64>(lhs.point) == std::bit_cast<u64>(rhs.point) &&
           std::bit_cast<u32>(lhs.size) == std::bit_cast<u32>(rhs.size) && lhs.startTick == rhs.startTick &&
           lhs.endTick == rhs.endTick && lhs.element == rhs.element && lhs.hasElement == rhs.hasElement &&
           lhs.showcase == rhs.showcase;
}

// How many seals raised in play `host` publishes at `tick` (test::RaisedSeals()).
std::size_t RaisedAt(game::Host& host, u64 tick) {
    return test::RaisedSeals(PublishedAt(host, tick)).size();
}

TEST_CASE("casting the swords raises one seal at the cast point until the last sword leaves") {
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    const core::Vec2 point = ReadyBolt(host);
    StampedCast(host, CAST, core::Slot::Special1, point);
    const core::Snapshot cast = PublishedAt(host, CAST);
    REQUIRE(cast.volleys.size() == 1);
    const std::vector<core::Seal> raised = test::RaisedSeals(cast);
    REQUIRE(raised.size() == 1);
    const core::Volley& volley = cast.volleys.front().volley;
    const core::Seal& seal = raised.front();
    const u64 end = core::LastDepartureTick(volley);
    CHECK(seal.key == core::SealKeyOf("sword_formation"));
    CHECK(seal.size == SWORDS_SEAL_SIZE);
    CHECK(seal.point.x == volley.origin.x); // centred on the rings
    CHECK(seal.point.y == volley.origin.y);
    CHECK(seal.startTick == CAST);
    CHECK(seal.endTick == end);
    CHECK(end > CAST + volley.shape.holdTicks); // past the first wave's hold: the swords are still on their rings
    CHECK(seal.hasElement);
    CHECK(seal.element == core::HitKind::Damage);
    CHECK_FALSE(seal.showcase);

    Run(host, CAST + 1, end - 1);
    CHECK(RaisedAt(host, end - 1) == 1); // the last sword still on its ring
    Run(host, end, end);
    CHECK(RaisedAt(host, end) == 0);
}

TEST_CASE("an ability without a seal raises none") {
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    const core::Vec2 point = ReadyBolt(host);
    StampedCast(host, CAST, core::Slot::Generator, point);
    const core::Snapshot snapshot = PublishedAt(host, CAST);
    CHECK(snapshot.volleys.size() == 1);
    const std::vector<core::Seal> raised = test::RaisedSeals(snapshot);
    CHECK(raised.empty());
}

TEST_CASE("a seal survives a save and load mid-formation") {
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    const core::Vec2 point = ReadyBolt(host);
    StampedCast(host, CAST, core::Slot::Special1, point);
    const core::Snapshot cast = PublishedAt(host, CAST);
    const std::vector<core::Seal> raisedAtCast = test::RaisedSeals(cast);
    REQUIRE(raisedAtCast.size() == 1);
    const core::Seal& raised = raisedAtCast.front();
    const u64 savedAt = CAST + ((raised.endTick - CAST) / 2);
    Run(host, CAST + 1, savedAt);
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    host.Save(writer);

    game::Host loaded(test::TestManifest(), game::CELLS_SCENE, Gathered(AMPLE));
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, savedAt));
    loaded.EnterScene(); // as the logic does at the swap
    CHECK(loaded.StateHash() == host.StateHash());
    const core::Snapshot snapshot = PublishedAt(loaded, savedAt);
    const std::vector<core::Seal> raisedLoaded = test::RaisedSeals(snapshot);
    REQUIRE(raisedLoaded.size() == 1);
    const bool same = SameSeal(raisedLoaded.front(), raised);
    CHECK(same);
    Run(loaded, savedAt + 1, raised.endTick - 1);
    CHECK(RaisedAt(loaded, raised.endTick - 1) == 1);
    Run(loaded, raised.endTick, raised.endTick);
    CHECK(RaisedAt(loaded, raised.endTick) == 0);
}

TEST_CASE("a scene change ends every seal") {
    constexpr u64 CHANGE = CAST + 5; // the swords still forming
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    const core::Vec2 point = ReadyBolt(host);
    StampedCast(host, CAST, core::Slot::Special1, point);
    Run(host, CAST + 1, CHANGE - 1);
    REQUIRE(RaisedAt(host, CHANGE - 1) == 1);
    const std::array<core::Action, 1> change{ { { .kind = core::ActionKind::CycleScene } } };
    std::vector<core::Event> events;
    host.Tick(At(CHANGE), { .actions = change, .held = {} }, events);
    host.EnterScene(); // as the logic does after the tick
    CHECK(RaisedAt(host, CHANGE) == 0);
}

TEST_CASE("a load ends every seal its save doesn't hold") {
    constexpr u64 LOADED = CAST + 5; // the swords still forming
    game::Host host(test::TestManifest(), game::ARENA_SCENE, Gathered(AMPLE));
    const core::Vec2 point = ReadyBolt(host);
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    host.Save(writer); // before the cast
    StampedCast(host, CAST, core::Slot::Special1, point);
    Run(host, CAST + 1, LOADED);
    REQUIRE(RaisedAt(host, LOADED) == 1);
    core::Reader reader{ bytes };
    REQUIRE(host.Load(reader, LOADED));
    host.EnterScene(); // as the logic does at the swap
    CHECK(RaisedAt(host, LOADED) == 0);
}

TEST_CASE("afflictions are valid with no negative damage") {
    constexpr i64 HARM = 40;
    CHECK(game::Afflictions{ .burn = { .damage = HARM, .ticksLeft = 1, .use = 1, .nextTick = 1 }, .poison = {} }.Valid());
    CHECK_FALSE(game::Afflictions{ .burn = {}, .poison = { .damage = -1, .ticksLeft = 1, .use = 1, .nextTick = 1 } }.Valid());
}

TEST_CASE("the data's combat rules set how long a death dissolves and when a fox respawns") {
    constexpr u64 DEATH = 5;
    constexpr u64 QUICK_DYING = 10;
    constexpr u64 QUICK_RESPAWN = 20;
    core::GameData data = FarFromTheCrystal();
    data.combat.dyingTicks = QUICK_DYING;
    data.combat.respawnTicks = QUICK_RESPAWN;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, data);
    Run(host, 1, DEATH);
    const core::StableId fox = Foxes(host).front();
    host.Damage(fox, FOX_HEALTH, DEATH);
    Run(host, DEATH + 1, DEATH + QUICK_DYING - 1);
    CHECK(host.State().DomainOf(fox).has_value());
    Run(host, DEATH + QUICK_DYING, DEATH + QUICK_DYING);
    CHECK_FALSE(host.State().DomainOf(fox).has_value());
    Run(host, DEATH + QUICK_DYING + 1, DEATH + QUICK_DYING + QUICK_RESPAWN - 1);
    CHECK(Foxes(host).size() == FOXES - 1);
    Run(host, DEATH + QUICK_DYING + QUICK_RESPAWN, DEATH + QUICK_DYING + QUICK_RESPAWN);
    CHECK(Foxes(host).size() == FOXES);
}

TEST_CASE("a respawn shortened by new game data comes on its own tick, before an earlier one, and survives a save") {
    constexpr u64 FIRST_DEATH = 5;
    constexpr u64 RELOAD = FIRST_DEATH + RULES.dyingTicks; // the first fox's removal queues its respawn first
    constexpr u64 SECOND_DEATH = RELOAD + 1;
    constexpr u64 QUICK_RESPAWN = 20;
    constexpr u64 FIRST_DUE = FIRST_DEATH + RULES.dyingTicks + RULES.respawnTicks;
    constexpr u64 SECOND_DUE = SECOND_DEATH + RULES.dyingTicks + QUICK_RESPAWN;
    static_assert(SECOND_DUE < FIRST_DUE);
    core::GameData data = FarFromTheCrystal();
    game::Host host(test::TestManifest(), game::ARENA_SCENE, data);
    Run(host, 1, FIRST_DEATH);
    const std::vector<core::StableId> foxes = Foxes(host);
    host.Damage(foxes.at(0), FOX_HEALTH, FIRST_DEATH);
    Run(host, FIRST_DEATH + 1, RELOAD);
    data.combat.respawnTicks = QUICK_RESPAWN;
    host.SetData(data);
    Run(host, SECOND_DEATH, SECOND_DEATH);
    host.Damage(foxes.at(1), FOX_HEALTH, SECOND_DEATH);
    Run(host, SECOND_DEATH + 1, SECOND_DUE - 1);
    REQUIRE(Foxes(host).size() == FOXES - 2);

    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    host.Save(writer);
    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, data);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SECOND_DUE - 1));
    loaded.EnterScene(); // as the logic does at the swap
    CHECK(loaded.StateHash() == host.StateHash());

    for (game::Host* const played : { &host, &loaded }) {
        Run(*played, SECOND_DUE, SECOND_DUE);
        CHECK(Foxes(*played).size() == FOXES - 1);
        Run(*played, SECOND_DUE + 1, FIRST_DUE - 1);
        CHECK(Foxes(*played).size() == FOXES - 1);
        Run(*played, FIRST_DUE, FIRST_DUE);
        CHECK(Foxes(*played).size() == FOXES);
    }
}

TEST_CASE("the data's combat rules set the aura's period and the lance's burst") {
    constexpr u64 PERIOD = 20;
    constexpr u32 SET_DAMAGE = 7;
    core::GameData data = NearTheCrystal();
    data.combat.auraPeriod = PERIOD;
    data.combat.auraDamageMin = SET_DAMAGE;
    data.combat.auraDamageMax = SET_DAMAGE;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, data);
    std::vector<core::Event> events;
    Run(host, 1, PERIOD - 1);
    host.Tick(At(PERIOD), {}, events);
    const auto hit = std::ranges::find(events, core::EventKind::Hit, &core::Event::kind);
    REQUIRE(hit != events.end());
    CHECK(hit->value == SET_DAMAGE);
}

TEST_CASE("the data's combat rules set the lance's burst and its poison's period") {
    constexpr u64 FLASH = 5;
    constexpr u64 PERIOD = 7;
    core::GameData data = Gathered(AMPLE);
    data.combat.flashTicks = FLASH;
    data.combat.afflictionPeriod = PERIOD;
    constexpr f32 EVERYONE = 5'000.0F; // tags every fox, wherever the point
    data.abilities.table.at(POISON_LANCE).radius = EVERYONE;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, data);
    Run(host, 1, CAST - 1);
    static_cast<void>(StampedCast(host, CAST, core::Slot::Spender, ChestOf(host, Foxes(host).front())));
    std::vector<core::Event> events;
    for (u64 tick = CAST + 1; tick <= CAST + PERIOD; ++tick) {
        std::vector<core::Event> now;
        host.Tick(At(tick), {}, now);
        for (core::Event event : now) {
            event.tick = tick;
            events.push_back(event);
        }
        if (tick == CAST + FLASH - 1) {
            CHECK(PublishedAt(host, tick).flashes.size() == 1);
        }
        if (tick == CAST + FLASH) {
            CHECK(PublishedAt(host, tick).flashes.empty());
        }
    }
    constexpr u64 USE = game::CastUse(CAST, core::Slot::Spender);
    const auto ticked = std::ranges::count_if(events, [](const core::Event& event) {
        return event.kind == core::EventKind::Hit && event.use == USE && event.tick == CAST + PERIOD;
    });
    CHECK(ticked >= 1);
}

} // namespace
