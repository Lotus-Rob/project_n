#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/jobs.hpp"
#include "core/state.hpp"
#include "data/effects.hpp"
#include "data/game_data.hpp"
#include "game/arena.hpp"
#include "game/npcs.hpp"
#include "platform/paths.hpp"
#include "support/manifest.hpp"

#include <doctest/doctest.h>

namespace {

using core::atlas::SpriteId;

// Ticks `arena`, which the tests below that look at no events do.
void Tick(game::Arena& arena, const core::TickCounters& counters, const core::TickInput& input) {
    std::vector<core::Event> events;
    arena.Tick(counters, input, events);
}

core::Snapshot Published(game::Arena& arena) {
    core::Jobs jobs{ 1 };
    core::Snapshot snapshot;
    arena.Publish(snapshot, jobs);
    return snapshot;
}

// Whether an instance draws `sprite`: its region starts where the sprite's does, on the same page (the low half of
// layerFlags; sprites on different pages can start at the same point).
bool Draws(const core::Instance& instance, SpriteId sprite) {
    constexpr u32 PAGE_MASK = 0xFFFFU;
    const core::Instance expected = core::MakeInstance(*test::TestManifest(), { .sprite = sprite });
    return instance.regionMin == expected.regionMin && (instance.layerFlags & PAGE_MASK) == (expected.layerFlags & PAGE_MASK);
}

TEST_CASE("the ground is drawn first") {
    game::Arena arena(test::TestManifest());
    const core::Snapshot snapshot = Published(arena);
    REQUIRE_FALSE(snapshot.instances.empty());
    CHECK(Draws(snapshot.instances.front(), SpriteId::Ground));
}

TEST_CASE("shadows are drawn before what casts them") {
    game::Arena arena(test::TestManifest());
    const core::Snapshot snapshot = Published(arena);
    const auto firstShadow =
        std::ranges::find_if(snapshot.instances, [](const auto& instance) { return Draws(instance, SpriteId::Shadow); });
    const auto firstFox =
        std::ranges::find_if(snapshot.instances, [](const auto& instance) { return Draws(instance, SpriteId::Fox); });
    REQUIRE(firstShadow != snapshot.instances.end());
    REQUIRE(firstFox != snapshot.instances.end());
    CHECK(firstShadow < firstFox);
}

// The positions of every instance, sorted, so two snapshots compare whatever order they drew in.
std::vector<std::pair<f32, f32>> Positions(const core::Snapshot& snapshot, core::Vec2 core::Instance::* which) {
    std::vector<std::pair<f32, f32>> positions;
    positions.reserve(snapshot.instances.size());
    for (const core::Instance& instance : snapshot.instances) {
        positions.emplace_back((instance.*which).x, (instance.*which).y);
    }
    std::ranges::sort(positions);
    return positions;
}

TEST_CASE("a tick moves the foxes, and their previous position is where they were") {
    game::Arena arena(test::TestManifest());
    const core::Snapshot before = Published(arena);
    Tick(arena, { .wall = 1, .simulation = 1, .state = 1 }, {});
    const core::Snapshot after = Published(arena);
    // Copied, not recomputed: the previous positions are the same floats.
    CHECK(Positions(after, &core::Instance::previous) == Positions(before, &core::Instance::current));
    CHECK(Positions(after, &core::Instance::current) != Positions(before, &core::Instance::current));
}

// The packed rotation (previous half low, current half high) of the first instance drawing `sprite`.
u32 RotationOf(const core::Snapshot& snapshot, SpriteId sprite) {
    const auto found =
        std::ranges::find_if(snapshot.instances, [sprite](const core::Instance& instance) { return Draws(instance, sprite); });
    REQUIRE(found != snapshot.instances.end());
    return found->rotation;
}

TEST_CASE("the ice crystal turns each tick, from where it was") {
    constexpr u32 HALF_BITS = 16;
    game::Arena arena(test::TestManifest());
    Tick(arena, { .wall = 1, .simulation = 1, .state = 1 }, {});
    const u32 before = RotationOf(Published(arena), SpriteId::CrystalIce);
    Tick(arena, { .wall = 2, .simulation = 2, .state = 2 }, {});
    const u32 after = RotationOf(Published(arena), SpriteId::CrystalIce);
    CHECK((after & 0xFFFFU) == (before >> HALF_BITS)); // its previous rotation is the last tick's current one
    CHECK((after >> HALF_BITS) != (after & 0xFFFFU));
}

// The current position of the instance drawing `entity`, or NaN when none does.
core::Vec2 PositionOf(const core::Snapshot& snapshot, core::StableId entity) {
    const auto found = std::ranges::find(snapshot.ids, entity);
    if (found == snapshot.ids.end()) {
        return { .x = std::numeric_limits<f32>::quiet_NaN(), .y = std::numeric_limits<f32>::quiet_NaN() };
    }
    return snapshot.instances.at(static_cast<std::size_t>(found - snapshot.ids.begin())).current;
}

constexpr core::HeldInput HELD_RIGHT{ .move = { .x = 1.0F, .y = 0.0F } };

TEST_CASE("the cultivator walks where the held input points, and the camera follows it") {
    game::Arena arena(test::TestManifest());
    REQUIRE(arena.Player() != core::StableId{});
    const core::Snapshot before = Published(arena);
    REQUIRE(before.follow.has_value());
    Tick(arena, { .wall = 1, .simulation = 1, .state = 1 }, { .actions = {}, .held = HELD_RIGHT });
    const core::Snapshot after = Published(arena);
    REQUIRE(after.follow.has_value());
    const core::Follow from = before.follow.value_or(core::Follow{});
    const core::Follow onto = after.follow.value_or(core::Follow{});
    CHECK(onto.previous.x == from.current.x);
    CHECK(onto.current.x > from.current.x);
    CHECK(onto.current.y == from.current.y);
    CHECK(PositionOf(after, arena.Player()).x - PositionOf(before, arena.Player()).x == onto.current.x - from.current.x);
}

TEST_CASE("the cultivator walks at the game data's speed") {
    constexpr f32 SPEED = 7.0F;
    core::GameData data = game::BuiltInData();
    data.cultivator.walkSpeed = SPEED;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, data);
    const core::Vec2 before = PositionOf(Published(arena), arena.Player());
    Tick(arena, { .wall = 1, .simulation = 1, .state = 1 }, { .actions = {}, .held = HELD_RIGHT });
    const core::Vec2 after = PositionOf(Published(arena), arena.Player());
    CHECK(after.x - before.x == doctest::Approx(SPEED));
}

