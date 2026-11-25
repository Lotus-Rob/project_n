# Review: tests-game

**Scope:** tests/unit/game/* and tests/support/*: TEST QUALITY review (not the code under test): tests that cannot fail, over-specified or tautological assertions, sweeps violating the "~20 representative samples" rule, hidden order dependence, missing negative cases, duplicated fixtures, slow tests, gaps versus src/game.

**Findings:** 25 standing (0 critical, 0 high, 4 medium, 16 low, 5 info), 0 raised but refuted on verification.

**Parts:** tests-game.md, tests-game-part2.md, tests-game-part3.md

## Assessment

## Assessment of tests/unit/game and tests/support

**Strengths**
- The suite is behaviour-first. Test names state the behaviour, expected values are literal, and the loop-count-once idiom (`Missed()`, `allHits`, `differentEvents`) is used consistently where a sweep is unavoidable.
- Determinism and save fidelity are covered well. There are saved / never-saved / loaded triples, 1-vs-4-worker comparisons, replays recorded through the real `core::Logic`, and mid-volley, mid-burn, mid-arrival and mid-route saves.
- The save-refusal matrix is thorough: NaN, ranges, counters, versions, clocks, journals, plans and profiles. Many cases have a "sound" control beside them (`ArrivingSave`, the seal and volley cases).
- `support/fog.hpp` says explicitly that it computes its expectation independently of `core/fog.hpp`. That is the right pattern.
- `travel_tests.cpp` is the best file. Its fixtures are small and explicit, and its maps are built per concern (visit, danger, staircase).

**Weaknesses**
1. **Fixture cost.** `TestManifest()` re-parses `manifest.yaml` on every call, about 330 calls in this suite. `ShippedData()`, `ShippedWorld()` and `ShippedNames()` re-parse YAML and rebuild the map each call. `replay_tests.cpp`'s `Session` defaults to `ShippedData()`, so even arena, painted and cells sessions build the 50,000-NPC world, twice per test. Many save and scene tests use the shipped data where `SmallData()` would do. In debug builds this is probably the largest share of runtime.
2. **Monolithic files.** `host_tests.cpp` (3,709 lines, 135 cases), `save_tests.cpp` (1,810) and `combat_tests.cpp` (1,695) each run in one process. AGENTS.md parallelises per file, so `host` is the longest pole and the 3,700-line file is hard to navigate.
3. **Copy-pasted helpers.** `support/host.hpp` already offers `At`, `Tick`, `Foxes`, `FOX_HEALTH`, `Published` and `BodyOf`. Yet `host_tests`, `combat_tests`, `replay_tests` and `scenes_tests` each redefine some of them, sometimes with different semantics (`Foxes` includes or excludes dying foxes). `SamePoint`, `SameEvents`, `StepMonth`, `Loads*`, `NearCrystalData` and `FarFromTheCrystal` are duplicated too.
4. **Refusal tests assert only a bool.** `Loads()` and `LoadsWithMap()` return true or false and never say why a load was refused. `saves.hpp` hand-copies the format constants, the component list and the layout of `host.cpp`. If it drifts, nearly every `CHECK_FALSE(Loads(...))` passes vacuously. The few positive controls cover only some of those cases.
5. **Fuzz-seed gap.** "The save fuzz target's seeds load in its game" regenerates the saves in-process. It never reads the committed `tests/fuzz/corpus/save/*` files. A stale committed seed is only noticed by `check --sanitizers`, which is on request only.
6. **Shipped-data balance gates in unit tests.** `shipped_world_tests` pins road counts (2,400 to 3,300), travellers in the busiest view (at least 2,000), exact route costs (126, 162, 562, 173, 152) and city indices. These are data or balance facts, not code behaviour, and they will fail on any map edit.
7. **Weak or tautological tests.** Examples: "starting power is the same for the same identity and seed", "starting power lies in its bands", "a save after a fold loads", the stress-arena month test, `ticked >= 1`, and a test named for the lance's burst that tests aura period.
8. **Measurement code in a test file.** `MeasureLongSave` (about 100 lines in `shipped_world_tests.cpp`) re-implements `Travel::SaveDelta`'s classification. It will drift, and its "rest" figure is derived by subtraction.
9. **Shared temp paths.** The replay tests and the skipped measure tests use fixed temp-file names, which can collide across parallel presets or Wine runs.
10. **No change detector on simulation output.** Every determinism test compares two runs in one binary. The only golden values are three `StartingPower` numbers. This follows the recorded decision (no cross-platform determinism goal), but a silent change to what the sim produces is only caught by the data-hash tests.

**Gaps against src/game**
- Few negative cases for `Activate`, `Deactivate`, `Materialise` and `Fold`.
- `Relationships::Load` has no truncated or hostile-input test.
- No test of `GenerateWorld` with different seeds producing different worlds.
- The painted, cells and stress scenes are barely tested beyond instance counts.

**Overall health:** good. The correctness coverage is strong. The main risks are test-suite maintainability (file size, duplicated helpers, fixed magic numbers from shipped data) and fixture cost. Refusal tests that pass for the wrong reason are the main correctness risk.

**Files read in full:** (not reported)

## Findings

### TESTS-GAME-01: Replay Session defaults to the shipped 50,000-NPC data for every scene

- **Severity:** medium · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/game/replay_tests.cpp:58`

Session's constructor default is `const core::GameData& data = test::ShippedData()`. The cells, painted and arena sessions, and every playback session, build the full shipped world although they never show it. Most tests make two sessions.

**Evidence:** `explicit Session(const std::filesystem::path& folder, core::SceneKey scene = game::ARENA_SCENE, const core::GameData& data = test::ShippedData(), u32 workers = WORKERS)`. The second constructor hard-codes `test::ShippedData()` as well (line 73).

**Impact:** The replay file is a slow serial chain. AGENTS.md says to keep the shipped map for cases that need its geometry or size.

**Recommendation:** Default to SmallData() or BuiltInData() and pass ShippedData() only in the world and walk-to cases. Also stop calling test::ShippedWorld() repeatedly.

**Verification:** replay_tests.cpp:58 and line 73 default to test::ShippedData(), and CELLS/PAINTED scene sessions at lines 174 and 229 pay for it. AGENTS.md says to keep the shipped map for cases that need its geometry or size. I did not check whether replay hashes depend on the shipped data. The replay-tests-per-file process is the serial critical path.

### TESTS-GAME-02: Helper functions duplicated across test files, some with divergent semantics

- **Severity:** medium · **Verdict:** confirmed · **Category:** duplication · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/game/combat_tests.cpp:31`

support/host.hpp provides At, Tick, Foxes (dying included), FOX_HEALTH, Published and BodyOf. combat_tests.cpp:31-82 and 24 redefine At, Foxes, FootOf, Published, Entities and FOX_HEALTH. host_tests.cpp:36-56, 747 and 769-783 redefine Tick, Published, At, FootOf and Foxes. replay_tests.cpp:260-275 defines Foxes excluding dying foxes. scenes_tests.cpp:312-354 redefines At, Tick, StepMonth and FootOf. SamePoint and SameEvents are copied in save_tests, travel_tests and host_tests. NearTheCrystal (combat:107) duplicates NearCrystalData (host:737), and FarFromTheCrystal duplicates the host aura fixture. StepMonth, Loads/LoadsWith/LoadsWithMap and CultivatorAt also repeat.

**Evidence:** Same-named functions in five files. replay_tests.cpp:264 `&& !entities.all_of<game::Dying>(entity)` versus support/host.hpp Foxes doc 'dying ones included'.

**Impact:** Fixes must be made in several places. A reader cannot tell whether `Foxes` includes dying foxes (replay_tests excludes them, support includes them), which risks silent misuse.

**Recommendation:** Move the shared helpers into support/ and delete the local copies. If a variant is intended, name it (LivingFoxes).

**Verification:** Foxes is defined locally in combat_tests.cpp:61, host_tests.cpp:774 and replay_tests.cpp:260, alongside support/host.hpp:47, which says 'dying ones included'. FootOf appears in combat:76, host:769 and scenes:345. At appears in combat:31, host:747 and scenes:312. Tick is in host:37 and scenes:317. SamePoint is in host:1675 and save:1190. CultivatorAt is in scenes:607 and host:1558. The replay variant excludes Dying (finding's claim, not re-read), so the same-name divergence risk is real. I did not check whether a local version differs only in signature.

### TESTS-GAME-03: Save-refusal tests assert only that a load failed, so they can pass for the wrong reason

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** test-quality · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/game/save_tests.cpp:421`

`Loads()`, `LoadsWithMap()` and `LoadsWith()` return a bool. Over 40 `CHECK_FALSE(Loads(...))` cases have no check on why Host::Load refused. support/saves.hpp hand-copies HOST_FORMAT, ACTIVE_VERSION, WORLD_VERSION, the ActiveComponents order and the whole section layout. If any drifts, or a different validation trips first, the refusal tests still pass. Positive controls exist for only some cases (for example 'a sound save built as the refusals…', ArrivingSave and the seal sound/never-ending pair).

**Evidence:** `bool Loads(...){ ... const bool loaded = game.Load(reader, savedTick); ... return loaded; }`; saves.hpp:62-70 `HOST_FORMAT = 10`, `WORLD_VERSION = 8` duplicated from host.cpp:85-92.

**Impact:** A format bump or helper drift can turn the whole refusal matrix green vacuously, which hides regressions in validation. That is the most important property for untrusted saves.

**Recommendation:** Add a paired positive control per refusal builder, or have Load report a reason or error code (core::Error) so tests assert the reason. Add a test that ArenaSave's constants equal host.cpp's, for example by exposing a Host::Format() accessor.

**Verification:** Loads() at save_tests.cpp:421 returns only a bool, so tests cannot assert the refusal reason. It does CHECK that the state hash is unchanged and that Requested() matches the load result. That catches partial-load bugs but not a wrong reason. Hand-copied constants live in saves.hpp (HOST_FORMAT, WORLD_VERSION). Some positive controls exist, as the finding admits. The vacuous-pass risk is real but mitigated, so high is overstated. I did not check whether a drift test for the constants exists.

### TESTS-GAME-04: Fuzz-seed test never reads the committed corpus files

- **Severity:** medium · **Verdict:** confirmed · **Category:** test-gap · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/game/save_exactness_tests.cpp:486`

'the save fuzz target's seeds load in its game' regenerates the fight, world, delta and scratch saves in-process and loads them. It never reads tests/fuzz/corpus/save/{fight,world,delta,scratch}, the files the fuzzer actually starts from. AGENTS.md says regenerate all four whenever the save layout changes, and this test is meant to check them.

**Evidence:** `CHECK(FuzzGameLoads(Saved(fight))); CHECK(FuzzGameLoads(WorldSeed())); ...` uses regenerated bytes only; WriteSeed is only in skipped cases.

**Impact:** A stale committed seed (old format) is not caught by the unit gate. It silently degrades to refusal-only fuzzing until `check --sanitizers`, and even that only replays it without flagging it.

**Recommendation:** Read each file from PN_FUZZ_CORPUS in the test and CHECK(FuzzGameLoads(bytes)), or compare it byte for byte with the regenerated save.

**Verification:** The test at save_exactness_tests.cpp:486 only loads regenerated bytes (Saved(fight), WorldSeed(), DeltaSeed(), ScratchSeed()). PN_FUZZ_CORPUS is read only in the WriteSeed helper (line 165). tests/AGENTS.md says the test 'checks each loads' and to regenerate all four on a layout change, so a stale committed file is not caught by the unit gate.

### TESTS-GAME-05: TestManifest() and the Shipped*() fixtures re-parse YAML on every call

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/support/manifest.hpp:19`

TestManifest() loads assets/manifest.yaml each call, about 330 calls in tests/unit/game. ShippedWorld(), ShippedNames(), ShippedAnimations() and ShippedData() likewise re-read YAML and rebuild the map each call. replay_tests.cpp calls test::ShippedWorld() twice in one test body (lines 559-560 and 608-609).

**Evidence:** manifest.hpp: `std::expected<core::Manifest, core::Error> loaded = platform::AssetsDirectory().and_then(... data::LoadManifest(...))` on every call; world.hpp ShippedData() builds `data.world = ShippedWorld(); data.names = ShippedNames();` each time.

**Impact:** Large avoidable debug-build runtime, which AGENTS.md already flags for the shipped map (a large part of a second in debug). It also inflates the longest-pole files.

**Recommendation:** Cache the immutable result in a function-local static shared_ptr<const T>. The result is const and shared anyway, and the abort-on-failure contract is unchanged.

**Verification:** manifest.hpp:19 TestManifest() reloads every call (~310 calls in unit/game). ShippedWorld() (shipped_world.hpp:23) has no static cache, and replay_tests.cpp:559-560 and 608-609 call it twice in one body. ShippedData() rebuilds each call. Medium is too high: the manifest YAML is small. The shipped world is the real cost, and that is already covered by finding 1.

### TESTS-GAME-06: Shipped data used where small data suffices in save, exactness and scene tests

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/game/save_tests.cpp:83`

'a save keeps the scene counter' (line 83), 'the world scene's NPCs number from 1' (line 70) and 'a save made in world loads back' (line 46) build two 50,000-NPC hosts each. exactness 'a loaded world carries on' builds six shipped hosts (two worker counts × three hosts). scenes_tests 'each scene's camera reaches the snapshot' builds five shipped hosts.

**Evidence:** `game::Host saved(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());` in a test that only compares SceneIds().next.

**Impact:** Slow tests that check scene counters and cameras, which do not depend on the shipped size.

**Recommendation:** Use SmallData() or TravellingSmallData() unless the geometry or size is the point.

**Verification:** save_tests.cpp:70 and 83 use ShippedData() for scene-counter and NPC-numbering checks, though line 70 does use the shipped map's size in its body, so that one may need it. The scene-counter case at line 83 looks replaceable. I did not check the exactness or scenes cases. This is a runtime nit, not a correctness problem.

### TESTS-GAME-07: host_tests.cpp is a 3,709-line, 135-case monolith (combat and save tests are similar)

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/game/host_tests.cpp:1`

Publishing and culling, hits, glows, seals, portals, the calendar, travel drawing, walk frames, walk-to routes, fog and the cultivator's walk are all in one file. `unit_tests()` runs one process per file, so this file is the critical path. combat_tests (1,695 lines) and save_tests (1,810 lines) are also oversized.

**Evidence:** 3709 lines, 135 TEST_CASEs.

**Impact:** The slowest file sets the wall time, the file is hard to navigate, and an unrelated failure hides the rest of its process's output.

**Recommendation:** Split by area: host_publish, host_hits, host_calendar, host_travellers, host_walkto, host_fog, host_cultivator_walk, and likewise for save and combat. Move the shared TravelWorld and CornerWalk fixtures into support/.

**Verification:** Line counts hold: host 3709 with 135 TEST_CASEs, combat 1695, save 1810. tests/AGENTS.md:16 says each file runs in its own process, so the biggest file is the wall-time pole. Splitting is a taste and maintainability call, and no doc forbids it. Impact is modest. 'An unrelated failure hides output' is weak, since doctest reports each case.

### TESTS-GAME-08: ArenaSave takes 14 positional parameters and is called with repeated std::nullopt

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** maintainability · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/support/saves.hpp:232`

Calls such as `ArenaSave(state, {}, std::nullopt, std::nullopt, std::nullopt, game::WORLD_SCENE, test::WORLD_VERSION, test::ACTIVE_VERSION, {...clock}, travel, std::nullopt, game::Relationships{}, {}, &data)` appear in at least ten places in host_tests, save_tests, scenes_tests, npcs_tests, replay_tests and save_exactness_tests. Parameters are easy to transpose, since several are `{}`.

**Evidence:** saves.hpp ArenaSave signature.

**Impact:** A mis-ordered argument silently builds a different save, and the refusal tests then pass for the wrong reason.

**Recommendation:** Replace the parameters with an options struct using designated initialisers (`SaveSpec{ .scene=..., .clock=... }`).

**Verification:** The ArenaSave signature is long with many positional optional and {} parameters, and I did not re-count it or the call sites. An options struct is a reasonable cleanup. The 'refusal tests pass for the wrong reason' impact overlaps finding 6. I rate it a maintainability nit.

### TESTS-GAME-09: Fixed temp folder and file names collide across concurrent runs

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** test-isolation · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/game/replay_tests.cpp:144`

`Folder` uses `temp_directory_path()/project_n_replay_tests_<name>` and calls `remove_all` in its constructor. The skipped measure cases use fixed names (project_n_measure_save.bin, project_n_measure_hours*.bin). Presets built under `check`, Wine runs and sanitizer runs may share a temp directory.

**Evidence:** `std::filesystem::temp_directory_path() / std::format("project_n_replay_tests_{}", name)`; `remove_all(m_path)` in the constructor.

**Impact:** Two simultaneous runs of the same test delete each other's replay and save files, giving a flaky 'replay not found' or hash mismatch. This breaks AGENTS.md's 'contained, no shared state' rule.

**Recommendation:** Add a per-process component, such as the pid or a random suffix, or use `mkdtemp`-style unique directories.

**Verification:** The fixed names are real: replay_tests.cpp:144 uses project_n_replay_tests_<name> and calls remove_all, and shipped_world_tests.cpp:553 and :750 use fixed measure file names. The measure cases are skipped by default, so they do not matter for the gate. A collision needs two runs sharing one temp dir at once, which is unlikely: presets build into separate trees and run sequentially under check. I did not verify the TMPDIR setup for the Wine and container runs. A real but low-probability hazard.

### TESTS-GAME-10: Shipped-data balance and demo-load values asserted in unit tests

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** test-quality · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/game/shipped_world_tests.cpp:319`

These cases pin shipped data rather than code behaviour: 'the shipped roads carry 2,400 to 3,300 at once' (its own comment says 'a demo load, not a balance target'); 'the busiest view holds at least 2,000 travellers'; 'the shipped cities' shares sum to 50,000' with capital == largest; exact route costs 126/162/562 and 173/152; the 80/18/2 power-band split ±2; and `onRoads == {ghostlight_woods, jade_cave, bone_forest}` in map order. The PAIRS array uses raw place indices 0-4 while the comments name capital, port, sect and so on.

**Evidence:** `CHECK(std::ranges::min(counts) >= FEWEST); CHECK(std::ranges::max(counts) <= MOST);`; `constexpr std::array<CityPair, 8> PAIRS{ { { .from = 0, .target = 1 }, ...`

**Impact:** Any legitimate map or data tweak breaks the gate. Reordering places in world.yaml silently changes the cases, which still pass.

**Recommendation:** Move the data-shape checks to data_tests or art/selftest. Look places up by key (as PlaceIndex already does). Keep only structural invariants here, such as reachability and search equals distance map.

**Verification:** The shipped-data pins exist, and the test's own comment at line 315 calls the road load 'a demo load, not a balance target'. Pinning shipped data is arguably the intent of a shipped_world suite, and the comment accepts that an edit may move the count. I did not check the claim that PAIRS uses raw indices or the exact route costs. The impact is maintenance friction, not a hole in coverage.
