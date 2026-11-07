#pragma once

/// @file
/// @ingroup app
/// The logic thread's side of the game (design doc 0002 §5, §6): the systems it owns, built on
/// that thread, and its loop of waiting for each tick and running the ticks due.

#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/logic.hpp"
#include "core/replay.hpp"
#include "core/saves.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"
#include "game/arena.hpp"

#include <filesystem>
#include <stop_token>

namespace app {

/// What a Game is built for.
struct GameSetup {
    /// The scene the arena is built as.
    game::Scene scene = game::Scene::Arena;
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// The save slot; no saves when empty.
    std::filesystem::path saveSlot{};
    /// The replay file; no replays when empty.
    std::filesystem::path replaySlot{};
    // NOLINTEND(readability-redundant-member-init)
    /// When the first tick is due, on the clock `now` reads.
    u64 startNs = 0;
    /// The clock.
    u64 (*now)() = nullptr;
};

/// The logic thread's systems: the job system, the save slot, the replay file's slot, the replay,
/// the simulation (the arena), the logic that drives them and the tick clock, built in that order
/// and destroyed in reverse.
class Game {
public:
    /// Builds the systems for `setup`, publishing through `handover`. The calling thread becomes the job system's forking
    /// thread, so it must be the thread that runs Step() and Run(), with its floating-point environment set
    /// (core::math::SetFloatingPointEnvironment).
    Game(core::GameHandover& handover, GameSetup setup);
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
    core::Saves m_replays;
    core::Replay m_replay{};
    game::Arena m_arena;
    core::Logic m_logic;
    core::TickClock m_clock;
    bool m_overloaded = false;
};

} // namespace app
