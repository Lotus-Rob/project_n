# Review: x-performance

**Scope:** CROSS-CUTTING performance and scaling over src/: per-frame and per-tick hot paths, allocations in loops, O(n^2) or worse over entities/tiles/NPCs/projectiles, work done for culled or unused results, cache behaviour, GPU upload volume, and what breaks first when the world map is very large and NPC counts reach Tale-of-Immortal scale (thousands of simulated NPCs).

**Findings:** 29 standing (0 critical, 0 high, 4 medium, 21 low, 4 info), 0 raised but refuted on verification.

**Parts:** x-performance.md, x-performance-part2.md, x-performance-part3.md

## Assessment

## Cross-cutting performance and scaling over src/ (x-performance)

**Strengths**
- The hot paths are built with care. The publish is chunked over the job system, and each part sits on its own cache line. The draw list uses a radix sort that skips constant bytes. Snapshot buffers are pooled and keep their capacity. Label overlap uses a grid. Text layout has a per-frame time budget. Glyphs rasterise on a worker. Leg walk tables are cached and each journey keeps its own walk. Terrain is laid out again only when the tile span or zoom changes. The fog texture uploads only when its revision changes. Active NPC decisions are time-sliced. Month-step work is chunked deterministically. Many cases where work is skipped on purpose are documented.
- Determinism and performance are kept apart where it counts: fixed chunking whatever the worker count, presentation-only caches, nothing hashed that the view decides.

**Weaknesses (what breaks first as the map and NPC counts grow)**
1. **Routing memory and time scale with tiles × cities × profiles.** `core::Paths` keeps a full-map `u32` distance map for every city and profile. Each is built by a whole-map Dijkstra, all of them warmed on the first month and at generation, on one thread. On the shipped 150×90 map this is under 1 MB. On a 1024×1024 procedural map with 60 cities and 3 profiles it is about 720 MB and tens of seconds of world generation. This is the first hard wall for "maps generated per seed that may be very large".
2. **World-wide GPU buffers have fixed caps.** Roads and rivers are built once for the whole map into fixed buffers of 8,192 quads each (8 quads per tile of road or river). The shipped map already uses 31% of the road cap and 44% of the river cap, so a map 2 to 3 times larger silently drops whole roads and rivers.
3. **The fog is handled whole-map on the main thread.** `FogFade::SetTargets` rebuilds a float target per tile for the whole map and scans every tile each time the player's tile changes. While a fade runs, the whole-map fog texture is re-uploaded every frame. This costs O(tiles) per step and per frame, where O(sight area) would do.
4. **Publish work runs over everything, not only what is in view.** `PublishJourneys` walks every journey each publish, and the entity loop builds every instance before culling it. Nothing is indexed by space, so publish cost scales with world population rather than with what is in view.
5. **Some per-frame presentation work is O(n²) or repeated.** Bridge headings scan every road curve point on the map. Volleys recompute each sword's plan about 12 times a frame, and the target walk is O(T²) per sword. The stress scene's orbit loop runs single-threaded with two separate deterministic `Sin` calls and one `Cos` per entity per tick.
6. **Resource residency.** The whole atlas texture array is allocated up front and nothing is ever evicted. That is fine at 4 pages but does not scale to Tale of Immortal–sized content.

**Overall health:** good at today's demo scale (50k NPCs, about 3k journeys, a 14k-tile map), and the code measures itself well (Tracy zones, `measure`). The architecture has the right seams: chunked jobs, presentation-only caches, snapshot pooling. But several systems are built "per map" rather than "per view" or "per region": distance maps, ribbons, the fog texture, road curve indices, the ground field, terrain bridge lookups. That will need redesigning before large procedural worlds. Routing in particular should move to hierarchical or regional pathfinding, or cached routes between city pairs, rather than full-map fields per city.

**Risks:** world generation and load time (a load regenerates the base world) grow with the whole map's Dijkstra fields. Main-thread hitches come from the fog rebuild on tile changes and from rebuilding the ground field and road curves on a look reload. The logic thread hitches on long player clicks (full-map A* with three full-map arrays) and on the per-second hash during replays. Parts of large maps go missing silently once the quad caps are hit (logged only once).

