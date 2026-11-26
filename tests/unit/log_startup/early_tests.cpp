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
// First in the file, because the early logger is created only once: it writes to the same console
// as Start's, so it sets the code page too.
TEST_CASE("the early logger sets the console's output code page to UTF-8") {
    if (GetConsoleOutputCP() == 0 && !AllocConsole()) {
        WARN_MESSAGE(false, "no console can be attached here");
        return;
    }
    REQUIRE(SetConsoleOutputCP(437));
    REQUIRE(GetConsoleOutputCP() == 437);
    REQUIRE(core::log::Root() != nullptr);

    CHECK(GetConsoleOutputCP() == CP_UTF8);
}
#endif

// Its own executable: Quill's loggers live for the whole process, and nothing here calls Start().
TEST_CASE("before Start, log calls go to the console-only early logger") {
    CHECK(core::log::Root()->get_logger_name() == "early");
}

} // namespace
