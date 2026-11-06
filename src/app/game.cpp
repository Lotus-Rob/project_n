#include "app/game.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/profile.hpp"
#include "platform/clock.hpp"

namespace app {

namespace {

constexpr u64 NS_PER_MS = core::NS_PER_SECOND / 1'000;

} // namespace

Game::Game(
    core::GameHandover& handover,
    game::Scene scene,
    std::filesystem::path saveSlot,
    std::filesystem::path replaySlot,
    u64 startNs,
    u64 (*now)()
)
  : m_now{ now }
  , m_jobs{ core::Jobs::DefaultWorkers() }
  , m_saves(std::move(saveSlot), m_jobs)
  , m_replays(std::move(replaySlot), m_jobs)
  , m_arena{ scene }
  , m_logic(handover, m_arena, m_jobs, m_saves, m_replays, m_replay, now)
  , m_clock{ startNs } {
    PN_LOG_INFO(
        "logic thread ticking {} times a second, job system running {} workers", core::TICKS_PER_SECOND, m_jobs.Workers()
    );
}

Game::~Game() {
    const core::TickCounters& counters = m_logic.Counters();
    PN_LOG_INFO("logic thread stopping after {} ticks, {} simulated", counters.wall, counters.simulation);
}

void Game::Step(u64 nowNs, const std::stop_token& stop) {
    PN_PROFILE_ZONE();
    const core::TickBurst burst = m_clock.Advance(nowNs);
    if (burst.droppedTicks > 0) {
        PN_LOG_DEBUG("{} ticks dropped after a hitch: the game slowed down", burst.droppedTicks);
    }
    if (m_clock.Overloaded(nowNs) != m_overloaded) {
        m_overloaded = !m_overloaded;
        if (m_overloaded) {
            PN_LOG_WARNING("ticks have run late for over a second: the machine can't keep up");
        }
        else {
            PN_LOG_INFO("ticks are on time again");
        }
    }
    if (m_logic.RunBurst(burst, stop)) {
        const u64 stalledNs = m_now();
        PN_LOG_INFO("an expected stall (a month, a save or a load) took {} ms", (stalledNs - nowNs) / NS_PER_MS);
        m_clock.Restart(stalledNs);
    }
}

void Game::Run(const std::stop_token& stop) {
    while (!stop.stop_requested()) {
        platform::WaitUntilNs(m_clock.NextNs());
        Step(m_now(), stop);
    }
}

} // namespace app
