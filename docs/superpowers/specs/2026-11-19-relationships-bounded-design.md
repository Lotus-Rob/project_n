# Relationships bounded: NPCs forget the people they met longest ago

Status: implemented.

## Intent

5c1's measurement of saves over hours of play found that nothing caps or forgets a relationship. Every Socialise
meeting adds an edge (about 3,700 a month), so the table holds 37.4 million edges at 250 h. That is 99% of the raw save
(748 of 755 MB) and nearly all of its 64.8 MB on disk. The raw save passes `MAX_SAVE_BYTES` (1 GiB) near 360 h, after
which the game can't save at all, and the month step slows from 4.5 to 65.6 ms. This branch bounds the table.

It is a stopgap. Relationships stay a placeholder until the game design says what they are. The user expects them to
split later into a log or journal of socialising plus a short list of roles on each NPC (friends, enemies, partner,
master, apprentice). Until then they take the travel journal's shape: a fixed number per NPC, more once the player has
met it, the oldest dropped first. The limits must be easy to change.

## Goals

1. Each NPC holds at most `RELATIONSHIP_LENGTH` relationships (10), or `MET_RELATIONSHIP_LENGTH` (50) once it has `Met`,
   the same numbers the travel journal keeps (`JOURNAL_LENGTH`, `MET_JOURNAL_LENGTH` in `game/travel.hpp`). The table is
   then at most about 50,000 × 10 edges for the shipped world, about 14 MB raw.
