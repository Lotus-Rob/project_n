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

struct BurstResult {
    bool stalled = false;
    core::Snapshot snapshot{};
};

// The logic and the systems it drives, held as a game holds them; saves go to `slot` (none by default).
struct Rig {
    explicit Rig(std::filesystem::path slot = {})
      : saves(std::move(slot), jobs) {
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
    core::Logic logic{ handover, simulation, jobs, saves, PublishedClock };
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

TEST_CASE("each publish starts from empty instances that keep their capacity") {
    Rig rig;
    // By the time there have been more bursts than buffers, every write reuses a buffer filled before.
    for (u64 burst = 0; burst < 2 * core::GameHandover::BUFFERS; ++burst) {
        static_cast<void>(rig.Burst(1));
        CHECK(rig.simulation.sizeAtPublish == 0);
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
    Rig rig{ slot.File() };
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
    Rig rig{ slot.File() };
    rig.handover.SendInput({ .kind = core::ActionKind::Load });
    CHECK(rig.Burst(1).stalled);
    CHECK(rig.logic.Epoch() == 0);
    CHECK(rig.logic.Counters().wall == 1);
    CHECK(rig.logic.Counters().simulation == 0);
}

TEST_CASE("a load the simulation rejects changes nothing but the tick count") {
    constexpr u64 TICKS = 2;
    const SaveSlot slot{ "rejected" };
    Rig rig{ slot.File() };
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

} // namespace
