#include "app/camera_path.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

#include <doctest/doctest.h>

namespace {

constexpr f32 LAST = 22.0F; // the end of the path's last segment

TEST_CASE("the camera path ends with a quit after its last segment") {
    constexpr f32 AFTER = 22.05F;
    CHECK_FALSE(app::CameraPathOver(0.0F));
    CHECK_FALSE(app::CameraPathOver(LAST));
    CHECK(app::CameraPathSegment(LAST) == app::CameraPath().size() - 1);
    CHECK(app::CameraPathOver(AFTER));
    CHECK_FALSE(app::CameraPathSegment(AFTER));
}

TEST_CASE("the camera path holds Move from its start through its follow segment only") {
    constexpr f32 FOLLOW_END = 4.0F;
    constexpr f32 PANNING = 4.05F;
    for (const f32 seconds : { 0.0F, FOLLOW_END }) {
        CAPTURE(seconds);
        const core::Vec2 move = app::CameraPathMove(seconds);
        CHECK(move.x == 1.0F); // east
        CHECK(move.y == 0.0F);
    }
    for (const f32 seconds : { PANNING, LAST }) {
        CAPTURE(seconds);
        const core::Vec2 move = app::CameraPathMove(seconds);
        CHECK(move.x == 0.0F);
        CHECK(move.y == 0.0F);
    }
}

} // namespace
