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

/// The curves of a meadow map of `roads` (the city stands on tile (0, 0)).
std::vector<std::vector<core::Vec2>> Curves(std::vector<std::vector<TilePoint>> roads) {
    return core::RoadCurves(test::MeadowMap(8, 8, {}, std::move(roads)));
}

TEST_CASE("a straight road's curve runs through its tile centres") {
    const auto curves = Curves({ { { .column = 1, .row = 2 }, { .column = 6, .row = 2 } } });
    REQUIRE(curves.size() == 1);
    const std::vector<core::Vec2>& curve = curves[0];
    REQUIRE(curve.size() == 1 + (std::size_t{ 5 } * core::ROAD_STEPS));
    f32 worst = 0.0F;
    for (const core::Vec2 point : curve) {
        worst = std::max(worst, std::abs(point.y - Middle(1, 2).y));
    }
    CHECK(worst < CLOSE);
    CHECK(curve.front().x == Middle(1, 2).x);
    CHECK(curve.front().y == Middle(1, 2).y);
    CHECK(curve.back().x == Middle(6, 2).x);
    CHECK(curve.back().y == Middle(6, 2).y);
}

TEST_CASE("a road's curve passes through every waypoint") {
    const std::vector<TilePoint> line{ { .column = 1, .row = 1 },
                                       { .column = 5, .row = 1 },
                                       { .column = 5, .row = 1 },
                                       { .column = 6, .row = 4 },
                                       { .column = 2, .row = 6 } };
    const std::vector<core::Vec2> waypoints = core::Waypoints(line, TILE_SIZE);
    REQUIRE(waypoints.size() == 4);
    const std::vector<core::Vec2> curve = Curves({ line })[0];
    for (const core::Vec2 waypoint : waypoints) {
        const auto nearest = std::ranges::min(curve, {}, [&](core::Vec2 point) { return Distance(point, waypoint); });
        CHECK(Distance(nearest, waypoint) < CLOSE);
    }
}

TEST_CASE("a road of fewer than two distinct points has an empty curve, its index kept") {
    const auto curves = Curves(
        { { { .column = 3, .row = 3 }, { .column = 3, .row = 3 } }, { { .column = 1, .row = 1 }, { .column = 4, .row = 1 } } }
    );
    REQUIRE(curves.size() == 2);
    CHECK(curves[0].empty());
    CHECK(curves[1].size() == 1 + (std::size_t{ 3 } * core::ROAD_STEPS));
}

TEST_CASE("a road's end meeting another's middle joins it") {
    // The main road crosses row 4; the branch stops at (4, 3), a tile short of it: exactly the reach, so it counts.
    const auto curves = Curves(
        { { { .column = 0, .row = 4 }, { .column = 7, .row = 4 } }, { { .column = 4, .row = 0 }, { .column = 4, .row = 3 } } }
    );
    CHECK(Distance(curves[1].back(), Middle(4, 4)) < CLOSE);
    CHECK(curves[1].back().x == doctest::Approx(Middle(4, 0).x));
}

TEST_CASE("a road ending past another's middle is cut back to it") {
    // The main road runs from (0, 4) to (7, 5), so at column 4 its middle lies 4/7 of a tile below row 4's centre.
    const auto curves = Curves(
        { { { .column = 0, .row = 4 }, { .column = 7, .row = 5 } }, { { .column = 4, .row = 0 }, { .column = 4, .row = 5 } } }
    );
    CHECK(curves[1].back().x == doctest::Approx(Middle(4, 0).x));
    CHECK(std::abs(curves[1].back().y - (Middle(4, 4).y + (TILE_SIZE * 4.0F / 7.0F))) < 1.0F);
}

TEST_CASE("a road starting beside another's middle joins it") {
    // The branch leaves from (4, 5), a tile below the main road at row 4, and runs south.
    const auto curves = Curves(
        { { { .column = 0, .row = 4 }, { .column = 7, .row = 4 } }, { { .column = 4, .row = 5 }, { .column = 4, .row = 7 } } }
    );
    CHECK(Distance(curves[1].front(), Middle(4, 4)) < CLOSE);
    CHECK(Distance(curves[1].back(), Middle(4, 7)) < CLOSE);
}

