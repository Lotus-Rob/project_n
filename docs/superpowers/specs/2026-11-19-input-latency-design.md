# Input latency: the snapshot picked after the GPU wait, an adaptive display delay, a probe of many presses

Status: implemented 2026-11-19 (approved by the user 2026-11-19; Design 8 approved by the user 2026-11-19).

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

## Design 8: the handover keeps a snapshot that may still be due (added 2026-11-19)

Status: approved by the user (2026-11-19).

**Why.** Design 6's timeline harness ("test(core): drive frames on a synthetic timeline") found that at 60 Hz the render
time holds for up to a tick, then jumps about two. The state channel is newest-wins: `Handover::Publish` replaces a
snapshot the main thread hasn't taken, and `Display` takes one only when it holds none. When two publishes land between
two frames' clock readings, the first is never seen. That needs readings more than a publish interval apart, which 60 Hz
gives in two ways: at start-up (the first snapshot's frame reads the clock before its acquire, so the next reading comes
a period plus the acquire wait later, 20.17 ms), and in steady play whenever the frame's reading drifts into the band of
publish latenesses (frames and ticks differ by 3.3 µs a frame, so the phase lingers there). Example, D = 3 ms: the reading
at 3301.03 ms falls in tick 197's interval, but tick 198 (published at 3301.0) had replaced 197 (published at 3285.3,
after the previous reading), so the frame shows tick 196 at alpha 1 and the next jumps to 198. 165 Hz and unpaced runs
are immune: their readings are closer together than two publishes. The old frame order has the same gap at other
phases, so this predates the branch: design doc 0002 §5 sized the pool at four on the premise "at most one snapshot is
ever taken but not yet due", and nothing on record weighed a queue against replacing.

**Approaches.**
- **A. The handover queues two finished snapshots (picked by the user, 2026-11-19).** Snapshots wait in publish order,
  at most two; a third publish drops the oldest waiting one. The main thread takes them oldest first. Fixes start-up
  and steady play; one more buffer.
- B. The logic thread replaces a waiting snapshot only once the newer one is already due. Keeps four buffers, but the
  logic thread would need the main thread's adaptive D and clock, and it decides at publish time rather than at the
  frame's reading, so it races. Rejected.
- A change to `Display` alone can't work: the snapshot is gone before the main thread looks. A deeper queue (three or
  more) helps only main-thread hitches, which jump the picture anyway.

**Design.**
1. **Handover.** The waiting slot becomes a queue of up to two snapshots in publish order. `Publish` appends; with two
   already waiting it first returns the oldest to the free list (so a stalled main thread holds no backlog and the
   steady state still allocates nothing). `TakeState` returns the oldest waiting snapshot, or null. `BUFFERS` goes from 4
   to 5: one written, up to two waiting, up to two held by the main thread (drawn and held). The main thread still holds
   at most two, and the assert says so.
2. **Display.** `Update` repeats its step until it holds a snapshot that isn't due or none is waiting: adopt the held
   snapshot if due (returning the drawn one), then take the next waiting one if it holds none. So one reading adopts the
   newest due snapshot, returns any older one it skips undrawn, and holds the first one not yet due. The adoption rule
   is unchanged (0002 §6): a snapshot is drawn from when it is due, exactly when the one before it reaches alpha 1.
3. **Every taken snapshot counts once.** One `Update` may now take two snapshots. Each one whose tick advanced reports
   its lateness to the adaptive delay once, and each published after it was due counts as a late adoption, as today.
   *Amended 2026-11-20 (the user): only snapshots whose tick advanced count as late, the same rule as the delay's; see
   the Outcome.*
   The interface for that (`TakenLateness` giving up to two values, or `Update` handing them to the delay) is the
   builder's.
4. **Latency unchanged.** When a snapshot shows is still set only by D: the newest snapshot reaches the main thread as
   soon as it is published, as today. The only frames that change are the ones that hold today, which now show the
   snapshot that was lost, newer than the held picture.
5. **Stale events.** `Publish`'s drop of undrained cosmetic events rests on "the render clock reaches at least the newest
   tick minus one". With up to two waiting and one held, the drawn snapshot can be further behind the newest. The
   builder re-derives that bound and corrects the margin and its comments, so `Publish` never drops an event `PlayDue`
   would still play.
6. **Memory.** One more snapshot buffer: about 5.6 MB of instances at 100,000 of 56 bytes, plus the distance-field
   array; snapshots are culled to the view, so usually far less.

**Tests.**
- Handover: two publishes without a take give both, oldest first; three drop the oldest; buffers keep returning, so
  publishing never runs out. The test "the state channel hands over only the newest snapshot" asserts the old rule, so it
  is rewritten to the new one: a deliberate change of behaviour, not a loosened test. The threaded test keeps checking
  that snapshots arrive in order.
