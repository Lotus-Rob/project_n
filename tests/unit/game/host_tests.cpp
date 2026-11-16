#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/draw_list.hpp"
#include "core/instances.hpp"
#include "core/jobs.hpp"
#include "core/seal.hpp"
#include "core/state.hpp"
#include "core/view_limits.hpp"
#include "data/effects.hpp"
#include "data/game_data.hpp"
#include "game/host.hpp"
#include "game/npcs.hpp"
#include "game/scenes/world.hpp"
#include "platform/paths.hpp"
#include "support/manifest.hpp"
#include "support/saves.hpp"
#include "support/seals.hpp"
#include "support/world.hpp"

#include <doctest/doctest.h>

namespace {

using core::atlas::SpriteId;

// Ticks `host`, which the tests below that look at no events do.
void Tick(game::Host& host, const core::TickCounters& counters, const core::TickInput& input) {
    std::vector<core::Event> events;
    host.Tick(counters, input, events);
}

// What `host` publishes culled to `view`, or with none sent yet.
core::Snapshot Published(game::Host& host, const std::optional<core::Rect>& view = std::nullopt) {
    core::Jobs jobs{ 1 };
    core::Snapshot snapshot;
    host.Publish(snapshot, jobs, view);
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
    game::Host host(test::TestManifest());
    const core::Snapshot snapshot = Published(host);
    REQUIRE_FALSE(snapshot.instances.empty());
    CHECK(Draws(snapshot.instances.front(), SpriteId::Ground));
}

TEST_CASE("shadows are drawn before what casts them") {
    game::Host host(test::TestManifest());
    const core::Snapshot snapshot = Published(host);
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
    game::Host host(test::TestManifest());
    const core::Snapshot before = Published(host);
    Tick(host, { .wall = 1, .simulation = 1, .state = 1 }, {});
    const core::Snapshot after = Published(host);
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
    game::Host host(test::TestManifest());
    Tick(host, { .wall = 1, .simulation = 1, .state = 1 }, {});
    const u32 before = RotationOf(Published(host), SpriteId::CrystalIce);
    Tick(host, { .wall = 2, .simulation = 2, .state = 2 }, {});
    const u32 after = RotationOf(Published(host), SpriteId::CrystalIce);
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
    game::Host host(test::TestManifest());
    REQUIRE(host.Player() != core::StableId{});
    const core::Snapshot before = Published(host);
    REQUIRE(before.follow.has_value());
    Tick(host, { .wall = 1, .simulation = 1, .state = 1 }, { .actions = {}, .held = HELD_RIGHT });
    const core::Snapshot after = Published(host);
    REQUIRE(after.follow.has_value());
    const core::Follow from = before.follow.value_or(core::Follow{});
    const core::Follow onto = after.follow.value_or(core::Follow{});
    CHECK(onto.previous.x == from.current.x);
    CHECK(onto.current.x > from.current.x);
    CHECK(onto.current.y == from.current.y);
    CHECK(PositionOf(after, host.Player()).x - PositionOf(before, host.Player()).x == onto.current.x - from.current.x);
}

TEST_CASE("the cultivator walks at the game data's speed") {
    constexpr f32 SPEED = 7.0F;
    core::GameData data = game::BuiltInData();
    data.cultivator.walkSpeed = SPEED;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, data);
    const core::Vec2 before = PositionOf(Published(host), host.Player());
    Tick(host, { .wall = 1, .simulation = 1, .state = 1 }, { .actions = {}, .held = HELD_RIGHT });
    const core::Vec2 after = PositionOf(Published(host), host.Player());
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
    game::Host host(test::TestManifest(), game::ARENA_SCENE, data);
    for (u64 tick = 1; tick <= ticks; ++tick) {
        Tick(host, { .wall = tick, .simulation = tick, .state = tick }, { .actions = {}, .held = held });
    }
    f32 left = std::numeric_limits<f32>::max();
    for (const core::Instance& instance : Published(host).instances) {
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
    game::Host host(test::TestManifest());
    core::GameData data = game::BuiltInData();
    data.fox.sprite = SpriteId::FoxYellow;
    data.fox.orbitRadius *= WIDER;
    const u64 before = host.DataHash();
    const f32 builtInSpread = Spread(Published(host), SpriteId::Fox);
    host.SetData(data);
    CHECK(host.DataHash() == data.Hash());
    CHECK(host.DataHash() != before);
    Tick(host, { .wall = 1, .simulation = 1, .state = 1 }, {});
    const core::Snapshot snapshot = Published(host);
    CHECK(std::ranges::none_of(snapshot.instances, [](const auto& instance) { return Draws(instance, SpriteId::Fox); }));
    CHECK(Spread(snapshot, SpriteId::FoxYellow) > builtInSpread * CLEARLY_WIDER);
}

TEST_CASE("standing still, the cultivator draws still") {
    game::Host host(test::TestManifest());
    Tick(host, { .wall = 1, .simulation = 1, .state = 1 }, { .actions = {}, .held = HELD_RIGHT });
    Tick(host, { .wall = 2, .simulation = 2, .state = 2 }, {});
    const core::Snapshot snapshot = Published(host);
    REQUIRE(snapshot.follow.has_value());
    const core::Follow follow = snapshot.follow.value_or(core::Follow{});
    CHECK(follow.previous.x == follow.current.x);
}

TEST_CASE("each entity's instance carries its ID; the ground and shadows carry none") {
    game::Host host(test::TestManifest());
    const core::Snapshot snapshot = Published(host);
    REQUIRE(snapshot.ids.size() == snapshot.instances.size());
    CHECK(snapshot.ids.front() == core::StableId{});
    CHECK(std::ranges::contains(snapshot.ids, host.Player()));
}

TEST_CASE("the snapshot names the selected entity's instance; selecting nothing clears it") {
    game::Host host(test::TestManifest());
    const std::array<core::Action, 1> select{ { { .kind = core::ActionKind::Select, .target = host.Player() } } };
    Tick(host, { .wall = 1, .simulation = 1, .state = 1 }, { .actions = select, .held = {} });
    CHECK(host.Selected() == host.Player());
    const core::Snapshot snapshot = Published(host);
    const auto drawn = std::ranges::find(snapshot.ids, host.Player());
    REQUIRE(drawn != snapshot.ids.end());
    CHECK(snapshot.selected == std::optional<u32>{ static_cast<u32>(drawn - snapshot.ids.begin()) });

    const std::array<core::Action, 1> none{ { { .kind = core::ActionKind::Select } } };
    Tick(host, { .wall = 2, .simulation = 2, .state = 2 }, { .actions = none, .held = {} });
    CHECK(host.Selected() == core::StableId{});
    CHECK_FALSE(Published(host).selected.has_value());
}

TEST_CASE("a cast outside a combat scene is ignored") {
    constexpr core::TickCounters COUNTERS{ .wall = 1, .simulation = 1, .state = 1 };
    const std::array<core::Action, 1> cast{
        { { .kind = core::ActionKind::Cast, .slot = core::Slot::Generator, .point = { .x = 50.0F, .y = 60.0F } } }
    };
    for (const core::SceneKey scene : { game::STRESS_SCENE, game::PAINTED_SCENE, game::CELLS_SCENE, game::WORLD_SCENE }) {
        game::Host casting(test::TestManifest(), scene, test::ShippedData());
        game::Host idle(test::TestManifest(), scene, test::ShippedData());
        std::vector<core::Event> events;
        casting.Tick(COUNTERS, { .actions = cast, .held = {} }, events);
        Tick(idle, COUNTERS, { .actions = {}, .held = {} });
        CHECK(events.empty());
        CHECK(casting.StateHash() == idle.StateHash());
    }
}

TEST_CASE("the arena is a combat scene and the others aren't") {
    for (const core::SceneKey scene :
         { game::ARENA_SCENE, game::STRESS_SCENE, game::PAINTED_SCENE, game::CELLS_SCENE, game::WORLD_SCENE }) {
        game::Host host(test::TestManifest(), scene, test::ShippedData());
        const bool combat = scene == game::ARENA_SCENE;
        CHECK(host.IsCombatScene() == combat);
        CHECK(Published(host).combat == combat);
    }
}

TEST_CASE("a stale ID selects nothing") {
    constexpr core::StableId NEVER_HANDED_OUT{ 1'000'000 };
    game::Host host(test::TestManifest());
    const std::array<core::Action, 1> select{ { { .kind = core::ActionKind::Select, .target = NEVER_HANDED_OUT } } };
    Tick(host, { .wall = 1, .simulation = 1, .state = 1 }, { .actions = select, .held = {} });
    CHECK(host.Selected() == core::StableId{});
}

TEST_CASE("the still scenes have no player and follow nothing") {
    game::Host host(test::TestManifest(), game::PAINTED_SCENE);
    CHECK(host.Player() == core::StableId{});
    CHECK_FALSE(Published(host).follow.has_value());
}

TEST_CASE("the snapshot names the camera") {
    game::Host host(test::TestManifest());
    game::Host painted(test::TestManifest(), game::PAINTED_SCENE);
    CHECK(Published(host).camera == core::CameraKind::Follow);
    CHECK(Published(painted).camera == core::CameraKind::Pan);
}

// `area` grown by `margin` on every side.
core::Rect Grown(const core::Rect& area, core::Vec2 margin) {
    return { .min = area.min - margin, .max = area.max + margin };
}

// What a publish culled to round the player before the window's view: the fallback still covers it, so the reference
// pictures and the headless benchmark stay as they were.
constexpr core::Vec2 FOLLOW_HALF_VIEW{ .x = 7'680.0F, .y = 4'320.0F };
static_assert(
    std::bit_cast<u64>(core::WIDEST_HALF_VIEW) == std::bit_cast<u64>(FOLLOW_HALF_VIEW),
    "the widest view is what culling covered"
);

// Whether `point` lies within `area`, its edges included.
bool Within(core::Vec2 point, const core::Rect& area) {
    return point.x >= area.min.x && point.x <= area.max.x && point.y >= area.min.y && point.y <= area.max.y;
}

// The view a publish culls to with none sent: the widest a window shows round `centre`.
core::Rect WidestRound(core::Vec2 centre) {
    return Grown({ .min = centre, .max = centre }, core::WIDEST_HALF_VIEW);
}

constexpr core::Vec2 DRAW_MARGIN{ .x = core::DrawList::CULL_MARGIN, .y = core::DrawList::CULL_MARGIN };

TEST_CASE("a publish with a view holds only what lies within it") {
    constexpr core::Vec2 HALF_VIEW{ .x = 200.0F, .y = 150.0F }; // a 400 by 300 view round the cultivator
    game::Host host(test::TestManifest());
    const core::Snapshot everything = Published(host);
    const core::Vec2 cultivator = PositionOf(everything, host.Player());
    const core::Rect view = Grown({ .min = cultivator, .max = cultivator }, HALF_VIEW);
    const core::Snapshot culled = Published(host, view);
    REQUIRE(culled.instances.size() > 1);
    CHECK(Draws(culled.instances.front(), SpriteId::Ground)); // drawn whole: larger than the margin allows
    const bool allWithin = std::ranges::all_of(std::span{ culled.instances }.subspan(1), [&](const core::Instance& instance) {
        return Within(instance.current, Grown(view, DRAW_MARGIN));
    });
    CHECK(allWithin);
    // And everything within it is kept: as many as lie there in the publish with no view, which holds the whole arena.
    REQUIRE(Draws(everything.instances.front(), SpriteId::Ground));
    const auto inside = static_cast<std::size_t>(
        std::ranges::count_if(std::span{ everything.instances }.subspan(1), [&](const core::Instance& instance) {
            return Within(instance.current, Grown(view, DRAW_MARGIN));
        })
    );
    CHECK(culled.instances.size() == inside + 1);
    CHECK(std::ranges::contains(culled.ids, host.Player()));
    // A creature published with no view, outside this one and its margin, is absent.
    std::optional<core::StableId> outside;
    for (std::size_t i = 0; i < everything.ids.size() && !outside; ++i) {
        if (everything.ids[i] != core::StableId{} && !Within(everything.instances[i].current, Grown(view, DRAW_MARGIN))) {
            outside = everything.ids[i];
        }
    }
    REQUIRE(outside.has_value());
    CHECK_FALSE(std::ranges::contains(culled.ids, outside.value_or(core::StableId{})));
}

TEST_CASE("with no view, a publish culls to the widest view round the followed cultivator") {
    core::GameData data = game::BuiltInData();
    // One step takes the cultivator so far right that the widest view's left edge, margin and all, crosses the arena.
    data.cultivator.walkSpeed = core::WIDEST_HALF_VIEW.x + core::DrawList::CULL_MARGIN;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, data);
    Tick(host, { .wall = 1, .simulation = 1, .state = 1 }, { .actions = {}, .held = HELD_RIGHT });
    const core::Snapshot fallback = Published(host);
    REQUIRE(fallback.follow.has_value());
    const core::Rect widest = WidestRound(fallback.follow.value_or(core::Follow{}).current);
    // The same bytes as a publish sent that view: the same instances, the world's depth measured from the same top.
    const core::Snapshot sent = Published(host, widest);
    CHECK(std::ranges::equal(std::as_bytes(std::span{ fallback.instances }), std::as_bytes(std::span{ sent.instances })));
    // Some of the arena lies within it and some past it.
    constexpr core::Rect ALL{ .min = { .x = -1.0e6F, .y = -1.0e6F }, .max = { .x = 1.0e6F, .y = 1.0e6F } };
    const core::Snapshot everything = Published(host, ALL);
    REQUIRE(Draws(everything.instances.front(), SpriteId::Ground)); // drawn whole either way, so left out of the count
    const auto kept = static_cast<std::size_t>(
        std::ranges::count_if(std::span{ everything.instances }.subspan(1), [&](const core::Instance& instance) {
            return Within(instance.current, Grown(widest, DRAW_MARGIN));
        })
    );
    CHECK(kept > 0);
    CHECK(kept < everything.instances.size() - 1);
    CHECK(fallback.instances.size() == kept + 1);
}

