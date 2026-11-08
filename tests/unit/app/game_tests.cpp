#include "app/game.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/math.hpp"
#include "core/tick_clock.hpp"
#include "support/manifest.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 START_NS = 1'000'000'000;

// The time a test's clock reads, set by the test.
u64& FakeTime() {
    static u64 time = START_NS;
    return time;
}

u64 FakeNow() {
    return FakeTime();
}

// The snapshot the handover holds now.
core::Snapshot Taken(core::GameHandover& handover) {
    core::Snapshot* published = handover.TakeState();
    REQUIRE(published != nullptr);
    const core::Snapshot snapshot = *published;
    handover.ReturnState(published);
    return snapshot;
}

// Reports the arena's scene resident, as App does once the renderer holds it: the step taking it stalls, and the
// clock restarts at now().
void MakeResident(core::GameHandover& handover, app::Game& game) {
    // A manifest without the scene fails the ticks the tests expect next.
    const core::SceneId arena = test::TestManifest()->SceneNamed("arena").value_or(core::SceneId{});
    handover.SendInput({ .kind = core::ActionKind::SceneResident, .scene = arena });
    game.Step(FakeNow());
    REQUIRE(game.Logic().Counters().wall == 0);
}

TEST_CASE("a step before the scene is resident runs no tick but publishes the scene it waits for") {
    core::math::SetFloatingPointEnvironment();
    FakeTime() = START_NS;
    core::GameHandover handover;
    core::Jobs jobs{ 1 };
    app::Game game(handover, jobs, { .manifest = test::TestManifest(), .startNs = START_NS, .now = FakeNow });
    game.Step(START_NS);
    const core::Snapshot snapshot = Taken(handover);
    CHECK(snapshot.wallTick == 0);
    const std::optional<core::SceneId> arena = test::TestManifest()->SceneNamed("arena");
    CHECK(snapshot.scene == arena);
}

TEST_CASE("a step at a tick's time runs it and publishes its snapshot") {
    core::math::SetFloatingPointEnvironment();
    FakeTime() = START_NS;
    core::GameHandover handover;
    core::Jobs jobs{ 1 };
    app::Game game(handover, jobs, { .manifest = test::TestManifest(), .startNs = START_NS, .now = FakeNow });
    MakeResident(handover, game);
    game.Step(START_NS);
    const core::Snapshot snapshot = Taken(handover);
    CHECK(snapshot.wallTick == 1);
    CHECK(snapshot.simulationTick == 1);
    CHECK(snapshot.scheduledNs == START_NS);
    CHECK(game.Clock().NextNs() == core::TickTime(START_NS, 1));
}

TEST_CASE("after a calendar step, the clock restarts when the step ended instead of catching up") {
    constexpr u64 STALL_NS = 2 * core::NS_PER_SECOND;
    core::math::SetFloatingPointEnvironment();
    FakeTime() = START_NS;
    core::GameHandover handover;
    core::Jobs jobs{ 1 };
    app::Game game(handover, jobs, { .manifest = test::TestManifest(), .startNs = START_NS, .now = FakeNow });
    MakeResident(handover, game);
    handover.SendInput({ .kind = core::ActionKind::AdvanceCalendar });
    game.Step(START_NS);
    CHECK(Taken(handover).transition);

    const u64 stepNs = core::TickTime(START_NS, 1);
    FakeTime() = stepNs + STALL_NS;
    game.Step(stepNs);
    CHECK_FALSE(Taken(handover).transition);
    CHECK(game.Clock().NextNs() == stepNs + STALL_NS);
    CHECK(game.Logic().Counters().state == 1);
}

} // namespace
