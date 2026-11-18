# Reference hang Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `project_n reference` never deadlocks from thread exhaustion (a container's 2048-task cap), a hung case fails within a bound and is named, and one TSan run of the reference cases answers whether they should be gated under it.

**Architecture:** `run_references` gives each case its share of the cores, `max(1, jobs() // min(jobs(), count))`, as `LP_NUM_THREADS` (llvmpipe) and `PROJECT_N_REFERENCE_WORKERS` (read by the reference cases for `core::Jobs`); `run_reference_case` passes a timeout to `subprocess.run` and turns an expiry into a named failure. Then the bound is measured and set, and a thread-sanitizer build of `reference_tests` is run once by hand.

**Tech Stack:** Python 3 (tools/project_n.py, unittest self-tests), C++23 doctest (tests/reference/), Mesa lavapipe 26.2.6, Podman.

**Spec:** `docs/superpowers/specs/2026-11-18-reference-hang-design.md`

## Global Constraints

- The number of cases run at once stays `jobs()` (one process per case, all cores): only each process's threads change.
- No Mesa version change, no Mesa bug report, no change to the game's own worker count outside `tests/reference/`.
- Running `reference_tests` by hand without `PROJECT_N_REFERENCE_WORKERS` keeps `core::Jobs::DefaultWorkers()`.
- Before (native, warm, this host, 2026-11-18): `pixi run project_n reference` 15.4 s, 10.4 s, 10.1 s. The share must not make it slower; if it does, stop and bring it back to the user.
- Code comments say why, never cite the spec, plan or tasks (`lint` checks). Commits: Conventional Commits per the git-commit skill: `build(tools)` for the runner, `test(tests)` for the reference cases, `docs(design)` for the spec; never `feat`/`fix` (game code only).
- AGENTS.md lines change in the same commit as the behaviour they describe.

## Review Focus

1. A case that hangs prints nothing (`--minimal`): the failure must still name it, by doctest name where the binary can say, else `case N`. Test in Task 1.
2. A timeout's partial output may come back as bytes, str or None from `subprocess.TimeoutExpired`: the runner must not crash on any of them. Test in Task 1.
3. Fewer cases than cores (a future `--test` filter, or a 64-core host): the share divides by the cases actually running at once, not by `jobs()`. Test in Task 1.
4. A malformed `PROJECT_N_REFERENCE_WORKERS` (`""`, `"0"`, `"x"`, `"3x"`) fails loudly instead of silently using a default. Test in Task 2.
5. `--update` after a timed-out case: the existing "every committed view must be redrawn" refusal must still fire (a hung case drew nothing). Test in Task 1.

## Waves

| Wave | Tasks | Needs |
|---|---|---|
| 1 | Task 1 (tools), Task 2 (reference workers) | nothing; they share only the variable name fixed below |
| 2 | Task 3 (measure, set the bound, container run) | Tasks 1 and 2 on the branch; main checkout; runs alone |
| 3 | Task 4 (one TSan run) | Task 3; runs alone (whole machine) |

Fixed interface between Tasks 1 and 2: the environment variable `PROJECT_N_REFERENCE_WORKERS`, a decimal count ≥ 1.

---

### Task 1: Thread share and per-case timeout in the reference runner

review: full · implementer: sonnet

**Files:**
- Modify: `tools/project_n.py` (`run_reference_case` ~845, `run_references` ~858, a new constant beside them)
- Test: `tools/test_project_n.py` (`RunReferenceCaseTest` ~524, `ReferenceTest` ~554)
- Modify: `tools/AGENTS.md:47` (the `reference` line)

**Interfaces:**
- Produces: `REFERENCE_CASE_TIMEOUT: int` (seconds; `300` for now, Task 3 sets the measured value), `reference_share(running: int) -> int`, `reference_case_name(exe, env, index) -> str | None`, and `run_reference_case(exe, env, index)` unchanged in signature, now returning `(output, returncode)` with a non-zero code and a `timed out after N s` line on expiry.
- Consumes: `jobs()`, `doctest_case_name(output)`.