// How far apart, left to right, the instances drawing `sprite` lie.
f32 Spread(const core::Snapshot& snapshot, SpriteId sprite) {
    f32 left = std::numeric_limits<f32>::max();
    f32 right = std::numeric_limits<f32>::lowest();
    for (const core::Instance& instance : snapshot.instances) {
        if (Draws(instance, sprite)) {
            left = std::min(left, instance.current.x);
            right = std::max(right, instance.current.x);
        }
    }
    return right - left;
}

// Where the leftmost instance drawing `sprite` stands, left to right, after `ticks` ticks of `held` under `data`.
f32 LeftmostAfter(const core::GameData& data, u64 ticks, core::HeldInput held, SpriteId sprite) {
    game::Arena arena(test::TestManifest(), game::Scene::Arena, data);
    for (u64 tick = 1; tick <= ticks; ++tick) {
        Tick(arena, { .wall = tick, .simulation = tick, .state = tick }, { .actions = {}, .held = held });
    }
    f32 left = std::numeric_limits<f32>::max();
    for (const core::Instance& instance : Published(arena).instances) {
        if (Draws(instance, sprite)) {
            left = std::min(left, instance.current.x);
        }
    }
    return left;
}

TEST_CASE("foxes that orbit the player follow where it walks; others keep circling its start") {
    constexpr u64 TICKS = 30;
    core::GameData data = game::BuiltInData();
    data.fox.orbitsPlayer = true;
    const f32 walked = static_cast<f32>(TICKS) * data.cultivator.walkSpeed;
    CHECK(
        LeftmostAfter(data, TICKS, HELD_RIGHT, SpriteId::Fox) - LeftmostAfter(data, TICKS, {}, SpriteId::Fox) ==
        doctest::Approx(walked)
    );
    data.fox.orbitsPlayer = false;
    CHECK(LeftmostAfter(data, TICKS, HELD_RIGHT, SpriteId::Fox) == LeftmostAfter(data, TICKS, {}, SpriteId::Fox));
}

TEST_CASE("new game data changes the foxes' sprite and orbit at once") {
    constexpr f32 WIDER = 2.0F;
    constexpr f32 CLEARLY_WIDER = 1.5F; // the foxes' foot offsets blur the doubling
    game::Arena arena(test::TestManifest());
    core::GameData data = game::BuiltInData();
    data.fox.sprite = SpriteId::FoxYellow;
    data.fox.orbitRadius *= WIDER;
    const u64 before = arena.DataHash();
    const f32 builtInSpread = Spread(Published(arena), SpriteId::Fox);
    arena.SetData(data);
    CHECK(arena.DataHash() == data.Hash());
    CHECK(arena.DataHash() != before);
    Tick(arena, { .wall = 1, .simulation = 1, .state = 1 }, {});
    const core::Snapshot snapshot = Published(arena);
    CHECK(std::ranges::none_of(snapshot.instances, [](const auto& instance) { return Draws(instance, SpriteId::Fox); }));
    CHECK(Spread(snapshot, SpriteId::FoxYellow) > builtInSpread * CLEARLY_WIDER);
}

TEST_CASE("standing still, the cultivator draws still") {
    game::Arena arena(test::TestManifest());
    Tick(arena, { .wall = 1, .simulation = 1, .state = 1 }, { .actions = {}, .held = HELD_RIGHT });
    Tick(arena, { .wall = 2, .simulation = 2, .state = 2 }, {});
    const core::Snapshot snapshot = Published(arena);
    REQUIRE(snapshot.follow.has_value());
    const core::Follow follow = snapshot.follow.value_or(core::Follow{});
    CHECK(follow.previous.x == follow.current.x);
}

TEST_CASE("each entity's instance carries its ID; the ground and shadows carry none") {
    game::Arena arena(test::TestManifest());
    const core::Snapshot snapshot = Published(arena);
    REQUIRE(snapshot.ids.size() == snapshot.instances.size());
    CHECK(snapshot.ids.front() == core::StableId{});
    CHECK(std::ranges::contains(snapshot.ids, arena.Player()));
}

TEST_CASE("the snapshot names the selected entity's instance; selecting nothing clears it") {
    game::Arena arena(test::TestManifest());
    const std::array<core::Action, 1> select{ { { .kind = core::ActionKind::Select, .target = arena.Player() } } };
    Tick(arena, { .wall = 1, .simulation = 1, .state = 1 }, { .actions = select, .held = {} });
    CHECK(arena.Selected() == arena.Player());
    const core::Snapshot snapshot = Published(arena);
    const auto drawn = std::ranges::find(snapshot.ids, arena.Player());
    REQUIRE(drawn != snapshot.ids.end());
    CHECK(snapshot.selected == std::optional<u32>{ static_cast<u32>(drawn - snapshot.ids.begin()) });

    const std::array<core::Action, 1> none{ { { .kind = core::ActionKind::Select } } };
    Tick(arena, { .wall = 2, .simulation = 2, .state = 2 }, { .actions = none, .held = {} });
    CHECK(arena.Selected() == core::StableId{});
    CHECK_FALSE(Published(arena).selected.has_value());
}

