# Review: x-concurrency

**Scope:** CROSS-CUTTING over all of src/: threads, jobs, atomics, locks, data races, lifetime across threads, the sim/render handover, shutdown ordering, the thread-safety annotations and whether they are actually enforced.

**Findings:** 20 standing (0 critical, 0 high, 0 medium, 16 low, 4 info), 0 raised but refuted on verification.

**Parts:** x-concurrency.md, x-concurrency-part2.md

## Assessment

## Concurrency in src/: assessment

**Overall:** the threading model is sound for normal use. I found no data race in the steady-state paths I traced: handover, publish, month step, travel send-out, walk-table cache, text batch and scene decodes. The weak spots are at the edges: exceptions, cross-thread scheduling, things only a comment guarantees, and tests and tooling that rarely run under TSan.

### Strengths
- **Small, explicit model.** Two game threads (main and logic) plus one enkiTS pool. The handover is one mutex, held only to swap pointers and vectors, with five rotating `State` buffers whose capacity is kept. Ownership is clear: `Writing()` belongs to the logic thread, `Display` and `m_drained` to the main thread, and the shared channels are guarded.
- **Enforced annotations.** `core::Mutex`, `SharedMutex` and `SharedLock` carry Clang thread-safety attributes, including negative capabilities. The linux-clang preset builds with `-Weverything -Werror`. The pinned libc++ marks `std::scoped_lock` as scoped-lockable unconditionally (`__config:976`), so the guarded members in `Handover`, `LegWalkCache` and `Fonts` really are compiler-checked.
- **Determinism by construction.** Chunks have a fixed size and one enkiTS item each. Per-chunk outputs are merged in chunk order. Random draws are keyed by seed, counter, ID and purpose. Workers set the floating-point environment in `threadStart`, and tests check results are identical across worker counts and that every chunk runs in the right environment.
- **TSan edges.** There is an explicit fork/join acquire-release pair because enkiTS isn't instrumented. The `Pending::Done()` early-completion race was found and fixed, with a regression test.
- **Shutdown order is right.** The `jthread` is the last member, `~App` joins it, then the renderer's `Pending`s wait, then the window (`SDL_Quit`), then `~Jobs` (`WaitforAllAndShutdown`). `Game::~Game` waits for a save still being written.

### Weaknesses and risks
1. **Exceptions are not job-safe.** A throw in a chunk calls `std::terminate` on a worker, with no log line. On the forking thread it unwinds through enkiTS and destroys the stack `ChunkTask` while workers still run its ranges (use-after-free). `RunLogic`'s promise that an escaping exception is "logged and stops the game" doesn't hold for anything inside a `ParallelFor`.
2. **Shared high-priority pool.** The main thread and the logic thread run each other's high-priority chunks whenever they wait in a join, so frame time is held hostage by month-step and travel chunks, and the reverse. The design (0002 §7.5, 0002.3.1 §1) only reasons about decodes and `Pending` waits. Waits on the background job and on `Pending`s run every priority, so the logic thread can also pick up image decodes, which contradicts the §7.5 guarantee.
3. **Invariants held only by comment.** Several rules have no checks:
   - `Paths` must be warmed before the fork.
   - Each chunk writes only its own `m_keptWalks` and draw-list part.
   - `LegWalkCache::Clear` runs only when no one holds a table.
   - A thread has one `Jobs` at a time.
   - `Background` is always called from the same thread.
   - The floating-point environment on the main thread is never re-checked.

   The Clang annotations cover only three classes, and TSan runs only on request (`check --sanitizers`), never in CI.
4. **Startup and shutdown.** The logic thread copies `App::m_options` while the main thread may be writing it (a small race). Quitting during world generation or a month step's `Paths::Warm`/send-out blocks the main thread in `join()` for as long as that takes, with no event pumping. That scales with map size.
5. **Docs drift.** Several APIs say "on the thread that created `jobs`", but the logic thread is a registered thread, not the creator. `saves.hpp` claims the logic thread never waits on the disk.

### Overall health
Good, for a codebase at this stage. Nothing in the findings blocks the goals. The two items most worth fixing soon:
- making job chunks exception-safe, which is cheap and removes a whole class of crash and use-after-free;
- separating render and simulation priorities (or at least measuring the stealing), since the world-map scale target makes month-step chunks heavy.

