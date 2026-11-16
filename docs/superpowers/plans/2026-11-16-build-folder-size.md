# A Smaller Build Folder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `build/` stops growing without bound: one precompiled header per header per build folder, build trees unused
for 7 days removed automatically, and XMake's build cache aged.

**Architecture:** A project XMake rule replaces `set_pcxxheader`, building `src/pch.hpp` and `tests/unit/pch.hpp`
once per build folder and pointing every target at them, switched over one toolchain at a time behind a go/no-go
(Clang, GCC, MSVC); the three one-off defines move into a generated header first so every target's flags agree.
`project_n.py` stamps each build tree in `configure()`, and `build`, `test` and `check` sweep stale trees and old
cache entries at their start.

**Tech Stack:** XMake 3.1.2 (Lua rules), Python 3 standard library (`tools/project_n.py`, `unittest` selftests),
Clang, GCC, MSVC under msvc-wine.

**Spec:** `docs/superpowers/specs/2026-11-16-build-folder-size-design.md`

## Global Constraints

- Every tool through Pixi; XMake only through `project_n`; no new dependency; exact pins unchanged.
- The spec's constants: stale tree age **7 days**, cache entry age **14 days**, warm trees: `NATIVE_PRESET`'s
  `debug` and `release`.
- A changed header or `pch.hpp` never leaves an object stale; a toolchain where sharing can't be proven safe keeps one
  PCH per target as today, with its reason in the Outcome (the user's option (a)).
- The sweep never touches anything outside the checkout's own `build/`, never `profiles/`, `measure/`, `artifact/`,
  `reference/`, `fuzz/`, `coverage/`, `docs/`, `doxygen-lint/`, never `PROJECT_N_BUILD_CACHE`, and never runs in CI
  (`GITHUB_ACTIONS`) or inside a container (`IN_CONTAINER`: the host already swept before re-entering).
- Commits: `build(xmake)` for xmake.lua/rules, `build(tools)` for project_n.py, `docs` for docs alone; AGENTS.md and
  design 0001 lines change in the same commit as the behaviour they describe. Code comments say why, never cite the
  spec, plan or tasks.
- Per-task gate: `pixi run project_n test` and `pixi run project_n lint --range <task base>..HEAD`. Measurements run
  alone (no other implementer building).
- Container builds from a worktree: try `build --preset windows-msvc`; if the container can't use the worktree, stop
  and ask the user (the main checkout holds another branch's work).

## Review Focus

1. A tree in use by a concurrent `project_n` command (e.g. `check` running in another terminal) is never swept: it
   was stamped when that command configured it, so it is fresh. Test: `sweep_build` keeps a tree stamped now.
2. A partial or unreadable stamp (empty file, garbage text) falls back to the newest-file time instead of crashing
   or sweeping. Test in Task 2.
3. `build/` missing, empty, or holding a preset folder with stray files (not trees) sweeps nothing and doesn't fail.
   Test in Task 2.
4. A cache entry `<key>` and its `<key>.txt` go together, never one without the other. Test in Task 3.
5. Editing `pch.hpp` itself, not only a header it includes, rebuilds every object on each toolchain. Verification
   step in Tasks 4–6.

## Waves

| Wave | Tasks | Needs |
|---|---|---|
| 0 | 0 (baseline) | nothing; runs alone |
| 1 | 1 (defines), 2 (stamps and sweep) | wave 0 |
| 2 | 3 (cache ageing), 4 (shared PCH: Clang) | 3 needs 2 (same files); 4 needs 1 |
| 3 | 5 (GCC) | 4 |
| 4 | 6 (MSVC) | 5 |
| 5 | 7 (after-measurements, Outcome) | all; runs alone |

---

### Task 0: Baseline measurements (review: light)

**Files:** none committed; results into the ledger (`.superpowers/sdd/`) for Task 7.

- [ ] **Step 1:** In the worktree, `pixi run project_n clean`, then time cold builds, alone on the machine:
  `time pixi run project_n build --mode debug` (linux-clang), `time pixi run project_n build --preset linux-gcc --mode release`,
  `time pixi run project_n build --preset windows-msvc --mode release`.
- [ ] **Step 2:** Record per build folder: count and total size of `*.pch` (Clang; MSVC's `*_v145.pch`) and `*.gch` (GCC), and
  `du -sh` of each tree.
- [ ] **Step 3:** Time an incremental rebuild after `touch src/core/types.hpp` on linux-clang debug, and record how
  many objects rebuilt (XMake's output count).

### Task 1: Move the one-off defines into a generated header (review: full)

**Files:**
- Modify: `xmake.lua` (core's `on_load` `PN_GAME_VERSION`; `project_n`'s debug `PN_SOURCE_DATA`)
- Modify: `tests/xmake.lua:81` (`PN_FUZZ_CORPUS` on `game_tests`)
- Create: `src/build_info.hpp.in` (template, XMake `add_configfiles`), generated into each build folder
- Modify: `src/core/replay.cpp`, `src/main.cpp`, `tests/unit/game/save_tests.cpp` (include the generated header)

**Interfaces:**
- Produces: a generated header `build_info.hpp` (in the build folder, on the include path of `core`, `project_n` and
  `game_tests` only) defining `PN_GAME_VERSION` always, `PN_FUZZ_CORPUS` always, and `PN_SOURCE_DATA` in debug only.
  After this task no target has a `-D` the others lack.

Why a header and not per-file defines: MSVC warns C4605 when a `/D` on a file's command line wasn't set when the PCH
was built, and `/WX` makes that an error; a header changes no flag.

- [ ] **Step 1:** Write the generated header's template and wire `add_configfiles` / `set_configdir` so it lands in
  the build folder (not the source tree), with `${define PN_SOURCE_DATA}`-style lines so release leaves it undefined.
- [ ] **Step 2:** Replace the three `add_defines` with the include in the three source files.
- [ ] **Step 3:** Verify flags agree: regenerate `build/compile_commands.json` (`pixi run project_n build`) and run
  the comparison below; expected: every target prints `+ [] - []`.

```bash
pixi run python - <<'EOF'
import json, re, shlex, collections
cc = json.load(open('build/compile_commands.json'))
by = collections.defaultdict(set)
for e in cc:
    args = e.get('arguments') or shlex.split(e['command'])
    t = re.search(r'\.objs/([^/]+)/', e.get('output', '') or ' '.join(args)).group(1)
    by[t] |= {a for a in args if a.startswith(('-D', '-f', '-std', '-O', '-g', '-m', '-W'))}
base = by['app']
for t, fl in sorted(by.items()):
    print(t, '+', sorted(fl - base), '-', sorted(base - fl))
EOF
```
  Only the fuzz targets (Task 4) may differ, and only in the address-sanitizer build.
- [ ] **Step 4:** `pixi run project_n test` passes (save tests read `PN_FUZZ_CORPUS`; replay tests the version);
  the generated header in `build/linux-clang/debug` defines `PN_SOURCE_DATA` and the one in
  `build/linux-clang/release` doesn't (`grep PN_SOURCE_DATA` on each, after `build --mode release`).
- [ ] **Step 5:** Commit `build(xmake): give the version and data paths their own generated header`.

### Task 2: Stamp build trees and sweep stale ones (review: full)

**Files:**
- Modify: `tools/project_n.py` (`configure`, `cmd_build`, `cmd_test`, `cmd_check`, `cmd_clean`, its parser)
- Test: `tools/test_project_n.py` (new `class BuildSweepTest`)
- Modify: `tools/AGENTS.md` (Commands: the sweep, `clean --stale`, what is never swept), root `AGENTS.md` (`clean`)

**Interfaces:**
- Produces, in `tools/project_n.py`:
  - `STALE_DAYS = 7`, `WARM_LEAVES = ("debug", "release")`, `USED_STAMP = ".project_n-used"`
  - `build_trees(build: Path) -> list[Path]`: every `build/<preset>/<leaf>` and `build/container/<preset>/<leaf>`
    directory whose `<preset>` is in `PRESETS`
  - `mark_used(bdir: Path, now: float) -> None`: writes `now` into `bdir / USED_STAMP` (creating `bdir`)
  - `last_used(tree: Path) -> float`: the stamp's float; on a missing or unreadable stamp, the newest file's mtime
    in the tree (0.0 if none)
  - `warm_trees(build: Path) -> set[Path]`: `build/NATIVE_PRESET/<leaf>` for `WARM_LEAVES`
  - `sweep_build(build: Path, now: float, days: int = STALE_DAYS, keep: Iterable[Path] = ()) -> list[tuple[Path, int]]`:
    removes each tree older than `days` not in `warm_trees(build) | set(keep)`, returns (tree, bytes) removed, prints
    one line per tree and a total
  - `sweep_here(keep: Iterable[Path] = ()) -> None`: `sweep_build(ROOT / "build", time.time(), keep=keep)` unless
    `IN_CONTAINER` or `GITHUB_ACTIONS` is set

- [ ] **Step 1: Write the failing tests** in `BuildSweepTest` on a `tempfile.TemporaryDirectory()` build folder
  (never the real `build/`), with times passed in:
  - `test_sweeps_a_tree_older_than_seven_days`: `linux-gcc/debug` stamped `now - 8*86400` is removed and returned.
  - `test_keeps_a_tree_used_today`: stamped `now - 3600` stays.
  - `test_keeps_warm_trees_however_old`: `linux-clang/debug` and `linux-clang/release` (patch `NATIVE_PRESET` to
    `linux-clang`) stamped 30 days ago stay; `linux-clang/releasedbg` goes.
  - `test_keeps_the_tree_about_to_be_used`: an old tree passed in `keep` stays.
  - `test_sweeps_container_trees`: `container/windows-msvc/release` 8 days old goes.
  - `test_unstamped_tree_uses_its_newest_file`: no stamp, one file with mtime `now - 2*86400` → stays; mtime
    `now - 9*86400` → goes.
  - `test_unreadable_stamp_falls_back`: stamp holding `"garbage"` or empty → newest-file time, no exception.
  - `test_ignores_what_no_command_owns`: `profiles/`, `measure/`, `coverage/`, `artifact/linux-gcc` (not under a
    preset/leaf pair the sweep owns: `artifact` is not in `PRESETS`) all 30 days old stay.
  - `test_missing_or_empty_build_folder`: `sweep_build` on a missing path and on an empty one returns `[]`.
  - `test_sweep_here_skips_ci_and_containers`: with `GITHUB_ACTIONS=true` or `IN_CONTAINER` patched true,
    `sweep_build` is not called.
  - `test_configure_stamps_its_tree`: `configure()` with `run` patched calls `mark_used` on its `build_dir`.
- [ ] **Step 2:** `pixi run project_n selftest`: the new tests fail (names not defined).
- [ ] **Step 3:** Implement the interfaces above; call `mark_used(ROOT / bdir, time.time())` in `configure()`;
  call `sweep_here(keep=[<the tree(s) the command will build>])` at the start of `cmd_build`, `cmd_test` and once at
  the start of `cmd_check` (before `selftest`; keep: none beyond the warm trees, as check's own builds stamp as they
  configure); add `clean --stale [--days N]` running `sweep_build` with `days` (default `STALE_DAYS`).
- [ ] **Step 4:** `pixi run project_n selftest`: all pass.
- [ ] **Step 5:** Update `tools/AGENTS.md` (one Commands line: stamp, sweep at start of build/test/check, warm trees,
  `clean --stale`, never-swept folders, user deletes `profiles/` and `measure/` themselves) and root `AGENTS.md`'s
  command list (`clean [--stale [--days N]]`). `pixi run project_n doctor --docs` passes.
- [ ] **Step 6:** Commit `build(tools): stamp each build tree and sweep those unused for a week`.

### Task 3: Age XMake's build cache (review: full)

**Files:**
- Modify: `tools/project_n.py` (beside `sweep_build`; the `BUILD_CACHE_ENV` comment)
- Test: `tools/test_project_n.py` (`class BuildCacheAgeTest`)
- Modify: `tools/AGENTS.md` (the no-shared-cache gotcha: caches are now aged locally)

**Interfaces:**
- Consumes: `build_trees`, `sweep_build`, `sweep_here` (Task 2).
- Produces:
  - `CACHE_DAYS = 14`
  - `uses_build_cache(tree: Path) -> bool`: the tree's preset's toolchain is `pixi-clang` (GCC never; MSVC never
    under Wine or on Windows' `cl`, per xmake.lua and tools/AGENTS.md); `tree`'s preset is its parent folder's name
  - `age_build_cache(tree: Path, now: float, days: int = CACHE_DAYS) -> list[int]`: removes `tree/.build_cache`
    entirely when `not uses_build_cache(tree)`; otherwise removes each `<key>` written more than `days` ago together
    with its `<key>.txt` (and an orphan `<key>.txt` older than `days`), then empty folders (`remove_empty_folders`);
    returns the removed sizes
  - `sweep_build` also runs `age_build_cache` on every tree it keeps, adding the freed total to its summary line

- [ ] **Step 1: Write the failing tests:**
  - `test_removes_dead_cache_of_gcc_and_msvc`: `.build_cache` under `linux-gcc/debug` and
    `container/windows-msvc/release` (fresh files) is removed entirely.
  - `test_ages_clang_entries`: under `linux-clang/debug/.build_cache/ab/`, `key1` + `key1.txt` written 15 days ago go;
    `key2` + `key2.txt` written 1 day ago stay.
  - `test_never_splits_an_entry`: `key3` old with `key3.txt` new → both go; `key4` new with `key4.txt` old → both stay.
  - `test_leaves_ci_cache_alone`: a `PROJECT_N_BUILD_CACHE` folder outside `build/` with old files is untouched.
- [ ] **Step 2:** `pixi run project_n selftest`: the new tests fail.
- [ ] **Step 3:** Implement; reword the comment above `BUILD_CACHE_ENV` ("each build folder keeps XMake's own
  cache": now aged after `CACHE_DAYS`).
- [ ] **Step 4:** `pixi run project_n selftest`: all pass. Then on the real tree:
  `pixi run project_n clean --stale` prints the trees and cache bytes removed and keeps `linux-clang/debug` and
  `release`.
- [ ] **Step 5:** Update tools/AGENTS.md's no-shared-cache gotcha; commit
  `build(tools): age XMake's build cache and drop the ones nothing reads`.

### Task 4: One precompiled header per build folder, Clang first (review: full)

**Files:**
- Create: `xmake/rules/shared_pch.lua` (or inline in `xmake.lua`; the builder's call, recorded in the ledger)
- Modify: `xmake.lua:261-318` (the seven `set_pcxxheader("src/pch.hpp")`), `tests/xmake.lua:70,98,107,121`
- Modify: `docs/design/0001-repository-build-environment.md` rule 19

**Interfaces:**
- Consumes: Task 1 (no target-wide define differs).
- Produces: a rule (name `shared_pch`) taking the header: `add_rules("shared_pch", {header = "src/pch.hpp"})`;
  on toolchains not yet switched over it falls back to `set_pcxxheader` with that header, so Tasks 5 and 6 only flip
  the toolchain on.

- [ ] **Step 1:** Read XMake's `private/action/build/pcheader.lua` and `rules/c++/precompiled_header/xmake.lua`
  (in `.pixi/envs/default/share/xmake/`) and its `build_cache.lua` handling of `-include`/`-include-pch`; write in
  the ledger how the rule will (a) build each header once per build folder before its users, (b) make it a
  dependency of every object using it, (c) keep the compile database's `-include` for clangd and clang-tidy.
- [ ] **Step 2:** Implement for `pixi-clang`: one `.pch` per header per build folder, users get `-include-pch`.
  The fuzz targets keep one of their own (`-fsanitize=fuzzer` differs) through the same rule.
- [ ] **Step 3: Go/no-go checks**, each with its output in the ledger; any failure that can't be fixed in the rule
  means Clang keeps per-target PCHs (option (a)) and the reason goes in the ledger:
  - `pixi run project_n build` then count: `find build/linux-clang/debug -name '*.pch' | wc -l` → `2`.
  - Header edit: append a harmless `inline constexpr int pn_probe = 1;` to `src/core/types.hpp`, build: every object
    of every target rebuilds (count from XMake's output equals Task 0's count); revert, build again: same.
  - `pch.hpp` edit: add a blank `#include <ranges>` to `src/pch.hpp`, build: every game object rebuilds; revert.
  - Build cache, no stale hit: temporarily add `inline constexpr int pn_probe = 1;` to `src/core/types.hpp` and
    `static_assert(pn_probe == 1);` to `src/core/replay.cpp`; build twice with
    `PROJECT_N_BUILD_CACHE=$PWD/build/cache-probe pixi run project_n build --mode release` (the second from the cache:
    delete `build/linux-clang/release/.objs` between them). Then change only the header to `pn_probe = 2` and build:
    expected **failure** on the `static_assert` (a stale cache hit would pass). Revert both; remove
    `build/cache-probe`.
  - Unity: `pixi run project_n test --unity` (or check's unity step) passes.
  - clangd and lint: `pixi run project_n lint --all` passes with no new finding.
  - Sanitizer build: `pixi run project_n test --sanitizer address` builds and passes (fuzz targets included).
- [ ] **Step 4:** `pixi run project_n test`, `pixi run project_n lint --range <base>..HEAD`.
- [ ] **Step 5:** Update design 0001 rule 19 (one precompiled header per header per build folder; how Clang takes it;
  the fuzz targets' own) and tools/AGENTS.md's C1382 gotcha only if it changes; commit
  `build(xmake): build each precompiled header once per build folder with Clang`.

### Task 5: Switch GCC over (review: full)

**Files:** the rule from Task 4; `xmake.lua` (`-Winvalid-pch` as an error for GCC); design 0001 rule 19.

**Interfaces:** Consumes the `shared_pch` rule (Task 4).

- [ ] **Step 1:** Enable the rule for `pixi-gcc`: one shared `<header>.gch` per build folder found through
  `-include`; add `-Winvalid-pch` (already an error under `set_warnings(..., "error")`, confirm).
- [ ] **Step 2: Go/no-go**, as Task 4 Step 3 with `--preset linux-gcc --mode release` (no build cache on GCC; skip
  that check): `.gch` count `2` per folder; header edit and `pch.hpp` edit rebuild everything; deliberately add
  `-DPN_PROBE` to one target and confirm the build *fails* on `-Winvalid-pch` (then remove it).
- [ ] **Step 3:** `pixi run project_n test --preset linux-gcc`, lint the range; update rule 19; commit
  `build(xmake): share the precompiled headers under GCC`.

### Task 6: Switch MSVC over (review: full)

**Files:** the rule from Task 4; `xmake.lua` (MSVC debug info if needed); design 0001 rule 19; tools/AGENTS.md
(C1382 gotcha, `CL_WRAPPER` gotcha if dependency tracking changes).

**Interfaces:** Consumes the `shared_pch` rule (Task 4).

- [ ] **Step 1:** Enable for `msvc`: `/Yc` once per header per build folder, users `/Yu<header> /Fp<file> /FI<header>`;
  link the `/Yc` object once (from `core` for `src/pch.hpp`, from each test binary or a shared test object for
  `tests/unit/pch.hpp`); resolve C2859 by `/Z7` or a shared PDB, recording which and why.
- [ ] **Step 2: Go/no-go** with `pixi run project_n build --preset windows-msvc --mode release` (container; stop and
  ask if it can't run from the worktree): `.pch` count `2`; header edit through `CL_WRAPPER`'s `/showIncludes`
  rebuilds everything; `pch.hpp` edit rebuilds everything with no C1382; two clean builds give identical binaries
  (`sha256sum` of `project_n.exe`: `/Brepro` and `/experimental:deterministic` still hold); debug mode builds and
  `test --preset windows-msvc` passes under Wine.
- [ ] **Step 3:** Update rule 19 and the C1382 gotcha; commit `build(xmake): share the precompiled headers under MSVC`.

### Task 7: After-measurements and Outcome (review: light)

**Files:** `docs/superpowers/specs/2026-11-16-build-folder-size-design.md` (Outcome, Status).

- [ ] **Step 1:** Alone on the machine, repeat Task 0's steps on the branch head.
- [ ] **Step 2:** Write the Outcome: each toolchain shared or not (with its reason), PCH counts and sizes before and
  after, cold build times before and after (goal 4: no slower), incremental rebuild after a header edit, bytes freed
  by the first sweep, the ledger's rulings (the rule's placement, `/Z7` or PDB, the generated header). Cite commits
  by subject. Mark the spec implemented.
- [ ] **Step 3:** Commit `docs: record the build folder outcome`.
