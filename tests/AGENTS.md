# tests/: unit tests

## Scope
doctest suites, registered in `tests/xmake.lua` and run by `xmake test` on every preset (Windows tests run under Wine in the container).

## Rules
- Layout mirrors `src/`: `tests/unit/<area>/`, one test executable per area (`<area>_tests`) to keep link times down.
- Each executable has exactly one `main.cpp` with `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`; test files include `<doctest/doctest.h>` only.
- A new area adds a target to `tests/xmake.lua` with `add_tests("default")`.

## Commands
- `pixi run project_n test --preset linux-clang` · one target with doctest arguments:
  `pixi run project_n test --preset linux-clang --target core_tests -- --test-case='*expected*'`

## Gotchas

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|
