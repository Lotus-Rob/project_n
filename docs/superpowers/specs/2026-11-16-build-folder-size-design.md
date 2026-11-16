# A smaller build folder: one precompiled header per build, cold trees and old cache entries removed

Date: 2026-11-16 · Status: implemented (Outcome below)

## Intent

The user, 2026-11-16: the main checkout's `build/` had reached 57 GB, and they asked to "fix this and prevent it
from growing as big in the future, especially those which are old/stale/not beneficial. And especially fixing the 63
precompiled headers on windows", then "Why are we even generating that many pch instead of reusing them??".

The builds used every day are **linux-clang debug and release**: "the rest can go cold". So everything else may be
deleted and rebuilt from source when next needed, and those two must stay warm (no slower to rebuild than today).

The user picked approach A in each of the three parts below, and option (a) for a toolchain that can't share safely
(Design §1, *A toolchain that can't share*), and a one-off cleanup, done the same day: the container
`linux-clang` and `linux-gcc` trees (last used 9 Nov, 9.3 GB), the container `windows-msvc` build caches (nothing has
written them since 11 Nov, as the cache is off for MSVC under Wine) and the stray `build/s4*` folders: 14 GB freed,
57 → 44 GB.

## Where the space goes (2026-11-16, before the cleanup)

- **Precompiled headers, 13.6 GB in 236 files.** Each target with `set_pcxxheader` compiles its own: 7 game targets
  (`core`, `game`, `platform`, `gfx`, `data`, `app`, `project_n`) plus `headless_bench` and the fuzz targets on
  `src/pch.hpp`, 9 test targets on `tests/unit/pch.hpp`, in every build folder. MSVC's are ~150 MB each (63 files,
  9.6 GB over 4 modes), GCC's `.gch` ~147 MB (68 files, 9 GB), Clang's ~23 MB (~4 GB). Copies differ byte for byte, so
  btrfs can't share them.
- **XMake's build cache, ~21 GB.** `.build_cache/` in each build folder where it is on (Clang; off for GCC and for
  MSVC under Wine) never evicts: linux-clang/debug's held 6.1 GB in 2,805 entries against 744 MB of objects. The disk
  is mounted `noatime`, so a file's times say when it was written, never when it was last read.
- **Build trees, ~25.** One per preset × mode × sanitizer × unity, natively and in the container. Nothing removes
  one but `clean --preset`, which removes the whole preset.

## History

- XMake has no option to share a precompiled header between targets (its `c++.build.pcheader` rule builds one per
  target into that target's object folder; CMake's `REUSE_FROM` has no XMake counterpart). No shared PCH was ever
  tried or rejected here; one per target was XMake's default, not a decision.
- Design 0001 rule 19: `src/pch.hpp` for the game, `tests/unit/pch.hpp` for the tests; `.cpp` files include no
  standard header (lint enforces it); clang-tidy and clangd see the PCH through the compile database's `-include`.
  That stays.
- Build cache rulings (xmake.lua, spec 2026-11-13 *Faster CI and local builds*): off for GCC everywhere (with a PCH,
  `-fpch-preprocess` hid header changes and returned stale objects); off for MSVC under Wine (`/showIncludes` lists no
  headers there, so a changed header rebuilt nothing); on for Clang. CI keeps one cache per build in
  `PROJECT_N_BUILD_CACHE` and prunes it each run to what that run used (`prune-build-cache`, `prune_objects`).
- **Rejected** (spec 2026-11-13, spike): a compile cache shared between local worktrees: 0 of 140 hits, as the key
  holds each worktree's `.pixi` compiler path and the PCH's absolute path. Not reopened.
- `clean` (tools/project_n.py `cmd_clean`) removes whole presets; `--images` and `--packages` already prune the stale
  ones of those, and `prune-worktrees` removes merged worktrees.

## Goals

1. One precompiled header per header per build folder: `src/pch.hpp` once, `tests/unit/pch.hpp` once (17 → 2 per
   folder; ~13.6 GB → ~1.6 GB on the trees measured), on every toolchain where sharing is proven safe (below).
2. Build trees not used for a while are removed without anyone remembering to; linux-clang debug and release never are.
3. The build cache stops growing without bound.
4. Builds, tests and `check` behave exactly as before, apart from cold trees compiling from source.

