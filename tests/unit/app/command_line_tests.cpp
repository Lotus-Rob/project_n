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

TEST_CASE("--zoom sets the zoom every scene starts at; without it the zoom is 1") {
    constexpr std::array<const char*, 4> FARTHEST{ { "project_n", "--zoom", "0.25", "--cast-swords" } };
    constexpr std::array<const char*, 3> CLOSEST{ { "project_n", "--zoom", "2" } };
    constexpr std::array<const char*, 2> WITHOUT{ { "project_n", "--cast-swords" } };
    const std::expected<f32, core::Error> farthest = app::ZoomOf(FARTHEST);
    const std::expected<f32, core::Error> closest = app::ZoomOf(CLOSEST);
    const std::expected<f32, core::Error> without = app::ZoomOf(WITHOUT);
    REQUIRE(farthest.has_value());
    REQUIRE(closest.has_value());
    REQUIRE(without.has_value());
    CHECK(*farthest == gfx::MIN_ZOOM);
    CHECK(*closest == gfx::MAX_ZOOM);
    CHECK(*without == 1.0F);
}

TEST_CASE("--zoom refuses a missing value, text that isn't a number and a zoom out of range") {
    constexpr std::array<const char*, 2> MISSING{ { "project_n", "--zoom" } };
    constexpr std::array<const char*, 3> TEXT{ { "project_n", "--zoom", "close" } };
    constexpr std::array<const char*, 3> TRAILING{ { "project_n", "--zoom", "1.5x" } };
    constexpr std::array<const char*, 3> TOO_FAR{ { "project_n", "--zoom", "0.2" } };
    constexpr std::array<const char*, 3> TOO_CLOSE{ { "project_n", "--zoom", "2.5" } };
    constexpr std::array<const char*, 3> NOT_A_NUMBER{ { "project_n", "--zoom", "nan" } };
    for (const std::span<const char* const> args : { std::span<const char* const>{ MISSING },
                                                     std::span<const char* const>{ TEXT },
                                                     std::span<const char* const>{ TRAILING },
                                                     std::span<const char* const>{ TOO_FAR },
                                                     std::span<const char* const>{ TOO_CLOSE },
                                                     std::span<const char* const>{ NOT_A_NUMBER } }) {
        const std::expected<f32, core::Error> zoom = app::ZoomOf(args);
        REQUIRE_FALSE(zoom.has_value());
        CAPTURE(zoom.error().message);
        CHECK(zoom.error().message.starts_with("--zoom: "));
    }
}

} // namespace
