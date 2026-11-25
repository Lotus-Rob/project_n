# Review: game-host-a

**Scope:** src/game/host.hpp and the FIRST half of src/game/host.cpp (by line count; read the whole file for context but review the first half line by line). Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 18 standing (0 critical, 0 high, 1 medium, 8 low, 9 info), 0 raised but refuted on verification.

**Parts:** game-host-a.md, game-host-a-part2.md

## Assessment

## Scope: src/game/host.hpp and the first half of src/game/host.cpp (lines 1-944), with the rest of the file read for context

**Strengths**
- Loading a save is defensive and well tested. `Reader::GetSequence` limits allocations to the bytes actually present. The save is fully parsed and checked before anything is kept (`m_loaded`). The checks cover component consistency (`Consistent`, `CombatConsistent`), which ID range belongs in which domain (`IdsSuitDomain`), the clock against the month count (`ClockFits`), the map and fog fit (`WorldFits`) and whether the route can be walked (`RouteFits`). Look, Health and Needs validate themselves. save_tests.cpp has about 80 cases, one per refusal class, and the save fuzz target covers this path too.
- The design is careful about determinism and order. `CopyOf` copies storage order exactly, saves never sort, presentation state is kept out of hashes and saves, and the publish is laid out the same for any worker count.
- Scene changes keep their invariants: scene IDs carry across scenes, the scratch world is separate from the game world, the base is kept, and `ClearScene` sends activated NPCs back to the world.
- host_tests.cpp has about 135 cases, including tricky ones: the portal check sweeps the dash's whole path, the drawn shift fades by distance, M's jump explores the route it skips, and the clock clamps at its end.

**Weaknesses**
- `Host` is a god class, about 2,460 lines across the two files. It owns the world, the save format, fog, the player's route, combat dispatch, the publish, presentation-only animation state (drawn shift, player walk, leg-walk cache, kept walks, traveller labels, debug sight, fog version) and the built-in data. The Doxygen on `Tick()`, `Load()` and `Publish()` is 20 to 40 lines each and walks through the implementation step by step, so it is already a burden to keep in sync.
- Some behaviour gaps the tests don't catch:
  - On the world map the cultivator can walk off the map with held movement, and a load accepts any finite place to come back to.
  - A walk-to route that passes through the portal's step area quietly sends the player to the arena.
- Wasted work on load:
  - A dev-scene save loaded cold builds the scene in full (stress: about 100k active entities plus 50k NPCs) and keeps it for the rest of the session as the scratch base.
  - Every load regenerates the game's world, even though the seed is a constant and the data hash usually matches the base already in memory.
- Most load refusals are not logged. Only the generator/data mismatch, an ID counter behind its base and a malformed delta, order or travel section say why.
- The world seed is hard-coded (`SEED = 0x5eed`) with no way to pass one in, though the stated direction is procedural maps per seed.

**Overall health:** good. The code is correct for the shipped content and heavily tested. The risks are scaling (map size, load cost, memory: the game world is kept twice, and four copies exist during a load) and maintainability (class size, documentation that restates the code, game content inside the host). The bugs found are edge cases and UX problems, not crashes.

**Files read in full:** src/game/host.hpp, src/game/host.cpp, src/game/world_state.hpp, src/game/world_state.cpp, src/game/scenes/world.cpp, src/AGENTS.md, /tmp/claude-1000/-home-lotus-Development-project-n/5e986e63-9457-419a-a1f3-2b39adbcdc44/scratchpad/review-history.md

## Findings

### GAME-HOST-A-01: Held movement walks the cultivator off the world map, and a load accepts any finite place to return to

