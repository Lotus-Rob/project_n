# Review: x-test-strategy

**Scope:** CROSS-CUTTING test strategy: map every src/ file to its tests and list untested or thinly tested code; judge the reference-picture tests, fuzzing, determinism/replay tests, the bench, coverage tooling, and what classes of bug the current suite cannot catch (integration, long-run, cross-platform, GPU). Do not duplicate per-file test-quality nits; focus on the strategy and the gaps.

**Findings:** 15 standing (0 critical, 0 high, 0 medium, 12 low, 3 info), 0 raised but refuted on verification.

**Parts:** x-test-strategy.md, x-test-strategy-part2.md

## Assessment

The strategy is unusually disciplined for a project this size: about 1,630 doctest cases in per-area executables, each test file in its own process, and mirrored layout. Fuzz targets cover every reader of untrusted bytes (replay, save, yaml). Replay and save tests check the state hash on 1 and 4 workers, and pictures are compared byte-near on software Vulkan. Reference cases are bounded in time, and the shimmer and jitter measurements are a good way to catch visual regressions that pixel diffs miss.

The weaknesses are about what runs where and when, and about what no test reaches.
- Almost everything that finds memory, UB or thread bugs, namely Linux ASan, UBSan, TSan, the fuzz seed replays and the headless bench, runs on request only. CI has only Windows and macOS ASan, plus a normal-mode release run.
- The reference pictures run on lavapipe on Linux alone. No real GPU, driver, swapchain, present-mode or Metal/D3D path is tested. The renderer (1,751 lines), sprite_array, the GPU features and app.cpp (907 lines) are covered only through those pictures.
- main.cpp, the SDL event path and the input-to-command integration have no test.
- The long-run and multi-system tests (an hour of play at 1 and 4 workers) are skipped and not gates.
- Determinism is guarded only within one binary (a recorded decision), so a refactor that changes hash-relevant behaviour is invisible: there is no golden hash, and a replay recorded by an older build is refused by version anyway.
- Coverage is reported but has no threshold, no CI step and no per-file floor.

These classes of bug are structurally invisible to the suite.
- Gameplay balance and soak behaviour: month-step drift, NPC population collapse, relationship explosion over game-years, and save growth.
- The real-time main-thread hitch, which only `measure` sees and which gates nothing.
- Cross-platform float differences in non-sim code, such as the renderer.
- Races that appear only on real hardware.
- Regressions in the game-design systems that do not exist yet. When cultivation, items and sects land, the test shape (a small host save, 20 samples) will need property-style or scenario tests that the current guidance rules out.

Overall health is good for what exists. The risk is that the gate (`check`) is strong for logic and weak for the platform, GPU and long-run classes, and the sanitizer coverage depends on someone asking for it.

**Files read in full:** (not reported)

## Findings

### X-TEST-STRATEGY-01: Linux ASan/UBSan/TSan, fuzz seed replay and the bench are not in CI or the gate

- **Severity:** low (reviewer said high) · **Verdict:** deliberate · **Category:** test-strategy · **Status:** new · **Reviewer confidence:** high
- **Where:** `.github/workflows/ci.yml:252`

The only sanitizer runs in CI are Windows and macOS ASan (lines 252 and 327). `check --sanitizers`, which covers the fuzz seed replays (linux-clang ASan), UBSan, TSan and leak checks, is on request only. The headless bench's phase-4 gates are also never run by CI or `check`.

**Impact:** UB, data races, signed overflow and out-of-bounds reads on the Linux compilers can sit unseen for weeks. The Gotchas record two bugs (a dangling reference, and a use-after-free that 'only ASan saw') that were caught by luck of which job ran. Fuzz seeds are the regression tests for every found crash, and none of them run in CI.

**Recommendation:** Add one scheduled (nightly) or per-PR Linux job running `test --preset linux-clang --sanitizer address` (including the fuzz seed suites) and a UBSan one, and a TSan job for the unit tests only (the reference tests are excluded already). Run the bench with a generous margin on a schedule to catch order-of-magnitude regressions.

