#include "game/arena.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/log.hpp"
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
constexpr u32 SELECTED_TINT = 0xFF78'DCFFU; // a warm gold, premultiplied RGBA8 (red in the low byte)
// Half the view around the player: the world shown at the smallest zoom (0.25) in a window up to 32:9 wide or about
// square, so the logic culls without the main thread's camera (design doc 0002.3 §4).
// shortcut: a fixed size; send the camera's view rectangle when a window shape or zoom outgrows it.
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
constexpr u32 ACTIVE_VERSION = 2; // 2: creatures
constexpr u32 WORLD_VERSION = 1;
using ActiveComponents = core::ComponentList<Body, Look, Orbit, Creature, Npc, Needs, Met, Wander>;
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

// What assets/data/creatures.yaml ships.
constexpr core::GameData BUILT_IN_DATA{
    .cultivator = { .sprite = SpriteId::Cultivator, .walkSpeed = 4.0F }, // 240 world units a second at full stick
    // A lap in about 17 seconds.
    .fox = { .sprite = SpriteId::Fox, .orbitRadius = 300.0F, .orbitWobble = 40.0F, .orbitSpeed = 0.006F },
};

} // namespace

core::GameData BuiltInData() {
    return BUILT_IN_DATA;
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

Arena::Arena(Scene scene, const core::GameData& data)
  : m_state{ DOMAINS, SEED }
  , m_data{ data }
  , m_scene{ scene }
  , m_view{ ViewOf(scene) } {
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
    core::Registry& entities = m_state[ACTIVE].Entities();
    const core::Entity cultivator = Spawn(CULTIVATOR, { .sprite = data.cultivator.sprite, .shadow = true });
    entities.emplace<Creature>(cultivator, CreatureKind::Cultivator);
    m_player = entities.get<core::StableId>(cultivator);
    for (u32 fox = 0; fox < FOXES; ++fox) {
        const Orbit orbit{
            .centre = CULTIVATOR,
            .radius = data.fox.orbitRadius,
            .wobble = data.fox.orbitWobble,
            .phase = TURN * static_cast<f32>(fox) / static_cast<f32>(FOXES),
            .speed = data.fox.orbitSpeed,
        };
        const core::Entity entity = Spawn(OrbitPoint(orbit, 0), { .sprite = data.fox.sprite, .shadow = true });
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
    if (scene == Scene::Stress) {
        SpawnStress();
    }
    if (scene == Scene::Npcs) {
        SpawnNpcs(SMALL_WORLD_NPCS);
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
    SpawnNpcs(WORLD_NPCS);
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

void Arena::Tick(const core::TickCounters& counters, const core::TickInput& input) {
    PN_PROFILE_ZONE();
    for (const core::Action& action : input.actions) {
        if (action.kind == core::ActionKind::Select) {
            Select(action.target);
        }
    }
    Walk(input.held.move);
    core::Domain& active = m_state[ACTIVE];
    for (auto [entity, body, orbit] : active.Entities().view<Body, const Orbit>().each()) {
        body.previous = body.current;
        body.current = OrbitPoint(orbit, counters.simulation);
    }
    DecideActive(active, m_state.Seed(), counters.state, counters.simulation);
    WalkActive(active);
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
    Scene scene = Scene::Arena;
    u64 seed = 0;
    u64 next = 0;
    u64 months = 0;
    core::StableId player{};
    core::StableId selected{};
    archive(format, scene, seed, next, months, player, selected);
    if (format != ARENA_FORMAT || scene > Scene::Npcs) {
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
        const core::CreatureData& kind = creature.kind == CreatureKind::Fox ? m_data.fox : m_data.cultivator;
        look.sprite = kind.sprite;
        if (Orbit* const orbit = entities.try_get<Orbit>(entity)) {
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
    const u32 chunks = core::ChunkCount(looks.size(), PUBLISH_CHUNK);
    core::Rect view = m_view;
    if (m_player != core::StableId{}) {
        const Body& body = bodies.get(m_state[ACTIVE].Find(m_player));
        snapshot.follow = core::Follow{ .previous = body.previous, .current = body.current };
        view = { .min = body.current - FOLLOW_HALF_VIEW, .max = body.current + FOLLOW_HALF_VIEW };
    }
    m_drawList.Begin(view, std::max(chunks, 1U));
    m_drawList.Add(0, core::Layer::Ground, core::Pipeline::Normal, core::MakeInstance({ .sprite = SpriteId::Ground }));
    jobs.ParallelFor(chunks, [&](u32 chunk) {
        const std::size_t begin = std::size_t{ chunk } * PUBLISH_CHUNK;
        const std::size_t end = std::min(looks.size(), begin + PUBLISH_CHUNK);
        for (std::size_t i = begin; i < end; ++i) {
            const core::Entity entity = looks[i];
            const Body& body = bodies.get(entity);
            const Look& look = looks.get(entity);
            const core::StableId stableId = ids.get(entity);
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
                    { .sprite = look.sprite,
                      .previous = body.previous,
                      .current = body.current,
                      .scale = look.scale,
                      .colour = stableId == m_selected ? SELECTED_TINT : core::OPAQUE_WHITE }
                ),
                body.current.y,
                stableId
            );
        }
    });
    m_drawList.Publish(snapshot, jobs);
}

} // namespace game
