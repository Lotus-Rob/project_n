#pragma once

/// @file
/// @ingroup gfx
/// Laying the world map's rivers out as smooth painted bands. Pure: the renderer builds the vertices once for a map and a
/// look, and tests read them without a device.

#include "core/types.hpp"
#include "core/world_map.hpp"
#include "gfx/volley_draw.hpp"

#include <span>
#include <vector>

namespace gfx {

/// How many quads a river's spline takes for each tile of a segment's length, per band (a segment shorter than a tile counts
/// as one tile). An engine limit, so in code.
inline constexpr u32 RIVER_STEPS = 4;
/// The most quads the rivers' buffer holds, both bands together (an engine limit, so in code): DrawRivers() leaves out
/// the rivers that don't fit.
inline constexpr u32 MAX_RIVER_QUADS = 8'192;

/// How a river is painted: a wide edge band under a narrower core band.
struct RiverShape {
    /// The core band's width in world units.
    f32 coreWidth = 0.0F;
    /// The edge band's width in world units, wider than the core's.
    f32 edgeWidth = 0.0F;
    /// The core's premultiplied colour, red in the low byte, as RibbonVertex::colour.
    u32 coreColour = 0;
    /// The edge's premultiplied colour, in the same layout.
    u32 edgeColour = 0;
};

/// Replaces `into` with the quads of every river's edge band, then of every river's core band, each river a centripetal
/// Catmull-Rom spline through its polyline's tile centres, ends included, arriving along the end segments. Each segment
/// takes RIVER_STEPS times its length in tiles (at least one tile) of quads per band, as whole quads in MeshVertex's corner
/// order with `u` running 0 to 1 along the river and `v` 0 on one side and 1 on the other. A river's end is drawn where it
/// flows on, though its waypoints stop short or overshoot: where its last stretch meets another river's middle, when that
/// lies within a tile of the end; else, when the end isn't on water, a tile into the first tile of a kind in `water` within
/// three tiles straight ahead (none for an empty `water`), both bands fading out over that last tile. Only the drawing changes:
/// the map is as it was. A river of fewer than two distinct points draws nothing; rivers that would take the quads past
/// MAX_RIVER_QUADS are left out whole, the later ones first.
void DrawRivers(const core::WorldMap& map, const RiverShape& shape, std::span<const u8> water, std::vector<RibbonVertex>& into);

} // namespace gfx
