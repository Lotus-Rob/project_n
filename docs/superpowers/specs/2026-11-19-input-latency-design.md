# Input latency: the snapshot picked after the GPU wait, an adaptive display delay, a probe of many presses

Status: approved by the user (2026-11-19), not yet planned.

## Intent

A Move press takes 23 to 31 ms to show on screen (`measure`'s world run: 23.8 in chunk 5a, 24 then 31.1 in 5b2), and
combat feel work (chunk 7) needs that as low as smoothness allows, measured well enough to tell a change from noise.
The backlog's reading ("a whole tick lost on every press") was wrong: no tick is lost. This branch removes the real
loss, makes the display delay as small as each machine allows, and makes the probe report many presses.

## History

- Design doc 0002 §6 and A3: ticks at 60 a second, rendering uncapped, snapshots interpolated from previous to current
  transforms; a snapshot is adopted once due (`now >= scheduledNs + D`), exactly when the one before reaches alpha 1,
  so motion never judders; D "proposed: 10 ms, then set from the measured 99th percentile of tick run time plus
  publish". That tuning was never done: `core::DEFAULT_DISPLAY_DELAY_NS` is still the provisional 10 ms.
- An earlier fix already moved alpha's clock reading after the swapchain acquire (`tests/unit/core/frame_tests.cpp`:
  "a frame reads the time for alpha after the swapchain acquire"), but left the snapshot's choice before it.
- The latency probe (`app::MoveLatency`, `src/app/cast.cpp`) takes one sample per run: the press that starts the camera
  path's follow segment. 5b2's spec recorded 31.1 ms "as observed", cause not looked for. The label-stack fix it was
  blamed on touched no input or tick path.
- Floated on 2026-11-17 and not built: pumping SDL events more than once a frame, and SDL3's event timestamps to assign
  a press to the tick it happened in (accuracy, not speed). Still out of scope (Non-goals).

## Evidence (the kept capture `build/measure/20261117-003212/world.tracy`, 165 Hz, paced, 2 workers)

The 31.1 ms press, from Tracy's zones:

| Stage | ms | |
|---|---|---|
| Waiting for the next tick | 10.1 | 0 to 16.7, inherent at 60 ticks a second |
| The display delay D | 10.0 | designed; the snapshot is due 10 ms after its scheduled time |
| Snapshot picked a frame late | 4.3 | `Display::Update` ran 2.2 ms before the snapshot was due, then `WaitForGpu` blocked 3.6 ms; the frame drawn after that wait still showed the old snapshot at alpha 1 (no movement); the new one was adopted only the next loop |
| GPU wait, draw and submit of the frame that moved | 6.8 | the probe stamps after `Draw` and `SubmitFrame` |

