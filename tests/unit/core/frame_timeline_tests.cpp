#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/display_delay.hpp"
#include "core/frame.hpp"
#include "core/tick_clock.hpp"
#include "core/vec2.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 ONE_MS = core::NS_PER_MS;
constexpr u64 ORIGIN_NS = core::NS_PER_SECOND; // tick 0's scheduled time, and the first frame's start by default
constexpr f32 STRIDE = 4.0F;                   // world units a walking tick moves the followed point
constexpr u64 TICKS_PER_SECOND = core::TICKS_PER_SECOND;

/// A move held down: the logic walks from the first tick scheduled at or after `downNs` and stands again from the first
/// at or after `upNs`.
struct Press {
    u64 downNs = 0;
    u64 upNs = 0;
};

/// How long after its scheduled time tick `tick`'s snapshot is published; under a tick.
using Lateness = u64 (*)(u64 tick);

/// What a timeline runs: frames every `framePeriodNs` from `firstFrameNs`, each acquire waiting `acquireWaitNs`;
/// ticks every TICK_NS from ORIGIN_NS, published `lateness(tick)` late; and the presses. `oldOrder` runs each frame in
/// the order that picked the snapshot before the acquire, from the same primitives.
struct Schedule {
    u64 framePeriodNs = 0;
    u64 acquireWaitNs = 0;
    u64 firstFrameNs = ORIGIN_NS;
    Lateness lateness = nullptr;
    // NOLINTNEXTLINE(readability-redundant-member-init): designated initializers omit it; Clang warns without one
    std::vector<Press> presses{};
    bool oldOrder = false;
};

/// One drawn frame: the clock reading its alpha came from, what it showed and D at that reading.
struct DrawnFrame {
    u64 t = 0;
    u64 tick = 0;
    f64 alpha = 0.0;
    core::Vec2 centre{};
    u64 delayNs = 0;
};

/// A single-threaded fake clock that drives the real handover, display, delay and PresentFrame() through a Schedule:
/// every snapshot is published before any frame reads the clock at or after its publish time.
class Timeline {
public:
    explicit Timeline(Schedule schedule)
      : m_schedule{ std::move(schedule) }
      , m_nextFrameNs{ m_schedule.firstFrameNs } {
    }

    /// Runs every frame that starts before `endNs`, from where the last call stopped.
    void RunUntil(u64 endNs) {
        while (m_nextFrameNs < endNs) {
            AdvanceTo(m_nextFrameNs);
            if (m_schedule.oldOrder) {
                OldFrame();
            }
            else {
                Frame();
            }
            m_nextFrameNs += m_schedule.framePeriodNs;
        }
    }

    /// Every frame drawn so far, in order.
    [[nodiscard]] const std::vector<DrawnFrame>& Draws() const {
        return m_draws;
    }

    [[nodiscard]] const core::DisplayDelay& Delay() const {
        return m_delay;
    }

    [[nodiscard]] u64 LateAdoptions() const {
        return m_display.LateAdoptions();
    }

private:
    void Frame() {
        static_cast<void>(core::PresentFrame(
            m_display,
            m_handover,
            m_delay,
            m_pending,
            true,
            [this] { AdvanceTo(m_nowNs + m_schedule.acquireWaitNs); },
            [this] {
                m_readNs = m_nowNs;
                return m_nowNs;
            },
            [](const core::Snapshot& /*drawn*/) { return true; },
            [](const core::Event& /*event*/) {},
            [this](const core::Snapshot& drawn, f64 alpha) { Record(drawn, alpha, m_delay.Current()); }
        ));
    }

    void OldFrame() {
        const u64 delayNs = m_delay.Advance(m_nowNs);
        static_cast<void>(m_display.Update(m_handover, m_nowNs, delayNs));
        if (const std::optional<u64> lateness = m_display.TakenLateness()) {
            m_delay.Observe(*lateness);
        }
        const core::Snapshot* drawn = m_display.Drawn();
        if (drawn == nullptr) {
            return;
        }
        AdvanceTo(m_nowNs + m_schedule.acquireWaitNs);
        m_readNs = m_nowNs;
        Record(*drawn, core::Alpha(drawn->scheduledNs, m_nowNs, delayNs, drawn->advanced), delayNs);
    }

    void AdvanceTo(u64 nowNs) {
        m_nowNs = nowNs;
        while (core::TickTime(ORIGIN_NS, m_nextTick) + m_schedule.lateness(m_nextTick) <= m_nowNs) {
            Publish(m_nextTick++);
        }
    }

