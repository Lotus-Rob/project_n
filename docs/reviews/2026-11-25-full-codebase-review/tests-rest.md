# Review: tests-rest

**Scope:** tests/unit/{gfx,data,app,platform}/* and any other tests/unit subfolder besides game and core, tests/xmake.lua, tests/.clang-tidy, tests/bench/*, tests/fuzz/* (harness quality, corpus), tests/reference/ (what is covered, tolerance, flakiness risk), tests/AGENTS.md: TEST QUALITY review and gaps versus src.

**Findings:** 21 standing (0 critical, 0 high, 0 medium, 13 low, 8 info), 0 raised but refuted on verification.

**Parts:** tests-rest.md, tests-rest-part2.md, tests-rest-part3.md

## Assessment

## Assessment: tests-rest (gfx, data, app, platform, log_startup, xmake.lua, bench, fuzz, reference)

I read all of tests/AGENTS.md, tests/xmake.lua, tests/.clang-tidy, the three fuzz targets, the bench, and the first 330 lines of reference_tests.cpp. I read its case list and the constants of its measure cases. I also read the support headers (manifest, shipped_world, seals, the end of saves.hpp) and about a dozen unit files in full. The rest of the unit files I sampled, with grep sweeps for filesystem use, sleeps and loops across the whole scope. The big gfx files (labels, volley_draw, seal_draw, cloud_draw, terrain_draw, river_draw, stamp_draw, text_place, number_place and others) were skimmed, not read in full.

**Strengths**
- The structure is disciplined. There is one test binary per area. A test file runs as its own parallel xmake test, and a `rest` target catches any case outside them. The Windows-under-Wine runner is explicit and keeps its output.
- Most tests are small, name the behaviour they check, and use literal inputs. Many name a failure scenario in the title.
- Parser tests assert the line number in each error message, which is good for hand-edited data files.
- Resource and boundary tests are above average: seal and table caps, 1e-50 narrowing to an f32, a period of exactly one tick, CRLF in a grid, a minimised 0x0 target, a reload that must keep the running table.
- `save_file_tests` covers truncated files and a header that lies about sizes.
- Sources of flakiness are thought through. Every wait has a bound, each reference case runs in its own process, and the timing tests are loose on purpose.
- The reference suite is unusually thorough for a renderer. It has 53 pictures on software Vulkan, shimmer and jitter metrics, glow drive checks, and a "within tolerance but changed" report.
- The fuzz harnesses are safe. `Host::Load` is transactional, and the save target restarts from a known state after any input that loaded. The YAML target aborts when a refusal has an empty message.
- The AGENTS.md gotchas record real post-mortems: dangling references, the TSan limits, and the ASan container-overflow false positives.

**Weaknesses**
1. The reference comparison is far looser than the suite's own evidence supports. AGENTS.md says lavapipe output is byte-identical across runs, thread counts and compilers. The check still allows 92 pixels (`ALLOWED_PIXELS = 1280*720/10000`) to differ by more than 2 levels per channel (`TOLERANCE`). A single glyph, an 8x8 sprite or a small flash can regress and pass. The comment "a missing orb must fail" is not guaranteed by these numbers.
2. Real GPU back ends are never rendered in tests. Only SPIR-V on lavapipe is covered, on Linux. The D3D12 and Metal paths, and real driver behaviour (blend, sRGB, mips), have no automated picture. `renderer.cpp` (1751 lines) and `gfx/internal/features/*` (sprites, seals, meshes and others) have no unit tests, only the reference pictures.
3. Fixed temp-file and directory names collide. Several tests use names like `/tmp/project_n_image_tests.png`, `project_n_effects_tests_load`, `project_n_settings_tests_<n>` and `project_n_data_watch_tests_<n>`, and each starts with `remove_all` on that path. AGENTS.md runs parallel implementers in separate worktrees, so two concurrent `test` runs corrupt each other. This breaks the stated "contained" rule.
4. Fuzz coverage is thin and not run continuously.
   - One YAML target feeds the same bytes to eleven parsers, which dilutes coverage-guided mutation.
   - There is no dictionary, and `ReadSaveFile` (the zstd header and checksum layer) has no target, though it is the file-facing reader of untrusted bytes.
   - The replay target has a single 119-byte seed.
   - Nothing runs the fuzzers beyond replaying seeds in the sanitizer jobs.
   - The CLI parsers and the Archive reader are not fuzzed.
5. The benchmark has narrow scope. It runs only the stress arena on the combat tick and a month step. It does not measure procedural map generation at large sizes, route planning, save and load time, publish culling on big maps, or memory. These are the stated scaling risks. It is also never run in `check` or CI, so the 8 ms and 0.5 s budgets are not gates in practice.
6. Platform input coverage is incomplete. The window's Poll handles wheel events, quit, resize, focus gain and loss, hidden/occluded/exposed visibility, and gamepad add and remove. None of these is tested. Focus-loss handling matters for held-key state and for the known "stick pan ignores focus" bug.
7. Maintainability issues in the tests:
   - Several 500-1150-line test files and one 2254-line reference file.
   - Table-driven cases that bundle many behaviours under one name.
   - Repeated file-reading boilerplate.
   - `ArenaSave` takes 14 positional parameters and mirrors the Host save layout byte for byte.
   - Tautological tests: the 4 MiB mesh buffer, the present-mode order, and the "SDL3_ttf links HarfBuzz" test, which checks a third-party library.
   - Silent `std::abort()` in test helpers.

**Overall health:** good for the logic modules and very good for the renderer-by-picture approach. The risks are mostly "the net has holes where it matters": loose reference tolerance, no real-GPU or large-scale coverage, a thin fuzz corpus, and test isolation that breaks under the repo's own parallel-worktree workflow. No test I read is wrong in a way that hides a currently shipping bug, except that a few lock in known-deferred behaviour, listed below.

**Files read in full:** (not reported)

## Findings

### TESTS-REST-01: Reference compare allows 92 pixels of difference beyond tolerance, contradicting the byte-identical claim

- **Severity:** low (reviewer said high) · **Verdict:** partially-confirmed · **Category:** test-quality · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/reference/reference_tests.cpp:58`

TOLERANCE = 2 per channel and ALLOWED_PIXELS = 1280*720/10000 = 92 pixels may differ by more than that. CompareWithReference does `CHECK(differing <= ALLOWED_PIXELS)`. The comments and AGENTS.md say lavapipe draws every case the same to the byte at any thread count, from GCC and Clang.

**Impact:** A regression confined to a few dozen pixels passes: one glyph, a small orb or icon (an 8x8 sprite is 64 pixels), a wrong outline colour on one label, or a one-tile fog change. The comment 'a missing orb must fail' holds only for large orbs. The 'within tolerance but changed' report is informational, so drift accumulates until someone updates the pictures.

**Recommendation:** Since output is byte-exact on the pinned lavapipe, compare with tolerance 0 and allow 0 pixels. If a Mesa pin bump needs slack, make it an explicit per-case allowance. Otherwise make 'within tolerance but changed' a failure.

**Verification:** The constants are as stated (TOLERANCE 2, ALLOWED_PIXELS = 92). But the code comment says the slack is 'room for another CPU's rounding', and tests/AGENTS.md line 44 documents the consequence: a change within TOLERANCE passes, and a passing run now names the pictures as 'within tolerance but changed'. So the trade-off and the informational report are deliberate, though the 'byte-identical' claim and the tolerance do sit in tension. The 'one 8x8 sprite' impact holds only for sub-92-pixel changes, and the 'within tolerance but changed' report surfaces them. Tolerance 0 would be riskier across CI CPUs and Mesa bumps, which nobody has measured.

### TESTS-REST-02: Fixed temp paths shared between concurrent test runs

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** test-isolation · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/gfx/image_tests.cpp:35`

Several tests build paths from fixed names under temp_directory_path() and `remove_all`/`remove` them: image_tests (project_n_image_tests.png), data/effects_tests.cpp:250 (project_n_effects_tests_load), data/settings_tests.cpp:12 (project_n_settings_tests_<name>) and app/data_watch_tests.cpp:25 (project_n_data_watch_tests_<name>). None of the names is per-process or per-build.

**Impact:** AGENTS.md runs parallel implementers in separate worktrees, and `check` or CI runs presets on one machine. Two concurrent test runs delete each other's folders or files and fail spuriously. The failure is intermittent and hard to attribute. It also breaks the 'no shared state' rule in tests/AGENTS.md.

**Recommendation:** Put test files under the build tree's cwd (as sdl_log_tests does) or add the process ID and a random suffix. Better, give a shared support helper a unique scratch directory.

**Verification:** Fixed names under temp_directory_path() are real: image_tests.cpp:35 and effects_tests.cpp:250 use a constant name, and settings_tests.cpp:12 and data_watch_tests.cpp:25 use per-case names with no process ID. The core tests (saves, save_file, logic, replay) and shipped_world_tests use the same pattern, so the problem is wider than the finding says. Two worktrees running tests at once would collide, and it is intermittent. Impact is modest because collisions need the same suite running concurrently on one host.

### TESTS-REST-03: Fuzz YAML target feeds the same bytes to eleven parsers

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** fuzz-quality · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/fuzz/yaml.cpp:58`

LLVMFuzzerTestOneInput calls ParseGameData, ParseAbilities, ParseEffects, ParseSettings, ParseManifest, ParseStrings, ParseNames, ParseSeals, ParseAnimations, ParseWorldMap and ParseWorldLook on the same text in one target. There is no dictionary of YAML keys, and each seed is valid for only one parser.

**Impact:** Coverage feedback is a union over all parsers, so a mutation that helps one parser shows up as new coverage and is kept even if irrelevant to the others. Every execution pays for eleven parses. Deep structure past the top-level keys is reached slowly for each reader, which makes a bug in e.g. ParseWorldLook or ParseSeals less likely to be found than in a dedicated target.

**Recommendation:** Split into one target per parser, or switch on the first input byte. Add a `-dict` of the shipped key names. Keep the empty-message-refusal invariant.

**Verification:** yaml.cpp does call all the parsers on the same text in one target, and no dictionary exists. This is a design choice: AGENTS.md says one target per reader family, and the seeds cover each parser. The speed and coverage-dilution argument is plausible but unmeasured. A -dict would be a cheap, valid improvement, and splitting is optional.

### TESTS-REST-04: ReadSaveFile (header, zstd, checksum) is a reader of untrusted bytes with no fuzz target

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** fuzz-coverage · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/fuzz/save.cpp:1`

AGENTS.md says there is one fuzz target per reader of untrusted bytes. The save target starts at Host::Load, behind the checksum. core::ReadSaveFile (src/core/save_file.cpp) parses the header, trusts claimed sizes and decompresses with Zstandard, and takes a path, so it is not fuzzed at all. Only hand-written cases in save_file_tests cover it.

**Impact:** A header or frame the unit tests did not anticipate (a claimed huge decompressed size, a multi-frame stream, a checksum mismatch path, a zstd dictionary id) could hit an allocation or decode bug in the one layer every save and load goes through.

**Recommendation:** Add a target over a bytes-based variant of ReadSaveFile (factor out a span overload). Seed it with the real saves compressed, and a truncated one.

**Verification:** ReadSaveFile takes a path and is not fuzzed directly (src/core/save_file.cpp:99). It does bound the size (MAX_SAVE_BYTES, ZSTD_compressBound) and check the magic and version before decompressing. The 'claimed huge size' risk is therefore partly guarded: the claimed sizes are checked against MAX_SAVE_BYTES, though I did not read the rest of the function. AGENTS.md says the save target starts at Host::Load, so this is a true gap relative to the 'every reader of untrusted bytes' wording. Medium is too high, since the header parsing is small and unit-tested.

### TESTS-REST-05: Replay fuzz target has one 119-byte seed and nothing that proves it still parses

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** fuzz-quality · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `tests/fuzz/replay.cpp:25`

tests/fuzz/corpus/replay holds a single 119-byte file. The target only checks that StartPlaying plus Next never crash. Nothing asserts that the seed is accepted (unlike the save seeds, which a unit test checks load). AGENTS.md admits a stale seed 'only exercises the refusal'.

**Impact:** After a replay format change the seed can be silently refused, so the target fuzzes only the header refusal path, with no signal. The known-deferred GB-reservation issue (Audit 2) is exactly the kind of bug a richer corpus would hit.

**Recommendation:** Add a unit test that the replay seed is accepted, and seed more shapes (long walk-to tick runs, held-key runs, every action kind, the maximum count fields).

**Verification:** I did not check the seed count or size. The AGENTS.md text does admit that a stale seed 'only exercises the refusal', and the save seeds do get a load check in a unit test. Whether replay has an equivalent accepted-seed check I did not confirm. The suggestion of a richer corpus is reasonable and low priority.

### TESTS-REST-06: Real GPU back ends (D3D12, Metal, real Vulkan drivers) are never rendered by any test

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** coverage-gap · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/reference/reference_tests.cpp:1`

The 53 reference pictures are drawn only with SDL's offscreen driver on Mesa lavapipe (software Vulkan on Linux). src/gfx/renderer.cpp (1751 lines) and src/gfx/internal/features/* (sprites, seals, meshes, ribbons, ground, composite) have no unit tests. The shaders are compiled for several back ends, but only the SPIR-V path is exercised.

**Impact:** A DXIL or MSL miscompile, a sampler, mip or blend difference on real GPUs, or a driver-specific present-mode bug ships unseen. The accessibility goals (reduce flashing) and the shimmer guards are validated only on one rasteriser.

**Recommendation:** Add a smoke render with a few hash or tolerance-bound pictures on the Windows and macOS runners (even a handful of cases), or a documented manual look-at-the-game checklist per release.

**Verification:** The reference pictures run on software Vulkan only (AGENTS.md: Linux, lavapipe, part of check and artifact), and no D3D12 or Metal rendering is tested. That is true by construction. It is a known limit of CI hardware, not a defect in a test. Severity is low-to-medium as a coverage gap. I did not check the line counts quoted.

### TESTS-REST-07: Platform window tests leave the event paths for quit, resize, focus, visibility, wheel and gamepad hot-plug untested

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** coverage-gap · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/platform/window_tests.cpp:1`

src/platform/window.cpp's Poll handles SDL_EVENT_QUIT, WINDOW_RESIZED, FOCUS_GAINED and FOCUS_LOST, HIDDEN, OCCLUDED, SHOWN and EXPOSED, ENTER_FULLSCREEN, MOUSE_WHEEL and GAMEPAD_ADDED and REMOVED. The tests cover only keys, mouse buttons, pad axes and buttons, minimise and restore, and display change. A test named 'the camera keys I, J, K, L and C' also asserts P, T, Enter, Escape and F5.

**Impact:** Regressions in quit handling, focus state (which drives pause and the drag-ends-on-focus-loss behaviour) or the hidden-window wait path go unnoticed. The hidden-wait path (SDL_WaitEventTimeout with hiddenWaitNs/SDL_NS_PER_MS cast to i32) has no test and is a potential overflow for large waits.

**Recommendation:** Add cases for each event type with the dummy driver, including WaitEventTimeout when not visible. Rename the camera-keys case to match what it checks.

**Verification:** Test cases cover keys, mouse buttons, axes, buttons, minimise and display change. The grep found no test for SDL_EVENT_QUIT, focus lost or gained, or mouse wheel, although window.cpp handles them (lines 167, 321, 324, 356). The coverage gap is real. The overflow claim is wrong: the only caller, app.cpp:314, passes core::TICK_NS, a small constant, so the i32 cast cannot overflow in practice. The case-name mismatch at line 230 is a valid nit.

### TESTS-REST-08: Headless benchmark misses the scaling paths and is never a gate

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** benchmark-scope · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/bench/headless.cpp:1`

The bench ticks only the stress arena for 600 ticks and runs 12 month steps. It does not measure procedural map generation at large sizes, route or journey planning on large maps, publish and culling on big maps, save or load time and size, or memory. The budgets (8 ms combat tick, 0.5 s month step) are wall-clock p99 and the slowest month on whatever machine runs it. It is not in `check` or CI.

**Impact:** The stated risk (very large procedural maps) has no regression signal, and the existing budgets are not enforced at any gate. A regression is found only when someone runs the bench or `measure`.

**Recommendation:** Add world-generation and route-planning cases at several map sizes with a size-scaling assertion (a time ratio or complexity guard, not an absolute ms). Add a save/load size and time case. Run it in a scheduled CI job.

**Verification:** Scope facts hold: tests/AGENTS.md:19 says the bench ticks the stress arena and month-steps world NPCs, and it is 'run on request', not in check/CI. But that is documented as deliberate (timing belongs to profiling, tests/AGENTS.md:9; `measure` exists and is never a gate). Absence of generation, route, and save benchmarks is a real gap, but the recommended scheduled CI job is a new proposal, not a defect.

### TESTS-REST-09: Silent std::abort() in shipped-data test helpers

- **Severity:** low · **Verdict:** confirmed · **Category:** diagnostics · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/support/manifest.hpp:22`

TestManifest(), ShippedWorld() and ShippedSeals() call std::abort() with no message when assets are missing or unreadable. The comment says 'one stop says it once' but nothing is printed.

**Impact:** A missing or broken assets copy (a packaging, LFS or rule problem) shows as a bare abort or SIGABRT from a test binary and the runner, with the cause in no output. Under Wine, or in fuzz targets, that is hard to diagnose.

**Recommendation:** Print the core::Error message to stderr before aborting (fputs or the log).

**Verification:** tests/support/manifest.hpp:20-22 calls std::abort() with only a comment and prints nothing. The other helpers cited were not opened, but the pattern is clear.

### TESTS-REST-10: Timing-dependent race tests in text_layout_tests are probabilistic

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** flakiness · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/unit/gfx/text_layout_tests.cpp:295`

'a glyph lands while another background job holds a worker' spins a yield loop up to PATIENCE and asserts the glyph landed first. 'a cache destroyed while the worker rasterises for it' relies on nine glyphs taking 'a few milliseconds each' so the destruction races the worker.

**Impact:** On a 1-2 core CI runner or under TSan or ASan the spin loop competes with the worker (the first test could fail on `landedFirst`). The second passes whether or not the race occurs, so it protects nothing deterministically.

**Recommendation:** Use a latch or condition variable handshake to force the interleaving (a hook in the rasteriser, or a test seam), not sleep and spin timing.

**Verification:** The first test (text_layout_tests.cpp ~297) spins up to PATIENCE and asserts !gaveUp. The test does Flush, which waits for the worker, so a flaky failure is only possible if the 2-worker pool starves the glyph job, which is unlikely. The second test (~320) does depend on timing, and the comment says 'a few milliseconds each'. It passes either way, so it is only a probabilistic guard, not a deterministic one. Severity is low.
