#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/handover.hpp"

#include <doctest/doctest.h>

namespace {

struct State {
    u64 sequence = 0;
    u64 scheduledNs = 0;
    u64 publishedNs = 0;
};

struct Event {
    u64 tick = 0;
    u64 epoch = 0;
    bool cosmetic = false;
};

using Handover = core::Handover<State, Event, u64>;
using Display = core::Display<State, Event, u64>;

constexpr u64 HANDOVER_DELAY_NS = 10;
constexpr u64 FIRST_NS = 100; // scheduled times
constexpr u64 SECOND_NS = 200;
constexpr u64 LATE_NS = 500;

// Published at its scheduled time unless `publishedNs` says otherwise.
void Publish(Handover& handover, u64 sequence, u64 scheduledNs, u64 publishedNs = 0) {
    handover.Writing() = { .sequence = sequence,
                           .scheduledNs = scheduledNs,
                           .publishedNs = std::max(publishedNs, scheduledNs) };
    std::vector<Event> none;
    handover.Publish(none);
}

TEST_CASE("the state channel hands over only the newest snapshot") {
    Handover handover;
    Publish(handover, 1, 0);
    Publish(handover, 2, 0);

    const State* taken = handover.TakeState();
    REQUIRE(taken != nullptr);
    CHECK(taken->sequence == 2);
    CHECK(handover.TakeState() == nullptr);
}

TEST_CASE("events accumulate until taken, in order") {
    Handover handover;
    std::vector<Event> events{ { .tick = 1 }, { .tick = 2 } };
    handover.Publish(events);
    CHECK(events.empty());
    events = { { .tick = 3 } };
    handover.Publish(events);

    std::vector<Event> taken;
    handover.TakeEvents(taken);
    REQUIRE(taken.size() == 3);
    CHECK(taken[0].tick == 1);
    CHECK(taken[2].tick == 3);
    handover.TakeEvents(taken);
    CHECK(taken.empty());
}

TEST_CASE("input accumulates until taken, in order") {
    Handover handover;
    handover.SendInput(1);
    handover.SendInput(2);

    std::vector<u64> taken;
    handover.TakeInput(taken);
    CHECK(taken == std::vector<u64>{ 1, 2 });
    handover.TakeInput(taken);
    CHECK(taken.empty());
}

TEST_CASE("nothing is drawn until the first snapshot is due") {
    Handover handover;
    Display display;
    Publish(handover, 1, FIRST_NS);

    CHECK_FALSE(display.Update(handover, FIRST_NS + HANDOVER_DELAY_NS - 1, HANDOVER_DELAY_NS));
    CHECK(display.Drawn() == nullptr);
    CHECK(display.Update(handover, FIRST_NS + HANDOVER_DELAY_NS, HANDOVER_DELAY_NS));
    REQUIRE(display.Drawn() != nullptr);
    CHECK(display.Drawn()->sequence == 1);
    CHECK(display.LateAdoptions() == 0);
}

TEST_CASE("a newer snapshot doesn't displace a held one that is due") {
    Handover handover;
    Display display;
    Publish(handover, 1, FIRST_NS);
    static_cast<void>(display.Update(handover, FIRST_NS, HANDOVER_DELAY_NS)); // taken, not due
    Publish(handover, 2, SECOND_NS);

    CHECK(display.Update(handover, FIRST_NS + HANDOVER_DELAY_NS, HANDOVER_DELAY_NS));
    CHECK(display.Drawn()->sequence == 1);
    CHECK(display.Update(handover, SECOND_NS + HANDOVER_DELAY_NS, HANDOVER_DELAY_NS));
    CHECK(display.Drawn()->sequence == 2);
}

TEST_CASE("a snapshot published after it was due counts as late") {
    Handover handover;
    Display display;
    Publish(handover, 1, FIRST_NS, LATE_NS);

    CHECK(display.Update(handover, LATE_NS, HANDOVER_DELAY_NS));
    CHECK(display.LateAdoptions() == 1);
}

TEST_CASE("a snapshot published on time but taken late doesn't count as late") {
    Handover handover;
    Display display;
    Publish(handover, 1, FIRST_NS);

    CHECK(display.Update(handover, LATE_NS, HANDOVER_DELAY_NS));
    CHECK(display.LateAdoptions() == 0);
}

TEST_CASE("buffers return to the handover, so publishing never runs out") {
    Handover handover;
    Display display;
    for (u64 sequence = 1; sequence <= 2 * Handover::BUFFERS; ++sequence) {
        Publish(handover, sequence, sequence * FIRST_NS);
        static_cast<void>(display.Update(handover, (sequence * FIRST_NS) + HANDOVER_DELAY_NS, HANDOVER_DELAY_NS));
    }
    CHECK(display.Drawn()->sequence == 2 * Handover::BUFFERS);
}

std::vector<u64> Played(std::vector<Event>& pending, u64 epoch, f64 renderTick) {
    std::vector<u64> played;
    core::PlayDue(pending, epoch, renderTick, [&played](const Event& event) { played.push_back(event.tick); });
    return played;
}

TEST_CASE("events play in order once the render clock reaches their tick") {
    constexpr u64 TICK = 4;
    constexpr f64 RENDER_TICK = TICK + 1.5;
    std::vector<Event> pending{ { .tick = TICK }, { .tick = TICK + 1 }, { .tick = TICK + 2 } };
    CHECK(Played(pending, 0, RENDER_TICK) == std::vector<u64>{ TICK, TICK + 1 });
    REQUIRE(pending.size() == 1);
    CHECK(pending[0].tick == TICK + 2);
}

TEST_CASE("events of an older epoch are dropped; a newer epoch's wait for its snapshot") {
    std::vector<Event> pending{ { .tick = 1, .epoch = 1 }, { .tick = 1, .epoch = 3 } };
    CHECK(Played(pending, 2, 100.0).empty());
    REQUIRE(pending.size() == 1);
    CHECK(pending[0].epoch == 3);
}

TEST_CASE("cosmetic events far behind the render clock are dropped; others still play") {
    std::vector<Event> pending{ { .tick = 1, .cosmetic = true }, { .tick = 2 } };
    constexpr f64 RENDER_TICK = 2.0 + core::STALE_EVENT_TICKS;
    CHECK(Played(pending, 0, RENDER_TICK) == std::vector<u64>{ 2 });
    CHECK(pending.empty());
}

struct NamedEvent {
    u64 tick = 0;
    u64 epoch = 0;
    bool cosmetic = false;
    std::string name = "unnamed";
};

TEST_CASE("events kept for a later tick keep their contents") {
    constexpr u64 LATER = 9;
    constexpr f64 RENDER_TICK = 1.0;
    std::vector<NamedEvent> pending{ { .tick = LATER, .name = "a sound that waits" } };
    core::PlayDue(pending, 0, RENDER_TICK, [](const NamedEvent& /*event*/) {});
    REQUIRE(pending.size() == 1);
    CHECK(pending[0].name == "a sound that waits");
}

// One writer, one reader, many ticks: every event delivered once and in order, snapshots in order.
// Run under ThreadSanitizer by `check --sanitizers`.
TEST_CASE("the handover is safe between a logic and a main thread") {
    constexpr u64 TICKS = 20'000;
    constexpr u64 EVENTS_PER_TICK = 3;
    constexpr u64 INPUTS = 5'000;
    Handover handover;
    std::vector<u64> inputs;

    std::jthread logic{ [&handover, &inputs] {
        std::vector<Event> events;
        std::vector<u64> taken;
        for (u64 tick = 1; tick <= TICKS; ++tick) {
            for (u64 i = 0; i < EVENTS_PER_TICK; ++i) {
                events.push_back({ .tick = ((tick - 1) * EVENTS_PER_TICK) + i });
            }
            handover.Writing() = { .sequence = tick, .scheduledNs = tick };
            handover.Publish(events);
            handover.TakeInput(taken);
            inputs.insert(inputs.end(), taken.begin(), taken.end());
        }
        while (inputs.size() < INPUTS) {
            handover.TakeInput(taken);
            inputs.insert(inputs.end(), taken.begin(), taken.end());
            std::this_thread::yield();
        }
    } };

    Display display;
    std::vector<Event> taken;
    u64 nextEvent = 0;
    u64 lastSequence = 0;
    bool inOrder = true;
    for (u64 input = 0; lastSequence < TICKS || nextEvent < TICKS * EVENTS_PER_TICK || input < INPUTS; ++input) {
        if (input < INPUTS) {
            handover.SendInput(input);
        }
        static_cast<void>(display.Update(handover, ~u64{ 0 } - HANDOVER_DELAY_NS, HANDOVER_DELAY_NS));
        if (const State* drawn = display.Drawn(); drawn != nullptr) {
            inOrder = inOrder && drawn->sequence >= lastSequence;
            lastSequence = drawn->sequence;
        }
        handover.TakeEvents(taken);
        for (const Event& event : taken) {
            inOrder = inOrder && event.tick == nextEvent;
            ++nextEvent;
        }
    }
    logic.join();

    CHECK(inOrder);
    CHECK(nextEvent == TICKS * EVENTS_PER_TICK);
    REQUIRE(inputs.size() == INPUTS);
    for (u64 i = 0; i < INPUTS; ++i) {
        CHECK(inputs[i] == i);
    }
}

} // namespace
