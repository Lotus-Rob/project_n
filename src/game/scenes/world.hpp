#pragma once

/// @file
/// @ingroup game
/// The world map (phase 6 chunk 5a): a stand-in about 20 screens across at zoom 1, for proving the camera, culling and
/// the measurements on a map too big to see at once. Its terrain is static scene content, not entities: a grid of tiles
/// whose kinds form regions, each published only while it lies in view and found by its grid position. A crowd of
/// walkers, each circling its own point and denser in a few gathering areas, wanders the whole map, every eighth
/// with a name label; they are plain entities (a body, a look and an orbit), so a save holds them as any scene's and
/// where each stands is a function of its orbit and the tick. The cultivator walks it, the camera following until it
/// is panned (core::CameraKind::Roam). 5b replaces the walkers with travellers and the tiles with the map as data.

#include "core/draw_list.hpp"
#include "core/frame.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "game/scene.hpp"

#include "core/generated/atlas.hpp" // by `project_n atlas`

#include <cstddef>

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

/// The kinds of terrain, from the lowest ground to the highest.
enum class Terrain : u8 {
    DeepWater, ///< Deep water.
    Shallows,  ///< Shallow water.
    Sand,      ///< Sand.
    Marsh,     ///< Marsh.
    Meadow,    ///< Meadow.
    Grassland, ///< Grassland.
    Forest,    ///< Forest.
    Hills,     ///< Hills.
    Rock,      ///< Bare rock.
    Snow,      ///< Snow.
};
/// How many kinds Terrain names.
inline constexpr std::size_t TERRAINS = 10;

/// A block of tiles, by their first and last columns and rows, both included; empty when its first column or row lies
/// past its last.
struct TileSpan {
    /// Its first column.
    u32 firstColumn = 0;
    /// Its last column.
    u32 lastColumn = 0;
    /// Its first row.
    u32 firstRow = 0;
    /// Its last row.
    u32 lastRow = 0;
};

/// The tiles `view` overlaps, clamped to the map's; empty when it misses the map.
[[nodiscard]] TileSpan TilesIn(core::Rect view);

/// The kind of terrain of the tile at `column` and `row` (within the map): a fixed low-frequency noise over the map,
/// thresholded so the kinds form regions, the same every time it is asked.
[[nodiscard]] Terrain TerrainAt(u32 column, u32 row);

/// The sprite that draws the tile at `column` and `row` (within the map): one of its kind's two variants, picked by a
/// hash of its column and row, the same every time it is asked.
[[nodiscard]] core::atlas::SpriteId TileSprite(u32 column, u32 row);

/// The world map: terrain tiles and wandering walkers, the cultivator walking it.
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

    /// The tiles `view` overlaps (TilesIn()), each drawn whole, so one whose centre lies past the view's edge never pops;
    /// no painted ground under them.
    void PublishGround(const SceneContext& context, core::Rect view, core::DrawList& drawList, u32 part) const override;

    /// A name label over every eighth walker (by its ID) standing within `view`.
    void Publish(const SceneContext& context, core::Rect view, core::Snapshot& snapshot) const override;
};

} // namespace game
