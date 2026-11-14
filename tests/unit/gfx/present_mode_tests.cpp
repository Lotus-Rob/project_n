#include "core/cxx23.hpp" // IWYU pragma: keep
#include "gfx/renderer.hpp"

#include <doctest/doctest.h>

namespace {

TEST_CASE("unpaced presentation prefers immediate, then mailbox") {
    const std::span<const gfx::PresentMode> order = gfx::PresentModeOrder(true);
    REQUIRE(order.size() == 2);
    CHECK(order[0] == gfx::PresentMode::Immediate);
    CHECK(order[1] == gfx::PresentMode::Mailbox);
}

TEST_CASE("by default presentation prefers mailbox, then immediate") {
    const std::span<const gfx::PresentMode> order = gfx::PresentModeOrder(false);
    REQUIRE(order.size() == 2);
    CHECK(order[0] == gfx::PresentMode::Mailbox);
    CHECK(order[1] == gfx::PresentMode::Immediate);
}

} // namespace
