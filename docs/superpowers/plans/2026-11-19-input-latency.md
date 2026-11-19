# Input latency Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pick each frame's snapshot after the swapchain wait, make the display delay D adapt to measured snapshot lateness, and make `measure` report many presses' latency, all proven on a synthetic timeline.

**Architecture:** A pure `core::DisplayDelay` follows the 99th percentile of recent snapshot lateness; `core::PresentFrame` takes over the frame's order (acquire, one clock reading, D, display update, alpha) so `App::Present` and a fake-clock test harness run the same code. The camera path's scripted walk becomes stop-start, and `measure` summarises the presses.

**Tech Stack:** C++23, doctest, XMake through `pixi run project_n`, Python (tools/project_n.py, unittest).

**Spec:** `docs/superpowers/specs/2026-11-19-input-latency-design.md`

## Global Constraints

- D is presentation only: never hashed, saved, recorded or sent to the logic thread.
- D's starting values: start 10 ms, window 600 snapshots, 99th percentile, margin 1 ms, floor 2 ms, ceiling `core::TICK_NS`, falling at most 2% of elapsed time, rising at most 10%. A task may tune one only with the reason recorded in its report.
- Adoption rule unchanged (0002 §6): adopt when `now >= scheduledNs + D`, the held one before a newer one; the held and drawn snapshots use the same D at each moment.
- Small unit tests: about 20 representative samples a case, never sweeps.
- Logging: outcomes only, never per frame. Code comments say why, never cite specs, tasks or design docs.
- Gates: an edit-build-test round runs `pixi run project_n test --suite <s> [--test <t>]`; a task ends with `pixi run project_n test` and `pixi run project_n lint --range <task base>..HEAD`.
- Commits: Conventional Commits; `feat`/`fix` only for game code; scopes from `SCOPES` in tools/project_n.py (git-commit skill).

## Review Focus

1. The first frame before any snapshot: no acquire without a draw (`Renderer::Acquire` asserts on a second acquire before a draw) → Task 2's case "a frame before any snapshot is due acquires nothing".
2. A hidden (minimised) window: no acquire, events still play and D still advances → Task 2's case "a hidden frame acquires nothing and still adopts and plays".
3. A snapshot published later than D (a hitch, a month step): late adoption counted, D rises, motion resumes without going back → Task 4's case "a lateness spike raises D and leaves no late adoption once risen".
4. Time standing still or a first `Advance`: D never moves on zero or the first elapsed time, never divides by zero → Task 1's case "the first advance and a repeated time leave D where it is".
5. A hitch of seconds between frames (debugger, suspend): D moves to its target, no further, never past floor or ceiling → Task 1's case "a long gap moves D to its target, never past it".
6. A main thread that stops taking snapshots for many ticks (a window drag, a debugger): the queue keeps only the newest two, publishing never runs out of buffers, and once frames resume the newest due one is drawn → Task 7's cases "a third publish drops the oldest waiting snapshot" and "buffers return to the handover, so publishing never runs out".

## Waves

| Wave | Tasks | Needs |
|---|---|---|
| A | 1, 5 | nothing (core delay; camera path and `measure`, no shared file) |
| B | 2 | Task 1's `DisplayDelay` |
| C | 3, 4 | Task 2's `PresentFrame` (app wiring; core test harness, no shared file) |
| C2 | 7 | Task 4's harness, whose 60 Hz render-time case it turns green: in Task 4's worktree, rebased onto the branch head first; Task 4 and Task 7 are then reviewed side by side |
| D | 6 | everything, Task 7 included (docs, `measure` run, Outcome) |

---

### Task 1: `core::DisplayDelay`, the adaptive display delay

review: full · implementer: sonnet

