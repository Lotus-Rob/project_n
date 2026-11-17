# Warnings: drop `-Weffc++`, relax tests, remove unity builds, release builds per task

Date: 2026-11-17 · Status: implemented 2026-11-17 (approved the same day) · Plan: `2026-11-17-warnings.md` · Amends:
design doc 0001 (rules 15 and 26)

## Outcome (2026-11-17)

Landed as five commits: "build(tests): relax MSVC evaluation-order and magic-number checks", "docs(agents): build GCC
and MSVC release after each task", "build(xmake): drop GCC's -Weffc++ and the initialisers it forced",
"build(tools)!: remove unity builds" and "docs(agents): say which builds show MSVC's warnings". The unity measurement
behind the removal is the table under Approaches.

Rulings and their evidence:

- **24 members in 10 files keep `{}`**, each under `NOLINTNEXTLINE(readability-redundant-member-init)` with the reason:
  a designated initialiser omits them, and Clang's `-Wmissing-designated-field-initializers` (which names one member
  per site) warns without one. The compiler decided which: of the 96 pairs `-Weffc++` forced, every `{}` Clang didn't
  name went, and a review's guess that more could go (`GameSetup`'s save and replay slots, `DisplayMode::name`) was
  wrong, as `game_tests.cpp` and `window_tests.cpp` omit them. `Action::data` takes `= nullptr` instead. So
  src/AGENTS.md's `-Weffc++` gotcha was replaced by this one rather than only deleted, as the Design said.
- **No member was left uninitialised** by the removed `{}`: the final review read every affected type that isn't a
  standard container; each scalar and pointer member has its own initialiser or a constructor that sets it.
- **The unity self-tests were deleted, not loosened**: they tested the feature the branch removes; one new self-test
  checks that `check` runs no unity step.
- **Design doc 0001's 2026-11-01 Revised entry keeps "unity builds configured but off"**: the Revised line is a dated
  record of what each revision did; rule 26 and the new Revised entry say they are gone.
- **The per-task release builds applied from the first task on** (the user's decision): linux-gcc and windows-msvc
  release passed at each of the three landings (Tasks 2 and 4, Task 1, Task 3), so no compiler error waited for
  `check`.
- **The final review's findings were all fixed**: eight half-sentences left by the removed `NOLINTBEGIN` blocks,
  rule 26's figures (GCC release cold builds were 3% faster, not slower; one-file rebuilds were not "unchanged";
  the agreed threshold is now stated), the removal of `test --unity` marked as breaking, a trailing comment the
  removed NOLINT had split, two `{}` on `Handover`'s vectors tidy misses inside a class template, the commits
  reshaped (the Clang initialisers folded into the `-Weffc++` commit, the formatting follow-up into the unity one),
  and the MSVC gotchas' "only in `check`" (each task's windows-msvc build shows them now). `game/scene.hpp`'s older
  suppression of the same kind took the standard one-line form.

## Why

5b1 and 5b2 each failed `check` four or five times in a row on errors only `check` builds: GCC's `-Weffc++`, warnings
only MSVC gives, then unity-build name clashes, each found one batch at a time at the branch head. The gate should
catch these at each task, and the warnings that only produce churn should go.

## In short

1. **No `-Weffc++`.** Its useful items are covered by `cppcoreguidelines-*` and Clang's `-Weverything`; its
   member-initialiser item only produced false positives (the 96 `{}` + `NOLINT(readability-redundant-member-init)`
   pairs it forced).
2. **Tests relax three MSVC warnings and the magic-number checks.** Everything else stays as strict as `src/`.
3. **Unity builds are removed.** Measured: they don't gain enough, and GCC can't build with them.
4. **Release builds per task.** The controller builds linux-gcc release and windows-msvc release after each task lands,
   so those compilers' errors go back to the task's implementer.

## Goals, non-goals, anti-goals

- Goals: fewer late `check` failures; no suppression noise for warnings that only produce false positives.
- Non-goals: rewriting existing test code to use the new freedoms (new tests use them; old ones stay as they are);
  touching the root `.clang-tidy` or `.clangd`.
- Anti-goals: loosening anything that finds real bugs: conversion warnings (C4242, C4267, `-Wconversion`), `bugprone-*`,
  sanitizers, `-Werror`, and every warning in `src/`.

## Approaches

The user decided the warning changes and the gates (2026-11-17); unity builds were measured first, with the rule agreed
beforehand: keep them and turn them on by default only if cold builds got at least 25% faster and one-file incremental
builds no more than 10% slower; otherwise remove them.

Measured 2026-11-17 on the dev PC (32 threads), XMake's builds of every target, seconds; incremental is the middle of
three runs after touching `src/game/combat.cpp` (one `.cpp`) or `src/core/world_map.hpp` (a header 23 files include):

