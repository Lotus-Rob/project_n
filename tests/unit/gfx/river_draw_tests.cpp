#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/world_map.hpp"
#include "gfx/river_draw.hpp"
#include "support/meadow.hpp"
#include "support/shipped_world.hpp"

#include <doctest/doctest.h>

namespace {

using core::TilePoint;

constexpr f32 TILE = 256.0F;
constexpr f32 CORE = 12.0F;
constexpr f32 EDGE = 20.0F;
constexpr u32 CORE_COLOUR = 0xFF'11'22'33;
constexpr u32 EDGE_COLOUR = 0xFF'44'55'66;
constexpr gfx::RiverShape SHAPE{ .coreWidth = CORE, .edgeWidth = EDGE, .coreColour = CORE_COLOUR, .edgeColour = EDGE_COLOUR };

/// The kind index MapOfTiles() gives its lake tiles: water.
constexpr u8 LAKE = 1;
constexpr std::array<u8, 1> LAKE_WATER{ { LAKE } };

std::vector<gfx::RibbonVertex> Draw(const core::WorldMap& map, std::span<const u8> water = {}) {
    std::vector<gfx::RibbonVertex> vertices;
    gfx::DrawRivers(map, SHAPE, water, vertices);
    return vertices;
}

core::Vec2 Middle(u32 column, u32 row) {
    return { .x = (static_cast<f32>(column) + 0.5F) * TILE, .y = (static_cast<f32>(row) + 0.5F) * TILE };
}

core::Vec2 Corner(const gfx::RibbonVertex& vertex) {
    return { .x = vertex.x, .y = vertex.y };
}

f32 Distance(core::Vec2 from, core::Vec2 target) {
    return std::hypot(target.x - from.x, target.y - from.y);
}

// The midline point a quad's near edge (`far` false) or far edge lies on: halfway between its two corners.
core::Vec2 Midline(const std::vector<gfx::RibbonVertex>& vertices, std::size_t quad, bool far) {
    const std::size_t first = (quad * gfx::QUAD_VERTICES) + (far ? 2 : 0);
    const core::Vec2 left = Corner(vertices[first]);
    const core::Vec2 right = Corner(vertices[first + 1]);
    return { .x = (left.x + right.x) / 2.0F, .y = (left.y + right.y) / 2.0F };
}

std::vector<TilePoint> Bend() {
    return { { .column = 1, .row = 1 }, { .column = 5, .row = 1 }, { .column = 5, .row = 5 } };
}

TEST_CASE("a straight river's ribbon has the core's width") {
    const core::WorldMap map = test::MeadowMap(8, 4, { { { .column = 1, .row = 2 }, { .column = 6, .row = 2 } } });
    const std::vector<gfx::RibbonVertex> vertices = Draw(map);
    const std::size_t quads = vertices.size() / gfx::QUAD_VERTICES;
    REQUIRE(quads == 2 * 5 * gfx::RIVER_STEPS);
    f32 worst = 0.0F;
    for (std::size_t quad = quads / 2; quad < quads; ++quad) {
        for (std::size_t edge = 0; edge < 2; ++edge) {
            const std::size_t first = (quad * std::size_t{ gfx::QUAD_VERTICES }) + (edge * 2);
            worst = std::max(worst, std::abs(Distance(Corner(vertices[first]), Corner(vertices[first + 1])) - CORE));
        }
    }
    CHECK(worst < 0.01F);
}

TEST_CASE("a ribbon passes through each tile centre") {
    const core::WorldMap map = test::MeadowMap(8, 8, { Bend() });
    const std::vector<gfx::RibbonVertex> vertices = Draw(map);
    const std::size_t core = vertices.size() / gfx::QUAD_VERTICES / 2;
    const std::size_t firstSegment = std::size_t{ 4 } * gfx::RIVER_STEPS;
    CHECK(Distance(Midline(vertices, core, false), Middle(1, 1)) < 1.0F);
    CHECK(Distance(Midline(vertices, core + firstSegment - 1, true), Middle(5, 1)) < 1.0F);
    CHECK(Distance(Midline(vertices, (2 * core) - 1, true), Middle(5, 5)) < 1.0F);
}

TEST_CASE("the ends arrive along the end segments") {
    const core::WorldMap map = test::MeadowMap(8, 8, { Bend() });
    const std::vector<gfx::RibbonVertex> vertices = Draw(map);
    const std::size_t core = vertices.size() / gfx::QUAD_VERTICES / 2;
    const core::Vec2 start = Midline(vertices, core, false);
    const core::Vec2 second = Midline(vertices, core, true);
    CHECK(std::abs(second.y - start.y) < 0.05F * std::abs(second.x - start.x));
    const core::Vec2 before = Midline(vertices, (2 * core) - 1, false);
    const core::Vec2 end = Midline(vertices, (2 * core) - 1, true);
    CHECK(std::abs(end.x - before.x) < 0.05F * std::abs(end.y - before.y));
}

TEST_CASE("edges come before cores") {
    const core::WorldMap map = test::MeadowMap(
        8,
        8,
        { { { .column = 1, .row = 1 }, { .column = 5, .row = 1 } }, { { .column = 1, .row = 4 }, { .column = 5, .row = 4 } } }
    );
    const std::vector<gfx::RibbonVertex> vertices = Draw(map);
    const std::size_t half = vertices.size() / 2;
    CHECK(vertices.size() % gfx::QUAD_VERTICES == 0);
    CHECK(vertices[0].colour == EDGE_COLOUR);
    CHECK(vertices[half - 1].colour == EDGE_COLOUR);
    CHECK(vertices[half].colour == CORE_COLOUR);
    CHECK(vertices.back().colour == CORE_COLOUR);
}

TEST_CASE("a tributary's end lies inside the main river's core") {
    // The main river has no waypoint at the tributary's end tile (4, 4): it crosses it between its two.
    const core::WorldMap map = test::MeadowMap(
        8,
        8,
        { { { .column = 0, .row = 4 }, { .column = 7, .row = 4 } }, { { .column = 4, .row = 0 }, { .column = 4, .row = 4 } } }
    );
    const std::vector<gfx::RibbonVertex> vertices = Draw(map);
    const core::Vec2 end = Midline(vertices, (vertices.size() / gfx::QUAD_VERTICES) - 1, true);
    CHECK(std::abs(end.y - Middle(0, 4).y) < CORE / 2.0F);
    CHECK(end.x > Middle(0, 4).x);
    CHECK(end.x < Middle(7, 4).x);
}

TEST_CASE("a tributary ending past the main river ends on the main river's middle") {
    // The main river runs from (0, 4) to (7, 5), so at the tributary's column 4 its middle lies 4/7 of a tile below row 4's
    // centre, and the tributary's last waypoint, (4, 5)'s centre, lies under half a tile past it.
    const core::WorldMap map = test::MeadowMap(
        8,
        8,
        { { { .column = 0, .row = 4 }, { .column = 7, .row = 5 } }, { { .column = 4, .row = 0 }, { .column = 4, .row = 5 } } }
    );
    const std::vector<gfx::RibbonVertex> vertices = Draw(map);
    const core::Vec2 end = Midline(vertices, (vertices.size() / gfx::QUAD_VERTICES) - 1, true);
    const f32 mainMiddle = Middle(4, 4).y + (TILE * 4.0F / 7.0F);
    CHECK(end.x == doctest::Approx(Middle(4, 0).x));
    CHECK(std::abs(end.y - mainMiddle) < 1.0F);
}

TEST_CASE("a river ending two tiles from another keeps its end") {
    const core::WorldMap map = test::MeadowMap(
        8,
        8,
        { { { .column = 0, .row = 4 }, { .column = 7, .row = 4 } }, { { .column = 4, .row = 0 }, { .column = 4, .row = 2 } } }
    );
    const std::vector<gfx::RibbonVertex> vertices = Draw(map);
    const core::Vec2 end = Midline(vertices, (vertices.size() / gfx::QUAD_VERTICES) - 1, true);
    CHECK(Distance(end, Middle(4, 2)) < 1.0F);
}

TEST_CASE("a river ending short of water runs on a tile into it, fading out") {
    // Meadow, with a lake over columns 6 and 7; the river stops at column 4, so water lies a tile and a half ahead of its end.
    constexpr u32 COLUMNS = 8;
    constexpr u32 ROWS = 3;
    std::array<u8, std::size_t{ COLUMNS } * ROWS> grid{};
    for (u32 row = 0; row < ROWS; ++row) {
        grid.at((row * COLUMNS) + 6) = LAKE;
        grid.at((row * COLUMNS) + 7) = LAKE;
    }
    const core::WorldMap map =
        test::MapOfTiles(COLUMNS, ROWS, grid, { { { .column = 1, .row = 1 }, { .column = 4, .row = 1 } } });
    const std::vector<gfx::RibbonVertex> mouth = Draw(map, LAKE_WATER);
    const core::Vec2 end = Midline(mouth, (mouth.size() / gfx::QUAD_VERTICES) - 1, true);
    CHECK(end.x == doctest::Approx(7.0F * TILE));
    CHECK(end.y == doctest::Approx(Middle(0, 1).y));
    // Whole up to the water, nothing at the end: its first core corner and its last.
    const std::size_t cores = mouth.size() / 2;
    CHECK(mouth[cores].colour == CORE_COLOUR);
    CHECK(mouth.back().colour == 0);
    CHECK(mouth[cores - 1].colour == 0);
    // Without water kinds the river ends where its waypoints do.
    const std::vector<gfx::RibbonVertex> dry = Draw(map);
    CHECK(Distance(Midline(dry, (dry.size() / gfx::QUAD_VERTICES) - 1, true), Middle(4, 1)) < 1.0F);
}

TEST_CASE("a river ending more than three tiles short of water keeps its end") {
    constexpr u32 COLUMNS = 8;
    constexpr u32 ROWS = 3;
    std::array<u8, std::size_t{ COLUMNS } * ROWS> grid{};
    for (u32 row = 0; row < ROWS; ++row) {
        grid.at((row * COLUMNS) + 7) = LAKE;
    }
    const core::WorldMap map =
        test::MapOfTiles(COLUMNS, ROWS, grid, { { { .column = 1, .row = 1 }, { .column = 3, .row = 1 } } });
    const std::vector<gfx::RibbonVertex> vertices = Draw(map, LAKE_WATER);
    CHECK(Distance(Midline(vertices, (vertices.size() / gfx::QUAD_VERTICES) - 1, true), Middle(3, 1)) < 1.0F);
}

TEST_CASE("the shipped rivers fit the river buffer") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    // With the shipped look's water kinds, so the rivers run on into the sea as they are drawn.
    const std::span<const core::TerrainKind> kinds = map->Kinds();
    const auto indexOf = [&](std::string_view name) {
        return static_cast<u8>(std::ranges::find(kinds, name, &core::TerrainKind::name) - kinds.begin());
    };
    const std::array<u8, 2> water{ { indexOf("shallows"), indexOf("deep_water") } };
    const std::vector<gfx::RibbonVertex> vertices = Draw(*map, water);
    bool finite = true;
    for (const gfx::RibbonVertex& vertex : vertices) {
        finite = finite && std::isfinite(vertex.x) && std::isfinite(vertex.y);
    }
    CHECK(finite);
    CHECK(vertices.size() > 0);
    CHECK(vertices.size() <= std::size_t{ gfx::MAX_RIVER_QUADS } * gfx::QUAD_VERTICES);
    // Nothing was left out: each river's two bands are there, each starting with a quad at `u` 0.
    std::size_t bands = 0;
    for (std::size_t quad = 0; quad < vertices.size() / gfx::QUAD_VERTICES; ++quad) {
        bands += (vertices[quad * gfx::QUAD_VERTICES].uv & 0xFFFFU) == 0 ? 1 : 0;
    }
    CHECK(bands == 2 * map->Rivers().size());
}

