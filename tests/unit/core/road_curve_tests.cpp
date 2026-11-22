#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/journey.hpp"
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
    CHECK(Distance(curves[1].front(), Middle(4, 3)) < CLOSE);
}

TEST_CASE("of two ends crossing a short road near both its ends, exactly one joins") {
    // The short road (3, 3) to (5, 3) is crossed at its middle by the long road's end. Its start lies on a third road, so
    // only its end and the long road's end would join each other, and the lower-indexed road's end stays.
    const std::vector<TilePoint> shortRoad{ { .column = 3, .row = 3 }, { .column = 5, .row = 3 } };
    const std::vector<TilePoint> longRoad{ { .column = 4, .row = 0 }, { .column = 4, .row = 4 } };
    const std::vector<TilePoint> third{ { .column = 3, .row = 1 }, { .column = 3, .row = 6 } };
    const auto curves = Curves({ shortRoad, longRoad, third });
    CHECK(Distance(curves[0].front(), Middle(3, 3)) < CLOSE);
    CHECK(Distance(curves[0].back(), Middle(5, 3)) < CLOSE);
    CHECK(Distance(curves[1].back(), Middle(4, 3)) < CLOSE);
    const auto swapped = Curves({ longRoad, shortRoad, third });
    CHECK(Distance(swapped[0].back(), Middle(4, 4)) < CLOSE);
    CHECK(Distance(swapped[1].back(), Middle(4, 3)) < CLOSE);
}

TEST_CASE("a road whose both ends would join one crossing keeps both ends") {
    // The short road's ends both lie a tile from the first road's line and each would be cut back to the same crossing.
    const auto curves = Curves(
        { { { .column = 3, .row = 3 }, { .column = 5, .row = 3 } }, { { .column = 4, .row = 2 }, { .column = 4, .row = 4 } } }
    );
    const bool finite =
        std::ranges::all_of(curves[1], [](core::Vec2 point) { return std::isfinite(point.x) && std::isfinite(point.y); });
    CHECK(finite);
    CHECK(Distance(curves[1].front(), Middle(4, 2)) < CLOSE);
    CHECK(Distance(curves[1].back(), Middle(4, 4)) < CLOSE);
}

