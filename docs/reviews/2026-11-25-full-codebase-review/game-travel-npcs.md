# Review: game-travel-npcs

**Scope:** src/game/travel.{hpp,cpp}, npcs.{hpp,cpp}, world_state.{hpp,cpp}. Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 20 standing (0 critical, 0 high, 0 medium, 13 low, 7 info), 0 raised but refuted on verification.

**Parts:** game-travel-npcs.md, game-travel-npcs-part2.md

## Assessment

## Scope: game-travel-npcs (travel.{hpp,cpp}, npcs.{hpp,cpp}, world_state.{hpp,cpp}, tests/unit/game/travel_tests.cpp, npcs_tests.cpp)

**Overall health: good. The code is careful and well defended, but it is built for the shipped 6-city map rather than for large procedural maps.**

### Strengths
- **Determinism.** Every draw is keyed on (seed, state counter, month, NPC). Chunk outputs are merged in chunk order and then sorted by leader, so the result does not depend on the worker count. Tests compare 1 worker against 4 or 7 to the byte.
- **Save robustness.** `LoadDelta`/`Adopt` check a malformed table very thoroughly: tick overflow bounds, plans checked before `Rebuild` asserts, membership against `Action::Travel` in both directions, and nothing changes on a refusal. The tests cover most refusal branches with named samples.
- **Integer weighting.** `DestinationWeight` is whole-number maths and saturates. Given `MOST_WORLD_PEOPLE` (100k) and `MOST_DRAW` (100), the totals cannot saturate in practice.
- **Small, sensible pieces.** The journal merge is a single sorted pass. `JourneysStamp` is a neat presentation-cache key with correct move semantics. `MonthStep` keeps all writes chunk-local, with the partner effects merged after the join.

### Weaknesses and risks
1. **Scaling of travel on procedural maps.** `Paths::Warm` builds an unbounded whole-map Dijkstra map for every city and every band profile, on the logic thread, at generation and at the first month step. That costs cities × profiles × tiles × 4 bytes, even though the reach rules make most of each map useless. `ChooseDestination` also scans every place twice for every traveller. Both are fine for 6 cities and both grow badly with map size and city count.
2. **The NPC-tier architecture is mostly fiction.** `Group`, `Materialise`, `Fold`, `Relationships::Forget` and `Travel::Forget` have no caller in production. No NPC on the world map can ever be activated. The npcs.hpp header still describes three tiers. `Npc::cell` means two different things (a 64-column grid cell or a map tile).
3. **No single way to remove an NPC.** Relationships, travel journeys and journals all name NPCs by stable ID. Only the dead `Fold` cleans all three. The planned NPC death and lifespan work will make it easy to destroy an NPC while it is still in a journey. `Finish` would then write a journal for a missing NPC, and the game's next save would be refused by its own load.
4. **Small correctness edges.** `CellAt` converts unbounded floats to `u32`, which is UB when a save holds a finite but huge `Body` position. `Adopt` and `Within` accept IDs that are not NPCs. `LegSound` uses an enum range check that will break silently when a new shape is added. A parts-less journey gets a bogus journal entry when assertions are off.
5. **Redundant state.** `m_travellers` duplicates `Npc::action == Travel`, and the code enforces that the two always agree.
6. **Tests.** Coverage is good for travel. Missing: `CellAt` bounds, journal merging across interleaved NPCs, a direct `Travel::Seed` test, and a golden hash of generation that would force `GENERATOR_VERSION` bumps.

Nothing in scope looked like a desync or crash in normal play. The highest risks are scaling and the fragile NPC-removal invariant, which upcoming work (NPC death, larger maps) will hit.

**Files read in full:** src/game/travel.hpp, src/game/travel.cpp, src/game/npcs.hpp, src/game/npcs.cpp, src/game/world_state.hpp, src/game/world_state.cpp, tests/unit/game/travel_tests.cpp, tests/unit/game/npcs_tests.cpp, src/game/random_purposes.hpp

## Findings

### GAME-TRAVEL-NPCS-01: Paths warms an unbounded whole-map distance map for every city and every band profile: memory and time grow as cities × profiles × tiles

