#pragma once

/// @file
/// @ingroup gfx
/// Drawing the world map's sprites over its painted ground and rivers: the road overlays by their neighbours, the bridges
/// and fords, the stamps, place paintings and mist puffs (stamp_draw.hpp), and a marker for each place in view. Pure: the
/// renderer uploads what DrawTerrain() writes as one more sprite buffer and draws it over the ground and the rivers, and
/// tests read it without a device.

#include "core/frame.hpp"
#include "core/manifest.hpp"
#include "core/rect.hpp"
#include "core/types.hpp"
#include "core/world_look.hpp"
#include "core/world_map.hpp"

#include <vector>

namespace gfx {

/// The most instances the terrain's buffer holds (an engine limit, so in code): the road overlays, crossings, stamps,
/// paintings, puffs and markers of the widest view (core::WIDEST_HALF_VIEW), with room to spare (a test holds the shipped
/// map to it).
inline constexpr u32 MAX_TERRAIN_INSTANCES = 8'192;

/// What the terrain draws: its sprite instances and the batches that draw them, in world units. The renderer keeps one, so
/// its buffers keep their capacity.
struct TerrainDrawing {
    /// The instances, each with `previous == current`: the map holds still between ticks.
    std::vector<core::Instance> instances;
    /// The instances' batches in drawing order, covering them all one after another: normal blend.
    std::vector<core::Batch> batches;
    /// How many of the leading batches draw the road overlays: 1, or 0 when no road is in view.
    u32 roadBatches = 0;
    /// The first batch that draws the places' markers, which the rest of the batches all draw; the batches' count when no
    /// marker is in view. The renderer lays the veil over the batches before it.
    u32 markersFrom = 0;
};

/// Replaces `into` with the drawing of the part of `map` that `view` overlaps (core::WorldMap::TilesIn()), each tile whole,
/// so one whose centre lies past the view's edge never pops, at the camera's `zoom`; nothing when `view` misses the map.
/// The ground and river pipelines paint the tiles and rivers themselves, so no tile and no river is drawn here. In drawing
/// order, a batch each when it draws anything:
///
/// - the road overlay of each tile a road crosses, chosen by which of its four neighbours (on or off the view, never past
///   the map) a road crosses (bits 1, 2, 4 and 8: north, east, south, west);
/// - on each bridge (road and river) the bridge sprite, across where its road runs east to west (also where it runs both
///   ways or neither), down where it runs only north to south; on each ford, the ford sprite;
/// - the stamps and place paintings, then the mist puffs, as DrawStamps() lays them out for the tiles in view at `zoom`;
/// - each place's marker, centred on its tile: the city marker, or the danger's sort's (none for a sort the look lacks).
///
/// Overlays and crossings have their foot at the tile's top-left corner, markers theirs at the tile's centre. Everything but
/// the markers stands on the land the fog covers (core::INSTANCE_FOGGED_LAND); markers are only shaded under the clouds
/// (core::INSTANCE_FOG_SHADE), so a place found once stays in colour. A `look` that lacks a kind of `map` draws no stamp on
/// that kind's tiles, never past a table.
void DrawTerrain(
    const core::WorldMap& map,
    const core::WorldLook& look,
    const core::Manifest& manifest,
    core::Rect view,
    f32 zoom,
    TerrainDrawing& into
);

} // namespace gfx