## Non-goals

- C++ modules or `import std` (headers only, design 0001).
- A compile cache shared between worktrees (rejected above), ccache or sccache.
- Shrinking what the precompiled headers include: rule 19 says what goes in them.
- Anything in CI's cache budget or pruning, beyond keeping it working (constraint below).

## Anti-goals (failure even if it works)

- A changed header, or a changed `pch.hpp`, that leaves any object stale: worse than any amount of disk.
- linux-clang debug or release rebuilding from cold because of this change.
- A precompiled header silently not used (GCC drops a mismatched `.gch` without a word, so builds would only get
  slower), or one forced on a target whose flags don't match it.
- Pruning that touches anything outside `build/` of the checkout it runs in, or a tree in use by another command.

## Approaches (the user picked A in each)

**1. Precompiled headers.**
- **A (picked): share one per build folder.** A project rule builds each header once and every target using it
  takes that one. It needs the targets' flags to agree, which they nearly do already (below).
- B: no precompiled headers in the cold builds (sanitizers, GCC, MSVC). Simpler, but those builds get noticeably
  slower and GCC and MSVC still pay per target in the builds that keep them.

**2. Cold build trees.**
- **A (picked): remove by age.** `project_n` marks a tree used whenever a command builds or tests in it, and removes
  trees unused for N days at the start of `build`, `test` and `check`, sparing linux-clang debug and release; `clean`
  can do the same on demand.
- B: `check` removes its cold trees once it passes. Every `check` would then rebuild GCC, the sanitizers, unity and
  MSVC from source.
- C: by hand only (`clean --cold`). The 57 GB came from cleanup that relied on remembering.

**3. The build cache.**
- **A (picked): remove what is dead, age out the rest.** Delete the build cache of any tree whose toolchain no longer
  uses it (GCC, MSVC under Wine), and in the others remove entries written more than M days ago.
- B: turn it off locally. Saves the most, but loses quick rebuilds across branch switches, and the cache's local worth
  was never measured.

## Design

### 1. One precompiled header per build folder

**Make the flags agree first.** Comparing every target's compile flags in the native linux-clang debug build, only
three defines differ:

| define | on | used by |
|---|---|---|
| `PN_GAME_VERSION` | `core` (`on_load`) | `src/core/replay.cpp` (stamped into replays) |
| `PN_FUZZ_CORPUS` | `game_tests` | `tests/unit/game/save_tests.cpp` |
| `PN_SOURCE_DATA` | `project_n`, debug only | `src/main.cpp` |

