# A faster development loop: lint what a change reaches, one gate per level, reviews sized to the task

Date: 2026-11-12 · Status: approved 2026-11-12, on the user's condition that it doesn't cost the game performance (§5)

## Intent

The user, 2026-11-12, after chunk 4 (PR #22): "the development loop speedup plans really need to come next/asap. We can't
keep running hour sessions for everything." The direction was already agreed and is only noted, never planned: the
sweep-up backlog's "lint only the `.cpp` files a changed header reaches" (the user: "Ensure lint is only run on changed
files too"), the half of the 2026-11-10 faster-check work that its spec left as a non-goal; and the rules the user set
during chunk 4 (lint at milestones, small unit tests, never build commit by commit), which live in project memory, so a
subagent only follows them when the controller remembers to relay them.

This branch makes the tools cheaper where the loop repeats them, and writes the loop's rules where every agent reads
them, so no step pays for a gate meant for a later one.

## Where chunk 4's time went (2026-11-12)

| What | Time | Status |
|---|---|---|
| Wrap-up: every commit built one by one (~80 commits, windows-msvc and linux-gcc release) | ~80 min | banned since (AGENTS.md Rules; memory verify-per-task-not-per-commit) |
| Wrap-up: `check --sanitizers` | 874 s | AGENTS.md already says "on request only"; it ran unasked |
| Full `check` run inside tasks (Task 10d measured 291 s → 267 s) | ~5 min each | AGENTS.md says `test` + `lint` per task; implementers ran `check` anyway |
| A task's `lint` after any header change: every `.cpp` (`lint_units`) | 71 s now, twice per task | this branch, §1 |
| Long implementer runs (Task 10b: 36 min, 124 tool calls; the fix wave: 70 min) | | this branch, §3 |
| Review and fix rounds per task | ~10–25 min | this branch, §3 |
| CI on the PR (container clang 14m47s, msvc 15m8s, the other four ~8 min) | ~15 min | non-goal (below) |

`check` itself is not the problem: chunk 4's "30 minutes" turned out to be a hang, and its real time stayed near the
faster-check numbers. Measured again at this branch's start (origin/main "docs(design): record the user's look at the
sword rings", 32 cores, each step on its own):

Seconds; cold is a new worktree's first run, warm follows a one-line change to `src/core/types.hpp` (the faster-check
spec's method); the 2026-11-10 Outcome's numbers in brackets:

| step | cold | warm |
|---|---|---|
| selftest + fmt | 1 (1) | 1 (2) |
| **lint** (127 units now, 93 then) | **99** (51) | **72** (31) |
| test linux-gcc release | 71 (58) | 52 (31) |
| test linux-clang release | 39 (30) | 18 (12) |
| **test windows-msvc release** (container, Wine) | **117** (80) | **107** (73) |
| reference pictures | 29 (4) | 29 (3) |
| unity test | 38 (26) | 21 (10) |
| releasedbg build | 43 (29) | 16 (11) |
| **total** | **437** (278) | **316** (174) |

The code grew by a third since then (93 → 127 lint units) and `check` with it, back over the 5 minutes faster-check
reached. What a task pays, warm, after a one-line change to `src/gfx/renderer.hpp`: **`lint` 71 s** (all 127 units,
though 8 reach that header), the native debug `test` 19 s.

## Goals

1. **`lint` lints the units a change reaches.** A changed `.cpp` lints itself; a changed header lints the `.cpp` units
   whose recorded dependencies include it, read from XMake's per-object dependency files of the lint build `lint`
   already runs first. Same findings for those units, same verdict rules (`tidy_verdict`, `IGNORED_FINDINGS`), same
   `--fix`.
2. **`check` prints how long each step took**, and the total, when it ends, pass or fail (the backlog's "first time each
   step", done by hand twice now).
3. **One gate per level**, written in the root AGENTS.md where every session and subagent reads it (below, §2).
4. **Reviews sized to the task**, also in AGENTS.md (§3).
5. **Warm `check` back to ≤ 5 min** (316 s now) by trimming its two slowest steps without dropping what they check
   (§5).
6. Measured in the Outcome: a typical task's lint, a header change's lint, and `check`, before and after; and the next
   chunk's (the glow chunk's) time per task against chunk 4's.

## Non-goals

- Dropping or loosening anything `check` checks, or moving a check to CI.
- Running XMake builds in parallel (each already uses every core; parallel package installs race).
- CI's 15 minutes: the PR waits on it once per branch, not once per task; its own look later (caching the MSVC and
  container package builds), on the backlog.
- `test --affected` (choosing suites from the dependency files): the native debug `test` is 19 s warm, and
  `test --suite` already exists for iterating.

## Anti-goals