TEST_CASE("a road starting past another's middle is cut back to it") {
    const auto curves = Curves(
        { { { .column = 0, .row = 4 }, { .column = 7, .row = 5 } }, { { .column = 4, .row = 5 }, { .column = 4, .row = 7 } } }
    );
    CHECK(curves[1].front().x == doctest::Approx(Middle(4, 0).x));
    CHECK(std::abs(curves[1].front().y - (Middle(4, 4).y + (TILE_SIZE * 4.0F / 7.0F))) < 1.0F);
}

TEST_CASE("a road ending two tiles from another keeps its end") {
    const auto curves = Curves(
        { { { .column = 0, .row = 4 }, { .column = 7, .row = 4 } }, { { .column = 4, .row = 0 }, { .column = 4, .row = 2 } } }
    );
    CHECK(Distance(curves[1].back(), Middle(4, 2)) < CLOSE);
}

TEST_CASE("a meeting within a tile of the other road's own end doesn't join") {
    // The short road's end (1, 4) lies where the first road's line would meet it, so it flows into the first, not the reverse.
    const auto curves = Curves(
        { { { .column = 1, .row = 0 }, { .column = 1, .row = 3 } }, { { .column = 0, .row = 4 }, { .column = 1, .row = 4 } } }
    );
    CHECK(Distance(curves[0].back(), Middle(1, 3)) < CLOSE);
}

TEST_CASE("two roads starting on one tile both start at its centre") {
    const auto curves = Curves(
        { { { .column = 3, .row = 3 }, { .column = 7, .row = 3 }, { .column = 7, .row = 6 } },
          { { .column = 3, .row = 3 }, { .column = 3, .row = 7 } },
          { { .column = 3, .row = 3 }, { .column = 6, .row = 6 } } }
    );
    for (const auto& curve : curves) {
        CHECK(Distance(curve.front(), Middle(3, 3)) < CLOSE);
    }
}

TEST_CASE("a road starting where another ends keeps both ends there") {
    const auto curves = Curves(
        { { { .column = 1, .row = 6 }, { .column = 3, .row = 3 } }, { { .column = 3, .row = 3 }, { .column = 7, .row = 3 } } }
    );
    CHECK(Distance(curves[0].back(), Middle(3, 3)) < CLOSE);
    CHECK(Distance(curves[1].front(), Middle(3, 3)) < CLOSE);
}

TEST_CASE("a meeting within a tile of the other road's start doesn't join") {
    // The second road starts a tile above the first's line and runs down across it, so the first road's end would be cut back
    // to that crossing if it didn't see the second as flowing out of the tile beside it.
    const auto curves = Curves(
        { { { .column = 1, .row = 3 }, { .column = 5, .row = 3 } }, { { .column = 4, .row = 2 }, { .column = 4, .row = 6 } } }
    );
    CHECK(Distance(curves[0].back(), Middle(5, 3)) < CLOSE);
}

TEST_CASE("a road ending at a city keeps its end on the city's centre") {
    // The city stands on (0, 0). The other road starts on the first road's line a tile from that end, so without the city
    // the end would be drawn there instead.
    const auto curves = Curves(
        { { { .column = 0, .row = 0 }, { .column = 3, .row = 0 } }, { { .column = 1, .row = 0 }, { .column = 1, .row = 3 } } }
    );
    CHECK(curves[0].front().x == Middle(0, 0).x);
    CHECK(curves[0].front().y == Middle(0, 0).y);
    const auto ending = Curves(
        { { { .column = 3, .row = 0 }, { .column = 0, .row = 0 } }, { { .column = 1, .row = 0 }, { .column = 1, .row = 3 } } }
    );
    CHECK(ending[0].back().x == Middle(0, 0).x);
    CHECK(ending[0].back().y == Middle(0, 0).y);
}

} // namespace
