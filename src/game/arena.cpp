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

// Nothing in it moves: the reference pictures need every run to draw the same.
constexpr std::array PAINTED{
    Prop{ .sprite = SpriteId::TreeDead, .foot = { .x = 260.0F, .y = 460.0F } },
    Prop{ .sprite = SpriteId::Crane, .foot = { .x = 620.0F, .y = 280.0F } },
    Prop{ .sprite = SpriteId::Hut, .foot = { .x = 1640.0F, .y = 420.0F } },
    Prop{ .sprite = SpriteId::Stele, .foot = { .x = 500.0F, .y = 720.0F } },
    Prop{ .sprite = SpriteId::TreantBlighted, .foot = { .x = 960.0F, .y = 640.0F } },
    Prop{ .sprite = SpriteId::CrystalIce, .foot = { .x = 1380.0F, .y = 680.0F } },
    Prop{ .sprite = SpriteId::FoxSpirit, .foot = { .x = 1640.0F, .y = 900.0F } },
    Prop{ .sprite = SpriteId::WispRed, .foot = { .x = 240.0F, .y = 960.0F } },
    Prop{ .sprite = SpriteId::WispRed, .foot = { .x = 300.0F, .y = 980.0F } }, // overlaps: semi-transparent ink over ink
    Prop{ .sprite = SpriteId::FoxYellow, .foot = { .x = 740.0F, .y = 930.0F } },
};
constexpr std::array PAINTED_GLOWS{
    Prop{ .sprite = SpriteId::OrbGold, .foot = { .x = 960.0F, .y = 430.0F } }, // over the treant's dark branches
    Prop{ .sprite = SpriteId::OrbBlue, .foot = { .x = 1120.0F, .y = 900.0F } },
    Prop{ .sprite = SpriteId::OrbRed, .foot = { .x = 1160.0F, .y = 900.0F } }, // overlaps: glows add
    Prop{ .sprite = SpriteId::OrbGold, .foot = { .x = 1140.0F, .y = 930.0F } },
    Prop{ .sprite = SpriteId::OrbBlue, .foot = { .x = 1380.0F, .y = 600.0F } }, // over the ice
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

// The stress scene's NPCs: a few walking about the arena, each in its own map cell, and a world of them living over
// the map, a dozen to a cell.
constexpr std::array<u32, 6> ACTIVE_CELLS{
    { MAP_COLUMNS + 1, MAP_COLUMNS + 4, MAP_COLUMNS + 6, (3 * MAP_COLUMNS) + 2, (3 * MAP_COLUMNS) + 3, (3 * MAP_COLUMNS) + 5 }
};
constexpr u32 WORLD_NPCS = 50'000;
constexpr u32 WORLD_CELLS = 4'096;
constexpr u64 SEED = 0x5eed;

constexpr u32 PUBLISH_CHUNK = 4'096; // entities packed per job chunk
// shortcut: the camera shows the whole ground, and lives on the main thread; the view follows it from phase 5.
constexpr core::Rect GROUND_VIEW{ .min = {}, .max = CROWD_AREA };

// A grid around the reference area's middle, wider than the view at the smallest zoom (gfx::MIN_ZOOM).
constexpr u32 CELL_COLUMNS = 32;
constexpr u32 CELL_ROWS = 20;
constexpr f32 GRID_CELL_SIZE = 256.0F;
constexpr core::Vec2 CELLS_MIN{ .x = 960.0F - (CELL_COLUMNS * GRID_CELL_SIZE / 2.0F),
                                .y = 540.0F - (CELL_ROWS * GRID_CELL_SIZE / 2.0F) };
constexpr core::Rect CELLS_VIEW{
    .min = CELLS_MIN,
    .max = { .x = CELLS_MIN.x + (CELL_COLUMNS * GRID_CELL_SIZE), .y = CELLS_MIN.y + (CELL_ROWS * GRID_CELL_SIZE) },
};
constexpr std::array<SpriteId, 5> CELLS{
    { SpriteId::CellPlain, SpriteId::CellForest, SpriteId::CellMountain, SpriteId::CellSnow, SpriteId::CellCity }
};

} // namespace

core::Vec2 OrbitPoint(const Orbit& orbit, u64 tick) {
    // In f64 and reduced to one turn: an f32 angle loses whole ticks within hours, and Sin and Cos are NaN past 2^20
    // quarter turns.
    const f64 angle = std::fmod(
        static_cast<f64>(orbit.phase) + (static_cast<f64>(orbit.speed) * static_cast<f64>(tick)), 2 * std::numbers::pi
    );
    const f64 radius = static_cast<f64>(orbit.radius) +
                       (static_cast<f64>(orbit.wobble) * core::math::Sin(static_cast<f64>(WOBBLES_PER_LAP) * angle));
    return {
        .x = orbit.centre.x + static_cast<f32>(radius * core::math::Cos(angle)),
        .y = orbit.centre.y + static_cast<f32>(radius * core::math::Sin(angle) * static_cast<f64>(VIEW_TILT)),
    };
}

