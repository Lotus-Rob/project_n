#include "game/host.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/delta.hpp"
#include "core/instances.hpp"
#include "core/log.hpp"
#include "core/profile.hpp"
#include "core/random.hpp"
#include "core/rect.hpp"
#include "core/seal.hpp"
#include "core/view_limits.hpp"
#include "core/world_map.hpp"
#include "game/random_purposes.hpp"

namespace game {

namespace {

using core::atlas::SpriteId;

// The demo glows' loops, in ticks (60 a second).
constexpr u64 CHARGE_LOOP = 192;    // 3.2 s
constexpr u64 CHARGE_RISE = 108;    // the value climbs from 0 to 1 over these, then holds
constexpr u64 CHARGE_RELEASE = 144; // and is released here
constexpr u64 PROGRESS_LOOP = 300;  // 5 s
constexpr u64 PROGRESS_FILL = 180;  // the value climbs over these, then the result shows
constexpr u64 PROGRESS_RESULTS = 4;

/// The demo glow `sprite` wears in a scene whose traits list `glows`: none for the rest. The role isn't stored on the
/// entity: that would change what saves hold and the state hash, and a glow is presentation only.
core::GlowRole DemoRoleOf(std::span<const DemoGlowSprite> glows, SpriteId sprite) {
    const auto found = std::ranges::find(glows, sprite, &DemoGlowSprite::sprite);
    return found == glows.end() ? core::GlowRole::None : found->role;
}

/// A value rising linearly from 0 to 1 over `rise` ticks, then held.
f32 Rise(u64 age, u64 rise) {
    return std::min(static_cast<f32>(age) / static_cast<f32>(rise), 1.0F);
}

// The debug burst: DEBUG_BURST_HITS hits round the foxes, values from 1 to DEBUG_BURST_MAX.
constexpr u32 DEBUG_BURST_HITS = 4'000;
constexpr u32 DEBUG_BURST_MAX = 99;
constexpr u32 USE_DEBUG_BURST = 2; // the burst's ability use source (core::AbilityUse); the arena's aura is 1

constexpr u64 SEED = 0x5eed;

// The portal as drawn: a ground seal twice its step area (PORTAL_RADIUS), so the step lands on its inner rings, and its
// caption PORTAL_RADIUS over its point, just past the far rim of the seal's squashed ellipse (PORTAL_DEPTH_RATIO of its
// size).
constexpr f32 PORTAL_SEAL_SIZE = 2.0F * PORTAL_RADIUS;
constexpr core::Vec2 PORTAL_LABEL_RISE{ .x = 0.0F, .y = -PORTAL_RADIUS };

// A foot y past any view's bottom: what is added with it sorts after everything else in the world layer.
constexpr f32 ABOVE_THE_WORLD = std::numeric_limits<f32>::max();

/// The foot y `entity`, whose foot stands at `footY`, sorts by: ABOVE_THE_WORLD for `onTop`, its own for the rest.
f32 SortingFootY(core::StableId entity, core::StableId onTop, f32 footY) {
    return entity == onTop ? ABOVE_THE_WORLD : footY;
}

// Halfway across a scene's view, where a still camera starts: what a publish culls round before the window's view arrives.
constexpr f32 MIDDLE = 0.5F;

// The host's part of a save: its format, then a version per domain, each changed with what its components save.
// 2: the player and the selection; 3: the respawns waiting, the volleys and the flashes; 4: the seals; 5: the world clock,
// the month steps run and the world pause in place of the months passed; 6: the player's route; 7: the scene's own stable-ID
// counter; 8: the game's world, a dev scene's scratch world and the scene in sections of their own, and the place on the
// world map the cultivator comes back to; 9: the game's world as its difference from the world its seed makes; 10: every
// storage in the order it ran in, never sorted for a save, and a dev scene's scratch world as its difference from its
// entry build
constexpr u32 ARENA_FORMAT = 10;
constexpr u32 ACTIVE_VERSION =
    5; // 2: creatures; 3: spin; 4: health, dying, arriving, caster, dashing, afflictions; 5: the NPCs' homes
// 2: the NPCs' homes; 3: the travelling action, the journeys and the journals; 4: the game's world as its difference from
// its base, a dev scene's journeys as their difference from none; 5: the world domain's order beside its difference, and a
// dev scene's world as its difference from its entry build; 6: each relationship's last-met month
constexpr u32 WORLD_VERSION = 6;
using ActiveComponents = core::ComponentList<
    Body,
    Look,
    Orbit,
    Spin,
    Creature,
    Npc,
    Needs,
    Met,
    Wander,
    Health,
    Dying,
    Arriving,
    Caster,
    Dashing,
    Afflictions>;
using WorldComponents = core::ComponentList<Npc, Needs, Met, Group>;

constexpr u32 PUBLISH_CHUNK = 4'096; // entities, or journeys, packed per job chunk

/// Writes `world`'s calendar, pause and place on the world map into `archive` (a Writer or a Hasher).
template<typename Archive>
void WriteClock(Archive& archive, const WorldState& world) {
    archive(world.worldTicks, world.months, world.paused, world.mapPlace.has_value());
    if (world.mapPlace) {
        archive(*world.mapPlace);
    }
}

/// Hashes `world` whole into `hasher`: its seed, its world stable-ID counter, its calendar, its pause and its place on
/// the world map, then its world domain, its relationships and its travel. Its active domain is the running scene's,
/// hashed with the scene.
void HashWorld(core::Hasher& hasher, const WorldState& world) {
    hasher(world.state.Seed(), world.state.Ids().next);
    WriteClock(hasher, world);
    hasher(WORLD_VERSION);
    core::SaveDomain(hasher, world.state[WORLD], WorldComponents{});
    world.relationships.Save(hasher);
    world.travel.Save(hasher);
}

/// Writes a dev scene's scratch world's section of a save into `archive` as its difference from `base`, the world the
/// scene's entry build made (BuildScratch()), whose seed it shares: the world stable-ID counter, the calendar, the pause
/// and the place on the world map, then the world domain's difference (core::SaveDomainDelta()) and order
/// (core::SaveDomainOrder()), the relationships whole and the travel as its difference from none (Travel::SaveDelta()),
/// as no dev scene shows the world map.
void WriteScratch(core::Writer& archive, const WorldState& world, const WorldState& base) {
    archive(world.state.Ids().next);
    WriteClock(archive, world);
    archive(WORLD_VERSION);
    core::SaveDomainDelta(archive, base.state[WORLD], world.state[WORLD], WorldComponents{});
    core::SaveDomainOrder(archive, base.state[WORLD], world.state[WORLD], WorldComponents{});
    world.relationships.Save(archive);
    world.travel.SaveDelta(archive, Travel{}, nullptr, nullptr);
}

/// Writes the game's world's section of a save into `archive` as its difference from `base`, the world as it was made
/// from data of base hash `baseHash`: the seed, GENERATOR_VERSION, `baseHash`, the world stable-ID counter, the calendar,
/// the pause and the place on the world map, then the world domain's difference and order, the relationships whole and
/// the travel's difference, its journeys rebuilt over `map` and `paths` (null without a map) to tell a plan from a whole
/// one.
void WriteGameWorld(
    core::Writer& archive,
    const WorldState& world,
    const WorldState& base,
    u64 baseHash,
    const core::WorldMap* map,
    core::Paths* paths
) {
    archive(world.state.Seed(), GENERATOR_VERSION, baseHash, world.state.Ids().next);
    WriteClock(archive, world);
    archive(WORLD_VERSION);
    core::SaveDomainDelta(archive, base.state[WORLD], world.state[WORLD], WorldComponents{});
    core::SaveDomainOrder(archive, base.state[WORLD], world.state[WORLD], WorldComponents{});
    world.relationships.Save(archive);
    world.travel.SaveDelta(archive, base.travel, map, paths);
}

/// A world's calendar, pause and place on the world map, as a save holds them.
struct Clock {
    u64 worldTicks = 0;
    u64 months = 0;
    bool paused = false;
    std::optional<core::Vec2> place = std::nullopt;
};

/// Reads what WriteClock() wrote.
Clock ReadClock(core::Reader& archive) {
    Clock clock;
    bool placed = false;
    archive(clock.worldTicks, clock.months, clock.paused, placed);
    if (placed) {
        archive(clock.place.emplace());
    }
    return clock;
}

/// Gives `world` the calendar, pause and place of `clock`.
void SetClock(WorldState& world, const Clock& clock) {
    world.worldTicks = clock.worldTicks;
    world.months = clock.months;
    world.paused = clock.paused;
    world.mapPlace = clock.place;
}

/// Gives each entity of `into` that `from` has `Component` on (every one of them, by stable ID) a copy of it, in `from`'s
/// storage order, so the copy iterates as the original does.
template<typename Component>
void CopyStorage(const core::Domain& from, core::Domain& into) {
    const auto* storage = from.Entities().storage<Component>();
    if (storage == nullptr) {
        return;
    }
    core::Registry& target = into.Entities();
    for (std::size_t index = 0; index < storage->size(); ++index) {
        const core::Entity entity = (*storage)[index];
        const core::Entity copied = into.Find(from.Entities().get<core::StableId>(entity));
        if constexpr (std::is_empty_v<Component>) {
            target.emplace<Component>(copied);
        }
        else {
            target.emplace<Component>(copied, storage->get(entity));
        }
    }
}

/// CopyStorage() of each of `Components`, in their order.
template<typename... Components>
void CopyStorages(const core::Domain& from, core::Domain& into, core::ComponentList<Components...> /*components*/) {
    (CopyStorage<Components>(from, into), ...);
}

/// A copy of `made`, a world as GenerateWorld() or BuildScratch() made it, without its scene: its world domain's entities
/// and components (those a save holds) in the same storage order, its travel, relationships, calendar and world-ID
/// counter, its active domain empty and its scene-ID counter at its range's start. Far cheaper than making it again,
/// which routes every journey, and the same world, as making it is a pure function of the seed and the data.
WorldState CopyOf(const WorldState& made) {
    WorldState copy{ core::State{
        DOMAINS, made.state.Seed(), made.state.Ids(), core::StableIdCounter{ .next = core::SCENE_ID_BASE } } };
    const core::Domain& from = made.state[WORLD];
    const auto* ids = from.Entities().storage<core::StableId>();
    for (std::size_t index = 0; ids != nullptr && index < ids->size(); ++index) {
        const bool restored = copy.state.Restore(WORLD, ids->get((*ids)[index]));
        PN_ASSERT(restored, "a world's entity copied twice");
    }
    CopyStorages(from, copy.state[WORLD], WorldComponents{});
    copy.relationships = made.relationships;
    copy.travel = made.travel;
    copy.worldTicks = made.worldTicks;
    copy.months = made.months;
    copy.paused = made.paused;
    copy.mapPlace = made.mapPlace;
    return copy;
}

/// The game's world as the section WriteGameWorld() wrote holds it, its state handing out scene IDs from `sceneIds`, and
/// the base it was written against (in that order), the base made again (GenerateWorld()) from the section's seed and
/// `data`, routed over `paths` (null without a map), and the world a copy of it. Fails `archive` on anything malformed, logging
/// which of these refused it: another generator version, other world data (core::GameData::BaseHash()), a world-ID counter
/// behind the base's, a world domain's difference core::LoadDomainDelta() or order core::LoadDomainOrder() refuses, or
/// journeys Travel::LoadDelta() refuses.
/// Nothing is made for a section refused before its difference is read; worlds read from a failed archive are to be thrown
/// away.
std::pair<WorldState, WorldState> ReadGameWorld(
    core::Reader& archive,
    core::StableIdCounter sceneIds,
    const core::GameData& data,
    core::Paths* paths
) {
    u64 seed = 0;
    u32 generator = 0;
    u64 baseHash = 0;
    u64 next = 0;
    archive(seed, generator, baseHash, next);
    const Clock clock = ReadClock(archive);
    u32 version = 0;
    archive(version);
    if (!archive.Failed() && generator != GENERATOR_VERSION) {
        PN_LOG_WARNING(
            "save refused: its world was made by world generator version {}, and this game's is {}",
            generator,
            GENERATOR_VERSION
        );
        archive.Fail();
    }
    if (!archive.Failed() && baseHash != data.BaseHash()) {
        PN_LOG_WARNING(
            "save refused: its world was made from other world data (its world map and names hash to {:016x}, this game's "
            "to {:016x})",
            baseHash,
            data.BaseHash()
        );
        archive.Fail();
    }
    const core::StableIdCounter ids{ .next = next };
    if (version != WORLD_VERSION || !core::State::CountersFit(ids, sceneIds)) {
        archive.Fail();
    }
    WorldState base{ core::State{ DOMAINS, seed } };
    if (archive.Failed()) {
        // Parentheses, as MSVC warns that it can't keep a braced list's order with temporaries (C4868).
        std::pair<WorldState, WorldState> refused(WorldState(core::State(DOMAINS, seed)), std::move(base));
        return refused;
    }
    GenerateWorld(base, data, paths);
    std::pair<WorldState, WorldState> made(CopyOf(base), std::move(base));
    auto& [world, generated] = made;
    world.state.SetSceneIds(sceneIds);
    if (ids.next < world.state.Ids().next) {
        PN_LOG_WARNING(
            "save refused: its world's next ID, {}, is behind the {} its base hands out", ids.next, world.state.Ids().next
        );
        archive.Fail();
        return made;
    }
    world.state.SetIds(ids);
    core::LoadDomainDelta(archive, world.state, WORLD, WorldComponents{});
    if (archive.Failed()) {
        PN_LOG_WARNING("save refused: its world's difference from the world its seed makes is malformed");
        return made;
    }
    // In the order the saved world ran in, so the loaded one iterates as it did.
    core::LoadDomainOrder(archive, world.state[WORLD], WorldComponents{});
    if (archive.Failed()) {
        PN_LOG_WARNING("save refused: its world's order is malformed");
        return made;
    }
    world.relationships.Load(archive, clock.months); // checked against the state once the scene's domain is read too (Within())
    if (!archive.Failed() && !world.travel.LoadDelta(archive, generated.travel, world.state, data.world.get(), paths)) {
        PN_LOG_WARNING(
            "save refused: its journeys' difference from those its seed makes is malformed or doesn't fit its world"
        );
    }
    SetClock(world, clock);
    return made;
}

/// A dev scene's scratch world as the section WriteScratch() wrote holds it, its state handing out scene IDs from
/// `sceneIds`, and the base it was written against (in that order), which `makeBase()` gives (the scene's entry build,
/// BuildScratch()), and the world a copy of it. Fails `archive` on anything malformed, logging a world-ID counter behind
/// the base's, or a world domain's difference or order that core::LoadDomainDelta() or core::LoadDomainOrder() refuses;
/// and on any travel, as a dev scene never shows the world map. No base is asked for when the section is refused before
/// its difference is read; worlds read from a failed archive are to be thrown away.
template<typename MakeBase>
std::pair<WorldState, std::shared_ptr<const WorldState>> ReadScratch(
    core::Reader& archive,
    core::StableIdCounter sceneIds,
    const MakeBase& makeBase
) {
    u64 next = 0;
    archive(next);
    const Clock clock = ReadClock(archive);
    u32 version = 0;
    archive(version);
    const core::StableIdCounter ids{ .next = next };
    if (version != WORLD_VERSION || !core::State::CountersFit(ids, sceneIds)) {
        archive.Fail();
    }
    if (archive.Failed()) {
        // Parentheses, as MSVC warns that it can't keep a braced list's order with temporaries (C4868).
        std::pair<WorldState, std::shared_ptr<const WorldState>> refused(WorldState(core::State(DOMAINS, SEED)), nullptr);
        return refused;
    }
    std::shared_ptr<const WorldState> base = makeBase();
    std::pair<WorldState, std::shared_ptr<const WorldState>> made(CopyOf(*base), std::move(base));
    WorldState& world = made.first;
    world.state.SetSceneIds(sceneIds);
    if (ids.next < world.state.Ids().next) {
        PN_LOG_WARNING(
            "save refused: its scratch world's next ID, {}, is behind the {} its scene's build hands out",
            ids.next,
            world.state.Ids().next
        );
        archive.Fail();
        return made;
    }
    world.state.SetIds(ids);
    core::LoadDomainDelta(archive, world.state, WORLD, WorldComponents{});
    if (archive.Failed()) {
        PN_LOG_WARNING("save refused: its scratch world's difference from its scene's build is malformed");
        return made;
    }
    core::LoadDomainOrder(archive, world.state[WORLD], WorldComponents{});
    if (archive.Failed()) {
        PN_LOG_WARNING("save refused: its scratch world's order is malformed");
        return made;
    }
    world.relationships.Load(archive, clock.months); // checked against the state once the scene's domain is read too (Within())
    static_cast<void>(world.travel.LoadDelta(archive, Travel{}, world.state, nullptr, nullptr)); // a refusal fails `archive`
    SetClock(world, clock);
    return made;
}

/// `point` in the units of the step area round `portal`, where the step area is the unit circle.
core::Vec2 StepAreaUnits(core::Vec2 point, core::Vec2 portal) {
    const core::Vec2 away = point - portal;
    return { .x = away.x / PORTAL_RADIUS, .y = away.y / (PORTAL_RADIUS * PORTAL_DEPTH_RATIO) };
}

/// Whether `point` lies in the step area round `portal`: an ellipse PORTAL_RADIUS across and PORTAL_DEPTH_RATIO as deep,
/// as the portal's seal is drawn, its rim included.
bool InStepArea(core::Vec2 point, core::Vec2 portal) {
    const core::Vec2 units = StepAreaUnits(point, portal);
    return (units.x * units.x) + (units.y * units.y) <= 1.0F;
}

/// Clears the running scene out of `state`: each world NPC it brought in goes back to the world domain under its stable
/// ID (Deactivate()), and the scene's own entities are destroyed.
void ClearScene(core::State& state) {
    const auto* ids = std::as_const(state)[ACTIVE].Entities().storage<core::StableId>();
    if (ids == nullptr) {
        return;
    }
    std::vector<core::StableId> present;
    present.reserve(ids->size());
    for (std::size_t index = 0; index < ids->size(); ++index) {
        present.push_back(ids->get((*ids)[index]));
    }
    for (const core::StableId entity : present) {
        if (std::to_underlying(entity) >= core::SCENE_ID_BASE) {
            state.Destroy(entity);
            continue;
        }
        PN_ASSERT(
            state[ACTIVE].Entities().all_of<Npc>(state[ACTIVE].Find(entity)), "a world entity in a scene that isn't an NPC"
        );
        Deactivate(state, entity);
    }
}

/// Whether the active domain's combat components come as combat reads them: every creature has health and only
/// creatures do, a creature is dying exactly when its health is out, an arriving one has the body and orbit it
/// eases between, and only something with health is afflicted.
bool CombatConsistent(const core::Registry& entities) {
    const auto hasHealth = [&entities](core::Entity entity) { return entities.all_of<Health>(entity); };
    const auto livesOrDies = [&entities](core::Entity entity) {
        return entities.all_of<Creature>(entity) &&
               (entities.get<Health>(entity).current == 0) == entities.all_of<Dying>(entity);
    };
    const auto hasBody = [&entities](core::Entity entity) { return entities.all_of<Body>(entity); };
    const auto bodyAndOrbit = [&entities](core::Entity entity) { return entities.all_of<Body, Orbit>(entity); };
    return std::ranges::all_of(entities.view<const Creature>(), hasHealth) &&
           std::ranges::all_of(entities.view<const Health>(), livesOrDies) &&
           std::ranges::all_of(entities.view<const Dying>(), hasHealth) &&
           std::ranges::all_of(entities.view<const Arriving>(), bodyAndOrbit) &&
           std::ranges::all_of(entities.view<const Caster>(), hasHealth) &&
           std::ranges::all_of(entities.view<const Dashing>(), hasBody) &&
           std::ranges::all_of(entities.view<const Afflictions>(), hasHealth);
}

/// Whether every NPC's cell and home suit the scene: with a world map, a tile on it and a city's place index; without,
/// no home.
bool NpcsPlaced(const core::State& state, const core::WorldMap* map) {
    const auto placed = [map](const Npc& person) {
        if (map == nullptr) {
            return person.home == NO_HOME;
        }
        return person.cell < map->Columns() * map->Rows() && person.home < map->Places().size() &&
               map->Places()[person.home].kind == core::PlaceKind::City;
    };
    return std::ranges::all_of(std::to_array({ ACTIVE, WORLD }), [&](core::DomainIndex domain) {
        const auto npcs = state[domain].Entities().view<const Npc>();
        return std::ranges::all_of(npcs, [&](core::Entity entity) { return placed(npcs.get<const Npc>(entity)); });
    });
}

/// Whether every ID in `entities`, the registry of domain `domain`, is in the range its domain holds: only world-range
/// IDs in the world domain, and in the active domain a world-range ID only on an NPC.
bool IdsSuitDomain(const core::Registry& entities, core::DomainIndex domain) {
    return std::ranges::all_of(entities.view<const core::StableId>().each(), [&](const auto& row) {
        const bool worldRange = std::to_underlying(std::get<1>(row)) < core::SCENE_ID_BASE;
        if (domain == WORLD) {
            return worldRange;
        }
        return !worldRange || entities.all_of<Npc>(std::get<0>(row));
    });
}

/// Whether every entity has the components that code reads together with the ones it has: a save can
/// hold any mix, and a missing partner would be read anyway (the month step reads an Npc's Needs,
/// publishing a Look's Body, applying the game data a Creature's Look, deactivating an active Npc's Body).
bool Consistent(const core::State& state) {
    for (const core::DomainIndex domain : { ACTIVE, WORLD }) {
        const core::Registry& entities = state[domain].Entities();
        if (!IdsSuitDomain(entities, domain)) {
            return false;
        }
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
    return CombatConsistent(state[ACTIVE].Entities()); // the world domain saves no combat components
}

// The world clock's last tick: the first of the last month (LAST_MONTH), where the calendar ends.
constexpr u64 CALENDAR_END = LAST_MONTH * TICKS_PER_MONTH;
// The world clock past which nobody sets out: journeys count signed ticks, and this leaves far more room than any visit
// lasts past its month.
constexpr u64 TRAVEL_END = static_cast<u64>(std::numeric_limits<i64>::max() / 2);

/// Whether a saved world clock fits the month steps run: within the month after the last step, or at its end, the next
/// step due, and never past the calendar's end.
bool ClockFits(u64 worldTicks, u64 months) {
    const u64 month = worldTicks / TICKS_PER_MONTH;
    return worldTicks <= CALENDAR_END &&
           (month == months || (month > months && month - months == 1 && worldTicks % TICKS_PER_MONTH == 0));
}

/// Whether `world`, read from a save, holds together for `map` (null for a world that never shows the world map: a dev
/// scene's, or the game's in a game without the map): every NPC placed for it (NpcsPlaced()), its clock within its month
/// steps (ClockFits()), and neither paused nor keeping a place on the map without one; a place kept is a finite point.
bool WorldFits(const WorldState& world, const core::WorldMap* map) {
    const bool placeFits =
        !world.mapPlace || (map != nullptr && std::isfinite(world.mapPlace->x) && std::isfinite(world.mapPlace->y));
    return NpcsPlaced(world.state, map) && ClockFits(world.worldTicks, world.months) && (!world.paused || map != nullptr) &&
           placeFits;
}

/// Whether the player's route read from a save can be walked: none, or, on `map` (null where the scene shows none) with
/// the world clock at `worldTicks` within journeys' ticks, a journey of `player` alone with one tile route leg by
/// core::WALKING of a step at least that LegSound() accepts.
bool RouteFits(const std::optional<core::Journey>& route, core::StableId player, const core::WorldMap* map, u64 worldTicks) {
    if (!route) {
        return true;
    }
    if (map == nullptr || player == core::StableId{} || worldTicks > TRAVEL_END || route->members.size() != 1 ||
        route->members.front() != player || route->parts.size() != 1 || route->parts.front().stay) {
        return false;
    }
    const core::Leg& leg = route->parts.front().leg;
    return leg.profile == core::WALKING && leg.tiles.size() >= 2 && LegSound(leg, *map);
}

core::SceneId SceneIdOf(const core::Manifest& manifest, core::SceneKey scene) {
    const std::optional<core::SceneId> found = manifest.SceneNamed(SceneName(scene));
    PN_ASSERT(found.has_value(), "the manifest lacks a scene: main checks it at start");
    return found.value_or(core::SceneId{});
}

// What assets/data/creatures.yaml and abilities.yaml ship. Parts rather than one GameData, which holds the world map's
// pointer and so is neither a constant expression nor free to destroy at exit.
constexpr core::CreatureData BUILT_IN_CULTIVATOR{ .sprite = SpriteId::Cultivator,
                                                  .health = 1'000,
                                                  .walkSpeed = 4.0F, // 240 world units a second at full stick
                                                  .sight = 3 };
// A lap in about 17 seconds.
constexpr core::CreatureData BUILT_IN_FOX{ .sprite = SpriteId::Fox,
                                           .health = 1'200,
                                           .orbitRadius = 300.0F,
                                           .orbitWobble = 40.0F,
                                           .orbitSpeed = 0.006F,
                                           .orbitsPlayer = true };
constexpr core::Abilities BUILT_IN_ABILITIES{
    .qiMax = 100,
    .qiStart = 50,
    .table = { {
        {},
        { .slot = core::SlotKind::Generator,
          .cooldownTicks = 15,
          .qiGain = 10,
          .damage = 140,
          .hit = core::HitKind::Burn,
          .radius = 120.0F,
          .affliction = { .ticks = 3, .damage = 40 },
          .speed = 25.0F },
        { .slot = core::SlotKind::Spender,
          .cooldownTicks = 30,
          .qiCost = 25,
          .damage = 360,
          .hit = core::HitKind::Poison,
          .radius = 90.0F,
          .affliction = { .ticks = 4, .damage = 60 } },
        { .slot = core::SlotKind::Movement, .cooldownTicks = 60, .distance = 260.0F, .durationTicks = 9 },
        { .slot = core::SlotKind::Special,
          .cooldownTicks = 360,
          .qiCost = 60,
          .damage = 20,
          .radius = 260.0F,
          .volley = { .count = 1'000,
                      .waves = 10,
                      .ringRadius = 160.0F,
                      .ringStep = 38.0F,
                      .scatter = 0.0F,
                      .angleJitter = 0.0F,
                      .staggerTicks = 3,
                      .waveGapTicks = 9,
                      .outTicks = 7,
                      .holdTicks = 18,
                      .departJitterTicks = 15,
                      .flightTicks = 102,
                      .sweep = 420.0F,
                      .sweepJitter = 0.6F,
                      .fadeTicks = 18 },
          .seal = core::SealCast{ .key = core::SealKeyOf("sword_formation"), .size = 420.0F } },
    } },
    .loadout = { {
        core::AbilityKind::FireBolt,
        core::AbilityKind::PoisonLance,
        core::AbilityKind::Dash,
        core::AbilityKind::FlyingSwords,
        core::AbilityKind::None,
        core::AbilityKind::None,
        core::AbilityKind::None,
        core::AbilityKind::None,
    } },
};

} // namespace

core::GameData BuiltInData() {
    return core::GameData{
        .cultivator = BUILT_IN_CULTIVATOR,
        .fox = BUILT_IN_FOX,
        .abilities = BUILT_IN_ABILITIES,
        .combat = core::CombatRules{}, // the shipped rules; a test checks that abilities.yaml says the same
        .world = nullptr,
    };
}

bool Host::IsCombatScene() const {
    return Running().Traits().combat;
}

core::SceneFacts Host::FactsOf(core::SceneKey scene) const {
    return {
        .key = scene,
        .sprites = SceneIdOf(*m_manifest, scene),
        .combat = SceneOf(scene).Traits().combat,
    };
}

std::optional<core::SceneFacts> Host::Requested() const {
    if (m_loaded) {
        return FactsOf(m_loaded->scene);
    }
    return m_requested.transform([this](core::SceneKey scene) { return FactsOf(scene); });
}

WorldContext Host::LiveContext() {
    return WorldContext{ Live(), *m_manifest, m_data, m_player };
}

std::shared_ptr<const WorldState> Host::ScratchBase(core::SceneKey scene) const {
    if (m_scratchBase && scene == m_scratchBaseScene) {
        return m_scratchBase;
    }
    auto built = std::make_shared<WorldState>(core::State{ DOMAINS, SEED });
    core::StableId player{}; // the base's own, never the running scene's
    WorldContext context{ *built, *m_manifest, m_data, player };
    BuildScratch(*built, SceneOf(scene), context);
    return built;
}

void Host::EnterScene() {
    if (m_loaded) {
        m_world = std::move(m_loaded->world);
        m_base = std::move(m_loaded->base);
        m_baseHash = m_loaded->baseHash;
        m_scratch = std::move(m_loaded->scratch);
        if (m_loaded->scratchBase) { // a game scene's save keeps the last dev scene's base
            m_scratchBase = std::move(m_loaded->scratchBase);
            m_scratchBaseScene = m_loaded->scene;
        }
        m_combat = std::move(m_loaded->combat);
        m_scene = m_loaded->scene;
        m_sceneId = SceneIdOf(*m_manifest, m_scene);
        m_player = m_loaded->player;
        m_selected = m_loaded->selected;
        m_route = std::move(m_loaded->route);
        m_loaded.reset();
        m_worldTicksBefore = Live().worldTicks;
        m_requested.reset(); // the load replaces the scene a CycleScene asked for
        ApplyData();
        return;
    }
    PN_ASSERT(m_requested.has_value(), "a scene entered that nothing asked for");
    if (!m_requested) {
        return; // with assertions off, the scene and its fight stay as they are
    }
    Build(*m_requested);
}

Host::Host(std::shared_ptr<const core::Manifest> manifest, core::SceneKey scene, core::GameData data)
  : m_manifest{ std::move(manifest) }
  , m_world{ core::State{ DOMAINS, SEED } }
  , m_base{ core::State{ DOMAINS, SEED } }
  , m_data{ std::move(data) }
  , m_scene{ scene }
  , m_sceneId{ SceneIdOf(*m_manifest, m_scene) } {
    if (m_data.world != nullptr) {
        m_paths.emplace(m_data.world);
    }
    GenerateWorld(m_base, m_data, m_paths ? &*m_paths : nullptr);
    m_world = CopyOf(m_base);
    m_baseHash = m_data.BaseHash();
    Build(m_scene);
}

/// The running scene's fight, selection, player and route go, and so do its entities, the world NPCs it brought in going
/// back to the game's world; a dev scene's scratch world goes whole. Then a dev scene gets a scratch world made afresh
/// (BuildScratch()), its calendar at the start, kept as it was made for its saves to be written against (the one kept from
/// an earlier visit, which is the same world, when it was this scene), and a game scene
/// runs on the game's world, which keeps its calendar, its pause, its travel and its NPCs. The scene IDs carry on from the
/// last scene's, so none is handed out twice in a session.
void Host::Build(core::SceneKey scene) {
    const core::StableIdCounter sceneIds = Live().state.SceneIds();
    if (!m_scratch) {
        ClearScene(m_world.state);
    }
    m_combat = Combat{};
    m_combat.SetRules(m_data.combat);
    m_scene = scene;
    m_sceneId = SceneIdOf(*m_manifest, scene);
    m_requested.reset();
    m_player = {};
    m_selected = {};
    m_route.reset();
    const SceneTraits& traits = Running().Traits();
    if (traits.devScene) {
        m_scratch.emplace(core::State{ DOMAINS, SEED });
    }
    else {
        m_scratch.reset();
    }
    WorldState& world = Live();
    world.state.SetSceneIds(sceneIds);
    m_worldTicksBefore = world.worldTicks;
    PN_ASSERT(!traits.worldMap || m_data.world != nullptr, "a world scene built without the world map: CheckScenes()");
    PN_ASSERT(traits.devScene || traits.worldNpcs == 0, "a game scene spawning a crowd into the game's world");
    WorldContext context = LiveContext();
    if (traits.devScene) {
        BuildScratch(world, Running(), context);
        if (!m_scratchBase || m_scratchBaseScene != scene) {
            m_scratchBase = std::make_shared<const WorldState>(CopyOf(world));
            m_scratchBaseScene = scene;
        }
    }
    else {
        Running().Build(context);
    }
    if (traits.worldMap) {
        world.mapPlace.reset(); // the cultivator stands there again, so its foot is where it is
    }
}

void Host::CastAction(const core::Action& action, const core::TickCounters& counters, std::vector<core::Event>& events) {
    if (IsCombatScene()) {
        m_combat.Cast(Live().state, *m_manifest, m_data.abilities, m_player, action, counters, events);
        return;
    }
    PN_LOG_DEBUG("tick {}: a cast is ignored: the scene {} is no combat scene", counters.simulation, Running().Traits().name);
}

void Host::Tick(const core::TickCounters& counters, const core::TickInput& input, std::vector<core::Event>& events) {
    PN_PROFILE_ZONE();
    WorldState& world = Live();
    m_worldTicksBefore = world.worldTicks;
    if (std::ranges::contains(input.actions, core::ActionKind::CycleScene, &core::Action::kind)) {
        const core::SceneKey next = NextScene(m_scene);
        PN_LOG_INFO("tick {}: the scene changes to {}", counters.simulation, SceneName(next));
        Leave(next, false, events);
        return;
    }
    TakeActions(input.actions, counters, events);
    const bool clockRuns = Running().Traits().worldMap && !world.paused && !MonthDue() && world.worldTicks < CALENDAR_END;
    if (!Combat::Dash(world.state, m_player, counters.simulation)) {
        // A route walks to where the clock is by the end of the tick, so the portal sees the step it takes.
        Move(input.held.move, clockRuns ? world.worldTicks + 1 : world.worldTicks, counters.simulation);
    }
    if (OnPortal()) {
        const core::SceneKey next = NextWalkableScene(m_scene);
        PN_LOG_INFO("tick {}: the cultivator steps through the portal to {}", counters.simulation, SceneName(next));
        Leave(next, true, events);
        return;
    }
    if (clockRuns) {
        ++world.worldTicks;
    }
    core::Domain& active = world.state[ACTIVE];
    if (m_player != core::StableId{}) {
        const core::Vec2 player = PlayerBody().current;
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
    const u32 respawns = m_combat.Tick(world.state, counters.simulation);
    // Before the respawns, which circle the player if there still is one.
    if (m_selected != core::StableId{} && !world.state.DomainOf(m_selected)) {
        PN_LOG_INFO("tick {}: the selected entity {} is gone", counters.simulation, std::to_underlying(m_selected));
        m_selected = {};
    }
    if (m_player != core::StableId{} && !world.state.DomainOf(m_player)) {
        PN_LOG_INFO("tick {}: the player's cultivator died and is gone", counters.simulation);
        m_player = {};
    }
    WorldContext context = LiveContext();
    for (u32 index = 0; index < respawns; ++index) {
        Running().Respawn(context, counters, index);
    }
    DecideActive(active, world.state.Seed(), counters.state, counters.simulation);
    WalkActive(active);
    Running().Tick(context, counters, events);
    if (std::ranges::contains(input.actions, core::ActionKind::DebugBurst, &core::Action::kind)) {
        DebugBurst(counters, events);
    }
    // Last, once everything that moves or kills this tick has.
    m_combat.Strike(world.state, *m_manifest, counters.simulation, events);
}

void Host::TakeActions(
    std::span<const core::Action> actions,
    const core::TickCounters& counters,
    std::vector<core::Event>& events
) {
    for (const core::Action& action : actions) {
        if (action.kind == core::ActionKind::Select) {
            Select(action.target);
        }
        else if (action.kind == core::ActionKind::Cast) {
            CastAction(action, counters, events);
        }
        else if (action.kind == core::ActionKind::ToggleWorldPause) {
            ToggleWorldPause(counters.simulation);
        }
        else if (action.kind == core::ActionKind::WalkTo) {
            WalkTo(action.point, counters.simulation);
        }
    }
}

void Host::ToggleWorldPause(u64 tick) {
    if (!Running().Traits().worldMap) {
        PN_LOG_DEBUG("tick {}: the world pause is ignored: the scene {} shows no world map", tick, Running().Traits().name);
        return;
    }
    bool& paused = Live().paused;
    paused = !paused;
    PN_LOG_INFO("tick {}: the world is {}", tick, paused ? "paused" : "resumed");
}

void Host::WalkTo(core::Vec2 point, u64 tick) {
    if (!Running().Traits().worldMap) {
        PN_LOG_DEBUG("tick {}: a walk-to is ignored: the scene {} shows no world map", tick, Running().Traits().name);
        return;
    }
    // On the map a press on open ground walks rather than selecting nothing, so it must also be what lets a selection go.
    if (m_selected != core::StableId{}) {
        PN_LOG_INFO("tick {}: a walk-to lets go of the selected entity {}", tick, std::to_underlying(m_selected));
        m_selected = {};
    }
    if (m_player == core::StableId{}) {
        PN_LOG_DEBUG("tick {}: a walk-to is ignored: there is no cultivator to walk", tick);
        return;
    }
    if (Live().worldTicks > TRAVEL_END) {
        PN_LOG_DEBUG("tick {}: a walk-to is ignored: the world clock is past where journeys can be timed", tick);
        return;
    }
    const core::WorldMap& map = *m_data.world;
    const core::Rect area{ .min = {},
                           .max = { .x = static_cast<f32>(map.Columns()) * map.TileSize(),
                                    .y = static_cast<f32>(map.Rows()) * map.TileSize() } };
    if (!core::Contains(area, point)) {
        PN_LOG_DEBUG("tick {}: a walk-to ({}, {}) off the map is ignored", tick, point.x, point.y);
        return;
    }
    const core::Vec2 foot = PlayerBody().current;
    const u32 target = map.TileAt(point);
    const std::vector<u32> tiles = core::FindRoute(map, core::WALKING, map.TileAt(foot), target);
    if (tiles.empty()) {
        PN_LOG_DEBUG("tick {}: a walk-to tile {} is ignored: no way there on foot", tick, target);
        return;
    }
    if (tiles.size() == 1) {
        // A route of the one tile would jump the foot to its centre, or be a step of no ticks that never ends.
        m_route.reset();
        PN_LOG_DEBUG("tick {}: the cultivator already stands on tile {} and stops there", tick, target);
        return;
    }
    core::Journey route;
    route.members = { m_player };
    route.parts.push_back(
        { .stay = false, .leg = core::MakeTileLeg(map, core::WALKING, static_cast<i64>(Live().worldTicks), foot, tiles) }
    );
    PN_LOG_INFO(
        "tick {}: the cultivator sets out for tile {}, {} tiles on, arriving at world tick {}",
        tick,
        target,
        tiles.size() - 1,
        core::EndTick(route)
    );
    m_route = std::move(route);
}

void Host::Move(core::Vec2 move, u64 worldTick, u64 tick) {
    if (m_route && (move.x != 0.0F || move.y != 0.0F)) {
        m_route.reset();
        PN_LOG_DEBUG("tick {}: held movement ends the cultivator's route", tick);
    }
    if (Live().paused) {
        Walk({}); // the cultivator stands, any route held
        return;
    }
    if (m_route) {
        FollowRoute(worldTick, tick);
        return;
    }
    Walk(move);
}

void Host::FollowRoute(u64 worldTick, u64 tick) {
    if (!m_route || m_player == core::StableId{}) {
        return;
    }
    const core::Journey& route = *m_route;
    Body& body = PlayerBody();
    const auto clock = static_cast<i64>(worldTick);
    body.previous = body.current;
    body.current = core::Where(*m_data.world, route, clock);
    if (clock >= core::EndTick(route)) {
        PN_LOG_DEBUG("tick {}: the cultivator reaches the end of its route", tick);
        m_route.reset();
    }
}

Body& Host::PlayerBody() {
    core::Domain& active = Live().state[ACTIVE];
    return active.Entities().get<Body>(active.Find(m_player));
}

const Body& Host::PlayerBody() const {
    const core::Domain& active = Live().state[ACTIVE];
    return active.Entities().get<Body>(active.Find(m_player));
}

void Host::Leave(core::SceneKey next, bool throughPortal, std::vector<core::Event>& events) {
    m_combat.EndUses(Live().state, events); // nothing of this scene's goes on to hit
    if (Running().Traits().worldMap && m_player != core::StableId{}) {
        // Where it stood before it touched the portal, so it never comes back onto the portal and straight out again.
        const Body& body = PlayerBody();
        const core::Vec2 place = throughPortal ? body.previous : body.current;
        // A month step's jump can leave it standing in the step area, where a place kept would send it straight out.
        if (OnMapPortal(place)) {
            Live().mapPlace.reset();
            PN_LOG_DEBUG("the cultivator left the world map from its portal, so it will come back to the start city");
        }
        else {
            Live().mapPlace = place;
            PN_LOG_DEBUG("the cultivator will come back to the world map at ({}, {})", place.x, place.y);
        }
    }
    m_requested = next; // the logic has it built after this tick
}

std::optional<core::Vec2> Host::Portal() {
    const WorldContext context = LiveContext();
    return Running().Portal(context);
}

core::Rect Host::View() {
    const WorldContext context = LiveContext();
    return Running().View(context);
}

bool Host::OnPortal() {
    const std::optional<core::Vec2> portal = Portal();
    if (!portal || m_player == core::StableId{}) {
        return false;
    }
    // The foot's whole path this tick, not only where it ends: a dash moves farther in a tick than the step area is deep.
    // Its nearest point to the portal is found in the step area's own units, where the area is the unit circle.
    const Body& body = PlayerBody();
    const core::Vec2 from = StepAreaUnits(body.previous, *portal);
    const core::Vec2 path = StepAreaUnits(body.current, *portal) - from;
    const f32 length = (path.x * path.x) + (path.y * path.y);
    const f32 along = length > 0.0F ? std::clamp(-((from.x * path.x) + (from.y * path.y)) / length, 0.0F, 1.0F) : 0.0F;
    return InStepArea(body.previous + ((body.current - body.previous) * along), *portal);
}

bool Host::OnMapPortal(core::Vec2 point) {
    const WorldContext context = LiveContext();
    return std::ranges::any_of(Scenes(), [&](const game::Scene* scene) {
        const std::optional<core::Vec2> portal = scene->Traits().worldMap ? scene->Portal(context) : std::nullopt;
        return portal.has_value() && InStepArea(point, *portal);
    });
}

void Host::PublishPortal(core::Snapshot& snapshot) {
    PN_PROFILE_ZONE();
    const std::optional<core::Vec2> portal = Portal();
    if (!portal) {
        return;
    }
    snapshot.seals.push_back(
        { .key = PORTAL_SEAL, .point = *portal, .size = PORTAL_SEAL_SIZE, .startTick = 0, .endTick = core::NEVER_ENDS }
    );
    const core::Vec2 over = *portal + PORTAL_LABEL_RISE;
    snapshot.texts.push_back(
        {
            .kind = core::TextKind::Caption,
            .string = core::StringId::PortalLabel,
            .previous = over,
            .current = over,
            .align = core::TextAlign::BottomCentre,
        }
    );
}

void Host::PublishOverhead(core::Snapshot& snapshot) const {
    PN_PROFILE_ZONE();
    if (m_player == core::StableId{}) {
        return;
    }
    const core::Registry& entities = Live().state[ACTIVE].Entities();
    const bool combat = IsCombatScene();
    for (auto [entity, creature, body, look, stableId, health] :
         entities.view<const Creature, const Body, const Look, const core::StableId, const Health>().each()) {
        const bool player = stableId == m_player;
        if ((!player && creature.kind != CreatureKind::Fox) || entities.all_of<Dying>(entity)) {
            continue;
        }
        const core::StringId name = player ? core::StringId::CultivatorName : core::StringId::FoxName;
        const f32 height = static_cast<f32>(m_manifest->Sprite(look.sprite).footY) * look.scale;
        const core::Vec2 head{ .x = 0.0F, .y = -height };
        if (combat && !player) {
            snapshot.healthBars.push_back(
                {
                    .entity = stableId,
                    .previous = body.previous + head,
                    .current = body.current + head,
                    .fraction = static_cast<f32>(static_cast<f64>(health.current) / static_cast<f64>(health.max)),
                }
            );
        }
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
            }
        );
    }
}

u32 Host::JourneyChunks() const {
    // Past TRAVEL_END the clock no longer fits the journeys' signed ticks.
    if (!Running().Traits().worldMap || m_data.world == nullptr || Live().worldTicks > TRAVEL_END) {
        return 0;
    }
    return core::ChunkCount(Live().travel.Journeys().size(), PUBLISH_CHUNK);
}

void Host::PublishJourneys(u32 chunk, u32 part, const core::Rect& area) {
    const core::WorldMap& map = *m_data.world;
    const WorldState& world = Live();
    const std::span<const core::Journey> journeys = world.travel.Journeys();
    const auto now = static_cast<i64>(world.worldTicks);
    const auto before = static_cast<i64>(m_worldTicksBefore);
    const Look look{ .sprite = SpriteId::Villager, .shadow = true };
    const f32 height = static_cast<f32>(m_manifest->Sprite(look.sprite).footY) * look.scale;
    const core::Vec2 head{ .x = 0.0F, .y = -height };
    const core::Names* const names = m_data.names.get();
    std::vector<core::TextItem>& labels = m_travellerLabels[chunk];
    const std::size_t begin = std::size_t{ chunk } * PUBLISH_CHUNK;
    const std::size_t end = std::min(journeys.size(), begin + PUBLISH_CHUNK);
    for (std::size_t index = begin; index < end; ++index) {
        const core::Journey& journey = journeys[index];
        if (!core::Underway(journey, now)) {
            continue;
        }
        for (std::size_t member = 0; member < journey.members.size(); ++member) {
            const core::Vec2 current = core::Where(map, journey, now, member);
            if (!core::Contains(area, current)) {
                continue;
            }
            const core::Vec2 previous = core::Where(map, journey, before, member);
            const core::StableId traveller = journey.members[member];
            m_drawList.Add(
                part,
                core::Layer::Decals,
                core::Pipeline::Normal,
                core::MakeInstance(*m_manifest, { .sprite = SpriteId::Shadow, .previous = previous, .current = current })
            );
            m_drawList.Add(
                part,
                look.layer,
                look.pipeline,
                core::MakeInstance(
                    *m_manifest, { .sprite = look.sprite, .previous = previous, .current = current, .scale = look.scale }
                ),
                current.y,
                traveller
            );
            if (names == nullptr) {
                continue; // a game without names has nothing to call them
            }
            const core::NpcName name = core::NameOf(*names, world.state.Seed(), traveller);
            labels.push_back(
                {
                    .kind = core::TextKind::Label,
                    .npcName = name,
                    .previous = previous + head,
                    .current = current + head,
                    .offset = LABEL_OFFSET,
                    .entity = traveller,
                    .height = height,
                    .nameKey = core::NameKey(name),
                    .priority = traveller == m_selected ? core::LabelPriority::Selected : core::LabelPriority::Named,
                }
            );
        }
    }
}

void Host::PublishCaster(core::Snapshot& snapshot) const {
    if (m_player == core::StableId{} || !IsCombatScene()) {
        return;
    }
    const core::Domain& active = Live().state[ACTIVE];
    const core::Registry& entities = active.Entities();
    const core::Entity player = active.Find(m_player);
    const Caster* const caster = entities.try_get<Caster>(player);
    const Health* const health = entities.try_get<Health>(player);
    if (caster == nullptr || health == nullptr) {
        return;
    }
    snapshot.caster = core::CasterView{
        .qi = caster->qi,
        .qiMax = m_data.abilities.qiMax,
        .health = health->current,
        .healthMax = health->max,
        .readyTick = caster->readyTick,
        .loadout = m_data.abilities.loadout,
    };
}

void Host::PublishRoute(core::Snapshot& snapshot) const {
    PN_PROFILE_ZONE();
    if (!m_route || m_player == core::StableId{}) {
        return;
    }
    const auto now = static_cast<i64>(Live().worldTicks);
    snapshot.route.push_back(PlayerBody().current);
    for (const core::JourneyPart& part : m_route->parts) {
        for (std::size_t tile = 0; !part.stay && tile < part.leg.tiles.size(); ++tile) {
            if (part.leg.startTick + i64{ part.leg.arrive[tile] } > now) {
                snapshot.route.push_back(m_data.world->TileCentre(part.leg.tiles[tile]));
            }
        }
    }
    if (snapshot.route.size() == 1) {
        snapshot.route.clear(); // nothing left to walk, so no line and no end to mark
    }
}

void Host::DebugBurst(const core::TickCounters& counters, std::vector<core::Event>& events) const {
    PN_PROFILE_ZONE();
    const u64 use = core::AbilityUse(counters.simulation, USE_DEBUG_BURST);
    const core::Registry& entities = Live().state[ACTIVE].Entities();
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
        const u64 random = core::Random(Live().state.Seed(), counters.state, std::to_underlying(target), PURPOSE_BURST, index);
        events.push_back(
            {
                .kind = core::EventKind::Hit,
                // The kinds take turns by round over the foxes, so each fox gets every kind whatever their number.
                .hit = static_cast<core::HitKind>((index / foxes.size()) % core::HIT_KINDS),
                .use = use,
                .target = target,
                .position = HeadOf(entities, *m_manifest, fox),
                .value = 1 + static_cast<i64>(core::ToBelow(random, DEBUG_BURST_MAX)),
            }
        );
    }
    events.push_back({ .kind = core::EventKind::UseEnded, .use = use });
}

