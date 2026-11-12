# A Faster Development Loop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut what each task and each `check` waits for: lint only the units a change reaches, stop the MSVC build
recompiling everything, draw the reference pictures in parallel, time `check`'s steps, and write the loop's gates and
review sizes into AGENTS.md.

**Architecture:** All tooling lives in `tools/project_n.py` (one file, standard library only, thin), tested in
`tools/test_project_n.py`; one policy line in `xmake.lua`; one wrapper script written by `ensure_msvc` into the MSVC
install; process rules in the root `AGENTS.md`.

**Tech Stack:** Python 3 (standard library), XMake, clang-tidy, doctest, msvc-wine under Podman, Mesa lavapipe.

**Spec:** `docs/superpowers/specs/2026-11-12-dev-loop-speedups-design.md` (approved 2026-11-12, on the user's condition
that the game's performance isn't affected: Task 6 measures it).

## Global Constraints

- `project_n.py` stays one file, standard library only, thin; new logic test-first in `tools/test_project_n.py`;
  `selftest` runs on Windows hosts too, so a test needing bash is skipped there (`os.name == "nt"`).
- Never pipe XMake's output through a filter (tools/AGENTS.md); count compile lines from a saved log instead.
- Nothing `check` checks is dropped or loosened; no test is deleted, skipped or loosened.
- Each task ends with `pixi run project_n selftest` and `pixi run project_n lint --range <task base>..HEAD`, plus the
  task's own verification; only the branch head runs `check`. Never build or `check` commit by commit.
- Commits: Conventional Commits, scopes `tools`, `xmake`, `agents`, `design` (SCOPES in tools/project_n.py).
- Docs written on the branch cite commits by subject, never by hash.
- Update the AGENTS.md that owns a changed behaviour in the same commit (tools/AGENTS.md for project_n.py).
- From this worktree, container gates are CI's `lint` and `container` jobs on the PR (Task 4 changes the build).

## Review Focus

1. A lint unit whose record is missing, stale or names a deleted file must never drop out: a `.cpp` without a record is
   linted; a record for a `.cpp` no longer in the tree is ignored (Task 1 tests both).
