# Review: core-infra

**Scope:** every file in src/core/ NOT in this list (those have their own reviewers): world_map, paths, road_curve, fog, world_look, leg_walk_cache, journey, volley, logic, state, frame, tick_clock, combat_rules, abilities, effects, seals, seal, glow, random, ecs, display_delay, delta, archive, save_file, saves, replay, handover. I.e. jobs, thread_safety, log, math, draw_list, instances, strings, names, ui_colours, manifest, game_data, settings and anything else (ls the directory). Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 25 standing (0 critical, 0 high, 1 medium, 19 low, 5 info), 0 raised but refuted on verification.

**Parts:** core-infra.md, core-infra-part2.md, core-infra-part3.md

## Assessment

## Scope: core infrastructure (src/core minus the files other reviewers own)

**Strengths**
- The low-level pieces are small, well documented and mostly correct. `PackHalf`/`UnpackHalf` round-trip on every half (checked exhaustively), ties go to even and subnormals are handled. The radix sort is stable, skips passes where every key has the same byte, and is tested against `std::stable_sort`. `QuantiseDepth` handles NaN and clamping on purpose. The sort-key bit layout has no overlaps.
- The job system is a careful wrapper over enkiTS:
  - one item per chunk, so results don't depend on how many workers run;
  - explicit atomic fork/join edges that ThreadSanitizer can see;
  - an assert that only allowed threads fork;
  - workers set the floating-point environment when they start;
  - a regression test for the `Pending::Done` race.
- The deterministic maths (Cody-Waite reduction, Taylor series cut off by an error bound, FTZ/DAZ control) is sound for "same binary, same results". Its edge cases (NaN, infinities, origin, range limits) are deliberate and documented.
- Clang thread-safety annotations, `RepeatedFailure`, `Error`/`Combine` and the `PN_ASSERT` release form `sizeof(!(cond))` are clean, idiomatic infrastructure.
- Doxygen discipline is excellent: almost every contract is written down, which made this review possible.

