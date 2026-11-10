# project_n: agent guide

C++23 game (headers, no modules) built with XMake; tests use doctest.
Every tool comes from the Pixi environment (`pixi.toml` / `pixi.lock`). Never use host compilers or host libraries.
Containers (Podman) are only for artefacts and container checks. Design and rationale: `docs/design/0001-repository-build-environment.md`.

## Map
- `src/` game and engine (src/AGENTS.md) · `tests/` doctest suites (tests/AGENTS.md)
- `tools/project_n.py` the only entry point (tools/AGENTS.md) · `containers/` artefact images (containers/AGENTS.md)
- `shaders/` HLSL, one file per stage; `bin/`: the committed binaries `shaders` writes · `assets/` what the game loads at runtime (`sprites/`: images, with `PROVENANCE.md`; `data/`: YAML data tables, which `game::BuiltInData()` mirrors; `manifest.yaml`: the atlas, which `atlas` writes) · `art/` `sprites.toml` (sprite list, foot points), `scenes.toml` (each scene's sprites), `placeholders.py` (procedural art) · `src/*/generated/`: headers `shaders` and `atlas` write (fmt and lint skip them) · `xmake/repo/` local fixes to xmake-repo packages · `docs/design/` past designs and their rationale: read before redesigning anything · `docs/superpowers/`: `specs/` (designs, `YYYY-MM-DD-<topic>-design.md`) and `plans/` (`YYYY-MM-DD-<feature>.md`), each approved before its code
- `.github/` CI workflows (.github/AGENTS.md)
- `.claude/settings.json` turns superpowers on and ponytail off, and swaps the global clangd plugin for `.claude/marketplace/`'s, which starts Pixi's clangd through `pixi run` on every host; the `LSP` tool reads `build/compile_commands.json`, which native builds refresh

## Commands
- `./dev <command>` (Windows `dev`) is shorthand for `pixi run project_n <command>`; either works from anywhere.
- `pixi run project_n setup`: once per clone (commit hooks, Git LFS's filters through Pixi, LFS files; design doc 0001.1) · `pixi run project_n doctor [--docs]` · `build|test|run [--preset linux-gcc|linux-clang|windows-msvc|macos-clang] [--mode debug|release|releasedbg]` (releasedbg: profiling with Tracy; default preset: the host's native one, `NATIVE_PRESET`; `build --preset all` builds every preset and summarises)
- `pixi run project_n check`: the merge gate (self-tests, commits, format and lint of the diff, build + test every preset, one releasedbg build). A change is done when it passes.
- `pixi run project_n shaders` (Linux or Windows): compile `shaders/` after changing it, and commit `shaders/bin/` and `src/gfx/generated/` with it; `lint` fails on stale binaries
- `pixi run project_n atlas [--placeholders]`: after changing `art/sprites.toml`, `art/scenes.toml` or `assets/sprites/`, commit `assets/manifest.yaml`, `src/core/generated/atlas.hpp` and `art/atlas.sha256` with them; `lint` fails on a stale atlas
- `pixi run project_n reference [--preset linux-gcc|linux-clang] [--update]`: pictures of fixed scenes against `tests/reference/` on software Vulkan (Linux; part of `check` and `artifact`) → tests/AGENTS.md
- `pixi run project_n coverage` (HTML in build/coverage/) · `fuzz <target> [--time S]` · `docs` (Doxygen, build/docs/) · `profile [--seconds N] [-- <game args>]` (records a releasedbg run with Tracy into build/profiles/; nothing takes the game's focus) · `profiler [file]` (Tracy viewer, live or on a saved capture)
- `pixi run project_n check --sanitizers`: also every supported sanitizer; on request only, not part of the gate.
- `pixi run project_n check --container`: the same in clean containers. Run it for any build, environment or dependency change: only a clean container catches host leaks.

## Working with superpowers
Superpowers runs the process (brainstorming, spec, plan, test-first work, verification, review, finishing). These lines are where it meets this repo, and they win over its skills:
- Worktrees go inside the repo (Claude Code's `.claude/worktrees/` or `.worktrees/`), never outside it: Git LFS's filter needs Pixi to find `pixi.toml` further up during the checkout. Project setup there is `pixi run project_n setup`, the baseline `pixi run project_n test`; run XMake only through `project_n`.
- Verification after each task: `pixi run project_n test` and `pixi run project_n lint` (native preset); before a PR: `pixi run project_n check`. Containers can't see a worktree's shared `.git`, so container gates (`check --container`, `lint --container`) run only from a main checkout; from a worktree, CI's `lint` and `container` jobs on the PR are that gate, and the PR waits for them.
- Code review: the reviewer also checks the diff against the AGENTS.md of every directory it touches, and finds callers of changed C++ symbols with the `LSP` tool (`findReferences`, `incomingCalls`) rather than grep, which misses overloads and templates.
- Rationale outlives the ledger: superpowers' plan ledger (`.superpowers/sdd/`) is gitignored and deleted when the work is done, so before the PR copy its rulings, with the measurements behind them, into an **Outcome** section of the spec, and mark the spec implemented.
- Finishing a branch: never "merge locally"; always the pull request below.

## Finishing a branch
Work happens on a branch; it merges into main only through a pull request, which the user merges.
1. `pixi run project_n lint --fix` (it covers the diff; `--all` for every file); fix every remaining finding by hand in the code. The pre-commit hook checks formatting at every commit: when it rejects one, `pixi run project_n fmt`, stage again and commit.
2. `pixi run project_n check` (plus `--container` for build, environment or dependency changes, or CI's container jobs from a worktree: above) until it passes.
3. Review: superpowers requesting-code-review on `main..HEAD`, acted on with receiving-code-review; `check` again after the fixes.
4. Main accepts nothing but a pull request, the user included (ruleset: .github/AGENTS.md). Push the branch and `pixi run gh pr create --fill`; when CI's six checks are green on a branch up to date with main, stop and tell the user: the user merges (GitHub deletes the merged branch).
5. After the merge, from the main checkout: `git switch main && git pull --ff-only`, then `pixi run project_n prune-worktrees` (removes this project's worktrees whose work is in origin/main; never forced).

## Rules
- Fix lint and warnings properly. Suppress only as the last resort, when no proper fix exists, and as narrowly as possible with the reason: `// NOLINT(check-name): why` (`lint` rejects bare ones), a commented pragma, or a nested `.clang-tidy` with a `#` reason. The root `.clang-tidy` and `.clangd` are the user's: never remove, disable or relax a check or option in them. `cppcoreguidelines-*` enforces the C++ Core Guidelines.
- Commits: Conventional Commits; `feat`/`fix` only for game code; scopes from `SCOPES` in tools/project_n.py → git-commit skill
- History is linear: rebase, never merge commits.
- Bugs in `src/` are fixed test-first: a test that fails because of the bug (what makes it good: tests/AGENTS.md), committed with the fix; a fuzzer crash also adds its input as a seed in `tests/fuzz/corpus/<target>/`.
- Never delete, skip or loosen a test case to make a run go green.
- Pins are exact (`==` in pixi.toml, exact `add_requires` versions). Change one only on purpose, with its lock file in the same commit.
- Don't call podman, xmake or git-cliff directly in docs or scripts; go through project_n.py.
- Only ever stop or remove containers this project started (`project_n-*` images), never all containers.
- Missing host tool: run `doctor` and give the user its install instructions. Never install system packages yourself.
- Agent docs: an AGENTS.md holds what any task in its directory needs (~100 lines, one line per rule) and has a sibling `CLAUDE.md` holding only `@AGENTS.md` (the root's is `.claude/CLAUDE.md` with `@../AGENTS.md`); a skill holds how to do one job (~200 lines) and names the AGENTS.md that owns it, which lists it under Skills. Commands quoted in them must exist (`doctor --docs`). Write and change skills with superpowers writing-skills.

## Skills (.claude/skills)
- git-commit: writing commit messages, splitting changes, breaking changes
- release: cutting a release, versions, changelog, artefacts
- add-dependency: adding, bumping or removing a C++ library (`add_requires`, licence record, lock)
- fix-xmake-package: a package that needs host files or drops a feature; overrides in `xmake/repo/` and their upstream fixes
- bump-toolchain: changing a pinned tool, toolchain, base image or MSVC version (containers/AGENTS.md)
- look-at-the-game: seeing what the game draws, screenshots and picture diffs (src/AGENTS.md)
- handover: writing a handover for the next session, and when to stop for one

## Agents (.claude/agents): subagents with a fresh context
- log-triage: the root cause of a long build, test or CI log; hand it logs instead of reading them in the main session

## Keeping this true
Update AGENTS.md files and skills in the same commit as the change that affects them. Log non-obvious fixes under Gotchas.
Propose a skill when a workflow repeats (superpowers writing-skills): add it to Skill candidates and ask the user first.

## Skill candidates
| Workflow | Evidence | Proposed skill | Owner |
|---|---|---|---|

## Gotchas
- GCC release (LTO) links failing with undefined references into a module library: our libraries need `gcc-ar`, which indexes LTO bytecode. xmake.lua's `lto` rule sets it per target, never in the toolchain: XMake hands autotools packages no `ARFLAGS` for `gcc-ar`, so their static archives fail (`ar: invalid option -- '.'`).