A nightly TSan job would turn most of the comment-only invariants into checked ones.

**Files read in full:** src/core/jobs.hpp, src/core/jobs.cpp, src/core/thread_safety.hpp, src/core/handover.hpp, src/core/logic.hpp, src/core/logic.cpp, src/core/leg_walk_cache.hpp, src/core/leg_walk_cache.cpp, src/core/draw_list.hpp, src/core/draw_list.cpp, src/core/saves.hpp, src/core/saves.cpp, src/core/log.cpp, src/app/game.hpp, src/app/game.cpp, src/platform/sdl_log.cpp, tests/unit/core/jobs_tests.cpp

## Findings

### X-CONCURRENCY-01: Exceptions thrown inside job chunks terminate workers silently or leave enkiTS holding a destroyed stack task (use-after-free)

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** exception-safety / lifetime · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/jobs.cpp:222`

`ChunkTask::ExecuteRange` (line 40), `Pending::Task::ExecuteRange` (line 77) and `BackgroundTask::ExecuteRange` (line 124) call user bodies without any try/catch. enkiTS has no exception handling. If a body throws on a worker thread, `std::terminate` is called. If it throws on the forking thread, which runs chunks inside `WaitforTask` in `Jobs::Run`, the exception unwinds out of enkiTS and destroys the stack-allocated `ChunkTask task` while other workers are still running its ranges, or still have it in their pipes. That task's running count never reaches zero.

**Evidence:** jobs.cpp:40-47 `for (...) { m_call(m_callable, chunk); }` with no catch; jobs.cpp:222-233 `ChunkTask task{...}; ... WaitforTask(&task, HIGH);`, a stack task; app.cpp:281 `// An exception that escapes is logged and stops the game.`

