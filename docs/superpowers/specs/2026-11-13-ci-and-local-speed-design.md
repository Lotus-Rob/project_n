# Faster CI and local builds: cache what we compile, spread what can't be cached, run test cases in parallel

Date: 2026-11-13 · Status: implemented 2026-11-14 (approved 2026-11-13; see Outcome)

## Intent

The user, 2026-11-13, picking the sweep-up backlog's "CI speed, another pass": a push should go green in **5 minutes,
10 at most** ("5 minutes would be preferable, but 10 mins is acceptable if that's the best we can do"), and "make sure
all the local builds are sped up as much as possible to reduce our development iteration time", pulling in whatever
the sweep-up backlog holds on the way. A pull request merges only when every required job is green on a branch up to
date with main, so the wait is the slowest job's, not the sum.

The user chose approach C (below): cache compiled code wherever it works, spread across jobs the work that can't be
cached, and run test cases in parallel. And, mid-design: "release creation and completion of merge to main are always
cold, full builds no matter what": caches speed up branch pushes only. "Cold" means our own code (the user,
2026-11-13): every build of it compiles from source, a job's second build in the same run included, while third-party
packages still come from the package cache, keyed on their exact pins.

## Where the time goes (2026-11-13)

**CI**, two runs on main, seconds per step (setup steps under 20 s left out):

| job | step | run 43359490863 (03:31) | run 43477698570 (17:56) |
|---|---|---|---|
| lint | `lint --container` (linux-clang debug build + clang-tidy) | 277 | 447 |
| container (linux-gcc) | `artifact` (clean release build, tests, reference pictures) | 267 | 461 |
| container (linux-clang) | `artifact` | 296 (failed: Mesa race, below) | 464 |
| container (linux-clang) | `build --mode releasedbg --container` | (not reached) | 446 |
| container (windows-msvc) | `artifact` (MSVC under Wine) | 455 | 454 |
| windows | `test --mode release` | 291 | 377 |
| windows | `test --sanitizer address` | 256 | 379 |
| macos | `test --mode release` | 655 | 232 |
| macos | `test --sanitizer address` | 333 | 221 |
| macos | `artifact` (a second, clean release build) | 234 | 233 |
| **wall** | | 21.5 min (macos) | 16.3 min (linux-clang) |

- Setup (Pixi, images, MSVC, LFS, XMake packages) is cached already and costs ~1–1.5 min a job.
- Every job compiles all our code from scratch, 1–3 times: **XMake turns its build cache off when it sees CI**
  (`private/cache/build_cache.lua`: `ci_is_running()` and no `XMAKE_ACTION_BUILD_CACHE`, unless the project sets the
  `build.ccache` policy), and off for `cl` on a Windows host; `xmake.lua` turns it off for MSVC under Wine. The
  container jobs don't pass `CI` into the container, so they compile with the cache on, but into a build directory the
  run then throws away.
- Hosted runners vary: the same build took 1.7× longer on the second run's Linux jobs, and macOS's first release
  build took 625 s in one run and ~200 s in the other. Targets are judged on several runs, not one.
- Main's 03:31 run failed in linux-clang's reference pictures: `MESA: error: get_or_create_user_temp_dir: mkdir
  /tmp/xdg-runtime-mesa-1000 failed: File exists`, then SIGSEGV. The dev-loop branch draws the cases in parallel
  (`run_reference_case`), each process without `XDG_RUNTIME_DIR`, so they race to create Mesa's fallback directory.

**Local**, `check` on main's head in the main checkout after the chunk 4b merge (stale build folders), 32 cores:
238 s in all; lint 0 s (nothing changed), test linux-gcc release 65 s (58 s build), linux-clang release 34 s (28 s
build), windows-msvc release 61 s (53 s build), reference 9 s, unity test 35 s, releasedbg build 35 s. Builds are
~75% of it. Of the tests, `game_tests` runs every case in one process: 3.4–6.1 s in the release builds, **20.8 s** in
the debug unity build (21 s too in a plain `test`, linux-clang debug), while every other test binary finishes in under
2 s. Its slowest cases (linux-clang debug): "a scene change mid-volley ends every open use" 6.3 s, "a game loaded from
a save goes on exactly as the game that saved" 2.9 s, "the world draws in foot-y order…" 1.4 s, then under 1 s each.