**Files read in full:** src/AGENTS.md, src/game/host.cpp, src/core/logic.cpp, src/core/draw_list.cpp, src/core/draw_list.hpp, src/core/instances.cpp, src/core/journey.cpp, src/core/paths.cpp, src/core/paths.hpp, src/core/world_map.cpp, src/core/world_map.hpp, src/game/travel.cpp, src/game/npcs.cpp, src/game/npcs.hpp, src/game/world_state.cpp, src/core/fog.cpp, src/core/leg_walk_cache.cpp, src/core/road_curve.cpp, src/gfx/renderer.cpp, src/gfx/internal/features/sprites.cpp, src/gfx/internal/features/sprites.hpp, src/gfx/fog.cpp, src/app/fog_fade.cpp, src/app/fog_fade.hpp, src/gfx/cloud_draw.cpp, src/gfx/terrain_draw.cpp, src/gfx/stamp_draw.cpp, src/game/combat.cpp, src/core/volley.cpp, src/gfx/volley_draw.cpp, src/gfx/labels.cpp, src/app/view.cpp, src/core/jobs.cpp, src/game/scenes/world.cpp, src/game/scenes/stress.cpp, src/game/components.cpp, src/game/scene.cpp, src/gfx/road_draw.cpp, src/gfx/road_draw.hpp, src/gfx/ground_field.cpp, src/app/game.cpp

## Findings

### X-PERFORMANCE-01: Paths keeps a full-map distance map per city per profile, all warmed at once on one thread: memory and generation time scale as tiles x cities x profiles

- **Severity:** medium (reviewer said high) · **Verdict:** confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/paths.cpp:230`

Paths::To builds and keeps DistanceMap::Build for each (profile, city): a std::vector<u32> of Columns*Rows entries, filled by a Dijkstra over the whole map (line 38). Travel::SendOut calls paths.Warm(WALKING) and Warm(band.profile) for every band (travel.cpp:396-399), which builds every city's map for every profile. Travel::Seed runs this during GenerateWorld without the job system, and ReadGameWorld runs GenerateWorld again on every load. The lookups in ChooseDestination then read one cache-missing u32 per city per traveller.

**Evidence:** `std::optional<DistanceMap>& slot = m_maps.at(profile).at(city); if (!slot) { slot = DistanceMap::Build(...) }`; DistanceMap::Build: `std::vector<u32> cost(static_cast<std::size_t>(map.Columns()) * map.Rows(), UNREACHABLE);`

**Impact:** Shipped map: about 14k tiles x 4 B x 6 cities x 2 profiles, under 1 MB. A procedural 1024x1024 map with 60 cities and 3 profiles: 4 MB x 180 = about 720 MB resident, plus 180 whole-map Dijkstras at generation, serially. MAX_TILES allows 16M tiles, where each map is 64 MB. This is the first hard wall for large per-seed maps.

**Recommendation:** Replace the per-city full-map fields with hierarchical routing: region or cluster graphs (HPA*), or a precomputed city-to-city route table along roads plus local A* from the NPC's tile to the nearest node. Alternatively, store fields only inside each city's reach window, or compress them (u16 and region-limited). At the least, build the Warm calls in parallel and log the memory use.

**Verification:** Paths::To lazily builds a full-map DistanceMap per (profile, city); Warm builds all (paths.cpp:229-247, travel.cpp:396-399). The arithmetic holds. The scaling concern is real, but the shipped map is under 1 MB and procedural maps are a future item (procedural-world-map memory), so I rate it medium rather than high. The backlog (sweep-up-backlog.md:107) already plans per-destination tables on top of these maps. Parallelising Warm is a fair side recommendation.

### X-PERFORMANCE-02: Road and river ribbons for the whole map share fixed 8,192-quad buffers; larger maps silently drop whole roads and rivers

- **Severity:** medium (reviewer said high) · **Verdict:** confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/road_draw.hpp:19`

