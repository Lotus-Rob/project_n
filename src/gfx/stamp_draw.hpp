#pragma once

/// @file
/// @ingroup gfx
/// Laying out the painted things the world map's terrain wears: the stamps scattered over each kind, the mist puffs among
/// some of them and the paintings that stand for places. Pure, like terrain_draw.hpp: tests read it without a device.

#include "core/manifest.hpp"
#include "core/types.hpp"
#include "core/world_look.hpp"
#include "core/world_map.hpp"
#include "gfx/terrain_draw.hpp"

namespace gfx {

/// Appends to `into`, past the instances already there, the stamps, place paintings and mist puffs of the tiles in `span` at
/// the camera's `zoom`, each batch with its own `first`. `span` is widened first by as far as a stamp or a puff can reach from
/// its tile (taken from `look` and `manifest`, and kept on the map), so one standing just past the view that shows in it is
/// laid out whole and never pops in. In drawing order:
///
/// - one normal batch of the stamps and the paintings together, sorted by foot (y, then x, then tile index) so nearer
///   things cover farther ones;
/// - then one normal batch of the mist puffs, laid among the stamps.
///
/// Each tile has a stamp with the chance of its kind's density, its sprite, mirroring, width and place within the
/// tile all fixed by a hash of the tile (never the view). A kind that thins out below a zoom shows each stamp by its own
/// fixed rank, so lowering the zoom fades the lowest-ranked ones out one after another and never reshuffles any. A place
/// within reach of `span` is drawn as its painting (its own, else its sort's, else the city's) centred on its tile, though
/// the tile itself lies outside `span`, so one spilling into view never pops; no stamp stands on a painting. The place is
/// still one tile for walking, picking and travel. Every instance stands still on the land the fog covers. A look lacking
/// a kind of `map`, or a kind with no stamp sprites, draws no stamp there. Does nothing for an empty `span`.
void DrawStamps(
    const core::WorldMap& map,
    const core::WorldLook& look,
    const core::Manifest& manifest,
    core::TileSpan span,
    f32 zoom,
    TerrainDrawing& into
);

} // namespace gfx
