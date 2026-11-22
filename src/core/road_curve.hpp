#pragma once

/// @file
/// @ingroup core
/// The centre lines of the world map's roads and rivers: a smooth curve through each polyline's tile centres. Pure, so the
/// drawing and whatever walks a road read one curve.

#include "core/types.hpp"
#include "core/vec2.hpp"
#include "core/world_map.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace core {

/// How many steps a road's curve takes for each tile of a segment's length (a segment shorter than a tile counts as one
/// tile). An engine limit, so in code.
inline constexpr u32 ROAD_STEPS = 4;

/// `tiles`' waypoints as world points (tile centres of `tileSize`), each repeat of the one before dropped.
[[nodiscard]] std::vector<Vec2> Waypoints(std::span<const TilePoint> tiles, f32 tileSize);

/// The centripetal Catmull-Rom spline through `waypoints` (two or more, none repeating its neighbour), as the points a
/// ribbon's quads stand at, beginning at the first waypoint and ending on the last, passing through every one. Phantom end
/// points mirror the first and last neighbours, so the curve arrives along the end segments. Each segment takes
/// `stepsPerTile` times its length in tiles (at least one tile) of steps.
[[nodiscard]] std::vector<Vec2> Midline(std::span<const Vec2> waypoints, f32 tileSize, u32 stepsPerTile);

/// The Midline() of each of `lines` (lists of waypoints), an empty one for a list of fewer than two.
[[nodiscard]] std::vector<std::vector<Vec2>> Midlines(std::span<const std::vector<Vec2>> lines, f32 tileSize, u32 stepsPerTile);

/// Where the last stretch of `waypoints` (two or more) should end to flow into another line, when it runs on or is cut short
/// to meet the middle of one of `midlines` (the line at index `own` skipped, `midlines.size()` or more to skip none): the
/// nearest such meeting within a tile of its end and not within a tile of the other line's own last point (there the other
/// flows into this one), nor, with `startsFlowIn`, of its first (for lines that may be read either way, as roads are; a
/// river's first point is its source, which nothing flows into), and not where the last stretch would be left no length.
/// None when it meets none there.
[[nodiscard]] std::optional<Vec2> JoinedEnd(
    std::span<const Vec2> waypoints,
    std::size_t own,
    std::span<const std::vector<Vec2>> midlines,
    f32 tileSize,
    bool startsFlowIn
);

/// Every road's curve of `map`, in the map's order, at ROAD_STEPS; a road of fewer than two distinct points gets an empty
/// one, so the indices still match `map.Roads()`. A road's end that meets another road's middle within a tile is drawn
/// there, at either end of the road, except an end on a city's tile, which stays at the city's centre. As in JoinedEnd(),
/// but a meeting within a tile of another road's end is skipped only when that end flows into this road: it would join this
/// road itself, or it lies on this end's line, where the two already meet. Of two ends that would each join the other's
/// road there, the later road's joins and the earlier's stays. Each end is judged against the others' joins found with
/// nothing skipped, so the result never depends on the order roads are worked through.
[[nodiscard]] std::vector<std::vector<Vec2>> RoadCurves(const WorldMap& map);

} // namespace core
