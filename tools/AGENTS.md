# tools/: build tooling

## Scope
`project_n.py` (the entry point, run as `pixi run project_n` or the root `dev`/`dev.cmd` wrappers), its tests, the Pixi activation script and the gdb init file.

## Rules
- `project_n.py` is one file, standard library only, and thin: commands call XMake, Podman, git or git-cliff; anything those tools do stays there.
- `dev` and `dev.cmd` only forward to `pixi run project_n`; behaviour belongs in project_n.py.
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
- `xmake f` runs with `-v` on purpose: it serialises package installs. In parallel, XMake installs duplicate variants with one build hash (libxcb, libx11 via SDL3) into the same directory and they corrupt each other ("install: ... No such file or directory").
- The env's gdb reads `$CONDA_PREFIX/etc/gdbinit`, which the gdb package already uses; `env/activate.sh` appends a single `source` line for `debug/gdbinit` rather than replacing the file.
- XMake sometimes rewrites `xmake-requires.lock` with only the `__meta__` block moved (Lua table order). The content is unchanged: `git checkout xmake-requires.lock`; commit the lock only when versions or hashes change.

- On Windows, `subprocess` decodes output with the locale (cp1252) and crashes on XMake's UTF-8; `capture()` forces UTF-8. Running a program by bare name finds only `.exe` files (conda's xmake is `xmake.cmd`); `run()`/`capture()` resolve it through `shutil.which`.
- `doctor` exits 3 for "problems found", so CI can report them without hiding real errors (exit 1).

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|
