# Faster CI and Local Builds Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A branch push goes green in ~5 minutes (10 at most) and local test rounds and fresh worktrees get faster,
while main and tag runs always compile our code cold.

**Architecture:** All logic in `tools/project_n.py` (one file, standard library only), tested in
`tools/test_project_n.py`; an XMake option in `xmake.lua` turns the build cache on explicitly; `tests/xmake.lua`
registers one test per test file so `xmake test` runs them in parallel; `.github/workflows/ci.yml` only restores and
saves cache folders and runs `pixi run project_n <command>`.

**Tech Stack:** Python 3 (standard library), XMake 3.1 (build cache: `private/cache/build_cache.lua`), doctest,
clang-tidy, GitHub Actions (`actions/cache` restore/save), Podman, msvc-wine, Mesa lavapipe.

**Spec:** `docs/superpowers/specs/2026-11-13-ci-and-local-speed-design.md` (approved 2026-11-13). Its "Where the time
goes" tables are the baseline every measurement compares against.

## Global Constraints

- **Cold main and tags** (the user): on `GITHUB_REF` `refs/heads/main` or `refs/tags/*`, every build of our code
  compiles from source: no object cache read, no build folder reused, no build reading objects another build in the
  same run wrote. Third-party packages still come from the package cache. A tag writes no cache either.
- No test or check is skipped, loosened or dropped; no required job carries an `if:` (a step may).
- No compile flag of a shipped build changes, except path-mapping flags (`-ffile-prefix-map` and the like), allowed in
  every mode (Task 9).
- `project_n.py` stays one standard-library file; new logic test-first in `tools/test_project_n.py`; `selftest` runs on
  Windows too (skip bash-only tests with `os.name == "nt"`). Never pipe XMake's output through a filter.
- Workflows hold no logic beyond setup: cache steps and `pixi run project_n …` only; actions pinned by SHA; restore and
  save as separate steps, save `if: always()`; no `pull_request` trigger; Dependabot's read-only token must not fail
  a cache step (an `actions/cache/save` needs no write token beyond the default; keep `PROJECT_N_PUSH_IMAGES` as is).
- Gates (root AGENTS.md): an edit round runs `pixi run project_n selftest` or `test --suite <s>`; each task ends with
  `pixi run project_n test` and `pixi run project_n lint --range <task base>..HEAD`; the branch head runs `check`.
  Container gates run as CI's jobs on the branch (worktree).
