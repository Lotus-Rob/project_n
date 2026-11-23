#include "core/cxx23.hpp" // IWYU pragma: keep
#include "gfx/road_draw.hpp"

#include <doctest/doctest.h>

namespace {

constexpr f32 CORE = 6.0F;
constexpr f32 EDGE = 10.0F;
constexpr u32 CORE_COLOUR = 0xFF'11'22'33;
constexpr u32 EDGE_COLOUR = 0xFF'44'55'66;
constexpr gfx::RibbonShape SHAPE{ .coreWidth = CORE, .edgeWidth = EDGE, .coreColour = CORE_COLOUR, .edgeColour = EDGE_COLOUR };

/// A straight curve of `steps` steps along y = `row`, 10 units each.
std::vector<core::Vec2> Straight(std::size_t steps, f32 row) {
    std::vector<core::Vec2> curve;
    for (std::size_t step = 0; step <= steps; ++step) {
        curve.push_back({ .x = static_cast<f32>(step) * 10.0F, .y = row });
    }
    return curve;
}

TEST_CASE("roads lay edge bands before core bands, whole quads in corner order") {
    const std::vector<std::vector<core::Vec2>> curves{ Straight(3, 0.0F), Straight(2, 50.0F) };
    std::vector<gfx::RibbonVertex> vertices;
    gfx::DrawRoads(curves, SHAPE, vertices);
    const std::size_t quads = std::size_t{ 2 } * (3 + 2);
    REQUIRE(vertices.size() == quads * gfx::QUAD_VERTICES);
    const std::size_t half = vertices.size() / 2;
    CHECK(vertices.front().colour == EDGE_COLOUR);
    CHECK(vertices[half - 1].colour == EDGE_COLOUR);
    CHECK(vertices[half].colour == CORE_COLOUR);
    CHECK(vertices.back().colour == CORE_COLOUR);
    // The first edge quad: corners 0 and 1 at its near end (x 0), 2 and 3 at its far end (x 10), left side first.
    CHECK(vertices[0].x == doctest::Approx(0.0F));
    CHECK(vertices[2].x == doctest::Approx(10.0F));
    CHECK(vertices[0].y == doctest::Approx(-vertices[1].y));
    CHECK(std::abs(vertices[0].y - vertices[1].y) == doctest::Approx(EDGE));
    CHECK(std::abs(vertices[half].y - vertices[half + 1].y) == doctest::Approx(CORE));
}

TEST_CASE("a curve of fewer than two points draws nothing") {
    const std::vector<std::vector<core::Vec2>> curves{ {}, { { .x = 1.0F, .y = 1.0F } } };
    std::vector<gfx::RibbonVertex> vertices(5);
    gfx::DrawRoads(curves, SHAPE, vertices);
    CHECK(vertices.empty());
}

TEST_CASE("roads past MAX_ROAD_QUADS are left out whole, the later first") {
    // Each road takes 2 * 2000 quads, so two fit in 8192 and the third doesn't; a short fourth is left out too, being later.
    const std::vector<std::vector<core::Vec2>> curves{
        Straight(2000, 0.0F), Straight(2000, 50.0F), Straight(2000, 100.0F), Straight(1, 150.0F)
    };
    std::vector<gfx::RibbonVertex> vertices;
    gfx::DrawRoads(curves, SHAPE, vertices);
    CHECK(vertices.size() == std::size_t{ 2 } * 2 * 2000 * gfx::QUAD_VERTICES);
    f32 highest = 0.0F;
    for (const gfx::RibbonVertex& vertex : vertices) {
        highest = std::max(highest, vertex.y);
    }
    CHECK(highest < 60.0F);
}

} // namespace
