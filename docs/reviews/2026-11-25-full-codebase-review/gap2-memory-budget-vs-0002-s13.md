# Review: gap2-memory-budget-vs-0002-s13

**Scope:** Make a static, quantitative memory account of the shipped game (the 50,000-NPC world on assets/data/world.yaml, the stress scene's 100,000 instances) and compare it with the budgets in docs/design/0002-engine-architecture.md section 13 (process 4 GB, GPU textures and targets 1.5 GB, game state 50 MB for a large world plus one serialised copy while saving, snapshot buffers 4 x about 4 MB at 100k instances, per-tick arenas 16 MB). Read only; no builds. From the types' sizes and container shapes, estimate each resident item: the game world and its kept base (m_world, m_base and the copies held during a load), per-entity EnTT and StableId map overhead, core::Paths distance maps per (city, profile), WorldMap's dense per-tile layers and per-profile costs, RoadCurveIndex, journeys and their route copies, journals and relationships, LegWalkCache, the save buffer plus the zstd output, the replay recording, the handover's snapshot buffers and DrawList's intermediate copies, the glyph pages and text-layout caches, the sprite atlas array with mips, the fog and ground textures, scene targets and the river and road buffers. Give a table: item, formula, bytes on the shipped map, bytes on a map 10x larger, the section 13 line it counts against. Earlier units already flagged many of these pieces one by one as scaling risks (distance maps, world kept twice or four times, journals as separate vectors, whole-atlas VRAM, full-map layers). Do not restate those as findings; add up the totals and judge each budget line: met, broken, or unmeasurable because the statistics section 13 relies on (section 8.9's Tracy plots) don't exist. (Why this unit exists: 0002 section 13 sets concrete memory budgets 'measured and adjusted from phase 3', and the game targets an 8 GB / 2 GB VRAM minimum-spec machine. Earlier units reported about a dozen separate memory-scaling findings, but nobody added them up or checked any budget line against the code. So the review can't say whether the 50 MB game-state budget is already broken several times over, which matters before the procedural maps that are meant to be 2 to 50 times larger.)

**Findings:** 12 standing (0 critical, 0 high, 2 medium, 9 low, 1 info), 0 raised but refuted on verification.

**Parts:** gap2-memory-budget-vs-0002-s13.md, gap2-memory-budget-vs-0002-s13-part2.md

## Assessment

## Memory account of the shipped game against 0002 §13 (static estimate, read-only, no builds)

**What the numbers rest on.** Sizes come from the types (sizeof, EnTT v4.0.0 with 4,096-entry sparse pages and 1,024-entry packed pages, libstdc++ hash nodes of 24 B in 32 B glibc chunks), plus the counts the specs measured on the shipped world: 373,990 relationships, 486,820 journal entries and about 7,000 journeys under way from 10 h of play onward, 5,146 journeys in the base, a 17.7 MB raw save (4.2 MB on disk). These come from the Outcome of `2026-11-19-relationships-bounded-design.md`. The 5b2 Outcome gives 866 B per serialised journey, which works out to about 42 tiles per leg. Shipped map: 150×85 = 12,750 tiles, 6 cities, 2 profiles, 50,000 NPCs. **"10× larger"** here means 10× the tiles (127,500), cities (60) and people (500,000), with the same city density so legs stay about 42 tiles. The code's `MOST_WORLD_PEOPLE` cap of 100,000 would refuse 500,000 people. Each row's cost per NPC, per tile and per (tile × city) is listed, so other assumptions can be read off. Everything is about `main`. The in-flight `shared-routes` worktree will shrink the journey rows.

