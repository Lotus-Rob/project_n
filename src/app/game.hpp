#pragma once

/// @file
/// @ingroup app
/// The logic thread's side of the game (design doc 0002 §5, §6): the systems it owns, built on
/// that thread, and its loop of waiting for each tick and running the ticks due.

#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/logic.hpp"
#include "core/saves.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"
#include "game/arena.hpp"

#include <filesystem>
#include <stop_token>

namespace app {

/// The logic thread's systems: the job system, the save system, the simulation (the arena), the
/// logic that drives it and the tick clock, built in that order and destroyed in reverse.
class Game {
public:
    /// Builds the systems for `scene`, publishing through `handover`, saving to `saveSlot` (none
    /// when empty), its first tick due at `startNs` on the clock `now()` reads. The calling thread
    /// becomes the job system's forking thread, so it must be the thread that runs Step() and
    /// Run(), with its floating-point environment set (core::math::SetFloatingPointEnvironment).
    Game(core::GameHandover& handover, game::Scene scene, std::filesystem::path saveSlot, u64 startNs, u64 (*now)());
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    Game(Game&&) = delete;
    Game& operator=(Game&&) = delete;
    /// Logs the ticks run; the job system then stops once idle, finishing a save in progress.
    ~Game();

    /// Runs the ticks due at `nowNs` (core::Logic::RunBurst), logging when ticks start and stop
    /// running late; after an expected stall (a calendar step, a save, a load) the clock restarts
    /// at `now()` rather than catching up. `stop` cuts a calendar step short.
    void Step(u64 nowNs, const std::stop_token& stop = {});

    /// Waits for each tick and Step()s until `stop` is requested.
    void Run(const std::stop_token& stop);

    /// The tick clock.
    [[nodiscard]] const core::TickClock& Clock() const {
        return m_clock;
    }

    /// The logic: its counters and epoch.
    [[nodiscard]] const core::Logic& Logic() const {
        return m_logic;
    }

private:
    u64 (*m_now)();
    core::Jobs m_jobs;
    core::Saves m_saves;
    game::Arena m_arena;
    core::Logic m_logic;
    core::TickClock m_clock;
    bool m_overloaded = false;
};

} // namespace app