void Host::Walk(core::Vec2 move) {
    if (m_player == core::StableId{}) {
        return;
    }
    core::Domain& active = Live().state[ACTIVE];
    Body& body = active.Entities().get<Body>(active.Find(m_player));
    body.previous = body.current; // every tick, so a cultivator standing still draws still
    body.current = body.current + (move * m_data.cultivator.walkSpeed);
}

void Host::Select(core::StableId target) {
    // A dying creature can't be picked, as its instance names none.
    const core::State& state = Live().state;
    const bool dying = state.DomainOf(target) == ACTIVE && state[ACTIVE].Entities().all_of<Dying>(state[ACTIVE].Find(target));
    m_selected = state.DomainOf(target) && !dying ? target : core::StableId{};
    PN_LOG_INFO(
        "selected {}", m_selected == core::StableId{} ? "nothing" : std::format("entity {}", std::to_underlying(target))
    );
}

void Host::AdvanceCalendar(const core::TickCounters& counters, core::Jobs& jobs, const std::stop_token& stop) {
    WorldState& world = Live();
    if (world.months < LAST_MONTH) {
        // A step asked for early (M) moves the clock on to the next month's first tick, so the calendar keeps up with the
        // steps.
        const u64 nextMonth = (world.months + 1) * TICKS_PER_MONTH;
        if (world.worldTicks < nextMonth) {
            world.worldTicks = nextMonth;
            m_worldTicksBefore = world.worldTicks; // rather than draw travellers streaking across the skipped days
            if (m_route) {
                FollowRoute(world.worldTicks, counters.simulation);
                PlayerBody().previous = PlayerBody().current; // it stands there a frame, as the travellers do
            }
        }
        ++world.months;
        PN_LOG_INFO("month {} begins at world tick {}", world.months + 1, world.worldTicks);
    }
    // Journeys end and begin before the month's actions, so those back act again and those leaving sit the month out.
    if (Running().Traits().worldMap && m_paths && world.worldTicks <= TRAVEL_END && !stop.stop_requested()) {
        world.travel.Finish(static_cast<i64>(world.worldTicks), world.state);
        world.travel.Month(
            world.state,
            *m_data.world,
            *m_paths,
            jobs,
            world.state.Seed(),
            counters.state,
            static_cast<i64>(world.months * TICKS_PER_MONTH)
        );
    }
    m_monthStep.Run(world.state, world.relationships, jobs, counters.state, world.months, stop);
}