| Item | Formula | Shipped | 10× map | §13 line |
|---|---|---|---|---|
| EnTT world domain (entity, StableId, Npc, Needs) | N × ~66 B (28 B payload + 4 sparse + 4 packed arrays) | 3.3 MB | 33 MB | game state (per world copy) |
| StableId maps (`Domain::m_entities` + `State::m_where`) | N × ~91 B (2 × (32 B node + ~13 B buckets)) | 4.6 MB | 46 MB | game state (per copy) |
| Relationships `m_edges` | E × 32 B (E ≈ 7.5 N) | 12.0 MB (16.8 MB capacity) | 120 MB | game state |
| Relationships `m_merged` (stale copy kept) | same | 12.0 MB | 120 MB | game state / per-tick |
| Journals | N × (32 + 208 B; capacity 16 for 10 entries) + 32 N B for `m_merged` shells | 13.6 MB | 136 MB | game state |
| Journeys + `m_travellers` | J × ~1,144 B (56 B slot, 32 B members, 352 B parts, 4 × 176 B tile/arrive) + 40 B per traveller | 8.3 MB | 83 MB (about 260 MB if legs grow √10) | game state |
| **m_world, late game** | sum | **53–64 MB** | **540–640 MB** | game state |
| m_base (EnTT, maps, 5,146 journeys; no relationships) | 157 N + 1,144 Jb B | 13.8 MB | 138 MB | game state |
| `core::Paths` distance maps | 4 B × T × C × P | 0.61 MB | 61 MB | game state (map) |
| WorldMap layers | T × (12 + 2P) B + region tiles | 0.22 MB | 2.2 MB | game state (map) |
| RoadCurveIndex | 12 B × T | 0.15 MB | 1.5 MB | game state (presentation) |
| LegWalkCache | ≤ ~2,600 legs × ~1.3 KB (bounded by the view) | 0.3–3.4 MB | same | game state (presentation) |
| KeptWalks | 40 B × J | 0.28 MB | 2.8 MB | game state (presentation) |
| MonthStep buffers | ~32 B × N | 1.5 MB | 15 MB | per-tick |
| **Game state, steady** | | **≈ 68–83 MB** | **≈ 0.75–0.85 GB** | 50 MB |
| Load peak, extra (`m_loaded` world ~36.5 MB + base 13.8 MB + decompressed save 17.7 MB, plus about 6 MB of journeys copied and dropped) | | +72 MB → **≈ 140–155 MB** | ≈ 1.5 GB | 50 MB + one serialised copy |
| Save | raw 17.7 MB (33.5 MB capacity, ~50 MB virtual at the last growth) + `ZSTD_compressBound` 17.8 MB (4.2 MB used) + `file` 4.2 MB + context ~1 MB | ≈ 27 MB used | ≈ 270 MB | "one serialised copy" |
| Replay recording | start state = raw save + 16.3 B a tick (~3.5 MB/h) | 17.7 MB + 3.5 MB/h | 177 MB + 3.5 MB/h | not budgeted |
| Stress scene scratch (100,043 active entities ~190 B each, 50k scratch NPCs ~157 B, scratch base) | | ≈ 35 MB on top of the game world | n/a | game state |
| Handover snapshot buffers | 5 × (56 + 8 B) × instances (+ texts, explored bits) | stress **32 MB**; world map ~2–4 MB | world map: bounded by the view | 4 × 4 MB |
| DrawList staging | instances × 176 B (parts 80 + flattened 96), up to ~256 B with part slack | stress **17.6–25.6 MB**; world ~1 MB | view-bound | per-tick arenas 16 MB |
| FogFade (main thread) | 18–26 B × T | 0.3 MB | 3.3 MB | — |
| Sprite atlas + distance fields | pages × 2048² × (4 + 1) B × 1.332 (5 mips) | **106.6 MiB** | content-bound, not map-bound | GPU 1.5 GB |
| Scene target RGBA16F | 8 B × W × H | 15.8 MiB (1080p), 63.3 MiB (4K) | same | GPU |
| Swapchain (driver) | 2–3 × 4 B × W × H | 16–24 MiB (1080p) | same | GPU |
| Glyph pages | 4 MiB each (~500 glyphs fit in 1) | 4 MiB | same | GPU |
| Fog + ground textures | 2 × 2 B × T | 51 KB | 0.5 MB | GPU |
| Sprite instance buffers | 131,072 × 56 B (×1–3 if SDL cycles) + 5 × 8,192 × 56 B | 7–21 MiB + 2.3 MiB | same | GPU |
| Mesh, ribbon, river and road buffers | fixed caps | ≈ 8 MiB | same (the caps clip content) | GPU |
| **GPU total** | | **≈ 170 MiB (1080p), ≈ 260 MiB (4K)** | same | 1.5 GB |

