#include "gfx/river_draw.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/profile.hpp"
#include "core/road_curve.hpp"
#include "core/vec2.hpp"
#include "gfx/ribbon.hpp"
#include "gfx/ribbon_band.hpp"

namespace gfx {

namespace {

/// How finely, in tiles, a river looks ahead for water: a quarter tile, so it never steps over a tile's corner unseen.
constexpr f32 MOUTH_STEP_TILES = 0.25F;
/// How many steps ahead of its end a river looks for the water it runs into: three tiles.
constexpr u32 MOUTH_STEPS = 12;
/// How far into the water, in tiles, a river runs past where it meets it, fading out all the way, so it melts into the
/// water's wash rather than stopping at a square end or on the shore line.
constexpr f32 MOUTH_INTO_TILES = 1.0F;

/// Whether `point`'s tile is of a kind in `water`.
bool IsWet(const core::WorldMap& map, std::span<const u8> water, core::Vec2 point) {
    return std::ranges::contains(water, map.KindAt(map.TileAt(point)));
}

/// Where a river ending at `last` after `from` runs into water ahead of it: MOUTH_INTO_TILES past the first point on its
/// heading, within MOUTH_STEPS steps and on the map, whose tile is of a kind in `water`, kept within the map; none when it
/// already ends in water or finds none.
std::optional<core::Vec2> Mouth(const core::WorldMap& map, std::span<const u8> water, core::Vec2 from, core::Vec2 last) {
    const f32 tileSize = map.TileSize();
    const f32 width = static_cast<f32>(map.Columns()) * tileSize;
    const f32 height = static_cast<f32>(map.Rows()) * tileSize;
    if (water.empty() || IsWet(map, water, last)) {
        return std::nullopt;
    }
    const core::Vec2 heading = HeadingOf(last - from, core::Vec2{ .x = 1.0F, .y = 0.0F });
    for (u32 step = 1; step <= MOUTH_STEPS; ++step) {
        const f32 ahead = static_cast<f32>(step) * MOUTH_STEP_TILES;
        const core::Vec2 point = last + (heading * (ahead * tileSize));
        if (point.x < 0.0F || point.y < 0.0F || point.x >= width || point.y >= height) {
            return std::nullopt;
        }
        if (IsWet(map, water, point)) {
            const core::Vec2 into = last + (heading * ((ahead + MOUTH_INTO_TILES) * tileSize));
            return core::Vec2{ .x = std::clamp(into.x, 0.0F, width), .y = std::clamp(into.y, 0.0F, height) };
        }
    }
    return std::nullopt;
}

/// How one river begins and, for a pond with water behind it, the point it begins at.
struct Start {
    /// The kind of start.
    RiverStart kind;
    /// The point a pond's river begins at before its own first point; none when it already starts in water or isn't a pond.
    std::optional<core::Vec2> point;
};

/// How the river of `points` (its waypoints; the river at `index` among those whose midlines are `stored`) begins. Shared
/// by RiverStarts() and DrawRivers() so they never disagree.
Start StartOf(
    const core::WorldMap& map,
    std::span<const u8> water,
    std::span<const core::Vec2> points,
    std::size_t index,
    std::span<const std::vector<core::Vec2>> stored
) {
    if (points.size() < 2) {
        return { .kind = RiverStart::Spring, .point = std::nullopt };
    }
    const std::vector<core::Vec2> reversed(points.rbegin(), points.rend());
    if (core::JoinedHead(reversed, index, stored, map.TileSize())) {
        return { .kind = RiverStart::Branch, .point = std::nullopt };
    }
    if (IsWet(map, water, points[0])) {
        return { .kind = RiverStart::Pond, .point = std::nullopt };
    }
    if (const std::optional<core::Vec2> mouth = Mouth(map, water, points[1], points[0])) {
        return { .kind = RiverStart::Pond, .point = mouth };
    }
    return { .kind = RiverStart::Spring, .point = std::nullopt };
}

/// Every river's waypoints of `map`, in the map's order.
std::vector<std::vector<core::Vec2>> WaypointsOf(const core::WorldMap& map) {
    std::vector<std::vector<core::Vec2>> waypoints;
    waypoints.reserve(map.Rivers().size());
    for (const std::vector<core::TilePoint>& river : map.Rivers()) {
        waypoints.push_back(core::Waypoints(river, map.TileSize()));
    }
    return waypoints;
}

} // namespace

std::vector<RiverStart> RiverStarts(const core::WorldMap& map, std::span<const u8> water) {
    const std::vector<std::vector<core::Vec2>> waypoints = WaypointsOf(map);
    const std::vector<std::vector<core::Vec2>> stored = core::Midlines(waypoints, map.TileSize(), RIVER_STEPS);
    std::vector<RiverStart> starts;
    starts.reserve(waypoints.size());
    for (std::size_t index = 0; index < waypoints.size(); ++index) {
        starts.push_back(StartOf(map, water, waypoints[index], index, stored).kind);
    }
    return starts;
}

void DrawRivers(
    const core::WorldMap& map,
    const RibbonShape& shape,
    std::span<const u8> water,
    std::vector<RibbonVertex>& into
) {
    PN_PROFILE_ZONE();
    into.clear();
    const std::span<const std::vector<core::TilePoint>> rivers = map.Rivers();
    std::vector<std::vector<core::Vec2>> waypoints = WaypointsOf(map);
    const std::vector<std::vector<core::Vec2>> stored = core::Midlines(waypoints, map.TileSize(), RIVER_STEPS);
    std::vector<Laid> lines;
    std::size_t quads = 0;
    for (std::size_t index = 0; index < rivers.size(); ++index) {
        std::vector<core::Vec2>& points = waypoints[index];
        if (points.size() < 2) {
            continue;
        }
        f32 fadeIn = 0.0F;
        f32 taper = 0.0F;
        const Start start = StartOf(map, water, points, index, stored);
        if (start.kind == RiverStart::Pond) {
            fadeIn = MOUTH_INTO_TILES * map.TileSize();
            if (start.point) {
                points.insert(points.begin(), *start.point);
            }
        }
        else if (start.kind == RiverStart::Spring) {
            taper = shape.taper;
        }
        // A river flows into the river its end lies on or by, else on into the water just ahead of it.
        f32 fade = 0.0F;
        if (const std::optional<core::Vec2> joined = core::JoinedEnd(points, index, stored, map.TileSize(), false)) {
            points.back() = *joined;
        }
        else if (const std::optional<core::Vec2> mouth = Mouth(map, water, points[points.size() - 2], points.back())) {
            points.push_back(*mouth);
            fade = MOUTH_INTO_TILES * map.TileSize();
        }
        std::vector<core::Vec2> line = core::Midline(points, map.TileSize(), RIVER_STEPS);
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
        lines.push_back({ .line = std::move(line), .fade = fade, .fadeIn = fadeIn, .taper = taper });
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
