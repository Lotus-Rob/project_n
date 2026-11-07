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

constexpr f32 LARGE_TURN = 1000.0F;

TEST_CASE("turning the wheel forward zooms in") {
    CHECK(gfx::ZoomedBy(1.0F, 1.0F) > 1.0F);
}

TEST_CASE("turning the wheel back by the same notches returns to the zoom before") {
    CHECK(gfx::ZoomedBy(gfx::ZoomedBy(1.0F, 1.0F), -1.0F) == doctest::Approx(1.0F));
}

TEST_CASE("no turn keeps the zoom") {
    CHECK(gfx::ZoomedBy(1.0F, 0.0F) == doctest::Approx(1.0F));
}

TEST_CASE("the zoom stops at MAX_ZOOM") {
    CHECK(gfx::ZoomedBy(1.0F, LARGE_TURN) == doctest::Approx(gfx::MAX_ZOOM));
}

TEST_CASE("the zoom stops at MIN_ZOOM") {
    CHECK(gfx::ZoomedBy(1.0F, -LARGE_TURN) == doctest::Approx(gfx::MIN_ZOOM));
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

TEST_CASE("ClipToWorld inverts WorldToClip at several sizes and zooms") {
    struct Case {
        u32 width = 0;
        u32 height = 0;
        f32 zoom = 1.0F;
    };
    constexpr std::array CASES{ Case{ .width = 1920, .height = 1080, .zoom = 1.0F },
                                Case{ .width = 1280, .height = 1024, .zoom = gfx::MIN_ZOOM },
                                Case{ .width = 3440, .height = 1440, .zoom = gfx::MAX_ZOOM } };
    constexpr core::Vec2 CENTRE{ .x = 300.0F, .y = -120.0F };
    constexpr core::Vec2 POINT{ .x = 410.0F, .y = 75.0F };
    for (const Case& size : CASES) {
        const gfx::ClipTransform transform = gfx::WorldToClip({ .centre = CENTRE, .zoom = size.zoom }, size.width, size.height);
        const Clip clip = Apply(transform, POINT);
        const core::Vec2 world = gfx::ClipToWorld(transform, { .x = clip.x, .y = clip.y });
        CHECK(world.x == doctest::Approx(POINT.x));
        CHECK(world.y == doctest::Approx(POINT.y));
    }
}

} // namespace