| | cold off → on | `.cpp` off → on | header off → on |
|---|---|---|---|
| linux-gcc debug | 23.6 → 18.6 (−21%) | 5.5 → 5.6 | 15.0 → 14.0 |
| linux-gcc release | 87.8 → 84.9 (−3%) | 62.3 → 64.6 (+4%) | 77.9 → 79.8 |
| linux-clang debug | 26.7 → 19.4 (−27%) | 1.4 → 1.3 | 2.3 → 1.9 |
| linux-clang release | 47.5 → 38.0 (−20%) | 13.0 → 9.9 | 19.2 → 16.2 |

Only linux-clang debug passes; GCC release barely moves (its time is the LTO link). GCC also can't build with unity at
all: `renderer.cpp`'s and `text_layout.cpp`'s pimpl `Impl` structs hold types from an anonymous namespace, which
`-Wsubobject-linkage` rejects once the `.cpp` is no longer the main file (`check`'s unity step only ran the native
Clang preset, so it never showed). The GCC unity numbers above were taken with that warning off locally.

- **A. Remove unity builds (chosen).** Saves 5–10 s on a cold build, which is rare; costs nothing to drop.
- **B. Keep them, off.** Today's state: no speed gained, and `check`'s unity step still finds clashes one at a time.
- **C. Turn them on.** Needs GCC's `Impl` types out of anonymous namespaces, a lint check for file-local name clashes,
  and merged reader helpers, for a gain the rule rejects.

## Design

### `-Weffc++`

- xmake.lua's GCC branch adds `-Wno-effc++` after `set_warnings("everything", ...)` (XMake's "everything" turns it on
  for GCC), with a comment on why.
- Remove every `{}` initialiser that existed only for `-Weffc++`, with its `NOLINT(readability-redundant-member-init)`
  (`NOLINTNEXTLINE`, and `NOLINTBEGIN`/`NOLINTEND` blocks), in `src/` and `tests/`. `readability-redundant-member-init`
  then holds them out.
- src/AGENTS.md loses its effc++ gotcha; design doc 0001 rule 15 lists GCC's set without `-Weffc++` and says why.

### Tests

- tests/xmake.lua adds `/wd4866`, `/wd4868` (doctest's `CHECK` arguments' evaluation order) and `/wd5246` (double braces
  in an array of aggregates) on MSVC for test targets only, each with its reason.
- `tests/.clang-tidy` inherits the root (`InheritParentConfig: true`) and turns off
  `readability-magic-numbers` and `cppcoreguidelines-avoid-magic-numbers`, with a `#` reason: a test's literals are its
  samples, and naming each one hides what is checked.
- tests/AGENTS.md: the no-exemptions rule names these four exemptions and says everything else matches `src/`; the
  "name test values as `constexpr`" clause and both C4866 gotchas go. src/AGENTS.md's C4868 and C5246 gotchas stay
  (they still apply to `src/`).

### Unity builds

Remove: xmake.lua's `unity_build` option and rule; tests/xmake.lua's `unity_ignored` flags and their comment;
project_n.py's `test --unity`, `check`'s unity step and every `unity` parameter (`build_leaf`, `build_dir`,
`own_trees`, `object_cache_dir`, `configure`, `build`), with their self-tests in `tools/test_project_n.py`;
src/AGENTS.md's unity rule; the unity reason in `src/data/abilities.cpp`'s comment (rewritten to the reason that
remains, or removed). Design doc 0001: rule 26 says unity builds were removed and why (this measurement), the `test`
row loses `--unity`, the `check` row its unity step, and the Revised line records it. tools/AGENTS.md follows if it
mentions any of it.

### Gates

The root AGENTS.md's gates: after each task's commits land on the feature branch, the controller runs
`pixi run project_n build --preset linux-gcc --mode release` and `pixi run project_n build --preset windows-msvc --mode
release` in the main checkout (warm trees; a worktree would build cold), and sends any error back to that task's
implementer before the next wave.

### Verification

Each task: `pixi run project_n test` and `lint --range`. The branch: `check`, then `check --container` (a build
environment change), and CI.

## History

- `-Weffc++` came in with rule 15 (2026-10-30: "highest warnings always as errors"); the sweep-up walk of 2026-11-05
  kept its NOLINT pattern. The user reversed that on 2026-11-17 after 5b1 and 5b2.
- "No test exemptions" (tests/AGENTS.md) has no recorded rationale beyond the rule itself.
- Unity builds came in 2026-10-31 ("build(xmake): configure unity builds, off by default") with rule 26: off until full
  builds are slow enough to gain; `check` kept them working on the native preset. 5b2's `check` hit four unity clashes
  in a row (Refuse and Text, MOST_REACH, HashText, DECIMAL).
- No earlier ruling on per-task release builds.

## Left to the builder

The exact wording of comments and doc lines; whether `abilities.cpp`'s helper keeps its file-specific name (it may: the
name is still clear).
