# Text layout budget Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Lay out new label text within 2 ms a frame, in turn order, and never lay out a label too small to read, so a camera cut or a month step meeting thousands of new names no longer spikes a frame.

**Architecture:** `gfx::TextLayout` gains a budgeted lookup (`TryLay`), a free one that never makes a layout (`Cached`), a settable budget and eviction by the frame clock. The renderer's label gathering (`PlaceTexts`, `LabelOf`, `MeasureStacks`) serves cached labels at once, leaves a too-small label without a layout out, and lays the rest out in turn order (two pure helpers in `gfx/labels.hpp`) until the budget is spent; `Renderer::Capture` lifts the budget.

**Tech Stack:** C++23, doctest, SDL3_ttf (through `gfx::TextLayout`), XMake through `pixi run project_n`.

**Spec:** `docs/superpowers/specs/2026-11-20-text-layout-budget-design.md`

## Global Constraints

- `LAYOUT_BUDGET_NS` = 2 ms (`2 * core::NS_PER_MS`), measured from the frame's first new layout; at least one new layout a frame.
- `LAYOUT_SECONDS` = 5 s of the frame clock replaces `LAYOUT_FRAMES` (300).
- Too small to read: `pixels < LABEL_MIN_PIXELS × text scale`, the rule `LabelPass` already applies.
- Turn order: priority highest first (`core::LabelPriority`: `Selected`, `Hovered`, `Player`, `Place`, `Named`, `Rest`), then nearest the screen's middle, then by entity.
- Captions, damage numbers and the HUD keep `TextLayout::Lay`, never budgeted; `Renderer::Capture` draws with the budget lifted.
- Presentation only: nothing here touches `core`/`game` simulation, hashes or saves.
- Small unit tests: about 20 representative samples a case, never sweeps; tests never depend on timing (budgets of zero and unlimited only).
- Logging: outcomes only, never per frame. Code comments say why, never cite specs, tasks or design docs.
- Gates: an edit-build-test round runs `pixi run project_n test --suite gfx_tests [--test <t>]`; a task ends with `pixi run project_n test` and `pixi run project_n lint --range <task base>..HEAD`.
- Commits: Conventional Commits; `feat`/`fix` only for game code (this is engine behaviour in `src/`: `perf(gfx)` where nothing visible changes, `feat(gfx)` for labels arriving over frames); scopes from `SCOPES` (git-commit skill).

## Review Focus

1. A clock that goes back (a capture's fixed view, a scene change) never forgets a layout in use and never stops eviction → Task 1's case "a clock that goes back forgets nothing in use".
2. A cached layout still waiting for a glyph, while the budget is spent: served as it is (placed, faded out), never treated as missing → Task 1's case "a cached incomplete layout is served while the budget is spent".
3. HUD and captions once the label budget is spent: `Lay` still makes them → Task 1's case "Lay still makes a layout once the budget is spent".
4. A brand-new selected NPC in a crowd of new names, far from the middle: its label goes first → Task 2's case "a selected label goes first however far from the middle".
5. A larger or smaller text size setting moves the readable line with it (accessibility) → Task 2's case "readability follows the text scale".

## Waves

| Wave | Tasks | Needs |
|---|---|---|
| A | 1 | nothing |
| B | 2 | Task 1's `TryLay`, `Cached`, `SetLayoutBudget`, `EndFrame(jobs, clock)` |
| C | 3 (controller) | Tasks 1 and 2 on the branch; runs alone (`measure` takes the display) |

---

### Task 1: the layout cache's budget, free lookup and eviction by the clock

review: full · implementer: sonnet

**Files:**
- Modify: `src/gfx/text_layout.hpp` (constants at lines 28-37, `Lay`/`EndFrame` at 117-128), `src/gfx/text_layout.cpp` (`CachedLayout` ~222, `Impl` ~264, `Lay`/`EndFrame` ~495-518)
- Modify: `src/gfx/renderer.cpp:423` (`text->EndFrame(jobs, shown.view->frameClock)`), `src/gfx/text_place.hpp:116` (its doc names `EndFrame()`; keep it true)
- Test: `tests/unit/gfx/text_layout_tests.cpp` (existing `EndFrame(jobs)` calls take a clock)

**Interfaces:**
- Produces, in `gfx/text_layout.hpp`:
  - `inline constexpr u64 LAYOUT_BUDGET_NS = 2 * core::NS_PER_MS;` (include `core/tick_clock.hpp`) and `inline constexpr f64 LAYOUT_SECONDS = 5.0;` replacing `LAYOUT_FRAMES`.
  - `[[nodiscard]] const Layout* TryLay(std::string_view text, core::TextDirection direction);` as `Lay`, except that once this frame's budget is spent it makes nothing: a text not cached gives `nullptr`, and a cached incomplete one whose glyph has landed is given as it is (made again on a later frame). The budget is spent when the time since the frame's first `TryLay` that made a layout reaches the budget, checked after each make (`std::chrono::steady_clock`, as `Prewarm` times itself), so a frame always makes at least one.
  - `[[nodiscard]] const Layout* Cached(std::string_view text, core::TextDirection direction);` the cached layout or `nullptr`, never making one; it counts as asked for (eviction).
  - `void SetLayoutBudget(u64 ns);` the budget for `TryLay` from now on, `LAYOUT_BUDGET_NS` until set; `std::numeric_limits<u64>::max()` lifts it.
  - `void EndFrame(core::Jobs& jobs, f64 clock);` `clock` is the frame clock in seconds (`View::frameClock`) of the frame that begins; forgets layouts not asked for since more than `LAYOUT_SECONDS` before `clock`; resets the frame's budget. A layout is stamped with the clock of the frame it was asked for in (the last `EndFrame`'s; 0 before the first).
  - `Lay` never counts against the budget nor checks it.

