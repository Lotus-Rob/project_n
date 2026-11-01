#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/logic.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 SCHEDULED_NS = 1'000;
constexpr u64 PUBLISHED_NS = 1'500;

u64 PublishedClock() {
    return PUBLISHED_NS;
}

// Records the ticks it ran, and publishes one instance per tick so far.
class Recorder final : public core::Simulation {
public:
    void Tick(u64 simulationTick) override {
        ticks.push_back(simulationTick);
    }
    void Publish(core::Snapshot& snapshot) override {
        capacityAtPublish = snapshot.instances.capacity();
        sizeAtPublish = snapshot.instances.size();
        snapshot.instances.resize(ticks.size());
    }

    std::vector<u64> ticks{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::size_t capacityAtPublish = 0;
    std::size_t sizeAtPublish = 0;
};

// The snapshot RunBurst published for a burst of `ticks`.
core::Snapshot Burst(core::Logic& logic, core::GameHandover& handover, u64 ticks) {
    Recorder simulation;
    logic.RunBurst({ .ticks = ticks, .scheduledNs = SCHEDULED_NS }, handover, simulation, PublishedClock);
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

TEST_CASE("the simulation ticks on advancing ticks only, then publishes") {
    constexpr u64 TICKS = 3;
    core::Logic logic;
    core::GameHandover handover;
    Recorder simulation;
    logic.RunBurst({ .ticks = TICKS, .scheduledNs = SCHEDULED_NS }, handover, simulation, PublishedClock);
    CHECK(simulation.ticks == std::vector<u64>{ 1, 2, 3 });
    handover.SendInput({ .kind = core::ActionKind::Pause });
    logic.RunBurst({ .ticks = TICKS, .scheduledNs = SCHEDULED_NS }, handover, simulation, PublishedClock);
    CHECK(simulation.ticks.size() == TICKS);
}

TEST_CASE("each publish starts from empty instances that keep their capacity") {
    core::Logic logic;
    core::GameHandover handover;
    Recorder simulation;
    // By the time there have been more bursts than buffers, every write reuses a buffer filled before.
    for (u64 burst = 0; burst < 2 * core::GameHandover::BUFFERS; ++burst) {
        logic.RunBurst({ .ticks = 1, .scheduledNs = SCHEDULED_NS }, handover, simulation, PublishedClock);
        CHECK(simulation.sizeAtPublish == 0);
        if (burst >= core::GameHandover::BUFFERS) {
            CHECK(simulation.capacityAtPublish >= burst + 1 - core::GameHandover::BUFFERS);
        }
        handover.ReturnState(handover.TakeState());
    }
}

} // namespace