DrawRoads and DrawRivers lay out every road and river on the map once, into buffers capped at MAX_ROAD_QUADS = MAX_RIVER_QUADS = 8192 (river_draw.hpp:22). Each tile of a curve takes ROAD_STEPS/RIVER_STEPS = 4 quads per band and there are 2 bands, so 8 quads per tile: about 1,024 tiles of road and of river for the whole world. On the shipped map, roads take about 2,536 quads (31%) and rivers about 3,584 (44%), counted from world.yaml.

**Evidence:** `inline constexpr u32 MAX_ROAD_QUADS = 8'192;`, `const std::size_t taken = 2 * (curve.size() - 1); if (quads + taken > MAX_ROAD_QUADS) { PN_LOG_WARNING(...); break; }`

**Impact:** A procedural map 2 to 3 times the shipped one's area overflows the caps. The later roads and rivers are left out whole, with only a one-time warning, so towns lose their roads and rivers stop mid-map. The cap is per map, not per view, so it cannot be fixed by culling.

**Recommendation:** Build the ribbons per view or per map chunk (tile the map into sectors and upload the sectors in view), or size the buffer from the map's total at SetWorld. Lower the step count with zoom (fewer quads per tile when zoomed out). Add a test that asserts the shipped map stays under, say, 50% of each cap.

**Verification:** road_draw.cpp:21 and river_draw.cpp:150 cap at 8192 quads and break, dropping the later roads and rivers whole with a warning. The header documents the behaviour and the roads spec records the 8192 cap, so it is known. It will bite once maps are procedural and larger. Severity is medium because the shipped map is at 31% and 44%. A test that asserts headroom is a reasonable addition.

### X-PERFORMANCE-03: FogFade rebuilds whole-map targets and scans every tile each time the player's tile changes, and the whole-map fog texture is re-uploaded every frame of a fade

- **Severity:** medium (reviewer said high) · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/fog_fade.cpp:20`

Source includes playerTile, so FogFade::Update calls SetTargets each time the player crosses a tile, and whenever FogVersion changes. SetTargets assigns m_target to tiles*2 floats, tests IsExplored for every tile, and then FindMovers scans every value. While any value eases, Ease bumps m_revision every frame a byte changes. FogTexture::Update (sprites.cpp:322-333) then maps and copies the whole columns*rows*2-byte texture, even though only the old and new sight diamonds changed.

**Evidence:** `m_target.assign(tiles * 2, 0.0F); for (std::size_t tile = 0; tile < tiles; ++tile) { m_target[tile * 2] = core::IsExplored(...) }` and FogTexture::Update `CopyFog(m_device, copy, m_transfer.get(), m_field.get(), fog.values.first(bytes), m_columns, m_rows)` whenever `m_held != fog.revision`

**Impact:** With a 0.5 s fade and a walk speed of about a tile a second, a whole-map rebuild runs on the main thread every tile, and a whole-map texture upload runs on about half of all frames while walking. At 1M tiles that is about 8 MB of float writes and 2M compares per tile step, plus 2 MB of uploads per frame (over 150 MB/s at 80 fps). At MAX_TILES 16M it is far worse. Trivial on the shipped map, so it won't show in `measure` today.

**Recommendation:** Make SetTargets incremental: clear the old sight diamond, set the new one, and OR in only the newly explored tiles (Host could publish the explored delta or its bounding box). Keep the movers only. Upload a dirty rectangle (the union of the bounding boxes of the changed tiles) with SDL_UploadToGPUTexture on a sub-region instead of the whole map.

**Verification:** fog_fade.cpp:20-35 assigns tiles*2 floats and scans every tile whenever Source changes. Source includes playerTile (line 89-96), so it runs on every tile crossing. FindMovers scans everything again. Frame() hands over the whole m_bytes with a revision, so the upload is whole-map (I did not open sprites.cpp, so the per-frame upload is taken from the finding). It is trivial at the shipped map size, and nothing in the fog spec records it as deliberate. Medium, since it only matters at large scale.

### X-PERFORMANCE-04: PublishJourneys walks every journey and every member each publish; no spatial index

- **Severity:** medium · **Verdict:** confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1290`