**Files:**
- Create: `src/core/display_delay.hpp`, `src/core/display_delay.cpp`
- Modify: `src/core/tick_clock.hpp:28-30` (rename `DEFAULT_DISPLAY_DELAY_NS` to `DISPLAY_DELAY_START_NS`, doc: the delay D starts at before any lateness is known; update every user: `src/core/frame.hpp`, `src/app/app.cpp`, `src/app/view.hpp`, `tests/unit/core/tick_clock_tests.cpp`, `tests/unit/core/frame_tests.cpp`)
- Test: `tests/unit/core/display_delay_tests.cpp`

**Interfaces:**
- Produces, in `core/display_delay.hpp`:
  - `inline constexpr u64 DISPLAY_DELAY_FLOOR_NS = 2'000'000;` `DISPLAY_DELAY_MARGIN_NS = 1'000'000;` `std::size_t DISPLAY_DELAY_WINDOW = 600;` `DISPLAY_DELAY_SETTLED = 60;` (samples before D may fall: the first second after a start or load is slow and unrepresentative); `f64 DISPLAY_DELAY_FALL_RATE = 0.02;` `DISPLAY_DELAY_RISE_RATE = 0.10;` ceiling is `TICK_NS`.
  - `class DisplayDelay { public: void Observe(u64 latenessNs); u64 Advance(u64 nowNs); [[nodiscard]] u64 Current() const; [[nodiscard]] u64 Lowest() const; [[nodiscard]] u64 Highest() const; [[nodiscard]] u64 Target() const; };`
  - `Observe`: records one snapshot's lateness (`publishedNs - scheduledNs`, the caller passes 0 for an early one) in a ring of `DISPLAY_DELAY_WINDOW`, and recomputes the target: `clamp(p99 + MARGIN, FLOOR, TICK_NS)` (p99 is the sorted samples' value at index `floor(0.99 × n)`, by `std::nth_element` on a copy, here, ~60 times a second, never per frame). With no samples the target is `DISPLAY_DELAY_START_NS`.
  - `Advance(nowNs)`: moves `Current()` toward the target by at most `RISE_RATE × elapsed` up or `FALL_RATE × elapsed` down (elapsed since the last `Advance`; none on the first call), never past the target; falls only once `DISPLAY_DELAY_SETTLED` samples were observed; returns the new D. Times never go back.
  - `Lowest()`/`Highest()`: the range `Current()` covered since construction (for the shutdown log).

- [ ] **Step 1: Write the failing tests** in `tests/unit/core/display_delay_tests.cpp` (fixed lateness patterns, times in ns from 1 s):
  - "D starts at the start value with nothing observed": `Current() == DISPLAY_DELAY_START_NS`, `Advance(t)` twice 1 s apart leaves it there.
  - "steady lateness brings D to the 99th percentile plus the margin": 600 samples cycling 1.0, 1.2 … 1.8 ms (with 6 of 2.4 ms); `Target() == 2.4 ms + 1 ms`; after `Advance` every 6 ms for 2 s, `Current() == Target()`.
  - "D falls at most 2% of the time passed": settled target 3.4 ms, from 10 ms, one `Advance` 100 ms later gives `10 ms − 2 ms`.
  - "D rises at most 10% of the time passed": settled at 3.4 ms, then 60 samples of 8 ms; `Target() == 9 ms`; `Advance` 10 ms later gives `3.4 + 1.0 ms`.
  - "D doesn't fall before a second of samples": 59 samples of 0, `Advance` 1 s later: still the start value.
  - "floor and ceiling hold": 600 samples of 0 → target `DISPLAY_DELAY_FLOOR_NS`; 600 of 30 ms → target `TICK_NS`.
  - "the first advance and a repeated time leave D where it is": `Advance(t)` then `Advance(t)` after a target change: unchanged.
  - "a long gap moves D to its target, never past it": settled target 3.4 ms, `Advance` 10 s later → exactly 3.4 ms.
  - "Lowest and Highest cover the range D took".
