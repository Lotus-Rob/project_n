#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/logic.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 SCHEDULED_NS = 1'000;
constexpr u64 PUBLISHED_NS = 1'500;

u64 PublishedClock() {
    return PUBLISHED_NS;
}

// Records the ticks it ran and its publishes, and publishes one instance per tick so far.
class Recorder final : public core::Simulation {
public:
    void Tick(const core::TickCounters& counters) override {
        ticks.push_back(counters.simulation);
        states.push_back(counters.state);
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
        return ticks.size();
    }
    void Publish(core::Snapshot& snapshot, core::Jobs& /*jobs*/) override {
        ++publishes;
        ticksAtPublish = ticks.size();
        capacityAtPublish = snapshot.instances.capacity();
        sizeAtPublish = snapshot.instances.size();
        snapshot.instances.resize(ticks.size());
    }

    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<u64> ticks{};
    std::vector<u64> states{}; // the state counter at each tick
    std::vector<u64> steps{};  // the state counter at each calendar step
    // NOLINTEND(readability-redundant-member-init)
    bool rejectLoads = false;
    std::size_t publishes = 0;
    std::size_t ticksAtPublish = 0;
    std::size_t capacityAtPublish = 0;
    std::size_t sizeAtPublish = 0;
};

// The snapshot RunBurst published for a burst of `ticks`.
core::Snapshot Burst(core::Logic& logic, core::GameHandover& handover, u64 ticks) {
    Recorder simulation;
    core::Jobs jobs{ 1 };
    logic.RunBurst({ .ticks = ticks, .scheduledNs = SCHEDULED_NS }, handover, simulation, jobs, PublishedClock);
    core::Snapshot* published = handover.TakeState();
    REQUIRE(published != nullptr);
    const core::Snapshot snapshot = *published;
    handover.ReturnState(published);
    return snapshot;
}

TEST_CASE("a burst runs and publishes its ticks") {
    constexpr u64 TICKS = 3;
    core::Logic logic;
    core::GameHandover handover;
    const core::Snapshot snapshot = Burst(logic, handover, TICKS);
    CHECK(snapshot.wallTick == TICKS);
    CHECK(snapshot.simulationTick == TICKS);
    CHECK(snapshot.advanced);
}

TEST_CASE("the snapshot carries the burst's scheduled time and when it was published") {
    core::Logic logic;
    core::GameHandover handover;
    const core::Snapshot snapshot = Burst(logic, handover, 1);
    CHECK(snapshot.scheduledNs == SCHEDULED_NS);
    CHECK(snapshot.publishedNs == PUBLISHED_NS);
}

TEST_CASE("paused ticks are frozen: wall ticks only, and the snapshot didn't advance") {
    constexpr u64 TICKS = 2;
    core::Logic logic;
    core::GameHandover handover;
    handover.SendInput({ .kind = core::ActionKind::Pause });
    const core::Snapshot snapshot = Burst(logic, handover, TICKS);
    CHECK(snapshot.wallTick == TICKS);
    CHECK(snapshot.simulationTick == 0);
    CHECK_FALSE(snapshot.advanced);
}

TEST_CASE("the last of several actions in one tick wins") {
    core::Logic logic;
    core::GameHandover handover;
    handover.SendInput({ .kind = core::ActionKind::Pause });
    handover.SendInput({ .kind = core::ActionKind::Resume });
    CHECK(Burst(logic, handover, 1).advanced);
}

TEST_CASE("pausing lasts across bursts until resumed") {
    core::Logic logic;
    core::GameHandover handover;
    handover.SendInput({ .kind = core::ActionKind::Pause });
    static_cast<void>(Burst(logic, handover, 1));
    CHECK_FALSE(Burst(logic, handover, 1).advanced);
    handover.SendInput({ .kind = core::ActionKind::Resume });
    CHECK(Burst(logic, handover, 1).advanced);
}