- [ ] **Step 1: Write the failing tests** in `tests/unit/gfx/text_layout_tests.cpp` (`OpenFonts()`, `core::Jobs jobs{ 1 }`, prewarmed Latin texts so `complete` holds):
  - "a spent budget makes one layout a frame and serves cached ones": `SetLayoutBudget(0)`; `TryLay("Alpha")` non-null; `TryLay("Beta")` null; `TryLay("Alpha")` non-null; `Cached("Beta")` null; `EndFrame(jobs, 0.1)`; `TryLay("Beta")` non-null, `TryLay("Gamma")` null.
  - "an unlimited budget lays out every text": `SetLayoutBudget(max)`; ten distinct texts all non-null in one frame.
  - "Cached never makes a layout": `Cached("Delta")` null, again null; after `Lay("Delta")`, non-null and equal in width to it.
  - "Lay still makes a layout once the budget is spent": `SetLayoutBudget(0)`; `TryLay("A1")`; `TryLay("B1")` null; `Lay("B1").width > 0`; then `TryLay("B1")` non-null (cached).
  - "a cached incomplete layout is served while the budget is spent": `TryLay("鹤")` (not prewarmed: incomplete); `EndFrame`, `Flush`; `SetLayoutBudget(0)`; `TryLay("Epsilon")` (spends it); `TryLay("鹤")` non-null and still `!complete`; `EndFrame`; next frame's first `TryLay("鹤")` is complete.
  - "a layout unused for five seconds is forgotten": `Lay("Zeta")` in the frame of clock 0; `EndFrame(jobs, 4.0)`; `Cached("Zeta")` non-null (now stamped 4.0); `EndFrame(jobs, 8.9)`: `Cached` non-null; `EndFrame(jobs, 14.0)` (unused since 8.9): `Cached` null.
  - "a clock that goes back forgets nothing in use": `EndFrame(jobs, 100.0)`; `Lay("Eta")`; `EndFrame(jobs, 2.0)`; `Cached("Eta")` non-null; `EndFrame(jobs, 3.0)`; `Cached("Eta")` non-null.
- [ ] **Step 2: Run** `pixi run project_n test --suite gfx_tests --test "*budget*"` → FAIL to compile (no `TryLay`).
- [ ] **Step 3: Implement** the interface above; `CachedLayout::lastUsed` becomes the frame clock (`f64`); the existing callers' `EndFrame(jobs)` take a clock (tests: increasing values a frame apart, e.g. `1.0 / 60.0` steps).
- [ ] **Step 4: Run** `pixi run project_n test --suite gfx_tests` → PASS.
- [ ] **Step 5: Gate and commit:** `pixi run project_n test`, `pixi run project_n lint --range <task base>..HEAD`; commit `perf(gfx): give text layouts a per-frame budget and forget them by time`.

---

### Task 2: labels laid out in turn within the budget, none too small to read

review: full · implementer: opus

**Files:**
- Modify: `src/gfx/labels.hpp`, `src/gfx/labels.cpp` (the readable rule at lines 115 and 279 through `Readable`; the order helper)
- Modify: `src/gfx/renderer.cpp` (`PlaceTexts` 558-620, `MeasureStacks` 674-697, `LabelOf` 719-747, `Capture` 1305-1330)
- Test: `tests/unit/gfx/labels_tests.cpp`; `pixi run project_n reference` (no picture moves)