TEST_CASE("a cast outside a combat scene is ignored") {
    constexpr core::TickCounters COUNTERS{ .wall = 1, .simulation = 1, .state = 1 };
    const std::array<core::Action, 1> cast{
        { { .kind = core::ActionKind::Cast, .slot = core::Slot::Generator, .point = { .x = 50.0F, .y = 60.0F } } }
    };
    for (const game::Scene scene : { game::Scene::Stress, game::Scene::Painted, game::Scene::Cells }) {
        game::Arena casting(test::TestManifest(), scene);
        game::Arena idle(test::TestManifest(), scene);
        std::vector<core::Event> events;
        casting.Tick(COUNTERS, { .actions = cast, .held = {} }, events);
        Tick(idle, COUNTERS, { .actions = {}, .held = {} });
        CHECK(events.empty());
        CHECK(casting.StateHash() == idle.StateHash());
    }
}

TEST_CASE("the arena is a combat scene and the others aren't") {
    for (const game::Scene scene : { game::Scene::Arena, game::Scene::Stress, game::Scene::Painted, game::Scene::Cells }) {
        game::Arena arena(test::TestManifest(), scene);
        const bool combat = scene == game::Scene::Arena;
        CHECK(arena.IsCombatScene() == combat);
        CHECK(Published(arena).combat == combat);
    }
}

TEST_CASE("a stale ID selects nothing") {
    constexpr core::StableId NEVER_HANDED_OUT{ 1'000'000 };
    game::Arena arena(test::TestManifest());
    const std::array<core::Action, 1> select{ { { .kind = core::ActionKind::Select, .target = NEVER_HANDED_OUT } } };
    Tick(arena, { .wall = 1, .simulation = 1, .state = 1 }, { .actions = select, .held = {} });
    CHECK(arena.Selected() == core::StableId{});
}

TEST_CASE("the still scenes have no player and follow nothing") {
    game::Arena arena(test::TestManifest(), game::Scene::Painted);
    CHECK(arena.Player() == core::StableId{});
    CHECK_FALSE(Published(arena).follow.has_value());
}

// The sprite an instance draws, and where its foot stands: MakeInstance offsets the quad's
// centre from the foot by a fixed amount per sprite, times the scale the instance's size shows.
std::optional<std::pair<SpriteId, f32>> SpriteAndFootY(const core::Instance& instance) {
    constexpr u32 LOW_HALF = 0xFFFFU;
    for (std::size_t id = 0; id < core::atlas::SPRITE_NAMES.size(); ++id) {
        const auto sprite = static_cast<SpriteId>(id);
        if (Draws(instance, sprite)) {
            const core::Instance unscaled = core::MakeInstance(*test::TestManifest(), { .sprite = sprite });
            const f32 scale = core::UnpackHalf(static_cast<u16>(instance.size & LOW_HALF)) /
                              core::UnpackHalf(static_cast<u16>(unscaled.size & LOW_HALF));
            return std::pair{ sprite, instance.current.y - (unscaled.current.y * scale) };
        }
    }
    return std::nullopt;
}

// The arena after enough ticks that foxes have moved in front of and behind things.
core::Snapshot MovedArena() {
    constexpr u64 TICKS = 90;
    game::Arena arena(test::TestManifest());
    for (u64 tick = 1; tick <= TICKS; ++tick) {
        Tick(arena, { .wall = tick, .simulation = tick, .state = tick }, {});
    }
    return Published(arena);
}

TEST_CASE("the arena publishes a normal batch, then an additive one that ends the snapshot") {
    const core::Snapshot snapshot = MovedArena();
    REQUIRE(snapshot.batches.size() == 2);
    CHECK(snapshot.batches[0].pipeline == core::Pipeline::Normal);
    CHECK(snapshot.batches[1].pipeline == core::Pipeline::Additive);
    CHECK(snapshot.batches[1].first + snapshot.batches[1].count == snapshot.instances.size());
}

TEST_CASE("the world draws in foot-y order, after the ground and its decals") {
    const core::Snapshot snapshot = MovedArena();
    REQUIRE_FALSE(snapshot.batches.empty());
    f32 lastFoot = -std::numeric_limits<f32>::infinity();
    std::size_t world = 0;
    for (u32 i = 0; i < snapshot.batches.back().first; ++i) {
        const auto drawn = SpriteAndFootY(snapshot.instances[i]);
        if (!drawn) {
            FAIL("an instance draws no sprite of the manifest");
            break;
        }
        if (drawn->first == SpriteId::Ground || drawn->first == SpriteId::Shadow) {
            CHECK(world == 0); // the ground and its decals come before the world
            continue;
        }
        ++world;
        CHECK(drawn->second >= lastFoot);
        lastFoot = drawn->second;
    }
    CHECK(world > 0);
}

TEST_CASE("the additive batch holds only the orbs") {
    const core::Snapshot snapshot = MovedArena();
    REQUIRE_FALSE(snapshot.batches.empty());
    for (u32 i = snapshot.batches.back().first; i < snapshot.instances.size(); ++i) {
        CHECK(
            (Draws(snapshot.instances[i], SpriteId::OrbBlue) || Draws(snapshot.instances[i], SpriteId::OrbRed) ||
             Draws(snapshot.instances[i], SpriteId::OrbGold))
        );
    }
}

TEST_CASE("the stress arena publishes 100,000 instances") {
    constexpr std::size_t INSTANCES = 100'000;
    game::Arena arena(test::TestManifest(), game::Scene::Stress);
    CHECK(Published(arena).instances.size() >= INSTANCES);
}

