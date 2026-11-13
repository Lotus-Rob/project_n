#include "app/bindings.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "platform/window.hpp"

#include <doctest/doctest.h>

namespace {

using platform::InputCode;

platform::InputEvent Key(InputCode code, bool pressed) {
    return { .code = code, .pressed = pressed, .value = pressed ? 1.0F : 0.0F };
}

platform::InputEvent Stick(InputCode axis, f32 value) {
    return { .device = platform::Device::Gamepad, .code = axis, .value = value };
}

TEST_CASE("a debug key gives its control, pressed and then released") {
    app::Bindings bindings;
    const std::optional<app::ControlEvent> down = bindings.Handle(Key(InputCode::KeyF5, true));
    const std::optional<app::ControlEvent> released = bindings.Handle(Key(InputCode::KeyF5, false));
    REQUIRE(down.has_value());
    REQUIRE(released.has_value());
    CHECK(down.value_or(app::ControlEvent{}).control == app::Control::Save);
    CHECK(down.value_or(app::ControlEvent{}).pressed);
    CHECK_FALSE(released.value_or(app::ControlEvent{ .pressed = true }).pressed);
}

TEST_CASE("a held movement key moves until released") {
    app::Bindings bindings;
    static_cast<void>(bindings.Handle(Key(InputCode::KeyD, true)));
    CHECK(bindings.Move().x == doctest::Approx(1.0F));
    static_cast<void>(bindings.Handle(Key(InputCode::KeyD, false)));
    CHECK(bindings.Move().x == 0.0F);
}

TEST_CASE("two movement keys held move diagonally, no faster than one") {
    app::Bindings bindings;
    static_cast<void>(bindings.Handle(Key(InputCode::KeyD, true)));
    static_cast<void>(bindings.Handle(Key(InputCode::KeyW, true)));
    const core::Vec2 move = bindings.Move();
    CHECK(std::hypot(move.x, move.y) == doctest::Approx(1.0F));
    CHECK(move.x > 0.0F);
    CHECK(move.y < 0.0F);
}

TEST_CASE("a movement key pressed and released in one frame leaves movement at rest") {
    app::Bindings bindings;
    CHECK_FALSE(bindings.Handle(Key(InputCode::KeyLeft, true)).has_value());
    CHECK_FALSE(bindings.Handle(Key(InputCode::KeyLeft, false)).has_value());
    CHECK(bindings.Move().x == 0.0F);
}

TEST_CASE("the stick inside its dead zone is at rest") {
    constexpr f32 INSIDE = app::STICK_DEAD_ZONE * 0.9F;
    app::Bindings bindings;
    static_cast<void>(bindings.Handle(Stick(InputCode::PadLeftX, INSIDE)));
    CHECK(bindings.Move().x == 0.0F);
}

TEST_CASE("the stick just past its dead zone moves slowly, from zero") {
    constexpr f32 JUST_PAST = app::STICK_DEAD_ZONE + 0.01F;
    app::Bindings bindings;
    static_cast<void>(bindings.Handle(Stick(InputCode::PadLeftX, JUST_PAST)));
    CHECK(bindings.Move().x > 0.0F);
    CHECK(bindings.Move().x < JUST_PAST);
}

TEST_CASE("the stick pushed all the way moves at full speed") {
    app::Bindings bindings;
    static_cast<void>(bindings.Handle(Stick(InputCode::PadLeftX, 1.0F)));
    CHECK(bindings.Move().x == doctest::Approx(1.0F));
}

TEST_CASE("the wheel zooms by its notches") {
    constexpr f32 NOTCHES = 2.0F;
    app::Bindings bindings;
    const app::ControlEvent wheel =
        bindings.Handle({ .device = platform::Device::Mouse, .code = InputCode::MouseWheel, .value = NOTCHES })
            .value_or(app::ControlEvent{});
    CHECK(wheel.control == app::Control::Zoom);
    CHECK(wheel.value == NOTCHES);
}

TEST_CASE("the left shoulder button zooms out") {
    app::Bindings bindings;
    const app::ControlEvent shoulder = bindings.Handle(Key(InputCode::PadLeftShoulder, true)).value_or(app::ControlEvent{});
    CHECK(shoulder.control == app::Control::Zoom);
    CHECK(shoulder.value < 0.0F);
}

TEST_CASE("the left button is Primary and carries Alt") {
    constexpr core::Vec2 WHERE{ .x = 10.0F, .y = 20.0F };
    app::Bindings bindings;
    const app::ControlEvent click = bindings
                                        .Handle(
                                            { .device = platform::Device::Mouse,
                                              .code = InputCode::MouseLeft,
                                              .pressed = true,
                                              .value = 1.0F,
                                              .position = WHERE,
                                              .alt = true }
                                        )
                                        .value_or(app::ControlEvent{});
    CHECK(click.control == app::Control::Primary);
    CHECK(click.position.x == WHERE.x);
    CHECK(click.position.y == WHERE.y);
    CHECK(click.alt);
}

TEST_CASE("the right button, Space and 1 to 5 drive their controls") {
    struct Pair {
        InputCode code;
        app::Control control;
    };
    constexpr std::array PAIRS{
        Pair{ .code = InputCode::MouseRight, .control = app::Control::Secondary },
        Pair{ .code = InputCode::KeySpace, .control = app::Control::Movement },
        Pair{ .code = InputCode::Key1, .control = app::Control::Special1 },
        Pair{ .code = InputCode::Key2, .control = app::Control::Special2 },
        Pair{ .code = InputCode::Key3, .control = app::Control::Special3 },
        Pair{ .code = InputCode::Key4, .control = app::Control::Special4 },
        Pair{ .code = InputCode::Key5, .control = app::Control::Ultimate },
    };
    app::Bindings bindings;
    for (const Pair& pair : PAIRS) {
        const std::optional<app::ControlEvent> down = bindings.Handle(Key(pair.code, true));
        const std::optional<app::ControlEvent> released = bindings.Handle(Key(pair.code, false));
        REQUIRE(down.has_value());
        REQUIRE(released.has_value());
        CHECK(down.value_or(app::ControlEvent{}).control == pair.control);
        CHECK(down.value_or(app::ControlEvent{}).pressed);
        CHECK_FALSE(released.value_or(app::ControlEvent{ .pressed = true }).pressed);
    }
}

TEST_CASE("a stick drifting inside its dead zone leaves the keyboard and mouse in use") {
    constexpr f32 DRIFT = 0.05F;
    app::DeviceInUse device;
    static_cast<void>(device.Take({ .device = platform::Device::Mouse, .code = InputCode::MouseLeft, .pressed = true }));
    const bool changed = device.Take(Stick(InputCode::PadLeftX, DRIFT));
    CHECK_FALSE(changed);
    CHECK(device.Used() == platform::Device::Mouse);
}

TEST_CASE("a stick held diagonally past its dead zone with each axis inside it puts the gamepad in use") {
    constexpr f32 DIAGONAL = 0.19F; // each axis under 0.2; the pair's length about 0.27, past it
    app::DeviceInUse device;
    static_cast<void>(device.Take({ .device = platform::Device::Mouse, .code = InputCode::MouseLeft, .pressed = true }));
    static_cast<void>(device.Take(Stick(InputCode::PadLeftX, DIAGONAL)));
    static_cast<void>(device.Take(Stick(InputCode::PadLeftY, DIAGONAL)));
    CHECK(device.Used() == platform::Device::Gamepad);
}

TEST_CASE("the cursor moving after a gamepad press puts the mouse in use") {
    constexpr core::Vec2 HERE{ .x = 10.0F, .y = 20.0F };
    constexpr core::Vec2 THERE{ .x = 11.0F, .y = 20.0F };
    app::DeviceInUse device;
    static_cast<void>(device.TakeCursor(HERE));
    static_cast<void>(
        device.Take({ .device = platform::Device::Gamepad, .code = InputCode::PadLeftShoulder, .pressed = true })
    );
    const bool changed = device.TakeCursor(THERE);
    CHECK(changed);
    CHECK(device.Used() == platform::Device::Mouse);
}

TEST_CASE("the cursor standing still leaves the gamepad in use") {
    constexpr core::Vec2 HERE{ .x = 10.0F, .y = 20.0F };
    app::DeviceInUse device;
    static_cast<void>(device.TakeCursor(HERE));
    static_cast<void>(
        device.Take({ .device = platform::Device::Gamepad, .code = InputCode::PadLeftShoulder, .pressed = true })
    );
    const bool changed = device.TakeCursor(HERE);
    CHECK_FALSE(changed);
    CHECK(device.Used() == platform::Device::Gamepad);
}

TEST_CASE("F7 sends the debug burst") {
    app::Bindings bindings;
    const std::optional<app::ControlEvent> down = bindings.Handle(Key(InputCode::KeyF7, true));
    REQUIRE(down.has_value());
    CHECK(down.value_or(app::ControlEvent{}).control == app::Control::DebugBurst);
    CHECK(down.value_or(app::ControlEvent{}).pressed);
}

TEST_CASE("F8 shows or hides the arena's showcase seals") {
    app::Bindings bindings;
    const std::optional<app::ControlEvent> down = bindings.Handle(Key(InputCode::KeyF8, true));
    REQUIRE(down.has_value());
    CHECK(down.value_or(app::ControlEvent{}).control == app::Control::ToggleShowcase);
    CHECK(down.value_or(app::ControlEvent{}).pressed);
}

} // namespace
