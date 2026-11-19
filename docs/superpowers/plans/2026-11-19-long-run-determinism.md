# Long-run determinism Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two skipped, on-request test cases that run the same long game on 1 and 4 workers side by side and check their hashes match all the way.

**Architecture:** Both games live in one test process. Task 1 extends the shipped world's hours-of-play measurement with a second, 1-worker host compared at each checkpoint. Task 2 adds a long tick run through `core::Logic` on the world map, driven by a seeded walk-to script, compared every `core::REPLAY_HASH_TICKS`.

**Tech Stack:** C++23, doctest (`* doctest::skip()` cases), XMake via `pixi run project_n`.

**Spec:** `docs/superpowers/specs/2026-11-19-long-run-determinism-design.md`

## Global Constraints

- On request only: both cases carry `* doctest::skip()`; nothing in `check`, CI or a plain `test` run changes.
- Compare runs of one build; never pin or commit a hash value.
- Worker counts: exactly 1 and 4 (`core::Jobs jobs{ 1 }`, `core::Jobs jobs{ 4 }`).
- No new tooling, no `project_n` command, no log files: hashes go to the log with `PN_LOG_INFO`.
- Code comments never cite the design process; a plain comment explains only why (root AGENTS.md).
- Commits: `test(game): ...` for the cases, `docs(...)` for docs (git-commit skill).

## Review Focus

