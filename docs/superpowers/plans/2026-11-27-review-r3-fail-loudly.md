# R3: Fail loudly Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** No failure ends the game silently: an exception on any thread, a crash signal, a scene that can't load and a graphics card that stops drawing each leave their cause in the log and, where the process survives long enough, in a dialog naming the log folder, with a non-zero exit status.

**Architecture:** The job system catches at each of its three entry points: a parallel-for's exception is rethrown by the join, a started (low-priority) chunk's by the owner's `Done()`, a background job's is logged Critical and returned by the wait. The log backend starts with Quill's signal handler and a `std::terminate` handler that logs the active exception, so whatever still escapes is flushed. `App::Run` returns the failure that ended it (logic thread, scene load, lost graphics card), checked before any quit; `main` reports every runtime failure through one dialog function beside the start-up one. A banner at start ties a log to its build and machine.

**Tech Stack:** C++23, enkiTS, Quill 13.1.0 (`quill-wine`), SDL3 3.4.16 (message box, GPU device properties, CPU info), XMake, doctest.

**Spec:** `docs/superpowers/specs/2026-11-25-review-mediums-design.md`, area **R3. Fail loudly** (full; plan only). Findings: X-RESILIENCE-01..04 (`x-resilience.md`), CORE-INFRA-01 (`core-infra.md`), APP-MAIN-01, APP-MAIN-02 (`app-main.md`), DESIGN-DOCS-06 (`design-docs.md`), GD-ROADMAP-SCOPE-08 (`gd-roadmap-scope.md`: its silent exit only), GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-02, GAP1-SCENE-TRANSITION-FLOW-09 (its silent exit only), all in `docs/reviews/2026-11-25-full-codebase-review/`.

## History

