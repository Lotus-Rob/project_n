#include "core/cxx23.hpp" // IWYU pragma: keep
#include "platform/clock.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 NS_PER_MS = 1'000'000;

TEST_CASE("waiting returns no earlier than the deadline") {
    constexpr u64 WAIT_NS = 3 * NS_PER_MS;
    const u64 deadline = platform::NowNs() + WAIT_NS;
    platform::WaitUntilNs(deadline);
    CHECK(platform::NowNs() >= deadline);
}

TEST_CASE("a deadline that has passed returns at once") {
    const u64 start = platform::NowNs();
    platform::WaitUntilNs(0); // the clock's zero is always past
    // Loose on purpose: a busy runner may deschedule the thread, but a wrong wait sleeps far longer.
    CHECK(platform::NowNs() - start < 100 * NS_PER_MS);
}

} // namespace