- [ ] **Step 2: Run** `pixi run project_n test --suite core_tests --test "*D *"` → FAIL (no `core/display_delay.hpp`).
- [ ] **Step 3: Implement** the class as above, the rename across its users, (xmake.lua globs `src/core/**.cpp`, so the new file needs no listing).
- [ ] **Step 4: Run** `pixi run project_n test --suite core_tests` → PASS.
- [ ] **Step 5: Gate and commit:** `pixi run project_n test`, `pixi run project_n lint --range <task base>..HEAD`; commit `refactor(core): add an adaptive display delay` (the rename may be its own `refactor(core)` commit first).

---

### Task 2: the frame's order in `core::PresentFrame`

review: full · implementer: opus

**Files:**
- Modify: `src/core/handover.hpp` (`Display`), `src/core/frame.hpp:510-533` (`PresentFrame`)
- Test: `tests/unit/core/frame_tests.cpp`, `tests/unit/core/handover_tests.cpp`

**Interfaces:**
- Consumes: Task 1's `core::DisplayDelay` (`Advance`, `Observe`, `Current`).
- Produces:
  - `Display::TakenLateness()`: `[[nodiscard]] std::optional<u64> TakenLateness()`: the lateness (`publishedNs − scheduledNs`, 0 when published before its time) of the snapshot the last `Update` took from the handover, if it took one; reading clears it. `Update`'s signature stays.
  - `template<typename Acquire, typename Now, typename Prepare, typename Play, typename Draw> bool PresentFrame(GameDisplay& display, GameHandover& handover, DisplayDelay& delay, std::vector<Event>& pending, bool visible, Acquire acquire, Now now, Prepare prepare, Play play, Draw draw)`, in order:
    1. while `visible` and `display.Drawn()` is set, `acquire()` (may block);
    2. `t = now()` once; `d = delay.Advance(t)`; `display.Update(handover, t, d)`; a `TakenLateness()` goes to `delay.Observe`;
    3. no drawn snapshot: return true (nothing acquired, as 1 skipped);
    4. `prepare(drawn)` (App: scene loading, reports, epoch); false → return false;
    5. while `visible` and 1 didn't acquire (the first snapshot), `acquire()` now;
    6. `alpha = Alpha(drawn.scheduledNs, t, d, drawn.advanced)`; `PlayDue(...)`; while `visible`, `draw(drawn, alpha)`; return true.
  - `prepare`: `bool(const Snapshot&)`; `draw`: `void(const Snapshot&, f64 alpha)`.

- [ ] **Step 1: Write the failing tests** in `frame_tests.cpp` (rewrite its existing cases onto the new signature with a real `GameHandover`/`GameDisplay`, publishing `Snapshot`s with `scheduledNs`, `publishedNs`, `simulationTick`, `advanced`, `follow`):
  - "a snapshot that becomes due during the acquire is the one drawn": drawn snapshot A (tick 1) shown; B (tick 2, scheduled `TICK_NS` later) published and held, due at `S_B + D`; clock `S_B + D − 2 ms` before the acquire, the acquire moves it to `S_B + D + 1.5 ms`; `draw` gets B with `alpha == Approx(1.5 ms / TICK_NS)`.
  - keep "a frame reads the time for alpha after the swapchain acquire" (alpha from the post-acquire reading).
  - "a frame before any snapshot is due acquires nothing": nothing published → `acquire` never called, `draw` never called, returns true.
  - "the first snapshot is acquired for after it is chosen": empty display, one due snapshot → `acquire` called once, after the update; `draw` called.
  - "a hidden frame acquires nothing and still adopts and plays": `visible == false`, a due held snapshot → adopted (`display.Drawn()` is it), event due played, no `acquire`, no `draw`.
  - "a failed prepare draws nothing": `prepare` returns false → `PresentFrame` returns false, `draw` not called.
  - "the delay sees each taken snapshot's lateness": publish with `publishedNs = scheduledNs + 3 ms` → after the frame `delay.Target()` reflects it (one sample: target `3 ms + margin`).
  - `handover_tests.cpp`: "the display reports the lateness of the snapshot it took, once": a late publish → `TakenLateness() == lateness`, then empty; an early one → 0.
