# Test suite names: refuse a name that runs no test, replay fuzz seeds as a suite

Date: 2026-11-20 · Status: **approved 2026-11-20** · Plan: `2026-11-20-test-suite-names.md`

## Why

`pixi run project_n test --suite <name>` must never pass without having run a test. Today `cmd_test`
(tools/project_n.py) hands any name `test_names()` doesn't know straight to `xmake run <name>`: on the input-latency
branch `--suite core` ran the `core` static library, printed nothing and exited 0, so a red test looked green until it
was rerun as `--suite core_tests`. And `--suite fuzz_<name>` runs the libFuzzer binary with no arguments, which fuzzes
forever instead of replaying its seeds; replaying one target's seeds today needs the whole
`test --preset linux-clang --sanitizer address`.

## History

- `test_names()` reads only tests/xmake.lua's one-line `unit_tests(...)` calls: it knows no fuzz targets and no
  standalone test programs. tools/AGENTS.md promises "a target that is no suite (`headless_bench`) always runs its
  binary", and tests/AGENTS.md documents `--target headless_bench`: that stays.
- Each `tests/fuzz/<name>.cpp` becomes target `fuzz_<name>` only on linux + pixi-clang + address, with one test,
  `corpus`, running `-runs=0 tests/fuzz/corpus/<name>` (tests/xmake.lua); the full sanitizer run replays them through
  the bare `xmake test`. `cmd_fuzz` takes its target choices from the same glob of `tests/fuzz/*.cpp`.
- Both bugs were recorded in the sweep-up backlog (found 2026-11-18 on fuzz-yaml-asan and 2026-11-20 on
  input-latency), to be fixed together. Nothing was tried or rejected before.

## Goals

1. `test --suite fuzz_<name>` runs `xmake test fuzz_<name>/*`: the target's seed corpus replayed once with `-runs=0`.
   Fuzz suite names come from `tests/fuzz/*.cpp`, as `cmd_fuzz`'s do.
2. A fuzz suite on any preset or sanitizer but `--preset linux-clang --sanitizer address` is refused, printing that
   command; the preset is never switched silently, so the run is always the one asked for.
3. `--suite`/`--target` takes only a unit suite, a fuzz suite or a standalone test program of tests/xmake.lua
   (`headless_bench`, `reference_tests`: a binary target that is no suite). Anything else is refused, listing what it
   takes; `--suite project_n` is pointed at `run`.
4. `--test` needs a doctest suite (a unit suite): refused with a fuzz suite or a standalone program. Doctest
   arguments after `--` are refused with a fuzz suite; a standalone program keeps taking arguments after `--`.
5. `--help`'s possible values and `--list-tests` know the fuzz suites (which have no test files to list).
6. Test-first: self-tests in `tools/test_project_n.py` for each refusal and for the fuzz replay command, failing
   before the fix. tools/AGENTS.md and tests/AGENTS.md updated in the same commit.

## Non-goals

- Changing what a unit suite's run does (`xmake test <s>/*`, or `xmake run <s>` with `--test` or doctest arguments).
- Registering `reference_tests` as a test, or changing `reference`, `fuzz` or `check`.

## Approaches

- **Refuse unknown names, fuzz targets as suites (picked):** every name is checked against what tests/xmake.lua and
  `tests/fuzz/` define, so a new suite, fuzz target or program is known without a list to keep.
- **Warn on an unknown name:** still exits 0, so a gate still reads green; rejected.
- **A fixed allowlist of runnable names in project_n.py:** goes stale when a target is added; rejected.

## Left to the builder

How the standalone programs are found (reading tests/xmake.lua's `target("…")` blocks of `set_kind("binary")` that
aren't suites is one way), the refusal messages' wording, and where the checks sit in `cmd_test` (before the build, so
a refusal is instant; a container run re-enters `cmd_test` and checks again, harmlessly).
