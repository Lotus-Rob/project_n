#pragma once

/// @file
/// @ingroup core
/// The logic thread's work for each burst of ticks (design doc 0002 §6): taking input, running the
/// ticks, frozen while paused, and publishing the last one's snapshot. The thread around it only
/// waits for the clock and logs. Phase 1's simulation is the tick counters and pausing; the game
/// module's systems run here from phase 4.

#include "core/frame.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"

#include <vector>

namespace core {

/// The game state the logic thread owns, and the scratch it reuses between bursts.
class Logic {
public:
    /// Runs `burst.ticks` ticks back to back. Each takes the input sent since the one before (the
    /// last Pause or Resume wins) and counts itself, frozen while paused. Then publishes the
    /// snapshot of the burst's last tick, stamped with `now()` at publishing.
    void RunBurst(const TickBurst& burst, GameHandover& handover, u64 (*now)());

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
