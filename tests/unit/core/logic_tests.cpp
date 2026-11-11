#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/logic.hpp"
#include "core/save_file.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 SCHEDULED_NS = 1'000;
constexpr u64 PUBLISHED_NS = 1'500;

u64 PublishedClock() {
    return PUBLISHED_NS;
}

constexpr core::SceneId OTHER_SCENE{ 1 };

// Records the ticks it ran and its publishes, and publishes one instance per tick so far; a CycleScene moves it to
// OTHER_SCENE.
class Recorder final : public core::Simulation {
public:
    void Tick(const core::TickCounters& counters, const core::TickInput& input, std::vector<core::Event>& events) override {
        events.push_back({ .kind = core::EventKind::UseEnded, .use = counters.simulation });
        ticks.push_back(counters.simulation);
        states.push_back(counters.state);
        held.push_back(input.held);
        actions.push_back(input.actions.size());
        casts += static_cast<std::size_t>(std::ranges::count(input.actions, core::ActionKind::Cast, &core::Action::kind));
        if (std::ranges::contains(input.actions, core::ActionKind::CycleScene, &core::Action::kind)) {
            scene = OTHER_SCENE;
        }
    }
    void AdvanceCalendar(const core::TickCounters& counters, core::Jobs& /*jobs*/, const std::stop_token& /*stop*/) override {
        steps.push_back(counters.state);
    }
    // Its state is how many ticks it ran; a load restores that count.
    void Save(core::Writer& archive) override {
        archive(u64{ ticks.size() });
    }
    bool Load(core::Reader& archive) override {
        u64 count = 0;
        archive(count);
        if (archive.Failed() || archive.Remaining() != 0 || rejectLoads) {
            return false;
        }
        ticks.resize(count);
        return true;
    }
    [[nodiscard]] u64 StateHash() const override {
        return ticks.size() + hashSalt;
    }
    void SetData(const core::GameData& data) override {
        dataHash = data.Hash();
    }
    [[nodiscard]] u64 DataHash() const override {
        return dataHash;
    }
    [[nodiscard]] core::SceneId Scene() const override {
        return scene;
    }
    void Publish(core::Snapshot& snapshot, core::Jobs& /*jobs*/) override {
        ++publishes;
        ticksAtPublish = ticks.size();
        capacityAtPublish = snapshot.instances.capacity();
        sizeAtPublish = snapshot.instances.size();
        textsAtPublish = snapshot.texts.size();
        snapshot.instances.resize(ticks.size());
        snapshot.texts.push_back({});
    }

    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<u64> ticks{};
    std::vector<u64> states{};           // the state counter at each tick
    std::vector<u64> steps{};            // the state counter at each calendar step
    std::vector<core::HeldInput> held{}; // the held input at each tick
    std::vector<std::size_t> actions{};  // how many actions each tick took
    // NOLINTEND(readability-redundant-member-init)
    std::size_t casts = 0; // how many Cast actions it was given
    bool rejectLoads = false;
    u64 hashSalt = 0; // set to make its state hash differ from another's with the same ticks
    u64 dataHash = 0; // the hash of the game data it was given last
    core::SceneId scene{};
    std::size_t publishes = 0;
    std::size_t ticksAtPublish = 0;
    std::size_t textsAtPublish = 0;
    std::size_t capacityAtPublish = 0;
    std::size_t sizeAtPublish = 0;
};

struct BurstResult {
    bool stalled = false;
    core::Snapshot snapshot{};
};

// The logic and the systems it drives, held as a game holds them, its scene reported resident as the main thread
// does once it holds it; saves go to `slot` and replays to `replaySlot` (none by default).
struct Rig {
    explicit Rig(std::filesystem::path slot = {}, std::filesystem::path replaySlot = {})
      : saves(std::move(slot), jobs)
      , replays(std::move(replaySlot), jobs) {
        handover.SendInput({ .kind = core::ActionKind::SceneResident, .scene = simulation.scene });
        REQUIRE(Burst(1).stalled);
        simulation.publishes = 0;
    }