bool Host::MonthDue() const {
    const WorldState& world = Live();
    return world.worldTicks / TICKS_PER_MONTH > world.months;
}

template<typename Archive>
void Host::WriteScene(Archive& archive) const {
    archive(m_player, m_selected, ACTIVE_VERSION);
    core::SaveDomain(archive, Live().state[ACTIVE], ActiveComponents{});
    archive(m_route.has_value());
    if (m_route) {
        archive(*m_route);
    }
    m_combat.Save(archive);
}

void Host::Save(core::Writer& archive) {
    PN_PROFILE_ZONE();
    // The scene stays one byte, as the scene enum was.
    archive(ARENA_FORMAT, static_cast<u8>(std::to_underlying(m_scene)), Live().state.SceneIds().next);
    WriteGameWorld(archive, m_world, m_base, m_baseHash, m_data.world.get(), m_paths ? &*m_paths : nullptr);
    PN_ASSERT(
        !m_scratch || (m_scratchBase && m_scratchBaseScene == m_scene), "a scratch world without the build it was made as"
    );
    if (m_scratch && m_scratchBase) {
        WriteScratch(archive, *m_scratch, *m_scratchBase);
    }
    WriteScene(archive);
}

u64 Host::StateHash() const {
    PN_PROFILE_ZONE();
    core::Hasher hasher;
    hasher(ARENA_FORMAT, static_cast<u8>(std::to_underlying(m_scene)), Live().state.SceneIds().next);
    HashWorld(hasher, Live());
    WriteScene(hasher);
    return hasher.Value();
}

