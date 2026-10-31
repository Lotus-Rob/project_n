# src/: game and engine

## Scope
All game and engine code. Its structure comes from the architecture design doc (not written yet); until then, `main.cpp` is a minimal SDL3 window.

## Rules
- C++23 with conventional headers (no modules). Every translation unit's includes lead to `core/cxx23.hpp`, which fails the build below C++23; include it with `// IWYU pragma: keep` (it declares nothing).
- Include order: project headers, then library headers (`<SDL3/...>`), then the standard library, as blank-line-separated blocks. clang-format sorts within a block but doesn't regroup.
- Formatting and lint come from the pinned tools: `pixi run project_n fmt` and `pixi run project_n lint` (warnings are errors).
- `pch.hpp` is the game's precompiled header: add headers that are slow to parse and used widely. Sources still include what they use (lint, and builds without the PCH, rely on it).
- Profiling: Tracy's macros from `<tracy/Tracy.hpp>` (`ZoneScoped`, `FrameMark`). They collect only in `releasedbg` and compile to nothing elsewhere: `./dev run --mode releasedbg`, then `./dev profiler`.
- Third-party code comes from xmake-repo packages with exact versions; nothing is vendored into `src/`.
- New subsystems add their commit scope to `SCOPES` in tools/project_n.py.

## Commands
- `./dev run [--debug]` (native preset; `--preset` picks another)

## Gotchas
- SDL 3 silently falls back to XWayland when the compositor lacks `fifo-v1` (niri does), even with the Wayland driver compiled in. `main.cpp` hints `wayland,x11`; `SDL_VIDEO_DRIVER` still overrides it.

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|