Tick plus publish (`RunBurst` start to the logic's next wait), both kept world captures: p50 1.5 to 2.2 ms, p99 2.3 to
2.9 ms, one 20.7 ms outlier (the month step, which restarts the clock); 0 late adoptions in either run. The minimum-spec
CPU isn't recorded, so no fixed D can be justified for slower machines.

## Goals

1. A frame picks its snapshot from the same clock reading its alpha uses, taken after the swapchain acquire: a snapshot
   that becomes due while the acquire waits is the one drawn. This removes up to one frame period from every press
   (6 ms at 165 Hz, 16.7 at 60) and the hitch where a frame holds the old snapshot at alpha 1 and the next jumps.
2. D adapts at run time to the machine and scene: the smallest delay that keeps snapshots arriving before they are due.
3. `measure`'s world run reports the input latency over many presses (median and spread) and the display delay D
   reached, so a change of a few milliseconds can be told from the phase between press, tick and frame.

## Non-goals

- SDL event timestamps, pumping events more than once a frame, input on another thread (backlog: SDL forbids it).
- Changing the tick rate, interpolation or the adoption rule itself (0002 §6 stands; only D's value moves).
- A latency gate: `measure` stays a report, never in `check` or CI.

## Anti-goals

- Any judder: D falling below what ticks need, or changing fast enough to see.
- Anything the simulation sees: D is presentation only, never hashed, saved, recorded or sent to the logic.

## Approaches (for D; parts 1 and 3 have one sensible way)

- **A. A smaller fixed D (about 5 ms).** Simple; saves about 5 ms a press on the dev machine, but a guess for slower
  machines, where ticks running past it would judder on every late tick.
- **B. D adapts at run time (picked by the user, 2026-11-19).** The main thread watches how long after its scheduled
  time each snapshot arrives (`publishedNs - scheduledNs`, which every snapshot already carries) and follows a high
  percentile of the recent ones plus a margin, between a floor and one tick. Every machine and scene gets the smallest
  D that stays smooth; more code, and every place that computes alpha must use the current D.
- Rejected: D as a setting (players can't judge it; accessibility settings never trade smoothness for speed silently).

## Design

1. **Order of a frame.** Acquire the swapchain texture first, then read the clock once, take D for that time, update the
   display (adopt what is due) and compute alpha, all from that one reading. `core::PresentFrame`'s test file gains a
   case: a snapshot that becomes due during the acquire is the one drawn, at its alpha. How `App::Present` is reordered
   around it (scene loading, scene reports, the hidden window, the first frame before any snapshot, `Renderer::Acquire`
   asserting it isn't called twice without a draw) is the builder's.
2. **The adaptive delay**, a small pure class in `core` (no clock of its own, as `TickClock`): it is told each snapshot's
   lateness (`publishedNs - scheduledNs`) when the display takes it, and gives D for a time. Its target is a high
   percentile of a recent window plus a margin, clamped to [floor, `TICK_NS`]. D starts at today's 10 ms and moves
   toward the target continuously, never jumping: it rises fast (a late snapshot freezes the picture anyway) and falls
   slowly. Because the held and drawn snapshots are judged by the same D at each moment, adoption still happens exactly
   when the drawn one reaches alpha 1, so a moving D changes only the render clock's rate, briefly and by a few percent
   at most, never its continuity. Values to start from (the builder may tune them, recording why): window about 10 s
   (600 snapshots), 99th percentile, margin 1 ms, floor 2 ms, falling at most 2% of elapsed time, rising at most 10%.
   The builder decides which snapshots count (frozen ticks, the first after a month step's restart, catch-up bursts
   after a hitch) and says why.
3. **Every alpha uses it.** `App::Present`'s frame, `App::Click`'s pick, and `Display::Update` take the current D;
   `DEFAULT_DISPLAY_DELAY_NS` remains only as the start value (renamed if that reads better), and `app/view.hpp`'s
   cull-margin reasoning, which cites it, uses the ceiling (one tick) or is re-checked.
4. **Logging.** Outcomes only: at shutdown, D's final value and its range over the run beside the late-adoption count;
   no per-frame lines.
5. **The probe.** In `--camera-path`'s follow segment the scripted Move walks 0.4 s and stands 0.1 s, repeated: 8 presses
   where there was 1 (the stand outlasts tick + D + a frame, so each press starts from a standing snapshot). Each press
   logs its latency as today. `measure` reports their median and spread, and the run's D, in its table. Builder's call
   recorded here: the follow segment's smoothness numbers become stop-start, so they aren't comparable with runs before
   this branch; the spec's Outcome says so.
6. **Synthetic tests (the user, 2026-11-19: "ensure its tested synthetically").** Beside the unit cases, a doctest
   timeline harness with a fake clock and no window or GPU drives the real `core::Handover`, `core::Display`,
   `core::PresentFrame` and the adaptive delay through a synthetic schedule: ticks every `TICK_NS` with a chosen publish
   lateness each, frames at a chosen period with a chosen acquire wait, and presses that move a followed point from the
   next tick on (as `Host::Walk` does). It checks, over small representative sets of frame periods (60, 165 Hz,
   unpaced), acquire waits and press phases, never sweeps:
   - latency: a press's first frame showing movement comes no later than the wait for its tick plus D plus one frame
     period after it; with the old order (snapshot picked before the acquire) the same schedule loses up to a frame,
     so the case fails on today's code;
   - smoothness: the drawn render time (the drawn snapshot's tick less one plus alpha) never goes back, and never holds
     or jumps across an adoption, also while D moves;
   - the delay: steady lateness brings D to its percentile plus margin; a lateness spike raises it with no late adoption
     once risen; it falls slowly afterwards; floor and ceiling hold;
   - the probe: a run of synthetic presses gives the median and spread `measure` reports.
7. **Docs.** 0002 §6 revised: D adapts at run time (its rule, floor, ceiling and why), with a line in the Revised row;
   src/AGENTS.md's `--camera-path` description and tools/AGENTS.md's `measure` lines updated.

## Done when

- The new tests pass (frame order; the adaptive delay with small representative cases: steady lateness, a late spike,
  slow fall, floor and ceiling, continuity of alpha across adoption while D moves; the synthetic timeline harness of
  Design 6, whose latency case fails before the reorder); `check` passes.
- A `measure --runs world` on the dev machine reports 8 latencies with median and spread, and D, against 31.1 ms today,
  with 0 late adoptions; the numbers go in this spec's Outcome.

## Left to the builder

The reorder's details in `App::Present`, the adaptive delay's exact constants and which snapshots it counts (within the
bounds above, with reasons), where `measure` prints the new numbers.
