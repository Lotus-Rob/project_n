#include "app/click.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

#include <doctest/doctest.h>

namespace {

constexpr core::Vec2 GROUND{ .x = 1'234.0F, .y = -567.0F };
constexpr core::StableId TRAVELLER{ 42 };

TEST_CASE("a click on a traveller selects it") {
    const core::Action onMap = app::ClickAction(true, TRAVELLER, GROUND);
    CHECK(onMap.kind == core::ActionKind::Select);
    CHECK(onMap.target == TRAVELLER);
    const core::Action elsewhere = app::ClickAction(false, TRAVELLER, GROUND);
    CHECK(elsewhere.kind == core::ActionKind::Select);
    CHECK(elsewhere.target == TRAVELLER);
}

TEST_CASE("a click on open ground of the world map walks there") {
    const core::Action action = app::ClickAction(true, core::StableId{}, GROUND);
    CHECK(action.kind == core::ActionKind::WalkTo);
    CHECK(action.point.x == GROUND.x);
    CHECK(action.point.y == GROUND.y);
}

TEST_CASE("a click on open ground elsewhere selects nothing") {
    const core::Action action = app::ClickAction(false, core::StableId{}, GROUND);
    CHECK(action.kind == core::ActionKind::Select);
    CHECK(action.target == core::StableId{});
}

} // namespace
