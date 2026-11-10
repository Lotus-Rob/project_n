# Faster Check Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut warm `project_n check` from ~7.9 min to ≤ 5 min by running clang-tidy in parallel, keeping every check and verdict, and close the superpowers-switch loose ends.

**Architecture:** `cmd_lint` hands its units to a new `tidy_units()` that runs clang-tidy across all cores (`run-clang-tidy -j`, or a per-file process pool if the Task 2 spike shows `run-clang-tidy` can't keep today's verdict exactly) and returns the same (failed, ignored) verdict `tidy_verdict` gives today. Loose ends are small changes to `prune-worktrees`, the clangd plugin, the handover skill and AGENTS.md.

**Tech Stack:** Python 3.14 standard library (`tools/project_n.py`, unittest), clang-tidy / run-clang-tidy / clang-apply-replacements 22.1.8 from Pixi's `clang-tools`, Git worktrees, Claude Code plugin `.lsp.json`.

**Spec:** `docs/superpowers/specs/2026-11-10-faster-check-design.md`

## Global Constraints

- No new dependency: `run-clang-tidy` and `clang-apply-replacements` come from `clang-tools ==22.1.8`, already pinned.
- `project_n.py` stays one file, standard library only; XMake's output is never piped; clang-tidy output may be captured (it is today).
- Lint must never pass what it fails today: same units, same findings, `IGNORED_FINDINGS` ignored and nothing else, a crash or compiler error in any one file fails.
- `lint --fix` never has two processes writing one file: fixes are exported and applied once (`clang-apply-replacements`), then `fmt` as today.
- Job count: all cores (`os.cpu_count()`).
- Done when warm `check` ≤ 5 min (measured as in the spec); otherwise stop and bring the numbers to the user.
- Worktrees inside the repo; container gates from a worktree are CI's `lint` and `container` jobs (AGENTS.md).
- Commits: Conventional Commits, scopes from `SCOPES`; not `feat`/`fix` (no game code); nothing marked breaking.

## Review Focus

1. A clang-tidy crash or compiler error in one file while every other file has only ignored quill findings: lint must fail (Task 3 test `test_crash_in_one_file_fails_beside_ignored_findings`).
2. A finding in a header reached by many units, reported once per unit: still one failure, and the ignored-count message stays sensible (Task 3 test `test_ignored_count_sums_files`).
3. `lint --fix` with two units sharing a header: the header gets each fix once, not twice or garbled (Task 3 step: a real run on a scratch change).
4. `prune-worktrees` with a worktree folder deleted by hand: it carries on and prunes it (Task 4 test `test_skips_prunable_worktrees`).
5. The Windows clangd launcher printing anything but the language-server protocol on stdout (Task 5 step: protocol check).

---

### Task 1: Branch, spec and plan

- [ ] **Step 1:** superpowers:using-git-worktrees (it asks the user) → Claude Code's `EnterWorktree` named `faster-check` (branches from `origin/main`). In it: `pixi run project_n setup`.
- [ ] **Step 2:** Copy `2026-11-10-faster-check-design.md` and `2026-11-10-faster-check.md` from the scratchpad (`/tmp/claude-1000/-home-lotus-Development-project-n/e79c6df9-1141-4327-aeac-37b8bd04d80c/scratchpad/`) into `docs/superpowers/specs/` and `docs/superpowers/plans/`; set the spec's status to "approved 2026-11-10".
- [ ] **Step 3:** Commit `docs(design): add the faster check spec and plan`.

### Task 2: Spike: can `run-clang-tidy` keep today's verdict? (throwaway, no commit)

On the warm linux-clang debug build of this worktree (`pixi run project_n lint` once to make it), with the exact 93 units lint passes:
- [ ] **Step 1:** `run-clang-tidy -j $(nproc) -quiet -p build/linux-clang/debug <units as anchored, escaped regexes>`: record wall time, exit code, files checked (count the "clang-tidy … <file>" command lines; must be exactly the 93), whether each file's output is contiguous, and whether `tidy_verdict(stdout, rc)` gives today's (False, ignored=N).
- [ ] **Step 2:** Break one file on purpose in a scratch copy (a syntax error; then a forced crash if one can be provoked, e.g. `-checks=-*` plus an unreadable file) and record exit code, stream and wording.
- [ ] **Step 3: Rule** in the ledger: use `run-clang-tidy` only if its exit code and output let `tidy_units` tell "only ignored findings" from "a file crashed or failed to compile" with certainty; otherwise the per-file pool. Record the timings.

