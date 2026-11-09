#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/error.hpp"

#include <doctest/doctest.h>

namespace {

TEST_CASE("combining two failures keeps both causes") {
    constexpr std::string_view DRAW_CAUSE = "no scene target";
    constexpr std::string_view SUBMIT_CAUSE = "submit failed";
    const std::expected<void, core::Error> drawn = std::unexpected(core::Error{ std::string(DRAW_CAUSE) });
    const std::expected<void, core::Error> submitted = std::unexpected(core::Error{ std::string(SUBMIT_CAUSE) });

    const auto outcome = core::Combine(drawn, submitted);

    REQUIRE_FALSE(outcome);
    const std::string& message = outcome.error().message;
    CHECK(message.contains(DRAW_CAUSE));
    CHECK(message.contains(SUBMIT_CAUSE));
}

} // namespace