    // Runs a burst of `ticks` and returns what RunBurst said and published.
    BurstResult Burst(u64 ticks) {
        const bool stalled = logic.RunBurst({ .ticks = ticks, .scheduledNs = SCHEDULED_NS });
        core::Snapshot* published = handover.TakeState();
        REQUIRE(published != nullptr);
        BurstResult result{ .stalled = stalled, .snapshot = *published };
        handover.ReturnState(published);
        return result;
    }

    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    core::GameHandover handover{};
    Recorder simulation{};
    // NOLINTEND(readability-redundant-member-init)
    core::Jobs jobs{ 1 };
    core::Saves saves;
    core::Saves replays;
    core::Replay replay{};
    core::Logic logic{ handover, simulation, jobs, saves, replays, replay, PublishedClock };
};

TEST_CASE("a burst runs and publishes its ticks") {
    constexpr u64 TICKS = 3;
    Rig rig;
    const core::Snapshot snapshot = rig.Burst(TICKS).snapshot;
    CHECK(snapshot.wallTick == TICKS);
    CHECK(snapshot.simulationTick == TICKS);
    CHECK(snapshot.advanced);
}

TEST_CASE("the snapshot carries the burst's scheduled time and when it was published") {
    Rig rig;
    const core::Snapshot snapshot = rig.Burst(1).snapshot;
    CHECK(snapshot.scheduledNs == SCHEDULED_NS);
    CHECK(snapshot.publishedNs == PUBLISHED_NS);
}

TEST_CASE("paused ticks are frozen: wall ticks only, and the snapshot didn't advance") {
    constexpr u64 TICKS = 2;
    Rig rig;
    rig.handover.SendInput({ .kind = core::ActionKind::Pause });
    const core::Snapshot snapshot = rig.Burst(TICKS).snapshot;
    CHECK(snapshot.wallTick == TICKS);
    CHECK(snapshot.simulationTick == 0);
    CHECK_FALSE(snapshot.advanced);
}

TEST_CASE("the last of several actions in one tick wins") {
    Rig rig;
    rig.handover.SendInput({ .kind = core::ActionKind::Pause });
    rig.handover.SendInput({ .kind = core::ActionKind::Resume });
    CHECK(rig.Burst(1).snapshot.advanced);
}

TEST_CASE("a burst's presses go to its first tick, its held input to every tick") {
    constexpr u64 TICKS = 3;
    constexpr core::HeldInput HELD{ .move = { .x = 1.0F, .y = 0.0F } };
    Rig rig;
    rig.handover.SendInput({ .kind = core::ActionKind::Select, .target = core::StableId{ 1 } });
    rig.handover.SendHeld(HELD);
    static_cast<void>(rig.Burst(TICKS));
    CHECK(rig.simulation.actions == std::vector<std::size_t>{ 1, 0, 0 });
    CHECK(rig.simulation.held == std::vector{ HELD, HELD, HELD });
}

TEST_CASE("a cast sent while paused does nothing") {
    constexpr core::Vec2 POINT{ .x = 1.0F, .y = 2.0F };
    Rig rig;
    rig.handover.SendInput({ .kind = core::ActionKind::Pause });
    static_cast<void>(rig.Burst(1));
    rig.handover.SendInput({ .kind = core::ActionKind::Cast, .slot = core::Slot::Spender, .point = POINT });
    static_cast<void>(rig.Burst(1));
    rig.handover.SendInput({ .kind = core::ActionKind::Resume });
    static_cast<void>(rig.Burst(2));
    CHECK(rig.simulation.casts == 0);
    CHECK(rig.simulation.ticks.size() == 2);
}

TEST_CASE("pausing lasts across bursts until resumed") {
    Rig rig;
    rig.handover.SendInput({ .kind = core::ActionKind::Pause });
    static_cast<void>(rig.Burst(1));
    CHECK_FALSE(rig.Burst(1).snapshot.advanced);
    rig.handover.SendInput({ .kind = core::ActionKind::Resume });
    CHECK(rig.Burst(1).snapshot.advanced);
}

TEST_CASE("the simulation ticks once per advancing tick, in order") {
    constexpr u64 TICKS = 3;
    Rig rig;
    static_cast<void>(rig.Burst(TICKS));
    CHECK(rig.simulation.ticks == std::vector<u64>{ 1, 2, 3 });
}

TEST_CASE("paused ticks don't tick the simulation") {
    constexpr u64 TICKS = 3;
    Rig rig;
    rig.handover.SendInput({ .kind = core::ActionKind::Pause });
    static_cast<void>(rig.Burst(TICKS));
    CHECK(rig.simulation.ticks.empty());
}

