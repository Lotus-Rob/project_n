# Review: gd-world-sim (part 2)

### GD-WORLD-SIM-11: Each click runs an unbounded whole-map A* on the logic thread

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** performance/scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/paths.cpp:196`

`Host::WalkTo` (host.cpp:978) calls `core::FindShortestRoute(map, foot, target)` inside the tick. The search allocates cost, aside and parent arrays sized to the whole map (about 16 B per tile). When the target is passable but unreachable (an island, or a lake ringed by peaks) it explores the start's entire connected component before giving up. The search has no limit.

Shared-routes keeps the player's click route unchanged ('The player's click route ... unchanged'), so the new route graph won't help it.

**Impact:** On the test map this is fine. On a 10-50x generated map a click on an unreachable island stalls the logic tick for a whole-map search and allocation, a hitch caused directly by input. Being in a recorded action, it also slows replays.

**Recommendation:** - Label connected components per profile at map build, so unreachable targets are rejected in O(1).
- Route long clicks over the shared-routes graph with a fine search only at the ends, or bound the A* by node count.
- Reuse scratch buffers between searches.

**Verification:** I did not open paths.cpp. The whole-map scratch arrays and the lack of a search bound are plausible, and the shared-routes spec keeps the click route unchanged. Whether a hitch occurs depends on generated map size, which only a measurement decides.

### GD-WORLD-SIM-12: Publishing travellers evaluates every journey every frame before culling by view and sight

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** performance/scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1298`

`Host::PublishJourneys` loops over every journey in each chunk. For each underway one it calls `core::Where` for every member (a find_if over the parts plus a binary search), and only then tests `Contains(area, foot)` and `InSight(...)`.

With sight at 3 tiles, almost all of that work is thrown away: a few dozen travellers can be visible, but thousands are evaluated. The 5b2 Outcome claims "the cost follows the view and not the 100,000 people the world could hold". In fact it follows the number of travellers.

**Impact:** Cost per publish grows with the travelling population: about 3-7k journeys today, potentially 10^5 on large worlds or with a higher share. The publish p99 budget the travellers branch fought for (within ~10%) erodes as worlds grow.

**Recommendation:** Keep a coarse spatial index of journeys, rebuilt at the month step: for each sector or tile bucket, the journeys whose route passes through it, with time windows. The publish then walks only the buckets in sight or view. Shared-routes' segments are a natural key. Also skip `Underway` journeys whose route bounding box misses the view.

**Verification:** PublishJourneys (host.cpp ~1285-1330) skips journeys that are not Underway, then calls core::Where per member before the Contains and InSight tests. Cost therefore follows the number of underway travellers, not the view. The Outcome wording was not checked. Per-chunk work is cheap and parallel today, so impact is scale-dependent.