- **Severity:** low (reviewer said high) · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/travel.cpp:396`

SendOut calls `paths.Warm(core::WALKING)` and `paths.Warm(band.profile)` for every band. Each Warm builds `DistanceMap::Build` (core/paths.cpp:38) for every city: a full Dijkstra over every tile of the map, stored as `std::vector<u32>` of size columns × rows. This runs single-threaded on the logic thread, at GenerateWorld (Travel::Seed) and at the first month step, and the maps are kept for the whole run. Most of each map is wasted: a traveller can only use costs below its reach (`reachBase + whole power × reachPerPower`; shipped mortals reach 300 cost units, the strongest about 10,300).

**Evidence:** travel.cpp:396-399 `paths.Warm(core::WALKING); for (const core::PowerBand& band : map.Travel().bands) { paths.Warm(band.profile); }`; paths.cpp:39 `std::vector<u32> cost(static_cast<std::size_t>(map.Columns()) * map.Rows(), UNREACHABLE);` with no early stop; the Paths doc says 'each built on first use and kept'.

**Impact:** The shipped map has 6 cities and 2 profiles, so it is fine. A procedural 1024×1024 map with 500 cities and 3 profiles would need about 6 GB and minutes of Dijkstra at generation, blocking the logic thread. This conflicts with the recorded goal that map-derived data scales with map size.

**Recommendation:** Bound each Dijkstra at the largest reach any traveller can have (`reachBase + reachPerPower × the most whole power`), and store a sparse or windowed cost map per city. Alternatively, precompute a city-to-city cost graph and route per journey lazily. Build the maps across jobs during generation. Add a measured spike on a large generated map before the procedural generator lands.

**Verification:** Code facts hold: SendOut (travel.cpp:396) warms WALKING plus each band profile, and DistanceMap::Build (paths.cpp:38) is an unbounded full-map Dijkstra stored as a full u32 vector, kept for the run. The 6 GB and minutes figures are an extrapolation to a hypothetical 1024x1024 map with 500 cities. The procedural map is not built yet, and MOST_WORLD_PEOPLE caps the world at 100k people. The sweep-up backlog (lines ~76-82, 107) already names distance maps as the scaling candidate, with a planned redesign (per-destination walk tables over Paths' distance maps, scaling with destinations x profiles). High is overstated today. This is a known future scaling concern, not a present defect.

### GAME-TRAVEL-NPCS-02: ChooseDestination scans every place twice for every traveller, with a distance-map lookup each time

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/travel.cpp:243`

`weightOf` is called for every place in two passes (the total, then the cumulative pick). Each call goes through `paths.To(city, profile)` (two asserts and two `.at()` calls) and `CostFrom(npc.cell)` (`.at()`, a cache miss into a different multi-MB vector for each city). The cost per traveller is O(places), even though only cities within reach can have a non-zero weight.

**Evidence:** travel.cpp:250-266: two loops over `map.Places().size()`, each calling `weightOf(place)`, which calls `paths.To(city, profile).CostFrom(npc.cell)`.

**Impact:** About 5.8k travellers a month (100k people at a share of 58) × 2 passes × thousands of places would mean tens of millions of random reads per month step on large maps. This adds to the known month-step hitch.

**Recommendation:** Compute the weights once into a small per-chunk scratch buffer and reuse them for the pick. Better, precompute per home city (or per region tile) the candidate cities within the maximum reach, so the scan covers only those.

**Verification:** travel.cpp:243-266 does scan all places twice, calling weightOf (a distance-map lookup) each time, so the cost is O(places) per traveller. The scale numbers (thousands of places) are hypothetical. The backlog records the month step's cost and hitch, and lists destinations at ~0.9 ms CPU at share 200 as small. The two-pass scan is real but cheap at shipped sizes. Caching the weights is a reasonable small tweak.