Each moves to the one source file that uses it (XMake's per-file `add_files(..., {defines = ...})`) or into a small
generated header, the builder's choice; either way no target-wide define is left that differs from the rest. Package
flags (include directories and defines a package exports) may still differ per target: the headers in the PCH come
from the standard library and `src/core/` only, so a differing include path is harmless, but any differing *define*
must be checked on every toolchain (the comparison above covered `-D`/`-f`/`-std`/`-O`/`-g`/`-m`/`-W` on Clang
only).

**Build each header once, use it everywhere.** A project rule replaces `set_pcxxheader` (in `xmake/rules/` or
xmake.lua, the builder's choice) and, per build folder:
- builds `src/pch.hpp` once and `tests/unit/pch.hpp` once, before any target that uses them compiles, with the flags
  those targets share;
- gives each using target the compiler's way to take a prebuilt one: Clang `-include-pch <file>` (with the header
  still `-include`d where the compile database needs it for clangd and clang-tidy), GCC a shared `<header>.gch`
  found through `-include`, MSVC `/Yu<header> /Fp<file>` (plus `/FI`);
- rebuilds every object that uses a header when the header, anything it includes, or the shared flags change: the
  precompiled header is one of each object's dependencies;
- keeps the compile database (`build/compile_commands.json`) giving clangd and clang-tidy a command that works.

Constraints the builder must meet:
- **MSVC**: `/Yc` also writes an object holding the header's compiled code, which every binary using the PCH must
  link: link it once, from the lowest library (`core`) or each binary, the builder's call. Debug info: with `/Zi`,
  a PCH is tied to the PDB it was built with (C2859 when a target names another `/Fd`), so either all users share
  that PDB or MSVC builds use `/Z7` (debug info in the objects); `/Z7` also suits caching. Deterministic builds
  (`/Brepro`, `/experimental:deterministic`, `/pathmap`) must stay reproducible.
- **GCC**: a `.gch` built with different flags is ignored without a warning. The rule (or a test of it) must prove
  the shared `.gch` is actually used, e.g. `-Winvalid-pch` as an error.
- **Clang**: `-include-pch` refuses a mismatched PCH, so a mismatch fails the build loudly, which is wanted.
- **Targets whose flags genuinely differ** keep their own, through the same rule: the fuzz targets add
  `-fsanitize=fuzzer` and exist only in linux-clang debug-address, so one of their own for them is acceptable. Any
  other exception needs a stated reason.
- Sanitizer, unity and LTO builds each live in their own build folder already, so one PCH per folder covers them.
- CI's build cache (Clang, branch builds) must keep hitting: its key holds the PCH's path, which changes from one per
  target to one per folder, still stable between runs. XMake's `build_cache.lua` treats `-include` and `-include-pch`
  specially when it preprocesses, so a header change must still miss the cache with the shared PCH (the GCC
  stale-object bug must not come back on Clang).
- **Known weak spots**, each to prove before its toolchain switches over: MSVC under Wine, where XMake learns an
  object's headers only from `/showIncludes` through `CL_WRAPPER`, so a PCH built outside XMake's own rule may never
  become a dependency (and XMake already loses track of MSVC PCH rebuilds: the C1382 gotcha); unity builds, where
  `c++.unity_build` meets the PCH; Clang with the build cache (above).

**A toolchain that can't share** (the user's choice, option (a)): the work proves each toolchain in turn, Clang
first (the daily builds), then GCC, then MSVC, each with a go/no-go before it switches over. A toolchain where sharing
can't be made safe keeps one PCH per target, exactly as today, and the Outcome records why. The rejected options:
no PCH on that toolchain (slower, for nothing this work needs), patching XMake's own PCH rule in `xmake/repo/`
(more work and upkeep against XMake bumps), stopping to ask. Done means every toolchain either shares or has its
recorded reason.

### 2. Removing cold build trees

- **Mark.** Each `project_n` command that builds or tests in a tree (`build`, `test`, `run`, `check`'s builds,
  `reference`, `coverage`, `fuzz`, `profile`, `measure`, `artifact`: every caller of `build_dir()`) writes the time
  into a stamp file in that tree (`build/<preset>/<leaf>/` and `build/container/<preset>/<leaf>/`). A tree without
  a stamp counts as last used at its newest file's time, so the first run after this change judges today's trees
  fairly.
- **Sweep.** At the start of `build`, `test` and `check`, remove every tree under `build/` (native and container)
  whose stamp is older than **7 days** (my call; a named constant), except the **warm** trees: the native preset's
  (`NATIVE_PRESET`) `debug` and `release`, linux-clang's on the user's Linux machine (my call: a Windows or macOS
  developer keeps their own everyday builds warm the same way). Never the tree the command is about to use. Print one line per removed
  tree with its size, and the total.
- **Clean.** `clean --stale` runs the sweep on demand (`--days N` overrides the age); plain `clean` is unchanged.
- **Leftovers.** `build/` holds folders no command owns (`profiles/`, `measure/`, `artifact/`, `reference/`,
  `fuzz/`, `coverage/`, `docs/`, `doxygen-lint/`): the sweep doesn't touch them. `profiles/` (1.2 GB) and `measure/`
  hold the user's captures, which they delete themselves (my call: they may want an old profile; say so in
  tools/AGENTS.md).
- In CI every run starts clean, so the sweep finds nothing there; it must not fail when `build/` is missing.

### 3. Capping the build cache

- **Dead caches.** When the sweep runs, delete `.build_cache/` in any tree whose toolchain never uses it locally
  (GCC everywhere, MSVC under Wine: the same conditions as xmake.lua), so a leftover like the 5 GB found in
  `container/windows-msvc` can't build up again.
