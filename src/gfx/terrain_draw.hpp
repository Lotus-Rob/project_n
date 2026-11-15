#pragma once

/// @file
/// @ingroup gfx
/// Drawing the world map (phase 6 chunk 5b1): the terrain tiles in view by the map's kinds and the look's variants, the
/// river and road overlays by their neighbours, the bridges and fords, and a marker for each place in view. Pure: the
/// renderer uploads what DrawTerrain() writes as one more sprite buffer and draws it under the snapshot's ground, and tests
/// read it without a device.

#include "core/frame.hpp"
#include "core/manifest.hpp"
#include "core/rect.hpp"
#include "core/types.hpp"
#include "core/world_look.hpp"
#include "core/world_map.hpp"

#include <vector>

namespace gfx {

/// The most instances the terrain's buffer holds (an engine limit, so in code): the tiles of the widest view
/// (core::WIDEST_HALF_VIEW) and their overlays and markers, with room to spare (a test holds the shipped map to it).
inline constexpr u32 MAX_TERRAIN_INSTANCES = 8'192;

/// What the terrain draws: its sprite instances and the batches that draw them, in world units. The renderer keeps one, so
/// its buffers keep their capacity.
struct TerrainDrawing {
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// The instances, each with `previous == current`: the map holds still between ticks.
    std::vector<core::Instance> instances{};
    /// The instances' batches in drawing order, covering them all: normal blend.
    std::vector<core::Batch> batches{};
    // NOLINTEND(readability-redundant-member-init)
};

/// Replaces `into` with the drawing of the part of `map` that `view` overlaps (core::WorldMap::TilesIn()), each tile whole,
/// so one whose centre lies past the view's edge never pops; nothing when `view` misses the map. In drawing order:
///
/// - each tile, row by row, wearing one of the `look`'s variants for its kind, picked by a hash of its column and row
///   (the same every time, whichever the view);
/// - the river overlay of each tile a river crosses, chosen by which of its four neighbours (on or off the view, never
///   past the map) a river crosses (bits 1, 2, 4 and 8: north, east, south, west);
/// - the road overlay of each tile a road crosses, likewise by its neighbours a road crosses;
/// - on each bridge (road and river) the bridge sprite, across where its road runs east to west (also where it runs both
///   ways or neither), down where it runs only north to south; on each ford, the ford sprite;
/// - each place's marker, centred on its tile: the city marker, or the danger's sort's (none for a sort the look lacks).
///
/// Tiles and overlays have their foot at the tile's top-left corner, markers theirs at the tile's centre. A `look` that
/// lacks a kind of `map`, or a sprite list shorter than its kind needs, draws that tile bare, never past a table.
void DrawTerrain(
    const core::WorldMap& map,
    const core::WorldLook& look,
    const core::Manifest& manifest,
    core::Rect view,
    TerrainDrawing& into
);

} // namespace gfx
