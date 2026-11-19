#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 ONE_MS = core::NS_PER_MS;
constexpr u64 DELAY_NS = core::DISPLAY_DELAY_START_NS; // the delay until enough lateness is seen for it to fall
constexpr u64 FIRST_NS = 1'000'000'000;                // the first snapshot's scheduled time
constexpr u64 SECOND_NS = FIRST_NS + core::TICK_NS;    // the next tick's

/// One frame's clock and window: the clock reads `beforeNs` until the acquire returns, then `afterNs` (the acquire's
/// wait); `afterNs` 0 means the acquire doesn't wait. A nonzero `publishedInAcquire` is the tick of a snapshot the logic
/// publishes while the acquire waits, with Screen::events.
struct Frame {
    u64 beforeNs = 0;
    u64 afterNs = 0;
    bool visible = true;
    bool prepared = true;
    u64 publishedInAcquire = 0;
};

/// What one PresentFrame() call did.
struct Outcome {
    bool presented = false;
    u32 acquires = 0;
    u32 clockReads = 0;
    /// Whether a snapshot was already chosen (GameDisplay::Drawn()) at every acquire.
    bool chosenBeforeAcquire = true;
    /// The simulation tick of the snapshot drawn, if one was.
    std::optional<u64> drawnTick = std::nullopt;
    f64 alpha = -1.0;
    std::vector<u64> played;
};

/// The main thread's side of a frame, on a fake clock: the handover, the display and the delay.
struct Screen {
    core::GameHandover handover;
    core::GameDisplay display;
    core::DisplayDelay delay;
    std::vector<core::Event> pending;
    std::vector<core::Event> events; // the next Publish() hands these over

    /// Publishes a snapshot of `tick`, scheduled at `scheduledNs` and published `latenessNs` after it, with `events`.
    void Publish(u64 tick, u64 scheduledNs, u64 latenessNs = 0, bool advanced = true) {
        core::Snapshot& snapshot = handover.Writing();
        snapshot.simulationTick = tick;
        snapshot.scheduledNs = scheduledNs;
        snapshot.publishedNs = scheduledNs + latenessNs;
        snapshot.advanced = advanced;
        handover.Publish(events);
    }

    Outcome Present(const Frame& frame) {
        Outcome outcome;
        u64 nowNs = frame.beforeNs;
        outcome.presented = core::PresentFrame(
            display,
            handover,
            delay,
            pending,
            frame.visible,
            [&] {
                ++outcome.acquires;
                outcome.chosenBeforeAcquire = outcome.chosenBeforeAcquire && display.Drawn() != nullptr;
                nowNs = std::max(nowNs, frame.afterNs);
                if (frame.publishedInAcquire != 0) {
                    Publish(frame.publishedInAcquire, core::TickTime(FIRST_NS, frame.publishedInAcquire - 1));
                }
            },
            [&] {
                ++outcome.clockReads;
                return nowNs;
            },
            [&frame](const core::Snapshot& /*drawn*/) { return frame.prepared; },
            [&outcome](const core::Event& event) { outcome.played.push_back(event.tick); },
            [&outcome](const core::Snapshot& drawn, f64 alpha) {
                outcome.drawnTick = drawn.simulationTick;
                outcome.alpha = alpha;
            }
        );
        return outcome;
    }
};

TEST_CASE("a snapshot that becomes due during the acquire is the one drawn") {
    Screen screen;
    screen.Publish(1, FIRST_NS);
    static_cast<void>(screen.Present({ .beforeNs = FIRST_NS + DELAY_NS }));
    screen.Publish(2, SECOND_NS);
    REQUIRE(screen.Present({ .beforeNs = SECOND_NS + ONE_MS }).drawnTick == 1); // the second held, not yet due

    // Due 2 ms after the frame starts, while the acquire waits 3.5 ms.
    const Outcome outcome =
        screen.Present({ .beforeNs = SECOND_NS + DELAY_NS - (2 * ONE_MS), .afterNs = SECOND_NS + DELAY_NS + (3 * ONE_MS / 2) });
    CHECK(outcome.acquires == 1);
    CHECK(outcome.drawnTick == 2);
    CHECK(outcome.alpha == doctest::Approx(0.09)); // 1.5 ms into a 16.7 ms tick
}

TEST_CASE("a frame drains the events of every snapshot it takes, even one published during its acquire") {
    Screen screen;
    screen.Publish(1, FIRST_NS);
    static_cast<void>(screen.Present({ .beforeNs = FIRST_NS + DELAY_NS }));
    screen.events = { { .tick = 2 } };

    const Outcome taking = screen.Present(
        { .beforeNs = SECOND_NS, .afterNs = SECOND_NS + DELAY_NS + (core::TICK_NS / 2), .publishedInAcquire = 2 }
    );
    CHECK(taking.drawnTick == 2);
    CHECK(taking.played.empty()); // the render clock at tick 1.5
    REQUIRE(screen.pending.size() == 1);
    CHECK(screen.pending[0].tick == 2);
    std::vector<core::Event> undrained;
    screen.handover.TakeEvents(undrained);
    CHECK(undrained.empty());

    const Outcome later = screen.Present({ .beforeNs = SECOND_NS + DELAY_NS + core::TICK_NS + 1 });
    CHECK(later.played == std::vector<u64>{ 2 });
}

TEST_CASE("a frame reads the time for alpha after the swapchain acquire") {
    Screen screen;
    screen.Publish(1, FIRST_NS);
    static_cast<void>(screen.Present({ .beforeNs = FIRST_NS + DELAY_NS })); // alpha 0
    const Outcome outcome =
        screen.Present({ .beforeNs = FIRST_NS + DELAY_NS, .afterNs = FIRST_NS + DELAY_NS + (core::TICK_NS / 2) });
    CHECK(outcome.acquires == 1);
    CHECK(outcome.alpha == doctest::Approx(0.5));
    CHECK(outcome.clockReads == 1);
}

