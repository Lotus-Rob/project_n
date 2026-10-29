# tests/: unit tests

## Scope
doctest suites, registered in `tests/xmake.lua` and run by `xmake test` on every preset (Windows tests run under Wine in the container).

## Rules
- Layout mirrors `src/`: `tests/unit/<area>/`, one test executable per area (`<area>_tests`) to keep link times down.
- Each executable has exactly one `main.cpp` with `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`; test files include `<doctest/doctest.h>` only.
- A new area adds a target to `tests/xmake.lua` with `add_tests("default")`.
- Sanitizers run on request, never in plain `check`. Supported per preset: `SANITIZERS` in tools/project_n.py (GCC/Clang: address, undefined, thread, leak; MSVC: address). Any finding fails the run.

## Commands
- `pixi run project_n test --preset linux-clang --sanitizer address` (also `run --sanitizer ...` for the game)
- `pixi run project_n check --sanitizers`: every preset under every supported sanitizer
- `pixi run project_n test --preset linux-clang` · one target with doctest arguments:
  `pixi run project_n test --preset linux-clang --target core_tests -- --test-case='*expected*'`

## Gotchas
- MSVC sanitizer builds compile and link in the container, but only *run* on native Windows: MSVC's ASan can't hot-patch Wine's system DLLs.
- Sanitizer builds link the C++ runtime dynamically (static libc++abi collides with TSan) and don't instrument dependencies.

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|
