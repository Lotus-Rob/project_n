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

} // namespace
