# Phase 6, chunk 5c1: the world kept, and saves as deltas

Date: 2026-11-16 · Status: approved 2026-11-16 · Chunk spec: `2026-11-10-phase6-chunk5-world-map-design.md` (its 5c, split here) ·
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
7. **A load is exact.** A load regenerates the base, applies the delta, sorts canonically and checks the state as
   `Host::Load` does now (and refuses a delta naming an ID the base doesn't have, a field past a type's list, or an
   entity created twice: a save file is untrusted). The loaded state's `StateHash` equals the saved state's, its
   entities iterate in the same order, and from there every tick, month step, journey and draw is the same as in a run
   that never saved, on 1 or 4 workers.
8. **The hash stays the full state's.** `StateHash` keeps hashing the whole state that runs (the running world, game
   or scratch, its records and counters, and the scene), in the state's order as today (`Save` sorts canonically
   first, as today), not the delta's bytes, so a replay's checks mean what they mean today. While a dev scene runs, the
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
  Outcome records the numbers.
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
