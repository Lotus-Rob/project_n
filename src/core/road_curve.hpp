#pragma once

/// @file
/// @ingroup core
/// The centre lines of the world map's roads and rivers: a smooth curve through each polyline's tile centres. Pure, so the
/// drawing and whatever walks a road read one curve: travellers walk tile centre to tile centre and are drawn on it.

#include "core/journey.hpp"
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

/// How far, in tiles, a road tile's centre may lie from a road's curve and still have its walkers drawn on it
/// (RoadCurveIndex): well past the half tile a staircase's corner centres stand off their curve, and past the tile a joined
/// end moves the curve's end away from the last tile laid. An engine limit, so in code.
inline constexpr f32 ROAD_DRAWN_REACH = 1.5F;

/// `tiles`' waypoints as world points (tile centres of `tileSize`), each repeat of the one before dropped.
[[nodiscard]] std::vector<Vec2> Waypoints(std::span<const TilePoint> tiles, f32 tileSize);

/// The centripetal Catmull-Rom spline through `waypoints` (two or more), as the points a ribbon's quads stand at, beginning
/// at the first waypoint and ending on the last, passing through every one. A waypoint that repeats the one before it is
/// skipped, so when every waypoint is one point the curve is that single point. Phantom end points mirror the first and last
/// neighbours, so the curve arrives along the end segments. Each segment takes `stepsPerTile` times its length in tiles (at
/// least one tile) of steps.
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

/// Where the first stretch of `waypoints` (two or more, read backwards: the river's head is the last of `waypoints`) should
/// end to start on another line, as JoinedEnd() finds a join but skipping, within a tile of another line's end, only a
/// meeting at its first point (two sources side by side), never its last: a river that ends there flows into this one's head.
[[nodiscard]] std::optional<Vec2> JoinedHead(
    std::span<const Vec2> waypoints,
    std::size_t own,
    std::span<const std::vector<Vec2>> midlines,
    f32 tileSize
);

/// Every road's curve of `map`, in the map's order, at ROAD_STEPS; a road of fewer than two distinct points gets an empty
/// one, so the indices still match `map.Roads()`. A road's end that meets another road's middle within a tile is drawn
/// there, at either end of the road, except an end on a city's tile, which stays at the city's centre. As in JoinedEnd(),
/// but a meeting within a tile of another road's end is skipped only when that end flows into this road: it would join this
/// road itself, or it lies on this end's line, where the two already meet. Of two ends that would each join the other's
/// road there, the later road's joins and the earlier's stays. A road whose two ends would join at one point keeps both as
/// laid, crossing there. Each end is judged against the others' joins found with
/// nothing skipped, so the result never depends on the order roads are worked through.
[[nodiscard]] std::vector<std::vector<Vec2>> RoadCurves(const WorldMap& map);

/// Where a walker standing on each road tile's centre of a map is drawn: the closest point on any of its RoadCurves() within
/// ROAD_DRAWN_REACH tiles of the centre, the first road's in the map's order on a tie. Found once from the curves' segments
/// near each road tile, so placing a walker reads one point per tile.
class RoadCurveIndex {
public:
    /// Indexes the road tiles of `map`.
    explicit RoadCurveIndex(const WorldMap& map);

    /// Where a walker on `tile`'s centre is drawn on a road: none for a tile no road crosses (TileFlag::Road), or one no
    /// road's curve passes within ROAD_DRAWN_REACH tiles of, or a tile off the map.
    [[nodiscard]] std::optional<Vec2> OnRoad(u32 tile) const;

private:
    std::vector<std::optional<Vec2>> m_onRoad; // one per tile, row by row
};

/// Where `member` (an index into its members, below their count) of `journey` is drawn at `tick`: Where() moved onto the
/// road it walks, for presentation alone. A leg's walker is drawn at RoadCurveIndex::OnRoad() of each tile centre it
/// reaches that has one, else at the centre, and between two points of its leg moves straight from where it is drawn at
/// the one to where at the next, so it eases on and off a road over the step that joins or leaves it and never jumps. A
/// stay is moved as its city's tile centre is. The journey's own first and last points are drawn where they are, so a
/// walker drawn where it stands before or after the journey (the player's cultivator) meets it there. The other members
/// keep their places round member 0, moved with it. Where() for a journey without parts.
[[nodiscard]] Vec2 DrawnWhere(
    const WorldMap& map,
    const RoadCurveIndex& roads,
    const Journey& journey,
    i64 tick,
    std::size_t member = 0
);

} // namespace core
