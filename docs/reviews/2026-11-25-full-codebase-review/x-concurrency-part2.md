# Review: x-concurrency (part 2)

### X-CONCURRENCY-11: Handover::ReturnState has no bound or ownership check

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/handover.hpp:156`

`ReturnState` does `m_free[m_freeCount++] = state;` with no assertion. `PopFree` asserts `m_freeCount > 0`, but nothing catches a double return, the return of a pointer the handover didn't give, or more than `BUFFERS - 1` free buffers. A double return writes past the end of the 5-element `std::array`, or puts one buffer in the free list twice, so the logic thread later writes into a snapshot the main thread is drawing.

**Evidence:** handover.hpp:156-160.

**Impact:** A future bug in `Display` (or a second display) would corrupt memory or race silently instead of asserting.

**Recommendation:** Add `PN_ASSERT(m_freeCount < BUFFERS - 1, ...)`. In debug, also assert that `state` points into `m_states` and is not already in `m_free` or `m_waiting`.

**Verification:** handover.hpp:156-160 does an unchecked m_free[m_freeCount++]. PopFree asserts at :186. The array has BUFFERS=5 slots but at most 4 buffers are free (the loop at :48 starts at 1, so one is held by the logic thread). A single double return would therefore not overflow the array. It would still put a buffer in the free list twice. A bound assert is cheap but not needed to prevent an immediate overflow.

### X-CONCURRENCY-12: Every burst republishes the whole snapshot even while frozen or paused and nothing changed

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** wasted work · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/logic.cpp:44`

`RunBurst` clears and refills the snapshot and calls `Simulation::Publish` every burst (60 times a second), whether or not a tick advanced. That is a parallel pack, a radix sort and a gather of every entity and every visible traveller, plus labels. It happens while the window is unfocused (live `Pause`) and while the world clock is paused, even when the view hasn't changed.

**Evidence:** logic.cpp:44-87: unconditional clear and `m_simulation->Publish(snapshot, *m_jobs, culled)`.

**Impact:** The workers burn CPU at 60 Hz while the game is paused or in the background. With tens of thousands of travellers on the world map that is ms of work per tick for an identical picture. This conflicts with the project's 'avoid wasted work' rule. It is the logic-side counterpart of known Audit 12 (paused frames re-lay seals).

**Recommendation:** When the burst ran no advancing tick, the scene change hasn't moved, and the view (`ReadView`'s area, debug sight, animations pointer) is unchanged since the last publish, skip `Simulation::Publish`. Either publish a snapshot flagged as a repeat, or don't publish at all and let `Display` keep its drawn snapshot. Measure with `measure` while unfocused.

**Verification:** logic.cpp:44-87 clears and refills the snapshot and calls Simulation::Publish unconditionally on every burst. I did not check whether paused bursts take a different path earlier. I did not check the avoid-wasted-work rule or audit 12 either. The measured cost is unknown, so the severity is unverified.

### X-CONCURRENCY-13: Fonts::RasteriseRequests pops from the front of a vector under the queue mutex

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/text_layout.cpp:180`

The worker takes each glyph with `requests.erase(requests.begin())` while holding `queueMutex`, so draining n requests is O(n²). The main thread's `Exchange` takes the same mutex every frame to append.

**Evidence:** text_layout.cpp:176-181.

**Impact:** With a burst of new glyphs (a language switch, many new names on the world map) the worker shifts the whole queue for each glyph while holding the lock the main thread waits on each frame. Minor today.

**Recommendation:** Use a `std::deque`, or keep a read index and clear when drained, or swap the whole request vector out under the lock and rasterise it outside.

**Verification:** text_layout.cpp:176-181 does requests.erase(requests.begin()) under queueMutex, which is O(n^2) when draining. The queue is small in practice, so the impact is minor.

### X-CONCURRENCY-14: Glyph rasterisation on worker threads is outside SDL3_ttf's documented threading contract

- **Severity:** low · **Verdict:** unverifiable · **Category:** third-party thread safety · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/gfx/text_layout.cpp:191`

SDL3_ttf 3.2.2 documents `TTF_GetGlyphImageForIndex` as 'should be called on the thread that created the font'. The field font is opened on the main thread, but `Fonts::RasteriseField` calls it on enkiTS workers (serialised by `fieldMutex`), while the main thread shapes concurrently with another font on the same global FreeType library. FreeType's own rules (one face per thread, open and close serialised) are followed, so it works today.

