#include "core/cxx23.hpp" // IWYU pragma: keep
#include "platform/window.hpp"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

namespace {

constexpr i32 SIZE = 64;

void Push(SDL_EventType type) {
    SDL_Event event{};
    event.type = type;
    REQUIRE(SDL_PushEvent(&event));
}

// A window the tests push their own events to: it draws nothing and reads no device.
std::expected<platform::Window, core::Error> OpenWindow() {
    // The dummy driver, not offscreen: on macOS SDL gives a window OpenGL by default when its driver has it, and
    // offscreen's comes through EGL, which macOS lacks.
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    // No gamepad backends: macOS's GameController and IOKit leak under LeakSanitizer when SDL starts and stops them.
    for (const char* backend : { SDL_HINT_JOYSTICK_MFI, SDL_HINT_JOYSTICK_IOKIT, SDL_HINT_JOYSTICK_HIDAPI }) {
        REQUIRE(SDL_SetHintWithPriority(backend, "0", SDL_HINT_OVERRIDE));
    }
    std::expected<platform::Window, core::Error> window = platform::Window::Open("window_tests", SIZE, SIZE);
    const std::string why = window.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(window, why);
    return window;
}

TEST_CASE("a minimised window pauses the game even with the focus") {
    CHECK(platform::PausesGame({ .focused = true, .minimised = true }));
    CHECK(platform::PausesGame({ .focused = false, .minimised = false }));
    CHECK_FALSE(platform::PausesGame({ .focused = true, .minimised = false }));
}

TEST_CASE("a start-up failure is shown with what failed and why") {
    std::string shownTitle;
    std::string shownMessage;
    platform::ReportStartFailure(
        "the window couldn't be opened",
        core::Error{ "no display" },
        [&shownTitle, &shownMessage](std::string_view title, std::string_view message) {
            shownTitle = title;
            shownMessage = message;
        }
    );
    CHECK(shownTitle == "project_n can't start");
    CHECK(shownMessage == "the window couldn't be opened:\nno display");
}

TEST_CASE("a window moving between displays names each new one once, and its mode when that changes") {
    const platform::DisplayMode fast{
        .id = 1, .name = "DP-2", .width = 2560, .height = 1440, .refreshNumerator = 165, .refreshDenominator = 1
    };
    const platform::DisplayMode slow{
        .id = 2, .name = "DP-1", .width = 1920, .height = 1080, .refreshNumerator = 59950, .refreshDenominator = 1000
    };
    const platform::DisplayMode twin{
        .id = 3, .name = "HDMI-A-1", .width = 1920, .height = 1080, .refreshNumerator = 59950, .refreshDenominator = 1000
    };
    // fast's display, its refresh rate changed
    const platform::DisplayMode modeChanged{
        .id = 1, .name = "DP-2", .width = 2560, .height = 1440, .refreshNumerator = 144, .refreshDenominator = 1
    };
    platform::WindowState state;
    CHECK(platform::MoveToDisplay(state, slow)); // first: where SDL opened it, before the compositor placed it
    CHECK(platform::MoveToDisplay(state, fast)); // placed on another display
    CHECK(state.display == fast);
    CHECK_FALSE(platform::MoveToDisplay(state, fast)); // the same display again: nothing new to name
    CHECK(platform::MoveToDisplay(state, twin));       // another display of the same mode is still a move
    CHECK(state.display == twin);
    CHECK(platform::MoveToDisplay(state, fast));
    CHECK(platform::MoveToDisplay(state, modeChanged)); // same display, its refresh rate changed
    CHECK(state.display == modeChanged);
}

TEST_CASE("a display's log line gives its size, refresh rate and name") {
    CHECK(
        platform::DisplayLine(
            { .id = 1, .name = "DP-2", .width = 2560, .height = 1440, .refreshNumerator = 164999, .refreshDenominator = 1000 }
        ) == "display: 2560x1440 at 165 Hz (DP-2)"
    );
    CHECK(
        platform::DisplayLine(
            { .id = 2, .name = "DP-1", .width = 1920, .height = 1080, .refreshNumerator = 59950, .refreshDenominator = 1000 }
        ) == "display: 1920x1080 at 59.95 Hz (DP-1)"
    );
    CHECK(platform::DisplayLine({ .id = 3, .width = 800, .height = 600 }) == "display: 800x600 at 0 Hz (unnamed)");
}

TEST_CASE("an opened window knows the display it is on, and keeps it when SDL says the display changed") {
    std::expected<platform::Window, core::Error> window = OpenWindow();
    std::vector<platform::InputEvent> inputs;
    const SDL_DisplayID display = SDL_GetDisplayForWindow(static_cast<SDL_Window*>(window->Handle()));
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display);
    REQUIRE(mode != nullptr);
    const platform::WindowState& opened = window->Poll(0, inputs);
    CHECK(opened.display.id == display);
    CHECK(opened.display.width == mode->w);
    CHECK(opened.display.height == mode->h);
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_DISPLAY_CHANGED;
    event.window.windowID = SDL_GetWindowID(static_cast<SDL_Window*>(window->Handle()));
    event.window.data1 = static_cast<i32>(display);
    REQUIRE(SDL_PushEvent(&event));
    CHECK(window->Poll(0, inputs).display.id == display);
}

