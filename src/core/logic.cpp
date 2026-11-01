#include "core/logic.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/math.hpp"
#include "core/profile.hpp"

namespace core {

void Logic::RunBurst(const TickBurst& burst, GameHandover& handover, u64 (*now)()) {
    PN_PROFILE_ZONE();
    for (u64 tick = 0; tick < burst.ticks; ++tick) {
        handover.TakeInput(m_input);
        for (const Action& action : m_input) {
            m_paused = action.kind == ActionKind::Pause;
        }
        m_counters.Count(m_paused, false);
        PN_ASSERT(math::FloatingPointEnvironmentIsSet(), "a library changed the floating-point environment");
    }
    handover.Writing() = {
        .wallTick = m_counters.wall,
        .simulationTick = m_counters.simulation,
        .scheduledNs = burst.scheduledNs,
        .publishedNs = now(),
        .advanced = !m_paused,
    };
    handover.Publish(m_events);
}

} // namespace core