TEST_CASE("a burst publishes once, after its ticks") {
    constexpr u64 TICKS = 3;
    Rig rig;
    static_cast<void>(rig.Burst(TICKS));
    CHECK(rig.simulation.publishes == 1);
    CHECK(rig.simulation.ticksAtPublish == TICKS);
}

TEST_CASE("each publish starts from empty instances and texts, the instances keeping their capacity") {
    Rig rig;
    // By the time there have been more bursts than buffers, every write reuses a buffer filled before.
    for (u64 burst = 0; burst < 2 * core::GameHandover::BUFFERS; ++burst) {
        static_cast<void>(rig.Burst(1));
        CHECK(rig.simulation.sizeAtPublish == 0);
        CHECK(rig.simulation.textsAtPublish == 0);
        if (burst >= core::GameHandover::BUFFERS) {
            CHECK(rig.simulation.capacityAtPublish >= burst + 1 - core::GameHandover::BUFFERS);
        }
    }
}

TEST_CASE("a calendar step asked for ends the burst at once and publishes the transition first") {
    constexpr u64 TICKS = 3;
    Rig rig;
    rig.handover.SendInput({ .kind = core::ActionKind::AdvanceCalendar });
    const BurstResult result = rig.Burst(TICKS);
    CHECK_FALSE(result.stalled);
    CHECK(result.snapshot.wallTick == 1);
    CHECK(result.snapshot.transition);
    CHECK_FALSE(result.snapshot.advanced);
    CHECK(result.snapshot.simulationTick == 0);
    CHECK(rig.simulation.steps.empty());
}

TEST_CASE("the tick after the transition runs the step, changing the state, and ends its burst") {
    constexpr u64 TICKS = 3;
    Rig rig;
    rig.handover.SendInput({ .kind = core::ActionKind::AdvanceCalendar });
    static_cast<void>(rig.Burst(1));

    const BurstResult result = rig.Burst(TICKS);
    CHECK(result.stalled);
    CHECK(rig.simulation.steps == std::vector<u64>{ 1 });
    CHECK(rig.simulation.ticks.empty());
    CHECK(result.snapshot.wallTick == 2);
    CHECK_FALSE(result.snapshot.transition);
    CHECK_FALSE(result.snapshot.advanced);

    static_cast<void>(rig.Burst(1));
    CHECK(rig.simulation.ticks == std::vector<u64>{ 1 });
    CHECK(rig.simulation.states == std::vector<u64>{ 2 }); // the step counted as a change of state
}

TEST_CASE("input sent during the transition waits until after the step") {
    Rig rig;
    rig.handover.SendInput({ .kind = core::ActionKind::AdvanceCalendar });
    static_cast<void>(rig.Burst(1));
    rig.handover.SendInput({ .kind = core::ActionKind::Pause });
    CHECK(rig.Burst(1).stalled);
    CHECK_FALSE(rig.Burst(1).snapshot.advanced);
    CHECK(rig.simulation.ticks.empty());
}

// A rig whose simulation now needs a scene the main thread hasn't reported resident.
struct WaitingRig : Rig {
    WaitingRig() {
        simulation.scene = OTHER_SCENE;
    }
};

TEST_CASE("until its scene is resident, a burst runs no tick and publishes the scene it waits for") {
    constexpr u64 TICKS = 3;
    WaitingRig rig;
    const BurstResult result = rig.Burst(TICKS);
    CHECK_FALSE(result.stalled);
    CHECK(result.snapshot.wallTick == 0);
    CHECK_FALSE(result.snapshot.advanced);
    CHECK(result.snapshot.scene == OTHER_SCENE);
    CHECK(rig.simulation.ticks.empty());
    CHECK(rig.simulation.publishes == 1);
}

TEST_CASE("the burst that takes the scene's SceneResident stalls, and the next one ticks") {
    WaitingRig rig;
    rig.handover.SendInput({ .kind = core::ActionKind::SceneResident, .scene = OTHER_SCENE });
    CHECK(rig.Burst(1).stalled);
    CHECK(rig.Burst(1).snapshot.simulationTick == 1);
}

TEST_CASE("a SceneResident naming another scene leaves the burst waiting") {
    WaitingRig rig;
    rig.handover.SendInput({ .kind = core::ActionKind::SceneResident, .scene = core::SceneId{} });
    CHECK_FALSE(rig.Burst(1).stalled);
    CHECK(rig.Burst(1).snapshot.wallTick == 0);
}