TEST_CASE("the window is minimised from a minimise until a restore") {
    std::expected<platform::Window, core::Error> window = OpenWindow();
    std::vector<platform::InputEvent> inputs;
    Push(SDL_EVENT_WINDOW_MINIMIZED);
    CHECK(window->Poll(0, inputs).minimised);
    Push(SDL_EVENT_WINDOW_RESTORED);
    CHECK_FALSE(window->Poll(0, inputs).minimised);
}

void PushKey(SDL_Scancode key, bool down, bool repeat) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = key;
    event.key.down = down;
    event.key.repeat = repeat;
    REQUIRE(SDL_PushEvent(&event));
}

TEST_CASE("a key the game knows becomes an input, its repeats dropped and unknown keys ignored") {
    std::expected<platform::Window, core::Error> window = OpenWindow();
    PushKey(SDL_SCANCODE_W, true, false);
    PushKey(SDL_SCANCODE_W, true, true);
    PushKey(SDL_SCANCODE_Q, true, false);
    PushKey(SDL_SCANCODE_W, false, false);
    std::vector<platform::InputEvent> inputs;
    static_cast<void>(window->Poll(0, inputs));
    REQUIRE(inputs.size() == 2);
    CHECK(inputs[0].code == platform::InputCode::KeyW);
    CHECK(inputs[0].pressed);
    CHECK_FALSE(inputs[1].pressed);
}

TEST_CASE("a movement key is matched by its place on the keyboard, not by its label") {
    std::expected<platform::Window, core::Error> window = OpenWindow();
    // On AZERTY, the key where QWERTY has W is labelled Z.
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_W;
    event.key.key = SDLK_Z;
    event.key.down = true;
    REQUIRE(SDL_PushEvent(&event));
    std::vector<platform::InputEvent> inputs;
    static_cast<void>(window->Poll(0, inputs));
    REQUIRE(inputs.size() == 1);
    CHECK(inputs[0].code == platform::InputCode::KeyW);
}

TEST_CASE("the zoom keys, minus and equals, become inputs by their place on the keyboard") {
    std::expected<platform::Window, core::Error> window = OpenWindow();
    PushKey(SDL_SCANCODE_MINUS, true, false);
    PushKey(SDL_SCANCODE_EQUALS, true, false);
    std::vector<platform::InputEvent> inputs;
    static_cast<void>(window->Poll(0, inputs));
    REQUIRE(inputs.size() == 2);
    CHECK(inputs[0].code == platform::InputCode::KeyMinus);
    CHECK(inputs[1].code == platform::InputCode::KeyEquals);
}

TEST_CASE("Space and the number keys become inputs") {
    struct Pair {
        SDL_Scancode key;
        platform::InputCode code;
    };
    constexpr std::array PAIRS{
        Pair{ .key = SDL_SCANCODE_SPACE, .code = platform::InputCode::KeySpace },
        Pair{ .key = SDL_SCANCODE_1, .code = platform::InputCode::Key1 },
        Pair{ .key = SDL_SCANCODE_2, .code = platform::InputCode::Key2 },
        Pair{ .key = SDL_SCANCODE_3, .code = platform::InputCode::Key3 },
        Pair{ .key = SDL_SCANCODE_4, .code = platform::InputCode::Key4 },
        Pair{ .key = SDL_SCANCODE_5, .code = platform::InputCode::Key5 },
    };
    std::expected<platform::Window, core::Error> window = OpenWindow();
    for (const Pair& pair : PAIRS) {
        PushKey(pair.key, true, false);
    }
    std::vector<platform::InputEvent> inputs;
    static_cast<void>(window->Poll(0, inputs));
    REQUIRE(inputs.size() == PAIRS.size());
    for (std::size_t index = 0; index < PAIRS.size(); ++index) {
        CHECK(inputs[index].code == PAIRS[index].code);
        CHECK(inputs[index].pressed);
    }
}