Each publish (every tick) scans all journeys. For each, it runs Underway, then core::Where for every member (a find_if over the parts plus an upper_bound over arrive ticks), then the area and sight tests, even though only travellers near the player are ever drawn (the sight radius plus one, unless the debug sight is on). Journeys are sorted by leader ID, so nothing can stop early. PUBLISH_CHUNK of 4096 journeys a chunk also means the shipped map's roughly 3k journeys run as one chunk on one worker, which cannot balance the load.

**Evidence:** `for (std::size_t index = begin; index < end; ++index) { ... if (!core::Underway(journey, now)) continue; for (member...) { const core::Vec2 foot = core::Where(map, journey, now, member); if (!core::Contains(area, foot) || ...) continue;`

**Impact:** Cost is O(world travellers) per tick rather than O(travellers in view). At MOST_WORLD_PEOPLE (100k) with a high share, tens of thousands of Where calls run each tick for a handful of drawn walkers. That is the wasted work the 'avoid wasted work' rule forbids.

**Recommendation:** Keep a coarse spatial bucket per journey, updated at each month step from the tiles each leg crosses (or by tile bands over time), and visit only the buckets that overlap the cull area, or the sight diamond when the debug sight is off. Use a smaller journey chunk (for example 512) so the work spreads over workers.

**Verification:** host.cpp:1290-1300 loops over every journey in a chunk. It skips non-underway ones and calls core::Where per member before the area test, so the cost is O(travellers) per publish. PUBLISH_CHUNK is 4096 (host.cpp:111). The travellers spec and backlog already discuss publish cost, but not a spatial index. Real at 100k scale, though the shipped world is measured as fine.

### X-PERFORMANCE-05: Publish builds every entity's instance (two f64 std::remainder calls, a glow lookup, a dissolve) before DrawList::Add culls it

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** wasted-work · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1829`

The entity loop calls DrawnPoseOf, DissolveAt, DemoGlow(DemoRoleOf(...)) (a linear find over demoGlows) and core::MakeInstance (manifest lookup, 6 half packs, 2 f64 remainders) for every Look entity. DrawList::Add only then discards those whose position lies outside the view plus margin (draw_list.cpp:92). The shadow instance is built the same way.

**Evidence:** `m_drawList.Add(chunk, look.layer, look.pipeline, core::MakeInstance(*m_manifest, {... .glow = DemoGlow(DemoRoleOf(demoGlows, look.sprite), tick) }), ...)`; culling happens inside Add.

**Impact:** In the stress scene (about 100k entities) and in any large crowd, the culled majority pays the full instance build every tick at a zoomed-in view. This contradicts the 'hot paths compute only what is drawn' rule recorded in memory.

**Recommendation:** Test the body's current position against the cull rectangle (DrawList could expose `Culls(Vec2)`) before calling MakeInstance and the other helpers.

**Verification:** I did not read the entity loop at host.cpp:1829 or DrawList::Add. The claim that instances are built before culling is plausible given the cited evidence, but I could not confirm it. Profiling the stress scene would decide it.

### X-PERFORMANCE-06: FindShortestRoute runs a full-map A* with three full-map arrays inside the logic tick on every click

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/paths.cpp:136`

Host::WalkTo (host.cpp:979) calls core::FindShortestRoute synchronously in Tick. Search allocates cost (u32), astray (u64) and parent (u32) for every tile, 16 B a tile, on each call. Because of the 'aside' tie-break, a uniform-cost grid explores many equal-f nodes. For an unreachable target that is still passable (across water), it exhausts the whole connected component before returning empty.

**Evidence:** `std::vector<u32> cost(tiles, UNREACHABLE); std::vector<u64> astray(tiles, ...); std::vector<u32> parent(tiles, UNREACHABLE);`