TEST_CASE("the stress arena's 10,000 bullets draw last, in an additive batch") {
    constexpr u32 BULLETS = 10'000;
    game::Arena arena(test::TestManifest(), game::Scene::Stress);
    const core::Snapshot snapshot = Published(arena);
    REQUIRE_FALSE(snapshot.batches.empty());
    const core::Batch& effects = snapshot.batches.back();
    CHECK(effects.pipeline == core::Pipeline::Additive);
    CHECK(effects.count >= BULLETS);
    CHECK(effects.first + effects.count == snapshot.instances.size());
}

TEST_CASE("the painted scene's glows draw last, in an additive batch") {
    constexpr u32 GLOWS = 5;
    game::Arena arena(test::TestManifest(), game::Scene::Painted);
    const core::Snapshot snapshot = Published(arena);
    REQUIRE_FALSE(snapshot.batches.empty());
    CHECK(snapshot.batches.back().pipeline == core::Pipeline::Additive);
    CHECK(snapshot.batches.back().count == GLOWS);
}

TEST_CASE("the painted scene holds still") {
    constexpr u64 TICK = 7;
    game::Arena arena(test::TestManifest(), game::Scene::Painted);
    const core::Snapshot before = Published(arena);
    Tick(arena, { .wall = TICK, .simulation = TICK, .state = TICK }, {});
    const core::Snapshot after = Published(arena);
    CHECK(std::ranges::equal(std::as_bytes(std::span{ before.instances }), std::as_bytes(std::span{ after.instances })));
}

TEST_CASE("the cell grid publishes every cell, with none culled at its far corners") {
    constexpr std::size_t CELLS = 640;
    game::Arena arena(test::TestManifest(), game::Scene::Cells);
    const core::Snapshot snapshot = Published(arena);
    CHECK(snapshot.instances.size() == CELLS + 1); // and the ground, hidden beneath them
}

TEST_CASE("the stress arena publishes the same bytes with one worker and with many") {
    constexpr u64 TICK = 7;
    constexpr u32 MANY_WORKERS = 7;
    std::vector<std::vector<core::Instance>> published;
    for (const u32 workers : { 1U, MANY_WORKERS }) {
        game::Arena arena(test::TestManifest(), game::Scene::Stress);
        Tick(arena, { .wall = TICK, .simulation = TICK, .state = TICK }, {});
        core::Jobs jobs{ workers };
        core::Snapshot snapshot;
        arena.Publish(snapshot, jobs);
        published.push_back(std::move(snapshot.instances));
    }
    REQUIRE_FALSE(published[0].empty());
    CHECK(std::ranges::equal(std::as_bytes(std::span{ published[0] }), std::as_bytes(std::span{ published[1] })));
}

TEST_CASE("two arenas ticked alike publish the same bytes") {
    constexpr u64 TICKS = 120;
    game::Arena first(test::TestManifest());
    game::Arena second(test::TestManifest());
    for (u64 tick = 1; tick <= TICKS; ++tick) {
        Tick(first, { .wall = tick, .simulation = tick, .state = tick }, {});
        Tick(second, { .wall = tick, .simulation = tick, .state = tick }, {});
    }
    const std::vector<core::Instance> mine = Published(first).instances;
    const std::vector<core::Instance> theirs = Published(second).instances;
    CHECK(std::ranges::equal(std::as_bytes(std::span{ mine }), std::as_bytes(std::span{ theirs })));
}

TEST_CASE("an orbit stays on its circle after many ticks") {
    constexpr f64 RADIUS = 150.0;
    constexpr f32 SPEED = -0.02F;
    constexpr game::Orbit ORBIT{ .radius = static_cast<f32>(RADIUS), .speed = SPEED };
    constexpr f64 VIEW_TILT = 0.6;                                     // arena.cpp's: the circle is drawn flattened in y
    constexpr f64 TOLERANCE = 0.01;                                    // world units
    constexpr std::array<u64, 2> TICKS{ { 16'777'217, 100'000'000 } }; // 2^24 + 1, and about 19 days at 60 Hz
    for (const u64 tick : TICKS) {
        CAPTURE(tick);
        const f64 angle = static_cast<f64>(SPEED) * static_cast<f64>(tick); // exact in f64
        const core::Vec2 point = game::OrbitPoint(ORBIT, tick);
        CHECK(std::abs(static_cast<f64>(point.x) - (RADIUS * std::cos(angle))) < TOLERANCE);
        CHECK(std::abs(static_cast<f64>(point.y) - (RADIUS * std::sin(angle) * VIEW_TILT)) < TOLERANCE);
    }
}

TEST_CASE("months pass for the stress arena's world NPCs, and its active NPCs stay put") {
    constexpr u32 WORLD_NPCS = 50'000;
    constexpr std::size_t ACTIVE_NPCS = 6;
    game::Arena arena(test::TestManifest(), game::Scene::Stress);
    core::Jobs jobs{ 1 };
    const core::State& state = arena.State();
    CHECK(state[game::WORLD].Entities().storage<game::Npc>()->size() == WORLD_NPCS);
    CHECK(state[game::ACTIVE].Entities().storage<game::Npc>()->size() == ACTIVE_NPCS);

    constexpr u64 MONTHS = 12; // loneliness outweighs ambition after several
    for (u64 month = 1; month <= MONTHS; ++month) {
        arena.AdvanceCalendar({ .wall = month, .simulation = 0, .state = month }, jobs, {});
    }
    CHECK_FALSE(arena.NpcRelationships().All().empty());
    CHECK(state[game::ACTIVE].Entities().storage<game::Npc>()->size() == ACTIVE_NPCS);
}

TEST_CASE("an arena needs the manifest's scene of its own name") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::SceneId arena = game::Arena(manifest).Scene();
    const core::SceneId painted = game::Arena(manifest, game::Scene::Painted).Scene();
    CHECK(arena == manifest->SceneNamed("arena"));
    CHECK(painted == manifest->SceneNamed("painted"));
}

