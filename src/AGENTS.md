# src/: game and engine

## Scope
All game and engine code, structured by docs/design/0002-engine-architecture.md (being implemented, phase 1 of §16). Modules are XMake static libraries in `src/<module>/` (`core`, `platform` so far); each links and includes only the modules below it (`MODULES` in tools/project_n.py; `lint` checks the includes), and `game` links only `core`.

## Rules
- C++23 with conventional headers (no modules). Every translation unit's includes lead to `core/cxx23.hpp`, which fails the build below C++23; include it with `// IWYU pragma: keep` (it declares nothing).
- Include order: project headers, then library headers (`<SDL3/...>`), then (headers only) the standard library, as blank-line-separated blocks. clang-format sorts within a block but doesn't regroup.
- Formatting and lint come from the pinned tools: `pixi run project_n fmt` and `pixi run project_n lint` (warnings are errors).
- Headers include only the standard library and our own headers. Third-party and platform code (SDL, Tracy, `<windows.h>`) stays in `.cpp` files behind our own types (a pimpl or an opaque handle); a header doesn't forward-declare its types either. `lint` fails on a third-party include in a header; only a facade (`FACADES` in tools/project_n.py) includes its own library, and a module's private `src/<module>/internal/` headers its libraries (nothing outside the module includes them).
- Numbers use the sized types from `core/types.hpp`: `i8`–`i64`, `u8`–`u64`, `f32`, `f64`, never `int`, `unsigned`, `long`, `float`, `double` or `std::uint32_t`-style names (`int main(int, char**)` excepted; `std::size_t` stays for sizes and indices; `lint` fails on the others). `pch.hpp` includes it; headers include it themselves.
- `pch.hpp` is the game's precompiled header and holds every standard header the sources use: a `.cpp` never includes a standard header itself (`lint` fails on one); add it to `pch.hpp`. Headers still include everything they use. It is a header too: no third-party ones.
- Logging: extensive, through `core/log.hpp` (Quill; `std::format` syntax, written on a background thread). `PN_LOG_DEBUG` detail for diagnosing (decisions, state changes, values; log file only) · `PN_LOG_INFO` progress in a normal run (subsystems starting and stopping, what was chosen) · `PN_LOG_NOTICE` significant normal events (start, stop) · `PN_LOG_WARNING` unexpected but recovered · `PN_LOG_ERROR` a failure and its cause · `PN_LOG_CRITICAL` a failure the program can't continue after. Log outcomes, never code steps or internals, and never every frame (profile instead). `main` calls `core::log::Start(directory)` first; logs go to the console (Info and up) and to a rotating `project_n.log` in the per-user data folder. Nothing else writes output (`std::print`, iostreams, `printf`, `SDL_Log`) and nothing else includes Quill or Tracy: `lint` fails on them.
- Profiling: `PN_PROFILE_ZONE();` at the top of every function that runs each frame, `PN_PROFILE_FRAME();` once per frame, from `core/profile.hpp` (Tracy). They collect only in `releasedbg` and do nothing elsewhere: `./dev run --mode releasedbg`, then `./dev profiler`.
- Every header starts with a `/// @file` comment followed by `/// @ingroup <module>` (the group is in `src/<module>/module.dox`), and every public class, function, member, variable and enum it declares has a Doxygen comment (`///`): what it does and promises, not how. `lint` fails on a missing or broken one.
- Errors: an expected failure returns `std::expected<T, core::Error>` (`core/error.hpp`); a broken invariant is `PN_ASSERT(condition, message)` (`core/assert.hpp`: on in debug and releasedbg). Our code throws nothing.
- Comments in `.cpp` files only explain why, or a complexity the code can't show; never restate what the code does.
- Unity builds are configured but off (`unity_build` in xmake.lua); `check` builds and tests the native preset with them once to keep them working. They put several sources in one translation unit, so keep names in anonymous namespaces unique across files (test files stay out of the batches: doctest's names would clash).
- Third-party code comes from xmake-repo packages with exact versions; nothing is vendored into `src/`.
- A new module adds its directory's `module.dox`, an XMake target with `add_deps` on the modules below it, and its commit scope to `SCOPES` in tools/project_n.py.

## Commands
- `./dev run [--debug]` (native preset; `--preset` picks another)

## Gotchas
- A `///` comment right after the `/// @file` block joins it, leaving the next declaration undocumented: put a blank line after the file comment.
- SDL 3 silently falls back to XWayland when the compositor lacks `fifo-v1` (niri does), even with the Wayland driver compiled in. `main.cpp` hints `wayland,x11`; `SDL_VIDEO_DRIVER` still overrides it.
- A member whose type's default constructor is defaulted (`std::unordered_map`, EnTT's registry, `core::Mutex`): GCC's `-Weffc++` demands an initializer, clang-tidy calls `{}` redundant. Keep `{}` with `// NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer`, or use a value tidy accepts (`= std::nullopt`).

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|