### Budget verdicts
- **Process total, 4 GB.** Met at shipped scale: about 0.3–0.7 GB with the driver, a load peak and a save. Met at 10× (1.5–2.6 GB at a load) but tight for an 8 GB machine. Broken near 50× (distance maps alone 1.5 GB, world copies about 4 GB). **Unmeasured.**
- **GPU textures and targets, 1.5 GB.** **Met** by static count, with 6–9× headroom. Map size doesn't move it. The risk is content growth: §8.7's planned 16 × 4096² pages plus today's distance-field array come to 1.67 GiB uncompressed. **Unmeasured.**
- **Game state, 50 MB plus one serialised copy.** Met at a new game (~28 MB). **Broken from about 1–10 h of play**: about 68–83 MB steady (1.4–1.7×) and about 140–155 MB during a load (about 2.2× the 67.7 MB the line allows). **Broken about 15× on a 10× map.** Per world NPC, m_world already costs **about 1.1–1.3 KB** with no cultivation, sects, items or detailed tier. §13 sets about 2 KB for a *detailed* NPC and much less for a world-tier record.
- **Snapshot buffers, 4 × 4 MB.** **Broken 2× at 100k** (5 × 6.4 MB = 32 MB). The line itself contradicts §5: five buffers, and instances are now 56 B plus 8 B ids. Met on the world map.
- **Per-tick arenas, 16 MB.** No arenas exist; this scratch lives in capacity-retaining vectors. **Broken at 100k instances** by DrawList alone (17.6–25.6 MB), and by the relationship merge buffer once it counts. Met on the shipped world map, broken at 10×.
- **All five lines are unmeasurable at runtime.** `core/profile.hpp` has only ZONE, FRAME and MESSAGE: there is no plot macro, no TracyAlloc, no GPU allocation tally, no RSS figure in `measure` and no test that bounds bytes. §13's "measured and adjusted from phase 3" never happened for memory. The only hard data is the save-size measurement in the specs.

### Strengths
- Every large structure is plain data with predictable sizes, so a static account is possible at all.
- The relationship and journal caps worked: steady state is flat from 10 h to 1000 h, which the specs measured carefully.
- The GPU side is modest and tied to content, not map size.
- The save and load code checks sizes against `Remaining()`.

### Weaknesses
- The 50 MB line was never checked against the code. Nearly half the bytes are bookkeeping, not game data: the stale merge buffers, hash-map nodes, journal capacity slack, and the base world kept alongside the running one.
- Each separate scaling finding looked tolerable, but together they break the line at the shipped scale, before the game-design gaps (cultivation, sects, items) add any per-NPC data.
- Peak memory is set by loads, not steady play: four world copies plus the decompressed save.

### Risks
- At the planned 2–50× maps, game state grows linearly in NPCs and quadratically in map scale (distance maps). It would pass the 4 GB process line well before 50×.
- `MAX_SAVE_BYTES` (1 GiB) comes into reach at roughly 3 million NPCs, or sooner as the player meets people (their journals and relationships keep 50 entries).
- With no memory statistic, none of this shows up until a minimum-spec machine pages or crashes.

