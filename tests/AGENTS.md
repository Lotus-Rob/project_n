# tests/: unit, fuzz, benchmark and reference tests

## Scope
doctest suites, registered in `tests/xmake.lua` and run by `xmake test` on every preset (Windows tests run under Wine in the container); fuzz targets, the headless benchmark and the reference pictures, built with them.

## Rules
- A test is small, contained (no file, network, window, clock or shared state it doesn't need; independent of other tests), explicit (named after the behaviour, literal inputs, exact expected values) and tests one behaviour: `TEST_CASE("save header rejects a truncated file")`.
- The one way a test writes a repo file is a generator case decorated `* doctest::skip()`, run by name with `--no-skip`, never in a plain run (`save_exactness_tests.cpp`'s seed writers).
- Logic only reachable through `main.cpp` or SDL moves into a `src/` function first, without changing behaviour. A sanitizer finding's test fails under that sanitizer (`--sanitizer <name>`).
- A unit test checks a behaviour on about 20 representative samples, each named for why it is there (first, last, a boundary, the edge case), never a sweep; assert a loop's worst value once. Timing is profiling's, breadth is fuzzing's.
- Skipped `measure ...` cases and long-run comparisons on 1 and 4 workers run by name with `--no-skip --mode release`, write only temp files and are never gates: no `check`, no CI.
- Hand-built host saves (`tests/support/saves.hpp`) write the world as its difference from the one the loading game makes: small test data by default, `test::ShippedData()` (slow to make) only for cases that need its geometry or size.
- Comments in tests follow src/AGENTS.md: `///` on a helper or fixture that states a contract, a comment inside a body only for a why, none citing a spec, plan, ruling or task (`lint` fails on one; a `TEST_CASE` name or `CAPTURE` string is code too).
- Layout mirrors `src/`: `tests/unit/<area>/`, one executable per area (`<area>_tests`). A test of process-wide state that can be set once (logging start-up) gets its own, as in `tests/unit/log_startup/`.
- Test code passes the same lint and warnings as `src/` except MSVC C4866, C4868, C5246 and the magic-number checks (`tests/.clang-tidy`): `TEST_CASE`s go in an anonymous namespace; literals may stand in a `CHECK`.
- Each executable has exactly one `main.cpp` defining `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` and `DOCTEST_CONFIG_IMPLEMENT`; test files include `<doctest/doctest.h>` only.
- `unit_tests()` registers one XMake test per `*_tests.cpp` file (`game_tests/combat`, doctest's `--source-file`) plus `rest` for the other cases: no file named `rest_tests.cpp`, no two of one suite share a name (the selftest checks) → `tests/xmake.lua`.
- `unit/pch.hpp` holds every standard header the tests use: test `.cpp` files never include standard headers (`lint` fails on one). Fuzz targets and the benchmark take the game's `src/pch.hpp` instead.
- A new area is one line in `tests/xmake.lua`: `unit_tests("<area>_tests", "unit/<area>/*.cpp", "<module>")`; it links the module (default `core`) and runs Windows builds under Wine on Linux.
- `tests/bench/headless.cpp` is the headless benchmark (`headless_bench`): built with the tests, not registered as one, run on request optimised; fails over its 2-worker budgets → its file header.
- `tests/reference/` compares pictures of fixed scenes with the committed PNGs beside it: `reference_tests`, not registered as a test (needs a GPU); `project_n reference` runs it on software Vulkan → `tests/reference/reference_tests.cpp` header.
- A change to shaders, sprites or a scene's layout that moves pixels needs `reference --update`, a look at the new pictures and the PNGs in the same commit → `.claude/skills/look-at-the-game/references/reference-pictures.md`.
- A reference test never hangs: every wait or search loop has a bound that fails loudly, naming what never happened (`SCENE_LOAD_LIMIT_NS`, `MOST_TICKS_AFTER_CAST`); whole cases are bounded by `REFERENCE_CASE_TIMEOUT` in tools/project_n.py.
- Reference cases run as a release build only, never under a sanitizer (not in `check --sanitizers`, not in CI) → `run_references` in tools/project_n.py.
- Fuzz targets: each file in `tests/fuzz/` defines `LLVMFuzzerTestOneInput`, one per reader of untrusted bytes (`replay`, `save`, `yaml`), with at least one seed in `tests/fuzz/corpus/<name>/`; commit every input that found a bug as a seed.
- Fuzz targets build only for linux-clang with AddressSanitizer and replay their seeds as a test, so `check --sanitizers` covers them; a seed in an old format only exercises the refusal: regenerate it when the format bumps → file headers.
- Coverage counts `src/` only, from the tests (linux-gcc's gcov data, reported by gcovr).
- Sanitizers run on request, never in plain `check`. Supported per preset: `SANITIZERS` in tools/project_n.py. Any finding fails the run.

## Commands
- `pixi run project_n test --mode release --target headless_bench`: tick, publish and month-step times of the stress arena; exits 1 over a gate
- `pixi run project_n test --preset linux-clang --sanitizer address` (also `run --sanitizer ...` for the game)
- `pixi run project_n reference` · `pixi run project_n reference --update`: take this run's pictures as the references
- `pixi run project_n fuzz <name> [--time 60]`: new inputs and crash files land in `build/fuzz/<name>/`
- `pixi run project_n test --preset linux-clang --sanitizer address --suite fuzz_<name>`: replays one fuzz target's seeds once
- `pixi run project_n test --suite app_tests [--test data_watch]`: one target; `--test` or doctest arguments run the binary once → `cmd_test` in tools/project_n.py
- `pixi run project_n test --mode release --target game_tests -- --test-case="<skipped case name>" --no-skip`: a skipped long run or measurement by name; double quotes, as names hold apostrophes
- `pixi run project_n test --preset linux-clang --target core_tests -- --test-case='*expected*'`: one target with doctest arguments
- `pixi run project_n test --target game_tests -- --test-case='write the <name> save seed' --no-skip`: regenerate a save fuzz seed (`fight`, `world`, `delta`, `scratch`) when the save layout changes

## Gotchas
- `xmake test` runs a Windows `.exe` directly on Linux; the test targets call Wine themselves (`test_under_wine` in `tests/xmake.lua`), so no host binfmt is needed.
- MSVC sanitizer builds compile and link in the container, but only run on native Windows: MSVC's ASan can't hot-patch Wine's system DLLs. Sanitizer link details → `xmake.lua`.
- A test that opens a window but draws nothing uses SDL's `dummy` video driver, not `offscreen` (macOS lacks EGL) → `tests/unit/platform/window_tests.cpp`.
- A reference bound to a member of a returned temporary dangles (`const auto& x = game::BuiltInData().abilities...`; GCC release read garbage and hung `check`): copy the value out. Same for `test::TestManifest()->...` → `tests/support/manifest.hpp`.
- `reference --update` rewriting pictures your change didn't touch: they had drifted within `TOLERANCE`; look at them and commit them with the change that moved them → `.claude/skills/look-at-the-game/references/reference-pictures.md`.
- MSVC C4464 (windows-msvc only): an include path with `..`. Shared test headers in `tests/support/` are reached as `support/<name>` → `tests/xmake.lua`'s `with_manifest()`.
