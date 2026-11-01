#include "game/arena.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/math.hpp"
#include "core/profile.hpp"

namespace game {

namespace {

using core::atlas::SpriteId;

struct Prop {
    SpriteId sprite = SpriteId{};
    core::Vec2 foot{};
};

// Laid out on the 1920x1080 ground, which the camera shows whole.
constexpr std::array PROPS{
    Prop{ .sprite = SpriteId::Shrine, .foot = { .x = 960.0F, .y = 330.0F } },
    Prop{ .sprite = SpriteId::Pine, .foot = { .x = 290.0F, .y = 430.0F } },
    Prop{ .sprite = SpriteId::Pine, .foot = { .x = 1660.0F, .y = 400.0F } },
    Prop{ .sprite = SpriteId::Pine, .foot = { .x = 1500.0F, .y = 960.0F } },
    Prop{ .sprite = SpriteId::RockLarge, .foot = { .x = 610.0F, .y = 780.0F } },
    Prop{ .sprite = SpriteId::RockLarge, .foot = { .x = 1320.0F, .y = 640.0F } },
    Prop{ .sprite = SpriteId::RockSmall, .foot = { .x = 220.0F, .y = 960.0F } },
    Prop{ .sprite = SpriteId::RockSmall, .foot = { .x = 1180.0F, .y = 300.0F } },
};

constexpr core::Vec2 CULTIVATOR{ .x = 960.0F, .y = 620.0F };
constexpr u32 FOXES = 12;
constexpr f32 FOX_RADIUS = 300.0F;
constexpr f32 FOX_WOBBLE = 40.0F; // in and out of the circle, world units
constexpr f32 FOX_SPEED = 0.006F; // radians per tick: a lap in about 17 seconds
constexpr u32 ORBS = 8;
constexpr f32 ORB_RADIUS = 150.0F;
constexpr f32 ORB_SPEED = -0.02F; // anticlockwise, about three times the foxes' pace
constexpr f32 TURN = 2.0F * std::numbers::pi_v<f32>;
constexpr f32 WOBBLES_PER_LAP = 3.0F;
constexpr f32 VIEW_TILT = 0.6F; // circles on the ground look flattened: the view looks down at an angle

// The stress scene: a crowd on a grid over the whole ground, each member circling its own spot,
// and bullets on the arms of a spiral turning around the cultivator.
constexpr u32 CROWD_COLUMNS = 300;
constexpr u32 CROWD_ROWS = 300;
constexpr core::Vec2 CROWD_AREA{ .x = 1920.0F, .y = 1080.0F };
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

} // namespace

core::Vec2 OrbitPoint(const Orbit& orbit, u64 tick) {
    const f32 angle = orbit.phase + (orbit.speed * static_cast<f32>(tick));
    const f32 radius = orbit.radius + (orbit.wobble * core::math::Sin(WOBBLES_PER_LAP * angle));
    return {
        .x = orbit.centre.x + (radius * core::math::Cos(angle)),
        .y = orbit.centre.y + (radius * core::math::Sin(angle) * VIEW_TILT),
    };
}

Arena::Arena(Scene scene) {
    for (const Prop& prop : PROPS) {
        Spawn(prop.foot, { .sprite = prop.sprite });
    }
    Spawn(CULTIVATOR, { .sprite = SpriteId::Cultivator, .shadow = true });
    for (u32 fox = 0; fox < FOXES; ++fox) {
        const Orbit orbit{
            .centre = CULTIVATOR,
            .radius = FOX_RADIUS,
            .wobble = FOX_WOBBLE,
            .phase = TURN * static_cast<f32>(fox) / static_cast<f32>(FOXES),
            .speed = FOX_SPEED,
        };
        m_domain.Entities().emplace<Orbit>(Spawn(OrbitPoint(orbit, 0), { .sprite = SpriteId::Fox, .shadow = true }), orbit);
    }
    for (u32 orb = 0; orb < ORBS; ++orb) {
        const Orbit orbit{
            .centre = CULTIVATOR,
            .radius = ORB_RADIUS,
            .wobble = FOX_WOBBLE,
            .phase = TURN * static_cast<f32>(orb) / static_cast<f32>(ORBS),
            .speed = ORB_SPEED,
        };
        const Look look{
            .sprite = orb % 2 == 0 ? SpriteId::OrbBlue : SpriteId::OrbRed,
            .layer = core::Layer::Effects,
            .pipeline = core::Pipeline::Additive,
        };
        m_domain.Entities().emplace<Orbit>(Spawn(OrbitPoint(orbit, 0), look), orbit);
    }
    if (scene != Scene::Stress) {
        return;
    }
    for (u32 row = 0; row < CROWD_ROWS; ++row) {
        for (u32 column = 0; column < CROWD_COLUMNS; ++column) {
            const u32 member = (row * CROWD_COLUMNS) + column;
            const Orbit orbit{
                .centre = { .x = (static_cast<f32>(column) + 0.5F) * CROWD_AREA.x / static_cast<f32>(CROWD_COLUMNS),
                            .y = (static_cast<f32>(row) + 0.5F) * CROWD_AREA.y / static_cast<f32>(CROWD_ROWS) },
                .radius = CROWD_RADIUS,
                .wobble = CROWD_WOBBLE,
                .phase = GOLDEN_ANGLE * static_cast<f32>(member),
                .speed = member % 2 == 0 ? CROWD_SPEED : -CROWD_SPEED,
            };
            const Look look{ .sprite = member % 2 == 0 ? SpriteId::Fox : SpriteId::Cultivator, .scale = CROWD_SCALE };
            m_domain.Entities().emplace<Orbit>(Spawn(OrbitPoint(orbit, 0), look), orbit);
        }
    }
    for (u32 arm = 0; arm < SPIRAL_ARMS; ++arm) {
        for (u32 step = 0; step < BULLETS_PER_ARM; ++step) {
            const Orbit orbit{
                .centre = CULTIVATOR,
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
            m_domain.Entities().emplace<Orbit>(Spawn(OrbitPoint(orbit, 0), look), orbit);
        }
    }
}

core::Entity Arena::Spawn(core::Vec2 foot, const Look& look) {
    const core::Entity entity = m_domain.Create(m_ids.Allocate());
    m_domain.Entities().emplace<Body>(entity, foot, foot);
    m_domain.Entities().emplace<Look>(entity, look);
    return entity;
}

void Arena::Tick(u64 simulationTick) {
    PN_PROFILE_ZONE();
    for (auto [entity, body, orbit] : m_domain.Entities().view<Body, const Orbit>().each()) {
        body.previous = body.current;
        body.current = OrbitPoint(orbit, simulationTick);
    }
}

void Arena::Publish(core::Snapshot& snapshot) {
    PN_PROFILE_ZONE();
    m_drawList.Begin(
        0.0F
    ); // shortcut: the camera shows the whole ground, so nothing is culled yet; the job-system chunk culls.
    m_drawList.Add(core::Layer::Ground, core::Pipeline::Normal, core::MakeInstance({ .sprite = SpriteId::Ground }));
    for (auto [entity, body, look] : m_domain.Entities().view<const Body, const Look>().each()) {
        if (look.shadow) {
            m_drawList.Add(
                core::Layer::Decals,
                core::Pipeline::Normal,
                core::MakeInstance({ .sprite = SpriteId::Shadow, .previous = body.previous, .current = body.current })
            );
        }
        m_drawList.Add(
            look.layer,
            look.pipeline,
            core::MakeInstance(
                { .sprite = look.sprite, .previous = body.previous, .current = body.current, .scale = look.scale }
            ),
            body.current.y
        );
    }
    m_drawList.Publish(snapshot);
}

} // namespace game
