#include "core/logic.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/math.hpp"
#include "core/profile.hpp"
#include "core/save_file.hpp"

namespace core {

namespace {

constexpr u32 SAVE_FORMAT = 1; // a save's start: this, the tick counters, then the simulation's state

} // namespace

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
    bool stalled = false;
    for (u64 tick = 0; tick < burst.ticks && !stalled; ++tick) {
        advanced = false;
        stalled = true;
        if (m_calendar == Calendar::Published) {
            m_counters.Count(true, true);
            simulation.AdvanceCalendar(m_counters, jobs, stop);
            m_calendar = Calendar::None;
        }
        else if (const Requests requests = TakeInput(handover); requests.load) {
            if (!Load(simulation, jobs)) {
                m_counters.Count(true, false);
            }
        }
        else if (requests.save) {
            m_counters.Count(true, false);
            Save(simulation, jobs);
        }
        else {
            stalled = false;
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
    snapshot.epoch = m_epoch;
    snapshot.scheduledNs = burst.scheduledNs;
    snapshot.advanced = advanced;
    snapshot.transition = m_calendar == Calendar::Published;
    snapshot.instances.clear();
    snapshot.batches.clear();
    simulation.Publish(snapshot, jobs);
    snapshot.publishedNs = now();
    handover.Publish(m_events);
    return stalled;
}

Logic::Requests Logic::TakeInput(GameHandover& handover) {
    handover.TakeInput(m_input);
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

void Logic::Save(Simulation& simulation, Jobs& jobs) {
    PN_PROFILE_ZONE();
    if (m_saveFile.empty()) {
        PN_LOG_WARNING("not saved: there is no folder for saves");
        return;
    }
    std::vector<std::byte> state;
    Writer archive{ state };
    archive(SAVE_FORMAT, m_counters.wall, m_counters.simulation, m_counters.state);
    simulation.Save(archive);
    PN_LOG_INFO("saving the game at tick {}: {} bytes of state", m_counters.wall, state.size());
    jobs.Background([file = m_saveFile, bytes = std::move(state)] {
        const std::expected<void, Error> written = WriteSaveFile(file, bytes);
        if (written) {
            PN_LOG_INFO("saved the game to {}", file.string());
        }
        else {
            PN_LOG_ERROR("{}", written.error().message);
        }
    });
}

bool Logic::Load(Simulation& simulation, Jobs& jobs) {
    PN_PROFILE_ZONE();
    if (m_saveFile.empty()) {
        PN_LOG_WARNING("not loaded: there is no folder for saves");
        return false;
    }
    jobs.WaitForBackground(); // a save still being written is the newest
    const std::expected<std::vector<std::byte>, Error> state = ReadSaveFile(m_saveFile);
    if (!state) {
        PN_LOG_ERROR("not loaded: {}", state.error().message);
        return false;
    }
    Reader archive{ *state };
    u32 format = 0;
    TickCounters counters;
    archive(format, counters.wall, counters.simulation, counters.state);
    if (archive.Failed() || format != SAVE_FORMAT || !simulation.Load(archive)) {
        PN_LOG_ERROR("not loaded: {} is malformed or from another version of the game", m_saveFile.string());
        return false;
    }
    m_counters = counters;
    m_calendar = Calendar::None;
    ++m_epoch;
    PN_LOG_INFO("loaded the game from {} at tick {}; timeline {} begins", m_saveFile.string(), m_counters.wall, m_epoch);
    return true;
}

} // namespace core