### Task 3: Parallel clang-tidy in `lint`

**Files:** Modify `tools/project_n.py` (`cmd_lint`, new `tidy_units`, `tidy_verdict` unchanged), `tools/AGENTS.md` (a Gotcha line on how lint runs clang-tidy); Test `tools/test_project_n.py`.

**Interfaces:**
- Produces: `tidy_units(bdir: Path, units: list[str], fix: bool) -> tuple[bool, int]`: (failed, ignored findings), printing each file's output contiguously. `cmd_lint` raises `Fail` exactly as today on `failed`, and prints the ignored count as today.
- Per-file pool form (if Task 2 ruled so): `tidy_one(bdir, unit, fixes_dir: Path | None) -> tuple[str, int]` (stdout, returncode), run under `concurrent.futures.ThreadPoolExecutor(os.cpu_count())`; with `fix`, each run gets `--export-fixes=<fixes_dir>/<n>.yaml` and `clang-apply-replacements <fixes_dir>` runs once after all of them; the verdict is `tidy_verdict` per file, failed if any file fails, ignored summed.

- [ ] **Step 1: Write the failing tests** (`class ParallelTidyTest`, mocking the subprocess layer the chosen form uses): `test_clean_units_pass` (all rc 0 → `(False, 0)`); `test_crash_in_one_file_fails_beside_ignored_findings` (one file rc −11 or a crash marker, others rc 1 with only the quill `clang-analyzer-security.ArrayBound` finding → failed); `test_compiler_error_fails` (a `clang-diagnostic-error` finding → failed); `test_ignored_count_sums_files` (two files each with the quill finding, rc 1 → `(False, 2)`); `test_every_unit_checked_once` (the units passed in == the units run); `test_fix_applies_once` (with `fix=True`, `clang-apply-replacements` runs exactly once, after every file, and no clang-tidy call carries `--fix`).
- [ ] **Step 2:** `pixi run project_n selftest` → the new tests fail (`tidy_units` not defined).
- [ ] **Step 3:** Implement `tidy_units` in the form Task 2 chose; `cmd_lint` calls it in place of the single `clang-tidy` run.
- [ ] **Step 4:** `pixi run project_n selftest` passes; `pixi run project_n lint` passes in the worktree and prints the same ignored count as before the change; record lint's wall time.
- [ ] **Step 5: `--fix` for real:** add a fixable finding to a header two units include (scratch, not committed), `pixi run project_n lint --fix`, check the header changed exactly once, then discard the scratch change.
- [ ] **Step 6: Commit** `build(tools): run clang-tidy across all cores in lint`.

### Task 4: `prune-worktrees` minors

**Files:** Modify `tools/project_n.py` (worktree functions), `tools/test_project_n.py`.

**Interfaces:**
- Renames: `clean_worktrees()` → `prune_worktrees()`, registered directly as the `prune-worktrees` command's `func` (no `cmd_prune_worktrees` wrapper; it takes the parsed `args` and ignores them); `CleanWorktreesTest` → `PruneWorktreesTest`.
- `owned_worktrees` entries gain `"prunable": bool`.
- `worktree_unremovable` gains: `path == ROOT` → `"this checkout (run it from another)"`; locked → `"locked (a session is using it; once none is, git worktree unlock <path>)"`.

- [ ] **Step 1: Failing tests:** `test_skips_prunable_worktrees` (a porcelain block with `prunable gitdir file points to non-existent location` → no `git -C` call for it, the run reaches `git worktree prune`); `test_keeps_this_checkout` (entry path == `project_n.ROOT` → reason contains "this checkout"); `test_locked_says_how_to_unlock` (reason contains `git worktree unlock`); extend `test_owns_only_worktrees_under_the_project_folders` to assert `branch`, `locked` and detached (`None`) per entry.
- [ ] **Step 2:** `pixi run project_n selftest` → those fail.
- [ ] **Step 3:** Implement; prunable entries are skipped (left to the final `git worktree prune`).
- [ ] **Step 4:** `pixi run project_n selftest` passes; `pixi run project_n prune-worktrees --help` still works.
- [ ] **Step 5: Commit** `build(tools): make prune-worktrees safe around missing and current worktrees`.

