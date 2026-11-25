# Review: core-world

**Scope:** src/core/world_map.{hpp,cpp}, paths.{hpp,cpp}, road_curve.{hpp,cpp}, fog.{hpp,cpp}, world_look.hpp, leg_walk_cache.{hpp,cpp}. Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 26 standing (0 critical, 0 high, 2 medium, 15 low, 9 info), 0 raised but refuted on verification.

**Parts:** core-world.md, core-world-part2.md, core-world-part3.md

## Assessment

## core-world: world_map, paths, road_curve, fog, world_look, leg_walk_cache

**Strengths**
- The simulation-facing code (WorldMap::Build, StepCost, DistanceMap, RouteDown, the A* searches, Rasterise) uses whole numbers everywhere and breaks every tie in a fixed order. Each priority queue's key is unique per push, so pop order never depends on the standard library's heap. Rasterise puts the endpoints in a fixed order first, so a road gives the same tiles whichever way it was written. The lexicographic (cost, aside) A* with an (h, 0) heuristic is a correct way to break ties among equally short routes. I found no correctness bug that would desync a replay or a save in normal use.
- Validation in Build is thorough, and each refusal names the data line it came from. The tests cover the refusals, the hash, step costs, the corner rule and the region tie-breaks well.
- Presentation code is kept apart from the simulation (road curves, DrawnWhere, LegWalkCache, WorldLook), and the comments say so clearly. The thread-safety annotations on LegWalkCache are correct, and its hash-collision path is tested, because ROAD and DETOUR share a key.
- Header documentation is unusually precise.

**Weaknesses**
- **Scaling is the main risk.** Almost every structure is a full array over the whole map, and almost every search runs over the whole map:
  - a distance map for every (city, profile) pair, kept for the whole run;
  - three full-map arrays allocated on every click's A*, which explores the whole connected area when the target can't be reached;
  - the region Dijkstra, which pushes up to 8N queue entries and fills a cost array that nothing reads;
  - an O(roads × all road segments) pass to join road ends, done twice (renderer and host), and again on every look reload;
  - an optional<Vec2> per tile for the road index.
  
  All of this is fine on the 150×85 test map. It breaks well before MAX_TILES (2^24), and the memory says procedural maps of 2x to 50x are expected.
- **Diagonal steps are rounded down** (cost×7/5): roads cost 4 straight and 5 diagonal, and costs 1 and 2 make a diagonal cost the same as a straight step. This skews routes towards diagonals on cheap ground, roads above all, and feeds the known "travellers cut road corners" item. A test locks the current behaviour in.
- **Naming and documentation:** the single-argument CostAt/StepCost, RegionAt and the "reached on foot" refusal all describe the base layer as "walking", though the `on_foot` (WALKING) profile may differ from the base costs on purpose (RF1).
- **Fragile APIs:** Paths::To is a mutating lazy builder that the parallel month step calls, safe only because of an earlier Warm(). FindRoute is test-only code shipped in the library, and it indexes the profile without a check. WorldLook::Hash is a hand-kept field list with inline copies of its own helpers.
- **Test gaps:** no direct core tests for JoinedEnd/JoinedHead/Midlines, none for a WALKING profile stricter than the base layer, none for large costs or large maps, and nothing that makes sure every WorldLook field reaches the hash.

**Overall health:** good for the current test map. Correct and deterministic, well documented and well tested for behaviour. Least ready for the stated procedural, large-map future: the memory and time cost of the world-derived data grows with tiles × cities × profiles, and some of it lands on the logic or main thread as one-off hitches. The LegWalkCache is already planned for replacement (backlog item 107).

**Top risks:** full-map work per click on the logic thread on large maps; distance-map memory per (city, profile); the cost of joining roads; the bias from rounding diagonals; documentation that blurs "base" and "walking" costs.