Each branch and each parallel implementer works in a fresh worktree, and XMake's build cache lives in each build
folder, so every worktree compiles every preset from scratch (the 58/28/53 s builds above, for each preset and mode).

## History

- **Caches already in CI** (.github/AGENTS.md): setup caches (Pixi, GHCR images, MSVC install and native toolset, the
  macOS XMake bundle, Git LFS) and XMake *package* caches per job, as separate restore and save steps so a failing job
  still saves (`actions/cache` alone saves only on success). No cache of our own objects was ever tried in CI.
- **Decided:** every push runs every job, and no required job carries an `if:` (a skipped job counts as passed); MSVC
  files may sit in the Actions cache only because no workflow runs on `pull_request`; Dependabot's runs have a
  read-only token (`PROJECT_N_PUSH_IMAGES`); no disk-cleanup step (cost ~2 min a job); workflows only run
  `pixi run project_n <command>`, all logic lives in tools/project_n.py.
- **The build cache under Wine is off** (dev-loop speedups, 2026-11-12): with it on, `/showIncludes` lists no headers,
  so a header change rebuilt nothing in a reused build folder. That harm needs a reused folder; CI's are always fresh.
- **Tried and rejected, reopened with the user:** tools/AGENTS.md says no compile cache is shared between checkouts,
  on purpose, since XMake's cache keys on paths, and measured a cold build of our sources at ~10 s per preset and mode
  (2026-11-10, 109 files, linux-clang debug). Since then the code grew (149 compiles a build) and parallel worktree
  waves became the normal way to work; today's cold builds are 28–58 s each locally and 200–600 s in CI. Approach C
  reopens a shared local cache as a spike (§3).
- **Done, to prune from the backlog:** "lint only the `.cpp` files a changed header reaches" (dev-loop speedups) and
  the check-time PRIORITY (`check` is ~4 min; the volley tests' assertion count no longer shows: all of `core_tests`
  runs in 0.4 s).

## Goals