Arena::Arena(Scene scene)
  : m_state{ DOMAINS, SEED }
  , m_view{ scene == Scene::Cells ? CELLS_VIEW : GROUND_VIEW } {
    if (scene == Scene::Cells) {
        SpawnCells();
        return;
    }
    if (scene == Scene::Painted) {
        SpawnPainted();
        return;
    }
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
        m_state[ACTIVE].Entities().emplace<Orbit>(
            Spawn(OrbitPoint(orbit, 0), { .sprite = SpriteId::Fox, .shadow = true }), orbit
        );
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
        m_state[ACTIVE].Entities().emplace<Orbit>(Spawn(OrbitPoint(orbit, 0), look), orbit);
    }
    if (scene == Scene::Stress) {
        SpawnStress();
    }
}

void Arena::SpawnStress() {
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
            m_state[ACTIVE].Entities().emplace<Orbit>(Spawn(OrbitPoint(orbit, 0), look), orbit);
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
            m_state[ACTIVE].Entities().emplace<Orbit>(Spawn(OrbitPoint(orbit, 0), look), orbit);
        }
    }
    for (const u32 cell : ACTIVE_CELLS) {
        Activate(m_state, SpawnNpc(m_state, cell));
    }
    for (u32 npc = 0; npc < WORLD_NPCS; ++npc) {
        SpawnNpc(m_state, npc % WORLD_CELLS);
    }
}

void Arena::SpawnPainted() {
    for (const Prop& prop : PAINTED) {
        Spawn(prop.foot, { .sprite = prop.sprite, .shadow = true });
    }
    for (const Prop& glow : PAINTED_GLOWS) {
        Spawn(glow.foot, { .sprite = glow.sprite, .layer = core::Layer::Effects, .pipeline = core::Pipeline::Additive });
    }
}

void Arena::SpawnCells() {
    for (u32 row = 0; row < CELL_ROWS; ++row) {
        for (u32 column = 0; column < CELL_COLUMNS; ++column) {
            const core::Vec2 corner{ .x = CELLS_MIN.x + (static_cast<f32>(column) * GRID_CELL_SIZE),
                                     .y = CELLS_MIN.y + (static_cast<f32>(row) * GRID_CELL_SIZE) };
            // Steps of 2 by row keep equal cells from lining up in columns.
            const SpriteId sprite = CELLS.at((column + (2 * row)) % CELLS.size());
            Spawn(corner, { .sprite = sprite, .layer = core::Layer::Ground });
        }
    }
}

core::Entity Arena::Spawn(core::Vec2 foot, const Look& look) {
    const core::Entity entity = m_state.Create(ACTIVE).second;
    m_state[ACTIVE].Entities().emplace<Body>(entity, foot, foot);
    m_state[ACTIVE].Entities().emplace<Look>(entity, look);
    return entity;
}

void Arena::Tick(const core::TickCounters& counters) {
    PN_PROFILE_ZONE();
    core::Domain& active = m_state[ACTIVE];
    for (auto [entity, body, orbit] : active.Entities().view<Body, const Orbit>().each()) {
        body.previous = body.current;
        body.current = OrbitPoint(orbit, counters.simulation);
    }
    DecideActive(active, m_state.Seed(), counters.state, counters.simulation);
    WalkActive(active);
}

void Arena::AdvanceCalendar(const core::TickCounters& counters, core::Jobs& jobs, const std::stop_token& stop) {
    m_monthStep.Run(m_state, m_relationships, jobs, counters.state, stop);
}

void Arena::Publish(core::Snapshot& snapshot, core::Jobs& jobs) {
    PN_PROFILE_ZONE();
    const auto& bodies = m_state[ACTIVE].Entities().storage<Body>();
    const auto& looks = m_state[ACTIVE].Entities().storage<Look>();
    const u32 chunks = core::ChunkCount(looks.size(), PUBLISH_CHUNK);
    m_drawList.Begin(m_view, std::max(chunks, 1U));
    m_drawList.Add(0, core::Layer::Ground, core::Pipeline::Normal, core::MakeInstance({ .sprite = SpriteId::Ground }));
    jobs.ParallelFor(chunks, [&](u32 chunk) {
        const std::size_t begin = std::size_t{ chunk } * PUBLISH_CHUNK;
        const std::size_t end = std::min(looks.size(), begin + PUBLISH_CHUNK);
        for (std::size_t i = begin; i < end; ++i) {
            const core::Entity entity = looks[i];
            const Body& body = bodies.get(entity);
            const Look& look = looks.get(entity);
            if (look.shadow) {
                m_drawList.Add(
                    chunk,
                    core::Layer::Decals,
                    core::Pipeline::Normal,
                    core::MakeInstance({ .sprite = SpriteId::Shadow, .previous = body.previous, .current = body.current })
                );
            }
            m_drawList.Add(
                chunk,
                look.layer,
                look.pipeline,
                core::MakeInstance(
                    { .sprite = look.sprite, .previous = body.previous, .current = body.current, .scale = look.scale }
                ),
                body.current.y
            );
        }
    });
    m_drawList.Publish(snapshot, jobs);
}

} // namespace game