void PushButton(u8 button, f32 across, f32 down) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = button;
    event.button.down = true;
    event.button.x = across;
    event.button.y = down;
    REQUIRE(SDL_PushEvent(&event));
}

TEST_CASE("a mouse press carries its button, position and whether Alt was held") {
    constexpr f32 CLICK_X = 12.0F;
    constexpr f32 CLICK_Y = 34.0F;
    std::expected<platform::Window, core::Error> window = OpenWindow();
    // Alt is read as the window polls, so each press is polled under its own modifier state.
    SDL_SetModState(SDL_KMOD_NONE);
    PushButton(SDL_BUTTON_LEFT, CLICK_X, CLICK_Y);
    std::vector<platform::InputEvent> plain;
    static_cast<void>(window->Poll(0, plain));
    SDL_SetModState(SDL_KMOD_LALT);
    PushButton(SDL_BUTTON_RIGHT, CLICK_X, CLICK_Y);
    std::vector<platform::InputEvent> withAlt;
    static_cast<void>(window->Poll(0, withAlt));
    SDL_SetModState(SDL_KMOD_NONE);
    REQUIRE(plain.size() == 1);
    REQUIRE(withAlt.size() == 1);
    CHECK(plain[0].code == platform::InputCode::MouseLeft);
    CHECK(plain[0].device == platform::Device::Mouse);
    CHECK(plain[0].position.x == CLICK_X);
    CHECK(plain[0].position.y == CLICK_Y);
    CHECK_FALSE(plain[0].alt);
    CHECK(withAlt[0].code == platform::InputCode::MouseRight);
    CHECK(withAlt[0].alt);
}

TEST_CASE("the camera keys I, J, K, L and C become inputs, with an anchor each side") {
    struct Pair {
        SDL_Scancode key;
        platform::InputCode code;
    };
    constexpr std::array PAIRS{
        Pair{ .key = SDL_SCANCODE_I, .code = platform::InputCode::KeyI },
        Pair{ .key = SDL_SCANCODE_J, .code = platform::InputCode::KeyJ },
        Pair{ .key = SDL_SCANCODE_K, .code = platform::InputCode::KeyK },
        Pair{ .key = SDL_SCANCODE_L, .code = platform::InputCode::KeyL },
        Pair{ .key = SDL_SCANCODE_C, .code = platform::InputCode::KeyC },
        Pair{ .key = SDL_SCANCODE_ESCAPE, .code = platform::InputCode::KeyEscape },
        Pair{ .key = SDL_SCANCODE_F5, .code = platform::InputCode::KeyF5 },
    };
    std::expected<platform::Window, core::Error> window = OpenWindow();
    for (const Pair& pair : PAIRS) {
        PushKey(pair.key, true, false);
    }
    std::vector<platform::InputEvent> inputs;
    static_cast<void>(window->Poll(0, inputs));
    REQUIRE(inputs.size() == PAIRS.size());
    for (std::size_t index = 0; index < PAIRS.size(); ++index) {
        CHECK(inputs[index].code == PAIRS[index].code);
        CHECK(inputs[index].pressed);
    }
}

TEST_CASE("the mouse's buttons become their inputs, and an unknown one is ignored") {
    struct Pair {
        u8 button;
        platform::InputCode code;
    };
    constexpr std::array PAIRS{
        Pair{ .button = SDL_BUTTON_LEFT, .code = platform::InputCode::MouseLeft },
        Pair{ .button = SDL_BUTTON_MIDDLE, .code = platform::InputCode::MouseMiddle },
        Pair{ .button = SDL_BUTTON_RIGHT, .code = platform::InputCode::MouseRight },
    };
    constexpr f32 CLICK = 5.0F;
    std::expected<platform::Window, core::Error> window = OpenWindow();
    for (const Pair& pair : PAIRS) {
        PushButton(pair.button, CLICK, CLICK);
    }
    PushButton(SDL_BUTTON_X1, CLICK, CLICK);
    std::vector<platform::InputEvent> inputs;
    static_cast<void>(window->Poll(0, inputs));
    REQUIRE(inputs.size() == PAIRS.size());
    for (std::size_t index = 0; index < PAIRS.size(); ++index) {
        CHECK(inputs[index].code == PAIRS[index].code);
        CHECK(inputs[index].device == platform::Device::Mouse);
    }
}

