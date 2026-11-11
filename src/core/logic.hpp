#pragma once

/// @file
/// @ingroup core
/// The logic thread's work for each burst of ticks (design doc 0002 §6): taking input, running the
/// ticks, frozen while paused, and publishing the last one's snapshot. The thread around it only
/// waits for the clock and logs. The game module's simulation runs here through core::Simulation.

#include "core/archive.hpp"
#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/manifest.hpp"
#include "core/replay.hpp"
#include "core/saves.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"

#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
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

    /// Advances the game state by one simulation tick, `counters.simulation` (counting from 1),
    /// drawing random numbers keyed on `counters.state`, under the tick's `input`: the actions the
    /// simulation acts on (Select) and the held input. Frozen ticks don't call it, so their actions
    /// do nothing. It appends the events the tick starts to `events`; the logic stamps their tick and epoch.
    virtual void Tick(const TickCounters& counters, const TickInput& input, std::vector<Event>& events) = 0;

    /// Runs a calendar step (design doc 0002 §7.3) on a frozen tick that changes the state, keyed
    /// on `counters.state`: forks over `jobs` from the thread that created it, and returns early,
    /// leaving the step unfinished, once `stop` is requested (the game is closing).
    virtual void AdvanceCalendar(const TickCounters& counters, Jobs& jobs, const std::stop_token& stop) = 0;

    /// Sorts the state into canonical order (design doc 0002 §7.1), which is part of the timeline,
    /// and writes all of it into `archive`.
    virtual void Save(Writer& archive) = 0;

    /// Replaces the state with the one Save() wrote, read from `archive` to its end, saved at simulation tick
    /// `simulationTick` (the save's counters, which the logic reads before it). Returns false, changing nothing, when the
    /// bytes are malformed or of another version, or don't fit that tick.
    [[nodiscard]] virtual bool Load(Reader& archive, u64 simulationTick) = 0;

    /// A hash of the whole state as it is, order included: two runs that agree hash alike (§7.4).
    [[nodiscard]] virtual u64 StateHash() const = 0;

    /// Swaps in new game data (design doc 0002.3 §7), which takes effect at once: what the data
    /// decides for entities that exist (a creature's sprite, its orbit) changes with it.
    virtual void SetData(const GameData& data) = 0;

    /// A hash of the game data in use (GameData::Hash): a replay plays only under the data it was recorded with.
    [[nodiscard]] virtual u64 DataHash() const = 0;

    /// The scene its state needs drawn (design doc 0002.3.1 §3): it ticks only while the main thread holds it.
    [[nodiscard]] virtual SceneId Scene() const = 0;

    /// Writes what the state looks like now into `snapshot`'s drawing data: its instances, batches
    /// and IDs arrive empty, with their capacity kept, and it follows nothing until set. Called once per burst, after its last
    /// tick, on the thread that created `jobs`, whose workers it may fork.
    virtual void Publish(Snapshot& snapshot, Jobs& jobs) = 0;
};

/// What the logic thread keeps between bursts (the tick counters, pausing, the calendar step's
/// phase, the timeline epoch, scratch buffers), and the systems it drives, which it holds but
/// doesn't own.
class Logic {
public:
    /// Drives `simulation`, publishing through `handover`, forking on `jobs`, saving to and loading
    /// from `saves`, recording into and playing from `replay`, whose file is `replays`, and
    /// stamping snapshots with `now()`. All must outlive it.
    Logic(
        GameHandover& handover,
        Simulation& simulation,
        Jobs& jobs,
        Saves& saves,
        Saves& replays,
        Replay& replay,
        u64 (*now)()
    )
      : m_handover{ &handover }
      , m_simulation{ &simulation }
      , m_jobs{ &jobs }
      , m_saves{ &saves }
      , m_replays{ &replays }
      , m_replay{ &replay }
      , m_now{ now } {
    }
    Logic(const Logic&) = delete;
    Logic& operator=(const Logic&) = delete;
    Logic(Logic&&) = delete;
    Logic& operator=(Logic&&) = delete;
    ~Logic() = default;