**Verification:** Checked ci.yml: ASan runs only at lines 252 and 327 (Windows, macOS). AGENTS.md line 25 and tests/AGENTS.md line 24 record sanitizers as on request only, never in plain `check`. Fuzz seed replay is linux-clang ASan only (tests/AGENTS.md line 22), so it is not in CI either, and the bench is 'run on request' (tests/AGENTS.md line 19). Facts hold, but the policy is the user's recorded decision. The claim of 'bugs caught by luck' is only an opinion. Adding a Linux ASan or UBSan CI job is a fair proposal, not a defect.

### X-TEST-STRATEGY-02: Reference pictures run on lavapipe on Linux only; no real-GPU or per-platform rendering test

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** test-strategy · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/AGENTS.md:1`

`reference` runs on Mesa software Vulkan and is part of `check` and `artifact` (the Linux container job). Windows (D3D/Vulkan via the driver), macOS (Metal via MoltenVK or SDL GPU) and real drivers are never drawn from. Shader compile differences, format support, present modes and uniform-layout/alignment bugs on other backends have no test, and the committed shader binaries are only checked for staleness.

**Impact:** A renderer bug that appears only on a real driver or another backend (for example std140 padding, sRGB swapchain, or a missing texture format) ships undetected. The renderer (1,751 lines) and the GPU feature classes have no unit test and only these pictures.

**Recommendation:** Add a smoke job on the macOS and Windows runners that starts the game (or reference_tests with a few cases) with SDL's offscreen or dummy-window GPU and checks that it draws a nonblank frame, or compares a coarse perceptual hash against a loose threshold. Record in a spec which backends are supported at 1.0.

**Verification:** Lavapipe-only references are accurate (tests/AGENTS.md lines 25 and 44, tools/AGENTS.md line 47). I did not find a recorded decision on real-GPU or per-platform rendering, so the gap is real. Windows and macOS CI do run the unit tests with ASan, and window tests use SDL's dummy driver. The 'renderer has no unit test' claim is overstated, because uploads and sprite_block helpers were extracted and tested. Medium is too high for a pre-1.0 game.

### X-TEST-STRATEGY-03: TOLERANCE=2 hides drift; a drift guard is a known gap

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** test-strategy · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `tests/reference/reference_tests.cpp:58`

Pictures pass within a per-channel tolerance of 2. The Gotchas record that moves within the tolerance went uncommitted, leaving pictures stale for several branches. A passing run only names them ("within tolerance but changed"). The known-deferred list notes the missing drift guard.

**Impact:** References silently become stale. The next `--update` then rewrites pictures the change did not touch, so a review cannot tell an intended change from an unintended one. Lavapipe is byte-deterministic per the docs, so the tolerance masks real differences rather than noise.

**Recommendation:** Since lavapipe is exact on a pinned Mesa, make tolerance 0 in the gate (or make the 'within tolerance but changed' state fail `check` with its own message), and keep tolerance only for a Mesa upgrade, with a documented `--update` step.

**Verification:** TOLERANCE=2 is at tests/reference/reference_tests.cpp:58. The 'known-deferred' status is outdated. tools/AGENTS.md line 47 and tests/AGENTS.md line 44 say a passing `reference` run now names pictures that differ by any byte ('within tolerance but changed'). So a drift report exists. It does not fail the gate, which is the remaining suggestion (tolerance 0, or fail on drift). sweep-up-backlog line 40 describes the earlier plan to print them.

### X-TEST-STRATEGY-04: app.cpp, main.cpp and the SDL input-to-command path have no tests

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** coverage · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/app.cpp:1`

app.cpp (907 lines) and main.cpp (241 lines) have no matching tests; tests/unit/app/game_tests.cpp has 3 cases. The unit tests exercise helpers extracted from them (cast, click, camera_control, hud), but event polling, focus and window-state handling, resize and minimise, device hot-plug, pause, scene switching, the frame loop's pacing and frame interpolation, and the shutdown ordering are not driven. A fake event source or injected SDL events would reach them.

**Impact:** Input bugs, such as known audit items 4 and 5 (aim at the cursor, held-stick pan ignores focus), are exactly the kind that live here. Bugs in startup or shutdown, in resource ownership across the Jobs threads, and in minimise or lost-focus cases would surface only to a player.