TEST_CASE("a tick that changes the scene ends its burst, and the next burst waits for the scene") {
    constexpr u64 TICKS = 3;
    Rig rig;
    rig.handover.SendInput({ .kind = core::ActionKind::CycleScene });
    const core::Snapshot changed = rig.Burst(TICKS).snapshot;
    CHECK(changed.wallTick == 1);
    CHECK(changed.scene == OTHER_SCENE);
    CHECK(rig.Burst(TICKS).snapshot.wallTick == 1);
}

TEST_CASE("a Pause sent while the scene loads holds once it is resident") {
    WaitingRig rig;
    rig.handover.SendInput({ .kind = core::ActionKind::Pause });
    static_cast<void>(rig.Burst(1));
    rig.handover.SendInput({ .kind = core::ActionKind::SceneResident, .scene = OTHER_SCENE });
    static_cast<void>(rig.Burst(1));
    const core::Snapshot snapshot = rig.Burst(1).snapshot;
    CHECK(snapshot.wallTick == 1);
    CHECK(snapshot.simulationTick == 0);
}

TEST_CASE("other actions sent while the scene loads are dropped") {
    WaitingRig rig;
    rig.handover.SendInput({ .kind = core::ActionKind::Select });
    rig.handover.SendInput({ .kind = core::ActionKind::AdvanceCalendar });
    static_cast<void>(rig.Burst(1));
    rig.handover.SendInput({ .kind = core::ActionKind::SceneResident, .scene = OTHER_SCENE });
    static_cast<void>(rig.Burst(1));
    const core::Snapshot snapshot = rig.Burst(1).snapshot;
    CHECK_FALSE(snapshot.transition);
    REQUIRE(rig.simulation.actions.size() == 1);
    CHECK(rig.simulation.actions.front() == 0);
}

// A save slot in a folder of its own, removed when the test ends.
class SaveSlot {
public:
    explicit SaveSlot(std::string_view name)
      : m_folder{ std::filesystem::temp_directory_path() / std::format("project_n_logic_tests_{}", name) } {
        std::filesystem::remove_all(m_folder);
    }
    SaveSlot(const SaveSlot&) = delete;
    SaveSlot& operator=(const SaveSlot&) = delete;
    SaveSlot(SaveSlot&&) = delete;
    SaveSlot& operator=(SaveSlot&&) = delete;
    ~SaveSlot() {
        std::error_code ignored;
        std::filesystem::remove_all(m_folder, ignored);
    }

    [[nodiscard]] std::filesystem::path File() const {
        return m_folder / "slot.pns";
    }

private:
    std::filesystem::path m_folder;
};

TEST_CASE("a save then a load restore the counters and the state, and start a new timeline") {
    constexpr u64 BEFORE = 3;
    constexpr u64 AFTER = 2;
    const SaveSlot slot{ "round_trip" };
    Rig rig(slot.File());
    static_cast<void>(rig.Burst(BEFORE));
    rig.handover.SendInput({ .kind = core::ActionKind::Save });
    CHECK(rig.Burst(1).stalled);
    rig.jobs.WaitForBackground();
    REQUIRE(std::filesystem::exists(slot.File()));
    const core::TickCounters saved = rig.logic.Counters();
    CHECK(saved.wall == BEFORE + 1);

    static_cast<void>(rig.Burst(AFTER));
    rig.handover.SendInput({ .kind = core::ActionKind::Load });
    const BurstResult loaded = rig.Burst(1);
    CHECK(loaded.stalled);
    CHECK(rig.logic.Counters().wall == saved.wall);
    CHECK(rig.logic.Counters().simulation == saved.simulation);
    CHECK(rig.logic.Counters().state == saved.state);
    CHECK(rig.simulation.ticks.size() == BEFORE);
    CHECK(rig.logic.Epoch() == 1);
    CHECK(loaded.snapshot.epoch == 1);
}

TEST_CASE("a load with no save changes nothing but the tick count") {
    const SaveSlot slot{ "no_save" };
    Rig rig(slot.File());
    rig.handover.SendInput({ .kind = core::ActionKind::Load });
    CHECK(rig.Burst(1).stalled);
    CHECK(rig.logic.Epoch() == 0);
    CHECK(rig.logic.Counters().wall == 1);
    CHECK(rig.logic.Counters().simulation == 0);
}

