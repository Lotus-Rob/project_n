#include "core/cxx23.hpp" // IWYU pragma: keep
#include "gfx/camera.hpp"

#include <doctest/doctest.h>

namespace {

struct Clip {
    f32 x = 0.0F;
    f32 y = 0.0F;
};

Clip Apply(const gfx::ClipTransform& transform, core::Vec2 world) {
    return { .x = (world.x * transform.scaleX) + transform.offsetX, .y = (world.y * transform.scaleY) + transform.offsetY };
}

TEST_CASE("at the reference size, the reference area fills the target") {
    const gfx::ClipTransform transform = gfx::WorldToClip({}, 1920, 1080);
    CHECK(Apply(transform, { .x = 0.0F, .y = 0.0F }).x == doctest::Approx(-1.0F));
    CHECK(Apply(transform, { .x = 0.0F, .y = 0.0F }).y == doctest::Approx(1.0F)); // world top is clip top
    CHECK(Apply(transform, gfx::REFERENCE_SIZE).x == doctest::Approx(1.0F));
    CHECK(Apply(transform, gfx::REFERENCE_SIZE).y == doctest::Approx(-1.0F));
}

TEST_CASE("a wider target shows more of the world sideways, at the same scale vertically") {
    constexpr u32 WIDE = 2560;
    const gfx::ClipTransform transform = gfx::WorldToClip({}, WIDE, 1080);
    CHECK(Apply(transform, { .x = 0.0F, .y = 0.0F }).y == doctest::Approx(1.0F));
    CHECK(Apply(transform, { .x = 0.0F, .y = 0.0F }).x == doctest::Approx(-gfx::REFERENCE_SIZE.x / WIDE));
    CHECK(Apply(transform, gfx::REFERENCE_CENTRE).x == doctest::Approx(0.0F));
}

TEST_CASE("a taller target shows more of the world vertically") {
    const gfx::ClipTransform transform = gfx::WorldToClip({}, 1920, 1920);
    CHECK(Apply(transform, { .x = 0.0F, .y = 0.0F }).x == doctest::Approx(-1.0F));
    CHECK(Apply(transform, { .x = 0.0F, .y = 0.0F }).y == doctest::Approx(gfx::REFERENCE_SIZE.y / gfx::REFERENCE_SIZE.x));
}

TEST_CASE("zooming in doubles the scale about the camera's centre") {
    constexpr core::Vec2 CENTRE{ .x = 100.0F, .y = 200.0F };
    constexpr f32 QUARTER_WIDTH = 480.0F; // at zoom 2, half the target is a quarter of the reference
    const gfx::ClipTransform transform = gfx::WorldToClip({ .centre = CENTRE, .zoom = 2.0F }, 1920, 1080);
    CHECK(Apply(transform, CENTRE).x == doctest::Approx(0.0F));
    CHECK(Apply(transform, CENTRE).y == doctest::Approx(0.0F));
    CHECK(Apply(transform, { .x = CENTRE.x + QUARTER_WIDTH, .y = CENTRE.y }).x == doctest::Approx(1.0F));
}

TEST_CASE("a zero-sized target (a minimised window) gives a finite transform") {
    const gfx::ClipTransform transform = gfx::WorldToClip({}, 0, 0);
    CHECK(std::isfinite(transform.scaleX));
    CHECK(std::isfinite(transform.scaleY));
}

} // namespace
