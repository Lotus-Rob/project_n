# R11: Tests and tooling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close R11's findings: pinned `core::math` bits, cheaper replay sessions, one set of shared game test helpers, a font-coverage test, `float-cast-overflow` in clang's sanitizer builds, Tracy without forced callstacks (and a new `measure` baseline), and five tooling fixes (`--test` that matches nothing, the sanitizer summary, orphan reference pictures, 0-byte binaries, one manifest parse per reference case).

**Architecture:** Test-only and tooling changes; no game behaviour changes. Each item lands where its code already lives: `tests/unit/...`, `tests/support/`, `xmake.lua`, `tools/project_n.py` (self-tested in `tools/test_project_n.py`).

**Tech Stack:** C++23, doctest, XMake, SDL_ttf, Tracy, Python 3 standard library.

**Spec:** `docs/superpowers/specs/2026-11-25-review-mediums-design.md`, area R11 (search "**R11. Tests and tooling**"), decision 2 ("Pinned golden values"), and the R11 rows of "Backlog items folded into the areas". Findings: `docs/reviews/2026-11-25-full-codebase-review/` (TESTS-CORE-01, TESTS-GAME-01 with -05/-06, TESTS-GAME-02, X-TEXT-L10N-01, GAP2-REPO-CONFIG-FILES-01, GAP1-THIRD-PARTY-LIBRARY-USAGE-01).

## History

- Decision 2 pins `core::math` bit patterns only, never world or state hashes: each preset checks its own table, presets that agree share one. Backlog 4b's "never pinned hashes" (long-run-determinism spec, from 0002's determinism within one binary) is narrowed to maths, not reversed. No golden table exists; `tests/unit/core/math_tests.cpp` compares with libm within 4 ulps on 20,001 samples. Tests know no preset (no `PN_` preset define); only compiler macros.
- Replay: `Session` (`tests/unit/game/replay_tests.cpp:54`) defaults to `test::ShippedData()` (50,000 NPCs, YAML parsed each call, no cache) and its save-start constructor always uses it; only the fight (`BuiltInData`) and delta (`TravellingSmallData`) cases differ. `tests/support/saves.hpp` already defaults to small data.
- Duplicated game test helpers (TESTS-GAME-02): `support/host.hpp` has `At`, `Tick`, `Foxes` (dying included), `FOX_HEALTH`, `Published`, `BodyOf`; local copies in combat, host, replay (whose `Foxes` excludes dying ones), scenes, save and travel tests.
- Font: `assets/fonts/NotoSansSC-Regular.otf`; `ttf_tests.cpp` checks two glyphs with `TTF_FontHasGlyph`; `gfx/text_layout.cpp`'s `WarnMissing` logs missing ones at run time. 0002.4 §3 promised the coverage test.
- Sanitizers: `xmake.lua:12-31` sets XMake's `build.sanitizer.<name>` policy and no flags of its own; clang's `-fsanitize=undefined` excludes `float-cast-overflow`. `.clang-tidy` and `.clangd` are the user's and stay untouched.
- Tracy: `xmake.lua:266-267` (`tracy_on`/`tracy_off`) set only `tracy_enable` and `only_localhost`; the recipe defaults force callstacks and fibers. `core/profile.hpp:25-28` says a zone costs its caller about 0.25 µs under a capture (measured with callstacks on). `MEASURE_EARLIER` (`tools/project_n.py:2562`) holds older arena numbers, printed as "earlier, unpinned". Later baselines sit in specs' Outcomes (travellers, roads, shared-routes); none is comparable after this change.
- `--test`: `test_filter` (`tools/project_n.py:850`) turns a name that is no test file into `--test-case=*name*`, and a filter that matches nothing runs nothing and passes (its docstring says so). Selftest neighbour: `test_a_test_file_of_another_suite_is_refused`.
- `check --sanitizers` prints "every supported sanitizer" (`:2280`) though on Linux `cmd_test` only builds windows-msvc's ASan (`:886-889`).
- `run_references` (`:971`) finds committed pictures no case drew only under `--update`; the drift guard (within tolerance but changed) already prints (`:1009-1015`), so that backlog item is closed by existing code.
- A stopped build left 0-byte test binaries that XMake took as up to date (backlog, 2026-11-21); no code handles it.
- `RendererFor` (`tests/reference/reference_tests.cpp:190`) calls `test::TestManifest()` (no cache) up to four times and parses strings twice (once in `ShippedWorld`).

