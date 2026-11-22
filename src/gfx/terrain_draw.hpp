#pragma once

/// @file
/// @ingroup gfx
/// Drawing the world map's sprites over its painted ground, rivers and roads: the bridges and fords, the stamps, place
/// paintings and mist puffs (stamp_draw.hpp), and a marker for each place in view. Pure: the renderer uploads what
/// DrawTerrain() writes as one more sprite buffer and draws it over the ground, the rivers and the roads, and tests read it
/// without a device.

#include "core/frame.hpp"
#include "core/manifest.hpp"
#include "core/rect.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "core/world_look.hpp"
#include "core/world_map.hpp"

#include <span>
#include <vector>

namespace gfx {

/// The most instances the terrain's buffer holds (an engine limit, so in code): the crossings, stamps, paintings, puffs
/// and markers of the widest view (core::WIDEST_HALF_VIEW), with room to spare (a test holds the shipped map to it).
inline constexpr u32 MAX_TERRAIN_INSTANCES = 8'192;

/// What the terrain draws: its sprite instances and the batches that draw them, in world units. The renderer keeps one, so
/// its buffers keep their capacity.
struct TerrainDrawing {
    /// The instances, each with `previous == current`: the map holds still between ticks.
    std::vector<core::Instance> instances;
    /// The instances' batches in drawing order, covering them all one after another: normal blend.
    std::vector<core::Batch> batches;
    /// The first batch that draws the places' markers, which the rest of the batches all draw; the batches' count when no
    /// marker is in view. The renderer lays the veil over the batches before it.
    u32 markersFrom = 0;
};

/// Replaces `into` with the drawing of the part of `map` that `view` overlaps (core::WorldMap::TilesIn()), each tile whole,
/// so one whose centre lies past the view's edge never pops, at the camera's `zoom`; nothing when `view` misses the map.
/// The ground and ribbon pipelines paint the tiles, rivers and roads themselves, so no tile, river or road is drawn here. In
/// drawing order, a batch each when it draws anything:
///
/// - on each bridge (road and river) the bridge sprite, laid the way `roadCurves` (core::RoadCurves()) run at the point of
///   theirs nearest the tile's centre: across where that runs at least as much east to west as north to south (also when
///   no curve has a point), else down; on each ford, the ford sprite;
/// - the stamps and place paintings, then the mist puffs, as DrawStamps() lays them out for the tiles in view at `zoom`;
/// - each place's marker, centred on its tile: the city marker, or the danger's sort's (none for a sort the look lacks).
///
/// Crossings have their foot at the tile's top-left corner, markers theirs at the tile's centre. Everything but the
/// markers stands on the land the fog covers (core::INSTANCE_FOGGED_LAND); markers are only shaded under the clouds
/// (core::INSTANCE_FOG_SHADE), so a place found once stays in colour. A `look` that lacks a kind of `map` draws no
/// stamp on that kind's tiles, never past a table.
void DrawTerrain(
    const core::WorldMap& map,
    const core::WorldLook& look,
    std::span<const std::vector<core::Vec2>> roadCurves,
    const core::Manifest& manifest,
    core::Rect view,
    f32 zoom,
    TerrainDrawing& into
);

} // namespace gfx
