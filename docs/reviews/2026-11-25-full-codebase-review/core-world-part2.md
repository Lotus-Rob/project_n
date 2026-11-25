# Review: core-world (part 2)

### CORE-WORLD-11: WorldLook::Hash is a hand-kept field list with inline copies of its own helpers and no guard for new fields

- **Severity:** low · **Verdict:** confirmed · **Category:** maintainability · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/world_look.hpp:260`

The hash lists every field by hand. dangerMarkers (lines 260-267) and fog.clouds (269-272) repeat the `named` and `sprites` lambdas inline, and core already has HashText (archive.hpp). Nothing makes a new field fail to compile or fail a test when it is left out, and the tests check only a few fields (world_look_tests.cpp:299-300, 486-488, 757).

**Evidence:** world_look.hpp:235-244 defines `named` and 224-229 `sprites`, then 260-272 duplicate them inline.

**Impact:** A field added later but missed here makes the data watch treat its edit as no change, so a look edit silently doesn't show until restart.

**Recommendation:** Use the `named` and `sprites` lambdas (or HashText) consistently. Add a test that changes each field in turn (a table of mutators) and expects the hash to differ, or give the structs a Fields() archive listing and hash through it, as the game data does.

**Verification:** The inline copies are real. The dangerMarkers loop at lines 260-267 repeats `named`'s logic by hand. The fog.clouds loop at lines 269-272 repeats `sprites`'s. The hash is a hand-kept field list. I did not confirm that no test varies each field. The silent-miss risk when a field is added is real.

### CORE-WORLD-12: WorldMap::Hash rehashes the whole grid on every call; it could be cached at Build

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/world_map.cpp:736`

Hash() walks every grid byte, profile, line and place on each call. GameData::Hash and BaseHash call it on every debug data reload (app.cpp:199, twice: running and read data), at Host construction and on every save load (host.cpp:288). The map is immutable after Build (WithTravel makes a new one).

**Evidence:** world_map.cpp:736-738 `for (const u8 kind : m_source.grid) { hasher(kind); }`; game_data.hpp:90,99.

**Impact:** Small now. Tens of ms per call on a map near MAX_TILES, on the main thread during a reload and on the logic thread during a load.

**Recommendation:** Compute the hash once at the end of Build and WithTravel and store it in a member.

**Verification:** WorldMap::Hash walks the whole grid on each call (lines 736-738). game_data.hpp:90 and :99 call it, and app.cpp:199 reaches it through GameData::Hash on reloads. The map is immutable after Build, so caching is valid. Cost only matters for very large maps and happens on a reload or load, not per frame.

### CORE-WORLD-13: Full-map arrays for sparse data: m_place, RoadCurveIndex's optional<Vec2>, and a u16 per tile per profile

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/world_map.hpp:394`

WorldMap keeps per tile: flags (1 B), base cost (2 B), one u16 layer per profile (up to 16, so 32 B), place (2 B, though places are a few dozen), region (2 B) and region tile lists (4 B per passable tile), about 44 B per tile with 16 profiles. RoadCurveIndex adds a std::optional<Vec2> (12 B) per tile plus a temporary f32 per tile, though only road tiles have values.

**Evidence:** world_map.hpp:391-396; road_curve.hpp:89 `std::vector<std::optional<Vec2>> m_onRoad; // one per tile`; road_curve.cpp:378 `std::vector<f32> nearest(m_onRoad.size())`.

**Impact:** At MAX_TILES about 700 MB for the map plus about 270 MB for the road index. On the 50x maps the memory anticipates, tens of MB that are mostly empty.

**Recommendation:** Keep places in a small sorted tile-to-place map. Keep the road index as a hash map or a sorted vector of road tiles. Consider computing rarely used profile layers on demand, or packing them.

**Verification:** m_place is a full-map u16 vector (world_map.hpp:394, read through m_place.at at line 321), and RoadCurveIndex::m_onRoad is a full-grid vector of optional<Vec2> (road_curve.hpp:89), as stated. I did not verify the per-profile layer sizes or the 700 MB figure at MAX_TILES. The sparse-data point is valid, but the memory impact is speculative and no maps near that size exist.

### CORE-WORLD-14: LegWalkCache grows with distinct legs drawn per month (per-leg keying, origin per NPC tile)

