#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"

#include <doctest/doctest.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
// Its own executable: Quill's loggers live for the whole process, and this Start() must be the first.
// The console starts in another code page, so only Start can have switched it to UTF-8.
TEST_CASE("Start sets the console's output code page to UTF-8") {
    if (GetConsoleOutputCP() == 0 && !AllocConsole()) {
        MESSAGE("no console can be attached here");
        return;
    }
    REQUIRE(SetConsoleOutputCP(437));
    REQUIRE(GetConsoleOutputCP() == 437);
    core::log::Start({});

    CHECK(GetConsoleOutputCP() == CP_UTF8);
}
#endif

} // namespace
