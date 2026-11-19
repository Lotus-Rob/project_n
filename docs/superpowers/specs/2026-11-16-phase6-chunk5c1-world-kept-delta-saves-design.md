# Phase 6, chunk 5c1: the world kept, and saves as deltas

Date: 2026-11-16 · Status: implemented 2026-11-19 (approved 2026-11-16) · Chunk spec: `2026-11-10-phase6-chunk5-world-map-design.md` (its 5c, split here) ·
Phase spec: `2026-11-10-phase6-design.md` · Builds on: `2026-11-15-phase6-chunk5b2-moving-design.md` (its Outcome)

## In short

- **The world is made once.** A new game generates the world from the seed and the world data once; a load regenerates
  the same base and applies the save to it. A scene change never destroys and recreates the world: its NPCs, their
  months, relationships, journeys and journal, the calendar and the cultivator's place on the map carry on behind
  whatever scene runs, and come back as they were.
- **A save is a delta.** It holds only what differs from the regenerated base world, field by field, plus what the base
  doesn't have (new entities, removed ones, the records the months added) and the running scene. A new game saves next
  to nothing; a long game saves what play changed.
- **A load is exact.** Loading gives a state identical to the one saved (same hash, same order of every entity), and
  everything deterministic carries on as if the game had never been saved: the same ticks, month steps, journeys and
  draws.
- **Split from 5c:** 5c1 is this; **5c2** keeps 5c's enter prompt and stand-in town, in 5c's old place.

## Intent

The user, 2026-11-16: "we should definitely only try and generate it once during gameplay, however we shouldn't destroy
and recreate things like the travellers"; the world and saves made "from seed one time during game load and then for
maintaining state and saves use the delta serialization with an efficient, probably binary/compressed, save format";
"We should explore this as a chunk of work and refactor before we go too far". What it is for, in the user's words:
"Never recreating the world on scene changes, and small fast saves". The condition on the approach: "as long as this
will keep NPCs in the same place and following the same routine. Loading a save should restore the game to an
identical, deterministic point and continue all deterministic behaviours the same".

Why now: every scene build replaces the whole state (`Host::Build`), so since 5b2's travel, leaving the world map after
a month step and coming back shows different travellers (5b2's ruling R11e narrowed its Review Focus 5 for that), and
each chunk built on the rebuild (chunk 6's towns, folding visitors in and out) makes the change bigger.

Reference the user gave: Sven van Huessen, "Delta serialization" (svenvh.nl, 2026-03-13). It writes a JSON object's
fields only where they differ from a default-constructed one, so code defaults aren't frozen into old files (24% to
73% smaller scene files). Here the baseline is the regenerated world rather than a default object, and what the
article leaves out (a base from a generator, created and removed entities, generator changes, binary layout) is
designed below.

## Approaches

**How saves get small** (the user picked 1, 2026-11-16):
1. **A delta against the seeded world** (picked): a load regenerates the base from the seed and data, then applies the
   saved differences, field by field, keyed by stable ID, plus created and removed entities. Smallest saves (a new game
   is almost empty; afterwards the size follows what play changed). Cost: a save means something only with the
   generator and world data that made its base, so a save records both and is refused on a mismatch; before a release,
   that needs a policy (migrations, or keeping old generators).
2. Better full snapshots: components as columns, no per-entity overhead, a stronger compressor, world and scene in
   separate files. Survives data and generator edits; size follows the world's size, not play. Its column layout is
   kept inside approach 1's delta.
3. A base save plus incremental saves (each save appends what changed since the last): the smallest, fastest writes,
   but a load replays a chain that needs compacting. Not worth it at today's ~16 ms saves.

**Keeping the world across scene changes** has one sensible shape: the game's world is built once and outlives scene
builds; a scene build replaces only the scene's own state. The dev scenes that bring their own crowds get a world of
their own (Goal 2).

## History

- Measured 2026-11-16 (release, linux-clang, the shipped 50,000-NPC world, `Host::Save` then `core::WriteSaveFile`):
  a new game: 7.09 MB raw, 1.59 MB on disk (LZ4), 7.7 ms to serialise + 7.1 ms to compress and write; after three
  month steps: 8.02 MB raw, 1.84 MB on disk, 8.6 + 8.3 ms. A world build (Host constructed on the world scene,
  travel seeded) takes about 30 ms. The month step changes about 13 bytes per NPC (`Npc::power`, `Npc::action`,
  `Needs`); `Npc::cell` and `Npc::home` change only when an NPC moves house.
- Saves today (design 0002 §7.6, `core/save_file.*`): a checksummed header, LZ4 (the user's call, 2026-11-06), written
  to a temporary file and renamed; one list of fields per type (`core::Archive`) feeds the writer, the reader and the
  hasher, so `Host::StateHash` is a hash of the save's bytes. `Host::Write`: `ARENA_FORMAT` 5, the scene, seed, ID
  counter, calendar, pause, player, selection, `ACTIVE_VERSION` + the active domain, `WORLD_VERSION` 3 + the world
  domain, relationships, travel, combat. `Host::Load` reads into a fresh `core::State`, checks it (`Consistent`,
  `NpcsPlaced`, `ClockFits` …) and stages it for the scene change.
- Replays (0002.2) start from a saved state and check `StateHash` every 60 wall ticks; they carry the version and the
  data hash. Design 0002 §7.1: domains are separate registries, stable IDs from a never-reused counter, a canonical
  sort before saving; §7.4: determinism within one binary, no unordered iteration; §7.7: NPCs the player met never fold
  back into an aggregate.
- Decided before: 5b1's discussion (2026-11-10) found that enter-and-return needs a state refactor and deferred it to
  5c ("the world-and-scene split, the save version"); 5a, 5b1 and 5b2 kept "the world surviving a scene change, and its
  save format" out of scope. The scenes chunk rejected a stack of scenes: one current scene, and the world remembers
  where to return to. Chunk 6 (towns): "the save never stores its map", "saving in town saves the world and the active
  scene", leaving folds visitors back with their stable IDs. 5b2: names are never saved (a pure function of seed and
  ID); journeys are records and positions are computed from them.
- Rejected before: keeping only travel across builds (R11e: it would name NPCs the state lacks, break arena saves, or
  ride respawned NPCs).
- Cleanup found: `game::Fold` doesn't call `Travel::Forget` (sweep-up backlog, 2026-11-16); this chunk wires both
  domains into a lasting world, so it is fixed first. The backlog's "World persistence across scene builds" entry is
  replaced by this spec.

## Goals

1. **The game's world, built once.** A new game builds the world from its seed and the game data (the world scene's
   NPCs and their starting power, the travel seeded for the month before the clock's and the clock's own, the
   calendar at its start); a load regenerates that same base and applies the save. After that, nothing rebuilds it: a
   scene change replaces only the running scene's own state (its active entities, its fight, its selection, the
   player's route). The world's NPCs, their relationships, the travel table and journal, the calendar (with 5b2's
   rules: the clock moves only on the world map) and the world pause stay as they are. A scene says by a trait
   whether it is a **game scene**, running on the game's world (today the world scene; chunk 6's towns later), or a
   **dev scene**.