TEST_CASE("a frame before any snapshot is due acquires nothing") {
    Screen screen;
    const Outcome empty = screen.Present({ .beforeNs = FIRST_NS });
    CHECK(empty.presented);
    CHECK(empty.acquires == 0);
    CHECK_FALSE(empty.drawnTick);

    screen.Publish(1, FIRST_NS);
    const Outcome early = screen.Present({ .beforeNs = FIRST_NS + DELAY_NS - 1 });
    CHECK(early.presented);
    CHECK(early.acquires == 0);
    CHECK_FALSE(early.drawnTick);
}

TEST_CASE("the first snapshot is acquired for after it is chosen") {
    Screen screen;
    screen.Publish(1, FIRST_NS);
    const Outcome outcome = screen.Present({ .beforeNs = FIRST_NS + DELAY_NS });
    CHECK(outcome.presented);
    CHECK(outcome.acquires == 1);
    CHECK(outcome.chosenBeforeAcquire);
    CHECK(outcome.drawnTick == 1);
}

TEST_CASE("a hidden frame acquires nothing and still adopts and plays") {
    Screen screen;
    screen.Publish(1, FIRST_NS);
    static_cast<void>(screen.Present({ .beforeNs = FIRST_NS + DELAY_NS }));
    screen.Publish(2, SECOND_NS);
    static_cast<void>(screen.Present({ .beforeNs = SECOND_NS + ONE_MS })); // the second held, not yet due
    screen.pending = { { .tick = 1 }, { .tick = 2 } };

    const Outcome outcome = screen.Present({ .beforeNs = SECOND_NS + DELAY_NS + (core::TICK_NS / 2), .visible = false });
    CHECK(outcome.presented);
    CHECK(outcome.acquires == 0);
    CHECK_FALSE(outcome.drawnTick);
    REQUIRE(screen.display.Drawn() != nullptr);
    CHECK(screen.display.Drawn()->simulationTick == 2);
    CHECK(outcome.played == std::vector<u64>{ 1 }); // the render clock at tick 1.5
    CHECK(screen.pending.size() == 1);
}

TEST_CASE("a hidden window's frames still move the delay") {
    Screen screen;
    screen.Publish(1, FIRST_NS, 12 * ONE_MS); // 2 ms later than the delay: it rises toward 13 ms
    static_cast<void>(screen.Present({ .beforeNs = FIRST_NS + (12 * ONE_MS), .visible = false }));
    const Outcome outcome = screen.Present({ .beforeNs = FIRST_NS + (22 * ONE_MS), .visible = false });
    CHECK(outcome.acquires == 0);
    CHECK(screen.delay.Current() == 11 * ONE_MS); // a tenth of the 10 ms since the last frame
}

TEST_CASE("a failed prepare draws nothing") {
    Screen screen;
    screen.Publish(1, FIRST_NS);
    const Outcome outcome = screen.Present({ .beforeNs = FIRST_NS + DELAY_NS, .prepared = false });
    CHECK_FALSE(outcome.presented);
    CHECK(outcome.acquires == 0); // nothing was acquired that a draw doesn't follow
    CHECK_FALSE(outcome.drawnTick);
}

TEST_CASE("a failed prepare once a snapshot is drawn leaves its acquire undrawn") {
    Screen screen;
    screen.Publish(1, FIRST_NS);
    static_cast<void>(screen.Present({ .beforeNs = FIRST_NS + DELAY_NS }));
    const Outcome outcome = screen.Present({ .beforeNs = FIRST_NS + DELAY_NS + ONE_MS, .prepared = false });
    CHECK_FALSE(outcome.presented);
    CHECK(outcome.acquires == 1); // the acquire came before the choice, so the caller must present no more
    CHECK_FALSE(outcome.drawnTick);
}

TEST_CASE("the delay sees each taken snapshot's lateness") {
    Screen screen;
    screen.Publish(1, FIRST_NS, 3 * ONE_MS);
    static_cast<void>(screen.Present({ .beforeNs = FIRST_NS + (4 * ONE_MS) })); // taken, not yet due
    CHECK(screen.delay.Target() == (3 * ONE_MS) + core::DISPLAY_DELAY_MARGIN_NS);
}

TEST_CASE("a frozen snapshot's lateness doesn't move the delay") {
    Screen screen;
    screen.Publish(1, FIRST_NS, 3 * ONE_MS, false); // a stall's snapshot: published once the stall ended
    static_cast<void>(screen.Present({ .beforeNs = FIRST_NS + DELAY_NS }));
    CHECK(screen.delay.Target() == core::DISPLAY_DELAY_START_NS);
}

TEST_CASE("a frame judges and interpolates by the delay at its clock reading") {
    Screen screen;
    screen.delay.Observe(15 * ONE_MS); // the delay rises toward 16 ms, a tenth of the time passed
    static_cast<void>(screen.delay.Advance(FIRST_NS));
    screen.Publish(1, FIRST_NS);

    // The delay is 11.1 ms by then: not due, though it would be at the starting 10 ms.
    CHECK_FALSE(screen.Present({ .beforeNs = FIRST_NS + (11 * ONE_MS) }).drawnTick);
    // 11.3 ms by then: due, 1.7 ms into its tick.
    const Outcome outcome = screen.Present({ .beforeNs = FIRST_NS + (13 * ONE_MS) });
    CHECK(outcome.drawnTick == 1);
    CHECK(outcome.alpha == doctest::Approx(0.102));
}

} // namespace
