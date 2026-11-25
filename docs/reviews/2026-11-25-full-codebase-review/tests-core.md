# Review: tests-core

**Scope:** tests/unit/core/*: TEST QUALITY review as above, gaps versus src/core.

**Findings:** 13 standing (0 critical, 0 high, 1 medium, 10 low, 2 info), 0 raised but refuted on verification.

**Parts:** tests-core.md, tests-core-part2.md

## Assessment

## Assessment of tests/unit/core (test quality and gaps versus src/core)

**Coverage of the read files.** I read in full: animations, archive, command_line, cxx23, error, ecs, fog, log, names, random, rect, repeated_failure, schedule, seal, strings, display_delay, tick_clock, glow, leg_walk_cache, game_data, saves, scene_reports, jobs, math, paths, frame, frame_timeline, instances, draw_list, delta, handover, state, save_file, replay, ui_colours, and the first 520 lines of logic_tests. For logic (rest), volley, journey, world_map and road_curve I read the case lists plus selected bodies, not every line; none of the unread parts were needed to support a finding below. Read-only constraints respected, and nothing was built or run.

**Strengths**
- Strong behaviour focus. Test names are specific and sentence-like. Malformed-input paths are covered unusually well: delta.hpp has about 25 SUBCASEs for malformed deltas and orders, replay has truncation, forged counts and bad kinds, and save_file covers LZ4 version, size lies, truncation and rename failure.
- Thoughtful regression anchors. The Random golden value, the SealKey and FNV pins, and the half-float rounding table are the right shape for replay-critical code. The `jobs` DoneIntrusion race test, the 1-vs-N-worker bit-identity sum, and `frame_timeline_tests` (which drives the real PresentFrame on a fake clock and proves the old frame order is caught) are high-value.
- The logic suite uses a faithful Recorder and Rig and exercises the real handover and replay flow: pause, calendar step, scene change, load, replay divergence.
- The tests mostly follow tests/AGENTS.md (anonymous namespaces, named constants, no standard includes, no design-process citations in comments; my grep found none).

**Weaknesses and risks**
1. Determinism anchoring is thin outside Random and SealKey. `core::math::{Sin,Cos,Exp,Log,Pow,Atan2}` exist precisely so simulation maths is reproducible, yet the tests only compare against the platform's libm within 4 ulps. A change to the algorithm that stays inside the tolerance would silently change every replay and save. No golden bit pattern is pinned. The volley "bit for bit" test compares a volley with a copy of itself, which proves nothing about stability. The Hasher, Names hash and `GameData::Hash` have no pinned expected values either, only equality and inequality relations, although "the algorithm is part of the replay format".
2. `WorldMap::Build` `CheckShape` has several refusal branches with no test (zero columns or rows, MAX_TILES overflow, bad tile size, kind count, grid kind out of range, places at NO_PLACE). Procedural worlds make the MAX_TILES and u16 place-index limits real scaling edges, so these are cheap but valuable tests.
3. DrawList is only ever published with `Jobs{1}`. The parallel parts path ("filled in parts on the job system") is therefore not exercised across workers. Nor is there a 1-vs-N publish identity check like the one in `jobs_tests`.
4. Statistical tests on tiny samples: names ("about seven in ten" on 20 ids, bound 10..18) and weights (20 ids, at least 15) can only fail by a code change, not by chance, since they are deterministic. They are weak as specifications and fragile to any RNG-purpose tweak, and the `ToBelow` and Names outputs are not pinned.
5. Several tests violate the "never a sweep" rule or are costly: math (8 sweeps of 20,001 points), the 150,000-round jobs race, the 100,000-draw random bucket test, frame_timeline's multi-second simulated runs, and the 65,536-half loop (that last is justified as exhaustive). Some tests hang rather than fail if threading breaks, e.g. the `while(!workerRan)` spin in jobs_tests.
6. Duplicated helpers: the RAII temp-folder class is copied three times (saves, save_file, logic) and uses fixed names under the shared temp dir, so two concurrent runs of the same suite (two worktrees, presets) can collide. The save magic constant is copied three times in save_file_tests. The replay tests recompute the first-kind byte offset in two places.
7. A few weak or tautological assertions: `same identity and seed give same name` (calls the same function twice), `the same volley gives same positions bit for bit` (see 1), `differ == 0` counters that lose which tick failed, and `a tiny save...` tests that parse the error string after the last ': '.
8. Some src/core headers have no direct tests in core_tests: `CombatRules::Valid`, `Ability::Valid`, `Seal*::Valid` and `effects` Valid, `Vec2` operators and `IsFinite`, `view_limits` constants, and `assert.cpp`. Several are exercised indirectly in `unit/data` or `unit/game`, but a regression in a core `Valid()` predicate (which gates untrusted YAML) would surface only through YAML-driven tests.
9. Failure states left unchecked. A refused `LoadDomainDelta` or `LoadDomainOrder` is never checked for whether the state is left half-applied. Nothing tests `Saves`' behaviour on a failed write (only the success paths and the empty slot). The `replay` forged-count tests pass because a count above the remaining bytes is refused, but the amplification boundary (ticks equal to Remaining, a known-deferred audit item) is not tested.

**Overall health:** good. The suite is detailed and well organised, and many tests were clearly written from real bug history. The main risks are the missing golden anchors for replay-critical numeric code, a handful of untested validation limits that matter once maps get large, and test-infrastructure duplication.

**Files read in full:** /home/lotus/Development/project_n/tests/unit/core/animations_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/archive_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/command_line_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/cxx23_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/error_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/ecs_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/fog_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/log_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/names_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/random_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/rect_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/repeated_failure_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/schedule_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/seal_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/strings_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/display_delay_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/tick_clock_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/glow_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/leg_walk_cache_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/game_data_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/saves_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/scene_reports_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/jobs_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/math_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/paths_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/frame_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/frame_timeline_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/instances_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/draw_list_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/delta_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/handover_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/state_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/save_file_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/replay_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/ui_colours_tests.cpp, /home/lotus/Development/project_n/tests/unit/core/main.cpp, /home/lotus/Development/project_n/tests/unit/core/logic_tests.cpp (lines 1-520 plus case list), /home/lotus/Development/project_n/tests/unit/core/volley_tests.cpp (header, case list, selected cases), /home/lotus/Development/project_n/tests/unit/core/journey_tests.cpp (case list, selected cases), /home/lotus/Development/project_n/tests/unit/core/world_map_tests.cpp (case list, lines 195-340), /home/lotus/Development/project_n/tests/unit/core/road_curve_tests.cpp (case list only)

## Findings

### TESTS-CORE-01: core::math functions only compared with platform libm within 4 ulps; no pinned golden values

- **Severity:** medium · **Verdict:** confirmed · **Category:** determinism-tests · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/core/math_tests.cpp:26`

Sin/Cos/Exp/Log/Pow/Atan2 are tested by WorstError against std::sin/std::exp/etc. over 20,001 samples with a 4*epsilon tolerance (ULPS). There is no test pinning exact bit patterns of any core::math result for fixed inputs, unlike random_tests.cpp and seal_tests.cpp which pin golden values.

**Evidence:** constexpr f64 ULPS = 4 * std::numeric_limits<f64>::epsilon(); ... CHECK(WorstError(ours, platform, ...) < ULPS). Only Exp(0)==1, Log(1)==0, Atan2 axis exacts are exact.

**Impact:** The reason core::math exists is bit-reproducible simulation maths. A change to the polynomial, range reduction or constants that stays within 4 ulps of libm passes all tests while changing every simulated value, replay and save hash. The comparison against std:: also makes the test platform dependent.

**Recommendation:** Add a small table (about 20 inputs per function including range-reduction boundaries) of exact expected u64 bit patterns, like HALVES in instances_tests.cpp, and keep the ulp sweep as a secondary accuracy check.

**Verification:** math_tests.cpp has only ulp comparisons against std:: (cases at lines 43-110) plus exact special values (Exp(0), Log(1), Atan2 axes). No pinned bit patterns. Design 0002:261 says core::math exists for cross-machine identical results, so the claim holds. The tests also depend on the platform libm as the reference.

### TESTS-CORE-02: "same volley bit for bit" test compares a volley with a copy of itself

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** weak-test · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/core/volley_tests.cpp:861`

The test calls SwordAt(VOLLEY, ...) and SwordAt(copy, ...) with an identical copy and compares bits. It only proves the function is pure for identical input within one binary, and it is reversal-ordered to catch memoised state.

**Evidence:** const core::Volley copy = VOLLEY; ... first = SwordAt(VOLLEY,...), second = SwordAt(copy,...); same = same && bit_cast<u64>(first.position)==bit_cast<u64>(second.position)

**Impact:** The name overclaims. No expected positions, headings or arrival ticks are pinned for a fixed seed, so a refactor or compiler flag change that shifts every sword (and ArrivalTick/SwordTarget, which feed gameplay hit resolution) would not be caught by this suite.

**Recommendation:** Pin a handful of SwordAt results (positions and headings as bit patterns or tight literals) and ArrivalTick values for the shipped swords volley at 4 to 6 swords and ticks, and rename the purity test accordingly.

**Verification:** The test at volley_tests.cpp:861 does compare VOLLEY with a copy of itself, and the name overclaims. But it is a purity and no-hidden-state check, and the comment says so. Other volley tests may pin positions, which I did not exhaustively check. The core point that this test pins nothing holds. Severity is lower because it is a naming and coverage nit.

### TESTS-CORE-03: WorldMap::Build shape/limit refusals untested (size, MAX_TILES, tile size, kind count, grid kind, place count)

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** missing-test · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/core/world_map_tests.cpp:205`

CheckShape in src/core/world_map.cpp (about lines 187 to 260) refuses columns==0 or rows==0, columns*rows>MAX_TILES, tileSize non-positive/NaN/infinite, kinds empty or more than 255, a grid byte >= kinds.size(), and places.size() >= NO_PLACE. The 'Build refuses a map it can't use' case covers short grid, costs, roads, rivers, fords, places and starts, but none of these.

**Evidence:** grep for MAX_TILES finds it only in src; tileSize appears in tests only as a hash-difference input (line 506); no case builds a 0-column map, a NaN tile size, or a grid value past the kinds.

**Impact:** The u32 tile index, u8 grid kind and u16 place index are exactly the limits a very large procedural map hits. A regression (for example an overflow in the columns*rows check, or dropping the grid-kind check that guards later indexing) would pass the suite.

**Recommendation:** Add one-line refusals for each branch, including columns*rows overflowing u32 and exactly MAX_TILES+1, kinds.size()==256, a grid byte equal to kinds.size(), and places.size()==NO_PLACE (a source built cheaply by resizing).

**Verification:** grep of world_map_tests.cpp finds no MAX_TILES or NO_PLACE refusal cases. The tileSize hits are only a hash input at line 506 and the builder at line 31. I did not open CheckShape in src/core/world_map.cpp, so the exact list of unguarded branches is unchecked. The absence of tests for those limits looks real.

### TESTS-CORE-04: DrawList publish never tested with more than one worker or part

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** missing-test · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/core/draw_list_tests.cpp:150`

Every Publish test uses core::Jobs jobs{1}. The header says publish packs instances in parallel from parts, with the order defined as parts in order. Only one test uses multiple parts (Begin(VIEW, 3)) and it runs on one worker.

**Evidence:** core::Jobs jobs{ 1 }; list.Publish(snapshot, jobs); appears in every case; no comparison across worker counts.

**Impact:** A data race or ordering bug in the parallel pack or sort path (for example parts written out of order, or an offset computed from scheduling) would only surface in the game or under TSan in other suites, not here. The snapshot order is part of what the renderer and replays-of-pictures depend on.

**Recommendation:** Add a case publishing a few thousand instances over many parts with Jobs{1} and Jobs{7} and asserting identical snapshots (instances, ids, batches), alongside the existing 1 vs N pattern in jobs_tests.cpp.

**Verification:** draft_list_tests.cpp uses core::Jobs jobs{ 1 } in every Publish test I saw (lines 150, 169, 197, 209, 222). There is no comparison across worker counts. I did not verify whether the other suites cover this. The gap is real, but the impact depends on how parallel the pack is.

### TESTS-CORE-05: Hasher/Names/GameData hashes tested only by relations; no pinned values for a format that replays depend on

- **Severity:** low · **Verdict:** unverifiable · **Category:** determinism-tests · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/core/archive_tests.cpp:140`

archive.hpp documents the hash algorithm as part of the replay format, but tests only assert Hash(a)==Hash(a) and Hash(a)!=Hash(b) (archive, names, game_data, seal). Contrast random_tests, which pins a golden.

**Evidence:** CHECK(Hash(ONE, TWO) == Hash(ONE, TWO)); CHECK(Hash(ONE, TWO) != Hash(TWO, ONE)); ... base.Hash() == Table().Hash()

**Impact:** Any change to Mix, the byte folding or size-prefix handling that keeps the inequalities true silently invalidates every recorded replay's hashes and the data hash gate, with no failing test naming the change.

**Recommendation:** Pin one Hasher value for a fixed mixed sequence (scalar, enum, bool, vector, struct) and one HashText value.

**Verification:** A grep for hash assertions with 0x literals in tests/unit/core found nothing, which is consistent with relation-only hash tests. I did not read archive_tests.cpp or the other hash tests, so I did not check whether a pinned value exists elsewhere, for example in seal tests.

### TESTS-CORE-06: Archive Reader hardening cases missing: huge vector size, invalid enum, f32 NaN/-0 round trip

- **Severity:** low · **Verdict:** unverifiable · **Category:** missing-test · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/unit/core/archive_tests.cpp:100`

The Reader test for sizes only checks size 3 with two elements. No test gives a vector size near u64 max (multiplication overflow of size*element size, or a reserve before checking), an enum value outside its range, or a vector of non-Scalar fields whose size exceeds Remaining.

**Evidence:** 'a vector whose size is past what is left fails' uses SIZE = 3; Colour round trip only writes valid values.

**Impact:** The save and replay readers are untrusted-input paths (fuzzed, but fuzz is not run in a plain gate). An allocation-before-validation regression in the vector read would be caught only by fuzzing.

**Recommendation:** Add a size of ~u64{0} and of 2^61 over a u32 vector (fails, no allocation, value untouched), and decide and test what an out-of-range enum does.

**Verification:** I did not open archive_tests.cpp at line 100 or the Reader code. The finding says the vector-size test only uses SIZE = 3. The backlog's audit 2 note says Reader::GetSequence bounds by bytes, which suggests the hardening exists. Whether it is tested is unchecked.

### TESTS-CORE-07: Replay forged-count tests do not exercise the reserve amplification boundary

- **Severity:** low · **Verdict:** deliberate · **Category:** known-gap · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `tests/unit/core/replay_tests.cpp:262`

The 'claiming more ticks than it holds' case uses 1,000,000,000, which is rejected by `ticks > Remaining()`. The residual risk, a count equal to Remaining() reserving 8 to 16 bytes per byte of file, is not tested or bounded.

**Evidence:** Put(replay, Locate(replay).tickCount, HUGE_COUNT); src/core/replay.cpp: if (ticks > archive.Remaining()) Fail(); tickStarts.reserve(ticks); held.reserve(...)

**Impact:** This is the recorded audit 2. The tests give false assurance that a malformed replay cannot reserve large memory; a 64 MB replay file could reserve about 1 GB.

**Recommendation:** When fixing the audit, add a test with a count of exactly Remaining() on a small body that checks the vectors' capacity stays small (or the replay refuses).

**Verification:** sweep-up-backlog.md line 90 records this as audit 2: ReadTicks checks ticks only against Remaining() and then reserves. It plans a test-first fix, so the gap is a known deferral. The finding's own wording that a count equal to Remaining() amplifies the reserve matches the backlog.

### TESTS-CORE-08: Test infrastructure duplicated: temp-folder RAII class (x3) and save magic (x3), fixed shared temp names

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** duplication · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/core/saves_tests.cpp:13`

saves_tests.cpp (Folder), save_file_tests.cpp (Scratch) and logic_tests.cpp (line ~699) each define a near-identical RAII temp folder using temp_directory_path()/'project_n_<suite>_<name>' with remove_all at construction. save_file_tests.cpp hard-codes the save magic 0x0045'5641'535f'4e50 three times (lines 142, 163, 199).

**Evidence:** std::filesystem::temp_directory_path() / std::format("project_n_saves_tests_{}", name) in saves_tests.cpp:13, save_file_tests.cpp:13, logic_tests.cpp:699

**Impact:** Two concurrent runs of the same suite (two worktrees or presets sharing /tmp) delete each other's folders mid-test, giving rare flaky failures; the repeated magic breaks three places if the header layout changes. tests/AGENTS.md also asks tests to be contained.

**Recommendation:** Move one ScratchFolder into tests/support/ with a per-process unique name (pid or random suffix), and give the save header a test-visible constant or writer helper instead of the repeated literal.

**Verification:** saves_tests.cpp:13 defines Folder with a fixed name, project_n_saves_tests_<name>, in temp_directory_path, and calls remove_all at construction. The collision risk when two runs share /tmp holds. I did not check save_file_tests.cpp, logic_tests.cpp or the magic literal duplication. Impact is only a rare flake.

### TESTS-CORE-09: Statistical tests with tiny samples and unpinned outputs (names, weights)

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** weak-test · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/core/names_tests.cpp:51`

'surnames follow their weights' uses 20 ids and requires >=15 of weight 9:1; 'two given characters about seven times in ten' uses 20 ids with a 10..18 window. The outputs are deterministic for a given Random, so they pass or fail on the exact RNG purpose and sequence, not on the statistical property they name. 'the same identity and seed give the same name' calls the same function twice.

**Evidence:** constexpr u64 NAMED_IDS = 20; CHECK(first >= MOST_FIRST); CHECK(two >= LEAST_TWO); CHECK(two <= MOST_TWO);

**Impact:** They neither verify the claimed distribution (the window is too loose, and 20 samples cannot detect a bias) nor pin NameOf. A weighting bug (for example using weights as cumulative bounds wrongly) could still pass.

**Recommendation:** Pin NameOf for about 6 (seed, id) pairs as exact (surname, first, second) values, and test the weight selection with a deterministic function (a table walk at known random values) instead of a sample count.

**Verification:** Facts hold: NAMED_IDS=20, MOST_FIRST=15 of weight 9:1, two-given window 10..18 (lines 9-15, 51-70). Outputs are deterministic, so they pin the RNG sequence, not a distribution. The 15/20 floor would still catch gross weight misuse, so 'could still pass' is overstated. No exact pinning of NameOf exists. This is also within tests/AGENTS.md's ~20-sample guidance.

### TESTS-CORE-10: Sweep-style and unbounded-spin tests cost time and can hang

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** test-hygiene · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/core/jobs_tests.cpp:93`

The DoneIntrusion regression runs 150,000 rounds (its own comment: about 0.15 s on many cores, one core can't show the race); random_tests loops 100,000 draws and math_tests 8 sweeps of 20,001 points, contrary to tests/AGENTS.md 'about 20 samples, never a sweep'. The 'every parallel-for chunk runs in the simulation's FP environment' case spins forever (while(!workerRan)) if no worker thread ever runs a chunk.

**Evidence:** constexpr u32 ROUNDS = 150'000; ... while (!workerRan.load(...)) std::this_thread::yield(); in jobs_tests.cpp:93

**Impact:** Slower gate (especially under TSan, Wine, Windows CI) and a hung rather than failed test if workers regress; the hang is only killed by an outer timeout.

**Recommendation:** Bound the spin with a deadline that fails loudly (as the reference tests do), and move the 150k-round race and the libm sweeps to fuzz/skip-by-default cases or reduce them with a documented detection probability.

**Verification:** The unbounded spin at line ~93 is real: it yields until a non-forker thread runs a chunk, with no deadline. The 150k-round race test is real (line ~172), but its comment documents the cost (about 0.15 s) and the detection probability, so that part is deliberate. The libm sweeps and the random_tests loop were not checked. tests/AGENTS.md line 9 does say never a sweep, so some tension exists. The hang is a real but low risk.