2. **Dev scenes run on a scratch world.** Every dev scene (`arena`, `stress`, `painted`, `cells`) runs on a scratch
   world of its own, built on each entry as today: its `worldNpcs` crowd (5,000, 50,000, none, none) and **its own
   calendar**, so M there steps only the scratch world. The game's world is set aside untouched while a dev scene runs
   and returned on leaving. The dev scenes' NPCs, grids and replay hashes stay as they are.
3. **The cultivator's place on the map is kept.** Leaving the world map and coming back puts the cultivator where it
   stood; when it left through a portal, that is the last point it stood on before stepping into the portal's area,
   so it never comes back standing on the portal (which would send it straight out again). The route is dropped, as
   5b2's Review Focus 5 says; a new game starts it on the map's start city, as today.
4. **Travellers resume.** 5b2's Review Focus 5 holds in full again: leaving the world map (a portal, F2) and coming
   back, the route is gone, the clock stood still, and every traveller is where it was.
5. **The base's identity.** The base world is a pure function of: the seed, the game data that generation reads (the
   world map, which holds the travel rules, and the names: a hash of those two only, so an edit to `creatures.yaml`
   or `abilities.yaml` doesn't orphan saves), and a **generator version** (a constant raised whenever generation code
   changes what it makes, `StartingPower` included). Nothing else feeds it: not the scenes run before, the worker
   count, or the wall clock. A save records all three; a load whose generator version or base data hash differs is
   refused with a message that says which (as replays are today).
5a. **Stable IDs: the world's and the scene's.** The game's world draws stable IDs from its own counter, saved in its
   part, and generation runs first, so the world's NPCs are numbered from the start whatever scene ran before. A
   scene's own entities (the cultivator, place labels, a fight's creatures) draw from a separate range (a marked
   bit, as label keys are marked), its counter kept for the session and never reused, so visiting the world map
   doesn't move the world's counter. A world NPC brought into a scene (`Materialise`) keeps its world ID. This
   renumbers the shipped world's NPCs once: their names, starting power and journeys change once, and 5b2's travel
   numbers are measured again (the Outcome records them).
