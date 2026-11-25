#pragma once

/// @file
/// @ingroup gfx
/// Laying the world map's rivers out as smooth painted bands. Pure: the renderer builds the vertices once for a map and a
/// look, and tests read them without a device.

#include "core/types.hpp"
#include "core/world_map.hpp"
#include "gfx/ribbon.hpp"

#include <span>
#include <vector>

namespace gfx {

/// How many quads a river's spline takes for each tile of a segment's length, per band (a segment shorter than a tile counts
/// as one tile). An engine limit, so in code.
inline constexpr u32 RIVER_STEPS = 4;
/// The most quads the rivers' buffer holds, both bands together (an engine limit, so in code): DrawRivers() leaves out
/// the rivers that don't fit.
inline constexpr u32 MAX_RIVER_QUADS = 8'192;

/// How a river begins: on another river, out of water, or at a spring.
enum class RiverStart : u8 {
    /// Its first stretch, run on a tile past its head, meets another river's line (that river's end included, not within a
    /// tile of its start, where two sources lie side by side): it flows out of that river.
    Branch,
    /// It begins in water, or water lies behind its first point: it rises out of it.
    Pond,
    /// It begins on open ground and widens from a point.
    Spring
};

/// How each of `map`'s rivers begins, in the map's order, with `water` the kinds that count as water. A river of fewer than
/// two distinct points is a Spring. A shipped river must rise somewhere: on high ground (`hills`, `rock` or `snow`), out of
/// water (`shallows` or deep water on its first tile or within three tiles straight behind it along its first segment; a
/// pond is 2 to 4 `shallows` tiles, inland), or within a tile of another river's line (a branch); a test fails on a river
/// that starts as a Spring on low ground.
[[nodiscard]] std::vector<RiverStart> RiverStarts(const core::WorldMap& map, std::span<const u8> water);

/// Replaces `into` with the quads of every river's edge band, then of every river's core band, each river a centripetal
/// Catmull-Rom spline through its polyline's tile centres, ends included, arriving along the end segments. Each segment
/// takes RIVER_STEPS times its length in tiles (at least one tile) of quads per band, as whole quads in MeshVertex's corner
/// order with `u` running 0 to 1 along the river and `v` 0 on one side and 1 on the other. A river's end is drawn where it
/// flows on, though its waypoints stop short or overshoot: where its last stretch meets another river's middle, when that
/// lies within a tile of the end and not within a tile of the other river's own end (there the other flows into it); else, when
/// the end isn't on water, a tile into the first tile of a kind in `water` within three tiles straight ahead (none for an empty
/// `water`), both bands fading out over that last tile; a point drawn past the map's edge is kept on it. Its start is drawn
/// the same way mirrored: a river whose first stretch, run on a tile past its head, meets another river's line (that
/// river's end included, not within a tile of its start) is left as it is (a branch); one that begins on water keeps its
/// first point, and one with water within three tiles straight behind it begins a tile into that water, each fading in
/// over a tile (a pond); any other starts as a spring, both sides widening from a point over `shape.taper`. Only the
/// drawing changes: the map is as it was. A river of fewer than two distinct points draws nothing; rivers that would take
/// the quads past MAX_RIVER_QUADS are left out whole, the later ones first.
void DrawRivers(
    const core::WorldMap& map,
    const RibbonShape& shape,
    std::span<const u8> water,
    std::vector<RibbonVertex>& into
);

} // namespace gfx