TEST_CASE("a curve skips a waypoint repeating the one before it") {
    const std::vector<core::Vec2> repeated{ Middle(1, 1), Middle(1, 1), Middle(3, 1), Middle(3, 1) };
    const std::vector<core::Vec2> curve = core::Midline(repeated, TILE_SIZE, core::ROAD_STEPS);
    REQUIRE(curve.size() == 1 + (std::size_t{ 2 } * core::ROAD_STEPS));
    const bool finite =
        std::ranges::all_of(curve, [](core::Vec2 point) { return std::isfinite(point.x) && std::isfinite(point.y); });
    CHECK(finite);
    CHECK(Distance(curve.front(), Middle(1, 1)) < CLOSE);
    CHECK(Distance(curve.back(), Middle(3, 1)) < CLOSE);
    const std::vector<core::Vec2> single{ Middle(2, 2), Middle(2, 2) };
    const std::vector<core::Vec2> point = core::Midline(single, TILE_SIZE, core::ROAD_STEPS);
    REQUIRE(point.size() == 1);
    CHECK(Distance(point[0], Middle(2, 2)) < CLOSE);
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

// The drawn walkers below walk this many world units a tick: a straight step of a tile takes 16 ticks.
constexpr f32 PACE = 16.0F;
// How near a road's curve a walker drawn on it stands, and how much farther than its simulated step a drawn walker may
// move in a tick: a fiftieth and a twentieth of a tile.
constexpr f32 ON_CURVE = 0.02F * TILE_SIZE;
constexpr f32 EASE_SLACK = 0.05F * TILE_SIZE;
// A road from (1, 1) to (9, 5), whose tiles step as a staircase, (1, 1) (2, 1) (2, 2) (3, 2) (4, 2) (4, 3) (5, 3) (6, 3)
// (6, 4) (7, 4) (8, 4) (8, 5) (9, 5), while its curve runs straight between the two ends: each corner tile's centre, as
// (2, 1)'s or (6, 4)'s, stands 0.45 of a tile off it.
std::vector<TilePoint> Staircase() {
    return { { .column = 1, .row = 1 }, { .column = 9, .row = 5 } };
}

/// A 12 by 8 meadow map of `roads`.
core::WorldMap RoadMap(std::vector<std::vector<TilePoint>> roads) {
    return test::MeadowMap(12, 8, {}, std::move(roads));
}

/// A journey of `members` walkers on one leg over `tiles` of `map`, setting out from the first tile's centre (or `origin`)
/// at tick 0, PACE units a tick.
core::Journey Walk(
    const core::WorldMap& map,
    std::span<const TilePoint> tiles,
    u32 members = 1,
    std::optional<core::Vec2> origin = std::nullopt
) {
    std::vector<u32> indices;
    for (const TilePoint tile : tiles) {
        indices.push_back(map.TileIndex(tile));
    }
    std::optional<core::Leg> leg = core::MakePacedLeg(map, 0, origin.value_or(map.TileCentre(indices[0])), indices, PACE);
    REQUIRE(leg);
    core::Journey journey;
    for (u32 member = 1; member <= members; ++member) {
        journey.members.push_back(core::StableId{ member });
    }
    journey.parts.push_back({ .stay = false, .leg = std::move(leg).value_or(core::Leg{}), .rest = {} });
    return journey;
}

/// How far `point` lies from the polyline `curve`.
f32 OffCurve(core::Vec2 point, std::span<const core::Vec2> curve) {
    f32 nearest = std::numeric_limits<f32>::max();
    for (std::size_t index = 1; index < curve.size(); ++index) {
        const core::Vec2 along = curve[index] - curve[index - 1];
        const core::Vec2 apart = point - curve[index - 1];
        const f32 lengthSquared = (along.x * along.x) + (along.y * along.y);
        const f32 share = std::clamp(((apart.x * along.x) + (apart.y * along.y)) / lengthSquared, 0.0F, 1.0F);
        nearest = std::min(nearest, Distance(point, curve[index - 1] + (along * share)));
    }
    return nearest;
}

/// How far along the polyline `curve` the point of it closest to `point` lies.
f32 AlongCurve(core::Vec2 point, std::span<const core::Vec2> curve) {
    f32 nearest = std::numeric_limits<f32>::max();
    f32 along = 0.0F;
    f32 walked = 0.0F;
    for (std::size_t index = 1; index < curve.size(); ++index) {
        const core::Vec2 step = curve[index] - curve[index - 1];
        const core::Vec2 apart = point - curve[index - 1];
        const f32 length = Distance(curve[index - 1], curve[index]);
        const f32 share = std::clamp(((apart.x * step.x) + (apart.y * step.y)) / (length * length), 0.0F, 1.0F);
        const f32 off = Distance(point, curve[index - 1] + (step * share));
        if (off < nearest) {
            nearest = off;
            along = walked + (share * length);
        }
        walked += length;
    }
    return along;
}

/// The tick `journey`'s walker reaches its leg's tile `index`.
i64 ArriveTick(const core::Journey& journey, std::size_t index) {
    return journey.parts.front().leg.startTick + i64{ journey.parts.front().leg.arrive.at(index) };
}

/// The most that `journey`'s drawn walker moves farther than its simulated one in a tick, over the ticks from `first` to
/// `last`.
f32 WorstExtraMove(
    const core::WorldMap& map,
    const core::RoadCurveIndex& roads,
    const core::Journey& journey,
    i64 first,
    i64 last
) {
    f32 worst = 0.0F;
    for (i64 tick = first; tick < last; ++tick) {
        const f32 drawn =
            Distance(core::DrawnWhere(map, roads, journey, tick), core::DrawnWhere(map, roads, journey, tick + 1));
        const f32 walked = Distance(core::Where(map, journey, tick), core::Where(map, journey, tick + 1));
        worst = std::max(worst, drawn - walked);
    }
    return worst;
}

/// Whether two points have the same bits.
bool SamePoint(core::Vec2 lhs, core::Vec2 rhs) {
    return std::bit_cast<u64>(lhs) == std::bit_cast<u64>(rhs);
}

TEST_CASE("a walker at a road tile's centre mid-road is drawn on the curve") {
    const core::WorldMap map = RoadMap({ Staircase() });
    const core::RoadCurveIndex roads{ map };
    const std::vector<core::Vec2> curve = core::RoadCurves(map)[0];
    const std::vector<TilePoint> tiles{ { .column = 1, .row = 1 }, { .column = 2, .row = 1 }, { .column = 2, .row = 2 },
                                        { .column = 3, .row = 2 }, { .column = 4, .row = 2 }, { .column = 4, .row = 3 },
                                        { .column = 5, .row = 3 }, { .column = 6, .row = 3 }, { .column = 6, .row = 4 },
                                        { .column = 7, .row = 4 }, { .column = 8, .row = 4 }, { .column = 8, .row = 5 },
                                        { .column = 9, .row = 5 } };
    const core::Journey journey = Walk(map, tiles);
    // Every centre between the journey's two ends, the corner tiles' among them.
    f32 worstDrawn = 0.0F;
    f32 worstWalked = 0.0F;
    for (std::size_t index = 1; index + 1 < tiles.size(); ++index) {
        const i64 tick = ArriveTick(journey, index);
        worstDrawn = std::max(worstDrawn, OffCurve(core::DrawnWhere(map, roads, journey, tick), curve));
        worstWalked = std::max(worstWalked, OffCurve(core::Where(map, journey, tick), curve));
    }
    CHECK(worstDrawn < ON_CURVE);
    CHECK(worstWalked > 0.4F * TILE_SIZE);
}

TEST_CASE("a walker off the road is drawn where it walks") {
    const core::WorldMap map = RoadMap({ Staircase() });
    const core::RoadCurveIndex roads{ map };
    // Along the row under the road's end, whose tiles towards the east lie within a tile and a half of its curve; round the
    // road's end, beside it; and down the far column.
    const std::vector<TilePoint> under = [] {
        std::vector<TilePoint> row;
        row.reserve(12);
        for (u32 column = 0; column < 12; ++column) {
            row.push_back({ .column = column, .row = 6 });
        }
        return row;
    }();
    const std::vector<TilePoint> beside{
        { .column = 9, .row = 6 }, { .column = 10, .row = 6 }, { .column = 11, .row = 5 }, { .column = 11, .row = 4 }
    };
    const std::vector<TilePoint> farAway{ { .column = 11, .row = 0 }, { .column = 11, .row = 1 }, { .column = 11, .row = 2 } };
    bool same = true;
    for (const std::vector<TilePoint>* tiles : { &under, &beside, &farAway }) {
        const core::Journey journey = Walk(map, *tiles);
        for (const i64 tick : { -1, 0, 7, 16, 23, 41, 60, 152, 170, 1'000 }) {
            same = same && SamePoint(core::DrawnWhere(map, roads, journey, tick), core::Where(map, journey, tick));
        }
    }
    CHECK(same);
}

TEST_CASE("a walker stepping onto a road is drawn easing on") {
    const core::WorldMap map = RoadMap({ Staircase() });
    const core::RoadCurveIndex roads{ map };
    const std::vector<core::Vec2> curve = core::RoadCurves(map)[0];
    // Straight up onto the corner tile (6, 4), and diagonally across the staircase's corner at (4, 2) onto (4, 3); each
    // walks on along the road, so the tile it steps onto is no end of its journey.
    const std::vector<TilePoint> straight{ { .column = 6, .row = 6 },
                                           { .column = 6, .row = 5 },
                                           { .column = 6, .row = 4 },
                                           { .column = 7, .row = 4 },
                                           { .column = 8, .row = 4 } };
    const std::vector<TilePoint> diagonal{ { .column = 7, .row = 0 }, { .column = 6, .row = 1 }, { .column = 5, .row = 2 },
                                           { .column = 4, .row = 3 }, { .column = 3, .row = 2 }, { .column = 2, .row = 2 } };
    for (const auto& [tiles, onto] : { std::pair{ &straight, std::size_t{ 2 } }, std::pair{ &diagonal, std::size_t{ 3 } } }) {
        CAPTURE(onto);
        const core::Journey journey = Walk(map, *tiles);
        const i64 first = ArriveTick(journey, onto - 1);
        const i64 last = ArriveTick(journey, onto);
        CHECK(SamePoint(core::DrawnWhere(map, roads, journey, first), core::Where(map, journey, first)));
        CHECK(WorstExtraMove(map, roads, journey, first, last) <= EASE_SLACK);
        CHECK(OffCurve(core::DrawnWhere(map, roads, journey, last), curve) < ON_CURVE);
    }
}

TEST_CASE("a walker crossing a staircase corner diagonally is drawn keeping to the curve") {
    const core::WorldMap map = RoadMap({ Staircase() });
    const core::RoadCurveIndex roads{ map };
    const std::vector<core::Vec2> curve = core::RoadCurves(map)[0];
    // Diagonally past each corner tile, (2, 1), (4, 2), (6, 3) and (8, 4), rather than round it.
    const std::vector<TilePoint> tiles{ { .column = 1, .row = 1 }, { .column = 2, .row = 2 }, { .column = 3, .row = 2 },
                                        { .column = 4, .row = 3 }, { .column = 5, .row = 3 }, { .column = 6, .row = 4 },
                                        { .column = 7, .row = 4 }, { .column = 8, .row = 5 }, { .column = 9, .row = 5 } };
    const core::Journey journey = Walk(map, tiles);
    const i64 end = core::EndTick(journey);
    f32 worstDrawn = 0.0F;
    f32 worstWalked = 0.0F;
    for (i64 tick = 0; tick <= end; tick += end / 20) {
        worstDrawn = std::max(worstDrawn, OffCurve(core::DrawnWhere(map, roads, journey, tick), curve));
        worstWalked = std::max(worstWalked, OffCurve(core::Where(map, journey, tick), curve));
    }
    CHECK(worstDrawn < ON_CURVE);
    CHECK(worstWalked > 0.4F * TILE_SIZE);
    // Over the step from (2, 2) to (3, 2), past the corner at (3, 1)'s side of the road.
    CHECK(WorstExtraMove(map, roads, journey, ArriveTick(journey, 1), ArriveTick(journey, 2)) <= EASE_SLACK);
}

TEST_CASE("drawn positions move forward along the road") {
    // A road bending through (4, 2) and (9, 1), walked over the tiles it covers, a corner tile at each of its turns.
    const core::WorldMap map = RoadMap(
        { { { .column = 1, .row = 6 }, { .column = 4, .row = 2 }, { .column = 9, .row = 1 }, { .column = 10, .row = 5 } } }
    );
    const core::RoadCurveIndex roads{ map };
    const std::vector<core::Vec2> curve = core::RoadCurves(map)[0];
    const std::vector<TilePoint> tiles{
        { .column = 1, .row = 6 },  { .column = 1, .row = 5 },  { .column = 2, .row = 5 },  { .column = 2, .row = 4 },
        { .column = 3, .row = 4 },  { .column = 3, .row = 3 },  { .column = 4, .row = 3 },  { .column = 4, .row = 2 },
        { .column = 5, .row = 2 },  { .column = 6, .row = 2 },  { .column = 6, .row = 1 },  { .column = 7, .row = 1 },
        { .column = 8, .row = 1 },  { .column = 9, .row = 1 },  { .column = 9, .row = 2 },  { .column = 9, .row = 3 },
        { .column = 10, .row = 3 }, { .column = 10, .row = 4 }, { .column = 10, .row = 5 },
    };
    const core::Journey journey = Walk(map, tiles);
    const i64 end = core::EndTick(journey);
    f32 worstBack = 0.0F;
    f32 before = AlongCurve(core::DrawnWhere(map, roads, journey, 0), curve);
    for (i64 tick = end / 20; tick <= end; tick += end / 20) {
        const f32 along = AlongCurve(core::DrawnWhere(map, roads, journey, tick), curve);
        worstBack = std::max(worstBack, before - along);
        before = along;
    }
    CHECK(worstBack <= 0.0F);
}

TEST_CASE("a walker turning at a fork, or off the road there, is drawn without a jump") {
    // A branch down from the staircase's corner tile (6, 4): its end there joins the main road's curve, half a tile above
    // the corner's centre, so near the corner the two curves are equally near.
    const core::WorldMap map = RoadMap({ Staircase(), { { .column = 6, .row = 4 }, { .column = 6, .row = 7 } } });
    const core::RoadCurveIndex roads{ map };
    const std::vector<TilePoint> turning{ { .column = 4, .row = 3 },
                                          { .column = 5, .row = 3 },
                                          { .column = 6, .row = 4 },
                                          { .column = 6, .row = 5 },
                                          { .column = 6, .row = 6 } };
    const std::vector<TilePoint> leaving{ { .column = 4, .row = 3 },
                                          { .column = 5, .row = 3 },
                                          { .column = 6, .row = 4 },
                                          { .column = 7, .row = 5 },
                                          { .column = 8, .row = 6 } };
    for (const std::vector<TilePoint>* tiles : { &turning, &leaving }) {
        const core::Journey journey = Walk(map, *tiles);
        // The steps onto the fork's tile and away from it.
        CHECK(WorstExtraMove(map, roads, journey, ArriveTick(journey, 1), ArriveTick(journey, 3)) <= EASE_SLACK);
    }
}

TEST_CASE("a walker is drawn where it stands at its journey's ends, its party moved together") {
    const core::WorldMap map = RoadMap({ Staircase() });
    const core::RoadCurveIndex roads{ map };
    // From a foot off the centre of road tile (2, 1), as a cultivator's route sets out, to the corner tile (6, 4).
    const core::Vec2 foot{ .x = 600.0F, .y = 300.0F };
    const std::vector<TilePoint> tiles{ { .column = 2, .row = 1 },
                                        { .column = 3, .row = 2 },
                                        { .column = 4, .row = 3 },
                                        { .column = 5, .row = 3 },
                                        { .column = 6, .row = 4 } };
    const core::Journey journey = Walk(map, tiles, 3, foot);
    const i64 end = core::EndTick(journey);
    for (const i64 tick : { i64{ -5 }, i64{ 0 }, end, end + 5 }) {
        CAPTURE(tick);
        CHECK(SamePoint(core::DrawnWhere(map, roads, journey, tick), core::Where(map, journey, tick)));
    }
    // Mid-road the party keeps its places round its first member, all moved alike.
    const i64 middle = ArriveTick(journey, 2);
    const core::Vec2 shift = core::DrawnWhere(map, roads, journey, middle) - core::Where(map, journey, middle);
    REQUIRE(Distance(shift, {}) > 0.1F * TILE_SIZE);
    for (const std::size_t member : { std::size_t{ 1 }, std::size_t{ 2 } }) {
        CAPTURE(member);
        const core::Vec2 moved =
            core::DrawnWhere(map, roads, journey, middle, member) - core::Where(map, journey, middle, member);
        CHECK(Distance(moved, shift) < CLOSE);
    }
}

} // namespace