6. **A save is the delta.** A save holds, for the game's world (and, in a dev scene, its scratch world):
   - the base's identity (Goal 5);
   - every entity the base has that is now gone (its stable ID);
   - every entity the base lacks (created since), in full;
   - for every entity both have, each component field that differs from the base's value, keyed by the entity's
     stable ID: compared field by field, with the field list `core::Archive`'s `Fields()` already gives each type;
   - components added to or removed from a base entity;
   - journeys by what defines them (members, home, destination, start, stay, seed), not their legs: a leg's tiles and
     ticks are a pure function of the map, the profile and the leg's ends (5b2's route rules), so a load rebuilds them;
     the journeys the base seeded are diffed like entities (an unchanged one costs nothing), later ones written whole;
   - the records with no base counterpart in full: relationships, the journal, combat;
   - the scene's state in full (its active domain, as today: once 5b2's stand-ins are gone, the world scene's is the
     cultivator and its place labels), and the counters, calendar, pause, player and selection.
   Changes are laid out by component and field (all NPCs' changed `power` together, then `action`, …) so like values
   sit together and compress well.
7. **A load is exact.** A load regenerates the base, applies the delta, sorts canonically (a load restores
   the saved storage order instead: see the Outcome, R15) and checks the state as
   `Host::Load` does now (and refuses a delta naming an ID the base doesn't have, a field past a type's list, or an
   entity created twice: a save file is untrusted). The loaded state's `StateHash` equals the saved state's, its
   entities iterate in the same order, and from there every tick, month step, journey and draw is the same as in a run
   that never saved, on 1 or 4 workers.
8. **The hash stays the full state's.** `StateHash` keeps hashing the whole state that runs (the running world, game
   or scratch, its records and counters, and the scene), in the state's order as today (`Save` sorts canonically
   first, as today; a save keeps the running order instead: see the Outcome, R15), not the delta's bytes, so a replay's
   checks mean what they mean today. While a dev scene runs, the
   set-aside game world isn't part of it (nothing can change it), so dev scenes' hashes stay as they are; the game's
   world also has a hash of its own part, which the tests use to show it untouched.
9. **Small, fast.** Aims the plan works to, measured as the History's numbers were and recorded in the Outcome (a miss
   is reported there with its reason, never passed over): a new game's save of the shipped world under 10 KB on disk;
   after three month steps, at most a third of today's 1.84 MB; saving no slower than today's ~16 ms (release); a
   load, regeneration included, under 100 ms.
10. **One file.** A save stays one file with a section per part (the game's world, the scratch world when a dev scene
    runs, the scene), written to a temporary file and renamed, so a save is never half-written. (The user allowed
    splitting across files "if that helps"; one file keeps a save atomic and the container already exists.)
11. **Saves, replays and seeds move together.** The save format's version goes up (and `WORLD_VERSION`, whose layout
    becomes the delta); old saves are refused, not migrated; replays record from a delta save; the `save` fuzz seeds
    are regenerated and gain one for a delta (a world save after a month step) and one for a dev scene's scratch world.

## Non-goals

- 5c2's parts: the enter prompt and the stand-in town.
- Migrating old saves, or loading a save made by a different generator version or world data (refused; a policy for
  that is a release-time decision).
- The cultivator's own state (health, qi, abilities) carried across scene changes: the cultivator is still spawned by
  each scene as today; only its place on the map is kept (Goal 3).
- Save slots, autosave, a save menu.
- Chunk 6's town crowds and folding visitors in and out (this chunk makes the world they fold into last).
- Incremental (chained) saves (Approach 3).

## Anti-goals (a failure even if it works)

- A load that is close but not identical: any hash difference, any entity in a different order, any draw that differs
  after a load, is a failure.
- A save that loads against a different base without a word (another generator version, other world data).
- Anything in the game's world rebuilt, respawned or re-seeded by a scene change.
- A dev scene changing the game's world, or the game's world changing a dev scene's hashes.
- Two lists of fields for a type (one for the delta, one for the snapshot): the delta compares through the same
  `Fields()` every archive uses.

## Calls made here (not the user's; check them)

