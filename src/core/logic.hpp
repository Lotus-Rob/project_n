#pragma once

/// @file
/// @ingroup core
/// The logic thread's work for each burst of ticks: taking input, running the
/// ticks, frozen while paused, and publishing the last one's snapshot. The thread around it only
/// waits for the clock and logs. The game module's simulation runs here through core::Simulation.

#include "core/animations.hpp"
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
#include <memory>
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

    /// Runs a calendar step on a frozen tick that changes the state, keyed
    /// on `counters.state`: forks over `jobs` from the thread that created it, and returns early,
    /// leaving the step unfinished, once `stop` is requested (the game is closing).
    virtual void AdvanceCalendar(const TickCounters& counters, Jobs& jobs, const std::stop_token& stop) = 0;

    /// Whether a month has ended and its calendar step is due: after a tick that ran the simulation, the logic asks for
    /// the step as an AdvanceCalendar action does. It turns false once AdvanceCalendar() has run.
    [[nodiscard]] virtual bool MonthDue() const = 0;

    /// Writes all of the state into `archive`, changing nothing: its order is part of the timeline, so Load() gives every
    /// part of it back in the order it had.
    virtual void Save(Writer& archive) = 0;

    /// Reads and checks the state Save() wrote, read from `archive` to its end, saved at simulation tick
    /// `simulationTick` (the save's counters, which the logic reads before it), and holds it, changing nothing else:
    /// Requested() names its scene until EnterScene() makes it current. Returns false, changing nothing and holding
    /// nothing new, when the bytes are malformed or of another version, or don't fit that tick.
    [[nodiscard]] virtual bool Load(Reader& archive, u64 simulationTick) = 0;

    /// A hash of the whole state as it is, order included: two runs that agree hash alike.
    [[nodiscard]] virtual u64 StateHash() const = 0;

    /// Swaps in new game data, which takes effect at once: what the data
    /// decides for entities that exist (a creature's sprite, its orbit) changes with it.
    virtual void SetData(const GameData& data) = 0;

    /// A hash of the game data in use (GameData::Hash): a replay plays only under the data it was recorded with.
    [[nodiscard]] virtual u64 DataHash() const = 0;

    /// The scene its state needs drawn: it ticks only while the main thread holds it.
    [[nodiscard]] virtual SceneId Scene() const = 0;

    /// The scene it is: its `sprites` are what Scene() returns.
    [[nodiscard]] virtual SceneFacts Current() const = 0;

    /// The scene of the state Load() holds, or else the scene its last tick asked to change to, until EnterScene(); none
    /// otherwise. The tick that asks has already done what leaving the old scene does (open uses end); a load leaves
    /// nothing to do.
    [[nodiscard]] virtual std::optional<SceneFacts> Requested() const = 0;

    /// Makes the scene Requested() names current: the state Load() holds, or else the scene asked for, built. One must
    /// have been loaded or asked for.
    virtual void EnterScene() = 0;

    /// Tells it whether the player has turned on seeing the whole map through the fog, before each Publish(); it changes
    /// only what is published. Does nothing by default.
    virtual void SetDebugSight(bool /*on*/) {
    }

    /// Gives it the figures' animations, before each Publish(); they change only what is published (which frame a walking
    /// figure draws), never the state. Does nothing by default.
    virtual void SetAnimations(const std::shared_ptr<const Animations>& /*animations*/) {
    }

    /// Writes what the state looks like now into `snapshot`'s drawing data: its instances, batches
    /// and IDs arrive empty, with their capacity kept, and it follows nothing until set. Called once per burst, after its last
    /// tick, on the thread that created `jobs`, whose workers it may fork. `view` is the world the main thread's window
    /// shows, with its margin (ViewReport::area), taken of the scene the snapshot shows; none until one arrives, at the
    /// start and after each swap. It changes only what is published, never the state.
    virtual void Publish(Snapshot& snapshot, Jobs& jobs, const std::optional<Rect>& view) = 0;
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
      , m_now{ now }
      , m_change{ .stage = SceneStage::Loading, .from = simulation.Current(), .to = simulation.Current(), .change = 1 } {
    }
    Logic(const Logic&) = delete;
    Logic& operator=(const Logic&) = delete;
    Logic(Logic&&) = delete;
    Logic& operator=(Logic&&) = delete;
    ~Logic() = default;

    /// Runs up to `burst.ticks` ticks back to back, then publishes the snapshot of the last one run,
    /// filled by the simulation, naming its scene and the scene change it is in (Snapshot::sceneChange) and stamped
    /// with `now()`. Every change of scene runs through stages:
    /// - the game's start is change 1, loading (from and to the start scene);
    /// - a tick whose simulation asks for another scene (Simulation::Requested()) starts the next change, leaving,
    ///   and ends its burst (its replay check sees the old scene): the old scene shows, frozen, until a SceneLeft naming
    ///   the change;
    /// - that SceneLeft has the scene entered (Simulation::EnterScene()), then loading until a SceneResident names the
    ///   new scene's sprites (at once when they are resident already);
    /// - then entering, until a SceneEntered naming the change, which ends the change on a stall;
    /// - a sound load or a replay starting starts the next change too, on its tick, into the scene of the state the
    ///   simulation holds (Simulation::Load()); that state and its counters (and a replay's pausing and calendar phase)
    ///   replace the old at the swap, where a new timeline epoch begins and a replay's start hash is checked; until then
    ///   nothing of it is in force.
    /// While a change is under way, or the simulation's scene isn't resident, the burst runs no tick but waits: it takes
    /// live input only, records nothing and moves no counter; Pause and Resume still apply, SceneResident, ReloadData and the
    /// reports naming the change's stage are taken, and everything else (a second change asked for, a Load, a stale report) is
    /// dropped. Each tick takes the input sent since the one before (the last Pause or Resume wins; while a replay plays, the
    /// recorded input instead) and the newest held input, which every tick of a burst gets, counts itself, frozen while paused
    /// or while a calendar step is asked for, and ticks the simulation unless frozen. Ticks that change what comes next end the
    /// burst after them, so every change sits at a fixed wall tick whatever the burst sizes:
    /// - AdvanceCalendar, or a tick that ran the simulation after which it says a month is due (Simulation::MonthDue()):
    ///   the burst publishes the transition, and the next tick runs the step instead of taking input (input waits in the
    ///   handover);
    /// - Save (frozen): the state is serialised and handed to the save system (before a
    ///   ToggleRecording in the same tick takes effect);
    /// - Load (wins over Save): the save is read and checked, and, when sound, starts a scene change into it (above),
    ///   which ends none of the old scene's uses; a broken one is refused on its tick, which is frozen;
    /// - ReloadData (frozen, first): the simulation takes the new game data; a recording finishes
    ///   and a replay playing stops, as the timeline's rules changed;
    /// - ToggleRecording, PlayReplay: a replay starts or finishes recording, or starts or stops
    ///   playing (starting runs a scene change into its start, as a load does, and plays from the first tick after it);
    ///   every REPLAY_HASH_TICKS wall ticks the state's hash is recorded or checked, and a divergence stops playback with an
    ///   error. While a replay plays, live Pause and Resume (the window's focus) are kept aside and take effect when it stops.
    /// A SceneResident taken on a running tick is applied and never recorded, and a stale SceneLeft or SceneEntered is
    /// dropped. The stage is in neither Hash() nor a save. The publish is given the main thread's latest view
    /// (GameHandover::ReadView()) when it was taken of the scene shown (ViewReport::change is ShownChange()), else none;
    /// the view is never recorded, hashed or saved.
    /// Returns whether the burst ended on a stall the caller expected (a step, a save, a load, a
    /// replay starting or finishing, the wait for a scene ending), so its clock restarts rather than catching up
    /// (TickClock::Restart). `stop` cuts a step short.
    bool RunBurst(const TickBurst& burst, const std::stop_token& stop = {});

    /// The tick counters so far.
    [[nodiscard]] const TickCounters& Counters() const {
        return m_counters;
    }

    /// The timeline epoch: 0 at the start, one more at each load or replay start, from its scene change's swap.
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

    // What a sound load or replay brings for the logic, held from its tick to its scene change's swap.
    struct Restored {
        TickCounters counters;
        std::optional<bool> paused; // a replay's pausing; a load keeps the game's
        Calendar calendar;
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
    void StartChange(const SceneFacts& next);
    void TakeRestored(const Restored& restored);
    [[nodiscard]] bool Waiting() const;
    bool Wait();
    bool TakeReport(const Action& report);
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
    SceneChange m_change; // the scene change under way, or the last one, now running: never hashed or saved
    TickCounters m_counters{};
    u64 m_epoch = 0;
    bool m_paused = false;       // the simulation's pausing: live input's, or the replay's while one plays
    bool m_windowPaused = false; // the last Pause or Resume live input sent, also while a replay plays
    bool m_advanced = false;     // whether the simulation ran in the last tick
    Calendar m_calendar = Calendar::None;
    std::optional<SceneId> m_resident = std::nullopt;       // the scene the main thread said it holds last
    std::optional<Restored> m_restored = std::nullopt;      // a sound load or replay's, until its swap
    std::optional<ActionKind> m_heldPausing = std::nullopt; // the last Pause or Resume taken during a change, for the next tick
    std::vector<Action> m_input;
    std::vector<Event> m_events;
    HeldInput m_held{}; // the tick's held input: live, or the replay's while one plays
};

} // namespace core
