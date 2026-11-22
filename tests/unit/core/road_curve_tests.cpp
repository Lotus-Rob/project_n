#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/road_curve.hpp"
#include "support/meadow.hpp"

#include <doctest/doctest.h>

namespace {

using core::TilePoint;

constexpr f32 TILE_SIZE = 256.0F;
constexpr f32 CLOSE = 0.001F;

core::Vec2 Middle(u32 column, u32 row) {
    return { .x = (static_cast<f32>(column) + 0.5F) * TILE_SIZE, .y = (static_cast<f32>(row) + 0.5F) * TILE_SIZE };
}

f32 Distance(core::Vec2 from, core::Vec2 target) {
    return std::hypot(target.x - from.x, target.y - from.y);
}

std::vector<core::Vec2> Through(const std::vector<TilePoint>& line, const std::vector<std::vector<TilePoint>>& others = {}) {
    return core::CurveThrough(line, others, TILE_SIZE, core::ROAD_STEPS);
}

TEST_CASE("a straight road's curve runs through its tile centres") {
    const std::vector<core::Vec2> curve = Through({ { .column = 1, .row = 2 }, { .column = 6, .row = 2 } });
    REQUIRE(curve.size() == 1 + (std::size_t{ 5 } * core::ROAD_STEPS));
    f32 worst = 0.0F;
    for (const core::Vec2 point : curve) {
        worst = std::max(worst, std::abs(point.y - Middle(1, 2).y));
    }
    CHECK(worst < CLOSE);
    CHECK(Distance(curve.front(), Middle(1, 2)) < CLOSE);
    CHECK(Distance(curve.back(), Middle(6, 2)) < CLOSE);
}

TEST_CASE("a road's curve passes through every waypoint") {
    const std::vector<TilePoint> line{ { .column = 1, .row = 1 },
                                       { .column = 5, .row = 1 },
                                       { .column = 5, .row = 1 },
                                       { .column = 6, .row = 4 },
                                       { .column = 2, .row = 6 } };
    const std::vector<core::Vec2> curve = Through(line);
    // The repeated tile is one waypoint, so the knots are every 4th point of the first segments' steps.
    const std::vector<core::Vec2> waypoints = core::Waypoints(line, TILE_SIZE);
    REQUIRE(waypoints.size() == 4);
    for (const core::Vec2 waypoint : waypoints) {
        const auto nearest = std::ranges::min(curve, {}, [&](core::Vec2 point) { return Distance(point, waypoint); });
        CHECK(Distance(nearest, waypoint) < CLOSE);
    }
}

TEST_CASE("a road of fewer than two distinct points has no curve") {
    CHECK(Through({ { .column = 3, .row = 3 }, { .column = 3, .row = 3 } }).empty());
    CHECK(Through({}).empty());
}

TEST_CASE("a road's end meeting another's middle joins it") {
    // The main road crosses row 4 from column 0 to 7; the branch comes down column 4 and stops at (4, 3), half a tile
    // short of the main road's middle, so it runs on to it.
    const std::vector<TilePoint> main{ { .column = 0, .row = 4 }, { .column = 7, .row = 4 } };
    const std::vector<TilePoint> branch{ { .column = 4, .row = 0 }, { .column = 4, .row = 3 } };
    const std::vector<core::Vec2> curve = Through(branch, { main });
    CHECK(Distance(curve.back(), Middle(4, 4)) < CLOSE);
    CHECK(curve.back().x == doctest::Approx(Middle(4, 0).x));
}

TEST_CASE("a road ending two tiles from another keeps its end") {
    const std::vector<TilePoint> main{ { .column = 0, .row = 4 }, { .column = 7, .row = 4 } };
    const std::vector<TilePoint> branch{ { .column = 4, .row = 0 }, { .column = 4, .row = 2 } };
    CHECK(Distance(Through(branch, { main }).back(), Middle(4, 2)) < CLOSE);
}

TEST_CASE("every road of a map has its curve, an empty one for a road of a point") {
    const core::WorldMap map = test::MeadowMap(
        8,
        8,
        {},
        { { { .column = 0, .row = 4 }, { .column = 7, .row = 4 } },
          { { .column = 2, .row = 2 } },
          { { .column = 4, .row = 0 }, { .column = 4, .row = 3 } } }
    );
    const std::vector<std::vector<core::Vec2>> curves = core::RoadCurves(map);
    REQUIRE(curves.size() == 3);
    CHECK(curves[0].size() == 1 + (std::size_t{ 7 } * core::ROAD_STEPS));
    CHECK(curves[1].empty());
    CHECK(Distance(curves[2].back(), Middle(4, 4)) < CLOSE);
    CHECK(Distance(curves[0].back(), Middle(7, 4)) < CLOSE);
}

} // namespace
