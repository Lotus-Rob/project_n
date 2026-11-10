# A faster `project_n check`, and loose ends from the superpowers switch

Date: 2026-11-10 · Status: approved 2026-11-10, implemented with the changes in **Outcome** (end of this document)

## Intent

The user waits on `project_n check` before every PR (and on `lint` after every task, since superpowers' per-task
verification is `test` + `lint` on the native preset). `check` takes 7–9 minutes on the user's machine. They want it
around **5 minutes**, while keeping **everything it checks today**; leaning on CI is only a fallback if that proves
infeasible.

The same branch also closes the loose ends left by the superpowers switch (PR #17), so the next session can pick up
work from a file rather than a search.

## Measured (2026-11-10, origin/main be9ee0a, 32 cores, each step of `check` run on its own)

Seconds, cold checkout / warm after a change to `src/core/types.hpp`:

| step | cold | warm |
|---|---|---|
| selftest + fmt | 1 | 1 |
| **lint** (its build ~3 s; clang-tidy over 93 files in one serial process) | **333** | **313** |
| test linux-gcc release | 46 | 35 |
| test linux-clang release | 31 | 14 |
| test windows-msvc release (container, Wine) | 92 | 82 |
| reference pictures | 19 | 3 |
| unity test | 27 | 11 |
| releasedbg build | 36 | 12 |
| **total** | **586** | **471** |

Lint is about two thirds of `check` and nearly all of the per-task `lint`. clang-tidy 22.1.8 has no parallel option;
`run-clang-tidy`, already in the Pixi environment (`clang-tools`), has `-j`, `-p`, `-quiet` and a safe `-fix` (fixes
exported per file and applied once by `clang-apply-replacements`, also already installed).

## Goals

1. `lint` runs clang-tidy in parallel across all cores, with the same findings, the same failure rule and the same
   handling of library findings (`IGNORED_FINDINGS`, `tidy_verdict`) as today.
2. `lint --fix` stays correct when several files share a header (no concurrent writes to one file).
3. Measure `check` again, cold and warm, the same way as above. Done when warm `check` is ≤ 5 min; if not, come back to
   the user with the new numbers before doing more.
4. Loose ends (below) closed on the same branch.

## Non-goals

- Linting only the `.cpp` files a changed header reaches: with lint parallel it should save little and its dependency
  logic would have to be exactly right. Revisit only if goal 3's numbers say so.
- Overlapping other `check` steps (windows-msvc's container and Wine, reference pictures): next lever only if needed.
- Running XMake builds in parallel (each already uses every core; package installs race; XMake's output is never piped).
- Moving any check to CI.

## Anti-goals

- A lint that passes where today's fails: fewer files checked, a finding lost, a crash or compiler error read as
  success, or a library finding no longer ignored the way it is now.
- Interleaved, unreadable output: each file's findings stay together.
- `lint --fix` corrupting a header that two files reach.

## Constraints

- Every tool from Pixi; `project_n.py` stays one file, standard library only, thin (it calls the pinned tools; what
  they do stays in them). No new dependency: `run-clang-tidy` and `clang-apply-replacements` are in `clang-tools`.
- New logic is test-first in `tools/test_project_n.py`; `check` passes; container gates per AGENTS.md (from a
  worktree, CI's `lint` and `container` jobs).
- Container `lint --container` runs the same code in the Linux image, which has the same Pixi environment.

## Design

### 1. Investigate `run-clang-tidy` first (a short spike, findings into this spec's Outcome)

Before changing `lint`, establish on the pinned 22.1.8:
- how it takes the file list (it matches positional arguments as regular expressions against the compile database)
  and that exactly the 93 files lint passes today get checked, no more, no fewer;
- its exit code when there are findings, when a file fails to compile and when clang-tidy crashes, and where each is
  reported (stdout or stderr: `cmd_lint` reads only stdout today, so a crash in one file next to only ignored findings
  elsewhere must still fail, never pass);
- its output: whether each file's output stays together, and whether `tidy_verdict` and `IGNORED_FINDINGS` read it
  unchanged (the two quill `InlinedVector.h` findings must still be ignored, and nothing else);
- how `-fix` behaves with a header shared by two files;
- its wall-clock time on the warm 93-file lint.

### 2. `lint` calls `run-clang-tidy -j <cores>`

`-p` the same build directory, `-quiet`, the same units; `--fix` maps to its `-fix`. If the spike shows its output or
exit codes can't keep today's verdict exactly, the fallback is a process pool in `project_n.py` that runs one
`clang-tidy` per file, collects each file's output and prints it whole, and applies fixes through
`-export-fixes` + `clang-apply-replacements`.

### 3. Measure again

Same per-step timing, cold and warm; numbers into this spec's Outcome.

### 4. Loose ends

- **Picking work up again** (the user's request, 2026-11-10): the `handover` skill writes into the project memory's
  `in-flight.md` (the memory index loads it every session; worktrees share the one memory folder) instead of the
  session scratchpad, which the next session can't find. The user's decisions:
  - **one short section per piece of work in flight, headed by its branch**: its state, the decisions so far, the next
    step, and pointers to its spec, plan, ledger and worktree;
  - **a handover rewrites only its own section**; the section is deleted once its branch merges;
  - **the long handover format and the scratchpad file go**: commits, standing rules and gotchas already live in git,
    AGENTS.md and memory.
  Root AGENTS.md gains one line for "continue where we left off": read `in-flight.md`, `git status` and the worktrees,
  any `.superpowers/sdd/*/progress.md`, then search the palace from the section's date. The current `in-flight.md` is
  reshaped to that format.
- **Last branch's spec** (`2026-11-10-superpowers-workflow-design.md`, Outcome): record that after a restart ponytail is
  off and the LSP runs Pixi's clangd (verified 2026-11-10); LSP inside a worktree remains to be seen.
- **`prune-worktrees` minors** (from PR #17's review):
  - a worktree folder deleted by hand (`prunable` in the porcelain output) must not stop the run; prune those first or
    skip them;
  - never remove the checkout it runs in (`ROOT`);
  - a lock left by a crashed session: say how to release it (`git worktree unlock <path>` once no session uses it);
  - names left from the reversed ruling: `clean_worktrees` → `prune_worktrees`, `CleanWorktreesTest` →
    `PruneWorktreesTest`, and no one-line wrapper;
  - the ownership test also asserts branch, lock and detached parsing.
- **Windows LSP**: `.claude/marketplace/plugins/clangd/.lsp.json` names a POSIX path, so Windows hosts have no LSP now
  that the global clangd plugin is off there too. Make it work on Linux, macOS and Windows (for example by starting
  clangd through `pixi run` with the project's manifest, if that keeps stdout clean for the language server protocol).
  It can't be tested on Windows here: say so in the Outcome.
- **AGENTS.md "Finishing a branch" step 5** (my call): `git switch main` fails or carries changes when the main checkout
  holds other work; `prune-worktrees` fetches and uses `origin/main` itself, so the step says to update `main` only if
  the main checkout is free.

## Left to the builder

- Commit split and order (the lint change, the loose ends and docs are separable).
- How the file list reaches `run-clang-tidy` (escaped regexes, a single alternation, or a compile database filtered to
  the units).
- Job count (default: all cores).
- The exact wording of every doc line, within the size limits.

## Outcome (2026-11-10)

**Goal met:** warm `check` 471 s → **174 s (2.9 min)**, cold 586 s → 278 s (4.6 min), with everything `check` did before.
Measured as above, at this branch's head:

| step | cold | warm |
|---|---|---|
| selftest + fmt | 1 | 2 |
| **lint** | **51** | **31** |
| test linux-gcc release | 58 | 31 |
| test linux-clang release | 30 | 12 |
| test windows-msvc release (container, Wine) | 80 | 73 |
| reference pictures | 4 | 3 |
| unity test | 26 | 10 |
| releasedbg build | 29 | 11 |
| **total** | **278** | **174** |

`lint --all` on its own: 329 s serial → 33 s (93 units, 32 at a time). windows-msvc is now the largest step; the non-goals
stay non-goals while `check` is under 5 minutes.

What changed against the design, and why:
- **A per-file pool, not `run-clang-tidy`.** Its source (22.1.8, main loop) gives one exit code: 1 if any file returned
  non-zero for any reason. A file erroring without a finding ("Error while processing") beside files with only the
  ignored quill finding would look like a clean run with ignored findings, which lint fails today. `tidy_units` runs
  one clang-tidy per unit on every core and judges each with `tidy_verdict`.
- **`--fix` stays serial, on the failing units only** (changed after the final review). The first version exported each
  unit's fixes and applied them once with `clang-apply-replacements`; the reviewer showed on 22.1.8 that this also
  applies note fix-its that only silence a warning (a `static_cast<long>` around a widening multiply, plus an added
  `#include <cstddef>`), which `clang-tidy --fix` never does. Now the parallel run only judges, and `--fix` runs one
  serial `clang-tidy --fix` over the units that failed. Checked for real with a `typedef` and a widening multiply in
  `src/core/types.hpp`: the `typedef` became `using`, the multiply was left alone.
- **Every review finding was fixed, Minor included** (the user's rule, now in AGENTS.md "Finishing a branch" step 3).
- **A finding prints once** however many units report it (also from the review: one `typedef` in a header printed 93
  times). Findings are compared by resolved path, since clang-tidy names a shared header `./src/…`, `src/…` or by its
  absolute path depending on the unit.
- **The ignored count stays per finding.** One process per unit reports a shared header's finding once per unit that
  reaches it; findings are counted, like printed, once by resolved path, so `--all` still reports the same two quill
  `InlinedVector.h` findings (a first version summed them per unit: 5).
- **Job count:** `jobs()`, the CPUs this process may use (`os.process_cpu_count()`: affinity and cgroup cpusets), not
  the machine's.
- **clangd through `pixi run`** (`--manifest-path ${CLAUDE_PROJECT_DIR}/pixi.toml --frozen --quiet`): an LSP `initialize`
  request gets `Content-Length:` as the first bytes on stdout, as with the direct binary, both with the configured
  `--background-index`; it starts in 0.04 s instead of 0.02 s. **Untested on Windows** (no Windows host here); it loads at the next session start.
- **The handover skill**, tested with superpowers writing-skills: the old skill wrote a seven-section file to the
  session scratchpad and left `in-flight.md` stale; with the new one, two runs each rewrote only their own branch's
  section, with the other section and the header unchanged byte for byte.
- **`prune-worktrees`:** a folder deleted by hand is left to the closing `git worktree prune`; the checkout it runs in is
  kept; a stale lock says how to release it; `clean_worktrees` became `prune_worktrees`, registered directly. From the
  final review: a deleted folder is reported, and its branch deleted after the prune when its work is merged.