TEST_CASE("each scene is found by its name") {
    for (const game::Scene scene : { game::Scene::Arena, game::Scene::Stress, game::Scene::Painted, game::Scene::Cells }) {
        const std::optional<game::Scene> found = game::SceneNamed(game::SceneName(scene));
        CHECK(found == scene);
    }
    const std::optional<game::Scene> unknown = game::SceneNamed("nowhere");
    CHECK_FALSE(unknown.has_value());
}

TEST_CASE("F2's CycleScene visits every scene in order and back, clearing the selection") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    game::Arena arena(manifest);
    const std::array<core::Action, 1> select{ { { .kind = core::ActionKind::Select, .target = arena.Player() } } };
    Tick(arena, { .wall = 1, .simulation = 1, .state = 1 }, { .actions = select, .held = {} });
    REQUIRE(arena.Selected() == arena.Player());
    const std::array<core::Action, 1> cycle{ { { .kind = core::ActionKind::CycleScene } } };
    u64 tick = 1;
    for (const std::string_view name : { "stress", "painted", "cells", "arena" }) {
        ++tick;
        Tick(arena, { .wall = tick, .simulation = tick, .state = tick }, { .actions = cycle, .held = {} });
        const std::optional<core::SceneId> expected = manifest->SceneNamed(name);
        CHECK(arena.Scene() == expected);
    }
    CHECK(arena.Selected() == core::StableId{});
    CHECK(arena.Player() != core::StableId{});
}

TEST_CASE("the shipped creatures and abilities tables are the built-in data") {
    const std::expected<core::GameData, core::Error> shipped =
        platform::AssetsDirectory().and_then([](const std::filesystem::path& assets) {
            return data::LoadGameData(assets / "data");
        });
    REQUIRE_MESSAGE(shipped.has_value(), shipped.error().message);
    const u64 shippedHash = shipped->Hash();
    const u64 builtInHash = game::BuiltInData().Hash();
    CHECK(shippedHash == builtInHash);
}

TEST_CASE("the shipped effects table is the default looks and layout") {
    const std::expected<core::Effects, core::Error> shipped =
        platform::AssetsDirectory().and_then([](const std::filesystem::path& assets) {
            return data::LoadEffects(assets / "data");
        });
    REQUIRE_MESSAGE(shipped.has_value(), shipped.error().message);
    CHECK(shipped->Hash() == core::Effects{}.Hash());
}

TEST_CASE("the shipped manifest runs every scene with the built-in data") {
    const std::expected<void, core::Error> checked = game::CheckScenes(*test::TestManifest(), game::BuiltInData());
    CHECK(checked.has_value());
}

TEST_CASE("a manifest without one of the scenes is refused") {
    core::Manifest manifest = *test::TestManifest();
    std::erase_if(manifest.scenes, [](const core::SceneInfo& scene) { return scene.name == "cells"; });
    const std::expected<void, core::Error> checked = game::CheckScenes(manifest, game::BuiltInData());
    REQUIRE_FALSE(checked.has_value());
    CHECK(checked.error().message.contains("cells"));
}

TEST_CASE("a scene with creatures that doesn't load their data's sprites is refused") {
    const core::Manifest manifest = *test::TestManifest();
    core::GameData data = game::BuiltInData();
    data.fox.sprite = SpriteId::CellCity; // which only the cells load
    const std::expected<void, core::Error> checked = game::CheckScenes(manifest, data);
    REQUIRE_FALSE(checked.has_value());
    CHECK(checked.error().message.contains("cell_city"));
}

// Hits: the aura and the debug burst.

constexpr u64 AURA_TICK = 60;
constexpr u64 BURST_TICK = 1;
constexpr i64 AURA_DAMAGE_MIN = 50;
constexpr i64 AURA_DAMAGE_MAX = 400;
constexpr i64 AURA_HEALING_MIN = 50;
constexpr i64 AURA_HEALING_MAX = 200;
constexpr i64 BURST_MIN = 1;
constexpr i64 BURST_MAX = 99;
constexpr u32 USE_AURA = 1;
constexpr u32 USE_DEBUG_BURST = 2;
constexpr f32 AURA_REACH = 320.0F;
constexpr core::Vec2 CRYSTAL_FOOT{ .x = 420.0F, .y = 620.0F };
constexpr core::HeldInput HELD_LEFT{ .move = { .x = -1.0F, .y = 0.0F } };

// Game data that walks the cultivator to 100 units from the crystal in one tick and circles the foxes 300 units round
// it, which puts some of them in the aura's reach and some out.
core::GameData NearCrystalData() {
    constexpr f32 WALK_SPEED = 440.0F;
    constexpr f32 FOX_ORBIT = 300.0F;
    core::GameData data = game::BuiltInData();
    data.cultivator.walkSpeed = WALK_SPEED;
    data.fox.orbitRadius = FOX_ORBIT;
    data.fox.orbitWobble = 0.0F;
    return data;
}

core::TickCounters At(u64 tick) {
    return { .wall = tick, .simulation = tick, .state = tick };
}

// Ticks `arena` from tick 1 to `last`, the first tick walking left, and returns every event of them.
std::vector<core::Event> TickAll(game::Arena& arena, u64 last) {
    std::vector<core::Event> events;
    for (u64 tick = 1; tick <= last; ++tick) {
        const core::HeldInput held = tick == 1 ? HELD_LEFT : core::HeldInput{};
        arena.Tick(At(tick), { .actions = {}, .held = held }, events);
    }
    return events;
}

// The same, returning the events of the last tick alone.
std::vector<core::Event> TickTo(game::Arena& arena, u64 last) {
    std::vector<core::Event> events = TickAll(arena, last - 1);
    events.clear();
    arena.Tick(At(last), {}, events);
    return events;
}

