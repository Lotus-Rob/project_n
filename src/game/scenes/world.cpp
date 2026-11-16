#include "game/scenes/world.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"
#include "core/random.hpp"
#include "core/rect.hpp"
#include "game/npcs.hpp"
#include "game/random_purposes.hpp"

namespace game {

namespace {

constexpr SceneTraits WORLD_TRAITS{
    .name = "world",
    .creatures = true,
    .camera = core::CameraKind::Roam,
    .worldMap = true,
    .portalFromMap = true,
    .viewFromMap = true,
};

// Of every ten of a city's people, this many live on the city's own tile; the rest are spread over its region.
constexpr u32 TENTHS_ON_CITY_TILE = 7;
constexpr u32 TENTH = 10;

// The stable ID of the place `index`'s name label: far above any creature's, which count up from 1.
constexpr u64 PLACE_ENTITY_BASE = 1ULL << 40;
// The gap between the city marker's lower edge and the name hanging from it, in world units.
constexpr f32 PLACE_NAME_GAP = 8.0F;

/// Spawns the cities' people, city by city in place order, `npc` counting them all: the first seven tenths of a city's share
/// on its own tile, each of the rest on a tile of its region drawn for its running index; each starts with StartingPower().
void SpawnPeople(SceneContext& context, const core::WorldMap& map) {
    const u64 seed = context.State().Seed();
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
            const core::StableId identity{ context.State().Ids().next };
            const core::StableId spawned = SpawnNpc(context.State(), tile, city, StartingPower(seed, identity));
            PN_ASSERT(spawned == identity, "an NPC spawned under another stable ID than the one its power was drawn for");
        }
    }
}

} // namespace

const SceneTraits& WorldScene::Traits() const {
    return WORLD_TRAITS;
}

std::optional<core::Vec2> WorldScene::Portal(const SceneContext& context) const {
    if (context.Data().world == nullptr) {
        return std::nullopt;
    }
    return context.Data().world->StartCentre() + WORLD_PORTAL_OFFSET;
}

core::Rect WorldScene::View(const SceneContext& context) const {
    if (context.Data().world == nullptr) {
        return {};
    }
    const core::WorldMap& map = *context.Data().world;
    return { .min = {},
             .max = { .x = static_cast<f32>(map.Columns()) * map.TileSize(),
                      .y = static_cast<f32>(map.Rows()) * map.TileSize() } };
}

void WorldScene::Build(SceneContext& context) const {
    const core::WorldMap& map = *context.Data().world;
    context.SpawnPlayer(map.StartCentre());
    SpawnPeople(context, map);
    context.SeedTravel();
}

void WorldScene::Publish(const SceneContext& context, core::Rect view, core::Snapshot& snapshot) const {
    PN_PROFILE_ZONE();
    const core::WorldMap& map = *context.Data().world;
    // Every place's marker (world_look.yaml picks one per city and danger sort) shares the city marker's size below the
    // tile's centre; a test checks they do.
    const core::SpriteInfo& marker = context.Manifest().Sprite(core::atlas::SpriteId::MarkerCity);
    const f32 markerBelow = static_cast<f32>(marker.height - marker.footY);
    for (std::size_t index = 0; index < map.Places().size(); ++index) {
        const core::Place& place = map.Places()[index];
        const core::Vec2 centre = map.TileCentre(map.TileIndex(place.tile));
        if (!core::Contains(view, centre)) {
            continue;
        }
        // The name hangs under the marker drawn round the tile's centre, like a map label under its town: the cultivator
        // stands on the start city with its own label over its head, so a name above the tile would sit on that label.
        const core::Vec2 anchor = centre + core::Vec2{ .x = 0.0F, .y = markerBelow + PLACE_NAME_GAP };
        snapshot.texts.push_back(
            {
                .kind = core::TextKind::Place,
                .string = place.name,
                .previous = anchor,
                .current = anchor,
                .entity = static_cast<core::StableId>(PLACE_ENTITY_BASE + index),
                .nameKey = std::to_underlying(place.name),
                .priority = core::LabelPriority::Place,
                .align = core::TextAlign::TopCentre,
            }
        );
    }
}

} // namespace game
