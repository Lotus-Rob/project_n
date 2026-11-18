#include "game/world_state.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/random.hpp"
#include "core/world_map.hpp"
#include "game/random_purposes.hpp"

namespace game {

namespace {

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
    SpawnPeople(world.state, *data.world);
    if (paths != nullptr) { // with assertions off, a map without routes gives a world whose NPCs never set out
        world.travel.Seed(world.state, *data.world, *paths, world.state.Seed(), 0, 0);
    }
    // A save sorts the state and a load gives it back sorted: a world that starts sorted iterates alike either way.
    world.state.SortCanonical();
}

} // namespace game
