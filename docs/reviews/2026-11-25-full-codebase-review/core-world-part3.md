# Review: core-world (part 3)

### CORE-WORLD-21: Test name says a walk table is 'made once per leg' but the cache may make it several times and keep one

- **Severity:** info · **Verdict:** confirmed · **Category:** tests · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/core/leg_walk_cache_tests.cpp:67`

LegWalkCache::Of builds a missing table outside the lock, so several workers can each make the same table and all but one are dropped. The test checks Size() == 2 and that every pointer is shared, which is 'kept once', not 'made once'.

**Evidence:** leg_walk_cache.cpp:35-42; test name 'a walk table asked for from several workers at once is made once per leg'.

**Impact:** A misleading name only.

**Recommendation:** Rename to '... is kept once per leg, every worker given the same table'.

**Verification:** leg_walk_cache.cpp:35-42 makes the table outside the lock and drops losers if Find hits. The test (leg_walk_cache_tests.cpp:67) checks only Size()==2 and shared pointers, so 'made once' is inaccurate; 'kept once' is right. Name only.

### CORE-WORLD-22: Build accepts up to 255 terrain kinds, but the ground and look hold only MAX_GROUND_KINDS (16)

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/world_map.cpp:199`

CheckShape allows `kinds.size() <= 255`, while world_look.hpp:80 says MAX_GROUND_KINDS is 'the most a map may have', and data::ParseWorldLook refuses a look with more. A map with 17 to 255 kinds builds but can never get a look, so the game refuses to start only when the look loads.

**Evidence:** world_map.cpp:199 `source.kinds.size() > std::numeric_limits<u8>::max()`; world_look.hpp:79-80.

**Impact:** A procedural generator emitting more than 16 biomes fails late, with an error about the look rather than the map.

**Recommendation:** Have the map refuse more than MAX_GROUND_KINDS (or a shared constant), or document that the limit is the renderer's and check it where the map is generated.

**Verification:** world_map.cpp:199 allows up to 255 kinds. I did not re-open world_look.hpp:79-80 to check the 16 cap or ParseWorldLook's refusal; the finding's quoted text is plausible. If it holds, a late failure is real, but only matters once a generator emits more than 16 kinds. Design nit, not a bug.

### CORE-WORLD-23: Profile cost precedence makes a bridge override the river: a river-going profile can't pass under bridges unless its bridge cost is set

- **Severity:** info · **Verdict:** confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/world_map.cpp:428`

ProfileLayer checks FLAG_BRIDGE first and prices the tile at min(kind, bridge), ignoring features.river. For a boat-like profile (river passable, bridge impassable, as in the 'water profile' test) every bridge tile blocks the river.

**Evidence:** world_map.cpp:428-436; world_map_tests.cpp:636 expects BRIDGE_TILE impassable for WATER.

**Impact:** Future boat or convoy profiles (mounts and convoys are planned) need to set `bridge` to the river cost by hand, or the river network breaks at every bridge.

**Recommendation:** Document the rule in FeatureCosts, or price a bridge tile as the cheaper of features.bridge and features.river for profiles that can cross rivers.

**Verification:** ProfileLayer (world_map.cpp:428-436) checks FLAG_BRIDGE first and gives min(kindCost, features.bridge), so features.river is ignored on a bridge tile. The cited test expecting BRIDGE_TILE impassable for WATER is consistent. No boat profile ships, so no live impact. A doc line on FeatureCosts would settle it.

### CORE-WORLD-24: Travel rules and the world's population cap live in the map source; WithTravel copies every layer to change one rule

- **Severity:** info · **Verdict:** unverifiable · **Category:** architecture · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/world_map.cpp:580`

TravelRules, PowerBand and MOST_WORLD_PEOPLE are simulation tuning, not geography, yet they live in WorldMapSource and are hashed with the grid. WithTravel copies the whole WorldMap (grid, every profile layer, regions) to change the share.

**Evidence:** world_map.hpp:240 `TravelRules travel;`; world_map.cpp:584 `WorldMap map = *this;`.

**Impact:** With procedural maps the generator's output and the hand-tuned travel rules are tied together: a travel tweak changes the map's identity hash, and WithTravel's copy cost grows with map size.

**Recommendation:** When the generator lands, split the tuning (travel, bands, people cap) into its own hashed game-data table, apart from the generated geography.

**Verification:** world_map.cpp:584 `WorldMap map = *this` and travel inside the source are as quoted from the finding. I did not open WithTravel or the hash code. Whether to split the tuning from the geography is an architecture opinion, for the procedural generator design. Reasoning is sound but speculative. Nothing here is a defect today.

### CORE-WORLD-25: Hash includes TerrainKind::symbol, the grid's text encoding

- **Severity:** info · **Verdict:** unverifiable · **Category:** design · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/world_map.cpp:734`

The symbol only encodes the YAML grid. Renaming a symbol (and rewriting the grid to match) changes nothing in the built map but changes Hash(), and so BaseHash and every save's validity. The test at world_map_tests.cpp:512 expects this.

**Evidence:** world_map.cpp:734 `hasher(kind.symbol, kind.cost);`.

**Impact:** Harmless before 1.0. With procedural maps the symbol is meaningless and a needless input to the identity hash.

**Recommendation:** Consider hashing only what changes the built map (name, cost, grid indices), and drop the symbol when the generator replaces the YAML grid.

**Verification:** The finding says a test (world_map_tests.cpp:512) expects the symbol to be hashed, so it looks intentional for now. I did not open the hash code or the test. Dropping the symbol is a design choice for when the generator replaces the YAML grid. Pre-1.0 saves may break freely (memory: pre-1-0-no-compat), so no impact.

### CORE-WORLD-26: Region search and the reach check deliberately use base costs, so a city may be unreachable for WALKING travellers

- **Severity:** info · **Verdict:** deliberate · **Category:** design · **Status:** contradicts-recorded-decision · **Reviewer confidence:** low
- **Where:** `src/core/world_map.cpp:634`

Build accepts a map where a city is reachable by the base costs but not by the WALKING profile, and regions follow the base costs (spec 5b2 decision 3 'Regions stay on 5b1's costs', RF1). With procedural maps the generator will set profile 0 itself. If profile 0 regularly differs, the regions homing NPCs could include tiles walkers can't leave.

**Evidence:** world_map.cpp:608, 635-647; spec 2026-11-15-phase6-chunk5b2-moving-design.md RF1.

**Impact:** NPCs homed on region tiles their profile can't enter stay home forever (RF1's handling): parts of the population never travel, without any warning.

**Recommendation:** If reopened: compute regions by the WALKING profile, or at least log at Build how many region tiles WALKING can't enter, so a generated map with such pockets is noticed.

**Verification:** world_map.cpp:608-647 matches the description: the region search and reach check use the base layer (Search(map, ...)), while profile layers are built separately. The 5b2 spec records this as RF1: walkers on tiles their profile can't enter stay home, deliberately instead of Build refusing the map. The spec says walking may be stricter than the base on purpose, and the travel tests' isle is such a map. The finding's own status field is 'contradicts-recorded-decision'. The suggested logging at Build is an optional extra, not a defect.