- **Severity:** medium · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:528`

Walk() adds move × walkSpeed to the body with no bounds or terrain check, on the world map too. WorldFits() accepts any finite mapPlace, however far off the map. TileAt clamps, so exploring and PlayerTile quietly use the nearest edge tile while the body stands in empty space. Leave() stores that off-map point as mapPlace, and the world scene's Build respawns the cultivator there.

**Evidence:** host.cpp:1460 `body.current = body.current + (move * m_data.cultivator.walkSpeed);` with no clamp. host.cpp:529-530 `placeFits = !world.mapPlace || (map != nullptr && std::isfinite(...x) && std::isfinite(...y))`. world.cpp:50 `SpawnPlayer(context.MapPlace().value_or(...StartCentre()))`. No test walks past the map's edge.

**Impact:** The cultivator can wander indefinitely past the paper edge. The camera follows (Roam), the fog keeps exploring the clamped edge tile, and a walk-to from out there starts from the clamped edge tile with the leg's origin far off the map, which draws a long diagonal. Saves keep the off-map place.

**Recommendation:** Clamp held movement to MapArea(map) on world-map scenes (and decide whether impassable tiles block held walking). Make WorldFits refuse a mapPlace outside MapArea. Add tests for walking into each edge and for a save with an off-map place.

**Verification:** Walk() (host.cpp:1453-1460) adds move*walkSpeed with no clamp or terrain check. WorldFits (host.cpp:528) only checks finiteness of mapPlace, as the finding says. I found no design doc recording off-map walking as deliberate. The fog spec only says land off the map counts as explored.

### GAME-HOST-A-02: A walk-to route that crosses the portal's step area sends the cultivator through the portal

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:869`

Tick() runs OnPortal() after every Move(), including FollowRoute(). OnPortal sweeps the foot's whole path for the tick against the step-area ellipse. On a route the body follows core::Where(), which goes in straight lines between tile centres. Neither WalkTo nor core::FindShortestRoute knows where the portal is. The world portal sits at StartCentre + (-560, -200) (WORLD_PORTAL_OFFSET), and the ellipse is 48 wide by 18 deep (PORTAL_RADIUS 48 × SEAL_VIEW_RATIO 0.375). With 256-unit tiles, the diagonal step from tile (-2,-1) to (-3,0) relative to the start city passes x = -560 at y = -208, 8 units from the portal's centre, well inside. The vertical step between (-2,-1) and (-2,0) runs along x = -512, which touches the rim exactly (units.x = 1.0, and the rim counts).

**Evidence:** host.cpp:865 `Move(input.held.move, clockRuns ? world.worldTicks + 1 : world.worldTicks, ...)` → FollowRoute sets `body.current = core::Where(...)`; host.cpp:869 `if (OnPortal()) { ... Leave(next, true, events); return; }`; world.hpp:25 `WORLD_PORTAL_OFFSET{ .x = -560.0F, .y = -200.0F }`. No test walks a route across the portal: none of the host_tests portal cases (1198-1294) uses WalkTo.

**Impact:** A player who clicks a destination north-west of the start city can be thrown into the arena halfway along the walk. Their route is dropped and mapPlace is set to the point just before the portal. It looks like a random teleport.

**Recommendation:** Decide what a route through the portal should do. Either make it impassable for routes (FindShortestRoute avoids the portal tile's step area, or WalkTo refuses a route whose stretches cross it), or only count the portal for held movement and dashes (skip OnPortal while m_route drove this tick's move). Add a host test with a walk-to whose path crosses the step area.

**Verification:** Tick() does call OnPortal() after Move()/FollowRoute() (host.cpp:865-869) and OnPortal() sweeps body.previous->current, so a route leg passing the step area triggers Leave. Neither WalkTo nor the pathfinder knows the portal, so the mechanism holds. Geometry numbers not checked and no test walks a route across it. The impact depends on whether a route's leg really crosses the ellipse; this is an edge-case surprise, not a crash. I found no design record that decides it either way.