void PushAxis(SDL_GamepadAxis axis, i16 value) {
    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    event.gaxis.axis = static_cast<u8>(axis);
    event.gaxis.value = value;
    REQUIRE(SDL_PushEvent(&event));
}

TEST_CASE("the sticks' axes become inputs scaled to -1 to 1, and the triggers are ignored") {
    struct Sample {
        SDL_GamepadAxis axis;
        i16 raw;
        platform::InputCode code;
        f32 value;
    };
    constexpr std::array SAMPLES{
        Sample{ .axis = SDL_GAMEPAD_AXIS_LEFTX,
                .raw = SDL_JOYSTICK_AXIS_MAX,
                .code = platform::InputCode::PadLeftX,
                .value = 1.0F },
        Sample{ .axis = SDL_GAMEPAD_AXIS_LEFTY,
                .raw = SDL_JOYSTICK_AXIS_MIN,
                .code = platform::InputCode::PadLeftY,
                .value = -1.0F },
        Sample{ .axis = SDL_GAMEPAD_AXIS_RIGHTX,
                .raw = SDL_JOYSTICK_AXIS_MAX,
                .code = platform::InputCode::PadRightX,
                .value = 1.0F },
        Sample{ .axis = SDL_GAMEPAD_AXIS_RIGHTX,
                .raw = SDL_JOYSTICK_AXIS_MIN,
                .code = platform::InputCode::PadRightX,
                .value = -1.0F },
        Sample{ .axis = SDL_GAMEPAD_AXIS_RIGHTY,
                .raw = SDL_JOYSTICK_AXIS_MAX,
                .code = platform::InputCode::PadRightY,
                .value = 1.0F },
        Sample{ .axis = SDL_GAMEPAD_AXIS_RIGHTY, .raw = 0, .code = platform::InputCode::PadRightY, .value = 0.0F },
    };
    std::expected<platform::Window, core::Error> window = OpenWindow();
    for (const Sample& sample : SAMPLES) {
        PushAxis(sample.axis, sample.raw);
    }
    PushAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MAX);
    std::vector<platform::InputEvent> inputs;
    static_cast<void>(window->Poll(0, inputs));
    REQUIRE(inputs.size() == SAMPLES.size());
    for (std::size_t index = 0; index < SAMPLES.size(); ++index) {
        CHECK(inputs[index].code == SAMPLES[index].code);
        CHECK(inputs[index].device == platform::Device::Gamepad);
        CHECK(inputs[index].value == doctest::Approx(SAMPLES[index].value));
    }
}

void PushPadButton(SDL_GamepadButton button, bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    event.gbutton.button = static_cast<u8>(button);
    event.gbutton.down = down;
    REQUIRE(SDL_PushEvent(&event));
}

TEST_CASE("the shoulders and the right stick's click become inputs, other buttons are ignored") {
    std::expected<platform::Window, core::Error> window = OpenWindow();
    PushPadButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true);
    PushPadButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    PushPadButton(SDL_GAMEPAD_BUTTON_RIGHT_STICK, true);
    PushPadButton(SDL_GAMEPAD_BUTTON_RIGHT_STICK, false);
    PushPadButton(SDL_GAMEPAD_BUTTON_LEFT_STICK, true);
    std::vector<platform::InputEvent> inputs;
    static_cast<void>(window->Poll(0, inputs));
    REQUIRE(inputs.size() == 4);
    CHECK(inputs[0].code == platform::InputCode::PadLeftShoulder);
    CHECK(inputs[1].code == platform::InputCode::PadRightShoulder);
    CHECK(inputs[2].code == platform::InputCode::PadRightStick);
    CHECK(inputs[2].pressed);
    CHECK(inputs[3].code == platform::InputCode::PadRightStick);
    CHECK_FALSE(inputs[3].pressed);
}

} // namespace