2. `lint --container` reads records whose absolute paths start with the container's `/src`, not the host's root:
   resolution goes through `ROOT`, whatever it is (Task 1's path-spelling test patches `ROOT`).
3. A reference case that crashes without output, or several failing at once: every case still runs, every failing
   case's output prints, the run fails (Task 3).
4. The MSVC wrapper must pass every other argument through untouched, including one that merely contains
   `sourceDependencies` (a path) and ones with spaces (Task 4's wrapper test).
5. `check` interrupted by a failing step still prints the times of the steps that ran (Task 2).

---

### Task 1: `lint` lints the units a change reaches · review: full

**Files:**
- Modify: `tools/project_n.py` (`lint_units` at ~1058, `cmd_lint` at ~1174)
- Modify: `tools/AGENTS.md` (the `lint` Gotcha: how units are chosen)
- Test: `tools/test_project_n.py` (`ScopeTest`'s two `lint_units` tests at ~221–227 move to a new `LintUnitsTest`)

**Interfaces:**
- Produces: `unit_dependencies(bdir: Path) -> dict[str, frozenset[str]]`: each unit (repo-relative `.cpp`, forward
  slashes) to the repo-relative files its record names, itself included.
- Produces: `lint_units(sources: list[str], dependencies: dict[str, frozenset[str]]) -> list[str]` (sorted).
- Produces: `unreached_headers(sources: list[str], dependencies: dict[str, frozenset[str]]) -> list[str]` (sorted).

Records are `<bdir>/.deps/**/*.o.d` (Lua tables). Their `depfiles = "…"` string is Make syntax: the target (a token
ending in `:`, itself containing `.cpp`, e.g. `__cpp_clock.cpp.cpp:`), then the prerequisites, some relative to `ROOT`
and some absolute, broken by `\` + newline. Real sample:
`depfiles = "build/linux-clang/debug/.objs/platform/linux/x86_64/debug/src/platform/__cpp_clock.cpp.cpp:   src/platform/clock.cpp /home/…/project_n/src/pch.hpp   /home/…/project_n/src/core/types.hpp src/pch.hpp   src/platform/clock.hpp\`
then a newline and `",`. The unit is the first prerequisite ending in `.cpp`; prerequisites outside `ROOT` (packages,
the toolchain) are dropped.

- [ ] **Step 1: Write the failing tests** in a new `class LintUnitsTest(unittest.TestCase)`, using `temp_repo`:
  - `test_a_changed_header_lints_the_units_that_reach_it`: files `src/a.cpp`, `src/b.cpp`, `src/a.hpp`;
    `deps = {"src/a.cpp": frozenset({"src/a.cpp", "src/a.hpp"}), "src/b.cpp": frozenset({"src/b.cpp"})}` →
    `lint_units(["src/a.hpp"], deps) == ["src/a.cpp"]`.
  - `test_a_header_every_unit_reaches_lints_every_unit`: both records name `src/pch.hpp` → changing it lints both.
  - `test_a_unit_without_a_record_is_always_linted`: `deps` has only `src/a.cpp` → `lint_units(["src/a.hpp"], deps)`
    contains `src/b.cpp`.
  - `test_a_record_for_a_deleted_unit_is_ignored`: `deps` also has `src/gone.cpp` naming `src/a.hpp` → not returned.
  - `test_a_changed_source_file_lints_only_itself` (moved, new signature): `lint_units(["src/a.cpp"], deps) == ["src/a.cpp"]`.
  - `test_every_source_lints_every_unit`: `lint_units(project_n.source_files(), {})` is every `.cpp` (the widening
    path: `scoped_sources` returns every source).
  - `test_a_header_no_record_names_is_reported`: `unreached_headers(["src/new.hpp", "src/a.hpp", "src/a.cpp"], deps) == ["src/new.hpp"]`.
  - `test_records_spell_paths_relative_and_absolute`: a temporary root (patch `project_n.ROOT`) with
    `bdir/.deps/core/linux/x86_64/debug/src/a.cpp.o.d` written in the sample's shape, naming `src/a.cpp`,
    `<ROOT>/src/b.hpp`, `src/c.hpp` after a `\`-newline break, and `/opt/pkg/include/x.h` →
    `unit_dependencies(bdir) == {"src/a.cpp": frozenset({"src/a.cpp", "src/b.hpp", "src/c.hpp"})}`.
- [ ] **Step 2: Run them, expect failures:** `pixi run project_n selftest` → the new tests fail
  (`unit_dependencies` / `unreached_headers` missing, `lint_units` takes one argument).
- [ ] **Step 3: Implement** the three functions in `tools/project_n.py` beside `lint_units`, and reorder `cmd_lint`:
  keep the early "no changed C++ sources" return when `sources` holds no `.cpp`/`.hpp`; then configure and build as
  now; then `dependencies = unit_dependencies(bdir)`, `units = lint_units(sources, dependencies)`; print each of
  `unreached_headers(...)` as `lint: <header> reaches no unit, not linted`; then `tidy_units` as now.
- [ ] **Step 4: Run them, expect passes:** `pixi run project_n selftest` → all pass.
- [ ] **Step 5: Check it for real.** Append `typedef int project_n_probe;` to `src/gfx/renderer.hpp`, run
  `pixi run project_n lint`. Expected: `+ clang-tidy … on 8 unit(s)` (the units whose records name it; count them from
  the records if it differs) and a `modernize-use-using` finding in `renderer.hpp`, exit non-zero. Then
  `git checkout -- src/gfx/renderer.hpp`. Note the time against the spec's 71 s.
- [ ] **Step 6: Update tools/AGENTS.md's `lint` Gotcha** with one sentence: units are the changed `.cpp` files and
  those whose XMake records (`.deps/**/*.o.d`, fresh after lint's own build) name a changed file; a unit without a
  record is always linted.
- [ ] **Step 7: Commit** `git commit -m "build(tools): lint only the units a change reaches"`.

### Task 2: `check` prints each step's time · review: light

**Files:**
- Modify: `tools/project_n.py` (`cmd_check` at ~1255)
- Test: `tools/test_project_n.py` (`CheckTest`)

**Interfaces:**
- Produces: `step_times_table(times: list[tuple[str, float]]) -> str`: one line per step, name and seconds to 0.1, then
  a `total` line.

Step names, in order: `selftest`, `commit-check`, `fmt`, `lint`, `test <preset> release` (each preset), `reference
<preset>`, `unity test <preset> debug`, `build <preset> releasedbg`, and with `--sanitizers` `test <preset> debug
<sanitizer>`. Times use `time.monotonic()`.

- [ ] **Step 1: Write the failing tests** in `CheckTest`:
  - `test_check_prints_each_steps_time`: extend `container_check` to return the captured stdout; it contains
    `selftest`, `fmt`, `lint`, a `test … release` line per non-skipped preset, `reference`, `unity test`,
    `build … releasedbg` and `total`, in that order.
  - `test_step_times_print_when_a_step_fails`: `cmd_test` patched to raise `project_n.Fail("boom")` → `cmd_check`
    raises `Fail`, and the stdout still holds the table with `lint` and the failing `test … release` step.
- [ ] **Step 2:** `pixi run project_n selftest` → both fail.
- [ ] **Step 3: Implement** a small timing wrapper inside `cmd_check` (record name and elapsed seconds even when the
  step raises) and print `step_times_table(times)` from a `finally`.
- [ ] **Step 4:** `pixi run project_n selftest` → pass.
- [ ] **Step 5: Commit** `git commit -m "build(tools): print how long each check step took"`.

### Task 3: reference pictures drawn in parallel · review: full

**Files:**
- Modify: `tools/project_n.py` (`run_references` at ~403)
- Modify: `tests/AGENTS.md` if it describes how `reference` runs; `tools/AGENTS.md` Gotchas: one line (one process per
  case because lavapipe kept 1.4 of 32 cores busy)
- Test: `tools/test_project_n.py` (`ReferenceTest`)

**Interfaces:**
- Produces: `reference_case_count(exe: Path, env: dict) -> int`: runs `[exe, "--count"]` through `subprocess.run`
  (`capture` takes no `env`), and parses doctest's line `[doctest] unskipped test cases passing the current filters: 25`;
  a missing line is a `Fail`.
- Produces: `run_reference_case(exe: Path, env: dict, index: int) -> tuple[str, int]`: runs
  `[exe, f"--first={index}", f"--last={index}"]` (1-based) with stdout and stderr captured together; returns (output,
  return code).

Measured: 28 s wall for 25 cases on 38 s of CPU (1.4 of 32 cores); each case ~1 s, the slowest 2.1 s. Cases run
`jobs()` at once in a thread pool (as `tidy_units`); outputs print in case order once all finish; any non-zero return
code is a failure: without `--update` raise `subprocess.CalledProcessError` after every case ran; with `--update` the
existing "every committed view must be redrawn" check stays as it is.

- [ ] **Step 1: Write the failing tests** (rework `ReferenceTest.references` to patch `reference_case_count` → 3 and
  `run_reference_case` instead of `run` for the binary; `run` stays patched for `pixi install`):
  - keep `test_a_failing_comparison_fails`, `test_update_takes_the_pictures_of_a_failing_run`,
    `test_update_refuses_a_run_that_didnt_draw_every_view` passing with case 2 drawing `a.png` and failing;
  - `test_every_case_runs_once`: the indices passed are exactly `[1, 2, 3]`;
  - `test_every_case_runs_when_one_fails`: case 1 fails → cases 2 and 3 still ran, then the run raises;
  - `test_outputs_print_in_case_order`: cases return `"one"`, `"two"`, `"three"` (finishing in any order) → stdout
    has them in that order;
  - `test_the_case_count_is_read_from_doctest`: `subprocess.run` patched to return that line → 25; output without it
    → `project_n.Fail`.
- [ ] **Step 2:** `pixi run project_n selftest` → the new tests fail.
- [ ] **Step 3: Implement** the two functions and the pool in `run_references`.
- [ ] **Step 4:** `pixi run project_n selftest` → pass.
- [ ] **Step 5: Check it for real:** `pixi run project_n reference` passes; time it (expected a few seconds, from 28 s).
  Then copy `tests/reference/outlines.png` over `tests/reference/cells.png`, run again → fails naming the cells case;
  `git checkout -- tests/reference/cells.png`; run again → passes.
- [ ] **Step 6: Commit** `git commit -m "build(tools): draw the reference pictures in parallel"`.

### Task 4: the MSVC build stops recompiling everything · review: full

**Files:**
- Modify: `tools/project_n.py` (`ensure_msvc` at ~229; a module constant `CL_WRAPPER` beside `MSVC_INSTALL`)
- Modify: `xmake.lua` (inside `if is_plat("windows") then` at ~92)
- Modify: `tools/AGENTS.md` (a Gotcha: why `cl.exe` is our wrapper and the build cache is off for MSVC off Windows)
- Test: `tools/test_project_n.py` (new `MsvcWrapperTest`)

**Interfaces:**
- Produces: `CL_WRAPPER: str` (the script) and `install_cl_wrapper(dest: Path) -> None`, called by `ensure_msvc` on
  every call (idempotent: rewrites only when the content differs; replaces the `bin/x64/cl.exe → cl` symlink with the
  script, mode 0o755).

Cause (spec §5): XMake asks cl for `/sourceDependencies <file>` JSON, which under Wine names headers as lowercased
`z:\…` paths XMake on Linux can't stat, so every object rebuilds every build. The wrapper drops that argument and its
file, so XMake's own probe finds no JSON and falls back to `/showIncludes`, which msvc-wine's `cl` script already turns
into Linux paths; then `exec "$(dirname "$0")/cl"` with the rest. XMake's build cache must be off for this toolchain
off Windows: it compiles the preprocessed file, whose `/showIncludes` lists no headers. In `xmake.lua`:
`if not is_host("windows") then set_policy("build.ccache", false) end` inside the Windows block, with a comment
giving that reason.

- [ ] **Step 1: Write the failing tests** (`@unittest.skipIf(os.name == "nt", "bash script")` on the script tests):
  - `test_the_wrapper_drops_source_dependencies_and_its_file`: in a temp dir, write `CL_WRAPPER` as `cl.exe` and a
    fake `cl` that prints its arguments one per line; run `cl.exe -c /sourceDependencies dep.json "a b.cpp"
    -Fosrc/sourceDependencies.obj -sourceDependencies x.json -nologo` → printed `-c`, `a b.cpp`,
    `-Fosrc/sourceDependencies.obj`, `-nologo`.
  - `test_the_wrapper_is_installed_over_the_symlink_once`: a temp `dest/bin/x64/` with `cl.exe` symlinked to `cl` →
    after `install_cl_wrapper(dest)` it's a regular file holding `CL_WRAPPER`, executable; a second call leaves its
    mtime unchanged.
- [ ] **Step 2:** `pixi run project_n selftest` → fail.
- [ ] **Step 3: Implement** `CL_WRAPPER`, `install_cl_wrapper`, the call in `ensure_msvc`, and the `xmake.lua` policy.
- [ ] **Step 4:** `pixi run project_n selftest` → pass.
- [ ] **Step 5: Check it for real** (saving each run's output to a file under `build/`, never piping XMake):
  1. `pixi run project_n test --preset windows-msvc --mode release` passes (a full build first).
  2. `pixi run project_n build --preset windows-msvc --mode release` again: no `compiling` line in its output.
  3. Append `// probe` to `src/gfx/renderer.hpp`, build again: exactly as many `compiling` lines as units including it
     (8 on 2026-11-12); `git checkout -- src/gfx/renderer.hpp`, build: the same count again.
  4. Time `pixi run project_n test --preset windows-msvc --mode release` with nothing changed: expected ~5 s (from 107 s).
- [ ] **Step 6: Commit** `git commit -m "build(xmake): stop the MSVC build recompiling everything under Wine"`.

### Task 5: the loop's gates and review sizes in AGENTS.md · review: light

**Files:**
- Modify: `AGENTS.md` ("Working with superpowers")

Content (wording the builder's, one line per rule, the file staying ~100 lines; today 78):
- Replace "Verification after each task: …" with the gates per level: an edit-build-test round runs only
  `test --suite <s> [--test <t>]`; a task's implementation done, and again after its review fixes, runs `test` and
  `lint --range <task base>..HEAD`; only the branch head (before the PR, and after the final review's fixes) runs
  `check`; sanitizers only when the user asks; never builds per commit. Keep the existing sentence on container gates
  from a worktree.
- Add reviews sized to the task: plans size each task for one implementer run of ~30 min and split bigger ones in the
  plan; plans mark each task `review: full` (default: game logic, simulation, rendering, determinism, saves, tooling
  with a verdict) or `review: light` (docs, data values, mechanical moves and renames, generated outputs: one review
  on Sonnet, consecutive light tasks may share one); one fix pass per review, its re-review scoped to the fix diff
  (Sonnet; Haiku for comment-only); the final whole-branch review on the strongest model, once.

- [ ] **Step 1:** Edit `AGENTS.md`.
- [ ] **Step 2:** `pixi run project_n doctor --docs` → no problem reported for AGENTS.md.
- [ ] **Step 3: Commit** `git commit -m "docs(agents): one gate per level and reviews sized to the task"`.

(The controller then points the memories `lint-at-milestones` and `verify-per-task-not-per-commit` at AGENTS.md; memory
is outside the repo.)

### Task 6: measure, and the Outcome · review: light (the final whole-branch review follows)

**Files:**
- Modify: `docs/superpowers/specs/2026-11-12-dev-loop-speedups-design.md` (Status: implemented; Outcome)

- [ ] **Step 1:** `pixi run project_n check` passes; copy its step-time table (Task 2) into the Outcome, with a warm
  run after a one-line change to `src/core/types.hpp` (reverted after), against the spec's 437 s cold / 316 s warm.
- [ ] **Step 2:** A typical task's lint: append a comment to `src/gfx/renderer.hpp`, time `pixi run project_n lint`
  (units and seconds, against 127 units / 71 s), revert.
- [ ] **Step 3: The user's performance condition.** The code difference comes from the build cache alone (the
  dependency format doesn't change code generation), so "before" is the branch's sources built with the cache on. In
  one msvc container run (`container_cmd(preset_container("windows-msvc"))`, a script through `pixi run python`, as
  the spec's measurements did): configure a scratch output `build/bench-cmp/before-build` with `--ccache=y` and one
  `build/bench-cmp/after-build` as the branch configures it (cache off), `xmake build headless_bench` in each, copy each
  `windows/x64/release` folder to `build/bench-cmp/before` and `…/after`; then run each `headless_bench.exe`
  alternately 10 times under `wine` (absolute paths, `XDG_RUNTIME_DIR` set, the run's folder as cwd); its non-zero exit
  for the over-budget 2-worker tick under Wine is expected. Table the means of ticks, combat ticks and month steps per
  worker count. Expected: within noise (±~1–2%), as on 2026-11-12. A difference beyond noise stops the branch and goes
  to the user. Remove `build/bench-cmp` afterwards.
- [ ] **Step 4:** Write the Outcome (measurements, rulings from the ledger with their reasons), set Status to
  implemented, commit `git commit -m "docs(design): record the faster loop's outcome"`.
