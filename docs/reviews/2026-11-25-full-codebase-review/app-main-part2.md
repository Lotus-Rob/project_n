# Review: app-main (part 2)

### APP-MAIN-11: Logger shutdown relies on process-exit teardown; no explicit flush on early return or fatal paths

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/main.cpp:161`

core::log::Start has no matching stop or flush, and main never flushes. Early returns for bad flags and the fatal catch paths rely on Quill's static teardown to drain the queue. On std::abort, std::exit from elsewhere or a PN_ASSERT abort, queued messages (often the CRITICAL line) may be lost.

**Evidence:** main.cpp:163-241; core/log.cpp Start.

**Impact:** Low-confidence: after an assert abort the cause may be missing from the log file.

**Recommendation:** Add a core::log::Flush() called before every return from main and on the assert and abort path, if not already done there.

**Verification:** The assert path already flushes: core/assert.cpp calls log::Root()->flush_log() before std::abort(), so the recommendation is done there. main.cpp has no Start counterpart and does not flush. The early returns and the catch paths rely on Quill's teardown, which drains the queue when main returns normally. A raw std::abort or std::exit from elsewhere is the only real gap, and nothing in the cited code does that. Negligible.

### APP-MAIN-12: Scene resident report can name an intermediate scene when the wanted scene changes mid-load

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/app/app.cpp:792`

LoadScene finishes a load already begun even if `wanted` changed, then sets m_reported = scene and sends SceneResident for it. The next frame reloads the newly wanted scene. The logic receives a SceneResident for a scene it no longer waits for.

**Evidence:** app.cpp:798-813.

**Impact:** Relies on the logic ignoring a SceneResident that doesn't match. That is untested here, and a scene stage could advance spuriously if the guard ever changes. It also wastes a full upload of a discarded scene.

**Recommendation:** Cancel the load in the renderer when `wanted` changes, or add a test that the logic ignores a stale SceneResident. Document the rule at the SendInput site.

**Verification:** app.cpp LoadScene does finish a load already begun and then sends SceneResident for it, as described, and the code comment says this is intended because the renderer loads one scene at a time. The logic side is safe. logic.cpp:174 and :233 only store m_resident = action.scene, and the loading stage compares that to the scene it waits for. Tests in logic_tests.cpp (around line 646, 'a stale report is ignored') cover stale scene reports, though I did not find one for a mismatched SceneResident. The wasted upload is real but rare.

### APP-MAIN-13: Main thread, waiting on its own fork-join, may run the logic thread's long chunks

- **Severity:** info · **Verdict:** unverifiable · **Category:** performance · **Status:** known-deferred · **Reviewer confidence:** low
- **Where:** `src/app/app.cpp:263`

The main thread is the job system's thread 0. While it waits in Jobs::Run (WaitforTask at high priority) from Renderer::Draw, enkiTS may pick up the logic's chunks (the constructor comment says so). A long month-step chunk on the main thread delays the frame, and render code runs under the sim's FP environment.

**Evidence:** App constructor comment at app.cpp:263-264; jobs.cpp:231.

**Impact:** Related to the known month-step hitch. Not a correctness bug while the FP env is set, but it couples frame latency to the step's chunk length.

**Recommendation:** Measure whether main-thread frame spikes during a month step come from running the chunks. Consider a lower priority or smaller chunks for the month step.

**Verification:** I did not verify this one: I read the constructor comment's claim but not the enkiTS wait behaviour. The finding itself says known-deferred and low confidence. Only a Tracy measurement during a month step would decide whether the main thread runs logic chunks.

### APP-MAIN-14: data_watch_tests use fixed names in the shared temp directory

- **Severity:** info · **Verdict:** confirmed · **Category:** test-quality · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/app/data_watch_tests.cpp:22`

Each fixture remove_alls and recreates temp_directory_path()/project_n_data_watch_tests_<name>. Two test processes running at once (different presets, or CI plus a local run) share and delete each other's folder.

**Evidence:** data_watch_tests.cpp:21-30.

**Impact:** Rare flaky failures when presets run in parallel.

**Recommendation:** Add the process id or a random suffix to the folder name.

**Verification:** DataFolder in data_watch_tests.cpp (lines 21-30) builds the path as temp_directory_path()/project_n_data_watch_tests_<name> and calls remove_all in the constructor, with no pid or random suffix. Two concurrent test processes would clobber each other. This is a low-probability flake in a test-quality area.
