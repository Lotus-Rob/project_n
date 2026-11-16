#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/logic.hpp"
#include "core/save_file.hpp"
#include "core/scene_reports.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 SCHEDULED_NS = 1'000;
constexpr u64 PUBLISHED_NS = 1'500;

u64 PublishedClock() {
    return PUBLISHED_NS;
}

constexpr core::SceneId OTHER_SCENE{ 1 };
constexpr core::SceneKey OTHER_KEY{ 1 };

// What the logic asked of the simulation, in order.
enum class Call : u8 {
    Tick,
    EnterScene,
    Publish,
};

// Records the ticks it ran and its publishes (with the view each was given), and publishes one instance per tick so far,
// following with the camera when told to; a CycleScene asks for the scene `asks` names (OTHER_SCENE by default), and a load for
// the scene it saved, which it enters when told.
class Recorder final : public core::Simulation {
public:
    void Tick(const core::TickCounters& counters, const core::TickInput& input, std::vector<core::Event>& events) override {
        calls.push_back(Call::Tick);
        events.push_back({ .kind = core::EventKind::UseEnded, .use = counters.simulation });
        ticks.push_back(counters.simulation);
        states.push_back(counters.state);
        held.push_back(input.held);
        actions.push_back(input.actions.size());
        casts += static_cast<std::size_t>(std::ranges::count(input.actions, core::ActionKind::Cast, &core::Action::kind));
        if (std::ranges::contains(input.actions, core::ActionKind::CycleScene, &core::Action::kind)) {
            requested = asks;
        }
    }
    void AdvanceCalendar(const core::TickCounters& counters, core::Jobs& /*jobs*/, const std::stop_token& /*stop*/) override {
        steps.push_back(counters.state);
    }
    [[nodiscard]] bool MonthDue() const override {
        return dueAfter.has_value() && ticks.size() >= *dueAfter && steps.empty();
    }
    // Its state is how many ticks it ran, in its scene; a load holds them until EnterScene() restores them.
    void Save(core::Writer& archive) override {
        archive(u64{ ticks.size() }, key, scene);
    }
    bool Load(core::Reader& archive, u64 /*simulationTick*/) override {
        u64 count = 0;
        core::SceneFacts facts;
        archive(count, facts.key, facts.sprites);
        if (archive.Failed() || archive.Remaining() != 0 || rejectLoads) {
            return false;
        }
        loaded = { .ticks = count, .scene = facts };
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
    [[nodiscard]] core::SceneFacts Current() const override {
        return { .key = key, .sprites = scene };
    }
    [[nodiscard]] std::optional<core::SceneFacts> Requested() const override {
        return loaded ? std::optional{ loaded->scene } : requested;
    }
    void EnterScene() override {
        REQUIRE((loaded.has_value() || requested.has_value()));
        calls.push_back(Call::EnterScene);
        const core::SceneFacts entered = Requested().value_or(core::SceneFacts{});
        if (loaded) {
            ticks.resize(loaded->ticks);
        }
        key = entered.key;
        scene = entered.sprites;
        loaded.reset();
        requested.reset();
    }
    void Publish(core::Snapshot& snapshot, core::Jobs& /*jobs*/, const std::optional<core::Rect>& view) override {
        calls.push_back(Call::Publish);
        views.push_back(view);
        ++publishes;
        ticksAtPublish = ticks.size();
        capacityAtPublish = snapshot.instances.capacity();
        sizeAtPublish = snapshot.instances.size();
        textsAtPublish = snapshot.texts.size();
        cameraAtPublish = snapshot.camera;
        if (followCamera) {
            snapshot.camera = core::CameraKind::Follow;
        }
        snapshot.instances.resize(ticks.size());
        snapshot.texts.push_back({});
    }

    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<u64> ticks{};
    std::vector<u64> states{};           // the state counter at each tick
    std::vector<u64> steps{};            // the state counter at each calendar step
    std::vector<core::HeldInput> held{}; // the held input at each tick
    std::vector<std::size_t> actions{};  // how many actions each tick took
    std::vector<Call> calls{};
    std::vector<std::optional<core::Rect>> views{}; // the view each publish was given
    // NOLINTEND(readability-redundant-member-init)
    std::size_t casts = 0; // how many Cast actions it was given
    // After this many ticks a month is due (MonthDue()), until a calendar step runs; none is ever due by default.
    std::optional<std::size_t> dueAfter = std::nullopt;
    bool rejectLoads = false;
    u64 hashSalt = 0; // set to make its state hash differ from another's with the same ticks
    u64 dataHash = 0; // the hash of the game data it was given last
    core::SceneKey key{};
    core::SceneId scene{};
    std::optional<core::SceneFacts> requested = std::nullopt;
    // A load held until EnterScene(): how many ticks it ran, and its scene.
    struct Loaded {
        u64 ticks = 0;
        core::SceneFacts scene{};
    };
    std::optional<Loaded> loaded = std::nullopt;
    core::SceneFacts asks{ .key = OTHER_KEY, .sprites = OTHER_SCENE }; // the scene a CycleScene asks for
    bool followCamera = false;
    core::CameraKind cameraAtPublish = core::CameraKind::Pan;
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

// The logic and the systems it drives, held as a game holds them, before the game's start has been answered; saves go
// to `slot` and replays to `replaySlot` (none by default).
struct Unstarted {
    explicit Unstarted(std::filesystem::path slot = {}, std::filesystem::path replaySlot = {})
      : saves(std::move(slot), jobs)
      , replays(std::move(replaySlot), jobs) {
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

    // Answers `snapshot` as App does with a scene it loads at once: reports its scene resident when it is new, and the
    // stage of its scene change through core::SceneReports.
    void Answer(const core::Snapshot& snapshot) {
        if (snapshot.scene != reported) {
            reported = snapshot.scene;
            handover.SendInput({ .kind = core::ActionKind::SceneResident, .scene = snapshot.scene });
        }
        if (const std::optional<core::Action> report = reports.Answer(snapshot.sceneChange)) {
            handover.SendInput(*report);
        }
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
    core::SceneReports reports{};
    std::optional<core::SceneId> reported = std::nullopt; // the scene Answer() reported resident last
};

// Unstarted's systems once the game's start has run: its scene loaded, entered and running.
struct Rig : Unstarted {
    explicit Rig(std::filesystem::path slot = {}, std::filesystem::path replaySlot = {})
      : Unstarted(std::move(slot), std::move(replaySlot)) {
        Answer(Burst(1).snapshot); // loading: its scene is reported resident
        Answer(Burst(1).snapshot); // entering: the way in is reported played
        REQUIRE(Burst(1).stalled);
        simulation.publishes = 0;
    }
};

// Answers the scene change `result` shows `late` bursts after each stage shows, as a slow main thread would (at once by
// default), in bursts of 3, until the change ends; returns the burst that ended it.
BurstResult Settle(Unstarted& rig, BurstResult result, u32 late = 0) {
    constexpr u64 TICKS = 3;
    constexpr u32 MAX_STAGES = 10;
    for (u32 stage = 0; stage < MAX_STAGES && result.snapshot.sceneChange.stage != core::SceneStage::Running; ++stage) {
        for (u32 waited = 0; waited < late; ++waited) {
            result = rig.Burst(TICKS);
        }
        rig.Answer(result.snapshot);
        result = rig.Burst(TICKS);
    }
    REQUIRE(result.snapshot.sceneChange.stage == core::SceneStage::Running);
    return result;
}

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

TEST_CASE("each publish starts with the camera panning, whatever the publish before said") {
    Rig rig;
    rig.simulation.followCamera = true;
    // By the time there have been more bursts than buffers, every write reuses a buffer a Follow was published in.
    for (u64 burst = 0; burst < 2 * core::GameHandover::BUFFERS; ++burst) {
        CHECK(rig.Burst(1).snapshot.camera == core::CameraKind::Follow);
        CHECK(rig.simulation.cameraAtPublish == core::CameraKind::Pan);
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

TEST_CASE("a month the simulation says is due runs as a calendar step") {
    constexpr u64 TICKS = 5;
    constexpr std::size_t DUE_AFTER = 3;
    Rig rig;
    rig.simulation.dueAfter = DUE_AFTER;
    const BurstResult due = rig.Burst(TICKS);
    CHECK_FALSE(due.stalled);
    CHECK(due.snapshot.wallTick == 3); // the burst ends after the tick that made it due
    CHECK(due.snapshot.simulationTick == 3);
    CHECK(due.snapshot.advanced);
    CHECK(due.snapshot.transition);
    CHECK(rig.simulation.steps.empty());

    const BurstResult step = rig.Burst(TICKS);
    CHECK(step.stalled);
    CHECK(rig.simulation.steps == std::vector<u64>{ 4 }); // on the next tick, as a change of state
    CHECK(rig.simulation.ticks.size() == DUE_AFTER);
    CHECK_FALSE(step.snapshot.transition);

    static_cast<void>(rig.Burst(TICKS));
    CHECK(rig.simulation.steps.size() == 1); // once
    CHECK(rig.simulation.ticks.size() == DUE_AFTER + TICKS);
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
    const core::Snapshot asked = rig.Burst(TICKS).snapshot;
    CHECK(asked.wallTick == 1);
    rig.Answer(asked); // the way out played
    const core::Snapshot changed = rig.Burst(TICKS).snapshot;
    CHECK(changed.scene == OTHER_SCENE);
    CHECK(rig.Burst(TICKS).snapshot.wallTick == 1);
}

TEST_CASE("a scene asked for is entered after the tick that asks") {
    constexpr u64 TICKS = 3;
    Rig rig;
    rig.simulation.calls.clear();
    rig.handover.SendInput({ .kind = core::ActionKind::CycleScene });
    const core::Snapshot asked = rig.Burst(TICKS).snapshot;
    rig.Answer(asked); // the way out played
    const core::Snapshot changed = rig.Burst(TICKS).snapshot;
    const std::vector<Call> expected{ Call::Tick, Call::Publish, Call::EnterScene, Call::Publish };
    CHECK(rig.simulation.calls == expected);
    CHECK(changed.wallTick == 1);
    CHECK(changed.scene == OTHER_SCENE);
    CHECK_FALSE(rig.simulation.requested.has_value());
}

TEST_CASE("a scene asked for with the same sprites ends its burst too") {
    constexpr u64 TICKS = 3;
    Rig rig;
    rig.simulation.asks = { .key = OTHER_KEY, .sprites = rig.simulation.scene }; // another scene sharing the sprite set
    rig.simulation.calls.clear();
    rig.handover.SendInput({ .kind = core::ActionKind::CycleScene });
    const core::Snapshot asked = rig.Burst(TICKS).snapshot;
    rig.Answer(asked); // the way out played
    const core::Snapshot changed = rig.Burst(TICKS).snapshot;
    const std::vector<Call> expected{ Call::Tick, Call::Publish, Call::EnterScene, Call::Publish };
    CHECK(rig.simulation.calls == expected);
    CHECK(changed.wallTick == 1);
    CHECK(changed.sceneChange.stage == core::SceneStage::Entering); // its sprites are resident already
    CHECK(rig.simulation.key == OTHER_KEY);
}

TEST_CASE("a scene change leaves, swaps, loads and enters without a tick") {
    constexpr u64 TICKS = 3;
    constexpr u64 CHANGE = 2;
    constexpr core::SceneFacts START{};
    constexpr core::SceneFacts OTHER{ .key = OTHER_KEY, .sprites = OTHER_SCENE };
    Rig rig;
    static_cast<void>(rig.Burst(TICKS));
    rig.simulation.calls.clear();
    rig.handover.SendInput({ .kind = core::ActionKind::CycleScene });
    const BurstResult asked = rig.Burst(TICKS);
    const core::TickCounters counters = rig.logic.Counters();
    CHECK(counters.wall == TICKS + 1);
    CHECK(asked.snapshot.sceneChange.stage == core::SceneStage::Leaving);
    CHECK(asked.snapshot.sceneChange.from == START);
    CHECK(asked.snapshot.sceneChange.to == OTHER);
    CHECK(asked.snapshot.sceneChange.change == CHANGE);
    CHECK(asked.snapshot.scene == START.sprites); // the old scene shows while it leaves

    const BurstResult leaving = rig.Burst(TICKS); // no report yet
    CHECK(leaving.snapshot.sceneChange.stage == core::SceneStage::Leaving);
    rig.handover.SendInput({ .kind = core::ActionKind::SceneLeft, .change = CHANGE });
    const BurstResult left = rig.Burst(TICKS);
    CHECK_FALSE(left.stalled);
    CHECK(left.snapshot.sceneChange.stage == core::SceneStage::Loading);
    CHECK(left.snapshot.scene == OTHER_SCENE);
    rig.handover.SendInput({ .kind = core::ActionKind::SceneResident, .scene = OTHER_SCENE });
    const BurstResult resident = rig.Burst(TICKS);
    CHECK_FALSE(resident.stalled);
    CHECK(resident.snapshot.sceneChange.stage == core::SceneStage::Entering);
    rig.handover.SendInput({ .kind = core::ActionKind::SceneEntered, .change = CHANGE });
    const BurstResult entered = rig.Burst(TICKS);
    CHECK(entered.stalled);
    CHECK(entered.snapshot.sceneChange.stage == core::SceneStage::Running);
    CHECK(entered.snapshot.sceneChange.from == OTHER);
    CHECK(entered.snapshot.sceneChange.to == OTHER);

    const std::vector<Call> expected{ Call::Tick,    Call::Publish, Call::Publish, Call::EnterScene,
                                      Call::Publish, Call::Publish, Call::Publish };
    CHECK(rig.simulation.calls == expected);
    CHECK(rig.logic.Counters().wall == counters.wall);
    CHECK(rig.logic.Counters().simulation == counters.simulation);
    CHECK(rig.logic.Counters().state == counters.state);
    CHECK(rig.Burst(1).snapshot.simulationTick == counters.simulation + 1);
}

TEST_CASE("the start loads, then enters") {
    constexpr core::SceneFacts START{};
    Unstarted rig;
    const BurstResult loading = rig.Burst(1);
    CHECK_FALSE(loading.stalled);
    CHECK(loading.snapshot.sceneChange.stage == core::SceneStage::Loading);
    CHECK(loading.snapshot.sceneChange.change == 1);
    CHECK(loading.snapshot.sceneChange.from == START);
    CHECK(loading.snapshot.sceneChange.to == START);
    rig.handover.SendInput({ .kind = core::ActionKind::SceneResident, .scene = START.sprites });
    const BurstResult entering = rig.Burst(1);
    CHECK_FALSE(entering.stalled);
    CHECK(entering.snapshot.sceneChange.stage == core::SceneStage::Entering);
    rig.handover.SendInput({ .kind = core::ActionKind::SceneEntered, .change = 1 });
    const BurstResult running = rig.Burst(1);
    CHECK(running.stalled);
    CHECK(running.snapshot.sceneChange.stage == core::SceneStage::Running);
    CHECK(rig.logic.Counters().wall == 0);
    CHECK(rig.simulation.ticks.empty());
}

TEST_CASE("a change asked for while paused completes and stays paused") {
    constexpr u64 TICKS = 3;
    Rig rig;
    rig.handover.SendInput({ .kind = core::ActionKind::CycleScene });
    const BurstResult asked = rig.Burst(1);
    rig.handover.SendInput({ .kind = core::ActionKind::Pause }); // before the way out is reported played
    static_cast<void>(Settle(rig, asked));
    CHECK(rig.simulation.key == OTHER_KEY);
    CHECK_FALSE(rig.Burst(TICKS).snapshot.advanced);
    CHECK(rig.simulation.ticks.size() == 1);
}

TEST_CASE("a second CycleScene during a change is dropped") {
    Rig rig;
    rig.handover.SendInput({ .kind = core::ActionKind::CycleScene });
    static_cast<void>(rig.Burst(1));
    rig.handover.SendInput({ .kind = core::ActionKind::CycleScene }); // pressed again while leaving
    const BurstResult leaving = rig.Burst(1);
    static_cast<void>(Settle(rig, leaving));
    const BurstResult next = rig.Burst(1);
    CHECK(next.snapshot.sceneChange.stage == core::SceneStage::Running);
    CHECK(next.snapshot.sceneChange.change == 2);
    CHECK(rig.simulation.actions == std::vector<std::size_t>{ 1, 0 });
    CHECK_FALSE(rig.simulation.requested.has_value());
}

TEST_CASE("a stale report is ignored") {
    constexpr u64 SECOND = 2;
    constexpr u64 THIRD = 3;
    Rig rig;
    rig.handover.SendInput({ .kind = core::ActionKind::CycleScene });
    static_cast<void>(Settle(rig, rig.Burst(1)));
    rig.handover.SendInput({ .kind = core::ActionKind::CycleScene });
    REQUIRE(rig.Burst(1).snapshot.sceneChange.change == THIRD);
    rig.simulation.calls.clear();
    rig.handover.SendInput({ .kind = core::ActionKind::SceneLeft, .change = SECOND });   // the change before's
    rig.handover.SendInput({ .kind = core::ActionKind::SceneEntered, .change = THIRD }); // not this stage's
    const BurstResult stale = rig.Burst(1);
    CHECK(stale.snapshot.sceneChange.stage == core::SceneStage::Leaving);
    CHECK(stale.snapshot.sceneChange.change == THIRD);
    CHECK_FALSE(std::ranges::contains(rig.simulation.calls, Call::EnterScene));
    CHECK(rig.simulation.requested.has_value());

    rig.handover.SendInput({ .kind = core::ActionKind::SceneLeft, .change = THIRD });
    REQUIRE(rig.Burst(1).snapshot.sceneChange.stage == core::SceneStage::Entering);       // the same sprites, resident
    rig.handover.SendInput({ .kind = core::ActionKind::SceneEntered, .change = SECOND }); // the change before's
    const BurstResult staleEntered = rig.Burst(1);
    CHECK_FALSE(staleEntered.stalled);
    CHECK(staleEntered.snapshot.sceneChange.stage == core::SceneStage::Entering);
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
    const BurstResult entered = Settle(rig, loaded); // restored at the swap
    CHECK(rig.logic.Counters().wall == saved.wall);
    CHECK(rig.logic.Counters().simulation == saved.simulation);
    CHECK(rig.logic.Counters().state == saved.state);
    CHECK(rig.simulation.ticks.size() == BEFORE);
    CHECK(rig.logic.Epoch() == 1);
    CHECK(entered.snapshot.epoch == 1);
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

TEST_CASE("a Load during a change is dropped") {
    constexpr u64 TICKS = 3;
    const SaveSlot slot{ "load_mid_change" };
    Rig rig(slot.File());
    rig.handover.SendInput({ .kind = core::ActionKind::Save });
    static_cast<void>(rig.Burst(1));
    rig.jobs.WaitForBackground();
    REQUIRE(std::filesystem::exists(slot.File()));
    static_cast<void>(rig.Burst(TICKS));
    rig.handover.SendInput({ .kind = core::ActionKind::CycleScene });
    static_cast<void>(rig.Burst(1));
    const core::TickCounters counters = rig.logic.Counters();
    rig.handover.SendInput({ .kind = core::ActionKind::Load }); // pressed while leaving
    const BurstResult leaving = rig.Burst(1);
    static_cast<void>(Settle(rig, leaving));
    CHECK(rig.simulation.key == OTHER_KEY);
    CHECK(rig.logic.Epoch() == 0);
    CHECK(rig.logic.Counters().wall == counters.wall);
    const BurstResult next = rig.Burst(1); // nor is it taken once the scene runs
    CHECK(next.snapshot.advanced);
    CHECK(rig.logic.Epoch() == 0);
    CHECK(rig.simulation.ticks.size() == TICKS + 2);
}

TEST_CASE("a load is checked on its tick and restored at the swap") {
    constexpr u64 BEFORE = 3;
    constexpr u64 AFTER = 2;
    constexpr u64 TICKS = 3;
    constexpr core::SceneFacts START{};
    const SaveSlot slot{ "load_at_swap" };
    Rig rig(slot.File());
    static_cast<void>(rig.Burst(BEFORE));
    rig.handover.SendInput({ .kind = core::ActionKind::Save });
    static_cast<void>(rig.Burst(1));
    rig.jobs.WaitForBackground();
    const core::TickCounters saved = rig.logic.Counters();
    static_cast<void>(rig.Burst(AFTER));

    rig.simulation.rejectLoads = true; // broken: refused on its tick, a frozen tick, and no change starts
    rig.handover.SendInput({ .kind = core::ActionKind::Load });
    const BurstResult refused = rig.Burst(TICKS);
    CHECK(refused.stalled);
    CHECK(refused.snapshot.sceneChange.stage == core::SceneStage::Running);
    CHECK(refused.snapshot.sceneChange.change == 1);
    CHECK(rig.logic.Counters().wall == saved.wall + AFTER + 1);
    CHECK(rig.logic.Counters().simulation == BEFORE + AFTER);
    CHECK(rig.logic.Epoch() == 0);

    rig.simulation.rejectLoads = false; // sound: held, everything as it was, until the old scene is left
    const core::TickCounters before = rig.logic.Counters();
    rig.simulation.calls.clear();
    rig.handover.SendInput({ .kind = core::ActionKind::Load });
    const BurstResult asked = rig.Burst(TICKS);
    CHECK(asked.stalled);
    CHECK(asked.snapshot.sceneChange.stage == core::SceneStage::Leaving);
    CHECK(asked.snapshot.sceneChange.change == 2);
    CHECK(asked.snapshot.sceneChange.from == START);
    CHECK(asked.snapshot.sceneChange.to == START);
    CHECK(asked.snapshot.epoch == 0);
    const BurstResult leaving = rig.Burst(TICKS); // no report yet
    CHECK(leaving.snapshot.sceneChange.stage == core::SceneStage::Leaving);
    CHECK(leaving.snapshot.wallTick == before.wall);
    CHECK(rig.logic.Counters().wall == before.wall);
    CHECK(rig.logic.Counters().simulation == before.simulation);
    CHECK(rig.logic.Counters().state == before.state);
    CHECK(rig.simulation.ticks.size() == BEFORE + AFTER);
    CHECK(rig.logic.Epoch() == 0);
    CHECK_FALSE(std::ranges::contains(rig.simulation.calls, Call::EnterScene));

    rig.Answer(leaving.snapshot); // the swap: the state, the counters and a new epoch
    const BurstResult swapped = rig.Burst(TICKS);
    CHECK_FALSE(swapped.stalled);
    CHECK(swapped.snapshot.sceneChange.stage == core::SceneStage::Entering); // into the scene shown: nothing to load
    CHECK(swapped.snapshot.wallTick == saved.wall);
    CHECK(swapped.snapshot.epoch == 1);
    CHECK(rig.logic.Counters().wall == saved.wall);
    CHECK(rig.logic.Counters().simulation == saved.simulation);
    CHECK(rig.logic.Counters().state == saved.state);
    CHECK(rig.simulation.ticks.size() == BEFORE);
    CHECK(rig.logic.Epoch() == 1);
    rig.Answer(swapped.snapshot);
    const BurstResult running = rig.Burst(TICKS);
    CHECK(running.stalled);
    CHECK(running.snapshot.sceneChange.stage == core::SceneStage::Running);
    CHECK(rig.Burst(1).snapshot.simulationTick == saved.simulation + 1);
}

TEST_CASE("a load into another scene waits for it to be resident") {
    constexpr core::SceneFacts START{};
    constexpr core::SceneFacts OTHER{ .key = OTHER_KEY, .sprites = OTHER_SCENE };
    const SaveSlot slot{ "load_other_scene" };
    Rig rig(slot.File());
    rig.handover.SendInput({ .kind = core::ActionKind::Save }); // in the start scene
    static_cast<void>(rig.Burst(1));
    rig.jobs.WaitForBackground();
    rig.handover.SendInput({ .kind = core::ActionKind::CycleScene });
    static_cast<void>(Settle(rig, rig.Burst(1)));
    REQUIRE(rig.simulation.key == OTHER_KEY);

    rig.handover.SendInput({ .kind = core::ActionKind::Load });
    const BurstResult asked = rig.Burst(1);
    CHECK(asked.snapshot.sceneChange.stage == core::SceneStage::Leaving);
    CHECK(asked.snapshot.sceneChange.from == OTHER);
    CHECK(asked.snapshot.sceneChange.to == START);
    CHECK(asked.snapshot.scene == OTHER_SCENE);
    rig.Answer(asked.snapshot);
    const BurstResult swapped = rig.Burst(1);
    CHECK(swapped.snapshot.sceneChange.stage == core::SceneStage::Loading);
    CHECK(swapped.snapshot.scene == START.sprites);
    CHECK(rig.logic.Epoch() == 1);
    const BurstResult loading = rig.Burst(1); // not resident yet
    CHECK_FALSE(loading.stalled);
    CHECK(loading.snapshot.sceneChange.stage == core::SceneStage::Loading);
    rig.Answer(loading.snapshot); // resident
    const BurstResult entering = rig.Burst(1);
    CHECK(entering.snapshot.sceneChange.stage == core::SceneStage::Entering);
    rig.Answer(entering.snapshot);
    CHECK(rig.Burst(1).snapshot.sceneChange.stage == core::SceneStage::Running);
    CHECK(rig.simulation.key == START.key);
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

// Starts playing the replay in `rig`'s replay slot, answering the scene change into its start until it runs.
void StartPlaying(Rig& rig) {
    rig.handover.SendInput({ .kind = core::ActionKind::PlayReplay });
    const BurstResult started = rig.Burst(1);
    REQUIRE(rig.replay.Playing());
    static_cast<void>(Settle(rig, started));
}

// Plays the replay in `rig`'s replay slot, a tick per burst so it stops exactly at the replay's end, answering each
// snapshot as App does.
void Play(Rig& rig) {
    constexpr u32 MAX_BURSTS = 1'000;
    StartPlaying(rig);
    for (u32 bursts = 0; bursts < MAX_BURSTS && rig.replay.Playing() && !rig.replay.AtEnd(); ++bursts) {
        rig.Answer(rig.Burst(1).snapshot);
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

// Records a replay across a scene change into the replay file beside `slot`, the main thread answering each stage `late`
// bursts after it shows, and ending right after a hash tick; returns the replay file's bytes.
std::vector<std::byte> RecordAcrossChange(const SaveSlot& slot, u32 late) {
    constexpr u64 TICKS = 70;
    constexpr u64 BURST = 3;
    const std::filesystem::path replayFile = slot.File().parent_path() / "replay.pnr";
    Rig rig(slot.File(), replayFile);
    rig.handover.SendInput({ .kind = core::ActionKind::ToggleRecording });
    static_cast<void>(rig.Burst(1));
    static_cast<void>(rig.Burst(BURST));
    rig.handover.SendInput({ .kind = core::ActionKind::CycleScene });
    static_cast<void>(Settle(rig, rig.Burst(BURST), late));
    static_cast<void>(rig.Burst(TICKS));
    while (rig.logic.Counters().wall % core::REPLAY_HASH_TICKS != 0) {
        static_cast<void>(rig.Burst(1));
    }
    rig.handover.SendInput({ .kind = core::ActionKind::ToggleRecording });
    static_cast<void>(rig.Burst(1));
    rig.jobs.WaitForBackground();
    std::expected<std::vector<std::byte>, core::Error> replay = core::ReadSaveFile(replayFile);
    REQUIRE(replay.has_value());
    return std::move(replay).value_or(std::vector<std::byte>{});
}

TEST_CASE("nothing is recorded while a scene changes") {
    constexpr u32 LATE = 10;
    const SaveSlot instant{ "change_answered_at_once" };
    const SaveSlot slow{ "change_answered_late" };
    const std::vector<std::byte> answeredAtOnce = RecordAcrossChange(instant, 0);
    const std::vector<std::byte> answeredLate = RecordAcrossChange(slow, LATE);
    CHECK(answeredAtOnce == answeredLate);
    for (const SaveSlot* slot : { &instant, &slow }) {
        Rig rig(slot->File(), slot->File().parent_path() / "replay.pnr");
        Play(rig);
        CHECK(rig.replay.AtEnd());
        CHECK(rig.replay.HashCount() > 0);
        CHECK(rig.replay.Matched() == rig.replay.HashCount());
    }
}

TEST_CASE("a Pause sent while recording during a scene change is recorded, so the replay plays back alike") {
    constexpr u64 TICKS = 70;
    const SaveSlot slot{ "pause_during_change" };
    const std::filesystem::path replayFile = slot.File().parent_path() / "replay.pnr";
    {
        Rig rig(slot.File(), replayFile);
        rig.handover.SendInput({ .kind = core::ActionKind::ToggleRecording });
        static_cast<void>(rig.Burst(1));
        rig.handover.SendInput({ .kind = core::ActionKind::CycleScene });
        const BurstResult asked = rig.Burst(1);
        rig.handover.SendInput({ .kind = core::ActionKind::Pause }); // the window lost its focus while leaving
        static_cast<void>(Settle(rig, asked));
        static_cast<void>(rig.Burst(TICKS));
        rig.handover.SendInput({ .kind = core::ActionKind::Resume });
        static_cast<void>(rig.Burst(TICKS));
        while (rig.logic.Counters().wall % core::REPLAY_HASH_TICKS != 0) {
            static_cast<void>(rig.Burst(1));
        }
        rig.handover.SendInput({ .kind = core::ActionKind::ToggleRecording });
        static_cast<void>(rig.Burst(1));
        rig.jobs.WaitForBackground();
    }
    Rig rig(slot.File(), replayFile);
    Play(rig);
    CHECK(rig.replay.AtEnd());
    CHECK(rig.replay.HashCount() > 0);
    CHECK(rig.replay.Matched() == rig.replay.HashCount());
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

constexpr core::Rect AREA{ .min = { .x = -100.0F, .y = -50.0F }, .max = { .x = 1'380.0F, .y = 770.0F } };
constexpr core::Rect OTHER_AREA{ .min = { .x = 3'000.0F, .y = 2'000.0F }, .max = { .x = 4'000.0F, .y = 2'600.0F } };

// Whether `view` is `area`, bit for bit: the logic passes the main thread's view on untouched.
bool IsArea(const std::optional<core::Rect>& view, const core::Rect& area) {
    using Bits = std::array<u32, sizeof(core::Rect) / sizeof(u32)>;
    return view.has_value() && std::bit_cast<Bits>(*view) == std::bit_cast<Bits>(area);
}

TEST_CASE("the logic publishes with the current change's view") {
    Rig rig;
    static_cast<void>(rig.Burst(1));
    CHECK_FALSE(rig.simulation.views.back().has_value()); // none sent yet
    rig.handover.SendView({ .area = AREA, .change = 1 });
    static_cast<void>(rig.Burst(1));
    CHECK(IsArea(rig.simulation.views.back(), AREA));
    rig.handover.SendView({ .area = OTHER_AREA, .change = 1 });
    static_cast<void>(rig.Burst(1));
    CHECK(IsArea(rig.simulation.views.back(), OTHER_AREA));
}

TEST_CASE("a view taken in an earlier change is ignored after the swap") {
    constexpr u64 TICKS = 3;
    Rig rig;
    rig.handover.SendView({ .area = AREA, .change = 1 });
    rig.handover.SendInput({ .kind = core::ActionKind::CycleScene });
    const BurstResult leaving = rig.Burst(1);
    REQUIRE(leaving.snapshot.sceneChange.stage == core::SceneStage::Leaving);
    rig.Answer(leaving.snapshot);
    const BurstResult swapped = rig.Burst(TICKS);
    REQUIRE(swapped.snapshot.sceneChange.stage != core::SceneStage::Leaving);
    CHECK_FALSE(rig.simulation.views.back().has_value());
    const BurstResult running = Settle(rig, swapped);
    REQUIRE(running.snapshot.sceneChange.change == 2);
    CHECK_FALSE(rig.simulation.views.back().has_value());
    static_cast<void>(rig.Burst(TICKS));
    CHECK_FALSE(rig.simulation.views.back().has_value());
    rig.handover.SendView({ .area = OTHER_AREA, .change = 2 });
    static_cast<void>(rig.Burst(TICKS));
    CHECK(IsArea(rig.simulation.views.back(), OTHER_AREA));
}

TEST_CASE("while the old scene is left, the view taken of it still culls it") {
    Rig rig;
    rig.handover.SendView({ .area = AREA, .change = 1 });
    rig.handover.SendInput({ .kind = core::ActionKind::CycleScene });
    const BurstResult leaving = rig.Burst(1);
    REQUIRE(leaving.snapshot.sceneChange.stage == core::SceneStage::Leaving);
    REQUIRE(leaving.snapshot.sceneChange.change == 2);
    CHECK(IsArea(rig.simulation.views.back(), AREA));
    // App names a view of the old scene, drawn while it is left, by the change that entered it.
    const u64 shown = core::ShownChange(leaving.snapshot.sceneChange);
    CHECK(shown == 1);
    rig.handover.SendView({ .area = OTHER_AREA, .change = shown });
    static_cast<void>(rig.Burst(1));
    CHECK(IsArea(rig.simulation.views.back(), OTHER_AREA));
}

TEST_CASE("the logic keeps the last view while none is sent") {
    constexpr u32 BURSTS = 10;
    Rig rig;
    rig.handover.SendView({ .area = AREA, .change = 1 });
    for (u32 burst = 0; burst < BURSTS; ++burst) {
        static_cast<void>(rig.Burst(1));
    }
    CHECK(rig.simulation.publishes == BURSTS);
    CHECK(IsArea(rig.simulation.views.back(), AREA));
}

// A replay's bytes and the logic's hash where it ended.
struct Recording {
    std::vector<std::byte> replay;
    u64 hash = 0;
};

// Records a replay into the replay file beside `slot`, sending a view that moves on before every burst when `views`, and
// ending right after a hash tick; returns the replay file's bytes and the logic's hash at its end.
Recording RecordSendingViews(const SaveSlot& slot, bool views) {
    constexpr u64 TICKS = 3;
    constexpr f32 STEP = 37.0F; // how far each view moves from the one before
    const std::filesystem::path replayFile = slot.File().parent_path() / "replay.pnr";
    Rig rig(slot.File(), replayFile);
    rig.handover.SendInput({ .kind = core::ActionKind::ToggleRecording });
    static_cast<void>(rig.Burst(1));
    f32 offset = 0.0F;
    for (u64 burst = 0; burst == 0 || rig.logic.Counters().wall % core::REPLAY_HASH_TICKS != 0; ++burst) {
        if (views) {
            offset += STEP;
            const core::Vec2 moved{ .x = offset, .y = -offset };
            rig.handover.SendView({ .area = { .min = AREA.min + moved, .max = AREA.max + moved }, .change = 1 });
        }
        // Bursts of a tick near the end, so the recording stops right after a hash tick.
        const u64 left = core::REPLAY_HASH_TICKS - (rig.logic.Counters().wall % core::REPLAY_HASH_TICKS);
        static_cast<void>(rig.Burst(left > TICKS ? TICKS : 1));
    }
    const u64 hash = rig.logic.Hash();
    rig.handover.SendInput({ .kind = core::ActionKind::ToggleRecording });
    static_cast<void>(rig.Burst(1));
    rig.jobs.WaitForBackground();
    std::expected<std::vector<std::byte>, core::Error> replay = core::ReadSaveFile(replayFile);
    REQUIRE(replay.has_value());
    return { .replay = std::move(replay).value_or(std::vector<std::byte>{}), .hash = hash };
}

TEST_CASE("the view is never recorded and never hashed") {
    const SaveSlot without{ "views_never_sent" };
    const SaveSlot with{ "views_sent_each_burst" };
    const Recording unseen = RecordSendingViews(without, false);
    const Recording seen = RecordSendingViews(with, true);
    REQUIRE_FALSE(unseen.replay.empty());
    CHECK(seen.replay == unseen.replay);
    CHECK(seen.hash == unseen.hash);
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

TEST_CASE("a load while recording finishes the recording on its tick") {
    constexpr u64 TICKS = 3;
    const SaveSlot slot{ "replay_load_into_other_scene" };
    const std::filesystem::path replayFile = slot.File().parent_path() / "replay.pnr";
    Rig rig(slot.File(), replayFile);
    rig.handover.SendInput({ .kind = core::ActionKind::Save }); // in the start scene
    static_cast<void>(rig.Burst(1));
    rig.handover.SendInput({ .kind = core::ActionKind::CycleScene });
    static_cast<void>(Settle(rig, rig.Burst(1)));
    rig.handover.SendInput({ .kind = core::ActionKind::ToggleRecording });
    static_cast<void>(rig.Burst(1));
    static_cast<void>(rig.Burst(TICKS));
    rig.handover.SendInput({ .kind = core::ActionKind::Load }); // into the start scene
    const BurstResult asked = rig.Burst(1);
    REQUIRE(asked.snapshot.sceneChange.stage == core::SceneStage::Leaving);
    rig.jobs.WaitForBackground();
    CHECK_FALSE(rig.replay.Recording());
    std::expected<std::vector<std::byte>, core::Error> onItsTick = core::ReadSaveFile(replayFile);
    REQUIRE(onItsTick.has_value());

    static_cast<void>(Settle(rig, asked));
    rig.jobs.WaitForBackground();
    CHECK_FALSE(rig.replay.Recording());
    std::expected<std::vector<std::byte>, core::Error> afterTheSwap = core::ReadSaveFile(replayFile);
    REQUIRE(afterTheSwap.has_value());
    const bool unchanged = *afterTheSwap == *onItsTick;
    CHECK(unchanged);
}

// New game data, unlike the default.
std::shared_ptr<const core::GameData> NewData() {
    core::GameData data;
    data.cultivator.walkSpeed = 1.0F;
    return std::make_shared<const core::GameData>(std::move(data));
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

TEST_CASE("new game data mid-change is taken and the change goes on") {
    Rig rig;
    rig.handover.SendInput({ .kind = core::ActionKind::CycleScene });
    const BurstResult asked = rig.Burst(1);
    const std::shared_ptr<const core::GameData> data = NewData();
    rig.handover.SendInput({ .kind = core::ActionKind::ReloadData, .data = data });
    const BurstResult reloaded = rig.Burst(1);
    CHECK(rig.simulation.dataHash == data->Hash());
    CHECK(reloaded.snapshot.sceneChange.stage == core::SceneStage::Leaving);
    static_cast<void>(Settle(rig, asked));
    CHECK(rig.simulation.key == OTHER_KEY);
    CHECK(rig.logic.Counters().wall == 1);
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

TEST_CASE("a replay stopped while its start is held leaves the window's pausing at the swap") {
    Recorded recorded{ "replay_stopped_mid_change" }; // its start is unpaused, at wall tick 1
    Rig& rig = recorded.rig;
    rig.handover.SendInput({ .kind = core::ActionKind::PlayReplay });
    REQUIRE(rig.Burst(1).snapshot.sceneChange.stage == core::SceneStage::Leaving);
    rig.handover.SendInput({ .kind = core::ActionKind::Pause }); // the window's, while leaving
    rig.handover.SendInput({ .kind = core::ActionKind::ReloadData, .data = NewData() });
    const BurstResult leaving = rig.Burst(1);
    REQUIRE(leaving.snapshot.sceneChange.stage == core::SceneStage::Leaving);
    CHECK_FALSE(rig.replay.Playing());
    static_cast<void>(Settle(rig, leaving));
    CHECK(rig.logic.Counters().wall == 1);
    CHECK(rig.logic.Counters().simulation == 0);
    CHECK(rig.logic.Counters().state == 0);
    CHECK(rig.logic.Epoch() == 1);
    CHECK_FALSE(rig.Burst(1).snapshot.advanced); // paused, as the window asked
}

TEST_CASE("a calendar step asked for on a sound load's tick goes with the old state") {
    constexpr u64 TICKS = 3;
    const SaveSlot slot{ "calendar_with_load" };
    Rig rig(slot.File());
    rig.handover.SendInput({ .kind = core::ActionKind::Save });
    static_cast<void>(rig.Burst(1));
    rig.jobs.WaitForBackground();
    rig.handover.SendInput({ .kind = core::ActionKind::AdvanceCalendar });
    rig.handover.SendInput({ .kind = core::ActionKind::Load });
    const BurstResult asked = rig.Burst(1);
    REQUIRE(asked.snapshot.sceneChange.stage == core::SceneStage::Leaving);
    CHECK_FALSE(asked.snapshot.transition);
    static_cast<void>(Settle(rig, asked));
    static_cast<void>(rig.Burst(TICKS));
    CHECK(rig.simulation.steps.empty());
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
    const BurstResult started = rig.Burst(1);
    CHECK_FALSE(rig.replay.Recording());
    CHECK(rig.replay.Playing());
    static_cast<void>(Settle(rig, started));
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
    static_cast<void>(Settle(rig, rig.Burst(1))); // a new timeline: its epoch is not the events' default
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