TEST_CASE("the simulation ticks once per advancing tick, in order") {
    constexpr u64 TICKS = 3;
    core::Logic logic;
    core::GameHandover handover;
    Recorder simulation;
    core::Jobs jobs{ 1 };
    logic.RunBurst({ .ticks = TICKS, .scheduledNs = SCHEDULED_NS }, handover, simulation, jobs, PublishedClock);
    CHECK(simulation.ticks == std::vector<u64>{ 1, 2, 3 });
}

TEST_CASE("paused ticks don't tick the simulation") {
    constexpr u64 TICKS = 3;
    core::Logic logic;
    core::GameHandover handover;
    Recorder simulation;
    core::Jobs jobs{ 1 };
    handover.SendInput({ .kind = core::ActionKind::Pause });
    logic.RunBurst({ .ticks = TICKS, .scheduledNs = SCHEDULED_NS }, handover, simulation, jobs, PublishedClock);
    CHECK(simulation.ticks.empty());
}

TEST_CASE("a burst publishes once, after its ticks") {
    constexpr u64 TICKS = 3;
    core::Logic logic;
    core::GameHandover handover;
    Recorder simulation;
    core::Jobs jobs{ 1 };
    logic.RunBurst({ .ticks = TICKS, .scheduledNs = SCHEDULED_NS }, handover, simulation, jobs, PublishedClock);
    CHECK(simulation.publishes == 1);
    CHECK(simulation.ticksAtPublish == TICKS);
}

TEST_CASE("each publish starts from empty instances that keep their capacity") {
    core::Logic logic;
    core::GameHandover handover;
    Recorder simulation;
    core::Jobs jobs{ 1 };
    // By the time there have been more bursts than buffers, every write reuses a buffer filled before.
    for (u64 burst = 0; burst < 2 * core::GameHandover::BUFFERS; ++burst) {
        logic.RunBurst({ .ticks = 1, .scheduledNs = SCHEDULED_NS }, handover, simulation, jobs, PublishedClock);
        CHECK(simulation.sizeAtPublish == 0);
        if (burst >= core::GameHandover::BUFFERS) {
            CHECK(simulation.capacityAtPublish >= burst + 1 - core::GameHandover::BUFFERS);
        }
        handover.ReturnState(handover.TakeState());
    }
}

} // namespace

namespace {

struct BurstResult {
    bool stepped = false;
    core::Snapshot snapshot{};
};

// Runs a burst of `ticks` on `simulation` and returns what RunBurst said and published.
BurstResult Run(core::Logic& logic, core::GameHandover& handover, Recorder& simulation, u64 ticks) {
    core::Jobs jobs{ 1 };
    const bool stepped =
        logic.RunBurst({ .ticks = ticks, .scheduledNs = SCHEDULED_NS }, handover, simulation, jobs, PublishedClock);
    core::Snapshot* published = handover.TakeState();
    REQUIRE(published != nullptr);
    BurstResult result{ .stepped = stepped, .snapshot = *published };
    handover.ReturnState(published);
    return result;
}

TEST_CASE("a calendar step asked for freezes the burst and publishes the transition first") {
    constexpr u64 TICKS = 2;
    core::Logic logic;
    core::GameHandover handover;
    Recorder simulation;
    handover.SendInput({ .kind = core::ActionKind::AdvanceCalendar });
    const BurstResult result = Run(logic, handover, simulation, TICKS);
    CHECK_FALSE(result.stepped);
    CHECK(result.snapshot.transition);
    CHECK_FALSE(result.snapshot.advanced);
    CHECK(result.snapshot.simulationTick == 0);
    CHECK(simulation.steps.empty());
}

TEST_CASE("the tick after the transition runs the step, changing the state, and ends its burst") {
    constexpr u64 TICKS = 3;
    core::Logic logic;
    core::GameHandover handover;
    Recorder simulation;
    handover.SendInput({ .kind = core::ActionKind::AdvanceCalendar });
    static_cast<void>(Run(logic, handover, simulation, 1));

    const BurstResult result = Run(logic, handover, simulation, TICKS);
    CHECK(result.stepped);
    CHECK(simulation.steps == std::vector<u64>{ 1 });
    CHECK(simulation.ticks.empty());
    CHECK(result.snapshot.wallTick == 2);
    CHECK_FALSE(result.snapshot.transition);
    CHECK_FALSE(result.snapshot.advanced);

    static_cast<void>(Run(logic, handover, simulation, 1));
    CHECK(simulation.ticks == std::vector<u64>{ 1 });
    CHECK(simulation.states == std::vector<u64>{ 2 }); // the step counted as a change of state
}

TEST_CASE("input sent during the transition waits until after the step") {
    core::Logic logic;
    core::GameHandover handover;
    Recorder simulation;
    handover.SendInput({ .kind = core::ActionKind::AdvanceCalendar });
    static_cast<void>(Run(logic, handover, simulation, 1));
    handover.SendInput({ .kind = core::ActionKind::Pause });
    CHECK(Run(logic, handover, simulation, 1).stepped);
    CHECK_FALSE(Run(logic, handover, simulation, 1).snapshot.advanced);
    CHECK(simulation.ticks.empty());
}

} // namespace

