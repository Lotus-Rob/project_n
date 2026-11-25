# Review: tests-game (part 3)

### TESTS-GAME-21: Overlapping test cases between host_tests and scenes_tests

- **Severity:** info (reviewer said low) · **Verdict:** unverifiable · **Category:** duplication · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/game/scenes_tests.cpp:20`

These pairs cover the same behaviour:
- scenes 'names round-trip' and host 'each scene is found by its name'
- scenes 'every listed scene passes CheckScenes' and host 'the shipped manifest runs every scene' plus 'a manifest without one of the scenes is refused'
- scenes 'only the world scene shows the map' and host 'a world publish holds no tiles in view…'
- scenes 'each scene's camera' and host 'the snapshot names the camera'
- host 'the shipped creatures and abilities tables are the built-in data' and 'the shipped effects table…' are data-module checks living in the game suite.

**Evidence:** Same CheckScenes/SceneNamed/worldMap assertions in both files.

**Impact:** Duplicate runtime and divergent maintenance.

**Recommendation:** Keep one of each and move the shipped-table equality checks to data_tests.

**Verification:** I did not read the cited pairs in scenes_tests and host_tests, so I cannot judge the overlap. Moving the shipped-table equality checks to data_tests is plausible, but I did not check whether game::BuiltInData is reachable from the data suite, since game is above data in the module order. The impact is low.

### TESTS-GAME-22: Unlike() cannot tell that the compared games advanced; the ticks-0 pass relies on the caller

- **Severity:** info · **Verdict:** unverifiable · **Category:** test-quality · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/support/exactness.hpp:25`

Unlike() counts hash mismatches but never checks that the hash changed over the run. If Tick became a no-op for all three hosts, the result is still 0. UnlikeOnBothWorkerCounts's second pass uses `ticks = 0, every = 1`, so the 4-worker pass exercises only the month step and the draw.

**Evidence:** `unlike += games.loaded->StateHash() == hash && games.neverSaved->StateHash() == hash ? 0U : 1U;`

**Impact:** Mostly covered by other tests (the Play helpers assert relationships form and so on), but the helper's contract is weaker than its name suggests.

**Recommendation:** Return or assert that the final hash differs from the starting hash, and document that the 4-worker pass covers the fork points only.

**Verification:** I did not open tests/support/exactness.hpp, so I cannot confirm that Unlike() never checks the hash moved. The claim is a plausible helper-contract weakness, and the reviewer already notes other tests cover it. Reading the helper and its callers would decide it.

### TESTS-GAME-23: Default-sized Jobs used where the rest of the suite fixes worker counts

- **Severity:** info · **Verdict:** confirmed · **Category:** test-isolation · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/game/scenes_tests.cpp:93`

`core::Jobs jobs;` in 'only the world scene shows the map' (line 93) uses the hardware default. Every other test pins 1, 2, 4 or 7. The result is a machine-dependent thread count in a unit test, which also affects the TSan runs.

**Evidence:** `core::Jobs jobs;`

**Impact:** A minor reproducibility difference between machines.

**Recommendation:** Use `core::Jobs jobs{ 1 }`.

**Verification:** scenes_tests.cpp:93 declares `core::Jobs jobs;` with the default worker count. A fixed count would be more reproducible. The result is deterministic anyway, so the impact is trivial.

### TESTS-GAME-24: Magic scene-list and scale assertions couple tests to incidental layout

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** maintainability · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/unit/game/scenes_tests.cpp:222`

`REQUIRE(game::Scenes().size() == 5)` and the F2-order checks (31-44) break whenever a scene is added, even though the list-wraps behaviour is general. `CHECK(std::to_underlying(entities.front()) > 1'000'000)` (line 222) hard-codes the scene ID base instead of using core::SCENE_ID_BASE.

**Evidence:** `CHECK(std::to_underlying(entities.front()) > 1'000'000);`

**Impact:** Adding a scene breaks several unrelated tests (the arrays in host_tests and the CROWDS and CAMERAS tables as well).

**Recommendation:** Iterate `Scenes()` for the wrap behaviour. Keep explicit tables only where per-scene data is needed, and use the named constant.

**Verification:** scenes_tests.cpp:222 does hard-code `> 1'000'000`. core::SCENE_ID_BASE exists in src/core/state.hpp:39 as 2^62, so the named constant is available and the literal is a weak bound. The comment says the NPC ids count up from 1. I did not check the Scenes().size()==5 claim. The finding is a minor maintainability nit.

### TESTS-GAME-25: Statistical assertions ('near > far * 2', share bands) depend on one fixed seed's draw

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** test-quality · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/unit/game/travel_tests.cpp:631`

'bigger and nearer cities draw more visitors' asserts `near > far * 2` from 200 draws, and the strong/cultivator cases assert `> 0`. They are deterministic for GAME_SEED and not wrong, but they encode the RNG's output, so any RNG-purpose change flips them without a bug. The exact weights are pinned separately, which is the stronger check.

**Evidence:** `CHECK(near > far * 2); // its weight is over four times the far city's`

**Impact:** A false failure after an unrelated change to random purposes or draw order.

**Recommendation:** Keep, but also assert the draw-by-weight function directly (ChooseDestination with chosen draws, as 'the draw falls on the first city' does) and lower the sample reliance.

**Verification:** The code matches the citation. The exact DestinationWeight values are pinned directly just above (lines 617-621), and the near and far weights are 245M and 57.5M, a ratio of about 4.3. With 200 draws that gives an expected split of roughly 162 near to 38 far. The threshold `near > far * 2` sits many standard deviations from that, and `far > 0` is near-certain. So the checks hold for any seed or RNG-purpose change and do not just encode one seed's output. The claim that any RNG change flips them is overstated. A flip would take a real bug in weighted selection or an extreme RNG defect. The test is a loose end-to-end check of the draw path on top of the pinned weights, which is a reasonable design. A direct ChooseDestination check would add little, and the finding itself rates its confidence as low.
