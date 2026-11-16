#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/rect.hpp"

#include <doctest/doctest.h>

namespace {

constexpr f32 LEFT = -10.0F;
constexpr f32 RIGHT = 30.0F;
constexpr f32 TOP = 20.0F;
constexpr f32 BOTTOM = 50.0F;
constexpr f32 MIDDLE_X = 0.0F;
constexpr f32 MIDDLE_Y = 35.0F;
constexpr core::Rect AREA{ .min = { .x = LEFT, .y = TOP }, .max = { .x = RIGHT, .y = BOTTOM } };
constexpr f32 JUST_OUT = 0.001F;
constexpr core::Vec2 POINT{ .x = 5.0F, .y = 5.0F };

TEST_CASE("a rectangle contains the points within it, its edges included") {
    CHECK(core::Contains(AREA, { .x = MIDDLE_X, .y = MIDDLE_Y })); // inside
    CHECK(core::Contains(AREA, AREA.min));                         // the top-left corner
    CHECK(core::Contains(AREA, AREA.max));                         // the bottom-right corner
    CHECK(core::Contains(AREA, { .x = LEFT, .y = BOTTOM }));       // the bottom-left corner
    CHECK(core::Contains(AREA, { .x = RIGHT, .y = MIDDLE_Y }));    // on the right edge
    CHECK(core::Contains({ .min = POINT, .max = POINT }, POINT));  // a rectangle of one point
}

TEST_CASE("a rectangle doesn't contain a point past any edge, or a NaN") {
    constexpr f32 NOT_A_NUMBER = std::numeric_limits<f32>::quiet_NaN();
    CHECK_FALSE(core::Contains(AREA, { .x = LEFT - JUST_OUT, .y = MIDDLE_Y }));
    CHECK_FALSE(core::Contains(AREA, { .x = RIGHT + JUST_OUT, .y = MIDDLE_Y }));
    CHECK_FALSE(core::Contains(AREA, { .x = MIDDLE_X, .y = TOP - JUST_OUT }));
    CHECK_FALSE(core::Contains(AREA, { .x = MIDDLE_X, .y = BOTTOM + JUST_OUT }));
    CHECK_FALSE(core::Contains(AREA, { .x = NOT_A_NUMBER, .y = MIDDLE_Y }));
    CHECK_FALSE(core::Contains(AREA, { .x = MIDDLE_X, .y = NOT_A_NUMBER }));
}

} // namespace
