# Switch project_n's agent workflow from ponytail to superpowers

Date: 2026-11-10 · Status: approved 2026-11-10, implemented with the changes in **Outcome** (end of this document)

## Intent

The user installed the superpowers plugin and wants every session in project_n to follow the superpowers workflow:
brainstorming that asks questions and drills deep, a written design, a plan, test-first work, verification before claiming
done, review. Design documents and approvals continue, but in superpowers' form. Ponytail ("smallest change", the `shortcut:`
convention) is no longer how the project works.

Superpowers' own rule is that CLAUDE.md and AGENTS.md take precedence over its skills. So the job is (a) remove the project's
own skills and agents that superpowers replaces, (b) make AGENTS.md state only the project rules superpowers must respect, and
point at superpowers where it now owns the process, and (c) make worktrees, which superpowers uses, affordable in this repo.

## Goals

1. Superpowers is the process layer: brainstorming → spec → writing-plans → execution → review → finishing a branch.
2. AGENTS.md files keep the project's hard rules and say where they meet superpowers' steps; nothing in them contradicts a
   superpowers skill without saying it overrides it.
3. Worktrees are cheap enough to use by default: a shared compiler cache, a copy-on-write head start for `build/`, lean checks
   while working, and cleanup when done.

## Non-goals

- Changing any game code's behaviour.
- Rewriting or moving the existing `docs/design/` documents.
- Changing the merge gate (`check`), CI, the GitHub ruleset or the toolchain pins (other than adding ccache).
- Caching the `windows-msvc` preset (sccache) now: a follow-up only if MSVC proves the slow one.

## Anti-goals (a failure even if it works)

- Two processes fighting: an AGENTS.md rule and a superpowers skill that disagree with neither saying which wins.
- Superpowers' "merge locally" ending up on main: main accepts only rebase-merged PRs.
- A worktree build that silently links stale objects (from the copied `build/` or the cache) and passes.
- Containers using the host's compiler cache, so `check --container` no longer proves a clean build.
- Losing a project rule that only lived in a deleted skill or agent (test quality, logging, Doxygen, header hygiene).

## Constraints

- Every tool comes from Pixi; ccache is pinned exactly in `pixi.toml` with `pixi.lock` in the same commit (add-dependency /
  bump-toolchain conventions). Build-environment changes pass `check --container`.
- Scripts and docs go through `tools/project_n.py`, never `git worktree`, xmake or podman directly in docs.
- Commits are Conventional Commits with scopes from `SCOPES`; history linear; `doctor --docs` checks every quoted command.
- The root `.clang-tidy` and `.clangd` are the user's and stay untouched.
- Only worktrees this project created are ever removed (as with containers).
- Host: btrfs, so `cp --reflink` and Pixi's environment links are copy-on-write. Windows and macOS hosts must still work
  (they may simply not get the reflink head start).

## Decisions (the user's)

1. **Specs and plans** go where superpowers puts them: `docs/superpowers/specs/YYYY-MM-DD-<topic>-design.md` and
   `docs/superpowers/plans/YYYY-MM-DD-<feature>.md`. `docs/design/` stays as the record of past decisions; "read before
   redesigning" remains, the numbered sub-doc rule goes. Phase 6 chunk 3 (`0002.4.2`) finishes under its own doc.
2. **fix-bug skill and bug-reproducer agent are deleted**; bugs go through superpowers systematic-debugging then
   test-driven-development. The rules stay in AGENTS.md: a `src/` bug is fixed with a test that fails without the fix,
   committed together; never delete, skip or loosen a test; a fuzzer crash also adds a corpus seed.
3. **review-branch skill and branch-reviewer agent are deleted**; review is superpowers requesting-code-review /
   receiving-code-review, with its severities (Critical and Important fixed before going on, Minor may be noted) and its
   pushback. This replaces "every finding blocks, at most 3 rounds".
4. **agent-docs skill is deleted**; superpowers writing-skills governs writing and changing skills.
5. **Worktrees follow superpowers defaults**: no standing preference, so each session asks; Claude Code's native
   `EnterWorktree` first, `.worktrees/` as the git fallback.
6. **All four cost reductions**: (1) shared ccache, (2) reflink snapshot of `build/` into a new worktree, (3) lean checks while
   working, full `check` once before the PR, (4) cleanup on merge.
7. **The `shortcut:` convention goes.**
8. **"Clean up as you go" goes**; superpowers' refactor step and review take its place.
9. **This lands first, on a new branch from main, now.** The workflow files made in the session that wrote this spec
   (`.claude/settings.json`, `.claude/marketplace/`, the LSP lines, this spec) move off `phase6-text-chunk3` onto it; chunk 3's
   game code stays where it is and finishes under superpowers, rebased onto main after this merges.
