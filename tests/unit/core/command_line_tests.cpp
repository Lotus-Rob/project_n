#include "core/command_line.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/tick_clock.hpp"

#include <doctest/doctest.h>

namespace {

TEST_CASE("seconds from a command line are whole and above 0") {
    constexpr u64 FIVE_SECONDS_NS = 5 * core::NS_PER_SECOND;
    const std::expected<u64, core::Error> five = core::SecondsToNs("5");
    CHECK(five == FIVE_SECONDS_NS);
    for (const std::string_view bad : { "", "0", "-1", "5x", "1.5", "abc", "99999999999" }) {
        CAPTURE(bad);
        CHECK_FALSE(core::SecondsToNs(bad).has_value());
    }
}

} // namespace