- [ ] **Step 1: Write the failing tests** in `tools/test_project_n.py`:
  - `RunReferenceCaseTest.test_a_case_past_the_bound_fails_and_says_so`: `fake_run` raises `subprocess.TimeoutExpired(argv, kwargs["timeout"], output=b"partial\n")`; assert `kwargs["timeout"] == project_n.REFERENCE_CASE_TIMEOUT`, the returned code is non-zero, the output contains `"partial"` and `f"timed out after {project_n.REFERENCE_CASE_TIMEOUT} s"`, and the runtime directory is gone.
  - `test_a_timeout_without_output`: the same with `output=None` and with `output="text"`: no exception, the `timed out` line present.
  - `ReferenceTest.test_a_timed_out_case_is_named`: `case` returns `("reference: case 2 timed out after 300 s\n", -9)` for index 2; patch `reference_case_name` to return `"world_route"` for index 2; assert `CalledProcessError` whose `stderr` contains `world_route`. And with `reference_case_name` returning `None`: `stderr` contains `case 2`.
  - `ReferenceTest.test_each_case_gets_its_share`: patch `jobs` to 32, the count to 47 (and separately jobs 8, count 3); capture each `env` the fake `run_case` receives; assert `env["LP_NUM_THREADS"] == env["PROJECT_N_REFERENCE_WORKERS"] == "1"` for 32/47 and `"2"` for 8/3 (8 // 3).
  - `ReferenceTest.test_update_refuses_after_a_timeout`: committed `a.png` and `b.png`, case 2 times out without drawing: `--update` raises `project_n.Fail`.
- [ ] **Step 2: Run** `pixi run project_n selftest`. Expected: the five new tests fail (missing `REFERENCE_CASE_TIMEOUT`, no `timeout` kwarg, no share in env), the rest pass.
- [ ] **Step 3: Implement** in `tools/project_n.py`:
  - `REFERENCE_CASE_TIMEOUT = 300` with a comment on why a case needs a bound at all (a deadlocked case would hang the gate silently).
  - `reference_share(running)`: `max(1, jobs() // max(1, running))`, docstring saying why (each process's threads sized to its slice of the cores; every case at full width over-subscribes and exhausts a container's task cap, which deadlocks llvmpipe).
  - `run_reference_case`: `timeout=REFERENCE_CASE_TIMEOUT`; on `subprocess.TimeoutExpired` (the child is already killed by `subprocess.run`), decode `exc.output` if bytes, treat None as `""`, append `f"reference: case {index} timed out after {REFERENCE_CASE_TIMEOUT} s\n"`, return code `-9`.
  - `reference_case_name(exe, env, index)`: runs `[exe, f"--first={index}", f"--last={index}", "--list-test-cases"]` (short timeout, e.g. 30 s) and returns the one listed name, else None. Confirm on the real binary (`pixi run project_n build --mode release`, then run it by hand with `--first=3 --last=3 --list-test-cases`) that doctest's listing honours `--first/--last` in the same order as a run; if it doesn't, drop this function and name a timed-out case `case N` only, saying so in the report.
  - `run_references`: `env` gains `LP_NUM_THREADS` and `PROJECT_N_REFERENCE_WORKERS`, both `str(reference_share(min(jobs(), count)))` (set after `count` is known); the failed list names a timed-out case by `doctest_case_name(output) or reference_case_name(exe, env, index) or f"case {index}"`, calling `reference_case_name` only for cases whose output names none.
- [ ] **Step 4: Run** `pixi run project_n selftest`. Expected: all pass.
- [ ] **Step 5: Native check:** `pixi run project_n reference` passes (47 cases; pictures unchanged: lavapipe draws to the byte at any thread count).
- [ ] **Step 6: Docs:** `tools/AGENTS.md:47` gains, in the same style: each case's share of the cores (`reference_share`, as `LP_NUM_THREADS` and `PROJECT_N_REFERENCE_WORKERS`) and why (a 32-core container hit Podman's 2048-task cap; llvmpipe deadlocks waiting for rasterizer threads it couldn't create), and the `REFERENCE_CASE_TIMEOUT` bound naming a hung case.
- [ ] **Step 7: Gate and commit:** `pixi run project_n lint --range <task base>..HEAD`, then commit (git-commit skill; `build(tools)`-type subject such as "size each reference case's threads and bound its time").

### Task 2: Reference cases size `core::Jobs` from the harness

review: full · implementer: sonnet

**Files:**
- Modify: `tests/reference/reference_tests.cpp` (a helper in the anonymous namespace; every `core::Jobs jobs;`, about 11 sites)
- Modify: `tests/AGENTS.md:19` (the never-hang line)

**Interfaces:**
- Consumes: `PROJECT_N_REFERENCE_WORKERS` (set by Task 1's `run_references`).
- Produces: `std::expected<u32, std::string> WorkersFrom(const char* text)` and `u32 ReferenceWorkers()` (file-local).

- [ ] **Step 1: Write the failing test case** `TEST_CASE("reference workers come from the harness")` in `reference_tests.cpp` (no GPU, no renderer): `WorkersFrom(nullptr) == core::Jobs::DefaultWorkers()`; `WorkersFrom("1") == 1`; `WorkersFrom("3") == 3`; each of `""`, `"0"`, `"x"`, `"3x"`, `"-1"` gives an error whose text names `PROJECT_N_REFERENCE_WORKERS` and the value.
- [ ] **Step 2: Run** `pixi run project_n reference`. Expected: the build fails (`WorkersFrom` undeclared).
- [ ] **Step 3: Implement** `WorkersFrom` (parse with `std::from_chars` as `app.cpp`'s `WorkersOf` does: whole string, ≥ 1; nullptr → `core::Jobs::DefaultWorkers()`) and `ReferenceWorkers()` (reads the variable once with `std::getenv`; on an error, `FAIL(message)` so the case fails loudly). Every `core::Jobs jobs;` becomes `core::Jobs jobs{ ReferenceWorkers() };`. A comment on `ReferenceWorkers` says why: every case runs at once in its own process, so each takes its slice of the cores rather than all of them. If clang-tidy flags `std::getenv` (`concurrency-mt-unsafe`), fix it properly (read once before any thread starts) or suppress narrowly with the reason, per the root AGENTS.md rule.
- [ ] **Step 4: Run** `pixi run project_n reference`. Expected: 48 cases pass (the new one included). Then `PROJECT_N_REFERENCE_WORKERS=x` by hand on one case fails with the message (run the binary as `run_references` does, or temporarily through the tool).
- [ ] **Step 5: Docs:** `tests/AGENTS.md:19`: whole cases are bounded too (`REFERENCE_CASE_TIMEOUT` in tools/project_n.py), and the cases take their worker count from `PROJECT_N_REFERENCE_WORKERS` (the default when unset).
- [ ] **Step 6: Gate and commit:** `pixi run project_n test` (the registered suites still build and pass), `pixi run project_n lint --range <task base>..HEAD`, commit (`test(tests)`-type subject such as "take the reference cases' worker count from the harness").

### Task 3: Measure, set the bound, prove the container run

review: light · implementer: controller (main checkout, runs alone; no subagent: it needs the container and the whole machine)

**Files:**
- Modify: `tools/project_n.py` (`REFERENCE_CASE_TIMEOUT` value and its comment)
- Modify: `docs/superpowers/specs/2026-11-18-reference-hang-design.md` (Outcome: measurements)

- [ ] **Step 1: Native after:** `pixi run project_n reference` three times, warm, timed (`python3 -c` with `time.monotonic`, as the Before was). Expected: not slower than 10.1 to 10.4 s. If slower, stop and tell the user.
- [ ] **Step 2: Container after:** `timeout 1200 pixi run project_n reference --preset linux-gcc --container`, with a stall watchdog (log growth, `pgrep -fc reference_tests`). Expected: passes. Record its wall time and the cgroup's `pids.events` stays `max 0` (read it with `podman exec` while it runs, only on this project's container).
- [ ] **Step 3: Slowest case:** with a temporary, uncommitted timing print in `run_reference_case` (`time.monotonic()` around `subprocess.run`), one container run; the slowest case's seconds. Revert the print.
- [ ] **Step 4: Set** `REFERENCE_CASE_TIMEOUT` to about 10× the slowest case, rounded up, never under 60; its comment gives the measured figure and why the margin (a loaded CI runner, sanitizer-free release only).
- [ ] **Step 5:** `pixi run project_n selftest` passes; commit with the spec's Outcome measurements (before/after native, container wall time, slowest case, `pids.events`).

### Task 4: One TSan run of the reference cases

review: light · implementer: opus (triage of sanitizer reports needs judgment)

**Files:**
- Modify: `docs/superpowers/specs/2026-11-18-reference-hang-design.md` (Outcome: the TSan result and the decision)
- A throwaway runner script in the session's scratchpad, never committed.

- [ ] **Step 1: Build** `pixi run project_n build --preset linux-clang --mode debug --sanitizer thread` and find `reference_tests` in its tree (`build/linux-clang/debug-thread/...`).
- [ ] **Step 2: Run** every case, one process each, with `run_references`' environment (lavapipe ICD and loader from `pixi run -e lavapipe` prefix, `SDL_VIDEO_DRIVER=offscreen`, a private `XDG_RUNTIME_DIR` each, the share from Task 1, `SANITIZER_ENV`'s `TSAN_OPTIONS`), `--first=i --last=i --minimal=true`, from the repo root, at most `jobs() // 4` at once, each under a generous timeout (TSan and debug are 10–50× slower; e.g. 1800 s), output per case to the scratchpad. Watch for stalls.
- [ ] **Step 3: Triage:** for each TSan report, the top frames in our code (src/, tests/) versus only in Mesa, SDL, libc. Reports only inside uninstrumented libraries are noise; list them by library. A report in our code: write the reproduction down and stop: tell the controller, who tells the user before any fix (the scope grows; a fix goes test-first).
- [ ] **Step 4: Decide** with the evidence (reports in our code? noise volume? run time?) whether the gate should run the reference cases under TSan (a `check --sanitizers` step, CI, or never), and why; this is a recommendation for the user, recorded as such.
- [ ] **Step 5:** Write the spec's Outcome TSan part (build, cases run, time taken, reports by place, recommendation); commit (`docs(design)`).
