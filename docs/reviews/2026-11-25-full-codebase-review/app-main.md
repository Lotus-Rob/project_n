# Review: app-main

**Scope:** src/main.cpp, src/pch.hpp, src/build_info.hpp.in, src/app/app.{hpp,cpp}, game.{hpp,cpp}, view.{hpp,cpp}. Focus additionally on the main loop, threading between sim and render, shutdown, error paths. Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 14 standing (0 critical, 0 high, 1 medium, 8 low, 5 info), 0 raised but refuted on verification.

**Parts:** app-main.md, app-main-part2.md

## Assessment

## Assessment: app-main (main.cpp, pch.hpp, build_info.hpp.in, app/app, game, view)

I read all the in-scope sources and the matching tests (`game_tests`, `data_watch_tests`, `command_line_tests`, `view_tests` headings). I also read the relevant callees: `core::PresentFrame`, `Logic::RunBurst`, `Jobs::Background` and `WaitForBackground`, `platform::WaitUntilNs`, `core::log::Start`, `data::LoadSettings`. I did not read the other `app/` files in full.

**Strengths**
- The threading model is small and clear. Main and logic share only `GameHandover`.
- The jthread is the last member, so it joins before anything it uses is destroyed.
- The destructor stops and joins explicitly.
- `RunLogic` catches exceptions and publishes failure through an atomic.
- `PresentFrame` documents its acquire, prepare and draw ordering, including the fatal-on-prepare-failure case.
- Start-up failures go through one reporting path that logs and shows a dialog.
- Frame and wall clocks clamp huge gaps (`FrameClock`, `CameraControl`), so a hidden window or a stall can't make labels or the camera jump.
- Pure helpers (`view.cpp`, the `Keep*` and `ReloadOf` functions, the command-line parsers) are testable and mostly tested.
- `game.cpp` is tiny and well-factored. The tick clock is injected, so `game_tests` run without threads.

**Weaknesses and risks**
- **Data race in debug builds.** The logic thread copies `m_options.data` while the main thread's hot reload may assign it. The window is the world-generation time, which can last seconds. This is a real race and an undefined-behaviour copy of `shared_ptr` members.
- **Silent logic-thread death.** The only evidence is a log line and exit status 1. The window just vanishes, with no dialog and no "shutting down" summary.
- **Exit status can hide a crash.** If the user quits at the same moment, the status is 0.
- **Untested orchestration.** `main` and the `App` orchestration (`Run`, `LoadScene`, `HandleInput`, `WatchData`, `FollowPath`, `Present`) have no tests at all. They are about 600 lines of stateful logic, and defects in them (the race above, the quit and fail ordering, the load/report sequencing) would not be caught.
- **Controls and accessibility.**
  - Escape quits instantly with no confirmation.
  - Cast is press-only. `Cast()` and `CastAction()` carry a `pressed` argument that is always true, so hold-to-fire can't be added without rework.
  - Both matter given the accessibility-first priority.
- **Smaller costs.**
  - There are five copies of the same from_chars command-line parse.
  - Unknown flags are silently ignored.
  - The hover scan is linear over every instance each frame.
  - `GameData` is copied three or four times at start-up.
  - The window is opened before the slow data load and then not pumped.

**Overall health:** good for this layer. It is simple, deliberate and well-commented. The one real correctness hazard is the debug-only data race. The main gaps are test coverage of the app loop and failure UX (logic thread death, Escape). The threading and shutdown design is sound as written. I found no determinism hazards in this scope: the sim is not touched from the main thread except through the handover, and the FP environment is set on both threads.

**Files read in full:** (not reported)

## Findings

### APP-MAIN-01: Logic thread failure ends the game with no user-visible message and may be masked by a simultaneous quit

