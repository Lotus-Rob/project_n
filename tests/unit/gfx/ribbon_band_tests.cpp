#include "core/cxx23.hpp" // IWYU pragma: keep
#include "gfx/ribbon_band.hpp"

#include <doctest/doctest.h>

namespace {

constexpr f32 WIDTH = 20.0F;
constexpr f32 TOLERANCE = 0.01F;
constexpr u32 COLOUR = 0xFF'11'22'33;

/// A straight line of `points` points 64 units apart along x.
std::vector<core::Vec2> Straight(std::size_t points) {
    std::vector<core::Vec2> line;
    line.reserve(points);
    for (std::size_t point = 0; point < points; ++point) {
        line.push_back({ .x = static_cast<f32>(point) * 64.0F, .y = 0.0F });
    }
    return line;
}

std::vector<gfx::RibbonVertex> Band(const gfx::Laid& laid) {
    std::vector<gfx::RibbonVertex> vertices;
    gfx::AddBand(laid, WIDTH, COLOUR, vertices);
    return vertices;
}

/// How far apart the two corners at the near end (corners 0, 1) of quad `quad` stand.
f32 NearWidth(const std::vector<gfx::RibbonVertex>& vertices, std::size_t quad) {
    return std::abs(vertices[quad * gfx::QUAD_VERTICES].y - vertices[(quad * gfx::QUAD_VERTICES) + 1].y);
}

/// The same at the far end (corners 2, 3).
f32 FarWidth(const std::vector<gfx::RibbonVertex>& vertices, std::size_t quad) {
    return std::abs(vertices[(quad * gfx::QUAD_VERTICES) + 2].y - vertices[(quad * gfx::QUAD_VERTICES) + 3].y);
}

TEST_CASE("a band tapers from nothing at its start") {
    const std::vector<gfx::RibbonVertex> vertices = Band({ .line = Straight(9), .taper = 128.0F });
    CHECK(NearWidth(vertices, 0) < TOLERANCE);
    CHECK(std::abs(FarWidth(vertices, 0) - 10.0F) < TOLERANCE);
    CHECK(std::abs(FarWidth(vertices, 1) - WIDTH) < TOLERANCE);
    CHECK(std::abs(FarWidth(vertices, 7) - WIDTH) < TOLERANCE);
}

TEST_CASE("a band fades in from nothing at its start") {
    const std::vector<gfx::RibbonVertex> vertices = Band({ .line = Straight(9), .fadeIn = 128.0F });
    CHECK(vertices[0].colour == 0);
    CHECK(vertices[(gfx::QUAD_VERTICES) + 2].colour == COLOUR);
    CHECK(vertices[(gfx::QUAD_VERTICES) + 3].colour == COLOUR);
    CHECK(vertices[(7 * gfx::QUAD_VERTICES) + 2].colour == COLOUR);
}

TEST_CASE("a short band tapers over at most half its length") {
    const std::vector<gfx::RibbonVertex> vertices = Band({ .line = Straight(2), .taper = 256.0F });
    CHECK(std::abs(FarWidth(vertices, 0) - WIDTH) < TOLERANCE);
}

TEST_CASE("a short band fades in over at most half its length") {
    const std::vector<gfx::RibbonVertex> vertices = Band({ .line = Straight(2), .fadeIn = 256.0F });
    CHECK(vertices[2].colour == COLOUR);
}

TEST_CASE("start and end shaping multiply") {
    const std::vector<gfx::RibbonVertex> vertices =
        Band({ .line = Straight(9), .fade = 128.0F, .fadeIn = 128.0F, .taper = 128.0F });
    // Point 4 is the middle: the far end of quad 3.
    CHECK(vertices[(3 * gfx::QUAD_VERTICES) + 2].colour == COLOUR);
    CHECK(std::abs(FarWidth(vertices, 3) - WIDTH) < TOLERANCE);
    CHECK(vertices[0].colour == 0);
    CHECK(vertices[(7 * gfx::QUAD_VERTICES) + 2].colour == 0);
}

TEST_CASE("overlapping fade in and fade multiply their shares") {
    // On a line 256 long the fade-in of 256 is capped to 128: at 64 it is 0.5 and the fade's 192 of 256 left is 0.75, so
    // 0.375 (a minimum would give 0.5). Each channel of the colour is scaled and rounded: 255, 51, 34, 17 to 96, 19, 13, 6.
    const std::vector<gfx::RibbonVertex> vertices = Band({ .line = Straight(5), .fade = 256.0F, .fadeIn = 256.0F });
    CHECK(vertices[2].colour == 0x60'06'0D'13U);
}

TEST_CASE("an unshaped band is as before") {
    const std::vector<gfx::RibbonVertex> vertices = Band({ .line = Straight(9) });
    REQUIRE(vertices.size() == 8 * gfx::QUAD_VERTICES);
    for (const gfx::RibbonVertex& vertex : vertices) {
        CHECK(vertex.colour == COLOUR);
    }
    for (std::size_t quad = 0; quad < 8; ++quad) {
        CHECK(NearWidth(vertices, quad) == doctest::Approx(WIDTH));
        CHECK(FarWidth(vertices, quad) == doctest::Approx(WIDTH));
    }
}

} // namespace
