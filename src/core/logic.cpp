#include "core/logic.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/math.hpp"
#include "core/profile.hpp"

namespace core {

namespace {

constexpr u32 SAVE_FORMAT = 1; // a save's start: this, the tick counters, then the simulation's state

} // namespace

Simulation::~Simulation() = default;

bool Logic::RunBurst(const TickBurst& burst, const std::stop_token& stop) {
    PN_PROFILE_ZONE();
    bool advanced = false;
    bool stalled = false;
    for (u64 tick = 0; tick < burst.ticks && !stalled; ++tick) {
        advanced = false;
        stalled = true;
        if (m_calendar == Calendar::Published) {
            m_counters.Count(true, true);
            m_simulation->AdvanceCalendar(m_counters, *m_jobs, stop);
            m_calendar = Calendar::None;
        }
        else if (const Requests requests = TakeInput(); requests.load) {
            if (!Load()) {
                m_counters.Count(true, false);
            }
        }
        else if (requests.save) {
            m_counters.Count(true, false);
            Save();
        }
        else {
            stalled = false;
            advanced = !m_paused && m_calendar == Calendar::None;
            m_counters.Count(!advanced, false);
            if (advanced) {
                m_simulation->Tick(m_counters);
            }
        }
        PN_ASSERT(math::FloatingPointEnvironmentIsSet(), "a library changed the floating-point environment");
    }
    if (m_calendar == Calendar::Requested) {
        m_calendar = Calendar::Published;
    }
    // Field by field: assigning a whole Snapshot would free the instance and batch buffers' capacity.
    Snapshot& snapshot = m_handover->Writing();
    snapshot.wallTick = m_counters.wall;
    snapshot.simulationTick = m_counters.simulation;
    snapshot.epoch = m_epoch;
    snapshot.scheduledNs = burst.scheduledNs;
    snapshot.advanced = advanced;
    snapshot.transition = m_calendar == Calendar::Published;
    snapshot.instances.clear();
    snapshot.batches.clear();
    m_simulation->Publish(snapshot, *m_jobs);
    snapshot.publishedNs = m_now();
    m_handover->Publish(m_events);
    return stalled;
}

Logic::Requests Logic::TakeInput() {
    m_handover->TakeInput(m_input);
    Requests requests;
    for (const Action& action : m_input) {
        if (action.kind == ActionKind::AdvanceCalendar) {
            m_calendar = Calendar::Requested;
        }
        else if (action.kind == ActionKind::Save) {
            requests.save = true;
        }
        else if (action.kind == ActionKind::Load) {
            requests.load = true;
        }
        else {
            m_paused = action.kind == ActionKind::Pause;
        }
    }
    return requests;
}

void Logic::Save() {
    PN_PROFILE_ZONE();
    std::vector<std::byte> state;
    Writer archive{ state };
    archive(SAVE_FORMAT, m_counters.wall, m_counters.simulation, m_counters.state);
    m_simulation->Save(archive);
    PN_LOG_INFO("saving the game at tick {}: {} bytes of state", m_counters.wall, state.size());
    m_saves->Write(std::move(state));
}

bool Logic::Load() {
    PN_PROFILE_ZONE();
    const std::expected<std::vector<std::byte>, Error> state = m_saves->Read();
    if (!state) {
        PN_LOG_ERROR("not loaded: {}", state.error().message);
        return false;
    }
    Reader archive{ *state };
    u32 format = 0;
    TickCounters counters;
    archive(format, counters.wall, counters.simulation, counters.state);
    if (archive.Failed() || format != SAVE_FORMAT || !m_simulation->Load(archive)) {
        PN_LOG_ERROR("not loaded: {} is malformed or from another version of the game", m_saves->Slot().string());
        return false;
    }
    m_counters = counters;
    m_calendar = Calendar::None;
    ++m_epoch;
    PN_LOG_INFO("loaded the game from {} at tick {}; timeline {} begins", m_saves->Slot().string(), m_counters.wall, m_epoch);
    return true;
}

} // namespace core
