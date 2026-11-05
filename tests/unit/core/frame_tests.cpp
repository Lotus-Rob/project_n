#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 SCHEDULED_NS = 1'000'000'000;
constexpr u64 BEFORE_ACQUIRE_NS = SCHEDULED_NS + core::DEFAULT_DISPLAY_DELAY_NS; // alpha 0
constexpr u64 AFTER_ACQUIRE_NS = BEFORE_ACQUIRE_NS + (core::TICK_NS / 2);
constexpr f64 AFTER_ACQUIRE_ALPHA = 0.5;

TEST_CASE("a frame reads the time for alpha after the swapchain acquire") {
    const core::Snapshot drawn{ .simulationTick = 1, .scheduledNs = SCHEDULED_NS, .advanced = true };
    std::vector<core::Event> pending;
    u64 nowNs = BEFORE_ACQUIRE_NS;
    f64 drawnAlpha = -1.0;
    core::PresentFrame(
        drawn,
        pending,
        true,
        [&nowNs] { nowNs = AFTER_ACQUIRE_NS; }, // the acquire blocked for half a tick
        [&nowNs] { return nowNs; },
        [](const core::Event& /*event*/) {},
        [&drawnAlpha](f64 alpha) { drawnAlpha = alpha; }
    );
    CHECK(drawnAlpha == doctest::Approx(AFTER_ACQUIRE_ALPHA));
}

TEST_CASE("a hidden frame acquires and draws nothing but still plays the events due") {
    const core::Snapshot drawn{ .simulationTick = 1 }; // not advanced: alpha 1, the render clock at tick 1
    std::vector<core::Event> pending{ { .tick = 1 } };
    bool acquired = false;
    bool drew = false;
    u64 played = 0;
    core::PresentFrame(
        drawn,
        pending,
        false,
        [&acquired] { acquired = true; },
        [] { return SCHEDULED_NS; },
        [&played](const core::Event& /*event*/) { ++played; },
        [&drew](f64 /*alpha*/) { drew = true; }
    );
    CHECK_FALSE(acquired);
    CHECK_FALSE(drew);
    CHECK(played == 1);
    CHECK(pending.empty());
}

} // namespace
