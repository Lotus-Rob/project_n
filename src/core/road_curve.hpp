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
/// ribbon's quads stand at, beginning at the first waypoint and ending on the last, passing through every one.
/// Phantom end points mirror the first and last neighbours, so the curve arrives along the end segments. Each segment takes
/// `stepsPerTile` times its length in tiles (at least one tile) of steps.
[[nodiscard]] std::vector<Vec2> Midline(std::span<const Vec2> waypoints, f32 tileSize, u32 stepsPerTile);

/// The point where the last stretch of `waypoints` (two or more) runs on or is cut short to meet the middle of one of
/// `others`' curves (the line at index `own` skipped, `others.size()` or more to skip none), the nearest to its end and
/// within a tile of it; none when it meets none there. A meeting within a tile of the other curve's own end doesn't count:
/// there the other line flows into this one, not this one into it.
[[nodiscard]] std::optional<Vec2> Join(
    std::span<const Vec2> waypoints,
    std::size_t own,
    std::span<const std::vector<Vec2>> others,
    f32 tileSize
);

/// The curve through `line`'s tile centres, ending where its last stretch meets the middle of one of `others`' lines (as
/// Join() says) instead of at its last waypoint. A line of fewer than two distinct points gives no curve.
[[nodiscard]] std::vector<Vec2> CurveThrough(
    std::span<const TilePoint> line,
    std::span<const std::vector<TilePoint>> others,
    f32 tileSize,
    u32 stepsPerTile
);

/// Every road's curve of `map`, in the map's order, at ROAD_STEPS; a road of fewer than two distinct points gets an empty
/// one, so the indices still match `map.Roads()`.
[[nodiscard]] std::vector<std::vector<Vec2>> RoadCurves(const WorldMap& map);

} // namespace core
