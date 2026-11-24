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
constexpr gfx::RibbonShape SHAPE{ .coreWidth = CORE, .edgeWidth = EDGE, .coreColour = CORE_COLOUR, .edgeColour = EDGE_COLOUR };

/// The kind index MapOfTiles() gives its lake tiles: water.
constexpr u8 LAKE = 1;
constexpr std::array<u8, 1> LAKE_WATER{ { LAKE } };

/// SHAPE with springs that widen over a tile.
constexpr gfx::RibbonShape TAPERED{ .coreWidth = CORE,
                                    .edgeWidth = EDGE,
                                    .coreColour = CORE_COLOUR,
                                    .edgeColour = EDGE_COLOUR,
                                    .taper = TILE };

std::vector<gfx::RibbonVertex> Draw(
    const core::WorldMap& map,
    std::span<const u8> water = {},
    const gfx::RibbonShape& shape = SHAPE
) {
    std::vector<gfx::RibbonVertex> vertices;
    gfx::DrawRivers(map, shape, water, vertices);
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
    CHECK(Distance(end, Middle(4, 4)) < CORE / 2.0F);
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

TEST_CASE("a tributary stopping short of the main river runs on to its middle") {
    // The main river runs from (0, 4) to (7, 3), so at the tributary's column 4 its middle lies 4/7 of a tile above row 4's
    // centre, and the tributary's last waypoint, (4, 3)'s centre, lies under half a tile short of it.
    const core::WorldMap map = test::MeadowMap(
        8,
        8,
        { { { .column = 0, .row = 4 }, { .column = 7, .row = 3 } }, { { .column = 4, .row = 0 }, { .column = 4, .row = 3 } } }
    );
    const std::vector<gfx::RibbonVertex> vertices = Draw(map);
    const core::Vec2 end = Midline(vertices, (vertices.size() / gfx::QUAD_VERTICES) - 1, true);
    const f32 mainMiddle = Middle(4, 4).y - (TILE * 4.0F / 7.0F);
    CHECK(end.x == doctest::Approx(Middle(4, 0).x));
    CHECK(std::abs(end.y - mainMiddle) < 1.0F);
}

TEST_CASE("a river a tributary joins a tile before its end still runs on into the water") {
    // A lake over columns 15 and 16; the tributary comes down column 13 and ends on the main river, which ends at (14, 5).
    constexpr u32 COLUMNS = 17;
    constexpr u32 ROWS = 8;
    std::array<u8, std::size_t{ COLUMNS } * ROWS> grid{};
    for (u32 row = 0; row < ROWS; ++row) {
        grid.at((row * COLUMNS) + 15) = LAKE;
        grid.at((row * COLUMNS) + 16) = LAKE;
    }
    const core::WorldMap map = test::MapOfTiles(
        COLUMNS,
        ROWS,
        grid,
        { { { .column = 13, .row = 0 }, { .column = 13, .row = 5 } },
          { { .column = 0, .row = 5 }, { .column = 14, .row = 5 } } }
    );
    const std::vector<gfx::RibbonVertex> vertices = Draw(map, LAKE_WATER);
    // The main river is listed last, so its core's last quad is the last of all.
    const core::Vec2 end = Midline(vertices, (vertices.size() / gfx::QUAD_VERTICES) - 1, true);
    CHECK(end.x == doctest::Approx(16.0F * TILE));
    CHECK(end.y == doctest::Approx(Middle(0, 5).y));
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

TEST_CASE("a river starting on open ground tapers in") {
    const core::WorldMap map = test::MeadowMap(8, 4, { { { .column = 1, .row = 2 }, { .column = 6, .row = 2 } } });
    CHECK(gfx::RiverStarts(map, {}) == std::vector{ gfx::RiverStart::Spring });
    const std::vector<gfx::RibbonVertex> vertices = Draw(map, {}, TAPERED);
    const std::size_t core = (vertices.size() / gfx::QUAD_VERTICES / 2) * gfx::QUAD_VERTICES;
    CHECK(Distance(Corner(vertices[core]), Corner(vertices[core + 1])) < 0.01F);
    CHECK(Distance(Corner(vertices[0]), Corner(vertices[1])) < 0.01F);
}

TEST_CASE("a river starting by a lake starts inside it, fading in") {
    constexpr u32 COLUMNS = 8;
    constexpr u32 ROWS = 3;
    std::array<u8, std::size_t{ COLUMNS } * ROWS> grid{};
    for (u32 row = 0; row < ROWS; ++row) {
        grid.at(std::size_t{ row } * COLUMNS) = LAKE;
        grid.at((row * COLUMNS) + 1) = LAKE;
    }
    const core::WorldMap map =
        test::MapOfTiles(COLUMNS, ROWS, grid, { { { .column = 3, .row = 1 }, { .column = 6, .row = 1 } } });
    CHECK(gfx::RiverStarts(map, LAKE_WATER) == std::vector{ gfx::RiverStart::Pond });
    const std::vector<gfx::RibbonVertex> vertices = Draw(map, LAKE_WATER, TAPERED);
    const std::size_t cores = vertices.size() / 2;
    CHECK(Midline(vertices, cores / gfx::QUAD_VERTICES, false).x == doctest::Approx(0.75F * TILE));
    CHECK(vertices[cores].colour == 0);
    // A pond's river isn't tapered: its core is as wide as the look's.
    CHECK(std::abs(Distance(Corner(vertices[cores]), Corner(vertices[cores + 1])) - CORE) < 0.01F);
}

TEST_CASE("a river starting in a lake keeps its start") {
    constexpr u32 COLUMNS = 8;
    constexpr u32 ROWS = 3;
    std::array<u8, std::size_t{ COLUMNS } * ROWS> grid{};
    for (u32 row = 0; row < ROWS; ++row) {
        grid.at(std::size_t{ row } * COLUMNS) = LAKE;
        grid.at((row * COLUMNS) + 1) = LAKE;
    }
    const core::WorldMap map =
        test::MapOfTiles(COLUMNS, ROWS, grid, { { { .column = 1, .row = 1 }, { .column = 6, .row = 1 } } });
    CHECK(gfx::RiverStarts(map, LAKE_WATER) == std::vector{ gfx::RiverStart::Pond });
    const std::vector<gfx::RibbonVertex> vertices = Draw(map, LAKE_WATER, TAPERED);
    CHECK(Distance(Midline(vertices, vertices.size() / gfx::QUAD_VERTICES / 2, false), Middle(1, 1)) < 1.0F);
    CHECK(vertices[vertices.size() / 2].colour == 0);
}

TEST_CASE("a branch's start is drawn as it was") {
    const core::WorldMap map = test::MeadowMap(
        10,
        6,
        { { { .column = 1, .row = 4 }, { .column = 9, .row = 4 } }, { { .column = 5, .row = 4 }, { .column = 5, .row = 1 } } }
    );
    CHECK(gfx::RiverStarts(map, {}) == std::vector{ gfx::RiverStart::Spring, gfx::RiverStart::Branch });
    const std::vector<gfx::RibbonVertex> vertices = Draw(map, {}, TAPERED);
    // The branch is listed last, so its core's first quad is the first after the main river's.
    const std::size_t quads = vertices.size() / gfx::QUAD_VERTICES;
    const std::size_t mainQuads = std::size_t{ 8 } * gfx::RIVER_STEPS;
    const std::size_t first = ((quads / 2) + mainQuads) * gfx::QUAD_VERTICES;
    CHECK(std::abs(Distance(Corner(vertices[first]), Corner(vertices[first + 1])) - CORE) < 0.01F);
}

TEST_CASE("a river starting where another river ends is a branch") {
    const core::WorldMap map = test::MeadowMap(
        10,
        6,
        { { { .column = 1, .row = 4 }, { .column = 5, .row = 4 } }, { { .column = 5, .row = 4 }, { .column = 5, .row = 1 } } }
    );
    CHECK(gfx::RiverStarts(map, {}) == std::vector{ gfx::RiverStart::Spring, gfx::RiverStart::Branch });
}

TEST_CASE("a branch's head keeps its own first point") {
    // The main river slopes, so its middle at the branch's column lies half a tile below the branch's first waypoint.
    const core::WorldMap map = test::MeadowMap(
        10,
        6,
        { { { .column = 1, .row = 4 }, { .column = 9, .row = 5 } }, { { .column = 5, .row = 4 }, { .column = 5, .row = 1 } } }
    );
    CHECK(gfx::RiverStarts(map, {}) == std::vector{ gfx::RiverStart::Spring, gfx::RiverStart::Branch });
    const std::vector<gfx::RibbonVertex> vertices = Draw(map, {}, TAPERED);
    const std::size_t mainQuads = std::size_t{ 9 } * gfx::RIVER_STEPS;
    const std::size_t branch = (vertices.size() / gfx::QUAD_VERTICES / 2) + mainQuads;
    CHECK(Distance(Midline(vertices, branch, false), Middle(5, 4)) < 1.0F);
}

TEST_CASE("a river looking back off the map's edge at a lake corner is a spring") {
    constexpr u32 COLUMNS = 4;
    constexpr u32 ROWS = 4;
    std::array<u8, std::size_t{ COLUMNS } * ROWS> grid{};
    grid.at(0) = LAKE;
    // Looking back from tile (0, 1) towards (1, 2) leaves the map within a tile, where the tile lookup would clamp onto the
    // lake at (0, 0).
    const core::WorldMap map =
        test::MapOfTiles(COLUMNS, ROWS, grid, { { { .column = 0, .row = 1 }, { .column = 1, .row = 2 } } });
    CHECK(gfx::RiverStarts(map, LAKE_WATER) == std::vector{ gfx::RiverStart::Spring });
}

TEST_CASE("a river's start at the map's edge water stays on the map") {
    constexpr u32 COLUMNS = 4;
    constexpr u32 ROWS = 4;
    std::array<u8, std::size_t{ COLUMNS } * ROWS> grid{};
    grid.at(0) = LAKE;
    grid.at(COLUMNS) = LAKE;
    // The water lies behind the head, against the edge: a tile past it would be off the map.
    const core::WorldMap map =
        test::MapOfTiles(COLUMNS, ROWS, grid, { { { .column = 1, .row = 1 }, { .column = 2, .row = 1 } } });
    const std::vector<gfx::RibbonVertex> vertices = Draw(map, LAKE_WATER);
    REQUIRE(!vertices.empty());
    f32 lowest = vertices.front().x;
    for (const gfx::RibbonVertex& vertex : vertices) {
        lowest = std::min(lowest, vertex.x);
    }
    CHECK(lowest >= 0.0F);
}

TEST_CASE("two sources side by side on dry ground stay springs") {
    // The second one's head looks back along its line at the first one's start, a tile away.
    const core::WorldMap map = test::MeadowMap(
        7,
        7,
        { { { .column = 1, .row = 1 }, { .column = 1, .row = 5 } }, { { .column = 2, .row = 1 }, { .column = 5, .row = 1 } } }
    );
    CHECK(gfx::RiverStarts(map, {}) == std::vector{ gfx::RiverStart::Spring, gfx::RiverStart::Spring });
}

/// The index of `map`'s kind named `name`.
u8 ShippedKind(const core::WorldMap& map, std::string_view name) {
    const std::span<const core::TerrainKind> kinds = map.Kinds();
    const auto found = std::ranges::find(kinds, name, &core::TerrainKind::name);
    REQUIRE_MESSAGE(found != kinds.end(), "the shipped world has no terrain kind named ", name);
    return static_cast<u8>(found - kinds.begin());
}

/// `map`'s water kinds, as the shipped look names them.
std::array<u8, 2> ShippedWater(const core::WorldMap& map) {
    return { { ShippedKind(map, "shallows"), ShippedKind(map, "deep_water") } };
}

TEST_CASE("the shipped rivers fit the river buffer") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    // With the shipped look's water kinds, so the rivers run on into the sea as they are drawn.
    const std::array<u8, 2> water = ShippedWater(*map);
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
    // And none was cut short: each segment's quads are there, but where a river's end moved back onto another river, which
    // costs it at most one tile's worth of quads in each band.
    std::size_t expected = 0;
    for (const std::vector<TilePoint>& river : map->Rivers()) {
        for (std::size_t point = 1; point < river.size(); ++point) {
            const f32 across = static_cast<f32>(river[point].column) - static_cast<f32>(river[point - 1].column);
            const f32 down = static_cast<f32>(river[point].row) - static_cast<f32>(river[point - 1].row);
            const std::size_t tiles = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(std::hypot(across, down))));
            expected += std::size_t{ 2 } * gfx::RIVER_STEPS * tiles;
        }
    }
    const std::size_t joinLoss = map->Rivers().size() * std::size_t{ 2 } * gfx::RIVER_STEPS;
    CHECK((vertices.size() / gfx::QUAD_VERTICES) + joinLoss >= expected);
}

TEST_CASE("every shipped river rises on high ground, out of water or from another river") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    const std::array<u8, 3> high{ { ShippedKind(*map, "hills"), ShippedKind(*map, "rock"), ShippedKind(*map, "snow") } };
    const std::vector<gfx::RiverStart> starts = gfx::RiverStarts(*map, ShippedWater(*map));
    REQUIRE(starts.size() == map->Rivers().size());
    std::string broken;
    for (std::size_t index = 0; index < starts.size(); ++index) {
        const TilePoint head = map->Rivers()[index].front();
        const bool onHigh = std::ranges::contains(high, map->KindAt(map->TileIndex(head)));
        if (starts[index] == gfx::RiverStart::Spring && !onHigh) {
            broken += std::format("river {} at [{}, {}]; ", index, head.column, head.row);
        }
    }
    CHECK_MESSAGE(broken.empty(), "springs on low ground: ", broken);
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
