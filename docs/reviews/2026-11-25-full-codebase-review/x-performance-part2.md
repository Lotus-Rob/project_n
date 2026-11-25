# Review: x-performance (part 2)

### X-PERFORMANCE-11: Whole sprite atlas allocated in VRAM up front with no eviction; mips are regenerated for the whole array on every scene load

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/internal/resources/sprite_array.cpp:31`

CreateSpriteArray makes one texture array with layer_count = manifest.pages (all pages of every scene) and mip_levels, plus a distance-field array. Scenes only mark sprites resident; none is ever evicted. GenerateMips (line 211) regenerates the mips of both whole arrays after every scene load.

**Evidence:** `info.layer_count_or_depth = manifest.pages; info.num_levels = manifest.mipLevels;`; `SDL_GenerateMipmapsForGPUTexture(commands, array.texture.get()); SDL_GenerateMipmapsForGPUTexture(commands, array.field.get());`

**Impact:** Today that is 4 pages x 2048^2 RGBA plus fields and mips (about 85 MB plus fields). Content at Tale of Immortal scale (hundreds of sprites per region, portraits, items, skills) grows VRAM linearly with all content, whatever is in use, and every scene change regenerates mips for every page.

**Recommendation:** Plan residency: page-granular arrays or texture streaming with eviction by scene or LRU, and mip generation per uploaded layer or region (or mips precomputed offline in the atlas). Make the reserved texture-binding bits in SortKey real when a second array lands.

**Verification:** I did not open sprite_array.cpp and could not check the claims about page count, mip regeneration or VRAM size. Whether a whole-atlas array is a problem depends on future content volume, so it is a design opinion. Only measuring VRAM and scene-load time would decide it.

### X-PERFORMANCE-12: RoadCurves' end joins are O(roads x all curve points) and are computed twice (host and renderer); RoadCurveIndex is a dense per-tile optional array

- **Severity:** low · **Verdict:** unverifiable · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/road_curve.cpp:251`

For each road end, FindJoin scans every point of every other road's midline (line 88). RoadCurves runs it four times per road (free and resolved, each end). Host's RoadCurveIndex constructor calls RoadCurves(map), and Renderer::SetWorld calls it again (renderer.cpp:1634), including on every world-look hot reload. RoadCurveIndex stores std::optional<Vec2> for every tile (12 B a tile) plus a temporary f32 per tile, though roads cover a tiny fraction of tiles.

**Evidence:** `for (std::size_t other = 0; other < midlines.size(); ++other) { ... for (std::size_t point = 1; point < line.size(); ++point) {` and renderer `m_impl->roadCurves = map != nullptr ? core::RoadCurves(*map) : ...`

**Impact:** Quadratic in the number of roads, so load time and look-reload hitches grow with procedural road networks. 192 MB of mostly empty optionals at MAX_TILES.

