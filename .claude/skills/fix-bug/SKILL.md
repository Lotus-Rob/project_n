---
name: fix-bug
description: Fix a bug in the game or engine code (src/) test-first. Use whenever a bug, crash, wrong result, regression, sanitizer report, fuzzer crash or review finding in C++ source is to be fixed, before changing any src/ code, and when a test fails and seems to be in the way.
---

# Fixing a bug test-first

Owned by: root AGENTS.md. Every bug in `src/` is fixed through test-driven development: first a test that fails because of the bug, then the fix.

## Steps

1. **Reproduce the bug as one test** in `tests/unit/<area>/` (a new area: tests/AGENTS.md). The test is:
   - **small:** the fewest lines that reach the faulty code;
   - **contained:** no files, network, window, clock or shared state it doesn't need, and independent of other tests;
   - **explicit:** named after the behaviour (`TEST_CASE("save header rejects a truncated file")`), with literal inputs and the exact expected value in each `CHECK`;
   - **about the bug and only the bug:** one behaviour; no unrelated assertions folded in.

   Logic only reachable through `main.cpp` or SDL can't be tested: first move it into a function in `src/` the test can call, without changing behaviour, then test that. A fuzzer crash also goes in as a seed in `tests/fuzz/corpus/<target>/`; a sanitizer finding must make the test fail under that sanitizer (`--sanitizer <name>`).
2. **Watch it fail for the bug's reason:** `pixi run project_n test --preset linux-clang --target <area>_tests -- --test-case='<name>'`. Failing for another reason (it doesn't compile, the setup is wrong) is not a reproduction: fix the test first.
3. **Fix** the code with the smallest change that makes the test pass. Change the code, not the test.
4. **Watch it pass**, then `pixi run project_n check` (plus the sanitizer run, if a sanitizer found the bug).
5. **Commit the test with the fix:** `fix(<scope>): <what was wrong>`, the body naming the failing case. One commit keeps every commit green and bisectable.

## Never

- Never delete, skip, disable or loosen a test case or fuzz seed to make a run go green. When a test's expectation is genuinely wrong, tell the user, and correct it in its own commit that explains why it was wrong.
- Never fix a `src/` bug without its test, however small the fix.

## Checks before finishing

- The test fails without the fix (stash the `src/` change and rerun it) and passes with it.
- `pixi run project_n check` passes.

## Gotchas
