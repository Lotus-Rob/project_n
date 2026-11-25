# Review: game-host-b

**Scope:** the SECOND half of src/game/host.cpp (by line count; read the whole file and host.hpp for context but review the second half line by line). Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 19 standing (0 critical, 0 high, 0 medium, 10 low, 9 info), 0 raised but refuted on verification.

**Parts:** game-host-b.md, game-host-b-part2.md

## Assessment

## Scope: src/game/host.cpp lines ~940-1888 (ToggleWorldPause to Publish), with host.hpp and the matching tests

**Strengths**
- The simulation part of the second half is careful. Integer guards are in place everywhere they matter: TRAVEL_END and CALENDAR_END guard the i64 casts in WalkTo, JourneyChunks and AdvanceCalendar; `nextMonth` cannot overflow; FollowRoute reads the route's end before resetting it.
- `Load` is thorough. It refuses early before any expensive regeneration, checks the scene's domain before relationships, never mutates the host on refusal, and holds the result until EnterScene.
- Presentation state (drawn shift, player walk, kept walks, leg-walk cache, fog version, debug sight) is kept apart from saved and hashed state. The fade-by-distance logic in ShiftDrawnPlayer is correct (the linear scale telescopes and there is no divide-by-zero), and the kept-walk stamp scheme is thread-safe under the parallel publish.
- Input that reaches this code is checked upstream: replays check that `held.move` is finite and unit-bounded, and `WorldMap::TileAt` clamps NaN or off-map points, so NaN and out-of-range positions cannot index past the map.
- Test coverage is unusually dense: about 135 host cases, plus save, exactness and replay suites covering routes, pause, M's jump, fog, travellers, walk frames and refusal paths.

**Weaknesses and risks**
- **Invariants held only by convention.** "A route implies a player" is not kept when the player is removed; PlayerBody() then runs on a missing entity (UB) and the next save cannot be loaded. SetData accepts a new map without regenerating the world, its base or the base hash, so the game refuses its own saves and the explored tiles can be sized for the wrong map. Neither is reachable in shipped play today, but both will become reachable as the genre's features land (combat on the world map, procedural maps).
- **The world map has no boundary or terrain for held walking.** The cultivator can walk off the map and over deep water or peaks. Sight then explores the clamped edge tiles, and the place kept on the map can lie off the map.
- **Scaling.** Several per-frame and per-tick paths are linear in world size rather than view size:
  - PublishJourneys visits every journey and computes `Where` for every member of every journey under way, each publish.
  - PublishFog copies the whole explored-tile bitset each publish, even though FogVersion exists to make that unnecessary.
  - WalkTo runs an A* search that allocates 16 bytes per map tile, synchronously inside the tick.
  - ExploreRoute rescans the route from its start every tick.

  These are fine on today's map of about 12k tiles but conflict with the "maps may be very large" goal and the avoid-wasted-work rule.
- **Minor.** Two each-frame functions have no profile zone; ARENA_FORMAT is a misleading name; Walk() repeats PlayerBody(); a few docs are garbled or overstated; the selection lookup in a publish is linear.

**Would the tests catch these?** No. No test kills the player while a route is held, saves after a SetData that changes the map, walks held movement past the map's edge, or measures publish or WalkTo cost against map or journey count. The map-swap tests deliberately keep the map's dimensions, which hides the explored-tile sizing issue.

**Overall health:** good. Correctness is solid for the scenes and data shipped today. The risks are latent invariant gaps and scaling to procedural worlds; none is a present-day crash or desync.

**Coverage note:** I read in full host.cpp, host.hpp, scene.hpp and scenes/world.cpp, the relevant parts of travel.cpp/.hpp, journey.hpp, paths.cpp, world_map.cpp, world_state.cpp, game_data.hpp/.cpp, logic.cpp, replay.cpp and fog_fade.cpp, and the test names of host_tests, save_tests, save_exactness_tests and replay_tests. Only selected test bodies were read: the map-swap, SetTravelShare and data-reload tests.