- [ ] **Step 2: Run** `pixi run project_n test --suite core_tests --test "*frame*"` → FAIL (signature).
- [ ] **Step 3: Implement** `Display::TakenLateness` and `PresentFrame` as above; keep `App` compiling with the smallest bridge (Task 3 rewires it properly): App may call the new `PresentFrame` with its existing lambdas reshaped, holding a `core::DisplayDelay` member, so the build stays green.
- [ ] **Step 4: Run** `pixi run project_n test --suite core_tests` and `--suite app_tests` → PASS.
- [ ] **Step 5: Gate and commit:** `fix(core): pick a frame's snapshot after the swapchain wait` (tests with it).

---

### Task 3: `App` on the new frame and the adaptive D

review: full · implementer: opus

**Files:**
- Modify: `src/app/app.cpp:315-322` (shutdown log), `:617-635` (`Click`), `:785-860` (`Present`), `src/app/app.hpp` (a `core::DisplayDelay m_delay` member), `src/app/view.hpp:24-30` (cull margin reasoning)

**Interfaces:**
- Consumes: Task 2's `PresentFrame`, `Display::TakenLateness`; Task 1's `DisplayDelay`.
- Produces: the shutdown log line, exactly: `PN_LOG_INFO("display delay: {:.1f} ms at the end, {:.1f} to {:.1f} ms over the run", ...)` (milliseconds), logged next to the late-adoption line. Task 5 parses it.

- [ ] **Step 1: Rewire `App::Present`** onto `PresentFrame`: its `prepare` holds `LoadScene`, the scene report and the epoch reset; its `draw(drawn, alpha)` holds today's body; the hidden window's `m_numbers.Forget` and the transition wait stay after it, on `m_display.Drawn()`. Check `Renderer::ContinueScene` works with a frame acquired (it now runs after the acquire); if it can't, say so in the report and move the acquire as the renderer needs, keeping one clock reading for update and alpha.
- [ ] **Step 2: `App::Click`** computes alpha with `m_delay.Current()`.
- [ ] **Step 3: Shutdown log** as above, from `m_delay.Current()`, `Lowest()`, `Highest()`.
- [ ] **Step 4: `view.hpp`**: the cull margin's "about 43 ms" reasoning names D's ceiling (`core::TICK_NS`) instead of the old constant; re-check the arithmetic stays within the margins it states (D's ceiling is 16.7 ms, larger than 10, so the worst case grows to about 50 ms: confirm the 0.10 and 0.12 shares still hold or adjust the text and constant with the numbers, and add or update the view test if the constant changes).
- [ ] **Step 5: Run** `pixi run project_n test --suite app_tests` → PASS; then `pixi run project_n run -- --world --quit-after 5` and check the log has the display delay line.
- [ ] **Step 6: Gate and commit:** `fix(app): draw each frame's snapshot by the adaptive display delay`.

---

### Task 4: the synthetic frame timeline

review: full · implementer: opus

**Files:**
- Create: `tests/unit/core/frame_timeline_tests.cpp`

**Interfaces:**
- Consumes: Task 2's `PresentFrame`, `GameHandover`, `GameDisplay`; Task 1's `DisplayDelay`.

A single-threaded fake-clock harness (file-local, anonymous namespace): a `Timeline` with the schedule's parameters (`framePeriodNs`, `acquireWaitNs`, a lateness per tick as a function of the tick index, press times) steps simulated time. Ticks are scheduled every `TICK_NS` from a start; tick k's snapshot (`simulationTick = k`, `advanced = true`, `follow` previous/current) is published at `scheduled + lateness(k)` into a real `GameHandover` before any frame reading at or after that time. The followed point stands until the first tick scheduled at or after a press, then moves 4 units a tick (as `Host::Walk`: previous = current, current += 4), standing again at the press's release. Frames start every `framePeriodNs` (unpaced: a period of 1 ms and no wait); each frame's `acquire` advances the clock by `acquireWaitNs`, `now` reads it, `draw` records `(t, drawn.simulationTick, alpha, FollowedCentre)`. An `oldOrder` switch runs today's order instead (`display.Update` at the frame's start, then acquire, then alpha) using the same primitives, to prove the schedules catch it.