**Impact:** Chunk bodies can throw: `std::bad_alloc` from `push_back` in `DrawList::Add`, labels, `met`, journeys and visits; `std::out_of_range` from `.at()` (for example `Paths::To`'s `m_maps.at(profile).at(city)`, called from `SetOut` chunks); `std::format`. On a worker the process dies with no log line, and Quill's queue is not flushed. On the logic thread, `App::RunLogic`'s catch (app.cpp:281) logs 'logic thread failed' and lets the main thread return 1, but workers are by then touching freed stack memory, and `~Jobs`'s `WaitforAllAndShutdown` may hang or crash. The documented guarantee ('An exception that escapes is logged and stops the game') does not hold for any `ParallelFor`.

**Recommendation:** Make every `ExecuteRange` noexcept-safe: wrap each chunk in try/catch, store the first `std::exception_ptr` in the task (an atomic flag plus a slot), skip the remaining chunks once one has failed, and rethrow on the forking thread after `WaitforTask` in `Jobs::Run`. For `Pending` and `Background`, store the exception and rethrow or log it on `Done()` or the wait. Add a test where a chunk throws on many workers and the exception reaches the caller.

**Verification:** jobs.cpp ExecuteRange (ChunkTask, BackgroundTask) has no try/catch, and Run keeps a stack ChunkTask, as cited. The mechanism is real. A throw on a worker means std::terminate. A throw on the forking thread unwinds past WaitforTask and leaves workers on a destroyed stack task. The doc comment at app.cpp:281 only covers the logic thread's own code. Impact is lower than stated: the throws listed (bad_alloc, .at()) are effectively fatal anyway, so only the missing log line differs. Rated low-medium.

### X-CONCURRENCY-02: Main and logic threads steal each other's high-priority chunks: frame time is hostage to month-step and send-out chunks

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** scheduling / latency · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/jobs.cpp:230`

Every `ParallelFor` runs at `TASK_PRIORITY_HIGH` and waits with `WaitforTask(&task, TASK_PRIORITY_HIGH)`. Both the main thread (enkiTS thread 0) and the logic thread (registered thread 1) fork into the same pool. The main thread forks for volley drawing (volley_draw.cpp:354), number placement (text_place.cpp:305/316), and the draw list when the logic publishes. While waiting on its join, enkiTS lets a thread take any high-priority task from any pipe. So the main thread, waiting on a 2-chunk sword or number fork, can take a `MonthStep` chunk (1,024 NPCs), a `Travel::SetOut` chunk (1,024 NPCs, each weighing every city twice, then `RouteDown` and `Visit`) or a publish chunk (4,096 entities or journeys), and can't return until it finishes. The reverse also happens: a logic thread waiting on its publish join runs render chunks. The design (0002 §7.5; 0002.3.1 §1) only reasons about low-priority decodes and about `Pending` waits running logic chunks ('harmless' for determinism). It never considers the main thread's own high-priority forks, which arrived later.

**Evidence:** jobs.cpp:30 `m_Priority = enki::TASK_PRIORITY_HIGH;`, jobs.cpp:230 `WaitforTask(&task, enki::TASK_PRIORITY_HIGH)`; app.cpp:263 comment mentions only 'waiting on its low-priority work'; 0002-engine-architecture.md §7.5 'Priorities' paragraph.

**Impact:** Main-thread frame hitches of a few ms per stolen chunk during month steps and big publishes on large maps, beyond a 165 Hz frame budget (the dev PC has a 165 Hz monitor). A plausible contributor to the recorded '~53 ms main-thread hitch after the month step' (known-deferred, attributed elsewhere). Logic ticks can likewise be delayed by render chunks. Determinism is not affected.

**Recommendation:** Measure first: in a `profile` run across a month step, check whether `ExecuteRange` zones of `SetOut`, `LiveMonth` or publish chunks appear on the main thread. If they do, give render forks their own, higher priority than the simulation: render HIGH, simulation MEDIUM (enkiTS's `ENKITS_TASK_PRIORITIES_NUM` defaults to 3), decodes and saves LOW. The main thread's joins then wait at HIGH and never run simulation chunks. The logic thread waiting at MEDIUM may still run small render chunks, which is cheap. Update 0002 §7.5 and the comment at app.cpp:263.

**Verification:** Run waits with TASK_PRIORITY_HIGH and ChunkTask is HIGH (jobs.cpp:30, 230), so a main-thread HIGH join can pick up logic HIGH chunks. Docs (0002 §7.5 line 278, 0002.3.1 line 30) reason only about the logic thread and decodes, so this case is unaddressed. Stealing during a join is bounded by chunk size, and the claim is explicitly unmeasured (the finding itself says measure first). The hitch attribution is speculative.

### X-CONCURRENCY-03: Waits for background and Pending jobs run work of any priority: the logic thread can decode images or rasterise glyphs, and the main thread can write a save

- **Severity:** low · **Verdict:** confirmed · **Category:** scheduling / latency · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/jobs.cpp:213`

`WaitForBackground` (jobs.cpp:217) and `Pending::~Pending` (jobs.cpp:142) call `WaitforTask(task)` with enkiTS's default `priorityOfLowestToRun` (the lowest priority), so they run any task, low-priority ones included. The logic thread calls `WaitForBackground` from `Saves::Write` (whenever a previous save or replay write is still running), `Saves::Read` (Load, PlayReplay) and `Game::~Game`. While waiting it can pick up the renderer's scene decodes (one sprite plus its distance field per chunk) or the text batch `Fonts::RasteriseRequests`, which loops until the queue is empty while the main thread adds glyphs every frame. Conversely, the main thread in `TextLayout::Flush` or a `Pending` destructor can run the save-writing background task (compression and disk I/O).

**Evidence:** jobs.cpp:142 `m_jobs->m_impl->scheduler.WaitforTask(m_task.get());` and jobs.cpp:217 `WaitforTask(m_impl->background.get());` (enkiTS signature: `priorityOfLowestToRun_ = TaskPriority(TASK_PRIORITY_NUM - 1)`); text_layout.cpp:171-185 loop until `requests.empty()`.

**Impact:** This contradicts 0002 §7.5 ('a waiting logic thread never picks up an 80 ms image decode'). A quick save issued while the previous save or a replay is still writing, during scene loading or heavy label churn, can stall the logic thread for many ms or for an unbounded time with the text batch, making ticks late. The main thread can absorb a save's disk write during a capture.

**Recommendation:** In `WaitForBackground`, wait with `WaitforTask(background, enki::TASK_PRIORITY_HIGH)`: a worker runs the low-priority write and the waiter helps only with high-priority work. Consider the same for `Pending::~Pending` on the main thread, or document the choice. Note in jobs.hpp which waits help with which priorities.

**Verification:** WaitForBackground and ~Pending call WaitforTask(task) with no priority argument (jobs.cpp:142, 217). enkiTS's default is the lowest priority, so low-priority work can be run. This contradicts the 0002 §7.5 sentence that a waiting logic thread never picks up a decode. 0002.3.1 line 30 documents only the Pending-waits-run-logic-chunks direction. Impact is limited to occasional stalls.

### X-CONCURRENCY-04: Floating-point environment never re-checked on the main thread or on workers, though both run simulation chunks

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** determinism · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/jobs.cpp:40`

The environment is set once per thread: in the workers' `threadStart`, in the `App` constructor on the main thread (after `Renderer::Create`), and in `RunLogic`. It is asserted only on the logic thread after each tick (logic.cpp:35). The main thread then makes SDL, GPU-driver and swapchain calls every frame, and workers run `DecodeSprite` (libpng via SDL, distance fields). Simulation chunks (`MonthStep` `LiveMonth` float needs and power, `SetOut`'s `ProfileFor(power)` comparisons, publish chunks) can run on any of these threads (see the stealing finding).

**Evidence:** jobs.cpp:169 `threadStart = [](u32) { math::SetFloatingPointEnvironment(); }`; app.cpp:265 set once; logic.cpp:35 asserts on the logic thread only.

**Impact:** If any library or driver changes the rounding mode or FTZ/DAZ on the main thread or a worker, chunk results depend on which thread ran them. A month step then differs from run to run even within one binary, so a replay diverges or a hash check fails with no obvious cause. That breaks the 'replays deterministic within one binary' guarantee. The probability is low on current drivers, but nothing would catch it.

**Recommendation:** Add `PN_ASSERT(math::FloatingPointEnvironmentIsSet(), ...)` at the start of `ChunkTask::ExecuteRange` (debug and releasedbg), or cheaply call `SetFloatingPointEnvironment()` at the start of each high-priority range. Also re-assert on the main thread once per frame after `Present`.

**Verification:** The environment is set once per thread: app.cpp:265 on main, the worker threadStart, and RunLogic. It is asserted only on the logic thread. No per-chunk check exists. The risk is hypothetical (no known library changes the rounding mode), and the finding itself rates it low confidence. Adding an assert is cheap. Not a demonstrated bug.

### X-CONCURRENCY-05: TSan never runs automatically: the partitioned-chunk invariants are checked only by comments and on-request sanitizer runs

- **Severity:** low (reviewer said medium) · **Verdict:** deliberate · **Category:** testing / tooling · **Status:** contradicts-recorded-decision · **Reviewer confidence:** high
- **Where:** `tests/unit/core/handover_tests.cpp:368`

Race freedom rests on several invariants: each publish chunk writes only its own draw-list part, `m_travellerLabels[chunk]` and `m_keptWalks[index]`; `Paths` is warmed before `SetOut`; `MonthStep` writes only its own NPCs; `LegWalkCache` is shared under a lock; `Fonts` is shared between the main thread and a worker. Clang annotations cover only `Handover`, `LegWalkCache` and `Fonts`; everything else is by comment. ThreadSanitizer runs only in `check --sanitizers` (on request). CI runs ASan only (ci.yml:252/327). The stress test says 'Run under ThreadSanitizer by check --sanitizers', which in practice means almost never.

**Evidence:** xmake.lua:9 '-- Sanitizers only on request ... never in the default gate.'; .github/workflows/ci.yml only `--sanitizer address`.

**Impact:** A future change that breaks one of these invariants (for example, a chunk calling `registry.storage<T>()` for the first time, or `Paths::To` on a cold profile) is a silent data race that the gate won't catch. It would surface as rare non-determinism or crashes on players' machines.

**Recommendation:** Add a scheduled (nightly or weekly) CI job running the `core`, `game` and `gfx` unit suites built with `--sanitizer thread` on linux-clang, or run TSan in `check` on a small tagged subset (`jobs`, `handover`, `leg_walk_cache`, `npcs`, `travel`, `replay`, `save`). This reopens the 'sanitizers only on request' decision (xmake.lua:9) for TSan alone, because unlike ASan nothing else covers races.

**Verification:** Design 0001 §7.1 rule 17 and AGENTS.md line 25 record 'sanitizers on request, never part of the gate'. CI runs only --sanitizer address (ci.yml:252, 327). The gap is real, but it is a recorded decision. The finding itself says it contradicts that decision, so it is a proposal to reopen it, not a defect.

### X-CONCURRENCY-06: Data race on App::m_options at logic-thread start

- **Severity:** low · **Verdict:** confirmed · **Category:** data-race · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/app.cpp:285`

`RunLogic` builds `Game` by reading `m_options.manifest`, `.scene`, `.data` (copying a `GameData` with vectors and shared_ptrs), `.saveSlot` and `.replaySlot` on the logic thread at an arbitrary time after the thread starts. Meanwhile the main thread is already in `App::Run`. Its first `WatchData` call runs on the first frame (`m_dataCheckNs` starts at 0) and assigns `m_options.data = *gameData` (app.cpp:493) whenever the data folder changed after the `App` constructor took its listing.

**Evidence:** app.cpp:286-295 reads `m_options.*` inside `RunLogic`; app.cpp:493 `m_options.data = *gameData;` in `WatchData` on the main thread.

**Impact:** A data edit (or a build or tool touching `assets/data`) between `App` construction and the logic thread's copy is a C++ data race: undefined behaviour, a torn `GameData` copy, possibly a crash at startup. The window is narrow but real in development builds with hot reload.

**Recommendation:** Build the `GameSetup` in the `App` constructor on the main thread and move it into the `jthread` lambda capture, for example `m_logic = std::jthread{ [this, setup = MakeSetup()](std::stop_token stop) mutable { RunLogic(stop, std::move(setup)); } }`, so the logic thread never reads `m_options`.

**Verification:** The logic thread copies m_options.data, manifest and the slots at app.cpp:285-295, at an arbitrary time after the jthread starts in the constructor (line 272). The main thread's WatchData (line 328, first frame) assigns m_options.data at line 493 when the data folder changed (HOT_RELOAD only). That is an unsynchronised read and write. The window is narrow and applies to dev builds only. Moving the setup into the lambda capture is an easy fix.

### X-CONCURRENCY-07: Paths::To lazily builds distance maps and is shared across SetOut chunks; safety depends on an unchecked warm-up

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** data-race / API design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/travel.cpp:397`

`core::Paths::To(city, profile)` is non-const and assigns `std::optional<DistanceMap>` when the slot is cold (paths.cpp:233-236). `SendOut` calls `Warm(WALKING)` and `Warm` for each band's profile before the fork. The chunks then call `paths.To(...)` from many threads via `ChooseDestination` (a public function taking `core::Paths&`) and `month.paths->To(*destination, profile)`. This is safe only while every profile `ProfileFor` can return is warmed. No assertion or type guarantees it, and the class comment says 'Not thread-safe: Warm before any parallel use'.

**Evidence:** paths.cpp:230-238 `if (!slot) { slot = DistanceMap::Build(...); }`; travel.cpp:113 `core::Paths* paths` in VisitMonth; travel.cpp:236 `core::Paths& paths` in ChooseDestination.

**Impact:** A future change could break it: a return trip by another profile, a profile chosen from something other than the bands, or `ChooseDestination` reused elsewhere in a chunk. Two chunks would then build or assign the same optional concurrently: a data race (UB) and possibly a torn `DistanceMap`, so the month step becomes non-deterministic and may crash.

**Recommendation:** Give the parallel path a const accessor, for example `const DistanceMap& Paths::Warmed(u16 city, ProfileIndex) const`, which asserts the slot is built. Take `const core::Paths&` in `ChooseDestination` and `VisitMonth`. Keep `To()` for the single-threaded build-up.

**Verification:** Paths::To is non-const and lazy, and Warm is a separate step (paths.cpp:230-240). The class comment itself says 'not thread-safe: Warm first'. SendOut warms before forking. No const or asserted accessor exists, so a future change could break the invariant. The finding gives no present bug, only a hardening suggestion. The cited line (travel.cpp:397) was not individually confirmed.

### X-CONCURRENCY-08: Shutdown blocks the main thread's join for the length of world generation or an uninterruptible month-step phase

- **Severity:** low · **Verdict:** confirmed · **Category:** shutdown / scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1571`

`stop` is checked only before `Travel::Month` and inside `MonthStep` chunks. `Travel::Month` itself (`Paths::Warm` for every city and band profile, then `SetOut` over every NPC) never checks it, and neither does the `Host` constructor's `GenerateWorld`/`Travel::Seed`, which runs on the logic thread when `Game` is built and sends out two months serially with no job system. `App::~App` calls `m_logic.join()` on the main thread, which then pumps no window events.

**Evidence:** host.cpp:1571 `if (... && !stop.stop_requested()) { world.travel.Finish(...); world.travel.Month(...); }`; travel.cpp:397-420 no stop; app.cpp:275-278 `request_stop(); join();`.

**Impact:** On large procedural maps, quitting during startup generation or a month step leaves the window frozen ('not responding') until generation or warm-up finishes. That time grows with cities × tiles × profiles.

**Recommendation:** Pass `stop` into `Travel::Month`, `Paths::Warm` and `SendOut` (check it per city in Warm and per chunk in SetOut), and give the `Host` constructor's generation a stop token. Alternatively, keep pumping events (or hide the window) while joining.

**Verification:** travel.cpp has no stop_token use (my grep for 'stop' found only a comment about a NaN stopping at a band). The Host constructor's generation and Travel::Month cannot be interrupted. App::~App joins on the main thread with no event pumping. This is a latency matter that grows with map size, and the maps are currently small.

### X-CONCURRENCY-09: Travel::Seed (world generation) runs its chunks serially although the logic thread holds a job system

- **Severity:** low · **Verdict:** confirmed · **Category:** performance / scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/travel.cpp:447`

`Travel::Seed` calls `SendOut(..., nullptr, ...)` twice, running every chunk in turn on the calling thread, 'as a scene's build has no job system'. The `Host` constructor (on the logic thread, inside `Game`, which holds `core::Jobs&`) and `Host::Load`'s regeneration of the base world (host.cpp:307) both reach it. Each chunk is independent and deterministic per chunk, so the work is embarrassingly parallel.

**Evidence:** travel.cpp:447-458 `SendOut(state, map, paths, nullptr, ...)` twice; travel.hpp:156 doc 'its chunks in turn on the calling thread, as a scene's build has no job system'.

**Impact:** Startup and every load regenerate two months of visits on one core. On large procedural maps that is a long single-threaded stall, while the other cores sit idle.

**Recommendation:** Thread `core::Jobs*` through `Host` construction and `GenerateWorld` (`Game` has it) and pass it to `Seed`. Results are unchanged because chunks are the unit of determinism.

**Verification:** travel.cpp:447-458 passes nullptr to SendOut twice, and the travel.hpp doc says Seed runs on the calling thread. The doc's reason is that a scene build has no job system. The finding's claim that Host/Load could pass jobs is plausible but unchecked. Only a performance opportunity.

### X-CONCURRENCY-10: Docs say these run 'on the thread that created the job system', but they run on the registered logic thread

- **Severity:** low · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/saves.hpp:35`

`Saves::Write` and `Saves::Read` (saves.hpp:35,39), `MonthStep::Run` (npcs.hpp:270), `Travel::Month` (travel.hpp:143) and `Simulation::AdvanceCalendar` and `Publish` (logic.hpp:48,103) all say they run on the thread that created the job system. Since 0002.3.1 the main thread creates `Jobs` and the logic thread, which calls all of them, is a registered external thread. The real rule (jobs.hpp) is 'the creating thread or a registered one'. `saves.hpp`'s file comment also claims 'the logic thread never waits on the disk', but `Write` waits for a previous write and `Read` waits and reads synchronously.

**Evidence:** saves.hpp:35 'On the thread that created the job system.'; saves.hpp:6 'so the logic thread never waits on the disk'; app.cpp:284 ThreadRegistration on the logic thread.

**Impact:** Misleading contracts on the APIs that define who may fork. A reader could conclude the main thread must call them, or that a save never stalls the logic thread.

**Recommendation:** Reword to 'on a thread that may fork on `jobs` (its creator or one registered with it)'. Say that a save waits for one still being written and that a load reads synchronously.

**Verification:** saves.hpp:35 and :39 say 'On the thread that created the job system'. jobs.hpp:22 says the creator or a registered thread may fork. The saves.hpp file comment says the logic thread never waits on disk, while the Write doc says a write in progress finishes first and Read waits. Doc inaccuracy only.