- **Order:** 5b2 finishes first (its Tasks 13 to 19 don't depend on this), then 5c1, before 5b3; 5c2 keeps 5c's place
  after 5b-look. Order: 5b2 → 5c1 → 5b3 → 5b-look → 5c2 → chunk 6.
- **One file with sections**, not several files (Goal 10).
- **Dev scenes on a scratch world**, rebuilt on each entry as today (Goal 2), rather than sharing the game's world: their
  crowds are fixtures whose hashes the tests and `measure` rely on.
- **Records in full** (relationships, the journal, combat), but **journeys compact** (Goal 6): a journey's legs carry
  8 bytes per route tile and a few thousand are under way, so whole journeys would outweigh the NPC deltas; the
  Outcome records each part's share of a save.
- **Every dev scene on a scratch world with its own calendar** (Goal 2), so M in the arena never steps the game's
  world; `painted` and `cells` get an empty one.
- **The way back from a portal** (Goal 3): the last point outside the portal's area, so the portal's rules (and the
  replays resting on them) don't change.
- **Two ID counters and the shipped NPCs renumbered once** (Goal 5a): the base can't depend on which scene ran first
  unless the world numbers its NPCs before anything else; names, starting powers and journeys change once.
- **The base's data hash covers the world map and names only** (Goal 5): creatures, abilities and combat reload in
  debug builds and generation doesn't read them.
- **The compressor:** LZ4 stays unless a measurement shows a permissive alternative (zstd, BSD) makes the shipped
  world's three-month save at least a third smaller at no more than Goal 9's times; the plan measures both and the
  Outcome records the numbers. (Measured: zstd won, and the user switched saves to it on 2026-11-19; see the Outcome.)
- **The cultivator's map place** is part of the world (Goal 3), so chunk 6's "leaving returns to its tile" needs no
  further state.

## Testing

- **Exactness (the user's condition):** for the world scene, the arena and stress: run, save, load; the loaded hash
  equals the saved one; then run both on, through month steps (and travel, fights where the scene has them), and the
  hashes match tick by tick; on 1 and 4 workers. Also a save made in a dev scene, with the game's world set aside,
  loads to both worlds exactly.
- **The world kept:** after a month step, leave the world map (F2, a portal) and come back: the world's own hash,
  every NPC, journey and the journal unchanged, the world's ID counter unmoved, the cultivator where it stood (off the
  portal after a portal), the route gone; a dev scene in between, M pressed there, leaves the game's world and its
  calendar unchanged.
- **The base doesn't depend on history:** the base built after visiting every dev scene equals the one built first.
- **The delta:** a new game's world delta is empty; a changed field appears once; a removed NPC (folded away, as
  `Fold` does) and a created one round-trip; a component added to a base entity round-trips.
- **The base's identity:** a save loaded with another generator version or world data hash is refused, saying which.
- **Refusals:** a delta naming an unknown ID, a field past the type's list, a duplicate creation, a truncated section;
  through the `save` fuzz target with its new seeds.
- **Sizes and times** as Goal 9, measured and recorded (not a test gate: the numbers go in the Outcome).

## Left to the builder

- The delta's exact byte layout (how a changed field is marked, how IDs are run-length coded), within Goal 6.
- Whether the base's values are kept in memory for the comparison at save time or regenerated then, within Goal 9's
  save time.
- How the scratch world sits beside the game's world in `core::State` or `Host` (a second state, more domains), within
  Goal 2.
- How `Host::Write`/`Host::Load` split into the world part and the scene part.

## Outcome

