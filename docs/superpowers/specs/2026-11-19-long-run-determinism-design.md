# Long-run determinism: the same long game twice, compared

Status: approved design, not yet implemented.

## Intent

The engine promises that one build, given the same state and inputs, plays the same on any machine and any worker
count (design doc 0002, goal 5 and §7.4). Nothing checks that over a long game: the longest runs compared today are 40
months on 320 NPCs, one month of the shipped world, and replays of a few hundred ticks. A divergence that needs hours
of play to appear (an order that drifts, a draw keyed on the wrong thing, a merge that depends on scheduling) would go
unseen until a player's replay or save broke. This adds long runs that compare two games as they go, run only when asked.

## Goals

1. The shipped world's month steps, run for hours of play, give the same `StateHash` and `WorldHash` on 1 worker and on
   4 at every checkpoint.
2. The game ticked through its logic for about an hour of play, with a seeded script of walk-tos on the world map, gives
   the same hashes on 1 worker and on 4 at every checkpoint, covering the per-tick systems month steps skip.
3. Each run logs its hashes at each checkpoint, so a failure says when the runs parted.

## Non-goals

- Comparing separate processes, or logs across builds: two games in one process already sit at different addresses
  and schedule differently.
- Anything in `check`, CI or the normal `test` run: these are on request only (the user, 2026-11-19).
- Pinned or golden hashes, or cross-platform determinism (0002 goal 5).

## Anti-goals

- New tooling: no `project_n` command, no log diffing.
- Slowing the gate: every case added here is skipped unless asked for.

## Approaches

- **A. Skipped doctest cases, each running both games in one process and comparing as it goes** (chosen, the user,
  2026-11-19): no tooling, the check is in the test.
- B. Separate processes, logs diffed by a new `project_n` command: catches only state leaking between runs in one
  process, unlikely here, for a command, its tests and docs.
- C. The long tick run in `headless_bench`: built optimised already, but has no hashes or checks, and would mix timing
  with correctness.

## Design

- **Month steps over hours of play:** the existing case "measure the shipped world's saves over hours of play"
  (tests/unit/game/shipped_world_tests.cpp, skipped) runs a second host on 1 worker beside its own on 4, steps both the
  same months, and at each checkpoint (1, 10, 50, 100, 250 h) checks both hashes alike and logs them. Its save
  measurement stays as it is. The backlog's separate 24-month case is dropped as my call: the extended case covers it
  10,000 months deep, and both are on request.
- **Ticks with walk-tos:** a new skipped case beside the replay tests (tests/unit/game/replay_tests.cpp) runs two games
  through the logic, unpaced, from the same start on the world map, one on 1 worker and one on 4. A seeded script sends
  the same walk-tos to both at the same wall ticks (destinations drawn from the seed, every so many ticks, so the
  cultivator crosses the map and month steps run through the logic). Every `core::REPLAY_HASH_TICKS` both hashes are
  compared; at each checkpoint they are logged. Its length is about an hour of play (216,000 ticks), set after measuring
  the tick cost so a run stays within about 15 minutes in release.

## Tests

Both cases are themselves the tests, skipped in every run: `--test-case='<name>' --no-skip`, in release (tests/AGENTS.md
gains their commands). Run each once on this branch and record the hashes' agreement, the run times and the tick cost in
the Outcome.

## What's left to the builder

- How the walk-to script draws destinations (reachable tiles only) and how often.
- Whether the replay tests' `Session` gains a worker count or the new case builds its own pair of logics.
- The checkpoint spacing within the tick run.

## History

- Backlog item 4b (the user, 2026-11-19) scoped three parts; part 2 is folded into part 1 here.
- Decided: determinism within one binary only, never pinned hashes (0002 goal 5, §7.4); fixed chunks merged in chunk
  order, the same for any worker count (§7.5); order is part of the state and saves never sort (§7.1); a calendar
  request ends its burst, so month steps sit at fixed wall ticks (0002.2 §2).
- Existing pieces: the 1-vs-4 month step on the shipped world (one month), npcs_tests.cpp's `Stepped` (40 months, 320
  NPCs), `UnlikeOnBothWorkerCounts` (tests/support/exactness.hpp), the replay `Session` helpers (`WalkTo`, `Burst`),
  `headless_bench` for the tick cost.
- Past divergences: the month step's wall tick depending on burst size (phase 4); saves sorting live domains, so a game
  that saved iterated unlike one that never did (5c1). The flaky `game_tests/replay` (once, 2026-11-16, under four
  parallel worktrees, never reproduced) may show itself in a long run; if it does, it is debugged, not retried.

## Outcome

### Hours-of-play case on a 1-worker host

The shipped world's hours-of-play case now steps a second host on 1 worker beside the 4-worker one and compares
`StateHash()` and `WorldHash()` at each checkpoint. One release run (linux-clang) took about 3 minutes, the first
case's own sizes unchanged, and at all five checkpoints (1, 10, 50, 100 and 250 h; 40 to 10,000 months) both hashes
agreed between 1 and 4 workers.

The 1-worker month step cost 6.5 ms at the first checkpoint and 8.7 to 9.0 ms after; the 4-worker step, timed on its
own, cost 4.5 ms and then 6.9 to 7.1 ms. The logged month step is now timed on exactly 4 workers, where it used the
default worker count before, so it is not directly comparable with the times in the bounded relationships spec's
Outcome.
