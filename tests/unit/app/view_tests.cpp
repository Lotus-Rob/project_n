#include "app/view.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"

#include <doctest/doctest.h>

namespace {

constexpr f32 QUAD = 100.0F; // every test quad is this wide and high

core::Instance Quad(core::Vec2 previous, core::Vec2 current) {
    return { .previous = previous, .current = current, .size = core::PackPair(core::PackHalf(QUAD), core::PackHalf(QUAD)) };
}

constexpr core::Vec2 ORIGIN{};
constexpr core::Vec2 MOVED{ .x = 200.0F, .y = 0.0F };
constexpr f64 HALFWAY = 0.5;
constexpr f32 HALF = 0.5F;

TEST_CASE("the camera follows the target's foot interpolated by alpha") {
    const core::Follow follow{ .previous = ORIGIN, .current = MOVED };
    CHECK(app::FollowedCentre(follow, 0.0).x == ORIGIN.x);
    CHECK(app::FollowedCentre(follow, HALFWAY).x == doctest::Approx(MOVED.x * HALF));
    CHECK(app::FollowedCentre(follow, 1.0).x == MOVED.x);
}

TEST_CASE("the window's centre shows the camera's centre, and its top-left corner lies up and left") {
    constexpr i32 WIDTH = 1920;
    constexpr i32 HEIGHT = 1080;
    const gfx::Camera camera{ .centre = MOVED };
    const core::Vec2 centre = app::WindowToWorld(camera, { .x = WIDTH * HALF, .y = HEIGHT * HALF }, WIDTH, HEIGHT);
    CHECK(centre.x == doctest::Approx(MOVED.x));
    CHECK(centre.y == doctest::Approx(MOVED.y));
    const core::Vec2 corner = app::WindowToWorld(camera, {}, WIDTH, HEIGHT);
    CHECK(corner.x == doctest::Approx(MOVED.x - (gfx::REFERENCE_SIZE.x * HALF)));
    CHECK(corner.y == doctest::Approx(MOVED.y - (gfx::REFERENCE_SIZE.y * HALF)));
}

TEST_CASE("picking takes the instance drawn on top") {
    constexpr core::StableId UNDER{ 1 };
    constexpr core::StableId OVER{ 2 };
    core::Snapshot snapshot;
    snapshot.instances = { Quad(ORIGIN, ORIGIN), Quad(ORIGIN, ORIGIN) };
    snapshot.ids = { UNDER, OVER };
    CHECK(app::Pick(snapshot, 1.0, ORIGIN) == OVER);
}

TEST_CASE("picking skips instances without an ID, and misses where nothing is drawn") {
    constexpr core::StableId ENTITY{ 1 };
    constexpr core::Vec2 FAR{ .x = 1'000.0F, .y = 0.0F };
    core::Snapshot snapshot;
    snapshot.instances = { Quad(ORIGIN, ORIGIN), Quad(ORIGIN, ORIGIN) };
    snapshot.ids = { ENTITY, core::StableId{} };
    CHECK(app::Pick(snapshot, 1.0, ORIGIN) == ENTITY);
    CHECK(app::Pick(snapshot, 1.0, FAR) == core::StableId{});
}

TEST_CASE("picking hits a moving instance where alpha draws it") {
    constexpr core::StableId MOVING{ 1 };
    core::Snapshot snapshot;
    snapshot.instances = { Quad(ORIGIN, MOVED) };
    snapshot.ids = { MOVING };
    CHECK(app::Pick(snapshot, 0.0, ORIGIN) == MOVING);
    CHECK(app::Pick(snapshot, 1.0, ORIGIN) == core::StableId{});
    CHECK(app::Pick(snapshot, 1.0, MOVED) == MOVING);
}

} // namespace