**Files read in full:** docs/design/0002-engine-architecture.md (sections 5, 7.6, 8.1, 8.5-8.9, 13), assets/data/world.yaml, assets/manifest.yaml (header and page counts), src/core/world_map.hpp, src/core/paths.hpp, src/core/paths.cpp, src/core/state.hpp, src/core/ecs.hpp, src/core/ecs.cpp, src/core/journey.hpp, src/core/road_curve.hpp, src/core/leg_walk_cache.hpp, src/core/draw_list.hpp, src/core/draw_list.cpp (Publish), src/core/saves.hpp, src/core/saves.cpp, src/core/save_file.cpp (write/read paths), src/core/replay.hpp, src/core/replay.cpp (record/finish/stop), src/core/logic.cpp (Serialise/Save/Load/recording), src/core/profile.hpp, src/core/frame.hpp (Instance, TextItem, FogView, Snapshot, Action), src/core/handover.hpp (buffer count), src/core/calendar.hpp, src/game/components.hpp, src/game/world_state.hpp, src/game/world_state.cpp, src/game/npcs.hpp, src/game/npcs.cpp (month step, relationships, spawn), src/game/travel.hpp, src/game/travel.cpp, src/game/host.hpp (members, Loaded), src/game/host.cpp (CopyOf, ReadGameWorld, ScratchBase, BuildScene, Save, Load, SetData), src/game/scenes/stress.cpp, src/gfx/internal/resources/sprite_array.cpp, src/gfx/internal/resources/scene_target.cpp, src/gfx/internal/features/sprites.cpp (fog texture, instance buffers), src/gfx/internal/features/ground.cpp (field texture), src/gfx/internal/features/line_ribbons.cpp, src/gfx/internal/features/ribbons.cpp (buffers), src/gfx/mesh_vertex.hpp, src/gfx/text_layout.hpp, src/gfx/text_layout.cpp (Impl caches), src/gfx/renderer.cpp (glyph pages, scene load, buffer creation), src/app/fog_fade.hpp, src/app/fog_fade.cpp, docs/superpowers/specs/2026-11-19-relationships-bounded-design.md (Outcome), docs/superpowers/specs/2026-11-16-phase6-chunk5c1-world-kept-delta-saves-design.md (measurement tables), docs/superpowers/specs/2026-11-15-phase6-chunk5b2-moving-design.md (save size measurements), EnTT v4.0.0 config/config.h, entity/sparse_set.hpp (page sizes)

## Findings

### GAP2-MEMORY-BUDGET-VS-0002-S13-01: The 50 MB game-state budget is broken at shipped scale once the world has run 1-10 hours

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** memory-budget · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/design/0002-engine-architecture.md:535`

Summing the resident game state on the shipped map (50,000 NPCs) at steady state (from 10 h on, using the counts measured in the relationships-bounded Outcome) gives about 68-83 MB. The parts: m_world about 53-64 MB (EnTT 3.3 MB, StableId maps 4.6 MB, relationships 12 MB plus a 12 MB stale merge copy, journals 13.6 MB, journeys 8.3 MB), m_base about 13.8 MB, distance maps 0.6 MB, map layers, road index and leg-walk cache about 1-4 MB. §13 allows 50 MB for a 'large world' holding detailed NPCs at about 2 KB, tens of thousands more as compact records, relationships, sects and the map. A world-tier NPC today costs about 1.1-1.3 KB in m_world alone, with no cultivation, sects or items yet. On a 10x map the same arithmetic gives 0.75-0.85 GB, about 15x the line.

**Evidence:** Relationship = 32 B in memory (npcs.hpp), 373,990 edges measured; JournalEntry 12 B with capacity 16 per 10-entry journal; Journey about 1,144 B each with L about 42 tiles (derived from 866 B per serialised journey in the 5b2 Outcome), about 7,000 under way; unordered_map nodes 32 B plus buckets in Domain::m_entities and State::m_where; m_base holds 5,146 seeded journeys.

**Impact:** The budget meant to size the world tiers is already exceeded before the game-design gaps add per-NPC data. Procedural maps 2-50x larger, as planned, take game state to hundreds of MB or GB on an 8 GB minimum-spec machine. Nobody has noticed, because nothing measures it.

**Recommendation:** Restate the line as a per-NPC and per-tile budget: bytes per world-tier NPC, per detailed NPC, per tile, per (tile × city). Add a test that computes the account from the real containers (sizes and capacities) on the shipped world after N month steps, and fails past the budget. Then decide which of the items in the other findings to cut (stale merge buffer, hash maps, journal slack, base copy). Mark §13 as 'proposed, unmeasured' until then.

**Verification:** Section 13 line (design 0002, ~535) does budget 50 MB for a large world plus 'detailed NPCs at about 2 KB'. The arithmetic is plausible (373,990 edges at 32 B, m_base kept) but is an estimate, and part of the total is the stale merge copy (finding 2) and the base copy, which are separate choices. The 10x claim of 15x is extrapolation. Whether the line is 'broken' depends on whether m_base and the merge copy count as game state. Reduce to medium: real overshoot is likely, but it is unmeasured and has no gate.

### GAP2-MEMORY-BUDGET-VS-0002-S13-02: Memory budgets are unmeasurable: no Tracy plot, allocation or GPU-memory statistic exists, contrary to §8.9 and §13

- **Severity:** medium (reviewer said high) · **Verdict:** confirmed · **Category:** observability · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/profile.hpp:11`

