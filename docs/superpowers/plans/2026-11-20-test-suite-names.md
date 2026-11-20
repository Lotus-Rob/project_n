# Test suite names Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `project_n test --suite <name>` refuses a name that runs no test, and `--suite fuzz_<name>` replays that target's seed corpus.

**Architecture:** `cmd_test` (tools/project_n.py) checks its `--suite`/`--test`/doctest arguments against what tests/xmake.lua and `tests/fuzz/` define before anything builds, raising `Fail`; fuzz suites take the same `xmake test <s>/*` path unit suites do.

**Tech Stack:** Python 3 standard library (`project_n.py`), `unittest` self-tests (`tools/test_project_n.py`), XMake.

**Spec:** `docs/superpowers/specs/2026-11-20-test-suite-names-design.md`

## Global Constraints

- `project_n.py` stays one file, standard library only (tools/AGENTS.md).
- Fuzz targets exist only under `--preset linux-clang --sanitizer address`; the preset is never switched silently.
- `headless_bench` keeps running its binary, with any arguments after `--`.
- Unit suites' behaviour is unchanged: `xmake test <s>/*` without filters, `xmake run <s> …` with `--test` or doctest arguments.
- Bugs are fixed test-first: each new self-test fails before the fix (root AGENTS.md).
- Code comments never cite the design process (`lint`'s `design_references`).
- Commit scope `build(tools)`; Conventional Commits (git-commit skill).

## Review Focus

1. A container run (`--container`, or a preset the host can't build natively): `cmd_test` refuses before `reenter`, so a refused name never starts a container; the re-entered `cmd_test` checks again and passes. Test: `test_a_refused_name_never_reaches_a_container`.
2. `--list-tests --suite fuzz_yaml` prints nothing and exits 0 (a fuzz suite has no test files), not a refusal: `--list-tests` returns before the checks. Test: `test_listing_a_fuzz_suite_prints_nothing`.
3. A new `tests/fuzz/<name>.cpp` or a new standalone `target(...)` in tests/xmake.lua is known with no list to edit. Test: `test_every_fuzz_file_and_program_is_a_known_name` (reads the real tree).
4. `--suite` absent (the whole run) is untouched, including the internal callers' `Namespace(target=None, test=None, args=[])`. Covered by the existing `CheckTest` and unchanged `else` branch; the reviewer confirms.
5. The refusal for an unknown name lists every valid name, so the user can retype it at once. Asserted in `test_a_name_that_is_no_suite_is_refused`.

---

### Task 1: Refuse names that run no test; fuzz targets as suites

review: full · implementer: sonnet

**Files:**
- Modify: `tools/project_n.py` (`test_names` neighbourhood ~:753, `cmd_test` ~:788-815, the `test` and `fuzz` parsers ~:3210-3240)
- Test: `tools/test_project_n.py` (`TestFilesTest` ~:2204-2266)
- Modify: `tools/AGENTS.md` (Commands, the `test --suite` line), `tests/AGENTS.md` (the `--suite`/`--target`/`--test` lines ~:32-35, and the fuzz line ~:22)

**Interfaces:**
- Produces, in `tools/project_n.py`:
  - `fuzz_targets() -> list[str]`: sorted stems of `tests/fuzz/*.cpp` (`["replay", "save", "yaml"]` today). The `fuzz` parser's `choices` use it instead of its inline glob.
  - `fuzz_suites() -> list[str]`: `["fuzz_" + t for t in fuzz_targets()]`.
  - `test_programs() -> list[str]`: sorted names of tests/xmake.lua's `target("<name>")` blocks whose next line is `set_kind("binary")` (today `["headless_bench", "reference_tests"]`; `test_pch` is no binary, and `unit_tests`' targets are made inside a function, so they don't match).
  - `check_test_target(target: str | None, test: str | None, extra: list[str], preset: str, sanitizer: str) -> None`: raises `Fail` per the rules below, returns otherwise.

- [ ] **Step 1: Write the failing tests** in `TestFilesTest`, reusing its `ran()` helper (give `ran` `preset` and `sanitizer` keyword parameters, defaulting to today's `NATIVE_PRESET` and `"none"`):

```python
def test_a_fuzz_suite_replays_its_seeds(self):
    self.assertEqual(self.ran("fuzz_yaml", preset="linux-clang", sanitizer="address"),
                     [["xmake", "test", "--output-on-failure", "fuzz_yaml/*"]])

def test_a_fuzz_suite_elsewhere_is_refused_with_the_command(self):
    for preset, sanitizer in (("linux-clang", "none"), ("linux-gcc", "address"), ("linux-clang", "thread")):
        with self.assertRaisesRegex(project_n.Fail, r"--preset linux-clang --sanitizer address"):
            self.ran("fuzz_yaml", preset=preset, sanitizer=sanitizer)

def test_a_fuzz_suite_takes_no_test_or_doctest_arguments(self):
    with self.assertRaisesRegex(project_n.Fail, "fuzz_save"):
        self.ran("fuzz_save", test="save", preset="linux-clang", sanitizer="address")
    with self.assertRaisesRegex(project_n.Fail, "fuzz_save"):
        self.ran("fuzz_save", args=["--", "-runs=10"], preset="linux-clang", sanitizer="address")

def test_a_name_that_is_no_suite_is_refused(self):
    for name in ("core", "game", "fuzz_nothing"):
        with self.assertRaises(project_n.Fail) as refused:
            self.ran(name)
        for valid in ("core_tests", "game_tests", "headless_bench", "reference_tests", "fuzz_yaml"):
            self.assertIn(valid, str(refused.exception), name)

def test_the_game_is_pointed_at_run(self):
    with self.assertRaisesRegex(project_n.Fail, r"project_n run"):
        self.ran("project_n")

def test_a_program_takes_no_test(self):
    with self.assertRaisesRegex(project_n.Fail, "headless_bench"):
        self.ran("headless_bench", test="save")

def test_a_program_keeps_its_arguments(self):
    self.assertEqual(self.ran("headless_bench", args=["--", "--ticks", "10"]),
                     [["xmake", "run", "headless_bench", "--ticks", "10"]])

def test_a_refused_name_never_reaches_a_container(self):
    with mock.patch.multiple(project_n, needs_container=lambda *a: True, reenter=mock.Mock(),
                             build=mock.Mock()) as patched, self.assertRaises(project_n.Fail):
        project_n.cmd_test(project_n.argparse.Namespace(preset="linux-clang", mode="debug", sanitizer="none",
                                                        container=True, target="core", test=None, args=[]))
    patched["reenter"].assert_not_called()
    patched["build"].assert_not_called()

def test_listing_a_fuzz_suite_prints_nothing(self):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        project_n.cmd_test(project_n.argparse.Namespace(list_tests=True, target="fuzz_yaml"))
    self.assertEqual(out.getvalue().strip(), "")

def test_every_fuzz_file_and_program_is_a_known_name(self):
    self.assertEqual(project_n.fuzz_suites(),
                     sorted("fuzz_" + f.stem for f in (project_n.ROOT / "tests" / "fuzz").glob("*.cpp")))
    self.assertIn("headless_bench", project_n.test_programs())
    self.assertIn("reference_tests", project_n.test_programs())
    self.assertNotIn("test_pch", project_n.test_programs())
    self.assertTrue(set(project_n.test_programs()).isdisjoint(project_n.test_names()))
```

The existing `test_a_target_that_is_no_suite_runs_its_binary` (`headless_bench` → `["xmake", "run", "headless_bench"]`) stays as it is.

- [ ] **Step 2: Run them to verify they fail**

Run: `pixi run project_n selftest`
Expected: the new cases FAIL or ERROR (no `fuzz_suites`/`test_programs`, `Fail` not raised, `fuzz_yaml` run as `xmake run`), except two guards of behaviour that already holds and must keep holding: `test_a_program_keeps_its_arguments` and `test_listing_a_fuzz_suite_prints_nothing` pass already. Every other case passes. Record the failing run's output for the report.

- [ ] **Step 3: Implement** `fuzz_targets`, `fuzz_suites`, `test_programs` and `check_test_target` in `tools/project_n.py` beside `test_names`, and call `check_test_target(args.target, args.test, extra, args.preset, args.sanitizer)` in `cmd_test` right after the `--list-tests` early return and the existing "pass --suite" check, before `test_filter`, `check_sanitizer`, the sweep, `reenter` and `build`. Rules, in order (`target is None` returns at once):
  1. `target` in `fuzz_suites()`: refuse `test` or `extra` ("a fuzz suite replays its seeds and takes no --test or doctest arguments; fuzz it with `pixi run project_n fuzz <stem>`"); refuse unless `preset == "linux-clang" and sanitizer == "address"`, naming `pixi run project_n test --preset linux-clang --sanitizer address --suite <target>`.
  2. `target` in `test_names()`: return (unit suites unchanged).
  3. `target` in `test_programs()`: refuse `test` (a doctest filter means nothing to it); arguments after `--` pass.
  4. `target == "project_n"`: refuse, pointing at `pixi run project_n run`.
  5. Anything else: refuse, listing the unit suites, the fuzz suites and the programs.

  In the dispatch, the first branch becomes `args.target in (*test_names(), *fuzz_suites()) and not extra`. Add the fuzz suites to `--suite`'s `[possible values: …]` help, after the unit suites and the programs. The `fuzz` parser's `choices` becomes `fuzz_targets()`.

- [ ] **Step 4: Run the self-tests to verify they pass**

Run: `pixi run project_n selftest`
Expected: all pass.

- [ ] **Step 5: Check it for real**

Run: `pixi run project_n test --suite core; echo exit=$?`, expected: the refusal listing the suites, `exit=1`, nothing built.
Run: `pixi run project_n test --preset linux-clang --sanitizer address --suite fuzz_yaml`, expected: XMake's test summary for `fuzz_yaml/corpus` passing, finishing in seconds (no endless fuzzing).
Run: `pixi run project_n test --suite core_tests --test log`, expected: passes as before.

- [ ] **Step 6: Update the docs** in the same commit:
  - tools/AGENTS.md, the `test --suite` line: a suite is a unit suite or a fuzz suite (`fuzz_<name>`: replays its seeds, linux-clang + address only); a standalone program of tests/xmake.lua (`headless_bench`, `reference_tests`) runs its binary; any other name is refused before anything builds, so a gate can't pass on a run of no test.
  - tests/AGENTS.md: the `--suite`/`--target` lines say the same in a line; the fuzz line adds `test --preset linux-clang --sanitizer address --suite fuzz_<name>` to replay one target's seeds.
  - Run `pixi run project_n doctor --docs` and expect no problems.

- [ ] **Step 7: Gates and commit**

Run: `pixi run project_n test` (all pass) and `pixi run project_n lint --range <task base>..HEAD` after committing (no findings).

```bash
git add tools/project_n.py tools/test_project_n.py tools/AGENTS.md tests/AGENTS.md
git commit -m "build(tools): refuse test suite names that run no test"
```
(Body: the two bugs it fixes, as the spec's Why says them.)