    void Publish(u64 tick) {
        const u64 scheduledNs = core::TickTime(ORIGIN_NS, tick);
        const bool walking = std::ranges::any_of(m_schedule.presses, [scheduledNs](const Press& press) {
            return press.downNs <= scheduledNs && scheduledNs < press.upNs;
        });
        m_follow.previous = m_follow.current;
        if (walking) {
            m_follow.current.x += STRIDE;
        }
        core::Snapshot& snapshot = m_handover.Writing();
        snapshot.simulationTick = tick;
        snapshot.scheduledNs = scheduledNs;
        snapshot.publishedNs = scheduledNs + m_schedule.lateness(tick);
        snapshot.advanced = true;
        snapshot.follow = m_follow;
        std::vector<core::Event> none;
        m_handover.Publish(none);
    }

    void Record(const core::Snapshot& drawn, f64 alpha, u64 delayNs) {
        if (!drawn.follow) {
            FAIL("a drawn snapshot follows nothing");
            return;
        }
        const core::Follow& follow = *drawn.follow;
        // Interpolated as the app's FollowedCentre() does, which this module's tests don't link.
        m_draws.push_back(
            { .t = m_readNs,
              .tick = drawn.simulationTick,
              .alpha = alpha,
              .centre = follow.previous + ((follow.current - follow.previous) * static_cast<f32>(alpha)),
              .delayNs = delayNs }
        );
    }

    Schedule m_schedule;
    core::GameHandover m_handover;
    core::GameDisplay m_display;
    core::DisplayDelay m_delay;
    std::vector<core::Event> m_pending;
    core::Follow m_follow{}; // starts at the origin, where any movement at all shows in a float
    std::vector<DrawnFrame> m_draws;
    u64 m_nowNs = 0;
    u64 m_readNs = 0; // the clock reading the frame's alpha came from: the first snapshot is acquired for after it
    u64 m_nextFrameNs = 0;
    u64 m_nextTick = 1;
};

/// A display's frame period and how long its acquire waits for the GPU.
struct Rate {
    std::string_view name;
    u64 periodNs = 0;
    u64 waitNs = 0;
};

/// 60 Hz and 165 Hz paced, whose acquire waits; unpaced, whose doesn't.
constexpr std::array RATES{
    Rate{ .name = "60 Hz", .periodNs = 16'670'000, .waitNs = 3'500'000 },
    Rate{ .name = "165 Hz", .periodNs = 6'060'000, .waitNs = 3'500'000 },
    Rate{ .name = "unpaced", .periodNs = 1'000'000, .waitNs = 0 },
};

constexpr u64 PHASES = 8;

/// The `phase`th of PHASES points spread over a tick, each in the middle of its share, so none is on a tick's start.
constexpr u64 PhaseNs(u64 phase) {
    return ((2 * phase) + 1) * core::TICK_NS / (2 * PHASES);
}

/// Tick plus publish times like the dev machine's, cycling 1.0 to 2.0 ms in tenths: 99th percentile 2.0 ms.
u64 Steady(u64 tick) {
    return ONE_MS + ((tick % 11) * ONE_MS / 10);
}

constexpr u64 STEADY_TARGET_NS = (2 * ONE_MS) + core::DISPLAY_DELAY_MARGIN_NS;
constexpr u64 SETTLE_TOLERANCE_NS = ONE_MS / 10;

/// The render time, in ticks: what every picture follows.
f64 RenderTime(const DrawnFrame& frame) {
    return core::RenderTick(frame.tick, frame.alpha);
}

/// Ticks in `spanNs`, exactly (TICK_NS is rounded down).
f64 Ticks(u64 spanNs) {
    return static_cast<f64>(spanNs) * static_cast<f64>(TICKS_PER_SECOND) / static_cast<f64>(core::NS_PER_SECOND);
}

/// How the render time rose between consecutive draws, against the time that passed (1 runs it at the clock's rate):
/// the extremes over a run, and where the worst pair ended.
struct Factors {
    f64 lowest = 1.0;
    f64 highest = 1.0;
    u64 phaseNs = 0;
    u64 worstAtNs = 0;
};

constexpr f64 LOWEST_FACTOR = 1.0 - core::DISPLAY_DELAY_RISE_RATE - 0.01;
constexpr f64 HIGHEST_FACTOR = 1.0 + core::DISPLAY_DELAY_FALL_RATE + 0.01;

