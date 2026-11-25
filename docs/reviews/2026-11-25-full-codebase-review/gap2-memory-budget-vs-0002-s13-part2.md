# Review: gap2-memory-budget-vs-0002-s13 (part 2)

### GAP2-MEMORY-BUDGET-VS-0002-S13-11: Every publish copies the whole explored bitset into the snapshot instead of sharing it

- **Severity:** low · **Verdict:** confirmed · **Category:** memory · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:1412`

Each world-map publish assigns m_world.explored (T/8 bytes) into snapshot.fog.explored, so all five buffers hold copies and it is copied up to 60 times a second, though it changes only when the cultivator explores. §5 asks that data which rarely changes travel as shared_ptr<const T>, reused across buffers. The memory is small today (1.6 KB per copy, about 80 KB on a 50x map), but the per-publish copy and the FogFade full-map pass it triggers scale with tiles.

**Evidence:** host.cpp:1412 `snapshot.fog.explored.assign(m_world.explored.begin(), m_world.explored.end());`; FogView::explored is a std::vector<u64> (frame.hpp:360).

**Impact:** Wasted copies that grow linearly with the map; a small deviation from the handover design.

**Recommendation:** Publish the explored bits as shared_ptr<const std::vector<u64>>, rebuilt only when FogVersion changes.

**Verification:** host.cpp:1412 does `snapshot.fog.explored.assign(...)` on every PublishFog, and FogView::explored is a std::vector<u64>. Design §5 (line 164) says rarely-changing data goes in the snapshot as shared_ptr<const T>. A m_fogVersion already exists to key a rebuild. The copy is tiny today (1.6 KB), so the impact is low.

### GAP2-MEMORY-BUDGET-VS-0002-S13-12: MOST_WORLD_PEOPLE caps a world at 100,000 people while maps are meant to be 2-50x the shipped one

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/world_map.hpp:57`

WorldMap::Build refuses cities whose people sum past 100,000, twice the shipped 50,000. The procedural-map plan calls for 2-50x larger maps. Either people don't scale with the map (cities get emptier), or the cap and every per-NPC cost in the account must scale with it. The memory account shows the game-state line breaks long before 100,000 people (about 140-165 MB steady at 100k).

**Evidence:** world_map.hpp:55-57 doc comment and `inline constexpr u32 MOST_WORLD_PEOPLE = 100'000;`.

**Impact:** The world-scaling plan and its memory and save budgets rest on an unstated assumption about population density.

**Recommendation:** Decide and record how population scales with map size, then size §13's game-state line and MAX_SAVE_BYTES (1 GiB; reached at about 3M NPCs at 354 B of raw save each, sooner with met NPCs) from that decision.

**Verification:** The MOST_WORLD_PEOPLE = 100'000 constant and its comment are as quoted (world_map.hpp:55-57). The comment says it is deliberately the size the performance gates were set for, so it is a documented bound rather than an oversight. The population-vs-map-size question for procedural maps is a valid open design point, but it is a design opinion and not a code defect. The 140-165 MB figure is unverified.