**Files read in full:** src/core/world_map.hpp, src/core/world_map.cpp, src/core/paths.hpp, src/core/paths.cpp, src/core/road_curve.hpp, src/core/road_curve.cpp, src/core/fog.hpp, src/core/fog.cpp, src/core/world_look.hpp, src/core/leg_walk_cache.hpp, src/core/leg_walk_cache.cpp, tests/unit/core/world_map_tests.cpp, tests/unit/core/paths_tests.cpp, tests/unit/core/road_curve_tests.cpp, tests/unit/core/fog_tests.cpp, tests/unit/core/leg_walk_cache_tests.cpp, src/AGENTS.md, tests/AGENTS.md

## Findings

### CORE-WORLD-01: Distance maps are kept per (city, profile) over the whole map: memory and build time grow as tiles x cities x profiles

- **Severity:** medium · **Verdict:** confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/paths.hpp:92`

Paths keeps one DistanceMap (a u32 per tile of the whole map) per (profile, place) slot. Warm() builds a full-map Dijkstra for every city, for WALKING and for every band's profile (game/travel.cpp:396-398), and keeps them all for the run.

**Evidence:** paths.hpp:92 `std::vector<std::vector<std::optional<DistanceMap>>> m_maps;` sized profiles x places (paths.cpp:224-227); DistanceMap::Build allocates `Columns()*Rows()` u32 (paths.cpp:39); travel.cpp:396 `paths.Warm(core::WALKING); ... paths.Warm(band.profile);`

**Impact:** On the 12,750-tile test map this is about 600 KB. A procedural map 50x larger (about 640k tiles) with 300 cities and 2 profiles needs about 1.5 GB and 600 full-map Dijkstras (tens of seconds) at world start. MAX_TILES (2^24) allows far more. The procedural-world-map memory says map-derived data must scale with map size (reach limits, hierarchical pathfinding); nothing here does yet.

**Recommendation:** Bound each distance map by the travellers' greatest reach (travel rules cap the cost anyone will walk), so it covers only tiles within that cost, stored sparsely or as a windowed sub-grid. Or move to hierarchical pathfinding (regions or portals) for long trips. Measure memory and build time on a generated large map before chunk 6.

**Verification:** Checked paths.hpp/.cpp. m_maps is profiles x places of optional<DistanceMap>, and Warm builds one full-map map per city per profile (travel.cpp:396-398). The scaling claim holds. The 1.5 GB figure is an extrapolation, not a measurement. The procedural-map memory note says map-derived data must scale with size. This is a future-scale risk, not a current bug.

### CORE-WORLD-02: A player's click runs a whole-map A* on the logic thread, allocating three full-map arrays and exploring the whole connected area for an unreachable target

- **Severity:** medium · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/paths.cpp:136`

Search() allocates `cost` (u32), `astray` (u64) and `parent` (u32) for every tile of the map on every call, whatever the route's length. When the target is passable but can't be reached (an island, a walled valley), A* gives up only after settling every tile reachable from `from`. FindShortestRoute runs inside the tick for each WalkTo.

**Evidence:** paths.cpp:136-138 `std::vector<u32> cost(tiles, UNREACHABLE); std::vector<u64> astray(tiles, ...); std::vector<u32> parent(tiles, UNREACHABLE);`; only `CostAt(target) == IMPASSABLE_COST` is rejected early (line 123).

**Impact:** On large procedural maps each click allocates and fills 16 bytes per tile (about 10 MB at 640k tiles, 268 MB at MAX_TILES), and a click on an unreachable island explores the whole continent. That is a hitch in the tick, and the replayed WalkTo hitches the same way. Repeated clicks repeat it.

**Recommendation:** Label connected components per profile once in Build (a flood fill, one u16 or u32 per tile, or per region) and reject unreachable targets in O(1). Keep the search's scratch arrays in a reusable per-thread buffer, or use a hash map or generation-stamped arrays so only touched tiles cost anything.

**Verification:** Search allocates three full-map vectors per call, and the only early reject is an impassable target. The unreachable-island worst case follows from A* exhausting the connected area. This is not hit on the current small map. No component labels exist.