TEST_CASE("with no view and no player, a publish culls to the widest view round the scene's view centre") {
    constexpr core::Vec2 CENTRE{ .x = 960.0F, .y = 540.0F }; // the middle of the painted scene's view, the ground
    game::Host host(test::TestManifest(), game::PAINTED_SCENE);
    const core::Snapshot fallback = Published(host);
    const core::Snapshot sent = Published(host, WidestRound(CENTRE));
    REQUIRE(fallback.instances.size() > 1);
    CHECK(std::ranges::equal(std::as_bytes(std::span{ fallback.instances }), std::as_bytes(std::span{ sent.instances })));
    // A view far from the scene's culls all but the ground: the fallback isn't simply "keep everything".
    constexpr core::Rect FAR_AWAY{ .min = { .x = 50'000.0F, .y = 50'000.0F }, .max = { .x = 51'000.0F, .y = 51'000.0F } };
    CHECK(Published(host, FAR_AWAY).instances.size() == 1);
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
core::Snapshot MovedHost() {
    constexpr u64 TICKS = 90;
    game::Host host(test::TestManifest());
    for (u64 tick = 1; tick <= TICKS; ++tick) {
        Tick(host, { .wall = tick, .simulation = tick, .state = tick }, {});
    }
    return Published(host);
}

TEST_CASE("the arena publishes the ground's normal batch, the world's, then an additive one that ends the snapshot") {
    const core::Snapshot snapshot = MovedHost();
    REQUIRE(snapshot.batches.size() == 3);
    CHECK(snapshot.groundBatches == 1);
    CHECK(snapshot.batches[0].pipeline == core::Pipeline::Normal);
    CHECK(snapshot.batches[1].pipeline == core::Pipeline::Normal);
    CHECK(snapshot.batches[2].pipeline == core::Pipeline::Additive);
    CHECK(snapshot.batches[2].first + snapshot.batches[2].count == snapshot.instances.size());
}

TEST_CASE("the world draws in foot-y order, after the ground and its decals") {
    const core::Snapshot snapshot = MovedHost();
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
    const core::Snapshot snapshot = MovedHost();
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
    game::Host host(test::TestManifest(), game::STRESS_SCENE);
    CHECK(Published(host).instances.size() >= INSTANCES);
}

TEST_CASE("the stress arena's 10,000 bullets draw last, in an additive batch") {
    constexpr u32 BULLETS = 10'000;
    game::Host host(test::TestManifest(), game::STRESS_SCENE);
    const core::Snapshot snapshot = Published(host);
    REQUIRE_FALSE(snapshot.batches.empty());
    const core::Batch& effects = snapshot.batches.back();
    CHECK(effects.pipeline == core::Pipeline::Additive);
    CHECK(effects.count >= BULLETS);
    CHECK(effects.first + effects.count == snapshot.instances.size());
}

TEST_CASE("the painted scene's glows draw last, in an additive batch") {
    constexpr u32 GLOWS = 5;
    game::Host host(test::TestManifest(), game::PAINTED_SCENE);
    const core::Snapshot snapshot = Published(host);
    REQUIRE_FALSE(snapshot.batches.empty());
    CHECK(snapshot.batches.back().pipeline == core::Pipeline::Additive);
    CHECK(snapshot.batches.back().count == GLOWS);
}

TEST_CASE("the painted scene holds still") {
    constexpr u64 TICK = 7;
    game::Host host(test::TestManifest(), game::PAINTED_SCENE);
    const core::Snapshot before = Published(host);
    Tick(host, { .wall = TICK, .simulation = TICK, .state = TICK }, {});
    const core::Snapshot after = Published(host);
    CHECK(std::ranges::equal(std::as_bytes(std::span{ before.instances }), std::as_bytes(std::span{ after.instances })));
}

TEST_CASE("the cell grid publishes every cell, with none culled at its far corners") {
    constexpr std::size_t CELLS = 640;
    game::Host host(test::TestManifest(), game::CELLS_SCENE);
    const core::Snapshot snapshot = Published(host);
    CHECK(snapshot.instances.size() == CELLS + 1); // and the ground, hidden beneath them
}

// Whether an instance draws one of `terrain`, the instances of the world's terrain sprites, by region and page as Draws().
bool DrawsTerrain(const core::Instance& instance, std::span<const core::Instance> terrain) {
    constexpr u32 PAGE_MASK = 0xFFFFU;
    return std::ranges::any_of(terrain, [&](const core::Instance& tile) {
        return instance.regionMin == tile.regionMin && (instance.layerFlags & PAGE_MASK) == (tile.layerFlags & PAGE_MASK);
    });
}

TEST_CASE("a world publish holds no tiles in view and no painted ground: the renderer draws the map") {
    // A screen at zoom 1, off the tile grid on both axes: eight columns and five rows of tiles, which the map once published.
    constexpr core::Rect VIEW{ .min = { .x = 10'000.0F, .y = 5'000.0F }, .max = { .x = 11'920.0F, .y = 6'080.0F } };
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    game::Host host(manifest, game::WORLD_SCENE, test::ShippedData());
    const core::Snapshot snapshot = Published(host, VIEW);
    // The terrain sprites, which art/sprites.toml lists together.
    std::vector<core::Instance> terrain;
    for (auto id = std::to_underlying(SpriteId::TerrainMeadow0); id <= std::to_underlying(SpriteId::TerrainWater1); ++id) {
        terrain.push_back(core::MakeInstance(*manifest, { .sprite = static_cast<SpriteId>(id) }));
    }
    CHECK(snapshot.worldMap);
    CHECK(std::ranges::none_of(snapshot.instances, [&](const core::Instance& instance) {
        return DrawsTerrain(instance, terrain) || Draws(instance, SpriteId::Ground);
    }));
}

// The world's walkers (the entities on an orbit) and where each stands, in ID order.
std::vector<std::pair<core::StableId, game::Body>> Walkers(const game::Host& host) {
    std::vector<std::pair<core::StableId, game::Body>> walkers;
    const core::Registry& entities = host.State()[game::ACTIVE].Entities();
    for (auto [entity, body, orbit, stableId] :
         entities.view<const game::Body, const game::Orbit, const core::StableId>().each()) {
        walkers.emplace_back(stableId, body);
    }
    std::ranges::sort(walkers, {}, &std::pair<core::StableId, game::Body>::first);
    return walkers;
}

TEST_CASE("a walker's foot is the same for the same tick, built twice") {
    game::Host first(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    game::Host second(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    std::vector<std::vector<std::pair<core::StableId, game::Body>>> atEachTick;
    // The first tick, and one long after.
    for (const u64 tick : { 1ULL, 500ULL }) {
        CAPTURE(tick);
        Tick(first, { .wall = tick, .simulation = tick, .state = tick }, {});
        Tick(second, { .wall = tick, .simulation = tick, .state = tick }, {});
        const std::vector<std::pair<core::StableId, game::Body>> mine = Walkers(first);
        const std::vector<std::pair<core::StableId, game::Body>> theirs = Walkers(second);
        REQUIRE_FALSE(mine.empty());
        const bool same = std::ranges::equal(mine, theirs, [](const auto& lhs, const auto& rhs) {
            return lhs.first == rhs.first &&
                   std::bit_cast<u64>(lhs.second.previous) == std::bit_cast<u64>(rhs.second.previous) &&
                   std::bit_cast<u64>(lhs.second.current) == std::bit_cast<u64>(rhs.second.current);
        });
        CHECK(same);
        atEachTick.push_back(mine);
    }
    // And they walk: by tick 500 every walker stands somewhere else than at tick 1 (the fastest is short of a lap).
    REQUIRE(atEachTick.size() == 2);
    REQUIRE(atEachTick[0].size() == atEachTick[1].size());
    const bool allMoved = std::ranges::equal(atEachTick[0], atEachTick[1], [](const auto& early, const auto& late) {
        return early.first == late.first && std::bit_cast<u64>(early.second.current) != std::bit_cast<u64>(late.second.current);
    });
    CHECK(allMoved);
}

TEST_CASE("the world's busiest region holds thousands of walkers at the widest view") {
    constexpr core::Vec2 WIDEST_HALF_SCREEN{ .x = 3'840.0F, .y = 2'160.0F }; // a 16:9 window at zoom 0.25
    constexpr std::size_t THOUSANDS = 2'000;
    const game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    const core::Rect window = Grown({ .min = game::WORLD_BUSIEST, .max = game::WORLD_BUSIEST }, WIDEST_HALF_SCREEN);
    const std::vector<std::pair<core::StableId, game::Body>> walkers = Walkers(host);
    const auto inside = static_cast<std::size_t>(std::ranges::count_if(walkers, [&](const auto& walker) {
        return Within(walker.second.current, window);
    }));
    CHECK(inside >= THOUSANDS);
}

// How many of `walkers` stand within the screen (1,920 by 1,080 at zoom 1) centred on `centre`.
std::size_t OnScreenAt(const std::vector<std::pair<core::StableId, game::Body>>& walkers, core::Vec2 centre) {
    constexpr core::Vec2 HALF_SCREEN{ .x = 960.0F, .y = 540.0F };
    const core::Rect screen = Grown({ .min = centre, .max = centre }, HALF_SCREEN);
    return static_cast<std::size_t>(std::ranges::count_if(walkers, [&](const auto& walker) {
        return Within(walker.second.current, screen);
    }));
}

TEST_CASE("the world's cultivator starts in quiet country") {
    constexpr std::size_t QUIET_SCREEN = 8;
    constexpr std::size_t WIDEST_SCREENS = 64; // the widest view, WIDEST_HALF_VIEW each way, is 8 by 8 screens
    const game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    const std::vector<std::pair<core::StableId, game::Body>> walkers = Walkers(host);
    const core::Domain& active = host.State()[game::ACTIVE];
    const core::Vec2 home = active.Entities().get<game::Body>(active.Find(host.Player())).current;
    CHECK(OnScreenAt(walkers, home) <= QUIET_SCREEN);
    // Only a weak fallback: spread and gathered walkers look alike, so this bounds the widest view to the countryside's
    // count for each of its screens. The real guarantee that no gathering reaches it is scenes_tests' check on the shipped map.
    const core::Rect widest = Grown({ .min = home, .max = home }, core::WIDEST_HALF_VIEW);
    const auto inWidest = static_cast<std::size_t>(std::ranges::count_if(walkers, [&](const auto& walker) {
        return Within(walker.second.current, widest);
    }));
    CHECK(inWidest <= QUIET_SCREEN * WIDEST_SCREENS);
}

// The sample counts sit close to the bounds (the busiest window holds 2,051 where the widest-view test wants 2,000; the
// densest screen of the port's crowd, found by a sweep every 60 units, holds 206 where PORT_PILE allows 210; the densest
// of the other samples is the sect's 93 against 150, the oasis's 90 and the marsh town's 77): they depend on the walker
// draws, their purposes and the orbit ranges. A change there means retuning the density in world.cpp, never loosening a
// test.
TEST_CASE("no screen of the world is a pile of walkers") {
    constexpr std::size_t PILE = 150;
    // The port's crowd has a limit of its own: its widest view (the busiest region, kept at 2,000 walkers or more so
    // `measure` stays comparable with 5a) is about 35 to 40 percent sea, where no walker stands, so the land holds
    // more a screen than PILE allows. The user's decision, 2026-11-15.
    constexpr std::size_t PORT_PILE = 210;
    constexpr std::size_t COUNTRYSIDE = 8;
    const game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    const std::vector<std::pair<core::StableId, game::Body>> walkers = Walkers(host);
    const core::Vec2 busiest = game::WORLD_BUSIEST;
    struct Sample {
        std::string_view name;
        core::Vec2 centre;
        std::size_t most;
    };
    const std::array<Sample, 20> samples{ {
        { .name = "busiest's point", .centre = busiest, .most = PORT_PILE },
        { .name = "busiest, half out east", .centre = busiest + core::Vec2{ .x = 2'000.0F, .y = 0.0F }, .most = PILE },
        { .name = "map's north-east corner", .centre = { .x = 37'440.0F, .y = 540.0F }, .most = COUNTRYSIDE },
        { .name = "busiest, near its north-west corner",
          .centre = busiest + core::Vec2{ .x = -3'800.0F, .y = -2'200.0F },
          .most = PILE },
        { .name = "busiest, near its south-east corner",
          .centre = busiest + core::Vec2{ .x = 3'800.0F, .y = 2'200.0F },
          .most = PILE },
        { .name = "busiest, off its west edge", .centre = busiest + core::Vec2{ .x = -5'000.0F, .y = 0.0F }, .most = PILE },
        { .name = "sect's point", .centre = { .x = 6'272.0F, .y = 7'552.0F }, .most = PILE },
        { .name = "sect, half out", .centre = { .x = 7'472.0F, .y = 8'052.0F }, .most = PILE },
        { .name = "sect, near its edge", .centre = { .x = 4'272.0F, .y = 6'552.0F }, .most = PILE },
        { .name = "oasis's point", .centre = { .x = 15'488.0F, .y = 3'456.0F }, .most = PILE },
        { .name = "map's south-west corner", .centre = { .x = 960.0F, .y = 21'060.0F }, .most = COUNTRYSIDE },
        { .name = "oasis, near its edge", .centre = { .x = 14'328.0F, .y = 4'056.0F }, .most = PILE },
        { .name = "marsh town's point", .centre = { .x = 16'768.0F, .y = 18'304.0F }, .most = PILE },
        { .name = "busiest, the densest screen a sweep found", .centre = { .x = 31'240.0F, .y = 9'580.0F }, .most = PORT_PILE },
        { .name = "marsh town, near its edge", .centre = { .x = 17'708.0F, .y = 17'644.0F }, .most = PILE },
        { .name = "the start", .centre = (game::WORLD_MAP.min + game::WORLD_MAP.max) * 0.5F, .most = COUNTRYSIDE },
        { .name = "countryside, north", .centre = { .x = 19'200.0F, .y = 3'000.0F }, .most = COUNTRYSIDE },
        { .name = "countryside, west", .centre = { .x = 3'000.0F, .y = 11'000.0F }, .most = COUNTRYSIDE },
        { .name = "map's north-west corner", .centre = { .x = 960.0F, .y = 540.0F }, .most = COUNTRYSIDE },
        { .name = "map's south-east corner", .centre = { .x = 37'440.0F, .y = 21'060.0F }, .most = COUNTRYSIDE },
    } };
    for (const Sample& sample : samples) {
        CAPTURE(sample.name);
        CHECK(OnScreenAt(walkers, sample.centre) <= sample.most);
    }
}

TEST_CASE("the stress arena publishes the same bytes with one worker and with many") {
    constexpr u64 TICK = 7;
    constexpr u32 MANY_WORKERS = 7;
    std::vector<std::vector<core::Instance>> published;
    for (const u32 workers : { 1U, MANY_WORKERS }) {
        game::Host host(test::TestManifest(), game::STRESS_SCENE);
        Tick(host, { .wall = TICK, .simulation = TICK, .state = TICK }, {});
        core::Jobs jobs{ workers };
        core::Snapshot snapshot;
        host.Publish(snapshot, jobs, std::nullopt);
        published.push_back(std::move(snapshot.instances));
    }
    REQUIRE_FALSE(published[0].empty());
    CHECK(std::ranges::equal(std::as_bytes(std::span{ published[0] }), std::as_bytes(std::span{ published[1] })));
}

TEST_CASE("two hosts ticked alike publish the same bytes") {
    constexpr u64 TICKS = 120;
    game::Host first(test::TestManifest());
    game::Host second(test::TestManifest());
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
    constexpr f64 VIEW_TILT = 0.6;  // game/components.hpp's: the circle is drawn flattened in y
    constexpr f64 TOLERANCE = 0.01; // world units
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
    game::Host host(test::TestManifest(), game::STRESS_SCENE);
    core::Jobs jobs{ 1 };
    const core::State& state = host.State();
    CHECK(state[game::WORLD].Entities().storage<game::Npc>()->size() == WORLD_NPCS);
    CHECK(state[game::ACTIVE].Entities().storage<game::Npc>()->size() == ACTIVE_NPCS);

    constexpr u64 MONTHS = 12; // loneliness outweighs ambition after several
    for (u64 month = 1; month <= MONTHS; ++month) {
        host.AdvanceCalendar({ .wall = month, .simulation = 0, .state = month }, jobs, {});
    }
    CHECK_FALSE(host.NpcRelationships().All().empty());
    CHECK(state[game::ACTIVE].Entities().storage<game::Npc>()->size() == ACTIVE_NPCS);
}

TEST_CASE("a host needs the manifest's scene named after the scene it runs") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::SceneId arena = game::Host(manifest).Scene();
    const core::SceneId painted = game::Host(manifest, game::PAINTED_SCENE).Scene();
    CHECK(arena == manifest->SceneNamed("arena"));
    CHECK(painted == manifest->SceneNamed("painted"));
}

TEST_CASE("each scene is found by its name") {
    for (const core::SceneKey scene :
         { game::ARENA_SCENE, game::STRESS_SCENE, game::PAINTED_SCENE, game::CELLS_SCENE, game::WORLD_SCENE }) {
        const std::optional<core::SceneKey> found = game::SceneNamed(game::SceneName(scene));
        CHECK(found == scene);
    }
    const std::optional<core::SceneKey> unknown = game::SceneNamed("nowhere");
    CHECK_FALSE(unknown.has_value());
}

TEST_CASE("F2's CycleScene visits every scene in order and back, clearing the selection") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    game::Host host(manifest, game::ARENA_SCENE, test::ShippedData());
    const std::array<core::Action, 1> select{ { { .kind = core::ActionKind::Select, .target = host.Player() } } };
    Tick(host, { .wall = 1, .simulation = 1, .state = 1 }, { .actions = select, .held = {} });
    REQUIRE(host.Selected() == host.Player());
    const std::array<core::Action, 1> cycle{ { { .kind = core::ActionKind::CycleScene } } };
    u64 tick = 1;
    for (const std::string_view name : { "stress", "painted", "cells", "world", "arena" }) {
        ++tick;
        Tick(host, { .wall = tick, .simulation = tick, .state = tick }, { .actions = cycle, .held = {} });
        host.EnterScene(); // as the logic does after the tick
        const std::optional<core::SceneId> expected = manifest->SceneNamed(name);
        CHECK(host.Scene() == expected);
    }
    CHECK(host.Selected() == core::StableId{});
    CHECK(host.Player() != core::StableId{});
}

TEST_CASE("CycleScene asks for the next scene and EnterScene builds it") {
    constexpr core::TickCounters CAST_TICK{ .wall = 1, .simulation = 1, .state = 1 };
    constexpr core::TickCounters CHANGE_TICK{ .wall = 2, .simulation = 2, .state = 2 };
    constexpr core::Vec2 FAR_POINT{ .x = 1'500.0F, .y = 500.0F }; // a bolt's flight there outlasts a tick
    constexpr u64 BOLT_USE = game::CastUse(CAST_TICK.simulation, core::Slot::Generator);
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    game::Host host(manifest);
    const core::SceneFacts arenaFacts{ .key = core::SceneKey{ 0 }, .sprites = host.Scene(), .combat = true };
    REQUIRE(host.Current() == arenaFacts);
    const std::array<core::Action, 1> cast{
        { { .kind = core::ActionKind::Cast, .slot = core::Slot::Generator, .point = FAR_POINT } }
    };
    Tick(host, CAST_TICK, { .actions = cast, .held = {} });
    const std::array<core::Action, 1> cycle{ { { .kind = core::ActionKind::CycleScene } } };
    std::vector<core::Event> events;
    host.Tick(CHANGE_TICK, { .actions = cycle, .held = {} }, events);
    const std::optional<core::SceneId> stressSprites = manifest->SceneNamed("stress");
    REQUIRE(stressSprites.has_value());
    const core::SceneFacts stress{ .key = core::SceneKey{ 1 },
                                   .sprites = stressSprites.value_or(core::SceneId{}),
                                   .combat = false };
    const std::optional<core::SceneFacts> requested = host.Requested();
    CHECK(requested == stress);
    CHECK(host.Current() == arenaFacts);
    REQUIRE(events.size() == 1); // the bolt's use, still open, ends on the tick that asks
    CHECK(events.front().kind == core::EventKind::UseEnded);
    CHECK(events.front().use == BOLT_USE);
    host.EnterScene();
    CHECK(host.Current() == stress);
    CHECK(host.Scene() == stress.sprites);
    CHECK_FALSE(host.Requested().has_value());
}

TEST_CASE("a load held over a scene asked for wins, and EnterScene forgets both") {
    constexpr core::TickCounters CHANGE_TICK{ .wall = 1, .simulation = 1, .state = 1 };
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    game::Host saved(manifest, game::CELLS_SCENE);
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    saved.Save(writer);

    game::Host host(manifest);
    const std::array<core::Action, 1> cycle{ { { .kind = core::ActionKind::CycleScene } } };
    Tick(host, CHANGE_TICK, { .actions = cycle, .held = {} }); // asks for the stress arena
    core::Reader reader{ bytes };
    REQUIRE(host.Load(reader, CHANGE_TICK.simulation));
    const std::optional<core::SceneFacts> requested = host.Requested();
    CHECK(requested == saved.Current());
    host.EnterScene();
    CHECK(host.Current() == saved.Current());
    CHECK(host.StateHash() == saved.StateHash());
    CHECK_FALSE(host.Requested().has_value());
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

TEST_CASE("the shipped manifest runs every scene with the shipped data") {
    const std::expected<void, core::Error> checked = game::CheckScenes(*test::TestManifest(), test::ShippedData());
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

// Ticks `host` from tick 1 to `last`, the first tick walking left, and returns every event of them.
std::vector<core::Event> TickAll(game::Host& host, u64 last) {
    std::vector<core::Event> events;
    for (u64 tick = 1; tick <= last; ++tick) {
        const core::HeldInput held = tick == 1 ? HELD_LEFT : core::HeldInput{};
        host.Tick(At(tick), { .actions = {}, .held = held }, events);
    }
    return events;
}

// The same, returning the events of the last tick alone.
std::vector<core::Event> TickTo(game::Host& host, u64 last) {
    std::vector<core::Event> events = TickAll(host, last - 1);
    events.clear();
    host.Tick(At(last), {}, events);
    return events;
}

core::Vec2 FootOf(const game::Host& host, core::StableId entity) {
    const core::Domain& active = host.State()[game::ACTIVE];
    return active.Entities().get<game::Body>(active.Find(entity)).current;
}

std::vector<core::StableId> Foxes(const game::Host& host) {
    std::vector<core::StableId> foxes;
    const core::Registry& entities = host.State()[game::ACTIVE].Entities();
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
    game::Host host(test::TestManifest(), game::ARENA_SCENE, NearCrystalData());
    CHECK(TickAll(host, AURA_TICK - 1).empty());
    std::vector<core::Event> events;
    host.Tick(At(AURA_TICK), {}, events);
    REQUIRE_FALSE(events.empty());
    CHECK(events.back().kind == core::EventKind::UseEnded);
    CHECK(events.back().use == core::AbilityUse(AURA_TICK, USE_AURA));
    std::size_t damaged = 0;
    for (const core::StableId fox : Foxes(host)) {
        const bool reached = InReach(FootOf(host, fox));
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
    CHECK(damaged < Foxes(host).size());
}

TEST_CASE("the aura misses a fox out of reach") {
    constexpr f32 FAR_ORBIT = 1'000.0F; // round the cultivator, 540 units from the crystal: no fox comes within reach
    core::GameData data = game::BuiltInData();
    data.fox.orbitRadius = FAR_ORBIT;
    data.fox.orbitWobble = 0.0F;
    game::Host host(test::TestManifest(), game::ARENA_SCENE, data);
    const std::vector<core::Event> events = TickTo(host, AURA_TICK);
    for (const core::StableId fox : Foxes(host)) {
        REQUIRE_FALSE(InReach(FootOf(host, fox)));
    }
    REQUIRE(events.size() == 1);
    CHECK(events.front().kind == core::EventKind::UseEnded);
}

TEST_CASE("the aura heals the player in reach") {
    game::Host host(test::TestManifest(), game::ARENA_SCENE, NearCrystalData());
    const std::vector<core::Event> events = TickTo(host, AURA_TICK);
    const auto heal = std::ranges::find(events, host.Player(), &core::Event::target);
    REQUIRE(heal != events.end());
    CHECK(heal->kind == core::EventKind::Hit);
    CHECK(heal->hit == core::HitKind::Healing);
    CHECK(heal->value >= AURA_HEALING_MIN);
    CHECK(heal->value <= AURA_HEALING_MAX);
}

TEST_CASE("the aura's hits are the same on a rerun") {
    game::Host first(test::TestManifest(), game::ARENA_SCENE, NearCrystalData());
    game::Host second(test::TestManifest(), game::ARENA_SCENE, NearCrystalData());
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
    game::Host host(test::TestManifest(), game::ARENA_SCENE, NearCrystalData());
    const std::vector<core::Event> events = TickTo(host, AURA_TICK);
    const std::vector<core::Event> hits = HitsOf(events);
    REQUIRE_FALSE(hits.empty());
    const core::Vec2 foot = FootOf(host, hits.front().target);
    CHECK(hits.front().position.x == doctest::Approx(foot.x));
    CHECK(hits.front().position.y < foot.y);
}

TEST_CASE("the debug burst leaves the state hash alone") {
    game::Host burst(test::TestManifest());
    game::Host plain(test::TestManifest());
    const std::array<core::Action, 1> actions{ { { .kind = core::ActionKind::DebugBurst } } };
    std::vector<core::Event> events;
    burst.Tick(At(BURST_TICK), { .actions = actions, .held = {} }, events);
    plain.Tick(At(BURST_TICK), {}, events);
    CHECK(burst.StateHash() == plain.StateHash());
}

TEST_CASE("the debug burst hits every fox with all four kinds") {
    constexpr std::size_t HITS = 4'000;
    constexpr std::size_t KINDS = 4;
    game::Host host(test::TestManifest());
    const std::array<core::Action, 1> actions{ { { .kind = core::ActionKind::DebugBurst } } };
    std::vector<core::Event> events;
    host.Tick(At(BURST_TICK), { .actions = actions, .held = {} }, events);
    REQUIRE(events.size() == HITS + 1);
    CHECK(events.back().kind == core::EventKind::UseEnded);
    CHECK(events.back().use == core::AbilityUse(BURST_TICK, USE_DEBUG_BURST));
    const std::vector<core::StableId> foxes = Foxes(host);
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
    game::Host host(test::TestManifest(), game::PAINTED_SCENE);
    const std::array<core::Action, 1> actions{ { { .kind = core::ActionKind::DebugBurst } } };
    std::vector<core::Event> events;
    host.Tick(At(BURST_TICK), { .actions = actions, .held = {} }, events);
    REQUIRE(events.size() == 1);
    CHECK(events.front().kind == core::EventKind::UseEnded);
}

TEST_CASE("the arena labels the player and every fox, and captions the tablet") {
    constexpr std::size_t FOXES = 12;
    game::Host host(test::TestManifest());
    const core::Snapshot snapshot = Published(host);
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
    std::vector<core::StableId> expected = Foxes(host);
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
    game::Host host(test::TestManifest());
    const core::Snapshot snapshot = Published(host);
    REQUIRE_FALSE(snapshot.texts.empty());
    for (const core::TextItem& text : snapshot.texts) {
        CHECK(text.colour == core::TextItem{}.colour);
    }
}

TEST_CASE("the player's label stands a little over its head") {
    constexpr f32 CULTIVATOR_FOOT_Y = 122.0F; // assets/manifest.yaml
    game::Host host(test::TestManifest());
    const core::Snapshot snapshot = Published(host);
    const auto label = std::ranges::find(snapshot.texts, core::LabelPriority::Player, &core::TextItem::priority);
    REQUIRE(label != snapshot.texts.end());
    const core::Vec2 foot = FootOf(host, host.Player());
    CHECK(label->entity == host.Player());
    CHECK(label->string == core::StringId::CultivatorName);
    CHECK(label->nameKey == std::to_underlying(core::StringId::CultivatorName));
    CHECK(label->current.x == foot.x);
    CHECK(label->current.y == foot.y - CULTIVATOR_FOOT_Y);
    CHECK(label->height == CULTIVATOR_FOOT_Y);
    CHECK(label->offset.y < 0.0F);
}

TEST_CASE("the still scenes publish no texts") {
    game::Host host(test::TestManifest(), game::PAINTED_SCENE);
    CHECK(Published(host).texts.empty());
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
    game::Host host(test::TestManifest());
    const core::Snapshot snapshot = Published(host);
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

// The arena's showcase seals (the phase 6 chunk 4b spec, "The showcase"), in the order it publishes them.
constexpr std::array<core::SealKey, 7> SHOWCASE{ { core::SealKeyOf("showcase_grand"),
                                                   core::SealKeyOf("showcase_upright"),
                                                   core::SealKeyOf("showcase_overhead"),
                                                   core::SealKeyOf("showcase_tiers"),
                                                   core::SealKeyOf("showcase_flame"),
                                                   core::SealKeyOf("showcase_small_1"),
                                                   core::SealKeyOf("showcase_small_3") } };

// The keys of `snapshot`'s showcase seals, in its order: the seals test::RaisedSeals() leaves out.
std::vector<core::SealKey> ShowcaseKeys(const core::Snapshot& snapshot) {
    std::vector<core::SealKey> keys;
    keys.reserve(snapshot.seals.size());
    for (const core::Seal& seal : snapshot.seals) {
        if (seal.showcase) {
            keys.push_back(seal.key);
        }
    }
    return keys;
}

TEST_CASE("the arena publishes its showcase seals, unsaved") {
    constexpr u64 SAVED_AT = 30;
    const std::vector<core::SealKey> expected(SHOWCASE.begin(), SHOWCASE.end());
    game::Host host(test::TestManifest());
    for (u64 tick = 1; tick <= SAVED_AT; ++tick) {
        Tick(host, At(tick), {});
    }
    const core::Snapshot published = Published(host);
    const std::vector<core::SealKey> shown = ShowcaseKeys(published);
    CHECK(shown == expected);
    const std::vector<core::Seal> raised = test::RaisedSeals(published);
    CHECK(raised.empty());
    // Shown from the scene's start and never ending, of no element: scene content, not a seal raised in play.
    const bool lasting = std::ranges::all_of(published.seals, [](const core::Seal& seal) {
        return seal.startTick == 0 && seal.endTick == core::NEVER_ENDS && !seal.hasElement && seal.Valid();
    });
    CHECK(lasting);

    // Not state: publishing them leaves the save's bytes alone, and a load brings them back from the scene.
    std::vector<std::byte> before;
    core::Writer beforeWriter{ before };
    game::Host unpublished(test::TestManifest());
    for (u64 tick = 1; tick <= SAVED_AT; ++tick) {
        Tick(unpublished, At(tick), {});
    }
    unpublished.Save(beforeWriter);
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    host.Save(writer);
    CHECK(bytes == before);
    game::Host loaded(test::TestManifest(), game::CELLS_SCENE);
    const std::vector<core::SealKey> shownElsewhere = ShowcaseKeys(Published(loaded));
    CHECK(shownElsewhere.empty()); // the other scenes show none
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_AT));
    loaded.EnterScene(); // as the logic does at the swap
    const core::Snapshot reloaded = Published(loaded);
    const std::vector<core::SealKey> shownAgain = ShowcaseKeys(reloaded);
    CHECK(shownAgain == expected);
    const std::vector<core::Seal> raisedAgain = test::RaisedSeals(reloaded);
    CHECK(raisedAgain.empty());
}

TEST_CASE("glows leave the state hash alone") {
    game::Host published(test::TestManifest());
    game::Host plain(test::TestManifest());
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

// The portal's point in `scene`, which must have one.
core::Vec2 PortalOf(core::SceneKey scene) {
    game::Host host(test::TestManifest(), scene, test::ShippedData());
    const std::optional<core::Vec2> portal = host.Portal();
    REQUIRE(portal.has_value());
    return portal.value_or(core::Vec2{});
}

// Ticks `host` once at `tick` with `actions`, its cultivator walking from where it stands to `target`: held movement is at
// most 1 long, so the game data's walk speed becomes the distance.
void StepTo(
    game::Host& host,
    u64 tick,
    core::Vec2 target,
    std::vector<core::Event>& events,
    std::span<const core::Action> actions = {}
) {
    const core::Vec2 away = target - FootOf(host, host.Player());
    const f32 distance = std::hypot(away.x, away.y);
    core::GameData data = test::ShippedData();
    data.cultivator.walkSpeed = distance;
    host.SetData(data);
    host.Tick(At(tick), { .actions = actions, .held = { .move = away * (1.0F / distance) } }, events);
}

// Ticks `host` at ticks 1 and 2, its cultivator stepping to `step` from its portal's point straight in from three times as
// far out, so its path this tick crosses no more of the step area than `step` itself reaches.
void StepInTo(game::Host& host, core::Vec2 portal, core::Vec2 step, std::vector<core::Event>& events) {
    constexpr f32 FAR_OUT = 3.0F;
    StepTo(host, 1, portal + (step * FAR_OUT), events);
    REQUIRE_FALSE(host.Requested().has_value());
    StepTo(host, 2, portal + step, events);
}

// Each walkable scene and the one its portal leads to.
constexpr std::array<std::pair<core::SceneKey, core::SceneKey>, 3> PORTAL_LEADS{ {
    { game::ARENA_SCENE, game::STRESS_SCENE },
    { game::STRESS_SCENE, game::WORLD_SCENE },
    { game::WORLD_SCENE, game::ARENA_SCENE },
} };

TEST_CASE("stepping onto the portal asks for the next walkable scene") {
    for (const auto& [scene, next] : PORTAL_LEADS) {
        CAPTURE(game::SceneName(scene));
        game::Host host(test::TestManifest(), scene, test::ShippedData());
        std::vector<core::Event> events;
        StepTo(host, 1, PortalOf(scene), events);
        const std::optional<core::SceneFacts> requested = host.Requested();
        REQUIRE(requested.has_value());
        CHECK(requested.value_or(core::SceneFacts{}).key == next);
    }
    // Near the step area's edges, on the seal's inner rings: across, and up and down where the drawn seal is flatter.
    constexpr f32 ACROSS = 0.9F * game::PORTAL_RADIUS;
    constexpr f32 DEEP = 0.9F * game::PORTAL_RADIUS * game::PORTAL_DEPTH_RATIO;
    const core::Vec2 portal = PortalOf(game::ARENA_SCENE);
    for (const core::Vec2 step : { core::Vec2{ .x = ACROSS }, core::Vec2{ .y = DEEP }, core::Vec2{ .y = -DEEP } }) {
        CAPTURE(step.x);
        CAPTURE(step.y);
        game::Host host(test::TestManifest());
        std::vector<core::Event> events;
        StepInTo(host, portal, step, events);
        CHECK(host.Requested().has_value());
    }
}

TEST_CASE("a dash onto or across the portal asks for the next walkable scene") {
    constexpr u64 MOST_TICKS = 12; // longer than the dash lasts
    const core::Vec2 portal = PortalOf(game::ARENA_SCENE);
    // Ending on it from the right; and straight down across it, from above to below, where its eased steps in the middle
    // land either side of the step area, farther apart than it is deep.
    constexpr core::Vec2 RIGHT{ .x = 200.0F };
    constexpr core::Vec2 ABOVE{ .y = -130.0F };
    for (const auto& [from, onto] : { std::pair{ portal + RIGHT, portal }, std::pair{ portal + ABOVE, portal - ABOVE } }) {
        CAPTURE(from.y);
        game::Host host(test::TestManifest());
        std::vector<core::Event> events;
        StepTo(host, 1, from, events);
        REQUIRE_FALSE(host.Requested().has_value());
        const std::array<core::Action, 1> dash{
            { { .kind = core::ActionKind::Cast, .slot = core::Slot::Movement, .point = onto } }
        };
        host.Tick(At(2), { .actions = dash, .held = {} }, events);
        for (u64 tick = 3; tick < MOST_TICKS && !host.Requested(); ++tick) {
            host.Tick(At(tick), {}, events);
        }
        const std::optional<core::SceneFacts> requested = host.Requested();
        REQUIRE(requested.has_value());
        CHECK(requested.value_or(core::SceneFacts{}).key == game::STRESS_SCENE);
    }
}

TEST_CASE("the step onto the portal ends the uses still open") {
    constexpr core::Vec2 FAR_POINT{ .x = 1'500.0F, .y = 500.0F }; // a bolt's flight there outlasts a tick
    constexpr u64 BOLT_USE = game::CastUse(1, core::Slot::Generator);
    game::Host host(test::TestManifest());
    const std::array<core::Action, 1> cast{
        { { .kind = core::ActionKind::Cast, .slot = core::Slot::Generator, .point = FAR_POINT } }
    };
    std::vector<core::Event> events;
    host.Tick(At(1), { .actions = cast, .held = {} }, events);
    events.clear();
    StepTo(host, 2, PortalOf(game::ARENA_SCENE), events);
    REQUIRE(host.Requested().has_value());
    REQUIRE(events.size() == 1); // nothing of the arena's goes on to hit
    CHECK(events.front().kind == core::EventKind::UseEnded);
    CHECK(events.front().use == BOLT_USE);

    // A bolt cast on the stepping tick itself ends with it.
    constexpr u64 SAME_TICK_USE = game::CastUse(1, core::Slot::Generator);
    game::Host casting(test::TestManifest());
    events.clear();
    StepTo(casting, 1, PortalOf(game::ARENA_SCENE), events, cast);
    REQUIRE(casting.Requested().has_value());
    REQUIRE(events.size() == 1);
    CHECK(events.front().kind == core::EventKind::UseEnded);
    CHECK(events.front().use == SAME_TICK_USE);
}

TEST_CASE("a step beside the portal asks for nothing") {
    constexpr f32 BESIDE = 1.5F * game::PORTAL_RADIUS;
    // Straight up or down, past the drawn seal's rim (PORTAL_DEPTH_RATIO of its size, twice the radius) but within the radius.
    constexpr f32 PAST_THE_RIM = 0.9F * game::PORTAL_RADIUS;
    const core::Vec2 portal = PortalOf(game::ARENA_SCENE);
    for (const core::Vec2 step : { core::Vec2{ .x = BESIDE },
                                   core::Vec2{ .x = -BESIDE },
                                   core::Vec2{ .y = BESIDE },
                                   core::Vec2{ .y = PAST_THE_RIM },
                                   core::Vec2{ .y = -PAST_THE_RIM } }) {
        CAPTURE(step.x);
        CAPTURE(step.y);
        game::Host host(test::TestManifest());
        std::vector<core::Event> events;
        StepInTo(host, portal, step, events);
        CHECK_FALSE(host.Requested().has_value());
    }
}

TEST_CASE("arriving through a portal lands off the next scene's portal") {
    constexpr u64 FIRST_TICKS = 3;
    for (const auto& [scene, next] : PORTAL_LEADS) {
        CAPTURE(game::SceneName(scene));
        game::Host host(test::TestManifest(), scene, test::ShippedData());
        std::vector<core::Event> events;
        StepTo(host, 1, PortalOf(scene), events);
        REQUIRE(host.Requested().has_value());
        host.EnterScene(); // as the logic does after the tick
        REQUIRE(host.Current().key == next);
        const core::Vec2 away = FootOf(host, host.Player()) - PortalOf(next);
        CHECK(std::hypot(away.x, away.y) > game::PORTAL_RADIUS);
        for (u64 tick = 2; tick < 2 + FIRST_TICKS; ++tick) {
            host.Tick(At(tick), {}, events);
        }
        CHECK_FALSE(host.Requested().has_value());
    }
}

// Ticks `host` from tick `first` to `last` under `held` movement, with no actions.
void TickRange(game::Host& host, u64 first, u64 last, core::HeldInput held = {}) {
    std::vector<core::Event> events;
    for (u64 tick = first; tick <= last; ++tick) {
        host.Tick(At(tick), { .actions = {}, .held = held }, events);
    }
}

// Ticks `host` once at `tick` with one action of `kind`.
void TickWith(game::Host& host, u64 tick, core::ActionKind kind, core::HeldInput held = {}) {
    const std::array<core::Action, 1> actions{ { { .kind = kind } } };
    std::vector<core::Event> events;
    host.Tick(At(tick), { .actions = actions, .held = held }, events);
}

TEST_CASE("the world clock runs only on the world map and not while paused") {
    constexpr u64 TICKS = 10;
    game::Host world(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    TickRange(world, 1, TICKS);
    CHECK(world.WorldTicks() == TICKS);
    CHECK_FALSE(world.WorldPaused());

    TickWith(world, TICKS + 1, core::ActionKind::ToggleWorldPause, HELD_RIGHT);
    const core::Vec2 foot = FootOf(world, world.Player());
    TickRange(world, TICKS + 2, TICKS + 1 + TICKS, HELD_RIGHT);
    CHECK(world.WorldPaused());
    CHECK(world.WorldTicks() == TICKS);
    const core::Vec2 after = FootOf(world, world.Player());
    CHECK(after.x == foot.x); // the cultivator stands under held movement
    CHECK(after.y == foot.y);

    TickWith(world, (2 * TICKS) + 2, core::ActionKind::ToggleWorldPause);
    TickRange(world, (2 * TICKS) + 3, (2 * TICKS) + 2 + TICKS);
    CHECK_FALSE(world.WorldPaused());
    CHECK(world.WorldTicks() == (2 * TICKS) + 1); // resumed from the toggle's own tick

    game::Host arena(test::TestManifest());
    TickWith(arena, 1, core::ActionKind::ToggleWorldPause);
    TickRange(arena, 2, 1 + TICKS);
    CHECK(arena.WorldTicks() == 0);
    CHECK_FALSE(arena.WorldPaused()); // ignored off the map
}

TEST_CASE("a scene change clears the world pause, and the clock stands still away from the map") {
    constexpr u64 TICKS = 10;
    game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    TickRange(host, 1, TICKS);
    TickWith(host, TICKS + 1, core::ActionKind::ToggleWorldPause);
    REQUIRE(host.WorldPaused());
    TickWith(host, TICKS + 2, core::ActionKind::CycleScene);
    host.EnterScene(); // the arena, as the logic does after the tick
    REQUIRE(host.Current().key == game::ARENA_SCENE);
    CHECK_FALSE(host.WorldPaused());
    TickRange(host, TICKS + 3, TICKS + 2 + TICKS);
    CHECK(host.WorldTicks() == TICKS);
}

// A host with the shipped map that has loaded and entered an empty world save at `clock`: a world clock anywhere in a
// month without ticking the world scene there.
std::unique_ptr<game::Host> WorldAt(test::SavedClock clock) {
    auto host = std::make_unique<game::Host>(test::TestManifest(), game::ARENA_SCENE, test::ShippedData());
    const std::vector<std::byte> bytes = test::WorldSaveAt(clock);
    core::Reader reader{ bytes };
    REQUIRE(host->Load(reader, 0)); // at tick 0: it holds no flash or seal to date
    host->EnterScene();
    REQUIRE(host->Current().key == game::WORLD_SCENE);
    return host;
}

// Lets a month pass for `host`, as the logic does on the frozen tick after the transition.
void StepMonth(game::Host& host, u64 state) {
    core::Jobs jobs{ 1 };
    host.AdvanceCalendar({ .wall = state, .simulation = 0, .state = state }, jobs, {});
}

// What `host` publishes as its calendar.
std::optional<core::CalendarView> CalendarOf(game::Host& host) {
    core::Jobs jobs{ 1 };
    core::Snapshot snapshot;
    host.Publish(snapshot, jobs, std::nullopt);
    return snapshot.calendar;
}

TEST_CASE("a month's last day ends in the month step") {
    const std::unique_ptr<game::Host> world = WorldAt({ .ticks = game::TICKS_PER_MONTH - 1, .months = 0 });
    CHECK_FALSE(world->MonthDue()); // the last day's last tick is still to run
    TickRange(*world, 1, 1);
    CHECK(world->MonthDue());
    CHECK(world->WorldTicks() == game::TICKS_PER_MONTH);
    TickRange(*world, 2, 2);
    CHECK(world->WorldTicks() == game::TICKS_PER_MONTH); // the clock waits for the step

    StepMonth(*world, 3);
    CHECK(world->Months() == 1);
    CHECK_FALSE(world->MonthDue());
    CHECK(world->WorldTicks() == game::TICKS_PER_MONTH);
    TickRange(*world, 3, 3);
    CHECK(world->WorldTicks() == game::TICKS_PER_MONTH + 1); // and runs again
}

TEST_CASE("M moves the clock to the next month's first tick") {
    constexpr u64 CLOCK = 1'000;
    const std::unique_ptr<game::Host> world = WorldAt({ .ticks = CLOCK, .months = 0 });
    StepMonth(*world, 1);
    CHECK(world->WorldTicks() == game::TICKS_PER_MONTH);
    CHECK(world->Months() == 1);
    CHECK_FALSE(world->MonthDue());

    // Pressed while the world is paused: the clock jumps and the pause holds.
    TickWith(*world, 1, core::ActionKind::ToggleWorldPause);
    StepMonth(*world, 2);
    CHECK(world->WorldTicks() == 2 * game::TICKS_PER_MONTH);
    CHECK(world->Months() == 2);
    CHECK(world->WorldPaused());
    TickRange(*world, 2, 2);
    CHECK(world->WorldTicks() == 2 * game::TICKS_PER_MONTH);

    // Off the map the clock still moves with the steps, so the calendar keeps up with them.
    game::Host arena(test::TestManifest());
    StepMonth(arena, 1);
    CHECK(arena.WorldTicks() == game::TICKS_PER_MONTH);
    CHECK(arena.Months() == 1);
}

TEST_CASE("the calendar view shows the month and the day") {
    constexpr u64 MONTH = game::TICKS_PER_MONTH;
    constexpr u64 DAY = game::TICKS_PER_DAY;
    constexpr u32 ELEVENTH = 11;
    constexpr u32 LAST_DAY = game::DAYS_PER_MONTH - 1;
    constexpr u64 INTO_DAY = 5;
    const auto calendarAt = [](u64 ticks, u64 months) { return CalendarOf(*WorldAt({ .ticks = ticks, .months = months })); };
    CHECK(calendarAt(0, 0) == core::CalendarView{ .month = 0, .day = 0, .paused = false });       // the first tick
    CHECK(calendarAt(DAY - 1, 0) == core::CalendarView{ .month = 0, .day = 0, .paused = false }); // a day's last
    CHECK(calendarAt(DAY, 0) == core::CalendarView{ .month = 0, .day = 1, .paused = false });     // the next day's first
    CHECK(calendarAt(MONTH - 1, 0) == core::CalendarView{ .month = 0, .day = LAST_DAY, .paused = false }); // a month's last
    CHECK(calendarAt(MONTH, 0) == core::CalendarView{ .month = 1, .day = 0, .paused = false });            // its step due
    constexpr u64 MID_MONTH = (MONTH * 2) + (DAY * ELEVENTH) + INTO_DAY; // two months on, five ticks into the 11th day
    CHECK(calendarAt(MID_MONTH, 2) == core::CalendarView{ .month = 2, .day = ELEVENTH, .paused = false });

    const std::unique_ptr<game::Host> paused = WorldAt({ .ticks = 0, .months = 0 });
    TickWith(*paused, 1, core::ActionKind::ToggleWorldPause);
    CHECK(CalendarOf(*paused) == core::CalendarView{ .month = 0, .day = 0, .paused = true });

    game::Host arena(test::TestManifest());
    CHECK_FALSE(CalendarOf(arena).has_value());
}

TEST_CASE("the calendar ends at its last month's first tick, and a save of it loads") {
    constexpr u64 CALENDAR_END = game::LAST_MONTH * game::TICKS_PER_MONTH;
    const std::unique_ptr<game::Host> world = WorldAt({ .ticks = CALENDAR_END - 1, .months = game::LAST_MONTH - 1 });
    TickRange(*world, 1, 1);
    CHECK(world->MonthDue());
    StepMonth(*world, 2);
    CHECK(world->Months() == game::LAST_MONTH);
    CHECK_FALSE(world->MonthDue());
    TickRange(*world, 2, 2);
    CHECK(world->WorldTicks() == CALENDAR_END); // the clock stands still rather than wrap
    StepMonth(*world, 3);
    CHECK(world->Months() == game::LAST_MONTH); // M no longer counts
    CHECK(world->WorldTicks() == CALENDAR_END);

    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    world->Save(writer);
    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, test::ShippedData());
    core::Reader reader{ bytes };
    CHECK(loaded.Load(reader, 0));
}

} // namespace