TEST_CASE("a load the simulation rejects changes nothing but the tick count") {
    constexpr u64 TICKS = 2;
    const SaveSlot slot{ "rejected" };
    Rig rig(slot.File());
    rig.handover.SendInput({ .kind = core::ActionKind::Save });
    static_cast<void>(rig.Burst(1));
    static_cast<void>(rig.Burst(TICKS));
    rig.simulation.rejectLoads = true;
    rig.handover.SendInput({ .kind = core::ActionKind::Load });
    static_cast<void>(rig.Burst(1));
    CHECK(rig.logic.Epoch() == 0);
    CHECK(rig.logic.Counters().wall == TICKS + 2);
    CHECK(rig.simulation.ticks.size() == TICKS);
}

TEST_CASE("with no save slot, Save and Load are refused") {
    Rig rig;
    rig.handover.SendInput({ .kind = core::ActionKind::Save });
    CHECK(rig.Burst(1).stalled);
    rig.handover.SendInput({ .kind = core::ActionKind::Load });
    CHECK(rig.Burst(1).stalled);
    CHECK(rig.logic.Epoch() == 0);
}

TEST_CASE("the step's wall tick doesn't depend on how the clock split the ticks into bursts") {
    constexpr u64 LONG_BURSTS = 3;
    Rig longBursts;
    Rig shortBursts;
    longBursts.handover.SendInput({ .kind = core::ActionKind::AdvanceCalendar });
    shortBursts.handover.SendInput({ .kind = core::ActionKind::AdvanceCalendar });
    static_cast<void>(longBursts.Burst(LONG_BURSTS));
    static_cast<void>(longBursts.Burst(LONG_BURSTS));
    static_cast<void>(shortBursts.Burst(1));
    static_cast<void>(shortBursts.Burst(1));
    CHECK(longBursts.simulation.steps == shortBursts.simulation.steps);
    CHECK(longBursts.logic.Counters().wall == shortBursts.logic.Counters().wall);
    CHECK(longBursts.logic.Counters().state == shortBursts.logic.Counters().state);
}

// Records a replay into `replaySlot`: some ticks, a pause, a calendar step and a save, in bursts of
// `burst`, ending right after a hash tick so the last recorded state is checked. Returns the logic's
// hash at the end of the recording.
u64 Record(const std::filesystem::path& slot, const std::filesystem::path& replaySlot, u64 burst) {
    constexpr u64 TICKS = 70;
    Rig rig(slot, replaySlot);
    rig.handover.SendInput({ .kind = core::ActionKind::ToggleRecording });
    static_cast<void>(rig.Burst(1));
    static_cast<void>(rig.Burst(TICKS));
    rig.handover.SendInput({ .kind = core::ActionKind::Pause });
    static_cast<void>(rig.Burst(burst));
    rig.handover.SendInput({ .kind = core::ActionKind::Resume });
    rig.handover.SendInput({ .kind = core::ActionKind::AdvanceCalendar });
    static_cast<void>(rig.Burst(burst));
    static_cast<void>(rig.Burst(burst));
    rig.handover.SendInput({ .kind = core::ActionKind::Save });
    static_cast<void>(rig.Burst(burst));
    while (rig.logic.Counters().wall % core::REPLAY_HASH_TICKS != 0) {
        static_cast<void>(rig.Burst(1));
    }
    const u64 hash = rig.logic.Hash();
    rig.handover.SendInput({ .kind = core::ActionKind::ToggleRecording });
    static_cast<void>(rig.Burst(1));
    return hash;
}

// Starts playing the replay in `rig`'s replay slot.
void StartPlaying(Rig& rig) {
    rig.handover.SendInput({ .kind = core::ActionKind::PlayReplay });
    static_cast<void>(rig.Burst(1));
    REQUIRE(rig.replay.Playing());
}

// Plays the replay in `rig`'s replay slot, a tick per burst so it stops exactly at the replay's end.
void Play(Rig& rig) {
    constexpr u32 MAX_BURSTS = 1'000;
    StartPlaying(rig);
    for (u32 bursts = 0; bursts < MAX_BURSTS && rig.replay.Playing() && !rig.replay.AtEnd(); ++bursts) {
        static_cast<void>(rig.Burst(1));
    }
}