- Display: of two waiting snapshots, a due first and a not-yet-due second, the first is adopted and the second held;
  with both due, the second is adopted and the first returned undrawn; each taken snapshot's lateness is reported
  once, a frozen one's not at all.
- The timeline harness's "render time never goes back, holds or jumps" case passes at 60 Hz in all eight phases,
  start-up included, unchanged and with its bound unloosened; its latency cases still pass.

**Docs.** 0002 §5's state channel ("newest wins") and buffer count, A4's "newest-wins state", and the handover's class
comments describe the queue, with a line in the Revised row.

**Left to the builder.** The queue's representation, `TakenLateness`'s new shape, and the corrected stale-event margin,
each with its reason.

## Outcome

Implemented on branch `input-latency` in seven tasks, 2026-11-19 (the frame order is "fix(core): pick a frame's
snapshot after the swapchain wait", the adaptive delay "refactor(core): add an adaptive display delay", the queue "fix(core):
keep two waiting snapshots so a frame never misses a due one"). The plan's ledger (`.superpowers/sdd/`, gitignored) held
the rulings below; they are kept here with their reasons.

### Measured

`pixi run project_n measure --runs world` on the dev machine, 2026-11-20: releasedbg, fullscreen 2560x1440 at 165.1 Hz,
2 workers, unpaced, under Tracy (26.7 s, about 4,440 frames a run). Three runs: the first and second before the
late-adoption fix below (the first without `--keep`, so its game log and late count are gone), the third after it.

| | run 1 | run 2 | run 3 (branch head) | 5b2 (before) | 5a |
|---|---|---|---|---|---|
| Move press to its first drawn frame, 8 presses | median 12.2 ms, 10.9 to 14.1 | median 12.4 ms, 6.9 to 15.7 | median 12.2 ms, 11.8 to 16.5 | 31.1 ms (1 press) | 23.8 ms (1 press) |
| display delay D: at the end (range over the run) | 3.4 ms (2.8 to 10.0) | 3.4 ms (2.9 to 10.0) | 3.4 ms (3.1 to 10.0) | 10 ms, fixed | 10 ms, fixed |
| late adoptions | not kept | 1 (the month step's frozen snapshot) | 0 | 0 | |

Run 3's presses, in order: 11.8, 11.9, 12.7, 12.1, 16.5, 12.1, 15.6, 12.3 ms. About 10 ms of the 19 ms saved is D
falling from 10 ms to about 3 ms; the rest is the frame no longer drawing the old snapshot after the GPU wait (the
Evidence's 4.3 ms). The spread is the wait for the next tick, which 8 presses now sample at different phases instead
of one. Publish lateness over run 2 (from its Tracy capture): p50 1.45 ms, p99 2.29 ms, worst 2.63 ms outside the
month step, so D's 3.4 ms is the p99 plus 1 ms as designed. Frame times are unchanged in shape (median 6.0 ms in every
segment, p99 under 10 ms outside the month step, the month step's two main-thread hitches at 17.26 and 17.32 s in every
run); run 3 also had one in the widest view at 14.26 s (20.8 ms of work, 14.4 ms of it text layout's `Make`), on the
main thread, which this branch didn't touch. The follow segment's numbers are stop-start now (below), so not
comparable with earlier runs.

### Rulings and the builder's calls

- **The constants are the spec's, untuned:** window 600, 99th percentile, margin 1 ms, floor 2 ms, ceiling a tick, rise
  10% and fall 2% of elapsed time, start 10 ms, and D falls only after 60 observed snapshots (the first second after the
  start is slow and unrepresentative). Nothing measured asked for a change. The gate never resets: a load keeps the D the
  run settled on. The final review found the docs claiming it covered loads too; the wording was corrected rather than a
  reset added, as only falling is gated and a load's slow first ticks can only push D up.
- **Which snapshots count:** only those whose tick advanced; an early one counts as lateness 0. A frozen snapshot shows
  the transforms the one before it ends on at alpha 1, so its lateness can't show as a judder; and every expected stall
  (month step, save, load, replay toggle, waiting for a resident scene) publishes a frozen snapshot after the stall, so
  this one rule keeps all of them, the Evidence's 20.7 ms outlier included, out of D with no extra flag. Catch-up bursts
  after a hitch advance and count: their lateness is real.
- **Display owns the frozen-snapshot rule** (through the snapshot's `advanced`), not the caller: only Display sees each
  snapshot it takes, and a taken one may be held rather than drawn. Moving the rule would need a wider interface for no
  gain.
- **`PresentFrame` returning false is fatal to the frame:** an acquire may be left undrawn, so the caller must not present
  again; tested, also for a prepare failure in steady play.
- **Scene loading stays inside the frame, after the acquire:** a loading frame draws only the backdrop and holds the
  swapchain image for up to the 2 ms load budget. That adds to its CPU time instead of overlapping the wait, at worst one
  missed vblank on a slow machine during uploads; those frames are frozen, so D never sees them.
- **The cull margin** (`app/view.hpp`) now reasons from D's ceiling, a tick: 16.67 + 16.67 + 16.67 = 50 ms of zoom, 1.5
  notches, 0.117 of the view a side; with the pan's worst case 0.24, still inside the 0.25 margin, which stays.
- **The timeline harness** runs 60 Hz and 165 Hz with a 3.5 ms acquire wait and unpaced 1 ms frames, each at 8 phases
  `(2i+1)·TICK/16`, with steady lateness 1.0 to 2.0 ms (target exactly 3 ms). New order: worst latency margin +0.542 ms
  at 60 and 165 Hz, +0.042 ms unpaced. The old order (snapshot picked before the acquire) fails as required: −1.545 ms at
  60 Hz, and 5 of 8 phases at 165 Hz, worst −3.435 ms. The spike case checks D is back at 10.5 s, not 10: the window
  holds the spike for exactly 10 s and D then needs about 200 ms at 2% to fall, which "falls slowly afterwards" allows.
- **The 60 Hz snapshot drop** the harness found predates this branch (newest-wins handover); the user had it fixed here
  (Design 8). The queue is a two-slot array, oldest first, under the handover's mutex: a ring saves nothing at depth two.
  `Update` takes at most two snapshots a call, so the lateness store always has room, and a logic thread publishing
  during the loop can't keep one frame taking forever; a snapshot left by that bound is taken the next frame.
  `TakenLateness` gives one value a call, drained by `PresentFrame`'s loop; the final review had the harness's old-order
  control drain it the same way, as it read only the first of two.
- **The stale-event margin is 30 + 3 + 1 = 34 ticks** (was 31): with the queue the render clock may first reach undrained
  events by adopting the older of two waiting snapshots, up to a burst (3 ticks) behind the newest, drawn from its tick
  minus one.
- **Events go with each snapshot taken, in the same lock** (`TakeState` moves them to the main thread's side of the
  handover; `PresentFrame`'s drain after the takes hands them to `pending`); the stale drop relies on it. Task 7's review
  found the drain ran before `Display::Update`, and moving it after the takes still left a gap: the final review traced a
  publish landing between a take and the drain, after a stall, dropping an event about 28 ticks behind the render clock,
  which `PlayDue` would have played; no fixed margin covers k publishes in that gap. Draining under the take's lock closes
  it, so undrained events always belong to snapshots still waiting and the 34-tick bound holds. Both fixed test-first in
  this branch rather than deferred.
- **Late adoptions count only advanced snapshots** (the user, 2026-11-20, amending Design 8's item 3). The first
  `--keep` measure run logged 1 late adoption: the month step's frozen snapshot, published when its 26.8 ms stall ended
  (every other snapshot was under 2.7 ms late, p99 2.29 ms, from the run's Tracy capture). Before the queue the next
  tick's snapshot, published about 1.5 ms later, usually replaced it before a frame took it, so it was never counted;
  the queue keeps it. It moves nothing on screen, so counting it hid nothing real and broke "0 late adoptions"; the
  counter now follows the delay's rule (fixed test-first: "a late snapshot whose tick didn't advance doesn't count as
  late").
- **`game_tests`' `Taken()` helper** changed with the handover: it encoded the newest-wins rule.
- **The synthetic probe** (spec Design 6's fourth bullet, which the plan had left out; added after the final review):
  8 presses at 165 Hz, one at each of the 8 phases, give a median of 15.62 ms and a spread of 12.12 ms, checked against
  half a tick + D + a frame and the phases' span ± a frame. It sits above the game's 12.2 ms because the harness's acquire
  waits 3.5 ms every frame.
- **The probe's 8 presses** depend on the cultivator stopping dead and the 0.1 s stand outlasting a tick plus D plus a
  frame; all three runs above logged 8.
- **`measure`'s delay pattern** reads only non-negative finite numbers: D is clamped to [2 ms, a tick], so the log never
  prints anything else.
- **Not comparable with earlier runs:** the follow segment now walks stop-start (0.4 s walking, 0.1 s standing), so its
  smoothness numbers (frame percentiles, hitches, shimmer) can't be set beside runs before this branch.
