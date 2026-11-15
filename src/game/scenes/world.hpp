#pragma once

/// @file
/// @ingroup game
/// The world map (phase 6 chunk 5a): a stand-in about 20 screens across at zoom 1, for proving the camera, culling and
/// the measurements on a map too big to see at once. Its terrain is no scene content: the renderer draws the map
/// (`assets/data/world.yaml`) from its data and look (SceneTraits::worldMap). A crowd of
/// walkers, each circling its own point and denser in a few gathering areas, wanders the whole map, every eighth
/// with a name label; they are plain entities (a body, a look and an orbit), so a save holds them as any scene's and
/// where each stands is a function of its orbit and the tick. The cultivator walks it, the camera following until it
/// is panned (core::CameraKind::Roam). 5b2 replaces the walkers with travellers.

#include "core/frame.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "game/scene.hpp"

namespace game {

/// The side of one terrain tile, in world units.
inline constexpr f32 WORLD_TILE = 256.0F;
/// How many columns of tiles the map has.
inline constexpr u32 WORLD_COLUMNS = 150;
/// How many rows of tiles the map has: the last reaches a little past the map's bottom edge.
inline constexpr u32 WORLD_ROWS = 85;
/// The world map: 38,400 by 21,600 world units, 20 screens of 1,920 by 1,080 across and as many down.
inline constexpr core::Rect WORLD_MAP{ .min = {}, .max = { .x = 38'400.0F, .y = 21'600.0F } };
/// The middle of the map's busiest gathering, in the east and clear of the start: the widest view centred here
/// holds thousands of walkers, more than anywhere else.
inline constexpr core::Vec2 WORLD_BUSIEST{ .x = 33'000.0F, .y = 10'800.0F };

/// The world map's scene: wandering walkers, the cultivator walking it, over the map the renderer draws.
class WorldScene final : public Scene {
public:
    /// Named `world`, viewing the whole map, with creatures (the cultivator) and no world NPCs, the camera following the
    /// cultivator until panned, orbits returning to the map's centre, and its portal a short walk from the start, on the
    /// first screen.
    [[nodiscard]] const SceneTraits& Traits() const override;

    /// The cultivator at the map's centre, made the player, then 5,625 walkers: 1,600 spread evenly over the whole
    /// map (about 4 a screen) and the rest evenly over four gathering areas, none within the widest view of the start:
    /// the busiest, round WORLD_BUSIEST, 2,900 walkers (about 123 a screen) and three towns of 375 (about 60 a screen).
    /// Each circles a point of its own at a walking pace, its orbit drawn from core/random.hpp keyed on its index.
    void Build(SceneContext& context) const override;

    /// A name label over every eighth walker (by its ID) standing within `view`.
    void Publish(const SceneContext& context, core::Rect view, core::Snapshot& snapshot) const override;
};

} // namespace game
