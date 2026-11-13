# Faster CI and local builds: cache what we compile, spread what can't be cached, run test cases in parallel

Date: 2026-11-13 · Status: approved 2026-11-13

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

(Filled in before the PR.)