Implemented on branch `phase6-chunk5c1` in nine tasks, 2026-11-18 to 2026-11-19 (`feat(game)!` commits for the world
kept across scenes, the delta save, and the dev scenes' deltas; the measurements are the last commit). The plan's ledger
(`.superpowers/sdd/`, gitignored) held the rulings below; they are kept here with their reasons.

### Measured

Release, linux-clang, the shipped 50,000-NPC world, 2 workers; `Host::Save`, then `core::WriteSaveFile` (zstd level 1),
then a load into a fresh host with `EnterScene`, as the History's numbers were taken. The case is `measure the shipped
world's saves` in `tests/unit/game/shipped_world_tests.cpp` (skipped; `--no-skip`, release).

| | raw | on disk (zstd) | serialise | compress + write | load (read + regenerate + apply) |
|---|---|---|---|---|---|
| a new game | 3,771 B | 895 B | 12.5 ms | 0.1 ms | 30.6 ms |
| after 1 month step | 2,194,522 B | 306,293 B | 19.4 ms | 2.5 ms | 32.6 ms |
| after 2 month steps | 2,445,660 B | 403,335 B | 23.9 ms | 3.6 ms | 34.1 ms |
| after 3 month steps | 2,641,016 B | 450,884 B | 25.5 ms | 4.0 ms | 35.0 ms |

Every value in this table and in the aims below is from one run, the second of two (the second was the one kept; the
first read within 1 ms of it, 25.8 ms and 3.9 ms for the third step).

The same with LZ4, as the case first measured it (before the switch, a run of its own): on disk 1,369 B, 641,796 B,
872,313 B and 938,140 B; compress + write 0.0, 1.8, 2.6 and 3.0 ms.

Before (the History): a new game 7.09 MB raw, 1.59 MB on disk (LZ4), 7.7 + 7.1 ms; after three steps 8.02 MB raw, 1.84 MB
on disk, 8.6 + 8.3 ms.

Each part's share of a save (bytes, raw; the world scene has no scratch world):

| part | new game | 1 step | 2 steps | 3 steps |
|---|---|---|---|---|
| header, scene counter, clock, scene (cultivator, labels), combat | 406 | 406 | 406 | 406 |
| game world: domain delta (changed fields, gone, created) | 104 | 2,032,535 | 2,106,785 | 2,207,525 |
| game world: storage order | 5 | 5 | 5 | 5 |
| game world: relationships | 8 | 8 | 8 | 8 |
| game world: journeys (plans, gone, journals) | 3,248 | 161,568 | 338,456 | 433,072 |
| scratch world | none | none | none | none |

In a dev scene the scratch world is what the delta saves: an arena save mid-fight went from about 220 KB to under 5 KB
(Task 8; it is the delta against the entry build's crowd of 5,000).

### The aims (Goal 9)

| aim | measured | |
|---|---|---|
| a new game's save under 10 KB on disk | 895 B | met |
| after three month steps at most a third of 1.84 MB (about 613 KB) | 450,884 B (LZ4 alone: 938,140 B) | met, with zstd |
| saving no slower than today's ~16 ms (release) | 25.5 ms serialise + 4.0 ms write | **missed** |
| a load, regeneration included, under 100 ms | 35.0 ms | met |

Why the save time is missed: the month step touches 49,699 NPCs, and the delta compares every NPC field by field against
the kept base, a pass the old save, which only wrote them, didn't have; the delta also spends about 44 raw bytes on each
changed NPC (its place by `u64`, its field masks, the values), where the History's count of changed bytes was 13. That is
2.2 MB of the 2.64 MB raw; the journeys add 433 KB. The aim was set from the changed values alone. The levers left, none
taken: narrower places and masks and run-length coded IDs in `core/delta.hpp` (a layout change, bumps the format again),
and a cheaper by-ID lookup in the comparison (the same cost shows in the arena fuzz input below). The size aim was missed
(938 KB) while saves were LZ4-compressed, for the same reason, and is met only because zstd squeezes the delta's `u64`
places and masks about twice as well as LZ4 does.
A new game saves about 1,800 times smaller on disk than before (1.59 MB to 895 B), and a three-month save is about a
quarter the size it was (1.84 MB to 451 KB).

### The compressor

The user's call of 2026-11-06 was LZ4. The measurement the spec called for, on the three-month save's 2,641,016 raw
bytes dumped to files and compressed by the `zstd` and `lz4` command line tools of the Pixi environment (1.5.7). Times
are per run of the tool, process start included, so only the differences mean anything.

| compressor | bytes | time |
|---|---|---|
| LZ4 default (what the game used) | 938,823 | 3.0 ms |
| LZ4 HC level 9 | 808,162 | not timed |
| zstd level 1 | 451,958 | 4.0 ms |
| zstd level 3 | 482,599 | 5.5 ms |
| zstd level 9 | 380,755 | 18 ms |
| zstd level 19 | 450,942 | not timed |

A new game: LZ4 1,552 B, zstd level 1 863 B. Decompression of the three-month save: LZ4 2.3 ms, zstd level 1 2.5 ms.

By the spec's rule (at least a third smaller, within Goal 9's times) zstd level 1 won on size (52% smaller than LZ4, past
a third) at about 1 ms more to write and 0.2 ms more to read; the save time aim is missed with either compressor, for
the reason above. **The user decided on 2026-11-19 to switch saves to zstd level 1, replacing LZ4, and to keep one
compressor, not both.** The change: `zstd v1.5.7` (BSD-3-Clause) in place of `lz4 v1.10.0` in `xmake.lua`, the lock and
`THIRD_PARTY_NOTICES.md`; `SAVE_FILE_VERSION` 1 to 2, so a file of the LZ4 framing is refused by its version, never
misread (old saves are refused, not migrated); the decompression bound is zstd's own (one repeated byte costs 4 bytes
for 128 KiB, a ratio of 32,768) instead of LZ4's 255. Level 1 over 3: smaller and a third faster here; level 9 is 16%
smaller again at four times the time. It builds on linux-gcc, linux-clang and windows-msvc (container); the `macosx|arm64`
lock entry was written by hand from the others (the same hash on every platform, as LZ4's was) and CI's `macos` job
confirms it.

### Saves over hours of play

The case `measure the shipped world's saves over hours of play` (skipped, release, same file) runs the shipped world
forward by the path M takes: one `Host::AdvanceCalendar()` per month on the usual workers (`core::Jobs` at its default
worker count, this host's hardware threads less the program's other threads), which moves the clock to the next month's
first tick, finishes the journeys that are over, sends the month's new ones out and runs the month step. A world month
is 30 days of 180 ticks, 5,400 ticks, 90 s at 60 Hz, so 1 h is 40 months, 10 h 400, 50 h 2,000, 100 h 4,000 and 250 h
10,000. That is the upper bound for a player on the world map, unpaused, with no M jumps (time in a fight, in a dev
scene or paused moves the calendar not at all). Ticking each month instead (54 million ticks) would reach the same
state: a month's journeys are records drawn from the month's start (their ticks are in the plan), and the travellers'
positions are computed from them, not stepped; what a tick adds on the world map (the cultivator's walk, drawing) is not
part of the saved world. One run, cumulative, release, linux-clang; the run took about 8 minutes. Every checkpoint's
load matched the saved `StateHash` and `WorldHash` (asserted). Every time, size and count in the first tables is from
that one run; an earlier run of the same case is given as its own rows of times. Its raw and on-disk sizes matched the
kept run's byte for byte at every checkpoint (the figures are in the task's report, 250 h on disk 64,801,572 B). The
whole-save row comes from the kept run only. Its times differ from the kept run's by 1% to 18% in the save's total and
4% to 12% in the load, each as a share of the earlier run's time, except at 50 h, where the save's total differs by 49%
(earlier 349 ms, kept 520 ms) and the load by 33% (earlier 275 ms, kept 366 ms). Treat the times as good to about that,
and the sizes as exact.

| | 1 h | 10 h | 50 h | 100 h | 250 h |
|---|---|---|---|---|---|
| months | 40 | 400 | 2,000 | 4,000 | 10,000 |
| **raw save** | 7.50 MB | 40.3 MB | 169 MB | 324 MB | 755 MB |
| **on disk (zstd)** | **1.42 MB** | **5.41 MB** | **14.5 MB** | **26.8 MB** | **64.8 MB** |
| a whole (non-delta) save, raw | 13.9 MB | 47.9 MB | 177 MB | 332 MB | 763 MB |
| a whole save, on disk | 1.96 MB | 6.00 MB | 15.0 MB | 27.4 MB | 65.2 MB |
| save: serialise + write | 43.7 + 12.8 ms | 76.1 + 59.7 ms | 260 + 260 ms | 465 + 543 ms | 688 + 1,078 ms |
| load (regeneration included) | 61 ms | 114 ms | 366 ms | 655 ms | 1,369 ms |
| earlier run: save, serialise + write | 38 + 13 ms | 67 + 56 ms | 148 + 201 ms | 406 + 446 ms | 675 + 1,071 ms |
| earlier run: load | 56 ms | 102 ms | 275 ms | 604 ms | 1,429 ms |
| month step, mean over the stretch since the last checkpoint | 4.5 ms | 7.2 ms | 16.3 ms | 32.4 ms | 65.6 ms |

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
| **relationships** | 3,456,768 | 33,119,608 | 161,891,168 | 317,135,408 | 747,784,788 |
| header, scene, combat | 406 | 406 | 406 | 406 | 406 |

What is behind them:

| count | 1 h | 10 h | 50 h | 100 h | 250 h |
|---|---|---|---|---|---|
| NPCs alive (base 50,000), created, gone | 50,000, 0, 0 | 50,000, 0, 0 | 50,000, 0, 0 | 50,000, 0, 0 | 50,000, 0, 0 |
| NPCs with a changed field | 50,000 | 50,000 | 50,000 | 50,000 | 50,000 |
| journeys under way (base's gone: 5,146) | 6,627 | 7,086 | 7,065 | 7,044 | 7,231 |
| journal entries | 102,465 | 486,719 | 486,820 | 486,820 | 486,820 |
| relationships | 172,838 | 1,655,980 | 8,094,558 | 15,856,770 | 37,389,239 |

Findings (nothing was fixed; the controller and the user decide):

1. **Relationships grow without bound and are the whole save.** Nothing caps or forgets a relationship: the table holds
   37.4 million at 250 h (about 3,700 a month, 20 raw bytes each, written whole), 99% of the raw save (748 of 755 MB) and
   nearly all of the 64.8 MB on disk. At 250 h: 65 MB on disk, 1.8 s to save (688 ms serialising and 1.08 s compressing
   and writing, on the frozen tick the design expects a save to be), 1.4 s to load, and about 750 MB held in memory by the
   table. Everything else is bounded.
2. **The raw save reaches the file limit near 360 h.** `MAX_SAVE_BYTES` is 1 GiB and the save grows 72 KB a month: 755 MB
   at 250 h, 1 GiB at about 14,400 months (360 h), where `WriteSaveFile` refuses ("over the limit") and the game can no
   longer save at all.
3. **The month step slows with the table**: 4.5 ms a month over the first hour, 65.6 ms averaged over hours 100 to 250
   (a month step is one of these per 90 s, so still invisible there, but it grows linearly and would pass a frame's
   budget well past 250 h). The measurement did not isolate which part of the step reads the table.
4. **The delta stops paying once relationships dominate.** Against a whole save it saves 46% raw and 28% on disk at 1 h
   (1.42 against 1.96 MB), 16% and 10% at 10 h, 4% and 3% at 50 h, and at 250 h only 1% raw and 0.6% on disk (64.8
   against 65.2 MB): a delta that holds relationships whole is a whole save by then. The NPCs' part is a constant 2.2 MB
   (all 50,000 change every month), so it is neither the cost nor the gain.
5. **What stays bounded**: the changed-field delta (2.2 MB, constant; nobody is created or gone in play on the world map),
   the storage order (5 B), the journeys (about 7,000 under way, 41 KB gone and 280 to 290 KB by plan, none written whole),
   the journals (10 entries an NPC, 4.67 MB from about 10 h on; a met NPC keeps 50, so the player meeting people raises it
   to at most 50 each), and the header, scene and combat (406 B).

Finding 1 was fixed afterwards: the bounded table's figures at the same checkpoints are in the Outcome of
`2026-11-19-relationships-bounded-design.md`.

### Rulings and their reasons

- **R1: format numbers.** `ARENA_FORMAT` goes up by one from whatever the task's base held (the plan's numbers predate
  5b2's route bump), each with its line in the format comment. Cost if wrong: none, a number.
- **R2/R4: journeys.** The save writes a journey by its plan (`core::PlanOf`) and rebuilds it (`core::Rebuild`); a
  journey whose plan doesn't rebuild it byte for byte is written whole (0 of 5,638 after three months on the shipped
  world). R4's release precondition on `SavePlans` (document it and make it hold: no assert-then-dereference in a
  release build) became moot when Task 7 removed `SavePlans`. `Travel::Load` and the whole-table plan writer are gone: a
  save reads travel only through `Travel::LoadDelta`, so a save file never feeds a whole-journey load. The layout of the
  spec's Goal 6 is binding over any helper's shape.