u64 Host::WorldHash() const {
    core::Hasher hasher;
    HashWorld(hasher, m_world);
    return hasher.Value();
}

bool Host::Load(core::Reader& archive, u64 simulationTick) {
    PN_PROFILE_ZONE();
    u32 format = 0;
    u8 scene = 0;
    u64 sceneNext = 0;
    archive(format, scene, sceneNext);
    const bool sceneKnown = scene < Scenes().size();
    const SceneTraits* const traits = sceneKnown ? &SceneOf(core::SceneKey{ scene }).Traits() : nullptr;
    // A scene that shows the world map can't run without it, so a save of one is refused by a game that lacks the map.
    if (format != ARENA_FORMAT || traits == nullptr || (traits->worldMap && m_data.world == nullptr)) {
        archive.Fail();
    }
    const core::StableIdCounter sceneIds{ .next = sceneNext };
    const core::WorldMap* const map = m_data.world.get();
    auto [world, base] = ReadGameWorld(archive, sceneIds, m_data, m_paths ? &*m_paths : nullptr);
    std::optional<WorldState> scratch = std::nullopt;
    std::shared_ptr<const WorldState> scratchBase;
    if (traits != nullptr && traits->devScene) {
        auto [read, built] = ReadScratch(archive, sceneIds, [this, scene] { return ScratchBase(core::SceneKey{ scene }); });
        scratch.emplace(std::move(read));
        scratchBase = std::move(built);
    }
    WorldState& running = scratch ? *scratch : world;
    core::StableId player{};
    core::StableId selected{};
    u32 version = 0;
    archive(player, selected, version);
    if (version != ACTIVE_VERSION) {
        archive.Fail();
    }
    core::LoadDomain(archive, running.state, ACTIVE, ActiveComponents{});
    bool walking = false;
    archive(walking);
    std::optional<core::Journey> route = std::nullopt;
    if (walking) {
        archive(route.emplace());
    }
    Combat combat;
    combat.Load(archive);
    const bool sceneHasMap = traits != nullptr && traits->worldMap;
    // Checked only now the scene's domain is read too, as a relationship may name a world NPC the scene brought in.
    const bool worldsFit = Consistent(world.state) && WorldFits(world, map) && world.relationships.Within(world.state) &&
                           (!world.mapPlace || !OnMapPortal(*world.mapPlace)) &&
                           (!scratch || (Consistent(scratch->state) && WorldFits(*scratch, nullptr) &&
                                         scratch->relationships.Within(scratch->state)));
    const core::State& state = running.state;
    const bool playerFound = player == core::StableId{} || (state.DomainOf(player) == ACTIVE &&
                                                            state[ACTIVE].Entities().all_of<Body>(state[ACTIVE].Find(player)));
    const bool routeFits = RouteFits(route, player, sceneHasMap ? map : nullptr, running.worldTicks);
    if (archive.Failed() || archive.Remaining() != 0 || !worldsFit || !playerFound || !routeFits ||
        !combat.AimsAtCreatures(state) || !combat.FlashesStartBy(simulationTick) || !combat.SealsStartBy(simulationTick)) {
        return false;
    }
    m_loaded.emplace(
        std::move(world),
        std::move(base),
        m_data.BaseHash(),
        std::move(scratch),
        std::move(scratchBase),
        std::move(combat),
        core::SceneKey{ scene },
        player,
        selected,
        std::move(route)
    );
    return true;
}