### GAME-TRAVEL-NPCS-03: Removing an NPC has no single entry point; destroying one without Travel::Forget makes the game's next save unloadable

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/travel.cpp:316`

Journeys, journals and relationships all name NPCs by stable ID. Only `Fold` calls both `Relationships::Forget` and `Travel::Forget` before `State::Destroy`, and Fold has no production caller. If an NPC is destroyed while still a journey member, `Finish` writes a journal entry for it anyway (only a PN_ASSERT guards this; the release build pushes into m_written regardless). The next save then contains a journal for an NPC not in the state, and `Adopt`'s `journalSound` refuses it (`state.DomainOf(journal.npc).has_value()`). Host::Load would also refuse relationships naming it (`Within`).

**Evidence:** travel.cpp:316-323: `m_written.emplace_back(npc, entry); PN_ASSERT(IsWorldNpc(state, npc), ...)`, with the entry added unconditionally; travel.cpp:615 journal check; npcs.cpp:292-299 Fold is the only full cleanup.

**Impact:** NPC death and lifespan (planned in game-design-gaps) or any new despawn path will produce saves the game itself refuses unless every author remembers both Forget calls. Relationships::Forget is also O(edges) per NPC, so mass deaths each month would be quadratic.

**Recommendation:** Add one `RemoveNpc(WorldState&, StableId)` (or a batched `RemoveNpcs`) that forgets relationships, journeys and journals and then destroys the entity, and make it the only way to destroy a world NPC. Batch the Forget calls (collect the IDs, then one erase_if pass). Add a test that a save made after removing a travelling NPC loads.

**Verification:** The facts hold. Finish (travel.cpp:316-320) asserts IsWorldNpc but still emplaces the journal entry unconditionally. Fold is the only path that calls both Forget functions, and nothing in src outside npcs.* calls Fold, Materialise or SpawnGroup. So no production path destroys an NPC today. The risk is hypothetical and applies only to future death or despawn features. The proposed single RemoveNpc entry point is sensible design advice, not a live bug. The quadratic Forget claim was not verified.

### GAME-TRAVEL-NPCS-04: Group / Materialise / Fold and the three-tier model are dead in production, and the header documents tiers that don't exist

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** dead-code · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/npcs.hpp:5`

The header describes three tiers: active NPCs, world NPCs, and aggregate Groups standing for unmet people. In practice, GenerateWorld spawns every person as a full world NPC (SpawnPeople), nothing in src spawns a Group, and Materialise, Fold, SpawnGroup, Relationships::Forget and Travel::Forget are called only from tests. `Activate` asserts NO_HOME, so no world-map NPC can ever enter the active tier, yet the ACTIVE doc says 'the arena, or the world map around the player'. Fold is also why npcs.hpp has to forward-declare `Travel`, creating an npcs↔travel include cycle.

**Evidence:** grep finds no SpawnGroup, Materialise or Fold callers outside npcs.cpp and the tests; npcs.cpp:255 `PN_ASSERT(person.home == NO_HOME, ...)`; npcs.hpp:29-30 ACTIVE doc; npcs.hpp:27 `class Travel;`.

**Impact:** Readers and planners are misled about the scaling mechanism. The world's population is held to 100k (`MOST_WORLD_PEOPLE`), apparently because the aggregation that would lift that cap is not wired in. Dead code still costs tests and save-format surface (Group is in WorldComponents).

**Recommendation:** Either wire up aggregation for the procedural world (groups per region, materialised near the player) or remove Group, Materialise and Fold along with their save component. Rewrite the header and ACTIVE docs to describe what actually runs.

**Verification:** Grep finds no callers of SpawnGroup, Materialise or Fold in src outside npcs.*, so they are dead in production. Group and the tiers are documented in docs/design/0002-engine-architecture.md:310 and :535 as the intended scaling model, with the cap MOST_WORLD_PEOPLE = 100k in world_map.hpp:57. They look like planned infrastructure that is not wired up yet, not an oversight. Whether the header misleads is a judgement call. The ACTIVE doc claim about the world map is not checked in detail. Severity is low to medium, because the design record shows this is deliberate groundwork.