- A lint that passes where today's fails: a unit that reaches a changed header and isn't linted, a missing or stale
  dependency record read as "reaches nothing", a new file skipped.
- Rules that save time by skipping review or verification the user asked for: every finding is still fixed, Minor
  included; every task is still reviewed; the final review still runs on the strongest model.

## Design

### 1. `lint_units` from the dependency files

Today: `lint_units(sources)` returns every `.cpp` as soon as any `.hpp` changed (127 units). Each object's record,
`<build>/.deps/<target>/…/<file>.cpp.o.d`, holds a Make-style `depfiles` list: the source, then every header it reached,
`pch.hpp` and what it includes among them, some spelled relative and some absolute.

- `lint` already builds the lint preset before clang-tidy (`xmake build`), so the records match the tree on disk,
  uncommitted changes included. `lint_units` moves after that build and reads them.
- A unit is linted when it changed, when its record names a changed file, or **when it has no record** (new, or not
  built: never assume "reaches nothing").
- A changed header that no record names is reported by name ("reaches no unit, not linted"), as clang-tidy only ever
  sees headers through units; that is no less coverage than today, where the same header surfaces through no unit
  either.
- What `widens_scope` already widens (`.clang-tidy`, `xmake.lua`, the lock files…) still lints every unit; so does
  `--all`.
- Paths are compared resolved against `ROOT`, as `tidy_units` already compares findings (records mix `src/…` and
  absolute spellings; a worktree's absolute paths are under its own root).
- The container lint (`lint --container`) runs the same code on its own build directory's records.

Test-first in `tools/test_project_n.py`, on written records (no build): a header reached by some units; one reached
through `pch.hpp`; a unit without a record; a new header; both path spellings; a widening change. Then for real: a
planted `typedef` in `gfx/renderer.hpp` is reported, and the run lints the 8 units that reach it, not 127.

Measured from this branch's linux-clang debug records (124 objects; the 3 other units have none in a debug build and
are linted anyway): the median header reaches 11 units, three in four reach 46 or fewer; only `core/types.hpp` and
`core/cxx23.hpp` (with `src/pch.hpp`) reach every one, `core/error.hpp` 66, `core/profile.hpp` 57, `core/effects.hpp`
14, `game/arena.hpp` 13, `gfx/renderer.hpp` 8. A typical task's lint should fall from 71 s to about 10 s.

### 2. One gate per level (root AGENTS.md, "Working with superpowers")

One line replaces today's "Verification after each task", and states what each level runs and nothing more:

| Level | Runs | Never |
|---|---|---|
| An edit-build-test round inside a task | `test --suite <s> [--test <t>]` | `lint`, `check`, pictures |
| A task's implementation done; again after its review fixes | `test` + `lint --range <task base>..HEAD` | `check` |
| The branch's head before the PR (and after the final review's fixes) | `check`; `check --container` (or CI) only for build, environment or dependency changes | per-commit builds |
| Sanitizers | only when the user asks | in a wrap-up by default |

The lint and per-commit rules move from memory into this line, so implementers and reviewers read them from AGENTS.md
rather than from a brief; the memories then point at AGENTS.md.

### 3. Reviews sized to the task (same section)

- **Plans size tasks for one implementer run of ~30 min**: a task the plan expects to need more (Task 10b's 124 tool
  calls) is split in the plan, not discovered mid-run.
- **Plans mark each task `review: full` or `review: light`.** Full (the default): game logic, simulation, rendering,
  determinism, saves, tooling with a verdict. Light: docs, data values, mechanical moves and renames, generated
  outputs. Light tasks get one spec-and-quality review on Sonnet; consecutive light tasks may share one review of their
  combined range.
- **One fix pass per review**: one implementer fixes every finding of a review in one dispatch; the re-review is scoped
  to the fix diff (Sonnet; Haiku for comment-only fixes, as memory sdd-rereview-models already says).
- **The final whole-branch review stays on the strongest model**, once, then one re-review of its fixes.

### 4. `check`'s step times

`check` records each step's wall time (selftest, commit-check, fmt, lint, each preset's tests, reference pictures, the
unity test, the releasedbg build, and sanitizer runs when asked) and prints a table at the end, also when a step fails
(through `finally`), so a slow step shows up the day it appears rather than when someone measures by hand. No change to
what runs or in what order.

### 5. Trimming `check`'s slowest steps

Two steps grew most since 2026-11-10:

- **Reference pictures, 3 s → 29 s warm**, with no build in it (linux-gcc release is already built by the test step
  before): one `reference_tests` process draws all 23 pictures in turn on lavapipe. Measured 2026-11-12: 28 s of wall
  time on 38 s of CPU, so lavapipe keeps 1.4 of 32 cores busy; each of the 25 cases takes ~1 s, the slowest 2.1 s.
  `run_references` runs one process per case (doctest's `--first`/`--last` by position: several case names hold
  commas, which `--test-case` splits on), as many at once as `jobs()`, every picture still drawn and compared, every
  failing case's output reported; expected a few seconds.