**Recommendation:** Introduce a headless App driver (SDL dummy driver, pushing SDL_Events through SDL_PushEvent) and test a few end-to-end flows: key press to cast command recorded in the replay, focus loss clears held input, window close exits cleanly, and a resize is reflected in the view.

**Verification:** src/app/app.cpp is 907 lines. There is no app_tests file for app.cpp, and game_tests.cpp is small. The finding says main.cpp is 241 lines, but src/app/main.cpp does not exist (tests/unit/app/main.cpp is the doctest entry). tests/AGENTS.md line 7 sets the policy of moving logic that is only reachable through SDL or main into src/ functions, and the existing cast, click, camera_control and hud helper tests follow it. A headless SDL event-driving test would be new work, not a policy violation. Audit items 4 and 5 are unverified here.

### X-TEST-STRATEGY-05: Source files with no direct test beyond reference or indirect use

- **Severity:** low · **Verdict:** unverifiable · **Category:** coverage · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/internal/features/sprites.cpp:1`

By the include/name mapping, these have no *_tests.cpp and no direct includes: data/yaml.cpp, core/assert.cpp, core/combat_rules.hpp, scenes/arena.cpp, arena_layout.cpp, cells.cpp, painted.cpp, stress.cpp (reached through scenes_tests and host tests), gfx/internal/features/{sprites,ribbons,line_ribbons,composite}.cpp, resources/{sprite_array,scene_target,shaders}.cpp, gfx/colour.cpp, and renderer.cpp (included by 2 test files). The GPU feature classes are covered by reference pictures only.

**Impact:** A logic bug in the sprite batching, instance-offset math or atlas array layout shows up only as a pixel diff, which names the picture and not the cause. The 'one command buffer per sprite upload' audit item and the quad-index-upload flag are in this class.

**Recommendation:** For the GPU-side CPU logic (batch splitting, offsets, upload bookkeeping), extract pure helpers as was done for uploads/ and sprite_block, and test them. For core/combat_rules.hpp and arena_layout, add direct tests of the numbers.

**Verification:** The per-file mapping is plausible, and the policy is to cover GPU-side logic with reference pictures (tests/AGENTS.md). I did not enumerate every listed file against its tests. A list of this kind needs a coverage run to confirm, which I can't do read-only. The recommendation to extract pure helpers matches existing practice.

### X-TEST-STRATEGY-06: Coverage is reported but never enforced or tracked

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** tooling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:2407`

`coverage` builds with gcov and gcovr on linux-gcc and prints a summary, but it is not in `check` or CI, has no floor, and no trend or per-directory file is kept. Coverage counts only src/ lines (no branch or function coverage options).

**Impact:** Untested regions (see the coverage list) have no objective measure, and a PR that removes tests or adds an untested file looks the same as any other.

**Recommendation:** Run coverage as a nightly job, keep the summary as an artifact, set a per-directory floor (core, game, data) and add branch coverage for core and game.

**Verification:** tools/project_n.py has a `coverage` command, and AGENTS.md says it is not part of `check`. I confirmed there is no CI job for it. I did not verify the missing branch coverage option. It is a tooling nicety, and the project policy (tests/AGENTS.md line 9) relies on small representative tests plus fuzzing.

### X-TEST-STRATEGY-07: Long-run and multi-hour determinism, save-growth and soak tests are skipped and not gates

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** test-strategy · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/game/replay_tests.cpp:649`

The hour-of-walk-tos comparison on 1 and 4 workers and 'measure the shipped world's saves over hours of play' are doctest::skip and 'never gates'. The gate runs 1-month steps on small and shipped worlds and hash comparisons for short runs only.

**Impact:** Problems that appear only after game-years of calendar steps stay invisible. These include accumulation drift, relationships growing without bound, unbounded save growth, id exhaustion, u32 or tick-wrap overflows (the 32-bit tick wrap is tested only for glows), memory growth and thread-count dependent divergence that needs many months to appear.

**Recommendation:** Run these on a nightly schedule in release (one preset, with a time budget), asserting hash equality across 1 and 4 workers and bounds on save size, relationship count and memory. Record the numbers as a trend. Add a test with a world clock near the tick wrap.

**Verification:** The sweep-up backlog line 14 (items 4a and 4b) records the long-run determinism cases (10,000 months, an hour of walk-tos, 1 vs 4 workers) as deliberately skipped by default, and relationships are bounded by a per-NPC cap (PR #42, 373,990 edges flat to 1000 h). So the 'relationships grow without bound' impact is refuted by that fix. The cases not running as gates is a documented choice, and a nightly job is a suggestion. The tick-wrap overflow point is unverified.

### X-TEST-STRATEGY-08: Fuzzing is bounded to linux-clang ASan, with no continuous or scheduled run

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** fuzzing · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/fuzz/yaml.cpp:1`

