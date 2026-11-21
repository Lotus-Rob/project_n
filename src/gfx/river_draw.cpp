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

/// Adds the quads of one band of width `width` along `line`, in `colour`.
void AddBand(const std::vector<core::Vec2>& line, f32 width, u32 colour, std::vector<RibbonVertex>& into) {
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
}

} // namespace

void DrawRivers(const core::WorldMap& map, const RiverShape& shape, std::vector<RibbonVertex>& into) {
    into.clear();
    std::vector<std::vector<core::Vec2>> lines;
    std::size_t quads = 0;
    const std::span<const std::vector<core::TilePoint>> rivers = map.Rivers();
    for (std::size_t index = 0; index < rivers.size(); ++index) {
        const std::vector<core::TilePoint>& river = rivers[index];
        const std::vector<core::Vec2> waypoints = Waypoints(river, map.TileSize());
        if (waypoints.size() < 2) {
            continue;
        }
        std::vector<core::Vec2> line = Midline(waypoints, map.TileSize());
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
        lines.push_back(std::move(line));
    }
    into.reserve(quads * QUAD_VERTICES);
    for (const std::vector<core::Vec2>& line : lines) {
        AddBand(line, shape.edgeWidth, shape.edgeColour, into);
    }
    for (const std::vector<core::Vec2>& line : lines) {
        AddBand(line, shape.coreWidth, shape.coreColour, into);
    }
}

} // namespace gfx