core::Vec2 FootOf(const game::Arena& arena, core::StableId entity) {
    const core::Domain& active = arena.State()[game::ACTIVE];
    return active.Entities().get<game::Body>(active.Find(entity)).current;
}

std::vector<core::StableId> Foxes(const game::Arena& arena) {
    std::vector<core::StableId> foxes;
    const core::Registry& entities = arena.State()[game::ACTIVE].Entities();
    for (auto [entity, creature, stable] : entities.view<const game::Creature, const core::StableId>().each()) {
        if (creature.kind == game::CreatureKind::Fox) {
            foxes.push_back(stable);
        }
    }
    return foxes;
}

bool InReach(core::Vec2 foot) {
    return std::hypot(foot.x - CRYSTAL_FOOT.x, foot.y - CRYSTAL_FOOT.y) <= AURA_REACH;
}

std::vector<core::Event> HitsOf(const std::vector<core::Event>& events) {
    std::vector<core::Event> hits;
    std::ranges::copy_if(events, std::back_inserter(hits), [](const core::Event& event) {
        return event.kind == core::EventKind::Hit;
    });
    return hits;
}

TEST_CASE("the aura hits a fox in reach once a second and none between") {
    game::Arena arena(test::TestManifest(), game::Scene::Arena, NearCrystalData());
    CHECK(TickAll(arena, AURA_TICK - 1).empty());
    std::vector<core::Event> events;
    arena.Tick(At(AURA_TICK), {}, events);
    REQUIRE_FALSE(events.empty());
    CHECK(events.back().kind == core::EventKind::UseEnded);
    CHECK(events.back().use == core::AbilityUse(AURA_TICK, USE_AURA));
    std::size_t damaged = 0;
    for (const core::StableId fox : Foxes(arena)) {
        const bool reached = InReach(FootOf(arena, fox));
        const auto hit = std::ranges::find_if(events, [fox](const core::Event& event) {
            return event.kind == core::EventKind::Hit && event.target == fox;
        });
        CHECK((hit != events.end()) == reached);
        if (hit != events.end()) {
            ++damaged;
            CHECK(hit->hit == core::HitKind::Damage);
            CHECK(hit->value >= AURA_DAMAGE_MIN);
            CHECK(hit->value <= AURA_DAMAGE_MAX);
            CHECK(hit->use == events.back().use);
        }
    }
    CHECK(damaged > 0);
    CHECK(damaged < Foxes(arena).size());
}

TEST_CASE("the aura misses a fox out of reach") {
    constexpr f32 FAR_ORBIT = 1'000.0F; // round the cultivator, 540 units from the crystal: no fox comes within reach
    core::GameData data = game::BuiltInData();
    data.fox.orbitRadius = FAR_ORBIT;
    data.fox.orbitWobble = 0.0F;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, data);
    const std::vector<core::Event> events = TickTo(arena, AURA_TICK);
    for (const core::StableId fox : Foxes(arena)) {
        REQUIRE_FALSE(InReach(FootOf(arena, fox)));
    }
    REQUIRE(events.size() == 1);
    CHECK(events.front().kind == core::EventKind::UseEnded);
}

TEST_CASE("the aura heals the player in reach") {
    game::Arena arena(test::TestManifest(), game::Scene::Arena, NearCrystalData());
    const std::vector<core::Event> events = TickTo(arena, AURA_TICK);
    const auto heal = std::ranges::find(events, arena.Player(), &core::Event::target);
    REQUIRE(heal != events.end());
    CHECK(heal->kind == core::EventKind::Hit);
    CHECK(heal->hit == core::HitKind::Healing);
    CHECK(heal->value >= AURA_HEALING_MIN);
    CHECK(heal->value <= AURA_HEALING_MAX);
}

TEST_CASE("the aura's hits are the same on a rerun") {
    game::Arena first(test::TestManifest(), game::Scene::Arena, NearCrystalData());
    game::Arena second(test::TestManifest(), game::Scene::Arena, NearCrystalData());
    const std::vector<core::Event> one = TickTo(first, AURA_TICK);
    const std::vector<core::Event> two = TickTo(second, AURA_TICK);
    REQUIRE(one.size() == two.size());
    for (std::size_t index = 0; index < one.size(); ++index) {
        CHECK(one.at(index).kind == two.at(index).kind);
        CHECK(one.at(index).hit == two.at(index).hit);
        CHECK(one.at(index).target == two.at(index).target);
        CHECK(one.at(index).value == two.at(index).value);
        CHECK(one.at(index).use == two.at(index).use);
        CHECK(std::bit_cast<u64>(one.at(index).position) == std::bit_cast<u64>(two.at(index).position));
    }
}

TEST_CASE("the aura hits at a fox's head") {
    game::Arena arena(test::TestManifest(), game::Scene::Arena, NearCrystalData());
    const std::vector<core::Event> events = TickTo(arena, AURA_TICK);
    const std::vector<core::Event> hits = HitsOf(events);
    REQUIRE_FALSE(hits.empty());
    const core::Vec2 foot = FootOf(arena, hits.front().target);
    CHECK(hits.front().position.x == doctest::Approx(foot.x));
    CHECK(hits.front().position.y < foot.y);
}

TEST_CASE("the debug burst leaves the state hash alone") {
    game::Arena burst(test::TestManifest());
    game::Arena plain(test::TestManifest());
    const std::array<core::Action, 1> actions{ { { .kind = core::ActionKind::DebugBurst } } };
    std::vector<core::Event> events;
    burst.Tick(At(BURST_TICK), { .actions = actions, .held = {} }, events);
    plain.Tick(At(BURST_TICK), {}, events);
    CHECK(burst.StateHash() == plain.StateHash());
}