// A save slot and a replay recorded beside it in bursts of 3, and a rig over both.
struct Recorded {
    explicit Recorded(std::string_view name)
      : slot{ name }
      , hash{ Record(slot.File(), ReplayFile(), RECORD_BURSTS) }
      , rig(slot.File(), ReplayFile()) {
    }

    [[nodiscard]] std::filesystem::path ReplayFile() const {
        return slot.File().parent_path() / "replay.pnr";
    }

    static constexpr u64 RECORD_BURSTS = 3;
    SaveSlot slot;
    u64 hash;
    Rig rig;
};

TEST_CASE("a replay recorded in bursts of 3 plays to its end in bursts of 1") {
    Recorded recorded{ "replay_to_end" };
    Play(recorded.rig);
    CHECK(recorded.rig.replay.AtEnd());
}

TEST_CASE("every hash of a replay played back matches its recording") {
    Recorded recorded{ "replay_hashes" };
    Play(recorded.rig);
    CHECK(recorded.rig.replay.HashCount() == 2);
    CHECK(recorded.rig.replay.Matched() == 2);
}

TEST_CASE("a replay played back ends in the state it was recorded to") {
    Recorded recorded{ "replay_end_state" };
    Play(recorded.rig);
    CHECK(recorded.rig.logic.Hash() == recorded.hash);
}

TEST_CASE("a replay plays its calendar step back") {
    Recorded recorded{ "replay_step" };
    Play(recorded.rig);
    CHECK(recorded.rig.simulation.steps.size() == 1);
}

TEST_CASE("playing a replay starts a new timeline") {
    Recorded recorded{ "replay_epoch" };
    StartPlaying(recorded.rig);
    CHECK(recorded.rig.logic.Epoch() == 1);
}

TEST_CASE("a replay that diverges from its recording stops at the next hash check") {
    constexpr u64 SALT = 1'000;
    Recorded recorded{ "replay_divergence" };
    recorded.rig.simulation.hashSalt = SALT;
    Play(recorded.rig);
    CHECK_FALSE(recorded.rig.replay.Playing());
    CHECK_FALSE(recorded.rig.replay.AtEnd());
}

TEST_CASE("after its last tick, a replay stops by itself and live input takes over") {
    Recorded recorded{ "replay_live_after" };
    Play(recorded.rig);
    recorded.rig.handover.SendInput({ .kind = core::ActionKind::Pause });
    const BurstResult result = recorded.rig.Burst(1);
    CHECK_FALSE(recorded.rig.replay.Playing());
    CHECK_FALSE(result.snapshot.advanced);
}

TEST_CASE("PlayReplay sent while a replay plays stops it") {
    Recorded recorded{ "replay_stop" };
    StartPlaying(recorded.rig);
    recorded.rig.handover.SendInput({ .kind = core::ActionKind::PlayReplay });
    static_cast<void>(recorded.rig.Burst(1));
    CHECK_FALSE(recorded.rig.replay.Playing());
    CHECK_FALSE(recorded.rig.replay.AtEnd());
}

TEST_CASE("live input sent while a replay plays, a Load or a recording included, is dropped") {
    Recorded recorded{ "replay_drops_input" };
    StartPlaying(recorded.rig);
    recorded.rig.handover.SendInput({ .kind = core::ActionKind::Load });
    recorded.rig.handover.SendInput({ .kind = core::ActionKind::ToggleRecording });
    static_cast<void>(recorded.rig.Burst(1));
    CHECK(recorded.rig.replay.Playing());
    CHECK_FALSE(recorded.rig.replay.Recording());
    CHECK(recorded.rig.logic.Epoch() == 1);
}

TEST_CASE("a Pause sent while a replay plays holds once the replay ends by itself") {
    constexpr u32 MAX_BURSTS = 1'000;
    Recorded recorded{ "replay_pause_holds" };
    StartPlaying(recorded.rig);
    recorded.rig.handover.SendInput({ .kind = core::ActionKind::Pause });
    for (u32 bursts = 0; bursts < MAX_BURSTS && recorded.rig.replay.Playing() && !recorded.rig.replay.AtEnd(); ++bursts) {
        static_cast<void>(recorded.rig.Burst(1));
    }
    REQUIRE(recorded.rig.replay.AtEnd());
    const BurstResult result = recorded.rig.Burst(1);
    CHECK_FALSE(recorded.rig.replay.Playing());
    CHECK_FALSE(result.snapshot.advanced);
}

