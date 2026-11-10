# A faster `project_n check`, and loose ends from the superpowers switch

Date: 2026-11-10 · Status: approved 2026-11-10

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