2. When an NPC is over its limit, it forgets the subjects it met least recently.
3. Each limit is one named constant, and changing either one later orphans no save.
4. 5c1's skipped case `measure the shipped world's saves over hours of play` (tests/unit/game/shipped_world_tests.cpp),
   run again, shows the relationships' part of the save flat once NPCs reach their limits (about 3 to 4 hours of play,
   at about 0.07 new edges per NPC a month), and the month step no longer growing with play time.

## Non-goals

- What relationships mean: opinion stays a count of meetings, and the Socialise model stays as it is.
- Limits as data (YAML): the user chose constants for this stopgap (see Approaches).
- Anything that reads relationships: travel following relationships and the UI's relationship view stay in the backlog.
- Folding, materialising and `Relationships::Forget` (an NPC leaving the state): unchanged.

## Anti-goals

- A limit that depends on the job system's worker count, or on anything but the state: the result must stay deterministic
  within one binary, as every month step is.
- A save refused because it holds more relationships per NPC than today's limits allow.

## Approaches

The user chose the journal's pattern (a cap per NPC, larger once met) over decay of weak ties. Decay alone would keep
nothing worth keeping: a Socialise month picks its partner at random from the thousands on the NPC's tile, so two NPCs
almost never meet twice, nearly every opinion is 1, and every edge would fade at the same pace.

Where the limits live, the user picked 1:

1. **Named constants in code** beside the journal's: a one-line edit and a rebuild to change. The smallest branch, and
   it leaves the later redesign free. Chosen.
2. **Hashed game data in YAML**, reloaded live in debug builds like `abilities.yaml`: tunable without a rebuild and by
   mods, but a new file with its reader, refusals and fuzz seeds for two numbers that will be replaced. It can't go in
   `world.yaml`, which is part of a save's base identity (`GameData::BaseHash()`), so an edit there would orphan every
   save. Rejected for now; data-driven limits belong with the real relationship design.

## Design

- **A last-met month on each edge.** `Relationship` gains `lastMet` (u64, as the world's `months` count is), that count at the
  month step that last added to the edge. An edge is then 28 raw bytes, not 20 (Goal 1's 14 MB assumes 28).
  Dropping by opinion instead wouldn't work: nearly every opinion is 1, so a new acquaintance would push out an old one
  by ID, not by time.
- **The limits.** `RELATIONSHIP_LENGTH = 10` and `MET_RELATIONSHIP_LENGTH = 50` in `game/npcs.hpp`, documented as
  following the journal's. The code that trims takes the limits as a parameter (a small struct whose defaults are the
  constants), so tests can use small or lowered limits, and a later design can feed them from data without reshaping
  the code.
- **Trimming in the month step.** After `Relationships::Merge` adds the month's meetings (stamping each touched edge with
  the month), every holder over its limit keeps its most recently met subjects. Ties break by the higher opinion, then
  by the lower subject ID, so the result depends on nothing but the table. A holder's limit is the met one when it has
  `Met`, in whichever domain it is. Trimming covers every holder, not only those touched this month, so a lowered limit
  takes effect at the next month step (Goal 3).
- **Saves.** The edge's new field is saved, loaded and hashed, so the world section's `WORLD_VERSION` goes from 5 to 6
  (older saves are refused as any other version is; `GENERATOR_VERSION` stays, since generation makes no
  relationships). `Relationships::Load` checks `lastMet` is at most the loaded calendar's `months`. It never refuses a
  holder for having more edges than today's limits (anti-goal); the next month step trims them.
- **Docs.** Design doc 0002's data-layout line and src/AGENTS.md's relationships wording name the bound. The 5c1 spec's
  Outcome stays as written: it records what was measured then, so it gets one line pointing to this spec's Outcome.

## Tests

Small unit tests in `tests/unit/game/npcs_tests.cpp`, test-first (tests/AGENTS.md): a holder past its limit keeps the
most recently met; a `Met` holder keeps the larger number; the tie-break order; a lowered limit trims at the next step;
the result is the same for different worker counts; a save round-trips `lastMet`, and a load accepts a holder over the
limit but refuses a `lastMet` past the calendar. Then Goal 4's measurement, its tables in this spec's Outcome beside
5c1's figures.

## What's left to the builder

Where `lastMet` is stamped (inside `Merge`, given the month, or by the caller), how `Met` is looked up for the trim (a
predicate or the state), and whether trimming is a separate pass or part of `Merge`'s merge loop, as long as it stays one
linear pass over the table.

## History

- The cause is the placeholder needs model: Socialise (`game/npcs.cpp`, `LiveMonth`) emits one (partner, self) per
  meeting, and `Relationships::Merge` adds 1 per pair, creating missing edges. Only `Forget` (an NPC leaving the state,
  used by `Fold`) ever drops an edge.
- Nothing in `src/` reads `opinion`, `Of()` or `All()` yet; the planned readers are the backlog's travel following
  relationships and the UI chunk's relationship view (design doc 0002 §7, §8.14). A per-holder limit keeps `Of(holder)`
  a short span for both.
- Design doc 0002 §7.3: effects on others are commands merged in (target, source, emission) order, whatever the worker
  count; relationships are one edge table sorted by holder, by stable ID; met NPCs never fold back, so edges naming
  them never dangle.
- 5c1 (spec `2026-11-16-phase6-chunk5c1-world-kept-delta-saves-design.md`, "Saves over hours of play" and R23) measured
  the growth and fixed nothing, by the user's ruling (2026-11-19): this work gets its own branch, right after 5c1.
- No cap or decay was tried or rejected before.

## Outcome

The case `measure the shipped world's saves over hours of play` (skipped, release, linux-clang, unchanged) ran again on
the bounded table, with one `Host::AdvanceCalendar()` per month on the usual workers, as in 5c1. Every checkpoint's load
matched the saved `StateHash` and `WorldHash` (asserted). The run took under 3 minutes, against about 8 before. Times are
good to about 50% between runs (5c1's finding) and sizes are exact. The tables use 5c1's layout; 5c1's 250 h figures are
beside them.

| | 1 h | 10 h | 50 h | 100 h | 250 h | 5c1's 250 h |
|---|---|---|---|---|---|---|
| months | 40 | 400 | 2,000 | 4,000 | 10,000 | 10,000 |
| **raw save** | 8.87 MB | 17.7 MB | 17.7 MB | 17.7 MB | 17.7 MB | 755 MB |
| **on disk (zstd)** | **1.75 MB** | **4.00 MB** | **4.08 MB** | **4.12 MB** | **4.25 MB** | 64.8 MB |
| a whole (non-delta) save, raw | 15.2 MB | 25.2 MB | 25.3 MB | 25.3 MB | 25.5 MB | 763 MB |
| a whole save, on disk | 2.28 MB | 4.58 MB | 4.64 MB | 4.70 MB | 4.84 MB | 65.2 MB |
| save: serialise + write | 38.7 + 15.7 ms | 49.9 + 30.8 ms | 49.1 + 30.0 ms | 49.3 + 30.2 ms | 49.7 + 30.2 ms | 688 + 1,078 ms |
| load (regeneration included) | 56.7 ms | 67.9 ms | 61.7 ms | 61.2 ms | 65.2 ms | 1,369 ms |
| month step, mean over the stretch since the last checkpoint | 3.7 ms | 5.9 ms | 6.0 ms | 6.0 ms | 6.0 ms | 65.6 ms |

Parts of the raw save (bytes):

| part | 1 h | 10 h | 50 h | 100 h | 250 h |
|---|---|---|---|---|---|
| game world: changed fields (50,000 NPCs) | 2,222,619 | 2,222,386 | 2,222,147 | 2,222,399 | 2,222,340 |
| game world: created NPCs, gone NPCs | 0 | 0 | 0 | 0 | 0 |
| game world: storage order | 5 | 5 | 5 | 5 | 5 |
| journeys gone from the base's | 41,176 | 41,176 | 41,176 | 41,176 | 41,176 |
| journeys by plan | 265,088 | 283,448 | 282,608 | 281,768 | 289,248 |
| journeys written whole | 8 (none) | 8 | 8 | 8 | 8 |
| journals | 1,510,368 | 4,672,672 | 4,673,480 | 4,673,480 | 4,673,480 |
| **relationships** | 4,829,280 | 10,471,672 | 10,471,728 | 10,471,728 | 10,471,728 |
| header, scene, combat | 406 | 406 | 406 | 406 | 406 |

What is behind them:

| count | 1 h | 10 h | 50 h | 100 h | 250 h |
|---|---|---|---|---|---|
| NPCs alive (base 50,000), created, gone | 50,000, 0, 0 | 50,000, 0, 0 | 50,000, 0, 0 | 50,000, 0, 0 | 50,000, 0, 0 |
| NPCs with a changed field | 50,000 | 50,000 | 50,000 | 50,000 | 50,000 |
| journeys under way (base's gone: 5,146) | 6,627 | 7,086 | 7,065 | 7,044 | 7,231 |
| journal entries | 102,465 | 486,719 | 486,820 | 486,820 | 486,820 |
| relationships | 172,474 | 373,988 | 373,990 | 373,990 | 373,990 |
| holders: none, 1 to 9, exactly 10 | 4,268, 45,178, 554 | 3,680, 11,311, 35,009 | 3,680, 11,311, 35,009 | 3,680, 11,311, 35,009 | 3,680, 11,311, 35,009 |

What it shows:

1. **The table is bounded.** From 10 h on it holds 373,990 relationships (10.47 MB raw), 75% of the 14,000,008 bytes the
   limits allow (50,000 NPCs at 10 edges of 28 bytes) and 59% of the raw save, not 99%. At 250 h the raw save is 17.7 MB
   against 755 MB, 4.25 MB on disk against 64.8 MB, 6.0 ms a month step against 65.6 ms, 80 ms to save against 1.8 s,
   and a 65 ms load against 1,369 ms. `MAX_SAVE_BYTES` is no longer in reach.
2. **The month step and the save are flat.** The step went 3.7, 5.9, 6.0, 6.0, 6.0 ms and the save's size 17.69, 17.69,
   17.69, 17.70 MB from 10 h to 250 h. The journeys' plan (283,448, 282,608, 281,768, 289,248 bytes) moves up and down
   with the month: noise.
3. **What the settled count is made of.** The case now also logs the NPCs by how many relationships they hold. From 10 h
   on, 35,009 NPCs hold exactly 10, 11,311 hold 1 to 9 and 3,680 hold none (nobody holds more; nobody is met, so 10 is
   the limit for all). The count (373,990 = 35,009 x 10 + 23,900 in the middle group) did not change from 50 h to 250 h,
   nor did the three groups. So the table is not full: 30% of NPCs sit below the limit or at none, and they stay there,
   which an NPC that was still being met would not. At 1 h the groups were 4,268, 45,178 and 554.
   The code points to why, but I did not measure it. `LiveMonth` draws a Socialise partner only from the NPCs in the
   same cell, and the edge is made for the one drawn (`met.emplace_back(partner, self)` makes the partner the holder), so
   an NPC is met only by those in its own cell. An NPC alone in its cell can't be drawn and meets nobody (its own
   Socialise turns into Rest), so it holds none; one in a small cell can hold at most as many subjects as the cell has
   other NPCs, so below 10 when it has fewer. The 3,680 would be the lone ones and the 11,311 those in small cells. I did
   not log cell sizes, so this is a reading of the code that the numbers fit, not a result.
4. **Goal 4's "about 3 to 4 hours" is neither confirmed nor refuted.** The count stopped growing somewhere between 1 h
   (40 months, 172,474) and 10 h (400 months, 373,988); the checkpoints can't say which month. The fill time of the NPCs
   that do reach 10 is not measured either, and the 0.07 new edges per NPC a month the spec used was not checked.
5. **The delta still pays a little.** Against a whole save it saves 42% raw and 23% on disk at 1 h (1.75 against
   2.28 MB), 31% and 12% at 250 h (4.25 against 4.84 MB). The NPCs' part (2.2 MB) and the journals (4.67 MB) are now
   most of the rest.

The times are from a second run of the case, after "test(game): log holders by edge count in the long-play save
measurement" added the holders' counts; every size matched the first run's byte for byte, and the times differed by a few
percent.

A third run went on to 1000 h (40,000 months), with a 1000 h checkpoint added to the case's list for that run only (the
case still stops at 250 h). Every load's hashes matched again, and the 1 h to 250 h sizes matched the second run's byte for
byte. At 1000 h the raw save is 17.69 MB (17,692,006 bytes) and 4.17 MB on disk; a whole save is 25.3 MB raw and 4.79 MB
on disk. The relationships are the same 373,990 (10,471,728 bytes), held by the same 3,680, 11,311 and 35,009 NPCs, and
the journals the same 486,820 entries (4,673,480 bytes). Saving takes 49.2 + 29.7 ms, loading 60.5 ms and a month step
6.0 ms. The on-disk size went down from 250 h's 4.25 MB, so it doesn't grow steadily with play time. It moves with the
journeys' plan (282,808 bytes at 1000 h against 289,248 at 250 h), but not only with it: 50 h and 1000 h have nearly the
same plan bytes (282,608 and 282,808) and differ by 93 KB on disk (4,077,845 and 4,170,869 bytes), so the compressed
content of the other parts shifts too as the months go on. The raw sizes, which the limits bound, don't move. The 30,000
months from 250 h to 1000 h took about 3 minutes.

### Rulings

- The spec's "the same result for any worker count" is tested by running the month-step limit case, which reaches the
  limit, with one worker and with many and comparing whole tables. The existing 5,000-NPC worker-count case never reaches
  the limit, so it never trims.
- The world save fuzz seed now carries two NPCs and one relationship edge, so seeds exercise the edge layout and the
  last-met check. A month step on the small map makes no edges, so the delta seed couldn't carry one.
