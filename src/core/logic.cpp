#include "core/logic.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/math.hpp"
#include "core/profile.hpp"

namespace core {

Simulation::~Simulation() = default;

void Logic::RunBurst(const TickBurst& burst, GameHandover& handover, Simulation& simulation, u64 (*now)()) {
    PN_PROFILE_ZONE();
    for (u64 tick = 0; tick < burst.ticks; ++tick) {
        handover.TakeInput(m_input);
        for (const Action& action : m_input) {
            m_paused = action.kind == ActionKind::Pause;
        }
        m_counters.Count(m_paused, false);
        if (!m_paused) {
            simulation.Tick(m_counters.simulation);
        }
        PN_ASSERT(math::FloatingPointEnvironmentIsSet(), "a library changed the floating-point environment");
    }
    // Field by field: assigning a whole Snapshot would free the instance buffer's capacity.
    Snapshot& snapshot = handover.Writing();
    snapshot.wallTick = m_counters.wall;
    snapshot.simulationTick = m_counters.simulation;
    snapshot.scheduledNs = burst.scheduledNs;
    snapshot.advanced = !m_paused;
    snapshot.instances.clear();
    simulation.Publish(snapshot);
    snapshot.publishedNs = now();
    handover.Publish(m_events);
}

} // namespace core