- **R3: models.** Opus implemented Tasks 2, 3 and 5 to 8 (determinism, IDs across modules, untrusted input); Sonnet the rest.
- **R5/R6: the world state.** `WorldState::mapPlace` exists from Task 5 with nothing written until the way back fills it
  in Task 6; `Build` targets `Live()` throughout so the scene's build reaches the world it runs on.
- **R7: Load.** The game world is read whole in Task 6 and regenerated with the delta in Task 7: a whole save makes
  regeneration pointless, and the base identity check arrives with the delta.
- **R8: the way back.** `mapPlace` is recorded in `Leave`, where the leaving tick knows portal against F2.
- **R9: Review Focus 4.** Tested through `State::Move` on a hand-built save: `Activate` refuses homed NPCs and no game
  scene activates one today. An `Activate`-path test belongs with the first scene that does.
- **R10: after a jump.** One step-area test serves `OnPortal` and the way back; a kept place inside the portal's step
  area falls back to the start city's centre (recorded that way in `Leave`, and refused by `Load`'s fit check), so a
  cultivator landing inside the area after an M jump doesn't leave again at once.
- **R11: order of checks.** Relationships are read and checked after both domains, as before the chunk: a relationship
  naming an in-scene NPC is valid.
- **R12/R15: exactness is binding for dev scenes too (departure from Goal 8 and from "Save sorts canonically first").**
  The spec had `Save` sort the state canonically before writing, and `Load` sort after applying the delta. Task 7 found
  that `Save`'s sort reversed the order `GenerateWorld` makes (fixed by sorting at the end of `GenerateWorld`), and that
  the dev scenes still diverged from a run that never saved once combat created and destroyed entities (R12, which it
  left to Task 8). Task 8 found why: the arena's active domain is out of ID order from the build, and the stress arena's
  active domain and a scratch world are in creation order, reversed by EnTT's sort, so a sorting `Save` changed the
  saver's own state and a game that saved ran on differently from one that never did, against the user's condition.
  Ruling R15: a save leaves the saver's running state as it was, order included, and a load reproduces that order.
  `Save` no longer sorts; a fully written domain is in storage order, and a delta-saved world domain also writes its
  storage order (`core::SaveDomainOrder`/`LoadDomainOrder`: one byte per storage while a storage keeps its base's order,
  5 B for the world's). The alternative, sorting at every build, moved two arena reference pictures. Cost: a few bytes per
  delta-saved domain and the `StateHash` now follows storage order, which for a canonically built world is the same as
  before. Goal 8's and Goal 7's text stands, with an inline note pointing here.
- **R13: test time.** The shipped-data host built its base, sorted and copied it about 100 times in the unit tests; the
  small named test map serves refusal cases and the shipped data only exactness cases. Host tests went 32.5 s, then
  76 s, then 15 s.
- **R14: a comment-only fix** went to a fresh Sonnet implementer and a Haiku re-review rather than resuming a ~500k-token
  context.
- **R16: seeds.** Every file in `tests/fuzz/corpus/save/` comes from a writer case (the old `seed` was replaced, its reason
  in the commit body); the fuzz host takes a small map (`test::TravellingSmallData`) with travellers so the delta's gone and
  planned-journey branches and the counter-behind-base refusal are reachable. The delta and scratch seeds each hold a gone
  journey, a planned one and changed NPCs, asserted in their writer cases.
- **R17: stress** (its 60 ticks since cut to 10: R25). The stress arena's exactness test compares 60 ticks tick by tick, not
  the plan's 600: a stress tick only turns orbits as a function of the tick, so an order or state divergence shows from
  tick 1; the arena's 600 ticks through deaths and respawns carry the long run (600 stress ticks on three hosts cost about
  67 s in debug).