### GAME-TRAVEL-NPCS-05: CellAt converts unbounded floats to u32: UB for a finite but huge Body position from a save, and rows are never clamped

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** undefined-behaviour · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/npcs.cpp:133`

`row = static_cast<u32>(std::max(point.y / CELL_SIZE, 0.0F))` has no upper bound. Converting a float of 2^32 or more to u32 is undefined behaviour. `row * MAP_COLUMNS` also wraps for rows ≥ 2^26. `Body::Valid` checks only that positions are finite, so a save holding an active homeless NPC at y = 1e30 passes the load, and the next scene build's `Deactivate` calls `CellAt(body.current)`. The doc ('a point off the map's edge counts as in the nearest edge cell') is false for the bottom edge, since the grid has no row limit.

**Evidence:** npcs.cpp:131-135; components.hpp Body::Valid `return core::IsFinite(previous) && core::IsFinite(current);`; npcs.cpp:271-273 Deactivate uses CellAt.

**Impact:** A crafted or corrupted save produces UB (UBSan reports it; x86 yields 0x80000000 or similar garbage cells). Ordinary play keeps NPCs near their cell, so this is unlikely outside malformed input.

**Recommendation:** Clamp the row to a defined grid height (or to u32 max / MAP_COLUMNS) before the cast, or have Body/Wander validation bound positions to a sane world extent. Add CellAt tests for a far-off point and a negative point.

**Verification:** npcs.cpp:133 has no upper clamp on row: `static_cast<u32>(std::max(point.y / CELL_SIZE, 0.0F))`. A float of 2^32 or more converted to u32 is UB, and row*MAP_COLUMNS can wrap. Body::Valid checks only finiteness, as the finding says. It needs a malformed or crafted save, so the practical impact is low. A one-line clamp would fix it.

### GAME-TRAVEL-NPCS-06: Warm(WALKING) is always built even when no band uses WALKING

- **Severity:** low · **Verdict:** confirmed · **Category:** wasted-work · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/travel.cpp:396`

ProfileFor returns WALKING only when there are no bands. With bands present, SendOut still warms WALKING maps for every city, which no traveller reads unless a band names WALKING.

**Evidence:** travel.cpp:205-213 ProfileFor; travel.cpp:396 unconditional `paths.Warm(core::WALKING)`.

**Impact:** A whole set of distance maps (cities × tiles × 4 bytes) and their Dijkstra time wasted for data whose bands avoid on_foot. This breaks the 'avoid wasted work' rule.

**Recommendation:** Warm WALKING only when `rules.bands.empty()`, and otherwise only the profiles the bands name (deduplicated).

**Verification:** ProfileFor (travel.cpp:205-213) returns WALKING only when rules.bands is empty. SendOut (travel.cpp:396) warms WALKING unconditionally. When bands exist and none names WALKING, those maps are never read, so the work is wasted. Return and journey code that uses WALKING may read it elsewhere, which I did not check. If so the maps are not wasted. The fix is trivial but the impact is small at 6 cities.

### GAME-TRAVEL-NPCS-07: GENERATOR_VERSION is bumped by hand, with no golden test, though travel code is part of what generation makes

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** risk · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/world_state.hpp:28`

The base world includes the journeys Travel::Seed makes, which depend on ProfileFor, DestinationWeight, ChooseDestination, core::RouteDown, core::Visit and the Paths tie-breaking. The doc names only StartingPower as a reason to bump. SaveDelta writes base journeys gone by place index, and the world domain as a delta from the base. A code change to any of these functions without a bump makes old saves load against a different base. Usually the Action::Travel consistency check refuses them, but a save can load silently changed when the changed journeys happen to align.

**Evidence:** world_state.hpp:28-30 doc; world_state.cpp:66 Travel::Seed inside GenerateWorld; no test pins a generated world's hash (grep found none).

**Impact:** Pre-1.0 saves may break, which is accepted. The risk is a silent mis-load, or a confusing refusal that doesn't name the generator.

**Recommendation:** Add a test pinning `Host::WorldHash()` (or a hash of GenerateWorld on the small test data) to a literal, with a message telling the editor to bump GENERATOR_VERSION. List the travel functions in the GENERATOR_VERSION doc.

**Verification:** world_state.hpp:28-30 says 'raised whenever generation changes what it makes (StartingPower() included)', so travel seeding is covered by the general wording; only the example list is short. No golden hash literal exists (grep of shipped_world_tests/save_tests found only equality comparisons), so the missing pin is true. Pre-1.0 no-compat memory makes impact low.

### GAME-TRAVEL-NPCS-08: Journals are loaded for any entity ID, not only NPCs; relationships likewise accept Group IDs

- **Severity:** low · **Verdict:** confirmed · **Category:** validation · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/travel.cpp:615`