### CORE-WORLD-03: Region Dijkstra pushes every relaxation without a cost check, fills a cost array no caller reads, and a second full Dijkstra runs only to test reachability

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/world_map.cpp:126`

Search() pushes an entry for every unsettled neighbour of each settled tile without comparing against a best-known cost, so the heap can hold up to about 8N entries. Reach::cost (a u64 per tile) is filled but Build uses only `.owner`. Build also runs a whole Dijkstra from the start city purely to check that every city is connected, which a flood fill would answer in O(N).

**Evidence:** world_map.cpp:126-127 `if (step != IMPASSABLE_COST && !settled.at(next)) { queue.push(...) }`; line 109 allocates `cost` u64 per tile; lines 635 and 647 use only `.owner`; `settled.at()` is bounds-checked in the inner loop.

**Impact:** Wasted memory and time at map build: about 16 B x 8N of heap entries at peak (about 2 GB at MAX_TILES) plus 8 B per tile of unused costs. Build time grows faster than needed on large procedural maps.

**Recommendation:** Keep a best-cost array (or reuse `reach.cost`) and push only on strict improvement, as DistanceMap::Build does. Drop Reach::cost if nothing needs it. Replace the start-reachability Dijkstra with a BFS flood fill or with the component labels suggested for FindShortestRoute.

**Verification:** Read world_map.cpp:105-132. The code pushes on every neighbour that is not settled, with no best-cost array. reach.cost is filled, and I did not confirm any reader. The peak-heap figure of 8N is an upper bound. The inefficiency is real but harmless at current map sizes. I did not verify the separate start-reachability Dijkstra.

### CORE-WORLD-04: RoadCurves is O(roads x all road segments), computed twice (renderer and host), and again on every look reload

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/road_curve.cpp:75`

FindJoin tests each road end's last stretch against every segment of every other road's midline (ROAD_STEPS = 4 segments per tile). RoadCurves calls it up to four times per road: free last and first, then resolved last and first. RoadCurveIndex calls RoadCurves again on the logic thread (road_curve.cpp:382), and Renderer::SetWorld calls it on the main thread at start and on every world_look.yaml reload (gfx/renderer.cpp:1634), although the curves don't depend on the look.

**Evidence:** road_curve.cpp:88-108 nested loops over `midlines` and their points; renderer.cpp:1634 `m_impl->roadCurves = ... core::RoadCurves(*map)`; host.hpp:475 `std::optional<core::RoadCurveIndex> m_roadCurves`.

**Impact:** Ten roads now, so it's invisible. With a generated road network of thousands of roads (for example 2,000 roads of 50 tiles is about 400k segments, so about 3e9 Meeting tests) map setup takes seconds on both threads, and a debug look edit stalls the main thread.

**Recommendation:** Prefilter candidate segments with a uniform grid of segment bounding boxes, or test only segments whose box lies within JOIN_TILES of the end's last stretch. Compute the curves once per map and share them (the host's index and the renderer could take the same immutable vector), and don't recompute them on a look-only SetWorld.

**Verification:** I did not open road_curve.cpp or renderer.cpp. The complexity claim and the double computation are plausible from the described structure, but I can't confirm them. A measurement on a large generated road network would decide it. Severity is low while there are only about 10 roads.

### CORE-WORLD-05: Diagonal cost cost*7/5 rounds down, so cheap tiles price diagonals too low (road 4 -> 5; costs 1 and 2 make a diagonal as cheap as a straight step)