void Host::SetData(const core::GameData& data) {
    if (data.world != m_data.world) {
        m_paths.reset();
        if (data.world != nullptr) {
            m_paths.emplace(data.world);
        }
    }
    m_data = data;
    ApplyData();
}

void Host::ApplyData() {
    m_combat.SetRules(m_data.combat);
    core::Registry& entities = Live().state[ACTIVE].Entities();
    for (auto [entity, creature, look] : entities.view<const Creature, Look>().each()) {
        const core::CreatureData& kind = DataOf(creature.kind);
        look.sprite = kind.sprite;
        if (Health* const health = entities.try_get<Health>(entity); health != nullptr && health->current > 0) {
            health->max = kind.health;
            health->current = std::min(health->current, kind.health);
        }
        if (Caster* const caster = entities.try_get<Caster>(entity)) {
            caster->qi = std::min(caster->qi, m_data.abilities.qiMax);
        }
        if (Orbit* const orbit = entities.try_get<Orbit>(entity)) {
            if (!kind.orbitsPlayer) {
                orbit->centre = Running().Traits().home; // back from wherever the player led it
            }
            orbit->radius = kind.orbitRadius;
            orbit->wobble = kind.orbitWobble;
            orbit->speed = kind.orbitSpeed;
        }
    }
}

