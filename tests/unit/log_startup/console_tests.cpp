#include "core/cxx23.hpp" // IWYU pragma: keep
#include <doctest/doctest.h>

#ifdef _WIN32
#include "core/log.hpp"

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
// A private console, so no other process shares the code page this changes: the one inherited is
// the parent's, and `xmake test` runs other test executables on it at the same time. It starts in
// another code page, so only Start can have switched it to UTF-8.
TEST_CASE("Start sets the console's output code page to UTF-8") {
    FreeConsole();
    // Wine's console accepts AllocConsole but not a code page: the same "no console" here.
    if (!AllocConsole() || !SetConsoleOutputCP(437)) {
        WARN_MESSAGE(false, "no console can be attached here");
        return;
    }
    REQUIRE(GetConsoleOutputCP() == 437);
    core::log::Start({});

    CHECK(GetConsoleOutputCP() == CP_UTF8);
}
#endif

} // namespace