The seeds replay once per `check --sanitizers`. Real fuzzing (`fuzz <name> --time`) is by hand. There is no corpus growth, no OSS-Fuzz or ClusterFuzzLite, no structure-aware mutation for the world map or look formats, and no fuzz target for the archive, delta or save-file framing outside Host::Load, or for the command line and bindings. The checksum in front of the save reader means a coverage-guided fuzzer mostly bounces off it unless the target recomputes the checksum.

**Impact:** Parsers of untrusted bytes get seed coverage only. A repo this public that loads user data (saves, replays, mod YAML in future) gains from continuous fuzzing, and the known audit finding (a malformed replay reserving GBs) is the kind of issue a longer fuzz run finds.

**Recommendation:** Check that the save target fixes the checksum before calling Host::Load (or fuzzes the inner reader). Add a scheduled 10-minute fuzz job per target that uploads new corpus and crashes. Consider ClusterFuzzLite.

**Verification:** Fuzz is linux-clang ASan only, and seeds replay in check --sanitizers (design 0001 rule 24); no scheduled fuzz job exists. But the checksum worry is refuted: tests/fuzz/save.cpp header says it targets Host::Load 'behind the save file's checksum', so the fuzzer bypasses it. Continuous fuzzing is an unrecorded gap, not a bug.

### X-TEST-STRATEGY-09: Bench gate compares wall-clock against fixed budgets and is never run automatically

- **Severity:** low · **Verdict:** deliberate · **Category:** performance-test · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/bench/headless.cpp:86`

headless_bench fails over the phase-4 gates (combat tick 8 ms, month step 0.5 s) on 2 workers, using p99 or the slowest of wall-time samples. It is not registered as a test, not in check, and not in CI, and it covers the phase-4 stress arena only. `measure` is explicitly never a gate.

**Impact:** Performance regressions (the known ~53 ms main-thread hitch after a month step, the cost of the sprite upload path, culling at larger map sizes) are noticed late and by hand. Fixed ms budgets will also flake on a loaded runner if it is ever automated.

**Recommendation:** Add an instruction-count or allocation-count proxy (valgrind callgrind or a counting allocator) for deterministic regression detection of the hot paths, and run the wall-clock bench on a schedule on a dedicated runner, with trend storage.

**Verification:** tests/AGENTS.md lines 19 and 28 and tests/xmake.lua 109-111 state headless_bench is built with the tests but not registered, 'run on request, optimised'. Not in CI by design. The flakiness concern holds only if it is automated. The counting-allocator idea is a reasonable suggestion.

### X-TEST-STRATEGY-10: Scaling tests stop at the shipped 50,000-NPC map

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** scaling · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/support/shipped_world.hpp:1`

Maps are procedural per seed and 'may be very large'. The tests use the small test map and the shipped map (50,000 NPCs); no test, bench or fuzz case builds a world at 2x or 10x, or checks that map-derived data (roads, fog, rivers, name composition, label glyph preparation, saves) scales within a budget or memory limit. world_look and world_map readers cap sizes in fuzz, but no test checks the large-end behaviour.

**Impact:** Quadratic algorithms, 32-bit index limits, save size and load time on large seeds would surface only when someone generates a bigger world. The world-map generator is planned but untested at scale.

**Recommendation:** Add a skipped-by-default (nightly) scaling case that generates a map at 4x the shipped size and records build time, peak memory and save size, and test the index types at their limits with a small stand-in (for example a map with an artificially small index width).

**Verification:** I did not find a test at larger-than-shipped scale in what I read. Whether quadratic or 32-bit limits exist needs a real large-seed run. The finding is plausible, but the medium severity is too high for a speculative risk.
