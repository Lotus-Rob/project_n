# src/: game and engine

## Scope
All game and engine code. Its structure comes from the architecture design doc (not written yet); until then, `main.cpp` is a minimal SDL3 window.

## Rules
- C++23 with conventional headers (no modules). Every translation unit's includes lead to `core/cxx23.hpp`, which fails the build below C++23; include it with `// IWYU pragma: keep` (it declares nothing).
- Include order: project headers, then library headers (`<SDL3/...>`), then the standard library, as blank-line-separated blocks. clang-format sorts within a block but doesn't regroup.
- Formatting and lint come from the pinned tools: `pixi run project_n fmt` and `pixi run project_n lint` (warnings are errors).
- Third-party code comes from xmake-repo packages with exact versions; nothing is vendored into `src/`.
- New subsystems add their commit scope to `SCOPES` in tools/project_n.py.

## Commands
- `pixi run project_n run --preset linux-clang [--debug]`

## Gotchas
- SDL 3 silently falls back to XWayland when the compositor lacks `fifo-v1` (niri does), even with the Wayland driver compiled in. `main.cpp` hints `wayland,x11`; `SDL_VIDEO_DRIVER` still overrides it.

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|
