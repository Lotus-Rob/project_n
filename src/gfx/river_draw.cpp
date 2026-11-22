#include "gfx/river_draw.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
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
        waypoints.push_back(core::Waypoints(river, map.TileSize()));
        stored.push_back(
            waypoints.back().size() < 2 ? std::vector<core::Vec2>{}
                                        : core::Midline(waypoints.back(), map.TileSize(), RIVER_STEPS)
        );
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
        if (const std::optional<core::Vec2> joined = core::Join(points, index, stored, map.TileSize());
            joined && Length(*joined - points[points.size() - 2]) > SHORTEST_TANGENT) {
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