§13 says 'The statistics (§8.9) report each, so a broken budget shows up in Tracy before it shows up as a crash', and §8.9 promises Tracy plots through 'a plot macro beside PN_PROFILE_ZONE' for 'texture and target memory', among others. The profile facade defines only PN_PROFILE_ZONE, PN_PROFILE_FRAME and PN_PROFILE_MESSAGE. Nothing calls TracyPlot or TracyAlloc, the renderer keeps no tally of the bytes it gives SDL_CreateGPUTexture/Buffer, `measure` reports no RSS or peak memory, and no test bounds bytes. The phrase 'measured and adjusted from phase 3' never happened for memory.

**Evidence:** grep finds no TracyPlot, PN_PROFILE_PLOT, TracyAlloc, getrusage, VmHWM or maxrss anywhere in src/, tests/ or tools/project_n.py; profile.hpp lines 11-19 hold the only three macros.

**Impact:** Every §13 line can be broken silently. This review's verdicts are estimates from types, good to perhaps ±30%. A minimum-spec 2 GB VRAM / 8 GB machine would find the problems first.

**Recommendation:** Add PN_PROFILE_PLOT and allocation tracking to the facade. Plot published and culled instances, snapshot and DrawList capacity bytes, and game-state bytes (a Bytes() method on WorldState, Travel, Relationships and Paths). Tally GPU bytes in gfx at every create and destroy and plot them. Add peak RSS and the GPU tally to `measure`'s table. Add a doctest that asserts the game-state account (see the 50 MB finding).

**Verification:** grep over src, tests and tools finds no TracyPlot, PN_PROFILE_PLOT or TracyAlloc. Section 13 says the statistics report each budget, and that does not hold. High is too strong because this is a missing instrument, not a defect. Medium.

### GAP2-MEMORY-BUDGET-VS-0002-S13-03: Relationships keep a full stale copy of the edge table permanently (and Travel keeps its journal shells), doubling the largest game-state item

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** memory · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/npcs.hpp:226`

Merge() and Trim() build into m_merged and swap, without clearing afterwards, 'to keep both capacities'. After every month step m_merged therefore still holds the whole pre-trim table: about 374k edges × 32 B = 12 MB of dead data on the shipped world, 120 MB on a 10x map. Both vectors reach a capacity of 524,288 (16.8 MB each). The capacity being kept serves an operation that runs once per 90 s month and takes about 6 ms, where a 12 MB allocation costs microseconds. Travel::m_merged (travel.hpp:258) keeps about 1.6 MB of Journal shells the same way.

**Evidence:** npcs.cpp:165 `std::swap(m_edges, m_merged);` in Merge, and Trim ends the same way with no clear; npcs.hpp:226 comment 'swapped in, to keep both capacities'.

**Impact:** About 12 MB (17% of today's steady game state) is resident for no purpose, scaling linearly with NPCs.

**Recommendation:** Drop the persistent member: build into a local vector (or merge in place from the back, since the changes are sorted) and let it free. At least clear and shrink m_merged after the swap. Do the same for Travel::m_merged and MonthStep::m_merged if they show up in the account.

**Verification:** npcs.hpp:226 comment reads 'Merge()'s and Trim()'s output, swapped in, to keep both capacities'. npcs.cpp:165 and :199 swap and nothing clears afterwards, and travel.hpp:258 does the same. The swap is deliberate, to avoid per-month reallocation, but the dead copy stays resident. The claim stands: 12 MB dead on the shipped world. The cost is real but modest (a clear plus shrink once a month), so low-medium.

### GAP2-MEMORY-BUDGET-VS-0002-S13-04: StableId lookup maps cost about 91 B per entity, three times the NPC's own components

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** memory · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/state.hpp:182`