core::GlowInput DemoGlow(core::GlowRole role, u64 tick) {
    if (role == core::GlowRole::DemoPulse) {
        return { .role = role };
    }
    if (role == core::GlowRole::DemoCharge) {
        const u64 age = tick % CHARGE_LOOP;
        const bool released = age >= CHARGE_RELEASE;
        return { .role = role,
                 .previousValue = age == 0 ? 0.0F : Rise(age - 1, CHARGE_RISE),
                 .currentValue = Rise(age, CHARGE_RISE),
                 .startTick = released ? tick - age + CHARGE_RELEASE : 0,
                 .released = released };
    }
    if (role == core::GlowRole::DemoProgress) {
        const u64 age = tick % PROGRESS_LOOP;
        const u8 result = age >= PROGRESS_FILL ? static_cast<u8>(1 + ((tick / PROGRESS_LOOP) % PROGRESS_RESULTS)) : u8{ 0 };
        return { .role = role,
                 .previousValue = age == 0 ? 0.0F : Rise(age - 1, PROGRESS_FILL),
                 .currentValue = Rise(age, PROGRESS_FILL),
                 .result = result };
    }
    return {};
}

void Host::Publish(core::Snapshot& snapshot, core::Jobs& jobs, const std::optional<core::Rect>& view) {
    PN_PROFILE_ZONE();
    // Fetched before the fork: a storage asked for the first time is made, which chunks mustn't race to do.
    WorldState& world = Live();
    core::Domain& active = world.state[ACTIVE];
    const auto& bodies = active.Entities().storage<Body>();
    const auto& looks = active.Entities().storage<Look>();
    const auto& ids = active.Entities().storage<core::StableId>();
    const auto& spins = active.Entities().storage<Spin>();
    const auto& dyings = active.Entities().storage<Dying>();
    const u64 tick = snapshot.simulationTick;
    const SceneTraits& traits = Running().Traits();
    const std::span<const DemoGlowSprite> demoGlows = traits.demoGlows;
    // On the map, crowds stand on the cities' tiles where the cultivator starts: the player never loses sight of it.
    const core::StableId onTop = traits.worldMap ? m_player : core::StableId{};
    snapshot.combat = traits.combat;
    snapshot.camera = traits.camera;
    snapshot.worldMap = traits.worldMap;
    if (traits.worldMap) {
        snapshot.calendar = core::CalendarView{
            .month = world.worldTicks / TICKS_PER_MONTH,
            .day = static_cast<u32>((world.worldTicks % TICKS_PER_MONTH) / TICKS_PER_DAY),
            .paused = world.paused,
        };
        PublishRoute(snapshot);
    }
    const u32 chunks = core::ChunkCount(looks.size(), PUBLISH_CHUNK);
    const WorldContext context = LiveContext();
    const core::Rect sceneView = Running().View(context);
    core::Vec2 start = (sceneView.min + sceneView.max) * MIDDLE;
    if (m_player != core::StableId{}) {
        const Body& body = bodies.get(active.Find(m_player));
        snapshot.follow = core::Follow{ .previous = body.previous, .current = body.current };
        start = body.current;
    }
    // Before the main thread's view arrives the camera's zoom and window are unknown, so cull as widely as any shows.
    const core::Rect area =
        view.value_or(core::Rect{ .min = start - core::WIDEST_HALF_VIEW, .max = start + core::WIDEST_HALF_VIEW });
    // The ground takes the first part whether or not there are entities; the journeys' parts follow the entities'.
    const u32 entityParts = std::max(chunks, 1U);
    const u32 journeyChunks = JourneyChunks();
    m_travellerLabels.resize(journeyChunks);
    for (std::vector<core::TextItem>& labels : m_travellerLabels) {
        labels.clear();
    }
    m_drawList.Begin(area, entityParts + journeyChunks);
    Running().PublishGround(context, area, m_drawList, 0);
    jobs.ParallelFor(chunks + journeyChunks, [&](u32 chunk) {
        if (chunk >= chunks) {
            PublishJourneys(chunk - chunks, entityParts + (chunk - chunks), area);
            return;
        }
        const std::size_t begin = std::size_t{ chunk } * PUBLISH_CHUNK;
        const std::size_t end = std::min(looks.size(), begin + PUBLISH_CHUNK);
        for (std::size_t i = begin; i < end; ++i) {
            const core::Entity entity = looks[i];
            const Body& body = bodies.get(entity);
            const Look& look = looks.get(entity);
            const core::StableId stableId = ids.get(entity);
            const Spin spin = spins.contains(entity) ? spins.get(entity) : Spin{};
            // A dying entity can't be picked, hovered or outlined: its instance names none.
            const bool dying = dyings.contains(entity);
            const f32 dissolve = dying ? DissolveAt(dyings.get(entity), tick, m_data.combat.dyingTicks) : 0.0F;
            if (look.shadow) {
                m_drawList.Add(
                    chunk,
                    core::Layer::Decals,
                    core::Pipeline::Normal,
                    core::MakeInstance(
                        *m_manifest,
                        { .sprite = SpriteId::Shadow, .previous = body.previous, .current = body.current, .dissolve = dissolve }
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
                      .currentRotation = spin.current,
                      .dissolve = dissolve,
                      .glow = DemoGlow(DemoRoleOf(demoGlows, look.sprite), tick) }
                ),
                SortingFootY(stableId, onTop, body.current.y),
                dying ? core::StableId{} : stableId
            );
        }
    });
    m_drawList.Publish(snapshot, jobs);
    PublishOverhead(snapshot);
    for (const std::vector<core::TextItem>& labels : m_travellerLabels) {
        snapshot.texts.insert(snapshot.texts.end(), labels.begin(), labels.end());
    }
    PublishCaster(snapshot);
    // Before the seals raised in play, which draw over the scene's own.
    Running().Publish(context, area, snapshot);
    PublishPortal(snapshot);
    m_combat.Publish(snapshot);
    if (m_selected != core::StableId{}) {
        if (const auto drawn = std::ranges::find(snapshot.ids, m_selected); drawn != snapshot.ids.end()) {
            snapshot.selected = static_cast<u32>(drawn - snapshot.ids.begin());
        }
    }
}

} // namespace game