**Evidence:** SDL_ttf.h:1138-1143 threadsafety note; text_layout.cpp:188-192 `Rasterise(field.get(), glyph)` called from `RasteriseRequests` on a worker.

**Impact:** An SDL_ttf upgrade that adds per-thread or library-wide state (caches, a shared HarfBuzz buffer, the SDF renderer's raster) could make this a data race. Nothing tests it under TSan.

**Recommendation:** Record the reliance in the code comment and in the bump checklist for SDL3_ttf, and cover the text batch with a TSan-run test (main shaping while a batch rasterises). Alternatively, give the worker its own `TTF_Font` opened and closed under the documented rules.

**Verification:** text_layout.cpp:188-192 does call Rasterise on the field font from a worker thread under fieldMutex. I did not check the SDL_ttf header's threading note, TSan coverage or the font's creating thread. Only a TSan run or an SDL_ttf source audit would settle whether it is a real race.

### X-CONCURRENCY-15: Game's tick clock starts before world generation, so startup is counted as a hitch

- **Severity:** low · **Verdict:** confirmed · **Category:** startup ordering · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/game.cpp:17`

`m_clock{ setup.startNs }` uses `App::m_startNs`, taken on the main thread before the logic thread starts. The `Host` member, built just before the clock, generates the world (`GenerateWorld`, two months of `Travel::Seed`) on the logic thread. The first `Step` then sees a backlog of the whole generation time.

**Evidence:** game.cpp:10-18 initialiser order `m_host(...)` then `m_clock{ setup.startNs }`.

**Impact:** The first burst drops the backlog ('ticks dropped after a hitch: the game slowed down' at debug), and the late-tick tracking starts in a misleading state on large maps. Cosmetic today, but the first measurements after startup are skewed.

**Recommendation:** Start the clock at `m_now()` once `Host` and `Logic` are built (in the `Game` constructor body), or `Restart(now())` before the first `Run` iteration.

**Verification:** game.cpp:10-17 builds m_host (Host ctor runs GenerateWorld, host.cpp:786) before m_clock{setup.startNs}. startNs is App::m_startNs (app.cpp:243), taken before the logic thread starts. The first Advance therefore sees the whole generation time as backlog, and TickClock::Advance caps it at MAX_CATCH_UP and reports droppedTicks. The effect is a debug log line and a one-off late flag; the cap bounds it. Fix: m_clock.Restart(m_now()) in the Game ctor body.

### X-CONCURRENCY-16: No test exercises two threads forking on one Jobs at once, or Game::Run on a jthread with stop

- **Severity:** low · **Verdict:** confirmed · **Category:** test coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/core/jobs_tests.cpp:159`

'a registered thread forks, joins and runs background jobs' runs the registered thread while the creating thread just sits in `join()`. Nothing tests the production shape: the main thread forking (and waiting on `Pending`s) while the registered logic thread forks, with cross-stealing. Game tests call `Step` on the creating thread only, and stop tokens are tested only when requested in advance (npcs_tests.cpp:548). `App` and `Game::Run` shutdown is untested.

**Evidence:** jobs_tests.cpp:159-176; tests/unit/app/game_tests.cpp uses only `Step`.

**Impact:** Races in the cross-thread scheduling path, in shutdown (stop during a month step, during a save) and in `Pending` destruction are covered by no test, TSan or otherwise.

**Recommendation:** Add a test where the creating thread and a registered thread run many `ParallelFor`s and `Start`/`Pending`s concurrently, checking every chunk ran once, and run it under TSan in the proposed nightly job. Add a `Game::Run` test on a `jthread` that requests stop mid-run and checks a clean, bounded exit.

**Verification:** jobs_tests.cpp:159-172 has the registered thread fork while the creating thread only joins. No test has two threads forking concurrently. I found no Run( call in tests/unit/app. Coverage of cross-thread stealing and of the Run/stop shutdown path is missing. I did not check whether TSan is run in CI, so impact is uncertain, but the gap is real.

### X-CONCURRENCY-17: App constructor comment understates when the main thread runs simulation chunks

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/app.cpp:263`

The comment says the main thread 'waiting on its low-priority work... may run the logic's chunks'. It also runs them whenever it joins its own high-priority `ParallelFor` (volleys, numbers), because both threads fork at the same priority. 0002.3.1 §1 has the same omission.

**Evidence:** app.cpp:263-265.

**Impact:** The latency coupling between render and simulation goes unnoticed (see the stealing finding).

**Recommendation:** State the full rule: any join on the main thread may run logic chunks, and the reverse; or change it if priorities are split.

**Verification:** The app.cpp:263 comment mentions only low-priority waiting, and the finding's claim that the main thread also runs logic chunks when it joins its own ParallelFor follows from jobs.hpp:22 ('works on chunks too'). I did not check priorities or the design doc. Comment-level only.

### X-CONCURRENCY-18: Thread-role invariants (logic-only, main-only, same thread each time) are comments only; annotations could enforce them

- **Severity:** info · **Verdict:** unverifiable · **Category:** thread-safety annotations · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/thread_safety.hpp:1`

The annotations cover only mutex-guarded members. The most important invariants are thread roles, and none is checked:
- `Handover::m_writing` and `Writing()` are the logic thread's;
- `m_drained` and `Display` are the main thread's;
- `Jobs::Background` must be called from the same thread each time;
- a thread may have only one `Jobs`;
- `LegWalkCache::Clear` runs only outside a publish.

`AssertMayFork` is not called in `Pending::~Pending` or `Done()`, and all these asserts compile away in release.

**Evidence:** handover.hpp:211 `State* m_writing = ... // the logic thread's alone`, 213 `m_drained; // the main thread's alone`; jobs.hpp:24 'A thread has one at a time'.

**Impact:** A refactor that calls `Writing()` or `Clear()` from the wrong thread, or creates a second `Jobs` on a thread (enkiTS shares `thread_local` thread numbers), passes every gate.

**Recommendation:** Add role capabilities, for example `class PN_THREAD_ANNOTATION(capability("role")) ThreadRole {}; extern ThreadRole MAIN_THREAD, LOGIC_THREAD;`. Annotate `Writing()` with `PN_REQUIRES(LOGIC_THREAD)` and the `Display`/`m_drained` users with `MAIN_THREAD`, asserted with `PN_THREAD_ANNOTATION(assert_capability(...))` at thread entry. Add a debug `thread_local` guard in the `Jobs` constructor and record the `Background` owner thread.

**Verification:** Handover's logic-thread-only and main-thread-only invariants are comments (jobs.hpp:24 'A thread has one at a time'). I did not check whether thread roles are enforced anywhere. This is a design suggestion. Whether it is worth the annotation machinery is a judgment call.

### X-CONCURRENCY-19: Host comment says journey chunks 'share nothing else', but they share the walk-table cache and kept walks

- **Severity:** info · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.hpp:452`

The `PublishJourneys` comment says each chunk fills its draw-list part and its labels, 'sharing nothing else with the other chunks'. In fact all chunks share `m_legWalks` (taking its shared and exclusive locks) and write `m_keptWalks` by journey index.

**Evidence:** host.hpp:452-455; host.cpp:1274 `core::LegWalkCache& walks = *m_legWalks;` and 1305 `PoseOn(..., m_keptWalks, index, walks)`.

**Impact:** A reader may move or resize these structures inside a chunk believing the chunks are independent.

**Recommendation:** List what is shared and how it is protected: `LegWalkCache` locked, `m_keptWalks` partitioned by journey index and sized before the fork.

**Verification:** host.hpp:452-455 says the chunks share nothing else. host.cpp:1274 and 1305 show the chunks use the shared m_legWalks cache and write m_keptWalks by journey index. m_keptWalks is sized before the fork (host.cpp:1252-1254). The comment is imprecise, and the code is sound as far as I could see.

### X-CONCURRENCY-20: noexcept SDL log callback can throw from logging

- **Severity:** info · **Verdict:** confirmed · **Category:** exception-safety · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/platform/sdl_log.cpp:14`

`LogFromSdl` is `noexcept`, and SDL can call it from any thread, but `PN_LOG_*` copies the message into Quill's queue, which may allocate, and an allocation failure throws.

**Evidence:** sdl_log.cpp:14 `void SDLCALL LogFromSdl(...) noexcept {`.

**Impact:** Out of memory, an SDL message terminates the process from inside SDL. Negligible in practice.

**Recommendation:** Wrap the body in try/catch(...) to drop the message, or accept and note it.

**Verification:** sdl_log.cpp:14 is noexcept and calls PN_LOG_*, which can allocate in Quill, so an allocation failure would terminate. This only happens on OOM, where terminate is already the likely outcome. Negligible.