10. **Ponytail is disabled for this project**: `"ponytail@ponytail": false` in `.claude/settings.json` (the global plugin
    stays installed for other projects). `code-review` and `code-simplifier` stay enabled.
11. **Merging follows superpowers**: the agent stops at "PR open, CI green" and the user decides the merge;
    finishing-a-development-branch's "merge locally" option is replaced by the PR, since main accepts nothing else. Review
    rounds follow superpowers' own loop, with no `REVIEW: CLEAN` marker and no project round limit.

## Design

### Docs: what changes where

- **Root AGENTS.md**
  - Map: `docs/design/` becomes "past designs and rationale, read before redesigning"; add `docs/superpowers/` (specs, plans).
  - New short section **Working with superpowers** mapping its steps to project commands: project setup in a worktree =
    `./dev worktree setup`; baseline tests = `./dev test` (native preset); verification before completion and before a PR =
    `check`; finishing a branch = the PR-only rules below (its "merge locally" option is not allowed here).
  - **Finishing a branch** keeps lint/fmt, `check`, PR, CI, rebase merge; step 3 becomes superpowers requesting-code-review
    on the branch, with the reviewer told to check the diff against the AGENTS.md of each directory it touches.
  - Rules: the test-first bug rule loses "→ fix-bug skill"; add "the AGENTS.md files are review criteria"; the rules that
    lived only in agent-docs and are still wanted move here as one-liners (update docs in the same commit, log
    non-obvious fixes under Gotchas, each AGENTS.md has a sibling CLAUDE.md with `@AGENTS.md`, quoted commands must exist).
  - Skills / Agents lists lose agent-docs, fix-bug, review-branch, branch-reviewer, bug-reproducer; "Propose a skill" and
    the Skill candidates table point at writing-skills.
  - Worktree line: when to use `./dev worktree`, lean checks while working, cleanup.
- **tests/AGENTS.md**: gains what made a good bug test (small, contained, explicit, one behaviour; logic only reachable
  through `main.cpp` or SDL is moved into `src/` first; a sanitizer finding fails under that sanitizer).
- **src/AGENTS.md**: unchanged rules, now explicitly review criteria via the root rule (no copy).
- **handover skill**: decisions go into the spec, not a design doc; point at superpowers' plan progress ledger for resuming;
  drop the `shortcut:` line.
- **branch-reviewer's LSP line** (added this session, uncommitted) moves into the Working with superpowers section as advice
  to reviewers: find callers with the `LSP` tool rather than grep.
- **`shortcut:` comments**: the ~20 existing ones lose the tag and stay as plain comments; no new ones. Old design docs that
  mention them are untouched.
- **User memory** (outside the repo): rewrite "Plan before building" to point at brainstorming → spec → writing-plans; remove
  "Clean up as you go"; update "Handover at real stopping points" if its wording no longer fits.

### Worktree tooling: `./dev worktree`

A new `project_n` command (tools/AGENTS.md, `--help`, completion, selftest as the other commands have):

