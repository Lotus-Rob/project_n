#pragma once

/// @file
/// @ingroup core
/// Tick timing (design doc 0002 §6): the logic thread's fixed schedule of 60 ticks a second with
/// its overload policy, the three tick counters, and the main thread's display delay, adoption
/// test and render clock. Pure functions of the times passed in; nothing here reads a clock.

#include "core/types.hpp"

#include <algorithm>
#include <optional>

namespace core {

/// Ticks a second, fixed: game speed never depends on the machine.
inline constexpr u64 TICKS_PER_SECOND = 60;
/// Nanoseconds in a second.
inline constexpr u64 NS_PER_SECOND = 1'000'000'000;
/// One tick's length in nanoseconds, rounded down; schedules use TickTime(), which doesn't drift.
inline constexpr u64 TICK_NS = NS_PER_SECOND / TICKS_PER_SECOND;
/// The most ticks run back to back after a hitch; beyond that, wall time is dropped.
inline constexpr u64 MAX_CATCH_UP = 3;
/// How long ticks may run late before it counts as sustained overload.
inline constexpr u64 OVERLOAD_NS = NS_PER_SECOND;
/// The display delay D: how long after its scheduled time a snapshot starts to show, so tick run
/// times can vary without judder. Proposed in the design doc; to be set from measured tick times.
inline constexpr u64 DEFAULT_DISPLAY_DELAY_NS = 10'000'000;

/// The scheduled time of the tick `index` ticks after `originNs`, exact to the nanosecond: no
/// rounding error builds up however many ticks pass.
[[nodiscard]] constexpr u64 TickTime(u64 originNs, u64 index) {
    return originNs + (index * NS_PER_SECOND / TICKS_PER_SECOND);
}

/// The ticks TickClock::Advance() found due.
struct TickBurst {
    /// Ticks to run now, back to back: 0 before the next one is due, at most MAX_CATCH_UP.
    u64 ticks = 0;
    /// The scheduled time of the burst's last tick: the time its snapshot carries.
    u64 scheduledNs = 0;
    /// Ticks that were due beyond MAX_CATCH_UP and won't run: that wall time is dropped, and the game
    /// slows briefly.
    u64 droppedTicks = 0;
};

/// The logic thread's schedule: one tick every 1/60 s from a start time, catching up at most
/// MAX_CATCH_UP ticks after a hitch.
class TickClock {
public:
    /// A schedule whose first tick is due at `startNs`.
    explicit TickClock(u64 startNs)
      : m_originNs{ startNs } {
    }

    /// When the next tick is due: the logic thread waits until then.
    [[nodiscard]] u64 NextNs() const {
        return TickTime(m_originNs, m_index);
    }

    /// The ticks due at `nowNs`, and moves the schedule past them. When more than MAX_CATCH_UP are
    /// due, the schedule restarts so that MAX_CATCH_UP ticks end at `nowNs` and the next is a tick
    /// later. Times passed in never go back.
    TickBurst Advance(u64 nowNs);

    /// Starts the schedule again at `nowNs`, after a stall that was expected (a calendar step, a
    /// save): the next tick is due at `nowNs`, with no catching up and no overload counted.
    void Restart(u64 nowNs);

    /// Whether ticks have run late (more than one due at once) for longer than OVERLOAD_NS by
    /// `nowNs`, the last Advance() time; catching up ends it.
    [[nodiscard]] bool Overloaded(u64 nowNs) const {
        return m_lateSinceNs && nowNs - *m_lateSinceNs > OVERLOAD_NS;
    }

private:
    u64 m_originNs;
    u64 m_index = 0; // ticks run since m_originNs
    std::optional<u64> m_lateSinceNs = std::nullopt;
};

/// The three tick counters (design doc 0002 §6), saved with the state.
struct TickCounters {
    /// Every tick the schedule runs.
    u64 wall = 0;
    /// Ticks the simulation ran: real-time systems, rates and time slicing key on it.
    u64 simulation = 0;
    /// Ticks that changed the state: random numbers key on it.
    u64 state = 0;

    /// Counts one tick before it runs: a wall tick always, a simulation tick unless the simulation
    /// is `frozen` (paused, a calendar step's transition, loading), and a state change when the
    /// simulation runs or a frozen tick `changesState` (a calendar step, a UI decision).
    constexpr void Count(bool frozen, bool changesState) {
        ++wall;
        if (!frozen) {
            ++simulation;
        }
        if (!frozen || changesState) {
            ++state;
        }
    }
};

/// Whether a snapshot scheduled at `scheduledNs` is due for display at `nowNs`: `delayNs` (D) after
/// its scheduled time, exactly when the snapshot before it reaches alpha 1.
[[nodiscard]] constexpr bool IsDue(u64 scheduledNs, u64 nowNs, u64 delayNs) {
    return nowNs >= scheduledNs + delayNs;
}

/// How far to interpolate a snapshot from its previous transforms (0) to its current ones (1) at
/// `nowNs`: 0 to 1 over the tick that starts `delayNs` after `scheduledNs`. A snapshot from a tick
/// the simulation didn't advance (`advanced` false) shows its current transforms.
[[nodiscard]] constexpr f64 Alpha(u64 scheduledNs, u64 nowNs, u64 delayNs, bool advanced) {
    if (!advanced) {
        return 1.0;
    }
    if (nowNs <= scheduledNs + delayNs) {
        return 0.0;
    }
    const auto elapsed = static_cast<f64>(nowNs - scheduledNs - delayNs);
    return std::min(elapsed * static_cast<f64>(TICKS_PER_SECOND) / static_cast<f64>(NS_PER_SECOND), 1.0);
}

/// The render clock in simulation ticks: the drawn snapshot's simulation tick minus one plus
/// alpha. Pictures, animation poses, particles and events all follow it; while the simulation is
/// frozen (alpha 1) it holds at the snapshot's tick.
[[nodiscard]] constexpr f64 RenderTick(u64 simulationTick, f64 alpha) {
    return static_cast<f64>(simulationTick) - 1.0 + alpha;
}

} // namespace core
