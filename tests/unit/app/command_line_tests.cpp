#include "app/app.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

#include <doctest/doctest.h>

namespace {

constexpr std::string_view BURST = "--burst";

TEST_CASE("--burst asks for a debug burst every second") {
    constexpr std::array<const char*, 3> WITH{ { "project_n", "--stress", "--burst" } };
    constexpr std::array<const char*, 2> WITHOUT{ { "project_n", "--stress" } };
    const bool with = app::HasFlag(WITH, BURST);
    const bool without = app::HasFlag(WITHOUT, BURST);
    CHECK(with);
    CHECK_FALSE(without);
}

TEST_CASE("a flag is matched whole, not by its start") {
    constexpr std::array<const char*, 2> LONGER{ { "project_n", "--burst-size" } };
    const bool matched = app::HasFlag(LONGER, BURST);
    CHECK_FALSE(matched);
}

} // namespace
