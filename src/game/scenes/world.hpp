#pragma once

/// @file
/// @ingroup game
/// The world map, about 20 screens across at zoom 1, too big to see at once. Its terrain is no scene content: the renderer
/// draws the map (`assets/data/world.yaml`) from its data and look (SceneTraits::worldMap), and the host draws the
/// travellers on its roads and in its cities. The cultivator walks it, the camera following until it is panned
/// (core::CameraKind::Roam).

#include "core/frame.hpp"
#include "core/rect.hpp"
#include "core/vec2.hpp"
#include "game/scene.hpp"

#include <optional>

namespace game {

/// A point in the port's tile, in the east and clear of the start: the travellers crowd round the port, so the widest
/// view centred here holds thousands of them, more than anywhere else.
inline constexpr core::Vec2 WORLD_BUSIEST{ .x = 33'000.0F, .y = 10'800.0F };

/// Where the portal stands from the start city's tile centre, in world units: up and to the left, on the first screen, in
/// the quiet country round the start, west of it and off the strip east of the start that `--camera-path` walks along.
inline constexpr core::Vec2 WORLD_PORTAL_OFFSET{ .x = -560.0F, .y = -200.0F };

/// The world map's scene: the cultivator walking it among the game's world's people, over the map the renderer draws.
class WorldScene final : public Scene {
public:
    /// Named `world`, a game scene (it runs on the game's world, whose cities' people GenerateWorld() made) with
    /// creatures (the cultivator), its view the map's, the camera following the cultivator until panned, and its portal a
    /// short walk from the start, on the first screen.
    [[nodiscard]] const SceneTraits& Traits() const override;

    /// The start city's tile centre plus WORLD_PORTAL_OFFSET; none without a map.
    [[nodiscard]] std::optional<core::Vec2> Portal(const SceneContext& context) const override;

    /// The whole map, from its top left corner to its columns and rows of tiles; empty without a map.
    [[nodiscard]] core::Rect View(const SceneContext& context) const override;

    /// The cultivator, made the player, where it stood when it last left the map (SceneContext::MapPlace()), or on the
    /// start city's tile centre in a game that hasn't left it yet. The world's people are the game's world's, never
    /// spawned here.
    void Build(SceneContext& context) const override;

    /// The name of every place of the map whose tile centre lies within `view` and whose tile the player has explored
    /// (SceneContext::Explored(); core::TextKind::Place, shown only where explored: core::TextFog::Explored), one entity and
    /// name key a place, hung under the tile's marker so a cultivator standing on a city never has its own label over the
    /// name.
    void Publish(const SceneContext& context, core::Rect view, core::Snapshot& snapshot) const override;
};

} // namespace game
