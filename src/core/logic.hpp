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

    /// Advances the game state by one simulation tick, `simulationTick` (counting from 1). Frozen
    /// ticks don't call it.
    virtual void Tick(u64 simulationTick) = 0;

    /// Writes what the state looks like now into `snapshot`'s drawing data: its instances and batches
    /// arrive empty, with their capacity kept. Called once per burst, after its last tick, on the
    /// thread that created `jobs`, whose workers it may fork.
    virtual void Publish(Snapshot& snapshot, Jobs& jobs) = 0;
};

/// The game state the logic thread owns, and the scratch it reuses between bursts.
class Logic {
public:
    /// Runs `burst.ticks` ticks back to back. Each takes the input sent since the one before (the
    /// last Pause or Resume wins) and counts itself, frozen while paused, and ticks `simulation`
    /// unless frozen. Then publishes the snapshot of the burst's last tick, filled by
    /// `simulation` on `jobs` and stamped with `now()` at publishing.
    void RunBurst(const TickBurst& burst, GameHandover& handover, Simulation& simulation, Jobs& jobs, u64 (*now)());

    /// The tick counters so far.
    [[nodiscard]] const TickCounters& Counters() const {
        return m_counters;
    }

private:
    TickCounters m_counters{};
    bool m_paused = false;
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer for
    // members whose default constructor is defaulted.
    std::vector<Action> m_input{};
    std::vector<Event> m_events{};
    // NOLINTEND(readability-redundant-member-init)
};

} // namespace core