**Impact:** Each click allocates 16 MB at 1M tiles, and a long or unreachable click on a large map can take tens of milliseconds, a tick hitch on the logic thread that every replay of it repeats. The target cursor's Confirm and edge cases (an island target) are worst.

**Recommendation:** Reject unreachable targets in O(1) with a connected-component label per tile (computed once in WorldMap::Build). Keep reusable scratch arrays with a generation counter instead of reallocating. Bound the search by a window round the line from start to goal. For large maps use the same hierarchical graph as for NPC routing.

**Verification:** I did not open the Search allocations at paths.cpp:136. The 16 B per tile figure and the per-click cost rest on the finding's own evidence. A click-time A* over the whole map is plausible, and a measurement on a large map would decide it. Low until maps grow.

### X-PERFORMANCE-07: Bridge heading scans every point of every road curve on the map for each bridge tile, on every terrain re-layout (each zoom frame)

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/terrain_draw.cpp:59`

AddCrossings calls HeadingNear for each bridge tile in the span, and HeadingNear loops over all roadCurves and all of their points. The terrain is laid out again whenever the tile span changes, and whenever the zoom changes at all: TerrainKey compares the zoom bitwise, so every frame of a smooth zoom lays it out again.

**Evidence:** `for (const std::vector<core::Vec2>& curve : curves) { for (std::size_t index = 0; index < curve.size(); ++index) { ... } }` called from `pass.ForTiles(...)` per bridge tile

**Impact:** O(bridges in view x all road curve points on the map) per re-layout. On a procedural map with thousands of road tiles (4 points per tile), every zoom frame pays millions of distance tests on the main thread.

**Recommendation:** Precompute each bridge tile's heading once in SetWorld (or use RoadCurveIndex::OnRoad plus the segment it came from) and store it per tile. Separately, consider quantising the zoom in TerrainKey for stamp fading so a zoom animation doesn't lay out the whole span every frame.

**Verification:** terrain_draw.cpp:59-72 HeadingNear loops over every point of every curve. It is called per bridge tile inside ForTiles (line 81), so the cost is bridges in view times total curve points. Whether a zoom frame re-lays-out the terrain I did not verify. The shipped map has few bridges and curve points (about 640 road tiles), so it is cheap today. Low, since it scales only with procedural maps.

### X-PERFORMANCE-08: Volleys recompute each sword's plan about 12 times a frame, and the target walk is O(T^2) per sword

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/volley_draw.cpp:92`