- **Old entries.** In the remaining trees (Clang, local), remove cache entries (`<key>` with its `<key>.txt`) written
  more than **14 days** ago (my call; a named constant). Without read times, an old entry still in use is compiled
  once more and written fresh: one recompile per file per 14 days in the worst case.
- Never in CI, whose `PROJECT_N_BUILD_CACHE` is pruned by `prune-build-cache` as now; the local sweep leaves that
  variable's folder alone.

### Docs

`tools/AGENTS.md` (the sweep, the stamp, `clean --stale`, the warm trees, what isn't swept), the comment beside
`BUILD_CACHE_ENV` in tools/project_n.py ("each build folder keeps XMake's own cache": now aged), design 0001 rule 19
(one precompiled header per build folder, how each toolchain takes it) and root AGENTS.md's `clean` line, each in
the commit that changes the behaviour.

## Constraints

- Every tool through Pixi; XMake only through `project_n`; exact pins (no new dependency is expected).
- `selftest` covers the new `project_n.py` logic (the sweep's choices, the stamp fallback, the cache ageing) on
  temporary folders, never on the real `build/`.
- `check` passes, and CI's nine jobs pass on the PR (a worktree can't run the container gates).
- Commits: `build(xmake)` for the PCH rule, `build(tools)` for project_n.py, `docs` for the docs.

## Measures of success

Checked once while building and recorded in the Outcome (my call: a permanent header-edit check would rebuild all
three toolchains, MSVC's container included, in every `check`, against goal 4), except GCC's `-Winvalid-pch` as an
error, which stays as a compile flag so a `.gch` that stops matching fails the build.

- Per build folder: 2 precompiled headers (3 in the fuzz build), checked by counting `.pch`/`.gch` files after
  `check`.
- A full `check` from cold no slower than before (time one before and after); an edit to `src/core/types.hpp`
  rebuilds every object that includes it on all three toolchains (a test or a scripted check).
- `build/` after a `check` and a week of normal work: the warm trees, the check's trees, no tree older than 7 days,
  no cache entry older than 14 days.

## Left to the builder

- Where the PCH rule lives and how it hooks XMake's build graph so the PCH builds once, before its users, in
  parallel with other work.
- Moving the defines: per-file defines or a generated header.
- MSVC: where the `/Yc` object links, and `/Z7` versus a shared PDB.
- The stamp's file name and format.

## Outcome

Implemented 2026-11-16 on branch `build-size`, executed inline from the plan (`docs/superpowers/plans/2026-11-16-build-folder-size.md`).

### Each toolchain

- **Clang shares** ("build(xmake): share one precompiled header per build folder with Clang"): 2 per build folder,
  and 5 in the sanitizer folder (the 2 shared plus the 3 fuzz targets' own, as `-fsanitize=fuzzer` changes their
  flags).
- **GCC shares** ("build(xmake): share the precompiled headers under GCC"): XMake hands each source
  `-I <the owner's .gch folder> -include pch.hpp`, so a user finds the owner's wrapper header and `.gch` once
  `pcoutputfile` points at the owner. `-H` shows game and test objects loading the shared `.gch`. GCC keeps a `.gch`
  valid when a source adds a define the header never reads (its own documented rule), so the planned `-DPN_PROBE`
  check built fine and went stale nowhere. A define the header does read (`_GLIBCXX_ASSERTIONS`) fails on
  `-Werror=invalid-pch`, which names it. Without the flag, that build also failed, but only on
  `ignoring '#pragma system_header'` from parsing XMake's wrapper, so the flag's value is the clear cause.
- **MSVC keeps one per target** ("build(xmake): keep MSVC's precompiled headers per target"). `cl` refuses a
  precompiled header for any `/D` (C4605) or `/I` (C4599, which also checks their order) its build lacked. Trial 1
  failed in `gfx`, whose `utils.bin2c` include path comes first. Trial 2 moved a target's own include paths to its
  raw flags, which XMake emits last, and failed in `data` on yaml-cpp's `/DYAML_CPP_STATIC_DEFINE` and its `/I` ahead
  of the inherited ones. Every module but `game` adds a private package or inherits SDL's from `platform`. Sharing
  would need every package's flags public on the owner, undoing the modules' private packages (0002 §4), or
  silencing the only check that a precompiled header matches its source. So the `/Yc` object's linking and `/Z7`
  versus a shared PDB were never reached. Design 0001 rule 19 and xmake.lua say why.

### Before and after

Cold builds after `project_n clean`, on an idle machine (32 shared cores), XMake's time with wall time in brackets.
Before: Task 0 at "docs: plan a smaller build folder". After: the branch head.

| Build folder | Precompiled headers | Tree | Cold build |
|---|---|---|---|
| linux-clang debug | 17, 0.41 GB → 2, 0.05 GB | 1.6 → 1.3 GB | 23.2 s (35) → 19.8 s (29) |
| linux-gcc release | 17, 2.40 GB → 2, 0.28 GB | 3.3 → 1.3 GB | 79.5 s (90) → 60.7 s (67) |
| windows-msvc release (container) | 17, 2.60 GB → 17, 2.60 GB | 3.5 → 3.5 GB | 62.3 s (69) → 52.5 s (60) |

MSVC's build is unchanged and still ran 16% faster, so that is the run-to-run noise. Goal 4 (no slower) holds:
Clang's gain (15%) is within that noise, and GCC's (24%) only partly beyond it. After `touch src/core/types.hpp`, linux-clang debug recompiled
170 files (2 of them precompiled headers) in 3.8 s, against 185 (17) in 4.5 s before. An edit to `types.hpp`
rebuilt all 170 on Clang and GCC, and an edit to `src/pch.hpp` rebuilt 79 (the game's header and its 78 objects;
test objects don't include it). The build cache's stale-object check passed on Clang: a header change still missed
the cache. The unity and ASan builds pass.

### The sweep and the cache

- The sweep ("build(tools): stamp each build tree and sweep those unused for a week") first ran in the worktree as
  `clean --stale --days 0`. It removed linux-gcc/release (3.3 GiB) and the container's windows-msvc/release
  (3.5 GiB), and kept linux-clang debug and release. The main checkout's first sweep comes after the merge. Its
  build folder was 45 GB on 2026-11-16. Judged by their newest files, two trees were 7 days old or more that day
  (the container's windows-msvc releasedbg 2.2 GB and debug-address 3.5 GB). Four more (10.9 GB) reach that age
  within two days,
  and every shared-PCH tree drops 0.4–2.2 GB on its next rebuild.
- Cache ageing ("build(tools): age XMake's build cache and drop the ones nothing reads"): `CACHE_DAYS` went from
  14 to 7, the user's choice with these numbers ("build(tools): age the build cache after 7 days, not 14"). The
  main checkout's linux-clang/debug cache held 6.9 GB in 3,147 entries for 744 MB of objects, growing about 1 GB a
  day, with none older than 7 days. So 14 days would have capped it near 14 GB, and 7 days caps it near 7 GB.
  Release's held 2.7 GB. GCC's caches in the main checkout (264 MB to 490 MB, unused) go whole on the first sweep.

### Rulings

- The defines moved into a generated header ("build(xmake): give the version and data paths their own generated
  header"): a `headeronly` target `build_info`, public through the owner, so every target gets the same `-I`.
- The rule: owner targets `game_pch` and `test_pch` (kind `object`, `build.fence`), and rule `shared_pch` on each
  user. The rule turns off the user's `c++.build.pcheader` and replaces `target.pcoutputfile` with a lazy delegate
  to the owner, because XMake 3.1.2 caches the path in a private field. An XMake bump that renames `pcoutputfile`
  breaks the build loudly. `SHARED_PCH_TOOLCHAINS` reaches rule scripts through `set_values`. The fence covers
  indirect users, as XMake's deep orders walk the whole chain.
- The sweep keeps both the native and container tree of the command's preset and leaf (a host can't know which the
  command will use), and every preset's tree under `build --preset all`. A preset folder emptied of trees goes too.
- Cache ageing runs on every tree the sweep keeps, the warm ones included, with one summary line.
- `-Winvalid-pch` is set for `pixi-gcc` only.
- The success measure "a full `check` from cold no slower" was taken as the three cold builds above, which are
  `check`'s slowest parts (the plan's Task 0).