TEST_CASE("the debug burst hits every fox with all four kinds") {
    constexpr std::size_t HITS = 4'000;
    constexpr std::size_t KINDS = 4;
    game::Arena arena(test::TestManifest());
    const std::array<core::Action, 1> actions{ { { .kind = core::ActionKind::DebugBurst } } };
    std::vector<core::Event> events;
    arena.Tick(At(BURST_TICK), { .actions = actions, .held = {} }, events);
    REQUIRE(events.size() == HITS + 1);
    CHECK(events.back().kind == core::EventKind::UseEnded);
    CHECK(events.back().use == core::AbilityUse(BURST_TICK, USE_DEBUG_BURST));
    const std::vector<core::StableId> foxes = Foxes(arena);
    std::unordered_map<core::StableId, std::size_t> perFox;
    std::unordered_map<core::StableId, std::array<std::size_t, KINDS>> kindsPerFox;
    std::array<std::size_t, KINDS> perKind{};
    bool allHits = true; // each of the burst's hits, with one check for what they have in common
    bool ofTheUse = true;
    i64 least = BURST_MAX;
    i64 most = BURST_MIN;
    for (std::size_t index = 0; index < HITS; ++index) {
        const core::Event& event = events.at(index);
        allHits = allHits && event.kind == core::EventKind::Hit;
        ofTheUse = ofTheUse && event.use == events.back().use;
        least = std::min(least, event.value);
        most = std::max(most, event.value);
        ++perFox[event.target];
        ++kindsPerFox[event.target].at(std::to_underlying(event.hit));
        ++perKind.at(std::to_underlying(event.hit));
    }
    REQUIRE(allHits);
    CHECK(ofTheUse);
    CHECK(least >= BURST_MIN);
    CHECK(most <= BURST_MAX);
    CHECK(perFox.size() == foxes.size());
    std::size_t fewest = HITS;
    std::size_t mostOnOne = 0;
    for (const auto& [fox, count] : perFox) {
        fewest = std::min(fewest, count);
        mostOnOne = std::max(mostOnOne, count);
    }
    CHECK(fewest >= HITS / foxes.size());
    CHECK(mostOnOne <= (HITS / foxes.size()) + 1);
    std::size_t fewestOfAKind = HITS; // every fox gets every kind, whatever the number of foxes
    for (const auto& [fox, kinds] : kindsPerFox) {
        fewestOfAKind = std::min(fewestOfAKind, std::ranges::min(kinds));
    }
    CHECK(fewestOfAKind > 0);
    // The kinds take turns by round over the foxes, so the totals depend on the fox count: with the arena's 12, of the
    // 333 full rounds of 12 hits damage takes 84 and each other kind 83, and the last 4 hits are healing.
    constexpr std::size_t ARENA_FOXES = 12;
    constexpr std::array<std::size_t, KINDS> TOTALS{ { 1'008, 1'000, 996, 996 } }; // damage, healing, poison, burn
    REQUIRE(foxes.size() == ARENA_FOXES);
    for (std::size_t kind = 0; kind < KINDS; ++kind) {
        INFO("kind ", kind);
        CHECK(perKind.at(kind) == TOTALS.at(kind));
    }
}

TEST_CASE("the debug burst hits nothing in a scene without foxes") {
    game::Arena arena(test::TestManifest(), game::Scene::Painted);
    const std::array<core::Action, 1> actions{ { { .kind = core::ActionKind::DebugBurst } } };
    std::vector<core::Event> events;
    arena.Tick(At(BURST_TICK), { .actions = actions, .held = {} }, events);
    REQUIRE(events.size() == 1);
    CHECK(events.front().kind == core::EventKind::UseEnded);
}

TEST_CASE("the arena labels the player and every fox, and captions the tablet") {
    constexpr std::size_t FOXES = 12;
    game::Arena arena(test::TestManifest());
    const core::Snapshot snapshot = Published(arena);
    const auto labels = [&](core::LabelPriority priority) {
        return std::ranges::count_if(snapshot.texts, [priority](const core::TextItem& text) {
            return text.kind == core::TextKind::Label && text.priority == priority;
        });
    };
    CHECK(labels(core::LabelPriority::Player) == 1);
    CHECK(labels(core::LabelPriority::Rest) == FOXES);
    std::vector<core::StableId> foxes;
    std::vector<u32> foxKeys;
    for (const core::TextItem& text : snapshot.texts) {
        if (text.kind == core::TextKind::Label && text.priority == core::LabelPriority::Rest) {
            CHECK(text.string == core::StringId::FoxName);
            foxes.push_back(text.entity);
            foxKeys.push_back(text.nameKey);
        }
    }
    std::vector<core::StableId> expected = Foxes(arena);
    std::ranges::sort(foxes);
    std::ranges::sort(expected);
    CHECK(foxes == expected);
    CHECK(std::ranges::adjacent_find(foxKeys, std::ranges::not_equal_to{}) == foxKeys.end());
    const auto captions = std::ranges::count_if(snapshot.texts, [](const core::TextItem& text) {
        return text.kind == core::TextKind::Caption && text.string == core::StringId::TabletCaption &&
               text.direction == core::TextDirection::TopToBottom;
    });
    CHECK(captions == 1);
}

TEST_CASE("labels and the caption leave their colour to the table") {
    game::Arena arena(test::TestManifest());
    const core::Snapshot snapshot = Published(arena);
    REQUIRE_FALSE(snapshot.texts.empty());
    for (const core::TextItem& text : snapshot.texts) {
        CHECK(text.colour == core::TextItem{}.colour);
    }
}