### GAME-HOST-A-03: Loading a dev-scene save builds the whole scene as its scratch base and keeps it (stress: about 100k active entities) for the session

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:727`

ScratchBase() builds a fresh WorldState with BuildScratch, so it includes the scene's entire active domain. When the kept base belongs to another scene, Load stores that whole object as m_loaded->scratchBase, and EnterScene moves it into m_scratchBase, where it stays until another dev scene replaces it. Only the world domain of a scratch base is ever read (CopyOf and SaveDomainDelta use state[WORLD]). Build() itself keeps `CopyOf(world)`, which holds no active domain, so the same member ends up in two shapes depending on how it was made. The header comment even says 'its scene's entities left in it, unread'.

**Evidence:** host.cpp:731-735 `auto built = std::make_shared<WorldState>(...); BuildScratch(*built, SceneOf(scene), context); return built;` versus host.cpp:829 `m_scratchBase = std::make_shared<const WorldState>(CopyOf(world));`. stress.cpp builds a 300×300 crowd, 5×2,000 bullets and 50,000 world NPCs.

**Impact:** A stress save loaded from the world or the arena builds about 150k entities to throw most of them away, and keeps about 100k active entities (Body, Look, Orbit…) alive for no purpose. That cost recurs on every such load. It goes against the 'avoid wasted work' rule.

**Recommendation:** Return `std::make_shared<const WorldState>(CopyOf(*built))` from ScratchBase, or better, have BuildScratch build only what the world domain needs. Then both paths keep the same shape. Add a test that KeptScratchBase()'s active domain is empty after a cold load.

**Verification:** ScratchBase() (host.cpp:727-736) returns the fully built WorldState including its active domain, while Build keeps CopyOf(world) (host.cpp:829). The shapes differ and the active domain is wasted. Only the dev-scene (stress) load path is affected, so the real-world cost is limited to dev tooling. I did not measure the entity count.

### GAME-HOST-A-04: Most of Load's refusals are not logged

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** error-handling · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:298`

ReadGameWorld and ReadScratch call Fail() without a log when WORLD_VERSION doesn't match or CountersFit refuses. Load (line 1646) fails silently on another ARENA_FORMAT, an unknown scene or a world-map save loaded without a map. A wrong ACTIVE_VERSION, trailing bytes, or a failure in Consistent, WorldFits, Relationships::Within, the place-on-portal check, playerFound, RouteFits, AimsAtCreatures, FlashesStartBy or SealsStartBy all just return false. Only a generator or data-hash mismatch, an ID counter behind its base, and malformed delta, order or travel sections are logged.

**Evidence:** host.cpp:298-300 `if (version != WORLD_VERSION || !core::State::CountersFit(ids, sceneIds)) { archive.Fail(); }` with no log. host.cpp:1686-1688 is one combined condition that returns false.

**Impact:** A user or developer whose save is refused (after a format bump, for instance) can't tell why. The header's promise of refusals 'each logged, saying which' only holds for a few of them.

**Recommendation:** Log each refusal class once, at warning level, naming it ('save refused: format 9, this game reads 10'; 'save refused: its world's clock doesn't fit its month steps'), as the generator and data checks already do. Split the combined condition at 1686 so each failure names itself.

**Verification:** host.cpp:298-300 does Fail() without a log on a version or counter mismatch. host.hpp:148 says only that a refused generator or data hash and an ID counter behind its base are logged, so the header does not claim every refusal is logged. The finding overstates the header promise. I did not check the memory backlog for a deferral entry. The missing logs are a real, minor diagnosability gap.

### GAME-HOST-A-05: Every load regenerates the game's world even when it is identical to the base already in memory

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:307`

ReadGameWorld always runs GenerateWorld(base, data, paths), which spawns every city's people and seeds two months of travel. The Host's seed is the constant SEED, and data.BaseHash() usually equals m_baseHash, so the result equals m_base. CopyOf's own comment says copying is 'far cheaper than making it again, which routes every journey'. The plan was to regenerate on load (measured at 35-61 ms on the shipped map), but that cost grows with map size, which the user expects to grow 2x to 50x.

**Evidence:** host.cpp:301-308 `WorldState base{...}; ... GenerateWorld(base, data, paths); std::pair<WorldState, WorldState> made(CopyOf(base), std::move(base));`

**Impact:** Load latency grows with world size for no benefit. It is a hitch on the logic thread on every F9 or replay start.

**Recommendation:** When seed == m_base's seed and baseHash == m_baseHash, use CopyOf(m_base) for both the base and the world instead of regenerating (still from the seed when they differ). Keep a test that a load from a different seed or different data still regenerates.

**Verification:** ReadGameWorld always runs GenerateWorld (host.cpp:307) and never reuses m_base. The reuse optimisation is valid when seed and baseHash match. The finding itself cites a measured 35-61 ms and that regenerating on load was a planned choice, so the cost is real but accepted. The map-size growth argument is speculative. A cached copy would also need to match the loaded world's sceneIds and counters, which Load sets after generation.

### GAME-HOST-A-06: The world seed is hard-coded; a Host can't be made for another seed

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:48`

