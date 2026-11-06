#include "core/logic.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/math.hpp"
#include "core/profile.hpp"

namespace core {

Simulation::~Simulation() = default;

bool Logic::RunBurst(
    const TickBurst& burst,
    GameHandover& handover,
    Simulation& simulation,
    Jobs& jobs,
    u64 (*now)(),
    const std::stop_token& stop
) {
    PN_PROFILE_ZONE();
    bool advanced = false;
    bool stepped = false;
    for (u64 tick = 0; tick < burst.ticks && !stepped; ++tick) {
        if (m_calendar == Calendar::Published) {
            m_counters.Count(true, true);
            simulation.AdvanceCalendar(m_counters, jobs, stop);
            m_calendar = Calendar::None;
            advanced = false;
            stepped = true;
        }
        else {
            handover.TakeInput(m_input);
            for (const Action& action : m_input) {
                if (action.kind == ActionKind::AdvanceCalendar) {
                    m_calendar = Calendar::Requested;
                }
                else {
                    m_paused = action.kind == ActionKind::Pause;
                }
            }
            advanced = !m_paused && m_calendar == Calendar::None;
            m_counters.Count(!advanced, false);
            if (advanced) {
                simulation.Tick(m_counters);
            }
        }
        PN_ASSERT(math::FloatingPointEnvironmentIsSet(), "a library changed the floating-point environment");
    }
    if (m_calendar == Calendar::Requested) {
        m_calendar = Calendar::Published;
    }
    // Field by field: assigning a whole Snapshot would free the instance and batch buffers' capacity.
    Snapshot& snapshot = handover.Writing();
    snapshot.wallTick = m_counters.wall;
    snapshot.simulationTick = m_counters.simulation;
    snapshot.scheduledNs = burst.scheduledNs;
    snapshot.advanced = advanced;
    snapshot.transition = m_calendar == Calendar::Published;
    snapshot.instances.clear();
    snapshot.batches.clear();
    simulation.Publish(snapshot, jobs);
    snapshot.publishedNs = now();
    handover.Publish(m_events);
    return stepped;
}

} // namespace core