## Global Constraints

- Touches no game behaviour. A bug found on the way (a sanitizer or coverage finding) is fixed test-first in `src/`, with its test.
- Decision 2: pin `core::math` bit patterns only, never world or state hashes; each preset checks its own table; presets that agree share one.
- Tests follow tests/AGENTS.md: about 20 named samples per behaviour, never sweeps; test `.cpp` files include no standard headers; `TEST_CASE`s in an anonymous namespace; shared helpers in `tests/support/` reached as `support/<name>`.
- `.clang-tidy` and `.clangd` are never edited. Pins are exact; a changed `add_requires` config changes `xmake-requires.lock` in the same commit.
- `project_n.py` stays one standard-library file; every new function has a selftest.
- `measure` takes the display: the task that runs it runs alone, nothing else building.
- Code comments explain why, never cite the review, spec, plan or dates.

## Review Focus

1. A toolchain with no pinned table (macOS's clang on arm64, a new compiler): the pin test fails and prints the table it computed, never passes vacuously. Task 1.
2. `--test` with a name that is a file of another suite, a case substring, or nothing at all: refused with candidates only for the last; a case substring still runs. Task 5.
3. Replay cases that rely on the shipped world's scale or geometry keep it, named why, and still fail for their bug. Task 2.
4. The 0-byte cleanup never deletes anything but empty regular files in the binary folder (not objects, stamps or package caches). Task 7.
5. A reference run whose case failed must not also blame its missing picture as an orphan. Task 7.

## Waves

| Wave | Tasks | Needs |
|---|---|---|
| 1 | 1 (math pins), 2 (replay small data), 5 (`--test`), 8 (`RendererFor`) | main |
| 2 | 3 (shared helpers), 4 (font coverage), 6 (float-cast-overflow), 7 (tooling fixes) | 2 for 3 (`replay_tests.cpp`); 5 for 7 (`project_n.py`, rebase onto it) |
| 3 | 9 (Tracy and baseline), alone | 6 (`xmake.lua`); every other task merged (measure runs alone) |

Each implementer: `implementer: sonnet`. Reviews: tasks 1, 2, 3, 4, 8 `review: light` (tests); 5, 6, 7, 9 `review: full` (build flags, tooling that gives a verdict).

---

### Task 1: pinned `core::math` bit patterns
`implementer: sonnet` · `review: light`

**Files:** Modify `tests/unit/core/math_tests.cpp`, `tests/AGENTS.md`.

**Interfaces:** Consumes `core::Sin`, `Cos`, `Exp`, `Log`, `Pow`, `Atan2` (f64) from `src/core/math.hpp`.

- [ ] **Step 1:** Add a skipped generator case `TEST_CASE("print the pinned core::math table" * doctest::skip())` that prints, as C++ source, each f64 function's results on its samples as `std::bit_cast<u64>` hex literals.
- [ ] **Step 2:** Samples: 20 named inputs per f64 function (an array of `{ name, input }`, the name saying why: zero, a subnormal, each range-reduction boundary, the largest accepted input, a negative, a value past the range that yields NaN, ...). The f32 overloads are not pinned: "the f32 overloads round once" already ties them to the f64 results.
- [ ] **Step 3:** Toolchain key by compiler macros, one table per key: `_MSC_VER` → msvc; `__clang__` with `__aarch64__` → clang-arm64; `__clang__` → clang-x64; otherwise gcc-x64. Spike S1 (measured here, by the generator run with `--no-skip` on linux-gcc, linux-clang and windows-msvc): keys whose tables are identical share one table (the decision rule: byte-identical → one table, any bit differs → own table). Record which keys share in the report.
- [ ] **Step 4:** `TEST_CASE("core::math gives its pinned bits on this toolchain")`: compare every sample with its pinned value; on a mismatch name the function and sample. A key with no table fails with `FAIL` and prints the computed table (the generator's output), so a new toolchain's first run gives the values. clang-arm64 (macos-clang) is filled from the PR's macOS CI log by the controller before merge.
- [ ] **Step 5:** Run `pixi run project_n test --suite core_tests --test math` on linux-gcc, linux-clang (`--preset linux-clang`) and windows-msvc (`--preset windows-msvc`): pass. Change one pinned literal by one bit: fails naming the sample; restore it.
- [ ] **Step 6:** tests/AGENTS.md, one line: `core::math`'s bit patterns are pinned per toolchain (a change detector); world and state hashes never are: determinism holds within one binary.
- [ ] **Step 7:** Commit `test(core): pin core::math's bit patterns per toolchain`.

### Task 2: replay sessions default to small data
`implementer: sonnet` · `review: light`

**Files:** Modify `tests/unit/game/replay_tests.cpp`. Read TESTS-GAME-01, -05, -06 in `docs/reviews/2026-11-25-full-codebase-review/`.

- [ ] **Step 1:** Time `pixi run project_n test --suite game_tests --test replay` (native) three times; note the median.
- [ ] **Step 2:** `Session`'s default data becomes `test::SmallData()`; its save-start constructor takes `core::GameData data = test::SmallData()`.
- [ ] **Step 3:** A case that needs the shipped world's scale or geometry passes `test::ShippedData()` explicitly with a one-line why; every other case runs on small data. For each case switched, confirm it can still fail for its bug (break the behaviour it checks, see it fail, restore).
- [ ] **Step 4:** Run the suite native and on windows-msvc: pass. Time it again; put both medians in the report.
- [ ] **Step 5:** Commit `test(game): run replay sessions on the small test data`.

### Task 3: one set of shared game test helpers
`implementer: sonnet` · `review: light` · after Task 2

**Files:** Modify `tests/support/host.hpp` (or a new `tests/support/<topic>.hpp` when a helper doesn't belong to the host), `tests/unit/game/combat_tests.cpp`, `host_tests.cpp`, `replay_tests.cpp`, `scenes_tests.cpp`, `save_tests.cpp`, `travel_tests.cpp`. Read TESTS-GAME-02's body (its file under `docs/reviews/2026-11-25-full-codebase-review/`).

- [ ] **Step 1:** For each duplicated helper (`At`, `Tick`, `Foxes`, `FootOf`, `Published`, `Entities`, `FOX_HEALTH`, `SamePoint`, `SameEvents`, `StepMonth`, `Loads`/`LoadsWith`/`LoadsWithMap`, `CultivatorAt`, `NearTheCrystal`/`NearCrystalData`, the far-from-crystal fixture), compare every copy's signature and body. Identical → one copy in support. Different meaning → a support helper per meaning, each named for it (replay's `Foxes` without dying ones becomes `LivingFoxes`). Only a signature differs → one helper with the wider signature.
- [ ] **Step 2:** Delete the local copies; each call site uses the support name for the meaning it had. Every support helper gets a `///` comment stating its contract (dying included or not, and so on).
- [ ] **Step 3:** `pixi run project_n test --suite game_tests` native and windows-msvc: pass, with the same case count as before.
- [ ] **Step 4:** Commit `test(game): share the game tests' helpers, naming each variant`.

### Task 4: every shipped codepoint is in the font
`implementer: sonnet` · `review: light`

**Files:** Create `tests/unit/gfx/font_coverage_tests.cpp`; modify `tests/xmake.lua` only if `gfx_tests` doesn't link `data` yet (add it to that line's modules).

**Interfaces:** Consumes `data::LoadStrings(const std::filesystem::path& folder)` (`src/data/strings.hpp:39`) and `data::LoadNames(const std::filesystem::path& file)` (`src/data/names.hpp:29`), the font as `ttf_tests.cpp:31-45` opens it.

- [ ] **Step 1:** `TEST_CASE("every codepoint of the shipped strings and names is in the font")`: load `assets/data/strings.yaml` and `names.yaml` through the loaders (values only, never comments or keys), decode each string's UTF-8, check `TTF_FontHasGlyph` for every codepoint other than control characters; on a miss `CHECK_MESSAGE` names `U+XXXX` and the string's key or name pool, once per missing codepoint.
- [ ] **Step 2:** Run `pixi run project_n test --suite gfx_tests --test font_coverage` native and windows-msvc. A miss is a real finding: report it to the controller (the fix is the font or the text, the user's call), never loosen the test.
- [ ] **Step 3:** Prove it can fail: check one codepoint the font lacks (a private-use one) through the same helper in a throwaway edit; restore it.
- [ ] **Step 4:** Commit `test(gfx): check the font holds every shipped codepoint`.

### Task 5: `--test` that matches nothing fails, naming candidates
`implementer: sonnet` · `review: full`

**Files:** Modify `tools/project_n.py` (`test_filter`, `cmd_test` and its docstring), `tools/test_project_n.py`, `tools/AGENTS.md` if a rule changes.

**Interfaces:** Produces `doctest_case_count(listing: str) -> int` and `test_candidates(name: str, stems: list[str], cases: list[str]) -> list[str]`.

- [ ] **Step 1:** Selftests beside `test_a_test_file_of_another_suite_is_refused`: `doctest_case_count` reads doctest's `--list-test-cases` output ("unskipped test cases passing the current filters: N"); `test_candidates("route_graph_tests", ["route_graph", "routes"], [...])` includes `route_graph`; `cmd_test` with `--test nothing_like_it` and a mocked listing of 0 raises `Fail` whose text names the candidates; a substring that matches a case runs as before.
- [ ] **Step 2:** Run `pixi run project_n selftest`: the new tests fail.
- [ ] **Step 3:** Implement: when `--test` becomes a `--test-case` filter, after the build list the matching cases in each target it would run (`<target> --list-test-cases --test-case=*name*` through the same runner `cmd_test` uses, so windows-msvc goes through Wine); none in any target → `Fail("--test <name> matches no test file and no test case; did you mean: ...")`, candidates from `difflib.get_close_matches(name, stems + case names, n=5, cutoff=0.6)` over the file stems and every case name of those targets.
- [ ] **Step 4:** Selftest passes; `pixi run project_n test --suite core_tests --test route_graph_tests` fails naming `route_graph`; `--test path_text` still runs.
- [ ] **Step 5:** Commit `build(tools): fail a --test that matches no file or case, naming candidates`.

### Task 6: `float-cast-overflow` in clang's sanitizer builds
`implementer: sonnet` · `review: full`

**Files:** Modify `xmake.lua` (the sanitizer rule, lines 12-31).

- [ ] **Step 1:** For the `undefined` sanitizer on a clang toolchain (linux-clang, macos-clang), add `-fsanitize=float-cast-overflow` to compile and link flags. GCC and MSVC unchanged.
- [ ] **Step 2:** Spike S2 (measured here, once, not committed): a throwaway test casting `1e20` to `i32` under `pixi run project_n test --preset linux-clang --sanitizer undefined --suite core_tests` reports "outside the range of representable values" and fails the run. Decision rule: no report → the flag is not reaching the build: fix the wiring before going on. Remove the throwaway.
- [ ] **Step 3:** `pixi run project_n test --preset linux-clang --sanitizer undefined`: passes, or each finding is a bug fixed test-first in `src/` (its test fails under that sanitizer, tests/AGENTS.md).
- [ ] **Step 4:** Commit `build(xmake): catch float-to-int overflow in clang's UB sanitizer`. A build change: CI's container jobs on the PR are its container gate.

### Task 7: three tooling fixes
`implementer: sonnet` · `review: full` · after Task 5

**Files:** Modify `tools/project_n.py` (`cmd_check`'s summary, `run_references`, `build`), `tools/test_project_n.py`, `tools/AGENTS.md` where a rule changes.

**Interfaces:** Produces `check_summary(sanitizers: bool, skipped: list[str], built_only: list[str]) -> str`, `orphan_pictures(committed: Path, drawn: Path) -> list[str]`, `remove_empty_outputs(folder: Path) -> list[str]`.

- [ ] **Step 1 (sanitizer summary):** Selftests: with sanitizers on a non-Windows host, the summary reads "all presets passed with every supported sanitizer; windows-msvc's address built, not run (MSVC sanitizer runs need native Windows)"; on Windows, or without `--sanitizers`, as today. Implement `check_summary` and use it at `:2280`. Commit `build(tools): say which sanitizer check only built`.
- [ ] **Step 2 (orphan pictures):** Selftests: `orphan_pictures` returns the committed PNG names no case drew, sorted. A normal `reference` run with no failed case and orphans fails: "reference: no case drew <names>: delete them or restore their case"; with a failed case it reports only the failure. `--update` keeps its message. Commit `build(tools): fail reference on committed pictures no case draws`.
- [ ] **Step 3 (0-byte outputs):** Selftests: `remove_empty_outputs` deletes only empty regular files directly in the folder (not subfolders, not non-empty files) and returns their names. `build()` calls it on the binary folder (`binary_path`'s parent) before `xmake build` and prints "build: removed <n> empty outputs a stopped build left: <names>" when any. Commit `build(tools): remove the empty outputs a stopped build leaves`.
- [ ] **Step 4:** `pixi run project_n selftest`; `pixi run project_n reference` passes.

### Task 8: one manifest parse per reference case
`implementer: sonnet` · `review: light`

**Files:** Modify `tests/reference/reference_tests.cpp` (`RendererFor`, `:190-220`); `tests/support/shipped_world.hpp` only to add an overload taking loaded strings, if `ShippedWorld` has none.

- [ ] **Step 1:** Time `pixi run project_n reference` (note its total).
- [ ] **Step 2:** `RendererFor` loads the manifest once (`const auto manifest = test::TestManifest();`) and passes it to every use, and the strings once, given to `ShippedWorld`.
- [ ] **Step 3:** `pixi run project_n reference`: passes with no picture changed; note the new total in the report.
- [ ] **Step 4:** Commit `test(gfx): parse the manifest once per reference case`.

### Task 9: Tracy without forced callstacks or fibers, and a new baseline
`implementer: sonnet` · `review: full` · alone, last

**Files:** Modify `xmake.lua:260-267`, `xmake-requires.lock`, `src/core/profile.hpp:25-28`, `tools/project_n.py` (`MEASURE_EARLIER` and its comment).

- [ ] **Step 1 (before):** On the branch before the change, `pixi run project_n measure --keep` three times, alone, on the 165 Hz monitor (fullscreen lands on the focused one; a 60 Hz lock means the wrong one). Keep the tables.
- [ ] **Step 2:** Add `enforce_callstack = false, fibers = false` to both `tracy_on` and `tracy_off` configs (identical apart from `tracy_enable`, so the lock holds both); first confirm those config names exist in the `tracy-libdl` recipe (`xmake/repo/` or xmake-repo's). Regenerate the lock; commit both: `build(xmake): build Tracy without forced callstacks or fibers`.
- [ ] **Step 3 (after):** The same three `measure --keep` runs, alone.
- [ ] **Step 4:** Spike S3, the zone cost: per-zone cost = (swords run's median frame before − after) / zones per frame of that run (from the kept capture). Decision rule: the frame delta beyond the three runs' spread → `profile.hpp` states the new cost; within the spread, or zones uncountable → the sentence keeps its reason without a number. Commit `docs(core): state a zone's cost without forced callstacks`.
- [ ] **Step 5:** `MEASURE_EARLIER` takes the after runs' burst, swords and seals medians (its comment says they are this build's baseline under Tracy without callstacks, no date). Commit `build(tools): take the arena baseline without forced callstacks`. Both tables (before and after) go in the report for the spec's Outcome.
- [ ] **Step 6:** `pixi run project_n test` and `pixi run project_n build --preset linux-gcc --mode releasedbg` pass.

## After the last task (controller)

Spec: R11's Outcome (rulings, S1's shared tables, S2, S3 and the before/after tables) and R11 marked **Implemented**; step 2's Status `done` if R2 is on main. After merge: the R11 backlog entries ("A stopped build leaves 0-byte binaries", "RendererFor parses the manifest", "Reference drift guard", audits 3 and 9) leave `sweep-up-backlog.md`.