### Task 5: clangd on Windows too

**Files:** Modify `.claude/marketplace/plugins/clangd/.lsp.json`; AGENTS.md Map line if its wording changes.

- [ ] **Step 1:** Candidate: `"command": "pixi"`, `"args": ["run", "--manifest-path", "${CLAUDE_PROJECT_DIR}/pixi.toml", "--frozen", "--quiet", "clangd", "--background-index"]`.
- [ ] **Step 2: Protocol check:** send a minimal LSP `initialize` request (Content-Length framed) to the candidate on stdin; the first bytes on stdout must be `Content-Length:` (nothing before them). Compare startup time with the direct binary.
- [ ] **Step 3:** If stdout is clean, use it; if not, keep the direct path for Linux/macOS and record in the spec's Outcome that Windows LSP stays open with the reason. Either way the Outcome says Windows is untested here.
- [ ] **Step 4:** `claude plugin validate .claude/marketplace` passes; `pixi run project_n doctor --docs` passes.
- [ ] **Step 5: Commit** `chore(agents): start clangd through pixi so the LSP works on every host`.

### Task 6: Picking work up again, and AGENTS.md

**Files:** Modify `.claude/skills/handover/SKILL.md`, `AGENTS.md`; outside the repo: `~/.claude/projects/-home-lotus-Development-project-n/memory/in-flight.md`.

- [ ] **Step 1: Handover skill** (superpowers writing-skills governs the change): where = the section for this branch in the project memory's `in-flight.md`, created if missing, rewritten (never the other sections) at each stopping point, deleted when the branch merges; contents = state, decisions so far, next step, pointers (spec, plan, ledger, worktree path); drop the scratchpad file and the long format (commits table, standing rules, gotchas live in git, AGENTS.md, memory). Keep "When" and the checks that still apply.
- [ ] **Step 2: AGENTS.md**, Working with superpowers: one line, "Continue where we left off": read `in-flight.md` (memory), `git status` and `git worktree list`, any `.superpowers/sdd/*/progress.md`, then search the palace (`project_n` wing) from the section's date. Finishing a branch step 5: update `main` in the main checkout only if it is free (no other branch's work open there); `prune-worktrees` fetches and uses `origin/main` itself; after the merge also delete the branch's `in-flight.md` section.
- [ ] **Step 3:** Reshape `in-flight.md` to one section per branch (`faster-check`, `phase6-text-chunk3`), each short.
- [ ] **Step 4:** `pixi run project_n doctor --docs` passes; `wc -l AGENTS.md` ≤ 100.
- [ ] **Step 5: Commit** `docs(agents): hand work over through in-flight.md`.

### Task 7: Measure again; Outcomes

- [ ] **Step 1:** Per-step timing as in the spec (each step of `check` run on its own; cold in a fresh throwaway worktree at this branch's HEAD under `.worktrees/`, then warm after a change to `src/core/types.hpp`); remove the throwaway worktree (restore `xmake-requires.lock` if XMake reordered it; never `--force`).
- [ ] **Step 2:** If warm > 5 min: stop and bring the numbers to the user.
- [ ] **Step 3:** This spec's Outcome: the Task 2 ruling with its numbers, the new per-step table, Task 5's result, any other ruling from the ledger; status "implemented". The superpowers-workflow spec's Outcome: after a restart ponytail was off and the LSP ran Pixi's clangd (verified 2026-11-10); LSP inside a worktree still to be seen.
- [ ] **Step 4: Commit** `docs(design): record what the faster check became`.

### Task 8: Finish

- [ ] **Step 1:** `pixi run project_n lint --fix`, then `pixi run project_n check` (the new lint inside it) passes.
- [ ] **Step 2:** superpowers requesting-code-review on `origin/main..HEAD` with the spec; receiving-code-review; `check` again after fixes.
- [ ] **Step 3:** Push, `pixi run gh pr create --fill`, wait for CI's six checks (its `lint` job runs `lint --container`, the new code in the Linux image); report the PR and stop: the user merges.
