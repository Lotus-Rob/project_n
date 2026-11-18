# Reference hang: each case gets its share of the cores, a per-case timeout, one TSan run

Date: 2026-11-18 · Status: approved · Plan: `2026-11-18-reference-hang.md`

## Why

`check --container` hung for 65 minutes in `reference` on this host (32 cores), on main as well as on the warnings
branch: 20 of 47 case processes never finished, and nothing reported them. `subprocess.run` in `run_reference_case`
waits forever, so a hung case hangs the whole gate. Natively `reference` passes in about 11 s, and CI's container jobs
pass.

## History

- One process per case, all cores at once, was measured on the CI-speed branch: a single process kept lavapipe on 1.4
  of 32 cores (28 s for 25 cases), parallel runs took 4 s. That stays: this design only sizes each process's threads.
- Each case already has its own `XDG_RUNTIME_DIR` (commit "build(tools): give each reference case its own runtime
  directory"): cases starting at once raced to create Mesa's shared temp directory.
- tests/AGENTS.md already rules that a reference test never hangs (every wait has a bound that fails loudly); the
  harness had no such bound around a whole case.
- No Mesa bump or `LP_NUM_THREADS` setting was ever tried. Mesa is pinned at `mesa-lavapipe ==26.2.6` (pixi.toml).

## Root cause (reproduced on main, 2026-11-18)

Podman caps a container at 2048 tasks (`pids.max`). `reference` runs `jobs()` cases at once (32 here), and each
starts about 31 `core::Jobs` workers (`Jobs::DefaultWorkers()`, one per core) and up to 32 llvmpipe rasterizer
threads, plus Mesa's queue and disk-cache threads, Quill's backend and the main thread: well over 2048. The cgroup
refused 160 thread creations (`pids.events: max 160`). Every hung case had fewer than 32 `llvmpipe-N` threads (0 to
29). Their stacks: the main thread in `gfx::Renderer::Capture` → `SDL_CreateGPUTexture` → `lvp_CreateImageView`,
waiting on the queue's lock. Lavapipe's queue thread holds it in `lvp_execute_cmd_buffer` → `llvmpipe_fence_finish`,
waiting for a fence. The rasterizer threads that exist are idle. So llvmpipe waits for threads it never created.
Natively there is no 2048 cap; CI runs about 4 cases at once on about 4 cores, far under it.

## Why TSan didn't find it (verified)

- `check --sanitizers` runs `cmd_test` (debug builds of the registered tests) for each sanitizer, and fuzz seeds.
  `reference_tests` is built but not registered as a test (tests/xmake.lua), and `run_references` runs only the plain
  release binary (`binary_path(preset, "release", ...)`). The reference cases never ran under any sanitizer.
- Even if they had, the hang is not a data race. It is a refused `pthread_create` inside Mesa, which TSan doesn't
  instrument, and nothing ever signals the wait. Natively, where TSan runs, the cap isn't there, so the hang never
  happens.

## Goals

1. `reference` never deadlocks from thread exhaustion, natively or in a container, at any core count.
2. A hung case fails the run within a bounded time and is named, and the other cases' results are still reported.
3. One TSan run of the reference cases, to learn whether they find anything in our code, and a decision on whether
   the gate should run them so.

## Non-goals

- No Mesa bug report (the user, 2026-11-18). No Mesa version change.
- No change to how many cases run at once: the parallelism of the CI-speed decision stays.
- No change to the game's own worker count outside the reference cases.

## Approaches

1. **Each case gets its share of the cores (picked).** `run_references` gives each case
   `max(1, cores // cases running at once)` threads: `LP_NUM_THREADS` for llvmpipe and the same number of `core::Jobs`
   workers in the reference cases. That's about 3 threads of drawing and work per case instead of about 60, far under
   any cap, and it ends the over-subscription (32 processes × 60 threads on 32 cores).
2. **Raise Podman's limit** (`--pids-limit=-1` in `container_cmd`). One line, but a host or CI runner with a lower
   limit still deadlocks, and the over-subscription stays. Rejected.
3. **Run fewer cases at once.** Slower everywhere, and it undoes the measured parallelism. Rejected.

The user picked 1, with the timeout and one TSan run (2026-11-18).

## Design

- **Thread share.** `run_references` works out the share from `jobs()` and the number of cases actually run at once
  (`min(jobs(), count)`), and passes it to each case: `LP_NUM_THREADS` in the environment, and the worker count the
  reference cases give `core::Jobs`. When the reference binary runs by hand without it, the cases keep
  `Jobs::DefaultWorkers()`, so nothing changes outside the harness. The mechanism (an environment variable read by a
  helper in `tests/reference/`, or a doctest option) is the builder's call; one helper, used by every
  `core::Jobs jobs;` in `tests/reference/reference_tests.cpp`.
- **Timeout per case.** `run_reference_case` passes `timeout=` to `subprocess.run`. On expiry the case's process is
  killed and counts as failed, with a message naming it ("timed out after N s") and whatever output it printed. The
  other cases still finish and are reported. The bound is a named constant with a comment saying why it is that size:
  a generous multiple of the slowest case measured in the container after the thread share (my call: measure, then
  pick, about 10× the slowest case, never under 60 s). Self-tests in tools/test_project_n.py cover a case that times
  out (named, counted as failed, the others still reported) and the share passed to each case, with a fake
  subprocess, as the existing `run_reference_case` and `run_references` tests do.
- **Measurement.** Before and after: native `reference` wall time (today about 11 s) and `reference --preset
  linux-gcc --container` (today: hangs). The share must not make the native run slower; if it does, the plan comes
  back here.
- **One TSan run.** A thread-sanitizer build of `reference_tests` (linux-clang, the preset that supports `thread`),
  run case by case on lavapipe the way `run_references` does, natively, once. Mesa and SDL are uninstrumented, so
  reports inside them are noise to sort, not findings. Each report in our code is triaged: a real race is fixed
  test-first, as any bug in `src/` (if one turns up, the scope grows and the user is told before going on). The result
  and the decision (does the gate run the reference cases under TSan from now on, and how) go into this spec's
  Outcome. A one-off run doesn't need new `project_n` options; if the decision is to gate it, that becomes its own
  change.
- **Docs.** tools/AGENTS.md (the reference runner: share and timeout) and tests/AGENTS.md (the never-hang rule now
  also bounds whole cases; sanitizer coverage of the reference cases as decided) in the same commits. The backlog entry
  in the project memory is removed when this merges.

## Gates

`pixi run project_n test`, `lint`, then `pixi run project_n check` and `pixi run project_n check --container` from
the main checkout: the container gate is the one that hung, so it is this change's real test.

## Outcome

### Measurements (this host, 32 cores, 2026-11-18)

- Native `pixi run project_n reference`, warm: before 10.4 s and 10.1 s (15.4 s cold); after 8.5 s, 8.3 s, 8.5 s.
  With 48 cases on 32 cores every case runs on a one-thread share, and dropping the over-subscription made the run
  faster, not slower.
- `pixi run project_n reference --preset linux-gcc --container`: passed in 81.9 s wall (incremental container build
  included), where before it hung for 65 minutes. The container cgroup's `pids.events` stayed `max 0` throughout;
  `pids.current` peaked at 388 of the 2048 cap.
- Slowest case in that container run: 7.4 s (cases 5 to 9, 7.1 s to 7.4 s; one run, so one sample per case).
  `REFERENCE_CASE_TIMEOUT` is 80 s, about ten times that. A smaller CI runner gives each case one thread too, as on this
  host (4 cores, 4 cases at once), so a case's time there scales with its cores' speed and load rather than with the
  core count. That a tenfold margin covers this and the single sample is a judgement, not a measurement. The cases always run a release build, never a sanitizer one.

### ThreadSanitizer run (this host, 32 cores, 2026-11-18)

A one-off run by hand, outside the gate: the gate's reference step still runs the release build only, as above.

- Build: `pixi run project_n build --preset linux-clang --mode debug --sanitizer thread`, 26.8 s here (warm package
  and object caches; a cold one takes longer). Dependencies, Mesa and LLVM are not instrumented.
- Run: a throwaway runner with `run_references`' environment (Pixi's lavapipe ICD and loader, `SDL_VIDEO_DRIVER=offscreen`,
  a private `XDG_RUNTIME_DIR` per case), every case in its own process (`--first=i --last=i --minimal=true`), 8 at a
  time (`jobs() // 4`), each with a 4-thread share (`LP_NUM_THREADS` and `PROJECT_N_REFERENCE_WORKERS`), a 1800 s
  bound, and `TSAN_OPTIONS=halt_on_error=0:second_deadlock_stack=1` with a log per case.