**Interfaces:**
- Consumes: Task 1's `TryLay`, `Cached`, `SetLayoutBudget`, `LAYOUT_BUDGET_NS`.
- Produces, in `gfx/labels.hpp`:
  - `[[nodiscard]] bool Readable(f32 pixels, f32 scale);` true when `pixels >= LABEL_MIN_PIXELS * scale`; `LabelPass` uses it at both places.
  - `struct WaitingLabel { std::size_t item = 0; core::StableId entity{}; core::LabelPriority priority = core::LabelPriority::Rest; core::Vec2 anchor{}; };` a label whose name or stack text isn't laid out yet; `item` indexes the caller's list.
  - `void OrderWaiting(std::span<WaitingLabel> waiting, core::Vec2 middle);` sorts by priority highest first, then squared distance of `anchor` to `middle` smallest first, then `entity` ascending.

Behaviour of the renderer (the reference pictures and `measure` prove it; the helpers carry the unit tests):
- Each label item's priority (raised for selected and hovered, as `LabelOf` does) and anchor are worked out before any layout; `LabelOf` builds the `LabelInput` from a layout it is given.
- Not `Readable` at the frame's pixels and text scale: the name from `Cached` only; none → the label is left out of `labelInputs` this frame; one → it goes in with `stackWidth` 0 and no stack text measured (a too-small label covers no room and joins no stack in `LabelPass`).
- Readable, name and stack text both cached: in at once. Otherwise it waits; the waiting are ordered by `OrderWaiting` (middle: half the screen's width and height) and each takes its name then its stack text through `TryLay`, before the next label's, until one gives `nullptr`: that label and every later one stay out this frame. The per-name memo of stack widths stays (each distinct name's stack text measured once a frame).
- A label left out gives `LabelPass` nothing, so it never takes room; once in, it fades in as any new label.
- `Capture` calls `SetLayoutBudget(std::numeric_limits<u64>::max())` before it places texts and `SetLayoutBudget(LAYOUT_BUDGET_NS)` after, so every picture has every label.

- [ ] **Step 1: Write the failing tests** in `tests/unit/gfx/labels_tests.cpp`:
  - "a label is readable from the minimum size up": `Readable(LABEL_MIN_PIXELS, 1.0F)`; `!Readable(LABEL_MIN_PIXELS - 0.5F, 1.0F)`; `Readable(13.0F, 1.0F)`.
  - "readability follows the text scale": at scale 1.5, `!Readable(13.0F, 1.5F)`, `Readable(13.5F, 1.5F)`; at scale 0.75, `Readable(7.0F, 0.75F)`.
  - "waiting labels go highest priority first, then nearest the middle": five labels, priorities `Rest`, `Named`, `Rest`, `Named`, `Place`, at distances 10, 300, 50, 20, 400 from a middle (640, 360); the order is the `Place`, then `Named` at 20, `Named` at 300, then `Rest` at 10, `Rest` at 50 (check `item` indices).
  - "a selected label goes first however far from the middle": a `Selected` at (0, 0) among four `Rest` labels within 30 px of the middle: it is first.
  - "equal turns fall back to the entity": two `Rest` labels at the same distance, entities 9 and 4: 4 first.
- [ ] **Step 2: Run** `pixi run project_n test --suite gfx_tests --test "*label*"` → FAIL to compile.
- [ ] **Step 3: Implement** `Readable` and `OrderWaiting`, use `Readable` in `LabelPass`, then the renderer's behaviour above.
- [ ] **Step 4: Run** `pixi run project_n test --suite gfx_tests` → PASS; `pixi run project_n reference` → PASS with no picture updated (a moved picture means a label went missing or changed: find out why, never `--update`).
- [ ] **Step 5: Look at it:** `./dev run -- --world`, zoom out to 0.25 and back in, let a month pass (M): names fade in over a few frames at readable zooms, never with gaps, and the player's and places' labels never wait (look-at-the-game skill for a screenshot if anything looks off).
- [ ] **Step 6: Gate and commit:** `pixi run project_n test`, `pixi run project_n lint --range <task base>..HEAD`; commit `feat(gfx): lay out new labels in turn within a frame budget`.

---

### Task 3: measure, and record the outcome (controller, inline)

- [ ] **Step 1:** On main's head (a `measure` from the main checkout before the branch, or the kept `build/measure/20261120-002525/` run if the setup matches) and on the branch head: `pixi run project_n measure --runs world --keep`, alone, nothing else running (the dev-PC-monitors note: fullscreen lands on the focused monitor).
- [ ] **Step 2:** From the kept branch capture: the widest view's cut-in and the month step have no frame led by `Make`; `Make` self time per frame at most about 2 ms plus one layout; hitches not up; median frame unchanged against main.
- [ ] **Step 3:** Write the spec's **Outcome** (the numbers before and after, any ruling with its measurement), mark the spec implemented, delete the backlog entry "Text layout hitch when many new names enter the view at once" from the project memory's `sweep-up-backlog.md`; commit `docs(design): record the text layout budget's outcome`.
