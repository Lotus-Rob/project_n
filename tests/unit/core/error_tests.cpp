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

struct Caught {
    std::string_view why;
    std::exception_ptr error;
    std::string_view text;
};

TEST_CASE("an exception is described by its what(), or by a stand-in when it has none") {
    const std::array<Caught, 4> cases{ {
        { .why = "a standard exception",
          .error = std::make_exception_ptr(std::runtime_error{ "disk gone" }),
          .text = "disk gone" },
        { .why = "a derived exception",
          .error = std::make_exception_ptr(std::out_of_range{ "past the end" }),
          .text = "past the end" },
        { .why = "an exception of another type", .error = std::make_exception_ptr(7), .text = "an exception of unknown type" },
        { .why = "no exception at all", .error = nullptr, .text = "no exception" },
    } };
    for (const Caught& thrown : cases) {
        CAPTURE(thrown.why);
        CHECK(core::Describe(thrown.error) == thrown.text);
    }
}

} // namespace
