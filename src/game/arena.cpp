#include "game/arena.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/log.hpp"
#include "core/math.hpp"
#include "core/profile.hpp"
#include "core/random.hpp"
#include "game/random_purposes.hpp"

namespace game {

namespace {

using core::atlas::SpriteId;

struct Prop {
    SpriteId sprite = SpriteId{};
    core::Vec2 foot{};
    f32 scale = 1.0F;
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

// The stone tablet, with its vertical caption beside it (design doc 0002.4 §2).
constexpr Prop STELE{ .sprite = SpriteId::Stele, .foot = { .x = 760.0F, .y = 980.0F } };
// Game text (the phase 6 chunk 3 spec): offsets in world units, so they scale with the zoom as the text does.
constexpr core::Vec2 LABEL_OFFSET{ .x = 0.0F, .y = -6.0F };   // a little clear of the head
constexpr core::Vec2 CAPTION_OFFSET{ .x = 24.0F, .y = 0.0F }; // clear of the tablet's right edge
// Linear, as the scene is drawn: light #F4EFE6 for names and pale gold #F2D9A0 for the caption, in sRGB, read over the
// renderer's dark outline and shadow (it turns them to dark ink when both are off).
constexpr u32 LABEL_COLOUR = 0xFFCA'DCE7U;
constexpr u32 CAPTION_COLOUR = 0xFF5A'B1E2U;

// The painted art around the arena's edges, some larger or smaller than painted (design doc 0002.3 §2: every feature
// shows in the arena).
constexpr std::array ARENA_PAINTED{
    Prop{ .sprite = SpriteId::TreantBlighted, .foot = { .x = 1720.0F, .y = 720.0F }, .scale = 1.25F },
    Prop{ .sprite = SpriteId::TreeDead, .foot = { .x = 110.0F, .y = 700.0F }, .scale = 0.7F },
    Prop{ .sprite = SpriteId::Hut, .foot = { .x = 480.0F, .y = 250.0F } },
    Prop{ .sprite = SpriteId::Crane, .foot = { .x = 1250.0F, .y = 960.0F } },
    STELE,
    Prop{ .sprite = SpriteId::FoxSpirit, .foot = { .x = 1450.0F, .y = 260.0F }, .scale = 0.8F },
    Prop{ .sprite = SpriteId::FoxYellow, .foot = { .x = 1000.0F, .y = 1010.0F } },
    Prop{ .sprite = SpriteId::WispRed, .foot = { .x = 130.0F, .y = 320.0F }, .scale = 0.6F },
    Prop{ .sprite = SpriteId::WispRed, .foot = { .x = 175.0F, .y = 345.0F }, .scale = 0.6F },
};
constexpr Prop ARENA_GLOW{ .sprite = SpriteId::OrbGold, .foot = { .x = 1720.0F, .y = 420.0F } }; // in the treant's branches
constexpr Prop CRYSTAL{ .sprite = SpriteId::CrystalIce, .foot = { .x = 420.0F, .y = 620.0F }, .scale = 0.8F };
constexpr f32 CRYSTAL_SPIN = 0.02F; // radians per tick: a turn in about five seconds

// The crystal's aura: every AURA_PERIOD simulation ticks it hits what is within AURA_REACH world units of its foot.
constexpr u64 AURA_PERIOD = 60;
constexpr f32 AURA_REACH = 320.0F;
constexpr u32 AURA_DAMAGE_MIN = 5;
constexpr u32 AURA_DAMAGE_MAX = 40;
constexpr u32 AURA_HEALING_MIN = 5;
constexpr u32 AURA_HEALING_MAX = 20;
// The debug burst: DEBUG_BURST_HITS hits round the foxes, values from 1 to DEBUG_BURST_MAX.
constexpr u32 DEBUG_BURST_HITS = 4'000;
constexpr u32 DEBUG_BURST_MAX = 99;
// Ability use sources (core::AbilityUse).
constexpr u32 USE_AURA = 1;
constexpr u32 USE_DEBUG_BURST = 2;

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
// Half the view around the player: the world shown at the smallest zoom (0.25) in a window up to 32:9 wide or about
// square, so the logic culls without the main thread's camera (design doc 0002.3 §4).
// A fixed size; cull relative to the window (the camera's view rectangle) or the world's size when a window
// shape or zoom outgrows it.
constexpr core::Vec2 FOLLOW_HALF_VIEW{ .x = 7'680.0F, .y = 4'320.0F };
constexpr u32 FOXES = 12;
constexpr u32 ORBS = 8;
constexpr f32 ORB_RADIUS = 150.0F;
constexpr f32 ORB_WOBBLE = 40.0F; // in and out of the circle, world units
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
constexpr u32 SMALL_WORLD_NPCS = 5'000;
constexpr u32 WORLD_CELLS = 4'096;
constexpr u64 SEED = 0x5eed;

// The arena's part of a save: its format, then a version per domain, each changed with what its components save.
constexpr u32 ARENA_FORMAT = 2;   // 2: the player and the selection
constexpr u32 ACTIVE_VERSION = 3; // 2: creatures; 3: spin
constexpr u32 WORLD_VERSION = 1;
using ActiveComponents = core::ComponentList<Body, Look, Orbit, Spin, Creature, Npc, Needs, Met, Wander>;
using WorldComponents = core::ComponentList<Npc, Needs, Met, Group>;

constexpr u32 PUBLISH_CHUNK = 4'096; // entities packed per job chunk
// The still scenes' view: the camera starts on the whole ground.
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

// Whether every entity has the components that code reads together with the ones it has: a save can
// hold any mix, and a missing partner would be read anyway (the month step reads an Npc's Needs,
// publishing a Look's Body, applying the game data a Creature's Look, deactivating an active Npc's Body).
bool Consistent(const core::State& state) {
    for (const core::DomainIndex domain : { ACTIVE, WORLD }) {
        const core::Registry& entities = state[domain].Entities();
        for (const core::Entity entity : entities.view<const Npc>()) {
            if (!entities.all_of<Needs>(entity) || (domain == ACTIVE && !entities.all_of<Body>(entity))) {
                return false;
            }
        }
        for (const core::Entity entity : entities.view<const Look>()) {
            if (!entities.all_of<Body>(entity)) {
                return false;
            }
        }
        for (const core::Entity entity : entities.view<const Creature>()) {
            if (!entities.all_of<Look>(entity)) {
                return false;
            }
        }
    }
    return true;
}

core::Rect ViewOf(Scene scene) {
    return scene == Scene::Cells ? CELLS_VIEW : GROUND_VIEW;
}

// Each Scene's name in the manifest, in Scene's order, and whether it has creatures, which draw the data's sprites.
struct SceneEntry {
    std::string_view name;
    bool creatures = false;
};
constexpr std::array<SceneEntry, 4> SCENES{ {
    { .name = "arena", .creatures = true },
    { .name = "stress", .creatures = true },
    { .name = "painted", .creatures = false },
    { .name = "cells", .creatures = false },
} };

core::SceneId SceneIdOf(const core::Manifest& manifest, Scene scene) {
    const std::optional<core::SceneId> found = manifest.SceneNamed(SceneName(scene));
    PN_ASSERT(found.has_value(), "the manifest lacks a scene: main checks it at start");
    return found.value_or(core::SceneId{});
}

// What assets/data/creatures.yaml ships.
constexpr core::GameData BUILT_IN_DATA{
    .cultivator = { .sprite = SpriteId::Cultivator, .walkSpeed = 4.0F }, // 240 world units a second at full stick
    // A lap in about 17 seconds.
    .fox = { .sprite = SpriteId::Fox, .orbitRadius = 300.0F, .orbitWobble = 40.0F, .orbitSpeed = 0.006F, .orbitsPlayer = true },
};

} // namespace

core::GameData BuiltInData() {
    return BUILT_IN_DATA;
}

std::optional<Scene> SceneNamed(std::string_view name) {
    const std::span<const SceneEntry> scenes{ SCENES };
    const auto found = std::ranges::find(scenes, name, &SceneEntry::name);
    if (found == scenes.end()) {
        return std::nullopt;
    }
    return static_cast<Scene>(found - scenes.begin());
}

std::string_view SceneName(Scene scene) {
    return SCENES.at(std::to_underlying(scene)).name;
}

std::expected<void, core::Error> CheckScenes(const core::Manifest& manifest, const core::GameData& data) {
    for (const SceneEntry& entry : SCENES) {
        const std::optional<core::SceneId> scene = manifest.SceneNamed(entry.name);
        if (!scene) {
            return std::unexpected{ core::Error{ std::format("the atlas has no scene {}: run `./dev atlas`", entry.name) } };
        }
        const std::vector<SpriteId>& sprites = manifest.Scene(*scene).sprites;
        for (const SpriteId sprite : { data.cultivator.sprite, data.fox.sprite }) {
            if (entry.creatures && !std::ranges::contains(sprites, sprite)) {
                return std::unexpected{ core::Error{ std::format(
                    "the scene {} doesn't load {}, which the creatures data draws: add it to art/scenes.toml and run "
                    "`./dev atlas`",
                    entry.name,
                    core::atlas::SPRITE_NAMES.at(std::to_underlying(sprite))
                ) } };
            }
        }
    }
    return {};
}

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

Arena::Arena(std::shared_ptr<const core::Manifest> manifest, game::Scene scene, const core::GameData& data)
  : m_manifest{ std::move(manifest) }
  , m_state{ DOMAINS, SEED }
  , m_data{ data }
  , m_scene{ scene }
  , m_sceneId{ SceneIdOf(*m_manifest, scene) }
  , m_view{ ViewOf(scene) } {
    Build(scene);
}

// Everything but the calendar and the game data starts afresh: the entities, their IDs, the relationships, the
// selection.
void Arena::Build(game::Scene scene) {
    m_state = core::State{ DOMAINS, SEED };
    m_relationships = Relationships{};
    m_scene = scene;
    m_sceneId = SceneIdOf(*m_manifest, scene);
    m_view = ViewOf(scene);
    m_player = {};
    m_selected = {};
    if (scene == game::Scene::Cells) {
        SpawnCells();
        return;
    }
    if (scene == game::Scene::Painted) {
        SpawnPainted();
        return;
    }
    for (const Prop& prop : PROPS) {
        Spawn(prop.foot, { .sprite = prop.sprite });
    }
    for (const Prop& prop : ARENA_PAINTED) {
        Spawn(prop.foot, { .sprite = prop.sprite, .shadow = true, .scale = prop.scale });
    }
    Spawn(
        ARENA_GLOW.foot, { .sprite = ARENA_GLOW.sprite, .layer = core::Layer::Effects, .pipeline = core::Pipeline::Additive }
    );
    m_state[ACTIVE].Entities().emplace<Spin>(
        Spawn(CRYSTAL.foot, { .sprite = CRYSTAL.sprite, .shadow = true, .scale = CRYSTAL.scale }), Spin{ .speed = CRYSTAL_SPIN }
    );
    core::Registry& entities = m_state[ACTIVE].Entities();
    const core::Entity cultivator = Spawn(CULTIVATOR, { .sprite = m_data.cultivator.sprite, .shadow = true });
    entities.emplace<Creature>(cultivator, CreatureKind::Cultivator);
    m_player = entities.get<core::StableId>(cultivator);
    for (u32 fox = 0; fox < FOXES; ++fox) {
        const Orbit orbit{
            .centre = CULTIVATOR,
            .radius = m_data.fox.orbitRadius,
            .wobble = m_data.fox.orbitWobble,
            .phase = TURN * static_cast<f32>(fox) / static_cast<f32>(FOXES),
            .speed = m_data.fox.orbitSpeed,
        };
        const core::Entity entity = Spawn(OrbitPoint(orbit, 0), { .sprite = m_data.fox.sprite, .shadow = true });
        entities.emplace<Orbit>(entity, orbit);
        entities.emplace<Creature>(entity, CreatureKind::Fox);
    }
    for (u32 orb = 0; orb < ORBS; ++orb) {
        const Orbit orbit{
            .centre = CULTIVATOR,
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
        m_state[ACTIVE].Entities().emplace<Orbit>(Spawn(OrbitPoint(orbit, 0), look), orbit);
    }
    if (scene == game::Scene::Stress) {
        SpawnStress();
    }
    SpawnNpcs(scene == game::Scene::Stress ? WORLD_NPCS : SMALL_WORLD_NPCS);
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
}

void Arena::SpawnNpcs(u32 worldNpcs) {
    for (const u32 cell : ACTIVE_CELLS) {
        Activate(m_state, SpawnNpc(m_state, cell));
    }
    for (u32 npc = 0; npc < worldNpcs; ++npc) {
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

void Arena::Tick(const core::TickCounters& counters, const core::TickInput& input, std::vector<core::Event>& events) {
    PN_PROFILE_ZONE();
    if (std::ranges::contains(input.actions, core::ActionKind::CycleScene, &core::Action::kind)) {
        const auto next = static_cast<game::Scene>((std::to_underlying(m_scene) + 1U) % SCENES.size());
        PN_LOG_INFO("tick {}: the scene changes to {}", counters.simulation, SceneName(next));
        Build(next); // the new scene's first tick is the next one
        return;
    }
    for (const core::Action& action : input.actions) {
        if (action.kind == core::ActionKind::Select) {
            Select(action.target);
        }
    }
    Walk(input.held.move);
    core::Domain& active = m_state[ACTIVE];
    if (m_player != core::StableId{}) {
        const core::Vec2 player = active.Entities().get<Body>(active.Find(m_player)).current;
        for (auto [entity, creature, orbit] : active.Entities().view<const Creature, Orbit>().each()) {
            if (DataOf(creature.kind).orbitsPlayer) {
                orbit.centre = player;
            }
        }
    }
    for (auto [entity, body, orbit] : active.Entities().view<Body, const Orbit>().each()) {
        body.previous = body.current;
        body.current = OrbitPoint(orbit, counters.simulation);
    }
    for (auto [entity, spin] : active.Entities().view<Spin>().each()) {
        spin.previous = spin.current;
        spin.current = std::remainder(spin.current + spin.speed, TURN); // stays precise however long it turns
    }
    DecideActive(active, m_state.Seed(), counters.state, counters.simulation);
    WalkActive(active);
    if (m_scene == game::Scene::Arena && counters.simulation % AURA_PERIOD == 0) {
        Aura(counters, events);
    }
    if (std::ranges::contains(input.actions, core::ActionKind::DebugBurst, &core::Action::kind)) {
        DebugBurst(counters, events);
    }
}

core::Vec2 Arena::HeadOf(core::Entity entity) const {
    const core::Registry& entities = m_state[ACTIVE].Entities();
    const core::Vec2 foot = entities.get<Body>(entity).current;
    const f32 footY = static_cast<f32>(m_manifest->Sprite(entities.get<Look>(entity).sprite).footY);
    return { .x = foot.x, .y = foot.y - (footY * entities.get<Look>(entity).scale) };
}

void Arena::PublishTexts(core::Snapshot& snapshot) const {
    PN_PROFILE_ZONE();
    if (m_player == core::StableId{}) {
        return;
    }
    const core::Registry& entities = m_state[ACTIVE].Entities();
    for (auto [entity, creature, body, look, stableId] :
         entities.view<const Creature, const Body, const Look, const core::StableId>().each()) {
        const bool player = stableId == m_player;
        if (!player && creature.kind != CreatureKind::Fox) {
            continue;
        }
        const core::StringId name = player ? core::StringId::CultivatorName : core::StringId::FoxName;
        const f32 height = static_cast<f32>(m_manifest->Sprite(look.sprite).footY) * look.scale;
        const core::Vec2 head{ .x = 0.0F, .y = -height };
        snapshot.texts.push_back(
            {
                .kind = core::TextKind::Label,
                .string = name,
                .previous = body.previous + head,
                .current = body.current + head,
                .offset = LABEL_OFFSET,
                .entity = stableId,
                .height = height,
                .nameKey = std::to_underlying(name),
                .priority = player ? core::LabelPriority::Player : core::LabelPriority::Rest,
                .colour = LABEL_COLOUR,
            }
        );
    }
    const core::SpriteInfo& stele = m_manifest->Sprite(STELE.sprite);
    const core::Vec2 topRight{ .x = STELE.foot.x + static_cast<f32>(stele.width) - static_cast<f32>(stele.footX),
                               .y = STELE.foot.y - static_cast<f32>(stele.footY) };
    snapshot.texts.push_back(
        {
            .kind = core::TextKind::Caption,
            .string = core::StringId::TabletCaption,
            .previous = topRight,
            .current = topRight,
            .offset = CAPTION_OFFSET,
            .colour = CAPTION_COLOUR,
            .align = core::TextAlign::TopCentre,
            .direction = core::TextDirection::TopToBottom,
        }
    );
}

void Arena::Aura(const core::TickCounters& counters, std::vector<core::Event>& events) const {
    PN_PROFILE_ZONE();
    const u64 use = core::AbilityUse(counters.simulation, USE_AURA);
    const core::Registry& entities = m_state[ACTIVE].Entities();
    for (auto [entity, body, creature, target] : entities.view<const Body, const Creature, const core::StableId>().each()) {
        const core::Vec2 away = body.current - CRYSTAL.foot;
        if ((away.x * away.x) + (away.y * away.y) > AURA_REACH * AURA_REACH) {
            continue;
        }
        const bool healing = creature.kind == CreatureKind::Cultivator;
        if (!healing && creature.kind != CreatureKind::Fox) {
            continue;
        }
        const u32 low = healing ? AURA_HEALING_MIN : AURA_DAMAGE_MIN;
        const u32 high = healing ? AURA_HEALING_MAX : AURA_DAMAGE_MAX;
        const u64 random = core::Random(m_state.Seed(), counters.state, std::to_underlying(target), PURPOSE_AURA, 0);
        events.push_back(
            {
                .kind = core::EventKind::Hit,
                .hit = healing ? core::HitKind::Healing : core::HitKind::Damage,
                .use = use,
                .target = target,
                .position = HeadOf(entity),
                .value = static_cast<i32>(low + core::ToBelow(random, high - low + 1)),
            }
        );
    }
    events.push_back({ .kind = core::EventKind::UseEnded, .use = use });
}

void Arena::DebugBurst(const core::TickCounters& counters, std::vector<core::Event>& events) const {
    PN_PROFILE_ZONE();
    const u64 use = core::AbilityUse(counters.simulation, USE_DEBUG_BURST);
    const core::Registry& entities = m_state[ACTIVE].Entities();
    std::vector<core::Entity> foxes;
    for (auto [entity, creature] : entities.view<const Creature>().each()) {
        if (creature.kind == CreatureKind::Fox) {
            foxes.push_back(entity);
        }
    }
    if (foxes.empty()) {
        PN_LOG_INFO("tick {}: the debug burst has no foxes to hit in this scene", counters.simulation);
    }
    else {
        events.reserve(events.size() + DEBUG_BURST_HITS + 1);
    }
    for (u32 index = 0; !foxes.empty() && index < DEBUG_BURST_HITS; ++index) {
        const core::Entity fox = foxes.at(index % foxes.size());
        const core::StableId target = entities.get<core::StableId>(fox);
        const u64 random = core::Random(m_state.Seed(), counters.state, std::to_underlying(target), PURPOSE_BURST, index);
        events.push_back(
            {
                .kind = core::EventKind::Hit,
                // The kinds take turns by round over the foxes, so each fox gets every kind whatever their number.
                .hit = static_cast<core::HitKind>((index / foxes.size()) % core::HIT_KINDS),
                .use = use,
                .target = target,
                .position = HeadOf(fox),
                .value = static_cast<i32>(1 + core::ToBelow(random, DEBUG_BURST_MAX)),
            }
        );
    }
    events.push_back({ .kind = core::EventKind::UseEnded, .use = use });
}

void Arena::Walk(core::Vec2 move) {
    if (m_player == core::StableId{}) {
        return;
    }
    core::Domain& active = m_state[ACTIVE];
    Body& body = active.Entities().get<Body>(active.Find(m_player));
    body.previous = body.current; // every tick, so a cultivator standing still draws still
    body.current = body.current + (move * m_data.cultivator.walkSpeed);
}

void Arena::Select(core::StableId target) {
    m_selected = m_state.DomainOf(target) ? target : core::StableId{};
    PN_LOG_INFO(
        "selected {}", m_selected == core::StableId{} ? "nothing" : std::format("entity {}", std::to_underlying(target))
    );
}

void Arena::AdvanceCalendar(const core::TickCounters& counters, core::Jobs& jobs, const std::stop_token& stop) {
    m_monthStep.Run(m_state, m_relationships, jobs, counters.state, stop);
    ++m_months;
}

template<typename Archive>
void Arena::Write(Archive& archive) const {
    archive(ARENA_FORMAT, m_scene, m_state.Seed(), m_state.Ids().next, m_months, m_player, m_selected);
    archive(ACTIVE_VERSION);
    core::SaveDomain(archive, m_state[ACTIVE], ActiveComponents{});
    archive(WORLD_VERSION);
    core::SaveDomain(archive, m_state[WORLD], WorldComponents{});
    m_relationships.Save(archive);
}

void Arena::Save(core::Writer& archive) {
    PN_PROFILE_ZONE();
    m_state.SortCanonical();
    Write(archive);
}

u64 Arena::StateHash() const {
    PN_PROFILE_ZONE();
    core::Hasher hasher;
    Write(hasher);
    return hasher.Value();
}

bool Arena::Load(core::Reader& archive) {
    PN_PROFILE_ZONE();
    u32 format = 0;
    game::Scene scene = game::Scene::Arena;
    u64 seed = 0;
    u64 next = 0;
    u64 months = 0;
    core::StableId player{};
    core::StableId selected{};
    archive(format, scene, seed, next, months, player, selected);
    if (format != ARENA_FORMAT || scene > game::Scene::Cells) {
        archive.Fail();
    }
    core::State state{ DOMAINS, seed, core::StableIdCounter{ .next = next } };
    u32 version = 0;
    archive(version);
    if (version != ACTIVE_VERSION) {
        archive.Fail();
    }
    core::LoadDomain(archive, state, ACTIVE, ActiveComponents{});
    archive(version);
    if (version != WORLD_VERSION) {
        archive.Fail();
    }
    core::LoadDomain(archive, state, WORLD, WorldComponents{});
    Relationships relationships;
    relationships.Load(archive, state);
    const bool playerFound = player == core::StableId{} || (state.DomainOf(player) == ACTIVE &&
                                                            state[ACTIVE].Entities().all_of<Body>(state[ACTIVE].Find(player)));
    if (archive.Failed() || archive.Remaining() != 0 || !Consistent(state) || !playerFound) {
        return false;
    }
    m_state = std::move(state);
    m_relationships = std::move(relationships);
    m_scene = scene;
    m_sceneId = SceneIdOf(*m_manifest, scene);
    m_months = months;
    m_view = ViewOf(scene);
    m_player = player;
    m_selected = selected;
    ApplyData();
    return true;
}

void Arena::SetData(const core::GameData& data) {
    m_data = data;
    ApplyData();
}

void Arena::ApplyData() {
    core::Registry& entities = m_state[ACTIVE].Entities();
    for (auto [entity, creature, look] : entities.view<const Creature, Look>().each()) {
        const core::CreatureData& kind = DataOf(creature.kind);
        look.sprite = kind.sprite;
        if (Orbit* const orbit = entities.try_get<Orbit>(entity)) {
            if (!kind.orbitsPlayer) {
                orbit->centre = CULTIVATOR; // back from wherever the player led it
            }
            orbit->radius = kind.orbitRadius;
            orbit->wobble = kind.orbitWobble;
            orbit->speed = kind.orbitSpeed;
        }
    }
}

void Arena::Publish(core::Snapshot& snapshot, core::Jobs& jobs) {
    PN_PROFILE_ZONE();
    const auto& bodies = m_state[ACTIVE].Entities().storage<Body>();
    const auto& looks = m_state[ACTIVE].Entities().storage<Look>();
    const auto& ids = m_state[ACTIVE].Entities().storage<core::StableId>();
    const auto& spins = m_state[ACTIVE].Entities().storage<Spin>();
    const u32 chunks = core::ChunkCount(looks.size(), PUBLISH_CHUNK);
    core::Rect view = m_view;
    if (m_player != core::StableId{}) {
        const Body& body = bodies.get(m_state[ACTIVE].Find(m_player));
        snapshot.follow = core::Follow{ .previous = body.previous, .current = body.current };
        view = { .min = body.current - FOLLOW_HALF_VIEW, .max = body.current + FOLLOW_HALF_VIEW };
    }
    m_drawList.Begin(view, std::max(chunks, 1U));
    m_drawList.Add(
        0, core::Layer::Ground, core::Pipeline::Normal, core::MakeInstance(*m_manifest, { .sprite = SpriteId::Ground })
    );
    jobs.ParallelFor(chunks, [&](u32 chunk) {
        const std::size_t begin = std::size_t{ chunk } * PUBLISH_CHUNK;
        const std::size_t end = std::min(looks.size(), begin + PUBLISH_CHUNK);
        for (std::size_t i = begin; i < end; ++i) {
            const core::Entity entity = looks[i];
            const Body& body = bodies.get(entity);
            const Look& look = looks.get(entity);
            const core::StableId stableId = ids.get(entity);
            const Spin spin = spins.contains(entity) ? spins.get(entity) : Spin{};
            if (look.shadow) {
                m_drawList.Add(
                    chunk,
                    core::Layer::Decals,
                    core::Pipeline::Normal,
                    core::MakeInstance(
                        *m_manifest, { .sprite = SpriteId::Shadow, .previous = body.previous, .current = body.current }
                    )
                );
            }
            m_drawList.Add(
                chunk,
                look.layer,
                look.pipeline,
                core::MakeInstance(
                    *m_manifest,
                    { .sprite = look.sprite,
                      .previous = body.previous,
                      .current = body.current,
                      .scale = look.scale,
                      .previousRotation = spin.previous,
                      .currentRotation = spin.current }
                ),
                body.current.y,
                stableId
            );
        }
    });
    m_drawList.Publish(snapshot, jobs);
    PublishTexts(snapshot);
    if (m_selected != core::StableId{}) {
        if (const auto drawn = std::ranges::find(snapshot.ids, m_selected); drawn != snapshot.ids.end()) {
            snapshot.selected = static_cast<u32>(drawn - snapshot.ids.begin());
        }
    }
}

} // namespace game
