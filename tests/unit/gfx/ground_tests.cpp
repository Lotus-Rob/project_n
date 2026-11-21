#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/manifest.hpp"
#include "core/rect.hpp"
#include "core/ui_colours.hpp"
#include "core/world_look.hpp"
#include "core/world_map.hpp"
#include "data/world_look.hpp"
#include "gfx/ground.hpp"
#include "platform/paths.hpp"
#include "support/manifest.hpp"
#include "support/meadow.hpp"
#include "support/shipped_world.hpp"

#include <doctest/doctest.h>

namespace {

constexpr f32 TILE = 256.0F;
/// The shipped map's kind indices the cases read.
constexpr std::size_t DEEP_WATER = 0;
constexpr std::size_t SHALLOWS = 1;
constexpr std::size_t MEADOW = 4;
constexpr std::size_t PEAKS = 10;
/// How many kinds the shipped map has.
constexpr std::size_t SHIPPED_KINDS = 11;

/// The shipped map's look, as the game reads it.
core::WorldLook ShippedLook(const core::WorldMap& map) {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    std::expected<core::WorldLook, core::Error> look =
        platform::AssetsDirectory().and_then([&](const std::filesystem::path& assets) {
            return data::LoadWorldLook(assets / "data", *manifest, map);
        });
    REQUIRE(look.has_value());
    return *std::move(look);
}

/// The rectangle from (`left`, `top`) to (`right`, `bottom`) in world units.
core::Rect RectOf(f32 left, f32 top, f32 right, f32 bottom) {
    return { .min = { .x = left, .y = top }, .max = { .x = right, .y = bottom } };
}

/// `rect`'s left, top, right and bottom, to compare as one value.
std::array<f32, 4> EdgesOf(const core::Rect& rect) {
    return { { rect.min.x, rect.min.y, rect.max.x, rect.max.y } };
}

/// The edges of the ground quad for `view` over `map`, or all zero when there is none (no case expects a quad at the
/// origin of no size).
std::array<f32, 4> QuadEdges(core::Rect view, const core::WorldMap& map) {
    return EdgesOf(gfx::GroundQuad(view, map).value_or(core::Rect{}));
}

TEST_CASE("the ground's uniforms carry each kind's linear wash and strength") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    const gfx::GroundUniforms uniforms = gfx::GroundUniformsOf(*map, ShippedLook(*map), gfx::GROUND_MODE_VEIL);
    CHECK(uniforms.mode == gfx::GROUND_MODE_VEIL);
    CHECK(uniforms.mapSize == std::array<f32, 2>{ { static_cast<f32>(map->Columns()), static_cast<f32>(map->Rows()) } });
    CHECK(uniforms.tileSize == TILE);
    CHECK(uniforms.washes.at(MEADOW)[3] == 0.0F);
    CHECK(uniforms.washes.at(PEAKS)[3] == doctest::Approx(0.28F));
    // Peaks' wash is sRGB (88, 132, 126): the linear bytes 25, 59 and 53.
    CHECK(uniforms.washes.at(PEAKS)[0] == doctest::Approx(25.0F / 255.0F));
    CHECK(uniforms.washes.at(PEAKS)[1] == doctest::Approx(59.0F / 255.0F));
    CHECK(uniforms.washes.at(PEAKS)[2] == doctest::Approx(53.0F / 255.0F));
    CHECK(uniforms.deepKind == DEEP_WATER);
    CHECK(uniforms.shallowKind == SHALLOWS);
    // Past the map's kinds the table holds nothing.
    CHECK(uniforms.washes.at(SHIPPED_KINDS) == std::array<f32, 4>{});
    CHECK(uniforms.washes.back() == std::array<f32, 4>{});
}

TEST_CASE("the ground's uniforms are 16-byte rows the size the shader reads") {
    // Eight rows of values, then a row for each kind's wash.
    CHECK(sizeof(gfx::GroundUniforms) == (8 + core::MAX_GROUND_KINDS) * 16);
    CHECK(offsetof(gfx::GroundUniforms, paper) == 16);
    CHECK(offsetof(gfx::GroundUniforms, washes) == 8 * 16);
}

TEST_CASE("the ground quad is the view's overlap with the map") {
    // 6 by 4 tiles of 256 units: the map spans (0, 0) to (1536, 1024).
    const core::WorldMap map = test::MeadowMap(6, 4);
    CHECK(QuadEdges(RectOf(100.0F, 100.0F, 500.0F, 400.0F), map) == EdgesOf(RectOf(100.0F, 100.0F, 500.0F, 400.0F)));
    CHECK(QuadEdges(RectOf(-300.0F, 200.0F, 700.0F, 600.0F), map) == EdgesOf(RectOf(0.0F, 200.0F, 700.0F, 600.0F)));
    CHECK(QuadEdges(RectOf(1200.0F, 900.0F, 2000.0F, 1500.0F), map) == EdgesOf(RectOf(1200.0F, 900.0F, 1536.0F, 1024.0F)));
    CHECK(QuadEdges(RectOf(-500.0F, -500.0F, 3000.0F, 3000.0F), map) == EdgesOf(RectOf(0.0F, 0.0F, 1536.0F, 1024.0F)));
    CHECK_FALSE(gfx::GroundQuad(RectOf(-900.0F, 0.0F, -100.0F, 500.0F), map).has_value());
    CHECK_FALSE(gfx::GroundQuad(RectOf(200.0F, 1100.0F, 900.0F, 1600.0F), map).has_value());
    // Touching the map's edge covers none of it.
    CHECK_FALSE(gfx::GroundQuad(RectOf(1536.0F, 0.0F, 2000.0F, 500.0F), map).has_value());
}

TEST_CASE("a river's shape is its look in world units") {
    const core::WorldMap map = test::MeadowMap(2, 2);
    const core::RiverLook look{
        .width = 0.38F, .edge = 0.5F, .colour = { { 128, 172, 176 } }, .edgeColour = { { 70, 104, 112 } }, .edgeStrength = 0.67F
    };
    const gfx::RiverShape shape = gfx::RiverShapeOf(map, look);
    CHECK(shape.coreWidth == doctest::Approx(0.38F * TILE));
    CHECK(shape.edgeWidth == doctest::Approx(0.5F * TILE));
    CHECK(shape.coreColour == core::FromSrgb(128, 172, 176, 255));
    // 0.67 of 255 rounds to 171, and the colour is premultiplied by it.
    CHECK(shape.edgeColour == core::FromSrgb(70, 104, 112, 171));
    CHECK((shape.edgeColour >> 24U) == 171);
}

} // namespace