- [ ] **Step 1: Write the tests** (each over the representative set: periods 16.67 ms (60 Hz, wait 3.5 ms), 6.06 ms (165 Hz, wait 3.5 ms), 1 ms (unpaced, wait 0); 8 press phases spread over a tick, never a sweep):
  - "a press shows no later than its tick's wait plus D plus a frame": latency (first recorded `t` whose followed point differs from the stood point, minus the press) `<= (tick scheduled − press) + D at that frame + framePeriodNs`.
  - "the schedules catch a snapshot picked before the acquire": with `oldOrder`, at 165 Hz and 60 Hz at least one phase breaks the bound above (the guard that the first case would fail on the old order).
  - "the render time never goes back, holds or jumps": render time `simulationTick − 1 + alpha` between consecutive frames rises by `Δt / TICK_NS` within factors [1 − RISE_RATE − 0.01, 1 + FALL_RATE + 0.01], over 5 s with D falling from its start; with `oldOrder` at 165 Hz the check fails.
  - "steady lateness settles D, with no late adoption": lateness cycling 1.0 to 2.0 ms for 15 s → `delay.Current()` within 0.1 ms of `Target()`, `Target()` = 99th percentile + 1 ms, `display.LateAdoptions() == 0`.
  - "a lateness spike raises D and leaves no late adoption once risen": after settling, 2 s of 6 ms lateness: D reaches ≥ 7 ms; no late adoption in the spike's last second; render time never goes back across it; after 10 s back at 1 to 2 ms, D has fallen back to within 0.1 ms of the target.
- [ ] **Step 2: Run** `pixi run project_n test --suite core_tests --test "*frame*"` → PASS (the `oldOrder` cases prove the harness would have failed before Task 2). If a case fails on the new order, that's a finding: report it with the timeline, don't loosen the bound.
- [ ] **Step 3: Gate and commit:** `test(core): drive frames on a synthetic timeline`.

---

### Task 5: stop-start presses on the camera path, and `measure`'s summary

review: full · implementer: sonnet

**Files:**
- Modify: `src/app/camera_path.hpp:40-50,88-93`, `src/app/camera_path.cpp:84-87`, `tools/project_n.py:2480,2530,2655,2727-2735`, `src/AGENTS.md` (`--camera-path`), `tools/AGENTS.md` (`measure`)
- Test: `tests/unit/app/camera_path_tests.cpp`, `tools/test_project_n.py`

**Interfaces:**
- Produces: `inline constexpr f32 CAMERA_PATH_STEP_SECONDS = 0.5F;` and `CAMERA_PATH_STAND_SECONDS = 0.1F;` in `camera_path.hpp`: in a walking segment Move stands for the first 0.1 s of every 0.5 s and points east for the rest, so the follow segment makes 8 presses (at 0.1, 0.6 … 3.6 s), each from a standing cultivator.
- Consumes: Task 3's log line `display delay: X ms at the end, A to B ms over the run` (parse it; Task 5 may land before Task 3: its test uses a hand-written log).

- [ ] **Step 1: Write the failing tests:**
  - `camera_path_tests.cpp`: rewrite "the camera path holds Move from its start through its follow segment only" as "the camera path walks east in steps through its follow segment only": Move zero at 0.0, 0.05, 0.55, 3.55, 4.0; east at 0.1, 0.3, 0.6, 3.95; zero at 4.05 and `LAST`; and "the follow segment makes eight presses": sampled every 1/600 s over the path, zero-to-east transitions == 8. The portal-reach test stays (its sum overestimates the walk, so it stays safe).
  - `test_project_n.py`: `latency_summary([31.1, 23.0, 25.5])` == `"input latency median 25.5 ms, 23.0 to 31.1 ms over 3 presses"`; one press → `"input latency 31.1 ms over 1 press"`; none → `None`. `display_delay(log)` reads `(2.9, 2.6, 10.0)` from `display delay: 2.9 ms at the end, 2.6 to 10.0 ms over the run`, `None` without it. `measure_table` notes carry the summary and `display delay 2.9 ms (2.6 to 10.0)` instead of one note per latency.
