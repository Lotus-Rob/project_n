#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/tick_clock.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 START_NS = 5 * core::NS_PER_SECOND;
constexpr u64 DELAY_NS = core::DEFAULT_DISPLAY_DELAY_NS;

TEST_CASE("the schedule doesn't drift: 60 ticks take exactly one second") {
    CHECK(core::TickTime(START_NS, core::TICKS_PER_SECOND) == START_NS + core::NS_PER_SECOND);
}

TEST_CASE("no tick is due before the start") {
    core::TickClock clock{ START_NS };
    CHECK(clock.Advance(START_NS - 1).ticks == 0);
}

TEST_CASE("one tick is due per slot of the schedule") {
    core::TickClock clock{ START_NS };
    const core::TickBurst first = clock.Advance(START_NS);
    CHECK(first.ticks == 1);
    CHECK(first.scheduledNs == START_NS);

    const u64 next = core::TickTime(START_NS, 1);
    CHECK(clock.NextNs() == next);
    CHECK(clock.Advance(next - 1).ticks == 0);
    CHECK(clock.Advance(next).ticks == 1);
}

TEST_CASE("a late clock catches up the ticks it missed") {
    core::TickClock clock{ START_NS };
    static_cast<void>(clock.Advance(START_NS));

    const core::TickBurst burst = clock.Advance(core::TickTime(START_NS, 3) + 1);
    CHECK(burst.ticks == 3);
    CHECK(burst.droppedTicks == 0);
    CHECK(burst.scheduledNs == core::TickTime(START_NS, 3));
    CHECK(clock.NextNs() == core::TickTime(START_NS, 4));
}

TEST_CASE("after a long hitch, at most 3 ticks run and wall time is dropped") {
    core::TickClock clock{ START_NS };
    const u64 now = START_NS + core::NS_PER_SECOND;

    const core::TickBurst burst = clock.Advance(now);
    CHECK(burst.ticks == core::MAX_CATCH_UP);
    CHECK(burst.droppedTicks == core::TICKS_PER_SECOND + 1 - core::MAX_CATCH_UP);
    CHECK(burst.scheduledNs == now);
    CHECK(clock.NextNs() == now - core::TickTime(0, 2) + core::TickTime(0, 3));
}

TEST_CASE("ticks late for more than a second count as overload, until the clock catches up") {
    core::TickClock clock{ START_NS };
    u64 now = START_NS + core::TICK_NS + 1; // two ticks due at once
    static_cast<void>(clock.Advance(now));
    while (now <= START_NS + core::TICK_NS + 1 + core::OVERLOAD_NS) {
        now = clock.NextNs() + core::TICK_NS + 1; // always one tick behind
        static_cast<void>(clock.Advance(now));
    }
    CHECK(clock.Overloaded(now));

    now = clock.NextNs();
    static_cast<void>(clock.Advance(now));
    CHECK_FALSE(clock.Overloaded(now));
}

TEST_CASE("a restart after an expected stall neither catches up nor counts overload") {
    core::TickClock clock{ START_NS };
    static_cast<void>(clock.Advance(START_NS));
    const u64 afterStall = START_NS + (2 * core::NS_PER_SECOND);

    clock.Restart(afterStall);
    const core::TickBurst burst = clock.Advance(afterStall);
    CHECK(burst.ticks == 1);
    CHECK(burst.droppedTicks == 0);
    CHECK_FALSE(clock.Overloaded(afterStall));
}

TEST_CASE("a frozen tick counts a wall tick only, unless it changes the state") {
    core::TickCounters counters;
    counters.Count(false, false);
    counters.Count(true, false);
    counters.Count(true, true);
    CHECK(counters.wall == 3);
    CHECK(counters.simulation == 1);
    CHECK(counters.state == 2);
}

TEST_CASE("a snapshot is due exactly the display delay after its scheduled time") {
    CHECK_FALSE(core::IsDue(START_NS, START_NS + DELAY_NS - 1, DELAY_NS));
    CHECK(core::IsDue(START_NS, START_NS + DELAY_NS, DELAY_NS));
}

TEST_CASE("alpha runs from 0 to 1 over the tick after the display delay") {
    CHECK(core::Alpha(START_NS, START_NS, DELAY_NS, true) == 0.0);
    constexpr f64 HALF = 0.5;
    CHECK(core::Alpha(START_NS, START_NS + DELAY_NS + (core::TICK_NS / 2), DELAY_NS, true) == doctest::Approx(HALF));
    CHECK(core::Alpha(START_NS, START_NS + DELAY_NS + core::TICK_NS + 1, DELAY_NS, true) == 1.0);
}

TEST_CASE("a snapshot the simulation didn't advance shows its current transforms") {
    CHECK(core::Alpha(START_NS, START_NS, DELAY_NS, false) == 1.0);
}

TEST_CASE("the render clock runs a tick behind the snapshot and holds while frozen") {
    constexpr u64 TICK = 10;
    constexpr f64 ALPHA = 0.25;
    CHECK(core::RenderTick(TICK, ALPHA) == doctest::Approx(TICK - 1 + ALPHA));
    CHECK(core::RenderTick(TICK, 1.0) == doctest::Approx(TICK));
}

} // namespace
