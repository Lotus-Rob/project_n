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
    CHECK(NearWidth(vertices, 0) == doctest::Approx(0.0F).epsilon(TOLERANCE));
    CHECK(FarWidth(vertices, 0) == doctest::Approx(10.0F).epsilon(TOLERANCE));
    CHECK(FarWidth(vertices, 1) == doctest::Approx(WIDTH).epsilon(TOLERANCE));
    CHECK(FarWidth(vertices, 7) == doctest::Approx(WIDTH).epsilon(TOLERANCE));
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
    CHECK(FarWidth(vertices, 0) == doctest::Approx(WIDTH).epsilon(TOLERANCE));
}

TEST_CASE("start and end shaping multiply") {
    const std::vector<gfx::RibbonVertex> vertices =
        Band({ .line = Straight(9), .fade = 128.0F, .fadeIn = 128.0F, .taper = 128.0F });
    // Point 4 is the middle: the far end of quad 3.
    CHECK(vertices[(3 * gfx::QUAD_VERTICES) + 2].colour == COLOUR);
    CHECK(FarWidth(vertices, 3) == doctest::Approx(WIDTH).epsilon(TOLERANCE));
    CHECK(vertices[0].colour == 0);
    CHECK(vertices[(7 * gfx::QUAD_VERTICES) + 2].colour == 0);
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