**Recommendation:** Use a spatial grid of midline segments for the join search. Compute the curves once (in core with the map, or share the host's result through GameData) instead of in both modules. Store RoadCurveIndex sparsely (a sorted vector or hash of road tiles).

**Verification:** I did not read road_curve.cpp or the host and renderer call sites, so the O(roads x points) join scan, the double computation and the dense optional array are all unchecked. world_map.cpp:16 does set MAX_TILES = 1<<24, which is consistent with the 192 MB figure. A real procedural road count and a timing of RoadCurves would decide it.

### X-PERFORMANCE-13: GroundTexels does 121 neighbour reads per tile for the shore field, on the main thread at start and on every world-look reload

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/ground_field.cpp:47`

For every tile, ShoreTiles scans an 11x11 window with bounds-checked .at() lookups. GroundRenderer::Update rebuilds the whole field whenever worldVersion changes: SetWorld at start, and each hot reload of world_look.yaml, inside a frame.

**Evidence:** `for (u32 row = 0; row < map.Rows(); ++row) { for (u32 column ...) { const f32 shore = ShoreTiles(map, isWater, column, row, water);` with WINDOW = 5

**Impact:** 121M checked reads at 1M tiles (hundreds of ms) and seconds at MAX_TILES: a startup cost, and a frame hitch on every look edit in debug builds.

**Recommendation:** Use a two-pass distance transform (for example Felzenszwalb or a chamfer pass) bounded at MAX_SHORE_TILES, which is O(tiles), and build it on a worker.

**Verification:** ground_field.cpp:21-38 scans a (2*WINDOW+1)^2 window per tile with an at()-checked isWater lookup and a map.KindAt call, called from GroundTexels line 56 for every tile. I did not check WINDOW's value, which the finding gives as 5 (an 11x11 window), or the hot-reload trigger. The cost is a one-off at load or look reload and is much smaller at today's map sizes. Severity is low.

### X-PERFORMANCE-14: The logic publishes and the renderer uploads a full snapshot every tick, even while the game is paused and nothing changed

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** wasted-work · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/logic.cpp:80`

RunBurst always clears and republishes the snapshot (the full entity packing, the radix sort and the gather), even when every tick of the burst was paused (m_advanced false). Each publish carries a new wallTick, so the renderer's UploadedSnapshot never Holds it and Upload re-copies every instance to the GPU.

**Evidence:** `m_simulation->Publish(snapshot, *m_jobs, culled);` runs unconditionally after the tick loop; renderer `const bool newInstances = !uploaded.Holds(shownId) && ...` with `shownId{ .epoch, .wallTick }`

**Impact:** While the window is unfocused or the world is paused, the stress scene's 100k-instance publish plus a 5.6 MB GPU upload repeat 60 times a second for an identical picture: wasted CPU, bandwidth and battery.

**Recommendation:** Skip Publish when no tick advanced, the view and scene stage are unchanged, and no input changed the presentation, re-handing the previous snapshot's content. Or key the renderer's upload on a content revision rather than wallTick.

**Verification:** logic.cpp:80 calls Publish unconditionally after the burst. The renderer keys the upload on SnapshotId{epoch, wallTick} (renderer.cpp:459-460). logic.cpp:125-126 shows the wall counter advances via Count even when paused, so the snapshot id changes and the upload repeats. So the repeat while paused looks real. I did not check the 100k-instance and 5.6 MB figures. The cost is wasted work on an idle frame, not a correctness problem.

### X-PERFORMANCE-15: EndReleased calls Holds() per ending use, and each scans all flights and all afflicted entities

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/combat.cpp:636`

For every unique use ending this tick, Holds scans every flight and every Afflictions entity. A tick in which many afflictions end (a large crowd burnt by one cast, or many casts) costs O(uses x (flights + afflicted)).

**Evidence:** `for (const u64 use : ending) { if (!Holds(state, use)) {...` and Holds: `std::ranges::any_of(m_flights, pending) ... std::ranges::any_of(entities.view<const Afflictions>(), ...)`

**Impact:** In bullet-heavy fights with hundreds of afflicted enemies and many uses ending at once, the tick's strike phase turns quadratic. It is small today with a few foxes.

**Recommendation:** Build the set of held uses once per tick (pending flights' uses plus live affliction uses, sorted) and binary-search it, making the whole pass O((uses + held) log n).

**Verification:** EndReleased (l.632-640) dedups `ending`, then calls Holds() per use. Holds (l.586-599) scans m_flights and every Afflictions entity. So the cost is O(uses x (flights + afflicted)) as stated. It is only quadratic when many distinct uses end in one tick, and hundreds of afflicted entities are not yet a real scenario. Low is right.

### X-PERFORMANCE-16: The snapshot copies the whole explored bitset every publish, though its version is published beside it

- **Severity:** low · **Verdict:** confirmed · **Category:** wasted-work · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1412`

PublishFog assigns all of m_world.explored into snapshot.fog.explored every tick on the world map. The consumer (FogFade) only reads it when fog.version changes.

**Evidence:** `snapshot.fog.explored.assign(m_world.explored.begin(), m_world.explored.end());`

**Impact:** Small on the shipped map. At 16M tiles that is 2 MB a publish, 120 MB/s of memcpy on the logic thread for unchanged data.

**Recommendation:** Copy only when m_fogVersion differs from the version this pooled snapshot buffer last held (the buffers rotate, so track it per buffer), or hand over the bits through a shared, versioned, copy-on-write buffer.

**Verification:** PublishFog (l.1408-1416) does assign() of the whole explored vector every publish, with fog.version set next to it. 2 MB per publish at 16M tiles is arithmetically right (1 bit per tile). I did not trace the consumer's version gating in gfx. The pooled-buffer detail makes the fix less trivial than the finding implies, but the waste is real.

### X-PERFORMANCE-17: ExploreRoundFoot walks the whole sight diamond every tick even when the foot stays on the same tile

- **Severity:** low · **Verdict:** confirmed · **Category:** wasted-work · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:868`

Tick calls ExploreRoundFoot unconditionally, and ExploreAround visits about 2r^2 tiles (up to about 8k at MAX_SIGHT = 64), setting bits that are already set when the tile hasn't changed.

**Evidence:** `StepPlayerWalk(dashing); ExploreRoundFoot();` every tick

**Impact:** Negligible at sight 3, but up to about 8k bit operations per tick for a high-sight cultivator, the late-game cultivation powers the design anticipates, for nothing.

**Recommendation:** Remember the last tile and radius explored round, and skip when both are unchanged. ExploreRoute already handles the tiles a route crosses in between.

**Verification:** Tick calls ExploreRoundFoot() unconditionally (l.868). ExploreRound (l.1502) calls ExploreAround, which walks the diamond and only returns a count of newly set tiles, so there is no early-out on an unchanged tile. MAX_SIGHT = 64 (core/fog.hpp:19) gives roughly 8k tiles, and the shipped sight is small. Negligible today. The skip also has to handle changes to sightRadius and a map change.

### X-PERFORMANCE-18: Seals are laid out every frame with no view culling

- **Severity:** low · **Verdict:** confirmed · **Category:** wasted-work · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/seal_draw.cpp:872`

DrawSeals lays out every seal in the snapshot, including satellites two levels deep, whatever the camera shows. Host::Publish also adds every combat seal and the portal regardless of the cull area.

**Evidence:** `for (const core::Seal& seal : seals) { if ((seal.showcase && !showcase) || renderTick < seal.startTick || renderTick >= seal.endTick) continue; ... SealDrawOne(...)`

**Impact:** Today only a few seals exist. As seals become a common ability visual (formations, arrays), off-screen ones cost their full layout, and capacity in MAX_SEALS_DRAWN, every frame.

**Recommendation:** Cull by the seal's bounds (point plus size and satellite reach) against the view rect before SealDrawOne, and against the publish area in Combat::Publish.

**Verification:** The DrawSeals loop (l.872) filters only on showcase and time window. There is no view culling. I did not check Combat::Publish's seal cull for the other half of the claim. Seals are few today, so it is future-facing. The cap MAX_SEALS_DRAWN bounds the worst case.

### X-PERFORMANCE-19: World generation (Travel::Seed) runs both seeded months single-threaded, and a load runs the whole generation again

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** performance · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `src/game/travel.cpp:456`

Travel::Seed calls SendOut with jobs == nullptr, so its chunks run in turn ('generation has no job system'), including the first warm-up of every distance map. ReadGameWorld calls GenerateWorld(base, ...) on every load to rebuild the base the delta is read against.

**Evidence:** `SendOut(state, map, paths, nullptr, seed, stateCounter, monthStart - ...); Finish(...); SendOut(state, map, paths, nullptr, ...)`; host.cpp:307 `GenerateWorld(base, data, paths);` in ReadGameWorld. The rebuild of the base on load is recorded on purpose in src/AGENTS.md ('a load makes the base again from the save's seed'); I would reopen it only to cache the base, not to change the save format.

**Impact:** New-game and load time grow linearly with NPC count and, through Paths warming, with map size times cities. A load at Tale of Immortal scale blocks the logic thread for the whole generation.

**Recommendation:** Give GenerateWorld access to the job system (its chunking is already deterministic). Cache the base world across loads of the same seed and data (as ScratchBase does for dev scenes), so a quick reload does not regenerate.

**Verification:** Seed does call SendOut with nullptr jobs twice (l.456, 458). src/AGENTS.md records it explicitly: 'its chunks in turn as generation has no job system'. It also records that a load makes the base again from the save's seed. So single-threading and the base rebuild are documented deliberate choices, though not as a scaling trade-off. The suggestion to cache the base or pass jobs to generation is a reasonable future optimisation, not a defect. The cited line is about 447 and the call sites are 456 and 458.

### X-PERFORMANCE-20: ChooseDestination computes every city's weight twice per traveller

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/travel.cpp:243`

The weightOf lambda (a distance-map lookup that misses the cache, plus fixed-point maths) runs for every place in the summing pass and again in the picking pass.

**Evidence:** `for (place...) total = SaturatingSum(total, weightOf(place)); ... for (place...) { const u64 weight = weightOf(place);`

**Impact:** Doubles the per-traveller cost of choosing a destination: O(places) random memory reads twice per setting-out NPC each month. That grows with the number of cities on large maps.

**Recommendation:** Store the weights in a small per-chunk scratch array during the first pass and reuse them for the pick.

**Verification:** ChooseDestination (l.236-274) defines weightOf and runs it in the summing loop and again in the picking loop. It does a paths.To(city).CostFrom lookup each time, so the work is doubled as claimed. It is deterministic and correct. It is O(places) per traveller, the doubling is a constant factor, and the stored-weights fix is simple.