/// How far outside [LOWEST_FACTOR, HIGHEST_FACTOR] the factors went: 0 inside.
f64 Overshoot(const Factors& factors) {
    return std::max({ 0.0, LOWEST_FACTOR - factors.lowest, factors.highest - HIGHEST_FACTOR });
}

Factors RenderFactors(const std::vector<DrawnFrame>& draws) {
    Factors factors;
    for (std::size_t i = 1; i < draws.size(); ++i) {
        const f64 factor = (RenderTime(draws[i]) - RenderTime(draws[i - 1])) / Ticks(draws[i].t - draws[i - 1].t);
        const f64 before = Overshoot(factors);
        factors.lowest = std::min(factors.lowest, factor);
        factors.highest = std::max(factors.highest, factor);
        if (Overshoot(factors) > before) {
            factors.worstAtNs = draws[i].t;
        }
    }
    return factors;
}

/// A press's first movement on screen, against the bound: its tick's wait plus D then plus one frame period.
struct Latency {
    u64 phaseNs = 0;
    u64 latencyNs = 0;
    u64 tickWaitNs = 0;
    u64 delayNs = 0;
    i64 marginNs = 0; // the bound less the latency: negative breaks it
};

constexpr u64 PRESS_TICK = 2 * TICKS_PER_SECOND; // D has settled by then

/// The press `phaseNs` into the tick after PRESS_TICK. The main loop reads input at a frame's start, so frames run in
/// step with the press, and its phase in the tick sets the frame grid's against the ticks.
Latency PressLatency(const Rate& rate, u64 phaseNs, bool oldOrder) {
    const u64 pressNs = core::TickTime(ORIGIN_NS, PRESS_TICK) + phaseNs;
    Timeline timeline{ { .framePeriodNs = rate.periodNs,
                         .acquireWaitNs = rate.waitNs,
                         .firstFrameNs = ORIGIN_NS + ((pressNs - ORIGIN_NS) % rate.periodNs),
                         .lateness = Steady,
                         .presses = { { .downNs = pressNs, .upNs = pressNs + (400 * ONE_MS) } },
                         .oldOrder = oldOrder } };
    timeline.RunUntil(pressNs + (100 * ONE_MS));

    const u64 tickWaitNs = core::TickTime(ORIGIN_NS, PRESS_TICK + 1) - pressNs; // the phase is inside the tick
    const auto moved = std::ranges::find_if(timeline.Draws(), [pressNs](const DrawnFrame& frame) {
        return frame.t >= pressNs && frame.centre.x != 0.0F;
    });
    REQUIRE(moved != timeline.Draws().end());
    const u64 latencyNs = moved->t - pressNs;
    const u64 boundNs = tickWaitNs + moved->delayNs + rate.periodNs;
    return { .phaseNs = phaseNs,
             .latencyNs = latencyNs,
             .tickWaitNs = tickWaitNs,
             .delayNs = moved->delayNs,
             .marginNs = static_cast<i64>(boundNs) - static_cast<i64>(latencyNs) };
}

/// The phase with the smallest margin, over PHASES presses spread over a tick.
Latency WorstLatency(const Rate& rate, bool oldOrder) {
    Latency worst{ .marginNs = std::numeric_limits<i64>::max() };
    for (u64 phase = 0; phase < PHASES; ++phase) {
        const Latency latency = PressLatency(rate, PhaseNs(phase), oldOrder);
        if (latency.marginNs < worst.marginNs) {
            worst = latency;
        }
    }
    return worst;
}

TEST_CASE("a press shows no later than its tick's wait plus D plus a frame") {
    for (const Rate& rate : RATES) {
        const Latency worst = WorstLatency(rate, false);
        CAPTURE(rate.name);
        CAPTURE(worst.phaseNs);
        CAPTURE(worst.latencyNs);
        CAPTURE(worst.tickWaitNs);
        CAPTURE(worst.delayNs);
        CHECK(worst.marginNs >= 0);
    }
}

TEST_CASE("the schedules catch a snapshot picked before the acquire") {
    for (const Rate& rate : std::span{ RATES }.first(2)) { // the paced rates: unpaced frames don't wait to acquire
        CAPTURE(rate.name);
        CHECK(WorstLatency(rate, true).marginNs < 0);
    }
}

constexpr u64 SMOOTH_NS = 5 * core::NS_PER_SECOND;