TEST_CASE("rivers past the buffer are left out whole, the later ones first") {
    constexpr u32 LENGTH = 255;
    constexpr u32 QUADS_PER_RIVER = 2 * LENGTH * gfx::RIVER_STEPS;
    constexpr u32 FIT = gfx::MAX_RIVER_QUADS / QUADS_PER_RIVER;
    static_assert(FIT >= 1, "a river this long must fit the buffer at least once");
    const std::vector<TilePoint> river{ { .column = 0, .row = 1 }, { .column = LENGTH, .row = 1 } };
    const core::WorldMap map = test::MeadowMap(LENGTH + 1, 3, std::vector(FIT + 1, river));
    const std::size_t quads = Draw(map).size() / gfx::QUAD_VERTICES;
    CHECK(quads == std::size_t{ FIT } * QUADS_PER_RIVER);
}

TEST_CASE("a one-point river draws nothing") {
    const core::WorldMap map = test::MeadowMap(4, 4, { { { .column = 2, .row = 2 } } });
    CHECK(Draw(map).empty());
}

TEST_CASE("repeated points make the same river as each point once") {
    const core::WorldMap once = test::MeadowMap(8, 4, { { { .column = 1, .row = 2 }, { .column = 4, .row = 2 } } });
    const core::WorldMap twice = test::MeadowMap(
        8, 4, { { { .column = 1, .row = 2 }, { .column = 1, .row = 2 }, { .column = 4, .row = 2 }, { .column = 4, .row = 2 } } }
    );
    const std::vector<gfx::RibbonVertex> expected = Draw(once);
    const std::vector<gfx::RibbonVertex> got = Draw(twice);
    REQUIRE(got.size() == expected.size());
    f32 worst = 0.0F;
    for (std::size_t vertex = 0; vertex < got.size(); ++vertex) {
        worst = std::max(worst, Distance(Corner(got[vertex]), Corner(expected[vertex])));
    }
    CHECK(worst < 0.01F);
}

} // namespace
