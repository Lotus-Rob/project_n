#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/display_delay.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 ONE_MS = core::NS_PER_MS;
constexpr u64 START_NS = core::NS_PER_SECOND;

// 600 samples cycling 1.0 ... 1.8 ms with 6 of 2.4 ms: the 99th percentile is 2.4 ms.
void ObserveSteady(core::DisplayDelay& delay) {
    for (std::size_t i = 0; i < core::DISPLAY_DELAY_WINDOW; ++i) {
        delay.Observe(i % 100 == 0 ? 2'400'000 : 1'000'000 + ((i % 5) * 200'000));
    }
}

} // namespace

TEST_CASE("D starts at the start value with nothing observed") {
    core::DisplayDelay delay;
    CHECK(delay.Current() == core::DISPLAY_DELAY_START_NS);
    CHECK(delay.Target() == core::DISPLAY_DELAY_START_NS);
    static_cast<void>(delay.Advance(START_NS));
    CHECK(delay.Advance(START_NS + core::NS_PER_SECOND) == core::DISPLAY_DELAY_START_NS);
    CHECK(delay.Current() == core::DISPLAY_DELAY_START_NS);
}

TEST_CASE("steady lateness brings D to the 99th percentile plus the margin") {
    core::DisplayDelay delay;
    ObserveSteady(delay);
    CHECK(delay.Target() == (2'400'000 + core::DISPLAY_DELAY_MARGIN_NS));
    u64 now = START_NS;
    static_cast<void>(delay.Advance(now));
    for (i32 i = 0; i < 333; ++i) {
        now += 6 * ONE_MS;
        static_cast<void>(delay.Advance(now));
    }
    CHECK(delay.Current() == delay.Target());
}

TEST_CASE("D falls at most 2% of the time passed") {
    core::DisplayDelay delay;
    ObserveSteady(delay);
    static_cast<void>(delay.Advance(START_NS));
    CHECK(delay.Advance(START_NS + (100 * ONE_MS)) == (10 * ONE_MS) - (2 * ONE_MS));
}

TEST_CASE("D rises at most 10% of the time passed") {
    core::DisplayDelay delay;
    ObserveSteady(delay);
    u64 now = START_NS;
    static_cast<void>(delay.Advance(now));
    now += 2 * core::NS_PER_SECOND; // long enough to settle at the target
    static_cast<void>(delay.Advance(now));
    REQUIRE(delay.Current() == delay.Target());
    for (i32 i = 0; i < 60; ++i) {
        delay.Observe(8 * ONE_MS);
    }
    CHECK(delay.Target() == 9 * ONE_MS);
    const u64 before = delay.Current();
    CHECK(delay.Advance(now + (10 * ONE_MS)) == before + ONE_MS);
}

TEST_CASE("D doesn't fall before a second of samples") {
    core::DisplayDelay delay;
    for (std::size_t i = 0; i < core::DISPLAY_DELAY_SETTLED - 1; ++i) {
        delay.Observe(0);
    }
    CHECK(delay.Target() == core::DISPLAY_DELAY_FLOOR_NS);
    static_cast<void>(delay.Advance(START_NS));
    CHECK(delay.Advance(START_NS + core::NS_PER_SECOND) == core::DISPLAY_DELAY_START_NS);
    delay.Observe(0);
    CHECK(delay.Advance(START_NS + (2 * core::NS_PER_SECOND)) < core::DISPLAY_DELAY_START_NS);
}

TEST_CASE("floor and ceiling hold") {
    core::DisplayDelay early;
    for (std::size_t i = 0; i < core::DISPLAY_DELAY_WINDOW; ++i) {
        early.Observe(0);
    }
    CHECK(early.Target() == core::DISPLAY_DELAY_FLOOR_NS);

    core::DisplayDelay late;
    for (std::size_t i = 0; i < core::DISPLAY_DELAY_WINDOW; ++i) {
        late.Observe(30 * ONE_MS);
    }
    CHECK(late.Target() == core::TICK_NS);
}

TEST_CASE("the first advance and a repeated time leave D where it is") {
    core::DisplayDelay delay;
    ObserveSteady(delay);
    CHECK(delay.Advance(START_NS) == core::DISPLAY_DELAY_START_NS);
    CHECK(delay.Advance(START_NS) == core::DISPLAY_DELAY_START_NS);
    CHECK(delay.Advance(START_NS - ONE_MS) == core::DISPLAY_DELAY_START_NS);
}

TEST_CASE("a long gap moves D to its target, never past it") {
    core::DisplayDelay delay;
    ObserveSteady(delay);
    static_cast<void>(delay.Advance(START_NS));
    CHECK(delay.Advance(START_NS + (10 * core::NS_PER_SECOND)) == 3'400'000);
}

TEST_CASE("Lowest and Highest cover the range D took") {
    core::DisplayDelay delay;
    CHECK(delay.Lowest() == core::DISPLAY_DELAY_START_NS);
    CHECK(delay.Highest() == core::DISPLAY_DELAY_START_NS);
    ObserveSteady(delay);
    static_cast<void>(delay.Advance(START_NS));
    static_cast<void>(delay.Advance(START_NS + core::NS_PER_SECOND));
    CHECK(delay.Lowest() == 3'400'000);
    CHECK(delay.Highest() == core::DISPLAY_DELAY_START_NS);
    for (std::size_t i = 0; i < core::DISPLAY_DELAY_WINDOW; ++i) {
        delay.Observe(20 * ONE_MS);
    }
    static_cast<void>(delay.Advance(START_NS + (2 * core::NS_PER_SECOND)));
    CHECK(delay.Highest() > core::DISPLAY_DELAY_START_NS);
    CHECK(delay.Lowest() == 3'400'000);
}
