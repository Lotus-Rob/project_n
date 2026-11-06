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
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE));
    std::expected<platform::Window, core::Error> window = platform::Window::Open("window_tests", SIZE, SIZE);
    const std::string why = window.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(window, why);
    Push(SDL_EVENT_WINDOW_MINIMIZED);
    CHECK(window->Poll(0).minimised);
    Push(SDL_EVENT_WINDOW_RESTORED);
    CHECK_FALSE(window->Poll(0).minimised);
}

} // namespace