- `./dev worktree setup` (run inside a new worktree; superpowers' "project setup" step): Pixi environment, Git LFS checkout,
  then the reflink head start for `build/` from the main checkout when the filesystem supports it, else nothing.
- `./dev worktree clean [<path>]`: remove a merged worktree (and its `build/`) that this project or Claude Code created
  (`.worktrees/`, `.claude/worktrees/`); refuse one with uncommitted or unpushed work, never `--force`.
- `.gitignore` gains `.worktrees/` and `.claude/worktrees/`.
- The LSP plugin (`.claude/marketplace/`) starts clangd from `${CLAUDE_PROJECT_DIR}`: confirm whether that is the worktree or
  the main checkout when a session moves into a worktree, and make clangd use the worktree's `build/compile_commands.json`.

### Compiler cache

- `ccache` pinned in `pixi.toml` (with `pixi.lock`); XMake uses it on native builds.
- Shared cache directory under `~/.cache` for every checkout; `base_dir` = checkout root and `hash_dir` off so worktrees at
  different paths share hits; PCH sloppiness set so `pch.hpp` users still hit.
- Containers build without it.
- `windows-msvc` may run uncached; measured, not assumed.

### Reflink snapshot: a spike first

Unknown: how XMake treats a copied `build/` whose recorded paths point at the main checkout. A throwaway spike decides:
copy, touch one file, build; it stays only if XMake rebuilds exactly what changed (or everything, which is merely slow) and
never links an object from the other checkout. If it reuses stale objects it is dropped, and ccache does the job alone.

## Calls made by the designer (not the user's; check them)

- **Lean checks, wording only:** superpowers' verification-before-completion after each task means `./dev test` and
  `./dev lint` on the native preset; the full `check` runs once before the PR (and `check --container` when the change needs
  it). No new command or flag.
- **ccache is local only:** on for native builds on developer hosts, off when `CI` is set, so CI is unchanged; containers
  never use it.
- **Container gates run from the main checkout**, not from a worktree: containers mount only the checkout root and can't
  see a worktree's shared `.git`. Documented in the worktree line; no container change.
- **`./dev worktree clean`** decides "merged" by the branch's remote being gone after the PR merged (GitHub deletes it) and
  the work being in `origin/main`, never by SHAs, since a rebase merge rewrites them; it defers to `ExitWorktree` for
  `.claude/worktrees/` when a session is still in one.
- **User memory** also covers `sweep-up-backlog.md`, which still names `branch-reviewer`.
- **`shortcut:` tags in files chunk 3 changes** (`src/game/arena.cpp`, `src/main.cpp`, the untracked
  `src/gfx/text_layout.cpp`) are left for chunk 3 to drop, so the two branches don't conflict; this branch retags the rest.
- **Finishing a branch, step 4** no longer has the agent run `gh pr merge`; after the user merges, `git switch main &&
  git pull --ff-only` runs from the main checkout and `./dev worktree clean` removes the worktree.

## Left to the builder

- Order of work and commit split (docs and tooling are separable; the docs can land first).
- Exact ccache settings, cache location and size limit; how XMake is told to use it.
- Whether the LSP fix for worktrees needs a change to `.claude/marketplace/` or only to `worktree setup`.
- Wording of every AGENTS.md line within the existing size limits.

## Outcome (2026-11-10)

What was built differs from the design where measurement or review showed it should. Each change, and why:

- **No shared compiler cache (decision 6, item 1, dropped).** XMake compiles our sources through its built-in cache, per
  build directory and keyed on paths; it hands only package builds to an installed ccache, so a pinned ccache saw almost
  nothing. Pointing the built-in cache at one shared folder (`--ccachedir`) missed from a second checkout: 10.7 s, then
  11.7 s, the cache growing 139 MB → 277 MB; the same path rebuilt in 2.7 s. The premise was wrong too: a cold build of
  our sources is ~10 s per preset and mode (109 files, linux-clang debug), and the slow part, the packages, is already
  shared through `packages_dir()`. Recorded in tools/AGENTS.md Gotchas.
- **No copy-on-write `build/` snapshot (decision 6, item 2, dropped).** A reflinked copy of the main checkout's `build/`
  (38 GB in 2.2 s) is safe but useless: XMake recompiled all 109 files (10.8 s, the same as cold), since its cache and
  dependency records key on the other checkout's paths; nothing stale was linked.
- **No `worktree setup`.** Without the snapshot it would only repeat `project_n setup`, which works in a worktree;
  AGENTS.md maps superpowers' project setup to `setup`.
- **Cleanup is `project_n prune-worktrees`, not `worktree clean` or `clean --worktrees`.** `clean` always deletes the
  checkout's build directories, so pruning after every merge would wipe the main checkout's builds. It keeps locked,
  detached and dirty worktrees, and ones with no commits of their own yet (just made, never pushed: their untracked
  `build/` and plan ledger would go).
- **Found on the way: XMake built the main checkout from a worktree.** XMake takes an ancestor folder holding a project
  (one with `.xmake/`) over the current directory, so every build, test and lint in `.claude/worktrees/<name>` ran on the
  main checkout's sources. `resolve()` now passes `-P <ROOT>` to every XMake task (`XMAKE_PROJECT_DIR` doesn't work).
- **Worktrees go inside the repo.** One outside it fails to check out: Git LFS's filter runs `pixi run git-lfs` in the
  new folder before its `pixi.toml` exists. Inside, Pixi finds the main checkout's `pixi.toml` further up.
- **Container gates from a worktree are CI's.** Containers mount only the checkout and can't see a worktree's shared
  `.git`; from a worktree, the PR's `lint` and `container` CI jobs (which run `lint --container`, a clean-room `artifact`
  per preset and a releasedbg build) are the container gate.
- **The docs commit isn't marked breaking.** Removing agent skills is not a breaking change as the git-commit skill
  defines it (saves, config, content formats, the build interface), and a `!` would bump the next release.
- **Left to check after a Claude Code restart:** that the repo's clangd plugin starts (and, in a worktree, which
  checkout's clangd and compile database it uses), and that ponytail is really off: a subagent still received its hook
  in the session that built this, likely settings cached at session start.
