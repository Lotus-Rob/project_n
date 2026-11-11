#include "app/cast.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

#include <doctest/doctest.h>

namespace {

constexpr core::Vec2 HERE{ .x = 10.0F, .y = 20.0F };
constexpr core::Vec2 THERE{ .x = -30.0F, .y = 40.0F };

// Whether a press or release of `control` casts at `point` (none: the cursor isn't over the window). A call of its own:
// MSVC's C4866 refuses CastAction's argument and result temporaries inside a CHECK (tests/AGENTS.md).
bool Casts(app::Control control, bool pressed, bool combat, bool alt, std::optional<core::Vec2> point) {
    return app::CastAction(control, pressed, combat, alt, point).has_value();
}

// The cast a press of `control` in combat sends at `point`, or a default Action when it sends none.
core::Action PressedInCombat(app::Control control, core::Vec2 point) {
    return app::CastAction(control, true, true, false, point).value_or(core::Action{});
}

TEST_CASE("a left click in combat casts the generator at the point") {
    const bool casts1 = Casts(app::Control::Primary, true, true, false, HERE);
    REQUIRE(casts1);
    const core::Action cast = PressedInCombat(app::Control::Primary, HERE);
    CHECK(cast.kind == core::ActionKind::Cast);
    CHECK(cast.slot == core::Slot::Generator);
    CHECK(cast.point.x == HERE.x);
    CHECK(cast.point.y == HERE.y);
}

TEST_CASE("a right click in combat casts the spender") {
    const bool casts2 = Casts(app::Control::Secondary, true, true, false, HERE);
    REQUIRE(casts2);
    CHECK(PressedInCombat(app::Control::Secondary, HERE).slot == core::Slot::Spender);
}

TEST_CASE("two quick left clicks are two casts") {
    // Press, release, press in one frame's events: the release sends nothing and the presses aren't merged.
    struct Click {
        bool pressed = false;
        core::Vec2 point{};
    };
    constexpr std::array CLICKS{
        Click{ .pressed = true, .point = HERE },
        Click{ .pressed = false, .point = HERE },
        Click{ .pressed = true, .point = THERE },
    };
    std::vector<core::Action> sent;
    for (const Click& click : CLICKS) {
        const std::optional<core::Action> cast =
            app::CastAction(app::Control::Primary, click.pressed, true, false, click.point);
        if (cast) {
            sent.push_back(*cast);
        }
    }
    REQUIRE(sent.size() == 2);
    CHECK(sent[0].slot == core::Slot::Generator);
    CHECK(sent[1].slot == core::Slot::Generator);
    CHECK(sent[0].point.x == HERE.x);
    CHECK(sent[1].point.x == THERE.x);
}

TEST_CASE("a press in combat casts its slot at the point") {
    struct Pair {
        app::Control control;
        core::Slot slot;
    };
    constexpr std::array PAIRS{
        Pair{ .control = app::Control::Movement, .slot = core::Slot::Movement },
        Pair{ .control = app::Control::Special1, .slot = core::Slot::Special1 },
        Pair{ .control = app::Control::Special2, .slot = core::Slot::Special2 },
        Pair{ .control = app::Control::Special3, .slot = core::Slot::Special3 },
        Pair{ .control = app::Control::Special4, .slot = core::Slot::Special4 },
        Pair{ .control = app::Control::Ultimate, .slot = core::Slot::Ultimate },
    };
    for (const Pair& pair : PAIRS) {
        const bool casts3 = Casts(pair.control, true, true, false, THERE);
        REQUIRE(casts3);
        const core::Action cast = PressedInCombat(pair.control, THERE);
        CHECK(cast.slot == pair.slot);
        CHECK(cast.point.y == THERE.y);
    }
}

TEST_CASE("a release casts nothing") {
    const bool casts4 = Casts(app::Control::Primary, false, true, false, HERE);
    CHECK_FALSE(casts4);
    const bool casts5 = Casts(app::Control::Ultimate, false, true, false, HERE);
    CHECK_FALSE(casts5);
}

TEST_CASE("a held button casts once") {
    // The press is one event and the held button sends no more: only the press casts, the release nothing.
    const std::optional<core::Action> press = app::CastAction(app::Control::Secondary, true, true, false, HERE);
    const std::optional<core::Action> release = app::CastAction(app::Control::Secondary, false, true, false, HERE);
    CHECK(press.has_value());
    CHECK_FALSE(release.has_value());
}

TEST_CASE("Alt and a left click selects instead in combat") {
    CHECK(app::SelectsInstead(app::Control::Primary, true, true));
    CHECK_FALSE(app::SelectsInstead(app::Control::Primary, true, false));
    const bool casts6 = Casts(app::Control::Primary, true, true, true, HERE);
    CHECK_FALSE(casts6);
    const bool casts7 = Casts(app::Control::Secondary, true, true, true, HERE);
    CHECK(casts7);
}

TEST_CASE("outside combat a left click selects and the other slots cast nothing") {
    CHECK(app::SelectsInstead(app::Control::Primary, false, false));
    CHECK_FALSE(app::SelectsInstead(app::Control::Secondary, false, false));
    for (const app::Control control : { app::Control::Primary,
                                        app::Control::Secondary,
                                        app::Control::Movement,
                                        app::Control::Special1,
                                        app::Control::Ultimate }) {
        const bool casts8 = Casts(control, true, false, false, HERE);
        CHECK_FALSE(casts8);
    }
}

TEST_CASE("a key cast without a cursor sends nothing") {
    const bool casts9 = Casts(app::Control::Movement, true, true, false, std::nullopt);
    CHECK_FALSE(casts9);
    const bool casts10 = Casts(app::Control::Special2, true, true, false, std::nullopt);
    CHECK_FALSE(casts10);
}

TEST_CASE("a control without a slot casts nothing") {
    CHECK_FALSE(app::SlotOf(app::Control::Zoom).has_value());
    const bool casts11 = Casts(app::Control::Save, true, true, false, HERE);
    CHECK_FALSE(casts11);
}

} // namespace