    /// Runs up to `burst.ticks` ticks back to back, then publishes the snapshot of the last one run,
    /// filled by the simulation, naming its scene and stamped with `now()`. While the simulation's
    /// scene isn't resident (no SceneResident has named it since it changed: at the start, after a
    /// load or a replay starting in another scene), the burst runs no tick but waits (design doc
    /// 0002.3.1 §4): it takes live input only, records nothing and moves no counter; Pause and
    /// Resume still apply, SceneResident and ReloadData are taken, everything else is dropped; a tick
    /// that changes the scene (CycleScene) ends its burst. Each tick takes the input sent since the
    /// one before (the last Pause or Resume wins; while a replay plays, the recorded input instead)
    /// and the newest held input, which every tick of a burst gets,
    /// counts itself, frozen while paused or while a calendar step is asked for, and ticks the
    /// simulation unless frozen. Ticks that change what comes next end the burst after them, so every
    /// change sits at a fixed wall tick whatever the burst sizes (design doc 0002.2 §2):
    /// - AdvanceCalendar: the burst publishes the transition, and the next tick runs the step
    ///   instead of taking input (input waits in the handover);
    /// - Save (frozen): the state is sorted, serialised and handed to the save system (before a
    ///   ToggleRecording in the same tick takes effect);
    /// - Load (wins over Save): the save is read and, when sound, replaces the state and the
    ///   counters and starts a new timeline epoch;
    /// - ReloadData (frozen, first): the simulation takes the new game data; a recording finishes
    ///   and a replay playing stops, as the timeline's rules changed;
    /// - ToggleRecording, PlayReplay: a replay starts or finishes recording, or starts or stops
    ///   playing (a new epoch); every REPLAY_HASH_TICKS wall ticks the state's hash is recorded or
    ///   checked, and a divergence stops playback with an error. While a replay plays, live Pause and
    ///   Resume (the window's focus) are kept aside and take effect when it stops.
    /// A SceneResident taken on a running tick is applied and never recorded.
    /// Returns whether the burst ended on a stall the caller expected (a step, a save, a load, a
    /// replay starting or finishing, the scene waited for becoming resident), so its clock restarts rather than catching up
    /// (TickClock::Restart). `stop` cuts a step short.
    bool RunBurst(const TickBurst& burst, const std::stop_token& stop = {});

    /// The tick counters so far.
    [[nodiscard]] const TickCounters& Counters() const {
        return m_counters;
    }

    /// The timeline epoch: 0 at the start, one more at each load (design doc 0002 §5).
    [[nodiscard]] u64 Epoch() const {
        return m_epoch;
    }

    /// A hash of the logic's state and the simulation's: what replays record and check.
    [[nodiscard]] u64 Hash() const;

private:
    enum class Calendar : u8 {
        None,      // no step asked for
        Requested, // asked for: the burst publishes the transition
        Published, // the transition is out: the next tick runs the step
    };

    enum class TickEnd : u8 {
        Continue, // the burst goes on
        EndBurst, // the burst ends here, on time
        Stall,    // the burst ends after a stall the caller expected
    };

    // The kinds of action a tick took, a bit per ActionKind.
    using ActionSet = u32;
    static_assert(ACTION_KINDS <= std::numeric_limits<ActionSet>::digits, "an ActionSet holds a bit per kind");

    TickEnd RunTick(const std::stop_token& stop);
    bool Wait();
    ActionSet TakeInput();
    void CheckReplay();
    [[nodiscard]] std::vector<std::byte> Serialise(bool withLogicState);
    [[nodiscard]] bool Restore(std::span<const std::byte> state, bool withLogicState);
    void Save();
    bool Load();
    void ToggleRecording();
    void FinishRecording();
    bool TogglePlayback();
    void StopPlaying(std::string_view why);
    void ReloadData();

    GameHandover* m_handover;
    Simulation* m_simulation;
    Jobs* m_jobs;
    Saves* m_saves;
    Saves* m_replays;
    Replay* m_replay;
    u64 (*m_now)();
    TickCounters m_counters{};
    u64 m_epoch = 0;
    bool m_paused = false;       // the simulation's pausing: live input's, or the replay's while one plays
    bool m_windowPaused = false; // the last Pause or Resume live input sent, also while a replay plays
    bool m_advanced = false;     // whether the simulation ran in the last tick
    Calendar m_calendar = Calendar::None;
    std::optional<SceneId> m_resident = std::nullopt; // the scene the main thread said it holds last
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer for
    // members whose default constructor is defaulted.
    std::vector<Action> m_input{};
    std::vector<Event> m_events{};
    // NOLINTEND(readability-redundant-member-init)
    HeldInput m_held{}; // the tick's held input: live, or the replay's while one plays
};

} // namespace core
