# src/: game and engine

## Scope
All game and engine code. Its structure comes from the architecture design doc (not written yet); until then, `main.cpp` is a minimal SDL3 window.

## Rules
- C++23 with conventional headers (no modules). Every translation unit's includes lead to `core/cxx23.hpp`, which fails the build below C++23; include it with `// IWYU pragma: keep` (it declares nothing).
- Include order: project headers, then library headers (`<SDL3/...>`), then the standard library, as blank-line-separated blocks. clang-format sorts within a block but doesn't regroup.
- Formatting and lint come from the pinned tools: `pixi run project_n fmt` and `pixi run project_n lint` (warnings are errors).
- Headers include only the standard library and our own headers. Third-party and platform code (SDL, Tracy, `<windows.h>`) stays in `.cpp` files behind our own types (a pimpl or an opaque handle); a header doesn't forward-declare its types either. `lint` fails on a third-party include in a header; only a facade (`FACADES` in tools/project_n.py) includes its own library.
- `pch.hpp` is the game's precompiled header: add standard headers that are slow to parse and used widely (it is a header too: no third-party ones). Sources still include what they use (lint, and builds without the PCH, rely on it).
- Logging: extensive, through `core/log.hpp` (Quill; `std::format` syntax, written on a background thread). `PN_LOG_DEBUG` detail for diagnosing (decisions, state changes, values; log file only) · `PN_LOG_INFO` progress in a normal run (subsystems starting and stopping, what was chosen) · `PN_LOG_NOTICE` significant normal events (start, stop) · `PN_LOG_WARNING` unexpected but recovered · `PN_LOG_ERROR` a failure and its cause · `PN_LOG_CRITICAL` a failure the program can't continue after. Log outcomes, never code steps or internals, and never every frame (profile instead). `main` calls `core::log::Start(directory)` first; logs go to the console (Info and up) and to a rotating `project_n.log` in the per-user data folder. Nothing else writes output (`std::print`, iostreams, `printf`, `SDL_Log`) and nothing else includes Quill or Tracy: `lint` fails on them.
- Profiling: `PN_PROFILE_ZONE();` at the top of every function that runs each frame, `PN_PROFILE_FRAME();` once per frame, from `core/profile.hpp` (Tracy). They collect only in `releasedbg` and do nothing elsewhere: `./dev run --mode releasedbg`, then `./dev profiler`.
- Every header starts with a `/// @file` comment, and every public class, function, member, variable and enum it declares has a Doxygen comment (`///`): what it does and promises, not how. `lint` fails on a missing or broken one.
- Comments in `.cpp` files only explain why, or a complexity the code can't show; never restate what the code does.
- Unity builds are configured but off (`unity_build` in xmake.lua); `check` builds and tests the native preset with them once to keep them working. They put several sources in one translation unit, so keep names in anonymous namespaces unique across files.
- Third-party code comes from xmake-repo packages with exact versions; nothing is vendored into `src/`.
- New subsystems add their commit scope to `SCOPES` in tools/project_n.py.

## Commands
- `./dev run [--debug]` (native preset; `--preset` picks another)

## Gotchas
- A `///` comment right after the `/// @file` block joins it, leaving the next declaration undocumented: put a blank line after the file comment.
- SDL 3 silently falls back to XWayland when the compositor lacks `fifo-v1` (niri does), even with the Wayland driver compiled in. `main.cpp` hints `wayland,x11`; `SDL_VIDEO_DRIVER` still overrides it.

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|