- **R18: the kept base.** Leaving a dev scene keeps its scratch base for the next entry, which relies on the dev scene's
  world build being independent of the scene counter and `GameData`; a test shows, for every dev scene, that the kept base
  equals a fresh entry build, also after `SetData` with edited creatures.
- **R19: stress hashes** (its time since cut: R25) are compared every tick (31.6 s in debug, its own file, in parallel
  with the rest, whole run 32.1 s), because the spec's Testing says "tick by tick".
- **R20/R21:** this task's: zstd's win was reported to the user instead of adopted unasked (a dependency change), who
  chose it on 2026-11-19; and R15 is recorded here.
- **R22: zstd replaces LZ4** (the user, 2026-11-19; see "The compressor"). One compressor, level 1; the save file's version
  goes up so LZ4 saves are refused, not migrated; the swap is its own commit ahead of the docs, through the add-dependency
  steps (licence record, exact pin, lock); the container gate for a dependency change is `check --container` from the main
  checkout after the branch lands there, or CI's container jobs. Cost: a dependency on every preset, and the
  `macosx|arm64` lock entry waiting for CI's macos job.
- **R23: saves over hours of play** (the user, 2026-11-19; see "Saves over hours of play"). A world month is 30 days of 180
  ticks, 5,400 ticks, 90 s at 60 Hz, so 1 h is 40 months and 250 h is 10,000: an upper bound for a player on the world
  map, unpaused, with no M jumps. Months are advanced by the path M takes (`Host::AdvanceCalendar`), not tick by tick. The
  findings are reported, not fixed. Cost: a run of about 8 minutes, on request, not in the suite.