- **Severity:** medium · **Verdict:** confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/app.cpp:325`

When RunLogic catches an exception it logs CRITICAL and sets `m_logicFailed`. App::Run checks the flag only after the quit checks, then does `return 1` with no dialog (unlike start-up failures, which use ReportStartFailure). If quitRequested, m_quit or --quit-after fires first, the loop breaks and returns 0, so the failure is logged but the exit status says success.

**Evidence:** app.cpp:301-308 sets the flag; app.cpp:317-327 orders the checks quit, quit-after, then logicFailed.

**Impact:** A player sees the window vanish on a simulation crash with no explanation. Automated runs (profile, measure, --quit-after) can report success for a run whose logic died early, producing garbage numbers.

**Recommendation:** Check `m_logicFailed` first and return 1 regardless of quit. Show the failure through the platform dialog path used for start-up.

**Verification:** app.cpp:317-327 checks quit and quit-after before m_logicFailed, so a failure that coincides with a quit returns 0. The failure branch itself does a bare `return 1` with only the CRITICAL log, and main.cpp uses platform::ReportStartFailure only for start-up failures. The flag is atomic, so there is no race. The masking needs a simultaneous quit, but a failure during a --quit-after run then reports success, which matters for measure and profile.

### APP-MAIN-02: Data race: logic thread copies m_options.data while the main thread's hot reload may assign it

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** thread-safety · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/app.cpp:291`

RunLogic builds `Game` with `.data = m_options.data` (line 291) on the logic thread, after the thread starts. WatchData, called from App::Run on the main thread from the first iteration, does `m_options.data = *gameData` (line 493) when files in the data folder change. The first check is due immediately (`m_dataCheckNs` = 0). Nothing synchronises the two.

**Evidence:** app.cpp:272 starts the jthread in the constructor body; app.cpp:289-291 reads m_options.data on that thread; app.cpp:493 writes m_options.data on the main thread. HOT_RELOAD is true in debug builds.

**Impact:** Debug builds only. If a data file is saved while the logic thread is starting, the copy of a GameData with shared_ptr members (world, names) races with the assignment. That is undefined behaviour: torn pointers, a refcount race, or a crash. It is more likely than it looks because the data folder is rescanned every 0.5 s and editors write several files.

**Recommendation:** Hand the logic thread its own copy made before the thread starts (a by-value capture, or a GameSetup built in the constructor). Alternatively start the data watch only after the first snapshot has been drawn, or guard the running data with a mutex. Add a TSan run or test that edits a file during start-up.

**Verification:** app.cpp:289-291 the logic thread reads m_options.data, and app.cpp:493 the main thread assigns it in WatchData. Nothing synchronises them, and m_options.data is a plain member. The window is narrow: a reload needs a data file to change within the logic thread's start-up. Debug builds only (HOT_RELOAD). It is real UB, but the likelihood is lower than the finding says, so I rate it low.

### APP-MAIN-03: No tests for main.cpp or the App orchestration

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/app.cpp:311`

tests/unit/app covers the pure helpers (command-line parsers, DataWatch, Keep*/ReloadOf, view maths) and Game::Step. App::Run, HandleInput, LoadScene, Present, WatchData, FollowPath and SendHeld are untested, as are main.cpp's ChooseScene and QuitAfterNs, --quit-after parsing (absent from command_line_tests), and the Game::Run, stop-token and m_overloaded paths.

**Evidence:** command_line_tests.cpp has cases for burst, debug-sight, zoom, workers, travel-share and month-step-at only. game_tests.cpp has 4 cases, all single Step calls with a fake clock.

**Impact:** A change to the scene load/report sequence, the quit and failure ordering, pause and resume, or the hot-reload orchestration passes test and lint and breaks only at run time. The race and ordering issues in these findings are exactly what such tests would catch.

**Recommendation:** Extract the orchestration pieces that don't need SDL and test them: the load/report state machine of LoadScene, the quit and failure decision, the input-to-action mapping of HandleInput. Add --quit-after and ChooseScene cases. Add a Game::Run test with a stop token and a fake clock.

**Verification:** grep of tests/unit finds no --quit-after case, so that part holds. App::Run and the other orchestration code need SDL and a window, so leaving them untested is a plausible trade-off, not necessarily a defect. Extracting the quit and failure decision (finding 1) would be cheap to test. I found no recorded decision on it.

### APP-MAIN-04: Casts are press-only: held input never repeats and the `pressed` parameter is dead

- **Severity:** low · **Verdict:** unverifiable · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/app.cpp:533`

