# Superpowers Workflow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make superpowers project_n's process layer, retire the project skills and agents it replaces, and make worktrees cheap (ccache, reflinked `build/`, `./dev worktree`).

**Architecture:** AGENTS.md files keep only project rules and map them onto superpowers' steps; deleted skills' still-wanted rules move into AGENTS.md as one-liners. Worktree support is one new `project_n.py` command plus ccache environment set in `xmake_env()`; both are covered by `tools/test_project_n.py`.

**Tech Stack:** Markdown agent docs, Python 3.14 standard library (`tools/project_n.py`, unittest), XMake, Pixi (conda-forge `ccache`), Git worktrees, btrfs reflinks.

**Spec:** `docs/superpowers/specs/2026-11-10-superpowers-workflow-design.md`

## Global Constraints

- Every tool comes from Pixi; `ccache` pinned with `==` in `pixi.toml`, `pixi.lock` in the same commit.
- Docs and scripts go through `project_n.py`; never quote `git worktree`, `xmake` or `podman` as the thing to run.
- `project_n.py` stays one file, standard library only; every new command has `--help` text and self-tests.
- Commits: Conventional Commits, scopes from `SCOPES` (`agents`, `tools`, `env`, `design`, …); `feat`/`fix` only for game code; linear history.
- Never touch the root `.clang-tidy` or `.clangd`.
- ccache: off when `CI` is set, inside containers (`IN_CONTAINER`) and on Windows hosts; CI behaviour unchanged.
- Only worktrees under `.worktrees/` or `.claude/worktrees/` are ever removed; never with `--force`.
- Container gates (`check --container`, `lint --container`) run from the main checkout.
- `shortcut:` tags in `src/game/arena.cpp`, `src/main.cpp`, `src/gfx/text_layout.cpp` are left for chunk 3.
- A change is done when `pixi run project_n check` passes (plus `--container` for the ccache task's environment change).

## Review Focus

1. A reflinked `build/` makes XMake link an object compiled from the main checkout's sources: must rebuild, never reuse (Task 5 spike decides; Task 6 test pins the fallback).
2. `worktree clean` on a worktree with uncommitted changes, unpushed commits or an open PR: must refuse and name why (Task 6 tests).
3. ccache returning a stale object after `pch.hpp` or a generated header changes: the edit must show in the next build (Task 4 step).
4. `worktree setup` on a host without reflinks (Windows, macOS, ext4): must succeed without the head start (Task 6 test).
5. ccache turning on in CI or a container through XMake's default detection: must stay off (Task 4 test).

---

### Task 1: Move this session's workflow files onto a new branch

**Files:**
- Move from the `phase6-text-chunk3` working tree: `.claude/settings.json`, `.claude/marketplace/` (whole tree), `docs/superpowers/specs/2026-11-10-superpowers-workflow-design.md`, `docs/superpowers/plans/2026-11-10-superpowers-workflow.md`
- Revert in the `phase6-text-chunk3` tree: `AGENTS.md` (the `.claude/settings.json` map line), `.claude/agents/branch-reviewer.md` (the LSP lines)
- Modify on the new branch: `.claude/settings.json`, `AGENTS.md` (Map)

- [ ] **Step 1:** Create branch `superpowers-workflow` from `origin/main` in a worktree via superpowers:using-git-worktrees (it asks the user first). A worktree is what leaves chunk 3's uncommitted tree untouched; if the user declines, stop and ask how to park chunk 3's work. `worktree setup` doesn't exist yet, so run `pixi run project_n setup` in the new worktree.
- [ ] **Step 2:** Copy the four paths above into the new branch's tree; then in the main checkout delete the untracked copies and `git restore AGENTS.md .claude/agents/branch-reviewer.md`. Check: `git -C <main checkout> status --short` lists only chunk 3's game files.
- [ ] **Step 3:** In `.claude/settings.json` add `"ponytail@ponytail": false` to `enabledPlugins`. Re-add the Map line to `AGENTS.md`: ``- `.claude/settings.json` turns ponytail off and swaps the global clangd plugin for `.claude/marketplace/`'s, which runs Pixi's clangd; the `LSP` tool reads `build/compile_commands.json`, which native builds refresh``.
- [ ] **Step 4: Verify:** `claude plugin validate .claude/marketplace` → "Validation passed"; `pixi run project_n doctor --docs` → "every quoted command exists".
- [ ] **Step 5: Commit** (two commits): `docs(design): add the superpowers workflow spec and plan` (the two docs); `chore(agents): run Pixi's clangd for the LSP tool and turn ponytail off` (settings, marketplace, Map line).

### Task 2: Rewire the agent docs onto superpowers

**Files:**
- Delete: `.claude/skills/fix-bug/`, `.claude/skills/review-branch/`, `.claude/skills/agent-docs/`, `.claude/agents/branch-reviewer.md`, `.claude/agents/bug-reproducer.md`
- Modify: `AGENTS.md`, `tests/AGENTS.md`, `.claude/skills/handover/SKILL.md`

- [ ] **Step 1: Root AGENTS.md**, all within ~100 lines:
  - Map: `docs/design/` → "past designs and their rationale: read before redesigning anything"; add ``docs/superpowers/`: `specs/` (designs, `YYYY-MM-DD-<topic>-design.md`) and `plans/` (`YYYY-MM-DD-<feature>.md`)``.
  - New section **Working with superpowers** (superpowers runs the process; these lines are where it meets this repo, and they win):
    - Worktree project setup = `pixi run project_n worktree setup`; baseline = `pixi run project_n test`.
    - Verification after each task = `pixi run project_n test` and `pixi run project_n lint` (native preset); before a PR = `pixi run project_n check`. Container gates run from the main checkout.
    - Code review: the reviewer also checks the diff against the AGENTS.md of every directory it touches, and finds callers of changed C++ symbols with the `LSP` tool (`findReferences`, `incomingCalls`) rather than grep.
    - Finishing a branch: never "merge locally"; always the PR (below).
  - **Finishing a branch**: 1 lint/fmt (unchanged) · 2 `check` (unchanged) · 3 superpowers requesting-code-review on `main..HEAD`, acting on it with receiving-code-review · 4 push, `pixi run gh pr create --fill`, wait for CI's six checks green on a branch up to date with main, then stop and tell the user: the user merges · 5 after the merge: from the main checkout `git switch main && git pull --ff-only`, then `pixi run project_n worktree clean`.
  - Rules: test-first bug rule drops "→ fix-bug skill" and adds "a fuzzer crash also adds its seed to `tests/fuzz/corpus/<target>/`"; add the agent-docs one-liners: AGENTS.md (facts for any task in its directory, ~100 lines) · skills (how to do one job, ~200 lines) · each AGENTS.md has a sibling `CLAUDE.md` holding only `@AGENTS.md` · quoted commands must exist (`doctor --docs`) · write and change skills with superpowers writing-skills.
  - Skills list: drop agent-docs, fix-bug, review-branch. Agents list: keep only log-triage.
  - Keeping this true: "Propose a skill when a workflow repeats" → "(superpowers writing-skills)".
- [ ] **Step 2: tests/AGENTS.md** gains one rule line: a bug's test is small (fewest lines that reach the fault), contained (no file, network, window, clock or shared state it doesn't need), explicit (named after the behaviour, literal inputs, exact expected values) and about one behaviour; logic only reachable through `main.cpp` or SDL moves into a `src/` function first; a sanitizer finding's test fails under that sanitizer (`--sanitizer <name>`).
- [ ] **Step 3: handover skill:** "Decisions and rationale go into the design doc" → "into the spec (`docs/superpowers/specs/`)"; item 1 adds "the plan and its progress ledger (superpowers' plan workspace), which the next session resumes from"; item 5 drops "and `shortcut:`s that are the user's call".
- [ ] **Step 4: Verify:** `pixi run project_n doctor --docs` passes; `grep -rn "fix-bug\|review-branch\|branch-reviewer\|bug-reproducer\|agent-docs\|REVIEW: CLEAN\|numbered sub-doc" AGENTS.md */AGENTS.md .claude` prints nothing; `wc -l AGENTS.md` ≤ 100.
- [ ] **Step 5: Commit:** `docs(agents)!: hand the process to superpowers` with a `BREAKING CHANGE:` footer naming the removed skills and agents (git-commit skill).

### Task 3: Drop the `shortcut:` tag

**Files:** every file `git grep -l "shortcut:" -- src tools` lists except `src/game/arena.cpp`, `src/main.cpp`, `src/gfx/text_layout.cpp`. Not `docs/design/`.

- [ ] **Step 1:** In each, remove the `shortcut: ` tag and keep the sentence (capitalise or re-join as the surrounding comment needs). Doxygen `///` comments stay valid.
- [ ] **Step 2: Verify:** `git grep -n "shortcut:" -- src tools` lists only the three chunk 3 files; `pixi run project_n lint` and `pixi run project_n fmt --check` pass.
- [ ] **Step 3: Commit:** `docs: drop the shortcut: tag from comments`.

### Task 4: Shared ccache for native builds

**Files:**
- Modify: `pixi.toml`, `pixi.lock`, `tools/project_n.py` (`xmake_env`, `configure`), `tools/AGENTS.md` (Gotchas)
- Test: `tools/test_project_n.py`

**Interfaces:**
- Produces: `ccache_env() -> dict[str, str]` in `project_n.py`: `{}` when `os.environ.get("CI")`, `IN_CONTAINER` or `HOST == "windows"` (MSVC with ccache is unproven; spec), otherwise `CCACHE_DIR=<~/.cache/ccache/project_n>`, `CCACHE_BASEDIR=<str(ROOT)>`, `CCACHE_NOHASHDIR=1`, `CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime`. `xmake_env()` merges it; `configure()` passes `--ccache=y` when it is non-empty, else `--ccache=n`.

- [ ] **Step 1: Write the failing tests** in a new `class CcacheTest(unittest.TestCase)`: `test_off_in_ci` (env `CI=true` → `ccache_env() == {}` and configure's `xmake f` argv contains `--ccache=n`); `test_off_in_container` (`IN_CONTAINER=True` → `{}`); `test_off_on_windows` (`HOST="windows"` → `{}`); `test_on_natively` (no `CI`, not in a container → `CCACHE_BASEDIR == str(project_n.ROOT)`, `CCACHE_NOHASHDIR == "1"`, `"pch_defines" in CCACHE_SLOPPINESS`, argv contains `--ccache=y`). Mock `run` as the existing tests do.
- [ ] **Step 2:** `pixi run project_n selftest` → the three fail (`ccache_env` not defined).
- [ ] **Step 3:** Pin the newest conda-forge `ccache` (`pixi search ccache`) with `==` in `[dependencies]`, `pixi install` to refresh the lock; implement `ccache_env()` and the `configure()` flag.
- [ ] **Step 4:** `pixi run project_n selftest` passes.
- [ ] **Step 5: Prove sharing and freshness:** `ccache --zero-stats`, `pixi run project_n build`; copy the checkout's tracked files to a second path (`git worktree add` into the scratchpad is fine for this check only), build there, `ccache --show-stats` → most compilations are hits. Then edit a comment in `src/pch.hpp` and a value in a header both checkouts include, build the second → those units recompile and the binary reflects the value change. If PCH users never hit, record the ccache/XMake reason under tools/AGENTS.md Gotchas and keep the cache for the rest.
- [ ] **Step 6:** `pixi run project_n check` and `pixi run project_n check --container` pass (from the main checkout for the latter).
- [ ] **Step 7: Commit** (two): `build(env): pin ccache` (pixi.toml, pixi.lock); `build(tools): share a compiler cache between native checkouts` (project_n.py, tests, tools/AGENTS.md).

### Task 5: Spike: can a reflinked `build/` be trusted? (throwaway)

No commit. Decides whether Task 6 includes the snapshot.

- [ ] **Step 1:** In a scratch worktree of the same commit: `cp -a --reflink=always <main>/build <worktree>/build`, then `pixi run project_n build`. Record whether XMake rebuilds nothing, some or all, and how long it takes.
- [ ] **Step 2:** Change one value in a `src/` header in the worktree only, build, run the affected test → the change is visible. Then check `build/` for absolute paths into the main checkout: `grep -rl "<main checkout path>" <worktree>/build/linux-clang | head` (depfiles, `.xmake` config). Any object file or depfile pointing at the main checkout that XMake treats as up to date = untrusted.
- [ ] **Step 3: Decide and write it in the plan's progress ledger:** trusted → Task 6 includes `snapshot_build`; untrusted → Task 6 drops it and tells `worktree setup` users ccache covers the cold build. Delete the scratch worktree.

### Task 6: `./dev worktree setup|clean`

**Files:**
- Modify: `tools/project_n.py`, `.gitignore`, `tools/AGENTS.md` (Commands), `AGENTS.md` (Commands line)
- Test: `tools/test_project_n.py`

**Interfaces:**
- Consumes: `cmd_setup(args)`, `capture`, `run`, `Fail`, `ROOT`, `HOST`.
- Produces:
  - `main_checkout() -> Path`: parent of `git rev-parse --path-format=absolute --git-common-dir`.
  - `snapshot_build(src: Path, dst: Path) -> bool` (only if Task 5 said trusted): `False` without copying when `HOST != "linux"`, `dst` exists, or `cp -a --reflink=always` fails (removing any partial `dst`); `True` after a copy.
  - `owned_worktrees(porcelain: str, main: Path) -> list[tuple[Path, str]]`: (path, branch) of `git worktree list --porcelain` entries under `main/.worktrees/` or `main/.claude/worktrees/`.
  - `removable(path: Path, branch: str) -> str | None`: `None` when clean to remove, else the reason: uncommitted changes (`git -C path status --porcelain` non-empty); branch still on `origin` (`git ls-remote --heads origin <branch>` non-empty = PR not merged yet); commits not in `origin/main` by content (`git cherry origin/main <branch>` has a `+` line).
  - `cmd_worktree(args)`: `setup` → `cmd_setup`, then `snapshot_build(main_checkout()/"build", ROOT/"build")` (print which happened); `clean [path]` → for each owned worktree (or the one given), `removable` → print the reason and skip, else `git worktree remove <path>` (no `--force`) and `git branch -D <branch>`; finally `git worktree prune`.

- [ ] **Step 1: Write the failing tests** in `class WorktreeTest`: `test_owned_only_under_project_dirs` (porcelain with the main checkout, `.worktrees/a`, `.claude/worktrees/b` and `/tmp/c` → only a and b); `test_refuses_uncommitted` / `test_refuses_branch_still_on_origin` / `test_refuses_unmerged_commits` (each mocked git answer → its reason string; `run` never called with `worktree remove`); `test_removes_merged` (all clean → `run` called with `["git", "worktree", "remove", path]` and no `--force` anywhere); and, if the snapshot stays, `test_snapshot_skipped_off_linux` (`HOST="windows"` → `False`, no `cp` call) and `test_snapshot_cleans_partial_copy` (`cp` raises → `False`, `dst` removed).
- [ ] **Step 2:** `pixi run project_n selftest` → the new tests fail.
- [ ] **Step 3:** Implement the functions above and register `worktree` in `make_parser()` with a positional `action` (`choices=["setup", "clean"]`) and optional `path`; help: "set up a new worktree (LFS, a copy-on-write head start for build/) or remove merged ones this project made". Add it to the macOS no-XMake list in `main()`. `.gitignore` gains `.worktrees/` and `.claude/worktrees/` under "Local environments".
- [ ] **Step 4:** `pixi run project_n selftest` passes; `pixi run project_n worktree --help` shows `[possible values: setup, clean]`.
- [ ] **Step 5: Try it for real:** create a worktree with Claude Code's `EnterWorktree`, run `pixi run project_n worktree setup` in it, then `pixi run project_n test` → passes. Check the LSP: `pgrep -af clangd` → which `.pixi` it runs, and an `LSP findReferences` on a symbol answers with paths inside the worktree. If it points at the main checkout, add a Gotcha line under tools/AGENTS.md ("restart Claude Code inside the worktree for LSP") rather than changing the plugin.
- [ ] **Step 6:** Docs: tools/AGENTS.md Commands gains the `worktree` line; root AGENTS.md Commands gains `` `worktree setup|clean` `` beside `setup`. `pixi run project_n doctor --docs` passes.
- [ ] **Step 7: Commit:** `build(tools): add worktree setup and clean`.

### Task 7: User memory (outside the repo, no commit)

**Files** in `~/.claude/projects/-home-lotus-Development-project-n/memory/`: `plan-before-building.md`, `cleanup-as-you-go.md`, `handover-after-each-task.md`, `sweep-up-backlog.md`, `MEMORY.md`.

- [ ] **Step 1:** `plan-before-building.md`: the rule becomes "superpowers brainstorming → spec in `docs/superpowers/specs/` → writing-plans, approved before code"; keep its Why.
- [ ] **Step 2:** Delete `cleanup-as-you-go.md` and its `MEMORY.md` line.
- [ ] **Step 3:** `handover-after-each-task.md` and `sweep-up-backlog.md`: replace any `branch-reviewer`, `review-branch`, design-doc-numbering or `shortcut:` wording with the superpowers equivalent; leave the rest.
- [ ] **Step 4: Verify:** `grep -rn "branch-reviewer\|review-branch\|shortcut:\|sub-doc\|Tidied on the way" ~/.claude/projects/-home-lotus-Development-project-n/memory/` prints nothing.

### Task 8: Finish the branch

- [ ] **Step 1:** `pixi run project_n lint --fix`; fix what remains by hand.
- [ ] **Step 2:** `pixi run project_n check` passes; `pixi run project_n check --container` passes from the main checkout (Task 4 changed the environment).
- [ ] **Step 3:** superpowers requesting-code-review on `origin/main..HEAD` with the spec as requirements; act on it with receiving-code-review; re-run `check` after fixes.
- [ ] **Step 4:** Push, `pixi run gh pr create --fill`, wait for CI green, report the PR URL to the user and stop: the user merges. Then (after the merge) Finishing a branch step 5.
