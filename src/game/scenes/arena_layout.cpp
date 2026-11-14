#include "game/scenes/arena_layout.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/math.hpp"
#include "core/random.hpp"
#include "game/combat.hpp"
#include "game/npcs.hpp"
#include "game/random_purposes.hpp"

namespace game {

namespace {

using core::atlas::SpriteId;

struct LayoutProp {
    SpriteId sprite = SpriteId{};
    core::Vec2 foot{};
    f32 scale = 1.0F;
};

// Laid out on the 1920x1080 ground, which the camera shows whole. The shrine, the stele and the crystal are its one prop
// each of the sprites in ARENA_DEMO_GLOWS.
constexpr std::array PROPS{
    LayoutProp{ .sprite = SpriteId::Shrine, .foot = { .x = 960.0F, .y = 330.0F } },
    LayoutProp{ .sprite = SpriteId::Pine, .foot = { .x = 290.0F, .y = 430.0F } },
    LayoutProp{ .sprite = SpriteId::Pine, .foot = { .x = 1660.0F, .y = 400.0F } },
    LayoutProp{ .sprite = SpriteId::Pine, .foot = { .x = 1500.0F, .y = 960.0F } },
    LayoutProp{ .sprite = SpriteId::RockLarge, .foot = { .x = 610.0F, .y = 780.0F } },
    LayoutProp{ .sprite = SpriteId::RockLarge, .foot = { .x = 1320.0F, .y = 640.0F } },
    LayoutProp{ .sprite = SpriteId::RockSmall, .foot = { .x = 48.0F, .y = 1050.0F } }, // in the corner, off the grand seal
    LayoutProp{ .sprite = SpriteId::RockSmall, .foot = { .x = 1180.0F, .y = 300.0F } },
};

// The stone tablet, with its vertical caption beside it (design doc 0002.4 §2).
constexpr LayoutProp STELE{ .sprite = SpriteId::Stele, .foot = { .x = 760.0F, .y = 980.0F } };
// Game text (the phase 6 chunk 3 spec): an offset in world units, so it scales with the zoom as the text does.
constexpr core::Vec2 CAPTION_OFFSET{ .x = 24.0F, .y = 0.0F }; // clear of the tablet's right edge

// The painted art around the arena's edges, some larger or smaller than painted (design doc 0002.3 §2: every feature
// shows in the arena).
constexpr std::array ARENA_PAINTED{
    LayoutProp{ .sprite = SpriteId::TreantBlighted, .foot = { .x = 1720.0F, .y = 720.0F }, .scale = 1.25F },
    LayoutProp{ .sprite = SpriteId::TreeDead, .foot = { .x = 110.0F, .y = 700.0F }, .scale = 0.7F },
    LayoutProp{ .sprite = SpriteId::Hut, .foot = { .x = 480.0F, .y = 250.0F } },
    LayoutProp{ .sprite = SpriteId::Crane, .foot = { .x = 1250.0F, .y = 960.0F } },
    STELE,
    LayoutProp{ .sprite = SpriteId::FoxSpirit, .foot = { .x = 1450.0F, .y = 260.0F }, .scale = 0.8F },
    LayoutProp{ .sprite = SpriteId::FoxYellow, .foot = { .x = 1000.0F, .y = 1010.0F } },
    LayoutProp{ .sprite = SpriteId::WispRed, .foot = { .x = 130.0F, .y = 320.0F }, .scale = 0.6F },
    LayoutProp{ .sprite = SpriteId::WispRed, .foot = { .x = 175.0F, .y = 345.0F }, .scale = 0.6F },
};
constexpr LayoutProp ARENA_GLOW{ .sprite = SpriteId::OrbGold,
                                 .foot = { .x = 1720.0F, .y = 420.0F } }; // in the treant's branches
constexpr LayoutProp CRYSTAL{ .sprite = SpriteId::CrystalIce, .foot = CRYSTAL_FOOT, .scale = 0.8F };
constexpr f32 CRYSTAL_SPIN = 0.02F; // radians per tick: a turn in about five seconds

constexpr u32 FOXES = 12;
constexpr u32 ORBS = 8;
constexpr f32 ORB_RADIUS = 150.0F;
constexpr f32 ORB_WOBBLE = 40.0F; // in and out of the circle, world units
constexpr f32 ORB_SPEED = -0.02F; // anticlockwise, about three times the foxes' pace

// The NPCs walking about the arena, each in its own map cell.
constexpr std::array<u32, 6> ACTIVE_CELLS{
    { MAP_COLUMNS + 1, MAP_COLUMNS + 4, MAP_COLUMNS + 6, (3 * MAP_COLUMNS) + 2, (3 * MAP_COLUMNS) + 3, (3 * MAP_COLUMNS) + 5 }
};

// A point on the edge of `ground`, `unit` (0 to 1) of the way round it clockwise from its top-left corner.
core::Vec2 EdgePoint(const core::Rect& ground, f64 unit) {
    const f64 width = static_cast<f64>(ground.max.x) - static_cast<f64>(ground.min.x);
    const f64 height = static_cast<f64>(ground.max.y) - static_cast<f64>(ground.min.y);
    constexpr f64 TWICE = 2.0; // round a rectangle passes each side twice
    f64 along = unit * TWICE * (width + height);
    if (along < width) {
        return { .x = ground.min.x + static_cast<f32>(along), .y = ground.min.y };
    }
    along -= width;
    if (along < height) {
        return { .x = ground.max.x, .y = ground.min.y + static_cast<f32>(along) };
    }
    along -= height;
    if (along < width) {
        return { .x = ground.max.x - static_cast<f32>(along), .y = ground.max.y };
    }
    along -= width;
    return { .x = ground.min.x, .y = ground.max.y - static_cast<f32>(std::min(along, height)) };
}

} // namespace

void BuildArenaLayout(SceneContext& context) {
    for (const LayoutProp& prop : PROPS) {
        context.Spawn(prop.foot, { .sprite = prop.sprite });
    }
    for (const LayoutProp& prop : ARENA_PAINTED) {
        context.Spawn(prop.foot, { .sprite = prop.sprite, .shadow = true, .scale = prop.scale });
    }
    context.Spawn(
        ARENA_GLOW.foot, { .sprite = ARENA_GLOW.sprite, .layer = core::Layer::Effects, .pipeline = core::Pipeline::Additive }
    );
    const core::Entity crystal =
        context.Spawn(CRYSTAL.foot, { .sprite = CRYSTAL.sprite, .shadow = true, .scale = CRYSTAL.scale });
    context.State()[ACTIVE].Entities().emplace<Spin>(crystal, Spin{ .speed = CRYSTAL_SPIN });
    context.SpawnPlayer(ARENA_START);
    const core::CreatureData& fox = context.Data().fox;
    for (u32 index = 0; index < FOXES; ++index) {
        const Orbit orbit{
            .centre = ARENA_START,
            .radius = fox.orbitRadius,
            .wobble = fox.orbitWobble,
            .phase = TURN * static_cast<f32>(index) / static_cast<f32>(FOXES),
            .speed = fox.orbitSpeed,
        };
        context.SpawnFox(orbit, OrbitPoint(orbit, 0));
    }
    for (u32 orb = 0; orb < ORBS; ++orb) {
        const Orbit orbit{
            .centre = ARENA_START,
            .radius = ORB_RADIUS,
            .wobble = ORB_WOBBLE,
            .phase = TURN * static_cast<f32>(orb) / static_cast<f32>(ORBS),
            .speed = ORB_SPEED,
        };
        const Look look{
            .sprite = orb % 2 == 0 ? SpriteId::OrbBlue : SpriteId::OrbRed,
            .layer = core::Layer::Effects,
            .pipeline = core::Pipeline::Additive,
        };
        const core::Entity entity = context.Spawn(OrbitPoint(orbit, 0), look);
        context.State()[ACTIVE].Entities().emplace<Orbit>(entity, orbit);
    }
}

void SpawnArenaNpcs(SceneContext& context) {
    for (const u32 cell : ACTIVE_CELLS) {
        Activate(context.State(), SpawnNpc(context.State(), cell));
    }
}

void RespawnFox(SceneContext& context, const core::TickCounters& counters, u32 index) {
    core::State& state = context.State();
    const core::CreatureData& fox = context.Data().fox;
    const u64 random = core::Random(state.Seed(), counters.state, 0, PURPOSE_RESPAWN, index);
    const core::Vec2 edge = EdgePoint(GROUND, core::ToUnit(random));
    core::Vec2 centre = ARENA_START;
    if (fox.orbitsPlayer && context.Player() != core::StableId{}) {
        centre = state[ACTIVE].Entities().get<Body>(state[ACTIVE].Find(context.Player())).current;
    }
    const f64 toward = core::math::Atan2(
        static_cast<f64>(edge.y - centre.y) / static_cast<f64>(VIEW_TILT), static_cast<f64>(edge.x - centre.x)
    );
    const f64 travelled = static_cast<f64>(fox.orbitSpeed) * static_cast<f64>(counters.simulation);
    const Orbit orbit{
        .centre = centre,
        .radius = fox.orbitRadius,
        .wobble = fox.orbitWobble,
        .phase = static_cast<f32>(std::remainder(toward - travelled, 2 * std::numbers::pi)),
        .speed = fox.orbitSpeed,
    };
    const core::Entity entity = context.SpawnFox(orbit, edge);
    state[ACTIVE].Entities().emplace<Arriving>(entity, edge, counters.simulation, context.Data().combat.arriveTicks);
    PN_LOG_DEBUG(
        "tick {}: a fox respawns as entity {} at ({}, {})",
        counters.simulation,
        std::to_underlying(state[ACTIVE].Entities().get<core::StableId>(entity)),
        edge.x,
        edge.y
    );
}

void PublishCaption(const SceneContext& context, core::Snapshot& snapshot) {
    if (context.Player() == core::StableId{}) {
        return;
    }
    const core::SpriteInfo& stele = context.Manifest().Sprite(STELE.sprite);
    const core::Vec2 topRight{ .x = STELE.foot.x + static_cast<f32>(stele.width) - static_cast<f32>(stele.footX),
                               .y = STELE.foot.y - static_cast<f32>(stele.footY) };
    snapshot.texts.push_back(
        {
            .kind = core::TextKind::Caption,
            .string = core::StringId::TabletCaption,
            .previous = topRight,
            .current = topRight,
            .offset = CAPTION_OFFSET,
            .align = core::TextAlign::TopCentre,
            .direction = core::TextDirection::TopToBottom,
        }
    );
}

} // namespace game
