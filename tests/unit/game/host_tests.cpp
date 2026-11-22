#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/draw_list.hpp"
#include "core/fog.hpp"
#include "core/instances.hpp"
#include "core/jobs.hpp"
#include "core/journey.hpp"
#include "core/names.hpp"
#include "core/paths.hpp"
#include "core/rect.hpp"
#include "core/seal.hpp"
#include "core/state.hpp"
#include "core/view_limits.hpp"
#include "data/effects.hpp"
#include "data/game_data.hpp"
#include "game/host.hpp"
#include "game/npcs.hpp"
#include "game/travel.hpp"
#include "platform/paths.hpp"
#include "support/fog.hpp"
#include "support/host.hpp"
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
        game::Host casting(test::TestManifest(), scene, test::SmallData());
        game::Host idle(test::TestManifest(), scene, test::SmallData());
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
        game::Host host(test::TestManifest(), scene, test::SmallData());
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
        return core::Contains(Grown(view, DRAW_MARGIN), instance.current);
    });
    CHECK(allWithin);
    // And everything within it is kept: as many as lie there in the publish with no view, which holds the whole arena.
    REQUIRE(Draws(everything.instances.front(), SpriteId::Ground));
    const auto inside = static_cast<std::size_t>(
        std::ranges::count_if(std::span{ everything.instances }.subspan(1), [&](const core::Instance& instance) {
            return core::Contains(Grown(view, DRAW_MARGIN), instance.current);
        })
    );
    CHECK(culled.instances.size() == inside + 1);
    CHECK(std::ranges::contains(culled.ids, host.Player()));
    // A creature published with no view, outside this one and its margin, is absent.
    std::optional<core::StableId> outside;
    for (std::size_t i = 0; i < everything.ids.size() && !outside; ++i) {
        if (everything.ids[i] != core::StableId{} &&
            !core::Contains(Grown(view, DRAW_MARGIN), everything.instances[i].current)) {
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
            return core::Contains(Grown(widest, DRAW_MARGIN), instance.current);
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
    // The shipped map, as VIEW lies far past the small test map's edge and a screen needs a map larger than one.
    game::Host host(manifest, game::WORLD_SCENE, test::ShippedData());
    const core::Snapshot snapshot = Published(host, VIEW);
    // The terrain's stamp sprites, which art/sprites.toml lists together.
    std::vector<core::Instance> terrain;
    for (auto id = std::to_underlying(SpriteId::StampForest0); id <= std::to_underlying(SpriteId::StampWater3); ++id) {
        terrain.push_back(core::MakeInstance(*manifest, { .sprite = static_cast<SpriteId>(id) }));
    }
    CHECK(snapshot.worldMap);
    CHECK(std::ranges::none_of(snapshot.instances, [&](const core::Instance& instance) {
        return DrawsTerrain(instance, terrain) || Draws(instance, SpriteId::Ground);
    }));
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
    game::Host host(manifest, game::ARENA_SCENE, test::SmallData());
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

// The arena's showcase seals, in the order it publishes them.
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

// The portal's point in `scene`, which must have one, in a game on the small test data (the world's follows its map).
core::Vec2 PortalOf(core::SceneKey scene) {
    game::Host host(test::TestManifest(), scene, test::SmallData());
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
    core::GameData data = test::SmallData(); // the data the hosts here walk on, the walk speed aside
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
        game::Host host(test::TestManifest(), scene, test::SmallData());
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
        game::Host host(test::TestManifest(), scene, test::SmallData());
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
    game::Host world(test::TestManifest(), game::WORLD_SCENE, test::SmallData());
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

TEST_CASE("the world keeps its pause and its clock through other scenes, which run on their own calendar") {
    constexpr u64 TICKS = 10;
    game::Host host(test::TestManifest(), game::WORLD_SCENE, test::SmallData());
    TickRange(host, 1, TICKS);
    TickWith(host, TICKS + 1, core::ActionKind::ToggleWorldPause);
    REQUIRE(host.WorldPaused());
    TickWith(host, TICKS + 2, core::ActionKind::CycleScene);
    host.EnterScene(); // the arena, as the logic does after the tick
    REQUIRE(host.Current().key == game::ARENA_SCENE);
    CHECK_FALSE(host.WorldPaused()); // the arena's own world, never paused
    TickRange(host, TICKS + 3, TICKS + 2 + TICKS);
    CHECK(host.WorldTicks() == 0); // its own clock, which stands still off the map

    u64 tick = TICKS + 3 + TICKS;
    while (host.Current().key != game::WORLD_SCENE) {
        TickWith(host, tick++, core::ActionKind::CycleScene);
        host.EnterScene();
    }
    CHECK(host.WorldPaused());
    CHECK(host.WorldTicks() == TICKS);
    TickRange(host, tick, tick + TICKS);
    CHECK(host.WorldTicks() == TICKS); // still paused
}

// A host with the small test map that has loaded and entered an empty world save at `clock`: a world clock anywhere in a
// month without ticking the world scene there.
std::unique_ptr<game::Host> WorldAt(const test::SavedClock& clock) {
    auto host = std::make_unique<game::Host>(test::TestManifest(), game::ARENA_SCENE, test::SmallData());
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
    game::Host loaded(test::TestManifest(), game::ARENA_SCENE, test::SmallData());
    core::Reader reader{ bytes };
    CHECK(loaded.Load(reader, 0));
}

// The world clock the hand-built travelling worlds below are saved at, early in the first month.
constexpr u64 TRAVEL_CLOCK = 1'000;
// The state seed of those worlds, which names their travellers.
constexpr u64 TRAVEL_SEED = 11;
// Their visits' stays, in days, and the seed the stays' day points are drawn from.
constexpr u32 TRAVEL_STAY_DAYS = 2;
constexpr u32 TRAVEL_STAY_SEED = 0x7e57;
// How far into its first step a traveller just setting out has walked.
constexpr i64 JUST_LEFT = 30;
// Half the side of a view round the first city's tile: it holds a traveller just setting out from it, not one in the next
// city.
constexpr f32 NEAR_HOME = 250.0F;

/// `map`'s first two cities, the walk from the first's tile to the second's, and how long the walk out and a
/// whole visit take.
struct Road {
    u16 from = core::NO_PLACE;
    u16 to = core::NO_PLACE;
    std::vector<u32> tiles;
    std::vector<u32> arrive; // the ticks from setting out to reach each tile's centre on the way out
    i64 outTicks = 0;
    i64 visitTicks = 0;
};

Road FirstRoad(const core::WorldMap& map) {
    Road road;
    std::vector<u16> cities;
    for (std::size_t index = 0; index < map.Places().size() && cities.size() < 2; ++index) {
        if (map.Places()[index].kind == core::PlaceKind::City) {
            cities.push_back(static_cast<u16>(index));
        }
    }
    REQUIRE(cities.size() == 2);
    road.from = cities[0];
    road.to = cities[1];
    road.tiles = core::FindRoute(
        map, core::WALKING, map.TileIndex(map.Places()[road.from].tile), map.TileIndex(map.Places()[road.to].tile)
    );
    REQUIRE(road.tiles.size() >= 3);
    const core::Journey probe =
        core::Visit(map, road.tiles, core::StableId{ 1 }, road.from, road.to, 0, TRAVEL_STAY_DAYS, TRAVEL_STAY_SEED);
    road.arrive = probe.parts.front().leg.arrive;
    road.outTicks = core::EndTick(probe.parts.front().leg);
    road.visitTicks = core::EndTick(probe);
    return road;
}

/// One visit along the first road: when it leaves, and how many travel together.
struct Departure {
    i64 tick = 0;
    u32 members = 1;
};

/// A world on the small test map and the shipped names, loaded from a save at TRAVEL_CLOCK, the journeys it holds and its
/// player, if any.
struct TravelWorld {
    std::unique_ptr<game::Host> host;
    std::vector<core::Journey> journeys;
    std::shared_ptr<const core::WorldMap> map;
    core::StableId player{};
};

/// A cultivator of `data` for the player, standing at `foot` in `state`'s active domain.
core::StableId CultivatorAt(core::State& state, const core::GameData& data, core::Vec2 foot) {
    const auto [stableId, entity] = state.Create(game::ACTIVE, core::IdRange::Scene);
    core::Registry& entities = state[game::ACTIVE].Entities();
    entities.emplace<game::Body>(entity, foot, foot);
    entities.emplace<game::Look>(entity, game::Look{ .sprite = data.cultivator.sprite, .shadow = true });
    entities.emplace<game::Creature>(entity, game::CreatureKind::Cultivator);
    entities.emplace<game::Health>(entity, data.cultivator.health, data.cultivator.health);
    entities.emplace<game::Caster>(entity, data.abilities.qiStart);
    return stableId;
}

/// A host that has loaded and entered a world save at `clock` holding one visit along FirstRoad() per departure, its
/// members world NPCs at home in the road's first city, and, with `player`, the player's cultivator on that city's tile
/// centre: nothing else lives there. Without a player the debug sight is on, as no one else sees the travellers. A
/// `walkSpeed` replaces the cultivator's in the game data.
TravelWorld TravelWorldOf(
    std::span<const Departure> departures,
    bool player = false,
    u64 clock = TRAVEL_CLOCK,
    std::optional<f32> walkSpeed = std::nullopt
) {
    TravelWorld world;
    // The shipped names, so a party's two members are named apart.
    core::GameData data = test::SmallData();
    if (walkSpeed) {
        data.cultivator.walkSpeed = *walkSpeed;
    }
    data.names = test::ShippedNames();
    world.map = data.world;
    const Road road = FirstRoad(*world.map);
    core::State state{ game::DOMAINS, TRAVEL_SEED };
    if (player) {
        world.player = CultivatorAt(state, data, world.map->TileCentre(road.tiles.front()));
    }
    game::Travel travel;
    for (const Departure& departure : departures) {
        std::vector<core::StableId> members;
        for (u32 member = 0; member < departure.members; ++member) {
            const core::StableId npc = game::SpawnNpc(state, road.tiles.front(), road.from);
            state[game::WORLD].Entities().get<game::Npc>(state[game::WORLD].Find(npc)).action = game::Action::Travel;
            members.push_back(npc);
        }
        core::Journey journey = core::Visit(
            *world.map, road.tiles, members.front(), road.from, road.to, departure.tick, TRAVEL_STAY_DAYS, TRAVEL_STAY_SEED
        );
        journey.members = members;
        travel.Start(journey);
        world.journeys.push_back(std::move(journey));
    }
    const std::vector<std::byte> bytes = test::ArenaSave(
        state,
        world.player,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        game::WORLD_SCENE,
        test::WORLD_VERSION,
        test::ACTIVE_VERSION,
        { .ticks = clock },
        travel,
        std::nullopt,
        game::Relationships{},
        {},
        &data
    );
    world.host = std::make_unique<game::Host>(test::TestManifest(), game::ARENA_SCENE, data);
    core::Reader reader{ bytes };
    REQUIRE(world.host->Load(reader, 0));
    world.host->EnterScene();
    world.host->SetDebugSight(!player);
    return world;
}

/// A view `half` either way of the first road's first city's tile centre.
core::Rect NearHome(const core::WorldMap& map, f32 half = NEAR_HOME) {
    const core::Vec2 home = map.TileCentre(FirstRoad(map).tiles.front());
    return { .min = home - core::Vec2{ .x = half, .y = half }, .max = home + core::Vec2{ .x = half, .y = half } };
}

/// The label `snapshot` publishes for `entity`; REQUIREs one.
core::TextItem LabelOf(const core::Snapshot& snapshot, core::StableId entity) {
    const auto found = std::ranges::find(snapshot.texts, entity, &core::TextItem::entity);
    REQUIRE(found != snapshot.texts.end());
    return *found;
}

/// Whether `snapshot` draws `entity`, or labels it.
bool Shows(const core::Snapshot& snapshot, core::StableId entity) {
    return std::ranges::contains(snapshot.ids, entity) ||
           std::ranges::contains(snapshot.texts, entity, &core::TextItem::entity);
}

/// The instance drawing `entity` in `snapshot`; REQUIREs one.
core::Instance InstanceOf(const core::Snapshot& snapshot, core::StableId entity) {
    const auto found = std::ranges::find(snapshot.ids, entity);
    REQUIRE(found != snapshot.ids.end());
    return snapshot.instances.at(static_cast<std::size_t>(found - snapshot.ids.begin()));
}

/// Whether two instances have the same bytes.
bool SameInstance(const core::Instance& lhs, const core::Instance& rhs) {
    return std::ranges::equal(std::as_bytes(std::span{ &lhs, 1 }), std::as_bytes(std::span{ &rhs, 1 }));
}

/// Whether two points have the same bits.
bool SamePoint(core::Vec2 lhs, core::Vec2 rhs) {
    return std::bit_cast<u64>(lhs) == std::bit_cast<u64>(rhs);
}

TEST_CASE("travellers in view are drawn and labelled with their names") {
    const std::array<Departure, 1> departures{ { { .tick = static_cast<i64>(TRAVEL_CLOCK) - JUST_LEFT, .members = 2 } } };
    const TravelWorld world = TravelWorldOf(departures);
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::Snapshot snapshot = Published(*world.host, NearHome(*world.map));
    const std::shared_ptr<const core::Names> names = test::ShippedNames();
    const core::Journey& journey = world.journeys.front();
    const f32 height = static_cast<f32>(manifest->Sprite(SpriteId::Villager).footY);
    const core::Vec2 head{ .x = 0.0F, .y = -height };
    // Both members of the party, the second beside the first.
    for (std::size_t member = 0; member < journey.members.size(); ++member) {
        CAPTURE(member);
        const core::StableId traveller = journey.members[member];
        const core::Vec2 foot = core::Where(*world.map, journey, static_cast<i64>(TRAVEL_CLOCK), member);
        const core::Instance expected = core::MakeInstance(
            *manifest, { .sprite = SpriteId::Villager, .previous = foot, .current = foot, .flags = core::INSTANCE_SIGHT_FADE }
        );
        CHECK(SameInstance(InstanceOf(snapshot, traveller), expected));
        const core::Instance shadow = core::MakeInstance(
            *manifest, { .sprite = SpriteId::Shadow, .previous = foot, .current = foot, .flags = core::INSTANCE_SIGHT_FADE }
        );
        CHECK(std::ranges::any_of(snapshot.instances, [&](const core::Instance& drawn) {
            return SameInstance(drawn, shadow);
        }));
        const core::TextItem label = LabelOf(snapshot, traveller);
        const core::NpcName name = core::NameOf(*names, TRAVEL_SEED, traveller);
        CHECK(label.kind == core::TextKind::Label);
        CHECK(label.npcName == std::optional{ name });
        CHECK(label.nameKey == core::NameKey(name));
        CHECK(label.priority == core::LabelPriority::Named);
        CHECK(SamePoint(label.current, foot + head));
        CHECK(SamePoint(label.previous, foot + head));
        CHECK(SamePoint(label.offset, game::LABEL_OFFSET));
        CHECK(label.height == height);
    }
    CHECK(core::NameOf(*names, TRAVEL_SEED, journey.members[0]) != core::NameOf(*names, TRAVEL_SEED, journey.members[1]));
}

TEST_CASE("travellers outside the view are not published") {
    const Road road = FirstRoad(*test::SmallWorld());
    const auto clock = static_cast<i64>(TRAVEL_CLOCK);
    const std::array<Departure, 2> departures{ {
        { .tick = clock - JUST_LEFT },                                                   // by its home
        { .tick = clock - road.outTicks - (static_cast<i64>(game::TICKS_PER_DAY) / 2) }, // half a day into its stay
    } };
    const TravelWorld world = TravelWorldOf(departures);
    const core::Rect view = NearHome(*world.map);
    const core::Vec2 away = core::Where(*world.map, world.journeys[1], clock);
    REQUIRE_FALSE(core::Contains(view, away));
    const core::Snapshot snapshot = Published(*world.host, view);
    CHECK(Shows(snapshot, world.journeys[0].members.front()));
    CHECK_FALSE(Shows(snapshot, world.journeys[1].members.front()));
}

TEST_CASE("a traveller still at home or home again isn't drawn") {
    const Road road = FirstRoad(*test::SmallWorld());
    const auto clock = static_cast<i64>(TRAVEL_CLOCK);
    constexpr i64 SOON = 100;
    const std::array<Departure, 4> departures{ {
        { .tick = clock - JUST_LEFT },       // under way, to show the others aren't culled for their place
        { .tick = clock + SOON },            // yet to leave
        { .tick = clock },                   // leaving on this very tick
        { .tick = clock - road.visitTicks }, // home on this very tick
    } };
    const TravelWorld world = TravelWorldOf(departures);
    const core::Snapshot snapshot = Published(*world.host, NearHome(*world.map));
    CHECK(Shows(snapshot, world.journeys[0].members.front()));
    CHECK_FALSE(Shows(snapshot, world.journeys[1].members.front()));
    CHECK(Shows(snapshot, world.journeys[2].members.front()));
    CHECK_FALSE(Shows(snapshot, world.journeys[3].members.front()));
}

TEST_CASE("a selected traveller's label outranks a place's") {
    const std::array<Departure, 1> departures{ { { .tick = static_cast<i64>(TRAVEL_CLOCK) - JUST_LEFT, .members = 2 } } };
    const TravelWorld world = TravelWorldOf(departures);
    const core::StableId chosen = world.journeys.front().members[0];
    const core::StableId other = world.journeys.front().members[1];
    const std::array<core::Action, 1> select{ { { .kind = core::ActionKind::Select, .target = chosen } } };
    std::vector<core::Event> events;
    world.host->Tick(At(1), { .actions = select, .held = {} }, events);
    REQUIRE(world.host->Selected() == chosen);
    world.host->Explore(NearHome(*world.map)); // a place is named only once explored
    const core::Snapshot snapshot = Published(*world.host, NearHome(*world.map));
    const auto place = std::ranges::find(snapshot.texts, core::TextKind::Place, &core::TextItem::kind);
    REQUIRE(place != snapshot.texts.end());
    CHECK(LabelOf(snapshot, chosen).priority == core::LabelPriority::Selected);
    CHECK(LabelOf(snapshot, chosen).priority > place->priority);
    CHECK(LabelOf(snapshot, other).priority == core::LabelPriority::Named);
    const auto drawn = static_cast<u32>(std::ranges::find(snapshot.ids, chosen) - snapshot.ids.begin());
    CHECK(snapshot.selected == std::optional{ drawn }); // outlined
}

TEST_CASE("the cultivator draws over a traveller standing where it stands") {
    // One setting out on this tick from the city's tile centre, where the cultivator stands, and one a step on its way.
    const auto clock = static_cast<i64>(TRAVEL_CLOCK);
    const std::array<Departure, 2> departures{ { { .tick = clock }, { .tick = clock - JUST_LEFT } } };
    const TravelWorld world = TravelWorldOf(departures, true);
    const core::Snapshot snapshot = Published(*world.host, NearHome(*world.map));
    const auto indexOf = [&snapshot](core::StableId entity) {
        const auto found = std::ranges::find(snapshot.ids, entity);
        REQUIRE(found != snapshot.ids.end());
        return found - snapshot.ids.begin();
    };
    const core::Vec2 foot = core::Where(*world.map, world.journeys[0], clock);
    REQUIRE(SamePoint(foot, world.map->TileCentre(FirstRoad(*world.map).tiles.front())));
    CHECK(indexOf(world.player) > indexOf(world.journeys[0].members.front()));
    CHECK(indexOf(world.player) > indexOf(world.journeys[1].members.front()));
}

TEST_CASE("travellers stand still while the world is paused") {
    const std::array<Departure, 1> departures{ { { .tick = static_cast<i64>(TRAVEL_CLOCK) - JUST_LEFT } } };
    const TravelWorld world = TravelWorldOf(departures);
    const core::StableId traveller = world.journeys.front().members.front();
    const core::Rect view = NearHome(*world.map);

    // Running, a tick takes it from where the clock was to where it is.
    TickRange(*world.host, 1, 1);
    const core::Snapshot running = Published(*world.host, view);
    const core::Vec2 from = core::Where(*world.map, world.journeys.front(), static_cast<i64>(TRAVEL_CLOCK));
    const core::Vec2 onward = core::Where(*world.map, world.journeys.front(), static_cast<i64>(TRAVEL_CLOCK) + 1);
    REQUIRE_FALSE(SamePoint(from, onward));
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    CHECK(SameInstance(
        InstanceOf(running, traveller),
        core::MakeInstance(
            *manifest, { .sprite = SpriteId::Villager, .previous = from, .current = onward, .flags = core::INSTANCE_SIGHT_FADE }
        )
    ));

    // Paused, on the pausing tick and after it.
    TickWith(*world.host, 2, core::ActionKind::ToggleWorldPause);
    TickRange(*world.host, 3, 3);
    const core::Snapshot paused = Published(*world.host, view);
    CHECK(SameInstance(
        InstanceOf(paused, traveller),
        core::MakeInstance(
            *manifest,
            { .sprite = SpriteId::Villager, .previous = onward, .current = onward, .flags = core::INSTANCE_SIGHT_FADE }
        )
    ));
    const core::TextItem label = LabelOf(paused, traveller);
    CHECK(SamePoint(label.previous, label.current));
}

TEST_CASE("a month step asked for early stands travellers where the new clock puts them, with no streak") {
    const auto nextMonth = static_cast<i64>(game::TICKS_PER_MONTH);
    const std::array<Departure, 1> departures{ { { .tick = nextMonth - JUST_LEFT } } };
    const TravelWorld world = TravelWorldOf(departures);
    const core::StableId traveller = world.journeys.front().members.front();
    TickRange(*world.host, 1, 1);
    StepMonth(*world.host, 2);
    REQUIRE(world.host->WorldTicks() == game::TICKS_PER_MONTH);
    const core::Vec2 foot = core::Where(*world.map, world.journeys.front(), nextMonth);
    const core::Instance expected = core::MakeInstance(
        *test::TestManifest(),
        { .sprite = SpriteId::Villager, .previous = foot, .current = foot, .flags = core::INSTANCE_SIGHT_FADE }
    );
    CHECK(SameInstance(InstanceOf(Published(*world.host, NearHome(*world.map)), traveller), expected));
}

// How many tiles along the first road a walk-to below goes, and a tile farther on.
constexpr std::size_t ROUTE_TILES = 4;
constexpr std::size_t FARTHER = ROUTE_TILES + 2;
// The ticks the cultivator walks a route before a test changes something: still on its first step.
constexpr u64 WALKED = 10;

using test::BodyOf;

/// Ticks `host` once at `tick` with a walk-to towards `point`, under `held` movement.
void WalkTo(game::Host& host, u64 tick, core::Vec2 point, core::HeldInput held = {}) {
    const std::array<core::Action, 1> walk{ { { .kind = core::ActionKind::WalkTo, .point = point } } };
    std::vector<core::Event> events;
    host.Tick(At(tick), { .actions = walk, .held = held }, events);
}

/// The leg a walk-to from `foot` towards `point` on `map` takes, set out on world tick `clock`: the shortest way on foot
/// from the foot's tile, starting from the foot itself, at the cultivator's walk speed. REQUIREs a step at least.
core::Leg LegTo(const core::WorldMap& map, core::Vec2 foot, core::Vec2 point, u64 clock) {
    const std::vector<u32> tiles = core::FindShortestRoute(map, map.TileAt(foot), map.TileAt(point));
    REQUIRE(tiles.size() >= 2);
    return core::MakePacedLeg(map, static_cast<i64>(clock), foot, tiles, test::SmallData().cultivator.walkSpeed);
}

/// Whether `route`, as a snapshot publishes one, is `foot` and then the centres of `leg`'s tiles from its `next`th on.
bool RouteIs(
    std::span<const core::Vec2> route,
    const core::WorldMap& map,
    core::Vec2 foot,
    const core::Leg& leg,
    std::size_t next
) {
    std::vector<core::Vec2> expected{ foot };
    for (std::size_t tile = next; tile < leg.tiles.size(); ++tile) {
        expected.push_back(map.TileCentre(leg.tiles[tile]));
    }
    return std::ranges::equal(route, expected, SamePoint);
}

/// The player's route in `host`; REQUIREs one.
core::Journey RouteOf(const game::Host& host) {
    REQUIRE(host.PlayerRoute().has_value());
    return host.PlayerRoute().value_or(core::Journey{});
}

/// The leg of the player's route in `host`; REQUIREs a route of one leg.
core::Leg RouteLegOf(const game::Host& host) {
    const core::Journey route = RouteOf(host);
    REQUIRE(route.parts.size() == 1);
    REQUIRE_FALSE(route.parts.front().stay);
    return route.parts.front().leg;
}

/// Whether `lhs` and `rhs` are the same leg, bit for bit.
bool SameLeg(const core::Leg& lhs, const core::Leg& rhs) {
    return lhs.shape == rhs.shape && lhs.profile == rhs.profile && lhs.startTick == rhs.startTick &&
           SamePoint(lhs.origin, rhs.origin) && lhs.tiles == rhs.tiles && lhs.arrive == rhs.arrive;
}

/// The first road's tiles on `map`, with room for the walks below.
std::vector<u32> RouteRoad(const core::WorldMap& map) {
    const Road road = FirstRoad(map);
    REQUIRE(road.tiles.size() > FARTHER);
    return road.tiles;
}

TEST_CASE("a walk-to sends the cultivator along the route") {
    const TravelWorld world = TravelWorldOf({}, true);
    const core::WorldMap& map = *world.map;
    const core::Vec2 start = FootOf(*world.host, world.player);
    const core::Vec2 target = map.TileCentre(RouteRoad(map)[ROUTE_TILES]);
    const core::Leg leg = LegTo(map, start, target, TRAVEL_CLOCK);

    // It sets out from its foot, the route a journey of its own.
    WalkTo(*world.host, 1, target);
    const core::Vec2 first = core::Where(map, leg, TRAVEL_CLOCK + 1);
    CHECK(SamePoint(BodyOf(*world.host, world.player).previous, start));
    CHECK(SamePoint(BodyOf(*world.host, world.player).current, first));
    const std::vector<core::StableId> alone{ world.player };
    CHECK(RouteOf(*world.host).members == alone);
    CHECK(SameLeg(RouteLegOf(*world.host), leg));
    const bool fromFirst = RouteIs(Published(*world.host).route, map, first, leg, 1);
    CHECK(fromFirst);

    // Halfway between its second and third tiles, it has those two and the rest still to walk.
    const u64 midway = TRAVEL_CLOCK + ((u64{ leg.arrive[1] } + u64{ leg.arrive[2] }) / 2);
    TickRange(*world.host, 2, midway - TRAVEL_CLOCK);
    const core::Vec2 there = core::Where(map, leg, static_cast<i64>(midway));
    CHECK(SamePoint(FootOf(*world.host, world.player), there));
    const bool fromThere = RouteIs(Published(*world.host).route, map, there, leg, 2);
    CHECK(fromThere);

    // On its end's tick it stands on the last tile's centre, the route done.
    const auto end = static_cast<u64>(core::EndTick(leg));
    TickRange(*world.host, midway - TRAVEL_CLOCK + 1, end - TRAVEL_CLOCK);
    CHECK(SamePoint(FootOf(*world.host, world.player), map.TileCentre(leg.tiles.back())));
    CHECK_FALSE(world.host->PlayerRoute().has_value());
    CHECK(Published(*world.host).route.empty());

    // Then it walks freely again.
    TickRange(*world.host, end - TRAVEL_CLOCK + 1, end - TRAVEL_CLOCK + 1, HELD_RIGHT);
    const core::Vec2 walked = map.TileCentre(leg.tiles.back()) + core::Vec2{ .x = test::SmallData().cultivator.walkSpeed };
    CHECK(SamePoint(FootOf(*world.host, world.player), walked));
}

TEST_CASE("a walk-to crosses meadow at the held walk's pace") {
    constexpr u64 SOUTH_TICKS = 70; // a step south takes 64
    constexpr u64 TILES_EAST = 5;
    constexpr u64 COMPARED_TICKS = 100;
    const TravelWorld world = TravelWorldOf({}, true);
    const core::WorldMap& map = *world.map;
    const f32 walkSpeed = test::SmallData().cultivator.walkSpeed;
    const core::Vec2 row = map.TileCentre(map.TileIndex({ .column = 0, .row = 1 }));
    // One step south, off the road's row, so the five tiles east are all meadow, which the old cost timing took 800 ticks
    // over.
    WalkTo(*world.host, 1, row);
    TickRange(*world.host, 2, SOUTH_TICKS);
    REQUIRE_FALSE(world.host->PlayerRoute().has_value());
    const core::Vec2 start = FootOf(*world.host, world.player);
    const u64 clock = world.host->WorldTicks();
    const core::Vec2 target = start + core::Vec2{ .x = static_cast<f32>(TILES_EAST) * test::small::TILE, .y = 0.0F };
    WalkTo(*world.host, SOUTH_TICKS + 1, target);
    const auto ticks = static_cast<i64>(std::ceil((target.x - start.x) / walkSpeed));
    REQUIRE(ticks == 320);
    CHECK(core::EndTick(RouteOf(*world.host)) == static_cast<i64>(clock) + ticks);
    CHECK(RouteLegOf(*world.host).tiles.size() == TILES_EAST + 1);
    CHECK(SamePoint(core::Where(map, RouteLegOf(*world.host), static_cast<i64>(clock) + ticks), target));

    // After the same number of ticks the route's foot is where held movement walking east would be.
    TickRange(*world.host, SOUTH_TICKS + 2, SOUTH_TICKS + COMPARED_TICKS);
    const TravelWorld held = TravelWorldOf({}, true);
    WalkTo(*held.host, 1, row);
    TickRange(*held.host, 2, SOUTH_TICKS);
    TickRange(*held.host, SOUTH_TICKS + 1, SOUTH_TICKS + COMPARED_TICKS, HELD_RIGHT);
    const core::Vec2 walked = FootOf(*held.host, held.player);
    CHECK(FootOf(*world.host, world.player).x == doctest::Approx(walked.x).epsilon(0.0001));
    CHECK(FootOf(*world.host, world.player).y == doctest::Approx(walked.y).epsilon(0.0001));
}

TEST_CASE("a walk-to goes the shortest way whatever the ground") {
    const TravelWorld world = TravelWorldOf({}, true);
    const core::WorldMap& map = *world.map;
    // The bottom row's column 5 is two diagonals and three straight steps from the start city. The cheapest way runs along
    // the road first; the shortest keeps near the straight line.
    const u32 target = map.TileIndex({ .column = test::small::ROAD_END, .row = 2 });
    WalkTo(*world.host, 1, map.TileCentre(target));
    const core::Leg leg = RouteLegOf(*world.host);
    CHECK(leg.tiles == std::vector<u32>{ 0, 1, 9, 10, 18, 19 });
    CHECK(core::FindRoute(map, core::WALKING, 0, target) == std::vector<u32>{ 0, 1, 2, 3, 11, 19 });
    // Three straight steps of 64 ticks and two diagonals: ceil((3 * 256 + 2 * 256 * sqrt 2) / 4).
    CHECK(leg.arrive.back() == 374);
    CHECK(game::LegSound(leg, map));
}

TEST_CASE("a walk-to from a cultivator with no walk speed changes nothing") {
    constexpr f32 NO_SPEED = 0.0F;
    const TravelWorld world = TravelWorldOf({}, true, TRAVEL_CLOCK, NO_SPEED);
    const core::WorldMap& map = *world.map;
    const core::Vec2 start = FootOf(*world.host, world.player);
    WalkTo(*world.host, 1, map.TileCentre(RouteRoad(map)[ROUTE_TILES]));
    CHECK_FALSE(world.host->PlayerRoute().has_value());
    CHECK(SamePoint(FootOf(*world.host, world.player), start));
}

TEST_CASE("a paced leg from a foot beside the next centre is sound") {
    const std::shared_ptr<const core::WorldMap> world = test::SmallWorld();
    const core::WorldMap& map = *world;
    constexpr f32 NEAR = 1.0F;
    const core::Leg leg = core::MakePacedLeg(
        map,
        static_cast<i64>(TRAVEL_CLOCK),
        map.TileCentre(1) - core::Vec2{ .x = NEAR, .y = 0.0F },
        std::array<u32, 2>{ { 0, 1 } },
        test::SmallData().cultivator.walkSpeed
    );
    CHECK(leg.arrive[1] == 1);
    CHECK(game::LegSound(leg, map));
}

TEST_CASE("held movement cancels a route") {
    const TravelWorld world = TravelWorldOf({}, true);
    const core::WorldMap& map = *world.map;
    const core::Vec2 start = FootOf(*world.host, world.player);
    const core::Vec2 target = map.TileCentre(RouteRoad(map)[ROUTE_TILES]);
    const core::Leg leg = LegTo(map, start, target, TRAVEL_CLOCK);
    WalkTo(*world.host, 1, target);
    TickRange(*world.host, 2, WALKED);
    const core::Vec2 onRoute = FootOf(*world.host, world.player);
    REQUIRE(SamePoint(onRoute, core::Where(map, leg, static_cast<i64>(TRAVEL_CLOCK + WALKED))));

    TickRange(*world.host, WALKED + 1, WALKED + 1, HELD_RIGHT);
    const core::Vec2 walked = onRoute + core::Vec2{ .x = test::SmallData().cultivator.walkSpeed };
    CHECK_FALSE(world.host->PlayerRoute().has_value());
    CHECK(SamePoint(BodyOf(*world.host, world.player).previous, onRoute));
    CHECK(SamePoint(BodyOf(*world.host, world.player).current, walked));
    CHECK(Published(*world.host).route.empty());

    // Let go, it stands where it walked to rather than taking the route up again.
    TickRange(*world.host, WALKED + 2, WALKED + 3);
    CHECK(SamePoint(FootOf(*world.host, world.player), walked));
}

TEST_CASE("a second walk-to replaces the route from where the cultivator stands") {
    const TravelWorld world = TravelWorldOf({}, true);
    const core::WorldMap& map = *world.map;
    const std::vector<u32> road = RouteRoad(map);
    const core::Vec2 start = FootOf(*world.host, world.player);
    const core::Leg leg = LegTo(map, start, map.TileCentre(road[ROUTE_TILES]), TRAVEL_CLOCK);
    WalkTo(*world.host, 1, map.TileCentre(road[ROUTE_TILES]));
    const u64 midway = TRAVEL_CLOCK + ((u64{ leg.arrive[1] } + u64{ leg.arrive[2] }) / 2);
    TickRange(*world.host, 2, midway - TRAVEL_CLOCK);
    const core::Vec2 there = FootOf(*world.host, world.player);

    // From the tile under its foot and the foot itself: no walk back to the first route's start, no jump.
    const core::Vec2 farther = map.TileCentre(road[FARTHER]);
    const core::Leg next = LegTo(map, there, farther, midway);
    REQUIRE(next.tiles.front() != leg.tiles.front());
    WalkTo(*world.host, midway - TRAVEL_CLOCK + 1, farther);
    CHECK(SamePoint(BodyOf(*world.host, world.player).previous, there));
    CHECK(SamePoint(BodyOf(*world.host, world.player).current, core::Where(map, next, static_cast<i64>(midway + 1))));
    CHECK(SameLeg(RouteLegOf(*world.host), next));
}

TEST_CASE("a walk-to the cultivator's own tile stops it where it stands") {
    const TravelWorld world = TravelWorldOf({}, true);
    const core::WorldMap& map = *world.map;
    const core::Vec2 start = FootOf(*world.host, world.player);
    const core::Vec2 target = map.TileCentre(RouteRoad(map)[ROUTE_TILES]);

    // Standing on its tile, it starts no route.
    WalkTo(*world.host, 1, start);
    CHECK_FALSE(world.host->PlayerRoute().has_value());
    CHECK(SamePoint(FootOf(*world.host, world.player), start));

    // Walking, it stops on the spot, with no route left that never ends.
    WalkTo(*world.host, 2, target);
    TickRange(*world.host, 3, WALKED);
    const core::Vec2 there = FootOf(*world.host, world.player);
    REQUIRE_FALSE(SamePoint(there, start));
    WalkTo(*world.host, WALKED + 1, there);
    CHECK_FALSE(world.host->PlayerRoute().has_value());
    CHECK(SamePoint(BodyOf(*world.host, world.player).previous, there));
    CHECK(SamePoint(BodyOf(*world.host, world.player).current, there));
    TickRange(*world.host, WALKED + 2, WALKED + 3);
    CHECK(SamePoint(FootOf(*world.host, world.player), there));
    CHECK(Published(*world.host).route.empty());
}

/// The centre of the first tile of `map` that no one can walk onto.
core::Vec2 FirstImpassable(const core::WorldMap& map) {
    for (u32 tile = 0; tile < map.Columns() * map.Rows(); ++tile) {
        if (map.CostAt(tile, core::WALKING) == core::IMPASSABLE_COST) {
            return map.TileCentre(tile);
        }
    }
    FAIL("the map has no impassable tile");
    return {};
}

TEST_CASE("a walk-to an impassable tile or off the map does nothing") {
    const TravelWorld world = TravelWorldOf({}, true);
    const core::WorldMap& map = *world.map;
    const core::Vec2 start = FootOf(*world.host, world.player);
    const core::Vec2 target = map.TileCentre(RouteRoad(map)[ROUTE_TILES]);
    const core::Vec2 impassable = FirstImpassable(map);
    const core::Vec2 offMap{ .x = -map.TileSize(), .y = map.TileSize() };

    // Standing, it stays.
    WalkTo(*world.host, 1, impassable);
    WalkTo(*world.host, 2, offMap);
    CHECK_FALSE(world.host->PlayerRoute().has_value());
    CHECK(SamePoint(FootOf(*world.host, world.player), start));

    // Walking, its route goes on as it was.
    constexpr u64 SET_OUT = 3;
    const core::Leg leg = LegTo(map, start, target, TRAVEL_CLOCK + SET_OUT - 1);
    WalkTo(*world.host, SET_OUT, target);
    WalkTo(*world.host, SET_OUT + 1, impassable);
    WalkTo(*world.host, SET_OUT + 2, offMap);
    CHECK(SameLeg(RouteLegOf(*world.host), leg));
    CHECK(SamePoint(FootOf(*world.host, world.player), core::Where(map, leg, static_cast<i64>(TRAVEL_CLOCK + SET_OUT + 2))));
}

TEST_CASE("a walk-to lets go of the selection, wherever it points") {
    const TravelWorld world = TravelWorldOf({}, true);
    const core::WorldMap& map = *world.map;
    const std::array<core::Action, 1> select{ { { .kind = core::ActionKind::Select, .target = world.player } } };
    std::vector<core::Event> events;
    const std::array<core::Vec2, 3> targets{
        { map.TileCentre(RouteRoad(map)[ROUTE_TILES]), FirstImpassable(map), { .x = -map.TileSize(), .y = map.TileSize() } }
    };
    u64 tick = 1;
    for (const core::Vec2 target : targets) {
        CAPTURE(tick); // a tile it walks to, an impassable one, a point off the map
        world.host->Tick(At(tick), { .actions = select, .held = {} }, events);
        REQUIRE(world.host->Selected() == world.player);
        WalkTo(*world.host, tick + 1, target);
        CHECK(world.host->Selected() == core::StableId{});
        tick += 2;
    }
}

TEST_CASE("the paused world holds the cultivator on its route") {
    const TravelWorld world = TravelWorldOf({}, true);
    const core::WorldMap& map = *world.map;
    const core::Vec2 start = FootOf(*world.host, world.player);
    const core::Leg leg = LegTo(map, start, map.TileCentre(RouteRoad(map)[ROUTE_TILES]), TRAVEL_CLOCK);
    REQUIRE(leg.arrive[1] > WALKED + 1);
    WalkTo(*world.host, 1, map.TileCentre(leg.tiles.back()));
    TickRange(*world.host, 2, WALKED);
    const core::Vec2 held = core::Where(map, leg, static_cast<i64>(TRAVEL_CLOCK + WALKED));

    // Paused, it stands, its route held.
    constexpr u64 RESUMED = WALKED + 5;
    TickWith(*world.host, WALKED + 1, core::ActionKind::ToggleWorldPause);
    TickRange(*world.host, WALKED + 2, RESUMED - 1);
    CHECK(SamePoint(BodyOf(*world.host, world.player).previous, held));
    CHECK(SamePoint(BodyOf(*world.host, world.player).current, held));
    CHECK(world.host->PlayerRoute().has_value());
    const bool fromHeld = RouteIs(Published(*world.host).route, map, held, leg, 1);
    CHECK(fromHeld);

    // Resumed, it goes on from there.
    TickWith(*world.host, RESUMED, core::ActionKind::ToggleWorldPause);
    CHECK(SamePoint(BodyOf(*world.host, world.player).previous, held));
    CHECK(
        SamePoint(BodyOf(*world.host, world.player).current, core::Where(map, leg, static_cast<i64>(TRAVEL_CLOCK + WALKED + 1)))
    );
}

TEST_CASE("held movement cancels a route while the world is paused too") {
    const TravelWorld world = TravelWorldOf({}, true);
    const core::WorldMap& map = *world.map;
    WalkTo(*world.host, 1, map.TileCentre(RouteRoad(map)[ROUTE_TILES]));
    TickRange(*world.host, 2, WALKED);
    const core::Vec2 there = FootOf(*world.host, world.player);

    // Paused, the held direction ends the route, and the cultivator still stands.
    TickWith(*world.host, WALKED + 1, core::ActionKind::ToggleWorldPause, HELD_RIGHT);
    CHECK_FALSE(world.host->PlayerRoute().has_value());
    CHECK(SamePoint(BodyOf(*world.host, world.player).current, there));
    CHECK(Published(*world.host).route.empty());

    // Resumed, it walks where the held input points, not along the old route.
    TickWith(*world.host, WALKED + 2, core::ActionKind::ToggleWorldPause, HELD_RIGHT);
    const core::Vec2 walked = there + core::Vec2{ .x = test::SmallData().cultivator.walkSpeed };
    CHECK(SamePoint(BodyOf(*world.host, world.player).previous, there));
    CHECK(SamePoint(BodyOf(*world.host, world.player).current, walked));
}

/// A world loaded `before` ticks ahead of the first month's end with the player's cultivator walking a route along the
/// first road, set out on tick 1, and that route's leg.
std::pair<TravelWorld, core::Leg> WalkingBeforeMonthEnd(u64 before) {
    TravelWorld world = TravelWorldOf({}, true, game::TICKS_PER_MONTH - before);
    const core::WorldMap& map = *world.map;
    const core::Leg leg = LegTo(
        map, FootOf(*world.host, world.player), map.TileCentre(RouteRoad(map)[ROUTE_TILES]), game::TICKS_PER_MONTH - before
    );
    REQUIRE(core::EndTick(leg) > static_cast<i64>(game::TICKS_PER_MONTH + 1)); // still walking past the step
    WalkTo(*world.host, 1, map.TileCentre(leg.tiles.back()));
    return { std::move(world), leg };
}

TEST_CASE("the automatic month step and an early one carry the cultivator on along its route") {
    constexpr u64 MONTH = game::TICKS_PER_MONTH;
    constexpr u64 BEFORE_MONTH = 40;
    {
        // The clock reaches the month's end on tick BEFORE_MONTH and waits there for the step.
        const auto [world, leg] = WalkingBeforeMonthEnd(BEFORE_MONTH);
        const core::WorldMap& map = *world.map;
        TickRange(*world.host, 2, BEFORE_MONTH);
        REQUIRE(world.host->MonthDue());
        const core::Vec2 due = core::Where(map, leg, static_cast<i64>(MONTH));
        TickRange(*world.host, BEFORE_MONTH + 1, BEFORE_MONTH + 2);
        CHECK(SamePoint(BodyOf(*world.host, world.player).previous, due)); // it stands while the step is due
        CHECK(SamePoint(BodyOf(*world.host, world.player).current, due));
        CHECK(world.host->PlayerRoute().has_value());
        StepMonth(*world.host, BEFORE_MONTH + 3);
        CHECK(SamePoint(FootOf(*world.host, world.player), due));
        TickRange(*world.host, BEFORE_MONTH + 3, BEFORE_MONTH + 3);
        CHECK(SamePoint(BodyOf(*world.host, world.player).previous, due));
        CHECK(SamePoint(BodyOf(*world.host, world.player).current, core::Where(map, leg, static_cast<i64>(MONTH + 1))));
    }
    {
        // M mid-route, unpaused, lands it where the new tick puts it, still on its way.
        const auto [world, leg] = WalkingBeforeMonthEnd(BEFORE_MONTH);
        const core::WorldMap& map = *world.map;
        TickRange(*world.host, 2, WALKED);
        StepMonth(*world.host, WALKED + 1);
        REQUIRE(world.host->WorldTicks() == MONTH);
        CHECK_FALSE(world.host->WorldPaused());
        const core::Vec2 stepped = core::Where(map, leg, static_cast<i64>(MONTH));
        CHECK(SamePoint(BodyOf(*world.host, world.player).previous, stepped));
        CHECK(SamePoint(BodyOf(*world.host, world.player).current, stepped));
        CHECK(world.host->PlayerRoute().has_value());
        TickRange(*world.host, WALKED + 1, WALKED + 1);
        CHECK(SamePoint(BodyOf(*world.host, world.player).previous, stepped));
        CHECK(SamePoint(BodyOf(*world.host, world.player).current, core::Where(map, leg, static_cast<i64>(MONTH + 1))));
    }
}

TEST_CASE("a month step carries the cultivator on along its route, and the pause holds through it") {
    constexpr u64 MONTH = game::TICKS_PER_MONTH;
    constexpr u64 BEFORE_MONTH = 40;
    const TravelWorld world = TravelWorldOf({}, true, MONTH - BEFORE_MONTH);
    const core::WorldMap& map = *world.map;
    const core::Vec2 start = FootOf(*world.host, world.player);
    const core::Leg leg = LegTo(map, start, map.TileCentre(RouteRoad(map)[ROUTE_TILES]), MONTH - BEFORE_MONTH);
    REQUIRE(core::EndTick(leg) > static_cast<i64>(MONTH + 1)); // still walking past the step
    REQUIRE(core::EndTick(leg) < static_cast<i64>(2 * MONTH));
    WalkTo(*world.host, 1, map.TileCentre(leg.tiles.back()));

    // M while paused: the clock jumps, the pause holds, and the cultivator stands where the new tick puts it, not streaking
    // there.
    TickWith(*world.host, 2, core::ActionKind::ToggleWorldPause);
    StepMonth(*world.host, 3);
    REQUIRE(world.host->WorldTicks() == MONTH);
    CHECK(world.host->WorldPaused());
    const core::Vec2 stepped = core::Where(map, leg, static_cast<i64>(MONTH));
    CHECK(SamePoint(BodyOf(*world.host, world.player).previous, stepped));
    CHECK(SamePoint(BodyOf(*world.host, world.player).current, stepped));
    CHECK(world.host->PlayerRoute().has_value());
    TickRange(*world.host, 3, 3);
    CHECK(SamePoint(FootOf(*world.host, world.player), stepped));

    // Resumed, it walks on from there.
    TickWith(*world.host, 4, core::ActionKind::ToggleWorldPause);
    CHECK(SamePoint(BodyOf(*world.host, world.player).previous, stepped));
    CHECK(SamePoint(BodyOf(*world.host, world.player).current, core::Where(map, leg, static_cast<i64>(MONTH + 1))));

    // M past the route's end leaves it on the last tile, the route done.
    StepMonth(*world.host, 4);
    REQUIRE(world.host->WorldTicks() == 2 * MONTH);
    CHECK(SamePoint(BodyOf(*world.host, world.player).previous, map.TileCentre(leg.tiles.back())));
    CHECK(SamePoint(BodyOf(*world.host, world.player).current, map.TileCentre(leg.tiles.back())));
    CHECK_FALSE(world.host->PlayerRoute().has_value());
}

TEST_CASE("leaving the world map drops the route") {
    const TravelWorld world = TravelWorldOf({}, true);
    const core::WorldMap& map = *world.map;
    WalkTo(*world.host, 1, map.TileCentre(RouteRoad(map)[ROUTE_TILES]));
    TickRange(*world.host, 2, WALKED);
    REQUIRE(world.host->PlayerRoute().has_value());
    const u64 clock = world.host->WorldTicks();

    TickWith(*world.host, WALKED + 1, core::ActionKind::CycleScene);
    world.host->EnterScene();
    REQUIRE(world.host->Current().key == game::ARENA_SCENE);
    CHECK_FALSE(world.host->PlayerRoute().has_value());
    TickRange(*world.host, WALKED + 2, WALKED + 3);

    // Back round to the world map: still none, and the clock stood still all the way.
    u64 tick = WALKED + 4;
    while (world.host->Current().key != game::WORLD_SCENE) {
        TickWith(*world.host, tick++, core::ActionKind::CycleScene);
        world.host->EnterScene();
    }
    CHECK_FALSE(world.host->PlayerRoute().has_value());
    CHECK(world.host->WorldTicks() == clock);
    CHECK(Published(*world.host).route.empty());
}

/// The small test data with the cultivator's sight cut to `sight`, so a walk along its top row leaves tiles unexplored.
core::GameData SmallDataSeeing(u32 sight) {
    core::GameData data = test::SmallData();
    data.cultivator.sight = sight;
    return data;
}

using test::Diamonds;
using test::ExploredCount;
using test::ExploredOf;

/// A rectangle of no size on `tile`'s centre of `map`: Host::Explore() of it explores that tile alone.
core::Rect OnTile(const core::WorldMap& map, u32 tile) {
    const core::Vec2 centre = map.TileCentre(tile);
    return { .min = centre, .max = centre };
}

TEST_CASE("a new game explores the start diamond and nothing else") {
    const core::GameData data = test::SmallData();
    const core::WorldMap& map = *data.world;
    const game::Host host(test::TestManifest(), game::WORLD_SCENE, data);
    CHECK(host.SightRadius() == data.cultivator.sight);
    const std::vector<u32> start{ map.TileAt(map.StartCentre()) };
    const std::vector<u64> diamond = Diamonds(map, start, data.cultivator.sight);
    CHECK(ExploredOf(host) == diamond);
    // The start city lies in the map's corner, so the diamond is clipped to the 9 tiles within 3 of it.
    CHECK(ExploredCount(host.ExploredTiles()) == 9);

    // A game that has never shown the map has explored nothing, and one without the map has no tiles to explore.
    const game::Host arena(test::TestManifest(), game::ARENA_SCENE, data);
    CHECK(ExploredCount(arena.ExploredTiles()) == 0);
    CHECK(arena.ExploredTiles().size() == core::FogWords(test::small::TILES));
    const game::Host mapless(test::TestManifest());
    CHECK(mapless.ExploredTiles().empty());
    CHECK(mapless.SightRadius() == 0);
}

TEST_CASE("walking a route explores within sight of every tile on it") {
    const core::GameData data = SmallDataSeeing(1);
    const core::WorldMap& map = *data.world;
    game::Host host(test::TestManifest(), game::WORLD_SCENE, data);
    WalkTo(host, 1, map.TileCentre(test::small::COLUMNS - 1)); // along the top row to its far end
    const core::Leg leg = RouteLegOf(host);
    REQUIRE(leg.tiles.size() == test::small::COLUMNS);
    const auto end = static_cast<u64>(core::EndTick(leg));

    // As its foot first stands on the route's fourth tile: round the tiles it passed and that one, nothing ahead.
    constexpr std::size_t FOURTH = 3;
    u64 tick = 2;
    for (; tick <= end && map.TileAt(FootOf(host, host.Player())) != leg.tiles[FOURTH]; ++tick) {
        TickRange(host, tick, tick);
    }
    REQUIRE(map.TileAt(FootOf(host, host.Player())) == leg.tiles[FOURTH]);
    CHECK(ExploredOf(host) == Diamonds(map, std::span{ leg.tiles }.first(FOURTH + 1), 1));

    // At its end: round every tile of the route, and the bottom row beyond sight left under cloud.
    TickRange(host, tick, end);
    REQUIRE_FALSE(host.PlayerRoute().has_value());
    CHECK(ExploredOf(host) == Diamonds(map, leg.tiles, 1));
    CHECK_FALSE(core::IsExplored(host.ExploredTiles(), map.TileIndex({ .column = 0, .row = test::small::ROWS - 1 })));
}

TEST_CASE("a month's jump along a route explores every tile it passes") {
    constexpr u64 MONTH = game::TICKS_PER_MONTH;
    constexpr std::size_t FOURTH = 3;
    // The walk sets out a little before the month's end, so the jump carries it past its fourth tile's centre, a quarter
    // of the way on to the fifth: several tiles in one go, and still short of the end.
    const core::GameData probeData = test::SmallData();
    const core::WorldMap& probeMap = *probeData.world;
    const core::Leg probe = LegTo(probeMap, probeMap.StartCentre(), probeMap.TileCentre(test::small::COLUMNS - 1), 0);
    REQUIRE(probe.tiles.size() > FOURTH + 1);
    const u64 walked = probe.arrive[FOURTH] + ((probe.arrive[FOURTH + 1] - probe.arrive[FOURTH]) / 4);
    const TravelWorld world = TravelWorldOf({}, true, MONTH - walked);
    const core::WorldMap& map = *world.map;
    world.host->SetSightRadius(1);
    WalkTo(*world.host, 1, map.TileCentre(test::small::COLUMNS - 1));
    const core::Leg leg = RouteLegOf(*world.host);
    REQUIRE(leg.tiles == probe.tiles);
    const std::vector<u32> first{ leg.tiles.front() };
    REQUIRE(ExploredOf(*world.host) == Diamonds(map, first, 1));

    StepMonth(*world.host, 2);
    REQUIRE(world.host->WorldTicks() == MONTH);
    REQUIRE(map.TileAt(FootOf(*world.host, world.player)) == leg.tiles[FOURTH]);
    CHECK(ExploredOf(*world.host) == Diamonds(map, std::span{ leg.tiles }.first(FOURTH + 1), 1));

    // The next jump carries it to the route's end, round every tile of it.
    StepMonth(*world.host, 3);
    REQUIRE_FALSE(world.host->PlayerRoute().has_value());
    CHECK(ExploredOf(*world.host) == Diamonds(map, leg.tiles, 1));
}

TEST_CASE("a grown sight radius explores the new ring at the next tick") {
    const core::GameData data = SmallDataSeeing(1);
    const core::WorldMap& map = *data.world;
    game::Host host(test::TestManifest(), game::WORLD_SCENE, data);
    const std::vector<u32> start{ map.TileAt(map.StartCentre()) };
    const std::vector<u64> before = ExploredOf(host);
    const u64 version = host.FogVersion();

    host.SetSightRadius(2);
    CHECK(host.SightRadius() == 2);
    CHECK(ExploredOf(host) == before); // not until the tick
    CHECK(host.FogVersion() == version);
    TickRange(host, 1, 1);
    CHECK(ExploredOf(host) == Diamonds(map, start, 2));
    CHECK(host.FogVersion() > version);

    // A tick that uncovers nothing new leaves the fog's version alone.
    const u64 grown = host.FogVersion();
    TickRange(host, 2, 2);
    CHECK(host.FogVersion() == grown);
}

TEST_CASE("a dev scene's world has no explored tiles and leaves the game's alone") {
    game::Host host(test::TestManifest(), game::WORLD_SCENE, test::SmallData());
    const std::vector<u64> explored = ExploredOf(host);
    REQUIRE(ExploredCount(explored) > 0);
    TickWith(host, 1, core::ActionKind::CycleScene);
    host.EnterScene();
    REQUIRE(host.Current().key == game::ARENA_SCENE);
    REQUIRE(host.KeptScratchBase() != nullptr);
    CHECK(host.KeptScratchBase()->explored.empty());
    CHECK(host.KeptScratchBase()->sightRadius == 0);

    // The cultivator walks the arena, and the game's world, set aside, keeps what it had explored.
    constexpr u64 WALK = 20;
    TickRange(host, 2, 1 + WALK, HELD_RIGHT);
    CHECK(ExploredOf(host) == explored);
}

TEST_CASE("coming back to the world map explores round where the cultivator lands") {
    const core::GameData data = SmallDataSeeing(1);
    const core::WorldMap& map = *data.world;
    // A game in the arena whose world, set aside, has explored nothing and keeps a place on the bottom row's far side.
    const std::vector<u32> landing{ map.TileIndex({ .column = test::small::COLUMNS - 2, .row = test::small::ROWS - 1 }) };
    const core::State state{ game::DOMAINS, test::EMPTY_SAVE_SEED };
    const std::vector<std::byte> bytes = test::ArenaSave(
        state,
        {},
        std::nullopt,
        std::nullopt,
        std::nullopt,
        game::ARENA_SCENE,
        test::WORLD_VERSION,
        test::ACTIVE_VERSION,
        {},
        game::Travel{},
        std::nullopt,
        game::Relationships{},
        { .place = map.TileCentre(landing.front()) },
        &data
    );
    game::Host host(test::TestManifest(), game::ARENA_SCENE, data);
    core::Reader reader{ bytes };
    REQUIRE(host.Load(reader, 0));
    host.EnterScene();
    REQUIRE(ExploredCount(host.ExploredTiles()) == 0);

    // Entered, before any tick: round the tile it lands on, and nothing round the start city.
    u64 tick = 1;
    while (host.Current().key != game::WORLD_SCENE) {
        TickWith(host, tick++, core::ActionKind::CycleScene);
        host.EnterScene();
    }
    const u64 version = host.FogVersion();
    REQUIRE(map.TileAt(FootOf(host, host.Player())) == landing.front());
    CHECK(ExploredOf(host) == Diamonds(map, landing, 1));
    CHECK_FALSE(core::IsExplored(host.ExploredTiles(), map.TileAt(map.StartCentre())));

    // Each scene entered counts a new fog version, as the publish must hand the fog on again.
    TickWith(host, tick++, core::ActionKind::CycleScene);
    host.EnterScene();
    CHECK(host.FogVersion() > version);
}

TEST_CASE("leaving the world map through its portal and coming back keeps the explored tiles and explores the landing") {
    const core::GameData data = SmallDataSeeing(1);
    const core::WorldMap& map = *data.world;
    game::Host host(test::TestManifest(), game::WORLD_SCENE, data);
    std::vector<core::Event> events;
    // A step out to a tile of its own, then one onto the portal, which keeps that tile's centre as where it comes back.
    const std::vector<u32> landing{ map.TileIndex({ .column = 3, .row = 1 }) };
    StepTo(host, 1, map.TileCentre(landing.front()), events);
    const std::vector<u64> before = ExploredOf(host);
    REQUIRE(core::IsExplored(before, landing.front()));
    const std::optional<core::Vec2> worldPortal = host.Portal();
    REQUIRE(worldPortal.has_value());
    StepTo(host, 2, worldPortal.value_or(core::Vec2{}), events);
    REQUIRE(host.Requested().has_value());
    host.EnterScene();
    REQUIRE(host.Current().key == game::ARENA_SCENE);
    CHECK(ExploredOf(host) == before);

    // Its sight grows while it is away, then the arena's and the stress arena's portals lead back to the world.
    host.SetSightRadius(2);
    u64 tick = 3;
    while (host.Current().key != game::WORLD_SCENE) {
        const std::optional<core::Vec2> portal = host.Portal();
        REQUIRE(portal.has_value());
        StepTo(host, tick++, portal.value_or(core::Vec2{}), events);
        REQUIRE(host.Requested().has_value());
        host.EnterScene();
    }
    REQUIRE(map.TileAt(FootOf(host, host.Player())) == landing.front());

    // Entered, before any tick: what it had explored, and the grown diamond round where it landed.
    std::vector<u64> expected = Diamonds(map, landing, 2);
    for (std::size_t word = 0; word < expected.size(); ++word) {
        expected[word] |= before[word];
    }
    CHECK(ExploredOf(host) == expected);
}

TEST_CASE("the world's hash changes with an explored tile and with the radius") {
    const core::GameData data = SmallDataSeeing(1);
    const core::WorldMap& map = *data.world;
    game::Host host(test::TestManifest(), game::WORLD_SCENE, data);
    const u32 far = map.TileIndex({ .column = test::small::COLUMNS - 1, .row = test::small::ROWS - 1 });
    REQUIRE_FALSE(core::IsExplored(host.ExploredTiles(), far));
    const u64 unexplored = host.WorldHash();
    const u64 version = host.FogVersion();

    host.Explore(OnTile(map, far));
    CHECK(core::IsExplored(host.ExploredTiles(), far));
    CHECK(ExploredCount(host.ExploredTiles()) == 4); // the start's diamond of 3, and that tile alone
    const u64 explored = host.WorldHash();
    CHECK(explored != unexplored);
    CHECK(host.FogVersion() > version);

    // A tile explored already changes nothing.
    const u64 exploredVersion = host.FogVersion();
    host.Explore(OnTile(map, far));
    CHECK(host.WorldHash() == explored);
    CHECK(host.FogVersion() == exploredVersion);

    host.SetSightRadius(2);
    CHECK(host.WorldHash() != explored);
}

TEST_CASE("exploring an area explores every tile it covers, edges included, and none off the map") {
    const core::GameData data = SmallDataSeeing(0);
    const core::WorldMap& map = *data.world;
    game::Host host(test::TestManifest(), game::WORLD_SCENE, data);
    REQUIRE(ExploredCount(host.ExploredTiles()) == 1); // the start's own tile

    // From the bottom row's second tile's centre to the third's left edge: both tiles.
    const u32 second = map.TileIndex({ .column = 1, .row = test::small::ROWS - 1 });
    const u32 third = map.TileIndex({ .column = 2, .row = test::small::ROWS - 1 });
    const core::Vec2 edge{ .x = 2.0F * test::small::TILE, .y = map.TileCentre(third).y };
    host.Explore({ .min = map.TileCentre(second), .max = edge });
    CHECK(core::IsExplored(host.ExploredTiles(), second));
    CHECK(core::IsExplored(host.ExploredTiles(), third));
    CHECK(ExploredCount(host.ExploredTiles()) == 3);

    // An area wholly off the map, or not a number, explores nothing.
    host.Explore({ .min = { .x = -2.0F * test::small::TILE, .y = 0.0F }, .max = { .x = -test::small::TILE, .y = 0.0F } });
    constexpr f32 NOT_A_NUMBER = std::numeric_limits<f32>::quiet_NaN();
    host.Explore({ .min = { .x = NOT_A_NUMBER, .y = 0.0F }, .max = { .x = NOT_A_NUMBER, .y = 0.0F } });
    CHECK(ExploredCount(host.ExploredTiles()) == 3);
}

TEST_CASE("walkers beyond sight's ring aren't published, those in sight and on the ring are") {
    const Road road = FirstRoad(*test::SmallWorld());
    const auto clock = static_cast<i64>(TRAVEL_CLOCK);
    // Each stands on the top row's tile 3, 4 or 5 columns from the cultivator's, the first: in sight, on the ring just
    // beyond it, and past that.
    constexpr std::array<u32, 3> COLUMNS{ 3, 4, 5 };
    const std::array<Departure, 3> departures{ {
        { .tick = clock - i64{ road.arrive.at(COLUMNS[0]) } },
        { .tick = clock - i64{ road.arrive.at(COLUMNS[1]) } },
        { .tick = clock - i64{ road.arrive.at(COLUMNS[2]) } },
    } };
    const TravelWorld world = TravelWorldOf(departures, true);
    const core::WorldMap& map = *world.map;
    REQUIRE(world.host->SightRadius() == 3);
    REQUIRE(map.TileAt(FootOf(*world.host, world.player)) == map.TileIndex({ .column = 0, .row = 0 }));
    for (std::size_t index = 0; index < COLUMNS.size(); ++index) {
        const core::Vec2 foot = core::Where(map, world.journeys[index], clock);
        REQUIRE(map.TileAt(foot) == map.TileIndex({ .column = COLUMNS[index], .row = 0 }));
    }
    const core::Snapshot snapshot = Published(*world.host, world.host->View());
    CHECK(Shows(snapshot, world.journeys[0].members.front()));
    CHECK(Shows(snapshot, world.journeys[1].members.front()));
    CHECK_FALSE(Shows(snapshot, world.journeys[2].members.front()));
}

TEST_CASE("with the debug sight every walker in view is published") {
    const Road road = FirstRoad(*test::SmallWorld());
    const auto clock = static_cast<i64>(TRAVEL_CLOCK);
    // One just setting out beside the cultivator, one 5 tiles off, past sight's ring.
    const std::array<Departure, 2> departures{ { { .tick = clock - JUST_LEFT },
                                                 { .tick = clock - i64{ road.arrive.at(5) } } } };
    const TravelWorld world = TravelWorldOf(departures, true);
    const core::StableId near = world.journeys[0].members.front();
    const core::StableId far = world.journeys[1].members.front();
    REQUIRE_FALSE(Shows(Published(*world.host, world.host->View()), far));

    world.host->SetDebugSight(true);
    const core::Snapshot whole = Published(*world.host, world.host->View());
    CHECK(Shows(whole, near));
    CHECK(Shows(whole, far));
    CHECK(whole.fog.debugSight);
    // The view still culls.
    const core::Snapshot home = Published(*world.host, NearHome(*world.map));
    CHECK(Shows(home, near));
    CHECK_FALSE(Shows(home, far));

    // With no cultivator to see them, only the debug sight shows them.
    const TravelWorld alone = TravelWorldOf(departures);
    CHECK(Shows(Published(*alone.host, alone.host->View()), alone.journeys[1].members.front()));
    alone.host->SetDebugSight(false);
    const core::Snapshot unseen = Published(*alone.host, alone.host->View());
    CHECK_FALSE(Shows(unseen, alone.journeys[0].members.front()));
    CHECK_FALSE(Shows(unseen, alone.journeys[1].members.front()));
    CHECK(unseen.fog.on);
    CHECK(unseen.fog.playerTile == std::nullopt);
}

TEST_CASE("walkers and their shadows and labels are flagged to fade with sight") {
    const std::array<Departure, 1> departures{ { { .tick = static_cast<i64>(TRAVEL_CLOCK) - JUST_LEFT } } };
    const TravelWorld world = TravelWorldOf(departures, true);
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::StableId traveller = world.journeys.front().members.front();
    const core::Vec2 foot = core::Where(*world.map, world.journeys.front(), static_cast<i64>(TRAVEL_CLOCK));
    const core::Snapshot snapshot = Published(*world.host, NearHome(*world.map));

    CHECK(InstanceOf(snapshot, traveller).flags == core::INSTANCE_SIGHT_FADE);
    const core::Instance shadow = core::MakeInstance(
        *manifest, { .sprite = SpriteId::Shadow, .previous = foot, .current = foot, .flags = core::INSTANCE_SIGHT_FADE }
    );
    CHECK(std::ranges::any_of(snapshot.instances, [&](const core::Instance& drawn) { return SameInstance(drawn, shadow); }));
    const core::TextItem label = LabelOf(snapshot, traveller);
    CHECK(label.fog == core::TextFog::Sight);
    CHECK(label.current.y + label.height == doctest::Approx(foot.y)); // anchor and height reach its foot

    // The cultivator, whose sight it is, never fades.
    CHECK(InstanceOf(snapshot, world.player).flags == 0);
    CHECK(LabelOf(snapshot, world.player).fog == core::TextFog::None);
}

TEST_CASE("the fog in the snapshot follows exploring") {
    constexpr u64 MOST_TICKS = 1'000; // far more than a tile's walk
    const core::GameData data = SmallDataSeeing(1);
    const core::WorldMap& map = *data.world;
    game::Host host(test::TestManifest(), game::WORLD_SCENE, data);
    const u32 start = map.TileIndex({ .column = 0, .row = 0 });
    const u32 next = map.TileIndex({ .column = 1, .row = 0 });
    const core::Snapshot first = Published(host);
    CHECK(first.fog.on);
    CHECK(first.fog.version == host.FogVersion());
    CHECK(std::ranges::equal(first.fog.explored, host.ExploredTiles()));
    CHECK(first.fog.playerTile == std::optional{ start });
    CHECK(first.fog.sightRadius == 1);
    CHECK_FALSE(first.fog.debugSight);

    // A step onto the next tile uncovers the one beyond it.
    u64 tick = 1;
    for (; tick <= MOST_TICKS && map.TileAt(FootOf(host, host.Player())) != next; ++tick) {
        TickRange(host, tick, tick, HELD_RIGHT);
    }
    REQUIRE(map.TileAt(FootOf(host, host.Player())) == next);
    const core::Snapshot onward = Published(host);
    CHECK(onward.fog.version > first.fog.version);
    CHECK(std::ranges::equal(onward.fog.explored, host.ExploredTiles()));
    CHECK(onward.fog.explored != first.fog.explored);
    CHECK(onward.fog.playerTile == std::optional{ next });

    // A step back within explored land uncovers nothing.
    const u64 back = tick + MOST_TICKS;
    for (; tick <= back && map.TileAt(FootOf(host, host.Player())) != start; ++tick) {
        TickRange(host, tick, tick, HELD_LEFT);
    }
    REQUIRE(map.TileAt(FootOf(host, host.Player())) == start);
    const core::Snapshot home = Published(host);
    CHECK(home.fog.version == onward.fog.version);
    CHECK(home.fog.explored == onward.fog.explored);
    CHECK(home.fog.playerTile == std::optional{ start });
}

TEST_CASE("debug sight changes no state: the same ticks hash alike with it on and off") {
    constexpr u64 TICKS = 300;
    // The ticks the debug sight goes on and off again for one host, mid-route with the walker selected.
    constexpr u64 SIGHT_ON = 100;
    constexpr u64 SIGHT_OFF = 200;
    const std::array<Departure, 1> departures{ { { .tick = static_cast<i64>(TRAVEL_CLOCK) - JUST_LEFT } } };
    const TravelWorld seen = TravelWorldOf(departures, true);
    const TravelWorld shown = TravelWorldOf(departures, true);
    const core::WorldMap& map = *seen.map;
    const core::StableId chosen = seen.journeys.front().members.front();
    // The cultivator walks down to the bottom row while the walker it selects heads east along the top: out of sight.
    const core::Vec2 target = map.TileCentre(map.TileIndex({ .column = 0, .row = test::small::ROWS - 1 }));
    const std::array<core::Action, 1> select{ { { .kind = core::ActionKind::Select, .target = chosen } } };
    for (game::Host* const host : { seen.host.get(), shown.host.get() }) {
        host->SetSightRadius(1);
        WalkTo(*host, 1, target);
        std::vector<core::Event> events;
        host->Tick(At(2), { .actions = select, .held = {} }, events);
        for (u64 tick = 3; tick <= TICKS; ++tick) {
            if (host == shown.host.get() && (tick == SIGHT_ON || tick == SIGHT_OFF)) {
                host->SetDebugSight(tick == SIGHT_ON);
            }
            TickRange(*host, tick, tick);
            static_cast<void>(Published(*host, host->View()));
        }
    }
    CHECK(seen.host->StateHash() == shown.host->StateHash());
    CHECK(seen.host->Selected() == chosen);
    CHECK(shown.host->Selected() == chosen);
    // Out of sight, the walker is kept selected though it isn't drawn, and the debug sight draws it again.
    CHECK_FALSE(Shows(Published(*seen.host, seen.host->View()), chosen));
    CHECK_FALSE(Shows(Published(*shown.host, shown.host->View()), chosen));
    shown.host->SetDebugSight(true);
    CHECK(Shows(Published(*shown.host, shown.host->View()), chosen));
}

} // namespace
