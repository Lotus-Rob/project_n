# project_n: agent guide

C++23 game (headers, no modules) built with XMake; tests use doctest.
Every tool comes from the Pixi environment (`pixi.toml` / `pixi.lock`). Never use host compilers or host libraries.
Containers (Podman) are only for artefacts and container checks. Design and rationale: `docs/design/0001-repository-build-environment.md`.

## Map
- `src/` game and engine (src/AGENTS.md) · `tests/` doctest suites (tests/AGENTS.md)
- `tools/project_n.py` the only entry point (tools/AGENTS.md) · `containers/` artefact images (containers/AGENTS.md)
- `xmake/repo/` local fixes to xmake-repo packages · `docs/design/` rationale: read before redesigning anything
- `.github/` CI workflows (.github/AGENTS.md)

## Commands
- `./dev <command>` (Windows `dev`) is shorthand for `pixi run project_n <command>`; either works from anywhere.
- `pixi run project_n doctor [--docs]` · `build|test|run [--preset linux-gcc|linux-clang|windows-msvc|macos-clang] [--mode debug|release|releasedbg]` (releasedbg: profiling with Tracy; default preset: the host's native one, `NATIVE_PRESET`; `build --preset all` builds every preset and summarises)
- `pixi run project_n check`: the merge gate (self-tests, commits, format and lint of the diff, build + test every preset). A change is done when it passes.
- `pixi run project_n coverage` (HTML in build/coverage/) · `fuzz <target> [--time S]` · `docs` (Doxygen, build/docs/) · `profiler` (Tracy viewer for a `run --mode releasedbg` build)
- `pixi run project_n check --sanitizers`: also every supported sanitizer; on request only, not part of the gate.
- `pixi run project_n check --container`: the same in clean containers. Run it for any build, environment or dependency change: only a clean container catches host leaks.

## Finishing a branch
Work happens on a branch; it merges into main only through a pull request, after step 3.
1. `pixi run project_n lint --fix` (it covers the diff; `--all` for every file); fix every remaining finding by hand in the code. The pre-commit hook checks formatting at every commit: when it rejects one, `pixi run project_n fmt`, stage again and commit.
2. `pixi run project_n check` (plus `--container` for build, environment or dependency changes) until it passes.
3. Review: a fresh `branch-reviewer` subagent reviews the branch. Fix every finding (none is non-blocking), commit, and review again with a new subagent until it ends `REVIEW: CLEAN`, at most 5 rounds; after round 5's fixes the user decides on the merge → review-branch skill
4. Merge through a pull request; main accepts nothing else, the user included (ruleset: .github/AGENTS.md). Push the branch and `pixi run gh pr create --fill`; when CI's six checks are green on a branch up to date with main, `pixi run gh pr merge --rebase` (GitHub deletes the merged branch). Then `git switch main && git pull --ff-only`.

## Rules
- Fix lint and warnings properly. Suppress only as the last resort, when no proper fix exists, and as narrowly as possible with the reason: `// NOLINT(check-name): why` (`lint` rejects bare ones), a commented pragma, or a nested `.clang-tidy` with a `#` reason. The root `.clang-tidy` and `.clangd` are the user's: never remove, disable or relax a check or option in them. `cppcoreguidelines-*` enforces the C++ Core Guidelines.
- Commits: Conventional Commits; `feat`/`fix` only for game code; scopes from `SCOPES` in tools/project_n.py → git-commit skill
- History is linear: rebase, never merge commits.
- Bugs in `src/` are fixed test-first: a small test that fails because of the bug, committed with the fix → fix-bug skill
- Never delete, skip or loosen a test case to make a run go green.
- Pins are exact (`==` in pixi.toml, exact `add_requires` versions). Change one only on purpose, with its lock file in the same commit.
- Don't call podman, xmake or git-cliff directly in docs or scripts; go through project_n.py.
- Only ever stop or remove containers this project started (`project_n-*` images), never all containers.
- Missing host tool: run `doctor` and give the user its install instructions. Never install system packages yourself.

## Skills (.claude/skills)
- git-commit: writing commit messages, splitting changes, breaking changes
- agent-docs: editing AGENTS.md or skills, proposing new skills
- release: cutting a release, versions, changelog, artefacts
- add-dependency: adding, bumping or removing a C++ library (`add_requires`, licence record, lock)
- fix-xmake-package: a package that needs host files or drops a feature; overrides in `xmake/repo/` and their upstream fixes
- fix-bug: fixing any bug in `src/` test-first
- review-branch: the pre-merge review and acting on its findings

## Agents (.claude/agents): subagents with a fresh context
- branch-reviewer: the pre-merge reviewer, a new one per round (review-branch skill)
- log-triage: the root cause of a long build, test or CI log; hand it logs instead of reading them in the main session
- bug-reproducer: writes the failing test for a `src/` bug from its symptoms, before the fix (fix-bug skill)

## Keeping this true
Update AGENTS.md files and skills in the same commit as the change that affects them. Log non-obvious fixes under Gotchas.
Propose a skill when a workflow repeats → agent-docs skill

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|

## Gotchas
- GCC release (LTO) links failing with undefined references into `core` or `platform`: the module libraries need `gcc-ar`, which indexes LTO bytecode (the `pixi-gcc` toolchain's `ar` in xmake.lua).
- A build that passes natively can still fail in the container: hidden host files (headers, `/usr/bin/ar`, pkg-config paths) fill gaps on a dev machine. Trust `check --container`.