1. **CI:** a typical branch push (a few files of `src/` or `tests/` changed, a warm cache) has every required job
   green **within 5 minutes** of the push, judged as the median over at least three runs; a branch push with cold
   object cache (e.g. the first push after main's cache was evicted) within 10. A push that changes `pixi.toml` or
   `pixi.lock` also rebuilds every third-party package in every job: rare, measured, and not held to either target
   (my call: speeding up package rebuilds would be new work outside this design). Main and tag runs are always cold
   (anti-goals) and get faster only through §4–§6, never through caches.
2. **Local, test rounds:** a test binary's cases run in parallel, so `test` waits for its slowest case, not the sum
   (`game_tests` ~21 s → ~6–7 s in debug).
3. **Local, new worktrees:** a fresh worktree's first builds take seconds, not minutes, if the shared-cache spike
   (§3) finds a correct way; otherwise its findings are recorded and tools/AGENTS.md updated with today's numbers.
4. **The sweep-up items below are fixed**, and the backlog is pruned of what's done.
5. Every gain is measured before and after and recorded in the Outcome.

## Non-goals

- Paid or larger runners (they cost money even on a public repository).
- Remote build caches or cache servers (XMake's `remote_cache_client`, sccache backends).
- Changing what CI verifies: every preset, mode and sanitizer it builds and tests today stays.
- Making the game's own code compile faster (headers, includes, PCH layout): a later plan, if still needed.

## Anti-goals (failure even if it works)

- A test or check skipped, loosened or dropped to save time; a required job that can pass without running.
- **A cached object in a main or release build** (the user, 2026-11-13: "always cold, full builds no matter what"):
  on a push to `main` (a merge's completion) and a `v*` tag, every build of our code compiles from source: no object
  cache restored, no build reused, and no build taking objects from an earlier build in the same run. Third-party
  packages still come from the package cache (the user's scope).
- A stale object served from a cache: anything that could hand back an object for different inputs (flags, toolchain,
  headers) is a failure, however fast.
- Microsoft's files uploaded or pushed anywhere beyond the Actions cache (objects and PCHs of our code may go there
  under the same `pull_request` rule as the MSVC install).
- Any change to game performance: no compile flag of a shipped build changes, except path-mapping flags
  (`-ffile-prefix-map` and the like), which the user allowed in every mode (§3): they change only the paths in debug
  info and `__FILE__` strings, never the code.

## Approaches (the user picked C)

- **A. Cache compiled code everywhere.** CI's build cache on and kept between runs; locally, one shared across
  worktrees. Linux, macOS and lint drop to a few minutes, but MSVC isn't cached (Wine, native Windows), so the two
  Windows jobs stay 8–12 min: about a 10-minute wait.
- **B. Spread the work across more jobs**, one build each (macOS, native Windows release and ASan, releasedbg apart).
  No cache-correctness risk; ~7–9 min; nothing for local builds; changes main's required checks.
- **C. Both, plus parallel test cases (chosen).** Cache wherever it's correct, spike MSVC caching on CI's always-fresh
  build folders, split only what still runs long, and run each test binary's cases across processes. The only way to
  the 5-minute goal, and the only one that speeds local work too.

## Design

### 1. Mesa's race (first, as it fails main at random)

Each reference case gets its own runtime directory (`XDG_RUNTIME_DIR` set to a fresh private temporary directory per
process, removed after), so no two processes create Mesa's fallback directory at once. A selftest covers that each
case gets its own. This lands before anything else, so the branch's CI runs measure speed, not flakes.

### 2. CI's build cache

- XMake's build cache is turned on explicitly in CI, for every job and preset where it is correct (linux-gcc,
  linux-clang, macos-clang, lint's linux-clang debug; MSVC per §4), by `project_n`, never by workflow logic: the
  workflow says only where the cache lives (an environment variable, like `PROJECT_N_XMAKE_CACHE` today).
- The cache directory sits outside every build folder (`--ccachedir`), so `artifact`'s clean-room `rmtree` of the
  build folder keeps it, and one job's builds (release, ASan, releasedbg) share it where their keys can match.
- Kept between runs with separate `actions/cache/restore` and `actions/cache/save` steps, saving even when a later
  step failed (the package caches' pattern). Key: job, preset, toolchain (the same files as the package caches) and the
  commit; restore falls back to this branch's latest, then main's (Actions lets every branch read main's caches, and
  no branch write them).
- **Main and tags are cold** (the user's rule): on a push to `main` or a `v*` tag no object cache is restored, and
  no build reads objects another build wrote, the same job's earlier builds included, so every object compiles from
  source. A `main` run still *saves* objects for branches to fall back on: only builds that start from an empty cache
  write to it (e.g. each build its own empty directory, one of them saved per job; the mechanism is the builder's), so
  saving never changes what is compiled. A tag saves nothing. `project_n` enforces this itself from `GITHUB_REF`
  (refusing a non-empty object cache or a reused build on `main` or a tag), so the rule holds even if a workflow is
  edited wrongly. Only branch pushes, which are what a pull request waits on, are warm.
- XMake's cache key (`cachekey`: compiler path, flags except `-D`, and the preprocessed file's hash) never sees a
  `-D` change. A define reaches the code only through the preprocessed text, so this is safe as long as no object
  differs by a `-D` alone without the preprocessed text changing; the builder confirms that holds for our flags
  (e.g. `-D` used only in sources, not as a compiler-internal switch).
- The Actions cache holds 10 GB per repository, evicting the least recently used. Measure each new cache's size;
  keep the total (package caches, LFS, MSVC, objects) under the limit with room, or trim what's kept (e.g. one object
  cache per job rather than per mode).

### 2b. clang-tidy's results (my call: the backlog listed it, and `lint` is CI's one job that can't split along builds)

On a branch, CI's `lint` checks every commit since main, so a branch that touches a widely included header lints
nearly every unit (412 s for 127 units at 4 at a time on one run). Each unit's clang-tidy result (its output and
`tidy_verdict`'s inputs) is cached under a key of the unit's preprocessed text, its compile flags, the clang-tidy
binary's version and every `.clang-tidy` file that applies, so an unchanged unit is never re-linted on the same
inputs. Kept in CI with the object cache's steps and rules (warm on branches only; main and tags lint cold); locally,
in the build folder or the shared cache (§3). A cache hit prints the same findings a fresh run would.

### 3. A cache shared across local worktrees (spike first)

A spike answers, by measurement, whether a fresh worktree can take objects compiled in another worktree with XMake's
build cache: one global cache directory (`build.ccache.global_storage` or a shared `--ccachedir` from `project_n`), and
whatever makes the key path-independent (the compiler path: each worktree has its own Pixi environment; absolute `-I`
flags; the preprocessed file's line markers; debug info's paths, which a reused object would point at another, perhaps
deleted, worktree; `-ffile-prefix-map` and relative paths may cover these; the user allowed path-mapping flags in every
mode, release included, 2026-11-13, so the spike targets every mode `check` builds). If it hits correctly and debuggers
still find the right sources, adopt it with an eviction or size limit (`project_n clean` already prunes old package
folders); if not, record why, keep per-folder caches, and correct tools/AGENTS.md's "~10 s" with today's numbers. MSVC
under Wine stays as it is locally (the `/showIncludes` reason above).

### 4. MSVC in CI (spike first)

- **container windows-msvc:** CI's build folders are always fresh, so the reason the build cache is off under Wine
  (headers missing from dependency records in a *reused* folder) can't bite there. Spike: turn it on only for a build
  folder that is never reused (CI, and `artifact`'s clean room), check hits, and check the artefact is byte-identical
  to an uncached build's apart from what already differs between two clean builds (the PE timestamp and `.pdb`
  record: sweep-up's "MSVC reproducibility").
- **windows (native):** XMake turns the cache off for `cl` on Windows because its preprocessing is slow; the spike
  measures a cold and a warm build with it on (the `build.ccache` policy set for that job).
- Adopt where a warm build is clearly faster and correct. What still runs over the goal is split (§5).

### 5. Spread what still runs long

After §2, §2b and §4, measure each job on several runs. A job whose warm time is over the goal splits along its builds,
one build per job; if one build per job is still over, the next levers are building in one job and testing in another,
or one test suite's processes across jobs (my call: each only where a measurement shows it's needed, and the plan stops
at a measured median within 10 minutes, recording why 5 wasn't reached, rather than adding jobs without end). The likely
candidates: native Windows into release tests and ASan; macOS into release tests plus `artifact` (reusing the release
build, instead of a second clean build, on branch pushes only; main and tags keep the clean room) and ASan;
linux-clang's releasedbg into its own job. Each new job is required, carries no `if:`, and gets the same cache steps.
Jobs may be renamed freely for clarity (the user, 2026-11-13). **The ruleset's required checks change with them:**
`.github/rulesets/main.json` is updated in the same commit, and the switch is coordinated with the user as the PR's last
step: the user applies the new ruleset just before merging (the old names would never report on this PR), and every
other open branch then rebases onto the new main before it can merge.

### 6. Parallel test cases

`test` runs each doctest executable's cases across processes, as many at once as `jobs()` allows (the reference
pictures' pattern: picked by position, outputs printed in order after all finish, one closing line naming the failed
cases). It applies to every preset, mode and sanitizer: native, Wine (each process a Wine process), macOS, Windows,
ASan. What counts as a test still comes from XMake's registration (`tests/xmake.lua`); `--suite`/`--test` and doctest
arguments keep their meaning; a test binary with a handful of cases may run as one process when splitting buys
nothing. Test cases are independent already (tests/AGENTS.md); a case that turns out not to be is a bug, fixed
test-first, never serialised around. The 6.3 s case ("a scene change mid-volley…") then sets `game_tests`' time; it is
looked at for wasted ticks (same behaviour, same asserts, fewer frames to reach it), never loosened.

### 7. Sweep-up items pulled in

- **`xmake-requires.lock`'s `__meta__` reorder** after builds: fixed in `project_n` (restore the committed file when
  only the order changed, or write it in a stable order), so no agent restores it by hand.
- **Reference pictures that drift:** refresh, once on this branch, the 15 committed PNGs that differ from what
  lavapipe draws within tolerance, and find why `reference --update` rewrites `painted`, `cells` and `cells_far` with
  byte-level differences each run (the encoder's settings, or a nondeterministic draw); after this, an `--update` with
  nothing changed rewrites nothing.
- **Lint noise:** `lint` stops printing the Quill `clang-analyzer-security.ArrayBound` report it already ignores
  (IGNORED_FINDINGS); it still counts it in its closing line.
- **The backlog:** the two finished entries (History) are deleted; this one is marked done with the Outcome's numbers.

## Constraints

- tools/AGENTS.md: `project_n.py` stays one standard-library file; never pipe XMake's output through filters; the
  container re-enters the same script.
- .github/AGENTS.md: actions pinned by SHA; restore and save as separate steps; no `pull_request` trigger; Dependabot's
  read-only token must not fail a cache step.
- Each AGENTS.md and skill touched changes in the same commit as the behaviour (.github, tools, tests).
- Gates per the root AGENTS.md; `check --container` can't run from a worktree, so CI's jobs on the PR are that gate.

## Left to the builder

- How test cases split (by count per process, by position, or measured cost), and the cut-off below which a binary
  runs whole.
- Cache key details and which modes share an object cache.
- How the lock reorder is fixed.
- The order of spikes and splits, as long as every split is justified by a measurement after caching.

## Outcome

Built in the eighteen commits from "build(tools): give each reference case its own runtime directory" to "build(xmake):
drop msvc-wine's unused build cache exception". CI was measured on this branch's own runs, 2026-11-13 and 14 (the run
ids below: GitHub keeps each one's step times). Local numbers are dated, on the 32-core dev machine.

**CI, per job** (job wall time; the last two columns medians over three runs, all green):

| job | no object cache yet¹ | cached, before the split² | **after the split³** |
|---|---|---|---|
| lint | 12m37 | 2m44 | **2m29** |
| container (linux-clang) | 12m37 (with releasedbg) | 7m52 (with releasedbg) | **4m49** |
| releasedbg | (in linux-clang) | (in linux-clang) | **3m39** |
| container (linux-gcc) | 6m08 | 8m24 | **8m13** |
| container (windows-msvc) | 7m17 | 10m03 | **10m02** (8m55 over four runs⁴) |
| windows (release) | 13m42 (`windows`: both builds) | 10m22 (`windows`) | **6m13** |
| windows (address) | (in `windows`) | (in `windows`) | **5m56** |
| macos (release) | 12m45 (`macos`: three builds) | 9m53 (`macos`) | **3m45** |
| macos (address) | (in `macos`) | (in `macos`) | **2m29** |
| **run wall** (created to updated) | | 13m38 | **10m06** (9m15 over four runs⁴) |

¹ Run 43511118499, a single run: the first with the object cache steps, which found nothing to restore and compiled
cold, after the parallel test files had landed. For scale, main before this branch: run walls of 21.5 and 16.3 min
(the table at the top), 12.5 min on run 43293701453.
² Runs 43548418939 (a re-run), 43549321125 (one `.cpp` touched) and 43550574071 (`src/core/types.hpp` touched, which
every precompiled header includes, so every Clang object and clang-tidy result missed). Every cache was in place;
"build(tools): let artifact package an earlier release build on branches" and the split were not.
³ Runs 43553079531 (a push changing only a tools file), 43585392994 (a re-run of it) and 43586261018 (one `.cpp`
touched). The split's first run, 43552206135, failed in `selftest` and is not counted.
⁴ Adding run 43586955595, the branch head of the time (container (windows-msvc) 7m32, run wall 8m24): the four job
times 10m02, 10m08, 7m48, 7m32 and the four walls 10m06, 10m13, 8m17, 8m24, each median taken between the middle two.

Runner noise is large: the Windows jobs ran 25–35 % faster in 43586261018 than in 43585392994 for the same build
work, so only medians decided anything.

**What each change bought, with the ruling behind it:**

- **The object cache** ("build(tools): keep CI's compiled objects between runs", "ci: keep compiled objects between
  branch runs"). container (linux-clang) went from 12m37 cold to 7m01 warm (run 43513251309: `artifact` 359 → 206 s,
  releasedbg 308 → 132 s). The caches saved on the cold run took lint 38.8 MiB, container (linux-clang) 135.4 MiB and
  macos 127.2 MiB of the 10 GB. Only Clang uses it (`CI_CACHED_TOOLCHAINS`):
  - **GCC never does, locally too.** With a precompiled header, GCC's preprocessed text holds `#pragma GCC
    pch_preprocess "<pch>.gch"` instead of the headers, so XMake's key missed a change to `core/types.hpp` and handed
    back the old object; reproduced first (a test still saw the old value after the header changed), then shown gone
    with GCC off the cache in xmake.lua. That bug was latent in every local linux-gcc build before this branch.
  - **A clang miss costs more than an uncached compile** (the cache compiles the preprocessed file without the PCH):
    linux-clang release 44 s cold through the cache, 35.5 s uncached, 27 s warm; debug 25 / 19 / 11 s (32 cores, the
    builder's hand check). Main writes through the cache into an empty folder, so it pays the cold figure.
  - **macOS never hit at first**: the save key differed from the restore key every run, because the macOS job added
    to `xmake-requires.lock` the `macosx|arm64` entries of five requires added from Linux. The saves now use the key
    their restore step computed, and the missing entries are committed ("build(deps): lock the macOS packages added
    since entt"); on the first warm run after that (43521575852) the macos job went from 14m07 to 9m57 (release tests
    338 → 212 s, ASan 242 → 131 s).
- **clang-tidy's results** ("build(tools): keep clang-tidy's results for unchanged units"): locally `lint --all` 84.1 s
  cold, 13.0 s warm, 22.6 s after a comment-only edit (141 units, 32 cores); in CI lint went from 11m09 to 2m35
  (`lint --container` 613 → 108 s, runs 43529001973 and 43530664696). The key holds more than the plan's: the bytes of
  every repository file the `-E` text names (`-E` drops comments, so removing a NOLINT would otherwise have been served
  a cached pass), each unit's compile entries sorted (XMake exports a file two targets compile in either order),
  `tidy_command`'s own arguments; crashes are never stored, and the three fuzz targets (no compile database entry) stay
  uncached.
- **The lock file** ("build(tools): restore xmake-requires.lock when a build only reorders it", then "… only prunes
  it"): `run` writes the committed bytes back after every XMake call that only reordered the lock or dropped entries,
  and keeps and prints anything added or changed. Native Windows added meson, pkgconf and python entries to
  `windows|x64` every run, so "build(deps): lock the build tools native Windows resolves" committed them; run
  43522155375 then showed the windows-msvc container's `artifact` dropping them again (msvc-wine's host tools are Linux
  ones). Restoring only reorders, as the plan's `restore_reordered_lock` did, would have left that drop in every local
  windows-msvc build with a print asking to commit it, and committing it would have brought native Windows' drift
  back; so it became `restore_lock_unless_added`. The cost, recorded in the add-dependency skill: removing a dependency
  leaves its entries until deleted by hand. The next run (43525566167) printed no lock change in any job.
- **The split** ("ci: split the jobs that ran longest", nine required checks, `.github/rulesets/main.json` changed in
  the same commit): macos 9m53 → 3m45 and 2m29, windows 10m22 → 6m13 and 5m56, container (linux-clang) 7m52 → 4m49
  plus releasedbg 3m39. In macos (release), `artifact --reuse-build` packages the release build its tests just made:
  14–18 s instead of a second clean build of 181 s; main and tags still rebuild in the clean room.
- **The Mesa race** ("build(tools): give each reference case its own runtime directory"): none of the branch's CI
  runs failed in the reference pictures.
- **Sweep-up items:** the reference pictures did not drift between runs; four of them (`cells`, `cells_far`, `painted`,
  `fox_dissolving`) were stale within tolerance after earlier renderer changes and atlas re-packs, and every
  `--update` rewrote them identically (refreshed in "test(tests): refresh the reference pictures lavapipe draws
  today", with a Gotcha in tests/AGENTS.md: refresh such pictures, never revert them). `lint` no longer prints the
  ignored Quill report.

**Local** (linux-clang debug unless named):

| what | before | after |
|---|---|---|
| `test`, built, nothing changed | 37 s (`game_tests` one process, 20.9 s; the plan's baseline) | **8.4 s** (8.2–8.6 s over three runs; XMake's test phase 6.6–6.9 s for 76 file tests)⁵ |
| slowest test case ("a scene change mid-volley ends every open use") | 6.3 s | **1.3 s** (every assertion kept, the change made late in the swords' flight, so 32 stress ticks follow it instead of 180) |
| `test` in a fresh build folder (a fresh worktree's first) | not measured as one figure: a ~20 s cold build plus the 37 s run | **34.1 s** (build 20.2 s, tests 6.9 s)⁵; 33.4 s in a fresh worktree on 2026-11-14 |
| `lint --all` | 84.1 s (every unit linted, as before this branch) | **13.0 s** with every result kept |
| `check`, **cold** (every build folder fresh, packages kept) | 390 s, in a fresh worktree on 2026-11-14 midway through this branch (load average 13–29) | **370 s**⁶ |
| `check`, warm (stale build folders: a different condition) | 238 s on main (the table at the top) | not measured |

⁵ Measured for this Outcome on the branch head with the cold build folder moved away for the fresh run, load average 4–5
(other programs running). The slowest file tests are now game_tests/arena and game_tests/save at 6.3–6.7 s each.
⁶ Measured for this Outcome: one run on the branch head with every build folder removed first and the packages kept, as
for the 390 s run (load average 10 at the start, 32 at the end). Steps: lint 111 s (clang-tidy's cache empty), test
linux-gcc release 73 s, linux-clang release 44 s, windows-msvc release 63 s, reference 8 s, unity test 24 s, releasedbg
build 47 s. The 390 s run came after the parallel test files and the clang-tidy cache had landed, so the two differ by
noise, not by this branch. Cold, `check` is bound by its six builds (XMake's time: 21, 63, 34, 53, 15 and 39 s), which
only a cache shared between folders could shorten (the spike below). A warm `check` was not measured.

**The spikes:**

- **No compile cache shared between local worktrees** (record, don't adopt; tools/AGENTS.md). A shared `--ccachedir`
  hit 0 of 140 compiles from another worktree: XMake's key holds the compiler's path, which is each worktree's own
  `.pixi`, and the preprocessed text, whose line markers name that env's headers and, through `-include-pch`, every
  precompiled header's headers by absolute path; no flag mapped those, and `-ffile-prefix-map=<ROOT>=.` guarantees a
  miss, since XMake keys on the flag's text. Only a Pixi env shared by every worktree, a plain `-include` instead of
  each PCH and `-ffile-compilation-dir=.` hit 128 of 140, byte-identical to a cold compile: a fresh worktree's debug
  build in 9 s instead of 20 s (XMake's time), but every cold build 20 → 28 s, and gdb started from the binary's folder
  loses the sources. Not adopted: a shared env is an architecture change this spec didn't approve, and dropping the PCH
  changes shipped builds' compile flags.
- **No MSVC objects in CI** (.github/AGENTS.md, Gotchas). XMake's `cl` cache compiles each file's preprocessed text
  without the PCH, so even a cold-cached `.exe` differs in code from an uncached one (418,566 bytes; anonymous
  namespaces take the `__cpp_*` file's name), and it keeps only the `.obj`, so on a hit the debug types `/Zi` left in
  the build folder's pdb are gone: the warm artefact's `.pdb` shrank from 27.6 to 12.1 MB. Two uncached CI builds gave
  a byte-identical `.exe`. Under Wine it was no faster either (`artifact` 445 s uncached, 683 s cold, 472 s warm);
  native Windows was (job 797 → 464 s warm) but 342 s slower cold, as every main run is. Only `/Z7` would keep the
  debug info, a release compile flag change. The xmake.lua exception that let CI turn msvc-wine's cache on was then
  dead and is gone.

**The user's rulings (2026-11-14):** container (windows-msvc)'s three-run median of 10m02 is runner noise: over four
runs it is 8m55 (the fourth 7m32), within the 10-minute ceiling, and the job's median was 10m03 before the split, which
didn't touch it. The idea that would shorten it is recorded, not built: `artifact` and `test` taking a target subset,
so two container jobs each link about half of the twelve executables (/LTCG under Wine takes ~4m25 of the job), est.
~8 min; it needs new, test-first `project_n` logic and a tenth required check. Splitting
`tests/unit/game/arena_tests.cpp` goes on the sweep-up backlog (below).

**Goals:**

1. CI: the 10-minute goal is met on typical pushes (every job's median within 10 minutes, the msvc one by the ruling
   above); the 5-minute one is not, for four jobs (below). A push that misses every Clang object and clang-tidy result
   (a header every PCH includes, run 43550574071) is over 10 in lint, which can't split along builds: 17m53 there before
   the split, and 11m09 on the run that filled an empty clang-tidy cache (43529001973). The user accepted lint's
   cold time as recorded (2026-11-14); sharding it is an idea on the sweep-up backlog.
2. Test rounds: `game_tests` no longer runs in one process; `test` takes 8.4 s instead of 37 s.
3. Fresh worktrees: no correct shared cache (the spike above); tools/AGENTS.md has today's cold build times instead of
   "~10 s".
4. The sweep-up items are fixed and the backlog pruned (the lock reorder, the reference pictures, the Quill print, the
   finished entries); "CI speed, another pass" is marked done there.
5. Measured before and after, above.

**What's left:**

- **Jobs over 5 minutes, and why they stop there** (medians within 10 minutes, as §5 allows):
  - windows (release) 6m13: bound by /LTCG links, ~3.6 min of a 264 s build after ~1 min of compiling
    (`reference_tests.exe` alone ~80 s). Testing in another job saves the ~10 s of tests; LTO is design doc 0001's.
  - windows (address) 5m56: game_tests/arena is one process of 104–187 s (54 cases), while the other test files run
    beside it.
  - container (linux-gcc) 8m13: one clean-room `artifact`, ~2 min of compiling and then ~3 min of LTO links and `.sym`
    splits; GCC uses no object cache.
  - container (windows-msvc) 10m02: one clean-room `artifact`, ~2.3 min of compiling under Wine and then ~4m25 of
    /LTCG links; MSVC uses no object cache (above).
- **Splitting `arena_tests.cpp`** in two, no case changed, should take windows (address) from ~5m56 to ~4m45 (sweep-up
  backlog).
- **The msvc link split** above, if the job's median stays near 10 minutes.
- The ruleset's nine checks are applied by the user just before merging, after which every open branch rebases.
- "build(xmake): drop msvc-wine's unused build cache exception" changes xmake.lua, which the object caches' keys hash,
  so the branch's next run compiles every Clang object cold once.