TEST_CASE("the player's label stands a little over its head") {
    constexpr f32 CULTIVATOR_FOOT_Y = 122.0F; // assets/manifest.yaml
    game::Arena arena(test::TestManifest());
    const core::Snapshot snapshot = Published(arena);
    const auto label = std::ranges::find(snapshot.texts, core::LabelPriority::Player, &core::TextItem::priority);
    REQUIRE(label != snapshot.texts.end());
    const core::Vec2 foot = FootOf(arena, arena.Player());
    CHECK(label->entity == arena.Player());
    CHECK(label->string == core::StringId::CultivatorName);
    CHECK(label->nameKey == std::to_underlying(core::StringId::CultivatorName));
    CHECK(label->current.x == foot.x);
    CHECK(label->current.y == foot.y - CULTIVATOR_FOOT_Y);
    CHECK(label->height == CULTIVATOR_FOOT_Y);
    CHECK(label->offset.y < 0.0F);
}

TEST_CASE("the still scenes publish no texts") {
    game::Arena arena(test::TestManifest(), game::Scene::Painted);
    CHECK(Published(arena).texts.empty());
}

constexpr u32 GLOW_ROLE_MASK = 0xFFU; // the role fills the glow word's low byte
constexpr f32 HALF = 0.5F;
constexpr f32 CHARGE_RISE_TICKS = 108.0F; // the demo charge's rise, in ticks
constexpr f32 CHARGE_BEFORE_MIDWAY = 53.0F;
constexpr f32 CHARGE_TEN = 10.0F;

// The role in an instance's glow words (core::Frame's Instance::glow).
core::GlowRole GlowRoleOf(const core::Snapshot& snapshot, SpriteId sprite) {
    const auto found =
        std::ranges::find_if(snapshot.instances, [sprite](const core::Instance& instance) { return Draws(instance, sprite); });
    REQUIRE(found != snapshot.instances.end());
    return static_cast<core::GlowRole>(found->glow & GLOW_ROLE_MASK);
}

TEST_CASE("the shrine, stele and crystal wear the demo glows") {
    game::Arena arena(test::TestManifest());
    const core::Snapshot snapshot = Published(arena);
    CHECK(GlowRoleOf(snapshot, SpriteId::Shrine) == core::GlowRole::DemoPulse);
    CHECK(GlowRoleOf(snapshot, SpriteId::Stele) == core::GlowRole::DemoCharge);
    CHECK(GlowRoleOf(snapshot, SpriteId::CrystalIce) == core::GlowRole::DemoProgress);
    CHECK(GlowRoleOf(snapshot, SpriteId::Pine) == core::GlowRole::None);
    CHECK(GlowRoleOf(snapshot, SpriteId::Fox) == core::GlowRole::None);
}

TEST_CASE("the demo pulse starts at tick 0") {
    const core::GlowInput glow = game::DemoGlow(core::GlowRole::DemoPulse, 500);
    CHECK(glow.role == core::GlowRole::DemoPulse);
    CHECK(glow.startTick == 0);
}

TEST_CASE("the demo charge rises, holds, releases and loops") {
    const core::GlowInput rising = game::DemoGlow(core::GlowRole::DemoCharge, 54);
    CHECK(rising.currentValue == doctest::Approx(HALF));
    CHECK(rising.previousValue == doctest::Approx(CHARGE_BEFORE_MIDWAY / CHARGE_RISE_TICKS));
    CHECK_FALSE(rising.released);
    const core::GlowInput held = game::DemoGlow(core::GlowRole::DemoCharge, 120);
    CHECK(held.currentValue == doctest::Approx(1.0F));
    CHECK_FALSE(held.released);
    const core::GlowInput released = game::DemoGlow(core::GlowRole::DemoCharge, 150);
    CHECK(released.released);
    CHECK(released.startTick == 144);
    const core::GlowInput next = game::DemoGlow(core::GlowRole::DemoCharge, 192 + 10);
    CHECK(next.currentValue == doctest::Approx(CHARGE_TEN / CHARGE_RISE_TICKS));
    CHECK_FALSE(next.released);
    CHECK(game::DemoGlow(core::GlowRole::DemoCharge, 192).previousValue == 0.0F); // a loop starts clean
    const core::GlowInput later = game::DemoGlow(core::GlowRole::DemoCharge, 342);
    CHECK(later.released);
    CHECK(later.startTick == 336); // the absolute tick, not the one in the loop
}

TEST_CASE("the demo progress fills, then shows a result that changes each loop") {
    const core::GlowInput filling = game::DemoGlow(core::GlowRole::DemoProgress, 90);
    CHECK(filling.currentValue == doctest::Approx(HALF));
    CHECK(filling.result == 0);
    CHECK(game::DemoGlow(core::GlowRole::DemoProgress, 200).result == 1);
    CHECK(game::DemoGlow(core::GlowRole::DemoProgress, 500).result == 2);
    const core::GlowInput restarted = game::DemoGlow(core::GlowRole::DemoProgress, 300); // a loop starts clean
    CHECK(restarted.previousValue == 0.0F);
    CHECK(restarted.result == 0);
    CHECK(game::DemoGlow(core::GlowRole::DemoProgress, 1400).result == 1);
}

TEST_CASE("a role without a demo drive has no glow") {
    CHECK(game::DemoGlow(core::GlowRole::Hover, 10).role == core::GlowRole::None);
}

TEST_CASE("glows leave the state hash alone") {
    game::Arena published(test::TestManifest());
    game::Arena plain(test::TestManifest());
    std::vector<core::Event> events;
    for (const u64 tick : { BURST_TICK, BURST_TICK + 1 }) {
        published.Tick(At(tick), {}, events);
        plain.Tick(At(tick), {}, events);
        if (tick == BURST_TICK) {
            static_cast<void>(Published(published)); // between ticks, so a publish that changed state would show
        }
    }
    CHECK(published.StateHash() == plain.StateHash());
}

} // namespace