`constexpr u64 SEED = 0x5eed;` is used for the game's world (constructor), every scratch world and every refused-section placeholder. The Host constructor has no seed parameter, so every new game is the same world. The project's stated direction is maps generated procedurally per seed.

**Evidence:** host.cpp:776-777 `m_world{ core::State{ DOMAINS, SEED } }, m_base{ core::State{ DOMAINS, SEED } }`. Load does read the seed back from the save (ReadGameWorld), so saves are already seed-aware while new games are not.

**Impact:** A new-game seed can't be tested without changing code, so tests can't cover more than one world and the per-seed design goes unchecked (for example, nothing checks that two seeds give different worlds).

**Recommendation:** Add a seed to the Host constructor (defaulting to SEED for tests) and pass it from the app's new-game path. Add a test that two seeds give different WorldHash values and that each one saves and loads.

**Verification:** SEED is constexpr (host.cpp:48) and used by the constructor, so a new game is always the same world. Loads read the seed back from the save. The project memory 'World map is procedural' says maps are meant to be generated per seed, so a constructor seed is a natural future step, not a defect today. This is a design gap with no failure scenario.

### GAME-HOST-A-07: A save can put a homed world NPC in the world scene's active domain, where no system handles it

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** validation · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:471`

IdsSuitDomain accepts a world-range ID in the active domain whenever the entity has an Npc. NpcsPlaced accepts it if its home is a city and its cell a map tile. In the world scene, though, nothing ever activates NPCs. Such an NPC is skipped by the month step (WORLD domain only), by Travel and by the publish's travellers. If it also has Wander, DecideActive aims it at CellCentre(tile index), a point on the 64-column arena grid that has nothing to do with the map.

**Evidence:** host.cpp:477 `return !worldRange || entities.all_of<Npc>(std::get<0>(row));`; npcs.cpp DecideActive uses `CellCentre(person.cell)`; Activate asserts `person.home == NO_HOME`, which a save bypasses.

**Impact:** A malformed or hand-edited save loads into a world with a frozen or misplaced NPC. This is not a crash, but it is a state the game itself can never produce, which the loader otherwise tries hard to refuse.

**Recommendation:** Refuse world-range NPCs in the active domain when the scene shows the world map, or more generally any active NPC whose home isn't NO_HOME. Mirror Activate's assert as a load check and add a save_tests case.

**Verification:** IdsSuitDomain (host.cpp:471-478) does accept a world-range ID on an Npc in the active domain, and Activate's home==NO_HOME assert is not mirrored in the load checks. The scenario needs a crafted or edited save, and the effect is a frozen or misplaced NPC rather than a crash. The load path is legitimately used for scenes that activate NPCs, so the fix must be scene-aware. The NpcsPlaced details were not fully traced.

### GAME-HOST-A-08: Ability-use source numbers are spread across files with no central registry

- **Severity:** low · **Verdict:** confirmed · **Category:** maintainability · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:46`

USE_DEBUG_BURST = 2 is in host.cpp, USE_AURA = 1 in scenes/arena.cpp and CAST_SOURCE = 16 (+ slot) in combat.hpp. Each comment refers to the others by hand. Random purposes, by contrast, are centralised in game/random_purposes.hpp.

**Evidence:** host.cpp:46 `constexpr u32 USE_DEBUG_BURST = 2; // ... the arena's aura is 1`; arena.cpp:29 `constexpr u32 USE_AURA = 1;`; combat.hpp:31 `CAST_SOURCE = 16`.

**Impact:** A new scene effect could reuse a number. Two uses on the same tick would then share a use ID, merging their hits and UseEnded events (damage-number grouping, sounds).