1. A divergence must stop the run where it starts: the tick case uses `REQUIRE` on the hashes after each burst, and its failure message names the wall tick, so a hash mismatch 3 hours in doesn't scroll past as thousands of failed `CHECK`s.
2. The 1-worker host in Task 1 must not distort the logged month-step time: it steps outside the timed stretch.
3. A walk-to towards a place the cultivator can't reach is ignored by the host (logged at debug): the script must still move it, so destinations are the map's places (cities, all reachable), and the case requires the route to exist after the first walk-to.
4. Both sessions in Task 2 need their own save folders (`core::Saves` paths), or one's writes could clobber the other's.
5. The script's draws must not depend on either game's state (only on the seed, the walk-to's index), or a divergence would feed back into the input and hide itself.

## Waves

| Wave | Tasks | Needs |
|---|---|---|
| 1 | Task 1 | nothing |
| 2 | Task 2 | Task 1 on the branch (both edit tests/AGENTS.md and the spec's Outcome) |

---

### Task 1: The hours-of-play case compares a 1-worker host

review: full · implementer: sonnet

**Files:**
- Modify: `tests/unit/game/shipped_world_tests.cpp` (case "measure the shipped world's saves over hours of play", ~line 548, and the comment above it)
- Modify: `docs/superpowers/specs/2026-11-19-long-run-determinism-design.md` (Outcome)

**Interfaces:**
- Consumes: `game::Host::AdvanceCalendar(counters, jobs, stop)`, `Host::StateHash()`, `Host::WorldHash()` (as the case already uses them).
- Produces: nothing other tasks use.

- [ ] **Step 1: Give the case its worker counts.** Replace `core::Jobs jobs;` with `core::Jobs jobs{ 4 };` and add a second host `game::Host oneWorker(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());` with `core::Jobs oneJob{ 1 };`. Name the 4 as a constant (`MANY_WORKERS = 4`, as the file's 1-vs-4 month-step case does).
- [ ] **Step 2: Step the 1-worker host outside the timed stretch.** After the timed loop to each checkpoint, step `oneWorker` with the same counters for the same months (its own loop, untimed), so `stepMs` stays the 4-worker host's alone.
- [ ] **Step 3: Compare and log at each checkpoint.** Before the save measurement: `CHECK(oneWorker.StateHash() == host.StateHash());` and `CHECK(oneWorker.WorldHash() == host.WorldHash());`, then `PN_LOG_INFO("{} h: StateHash {:016x} WorldHash {:016x} on {} and 1 workers{}", ...)` with ` (alike)` or ` (DIFFERENT)`. Update the case's leading comment to say it also compares a 1-worker run's hashes at each checkpoint.
- [ ] **Step 4: Build and run the suite's plain run.** `pixi run project_n test --suite game` → passes (the case stays skipped).
- [ ] **Step 5: Run the case once.** `pixi run project_n test --mode release --target game_tests -- --test-case="measure the shipped world's saves over hours of play" --no-skip`, in the background with a stall watchdog (log growth; it ran under 3 minutes before, expect about double). Expected: every checkpoint logs `(alike)`, no failed check, sizes as in the relationships-bounded spec's Outcome.
- [ ] **Step 6: Record it.** Add an `## Outcome` section to the spec: the five checkpoints' hashes agreed (or not), the run time, the 1-worker step's cost if logged.
- [ ] **Step 7: Gates and commit.** `pixi run project_n test` and `pixi run project_n lint --range <task base>..HEAD` pass; commit `test(game): compare the hours-of-play run with a 1-worker one` (spec Outcome in a `docs(design): ...` commit).

### Task 2: An hour of walk-tos ticked on 1 and 4 workers

review: full · implementer: opus

**Files:**
- Modify: `tests/unit/game/replay_tests.cpp` (`Session` gains a worker count; new case at the end of the file's namespace)
- Modify: `tests/AGENTS.md:8` (the skipped-cases line)
- Modify: `docs/superpowers/specs/2026-11-19-long-run-determinism-design.md` (Outcome; Status → implemented)

**Interfaces:**
- Consumes: `Session` (this file), `core::Random(seed, stateCounter, stableId, purpose, draw)` and `core::ToBelow(random, bound)` (`src/core/random.hpp`), `core::WorldMap::Places()` and `TileCentre(TileIndex(place.tile))` (`src/core/world_map.hpp`), `test::ShippedWorld()`, `core::REPLAY_HASH_TICKS`.
- Produces: nothing other tasks use.

- [ ] **Step 1: `Session` takes a worker count.** Add a `u32 workers` parameter (default `WORKERS`) to the scene constructor, `Session(const std::filesystem::path& folder, core::SceneKey scene = game::ARENA_SCENE, const core::GameData& data = test::ShippedData(), u32 workers = WORKERS)`, initialising `jobs{ workers }` in the member-initialiser list (it is declared before `saves`, which takes it). Every existing caller compiles unchanged.
- [ ] **Step 2: Measure the tick cost.** Write the case (Step 3) with `TICKS` first set to 21,600 (6 minutes of play), run it in release (Step 5's command) and note the time per 1,000 ticks for both sessions together. Set `TICKS` to 216,000 (an hour) if that takes at most about 15 minutes, else the largest multiple of 21,600 that does; say which in the Outcome.
- [ ] **Step 3: Write the case** `TEST_CASE("the world ticked through an hour of walk-tos is the same on 1 and 4 workers" * doctest::skip())`:
  - Two sessions, `Session one(folderOne.Path(), game::WORLD_SCENE, test::ShippedData(), 1);` and `Session many(folderMany.Path(), game::WORLD_SCENE, test::ShippedData(), 4);`, each with its own `Folder`; `REQUIRE` both on `game::WORLD_SCENE` and alike after `Start()`.
  - Constants: `TICKS` (Step 2), `WALK_EVERY = 3'600` (a walk-to each minute of play), `LOG_EVERY = 21'600`, bursts of `core::REPLAY_HASH_TICKS`.
  - The script: before the burst that starts at a multiple of `WALK_EVERY`, walk-to index `k` sends both sessions `WalkTo(centre)` of place `core::ToBelow(core::Random(SEED, 0, 0, PURPOSE, k), places.size())`, a fixed `SEED` and `PURPOSE` named in the test; the draw reads nothing from either game. After the first walk-to's burst, `REQUIRE(many.host.PlayerRoute().has_value())`.
  - After every burst: `REQUIRE_MESSAGE(one.host.StateHash() == many.host.StateHash(), "parted by wall tick ", tick)`, the same for `WorldHash()` and `logic.Counters().wall`.
  - Every `LOG_EVERY` ticks: `PN_LOG_INFO` the wall tick, months passed (`host.Months()`), both hashes, and the walk-tos sent.
  - At the end: `CHECK(many.host.Months() >= TICKS / game::TICKS_PER_MONTH - 1)`, so month steps really ran through the logic.
- [ ] **Step 4: Plain run.** `pixi run project_n test --suite game` passes, the new case skipped; the existing replay cases still pass on the default worker count.
- [ ] **Step 5: Run the case.** `pixi run project_n test --mode release --target game_tests -- --test-case="the world ticked through an hour of walk-tos is the same on 1 and 4 workers" --no-skip`, in the background with a stall watchdog. Expected: passes, logs hashes alike at each checkpoint. A divergence is a determinism bug: stop and report it with the wall tick (superpowers systematic-debugging, test-first fix), never loosen the case.
- [ ] **Step 6: Docs.** tests/AGENTS.md:8 becomes: skipped `measure ...` cases (`shipped_world_tests.cpp`) and long-run comparisons on 1 and 4 workers (the hours-of-play case, `replay_tests.cpp`'s hour of walk-tos) are run by name with `--no-skip --mode release`, write only temp files, log their tables or hashes and are never gates. The spec's Outcome gains this run: ticks, months, time, checkpoints alike; Status → implemented.
- [ ] **Step 7: Gates and commit.** `pixi run project_n test` and `pixi run project_n lint --range <task base>..HEAD` pass; commits `test(game): tick an hour of walk-tos on 1 and 4 workers` and `docs(design): record the long-run comparisons` (tests/AGENTS.md with the test commit).