- Result: all 48 cases ran in 55.5 s wall (6.7 s to 15.1 s each, against 7.4 s at most for the release cases in the
  container); none timed out and no picture check failed. 47 exited 66, TSan's code for "reports found"; the one that
  draws nothing ("reference workers come from the harness") exited 0.
- 227 reports, all data races, none in our code: in every report both racing accesses are inside Mesa's lavapipe or
  its LLVM, and on threads lavapipe started (its queue thread and its `llvmpipe-N` rasterizer threads), never on ours.
  Our code shows up only in the stacks where lavapipe started those threads or created their mutexes and memory, all
  while the device was made: always `RendererFor` (tests/reference/reference_tests.cpp), called from
  `CheckAgainstReference`, `CheckGlows`, `CheckNumbers`, `CheckShowcase`, `CheckRecoloured` and `CheckVolley` or
  straight from a test case. No frame of src/ appears in any stack.
  - `libvulkan_lvp.so`, 120: `lp_fence_destroy` 96 (2 in every drawing case: a fence's mutex and condition variable
    destroyed after a rasterizer thread last locked them), `_mesa_hash_table_copy` 24.
  - `libLLVM.so.23.1`, 107, in 23 cases: llvmpipe threads compiling shaders at once (`IEEEFloat::bitwiseIsEqual` 34,
    `StringMapImpl::LookupBucketFor` 23, `FunctionType::get` 20, `StringMapImpl::RehashTable` 19,
    `DenseMap<APInt, ConstantInt>` 6, `ConstantDataVector::isSplatData` 5).
  - The count changes from case to case (2 to 12) with how the threads happen to interleave. TSan sees only the
    libraries' libc calls, not their own atomics, so most of these are synchronisation it can't see; any real ones are
    Mesa's to fix, not ours.
- No report names a thread our job pool started, and no racing access has a frame in our code. TSan lists only the
  threads a report involves, so the logs don't show what our 4 workers per case did.

**Decision (the user, 2026-11-18, on this recommendation): the gate never runs the reference cases under TSan or any
other sanitizer, neither in `check --sanitizers` nor in CI; they stay a release build only;** tests/AGENTS.md records
the rule. The run is cheap (a minute plus the build), so cost is not the reason:
- Every drawing case fails on Mesa's noise. Gating it needs suppressions for whole libraries (`race:libvulkan_lvp.so`,
  `race:libLLVM.so`), and TSan suppresses a race if any frame of it matches, so those would also hide a race our code
  causes inside Mesa, which is where a rendering race of ours would show.
- TSan can't see the renderer's real risk, the CPU writing memory llvmpipe's threads are still reading: the reading
  side is uninstrumented code that doesn't go through libc. Vulkan's synchronisation validation is the tool for that.
- Our CPU-side threading (the job pool, the simulation) already runs under TSan in the unit tests, which
  `check --sanitizers` covers.
- This run found nothing in our code. A by-hand run like this one is worth repeating when the renderer starts sharing
  memory between threads on the CPU side.