HandleInput skips every non-pressed control (`!control->pressed` continues), so a release never reaches Cast. Cast() passes `control.pressed` (always true here) to CastAction, which tests `!pressed`. Holding a button never auto-fires the generator.

**Evidence:** app.cpp:533 and 624; cast.cpp:45-47.

**Impact:** The bullet-heavy combat the project targets would need repeated clicks, an accessibility cost (repetitive strain). The dead parameter suggests a hold feature that doesn't exist.

**Recommendation:** Decide on hold-to-cast (as a setting, never changing difficulty) and send held-slot state through HeldInput. Otherwise remove the `pressed` parameter and check as dead code.

**Verification:** I did not open cast.cpp, so I could not check whether the `pressed` parameter is dead. app.cpp:533 does skip non-pressed controls, which fits the claim. Whether hold-to-cast is wanted is a game-design decision. The repo prioritises accessibility, but a hold feature is not recorded as planned or rejected.

### APP-MAIN-05: Command-line parsing duplicated five times and unknown flags are ignored

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** duplication · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/app.cpp:125`

ZoomOf, WorkersOf, TravelShareOf, MonthStepAtOf (app.cpp) and QuitAfterNs (main.cpp) each repeat the same find flag, take next arg, from_chars, check end and error pattern. QuitAfterNs and ChooseScene live in main.cpp, the rest in app.cpp. Nothing rejects an unknown or misspelled flag, or a flag whose value is another flag.

**Evidence:** Identical next-arg and from_chars blocks at app.cpp:130,146,161,180; main.cpp:44.

**Impact:** Drift risk between the copies. A typo such as `--quit-aftr 30` runs the game forever, and measure or profile would then hang.

**Recommendation:** One templated ValueOf<T>(args, name, validate) helper in a command-line header and one unknown-flag check against a table of known flags. Move all parsing out of main.cpp so it is testable.

**Verification:** The scattered parsers are real: QuitAfterNs in main.cpp:40-46 and the others in app.cpp. Unknown flags are ignored. The claim that a typo makes measure or profile hang is overstated, because those commands pass the flag themselves. The code duplication is minor.

### APP-MAIN-06: Hover pick scans every instance every frame on the main thread

- **Severity:** low · **Verdict:** unverifiable · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/app.cpp:745`

Hovered() runs PickInstance (a linear scan with per-instance half-float unpacking and a rotation test) once per drawn frame, even when the pointer hasn't moved and the drawn snapshot and alpha are unchanged.

**Evidence:** app.cpp:883 calls Hovered each drawn frame; view.cpp:65-99 iterates every instance backwards.

**Impact:** The stress scene has about 100k instances, and the world map grows with city count. Unpaced runs at several hundred fps repeat the scan for no new result, against the repo's avoid-wasted-work rule.

**Recommendation:** Skip the scan when pointer, camera, snapshot and fog are unchanged, or bin instances spatially at publish time. Measure first, as the repo prefers.

**Verification:** app.cpp:883 does call Hovered(...) once per drawn frame, so the call pattern is as stated. Whether the scan costs enough to matter needs a profile. The repo's rule is to measure first.