TEST_CASE("a SceneResident on a running tick reaches neither the simulation nor the replay") {
    const SaveSlot slot{ "scene_resident_unrecorded" };
    Rig rig(slot.File(), slot.File().parent_path() / "replay.pnr");
    rig.handover.SendInput({ .kind = core::ActionKind::ToggleRecording });
    static_cast<void>(rig.Burst(1));
    rig.handover.SendInput({ .kind = core::ActionKind::SceneResident, .scene = rig.simulation.scene });
    static_cast<void>(rig.Burst(1));
    CHECK(rig.simulation.actions.back() == 0);
    rig.handover.SendInput({ .kind = core::ActionKind::ToggleRecording });
    static_cast<void>(rig.Burst(1));
    StartPlaying(rig); // the replay reader refuses a SceneResident
}

TEST_CASE("a Save arriving in the same tick as ToggleRecording still writes the save file") {
    const SaveSlot slot{ "save_with_recording" };
    Rig rig(slot.File(), slot.File().parent_path() / "replay.pnr");
    rig.handover.SendInput({ .kind = core::ActionKind::Save });
    rig.handover.SendInput({ .kind = core::ActionKind::ToggleRecording });
    static_cast<void>(rig.Burst(1));
    rig.jobs.WaitForBackground();
    const bool saved = std::filesystem::exists(slot.File());
    CHECK(saved);
}

TEST_CASE("a load while recording finishes the recording and writes it") {
    const SaveSlot slot{ "replay_load_finishes" };
    const std::filesystem::path replayFile = slot.File().parent_path() / "replay.pnr";
    Rig rig(slot.File(), replayFile);
    rig.handover.SendInput({ .kind = core::ActionKind::Save });
    static_cast<void>(rig.Burst(1));
    rig.handover.SendInput({ .kind = core::ActionKind::ToggleRecording });
    static_cast<void>(rig.Burst(1));
    rig.handover.SendInput({ .kind = core::ActionKind::Load });
    static_cast<void>(rig.Burst(1));
    rig.jobs.WaitForBackground();
    CHECK_FALSE(rig.replay.Recording());
    CHECK(std::filesystem::exists(replayFile));
}

// New game data, unlike the default.
std::shared_ptr<const core::GameData> NewData() {
    constexpr core::GameData DATA{ .cultivator = { .walkSpeed = 1.0F } };
    return std::make_shared<const core::GameData>(DATA);
}

TEST_CASE("ReloadData gives the simulation the data on a frozen tick that ends the burst") {
    constexpr u64 TICKS = 3;
    Rig rig;
    const std::shared_ptr<const core::GameData> data = NewData();
    rig.handover.SendInput({ .kind = core::ActionKind::ReloadData, .data = data });
    const BurstResult result = rig.Burst(TICKS);
    CHECK(rig.simulation.dataHash == data->Hash());
    CHECK_FALSE(result.stalled);
    CHECK(result.snapshot.wallTick == 1);
    CHECK(result.snapshot.simulationTick == 0);
}

TEST_CASE("ReloadData sent while the scene loads reaches the simulation once the scene is resident") {
    WaitingRig rig;
    const std::shared_ptr<const core::GameData> data = NewData();
    rig.handover.SendInput({ .kind = core::ActionKind::ReloadData, .data = data });
    static_cast<void>(rig.Burst(1));
    rig.handover.SendInput({ .kind = core::ActionKind::SceneResident, .scene = OTHER_SCENE });
    static_cast<void>(rig.Burst(1));
    static_cast<void>(rig.Burst(1));
    CHECK(rig.simulation.dataHash == data->Hash());
}

TEST_CASE("ReloadData while recording finishes the recording and writes it") {
    const SaveSlot slot{ "replay_reload_finishes" };
    const std::filesystem::path replayFile = slot.File().parent_path() / "replay.pnr";
    Rig rig(slot.File(), replayFile);
    rig.handover.SendInput({ .kind = core::ActionKind::ToggleRecording });
    static_cast<void>(rig.Burst(1));
    rig.handover.SendInput({ .kind = core::ActionKind::ReloadData, .data = NewData() });
    static_cast<void>(rig.Burst(1));
    rig.jobs.WaitForBackground();
    CHECK_FALSE(rig.replay.Recording());
    CHECK(std::filesystem::exists(replayFile));
}