### GD-WORLD-SIM-13: Saving world NPCs as a delta costs more than writing them whole, while doubling world memory and tying loads to regeneration

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** saves/design · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/delta.hpp:10`

Every NPC's `power`, `action` and `Needs` change every month. The 5c1 and relationships Outcomes record 'NPCs with a changed field: 50,000' at every checkpoint, and 2,222,xxx bytes for the changed fields: about 44 B per NPC (u64 place, u64 masks, values).

A whole columnar write of Npc (11 B) + Needs (8 B) + StableId (8 B) is about 27 B per NPC (~1.35 MB). So after the first month, the delta is about 60% larger than a plain snapshot of the same data. It is only smaller for a brand-new game.

The scheme also costs:
- a full copy of the base world kept in memory (`m_base`) for the comparison;
- a field-by-field compare of every NPC at save time (the 25.5 ms save missed its 16 ms aim);
- a full regeneration at every load (Travel::Seed alone takes 37-46 ms on the test map). With a procedural generator and shared-routes' graph build (single-threaded at load, Decision 11), loads grow with generation cost.

**Impact:** For a world where most people change every month, the delta adds complexity, memory and load time while making saves bigger. The real savings (journeys as plans, the empty new game) don't need a per-field delta of hot NPC data.

**Recommendation:** - Keep the delta for slow-changing data (entities created or gone, cell/home, structural components).
- Write hot per-NPC simulation fields whole in columns (or delta against the previous save rather than the base).
- Narrow the places and masks (run-length IDs), as the 5c1 Outcome already lists as a lever.
- Measure the load-time budget against a realistic generator before committing further to regenerate-on-load.

**Verification:** I did not open the Outcomes to check the 44 B and 27 B per-NPC byte figures or the 25.5 ms save time. The design trade-off (delta against base, regenerate on load) is a recorded architecture choice, and the finding's own recommendation is to measure. Cannot be decided by reading.

### GD-WORLD-SIM-14: Shared-routes' place table assumes a fixed 'furthest reach', but reach has no bound

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** design/plan-flaw · **Status:** new · **Reviewer confidence:** medium
- **Where:** `.claude/worktrees/shared-routes/docs/superpowers/specs/2026-11-24-shared-routes-design.md:108`

Goal 7: "For each city and profile, the cost to every place within the furthest reach its people can have, from one bounded search of the graph per city, made at world build."

Reach is `reach_base + reach_per_power x floor(power)` (travel.cpp:223-224), and power grows by 1 for every Cultivate month with no cap (npcs.cpp:99). The furthest reach at world build is therefore not the furthest reach later: within a couple of in-game years every NPC's reach exceeds the build-time bound.

Goal 10 also makes every visit leave from the home city's tile. After that, `Npc::cell` matters only to the tile-partitioned Socialise, so region NPCs' tiles become almost meaningless.

**Impact:** Either the table silently omits destinations NPCs can now reach (travel changes once powers pass the build-time bound), or the implementation must quietly build it for all pairs. All pairs is affordable (4 B a pair) but contradicts the spec's 'bounded search' wording and its budget reasoning.

**Recommendation:** State explicitly that the place table covers all reachable places (all pairs per profile: about 2 MB at 50x is fine), or define a fixed cap on reach in the travel rules. Settle the meaning of `Npc::cell` (home tile vs city) at the same time.

**Verification:** The spec's Goal 7 (line 109) does say 'furthest reach its people can have' from a bounded search. Reach is reach_base + reach_per_power x power. Line 203 of the spec notes that strong cultivators' reach spans the map. Line 254 lists 'how the place table's bounded search takes the furthest reach (the travel rules' top band)' as an open question. The spec therefore already flags the question. The wording gap is real, but it is a known open point, not an overlooked flaw. The all-pairs memory figure is not checked.

### GD-WORLD-SIM-15: Docs still call world.yaml the hand-maintained source, contrary to the procedural decision

- **Severity:** low · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `assets/data/world.yaml:2`

Several documents describe a hand-maintained source map:
- world.yaml's header: "this file is the source from then on (running the script again starts over and loses the hand edits)";
- art/AGENTS.md: "the YAML is hand-tuned after and is the source from then on";
- the river-heads spec's non-goals: "world.yaml is hand-maintained now";
- src/AGENTS.md and 0002 §7: world.yaml is game data forming the save's base identity.

The user's decision of 2026-11-25 (memory `procedural-world-map.md`) says the map is generated per seed and `world.yaml` is only a test map. Nothing in the repo records that decision.

**Impact:** Future agents will keep investing in hand edits to the test map and in shipped-map pins as if they were the product.

**Recommendation:** Add the decision to docs/design (and the AGENTS.md files that describe world.yaml): world.yaml is a test and measurement fixture. Rules that matter for the game belong in the generator spec.

**Verification:** grep for 'procedural' in docs/design, AGENTS.md, src/AGENTS.md and art/AGENTS.md found nothing on the map. The only hit is art/AGENTS.md on procedural placeholder art, which is unrelated. The hand-maintained-source wording therefore stands uncontradicted in the repo. The decision is only in memory. I did not re-open world.yaml's header.

### GD-WORLD-SIM-16: world_look.yaml names the place key `capital`, so any map without it is refused at start

- **Severity:** low · **Verdict:** confirmed · **Category:** data-coupling · **Status:** new · **Reviewer confidence:** high
- **Where:** `assets/data/world_look.yaml:186`

`places.by_key: { capital: painting_capital }`. The reader refuses a key no place has (src/data/world_look.cpp:853-860, `isKey` passed to `PaintingsIn`), and a broken look stops the game at start (src/AGENTS.md).

**Impact:** A generated world (or any test map) without a place keyed `capital` fails to start. Presentation data is coupled to one specific map.

**Recommendation:** Key special paintings by a place property (the start city, a 'capital' role, or size), or downgrade an unknown key to a warning.

**Verification:** world_look.yaml:186 has `by_key: { capital: painting_capital }`. The finding cites the reader refusing an unknown key (world_look.cpp 853-860), which I did not open. The coupling is real for any map without that key. It is a small fix, and the generator spec would normally cover it.

### GD-WORLD-SIM-17: npcs.hpp docs describe world-map behaviour that doesn't exist

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/npcs.hpp:36`

`MAP_COLUMNS` is documented as "Cells in each row of the world map", but it is the 64-column grid of the dev scenes; the world map uses `WorldMap` tile indices.

`ACTIVE` is "The domain of the active region: the arena, or the world map around the player", but the world map has no active NPCs: travellers are journey records, and residents aren't drawn.

