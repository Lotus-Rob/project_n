#include "game/world_state.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/fog.hpp"
#include "core/random.hpp"
#include "core/world_map.hpp"
#include "game/combat.hpp"
#include "game/random_purposes.hpp"

namespace game {

namespace {

// A dev scene's crowd stands on the cells of a 64 by 64 grid in turn.
constexpr u32 WORLD_CELLS = 4'096;

// Of every ten of a city's people, this many live on the city's own tile; the rest are spread over its region.
constexpr u32 TENTHS_ON_CITY_TILE = 7;
constexpr u32 TENTH = 10;

/// Spawns the cities' people into `state`, city by city in place order, `npc` counting them all: the first seven tenths
/// of a city's share on its own tile, each of the rest on a tile of its region drawn for its running index; each starts
/// with StartingPower().
void SpawnPeople(core::State& state, const core::WorldMap& map) {
    const u64 seed = state.Seed();
    u32 npc = 0;
    for (std::size_t index = 0; index < map.Places().size(); ++index) {
        const core::Place& place = map.Places()[index];
        if (place.kind != core::PlaceKind::City) {
            continue;
        }
        const u16 city = static_cast<u16>(index);
        const u32 cityTile = map.TileIndex(place.tile);
        const std::span<const u32> region = map.RegionTiles(city);
        const u32 onCityTile = place.people * TENTHS_ON_CITY_TILE / TENTH;
        for (u32 member = 0; member < place.people; ++member, ++npc) {
            u32 tile = cityTile;
            if (member >= onCityTile && !region.empty()) {
                tile = region[core::ToBelow(core::Random(seed, 0, npc, PURPOSE_HOME_TILE, 0), static_cast<u32>(region.size()))];
            }
            // the NPC's identity is the next stable ID the state hands out
            const core::StableId identity{ state.Ids().next };
            const core::StableId spawned = SpawnNpc(state, tile, city, StartingPower(seed, identity));
            PN_ASSERT(spawned == identity, "an NPC spawned under another stable ID than the one its power was drawn for");
        }
    }
}

} // namespace

void GenerateWorld(WorldState& world, const core::GameData& data, core::Paths* paths) {
    PN_ASSERT(
        world.state[WORLD].Entities().view<const core::StableId>().empty() && world.months == 0 && world.worldTicks == 0,
        "a world generated over one already made or under way"
    );
    if (data.world == nullptr) {
        return;
    }
    PN_ASSERT(paths != nullptr, "a world with a map generated without its routes");
    const core::WorldMap& map = *data.world;
    world.explored.assign(core::FogWords(static_cast<std::size_t>(map.Columns()) * map.Rows()), 0);
    world.sightRadius = data.cultivator.sight;
    SpawnPeople(world.state, map);
    if (paths != nullptr) { // with assertions off, a map without routes gives a world whose NPCs never set out
        world.travel.Seed(world.state, map, *paths, world.state.Seed(), 0, 0);
    }
    // Stable-ID order, which the month step and travel visit the NPCs in, rather than the order generation made them in.
    world.state.SortCanonical();
}

WorldContext::~WorldContext() = default; // out of line: the vtable's home

core::Entity WorldContext::Spawn(core::Vec2 foot, const Look& look) {
    core::State& state = m_world->state;
    const core::Entity entity = state.Create(ACTIVE, core::IdRange::Scene).second;
    state[ACTIVE].Entities().emplace<Body>(entity, foot, foot);
    state[ACTIVE].Entities().emplace<Look>(entity, look);
    return entity;
}

core::Entity WorldContext::SpawnFox(const Orbit& orbit, core::Vec2 foot) {
    const core::Entity entity = Spawn(foot, { .sprite = m_data->fox.sprite, .shadow = true });
    core::Registry& entities = m_world->state[ACTIVE].Entities();
    entities.emplace<Orbit>(entity, orbit);
    entities.emplace<Creature>(entity, CreatureKind::Fox);
    entities.emplace<Health>(entity, m_data->fox.health, m_data->fox.health);
    return entity;
}

core::Entity WorldContext::SpawnPlayer(core::Vec2 foot) {
    const core::Entity entity = Spawn(foot, { .sprite = m_data->cultivator.sprite, .shadow = true });
    core::Registry& entities = m_world->state[ACTIVE].Entities();
    entities.emplace<Creature>(entity, CreatureKind::Cultivator);
    entities.emplace<Health>(entity, m_data->cultivator.health, m_data->cultivator.health);
    entities.emplace<Caster>(entity, m_data->abilities.qiStart);
    *m_player = entities.get<core::StableId>(entity);
    return entity;
}

void WorldContext::Damage(core::StableId target, i64 amount, u64 tick) {
    Combat::Damage(m_world->state, target, amount, tick);
}

void BuildScratch(WorldState& world, const Scene& scene, SceneContext& context) {
    PN_ASSERT(scene.Traits().devScene, "a game scene built as a dev scene's scratch world");
    PN_ASSERT(&context.State() == &world.state, "a dev scene built into another world than its context's");
    scene.Build(context);
    for (u32 npc = 0; npc < scene.Traits().worldNpcs; ++npc) {
        SpawnNpc(world.state, npc % WORLD_CELLS);
    }
}

} // namespace game