- **Severity:** low · **Verdict:** deliberate · **Category:** scaling · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/core/leg_walk_cache.hpp:46`

Tables are keyed per distinct (origin, tiles) leg. NPC legs start on their own tiles (3/10 on region tiles), so the number of tables is about the number of distinct legs drawn in a month (about 2,600 on the test map with debug sight), each copying its tiles plus 24 B of marks per tile. libc++'s shared_mutex serialises the readers. The backlog's next design (per-destination walk tables) replaces it.

**Evidence:** leg_walk_cache.cpp:14-18 KeyOf on origin, ends and size; sweep-up-backlog item 'NEXT DESIGN after the travellers branch'.

**Impact:** Memory and first-draw cost scale with crowd size and map size rather than with destinations x profiles.

**Recommendation:** Proceed with the planned per-destination tables built off the publish.

**Verification:** Backlog 'NEXT DESIGN' (sweep-up-backlog.md:107) records exactly this (about 2,300-2,700 tables a month, NPC legs start on own tiles 7/10 vs 3/10) and the planned replacement. src/AGENTS.md also documents about 2,600 tables and the lock trade-off. Finding's '3/10 on region tiles' matches.

### CORE-WORLD-15: ExploreAround doesn't assert the radius cap or the centre that MAX_SIGHT's comment says keep it safe

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/fog.cpp:31`

MAX_SIGHT's comment says it 'keeps a tile's row or column plus the radius far from wrapping in ExploreAround()', but ExploreAround and ForEachInSight accept any u32 radius and any centre. Callers (Host load, creatures.yaml) cap the radius, and nothing checks the centre. With a radius near u32 max, `middle.row + radius` wraps and the diamond is clipped wrongly. A centre past the map explores the bottom rows.

**Evidence:** fog.hpp:41-46 `std::min(map.Rows() - 1, middle.row + radius)`; fog.cpp:33 asserts only the bitset size.

**Impact:** No out-of-bounds write (indices are clamped), but a wrong exploration in debug that goes unnoticed if a future caller (Host::SetSightRadius, a harness) passes an uncapped radius.

**Recommendation:** PN_ASSERT(radius <= MAX_SIGHT) and PN_ASSERT(centre < tiles) in ExploreAround (and ForEachInSight).

**Verification:** ForEachInSight (fog.hpp:41-46) has no assertion on radius or centre, and ExploreAround asserts only the bitset size. But callers are guarded: Host::SetSightRadius asserts and clamps to MAX_SIGHT (host.cpp:1464-65), Load refuses a larger sight (host.cpp:532), and creatures.yaml is read against MAX_SIGHT (game_data.cpp:75). Wrap needs a radius near u32 max, which no caller can pass. Adding asserts is a cheap hardening, nothing more. The MAX_SIGHT comment overstates what the function itself enforces.

### CORE-WORLD-16: Duplicate place-key check is O(P^2)

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/world_map.cpp:513`

PlaceTiles finds each place's key among all earlier places with a linear std::ranges::find, which is quadratic in the number of places (up to 65,534 allowed). RoadCurves' atCity lambda (road_curve.cpp:254) is likewise O(roads x places).

**Evidence:** world_map.cpp:513-515; road_curve.cpp:254-258.

**Impact:** Negligible with 6 cities. Billions of string comparisons with tens of thousands of generated places.

**Recommendation:** Use a sorted copy of the keys or a hash set for the duplicate check, and use m_place (PlaceAt) for atCity.

**Verification:** world_map.cpp:513-515 does a linear ranges::find over earlier places, so O(P^2). road_curve.cpp:254-258 atCity scans all places per call, O(roads x places) at most. Both are real but cheap at 6 cities. This is a build-time cost only. A hash set would fix it. The cap is 65,534 places, so it only bites with a large generated map. Procedural maps are planned (memory: procedural-world-map), so low is right.

### CORE-WORLD-17: Test gaps: JoinedEnd/JoinedHead/Midlines untested in core; no stricter-WALKING-than-base case; no large-map or large-cost cases

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** tests · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/core/road_curve_tests.cpp:1`

JoinedEnd, JoinedHead and Midlines (the rivers' join rules, with yield rules different from roads') have no direct core tests, only whatever gfx river_draw exercises. world_map_tests never builds a map whose WALKING profile is stricter than the base layer to pin down that regions and reachability follow the base. Nothing exercises the u32 route-cost bound, TilesIn with NaN bounds, or Paths with a non-WALKING profile.

**Evidence:** grep finds no test naming JoinedEnd, JoinedHead or Midlines; paths_tests.cpp uses only WALKING.