- Pushing: `git push --no-follow-tags` (the user's global `push.followTags`); if SSH has no key, push once over HTTPS
  with `gh auth git-credential` through `GIT_CONFIG_COUNT` environment variables (no config change).
- Measurement commits pushed to time CI (Task 11) are dropped from the branch before the PR (rebase, force-push the
  branch only).
- Commits: Conventional Commits (git-commit skill); scopes from `SCOPES` (`tools`, `xmake`, `tests`, `agents`,
  `design`, `game`, `gfx`); workflow changes are type `ci` without a scope; `fix`/`feat` only for game code.
- Update the owning AGENTS.md in the same commit (tools/, tests/, .github/); docs cite commits by subject.

## Review Focus

1. **A test file whose cases run in no process, or in two:** `*log_tests.cpp` also matches `sdl_log_tests.cpp`,
   `*clock_tests.cpp` matches `tick_clock_tests.cpp`; a file outside every pattern must still run (Task 3 tests both).
2. **A cold ref with a non-empty object cache or a reused build folder** (a workflow edited to restore on main): the
   build refuses, inside the container too (Task 5 tests `GITHUB_REF` reaching the container and the refusal).
3. **The container never sees the cache**: `PROJECT_N_BUILD_CACHE` set outside but not mounted or forwarded, so CI
   silently compiles cold forever (Task 5 tests `container_cmd`'s mount and environment).
4. **A stale clang-tidy pass**: a changed nested `.clang-tidy`, a clang-tidy upgrade or a changed flag must miss the
   cache (Task 7 tests each key input).
5. **A failing job's cache**: objects compiled before a later step failed are still saved on a branch, never on a tag
   (Task 6's workflow review; actionlint).

## Waves

Tasks sharing a file stay in order; `tools/project_n.py` tasks are serial.

| Wave | Tasks | Needs |
|---|---|---|
| 1 | 1 (Mesa race), 2 (slow game case), 4 (reference drift; joins the `project_n.py` lane if its fix lands there) | — |
| 2 | 3 (parallel test files) | 1 (project_n.py) |
| 3 | 5 (object cache in project_n) | 3 |
| 4 | 6 (CI cache steps), 8 (lock reorder) | 5 |
| 5 | 7 (clang-tidy cache + lint noise) | 6, 8 |
| 6 | 9 (shared local cache spike), 10 (MSVC cache spike) | 7; 10 also 6 |
| 7 | 11 (measure and split CI jobs) | 9, 10 |
| 8 | 12 (Outcome, docs, backlog) | 11 |

---

### Task 1: Each reference case gets its own runtime directory — review: full

**Files:** Modify `tools/project_n.py` (`run_reference_case`), `tools/test_project_n.py` (`ReferenceTest`),
`tools/AGENTS.md` (the `reference` gotcha).

**Interfaces:** Produces nothing new; `run_reference_case(exe, env, index) -> (str, int)` keeps its signature.

- [ ] **Step 1: Write the failing test** `test_each_case_gets_its_own_runtime_directory` in `ReferenceTest`: patch
  `project_n.subprocess.run` with a fake that records `kwargs["env"]["XDG_RUNTIME_DIR"]`, asserts the directory exists
  and is mode `0o700` during the call, and returns `CompletedProcess([], 0, "")`; call `run_reference_case` for
  indices 1 and 2 with `env={"XDG_RUNTIME_DIR": "/shared"}`. Assert the two recorded paths differ, neither is
  `/shared`, and neither exists afterwards. Add `test_the_runtime_directory_goes_when_a_case_crashes` (fake returns
  code -11: the directory is still removed).
- [ ] **Step 2:** `pixi run project_n selftest` → both FAIL.
- [ ] **Step 3: Implement** in `run_reference_case`: a `tempfile.TemporaryDirectory(prefix="project_n-xdg-")` around
  the `subprocess.run`, passing `{**env, "XDG_RUNTIME_DIR": tmp}`. Comment why: without it every case makes Mesa
  create `/tmp/xdg-runtime-mesa-<uid>` at once, and the loser crashes (main, 2026-11-13).
- [ ] **Step 4:** `pixi run project_n selftest` → PASS; `pixi run project_n reference` → 30 cases pass.
- [ ] **Step 5:** Add a clause to tools/AGENTS.md's `reference` gotcha; commit
  `build(tools): give each reference case its own runtime directory`.

### Task 2: The slowest game case without its wasted frames — review: full

**Files:** Modify `tests/unit/game/combat_tests.cpp` ("a scene change mid-volley ends every open use", line ~1160).

- [ ] **Step 1: Measure.** `pixi run project_n test --suite game_tests -- --duration=true` (linux-clang debug): note
  this case's seconds (6.3 s on 2026-11-13) and the next two.
- [ ] **Step 2: Find the waste.** Profile the case alone (`-- --test-case='a scene change mid-volley*'` under `perf` or
  by timing its phases): which ticks or helpers (`ReadyBolt`, `Play`, `Published`, the burn tail) cost the time.
- [ ] **Step 3: Remove only waste**: fewer ticks to reach the same states, or cheaper setup that reaches the same
  arena. Every `REQUIRE`/`CHECK` stays, with the same expected values; the behaviour named in the test's name is still
  what it proves (a volley in flight and a burn between its ticks when the scene changes). If nothing is wasteful,
  change nothing and report the profile.
- [ ] **Step 4:** `pixi run project_n test --suite game_tests` passes; re-measure as in Step 1.
- [ ] **Step 5:** Commit `test(game): reach the scene change mid-volley in fewer frames`, the before/after seconds in
  the body.

### Task 3: Each test file runs in its own process — review: full

**Files:** Modify `tests/xmake.lua` (`unit_tests`), `tools/project_n.py` (`cmd_test`, `test_names`),
`tools/test_project_n.py`, `tests/AGENTS.md`, `tools/AGENTS.md`.

**Interfaces:**
- Produces: each unit test target registers one XMake test per `*_tests.cpp` file, named after the file's stem
  (`combat`, `save`, …), plus one named `rest` that runs every case outside those files;
  `test_file_pattern(stem) -> str`, the doctest `--source-file` value one file's test uses, in `project_n.py`
  (mirrored in `tests/xmake.lua`).

- [ ] **Step 1: Write the failing tests** in a new `TestFilesTest`:
  - `test_a_file_pattern_matches_only_its_file`: for `log` and `clock` (real clashes: `sdl_log_tests.cpp`,
    `tick_clock_tests.cpp`), `fnmatch` each comma-separated part of `test_file_pattern(stem)` against
    `/src/tests/unit/core/log_tests.cpp`, `z:\src\tests\unit\core\log_tests.cpp` (Wine) and the clashing file's two
    spellings: matches its own, never the other.
  - `test_no_suite_has_two_files_one_pattern_catches`: over `test_names()`, no file's pattern matches another file
    of the same suite.
  - `test_a_suite_without_filters_runs_its_file_tests`: `cmd_test` with `target="game_tests"`, no `--test`, no extra
    → runs `["xmake", "test", "--output-on-failure", "game_tests/*"]`; with `--test` or extra arguments it still
    runs `xmake run game_tests …` as today.
- [ ] **Step 2:** `pixi run project_n selftest` → FAIL.
- [ ] **Step 3: Implement.** `test_file_pattern(stem)` returns `*/<stem>_tests.cpp,*\<stem>_tests.cpp` (the leading
  separator stops `*log_tests.cpp` matching `sdl_log_tests.cpp`; both separators for Wine and MSVC). In
  `tests/xmake.lua`'s `unit_tests`, glob the target's test files at description time, `add_tests(stem, {runargs =
  {"--source-file=" .. pattern}})` per `*_tests.cpp`, and `add_tests("rest", {runargs =
  {"--source-file-exclude=" .. all patterns joined}})` so a case in any other file still runs (doctest passes a run that
  selects nothing). `test_under_wine` already reads `opt.runargs`. Confirm `xmake test` runs a target's tests in
  parallel and accepts `game_tests/*`; if the pattern syntax differs, use what `xmake test --help` documents.
- [ ] **Step 4:** `pixi run project_n selftest` → PASS. `pixi run project_n test` → every suite passes; the summary
  lists one line per file; total cases across a suite's lines equal the binary's `--count` (check `game_tests` by
  hand once). Measure `test`'s wall time against the 37 s / `game_tests` 20.9 s baseline.
- [ ] **Step 5:** `pixi run project_n test --preset windows-msvc` (Wine, one process per file) passes.
- [ ] **Step 6:** tests/AGENTS.md (the `unit_tests` rule: one XMake test per test file plus `rest`; a test file's
  name must not end another's in its suite unless its pattern still separates them), tools/AGENTS.md (`test --suite`
  without filters runs the suite's file tests in parallel). Commit `build(tests): run each test file in its own process`.

### Task 4: Reference pictures that drift — review: full

**Files:** `tests/reference/*.png`; whatever the cause lives in (likely `tests/reference/reference_tests.cpp`'s PNG
writing or a draw that isn't deterministic in `src/gfx/`); `tests/AGENTS.md` if a rule changes.

- [ ] **Step 1: Reproduce.** `pixi run project_n reference --update` twice on this branch's base with nothing changed;
  `git status tests/reference/` after each. List which files change, and whether their decoded pixels differ (compare
  with Pillow from Pixi's Python, outside the repo's code) or only their bytes.
- [ ] **Step 2: Find the cause** of `painted`, `cells` and `cells_far` changing every run. Bytes only → the encoder
  (settings, metadata, timestamps). Pixels → a nondeterministic draw: a bug in `src/`, fixed test-first with a test that
  fails because of it (root AGENTS.md).
- [ ] **Step 3: Fix it** so two `--update` runs in a row leave `git status` clean.
- [ ] **Step 4: Refresh the drifted pictures** (the 15 that differ within tolerance from what lavapipe draws now):
  `reference --update`, look at each changed picture against the old one (look-at-the-game skill), and keep only
  pictures whose change is the known drift.
- [ ] **Step 5:** `pixi run project_n reference` passes; a second `--update` changes nothing. Commit the fix (its own
  commit, `fix(gfx): …` if in `src/`, else `test(tests): …`/`build(tools): …`) and then
  `test(tests): refresh the reference pictures lavapipe draws today`.

### Task 5: The object cache in `project_n` — review: full

**Files:** Modify `tools/project_n.py` (`configure`, `build`, `container_cmd`, `cmd_artifact`), `xmake.lua` (new
option, the MSVC policy lines ~91-98), `tools/test_project_n.py`, `tools/AGENTS.md`.

**Interfaces — Produces:**
- `BUILD_CACHE_ENV = "PROJECT_N_BUILD_CACHE"`: where CI keeps object caches (a folder; unset locally).
- `cold_ref() -> bool`: `GITHUB_REF` is `refs/heads/main` or starts with `refs/tags/`.
- `tag_ref() -> bool`: `GITHUB_REF` starts with `refs/tags/`.
- `CI_CACHED_TOOLCHAINS = ("pixi-gcc", "pixi-clang")`: toolchains whose CI builds use the object cache (Task 10 may
  add `msvc`).
- `object_cache_dir(preset, mode, sanitizer="none", unity=False) -> Path | None`: `<BUILD_CACHE>/<preset>-<leaf>`
  (leaf as in `build_dir`), or `None` when the variable is unset, on a tag, or for a toolchain not in
  `CI_CACHED_TOOLCHAINS`.
- XMake option `build_cache` (bool, default false): when set, `set_policy("build.ccache", true)`, which beats XMake's
  own CI and Windows-`cl` switches; the msvc-wine `false` applies only when the option is unset.

- [ ] **Step 1: Write the failing tests** (new `ObjectCacheTest`, patching `os.environ` and `run`):
  - `test_no_cache_without_the_variable`: `object_cache_dir("linux-gcc", "release")` is `None`; `configure` passes no
    `--build_cache`.
  - `test_a_branch_build_uses_its_leaf_folder`: variable `/c`, `GITHUB_REF=refs/heads/ci-speed` → `Path("/c/linux-gcc-
    release")`; `configure`'s `xmake f` gets `--build_cache=y` and `--ccachedir=/c/linux-gcc-release`.
  - `test_msvc_stays_uncached`: `windows-msvc` → `None`.
  - `test_a_tag_uses_no_cache`: `refs/tags/v0.4.0` → `None`.
  - `test_main_writes_only_into_an_empty_folder`: `refs/heads/main`, empty leaf folder → cache flags passed; leaf
    folder holding a file → no cache flags (the build compiles from source and writes nothing).
  - `test_a_cold_ref_refuses_a_reused_build_folder`: `refs/heads/main`, the build folder already holds compiled
    objects → `build` raises `Fail` naming the folder; a missing or freshly cleaned folder builds.
  - `test_the_container_gets_the_cache_and_the_ref`: with the variable `/home/runner/build-cache` and `GITHUB_REF`
    set, `container_cmd` mounts the folder (`:Z`) at `/home/dev/build-cache` and passes
    `PROJECT_N_BUILD_CACHE=/home/dev/build-cache` and `GITHUB_REF`; with neither set, neither appears.
  - `test_artifact_on_a_tag_builds_without_a_cache`: `cmd_artifact` on `refs/tags/v1.0.0` with the variable set →
    no `--build_cache` reaches `xmake f`.
- [ ] **Step 2:** `pixi run project_n selftest` → FAIL.
- [ ] **Step 3: Implement** the interfaces above. In `configure`, when `object_cache_dir` is a folder and (not
  `cold_ref()` or the folder is empty), append `--build_cache=y --ccachedir=<folder>` and create it; set
  `XMAKE_ACTION_BUILD_CACHE=1` only if the option alone doesn't beat XMake's CI switch (verify by reading
  `build_cache.lua`'s order: project policy first). In `build`, on `cold_ref()`, refuse a build folder that already
  holds objects (XMake's `.objs` under it) unless empty. `cmd_artifact` already deletes its folder first. Confirm from
  XMake's `cachekey` that no flag of ours changes an object through `-D` alone without changing the preprocessed text
  (it ignores `-D` flags); write the finding in the comment above `object_cache_dir`.
- [ ] **Step 4:** `pixi run project_n selftest` → PASS. By hand: `PROJECT_N_BUILD_CACHE=$scratch/c GITHUB_REF=refs/
  heads/x pixi run project_n test --preset linux-clang --mode release` twice with `clean` between: the second build
  prints `cache compiling` hits for every unit and finishes in a fraction of the first's time; with
  `GITHUB_REF=refs/heads/main` the second refuses to reuse the cache folder (compiles from source).
- [ ] **Step 5:** tools/AGENTS.md: replace the "No compile cache is shared…" gotcha's CI half with the object cache
  (where, which toolchains, the cold rule). Commit `build(tools): keep CI's compiled objects between runs`.

### Task 6: CI restores and saves the object caches — review: full

**Files:** Modify `.github/workflows/ci.yml`, `.github/AGENTS.md`.

**Interfaces — Consumes:** `PROJECT_N_BUILD_CACHE` (Task 5).

- [ ] **Step 1:** Set `PROJECT_N_BUILD_CACHE` per job: `/home/runner/build-cache` on Linux (beside
  `PROJECT_N_XMAKE_CACHE`), a folder under the home directory on macOS and Windows, in each job's existing path style.
- [ ] **Step 2:** In `lint`, each `container` preset and `macos`, add (next to the package cache steps)
  `Restore objects` (`actions/cache/restore`, same pinned SHA, `if: github.ref_type == 'branch' && github.ref !=
  'refs/heads/main'`, key `objects-<job>-<preset>-${{ hashFiles('pixi.toml', 'pixi.lock', 'xmake-requires.lock',
  'xmake/repo/**', 'xmake.lua') }}-${{ github.sha }}`, restore-keys the same without the SHA) and `Save objects`
  (`if: always() && github.ref_type != 'tag'`, same key). `windows` gets the steps in Task 10 if MSVC is cached.
- [ ] **Step 3:** `pixi run project_n lint --range <base>..HEAD` (runs actionlint on `.github/` changes) passes.
- [ ] **Step 4:** .github/AGENTS.md: a third cache layer, objects (warm on branch pushes only; main restores nothing
  and saves its cold build; tags neither; `project_n` enforces the cold rule). Commit `ci: keep compiled objects
  between branch runs`.
- [ ] **Step 5: Push** the branch twice (this commit, then the next task's): the second run's container, lint and
  macOS builds report cache hits; record each job's step times as in the spec's table, and `pixi run gh cache list`'s
  sizes (stay well under 10 GB with the package caches).

### Task 7: clang-tidy's results cached, and lint's ignored finding unprinted — review: full

**Files:** Modify `tools/project_n.py` (`tidy_one`, `tidy_units`, `cmd_lint`), `tools/test_project_n.py`
(`ParallelTidyTest`, new `TidyCacheTest`), `tools/AGENTS.md`.

**Interfaces — Produces:**
- `tidy_cache_dir(bdir) -> Path | None`: `<BUILD_CACHE>/tidy` when the variable is set, else `<bdir>/.tidy-cache`;
  `None` on a tag.
- `tidy_key(bdir, unit) -> str`: SHA-256 over the unit's preprocessed text (its compile database command with `-E`,
  output to a pipe), that command's flags without the input and output paths, `clang-tidy --version`'s output, and the
  paths and contents of every `.clang-tidy` in the repository, sorted by path.
- `tidy_one(bdir, unit, cache=None) -> (str, int)`: returns the cached `(output, returncode)` when reading is allowed
  (`cache` given and not `cold_ref()`), else runs clang-tidy and stores the result in `cache` (unless on a tag).

- [ ] **Step 1: Write the failing tests:** `test_a_cached_unit_is_not_linted_again` (second call doesn't run
  clang-tidy, same output and code); `test_each_key_input_misses` (a changed preprocessed text, flag, clang-tidy
  version or nested `.clang-tidy` each gives another key); `test_main_reads_no_cached_result` (`GITHUB_REF=refs/heads/
  main`: runs clang-tidy although a result is cached, and stores it); `test_a_tag_stores_nothing`;
  `test_an_ignored_finding_is_counted_not_printed` (`tidy_units` over output holding the listed Quill finding and its
  notes: nothing of it printed, `ignored == 1`, not failed); `test_a_failing_cached_unit_is_still_fixed` (with `fix`,
  a failing unit served from the cache still goes to `tidy_fix`).
- [ ] **Step 2:** `pixi run project_n selftest` → FAIL.
- [ ] **Step 3: Implement** the interfaces; `cmd_lint` passes `tidy_cache_dir(bdir)` through `tidy_units`. Cache files
  are `<key>.json` holding `{"output": …, "returncode": …}`; a corrupt file counts as a miss. In `tidy_units`, skip
  printing a finding block whose finding is listed (`listed_finding`); keep counting it.
- [ ] **Step 4:** `pixi run project_n selftest` → PASS; `pixi run project_n lint --all` twice: the second run lints no
  unit through clang-tidy and prints the same findings (none) and no Quill report; time both.
- [ ] **Step 5:** tools/AGENTS.md's lint gotcha gains the cache (key, cold rule, where) and loses nothing else.
  Commit `build(tools): keep clang-tidy's results for unchanged units`; push and confirm CI's `lint` hits on the next
  run.

### Task 8: `xmake-requires.lock` keeps its order — review: light

**Files:** Modify `tools/project_n.py` (`configure`, `build`), `tools/test_project_n.py`, `tools/AGENTS.md`.

**Interfaces — Produces:** `restore_reordered_lock(before: bytes) -> None`: if `xmake-requires.lock` now differs from
`before` but holds the same lines in another order, write `before` back.

- [ ] **Step 1: Write the failing tests:** a reordered file is restored byte for byte; a file with a changed or added
  line is kept; an unchanged file isn't written (its mtime stays).
- [ ] **Step 2:** `pixi run project_n selftest` → FAIL.
- [ ] **Step 3: Implement** and call it after `xmake f` in `configure` and after `xmake build` in `build`, with the
  bytes read before each.
- [ ] **Step 4:** PASS; a `test` run that used to reorder the lock leaves `git status` clean.
- [ ] **Step 5:** tools/AGENTS.md: the `__meta__` gotcha now says `project_n` restores it. Commit
  `build(tools): restore xmake-requires.lock when a build only reorders it`.

### Task 9: Spike, then adopt or record: one compile cache for every local worktree — review: full

**Files:** Possibly `xmake.lua` (path-mapping flags), `tools/project_n.py` (a shared `--ccachedir`, its pruning in
`clean`), `tools/test_project_n.py`, `tools/AGENTS.md`.

- [ ] **Step 1: Baseline.** In a fresh worktree (`git worktree add .worktrees/probe`, `pixi run project_n setup`):
  time `pixi run project_n test` (linux-clang debug) and `pixi run project_n check`'s builds, cold.
- [ ] **Step 2: Spike.** Point both worktrees at one cache folder (`build.ccache.global_storage` or `--ccachedir` under
  `~/.xmake/project_n/`); find every per-worktree input of XMake's key: the compiler path (each worktree has its own
  `.pixi`), absolute `-I` flags, preprocessed line markers, debug-info paths. Try `-ffile-prefix-map=<ROOT>=.` (allowed
  in every mode), relative include paths, and whatever makes the compiler path stable. Measure the hit rate
  (`cache compiling` lines) in the second worktree.
- [ ] **Step 3: Check correctness.** A hit must give the same object a cold compile gives in that worktree (compare
  `.o` bytes or `objdump -d` plus debug line tables), gdb must open the right worktree's source for a cached object, and
  the release artefact's code must not change (`artifact`'s binary differs only in paths, if at all).
- [ ] **Step 4: Decide.** Hits correct and large → adopt test-first (tests for the shared folder and its size limit
  or pruning in `clean`), Task 5's CI folders untouched. Otherwise → change nothing but tools/AGENTS.md: why not, with
  the numbers, replacing "~10 s per preset and mode". Remove the probe worktree.
- [ ] **Step 5:** `pixi run project_n test` and `lint --range`; commit (`build(xmake): …`/`build(tools): …` or
  `docs(agents): …`).

### Task 10: Spike, then adopt or record: MSVC's objects in CI — review: full

**Files:** Possibly `xmake.lua`, `tools/project_n.py` (`CI_CACHED_TOOLCHAINS`), `.github/workflows/ci.yml` (the
`windows` job's object steps), `tools/test_project_n.py`, `.github/AGENTS.md`, `tools/AGENTS.md`.

- [ ] **Step 1: container windows-msvc.** With `build_cache` set the msvc-wine `false` no longer applies (Task 5). On
  a scratch push, add `msvc` to `CI_CACHED_TOOLCHAINS`: run twice; check hits, and that two clean artefacts (cached and
  uncached) differ only where two uncached ones already do (PE timestamp at 0x120, the `.pdb` record).
- [ ] **Step 2: windows (native).** The same on the `windows` job: cold and warm build times with the policy on
  (XMake turns it off for `cl` because preprocessing is slow).
- [ ] **Step 3: Decide per job:** adopt where a warm build is clearly faster and correct (tests for the toolchain
  list; the job's object steps as in Task 6); otherwise revert and record why in .github/AGENTS.md. Locally nothing
  changes for MSVC (the `/showIncludes` reason).
- [ ] **Step 4:** `selftest`, `lint --range`; commit `build(tools): keep MSVC's objects between CI runs` or the
  record.

### Task 11: Measure every job, then split what still runs long — review: full

**Files:** Modify `.github/workflows/ci.yml`, `.github/rulesets/main.json`, `.github/AGENTS.md`, root `AGENTS.md`
(the six checks it names), possibly `tools/project_n.py` (`artifact` reusing a release build on branch pushes).

- [ ] **Step 1: Measure.** Three runs on the branch head with a warm cache: one re-run of the same commit
  (`pixi run gh workflow run ci --ref ci-speed`) and two measurement commits touching (a) one `.cpp` and (b) a widely
  included header's comment (`src/core/types.hpp`), each dropped before the PR. Table each job's wall time and steps.
- [ ] **Step 2: Split** each job whose median is over 5 minutes along its builds, one build per job: native Windows
  into release tests and ASan; macOS into release tests plus `artifact` (reusing that release build on branch pushes
  only, refusing reuse on cold refs as in Task 5, test-first) and ASan; linux-clang's releasedbg into its own job. If
  one build per job is still over, build in one job and test in another, or spread a suite's file tests across jobs;
  stop at a median within 10 minutes and record why 5 wasn't reached. Names may change freely; every job is required
  and has no `if:`.
- [ ] **Step 3:** Update `.github/rulesets/main.json`'s required checks to the new names, .github/AGENTS.md's job
  list, and the root AGENTS.md's "six checks". Do **not** apply the ruleset: the user applies it just before merging.
- [ ] **Step 4:** Measure again (three runs as in Step 1); actionlint passes. Commit `ci: split the jobs that ran
  longest`.

### Task 12: The Outcome, the docs, the backlog — review: light

**Files:** Modify the spec (Outcome, status implemented), `tools/AGENTS.md`/`tests/AGENTS.md`/`.github/AGENTS.md`
where a number or rule went stale; the project memory's `sweep-up-backlog.md` (outside the repo).

- [ ] **Step 1:** Write the spec's Outcome: before/after tables (CI per job, three runs; local `test`, `check`, a fresh
  worktree), every ruling with its measurement, spike results, what's left.
- [ ] **Step 2:** Prune the backlog: delete "lint only the `.cpp` files…" and the check-time PRIORITY; mark "CI speed,
  another pass" done with the Outcome's headline numbers; move any new deferral there.
- [ ] **Step 3:** `doctor --docs` passes; commit `docs(design): record what faster CI and local builds built`.
- [ ] **Step 4 (PR, with the user):** `check` at the head, final review, push; the user applies the new ruleset just
  before merging; after the merge, open branches rebase.
