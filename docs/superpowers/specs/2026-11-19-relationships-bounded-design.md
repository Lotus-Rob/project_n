# Relationships bounded: NPCs forget the people they met longest ago

Status: approved design, not yet implemented.

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
   then at most about 50,000 × 10 edges for the shipped world, about 12 MB raw.
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

- **A last-met month on each edge.** `Relationship` gains `lastMet` (u32), the world's `months` count at the month step
  that last added to the edge. An edge is then 24 raw bytes, not 20 (Goal 1's 12 MB assumes 24).
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