**Files read in full:** src/game/host.cpp, src/game/host.hpp, src/game/scene.hpp, src/game/scenes/world.cpp, src/AGENTS.md, /tmp/claude-1000/-home-lotus-Development-project-n/5e986e63-9457-419a-a1f3-2b39adbcdc44/scratchpad/review-history.md

## Findings

### GAME-HOST-B-01: A route held when the player's cultivator is removed: PlayerBody() on a missing entity in AdvanceCalendar, and the next save is refused by its own game

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** correctness/ub · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1561`

Tick clears m_player when its entity is gone (lines 901-904) but never resets m_route, so the invariant "a route implies a player" breaks. AdvanceCalendar then checks only `if (m_route)`: FollowRoute returns early because m_player is zero, and the next line `PlayerBody().previous = PlayerBody().current;` calls `active.Entities().get<Body>(active.Find(StableId{0}))`. Find returns entt::null, and `get<Body>` on null is UB (an EnTT assert in debug). Separately, WriteScene saves a route with m_player == 0, and Load's RouteFits refuses a route whose player is zero, so the game cannot load its own save.

**Evidence:** Lines 901-904: `if (m_player != core::StableId{} && !world.state.DomainOf(m_player)) { ... m_player = {}; }` with no `m_route.reset()`. Line 1559-1561: `if (m_route) { FollowRoute(...); PlayerBody().previous = PlayerBody().current;`. RouteFits (line 552) has `player == core::StableId{} ... return false`. ecs.hpp: Find returns `entt::null` when the ID is absent.

**Impact:** Not reachable in shipped play today: the world map is not a combat scene, so the cultivator can only die there through Host::Damage. It becomes UB or a crash on M, plus an unloadable save, as soon as anything can kill the cultivator on the world map (encounters, dangers), which is the genre's direction.

**Recommendation:** Reset m_route (and restart the drawn shift) wherever m_player is cleared, or guard the jump block with `m_player != StableId{}`. Add a test that damages the cultivator to death while a route is held, then runs AdvanceCalendar and a save/load round trip.

**Verification:** Tick clears m_player at host.cpp 901-904 with no m_route.reset(). AdvanceCalendar (1559-1561) checks only `if (m_route)`. FollowRoute guards m_player (1031), but PlayerBody() on the next line does not, so Find(StableId{0}) gives null and get<Body> is UB. RouteFits (552) refuses a zero player, so the save is unloadable. The scene-build path does reset both (808-810). I did not find a test for it. Host::Damage is public, so a harness can reach it. It cannot happen in shipped play, so I rate it low.

### GAME-HOST-B-02: PublishJourneys visits every journey in the world each publish; culling to the view happens per member after Where()

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** performance/scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:1290`

Each publish forks ChunkCount(journeys, 4096) chunks covering every journey in the world. For each one it calls core::Underway, then core::Where for every member, and only then tests `Contains(area, foot)` and InSight. Nothing indexes journeys spatially or by sight, so the cost grows with the number of journeys in the whole world, not with what is on screen or in sight. The sight check makes most of that work wasted: without debug sight only a small diamond round the player can show anything.

**Evidence:** `for (std::size_t index = begin; index < end; ++index) { ... if (!core::Underway(journey, now)) continue; for (member...) { const core::Vec2 foot = core::Where(map, journey, now, member); if (!core::Contains(area, foot) || (!m_debugSight && !core::InSight(...))) continue;`

**Impact:** On procedural maps many times the shipped one (tens or hundreds of thousands of NPCs travelling), the publish costs O(all travellers) every frame, against the memory rule 'hot paths compute only what is drawn'. Today it is hidden by the shipped map's size.

**Recommendation:** Cull before Where: bucket journeys by the tiles or regions their legs cross (rebuilt at each month step, when journeys change), or keep a per-month coarse grid of journey bounding boxes, and visit only buckets overlapping the area intersected with the sight diamond. Add a measure scenario with a large generated map.

**Verification:** The publish loop (about 1280-1310) calls Underway and then core::Where per member before the Contains and InSight tests, over every journey. src/AGENTS.md describes this chunked publish as the design. The backlog says performance items wait until a chunk needs them, measured first. The cost is real but only matters on much larger maps, and it is unmeasured.

### GAME-HOST-B-03: WalkTo runs a full-map-sized A* synchronously inside the tick for every click

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** performance/scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:979`

core::FindShortestRoute calls Search, which allocates and fills three arrays sized to every map tile (cost u32, astray u64, parent u32: 16 bytes per tile) for each WalkTo, inside Host::Tick on the logic thread. A target that is passable but unreachable (an island, or land enclosed by peaks or deep water) floods the start's whole connected component before returning empty. Several WalkTo actions in one tick each run a search, though only the last route is kept.

**Evidence:** host.cpp:979 `core::FindShortestRoute(map, map.TileAt(foot), target)`. paths.cpp Search: `std::vector<u32> cost(tiles, UNREACHABLE); std::vector<u64> astray(tiles, ...); std::vector<u32> parent(tiles, UNREACHABLE);`

**Impact:** On a 4096×4096 procedural map that is about 268 MB allocated and initialised per click, and a tick hitch proportional to the map rather than the route. Fine on the shipped map of about 12k tiles.

**Recommendation:** Reuse scratch buffers kept by the host with generation stamps or a sparse visited map instead of full-map vectors; bound the search (a node budget, or refuse a target in another connected component using a precomputed component ID per tile); and run only the last WalkTo of a tick.

**Verification:** WalkTo (979) runs core::FindShortestRoute synchronously in Tick, and TakeActions runs it once per WalkTo action, so several in one tick each search. I did not read the Search allocations. The map-size cost applies only to maps far larger than the shipped one. The procedural-map direction makes it a plausible future issue, but it is unmeasured, so the 268 MB figure is an estimate.

### GAME-HOST-B-04: SetData with a different world map keeps the world, its base and m_baseHash made for the old map

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** correctness/api-contract · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:1706`

When `data.world != m_data.world`, SetData rebuilds m_paths, m_roadCurves and m_legWalks but leaves m_world and m_base as generated from the old map (NPC cells and homes, journeys' tiles, explored-tile size) and leaves m_baseHash at the old BaseHash. Save writes m_baseHash while Load compares the save's base hash with `data.BaseHash()` of the new data, so a host that took a new map (even only SetTravelShare's, which changes the map's hash) refuses its own saves. If the new map's dimensions differ, FogFits asserts on the next explore, and journeys, NPC cells and kept places index past the new map (Where, TileCentre, InSight).

**Evidence:** Lines 1706-1718 touch only the caches, then `m_data = data; ApplyData();`. Line 1612 `WriteGameWorld(archive, m_world, m_base, m_baseHash, ...)`. ReadGameWorld line 288 `baseHash != data.BaseHash()` → refused. host_tests 2177 and 2288 swap maps via SetData (same columns and rows) and never save afterwards.

**Impact:** Not reachable in the shipped app, which keeps the running map through app::KeepWorld. It is a silent contract gap for harnesses and for any future map reload or mod path, and the tests that swap maps hide the dimension hazard.

**Recommendation:** Either assert in SetData that `data.BaseHash() == m_baseHash` (a map swap needs a new game), or treat a map change as a new world: regenerate m_base and m_world and update m_baseHash. Document which one in host.hpp. Add a test that saves and loads after a SetTravelShare'd SetData.

**Verification:** SetData (1706-1718) rebuilds only the caches. m_baseHash and m_base are untouched, and Load compares against data.BaseHash(), so a swapped map would fail the host's own save. src/AGENTS.md says the caches are dropped when the map changes via SetData, so map swaps are a supported harness path, but nothing states the world must match. I did not check how m_baseHash is set elsewhere. The tests swap same-size maps and never save afterwards. This is a contract gap that the shipped app cannot reach.

### GAME-HOST-B-05: Held walking on the world map has no bounds: the cultivator walks off the map and over impassable ground

- **Severity:** low (reviewer said medium) · **Verdict:** deliberate · **Category:** design/correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:1460`

Walk() adds `move * walkSpeed` to the body with no clamp to the map's area and no terrain test. WalkTo refuses impassable targets and points off the map, but held movement lets the cultivator walk across deep water and peaks and out past the map's edge forever. PlayerTile() then clamps the foot to the nearest edge tile (TileAt clamps), so sight explores edge tiles as if the cultivator stood on them. A CycleScene there keeps an off-map mapPlace, which Load accepts because WorldFits checks only that the place is finite. WalkTo from such a foot plans from the clamped edge tile while the leg's origin lies off the map.

**Evidence:** Line 1460 `body.current = body.current + (move * m_data.cultivator.walkSpeed);`. Line 1518 `m_data.world->TileAt(PlayerBody().current)`. world_map.cpp:698 TileAt clamps each axis. WorldFits line 529-530 checks only `std::isfinite` for mapPlace.

**Impact:** The player can leave the playable world, see past the fog by standing off the edge, and land back off the map. It is inconsistent with the walk-to rules (impassable terrain refused) and with a living-world map. Saves keep off-map places.

**Recommendation:** On scenes showing the world map, clamp the held step to the map's area and refuse or slide steps into tiles WALKING cannot enter (as the route rules do). Have Load refuse a mapPlace or player body outside the map's area. Add tests that walk held movement into the edge and into deep water.

**Verification:** Walk (1453-1460) is unclamped. src/AGENTS.md says 'held movement ... walks freely unless paused' and that WalkTo goes 'whatever the ground'. Free held movement is documented. Off-map TileAt clamping is real (world_map.cpp 698-702). The off-map mapPlace being accepted by Load is a smaller, undocumented gap. The fog-peeking effect at the edge is real but minor.

### GAME-HOST-B-06: PublishFog copies the whole explored-tile bitset every publish, though FogVersion exists to say when it changed

- **Severity:** low · **Verdict:** confirmed · **Category:** performance/wasted-work · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1412`

`snapshot.fog.explored.assign(m_world.explored.begin(), m_world.explored.end())` runs on every publish on the world map, whether or not FogVersion changed. The main thread's FogFade already decides from `fog.version` whether to rebuild, so the copy is wasted almost every frame. The function also has no PN_PROFILE_ZONE, so `measure` cannot see its cost.

**Evidence:** Lines 1409-1416. app/fog_fade.cpp Update builds `Source{ .version = fog.version, ... }` and snaps or eases by it.

**Impact:** Memory bandwidth proportional to map tiles per publish: trivial on the shipped map, about 2 MB per publish on a 4096×4096 map. Against the avoid-wasted-work rule.

**Recommendation:** Hand the bits on only when m_fogVersion differs from the version last published to this snapshot buffer, or share them as an immutable `shared_ptr<const std::vector<u64>>` replaced copy-on-write when exploring changes them. Add a PN_PROFILE_ZONE.

**Verification:** PublishFog (1409-1416) assigns explored every publish even though it already publishes m_fogVersion. This is valid wasted work on a small shipped map. I did not verify the missing PN_PROFILE_ZONE.

### GAME-HOST-B-07: ExploreRoute rescans the route from its first tile every tick

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1527`

Each tick that follows a route, ExploreRoute loops from index 0 over all of the leg's tiles until `reached > toTick`, although arrive ticks rise and only tiles reached in (fromTick, toTick] matter. The cost per tick grows with progress along the route, so a whole walk costs O(n²).

**Evidence:** `for (std::size_t index = 0; index < leg.tiles.size() && index < leg.arrive.size(); ++index) { const i64 reached = ...; if (reached > toTick) break; if (reached > fromTick) ExploreRound(...); }`

**Impact:** Negligible on short routes. On a cross-map route over thousands of tiles on a large map it is thousands of compares per tick for nothing.

**Recommendation:** Binary-search the first arrive tick past fromTick (std::ranges::upper_bound on arrive, offset by startTick), then walk forward to toTick.

**Verification:** ExploreRoute (1527-1540) scans from index 0 each call and breaks at reached > toTick. That is O(progress) per tick, so a walk is O(n^2). The cost is negligible on shipped map routes.

### GAME-HOST-B-08: Load accepts any saved selection, which Select() would refuse

- **Severity:** low · **Verdict:** confirmed · **Category:** validation · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1663`

`selected` is read and held without checking that it exists or is not dying. Select() refuses a dying entity and Tick clears a vanished one only on the next tick, so a loaded game can start, and hash, with a selection no game could have.

**Evidence:** `archive(player, selected, version);` ... `m_loaded.emplace(..., player, selected, ...)` with no check like `playerFound`.

**Impact:** Small: one tick of a stale selection, and a StateHash that differs from the one after Tick clears it. It is inconsistent with how strictly Load checks everything else.

**Recommendation:** Refuse, or clear, a selection that is neither zero nor present and selectable in the loaded state, as Select() does.

**Verification:** Load (about line 1663) reads `selected` with no check. playerFound validates only `player`. The stale selection is cleared on the next Tick, so the impact is one tick. Crafted saves only.

### GAME-HOST-B-09: RouteFits accepts routes WalkTo could never make (non-adjacent or impassable steps, an end past TRAVEL_END)

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** validation · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:1685`

Load's route check uses LegSound, which checks shape, profile, origin, tile range and rising arrive ticks, but not that consecutive tiles are adjacent or walkable by WALKING. RouteFits also does not require `EndTick(route) <= TRAVEL_END`, which WalkTo enforces at line 992. A crafted save can teleport the cultivator across the map, walk it over deep water, or hold a route that ends beyond the ticks journeys are timed in.

**Evidence:** RouteFits (line 548-558) calls `LegSound(leg, *map)`. LegSound in travel.cpp has no adjacency or passability test; WalkTo has `core::EndTick(*leg) > static_cast<i64>(TRAVEL_END)` → ignored.

**Impact:** Crafted or fuzzed saves only. The game state becomes one no play could reach, against the host.hpp promise that Load refuses what 'no game could' hold.

**Recommendation:** In RouteFits also require each step to be between neighbouring tiles with a finite WALKING step cost and `EndTick(*route) <= TRAVEL_END`.

**Verification:** RouteFits (host.cpp 548-558) checks worldTicks <= TRAVEL_END but not EndTick(route) <= TRAVEL_END, while WalkTo enforces that at line 992. The LegSound doc in travel.hpp covers shape, profile, start point, tiles on the map, rising arrive ticks and an end tick that fits an i64. It lists no adjacency or passability test, so the doc supports the adjacency claim. I did not read LegSound's body. The end-tick bound is looser than TRAVEL_END but still i64-safe. Crafted saves only.

### GAME-HOST-B-10: Each-frame publish helpers without PN_PROFILE_ZONE

- **Severity:** low · **Verdict:** confirmed · **Category:** agents-rule · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:1409`

src/AGENTS.md requires PN_PROFILE_ZONE at the top of every function that runs each frame, except per-item functions. PublishFog (an O(map) copy) and PublishCaster run on every publish without one; PublishJourneys, a per-chunk job body, has none either.

**Evidence:** Lines 1357 and 1409 begin without PN_PROFILE_ZONE(); PublishRoute, PublishPortal and PublishOverhead have one.

**Impact:** `measure` and Tracy captures attribute their cost to Publish's own zone, hiding the fog copy as it grows with the map.

**Recommendation:** Add PN_PROFILE_ZONE() to PublishFog and PublishCaster, and one per journey chunk if ParallelFor does not already zone its chunks.

**Verification:** PublishFog and PublishCaster have no PN_PROFILE_ZONE, while PublishOverhead (line 1191) does. src/AGENTS.md requires a zone in every per-frame function. PublishFog copies the explored set, so it is O(map). PublishJourneys is a per-chunk job body, and AGENTS.md line 45 says a job chunk's own zone covers it, so that part is weak.
