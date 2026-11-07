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

} // namespace