**Impact:** A change to the river join rules or to which layer regions use would not be caught by the core suite.

**Recommendation:** Add small cases: a river ending beside another's middle (JoinedEnd), two sources side by side (JoinedHead), a profile-0 override making a region tile impassable (regions unchanged, documented), and Paths::To with a second profile.

**Verification:** grep over src and tests: JoinedEnd/JoinedHead/Midlines appear only in src/core/road_curve.{hpp,cpp} and src/gfx/river_draw.cpp, with no core test naming them. tests/unit/gfx/river_draw_tests.cpp exists and probably exercises them indirectly, so the gap is that core has no direct tests, not that nothing covers them. I did not check the other sub-claims (NaN TilesIn, u32 cost bound, non-WALKING Paths) in detail. They are plausible but unverified.

### CORE-WORLD-18: Docs call the base layer 'walking' cost, but the WALKING profile may differ from it

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/world_map.hpp:323`

RegionAt says 'nearest to by walking cost', the single-argument CostAt and StepCost say 'what entering costs', and Build refuses a city that 'can't be reached on foot from the start'. All of these use m_cost, the base layer built from the source's kinds and road and ford costs (OnFoot(source)). Profile 0 (`on_foot` in world.yaml, WALKING) can override kinds and features and be stricter on purpose (spec 5b2 RF1), so 'walking' and 'on foot' here don't mean the WALKING profile's costs.

**Evidence:** world_map.cpp:608 `map.m_cost = ProfileLayer(source, map.m_flags, OnFoot(source));`; world_map.cpp:641 "can't be reached on foot from the start"; data/world_map.cpp:441-470 lets a profile override every kind.

**Impact:** Readers and future code may assume RegionAt or the reachability check match what travellers walk. Regions on base costs and RF1 are deliberate, so the wording is the problem.

**Recommendation:** Rename or re-document: 'by the base costs (the map's kinds, road and ford costs, before any profile)'. Change the refusal to '... can't be reached from the start by the base costs'. Consider renaming CostAt(tile) and StepCost(from, target) to BaseCostAt and BaseStepCost.

**Verification:** m_cost is built as ProfileLayer(source, flags, OnFoot(source)) at world_map.cpp:608, and the region and reachability code use the base layer. The wording issue is real but trivial. I did not check whether WALKING can differ from OnFoot in shipped data. Docs-only.

### CORE-WORLD-19: SHORTEST_RUN is used both as an absolute world length and as a share of the tile size

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** consistency · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/road_curve.cpp:110`

The same 1e-4 constant is used as an absolute world length in FindJoin (`Length(nearest->point - from) > SHORTEST_RUN`) and in Midline's dedup, as a relative factor in Meeting and AheadOnLine, and multiplied by tileSize in RoadCurves (line 296). Its comment covers both uses, but the absolute uses depend on scale.

**Evidence:** road_curve.cpp:110, 138 (absolute), 55, 218 (relative), 296 (x tileSize).

**Impact:** With a much smaller tile size or world unit, the absolute thresholds behave differently from the relative ones. Mostly harmless now.

**Recommendation:** Use separate named constants for the two meanings, or scale the absolute ones by tileSize throughout.

**Verification:** Line 110 does compare an absolute length against SHORTEST_RUN, and line 296 scales it by tileSize, so the mixed use is real. The claim of a scale problem is speculative: tileSize is a fixed map parameter. Cosmetic.

### CORE-WORLD-20: FindJoin's tie goes to the later line, undocumented and unlike RoadCurveIndex's first-on-tie

- **Severity:** info · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/road_curve.cpp:104`

`if (apart <= nearestApart)` lets a later segment or line replace an equally near earlier one, so the last candidate in order wins. RoadCurveIndex documents and uses first-on-tie (strict <).

**Evidence:** road_curve.cpp:104 `apart <= nearestApart`; road_curve.hpp:77 'the first road's in the map's order on a tie'.

**Impact:** Only a presentation choice, but an inconsistent and undocumented one. Two roads meeting at one point can join the other road than a reader expects.

**Recommendation:** Use `<` with the reach check made separately (keep the inclusive reach), and document the tie rule in JoinedEnd's comment.

**Verification:** `apart <= nearestApart` at line 104 means the last equally near candidate wins. The header documents first-on-tie only for RoadCurveIndex (road_curve.hpp:77), and the FindJoin tie rule is not documented. It is deterministic, so the output is stable. Presentation only.