- [ ] **Step 2: Run** `pixi run project_n test --suite app_tests --test "*camera path*"` and `pixi run project_n selftest` → FAIL.
- [ ] **Step 3: Implement** `CameraPathMove` with the step (phase = `fmod(seconds − segment start, CAMERA_PATH_STEP_SECONDS)`, standing while phase < `CAMERA_PATH_STAND_SECONDS`), `latency_summary`, `display_delay` (a `DELAY_RE` beside `LATENCY_RE`), `MeasuredRun` gaining the delay, and the table notes. Update src/AGENTS.md's `--camera-path` text (the walk east in steps of 0.4 s with 0.1 s standing, eight presses, each logging its latency) and tools/AGENTS.md's `measure` lines (latency median and spread, display delay).
- [ ] **Step 4: Run** both suites → PASS.
- [ ] **Step 5: Gate and commit:** `test(app): step the camera path's walk for eight presses` and `build(tools): summarise measure's presses and display delay` (scopes as `SCOPES` allows).

---

### Task 7: the handover queues two snapshots (spec Design 8)

review: full · implementer: opus

**Files:**
- Modify: `src/core/handover.hpp` (`Handover`: class comment, `BUFFERS`, `Publish`, `TakeState`, `PopFree`'s assert, the waiting slot; `Display::Update`, `TakenLateness`, class comment), `src/core/frame.hpp:540-546` (`PresentFrame`'s lateness hand-off, if `TakenLateness`'s shape changes), `docs/design/0002-engine-architecture.md` (§5's state channel row and Buffers bullet, A4's "newest-wins state", a Revised row line)
- Test: `tests/unit/core/handover_tests.cpp`, `tests/unit/core/frame_timeline_tests.cpp` (unchanged; it must go green)

**Interfaces:**
- Consumes: Task 4's harness (`core_tests/frame_timeline`), Task 1's `DisplayDelay::Observe(u64 latenessNs)`, Task 2's `PresentFrame`.
- Produces: `Handover::BUFFERS == 5`; `Handover::TakeState()` returns the oldest waiting snapshot (at most two wait; `Publish` with two waiting returns the oldest to the free list first); `Display::Update(handover, nowNs, delayNs)` unchanged in signature and return (whether a new snapshot was adopted), now adopting the newest due snapshot of all waiting; the latenesses of every snapshot one `Update` took (up to two) reach `DisplayDelay::Observe` once each, through whatever shape the implementer gives `TakenLateness` (say why in the report).

- [ ] **Step 1: Write the failing tests** in `handover_tests.cpp` (`Publish(handover, sequence, scheduledNs, publishedNs)` is the file's helper):
  - Rewrite "the state channel hands over only the newest snapshot" as "two publishes without a take are both handed over, oldest first": publish 1, 2 → `TakeState()` gives 1, then 2, then null. (The old rule changes on purpose: say so in the commit body.)
  - "a third publish drops the oldest waiting snapshot": publish 1, 2, 3 → takes give 2, 3, null.
  - "of two waiting snapshots the display adopts the due one and holds the next": publish 1 at `FIRST_NS`, 2 at `SECOND_NS`; `Update` at `FIRST_NS + HANDOVER_DELAY_NS` → returns true, `Drawn()->sequence == 1`; `Update` at `SECOND_NS + HANDOVER_DELAY_NS` → true, `Drawn()->sequence == 2`.
  - "with two waiting snapshots due, the display adopts the newer and reports both latenesses": publish 1 at `FIRST_NS` published `FIRST_NS + 1000`, 2 at `SECOND_NS` published `SECOND_NS + 2000`; one `Update` at `SECOND_NS + HANDOVER_DELAY_NS` → `Drawn()->sequence == 2`, and the latenesses reported are exactly {1000, 2000}, each once (a second read gives none).
  - "the display doesn't report the lateness of a snapshot whose tick didn't advance": extend with a second, advanced snapshot taken in the same `Update`: only its lateness is reported.
  - "buffers return to the handover, so publishing never runs out": add a variant that publishes `3 * Handover::BUFFERS` snapshots with no `Update`, then one `Update` → the newest due one is drawn and nothing asserts.
- [ ] **Step 2: Run** `pixi run project_n test --suite core_tests --test handover` → the new cases FAIL; `--test frame_timeline` → its render-time case FAILS at 60 Hz (as Task 4 left it).
- [ ] **Step 3: Implement** the queue in `Handover` (two waiting snapshots in publish order, `BUFFERS = 5`, the assert "the main thread holds more than two snapshots" kept), `Display::Update`'s loop (adopt the held one if due; with none held take the next waiting one; repeat until the held one isn't due or none waits; a skipped one goes back undrawn; late adoptions counted per taken snapshot as today), and the lateness hand-off to `DisplayDelay` in `PresentFrame`. Re-derive `Publish`'s stale-event bound ("the render clock reaches at least the newest tick minus one"): with two waiting and one held, find how far behind the newest the render clock can be, correct `STALE_EVENT_TICKS + 1.0`'s margin and every comment stating it (`Publish`'s doc and body, 0002 §5's Events row) so `Publish` never drops an event `PlayDue` would still play; add a case to "undrained cosmetic events too far behind the newest to be played are dropped; others are kept" at the new boundary. Update the class comments and 0002 §5/A4 (state: up to two finished snapshots wait in order, a third drops the oldest; five buffers and why: one written, two waiting, two held), Revised row: `2026-11-19, phase 6 sweep: the state channel keeps two finished snapshots in order instead of only the newest, so a frame never misses one that is still due (§5)`.
- [ ] **Step 4: Run** `pixi run project_n test --suite core_tests` → PASS, `frame_timeline` included: its render-time case passes at 60 Hz in all 8 phases, start-up included, with its bound and code unchanged; its latency cases still pass.
- [ ] **Step 5: Gate and commit:** `pixi run project_n test`, `pixi run project_n lint --range <task base>..HEAD`; commit `fix(core): keep two waiting snapshots so a frame never misses a due one` (test and fix together), and the docs with it or as `docs(design): ...`.

---

### Task 6: design doc, measurement and Outcome

review: light · implementer: controller (the `measure` run takes the display: run it alone, with the user told first)

**Files:**
- Modify: `docs/design/0002-engine-architecture.md` (§6's D paragraph, A3 if it names 10 ms, the Revised row), `docs/superpowers/specs/2026-11-19-input-latency-design.md` (Outcome, status implemented)

- [ ] **Step 1:** 0002 §6: D adapts at run time (the 99th percentile of the last 600 snapshots' lateness plus 1 ms, between 2 ms and a tick, rising at most 10% and falling at most 2% of elapsed time, starting at 10 ms; why a moving D keeps adoption at alpha 1: both snapshots judged by the same D); the frame picks its snapshot after the swapchain acquire from the one clock reading alpha uses; Revised row: `2026-11-19, phase 6 sweep: the display delay adapts at run time; the snapshot is picked after the swapchain acquire (§6)`.
- [ ] **Step 2:** `pixi run project_n measure --runs world` (alone; the user warned). Record the 8 latencies' median and spread, D's final value and range, the late adoptions (log line), against 31.1 ms (5b2) and 23.8 ms (5a), in the spec's Outcome with the rulings from the ledger; note the follow segment's smoothness numbers are stop-start from here on.
- [ ] **Step 3:** `pixi run project_n test`, `pixi run project_n lint --range <task base>..HEAD`; commit `docs(design): record the input latency outcome`.