- `platform::ShowError` and `platform::ReportStartFailure` (`src/platform/window.cpp:372-387`, injectable `show`, tested in `tests/unit/platform/window_tests.cpp:41`) are the start-up dialog; only `main.cpp:62` calls it. Nothing names the log folder.
- `App::RunLogic` (`src/app/app.cpp:282-310`) catches on the logic thread, logs Critical and sets `m_logicFailed`; `App::Run` (`:313-334`) checks quit and `--quit-after` before it. `main` (`src/main.cpp:234-241`) catches on the main thread and only logs.
- `src/core/jobs.cpp` has no `try`: `ChunkTask::ExecuteRange` (`:40`), `Pending::Task::ExecuteRange` (`:77`), `BackgroundTask::ExecuteRange` (`:124`). `Jobs::Run` keeps its `ChunkTask` on the stack (`:228`): a chunk that throws on the forking thread unwinds it while workers still run it.
- `src/core/log.cpp` starts Quill with plain `quill::Backend::start()` in two places (`:79` in `Start`, `:125` for the early logger); Quill starts once (`call_once`), so both must pass the same options. Only `core/assert.cpp` flushes before aborting.
- R2's Outcome: a save to a non-ASCII slot once ended the process with SIGABRT from inside the background job: this branch's uncaught path, seen for real.
- The renderer logs a run of failed frames once (`core::RepeatedFailure`, `Renderer::Impl::Report`, `src/gfx/renderer.cpp:1320`) and never escalates; a minimised window (no swapchain texture) is not a failure.
- Decided in the spec, not reopened here: device loss ends cleanly with the dialog, rebuilding the device is later; an emergency save on a mid-session failure goes to D1 with autosave; no crash dumps or minidumps (opt-in crash reporting is in `game-design-gaps.md`).
- Nothing tried or rejected before (palace, git, docs). No backlog item is folded into R3. `review-r11` (PR #60) touches none of these sources; only the spec file is shared (expect a rebase of the spec at the end).
- Cleanup found on the way: the job system logs its start (`app.cpp:267`) but never its stop; Task 4 adds the line.

## Global Constraints

- Our code still throws nothing (`src/AGENTS.md`): the job system only carries the standard library's and third-party exceptions to a thread that handles them. No new `throw` expression except `std::rethrow_exception`.
- Bugs in `src/` are fixed test-first: each task's failing test is committed with its fix.
- Tests stay small: no sweeps, no sleeps as synchronisation; a test that needs a process to die runs it as a child.
- Logs say outcomes; a failure is logged once, at the level of its consequence (Critical only when the game stops because of it).
- Code comments say why, never cite the review, spec or plan.
- `xmake.lua` and `tests/xmake.lua` change (Tasks 2, 3): a build change, so CI's `container` jobs on the PR are its container gate (this is a worktree).
- Windows presets run their tests under Wine; every new test passes on linux-gcc, linux-clang and windows-msvc, or says in the test why a platform is skipped, by measurement (the spikes below).
- The dialog's texts are plain English literals, like the start-up one (localisation is a backlog item).

## Review Focus

1. A parallel-for chunk that throws while other chunks are still running: the join returns only after every chunk has finished or been skipped, then rethrows; no worker touches the task afterwards. Task 1 (run under `--sanitizer thread` and `address` once, by the reviewer's request to the controller).
2. Two chunks throw in the same parallel-for: one exception is rethrown, the other dropped, no terminate. Task 1.
3. A background save that throws: the game keeps running, the next save still works, and the wait reports it exactly once. Task 1.
4. A logic failure in the same loop turn as a quit or `--quit-after`: exit status 1, dialog shown. Task 4.
5. Ctrl-C in a terminal still quits the game cleanly through SDL's quit event: the crash handler must not take SIGINT or SIGTERM. Task 2.

## Spikes (measured inside their task, before its code)

| Spike | Measured by | Decision rule | When |
|---|---|---|---|
| S1: can a doctest case run its own binary as a child and read its exit status on all three presets? | Task 2, step 1: `std::system` on the test's own path with `--crash abort` under linux-gcc, linux-clang and windows-msvc (Wine) | Works on all three: the crash tests run everywhere. Fails only under Wine: the crash cases are registered with `doctest::skip(true)` on `_WIN32` and the Outcome says CI's `windows` jobs (native) are their gate, after confirming there that the same cases pass with the skip removed for one push; if they fail natively too, Task 2 is not done. | Task 2, first step |
| S2: does Quill's handler flush on `abort()` and on an access violation in a windows-msvc build? | Task 2's crash cases on windows-msvc | A case fails: add `quill::init_signal_handler<quill::FrontendOptions>()` for the calling thread in `Start()` (Quill's CRT signal handlers are per thread on Windows) and rerun; still failing is a finding for the controller, not a skipped test. | Task 2, after S1 |
| S3: how long does a failed acquire take per frame (does the loop spin)? | Task 5: a debug hook that makes `AcquireFrame` fail, `./dev run -- --quit-after 5`, frames counted in the log line the task adds | Under 1 ms a frame: the main loop sleeps 10 ms after a failed frame. Otherwise no throttle. | Task 5, before the threshold code |

## Waves

| Wave | Tasks | Needs |
|---|---|---|
| 1 | 1, 2, 3 | main; they share no file (1: `core/jobs.*`, `core/saves.*`, `app/game.cpp`; 2: `core/log.*`, `tests/xmake.lua`, `tests/unit/log_startup/`; 3: `xmake.lua`, `build_info.hpp.in`, `main.cpp`, `platform/system.*`, `gfx/renderer.cpp:1397`) |
| 2 | 4 | Task 2's `core::log::Folder()`; Task 3 (`main.cpp`); Task 1 (`Pending::Done()` may throw on the main thread) |
| 3 | 5, 6 | Task 4 (`app.cpp`, `app::LoopEnd`); 6 is docs only and shares no file with 5 |

---

### Task 1: the job system carries exceptions to a thread that handles them

implementer: opus · review: full

**Files:**
- Modify: `src/core/jobs.hpp`, `src/core/jobs.cpp`, `src/core/saves.cpp:28-35`, `src/app/game.cpp:21-25`
- Test: `tests/unit/core/jobs_tests.cpp`, `tests/unit/core/saves_tests.cpp` (the existing saves suite; create it only if none exists)

**Interfaces:**
- Produces:
  - `template<typename Body> void Jobs::ParallelFor(u32 chunks, const Body& body)`: unchanged signature; when a chunk throws, every chunk not yet begun is skipped, the join completes, and the first exception caught is rethrown on the calling thread.
  - `[[nodiscard]] bool Jobs::Pending::Done() const`: when it would return true for chunks of which one threw, it rethrows that exception instead, once; later calls return true. `~Pending()` never throws: an exception nobody took is logged at Critical there.
  - `[[nodiscard]] std::expected<void, core::Error> Jobs::WaitForBackground()`: the error is the last background job's exception (`what()`, or `"an exception of unknown type"`), returned by the first wait after that job and cleared; the job's thread has already logged it at Critical (`"a background job failed: {}"`).
  - `void Jobs::Background(std::function<void()> work)`: unchanged; the wait inside it drops an earlier job's failure (already logged).

- [ ] **Step 1: Write the failing tests** in `jobs_tests.cpp`:

```cpp
TEST_CASE("a parallel-for rethrows a chunk's exception on the thread that forked") {
    core::Jobs jobs{ 4 };
    CHECK_THROWS_WITH_AS(
        jobs.ParallelFor(64, [](u32 chunk) { if (chunk == 17) { throw std::runtime_error{ "chunk 17" }; } }),
        "chunk 17", std::runtime_error);
    std::atomic<u32> ran = 0; // the job system still works afterwards
    jobs.ParallelFor(64, [&](u32) { ++ran; });
    CHECK(ran == 64);
}
TEST_CASE("a parallel-for whose every chunk throws rethrows one of them") {
    core::Jobs jobs{ 4 };
    CHECK_THROWS_AS(jobs.ParallelFor(64, [](u32) { throw std::runtime_error{ "all" }; }), std::runtime_error);
}
TEST_CASE("a parallel-for with one worker rethrows too") { /* Jobs{ 1 }, same as the first */ }
TEST_CASE("started chunks' exception is rethrown by Done, once") {
    core::Jobs jobs{ 2 };
    core::Jobs::Pending pending = jobs.Start(4, [](u32 chunk) { if (chunk == 2) { throw std::runtime_error{ "decode" }; } });
    CHECK_THROWS_WITH([&] { while (!pending.Done()) { std::this_thread::yield(); } }(), "decode"); // the suite's polling loop
    CHECK(pending.Done());
}
TEST_CASE("started chunks whose exception nobody took end without terminating") { /* Start, then let Pending go */ }
TEST_CASE("a background job's exception is reported by the wait, once") {
    core::Jobs jobs{ 2 };
    jobs.Background([] { throw std::runtime_error{ "disk" }; });
    const std::expected<void, core::Error> first = jobs.WaitForBackground();
    REQUIRE_FALSE(first);
    CHECK(first.error().message == "disk");
    CHECK(jobs.WaitForBackground());
    bool ran = false;
    jobs.Background([&] { ran = true; });
    CHECK(jobs.WaitForBackground());
    CHECK(ran);
}
```

and in the saves suite: `"a read after a save that failed in the background still reads the save before it"` (`Saves::Write` a good save and wait; start `jobs.Background([] { throw std::runtime_error{ "disk" }; })` on the same `Jobs`; assert `Read()` returns the first save's bytes).

- [ ] **Step 2: Run** `pixi run project_n test --suite core_tests --test jobs`. Expected: the new cases end the test binary (terminate), i.e. FAIL.
- [ ] **Step 3: Implement.** Each of the three `ExecuteRange` bodies wraps its calls in `try`/`catch (...)`. `ChunkTask` and `Pending::Task` keep `std::exception_ptr` + `std::atomic<bool> failed` (first writer wins by `compare_exchange` on a claim flag before storing the pointer; the pointer is read only after the join, behind the existing `ran` release/acquire edge so ThreadSanitizer sees it); once `failed` is set a chunk's body is skipped but still counted in `ran`, so the join's asserts hold. `Jobs::Run` rethrows after `WaitforTask` returns and `ran == chunks` is checked. `BackgroundTask` logs and stores an `Error`; `m_done` is set on both paths.
- [ ] **Step 4: Callers.** `Saves::Read`: `if (const auto waited = m_jobs->WaitForBackground(); !waited) { PN_LOG_WARNING("the save being written failed, so the one before it is read"); }`. `Game::~Game`: a failure here is already logged; take the result into a named local and log nothing more (no `static_cast<void>` on a `[[nodiscard]]` result without the reason in a comment).
- [ ] **Step 5: Run** `pixi run project_n test --suite core_tests --test jobs` and `--test saves`: PASS. Then `pixi run project_n test` and `pixi run project_n lint --range <task base>..HEAD`.
- [ ] **Step 6: Commit** `fix(core): carry exceptions out of job chunks and background jobs`.

Closes X-RESILIENCE-02, DESIGN-DOCS-06 (code half).

### Task 2: the log survives a crash

implementer: sonnet · review: full

**Files:**
- Modify: `src/core/log.hpp`, `src/core/log.cpp`, `tests/xmake.lua:109-111` (one more target), `tests/AGENTS.md` (the log_startup line names the new target)
- Create: `tests/unit/log_startup/crash_tests.cpp`, `tests/unit/log_startup/crash_main.cpp`

**Interfaces:**
- Produces:
  - `[[nodiscard]] const std::filesystem::path& core::log::Folder()`: the folder `Start()` writes its file in; empty before `Start()` and when it logs to the console only.
  - Behaviour: on SIGABRT, SIGFPE, SIGILL, SIGSEGV (and SIGBUS where it exists) and on an unhandled Windows exception, the lines already logged are in the file before the process dies; `std::terminate` first logs `"terminating: {what}"` at Critical (`"terminating: an exception of unknown type"`, `"terminating with no active exception"`), flushes and aborts.

- [ ] **Step 1 (spike S1):** `crash_main.cpp` is the target's `main`: with `--crash <kind> <folder>` it calls `core::log::Start(folder)`, logs `PN_LOG_INFO("last line before the crash")`, then by kind: `abort` → `std::abort()`; `segv` → `std::raise(SIGSEGV)`; `terminate` → `std::thread{ [] { throw std::runtime_error{ "boom on a thread" }; } }.join()`; otherwise it runs doctest and keeps its own path (`argv[0]`) for the cases. Register `unit_tests("log_crash_tests", {"unit/log_startup/crash_main.cpp", "unit/log_startup/crash_tests.cpp"})`. Measure the child run on the three presets and apply S1's rule; record the result in the task report.
- [ ] **Step 2: Write the failing tests** in `crash_tests.cpp`, each running the child with `std::system` into a fresh temporary folder and reading `project_n.log` there:

```cpp
TEST_CASE("an abort leaves the lines logged before it in the file") {      // kind abort
    CHECK(status != 0);
    CHECK(log.contains("last line before the crash"));
}
TEST_CASE("a segmentation fault leaves the lines logged before it in the file") { /* kind segv, same checks */ }
TEST_CASE("an exception that escapes a thread is named in the log") {      // kind terminate
    CHECK(status != 0);
    CHECK(log.contains("last line before the crash"));
    CHECK(log.contains("terminating: boom on a thread"));
}
TEST_CASE("Folder is the folder Start was given") { /* in-process: Start(dir); CHECK(core::log::Folder() == dir); */ }
```

- [ ] **Step 3: Run** `pixi run project_n test --suite log_crash_tests`. Expected: FAIL (the lines are missing: the queue is lost).
- [ ] **Step 4: Implement.** One private `StartBackend()` used by both `Start()` and the early logger: `quill::Backend::start<quill::FrontendOptions>(quill::BackendOptions{}, options)` with `options.catchable_signals = { SIGABRT, SIGFPE, SIGILL, SIGSEGV }` plus `SIGBUS` under `#ifdef SIGBUS`, `options.timeout_seconds = 5`, `options.logger_name` left empty; then `std::set_terminate` with the handler above (it calls `Root()->flush_log()` then `std::abort()`). SIGINT and SIGTERM are left out on purpose: SDL turns them into its quit event only while their handlers are the defaults. `<csignal>` and `<thread>` go into `pch.hpp` if missing.
- [ ] **Step 5 (spike S2):** run the suite on windows-msvc; apply S2's rule.
- [ ] **Step 6: Verify Review Focus 5 by hand:** `./dev run`, Ctrl-C in the terminal: the log ends with `project_n stopped with status 0`. Put the log's last two lines in the task report.
- [ ] **Step 7: Run** `pixi run project_n test` on the native preset, `pixi run project_n test --preset windows-msvc --suite log_crash_tests`, `pixi run project_n lint --range <task base>..HEAD`.
- [ ] **Step 8: Commit** `fix(core): flush the log on a crash and name an uncaught exception`.

Closes X-RESILIENCE-01, CORE-INFRA-01.

### Task 3: a start-up banner that ties a log to its build and machine

implementer: sonnet · review: full

**Files:**
- Modify: `xmake.lua:319-340` (two more config variables), `src/build_info.hpp.in`, `src/main.cpp:167`, `src/gfx/renderer.cpp:1397-1402`
- Create: `src/platform/system.hpp`, `src/platform/system.cpp`
- Test: `tests/unit/platform/system_tests.cpp`

**Interfaces:**
- Produces:
  - `PN_GAME_COMMIT` (a string literal: `git rev-parse --short=12 HEAD`, with `-dirty` appended when `git status --porcelain` prints anything, `"unknown"` when git fails or there is no repository: a container build of a worktree) and `PN_BUILD` (`"<plat>-<arch> <toolchain> <mode>"`, e.g. `"linux-x86_64 clang release"`), both set where `PN_GAME_VERSION` is, in the target's `on_config`.
  - `[[nodiscard]] std::string platform::SystemSummary()`: `"<OS name and version>, <n> logical CPUs, <m> MiB RAM"`. OS text: Linux `PRETTY_NAME` from `/etc/os-release` plus `uname`'s release; macOS `uname` sysname and release; Windows `RtlGetVersion`'s major.minor.build; `SDL_GetPlatform()` alone when the source can't be read. CPUs and RAM from `SDL_GetNumLogicalCPUCores` and `SDL_GetSystemRAM`.
  - The log's first line: `PN_LOG_NOTICE("project_n {} ({}), {}", PN_GAME_VERSION, PN_GAME_COMMIT, PN_BUILD)`, replacing `"project_n starting"`; the second: `PN_LOG_NOTICE("running on {}", platform::SystemSummary())`.
  - The GPU line becomes `"GPU: {} ({} {}), driver {}, presenting with {}{}"`: `SDL_PROP_GPU_DEVICE_NAME_STRING`, `SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING`, `SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING` from `SDL_GetGPUDeviceProperties` (each `"unknown"` when absent), then `SDL_GetGPUDeviceDriver` and the present mode as now.

- [ ] **Step 1: Write the failing test** `"the system summary names the OS, the CPU count and the memory"`: `SystemSummary()` matches `^.+, [1-9][0-9]* logical CPUs, [1-9][0-9]* MiB RAM$` and its OS part is not empty.
- [ ] **Step 2: Run** `pixi run project_n test --suite platform_tests --test system`: FAIL (no such header).
- [ ] **Step 3: Implement** `platform/system.*` (`<windows.h>` and `<sys/utsname.h>` only in the `.cpp`), the two config variables, the banner lines and the GPU line.
- [ ] **Step 4: Run** the suite: PASS. `./dev run -- --quit-after 1` and paste the three lines into the task report; `pixi run project_n build --preset windows-msvc --mode release` is the controller's after the task lands.
- [ ] **Step 5: Run** `pixi run project_n test`, `pixi run project_n lint --range <task base>..HEAD`.
- [ ] **Step 6: Commit** `build(xmake): stamp the commit and build into the binary` then `feat(app): open the log with the build, the machine and the graphics card` (two commits: the build change stands alone).

Closes GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-02.

### Task 4: runtime failures show the dialog and set the exit status

implementer: sonnet · review: full

**Files:**
- Modify: `src/platform/window.hpp:170-182`, `src/platform/window.cpp:372-387`, `src/main.cpp` (`FailToStart`, `Run`, the two `catch` blocks), `src/app/app.hpp`, `src/app/app.cpp:251-334` and `:793-812`, `src/app/module.dox` if it describes the exit status
- Create: `src/app/loop_end.hpp`
- Test: `tests/unit/app/loop_end_tests.cpp`, `tests/unit/platform/window_tests.cpp`

**Interfaces:**
- Consumes: `core::log::Folder()` (Task 2); `Jobs::Pending::Done()` may throw on the main thread (Task 1): it reaches `main`'s handlers.
- Produces:
  - `void platform::ReportFailure(std::string_view title, std::string_view what, const core::Error& error, const std::filesystem::path& logFolder, const std::function<void(std::string_view, std::string_view)>& show = ShowError)`: logs `"{what}: {error}"` at Critical and shows `"{what}:\n{error}"`, followed by `"\n\nThe log is in:\n{folder}"` when `logFolder` isn't empty (through `core::PathText`). `ReportStartFailure(what, error, logFolder, show)` gains the folder and calls it with the title `"project_n can't start"`; runtime failures use `"project_n has stopped"`.
  - `enum class app::LoopEnd : u8 { None, Failed, Quit }` and `[[nodiscard]] constexpr LoopEnd app::EndOf(bool failed, bool quit, bool quitAfter)`: `Failed` whenever `failed`, else `Quit` when either quit, else `None`.
  - `[[nodiscard]] std::expected<void, app::Failure> App::Run()` with `struct app::Failure { std::string what; core::Error error; }`: `{"the game's logic failed", <exception text>}` or `{"the scene <name> couldn't be loaded", <error>}`. The logic thread stores its `core::Error` under a mutex before setting `m_logicFailed`; `Run` checks `EndOf` first each turn and once more after the loop (a failure in the turn that quit).
  - `main`: `Run` failing → `platform::ReportFailure("project_n has stopped", failure.what, failure.error, core::log::Folder())`, status 1; both `catch` blocks do the same with `what = "an unexpected error"`.
  - The logic thread's `GameSetup` is built in `App`'s constructor body and moved into the thread's lambda, so the thread never reads `m_options` (APP-MAIN-02); `--cast-swords` and the hot reload keep using `m_options.data` on the main thread.
  - `App::~App` logs `"job system stopped"` at Info after the logic thread joined (the missing stop line).

- [ ] **Step 1: Write the failing tests.** `loop_end_tests.cpp`: `"a failure ends the loop as failed, whatever else asked to quit"` over the 8 input combinations as a table (`failed` true → `Failed` for all four; `{false,true,false}`, `{false,false,true}`, `{false,true,true}` → `Quit`; all false → `None`). `window_tests.cpp`: `"a reported failure is shown with the log folder under it"` (a `show` that records title and message: title as passed, message `"the scene arena couldn't be loaded:\nbad sprite\n\nThe log is in:\n<folder>"` for the folder `u8"logs_é"`), `"a reported failure with no log folder says nothing about the log"`, and the existing start-failure case updated for the new parameter.
- [ ] **Step 2: Run** `pixi run project_n test --suite app_tests --test loop_end` and `--suite platform_tests --test window`: FAIL.
- [ ] **Step 3: Implement** the interfaces above.
- [ ] **Step 4: Check the paths by hand** with a temporary local edit that is not committed: `throw std::runtime_error{ "test" }` at the top of `Game::Step` → `./dev run -- --quit-after 2` shows the dialog and `echo $?` is 1; rename a sprite the arena needs → the scene dialog, status 1. Put both dialogs' texts and statuses in the task report.
- [ ] **Step 5: Run** `pixi run project_n test`, `pixi run project_n lint --range <task base>..HEAD`.
- [ ] **Step 6: Commit** `fix(app): report a runtime failure in a dialog and in the exit status`.

Closes X-RESILIENCE-04, APP-MAIN-01, APP-MAIN-02, GD-ROADMAP-SCOPE-08's and GAP1-SCENE-TRANSITION-FLOW-09's silent exit.

### Task 5: a graphics card that stops drawing ends the game with the dialog

implementer: sonnet · review: full

**Files:**
- Modify: `src/core/repeated_failure.hpp`, `src/gfx/renderer.hpp`, `src/gfx/renderer.cpp:1320-1330` and `:1568-1577`, `src/app/app.cpp` (`Run`, `Present`), `src/app/app.hpp`
- Create: `src/core/failed_frames.hpp`
- Test: `tests/unit/core/failed_frames_tests.cpp`, `tests/unit/core/repeated_failure_tests.cpp` (extend the existing one if present)

**Interfaces:**
- Consumes: `app::EndOf`, `app::Failure` (Task 4).
- Produces:
  - `[[nodiscard]] u64 core::RepeatedFailure::Failures() const`: the current run's length.
  - `class core::FailedFrames` with `inline constexpr u64 LOST_FRAMES = 120;` and `inline constexpr u64 LOST_NS = 3 * NS_PER_SECOND;` and `[[nodiscard]] bool Lost(u64 failuresInARow, u64 nowNs)`: remembers when the run began (the first call with `failuresInARow > 0` after a zero) and returns true once the run has both `LOST_FRAMES` frames and `LOST_NS` of time. Both, because an unpaced loop fails 120 frames in milliseconds and a 1 Hz compositor takes minutes to reach 120.
  - `[[nodiscard]] u64 gfx::Renderer::FailedFrames() const` and `[[nodiscard]] const core::Error& gfx::Renderer::LastFailure() const` (the run's first error, as logged).
  - `App::Run` ends with `Failure{ "the graphics card stopped drawing", LastFailure() }` when `Lost` turns true; one Critical line `"{} frames failed in a row over {} s: stopping"`.

- [ ] **Step 1 (spike S3):** add a debug-only flag `--fail-frames` (in `app::Options`, read in `main`, documented in `src/app/module.dox`, kept: it is how this path is checked by hand) that makes `Renderer::Acquire` report `core::Error{ "frames fail on request" }` without acquiring. Measure and apply S3's rule.
- [ ] **Step 2: Write the failing tests** in `failed_frames_tests.cpp`: `"a run is lost only once it is both long in frames and in time"` as a table: `(119, 10 s)` false; `(120, 2.9 s)` false; `(120, 3 s)` true; `(5000, 0.5 s)` false; and `"a success in between starts the run again"`: `(100, t0)`, `(0, t0+1 s)`, `(120, t0+3.5 s)` false (the second run begins here), `(400, t0+6.4 s)` false, `(500, t0+6.5 s)` true. `"Failures counts the run and Succeed ends it"` for `RepeatedFailure`.
- [ ] **Step 3: Run** `pixi run project_n test --suite core_tests --test failed_frames`: FAIL.
- [ ] **Step 4: Implement**, and wire `Run`: `EndOf(failed || lost, ...)`.
- [ ] **Step 5: Check by hand:** `./dev run -- --fail-frames`: after 3 s the dialog names the log folder, status 1; a minimised window for 10 s does not trigger it. Report both.
- [ ] **Step 6: Run** `pixi run project_n test`, `pixi run project_n lint --range <task base>..HEAD`.
- [ ] **Step 7: Commit** `fix(gfx): stop with a dialog when frames keep failing`.

Closes X-RESILIENCE-03.

### Task 6: the docs say what is true

implementer: sonnet · review: light

**Files:**
- Modify: `docs/design/0002-engine-architecture.md:494-503` (§12), `src/AGENTS.md` (the Errors rule), `docs/design/0002.1-app-and-game.md` where it describes the logic thread's failure and the exit status

- [ ] **Step 1:** §12's bullets, rewritten to the behaviour Tasks 1-5 built: **Exceptions** (ours: none; the standard library's and third-party ones are caught at every thread's entry: `main`, the logic thread, and each job-system entry, which hands a parallel-for's to the forking thread, a started chunk's to its owner's `Done()`, and a background job's, logged Critical, to the wait; anything else reaches the terminate handler, which logs it and flushes); a new **Crashes** bullet (signals and unhandled Windows exceptions flush the log; no dump is written; SIGINT and SIGTERM stay SDL's); **Runtime failures** beside **Start-up failures** (logic thread, scene load, 120 failed frames over 3 s: the same dialog, naming the log folder, exit status 1 even when a quit arrives in the same turn; no emergency save yet). Dated nothing, cite no finding IDs.
- [ ] **Step 2:** `src/AGENTS.md`'s Errors line gains: `an exception inside a job reaches the forking thread (parallel-for), Done() (started chunks) or WaitForBackground() (background)` within the line limit; `pixi run project_n doctor --docs` passes.
- [ ] **Step 3: Commit** `docs(design): describe how failures are caught, logged and shown`.

Closes DESIGN-DOCS-06 (doc half).

## Finishing (the controller, not a task)

The spec's **Outcome: R3** from the ledger (the three spikes' measurements, the thresholds, the calls below), R3 marked **Implemented**, Order step 3 `done`, each finding's Closing cell; then `check`, the final review, the PR.

## Calls made (for the user to check)

1. Started (low-priority) chunks rethrow from `Pending::Done()`, not from the destructor: both users (scene decode, glyph rasterising) poll it on the main thread, so the exception reaches `main`'s handlers and the dialog.
2. A background job's failure is returned by `WaitForBackground()` (R4's save error path builds on it); nothing rethrows it.
3. The crash handler takes SIGABRT, SIGFPE, SIGILL, SIGSEGV, SIGBUS only: SIGINT and SIGTERM stay with SDL so Ctrl-C still quits cleanly.
4. Device loss: 120 failed frames in a row **and** 3 seconds, then the dialog; no rebuild (the spec's call).
5. `--fail-frames` stays as a debug-build flag: the only way to check the device-loss path without unplugging a card.
6. The banner's commit is read at configure time, so a build not reconfigured since a commit shows the older hash; `-dirty` marks local changes. Release artefacts are configured fresh.
