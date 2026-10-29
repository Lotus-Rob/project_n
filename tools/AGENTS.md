# tools/: build tooling

## Scope
`project_n.py` (the entry point, run as `pixi run project_n`), its tests, the Pixi activation script and the gdb init file.

## Rules
- `project_n.py` is one file, standard library only, and thin: commands call XMake, Podman, git or git-cliff; anything those tools do stays there.
- `PRESETS`, `IMAGES` and `SCOPES` at the top of the file are the single source of truth. Changing a preset or scope updates design doc §3/§11 and any AGENTS.md that names it.
- Containers re-enter the same script (`reenter`): add behaviour once, not per environment.
- Never pipe XMake's output through filters; it stalled twice that way in spike S4.
- `env/activate.sh` runs on every `pixi run`: keep it fast and idempotent.

## Commands
- `pixi run python -m unittest discover -s tools` runs the self-checks (no Podman needed).

## Skills
- git-commit (for `SCOPES` changes)

## Gotchas
- XMake's package cache ignores the compiler version: `env/activate.sh` gives each `pixi.toml` + `pixi.lock` its own `XMAKE_PKG_INSTALLDIR`. `project_n clean --packages` removes old ones.
- The env's gdb reads `$CONDA_PREFIX/etc/gdbinit`, which the gdb package already uses; `env/activate.sh` appends a single `source` line for `debug/gdbinit` rather than replacing the file.

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|