/// The worst factors over PHASES frame grids spread over a tick, each run for SMOOTH_NS while D falls from its start.
Factors WorstFactors(const Rate& rate, bool oldOrder) {
    Factors worst;
    for (u64 phase = 0; phase < PHASES; ++phase) {
        Timeline timeline{ { .framePeriodNs = rate.periodNs,
                             .acquireWaitNs = rate.waitNs,
                             .firstFrameNs = ORIGIN_NS + PhaseNs(phase),
                             .lateness = Steady,
                             .oldOrder = oldOrder } };
        timeline.RunUntil(ORIGIN_NS + SMOOTH_NS);
        REQUIRE(timeline.Delay().Lowest() == STEADY_TARGET_NS); // D fell from its start all the way
        Factors factors = RenderFactors(timeline.Draws());
        factors.phaseNs = PhaseNs(phase);
        if (Overshoot(factors) >= Overshoot(worst)) {
            worst = factors;
        }
    }
    return worst;
}

TEST_CASE("the render time never goes back, holds or jumps") {
    for (const Rate& rate : RATES) {
        const Factors worst = WorstFactors(rate, false);
        CAPTURE(rate.name);
        CAPTURE(worst.phaseNs);
        CAPTURE(worst.worstAtNs);
        CHECK(worst.lowest >= LOWEST_FACTOR);
        CHECK(worst.highest <= HIGHEST_FACTOR);
    }
    CHECK(Overshoot(WorstFactors(RATES[1], true)) > 0.0); // 165 Hz in the old order
}

constexpr u64 STEADY_NS = 15 * core::NS_PER_SECOND;

TEST_CASE("steady lateness settles D, with no late adoption") {
    for (const Rate& rate : RATES) {
        Timeline timeline{ { .framePeriodNs = rate.periodNs, .acquireWaitNs = rate.waitNs, .lateness = Steady } };
        timeline.RunUntil(ORIGIN_NS + STEADY_NS);
        CAPTURE(rate.name);
        CHECK(timeline.Delay().Target() == STEADY_TARGET_NS);
        CHECK(timeline.Delay().Current() <= timeline.Delay().Target() + SETTLE_TOLERANCE_NS);
        CHECK(timeline.Delay().Current() + SETTLE_TOLERANCE_NS >= timeline.Delay().Target());
        CHECK(timeline.LateAdoptions() == 0);
    }
}

constexpr u64 SPIKE_FIRST_TICK = 15 * TICKS_PER_SECOND;
constexpr u64 SPIKE_END_TICK = SPIKE_FIRST_TICK + (2 * TICKS_PER_SECOND);
constexpr u64 SPIKE_LATENESS_NS = 6 * ONE_MS;
// The window holds the spike's latenesses for its 600 snapshots, ten seconds, before the target drops; D then takes
// a fifth of a second to fall the 4 ms at its slow rate.
constexpr u64 FALLEN_TICK = SPIKE_END_TICK + (10 * TICKS_PER_SECOND) + (TICKS_PER_SECOND / 2);

/// Steady, but for two seconds of 6 ms from 15 s in.
u64 Spiked(u64 tick) {
    return tick >= SPIKE_FIRST_TICK && tick < SPIKE_END_TICK ? SPIKE_LATENESS_NS : Steady(tick);
}

TEST_CASE("a lateness spike raises D and leaves no late adoption once risen") {
    for (const Rate& rate : RATES) {
        Timeline timeline{ { .framePeriodNs = rate.periodNs, .acquireWaitNs = rate.waitNs, .lateness = Spiked } };
        CAPTURE(rate.name);
        timeline.RunUntil(core::TickTime(ORIGIN_NS, SPIKE_FIRST_TICK + TICKS_PER_SECOND));
        const u64 lateBefore = timeline.LateAdoptions();
        timeline.RunUntil(core::TickTime(ORIGIN_NS, SPIKE_END_TICK + 1)); // a tick on, so the spike's last is taken
        CHECK(timeline.Delay().Current() >= SPIKE_LATENESS_NS + core::DISPLAY_DELAY_MARGIN_NS);
        CHECK(timeline.LateAdoptions() == lateBefore);

        timeline.RunUntil(core::TickTime(ORIGIN_NS, FALLEN_TICK));
        CHECK(timeline.Delay().Target() == STEADY_TARGET_NS);
        CHECK(timeline.Delay().Current() <= timeline.Delay().Target() + SETTLE_TOLERANCE_NS);

        f64 steepestFall = 0.0;
        const std::vector<DrawnFrame>& draws = timeline.Draws();
        for (std::size_t i = 1; i < draws.size(); ++i) {
            steepestFall = std::min(steepestFall, RenderTime(draws[i]) - RenderTime(draws[i - 1]));
        }
        CHECK(steepestFall == 0.0); // never back
    }
}

} // namespace
