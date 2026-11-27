# Acting on the full codebase review: every medium finding, area by area

Status: **approved by the user 2026-11-25** (approach 1, with shared-routes going on beside it; the calls below approved as written; this spec reaches main with R1's pull request). Each area below
gets its own plan (and, where marked, its own short spec) and its own pull request.

## Intent

The full codebase review of 2026-11-25 (`docs/reviews/2026-11-25-full-codebase-review/`) is finished: 1,157 standing
findings, none critical or high, 107 medium. The user wants every medium finding acted on, area by area (2026-11-25),
in the order the review recommends (its README §9: silent failure, Windows and concrete bugs before building more; then
the game-design brainstorm, the generator spec and the chunk 6-11 re-brainstorm). The shared-routes branch, stage 1
half built, carries on beside this work (the user, 2026-11-25: "1 and shared routes").

The 107 findings are about 45 distinct issues (README §4 merges the duplicates). About 60 are code, test or doc
defects with a known fix. The rest are design gaps: the review asks for a decision, a spec or a brainstorm, not a patch.
This spec sorts all 107 into areas, says for each area what "fixed" means, and sets the order.

## History

- The order recorded before the review (the user, 2026-11-13, restated 2026-11-21/22): phase 6, sweep-up, full project
  review, phase 7. The review ran early, before phase 6 finished; the user's words on 2026-11-21: "I want to ensure there
  is also a entire codebase review".
- The game-design brainstorm was placed after chunk 5 and before chunk 6 (the user, 2026-11-16; memory
  `game-design-gaps.md`). Chunk 5 kept growing (GD-ROADMAP-SCOPE-01).
- Already decided and binding here: pause freezes in-game animation (the user, 2026-11-22; memory
  `pause-freezes-game-animation.md`); gamepad gameplay bindings wait for their own piece of work, and casts aiming at
  the mouse need "a small design (followed creature, target, or aim stick)", not a one-line fallback (backlog audit
  item 4); 0002 §7.4 determinism within one binary; saves may break freely before 1.0; fix every finding, Minor
  included; accessibility first; settings never change difficulty.
- In the backlog already (`sweep-up-backlog.md`): audit item 1 (quad-index uploads, GFX-FEATURES-01), audit item 12
  (paused frames still animate), unlogged save refusals (GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-01), the data-driven
  sweep (GD-COMBAT-SYSTEMS-02, GD-TOI-COVERAGE-06, -08), localisation, the D3D12/Metal spikes (GD-ROADMAP-SCOPE-05).
  The backlog misses crash handling, save durability, Windows paths and procedural scaling (GAP1-SWEEP-UP-BACKLOG-VS-CODE-01, -02).
- shared-routes (spec `2026-11-24-shared-routes-design.md`, stage 1 plan `2026-11-25-shared-routes-stage1.md`): stage 1
  adds new `core` files (route graph, segments, route search, place table) and edits `core/paths.*`, `core/journey.*`,
  `src/AGENTS.md` and `tests/AGENTS.md`; stage 2 (journeys as plans, the month step, saves) touches `game/travel.*` and
  `game/host.*`. It replaces the whole-map distance maps (CORE-WORLD-01's cluster).
- Nothing in REFUTED-01.md touches a medium finding.

## Goals

1. Every one of the 107 medium findings, and the 15 low ones the review lists as "before building more" (README §9
   items 3-4; added by the user, 2026-11-25), ends in one of four closings, recorded against its ID in the table at the end:
   - **fixed**: code changed, with a test that fails without the change (bugs in `src/` test-first, the repo rule);
   - **amended**: the doc or spec corrected;
   - **decided**: a spec or brainstorm records a decision on it, naming the ID;
   - **ruled**: the user's written ruling with its reason.
2. A branch that fixes a canonical medium also fixes the low and info findings the README §4 cluster merges into it
   (they are the same issue in the same code).
3. Small areas share this spec as their design and go straight to a plan; areas marked **own spec** get a short
   brainstorm and spec first. That keeps the ceremony in proportion (GD-ROADMAP-SCOPE-15).
4. Areas that share no files run in parallel with each other and with shared-routes (separate worktrees and sessions).

## Non-goals

- The other low and info findings outside a medium's cluster: no area closes them; the low and info pass (step 10 of
  the Order, added by the user 2026-11-26) rules or fixes each.
- The README §7a rulings that touch no medium finding (affliction refresh, speed as difficulty, `-Wpadded` report, …):
  they stand as recorded; a low or info finding one of them answers closes in step 10 as "ruled", citing it.
- Crash dumps (minidumps, crashpad, sentry-native): before external playtests, not now (GD-ROADMAP-SCOPE-08 says the same).
- Autosave: it is a game-design question; the game-design brainstorm (D1) decides it.

## Anti-goals

- A fix branch that redesigns: when a fix turns out to need a design decision, it stops and that decision comes to the user.
- Stand-in UI built only to close a finding and thrown away by chunk 8 or 10 (SPECS-ROADMAP-11).
- Breaking shared-routes' plan: its files are listed above; areas touching them wait for the stage that does.

## Approaches

1. **One roadmap spec, one branch per area (picked by the user).** Small pull requests that fit the gates, parallel where
   files don't overlap, each area's findings closed as it merges.
2. One plan with every code fix as a task on one branch: one enormous final review and `check`, nothing on main until
   all of it is done.
3. Leave the findings in the backlog and fix each when a chunk touches its code: cheapest now, but against the review's
   "fix silent failures and Windows before building more", and the backlog is where these classes were already lost.

## The areas

Each area names its findings (canonical ID first, then the duplicates it closes), the fix, the files, the review level
for its plan's tasks, and whether it needs its own spec. "Test" means a test that fails before the fix.

### Fix areas

**R1. Agent docs to one line per rule** (light review; plan only). X-ARCHITECTURE-08, DESIGN-DOCS-02, GD-ROADMAP-SCOPE-07.
`src/AGENTS.md` is 45 KB in 49 lines, `tools/` 21 KB, `tests/` 19 KB, and every task under `src/` loads the first. Cut each
to one short line per rule with pointers; move the narrative into Doxygen on the classes it describes or into the specs'
Outcomes (nothing is deleted without a home). Make the size rule checkable: `doctor --docs` fails an AGENTS.md over a byte
budget or with a line over a length budget (budgets set by the plan from what the cut files need; the root rule "about 100
lines" becomes a byte budget in 0001 §10.1). First, because every later session pays for the current size.
**Implemented** (Outcome: R1, below).

**R2. Windows paths** (full; plan only). X-UNTRUSTED-INPUT-01, X-CPP-SAFETY-02, X-TEXT-L10N-02, X-UNTRUSTED-INPUT-02,
X-CPP-SAFETY-01. One helper returning a path's UTF-8 bytes (from `u8string()`), used for every log or error argument and
every path handed to SDL or SDL_ttf (`gfx/image.cpp` already converts; share one helper). `lint` rejects `.string()` on a
path in `src/`. A UTF-8 `activeCodePage` manifest for windows-msvc builds (a build change: `check --container`). Test: save,
settings and font loading under a folder named with non-ASCII characters (runs under Wine for windows-msvc).
**Implemented** (Outcome: R2, below).

**R3. Fail loudly** (full; plan only). X-RESILIENCE-02 (+ DESIGN-DOCS-06), X-RESILIENCE-01 (+ CORE-INFRA-01),
X-RESILIENCE-04 (+ APP-MAIN-01, GD-ROADMAP-SCOPE-08's silent exit), X-RESILIENCE-03, GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-02.
- Job chunks and background jobs catch every exception; a chunk's is rethrown on the joining thread (so the logic thread's
  handler sees it), a background job's is logged CRITICAL and reported to its caller. Removes the use-after-free on the
  forking thread.
- The log backend starts with Quill's signal handler; `std::set_terminate` logs the active exception and flushes.
- Runtime fatal paths (logic-thread failure, scene load failure) show the start-up error dialog naming the log folder; the
  logic failure is checked before a simultaneous quit, so its exit status is never masked.
- Device loss: after a run of consecutive failed frames, the same dialog and a clean exit.
- APP-MAIN-02 (low): the debug-build data race on `m_options` at logic start.
- A start-up banner: version and commit, preset and mode, OS, CPU count, GPU name.
- 0002 §12 then says what is true.

**R4. Saves you can trust** (full; plan only). X-DETERMINISM-01 (+ DESIGN-DOCS-04, GD-WORLD-SIM-02), X-RESILIENCE-05
(+ DESIGN-DOCS-05), X-RESILIENCE-06, GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-01, CORE-PERSIST-01, TESTS-GAME-03, TESTS-GAME-04.
- Base identity (decision 1): the save stores the hash of the base world as generated (`WorldHash` of the base, and
  of the scratch base), the load compares it after regenerating and refuses a mismatch naming both values.
  `GENERATOR_VERSION` stays as a fast pre-check.
- Durability: write, flush, sync the file, close and check, rename, sync the folder (POSIX); keep the previous save as a
  backup the load falls back to when the main one is refused as damaged.
- Refusal reasons: `Host::Load` returns why it refused (one reason per check), logs it once at WARNING, and the refusal
  tests assert the reason, not only a failed load (closes TESTS-GAME-03's "passes for the wrong reason").
- The player is told: the save job's result and a load's refusal reason come back to the main thread through the
  handover and show as a short HUD line ("Saved", "Couldn't save: …", "Couldn't load: …"), from `strings.yaml`.
- Tests: the checksum-mismatch path; the committed fuzz seeds are read from `tests/fuzz/corpus/` and loaded.
- Identity of layouts and builds (low): an automatic fingerprint of the component layouts in the save (CORE-PERSIST-03);
  a replay carries a build ID and another build's replay is refused, not reported as diverged (X-DETERMINISM-02).
- Amplification (low): counts are bounded by the bytes left before anything is resized or rebuilt, so a small damaged
  save can't ask for gigabytes (X-UNTRUSTED-INPUT-03, -04, CORE-PERSIST-04).
- Before shared-routes stage 2, which changes the save layout of journeys.

**R5. Game and data bugs** (full; plan only; one task per bug, test-first). GAME-COMBAT-01: a lance cast on the portal tick
ends its use (`EndUses` emits UseEnded for pending strikes and clears them). GAME-HOST-A-01: held movement is clamped to the
map's area on world-map scenes, and a load refuses an off-map place. GD-TOI-COVERAGE-07: `abilities.yaml` refuses
`healing` on an ability that targets enemies and an affliction key that doesn't match the element. DATA-COMBAT-01: one shared
ranged f32 reader in `data/internal` used by abilities, effects and seals. After R4 (both edit `host.cpp`).

**R5b. Low game bugs the review puts first** (full; plan only; one task per bug, test-first). CORE-SIM-01: a month due on a
scene-change tick no longer carries its calendar step across the change. GAME-HOST-B-01: a held route ends with the player
it belongs to. GAME-HOST-A-02: a walk-to route doesn't send the player through the portal's area. GAME-SCENES-02: arena
NPCs keep off the showcase seals (memory: showcases never overlap). GAME-TRAVEL-NPCS-05: `CellAt`'s float-to-integer
conversion is checked. X-CPP-SAFETY-03: a leg's arrive ticks can't wrap u32 (the map is refused first). After R5.

**R6. Rendering bugs** (full; plan only). GAP1-SCENE-TRANSITION-FLOW-01: `core::Logic` stamps every snapshot with a publish
sequence number (never saved, recorded or hashed) and the renderer keys its uploaded snapshot on it, not on
`(epoch, wallTick)`. GFX-FEATURES-01: one shared quad-index-buffer type with recorded and held states, used by meshes,
ribbons and seals, forwarded from `Impl::Submitted`. GFX-COMBAT-DRAW-01: the reduce-motion scale no longer stretches a
seal's appear and fade; a seal's peak opacity under reduced motion is never below its peak at full motion. Low ones the review puts first:
GFX-TEXT-01 (a NUL in a string hangs start-up) and X-UNTRUSTED-INPUT-05 (manifest page geometry checked before any
transfer size is computed: a latent heap overflow). Parallel with R5.

**R7. Pause** (full; **own spec**). GAP1-CLOCK-DISCIPLINE-PAUSE-01, X-ACCESSIBILITY-04, GAP1-CLOCK-DISCIPLINE-PAUSE-02;
it also closes backlog audit item 12. The world's pause stops the simulation tick (orbits, spins, combat, respawns),
not only the world clock, and works in every scene, combat included; UI and map actions still go through, as frozen ticks
already take UI decisions. Paused or unfocused, frames are throttled. The spec settles which clock each animation reads.
After R6 (both touch `core/logic.cpp`).

**R8. Input and settings** (full; plan only). X-ACCESSIBILITY-01: bindings release every held input and zero the sticks
on pad removal and on focus loss. X-ACCESSIBILITY-05 (+ DATA-YAML-CORE-01): `SaveSettings` writes every field, with a
round trip over all of them; `Settings` gains the accessibility fields the hooks already read (text scale, reduce motion,
reduce flashing, stick dead zones). X-ACCESSIBILITY-03: Escape no longer quits at once (a second press within a few seconds
confirms, shown on the HUD); the gamepad's Start does the same; platform gains the Start, East, West and trigger buttons.
DATA-YAML-CORE-08 (low): settings are written to a temporary file and renamed, never
rewritten in place. X-TEXT-L10N-03: the HUD names each control from its binding and the device last used, through one translatable template.

**R9. HiDPI** (full; plan only). PLATFORM-01. The window asks for high pixel density; `WindowState` carries pixel size and
content scale beside the window size, updated on pixel-size and display-scale events; mouse input stays in window units;
the swapchain and text use pixels. After R8 (both edit `platform/window.cpp` and `app`).

**R10. Keyboard and gamepad play** (full; **own spec**). X-ACCESSIBILITY-02 (+ APP-INPUT-UI-01, GD-COMBAT-SYSTEMS-08),
GAP2-COORDINATE-SPACES-AND-PICKING-01, -02, GD-COMBAT-SYSTEMS-07. The aim model (followed creature, target cycling or aim
stick), casting from keys and pad buttons, selecting with the target cursor, the camera following that cursor, and (decision
3) a short cast buffer and an optional hold-to-repeat. After R8.

**R11. Tests and tooling** (light for tests, full for the build flags; plan only). TESTS-CORE-01 (decision 2): pinned bit
patterns for `core::math`, about 20 inputs per function. TESTS-GAME-01: replay sessions default to small data.
TESTS-GAME-02: shared test helpers move into `tests/support/`, variants named. X-TEXT-L10N-01: every codepoint of
`strings.yaml` and `names.yaml` is in the font. GAP2-REPO-CONFIG-FILES-01: `-fsanitize=float-cast-overflow` in the clang
sanitizer builds (`.clang-tidy` untouched: it is the user's). GAP1-THIRD-PARTY-LIBRARY-USAGE-01: Tracy without forced
callstacks or fibers, then a new `measure` baseline (run alone: it takes the display). Also, found during
shared-routes and added by the user (2026-11-25): `project_n test --test <name>` whose name matches no doctest case
runs nothing and reports success (seen with `--test route_graph_tests`); it fails instead, naming the cases it could
have meant (test: `selftest`, beside `test_a_test_file_of_another_suite_is_refused`). Any time; touches no game code.
**Implemented** (Outcome: R11, below).

**R12. Design and roadmap docs** (light; plan only). DESIGN-DOCS-01: 0001 sweep (removed agents and skills, the process
now run by superpowers). DESIGN-DOCS-03: 0002's unbuilt rules marked as such (arenas, command merging). SPECS-ROADMAP-03:
the phase table and chunk 5's statuses. GAP1-SWEEP-UP-BACKLOG-VS-CODE-01, -02: the backlog gets a status column and the
missing classes (in the project memory, not the repo). Last of the fix areas (after R13, the backlog sweep and the low and info pass), so the docs describe what they built.

**R13. Wasted work and a memory measure** (full; plan only). GAP1-THIRD-PARTY-LIBRARY-USAGE-02: `Host::Publish` culls a
body before building its instance, pose and glow. GAP2-MEMORY-BUDGET-VS-0002-S13-02: Tracy plots and a `Bytes()` account
for the game state (world, travel, relationships, paths) and a GPU-memory tally; `measure` reports peak RSS and the GPU
tally. GAP2-MEMORY-BUDGET-VS-0002-S13-01: 0002 §13's game-state line marked "proposed, unmeasured", restated per NPC and
per tile once the account exists; the cuts it shows are measured spikes for D2. After R5 (`host.cpp`).

### Inside shared-routes

**S. Routing at scale.** CORE-WORLD-01 (+ X-ARCHITECTURE-03, X-PERFORMANCE-01, GD-WORLD-SIM-04): replaced by the route
graph (stage 1 builds it, stage 2 switches the game to it); closes when stage 2 merges and its Outcome shows no whole-map
map per city remains. CORE-WORLD-02 (whole-map A* per click) and X-PERFORMANCE-04 (`PublishJourneys` walks every journey)
go into stage 2's plan, which owns the player's route and journeys; if stage 2 leaves them, a task after it takes them.

### Design projects (each a brainstorm, then a spec)

**D1. Game-design brainstorm.** GD-ROADMAP-SCOPE-01, -02, -09, GD-TOI-COVERAGE-01, -02, -03, -04, -05, GD-WORLD-SIM-03,
GD-COMBAT-SYSTEMS-01, -03, -04, -05, -06. Its agenda (README §9 item 8): the persistent player character and where its
state lives, enemy verbs and threat, collision, the Qi loop, NPC growth and life, the time model, autosave, and a
vertical slice. Each finding closes when the design document records a decision on it.

**D2. World generator spec.** GD-WORLD-SIM-01, X-ARCHITECTURE-02, GD-ROADMAP-SCOPE-04, SPECS-ROADMAP-01, and the scaling
that waits for target map sizes: GFX-WORLD-DRAW-01 (+ GFX-RENDERER-01, X-PERFORMANCE-02), X-PERFORMANCE-03
(+ X-ARCHITECTURE-04). The generator at run time, world identity (seed, generator version, data hash), how a new map reaches
the renderer, the target sizes, and per-view or per-region ribbons, fog and textures, each open size a measured spike.
After D1.

**D3. Architecture before content.** X-ARCHITECTURE-01 (Host split), X-ARCHITECTURE-05 (scene parameters),
X-ARCHITECTURE-06 (+ GD-TOI-COVERAGE-06: factions, per-caster loadouts, creature table), X-ARCHITECTURE-07 (NPC tiers on
the world map), GD-COMBAT-SYSTEMS-02 and GD-TOI-COVERAGE-08 (the data-driven sweep: abilities and sprites as data). Shaped
by D1 and D2; before chunk 6 adds content.

**D4. Chunks 6-11 re-brainstormed as a set.** GD-ROADMAP-SCOPE-03, -06, GD-TOI-COVERAGE-09, SPECS-ROADMAP-02, -04, -05, -06.
Against D1 and D2: Accessibility sections in every chunk, the chunk 9 roster bound, settings.yaml ownership, keyboard and
gamepad window moving, and whether the UI spike (S13) and chunk 8 move earlier.

**D5. Real Windows and macOS GPUs.** GD-ROADMAP-SCOPE-05: S9 and S10 on D3D12 and Metal. Needs hardware the user provides
(a Windows PC, a Mac, or a cloud GPU machine); stays open until then.

## Order

This is the project's what-next order (the user, 2026-11-26): every piece of work after phase 6's merged chunks comes from
this table, and the backlog's old priority list points here. Two tracks run side by side: shared-routes (its own plan;
stage 1 is PR #57, which merges before any area touching its files, R5b's `journey.hpp` item included; stage 2's plan
waits for D1 and D2 per decision 4) and this work. Within this work:

| Step | Areas | Needs | Why here | Status |
|---|---|---|---|---|
| 1 | R1 | none | every later session loads less; shared-routes' tasks then add one-line rules (tell its next dispatch) | done |
| 2 | R2, R11 (parallel) | R1 | R2 is small and stops start-up and save failures; R11 shares no files | done |
| 3 | R3 | R2 (`main.cpp`, `app.cpp`) | the silent-failure class |  |
| 4 | R4 | R3 (the save job's error path) | before shared-routes stage 2 changes the save layout |  |
| 5 | R5, R6 (parallel) | R4 for R5 (`host.cpp`) | concrete bugs |  |
| 6 | R5b, R7 (spec first), R8 (parallel) | R5 for R5b; R6 for R7 (`core/logic.cpp`); R5b and R7 both touch the tick, so R7 rebases on R5b | R7's spec can be written during step 5 |  |
| 7 | R9, R13 (parallel) | R8 for R9; R5 for R13 | |  |
| 8 | R10 (spec first) | R8 | its spec can be written during step 7 |  |
| 9 | Backlog sweep | R1-R11 (R5b included), R13 merged | walked with the user: each backlog item left after the fold-in below is fixed, parked with its trigger, or dropped; the fixes are grouped into branches by the files they share |  |
| 10 | Low and info pass | step 9 | every review finding no area closes ends fixed or ruled, as Goal 1's closings; first a triage table by review file, where a finding in code D2 or D3 will replace is ruled "superseded by D2/D3" rather than patched, then fix branches by area |  |
| 11 | R12 | steps 9 and 10 | the docs describe what was built; the backlog's status column (GAP1-SWEEP-UP-BACKLOG-VS-CODE-01, -02) records the sweep's results |  |
| 12 | D1, then D2, then D4 and D3 | R1-R12 | the review's items 8-10; shared-routes stage 2 is planned after D1 and D2 |  |
| 13 | D5 | the user's hardware | S9 and S10 on D3D12 and Metal |  |

Status says only what is done (what is open now lives in the project memory's `in-flight.md`). An area's pull request
marks its own paragraph above **Implemented**, as R1's is, so parallel areas never edit the same line; the step's Status
becomes `done` in the pull request that, rebased on main, finds every other area of its step already implemented. The
backlog items an area closed leave the backlog after its merge. Each area's plan also takes the backlog items assigned
to it in "Backlog items folded into the areas" (below), test-first where they are bugs; that table, not
the area's paragraph, is the list. A spec for the next area may be written while the current one's pull request waits on CI (the root
AGENTS.md's "never sit idle while CI runs").

## Decisions for the user (all four taken as recommended by the user, 2026-11-25)

1. **Base-world fingerprint in saves** (R4; README §7a, row 1). Decided: yes. It turns silent mis-loads into refusals at
   the cost of one hash at load (the load regenerates the base anyway), and fits "saves may break before 1.0".
2. **Pinned golden values** (R11; §7a row 2, against backlog 4b's "never pinned hashes"). Decided: pin `core::math` bit
   patterns, never world or state hashes. `core::math` promises the same bits only within one binary (0002 §7.4,
   contraction on), so each preset checks its own table; presets that agree share one.
3. **Cast buffer and hold-to-repeat** (R10; §7a row 7, beside chunk 4 decision 10). Decided: a buffer of a few ticks for
   an early press, and hold-to-repeat as an accessibility option, off by default; the cooldown stays the limit, so neither
   changes difficulty.
4. **shared-routes stage 2** (GD-ROADMAP-SCOPE-01). Decided: stage 1 carries on; stage 2's plan waits for D1's travel
   decisions and D2's target sizes, so it isn't built for assumptions they overturn.

## Calls made (for the user to check)

- R1 first, though the review puts docs at item 7: every session after it is cheaper, and nothing it touches is code.
- Small areas use this spec as their design (Goal 3); R7 and R10 get their own, since each has open behaviour to settle.
- Escape's confirmation is a second press shown on the HUD, not a stand-in dialog (anti-goal: no throwaway UI); chunk 10's
  pause menu replaces it.
- Device loss ends cleanly with a dialog; rebuilding the device is left for later.
- `.clang-tidy` is not changed (the user's file); the float-cast check is a sanitizer flag.
- R1's byte and line budgets apply to every AGENTS.md below the root; the root's dated process rules are the user's and
  stay as written (the check exempts it, saying why).
- R3 closes GAP1-SCENE-TRANSITION-FLOW-09's silent exit with the dialog; whether a mid-session failure first tries an
  emergency save goes to D1 with autosave.
- The interim cap on NPC power growth (GD-WORLD-SIM-03's cheap option) is not taken; D1 decides growth.

## Left to each area's plan

Task split and waves inside the area, exact thresholds (failed-frame count, size budgets, buffer ticks), test names,
which low findings in the area's clusters each task closes.

## Outcome: R1 (implemented)

R1's plan (`docs/superpowers/plans/2026-11-25-review-r1-agent-docs.md`, six tasks in three waves) is built.

- **Budgets** (set by the plan from what the cut files needed): every tracked `AGENTS.md` below the root ≤ 8,192 bytes
  and every line ≤ 300 characters; `.claude/skills/*/SKILL.md` ≤ 200 lines (a skill's `references/` files are not held
  to it); `.claude/agents/*.md` ≤ 60 lines. The root `AGENTS.md` is exempt. `doctor --docs` fails any file over its
  budget; its selftests pin each boundary (8,192 passes, 8,193 fails; 300 characters pass, 301 fail).
- **Sizes**, before → after: `src/AGENTS.md` 45,264 → 7,190 B (plus new `src/gfx/AGENTS.md` 441 B and
  `src/game/AGENTS.md` 1,379 B for rules true in one module), `tools/` 21,137 → 5,644, `tests/` 19,275 → 7,515,
  `.github/` 8,504 → 3,646, `assets/` 2,807 → 2,059, `art/` 2,533 → 1,395. A task under `src/` now loads about a
  sixth of what it did, and `src/AGENTS.md` keeps 1 KB of room for new rules (a rule binding more than one module
  stays there: a nested file loads only for its own module).
- **`doctor --docs`** also fails a `CLAUDE.md` that is not exactly the pointer to its `AGENTS.md`, and checks the
  commands quoted in `src/` headers and `.dox` files, where much of the cut text now lives.
- **Nothing lost:** each cut fact went to its home (Doxygen on its class or `module.dox`, the skill that owns the job, a
  comment at the CI step, or the Outcome it came from), was found already there, or was dropped only as stale history
  (incident anecdotes and dates). Each task's reviewer checked its fact map; facts a review found homeless were carried
  into a later task (below) rather than a second fix round.

Rulings made while building it:

- Tasks 3 and 4 ran in parallel though both edit `tools/project_n.py` docstrings: they own disjoint functions, so a
  rebase clash is a trivial hunk fix (none happened).
- Tasks that changed no compiled file skipped the full `test` (`selftest`, `lint --range` and `doctor --docs` were
  their gates): four cold worktree builds at once beside shared-routes would double the wait for nothing a doc can
  break. The controller's test and release builds after each wave covered the one C++ comment such a task touched.
- One fix pass per review: three findings that a re-review raised after the fix pass were carried into the next task
  that edited the same files (the look-at-the-game skill's view names, which also come from `CompareWithReference`
  and six helpers; the scene-ID counter in the saves line; a label wholly hidden by fog taking no room and the
  clouds' clock, as Doxygen), and one into the final review's fixes (the `Renderer::Impl::GatherLabels` name).
- The final review's commit-scope notes (three landed subjects that also touch other files) changed nothing:
  `commit-check` passes and each subject names its main change.
- The plan's at-budget selftest input was one 8,191-character line, which breaks its own line budget; the check's
  tests use 81 lines of 99 characters plus one of 91 (8,192 bytes, the budget exactly) to test the same byte boundary.

## Outcome: R2 (implemented)

R2's plan (`docs/superpowers/plans/2026-11-26-review-r2-windows-paths.md`, five tasks in five waves; Task 5 added by the
user's "UTF-8 everywhere" during Task 1) is built.

- **One conversion:** `core::PathText` (a path's UTF-8 bytes, from `u8string()`) and `core::PathOf` (UTF-8 text back to a
  path) in `core/path_text.hpp` replace the two private copies (`PngPath` in `gfx/image.cpp`, `FromUtf8` in
  `platform/paths.cpp`) and every `.string()` the review listed: logs, errors, `TTF_OpenFont`, the save job, settings,
  YAML, the log file name. The round trip is checked on named samples by their bytes and `native()`, so a changed
  separator shows, not only an unequal path.
- **Tests that fail without the fix** (RED recorded on windows-msvc under Wine): a save to a non-ASCII slot aborted the
  process (SIGABRT in the background job); settings and the font failed with "No Unicode translation"; the log case logs
  and reads back a unique line, so a file seeded by the fixture cannot pass it. The test folder is `u8"项目_é_🎮"`: outside
  every Western code page, inside 1252, and outside the BMP.
- **UTF-8 code page for windows-msvc executables:** `windows/utf8.manifest` (`activeCodePage` UTF-8) is embedded in every
  Windows binary with `/MANIFEST:EMBED /MANIFESTINPUT`, and a manifest edit relinks them (`linkdepfiles`, as XMake's own
  manifest rule does). Measured (spike S1): host Wine 10.0 reported `GetACP()` 1252 before and 65001 after, with the
  RT_MANIFEST resource present; the windows-msvc tests also ran in the container's Wine. `core_tests` checks
  `GetACP() == CP_UTF8` on every Windows run, so CI's native windows job is the native measurement.
- **UTF-8 everywhere:** every compiler reads sources and encodes narrow literals as UTF-8 (`set_encodings("utf-8")`,
  global); the Python tools run in UTF-8 mode (`PYTHONUTF8` in the Pixi environment); the logger sets the Windows
  console's output code page to UTF-8 from `Start` and the early logger alike, and puts the launching console's code
  page back at exit.
- **`lint`:** rejects `.string()`, `.generic_string()` and `->string()` in `src/`, and any tracked or new text file that
  is not UTF-8 or starts with a byte-order mark. Text is classified by git (content `-text`, a `-text`/`binary`
  attribute, or LFS), so a new file type needs no edit; the fuzz corpus is exempt (malformed on purpose).

Rulings made while building it:

- MSVC read the raw `u8` literals of Task 1's samples in code page 1252 (3 of 10 missed): fixed with
  `set_encodings("utf-8")` for every toolchain rather than escapes, since existing literals (`hud_tests`' 生命,
  `tests/support/world.hpp`'s 王) were mojibake on windows-msvc too; GCC and Clang already read UTF-8.
- The manifest goes in through linker flags, not `add_files(".manifest")` (XMake rejected it for these targets); the
  manifest path is project-relative, which resolves because XMake links from the project folder.
- `lint`'s `code_of()` is unchanged: its limits (block comments, multi-line raw strings) are shared by every check and no
  such case occurs in `src/`; a hit inside a commented-out call costs nothing. `.string ()` with a space is not matched:
  `fmt` never writes it.
- Text files are classified by git rather than a suffix list, which missed tracked text files.
- The console code-page tests each own a private console (`FreeConsole`, `AllocConsole`) in their own executable: the
  inherited console is shared with the shell and with test processes running in parallel, so setting and asserting its
  code page there raced and changed the caller's terminal. Where no console can be made (plain Wine), the case warns
  instead of passing silently; CI's native windows job runs the assertions.
- Each task's reviews and fixes: one fix pass each, Minors included; release builds of linux-gcc and windows-msvc passed
  after Tasks 2, 3 and at the branch head.
- The final review (Opus, and GLM-5.3-Flash, DeepSeek V4.1 Flash, GPT-6 Luna and Qwen3.8 Flash) found no Critical
  issue. Fixed from it: the manifest's `<application>` now declares the `asm.v3` namespace Microsoft documents for
  `activeCodePage` (Wine accepted it without; the native loader may not); the game restores the console's code page;
  the debug data folder's path is built with `PathOf`; and `lint`'s git listings use raw (`-z`) names, since
  `core.quotePath` quotes non-ASCII names, which then never matched a file. The UTF-8 check also classifies untracked
  binaries and runs before any lint step that decodes files, so a non-UTF-8 source gets its message, not a traceback.
- Rejected from it: including `<cstdlib>` in `log.cpp` for `std::atexit` (`pch.hpp` holds it, and a `.cpp` includes no
  standard header). Accepted: a console test run straight from a terminal sends doctest's output to its new console;
  under `xmake test` and CI the streams are redirected and unaffected.

## Outcome: R11 (implemented)

R11's plan (`docs/superpowers/plans/2026-11-26-review-r11-tests-tooling.md`, nine tasks in three waves, the Tracy task
alone and last) is built.

- **Pinned `core::math` bits** (TESTS-CORE-01): 20 named samples each for `Sin`, `Cos`, `Exp`, `Log`, `Pow` and `Atan2`,
  compared by bit pattern against a table per toolchain; a compiler and architecture with no table doesn't build, and
  a skipped case prints a toolchain's table. Spike S1, measured: the generated tables of linux-gcc, linux-clang and
  windows-msvc (under Wine, debug builds) are byte-identical, so the three share one table. clang-arm64 (the pull
  request's macOS CI log, release build) differs from it in 11 of the 120 values, each by one unit in the last place
  (1 of `Cos`, 6 of `Exp`, 1 of `Log`, 3 of `Pow`; `Sin` and `Atan2` agree), so it has its own table.
- **Replay sessions on the small test data** (TESTS-GAME-01): the replay file's median went from 29.5 s to 14.0 s; the
  world-scene case and the skipped workers case keep the shipped data. Each guarded property was shown to fail with
  its guard's subject broken (walk-to, exploring, arena, cells, painted, no-save); the exploring case was rewritten
  across a month's jump because the first version passed with `ExploreRoute` doing nothing.
- **Shared game test helpers** (TESTS-GAME-02): the local copies moved into `tests/support/`, each variant under its
  own name (`LivingFoxes`, `SameEventsIgnoringTick`, `StepMonthFrozen`, `PublishedAt`, `PublishedSnapshot`,
  `HostWithCultivatorAt` beside `AddCultivator`), and the two hand-built lone-cultivator world saves are one
  (`LoneCultivatorSave`); `game_tests` has 388 cases before and after on linux-clang and windows-msvc.
- **Font coverage** (X-TEXT-L10N-01): every codepoint of `strings.yaml` and `names.yaml` (both languages, characters
  and pinyin) is looked up in the shipped font, a miss naming its codepoint and its string's key. No glyph is missing.
- **`test --test`** matching no test file and no case now fails, naming the closest files and cases; the listing is
  taken with the run's own doctest arguments, so a skipped case named with `--no-skip` still runs (named without it,
  the run fails saying so), and a container run
  is handed the name as given and checks it against its own build (the first version passed the computed filter in,
  so the check never ran there).
- **`check --sanitizers`** says which sanitizer check only built (windows-msvc's AddressSanitizer off Windows) rather
  than "every supported sanitizer"; **`reference`** fails on a committed picture no case draws, not only under
  `--update`; **`build`** removes the 0-byte outputs a stopped build left in its own folder (a sanitizer build its own,
  never the plain one) before XMake takes them as up to date. `RendererFor` parses the manifest and the strings once
  per call (it parsed the manifest up to four times).
- **Tracy without forced callstacks or fibers** (GAP1-THIRD-PARTY-LIBRARY-USAGE-01), and `measure`'s baseline
  (`MEASURE_BASELINE`, shown as "baseline:") retaken with it: below.

Measurements and the rulings they led to:

- **The float-cast-overflow flag went to gcc, not clang** (GAP2-REPO-CONFIG-FILES-01; this spec and the plan named
  clang). Spike S2, measured with a throwaway `static_cast<int32_t>` of 1e20: clang's `-fsanitize=undefined` already
  reports it; gcc's does not (the run passed with no report) until `-fsanitize=float-cast-overflow` is added, when it
  reports "1e+20 is outside the range of representable values of type 'int'" and the run fails
  (`UBSAN_OPTIONS=halt_on_error=1`, already set). The flag is on `pixi-gcc`'s undefined builds only; gcc's whole
  undefined run then passed with no finding.
- **S3 uses the swords run's `work` median, not its frame median:** every frame median sat at 6.06 ms, the 165 Hz
  display's interval (the runs are paced by the display), so a frame delta cannot show a zone's cost.
- **The baseline is an interleaved A/B from one quiet sitting** (the user, 2026-11-27: measure only when they say the
  machine is quiet and the screen free). The plan's before and after runs, a night apart, could not be compared: swords
  work medians 0.97 to 0.98 ms before against 1.79 to 2.40 ms after, while the old configuration rebuilt in the after
  session read 2.62 ms, so the machine's state had changed, not the build (the first before runs were also unusable:
  window unfocused, one at about 59 Hz). A first re-baseline stopped at its own rule (the first old run's swords work
  median over 1.5 ms: it read 2.20 ms with another session's compiler at 100% of a core). The sitting that counts
  alternated old and new three times, both built beforehand, the load noted before each run (1-minute load 0.5 to
  0.9), all six runs trusted, on the 165.1 Hz display:

  | Work, main thread, ms (median/p99/worst) | burst | swords | seals |
  |---|---|---|---|
  | old 1 | 1.44/4.38/4.79 | 1.32/4.30/5.69 | 1.31/4.35/5.43 |
  | old 2 | 1.35/4.22/4.60 | 1.30/4.18/4.87 | 1.29/4.07/5.12 |
  | old 3 | 1.33/4.37/5.16 | 1.28/4.22/5.08 | 1.30/4.27/4.84 |
  | new 1 | 1.29/4.26/5.45 | 1.25/4.15/5.48 | 1.26/4.18/4.80 |
  | new 2 | 1.28/4.11/4.52 | 1.23/4.11/4.75 | 1.26/4.03/4.84 |
  | new 3 | 1.29/4.31/4.63 | 1.24/4.12/4.77 | 1.26/4.31/4.87 |
  | `MEASURE_BASELINE` (new, each column's median) | 1.29/4.26/4.63 | 1.24/4.12/4.77 | 1.26/4.18/4.84 |

- **S3, the zone cost:** swords work median 1.30 ms old (1.28 to 1.32) against 1.24 ms new (1.23 to 1.25); the 0.06 ms
  between them is beyond either side's spread and the ranges do not overlap. The measured window holds 120.3
  main-thread zones a frame (the first count, 246, took every thread over the whole capture), so forced callstacks and
  fibers together cost a zone about 0.5 µs, which `profile.hpp` now states. It is a difference, not a zone's whole
  cost, and its margin over the spread is small (0.02 ms). Seen beside it, not part of S3: the uncapped start-up
  second ran about 11,300 frames old against about 61,700 new.
- The machine read about 1.3 ms of work in that sitting against 0.97 ms the first evening: the baseline is for
  comparing runs of one sitting, as `measure` always was.
- `Pow(2, 1024)` is finite (0x7fefffffffffff2a), not infinity: `Exp(1024 · Log 2)` lands just under the largest
  double, which is `Pow`'s contract; the sample is named for it.
- The manifest and strings parses saved no wall time (`reference` 16 s warm before and after): they cost milliseconds
  beside the GPU work. The change stays for the single parse, not for speed.
- The "reference drift guard" backlog item was already closed by existing code (`reference` prints pictures that
  changed within tolerance); nothing was added for it.
- Task 9's implementer worked in the feature worktree itself: it ran alone and last, and its own worktree would have
  built releasedbg and Tracy cold twice.
- The lock's macOS section was renamed by hand (the two `tracy-libdl` hashes the Linux and Windows builds produced);
  the pull request's macOS job confirms it.
- Each task's reviews: one fix pass each, Minors included; release builds of linux-gcc and windows-msvc passed after
  each wave and at the branch head.
- The final review (Opus, and GLM-5.3-Flash, DeepSeek V4.1 Flash, GPT-6 Luna and Qwen3.8 Flash) found no Critical
  issue. Fixed from it: the two `CultivatorAt` helpers that shared a name and the two hand-built lone-cultivator saves
  (TESTS-GAME-02's last pair); the findings table's Closing cells; `--test` naming only skipped cases, or a test file
  with no case left to run, fails for that reason (the check had covered only the case-name form); the shared event
  comparison also compares `epoch` and `cosmetic`; `check --sanitizers --container` on a Windows host names
  windows-msvc's AddressSanitizer as built only (its container runs under Wine); the empty-output cleanup is anchored
  to the repository, not the working directory; the font check steps past a NUL byte (SDL's decoder stops on one
  without advancing) and opens the font through `core::PathText`; the pinned table's toolchain key names its
  architecture and refuses an unknown one rather than calling it gcc-x64; the last local `SamePoint`.
- Rejected from it: giving the replay tests' temp folders unique names. The collision under parallel runs is real
  (seen once during Task 1) and older than this branch; it is TESTS-GAME-09's cluster, which the low and info pass
  (step 10) closes across all eight test files that share the pattern. The empty clang-arm64 table, raised by three
  models, was the planned fill from the macOS CI log, not a defect of the design; it is filled now.

## The findings and their areas

Every medium finding, with the area that closes it. Each area's pull request updates its rows' Closing column.

| Finding | Area | Closing |
|---|---|---|
| APP-INPUT-UI-01 | R10 | |
| APP-MAIN-01 | R3 | |
| CORE-INFRA-01 | R3 | |
| CORE-PERSIST-01 | R4 | |
| CORE-WORLD-01 | S | |
| CORE-WORLD-02 | S | |
| DATA-COMBAT-01 | R5 | |
| DATA-YAML-CORE-01 | R8 | |
| DESIGN-DOCS-01 | R12 | |
| DESIGN-DOCS-02 | R1 | amended |
| DESIGN-DOCS-03 | R12 | |
| DESIGN-DOCS-04 | R4 | |
| DESIGN-DOCS-05 | R4 | |
| DESIGN-DOCS-06 | R3 | |
| GAME-COMBAT-01 | R5 | |
| GAME-HOST-A-01 | R5 | |
| GAP1-CLOCK-DISCIPLINE-PAUSE-01 | R7 | |
| GAP1-CLOCK-DISCIPLINE-PAUSE-02 | R7 | |
| GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-01 | R4 | |
| GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-02 | R3 | |
| GAP1-SCENE-TRANSITION-FLOW-01 | R6 | |
| GAP1-SWEEP-UP-BACKLOG-VS-CODE-01 | R12 | |
| GAP1-SWEEP-UP-BACKLOG-VS-CODE-02 | R12 | |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-01 | R11 | fixed (measured by the A/B; Outcome: R11) |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-02 | R13 | |
| GAP2-COORDINATE-SPACES-AND-PICKING-01 | R10 | |
| GAP2-COORDINATE-SPACES-AND-PICKING-02 | R10 | |
| GAP2-MEMORY-BUDGET-VS-0002-S13-01 | R13 | |
| GAP2-MEMORY-BUDGET-VS-0002-S13-02 | R13 | |
| GAP2-REPO-CONFIG-FILES-01 | R11 | fixed (gcc's undefined builds, not clang's; Outcome: R11) |
| GD-COMBAT-SYSTEMS-01 | D1 | |
| GD-COMBAT-SYSTEMS-02 | D3 | |
| GD-COMBAT-SYSTEMS-03 | D1 | |
| GD-COMBAT-SYSTEMS-04 | D1 | |
| GD-COMBAT-SYSTEMS-05 | D1 | |
| GD-COMBAT-SYSTEMS-06 | D1 | |
| GD-COMBAT-SYSTEMS-07 | R10 | |
| GD-COMBAT-SYSTEMS-08 | R10 | |
| GD-ROADMAP-SCOPE-01 | D1 | |
| GD-ROADMAP-SCOPE-02 | D1 | |
| GD-ROADMAP-SCOPE-03 | D4 | |
| GD-ROADMAP-SCOPE-04 | D2 | |
| GD-ROADMAP-SCOPE-05 | D5 | |
| GD-ROADMAP-SCOPE-06 | D4 | |
| GD-ROADMAP-SCOPE-07 | R1 | amended |
| GD-ROADMAP-SCOPE-08 | R3 | |
| GD-ROADMAP-SCOPE-09 | D1 | |
| GD-TOI-COVERAGE-01 | D1 | |
| GD-TOI-COVERAGE-02 | D1 | |
| GD-TOI-COVERAGE-03 | D1 | |
| GD-TOI-COVERAGE-04 | D1 | |
| GD-TOI-COVERAGE-05 | D1 | |
| GD-TOI-COVERAGE-06 | D3 | |
| GD-TOI-COVERAGE-07 | R5 | |
| GD-TOI-COVERAGE-08 | D3 | |
| GD-TOI-COVERAGE-09 | D4 | |
| GD-WORLD-SIM-01 | D2 | |
| GD-WORLD-SIM-02 | R4 | |
| GD-WORLD-SIM-03 | D1 | |
| GD-WORLD-SIM-04 | S | |
| GFX-COMBAT-DRAW-01 | R6 | |
| GFX-FEATURES-01 | R6 | |
| GFX-RENDERER-01 | D2 | |
| GFX-WORLD-DRAW-01 | D2 | |
| PLATFORM-01 | R9 | |
| SPECS-ROADMAP-01 | D2 | |
| SPECS-ROADMAP-02 | D4 | |
| SPECS-ROADMAP-03 | R12 | |
| SPECS-ROADMAP-04 | D4 | |
| SPECS-ROADMAP-05 | D4 | |
| SPECS-ROADMAP-06 | D4 | |
| TESTS-CORE-01 | R11 | fixed |
| TESTS-GAME-01 | R11 | fixed |
| TESTS-GAME-02 | R11 | fixed |
| TESTS-GAME-03 | R4 | |
| TESTS-GAME-04 | R4 | |
| X-ACCESSIBILITY-01 | R8 | |
| X-ACCESSIBILITY-02 | R10 | |
| X-ACCESSIBILITY-03 | R8 | |
| X-ACCESSIBILITY-04 | R7 | |
| X-ACCESSIBILITY-05 | R8 | |
| X-ARCHITECTURE-01 | D3 | |
| X-ARCHITECTURE-02 | D2 | |
| X-ARCHITECTURE-03 | S | |
| X-ARCHITECTURE-04 | D2 | |
| X-ARCHITECTURE-05 | D3 | |
| X-ARCHITECTURE-06 | D3 | |
| X-ARCHITECTURE-07 | D3 | |
| X-ARCHITECTURE-08 | R1 | amended |
| X-CPP-SAFETY-01 | R2 | fixed |
| X-CPP-SAFETY-02 | R2 | fixed |
| X-DETERMINISM-01 | R4 | |
| X-PERFORMANCE-01 | S | |
| X-PERFORMANCE-02 | D2 | |
| X-PERFORMANCE-03 | D2 | |
| X-PERFORMANCE-04 | S | |
| X-RESILIENCE-01 | R3 | |
| X-RESILIENCE-02 | R3 | |
| X-RESILIENCE-03 | R3 | |
| X-RESILIENCE-04 | R3 | |
| X-RESILIENCE-05 | R4 | |
| X-RESILIENCE-06 | R4 | |
| X-TEXT-L10N-01 | R11 | fixed |
| X-TEXT-L10N-02 | R2 | fixed |
| X-TEXT-L10N-03 | R8 | |
| X-UNTRUSTED-INPUT-01 | R2 | fixed |
| X-UNTRUSTED-INPUT-02 | R2 | fixed |

### The low findings added by the user

| Finding | Area | Closing |
|---|---|---|
| APP-MAIN-02 | R3 | |
| CORE-PERSIST-03 | R4 | |
| CORE-PERSIST-04 | R4 | |
| X-DETERMINISM-02 | R4 | |
| X-UNTRUSTED-INPUT-03 | R4 | |
| X-UNTRUSTED-INPUT-04 | R4 | |
| CORE-SIM-01 | R5b | |
| GAME-HOST-A-02 | R5b | |
| GAME-HOST-B-01 | R5b | |
| GAME-SCENES-02 | R5b | |
| GAME-TRAVEL-NPCS-05 | R5b | |
| X-CPP-SAFETY-03 | R5b | |
| GFX-TEXT-01 | R6 | |
| X-UNTRUSTED-INPUT-05 | R6 | |
| DATA-YAML-CORE-08 | R8 | |

### Backlog items folded into the areas

Added by the user, 2026-11-26: open items of the project's sweep-up backlog (the project memory) that fit an area's files
go into that area's plan; the rest wait for the backlog sweep (step 9). An item marked "already" is a finding the area
closes anyway.

| Backlog item | Area |
|---|---|
| Audit 1: quad-index uploads flagged done when recorded | R6, already (GFX-FEATURES-01) |
| Audit 2: a malformed replay's claimed tick count reserves gigabytes (`ReadTicks`) | R4, with the amplification bounds |
| Audit 3: `check --sanitizers` reports "every supported sanitizer" though Linux skips windows-msvc's ASan | R11 |
| Audit 4: keyboard and gamepad casts aim at the mouse cursor | R10, already (the aim model) |
| Audit 5: the held camera pan ignores focus | R8, with the focus-loss release |
| Audit 6: `creatures.yaml`'s `walk_speed` is read unranged | R5, through DATA-COMBAT-01's shared ranged reader |
| Audit 7: scene loading creates a transfer buffer and submit per sprite | R13 |
| Audit 9: `reference` flags committed pictures with no case only under `--update` | R11 |
| Audit 11: the travel journal's upper day clamp has no test | R5b |
| Audit 12: paused frames still animate and re-upload | R7, already |
| Audit 13: a leg's arrive ticks only asserted to fit u32 | R5b, already (X-CPP-SAFETY-03) |
| Audit 15: the sight radius doesn't follow a `creatures.yaml` reload | R5 |
| Cast queued behind the dash | R10, with decision 3's cast buffer |
| Seal sprites checked only against the manifest, not the scene's list | R5 |
| YAML alias amplification in unknown-key warnings | R5 |
| `core/journey.hpp` includes `core/calendar.hpp` only its .cpp needs | R5b |
| Split `renderer.cpp`'s world-map upload block into its own file | R6 |
| A stopped build leaves 0-byte binaries XMake treats as up to date | R11 |
| `reference_tests.cpp`'s `RendererFor` parses the manifest up to four times | R11 |
| Reference drift guard: pictures drift within tolerance unnoticed | R11, with audit 9 |
| `measure` counts `SubmitFrame`'s GPU wait as main-thread work | R13 |
| The main-thread hitch after a month step (a measured spike first) | R13 |
| Localisation to industry practice | D3, with the data-driven sweep, before chunk 6 |