`journalSound` requires only `state.DomainOf(journal.npc).has_value()`. A journal for a Group, or for a scene-range ID such as the cultivator or a fox in ACTIVE, passes. A scene change then destroys that scene entity and leaves a journal naming a gone ID, which the game's next save fails to reload. `Relationships::Within` similarly accepts any world-range ID that exists, Groups included.

**Evidence:** travel.cpp:614-618; npcs.cpp:230-237 `isNpcId` checks range and existence, not the Npc component.

**Impact:** A crafted save can produce a game whose own saves are later refused. This is a robustness gap rather than a crash.

**Recommendation:** Require IsWorldNpc (or Npc in either domain under a world-range ID) for journal owners and relationship endpoints.

**Verification:** travel.cpp:614-618 journalSound checks only state.DomainOf(journal.npc).has_value(); npcs.cpp:232-234 isNpcId checks only the ID range and DomainOf, not the Npc component. Test at travel_tests.cpp:425 covers 'NPC not there' but a non-NPC existing entity is not what it checks. Robustness gap in crafted saves only.

### GAME-TRAVEL-NPCS-09: LegSound checks the shape with `<= LegShape::TileRoute`, which silently breaks when a shape is added

- **Severity:** low · **Verdict:** confirmed · **Category:** maintainability · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/travel.cpp:178`

Validity of a loaded leg shape is tested as `leg.shape <= core::LegShape::TileRoute`, assuming TileRoute is the last enumerator. journey.hpp says 'a new leg shape is one more case there and nothing that uses legs changes'. A new shape appended after TileRoute would be refused on load. One inserted before it would be accepted without the shape-specific checks (arrive ticks per tile and so on) that LegSound applies only to tile routes.

**Evidence:** travel.cpp:178 `leg.shape <= core::LegShape::TileRoute && ...`; core/journey.hpp:26-28 enum with one value.

**Impact:** A future save-validation hole, or false refusals, when leg shapes are added.

**Recommendation:** Add a `LAST`/`COUNT` sentinel or a `core::LegShapeValid()` helper next to the enum, and branch LegSound per shape.

**Verification:** travel.cpp:178 uses `leg.shape <= TileRoute`; journey.hpp:26-28 has TileRoute as the only enumerator, so the check is correct today and a latent maintenance hazard exactly as described. Speculative until a shape is added.

### GAME-TRAVEL-NPCS-10: Journals stored as 100k separately allocated vectors, saved and hashed whole

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/travel.hpp:254`

Each NPC with a journal owns a `std::vector<JournalEntry>`. At a share of 58 per mille, nearly every NPC has travelled within a few in-game years, so that becomes about 100k heap blocks and about 9.6 MB of save (8 + 8 + 10 × 8 bytes each) written whole and hashed whole. Finish moves every journal into m_merged on each month step.

**Evidence:** travel.hpp:210-218 Journal; travel.cpp:357-358 the whole table moved each Finish; travel.hpp:162-164 Save writes m_journals whole; SaveDelta writes `m_journals` whole.

**Impact:** Save size and hash cost grow linearly with population. Allocation churn as journals reach JOURNAL_LENGTH (erase from the front).

**Recommendation:** Use a fixed-capacity ring per NPC in one flat array (10 entries, 50 for met NPCs in a side table). Consider saving journals as a delta, or truncated for un-met NPCs.

**Verification:** Per-NPC vector journals saved whole is true (design doc 0002 line 293 records 'relationships and journals whole'), so the save-whole part is deliberate. The 100k-journal / 9.6 MB figure is an extrapolation that depends on the travel share and population, not measured here; the allocation point is plausible. A performance suggestion, not a defect.