Each frame, for every sword, DrawSword calls LaunchTick and ArrivalTick (each runs PlaceInWave's binary search over WaveStart's f64 maths, and Departure's random draw), then SwordAim and SwordTarget (each runs WalkTargets), then SwordAt for the head plus TRAIL_SEGMENTS = 8 more SwordAt calls for the trail. Each SwordAt calls Plan again, with 4 Random draws and the deterministic software Cos/Sin. DepartureTick is computed yet again for the trail. In volley.cpp:240-266, WalkTargets runs count_if over all targets and a second scan for each move, up to T moves: O(T^2) per sword per call, twice per sword per frame. Nothing is culled to the view either.

**Evidence:** `const u64 launch = core::LaunchTick(volley, sword); ... const u64 arrival = core::ArrivalTick(volley, sword); const core::Vec2 aim = core::SwordAim(...); ... core::SwordTarget(...); const core::SwordPose head = core::SwordAt(...)`; DrawTrail: `points.at(point) = core::SwordAt(volley, sword, ..., aim).position;` x8

**Impact:** A 1,000-sword volley costs about 12k plan computations (each with software trig) and up to 2,000 x T^2 target scans per frame. The cost grows with several casts and many tagged enemies, the bullet-heavy combat the genre needs. The 'GPU projectiles at scale' item is planned, but this per-sword redundancy is a separate cost.

**Recommendation:** Compute SwordPlan, launch, departure and arrival once per sword per frame and pass them in (add SwordAt overloads that take a plan). Precompute, once per volley per frame, the list of targets alive at each death tick, so a target walk is O(moves). Skip swords whose path bounds lie outside the view. Presentation could use std::sin/cos, or one shared sincos per angle, since only the simulation's arrival ticks need determinism.

**Verification:** I did not open volley_draw.cpp or volley.cpp. The redundant per-sword plan recomputation is plausible, but the multipliers, such as the 12x factor and the T^2 target walk, were not checked. Profiling a large volley would decide it.

### X-PERFORMANCE-09: Orbiting entities update single-threaded on the logic thread with three deterministic software trig calls each per tick

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/components.cpp:14`

Host::Tick (host.cpp:887-890) runs OrbitPoint over every Body+Orbit entity serially each tick. OrbitPoint does an f64 fmod plus core::math::Sin(3*angle), Cos(angle) and Sin(angle). Each is a full Cody-Waite reduction plus a 9- or 10-term Horner series, and the Horner loop (math.cpp:36-41) tests the sign alternation in every iteration. Sin and Cos of the same angle are reduced separately.

**Evidence:** `for (auto [entity, body, orbit] : active.Entities().view<Body, const Orbit>().each()) { body.previous = body.current; body.current = OrbitPoint(orbit, counters.simulation); }`

**Impact:** The stress scene's 100k orbiters (crowd plus spiral bullets) mean 300k software trig calls on one thread each tick, a large share of the 16.7 ms tick budget, while the job system sits idle. Any future entity-heavy simulation (projectiles as entities) hits the same pattern.

**Recommendation:** Chunk the orbit loop with jobs.ParallelFor. It is deterministic because each entity is independent and writes only itself, though structural changes must stay out. Add a math::SinCos that does one reduction and returns both. Precompute the signed coefficient tables at compile time so Horner has no branch.

**Verification:** host.cpp:887-890 is a serial view loop calling OrbitPoint, as described. ParallelFor is used elsewhere (npcs.cpp, travel.cpp) but not here. I did not check the 100k-orbiter stress figure or the trig internals. Orbit is the stand-in walker motion (sweep-up-backlog item on walkers) and is meant to be replaced, so the impact is a stress-scene cost, not a production one. Severity is low rather than medium.

### X-PERFORMANCE-10: Replay hash checks hash the whole world every 60 ticks while recording or playing

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/logic.cpp:298`

CheckReplay calls Hash(), and so Host::StateHash (host.cpp:1622), every REPLAY_HASH_TICKS (60) wall ticks. That hashes the whole world domain (every NPC's Npc, Needs and Met), every relationship edge, every journey's tiles and arrive ticks, and every journal, all through Hasher, which does two Mix calls in a serial chain per scalar.

**Evidence:** `if (m_restored || m_counters.wall % REPLAY_HASH_TICKS != 0) return; if (m_replay->Recording()) m_replay->RecordHash(m_counters.wall, Hash());` and HashWorld: `core::SaveDomain(hasher, world.state[WORLD], ...); world.relationships.Save(hasher); world.travel.Save(hasher);`

**Impact:** At 50k to 100k NPCs with up to 10 edges each and thousands of journeys, that is millions of scalars, around 10 ms or more on the logic thread once a second during recording and playback. The periodic hitch perturbs the very timing measurements replays are used for, and it scales linearly with world size.

**Recommendation:** Hash the world incrementally: keep running hashes updated at the month step, where the world domain changes, and hash only the active scene per check. Or hash the world section only at month boundaries and the scene every 60 ticks. Alternatively, use a faster wide hash (process 8-byte blocks without the double Mix).

**Verification:** logic.cpp:298-304 calls Hash() every REPLAY_HASH_TICKS wall ticks, and only while a replay is recording or playing. Design 0002.2 specifies hashing every 60 wall ticks on purpose. Line 291 shows Hash() includes m_simulation->StateHash(). I did not verify the per-scalar hasher cost or the millisecond estimate. It applies only to replay mode, never normal play, so severity is low. The scaling concern with world size is plausible.