- **windows-msvc, 107 s warm: every MSVC build recompiles everything, and `check` builds it twice.** Measured
  2026-11-12 (the user asked whether caching, copying or setting up ahead would help): the container starts in 0.1 s,
  Pixi in it in 0.3 s, Wine in 1.2 s, configure takes 0.1 s, and the 8 test suites run in parallel in 4 s. The rest is
  two full builds of 143 compiles (17 precompiled headers) and 17 links, ~47 s each, with nothing changed: one from
  `test`'s build, one from `xmake test`, which builds again before running.
  - **Cause:** XMake asks `cl` for `/sourceDependencies` JSON. Under Wine that names every header as a lowercased Wine
    path (`z:\opt\msvc\…`, `z:\home\dev\.xmake\…\include\sdl3\sdl.h`, `z:\src\src\…`), which XMake on Linux can't
    stat, and its `depend.is_changed` treats a file it can't stat as changed. Mapping `z:` back wouldn't be enough:
    the case is lost (`SDL3/SDL.h`).
  - **Fix, tried by hand:** `/showIncludes` instead, which msvc-wine's `cl` wrapper already rewrites to Linux paths with
    their case, and XMake's build cache off for MSVC under Wine: the cache compiles the preprocessed file, whose
    `/showIncludes` lists no headers, so with the cache on, a header change rebuilt nothing (wrong, not just fast).
    With both: a build with nothing changed **0.3 s** (from ~47 s); a change to `gfx/renderer.hpp` rebuilds **exactly
    the 8 units that include it**, 19 s with their links; the whole step after no change 5 s.
  - **How, durably:** XMake has no option for the dependency format; it probes once whether `cl` takes
    `/sourceDependencies` and caches the answer. The builder picks, in this order of preference: (a) our MSVC install's
    `cl` wrapper (set up by `ensure_msvc`, already post-processed there for the toolset stub) drops
    `/sourceDependencies <file>`, so XMake's own probe finds no JSON and falls back by itself, applied idempotently so
    existing installs get it without a reinstall; (b) anything else only if (a) fails, back to the user. The build cache
    goes off in xmake.lua for the msvc toolchain on a non-Windows host (`build.ccache` policy), with the reason. A
    regression test where one exists: the windows-msvc step after no change stays a no-op build (`check`'s step
    times show it).
  - **The user's condition (2026-11-12): "as long as it doesn't impact our performance".** No compiler or linker flag
    changes; Linux builds aren't touched. Turning the build cache off does change the MSVC binaries' bytes: with the
    cache on, cl compiles a preprocessed temporary copy, so anonymous namespaces get other path-derived names
    (`?A0xa99cabdd@core` against `?A0x9c794e1e@core`) and addresses shift; each mode is stable build to build apart from
    69 identity bytes (the PE timestamp and the `.pdb` record), code sections equal. Measured: the MSVC
    `headless_bench.exe` of each mode, alternated 10 times each under Wine (9 measures: ticks, combat ticks, month
    steps at 3 worker counts): 8 of 9 within ±1.2%, combat ticks 0.5–1.2% faster without the cache; month steps at the
    third worker count +6.7% by mean from two outlier runs (3.28 and 3.30 ms against ~2.6–2.7), ~2% without them, on
    12 samples a run. Read: no difference beyond noise. The renderer's GPU side can't run under Wine here; its CPU side
    compiles with the same flags. The Outcome repeats the alternated benchmark on the built branch.
  - **Upstream:** a report drafted for the user to post (XMake: `/sourceDependencies` under msvc-wine never resolves,
    and the build cache drops `/showIncludes` dependencies), on the backlog with the others.
  - Expected: the step ~25 s after a typical header change, ~55 s after a change every unit reaches (one build, not
    two), 5 s after none.

Any trim beyond these two (dropping a step, a preset or a mode; moving one to CI) comes back to the user with the
numbers instead.

## Constraints

- `project_n.py` stays one file, standard library only, thin; new logic test-first in `tools/test_project_n.py`.
- AGENTS.md stays ~100 lines, one line per rule (root is 78 now): §2 and §3 replace lines rather than add paragraphs.
- tools/AGENTS.md's lint Gotcha gains how units are chosen.
- From a worktree, CI's `lint` and `container` jobs are the container gate (AGENTS.md).

## Left to the builder

- Whether `lint_units` parses the records itself or asks XMake (the records are plain Lua tables; parsing the
  `depfiles` string is a few lines).
- The step-time table's exact format.
- The wording of the AGENTS.md lines, within the size limit.

## Outcome

(Filled in when implemented.)
