#include "core/cxx23.hpp" // IWYU pragma: keep
#include "gfx/fog.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u32 SIDE = 4;
constexpr std::size_t VALUES = std::size_t{ SIDE } * SIDE * 2;
constexpr f32 TILE = 100.0F;
constexpr u8 FULL = 255;
constexpr u32 EXPLORED = 0;
constexpr u32 IN_SIGHT = 1;

// A 4 x 4 field: the left two columns explored, only the tile at column 2, row 1 in sight.
struct Field {
    std::array<u8, VALUES> values{};
    gfx::FogFrame frame{};

    Field() {
        for (u32 row = 0; row < SIDE; ++row) {
            for (u32 column = 0; column < SIDE; ++column) {
                const std::size_t tile = (row * SIDE) + column;
                values.at(tile * 2) = column < 2 ? FULL : 0;
                values.at((tile * 2) + 1) = (column == 2 && row == 1) ? FULL : 0;
            }
        }
        frame = { .columns = SIDE, .rows = SIDE, .tileSize = TILE, .values = values, .revision = 1 };
    }
};

core::Vec2 Centre(u32 column, u32 row) {
    return { .x = (static_cast<f32>(column) + 0.5F) * TILE, .y = (static_cast<f32>(row) + 0.5F) * TILE };
}

TEST_CASE("a fog sample at a tile's centre is its value") {
    const Field field;
    CHECK(gfx::FogSample(field.frame, Centre(0, 0), EXPLORED) == doctest::Approx(1.0F));
    CHECK(gfx::FogSample(field.frame, Centre(3, 3), EXPLORED) == doctest::Approx(0.0F));
    CHECK(gfx::FogSample(field.frame, Centre(2, 1), IN_SIGHT) == doctest::Approx(1.0F));
    CHECK(gfx::FogSample(field.frame, Centre(1, 1), IN_SIGHT) == doctest::Approx(0.0F));
}

TEST_CASE("a fog sample on the edge between an explored and an unexplored tile is half") {
    const Field field;
    CHECK(gfx::FogSample(field.frame, { .x = 2.0F * TILE, .y = Centre(0, 2).y }, EXPLORED) == doctest::Approx(0.5F));
}

TEST_CASE("a fog sample past the map's edge holds the edge tile's value") {
    const Field field;
    CHECK(gfx::FogSample(field.frame, { .x = -50.0F, .y = -50.0F }, EXPLORED) == doctest::Approx(1.0F));
    CHECK(gfx::FogSample(field.frame, { .x = 900.0F, .y = 900.0F }, EXPLORED) == doctest::Approx(0.0F));
}

TEST_CASE("sight fades from full half a tile inside its edge to nothing at the edge") {
    const Field field;
    const f32 rowY = Centre(2, 1).y;
    CHECK(gfx::SightFade(field.frame, { .x = Centre(2, 1).x, .y = rowY }) == doctest::Approx(1.0F));
    const core::Vec2 quarterIn{ .x = (2.0F * TILE) + (0.25F * TILE), .y = rowY };
    CHECK(gfx::FogSample(field.frame, quarterIn, IN_SIGHT) == doctest::Approx(0.75F));
    CHECK(gfx::SightFade(field.frame, quarterIn) == doctest::Approx(0.5F));
    CHECK(gfx::SightFade(field.frame, { .x = 2.0F * TILE, .y = rowY }) == doctest::Approx(0.0F));
}

TEST_CASE("a place's tile fades by its own value, whatever its neighbours") {
    const Field field;
    // Column 1 is explored, column 2 not: a point at column 1's edge samples half, but its tile is 1.
    const core::Vec2 nearEdge{ .x = (2.0F * TILE) - 1.0F, .y = Centre(0, 0).y };
    CHECK(gfx::FogSample(field.frame, nearEdge, EXPLORED) < 0.6F);
    CHECK(gfx::TileExplored(field.frame, nearEdge) == doctest::Approx(1.0F));
    CHECK(gfx::TileExplored(field.frame, Centre(2, 0)) == doctest::Approx(0.0F));
    CHECK(gfx::TileExplored(field.frame, { .x = -5.0F, .y = 10.0F }) == doctest::Approx(1.0F));
}

TEST_CASE("no fog fades and shades nothing") {
    const gfx::FogFrame none{};
    CHECK(gfx::FogSample(none, Centre(1, 1), EXPLORED) == doctest::Approx(1.0F));
    CHECK(gfx::SightFade(none, Centre(1, 1)) == doctest::Approx(1.0F));
    CHECK(gfx::TileExplored(none, Centre(1, 1)) == doctest::Approx(1.0F));
}

} // namespace