- **Severity:** low · **Verdict:** confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/world_map.cpp:695`

StepOver returns `cost * DIAGONAL_NUMERATOR / DIAGONAL_DENOMINATOR` with integer division. For the shipped road cost 4 a diagonal costs 5 (ratio 1.25, not 1.4). Sand 14 -> 19, forest 16 -> 22. A profile with road or kind cost 1 or 2 makes diagonal and straight steps cost the same (a Chebyshev metric).

**Evidence:** world_map.cpp:695 `return cost * DIAGONAL_NUMERATOR / DIAGONAL_DENOMINATOR;`; paths_tests.cpp:97 locks in `4 x 7 / 5 = 5`.

**Impact:** Routes on roads and cheap ground lean towards diagonal zigzags more than geometry would. A diagonal along a road staircase saves 37.5% against two orthogonal steps instead of about 29%. That makes the known 'travellers cut road corners' look worse, and any future fast profile (cost 1-2) loses the diagonal penalty entirely. Decision 8 fixed the 7/5 rule but not the rounding.

**Recommendation:** Make the ratio exact: price a straight step at cost x DIAGONAL_DENOMINATOR and a diagonal at cost x DIAGONAL_NUMERATOR internally (still whole numbers, u32 is enough), scaling ticksPerCost to match, or round to nearest. Update the tests that lock in the floor. Saves may break before 1.0.

**Verification:** Line 695 is `cost * DIAGONAL_NUMERATOR / DIAGONAL_DENOMINATOR` in u32, so it floors: 4 gives 5, and 1 or 2 gives 1 or 2. paths.cpp:180 shows a static_assert that only SHORTEST_STEP is divisible, so the floor on profile costs was not addressed there. The impact is mild. I did not check whether decision 8 in the specs mentions the rounding.

### CORE-WORLD-06: Route costs are u32 while MAX_TILES x MAX_WALK_COST x 7/5 exceeds it; overflow silently becomes 'unreachable'

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** integer · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/paths.cpp:54`

DistanceMap and Search keep costs in u32 with UNREACHABLE = u32 max. A route costing 4.29e9 or more is never relaxed (`total < cost[next]` fails), so it reads as unreachable rather than failing loudly. The bounds allow it: MAX_TILES = 2^24 and MAX_WALK_COST = 1000 give winding routes up to about 2.3e10. world_map's region search uses u64, so the two disagree.

**Evidence:** paths.cpp:54-57 `const u64 total = reached + step; if (total < cost[next]) { cost[next] = static_cast<u32>(total); ...}`; paths.cpp:162-167 likewise; world_map.cpp:16 `MAX_TILES = 1U << 24`.

**Impact:** Very unlikely on today's maps. On a huge generated map with a high-danger profile, a far city could silently drop out of travel choices instead of tripping an assert.

**Recommendation:** Add a static_assert or a Build-time check that MAX_TILES x MAX_WALK_COST x 7/5 fits in u32, or lower MAX_TILES or keep costs in u64. At least PN_ASSERT when a total reaches UNREACHABLE.

**Verification:** MAX_TILES is 2^24 (world_map.cpp:16) and MAX_WALK_COST is 1000. No static_assert guards the u32 cost, so the bound gap is real. A route of 4.29e9 needs a path of about 3 million tiles at max cost. That is far beyond any plausible map. Silent overflow into unreachable is theoretical. A cheap static_assert or assert would be a reasonable fix.

### CORE-WORLD-07: Paths::To is a mutating lazy builder called from parallel month-step chunks; it is safe only because Warm() ran first

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** thread-safety · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/paths.hpp:85`

To() writes the slot when it is empty. Travel::Month calls `paths.To(city, profile)` inside the forked chunks (game/travel.cpp:248), relying on Warm() having filled every (city, profile) the chunks can ask for. Nothing enforces it: no const accessor, no thread-safety annotation, no assert that the slot is already built while forked.

**Evidence:** paths.cpp:233-236 `if (!slot) { slot = DistanceMap::Build(...); }`; paths.hpp:77 'Not thread-safe: Warm before any parallel reads'; travel.cpp:248 calls To from chunk code.

**Impact:** A future profile chosen inside the fork but not warmed (a new band source, a danger destination, WithTravel adding bands) would be a data race on std::optional: a nondeterministic crash or a desync that only TSan might catch.

**Recommendation:** Split the API: a const `const DistanceMap& At(city, profile) const` that PN_ASSERTs the slot is built, for the parallel path, and keep the lazy To() for serial callers. Or build every (city, profile) eagerly in the constructor.

**Verification:** To() lazily writes the slot. The header already documents 'Not thread-safe: Warm before any parallel reads'. travel.cpp:248 calls To from chunk code, and Warm at 396-398 covers WALKING and the band profiles. The hazard exists only if a future profile is used without warming. The risk is documented but not enforced, so a const accessor with an assert would be an improvement. It is not a current bug.

### CORE-WORLD-08: FindRoute is shipped but used only by tests, and indexes Profiles() without a bounds check

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** dead-code · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/paths.cpp:190`