namespace {

// A save file path in a folder of its own, removed when the test ends.
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
    core::Logic logic{ slot.File() };
    core::GameHandover handover;
    Recorder simulation;
    static_cast<void>(Run(logic, handover, simulation, BEFORE));
    handover.SendInput({ .kind = core::ActionKind::Save });
    CHECK(Run(logic, handover, simulation, 1).stepped);
    REQUIRE(std::filesystem::exists(slot.File()));
    const core::TickCounters saved = logic.Counters();
    CHECK(saved.wall == BEFORE + 1);

    static_cast<void>(Run(logic, handover, simulation, AFTER));
    handover.SendInput({ .kind = core::ActionKind::Load });
    const BurstResult loaded = Run(logic, handover, simulation, 1);
    CHECK(loaded.stepped);
    CHECK(logic.Counters().wall == saved.wall);
    CHECK(logic.Counters().simulation == saved.simulation);
    CHECK(logic.Counters().state == saved.state);
    CHECK(simulation.ticks.size() == BEFORE);
    CHECK(logic.Epoch() == 1);
    CHECK(loaded.snapshot.epoch == 1);
}

TEST_CASE("a load with no save changes nothing but the tick count") {
    const SaveSlot slot{ "no_save" };
    core::Logic logic{ slot.File() };
    core::GameHandover handover;
    Recorder simulation;
    handover.SendInput({ .kind = core::ActionKind::Load });
    CHECK(Run(logic, handover, simulation, 1).stepped);
    CHECK(logic.Epoch() == 0);
    CHECK(logic.Counters().wall == 1);
    CHECK(logic.Counters().simulation == 0);
}

TEST_CASE("a load the simulation rejects changes nothing but the tick count") {
    constexpr u64 TICKS = 2;
    const SaveSlot slot{ "rejected" };
    core::Logic logic{ slot.File() };
    core::GameHandover handover;
    Recorder simulation;
    handover.SendInput({ .kind = core::ActionKind::Save });
    static_cast<void>(Run(logic, handover, simulation, 1));
    static_cast<void>(Run(logic, handover, simulation, TICKS));
    simulation.rejectLoads = true;
    handover.SendInput({ .kind = core::ActionKind::Load });
    static_cast<void>(Run(logic, handover, simulation, 1));
    CHECK(logic.Epoch() == 0);
    CHECK(logic.Counters().wall == TICKS + 2);
    CHECK(simulation.ticks.size() == TICKS);
}

TEST_CASE("with no save file, Save and Load are refused") {
    core::Logic logic;
    core::GameHandover handover;
    Recorder simulation;
    handover.SendInput({ .kind = core::ActionKind::Save });
    CHECK(Run(logic, handover, simulation, 1).stepped);
    handover.SendInput({ .kind = core::ActionKind::Load });
    CHECK(Run(logic, handover, simulation, 1).stepped);
    CHECK(logic.Epoch() == 0);
}

} // namespace
