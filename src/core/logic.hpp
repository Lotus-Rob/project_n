#pragma once

/// @file
/// @ingroup core
/// The logic thread's work for each burst of ticks (design doc 0002 §6): taking input, running the
/// ticks, frozen while paused, and publishing the last one's snapshot. The thread around it only
/// waits for the clock and logs. The game module's simulation runs here through core::Simulation.

#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"

#include <stop_token>
#include <vector>

namespace core {

/// The game's simulation as the logic thread drives it: implemented by the game module, which
/// links only core.
class Simulation {
public:
    Simulation() = default;
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;
    Simulation(Simulation&&) = delete;
    Simulation& operator=(Simulation&&) = delete;
    virtual ~Simulation();

    /// Advances the game state by one simulation tick, `counters.simulation` (counting from 1),
    /// drawing random numbers keyed on `counters.state`. Frozen ticks don't call it.
    virtual void Tick(const TickCounters& counters) = 0;

    /// Runs a calendar step (design doc 0002 §7.3) on a frozen tick that changes the state, keyed
    /// on `counters.state`: forks over `jobs` from the thread that created it, and returns early,
    /// leaving the step unfinished, once `stop` is requested (the game is closing).
    virtual void AdvanceCalendar(const TickCounters& counters, Jobs& jobs, const std::stop_token& stop) = 0;

    /// Writes what the state looks like now into `snapshot`'s drawing data: its instances and batches
    /// arrive empty, with their capacity kept. Called once per burst, after its last tick, on the
    /// thread that created `jobs`, whose workers it may fork.
    virtual void Publish(Snapshot& snapshot, Jobs& jobs) = 0;
};

/// The game state the logic thread owns, and the scratch it reuses between bursts.
class Logic {
public:
    /// Runs `burst.ticks` ticks back to back. Each takes the input sent since the one before (the
    /// last Pause or Resume wins) and counts itself, frozen while paused or while a calendar step
    /// is asked for, and ticks `simulation` unless frozen. Then publishes the snapshot of the
    /// burst's last tick, filled by `simulation` on `jobs` and stamped with `now()` at publishing.
    /// A burst that takes AdvanceCalendar publishes the transition; the first tick of the next
    /// runs the step instead of taking input, which waits in the handover, and ends its burst.
    /// Returns whether it ran a step: a stall the caller expected, so its clock restarts rather
    /// than catching up (TickClock::Restart). `stop` cuts a step short.
    bool RunBurst(
        const TickBurst& burst,
        GameHandover& handover,
        Simulation& simulation,
        Jobs& jobs,
        u64 (*now)(),
        const std::stop_token& stop = {}
    );

    /// The tick counters so far.
    [[nodiscard]] const TickCounters& Counters() const {
        return m_counters;
    }

private:
    enum class Calendar : u8 {
        None,      // no step asked for
        Requested, // asked for: the burst publishes the transition
        Published, // the transition is out: the next tick runs the step
    };

    TickCounters m_counters{};
    bool m_paused = false;
    Calendar m_calendar = Calendar::None;
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer for
    // members whose default constructor is defaulted.
    std::vector<Action> m_input{};
    std::vector<Event> m_events{};
    // NOLINTEND(readability-redundant-member-init)
};

} // namespace core