Every entity is indexed twice by u64 StableId in node-based std::unordered_map: Domain::m_entities (u64→Entity, ecs.hpp:71) and State::m_where (u64→DomainIndex). Each node is a 24 B payload in a 32 B malloc chunk, plus about 13 B of bucket array, so about 45 B per map and 91 B per entity. The NPC's own components are 28 B (StableId 8, Npc 12, Needs 8). That is 4.6 MB per 50k world, kept in m_world and m_base (9.2 MB), in the scratch world and its base, and about 9 MB more for the stress scene's 100k active entities. World IDs are dense from 1 and never reused, and scene IDs are dense from 2^62.

**Evidence:** ecs.hpp:71 `std::unordered_map<u64, Entity> m_entities;`; state.hpp:182 `std::unordered_map<u64, DomainIndex> m_where;`; State::Create allocates IDs from monotonically increasing counters.

**Impact:** About 13% of steady game state, and a pointer-chasing lookup on every Find/DomainOf in the month step and travel. It scales linearly: 46 MB per world copy at 500k NPCs.

**Recommendation:** Index world-range IDs with a flat vector (or a paged sparse array like EnTT's) holding Entity plus domain in 5 B, and keep a small map only for the scene range. Better still, store the domain in the high bits of the same table, so one table serves both Domain::Find and State::DomainOf.

**Verification:** I did not open ecs.hpp:71 or state.hpp:182. The per-entity overhead of about 91 B is a plausible estimate for node-based unordered_map, but only a measurement would decide it. As a design observation it is reasonable, but a flat table is not clearly better than the hash maps while IDs are sparse across domains.

### GAP2-MEMORY-BUDGET-VS-0002-S13-05: §13's snapshot-buffer line contradicts §5 and is broken 2x at 100k instances

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** docs-vs-code · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002-engine-architecture.md:536`

§13 budgets '4 × about 4 MB at 100,000 instances'. §5 itself specifies five buffers (GameHandover BUFFERS = 5, handover.hpp:45). Instances are 56 B (frame.hpp:71), and each buffer also carries 8 B ids per instance plus texts and batches. At the stress scene's 100,043 instances that is 5 × 6.4 MB = 32 MB. The buffers keep their capacity for the rest of the session, so after one visit to the stress scene the 32 MB stays resident on the world map too. §5 and §8.1 carry a 'this text is the earlier design' note, but §13's budget line was never redone.

**Evidence:** handover.hpp:45 `static constexpr std::size_t BUFFERS = 5;`; frame.hpp:71 `INSTANCE_BYTES = 56`; DrawList::Publish resizes snapshot.instances and snapshot.ids to the full count.

**Impact:** The budget table can't be trusted as a design input, and the real figure is twice the line with no statistic to show it.

**Recommendation:** Rewrite the line as 5 × (64 B × instances + texts) and either accept about 32 MB at 100k or cut it: shrink-to-fit buffers past a high-water mark after a scene change, or hand only the ids the main thread picks with. Do this together with the Tracy plot of snapshot bytes.

**Verification:** handover.hpp:45 has BUFFERS = 5 and frame.hpp:71 has INSTANCE_BYTES = 56, while section 13 says 4 x about 4 MB. The docs contradict the code. The 100k-instance size (about 32 MB of buffers) is arithmetic and the capacity retention is plausible. It is a docs staleness issue with small impact, so low.

### GAP2-MEMORY-BUDGET-VS-0002-S13-06: DrawList holds each instance three times; its staging alone exceeds the 16 MB per-tick line at 100k instances

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** memory · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/draw_list.hpp:118`

Per published instance, DrawList keeps the per-part copies (Instance 56 + SortEntry 16 + StableId 8 = 80 B, with push_back slack), a flattened copy (m_instances 56 + m_ids 8 + m_entries 16 + m_scratch 16 = 96 B), and then gathers a third copy into the snapshot. At 100,043 instances that is 17.6 MB of sizes, up to about 25 MB of capacities, resident for the session after the stress scene. The project has no per-tick arenas; this capacity-keeping scratch is what stands in for the 16 MB line.

**Evidence:** draw_list.cpp:115-117 resize m_instances, m_ids and m_entries to the total and copy every part into them before RadixSort; the gather then copies again into snapshot.instances.

**Impact:** The per-tick line is broken by this one structure at the stress load, and the extra copy is wasted bandwidth in the 2-3 ms publish the §8.1 budget is tight on.

**Recommendation:** Sort keys that carry (part, index), or a prefix offset, and gather straight from the parts into the snapshot. That removes m_instances and m_ids (64 B per instance, about 6.4 MB at 100k). Report the remaining scratch as the per-tick figure in Tracy.

**Verification:** I did not open draw_list.cpp. The three-copy claim is plausible, but the exact per-instance byte counts and capacity slack were not checked. The project has no 'per-tick arenas', so that line is moot. It matters only in the stress scene.

### GAP2-MEMORY-BUDGET-VS-0002-S13-07: Saving holds about three buffer copies, and the serialise vector grows by doubling with no reserve

- **Severity:** low · **Verdict:** unverifiable · **Category:** memory · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/logic.cpp:312`

Logic::Serialise appends into an empty vector. A 17.7 MB late-game save goes through about 25 doublings, ending at a 33.5 MB capacity, with 16.8 MB and 33.5 MB alive at once during the last growth (about 50 MB virtual) and about 35 MB copied. The worker then allocates ZSTD_compressBound(size) (17.8 MB, about 4.2 MB written) and copies the result into a third vector `file`. §13 allows 'one serialised copy while saving'.

**Evidence:** logic.cpp:311-312 `std::vector<std::byte> state; Writer archive{ state };`; save_file.cpp:58 `std::vector<std::byte> compressed(ZSTD_compressBound(state.size()));` and :68 `file.reserve(...)` followed by an insert copy.

**Impact:** The save peak is about 1.5-3x the budgeted copy: about 27 MB touched on the shipped world, about 270 MB at 10x. The doubling also costs serialise time on the frozen tick.

**Recommendation:** Reserve from the last save's size (keep it in Logic). Compress into a buffer that already holds the header and write it directly, or stream with ZSTD_compressStream2 into the temporary file and skip the bound-sized buffer.

**Verification:** I did not open logic.cpp or save_file.cpp. The doubling serialise buffer and the extra compressed copy are plausible. Saving is a one-off allocation of tens of MB, so low.

### GAP2-MEMORY-BUDGET-VS-0002-S13-08: Replay keeps its start state (a full raw save) and tick arrays after recording or playback ends

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** memory · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/replay.cpp:187`

StartRecording moves a whole Serialise(true) into m_bytes (about 17.7 MB late game). FinishRecording copies it into the output and sets m_mode = Off, but never clears m_bytes, m_actions, m_tickStarts, m_held or m_hashes. StopPlaying (line 278) likewise keeps the entire replay in m_bytes. The memory stays until the next recording starts.

**Evidence:** replay.cpp:122 `m_bytes = std::move(start);`, :187 `m_mode = Mode::Off; return replay;` with no clear; :277-280 StopPlaying resets only m_mode and m_start.

**Impact:** About 17.7 MB plus 16 B per recorded tick (about 3.5 MB/h) stays resident for nothing after any F6 or F10 use; 177 MB+ at 10x scale.

**Recommendation:** Release m_bytes and the per-tick vectors in FinishRecording and StopPlaying (swap with empty vectors).

**Verification:** replay.cpp: FinishRecording ends with m_mode = Off; return replay with no clearing of m_bytes. StopPlaying (lines 277-280) resets only m_mode and m_start. So the retained start-state bytes claim holds. I did not check whether StartRecording or StartPlaying already clears the buffers or whether m_bytes is the same size as the save. Impact is a one-off 17 MB after debug F6/F10 use, so low.

### GAP2-MEMORY-BUDGET-VS-0002-S13-09: A loaded dev-scene save keeps the scene's whole active domain in m_scratchBase

- **Severity:** low · **Verdict:** confirmed · **Category:** memory · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:731`

On the scene-entry path the scratch base is CopyOf(world): the world domain only. On the load path, Host::ScratchBase builds a fresh WorldState with BuildScratch, which also runs Scene::Build into its ACTIVE domain, and that object becomes m_scratchBase. Only its world domain is ever compared against. For the stress scene that keeps 100,043 active entities (about 19 MB with their maps) resident until another dev scene replaces the base.

**Evidence:** host.cpp:731-735 `auto built = std::make_shared<WorldState>(...); BuildScratch(*built, SceneOf(scene), context); return built;` versus host.cpp:829 `m_scratchBase = std::make_shared<const WorldState>(CopyOf(world));`.

**Impact:** About 19 MB of dead entities after loading a stress save, and different base contents depending on how the scratch base was made.

**Recommendation:** Return std::make_shared<const WorldState>(CopyOf(*built)) from ScratchBase, matching the entry path, so only the world domain is kept.

**Verification:** host.cpp:727-735 ScratchBase returns `built` whole after BuildScratch (its active domain holds the scene), while the entry path (829) stores CopyOf(world), whose doc (232-236) says active is left empty. Only ReadScratch's caller (1655) uses it. The asymmetry is real and the fix is a one-line CopyOf. The ~19 MB figure is an estimate.

### GAP2-MEMORY-BUDGET-VS-0002-S13-10: §8.7's planned atlas size no longer fits the 1.5 GB GPU line once the distance-field array is counted

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** design-budget · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002-engine-architecture.md:412`

§8.7 sizes the scene arrays as 'sixteen 4096² pages ... about 1.33 GiB uncompressed' against the 1.5 GB GPU line. The glows chunk added a parallel R8 distance-field array with full mips (sprite_array.cpp creates both), which adds 25%: 1.67 GiB for that plan, past 1.5 GB and nearly all of a 2 GB card. BC7 isn't implemented. Today's 4 × 2048² pages cost 106.6 MiB (85.3 colour + 21.3 field), and the GPU line is met at about 170 MiB (1080p) or 260 MiB (4K) in total.

**Evidence:** sprite_array.cpp CreateSpriteArray creates `sprite atlas` (R8G8B8A8_UNORM_SRGB) and `sprite fields` (R8_UNORM), both manifest.pages × pageSize² × mipLevels; manifest.yaml pages: 4, page_size: 2048, mip_levels: 5.

**Impact:** When content grows, the page budget will be chosen from a figure that is 25% low. With RGBA8 + R8 the line allows at most about 13 pages of 4096² (about 55 of 2048²) and leaves nothing for targets.

**Recommendation:** Redo §8.7's arithmetic with the field array (per 2048² page: 26.6 MiB uncompressed, about 7.1 MiB with BC7 + BC4). State the page cap the GPU line implies, and enforce it in `project_n atlas`.

**Verification:** sprite_array.cpp:114 creates a second R8 field array, and the manifest has pages 4, page_size 2048, mip_levels 5. §8.7 (line 412) does still state 1.33 GiB with no field array, and the +25% arithmetic holds (R8 vs RGBA8). However, the same section carries a dated note saying the glows chunk added the field array and that the text is the earlier design, and the 16 x 4096² figure is a hypothetical, not a shipped plan. So the doc is flagged stale, not silently wrong. The 'enforce page cap in atlas' recommendation is reasonable. Info or low.