- **R25: stress at 10 ticks** (the final review; supersedes R17's 60 and R19's time). R17's own reason holds at 10: a
  stress tick only turns orbits, so a divergence shows from the first tick, and each of the 10 is still compared, on both
  worker counts. The file had set the suite's wall time: save_stress 31.8 s to 10.7 s in debug, the whole run 32 s to 27 s.
  Cost: a stress-only divergence after tick 10 goes unseen; the arena's 600 ticks carry the long run.
- **R26: Load refuses IDs outside their domain** (the final review's one Important finding, fixed test-first in "fix(game):
  refuse a save whose IDs don't match their domains"). A scene build clears the last scene by sending its world-range
  entities back as NPCs and destroying its scene-range ones, so a save holding a world-range entity in the scene that isn't
  an NPC, a scene-range entity in the world domain, or a relationship naming a scene-range ID loaded and then broke the next
  scene change (an assertion, undefined behaviour in release, or a save its own game refused). The fuzz target still never
  changes scene after a load: each input would then build the next scene (stress: 50,000 NPCs), so unit cases cover it.
- **R27: left as it is** (the final review's triage). The arena fuzz input's ~155 ms belongs with the index-by-index delta
  compare named under "The aims"; caching every dev scene's base in the host would keep a 50,000-NPC stress base in the
  game's memory for a fuzz-only gain; XMake's "warning: Ascend SDK not found" comes from XMake 3.1.2 checking its ascendc
  toolchain on every Linux configure, predates zstd and has no setting to quieten it; the hand-written `macosx|arm64` zstd
  lock entry matches the machine-written ones (the same config hash on every platform). GLM's review found nothing.
- Accepted: worker sequencing in the exactness tests (`Host::Tick` takes no job system, so a month step and a publish
  fork on both worker counts rather than a whole sequence per count); `feat(game)!` BREAKING footers as precedent for
  format changes.

### Numbers carried

- **Renumbering (Goal 5a).** `the shipped roads carry a few thousand at once` (bounds 2,000 to 6,000), measured before
  and after Task 2, passed unchanged and `travel:` in `world.yaml` was not retuned:

  | instant | before | after |
  |---|---|---|
  | start (tick 0) | 2,538 | 2,544 |
  | first month's middle | 3,106 | 3,116 |
  | after the step | 3,248 | 3,266 |
  | second month's middle | 3,210 | 3,218 |
  | second month's last tick | 3,191 | 3,143 |

- **Reference pictures moved by the renumbering: 21**, looked at before updating. Six world pictures (other travellers
  on the roads, their names and places; the map, cultivator and its sort unchanged); ten arena pictures, whose villagers
  stand elsewhere because their wander targets are drawn from their IDs, now 1 to N instead of following the scene's
  entities (the spec's "dev scenes stay as they are" held for their hashes in tests but not for these pictures); five
  volley pictures, whose seeds are drawn from the caster's ID, now `SCENE_ID_BASE`, so the swords take other paths.
- **Fuzzing the `save` target:** 11 inputs/s after Task 8's first pass, 206 inputs/s after its fix (ASan, 30 s from the four
  seeds). An arena input still costs about 155 ms, from the delta comparison of 5,000 NPCs by ID.
- **Test times (debug):** the whole run 32.1 s; `save` 23.5 s, `save_exactness` 26.3 s, `save_stress` 31.6 s.