Nothing in src/ calls FindRoute (the header says it is 'for tests and tools'). It reads `map.Profiles()[profile]` through span::operator[] without a check, while every other profile access uses .at() or asserts.

**Evidence:** grep finds no caller outside paths.cpp and tests; paths.cpp:190 `CheapestEntry(map.Profiles()[profile])`.

**Impact:** Dead production code to maintain. An out-of-range profile is UB rather than an assert.

**Recommendation:** Move FindRoute into tests/support, or keep it and PN_ASSERT `profile < map.Profiles().size()`.

**Verification:** Only the declaration in paths.hpp:66 and the definition at paths.cpp:184 show up in src/ and tools/, so there are no production callers. The header says it is for tests and tools, so it is a documented test or tool API. FindShortestRoute's own A* text refers to FindRoute's rules. The unchecked Profiles()[profile] at paths.cpp:190 is as described. Low impact.

### CORE-WORLD-09: A* heuristic uses the cheapest cost any listed kind could have, not the cheapest tile on the map

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/paths.cpp:17`

CheapestEntry takes the minimum over every kind cost and feature cost in the profile, including kinds and features that appear on no tile. One cheap unused kind (or the road cost, on a map with few roads) drives the estimate down and A* expands nearly as much as Dijkstra.

**Evidence:** paths.cpp:17-22 loops over profile.kindCosts and features regardless of presence.

**Impact:** Wasted search on clicks (FindShortestRoute uses its own fixed price, so mainly FindRoute and any future A* on a profile).

**Recommendation:** Compute each profile's true minimum entering cost from m_profileCost once in Build and expose it.

**Verification:** CheapestEntry (paths.cpp:15-25) takes the minimum over all kindCosts and the four feature costs whether or not any tile has them, so the heuristic can be weaker than needed. It is still admissible, so results stay correct. It is only used by FindRoute, which has no production callers. Performance impact is minor.

### CORE-WORLD-10: Road joins are found against the other roads' unjoined midlines, so a joined end can sit slightly off the other road's final curve

- **Severity:** low · **Verdict:** unverifiable · **Category:** rendering · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/road_curve.cpp:264`

RoadCurves computes every join against `stored` (curves through the authored waypoints), then moves the ends and re-splines each road. When the road being joined also has its own end moved, Catmull-Rom changes the shape of its last two segments, which can reach past the one tile near its end that FindJoin skips. The joining road's end then lies on the old curve, not the drawn one.

**Evidence:** road_curve.cpp:264 `stored = Midlines(waypoints, ...)`; lines 291-307 move `points.back()/front()` and re-run `Midline(points, ...)` for every road, while joins hold points on `stored` segments.

**Impact:** A small visible gap or overlap at a fork when both roads involved have moved ends, and RoadCurveIndex snapping walkers to a point slightly off the drawn ribbon. The shipped-world test checks that ends lie on another curve within a tolerance, so small offsets pass.

**Recommendation:** After moving ends, re-project each joined end onto the final curve of the road it joins (one pass in the same order), or test the worst offset over the shipped and generated maps against a tight tolerance.

**Verification:** The code matches the description: joins are found against `stored` (road_curve.cpp:264) and the ends are then moved and re-splined (lines 291-307). The header documents that each end is judged against the others' joins found with nothing skipped. Whether the resulting offset is visible depends on how far Catmull-Rom shifts the last segments. Measuring the worst offset over the shipped maps would settle it.