`Npc::cell` means two different things depending on the scene (a grid cell or a tile index), and the type doesn't tell them apart.

**Impact:** These comments mislead anyone designing towns or encounters about where world NPCs live and how they are activated.

**Recommendation:** Rename the constants to the dev grid (e.g. `DEV_GRID_COLUMNS`), fix the `ACTIVE` doc, and consider a strong type, or separate components, for a grid cell versus a map tile.

**Verification:** npcs.hpp has 'Cells in each row of the world map' on MAP_COLUMNS, which is the 64-column dev grid. CellCentre's doc, a few lines below, already says it is for scenes without the world map and that with the map Npc::cell is a WorldMap tile index. The MAP_COLUMNS doc is misleading, and Npc::cell is dual-meaning but documented. I did not verify the ACTIVE doc claim about the world map having no active NPCs.

### GD-WORLD-SIM-18: WORLD_BUSIEST and WORLD_PORTAL_OFFSET are fixed to the test map

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** data-coupling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/scenes/world.hpp:20`

`WORLD_BUSIEST` (33,000, 10,800) is pinned to the test map's port tile, and `WORLD_PORTAL_OFFSET` (-560, -200) is measured from the start city. On another map, the portal (into the dev arena) can land in sea or peaks, and the measurement anchor means nothing. The world scene, a game scene, also hosts a portal into a dev scene.

**Impact:** Measurements and the portal break on any other map, and a dev scaffold sits in the player-facing hub.

**Recommendation:** Derive the busiest point from data (the largest city, or the densest traffic at generation), and place the portal on the nearest walkable tile. Plan to remove the portal from the game scene once 5c2's enter prompt exists.

**Verification:** world.hpp:20 and :25 match the claim. The constants are deliberate, though. The chunk5b2 spec (line 166) says WORLD_BUSIEST stays as measure's anchor, pinned to the port tile, and the plan (line 633) keeps both. art/world_map.py also hand-copies BUSIEST. So it is a test-map pin by design, and it is a real gap only once maps are generated. The portal-as-dev-scaffold point is a fair opinion, which I did not check against the 5c2 enter prompt.

### GD-WORLD-SIM-19: Dense per-tile layers and quadratic place checks will not scale to large generated maps

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** performance/scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/world_map.cpp:513`

Dense per-tile layers:
- `WorldMap` keeps a u16 place index per tile (`m_place`) for a handful of places;
- one full u16 cost layer per profile (up to 16) plus a base layer;
- `RoadCurveIndex` holds a 12-byte `optional<Vec2>` per tile plus a temporary f32 per tile, though road tiles are rare.

Quadratic or repeated scans:
- `PlaceTiles` checks duplicate keys by `ranges::find` over all earlier places (O(P^2) string compares);
- `RoadCurves`' `atCity` scans every place for every road end;
- `FindJoin` compares each road end with every point of every other curve.

The region `Search` pushes every unsettled neighbour without checking for a cost improvement (lines 124-129).

**Impact:** Fine at 12,750 tiles and 26 places. At MAX_TILES (16M) with thousands of points of interest, map build and memory become seconds and hundreds of MB.

**Recommendation:** - Use sparse maps for the place and road layers.
- Check duplicate keys with a hash set.
- Use a spatial grid for road joins.
- Keep a best-cost array in `Search`.
- Build only the profile layers a band uses.

These belong in the generator or shared-routes work.

**Verification:** I did not re-read world_map.cpp. The scaling concern is plausible and the data is small today (12,750 tiles, 26 places). The tile-count figure and MAX_TILES are from the finding, not my check. The remedies are generator work, so low severity is right.

### GD-WORLD-SIM-20: Every band reaching every city is guaranteed only by a shipped-map test

- **Severity:** low · **Verdict:** unverifiable · **Category:** validation · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/world_map.cpp:636`

`WorldMap::Build` refuses a city unreachable from the start only by base costs. The roads spec's anti-goal ('a cut-off city: a city that a band can't reach') is enforced only by `shipped_world_tests` ("every band's profile reaches every city from every city"). NPCs on tiles their profile can't enter stay home (RF1), and unreachable cities get weight 0.

**Impact:** A generated map can silently cut off cities for whole power bands, which changes traffic with no error.

**Recommendation:** Make the check a `Build` refusal (or a generator invariant) per band profile, so it holds for every map rather than one fixture.

**Verification:** I did not open Build. The finding says it checks reachability from the start by base costs only, and the roads spec's anti-goal is covered by shipped_world_tests. Reading cannot decide whether a generator would add its own invariant. The generator has no plan yet, so this is a reasonable item to carry into it.
