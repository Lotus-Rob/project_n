#include "game/scenes/stress.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"
#include "game/npcs.hpp"
#include "game/scenes/arena_layout.hpp"

namespace game {

namespace {

using core::atlas::SpriteId;

constexpr SceneTraits STRESS_TRAITS{
    .name = "stress",
    .view = GROUND,
    .home = ARENA_START,
    .creatures = true,
    .camera = core::CameraKind::Follow,
    .worldNpcs = 50'000,
    .portal = ARENA_PORTAL,
};

// A crowd on a grid over the whole ground, each member circling its own spot, and bullets on the arms of a spiral
// turning around the cultivator's start.
constexpr u32 CROWD_COLUMNS = 300;
constexpr u32 CROWD_ROWS = 300;
constexpr f32 CROWD_RADIUS = 8.0F;
constexpr f32 CROWD_WOBBLE = 2.0F;
constexpr f32 CROWD_SPEED = 0.01F;
constexpr f32 CROWD_SCALE = 0.15F;     // a fox about 17 pixels wide: crowds this large are small sprites (§8.1)
constexpr f32 GOLDEN_ANGLE = 2.39996F; // spreads the crowd's phases evenly
constexpr u32 SPIRAL_ARMS = 5;
constexpr u32 BULLETS_PER_ARM = 2'000;
constexpr f32 SPIRAL_INNER = 60.0F;
constexpr f32 SPIRAL_STEP = 0.25F;   // world units outward per bullet along an arm
constexpr f32 SPIRAL_TWIST = 0.004F; // radians per bullet along an arm: an arm winds about 1.3 times
constexpr f32 SPIRAL_SPEED = 0.01F;
constexpr f32 BULLET_SCALE = 0.5F;

void SpawnOnOrbit(SceneContext& context, const Orbit& orbit, const Look& look) {
    const core::Entity entity = context.Spawn(OrbitPoint(orbit, 0), look);
    context.State()[ACTIVE].Entities().emplace<Orbit>(entity, orbit);
}

void SpawnCrowd(SceneContext& context) {
    for (u32 row = 0; row < CROWD_ROWS; ++row) {
        for (u32 column = 0; column < CROWD_COLUMNS; ++column) {
            const u32 member = (row * CROWD_COLUMNS) + column;
            const Orbit orbit{
                .centre = { .x = (static_cast<f32>(column) + 0.5F) * GROUND.max.x / static_cast<f32>(CROWD_COLUMNS),
                            .y = (static_cast<f32>(row) + 0.5F) * GROUND.max.y / static_cast<f32>(CROWD_ROWS) },
                .radius = CROWD_RADIUS,
                .wobble = CROWD_WOBBLE,
                .phase = GOLDEN_ANGLE * static_cast<f32>(member),
                .speed = member % 2 == 0 ? CROWD_SPEED : -CROWD_SPEED,
            };
            const Look look{ .sprite = member % 2 == 0 ? SpriteId::Fox : SpriteId::Cultivator, .scale = CROWD_SCALE };
            SpawnOnOrbit(context, orbit, look);
        }
    }
}

void SpawnSpiral(SceneContext& context) {
    for (u32 arm = 0; arm < SPIRAL_ARMS; ++arm) {
        for (u32 step = 0; step < BULLETS_PER_ARM; ++step) {
            const Orbit orbit{
                .centre = ARENA_START,
                .radius = SPIRAL_INNER + (SPIRAL_STEP * static_cast<f32>(step)),
                .phase =
                    (TURN * static_cast<f32>(arm) / static_cast<f32>(SPIRAL_ARMS)) + (SPIRAL_TWIST * static_cast<f32>(step)),
                .speed = SPIRAL_SPEED,
            };
            const Look look{
                .sprite = arm % 2 == 0 ? SpriteId::OrbBlue : SpriteId::OrbRed,
                .layer = core::Layer::Effects,
                .pipeline = core::Pipeline::Additive,
                .scale = BULLET_SCALE,
            };
            SpawnOnOrbit(context, orbit, look);
        }
    }
}

} // namespace

const SceneTraits& StressScene::Traits() const {
    return STRESS_TRAITS;
}

void StressScene::Build(SceneContext& context) const {
    BuildArenaLayout(context);
    SpawnCrowd(context);
    SpawnSpiral(context);
    SpawnArenaNpcs(context);
}

void StressScene::Respawn(SceneContext& context, const core::TickCounters& counters, u32 index) const {
    RespawnFox(context, counters, index);
}

void StressScene::Publish(const SceneContext& context, core::Rect /*view*/, core::Snapshot& snapshot) const {
    PN_PROFILE_ZONE();
    PublishCaption(context, snapshot);
}

} // namespace game
