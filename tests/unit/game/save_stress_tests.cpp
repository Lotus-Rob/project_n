// A save of the stress arena, 50,000 world NPCs and 100,000 orbiting instances, run on exactly as the game that saved and
// one that never did: in a file of its own, as its hashes are costly in a debug build, so it runs beside the other save
// suites.
#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/jobs.hpp"
#include "game/host.hpp"
#include "support/exactness.hpp"
#include "support/host.hpp"
#include "support/manifest.hpp"

#include <doctest/doctest.h>

namespace {

using test::At;
using test::Tick;

// The simulation tick the save is loaded as made at: past any tick it played to.
constexpr u64 SAVED_TICK = 1'000;

TEST_CASE("a stress save loads and carries on as the saved one") {
    constexpr u64 PLAYED = 5;
    // Few ticks, each compared: a stress tick only turns orbits as a function of the tick, so a state or order
    // divergence shows from the first, and the arena's 600 ticks through deaths and respawns carry the long-run check.
    constexpr u64 TICKS_ON = 10;
    core::Jobs jobs{ 1 };
    game::Host saved(test::TestManifest(), game::STRESS_SCENE);
    game::Host neverSaved(test::TestManifest(), game::STRESS_SCENE);
    for (game::Host* host : { &saved, &neverSaved }) {
        for (u64 tick = 1; tick <= PLAYED; ++tick) {
            Tick(*host, At(tick), {});
        }
        host->AdvanceCalendar(At(PLAYED + 1), jobs, {}); // its scratch world's NPCs unlike its entry build's
    }
    const std::vector<std::byte> bytes = test::Saved(saved);
    game::Host loaded(test::TestManifest(), game::ARENA_SCENE);
    core::Reader reader{ bytes };
    REQUIRE(loaded.Load(reader, SAVED_TICK));
    loaded.EnterScene();
    CHECK(
        test::UnlikeOnBothWorkerCounts(
            test::Games{ .saved = &saved, .neverSaved = &neverSaved, .loaded = &loaded }, PLAYED + 2, TICKS_ON, 1
        ) == 0
    );
}

} // namespace