**Recommendation:** Move every source into one header (for example game/use_sources.hpp) with a static_assert that they are unique and below CAST_SOURCE.

**Verification:** USE_DEBUG_BURST=2 (host.cpp:46), USE_AURA=1 (arena.cpp:29) and CAST_SOURCE=16 (combat.hpp:31) are scattered, with only comments cross-referencing them. There is no uniqueness static_assert. The risk is currently theoretical.

### GAME-HOST-A-09: The host contains game content: fox labels and health bars, the debug burst, the built-in data tables, the demo glows and the traveller sprite

- **Severity:** low · **Verdict:** deliberate · **Category:** architecture · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:566`

src/AGENTS.md says the host 'never checks which scene it runs', yet host.cpp contains:
- BUILT_IN_CULTIVATOR/FOX/ABILITIES (copies of the YAML, about 65 lines);
- the demo glow loops (CHARGE_*, PROGRESS_*, DemoGlow);
- TRAVELLER_SPRITE = Villager;
- creature-kind branches in DataOf, PublishOverhead and DebugBurst (FoxName, CultivatorName, foxes only).

**Evidence:** host.cpp:23-29, 566-632, 685, 1200-1203, 1423-1426; host.hpp:465-467 `DataOf` maps every kind that isn't Fox to the cultivator.

**Impact:** Each new creature kind or label rule means editing the host. A third CreatureKind would silently take the cultivator's data.

**Recommendation:** Make this part of the data-driven sweep: move the built-ins to a data-defaults file, the demo glows to the arena scene's hooks, and the label and health-bar rules into per-kind data. Make DataOf a lookup that asserts on an unknown kind.

**Verification:** The hard-coded content is real (host.cpp built-ins, DebugBurst, fox labels). The backlog's 'Data-driven sweep' item (sweep-up-backlog.md line 53) already covers it, and the finding itself is marked known-deferred. The DataOf unknown-kind assert suggestion is still reasonable.

### GAME-HOST-A-10: Host is a god class mixing simulation state with presentation-only caches

- **Severity:** info (reviewer said medium) · **Verdict:** unverifiable · **Category:** architecture · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.hpp:67`

Host owns:
- the game, base, scratch and scratch-base worlds;
- the save, load and hash formats (WriteGameWorld/ReadGameWorld/WriteScratch/ReadScratch);
- scene lifecycle, combat dispatch, the player's route and fog exploring;
- the multi-threaded publish;
- the built-in data tables and the demo glows;
- about 10 presentation-only members: m_drawnShift, m_playerWalk, m_legWalks, m_keptWalks/m_keptWalksStamp, m_travellerLabels, m_animations, m_debugSight, m_fogVersion, m_worldTicksBefore and m_drawList.

The presentation members change during Tick() and AdvanceCalendar(), and their 'never hashed or saved' status is enforced only by comments and tests.

**Evidence:** host.cpp is 1,888 lines and host.hpp 571. Tick() calls StepPlayerWalk and ShiftDrawnPlayer, which are presentation, in the middle of simulation code (host.cpp:867, 1019, 1027, 1040). AdvanceCalendar clears m_legWalks.

**Impact:** Every new feature grows the class. Presentation state sits one line away from simulation state, so it is easy to read it in the simulation by mistake, which would break replay determinism without any hash catching it in tests that don't vary presentation. Reviews and the LSP have a hard time with a file this big.

**Recommendation:** Split it into separate pieces:
- a WorldSave module (Write/Read world and scratch, CopyOf, Fits checks);
- a PlayerRoute component (WalkTo, FollowRoute, ExploreRoute);
- a HostPresenter that owns all presentation-only state and the publish, reading the simulation through a const view and fed by events or deltas from Tick (the player's step, the clock jump);
- built-in data and demo glows moved to their own files.

**Verification:** The size claims and the mix of presentation state with simulation state are plausible, but whether a split is worthwhile is an architecture opinion. The risk that presentation state leaks into the simulation without a hash catching it is hypothetical. A refactor decision, not a bug.
