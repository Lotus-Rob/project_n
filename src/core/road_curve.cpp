#include "core/road_curve.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

namespace core {

namespace {

/// Where in a tile its centre lies, as a fraction of its size.
constexpr f32 TILE_CENTRE = 0.5F;
/// How far, in tiles, a line's last stretch may run on or stop short and still be taken to flow into another line.
constexpr f32 JOIN_TILES = 1.0F;
/// The shortest run or crossing that still has a direction, in world units or as a share of the lengths compared; a move
/// of an end shorter than this isn't one.
constexpr f32 SHORTEST_RUN = 1.0e-4F;
/// How far, in tiles, a road tile's centre may lie from a road's curve and still have its walkers drawn on it: well past
/// the half tile a staircase's corner centres stand off their curve, and past the tile a joined end moves the curve's end
/// away from the last tile laid.
constexpr f32 DRAWN_REACH_TILES = 1.5F;

f32 Length(Vec2 vec) {
    return std::sqrt((vec.x * vec.x) + (vec.y * vec.y));
}

Vec2 Lerp(Vec2 from, Vec2 target, f32 amount) {
    return from + ((target - from) * amount);
}

/// The point at parameter `param` in [knots[1], knots[2]] of the centripetal Catmull-Rom spline through `points[1]` and
/// `points[2]`, with `points[0]` and `points[3]` shaping its ends (Barry and Goldman's pyramid); knot spacing grows as the
/// square root of distance, the centripetal parametrisation, which never loops or cusps within a segment. Every neighbouring
/// pair must differ.
Vec2 SplineAt(const std::array<Vec2, 4>& points, const std::array<f32, 4>& knots, f32 param) {
    const auto blend = [&](Vec2 from, Vec2 target, std::size_t first, std::size_t last) {
        const f32 span = knots.at(last) - knots.at(first);
        return Lerp(from, target, (param - knots.at(first)) / span);
    };
    const Vec2 low = blend(points[0], points[1], 0, 1);
    const Vec2 mid = blend(points[1], points[2], 1, 2);
    const Vec2 high = blend(points[2], points[3], 2, 3);
    const Vec2 lowMid = blend(low, mid, 0, 2);
    const Vec2 midHigh = blend(mid, high, 1, 3);
    return blend(lowMid, midHigh, 1, 2);
}

/// How many steps a segment of `length` world units takes.
std::size_t SegmentSteps(f32 length, f32 tileSize, u32 stepsPerTile) {
    const f32 tiles = std::ceil(length / tileSize);
    return std::size_t{ stepsPerTile } * std::max<std::size_t>(1, static_cast<std::size_t>(tiles));
}

/// Where the line through `from` and `toward` meets the segment from `start` to `end`, as the share of the way from `from`
/// to `toward`; none when they run parallel or meet off the segment.
std::optional<f32> Meeting(Vec2 from, Vec2 toward, Vec2 start, Vec2 end) {
    const Vec2 runs = toward - from;
    const Vec2 along = end - start;
    const f32 cross = (runs.x * along.y) - (runs.y * along.x);
    if (std::abs(cross) <= SHORTEST_RUN * Length(runs) * Length(along)) {
        return std::nullopt;
    }
    const Vec2 apart = start - from;
    const f32 onSegment = ((apart.x * runs.y) - (apart.y * runs.x)) / cross;
    if (onSegment < 0.0F || onSegment > 1.0F) {
        return std::nullopt;
    }
    return ((apart.x * along.y) - (apart.y * along.x)) / cross;
}

/// Where an end of a line joins, and the line it joins.
struct Join {
    Vec2 point;
    std::size_t line;
};

/// The search behind JoinedEnd(): the nearest meeting of the last stretch of `waypoints` with a line of `midlines` other than
/// `own`, skipping those within a tile of an end of that line for which `yields(line, atFront)` says the other flows in.
template<typename Yields>
std::optional<Join> FindJoin(
    std::span<const Vec2> waypoints,
    std::size_t own,
    std::span<const std::vector<Vec2>> midlines,
    f32 tileSize,
    const Yields& yields
) {
    PN_ASSERT(waypoints.size() >= 2, "an end needs a stretch before it");
    const Vec2 from = waypoints[waypoints.size() - 2];
    const Vec2 last = waypoints.back();
    const f32 reach = JOIN_TILES * tileSize;
    std::optional<Join> nearest;
    f32 nearestApart = reach;
    for (std::size_t other = 0; other < midlines.size(); ++other) {
        if (other == own) {
            continue;
        }
        const std::vector<Vec2>& line = midlines[other];
        for (std::size_t point = 1; point < line.size(); ++point) {
            const std::optional<f32> share = Meeting(from, last, line[point - 1], line[point]);
            if (!share || *share <= 0.0F) {
                continue;
            }
            const Vec2 meets = Lerp(from, last, *share);
            const f32 apart = Length(meets - last);
            if ((Length(meets - line.back()) <= reach && yields(other, false)) ||
                (Length(meets - line.front()) <= reach && yields(other, true))) {
                continue;
            }
            if (apart <= nearestApart) {
                nearest = Join{ .point = meets, .line = other };
                nearestApart = apart;
            }
        }
    }
    return nearest && Length(nearest->point - from) > SHORTEST_RUN ? nearest : std::nullopt;
}

} // namespace

std::vector<Vec2> Waypoints(std::span<const TilePoint> tiles, f32 tileSize) {
    std::vector<Vec2> points;
    points.reserve(tiles.size());
    TilePoint last{};
    for (std::size_t index = 0; index < tiles.size(); ++index) {
        if (index > 0 && tiles[index] == last) {
            continue;
        }
        last = tiles[index];
        points.push_back(
            { .x = (static_cast<f32>(last.column) + TILE_CENTRE) * tileSize,
              .y = (static_cast<f32>(last.row) + TILE_CENTRE) * tileSize }
        );
    }
    return points;
}

std::vector<Vec2> Midline(std::span<const Vec2> waypoints, f32 tileSize, u32 stepsPerTile) {
    PN_ASSERT(waypoints.size() >= 2, "a curve needs two waypoints");
    // The spline's knots are spaced by the distance between waypoints, so a stretch of no length would divide by zero.
    std::vector<Vec2> distinct;
    distinct.reserve(waypoints.size());
    for (const Vec2 point : waypoints) {
        if (distinct.empty() || Length(point - distinct.back()) > SHORTEST_RUN) {
            distinct.push_back(point);
        }
    }
    const std::size_t count = distinct.size();
    if (count < 2) {
        return { distinct.front() };
    }
    std::vector<Vec2> padded;
    padded.reserve(count + 2);
    padded.push_back(distinct[0] + (distinct[0] - distinct[1]));
    padded.insert(padded.end(), distinct.begin(), distinct.end());
    padded.push_back(distinct[count - 1] + (distinct[count - 1] - distinct[count - 2]));

    std::vector<Vec2> line{ distinct[0] };
    for (std::size_t segment = 0; segment + 1 < count; ++segment) {
        const std::array<Vec2, 4> points{ padded[segment], padded[segment + 1], padded[segment + 2], padded[segment + 3] };
        std::array<f32, 4> knots{};
        for (std::size_t knot = 1; knot < 4; ++knot) {
            knots.at(knot) = knots.at(knot - 1) + std::sqrt(Length(points.at(knot) - points.at(knot - 1)));
        }
        const std::size_t steps = SegmentSteps(Length(points[2] - points[1]), tileSize, stepsPerTile);
        for (std::size_t step = 1; step <= steps; ++step) {
            const f32 along = static_cast<f32>(step) / static_cast<f32>(steps);
            line.push_back(SplineAt(points, knots, knots[1] + ((knots[2] - knots[1]) * along)));
        }
    }
    return line;
}

std::optional<Vec2> JoinedEnd(
    std::span<const Vec2> waypoints,
    std::size_t own,
    std::span<const std::vector<Vec2>> midlines,
    f32 tileSize,
    bool startsFlowIn
) {
    const auto yields = [&](std::size_t /*line*/, bool atFront) { return startsFlowIn || !atFront; };
    const std::optional<Join> join = FindJoin(waypoints, own, midlines, tileSize, yields);
    return join ? std::optional<Vec2>{ join->point } : std::nullopt;
}

std::vector<std::vector<Vec2>> Midlines(std::span<const std::vector<Vec2>> lines, f32 tileSize, u32 stepsPerTile) {
    std::vector<std::vector<Vec2>> midlines;
    midlines.reserve(lines.size());
    for (const std::vector<Vec2>& line : lines) {
        midlines.push_back(line.size() < 2 ? std::vector<Vec2>{} : Midline(line, tileSize, stepsPerTile));
    }
    return midlines;
}

namespace {

/// Where each road's last and first points join another road with nothing yielding, a road at a time.
struct EndJoins {
    std::vector<std::optional<Join>> lasts;
    std::vector<std::optional<Join>> firsts;
};

std::vector<Vec2> Reversed(std::span<const Vec2> points) {
    return { points.rbegin(), points.rend() };
}

/// Whether `point` lies on the line from `from` through `toward`, beyond `from`.
bool AheadOnLine(Vec2 from, Vec2 toward, Vec2 point) {
    const Vec2 runs = toward - from;
    const Vec2 apart = point - from;
    const f32 cross = (runs.x * apart.y) - (runs.y * apart.x);
    const f32 along = (runs.x * apart.x) + (runs.y * apart.y);
    return along > 0.0F && std::abs(cross) <= SHORTEST_RUN * Length(runs) * Length(apart);
}

/// The join of road `index`'s last point (`points` as authored) or first (`points` reversed), given every end's join with
/// nothing yielding, `free`. A meeting near another road's end is skipped only when that end flows into this road: its own
/// join is on this road, or it lies on this end's line (the two already meet there). When this end's join is on that road
/// too, only the lower-indexed road's end skips it.
std::optional<Join> ResolvedJoin(
    std::span<const Vec2> points,
    std::size_t index,
    bool atLast,
    std::span<const std::vector<Vec2>> midlines,
    f32 tileSize,
    const EndJoins& free
) {
    const std::optional<Join>& own = atLast ? free.lasts[index] : free.firsts[index];
    const Vec2 from = points[points.size() - 2];
    const Vec2 last = points.back();
    const auto yields = [&](std::size_t line, bool atFront) {
        const Vec2 end = atFront ? midlines[line].front() : midlines[line].back();
        if (AheadOnLine(from, last, end)) {
            return true;
        }
        const std::optional<Join>& theirs = atFront ? free.firsts[line] : free.lasts[line];
        const bool joinsThis = theirs && theirs->line == index;
        const bool joinsThat = own && own->line == line;
        return joinsThis && (!joinsThat || index < line);
    };
    return FindJoin(points, index, midlines, tileSize, yields);
}

} // namespace

std::vector<std::vector<Vec2>> RoadCurves(const WorldMap& map) {
    const std::span<const std::vector<TilePoint>> roads = map.Roads();
    const f32 tileSize = map.TileSize();
    const auto atCity = [&](TilePoint tile) {
        return std::ranges::any_of(map.Places(), [&](const Place& place) {
            return place.kind == PlaceKind::City && place.tile == tile;
        });
    };
    std::vector<std::vector<Vec2>> waypoints;
    waypoints.reserve(roads.size());
    for (const std::vector<TilePoint>& road : roads) {
        waypoints.push_back(Waypoints(road, tileSize));
    }
    const std::vector<std::vector<Vec2>> stored = Midlines(waypoints, tileSize, ROAD_STEPS);
    // Every end's join with nothing yielding comes first, and the real joins read only those, never each other, so the
    // answer never depends on the order of the work.
    EndJoins free;
    free.lasts.resize(roads.size());
    free.firsts.resize(roads.size());
    const auto never = [](std::size_t /*line*/, bool /*atFront*/) { return false; };
    for (std::size_t index = 0; index < roads.size(); ++index) {
        if (waypoints[index].size() < 2) {
            continue;
        }
        if (!atCity(roads[index].back())) {
            free.lasts[index] = FindJoin(waypoints[index], index, stored, tileSize, never);
        }
        if (!atCity(roads[index].front())) {
            free.firsts[index] = FindJoin(Reversed(waypoints[index]), index, stored, tileSize, never);
        }
    }
    std::vector<std::vector<Vec2>> curves(roads.size());
    for (std::size_t index = 0; index < roads.size(); ++index) {
        std::vector<Vec2>& points = waypoints[index];
        if (points.size() < 2) {
            continue;
        }
        std::optional<Join> last;
        std::optional<Join> first;
        if (!atCity(roads[index].back())) {
            last = ResolvedJoin(points, index, true, stored, tileSize, free);
        }
        if (!atCity(roads[index].front())) {
            first = ResolvedJoin(Reversed(points), index, false, stored, tileSize, free);
        }
        if (last && first && Length(last->point - first->point) <= SHORTEST_RUN * tileSize) {
            // Both ends drawn at one crossing would leave the road no length, so it crosses there as laid instead.
            last.reset();
            first.reset();
        }
        if (last) {
            points.back() = last->point;
        }
        if (first) {
            points.front() = first->point;
        }
        curves[index] = Midline(points, tileSize, ROAD_STEPS);
    }
    return curves;
}

namespace {

/// The point of the segment from `start` to `end` closest to `point`.
Vec2 ClosestOnSegment(Vec2 point, Vec2 start, Vec2 end) {
    const Vec2 along = end - start;
    const f32 lengthSquared = (along.x * along.x) + (along.y * along.y);
    if (lengthSquared <= 0.0F) {
        return start;
    }
    const Vec2 apart = point - start;
    const f32 share = std::clamp(((apart.x * along.x) + (apart.y * along.y)) / lengthSquared, 0.0F, 1.0F);
    return start + (along * share);
}

/// How far a walker standing at `point` on `tile` is moved: onto RoadCurveIndex::OnRoad() of the tile when it has a point
/// there, unless the walker is drawn where it stands (`staysPut`).
Vec2 ShiftAt(const RoadCurveIndex& roads, u32 tile, Vec2 point, bool staysPut) {
    if (staysPut) {
        return {};
    }
    const std::optional<Vec2> onRoad = roads.OnRoad(tile);
    return onRoad ? *onRoad - point : Vec2{};
}

/// How `journey`'s walkers are moved onto the road at `tick`, from where Where() puts them.
Vec2 JourneyShift(const WorldMap& map, const RoadCurveIndex& roads, const Journey& journey, i64 tick) {
    // The part holding `tick` as Where() picks it, so the two agree at every boundary.
    const auto holding = std::ranges::find_if(journey.parts, [tick](const JourneyPart& part) { return EndTick(part) > tick; });
    const std::size_t index =
        holding == journey.parts.end() ? journey.parts.size() - 1 : static_cast<std::size_t>(holding - journey.parts.begin());
    const JourneyPart& part = journey.parts[index];
    const bool opens = index == 0;
    const bool closes = index + 1 == journey.parts.size();
    if (part.stay) {
        // The drift round a city is no walk along a road: the whole stay keeps the shift of the centre it starts and ends
        // on, which the legs arriving and leaving share.
        const u32 city = map.TileIndex(map.Places()[part.rest.city].tile);
        return ShiftAt(roads, city, map.TileCentre(city), opens || closes);
    }
    const Leg& leg = part.leg;
    const std::size_t last = leg.tiles.size() - 1;
    const auto shiftOf = [&](std::size_t point) {
        const Vec2 standing = point == 0 ? leg.origin : map.TileCentre(leg.tiles[point]);
        return ShiftAt(roads, leg.tiles[point], standing, (opens && point == 0) || (closes && point == last));
    };
    if (tick <= leg.startTick) {
        return shiftOf(0);
    }
    if (tick >= EndTick(leg)) {
        return shiftOf(last);
    }
    // As Where() finds the step: before the end, the elapsed ticks fit a u32 and some tile comes after them.
    const auto elapsed = static_cast<u32>(tick - leg.startTick);
    const auto next = static_cast<std::size_t>(std::ranges::upper_bound(leg.arrive, elapsed) - leg.arrive.begin());
    const std::size_t from = next - 1;
    const f64 fraction = static_cast<f64>(elapsed - leg.arrive[from]) / static_cast<f64>(leg.arrive[next] - leg.arrive[from]);
    const Vec2 start = shiftOf(from);
    return start + ((shiftOf(next) - start) * static_cast<f32>(fraction));
}

} // namespace

RoadCurveIndex::RoadCurveIndex(const WorldMap& map)
  : m_onRoad(static_cast<std::size_t>(map.Columns()) * map.Rows()) {
    const f32 tileSize = map.TileSize();
    const f32 reach = DRAWN_REACH_TILES * tileSize;
    std::vector<f32> nearest(m_onRoad.size());
    const u8 road = std::to_underlying(TileFlag::Road);
    // Each segment measured against the road tiles round it, in the curves' order: a later segment takes a tile only when
    // strictly closer, so a tie goes to the first road.
    for (const std::vector<Vec2>& curve : RoadCurves(map)) {
        for (std::size_t point = 1; point < curve.size(); ++point) {
            const Vec2 start = curve[point - 1];
            const Vec2 end = curve[point];
            const Vec2 low{ .x = std::min(start.x, end.x), .y = std::min(start.y, end.y) };
            const Vec2 high{ .x = std::max(start.x, end.x), .y = std::max(start.y, end.y) };
            const TileSpan around =
                map.TilesIn({ .min = low - Vec2{ .x = reach, .y = reach }, .max = high + Vec2{ .x = reach, .y = reach } });
            for (u32 row = around.firstRow; row <= around.lastRow; ++row) {
                for (u32 column = around.firstColumn; column <= around.lastColumn; ++column) {
                    const u32 tile = map.TileIndex({ .column = column, .row = row });
                    if ((map.FlagsAt(tile) & road) == 0) {
                        continue;
                    }
                    const Vec2 centre = map.TileCentre(tile);
                    const Vec2 closest = ClosestOnSegment(centre, start, end);
                    const f32 apart = Length(closest - centre);
                    if (apart <= reach && (!m_onRoad[tile] || apart < nearest[tile])) {
                        nearest[tile] = apart;
                        m_onRoad[tile] = closest;
                    }
                }
            }
        }
    }
}

std::optional<Vec2> RoadCurveIndex::OnRoad(u32 tile) const {
    return tile < m_onRoad.size() ? m_onRoad[tile] : std::nullopt;
}

Vec2 DrawnWhere(const WorldMap& map, const RoadCurveIndex& roads, const Journey& journey, i64 tick, std::size_t member) {
    const Vec2 shift = journey.parts.empty() ? Vec2{} : JourneyShift(map, roads, journey, tick);
    return Where(map, journey, tick, member) + shift;
}

} // namespace core