TEST_CASE("ReloadData while a replay plays stops it, and the data is taken") {
    Recorded recorded{ "replay_reload_stops" };
    StartPlaying(recorded.rig);
    const std::shared_ptr<const core::GameData> data = NewData();
    recorded.rig.handover.SendInput({ .kind = core::ActionKind::ReloadData, .data = data });
    static_cast<void>(recorded.rig.Burst(1));
    CHECK_FALSE(recorded.rig.replay.Playing());
    CHECK(recorded.rig.simulation.dataHash == data->Hash());
}

TEST_CASE("a replay recorded under other game data doesn't play") {
    Recorded recorded{ "replay_other_data" };
    recorded.rig.simulation.SetData(*NewData());
    recorded.rig.handover.SendInput({ .kind = core::ActionKind::PlayReplay });
    static_cast<void>(recorded.rig.Burst(1));
    CHECK_FALSE(recorded.rig.replay.Playing());
    CHECK(recorded.rig.logic.Epoch() == 0);
}

TEST_CASE("with no replay file, playing is refused") {
    const SaveSlot slot{ "no_replay" };
    Rig rig(slot.File(), slot.File().parent_path() / "replay.pnr");
    rig.handover.SendInput({ .kind = core::ActionKind::PlayReplay });
    CHECK(rig.Burst(1).stalled);
    CHECK_FALSE(rig.replay.Playing());
    CHECK(rig.logic.Epoch() == 0);
}

TEST_CASE("a replay whose start state the logic rejects doesn't play, and nothing changes") {
    constexpr u8 NO_SUCH_CALENDAR_PHASE = 7;
    const SaveSlot slot{ "replay_bad_start" };
    const std::filesystem::path replayFile = slot.File().parent_path() / "replay.pnr";
    std::vector<std::byte> start;
    core::Writer writer{ start };
    writer(false, NO_SUCH_CALENDAR_PHASE);
    core::Replay recording;
    recording.StartRecording(std::move(start));
    const auto written = core::WriteSaveFile(replayFile, recording.FinishRecording());
    REQUIRE(written.has_value());

    Rig rig(slot.File(), replayFile);
    rig.handover.SendInput({ .kind = core::ActionKind::PlayReplay });
    static_cast<void>(rig.Burst(1));
    CHECK_FALSE(rig.replay.Playing());
    CHECK(rig.logic.Epoch() == 0);
    CHECK(rig.logic.Counters().wall == 1);
    CHECK(rig.logic.Counters().simulation == 0);
}

TEST_CASE("PlayReplay sent while recording finishes the recording and plays it") {
    constexpr u64 TICKS = 5;
    const SaveSlot slot{ "replay_play_while_recording" };
    Rig rig(slot.File(), slot.File().parent_path() / "replay.pnr");
    rig.handover.SendInput({ .kind = core::ActionKind::ToggleRecording });
    static_cast<void>(rig.Burst(1));
    static_cast<void>(rig.Burst(TICKS));
    rig.handover.SendInput({ .kind = core::ActionKind::PlayReplay });
    static_cast<void>(rig.Burst(1));
    CHECK_FALSE(rig.replay.Recording());
    CHECK(rig.replay.Playing());
    CHECK(rig.logic.Epoch() == 1);
}

TEST_CASE("logic stamps and publishes a tick's events") {
    constexpr u64 TICKS = 3;
    constexpr u64 EPOCH = 1;
    const SaveSlot slot{ "stamped_events" };
    Rig rig(slot.File());
    rig.handover.SendInput({ .kind = core::ActionKind::Save });
    static_cast<void>(rig.Burst(1));
    rig.jobs.WaitForBackground();
    rig.handover.SendInput({ .kind = core::ActionKind::Load });
    static_cast<void>(rig.Burst(1)); // a new timeline: its epoch is not the events' default
    REQUIRE(rig.logic.Epoch() == EPOCH);
    std::vector<core::Event> events;
    rig.handover.TakeEvents(events); // those of the earlier ticks
    const u64 first = rig.logic.Counters().simulation + 1;
    static_cast<void>(rig.Burst(TICKS));
    rig.handover.TakeEvents(events);
    REQUIRE(events.size() == TICKS);
    for (u64 index = 0; index < TICKS; ++index) {
        CHECK(events.at(index).tick == first + index);
        CHECK(events.at(index).use == first + index);
        CHECK(events.at(index).epoch == EPOCH);
    }
}

} // namespace
