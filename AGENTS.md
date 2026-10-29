# project_n: agent guide

C++23 game (headers, no modules) built with XMake; tests use doctest.
Every tool comes from the Pixi environment (`pixi.toml` / `pixi.lock`). Never use host compilers or host libraries.
Containers (Podman) are only for artefacts and container checks. Design and rationale: `docs/design/0001-repository-build-environment.md`.

## Map
- `src/` game and engine (src/AGENTS.md) · `tests/` doctest suites (tests/AGENTS.md)
- `tools/project_n.py` the only entry point (tools/AGENTS.md) · `containers/` artefact images (containers/AGENTS.md)
- `xmake/repo/` local fixes to xmake-repo packages · `docs/design/` rationale: read before redesigning anything

## Commands
- `pixi run project_n doctor` · `build|test|run --preset linux-gcc|linux-clang|windows-msvc [--mode debug|release]`
- `pixi run project_n check`: the merge gate (commits, fmt, lint, build + test every preset). A change is done when it passes.
- `pixi run project_n check --container`: the same in clean containers. Run it for any build, environment or dependency change: only a clean container catches host leaks.

## Rules
- Commits: Conventional Commits; `feat`/`fix` only for game code; scopes from `SCOPES` in tools/project_n.py → git-commit skill
- History is linear: rebase, never merge commits.
- Pins are exact (`==` in pixi.toml, exact `add_requires` versions). Change one only on purpose, with its lock file in the same commit.
- Don't call podman, xmake or git-cliff directly in docs or scripts; go through project_n.py.
- Only ever stop or remove containers this project started (`project_n-*` images), never all containers.
- Missing host tool: run `doctor` and give the user its install instructions. Never install system packages yourself.

## Skills (.claude/skills)
- git-commit: writing commit messages, splitting changes, breaking changes
- agent-docs: editing AGENTS.md or skills, proposing new skills

## Keeping this true
Update AGENTS.md files and skills in the same commit as the change that affects them. Log non-obvious fixes under Gotchas.
Propose a skill when a workflow repeats → agent-docs skill

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|
| Fix an xmake-repo package that only builds with host files | Done twice in spike S4 (`xmake/repo/`) | `fix-xmake-package` |

## Gotchas
- A build that passes natively can still fail in the container: hidden host files (headers, `/usr/bin/ar`, pkg-config paths) fill gaps on a dev machine. Trust `check --container`.