**Weaknesses**
- **Hidden scale limits.**
  - World coordinates are f32 (`Vec2`). Positions are advanced by adding a step each tick, while `MIN_WALK_SPEED` is justified by "walks of 100,000 tiles". Those two can't both hold: past about 1,000 tiles a minimum-speed walk stalls.
  - `WIDEST_HALF_VIEW` claims to be the widest view any window shows, and the terrain instance budget is tested against it. Windows narrower than 8:9 (tiled or portrait, which the user's niri setup makes likely) or wider than 32:9 show much more.
- **Determinism guard gaps.** The main thread and the workers run simulation chunks, but only the logic thread re-checks the floating-point environment. The check itself covers rounding and FTZ/DAZ only.
- **Diagnosability.** Logging is asynchronous, with no signal or terminate handler. Bounds-checked `.at()` throws (44 uses in core) despite the "we throw nothing" rule, so a bug inside a job chunk ends in `std::terminate` and loses the last queued log lines, usually the ones that explain the crash.
- **Tests.** The maths tests measure error relative to `max(1, |expected|)`. Every result below 1 (Exp of negatives, Sin near 0, Log near 1, small Atan2) is therefore only checked to an absolute 1e-15, and the "few ulps" claim is not tested where most values fall. Several suites are sweeps, which tests/AGENTS.md forbids. The draw-list tests never use more than one worker or more than one gather chunk.
- **Hand-kept counts.** Enum counts are kept by hand (`STRINGS`, `UI_COLOURS`, `HIT_KINDS`, ...) with no static_asserts, and fixed `std::array`s quietly accept too few initialisers.
- **Small amounts of wasted work** in hot publish paths:
  - the draw list copies instances twice;
  - `MakeInstance` makes two f64 `remainder` calls even for unrotated sprites;
  - `NameOf` adds up every surname weight on every call.
- **Shape of `core`.** It has become a 12.6k-line catch-all of infrastructure plus game-domain code, which neither `module.dox` nor the 0002 module table reflects.

**Overall health:** good for its current size, with no critical defects in this scope. The main risks are scale assumptions (f32 world space, the "widest view" constant) and determinism and crash-forensics guard rails, which will bite as procedural maps grow and as more third-party code (audio, UI, drivers) runs on threads that also run simulation chunks.

**Risks to watch:** f32 world space before large procedural maps land; the floating-point environment on non-logic threads; lost crash logs; the save, decode and glyph work all sharing one low priority on machines with few cores.

**Files read in full:** src/core/animations.hpp, src/core/animations.cpp, src/core/assert.hpp, src/core/assert.cpp, src/core/calendar.hpp, src/core/command_line.hpp, src/core/command_line.cpp, src/core/cxx23.hpp, src/core/draw_list.hpp, src/core/draw_list.cpp, src/core/error.hpp, src/core/game_data.hpp, src/core/hit.hpp, src/core/instances.hpp, src/core/instances.cpp, src/core/jobs.hpp, src/core/jobs.cpp, src/core/log.hpp, src/core/log.cpp, src/core/manifest.hpp, src/core/math.hpp, src/core/math.cpp, src/core/module.dox, src/core/names.hpp, src/core/names.cpp, src/core/profile.hpp, src/core/rect.hpp, src/core/repeated_failure.hpp, src/core/scene_reports.hpp, src/core/scene_reports.cpp, src/core/schedule.hpp, src/core/settings.hpp, src/core/stable_id.hpp, src/core/strings.hpp, src/core/strings.cpp, src/core/thread_safety.hpp, src/core/types.hpp, src/core/ui_colours.hpp, src/core/ui_colours.cpp, src/core/vec2.hpp, src/core/view_limits.hpp, src/core/generated/atlas.hpp (skimmed; generated), src/AGENTS.md, tests/unit/core/animations_tests.cpp, tests/unit/core/command_line_tests.cpp, tests/unit/core/cxx23_tests.cpp, tests/unit/core/draw_list_tests.cpp, tests/unit/core/error_tests.cpp, tests/unit/core/game_data_tests.cpp, tests/unit/core/instances_tests.cpp, tests/unit/core/jobs_tests.cpp, tests/unit/core/log_tests.cpp, tests/unit/core/math_tests.cpp, tests/unit/core/names_tests.cpp, tests/unit/core/rect_tests.cpp, tests/unit/core/repeated_failure_tests.cpp, tests/unit/core/scene_reports_tests.cpp, tests/unit/core/schedule_tests.cpp, tests/unit/core/strings_tests.cpp, tests/unit/core/ui_colours_tests.cpp, tests/unit/log_startup/early_tests.cpp, tests/unit/log_startup/fallback_tests.cpp, src/game/random_purposes.hpp

## Findings

### CORE-INFRA-01: Asynchronous logging with no crash/terminate handler: the lines explaining a crash are lost

- **Severity:** medium · **Verdict:** confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/log.cpp:39`

Start() uses `quill::Backend::start()` with no signal handler options and installs no std::set_terminate hook, and nothing else in src does (grep found no set_terminate, signal handlers or SetUnhandledExceptionFilter). Messages are formatted and written on a background thread, so on SIGSEGV, std::terminate (for example an uncaught std::out_of_range from one of core's 44 `.at()` calls inside an enkiTS job chunk, which nothing catches) or any abort other than PN_ASSERT's, every queued message is lost. Only AssertFailed flushes.

**Evidence:** log.cpp:39 `quill::Backend::start();`; assert.cpp:12 is the only flush before abort.

**Impact:** Crash reports from players and CI lose exactly the last log lines that explain the failure. The design leans on 'extensive logging' for diagnosis.

**Recommendation:** Start the backend with Quill's signal handler (`Backend::start_with_signal_handler` / SignalHandlerOptions, which flushes on SIGSEGV/SIGABRT/terminate and handles Windows exceptions), or install a std::set_terminate handler that logs Critical and calls flush_log. Add a log_startup-style test that the handler is installed.

**Verification:** log.cpp:39 calls plain quill::Backend::start(). grep of src found no set_terminate or signal handler. The only other flush is the PN_ASSERT path. RunLogic catches exceptions on the logic thread, so the uncaught-exception case applies only to the main thread and workers. The signal and terminate loss is real. Medium is appropriate. I did not check whether the project docs record this as a decision.

### CORE-INFRA-02: f32 world coordinates contradict MIN_WALK_SPEED's premise and cap procedural map size

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/game_data.hpp:24`

MIN_WALK_SPEED (0.01 units/tick) is justified by 'a walk of over 100,000 tiles ... still fits a leg's 32-bit arrive ticks', which assumes maps tens of millions of world units across. But every world position is a core::Vec2 of f32 (vec2.hpp:16-18), and held movement advances it by adding a step each tick (game/host.cpp:1460: `body.current = body.current + (move * walkSpeed)`). Past 2^17 = 131,072 units (512 tiles of 256) the f32 ulp is 0.0156, so a 0.01 step rounds up to 0.0156 (56% too fast). Past 262,144 units (1,024 tiles) it rounds to 0 and the walker stalls. A partial-stick step at walk_speed 4 (for example 0.4) stalls past 2^23 (about 32k tiles). QuantiseDepth also subtracts footY - viewTop in f32 (draw_list.cpp:31), so its sixteenth-unit depth steps are lost long before that.

**Evidence:** vec2.hpp: `f32 x = 0.0F; f32 y = 0.0F;`; game_data.hpp:21-24 comment about 100,000-tile walks; host.cpp:1460 accumulates per tick.

**Impact:** On a large procedural map, slow walkers stall or move at the wrong speed far from the origin, sprites jitter, and y-sort ties appear. The doc comment promises a scale the coordinate type cannot deliver. The shipped 20-screen map (about 38k units) is unaffected, which is why tests pass.

**Recommendation:** Decide the coordinate model before procedurally generated large maps land: tile-relative positions (tile index plus local f32 offset), f64 world positions in the simulation, or a documented maximum map extent asserted in WorldMap::Build. Fix the MIN_WALK_SPEED comment to state the real bound, and add a test that walks at MIN_WALK_SPEED at the far corner of the largest allowed map.

**Verification:** The comment at game_data.hpp:21-24 does promise a 100,000-tile walk, and Vec2 is f32, so the f32 ulp arithmetic is right. But the premise is thin: it is a latent limit on a procedural map that does not exist yet (the shipped map is about 38k units), and the comment is about leg arrive ticks, not position precision. Valid as a note to fix the comment or assert a max extent when procedural maps land. I did not open the host.cpp:1460 line or the legs code.

### CORE-INFRA-03: WIDEST_HALF_VIEW is not the widest view: narrow, portrait or >32:9 windows show more, and the terrain budget is tested against it

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/view_limits.hpp:25`

gfx::camera scales the world by min(px/1920, py/1080) × zoom, so REFERENCE_SIZE is fitted inside the window. WIDEST_HALF_VIEW = REFERENCE_SIZE × 4 = (7680, 4320) holds only for aspects between 8:9 and 32:9. Two counter-examples at zoom 0.25: a 640×1080 niri column (aspect 0.59) shows 12,960 units high (half 6,480 > 4,320), and a 48:9 triple-monitor window shows 23,040 across (half 11,520 > 7,680). The REFERENCE_SIZE comment ('at zoom 1 the window shows this much of the world on its shorter axis') is also wrong for any window narrower than 16:9: a 4:3 window shows 1440 on its shorter axis.

**Evidence:** gfx/camera.cpp:18 `std::min(pixelsX / REFERENCE_SIZE.x, pixelsY / REFERENCE_SIZE.y) * camera.zoom`; view_limits.hpp:22-25; host.cpp:1811 fallback cull box.

**Impact:** (1) Before the first ViewReport (the start, after each swap, and in harnesses) the host culls to a box smaller than what such windows show, so edge sprites are missing for those frames. (2) gfx::MAX_TERRAIN_INSTANCES is justified and tested ('the widest view of the shipped map stays within MAX_TERRAIN_INSTANCES', terrain_draw_tests.cpp:484) against WIDEST_HALF_VIEW, so narrow-tiled or portrait windows at zoom 0.25 can overflow the terrain buffer and drop stamps or paintings. The user's tiling WM makes narrow windows likely.

**Recommendation:** Either clamp the window aspect the camera honours (for example letterbox past 8:9 and 32:9) and say so, or derive the widest view from the real extreme aspects supported and size MAX_TERRAIN_INSTANCES from it. Fix the REFERENCE_SIZE doc to say the reference rectangle is fitted inside the window. Add a terrain-budget test case for a portrait window.

**Verification:** Facts hold. camera.cpp:18 fits the reference rectangle inside the window with min(px/1920, py/1080), and nothing in app/gfx/platform clamps the aspect (grep for aspect, 8:9, 32:9 found nothing). So a 0.59 aspect or a 48:9 window does show more than WIDEST_HALF_VIEW. view_limits.hpp:22 states the 8:9 to 32:9 range as the design bound, so the limit is documented, but it is not enforced. Impact is limited: the host cull box is only a fallback until the first ViewReport, and the terrain-budget overflow depends on how the instance buffer is sized, which I did not check. The REFERENCE_SIZE doc is probably wrong for narrow windows. I did not read it.

### CORE-INFRA-04: Simulation chunks run on the main thread and on workers, but only the logic thread's FP environment is ever re-checked

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** determinism · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/jobs.cpp:40`

The main thread is enkiTS thread 0, and its high-priority joins (gfx volley_draw/text_place ParallelFor) and Pending waits run the logic's simulation chunks (npcs.cpp:351, travel.cpp:418, host.cpp:1822). App sets its FP environment once in its constructor (app.cpp:265), and workers set theirs once at thread start (jobs.cpp:169). Only the logic thread asserts FloatingPointEnvironmentIsSet, after each tick (logic.cpp:35). The main thread calls into SDL and the GPU driver every frame, and workers run image decoding and glyph rasterisation libraries; if any of these sets FTZ/DAZ or changes rounding, simulation chunks run on that thread silently compute differently from chunks run elsewhere.

**Evidence:** app.cpp:263-265 comment: 'waiting on its low-priority work, it may run the logic's chunks'; logic.cpp:35 is the only runtime check.

**Impact:** A replay or save diverges depending on which thread happened to run a chunk. Nothing would show it except a replay hash mismatch long after the fact.

**Recommendation:** In ChunkTask::ExecuteRange, PN_ASSERT(math::FloatingPointEnvironmentIsSet()) (debug and releasedbg), or simply re-apply SetFloatingPointEnvironment at the start of each high-priority chunk task (one MXCSR/FPCR read and write) and after each low-priority task returns. Add a test where a low-priority task sets FTZ and a following ParallelFor checks every chunk.

**Verification:** The structure is as described. app.cpp:263-265 sets the main thread's FP environment once, jobs.cpp:169 sets it per worker at thread start, and logic.cpp:35 is the only runtime check. ChunkTask::ExecuteRange (jobs.cpp:40-47) has no check. But the scenario needs a library to change MXCSR or FPCR on the main thread or on a worker. That is speculative, and the reviewer rated it low confidence. A debug-only assert in ExecuteRange is a cheap hardening step. I would not call it a defect.

### CORE-INFRA-05: Maths accuracy tests measure absolute error below 1, so 'a few ulps' is untested for most results

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/core/math_tests.cpp:35`

WorstError divides by max(1, |expected|). For every expected value below 1 in magnitude the check is therefore absolute (< 8.9e-16): Exp over [-700, 0] (all < 1, down to 1e-304), Sin/Cos near zero crossings, Log near 1, Atan2 near the axes and Pow(1.7, negative). An Exp that returned 0 for every negative argument would pass. The Log range [1e-300, 1e6] with 20,001 linear samples has a step of 50, so it never samples subnormals or (0, 50) except at its first point. Subnormal inputs to Log and Exp results in the subnormal range are never tested.

**Evidence:** `worst = std::max(worst, std::abs(ours(value) - expected) / std::max(1.0, std::abs(expected)));`

**Impact:** math.hpp's promise 'within a few units in the last place' is unverified where most simulation values live (small angles, decays, probabilities). A regression in the series or reduction for small arguments would not be caught.

**Recommendation:** Measure relative error (|ours - expected| / |expected|, with a floor at the smallest normal) and use ~20 named samples per function as tests/AGENTS.md requires: tiny arguments, near reduction boundaries (π/4, ln2/2, tan(π/12)), subnormal inputs, values near 1 for Log, and large negative arguments for Exp. Assert the worst value once.

**Verification:** math_tests.cpp:30-34 does divide by max(1.0, |expected|), so results below 1 are checked in absolute terms. That weakens the 'a few ulps' claim for small outputs. A zero-returning Exp would not pass, because 1e-304 is within the absolute bound only if the test accepts it, and I did not check the other assertions in the file. Medium is too high. This weakens a test and does not break the code.

### CORE-INFRA-06: Unit tests in scope are sweeps, contrary to tests/AGENTS.md

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/core/math_tests.cpp:26`

tests/AGENTS.md: 'about 20 [samples] ... never a sweep over every item'; memory 'Small unit tests'. In scope: math_tests (SAMPLES = 20,001 per function, 7 sweeps), schedule_tests (6,000 entities × 6 ticks), jobs_tests (10,000-chunk loops; a 150,000-round race loop), instances_tests (all 65,536 halves), draw_list radix test (5,000 random entries).

**Evidence:** math_tests.cpp:26 `constexpr i32 SAMPLES = 20'001;`; jobs_tests.cpp:178 `ROUNDS = 150'000`; schedule_tests.cpp:41 `ENTITIES = 6'000`.

**Impact:** Slower suites and tests that are harder to read than the rule intends. Breadth belongs to fuzzing. Some of these (the exhaustive half round-trip, the race loop) have a stated reason and may deserve a recorded exception rather than removal.

**Recommendation:** Replace the maths sweeps with named samples (see the accuracy finding). Keep the exhaustive half check and the jobs race test only with a comment recording why they are exempt, or move them into a fuzz or bench target.

**Verification:** math_tests.cpp:26 does set SAMPLES = 20'001. That conflicts with the small-unit-tests rule in tests/AGENTS.md and the user's memory. I did not open the other cited files. The exhaustive half round-trip and the race loop may have a stated reason, as the reviewer says. Low is right.

### CORE-INFRA-07: Save writes, scene decodes and glyph batches share one low priority; a Pending wait can run a save on the main thread

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/jobs.cpp:93`

Background (saves, `TASK_PRIORITY_LOW`) and Start (scene decodes, text glyph batches, also `TASK_PRIORITY_LOW`) share one priority. With one worker (DefaultWorkers() on ≤3 hardware threads, or `--workers 1`), a save in progress holds up scene decoding, and the logic's bursts wait for SceneResident. ~Pending calls WaitforTask with the default lowest priority, so the main thread destroying a SceneLoad or a TextLayout batch can pick up and run a whole save write, a disk-bound frame hitch. Likewise WaitForBackground on the logic thread can run decodes. gfx/text_layout.cpp:419 claims a batch is 'never queued behind a background job such as a save, nor holding one up', which this design doesn't guarantee.

**Evidence:** jobs.cpp:62 and :93 both `m_Priority = enki::TASK_PRIORITY_LOW`; jobs.cpp:142 `WaitforTask(m_task.get())` with the default priority.

**Impact:** Occasional main-thread hitches and slower scene loads during saves on low-core machines. A comment elsewhere overclaims the scheduling guarantee.

**Recommendation:** Use enkiTS's three priorities: HIGH for ParallelFor, MED for Start (decodes, glyphs), LOW for Background. Pass a priority floor to WaitforTask in ~Pending (MED) and WaitForBackground so a wait never runs a save. Then fix or keep the text_layout comment.

**Verification:** jobs.cpp:62 and :93 both set TASK_PRIORITY_LOW. ~Pending (jobs.cpp:142) calls WaitforTask(m_task.get()) with the default priority. So on a single-worker setup a wait can pick up a queued background save. That matches the finding. The impact is a hitch on low-core machines only. I did not check the text_layout.cpp:419 comment.

### CORE-INFRA-08: Out-of-range bugs throw std::out_of_range through .at(), against 'our code throws nothing'

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/manifest.hpp:91`

Core uses `.at()` as its bounds check in 44 places (Manifest::Sprite/Scene, Strings::Text, UiColours::Of/Set, RadixSort's histogram, ...). src/AGENTS.md says a broken invariant is PN_ASSERT and 'our code throws nothing'. A bad index therefore throws, and inside an enkiTS chunk or on the main thread that is std::terminate, with no PN_ASSERT log or flush (see the logging finding). `.at()` stays on in release, unlike PN_ASSERT, and costs a branch per access in hot loops (RadixSort's 8 × N histogram increments, draw_list.cpp:46/56).

**Evidence:** manifest.hpp:91 `return sprites.at(std::to_underlying(sprite));`; strings.hpp:154; ui_colours.hpp:174; draw_list.cpp:46 `++counts.at(pass).at(Digit(...))`.

**Impact:** Out-of-range bugs crash without a log message. The policy is inconsistent, and hot paths pay a small cost.

**Recommendation:** Pick one policy. Either PN_ASSERT(index < size) plus operator[] (matching the rule), or document `.at()` as the release-mode bounds check and install a terminate handler that logs. In RadixSort use operator[] on the fixed-size arrays, whose index is masked to 255.

**Verification:** I did not open manifest.hpp or the other .at() sites, so I did not verify the count of 44 or the exact policy wording in src/AGENTS.md. A throwing .at() reaching the main thread or a worker would hit std::terminate with no log, which follows from finding 2. RunLogic's catch covers the logic thread. Most of the value here comes from finding 2's terminate handler. Low is right.

### CORE-INFRA-09: Enum counts are maintained by hand; fixed arrays silently accept too few initialisers

- **Severity:** low · **Verdict:** confirmed · **Category:** maintainability · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/strings.hpp:80`

STRINGS = 50, UI_COLOURS = 26, HIT_KINDS = 4, COLOUR_AREAS = 5, LANGUAGES = 2 and NUMBER_MOTIONS = 2 are literals with no static_assert tying them to the last enumerator. `std::array<T, N>{ { ... } }` with fewer than N initialisers compiles. A colour added to UiColour, with UI_COLOURS bumped but no DEFAULT_UI_COLOURS entry, defaults to 0 (transparent black), and ui_colours_tests' round-trip passes on 0 (ToSrgb gives alpha 0, FromSrgb(0,0,0,0) == 0). A StringId added without bumping STRINGS makes Strings::Text(new id) throw.

**Evidence:** strings.hpp:80 `inline constexpr std::size_t STRINGS = 50;`; ui_colours.hpp:63 and :138.

**Impact:** Silent invisible UI colours or a crash after a routine content addition.

**Recommendation:** Add `static_assert(STRINGS == std::to_underlying(StringId::DatePaused) + 1)` and the like next to each count (or a trailing Count enumerator). Build the tables with std::to_array so their size is deduced and compared to the count. This overlaps with the deferred data-driven sweep, which may replace these enums.

**Verification:** strings.hpp:80 STRINGS=50 and ui_colours.hpp:63 UI_COLOURS=26 are literals. A grep for static_assert in strings.hpp and ui_colours.hpp found none. std::array aggregate init with too few entries compiles and zero-fills. Too many entries fail to compile, so only the 'too few' direction is silent. strings_tests only checks text.size()==STRINGS, which would not catch a missing table entry.

### CORE-INFRA-10: NameOf sums every surname weight on every call, per drawn traveller per publish

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/names.cpp:67`

NameOf adds up all surname weights (about 100 entries in names.yaml, up to 1,024 allowed), then walks them linearly. It is called for every labelled traveller in every publish (game/host.cpp:1338), so the same constant total is recomputed thousands of times per publish with the whole map in view.

**Evidence:** `for (const NameEntry& entry : names.surnames) { total += entry.weight; }` inside NameOf.

**Impact:** Wasted work on the publish hot path (memory: 'Avoid wasted work').

**Recommendation:** Precompute cumulative weights once when the table is read (a `std::vector<u32> cumulative` in Names, filled by the data reader or lazily), then find the surname with std::upper_bound. Alternatively cache NpcName per journey member, since it is a pure function of seed and ID.

**Verification:** names.cpp:67-70 re-sums the surname weights on every call, and the sum is constant per table. Caching or precomputing is a valid win. I did not verify the host.cpp:1338 call frequency, but the loop cost is plain from the code. The impact is small, about 100 additions per traveller.
