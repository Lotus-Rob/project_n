#include "game/scenes/world.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"
#include "core/rect.hpp"

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

// The stable ID of the place `index`'s name label: far above any world NPC's, which count up from 1, and below the scene's
// own entities' range, so it names neither.
constexpr u64 PLACE_ENTITY_BASE = 1ULL << 40;
// The gap between the city marker's lower edge and the name hanging from it, in world units.
constexpr f32 PLACE_NAME_GAP = 8.0F;

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
    context.SpawnPlayer(context.MapPlace().value_or(context.Data().world->StartCentre()));
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
