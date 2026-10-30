# tests/: unit tests

## Scope
doctest suites, registered in `tests/xmake.lua` and run by `xmake test` on every preset (Windows tests run under Wine in the container).

## Rules
- Layout mirrors `src/`: `tests/unit/<area>/`, one test executable per area (`<area>_tests`) to keep link times down.
- Test code passes the same lint as `src/` (no test exemptions): put `TEST_CASE`s inside an anonymous namespace and name test values as `constexpr` constants.
- Each executable has exactly one `main.cpp` defining `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` and `DOCTEST_CONFIG_IMPLEMENT` (the precompiled `unit/pch.hpp` already included doctest, so the first no longer implies the second); test files include `<doctest/doctest.h>` only.
- A new test target also gets `set_pcxxheader("unit/pch.hpp")`.
- A new area adds a target to `tests/xmake.lua` with `add_tests("default")`, plus the `on_test(test_under_wine)` block from `core_tests` so its Windows build runs under Wine on Linux.
- Sanitizers run on request, never in plain `check`. Supported per preset: `SANITIZERS` in tools/project_n.py (GCC/Clang: address, undefined, thread, leak; MSVC: address). Any finding fails the run.

## Commands
- `pixi run project_n test --preset linux-clang --sanitizer address` (also `run --sanitizer ...` for the game)
- `pixi run project_n check --sanitizers`: every preset under every supported sanitizer
- `pixi run project_n test --preset linux-clang` · one target with doctest arguments:
  `pixi run project_n test --preset linux-clang --target core_tests -- --test-case='*expected*'`

## Gotchas
- `xmake test` runs a Windows `.exe` directly on Linux; it only worked on a dev machine whose kernel hands `.exe` files to Wine (binfmt_misc). The test targets call Wine themselves (`test_under_wine`).
- MSVC sanitizer builds compile and link in the container, but only *run* on native Windows: MSVC's ASan can't hot-patch Wine's system DLLs.
- Sanitizer builds link the C++ runtime dynamically (static libc++abi collides with TSan) and don't instrument dependencies.

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|
