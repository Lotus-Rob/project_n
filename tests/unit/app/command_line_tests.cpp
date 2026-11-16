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

TEST_CASE("--workers takes a count from 1 to 64") {
    constexpr std::array<const char*, 4> TWO{ { "project_n", "--workers", "2", "--unpaced" } };
    constexpr std::array<const char*, 3> MOST{ { "project_n", "--workers", "64" } };
    constexpr std::array<const char*, 2> WITHOUT{ { "project_n", "--unpaced" } };
    const std::expected<u32, core::Error> two = app::WorkersOf(TWO);
    const std::expected<u32, core::Error> most = app::WorkersOf(MOST);
    const std::expected<u32, core::Error> without = app::WorkersOf(WITHOUT);
    REQUIRE(two.has_value());
    REQUIRE(most.has_value());
    REQUIRE(without.has_value());
    CHECK(*two == 2);
    CHECK(*most == 64);
    CHECK(*without == 0);
}

TEST_CASE("--workers refuses a missing, non-numeric or out-of-range count") {
    constexpr std::array<const char*, 2> MISSING{ { "project_n", "--workers" } };
    constexpr std::array<const char*, 3> TEXT{ { "project_n", "--workers", "two" } };
    constexpr std::array<const char*, 3> ZERO{ { "project_n", "--workers", "0" } };
    constexpr std::array<const char*, 3> TOO_MANY{ { "project_n", "--workers", "65" } };
    for (const std::span<const char* const> args : { std::span<const char* const>{ MISSING },
                                                     std::span<const char* const>{ TEXT },
                                                     std::span<const char* const>{ ZERO },
                                                     std::span<const char* const>{ TOO_MANY } }) {
        const std::expected<u32, core::Error> workers = app::WorkersOf(args);
        REQUIRE_FALSE(workers.has_value());
        CAPTURE(workers.error().message);
        CHECK(workers.error().message.starts_with("--workers: "));
    }
}

TEST_CASE("--travel-share sets the share of people on the road, in parts per thousand; without it none") {
    constexpr std::array<const char*, 4> RAISED{ { "project_n", "--world", "--travel-share", "200" } };
    constexpr std::array<const char*, 3> NONE{ { "project_n", "--travel-share", "0" } };
    constexpr std::array<const char*, 3> ALL{ { "project_n", "--travel-share", "1000" } };
    constexpr std::array<const char*, 2> WITHOUT{ { "project_n", "--world" } };
    const std::expected<std::optional<u16>, core::Error> raised = app::TravelShareOf(RAISED);
    const std::expected<std::optional<u16>, core::Error> none = app::TravelShareOf(NONE);
    const std::expected<std::optional<u16>, core::Error> all = app::TravelShareOf(ALL);
    const std::expected<std::optional<u16>, core::Error> without = app::TravelShareOf(WITHOUT);
    REQUIRE(raised.has_value());
    REQUIRE(none.has_value());
    REQUIRE(all.has_value());
    REQUIRE(without.has_value());
    CHECK(*raised == std::optional<u16>{ 200 });
    CHECK(*none == std::optional<u16>{ 0 });
    CHECK(*all == std::optional<u16>{ 1'000 });
    CHECK_FALSE(without->has_value());
}

TEST_CASE("--travel-share refuses a missing, non-numeric, negative or past-a-thousand share") {
    constexpr std::array<const char*, 2> MISSING{ { "project_n", "--travel-share" } };
    constexpr std::array<const char*, 3> TEXT{ { "project_n", "--travel-share", "many" } };
    constexpr std::array<const char*, 3> TRAILING{ { "project_n", "--travel-share", "200%" } };
    constexpr std::array<const char*, 3> NEGATIVE{ { "project_n", "--travel-share", "-1" } };
    constexpr std::array<const char*, 3> PAST{ { "project_n", "--travel-share", "1001" } };
    for (const std::span<const char* const> args : { std::span<const char* const>{ MISSING },
                                                     std::span<const char* const>{ TEXT },
                                                     std::span<const char* const>{ TRAILING },
                                                     std::span<const char* const>{ NEGATIVE },
                                                     std::span<const char* const>{ PAST } }) {
        const std::expected<std::optional<u16>, core::Error> share = app::TravelShareOf(args);
        REQUIRE_FALSE(share.has_value());
        CAPTURE(share.error().message);
        CHECK(share.error().message.starts_with("--travel-share: "));
    }
}

TEST_CASE("--month-step-at gives the second of the camera path a month passes at; without it none") {
    constexpr std::array<const char*, 3> LATE{ { "project_n", "--month-step-at", "15.5" } };
    constexpr std::array<const char*, 3> START{ { "project_n", "--month-step-at", "0" } };
    constexpr std::array<const char*, 2> WITHOUT{ { "project_n", "--camera-path" } };
    constexpr f32 LATE_SECOND = 15.5F;
    const std::expected<std::optional<f32>, core::Error> late = app::MonthStepAtOf(LATE);
    const std::expected<std::optional<f32>, core::Error> start = app::MonthStepAtOf(START);
    const std::expected<std::optional<f32>, core::Error> without = app::MonthStepAtOf(WITHOUT);
    REQUIRE(late.has_value());
    REQUIRE(start.has_value());
    REQUIRE(without.has_value());
    CHECK(*late == std::optional<f32>{ LATE_SECOND });
    CHECK(*start == std::optional<f32>{ 0.0F });
    CHECK_FALSE(without->has_value());
}

TEST_CASE("--month-step-at refuses a missing, non-numeric, negative or infinite time") {
    constexpr std::array<const char*, 2> MISSING{ { "project_n", "--month-step-at" } };
    constexpr std::array<const char*, 3> TEXT{ { "project_n", "--month-step-at", "soon" } };
    constexpr std::array<const char*, 3> TRAILING{ { "project_n", "--month-step-at", "5s" } };
    constexpr std::array<const char*, 3> NEGATIVE{ { "project_n", "--month-step-at", "-1" } };
    constexpr std::array<const char*, 3> INFINITE{ { "project_n", "--month-step-at", "inf" } };
    for (const std::span<const char* const> args : { std::span<const char* const>{ MISSING },
                                                     std::span<const char* const>{ TEXT },
                                                     std::span<const char* const>{ TRAILING },
                                                     std::span<const char* const>{ NEGATIVE },
                                                     std::span<const char* const>{ INFINITE } }) {
        const std::expected<std::optional<f32>, core::Error> step = app::MonthStepAtOf(args);
        REQUIRE_FALSE(step.has_value());
        CAPTURE(step.error().message);
        CHECK(step.error().message.starts_with("--month-step-at: "));
    }
}

} // namespace
