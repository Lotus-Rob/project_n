#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/world_look.hpp"
#include "gfx/fog.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u32 COLUMNS = 6;
constexpr u32 ROWS = 4;
constexpr f32 TILE = 256.0F;
/// Rec. 709's weights of linear red, green and blue in luminance, as the sprite shader drains by them.
constexpr std::array<f32, 3> LUMINANCE{ 0.2126F, 0.7152F, 0.0722F };

/// The shipped look's fog colours and strengths.
core::FogLook Look() {
    core::FogLook look;
    look.drain = 0.85F;
    look.sepia = { 160, 138, 106 };
    look.mist = { 239, 233, 220 };
    look.mistStrength = 0.25F;
    look.shade = { 214, 206, 192 };
    return look;
}

/// A fog over a 6 x 4 map of 256-unit tiles; its values aren't read.
gfx::FogFrame Fog(f32 tileSize) {
    return { .columns = COLUMNS, .rows = ROWS, .tileSize = tileSize, .revision = 1 };
}

TEST_CASE("fog uniforms are off without tiles to shade") {
    CHECK(gfx::FogUniformsOf(gfx::FogFrame{}, Look()).on == 0);
    CHECK(gfx::FogUniformsOf(Fog(0.0F), Look()).on == 0);
}

TEST_CASE("fog uniforms carry the map and the look's strengths") {
    const gfx::FogUniforms uniforms = gfx::FogUniformsOf(Fog(TILE), Look());
    CHECK(uniforms.on == 1);
    CHECK(uniforms.mapSize == std::array<f32, 2>{ 6.0F, 4.0F });
    CHECK(uniforms.tileSize == TILE);
    CHECK(uniforms.sepia[3] == doctest::Approx(0.85F));
    CHECK(uniforms.mist[3] == doctest::Approx(0.25F));
}

TEST_CASE("the sepia tint keeps a drained colour's luminance") {
    const gfx::FogUniforms uniforms = gfx::FogUniformsOf(Fog(TILE), Look());
    const f32 luminance =
        (uniforms.sepia[0] * LUMINANCE[0]) + (uniforms.sepia[1] * LUMINANCE[1]) + (uniforms.sepia[2] * LUMINANCE[2]);
    CHECK(luminance == doctest::Approx(1.0F));
}

TEST_CASE("fog colours reach the shader linear") {
    const gfx::FogUniforms uniforms = gfx::FogUniformsOf(Fog(TILE), Look());
    // sRGB 214 is linear 0.672, held as the byte 171; 239 is 0.863, the byte 220.
    CHECK(uniforms.shade[0] == doctest::Approx(171.0F / 255.0F));
    CHECK(uniforms.mist[0] == doctest::Approx(220.0F / 255.0F));
}

} // namespace