### APP-MAIN-07: Start-up copies GameData repeatedly and the window sits unresponsive during data load

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/main.cpp:67`

The window is opened first (line 69) and every data file is then parsed with no event pump. core::GameData is copied from *gameData into options.data, again into the Game setup (app.cpp:291), and again on every reload into a make_shared.

**Evidence:** main.cpp:69-140 loads manifest, game data, effects, seals, animations, strings, world map, names and look after the window exists; main.cpp:90 `options.data = *gameData`.

**Impact:** On a large procedural world map the window can show as not responding or blank for seconds. Extra copies cost time and memory in proportion to the data size.

**Recommendation:** Load the data first and open the window after it (or show a loading frame). Move the data into Options and Game, or share it through shared_ptr<const GameData>.

**Verification:** main.cpp:66-69 opens the window before the data loads, as stated. The data copies are real, but the blank-window impact depends on data size and I did not measure it. The ordering may be deliberate, since a failure is reported through the window dialog path. The impact is speculative.

### APP-MAIN-08: --quit-after counts from App construction, including scene load time

- **Severity:** low (reviewer said info) · **Verdict:** confirmed · **Category:** misleading-semantics · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/app.cpp:321`

m_startNs is set in the constructor. --quit-after N measures wall time from there, so the first scene's load and world generation use up part of the N seconds. For profile --seconds N the recorded steady state is shorter and varies with load time.

**Evidence:** app.cpp:243 and 321.

**Impact:** Profile runs capture different amounts of steady state across machines. measure uses --camera-path, which starts after the scene runs, so it is unaffected.

**Recommendation:** Start the quit-after clock at the first Running snapshot, as FollowPath does for the camera path, or document the behaviour.

**Verification:** m_startNs is set in the App constructor initializer list (app.cpp:243). The quit-after check at app.cpp:~321 uses NowNs() - m_startNs, so scene load time counts against N. Because the load happens in the loop after construction, steady-state time varies. Harmless for the measure harness, which uses --camera-path. It only affects profile --seconds accuracy.

### APP-MAIN-09: DataWatch can throw from directory iteration and does not watch subfolders

- **Severity:** low · **Verdict:** confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/app/app.cpp:112`

List() uses a range-for over directory_iterator{folder, error}. Only the constructor takes the error code; operator++ is the throwing overload and can throw filesystem_error if the directory changes mid-iteration. The repo rule is that our code throws nothing. The listing is also non-recursive, so edits in a subfolder of assets/data would never reload.

**Evidence:** app.cpp:114; `find assets/data -type d` shows no subfolders today.

**Impact:** A throw from WatchData would unwind App::Run and end the game (debug builds only). Subfolders are not a problem today but are a latent trap if data is split into folders.

**Recommendation:** Iterate with an explicit loop and increment(error). Use a recursive iterator if subfolders are ever added.

**Verification:** app.cpp:114 uses a range-for over std::filesystem::directory_iterator{folder, error}. Range-for calls the throwing operator++, so a filesystem_error is possible if the directory changes mid-iteration. entry.last_write_time(unreadable) does use the error-code overload. The listing is not recursive. The throw would escape through App::Run and be caught by the catch in main.cpp, which logs CRITICAL and returns 1, so the game stops. This applies to debug builds only (the data watch), and the race is very rare.

### APP-MAIN-10: Escape quits the game instantly with no confirmation or pause menu

- **Severity:** info (reviewer said low) · **Verdict:** deliberate · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/app.cpp:536`

Control::Quit (Escape by default) sets `m_quit` immediately. There is no confirmation, no autosave and no menu.

**Evidence:** app.cpp:536-538.

**Impact:** An accidental Escape, easy for players with motor impairments, loses unsaved progress. The repo lists accessibility as its top priority.

**Recommendation:** Route Escape to a pause or menu overlay (or a confirm step) before there is real progress to lose. Keep the immediate quit for a debug flag only.

**Verification:** The phase 6 chunk 10 screens spec (docs/superpowers/specs/2026-11-10-phase6-chunk10-screens-design.md, lines 53-55 and 160) plans a pause menu that Escape opens, with quitting with unsaved progress asking once. The immediate quit at app.cpp:536 is the interim state until that chunk lands.
