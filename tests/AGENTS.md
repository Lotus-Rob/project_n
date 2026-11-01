# tests/: unit tests

## Scope
doctest suites, registered in `tests/xmake.lua` and run by `xmake test` on every preset (Windows tests run under Wine in the container).

## Rules
- A test is small, contained and explicit, and tests one behaviour. Never delete, skip or loosen one to go green (fix-bug skill).
- Layout mirrors `src/`: `tests/unit/<area>/`, one test executable per area (`<area>_tests`) to keep link times down.
- Exception: a test of process-wide state that can only be set once (logging start-up: Quill's loggers live for the whole process) gets its own executable, as in `tests/unit/log_startup/`.
- Test code passes the same lint as `src/` (no test exemptions): put `TEST_CASE`s inside an anonymous namespace and name test values as `constexpr` constants.
- Each executable has exactly one `main.cpp` defining `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` and `DOCTEST_CONFIG_IMPLEMENT` (the precompiled `unit/pch.hpp` already included doctest, so the first no longer implies the second); test files include `<doctest/doctest.h>` only.
- `unit/pch.hpp` holds every standard header the tests use: test `.cpp` files never include standard headers themselves (`lint` fails on one). Unit targets get it from `unit_tests()`; a fuzz target sets it with `set_pcxxheader("unit/pch.hpp")`.
- A new area is one line in `tests/xmake.lua`: `unit_tests("<area>_tests", "unit/<area>/*.cpp", "<module>")`. The helper links the module under test (default `core`) with the modules below it and their packages, adds the precompiled header, registers the test and runs Windows builds under Wine on Linux.
- `tests/bench/headless.cpp` is the headless benchmark (`headless_bench`, design doc 0002 §16): it ticks a large world without a window, is built with the tests but not registered as one, and takes the game's precompiled header (it isn't a doctest suite).
- Fuzz targets: `tests/fuzz/<name>.cpp` defines `LLVMFuzzerTestOneInput`, with at least one seed in `tests/fuzz/corpus/<name>/`. They build only for linux-clang with AddressSanitizer, where each replays its seeds as a test, so `check --sanitizers` covers them. Commit every input that found a bug as a seed.
- Coverage counts `src/` only, from the tests (linux-gcc's gcov data, reported by gcovr).
- Sanitizers run on request, never in plain `check`. Supported per preset: `SANITIZERS` in tools/project_n.py (GCC/Clang: address, undefined, thread, leak; MSVC: address). Any finding fails the run.

## Commands
- `pixi run project_n test --mode release --target headless_bench`: tick times of the headless world
- `pixi run project_n test --preset linux-clang --sanitizer address` (also `run --sanitizer ...` for the game)
- `pixi run project_n check --sanitizers`: every preset under every supported sanitizer
- `pixi run project_n fuzz <name> [--time 60]`: new inputs and crash files land in `build/fuzz/<name>/`
- `pixi run project_n coverage`: `build/coverage/index.html` and a summary
- `pixi run project_n test --preset linux-clang` · one target with doctest arguments:
  `pixi run project_n test --preset linux-clang --target core_tests -- --test-case='*expected*'`

## Gotchas
- `xmake test` runs a Windows `.exe` directly on Linux; it only worked on a dev machine whose kernel hands `.exe` files to Wine (binfmt_misc). The test targets call Wine themselves (`test_under_wine`).
- MSVC sanitizer builds compile and link in the container, but only *run* on native Windows: MSVC's ASan can't hot-patch Wine's system DLLs.
- Sanitizer builds link the C++ runtime dynamically (static libc++abi collides with TSan) and don't instrument dependencies.

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|
