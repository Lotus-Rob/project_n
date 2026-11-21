#include "gfx/river_draw.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/vec2.hpp"
#include "gfx/ribbon.hpp"

namespace gfx {

namespace {

/// The spline's knot spacing grows as distance to this power: a half is the centripetal parametrisation, which never
/// loops or cusps within a segment.
constexpr f32 KNOT_POWER = 0.5F;
/// Where in a tile its centre lies, as a fraction of its size.
constexpr f32 TILE_CENTRE = 0.5F;
/// How far, in tiles, a river's last stretch may run on or stop short and still be taken to flow into another river.
constexpr f32 JOIN_TILES = 1.0F;
/// How finely, in tiles, a river looks ahead for water: a quarter tile, so it never steps over a tile's corner unseen.
constexpr f32 MOUTH_STEP_TILES = 0.25F;
/// How many steps ahead of its end a river looks for the water it runs into: three tiles.
constexpr u32 MOUTH_STEPS = 12;
/// How far into the water, in tiles, a river runs past where it meets it, fading out all the way, so it melts into the
/// water's wash rather than stopping at a square end or on the shore line.
constexpr f32 MOUTH_INTO_TILES = 1.0F;
constexpr u32 BYTE_BITS = 8;
constexpr u32 BYTE_MASK = 0xFFU;

core::Vec2 Lerp(core::Vec2 from, core::Vec2 target, f32 amount) {
    return from + ((target - from) * amount);
}

/// The point at parameter `param` in [knots[1], knots[2]] of the centripetal Catmull-Rom spline through `points[1]` and
/// `points[2]`, with `points[0]` and `points[3]` shaping its ends (Barry and Goldman's pyramid). Every neighbouring pair
/// must differ.
core::Vec2 SplineAt(const std::array<core::Vec2, 4>& points, const std::array<f32, 4>& knots, f32 param) {
    const auto blend = [&](core::Vec2 from, core::Vec2 target, std::size_t first, std::size_t last) {
        const f32 span = knots.at(last) - knots.at(first);
        return Lerp(from, target, (param - knots.at(first)) / span);
    };
    const core::Vec2 low = blend(points[0], points[1], 0, 1);
    const core::Vec2 mid = blend(points[1], points[2], 1, 2);
    const core::Vec2 high = blend(points[2], points[3], 2, 3);
    const core::Vec2 lowMid = blend(low, mid, 0, 2);
    const core::Vec2 midHigh = blend(mid, high, 1, 3);
    return blend(lowMid, midHigh, 1, 2);
}

/// The river's waypoints as world points (tile centres) with each repeat of the one before dropped.
std::vector<core::Vec2> Waypoints(std::span<const core::TilePoint> tiles, f32 tileSize) {
    std::vector<core::Vec2> points;
    points.reserve(tiles.size());
    core::TilePoint last{};
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

/// How many quads a segment of `length` world units takes per band.
std::size_t SegmentSteps(f32 length, f32 tileSize) {
    const f32 tiles = std::ceil(length / tileSize);
    return std::size_t{ RIVER_STEPS } * std::max<std::size_t>(1, static_cast<std::size_t>(tiles));
}

/// The spline through `waypoints` (two or more, none repeating its neighbour), as the points quads' edges lie at. Phantom
/// end points mirror the first and last neighbours, so the curve arrives along the end segments.
std::vector<core::Vec2> Midline(const std::vector<core::Vec2>& waypoints, f32 tileSize) {
    std::vector<core::Vec2> padded;
    padded.reserve(waypoints.size() + 2);
    padded.push_back(waypoints[0] + (waypoints[0] - waypoints[1]));
    padded.insert(padded.end(), waypoints.begin(), waypoints.end());
    const std::size_t count = waypoints.size();
    padded.push_back(waypoints[count - 1] + (waypoints[count - 1] - waypoints[count - 2]));

    std::vector<core::Vec2> line{ waypoints[0] };
    for (std::size_t segment = 0; segment + 1 < count; ++segment) {
        const std::array<core::Vec2, 4> points{
            padded[segment], padded[segment + 1], padded[segment + 2], padded[segment + 3]
        };
        std::array<f32, 4> knots{};
        for (std::size_t knot = 1; knot < 4; ++knot) {
            knots.at(knot) = knots.at(knot - 1) + std::pow(Length(points.at(knot) - points.at(knot - 1)), KNOT_POWER);
        }
        const std::size_t steps = SegmentSteps(Length(points[2] - points[1]), tileSize);
        for (std::size_t step = 1; step <= steps; ++step) {
            const f32 along = static_cast<f32>(step) / static_cast<f32>(steps);
            line.push_back(SplineAt(points, knots, knots[1] + ((knots[2] - knots[1]) * along)));
        }
    }
    return line;
}

/// Where the line through `from` and `toward` meets the segment from `start` to `end`, as the share of the way from `from`
/// to `toward`; none when they run parallel or meet off the segment.
std::optional<f32> Meeting(core::Vec2 from, core::Vec2 toward, core::Vec2 start, core::Vec2 end) {
    const core::Vec2 runs = toward - from;
    const core::Vec2 along = end - start;
    const f32 cross = (runs.x * along.y) - (runs.y * along.x);
    if (std::abs(cross) <= SHORTEST_TANGENT * Length(runs) * Length(along)) {
        return std::nullopt;
    }
    const core::Vec2 apart = start - from;
    const f32 onSegment = ((apart.x * runs.y) - (apart.y * runs.x)) / cross;
    if (onSegment < 0.0F || onSegment > 1.0F) {
        return std::nullopt;
    }
    return ((apart.x * along.y) - (apart.y * along.x)) / cross;
}

/// The point where the last stretch of `waypoints` (run on or cut short) meets the middle of one of `others`' lines but
/// the river's own (`own`), the nearest to its end and within JOIN_TILES of it; none when it meets none there. A meeting
/// within JOIN_TILES of the other line's own end doesn't count: there the other river flows into this one, not this one
/// into it, and this one keeps its end (and its way on into water).
std::optional<core::Vec2> Join(
    const std::vector<core::Vec2>& waypoints,
    std::size_t own,
    const std::vector<std::vector<core::Vec2>>& others,
    f32 tileSize
) {
    const core::Vec2 from = waypoints[waypoints.size() - 2];
    const core::Vec2 last = waypoints.back();
    const f32 reach = JOIN_TILES * tileSize;
    std::optional<core::Vec2> nearest;
    f32 nearestApart = reach;
    for (std::size_t other = 0; other < others.size(); ++other) {
        if (other == own) {
            continue;
        }
        const std::vector<core::Vec2>& line = others[other];
        for (std::size_t point = 1; point < line.size(); ++point) {
            const std::optional<f32> share = Meeting(from, last, line[point - 1], line[point]);
            if (!share || *share <= 0.0F) {
                continue;
            }
            const core::Vec2 meets = Lerp(from, last, *share);
            const f32 apart = Length(meets - last);
            if (Length(meets - line.back()) <= reach) {
                continue;
            }
            if (apart <= nearestApart) {
                nearest = meets;
                nearestApart = apart;
            }
        }
    }
    return nearest;
}

/// Where a river ending at `last` after `from` runs into water ahead of it: MOUTH_INTO_TILES past the first point on its
/// heading, within MOUTH_STEPS steps and on the map, whose tile is of a kind in `water`; none when it already ends in water
/// or finds none.
std::optional<core::Vec2> Mouth(const core::WorldMap& map, std::span<const u8> water, core::Vec2 from, core::Vec2 last) {
    const f32 tileSize = map.TileSize();
    const f32 width = static_cast<f32>(map.Columns()) * tileSize;
    const f32 height = static_cast<f32>(map.Rows()) * tileSize;
    const auto wet = [&](core::Vec2 point) { return std::ranges::contains(water, map.KindAt(map.TileAt(point))); };
    if (water.empty() || wet(last)) {
        return std::nullopt;
    }
    const core::Vec2 heading = HeadingOf(last - from, core::Vec2{ .x = 1.0F, .y = 0.0F });
    for (u32 step = 1; step <= MOUTH_STEPS; ++step) {
        const f32 ahead = static_cast<f32>(step) * MOUTH_STEP_TILES;
        const core::Vec2 point = last + (heading * (ahead * tileSize));
        if (point.x < 0.0F || point.y < 0.0F || point.x >= width || point.y >= height) {
            return std::nullopt;
        }
        if (wet(point)) {
            return last + (heading * ((ahead + MOUTH_INTO_TILES) * tileSize));
        }
    }
    return std::nullopt;
}

/// A river's midline and how far back from its end it fades out, in world units (0: it doesn't).
struct Laid {
    std::vector<core::Vec2> line;
    f32 fade = 0.0F;
};

/// The premultiplied colour `colour` (RibbonVertex::colour's layout) at `share` of its strength, from 0 to 1.
u32 Faded(u32 colour, f32 share) {
    u32 faded = 0;
    for (u32 shift = 0; shift < 4 * BYTE_BITS; shift += BYTE_BITS) {
        const auto channel = static_cast<f32>((colour >> shift) & BYTE_MASK);
        faded |= static_cast<u32>(std::lround(channel * share)) << shift;
    }
    return faded;
}

/// How much of the band shows at each point of `laid`'s line: all of it, but over the last `fade` world units before its
/// end, where it falls to nothing at the end.
std::vector<f32> Shares(const Laid& laid) {
    std::vector<f32> shares(laid.line.size(), 1.0F);
    f32 fromEnd = 0.0F;
    for (std::size_t point = laid.line.size() - 1; laid.fade > 0.0F && point > 0; --point) {
        shares[point] = std::min(1.0F, fromEnd / laid.fade);
        fromEnd += Length(laid.line[point] - laid.line[point - 1]);
    }
    return shares;
}

/// Adds the quads of one band of width `width` along `laid`'s line, in `colour`, fading out over its last stretch as
/// `laid` says.
void AddBand(const Laid& laid, f32 width, u32 colour, std::vector<RibbonVertex>& into) {
    const std::vector<core::Vec2>& line = laid.line;
    const std::size_t last = line.size() - 1;
    std::vector<core::Vec2> lefts;
    std::vector<core::Vec2> rights;
    lefts.reserve(line.size());
    rights.reserve(line.size());
    core::Vec2 heading{ .x = 1.0F, .y = 0.0F };
    for (std::size_t point = 0; point <= last; ++point) {
        // From the neighbours, so the band bends with the line without twisting; a stall keeps the last direction.
        const core::Vec2 runs = line[std::min(point + 1, last)] - line[point == 0 ? 0 : point - 1];
        heading = HeadingOf(runs, heading);
        const core::Vec2 side = SideOf(heading, width);
        lefts.push_back(line[point] + side);
        rights.push_back(line[point] - side);
    }
    const std::size_t first = into.size();
    into.resize(first + (last * QUAD_VERTICES));
    for (std::size_t quad = 0; quad < last; ++quad) {
        WriteRibbonQuad(
            std::span{ into }.subspan(first + (quad * QUAD_VERTICES), QUAD_VERTICES),
            { .left = lefts[quad], .right = rights[quad], .along = static_cast<f32>(quad) / static_cast<f32>(last) },
            { .left = lefts[quad + 1],
              .right = rights[quad + 1],
              .along = static_cast<f32>(quad + 1) / static_cast<f32>(last) },
            colour
        );
    }
    if (laid.fade <= 0.0F) {
        return;
    }
    const std::vector<f32> shares = Shares(laid);
    for (std::size_t quad = 0; quad < last; ++quad) {
        const std::span<RibbonVertex> corners = std::span{ into }.subspan(first + (quad * QUAD_VERTICES), QUAD_VERTICES);
        // Corners 0 and 1 stand at the quad's near end, 2 and 3 at its far end (WriteRibbonQuad()).
        corners[0].colour = Faded(colour, shares[quad]);
        corners[1].colour = corners[0].colour;
        corners[2].colour = Faded(colour, shares[quad + 1]);
        corners[3].colour = corners[2].colour;
    }
}

} // namespace

void DrawRivers(
    const core::WorldMap& map,
    const RiverShape& shape,
    std::span<const u8> water,
    std::vector<RibbonVertex>& into
) {
    into.clear();
    const std::span<const std::vector<core::TilePoint>> rivers = map.Rivers();
    std::vector<std::vector<core::Vec2>> waypoints;
    std::vector<std::vector<core::Vec2>> stored;
    waypoints.reserve(rivers.size());
    stored.reserve(rivers.size());
    for (const std::vector<core::TilePoint>& river : rivers) {
        waypoints.push_back(Waypoints(river, map.TileSize()));
        stored.push_back(waypoints.back().size() < 2 ? std::vector<core::Vec2>{} : Midline(waypoints.back(), map.TileSize()));
    }
    std::vector<Laid> lines;
    std::size_t quads = 0;
    for (std::size_t index = 0; index < rivers.size(); ++index) {
        std::vector<core::Vec2>& points = waypoints[index];
        if (points.size() < 2) {
            continue;
        }
        // A river flows into the river its end lies on or by, else on into the water just ahead of it.
        f32 fade = 0.0F;
        if (const std::optional<core::Vec2> joined = Join(points, index, stored, map.TileSize());
            joined && Length(*joined - points[points.size() - 2]) > SHORTEST_TANGENT) {
            points.back() = *joined;
        }
        else if (const std::optional<core::Vec2> mouth = Mouth(map, water, points[points.size() - 2], points.back())) {
            points.push_back(*mouth);
            fade = MOUTH_INTO_TILES * map.TileSize();
        }
        std::vector<core::Vec2> line = Midline(points, map.TileSize());
        const std::size_t taken = 2 * (line.size() - 1);
        if (quads + taken > MAX_RIVER_QUADS) {
            PN_LOG_WARNING(
                "{} of {} rivers left out: drawing them takes more than {} quads",
                rivers.size() - index,
                rivers.size(),
                MAX_RIVER_QUADS
            );
            break;
        }
        quads += taken;
        lines.push_back({ .line = std::move(line), .fade = fade });
    }
    into.reserve(quads * QUAD_VERTICES);
    for (const Laid& laid : lines) {
        AddBand(laid, shape.edgeWidth, shape.edgeColour, into);
    }
    for (const Laid& laid : lines) {
        AddBand(laid, shape.coreWidth, shape.coreColour, into);
    }
}

} // namespace gfx
