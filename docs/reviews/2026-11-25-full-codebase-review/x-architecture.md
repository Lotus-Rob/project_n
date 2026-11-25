# Review: x-architecture

**Scope:** CROSS-CUTTING architecture over src/ and docs/design/0002*: module boundaries (core, data, game, gfx, app, platform) and whether includes respect them (grep the includes), dependency direction, god objects (host.cpp, renderer.cpp, app.cpp), ECS design and its fit for the planned game, API surface quality, duplication across modules, extensibility for the planned systems (towns, UI screens, view models, settings, cultivation, sects, events), and what should be restructured before more is built on it.

**Findings:** 23 standing (0 critical, 0 high, 8 medium, 12 low, 3 info), 0 raised but refuted on verification.

**Parts:** x-architecture.md, x-architecture-part2.md, x-architecture-part3.md

## Assessment

## x-architecture: cross-cutting assessment of src/ and docs/design/0002*

**What's good.** The module boundaries are real and nothing breaks them. `core <- data/platform <- gfx <- app` and `game -> core only` all hold: grepping every `#include "<module>/` found no upward or sideways include, and lint backs this up with `MODULES`. The thread model is clean. The logic and main threads share only `GameHandover`, `game` never sees SDL, and the frame contract is plain data. The determinism work is careful: counter-keyed RNG, fixed-size job chunks, canonical generation order, save order kept, hashes compared across worker counts. Saves stored as differences from a base world regenerated from the seed are a good idea, and it is carried through. Errors use `std::expected` throughout, and unit tests cover nearly every pure piece (camera, cursor, click, fog fade, logic, handover, journey, paths).

**What's weak.** The architecture grew well for a test bed with one world map, one player, foxes and a ring of scenes. It is now under strain at exactly the points the Tale of Immortal-style game needs next:

1. **God objects.**
   - `game::Host` (about 2,460 lines with its header) is the simulation, the save and hash schema, player control, routing, fog and exploration, travellers' presentation caches and all publishing.
   - `gfx::Renderer::Impl` has about 110 members and a hand-written draw order.
   - `app::App` mixes the main loop, input routing, data hot reload and the profiling harness.

   Every planned system (cultivation, sects, items, towns, UI screens) would land in one of these three files.
2. **`core` is two modules in one.** It is the engine kernel (jobs, handover, logic, archive, replay, ECS facade) and also the game's whole domain vocabulary and presentation config (`GameData::fox`, `AbilityKind`, `CasterView`, journeys, `WorldLook`, `Seals`, `Effects`). The "game links only core" rule forces this.
3. **Hard-wired gameplay shapes.**
   - Only the player casts.
   - Only `CreatureKind::Fox` is an enemy.
   - There is one global loadout.
   - Scenes are fixed singletons in a ring, with no parameters.
   - There is no active NPC tier on the world map.
   - There is no spatial index or collision.
   - There is no command apply step, which 0002 §7.2 promises.
4. **Scaling against procedural, possibly very large maps.**
   - The map is loaded once from YAML and shared immutably with the renderer at start.
   - `Paths` keeps a full-map distance map per city and profile.
   - The fog bits are copied on every publish and rebuilt over the whole map on every tile step.
   - Fog and ground textures use one texel per tile, so a map can't be wider or taller than the GPU's texture limit.
5. **View models drift from the plan.** Instead of 0002 A13's versioned, shared-pointer views that the UI subscribes to, new UI data is added as ad hoc `Snapshot` fields filled on every publish.
6. **The docs don't follow their own rules.** `src/AGENTS.md` has single "rules" of 5,000 to 15,000 characters that copy the Doxygen word for word. 0002 still describes systems that don't exist.

**Overall health.** The code is good and the main parts are sound, but the shape is still that of a demo. Correctness and determinism are strong. Extensibility is the main risk.

**Restructure before building more:**
- (a) Split `Host` into a world/simulation core, a save and hash registry, player control, and a presentation publisher that owns the presentation caches.
- (b) Give the world map a run-time path, generated per world on the logic side and delivered to the renderer, and replace the per-city full distance maps with something that scales.
- (c) Bring in factions or allegiance, per-caster loadouts and data-driven creature kinds before combat grows.
- (d) Add scene parameters (scene, place or encounter) to the scene-change request and the save.
- (e) Build the 0002 §8.14 view-model and subscription path before the first UI screen, and add an input-context stack in App.
- (f) Separate the engine kernel from the shared game model in `core`.
- (g) Cut `src/AGENTS.md` back to one line per rule.

**Files read in full:** src/AGENTS.md, src/game/host.hpp, src/game/host.cpp, src/app/app.hpp, src/app/app.cpp, src/app/game.hpp, src/app/game.cpp, src/app/fog_fade.cpp, src/gfx/renderer.hpp, src/gfx/renderer.cpp (lines 1-780, 1149-1350 and the function list), src/core/frame.hpp, src/core/game_data.hpp, src/core/ecs.hpp, src/core/schedule.hpp, src/core/logic.hpp, src/core/paths.hpp, src/game/components.hpp, src/game/scene.hpp, src/game/world_state.hpp, src/game/npcs.hpp, src/game/scenes.cpp, src/main.cpp, src/app/bindings.cpp (default table), src/game/combat.cpp (Tag and the function list), src/game/travel.cpp (SaveDelta), src/gfx/internal/features/sprites.cpp (FogTexture::Update), docs/design/0002-engine-architecture.md (sections 1-7.7 and 8.14), docs/design/0002.1-app-and-game.md, docs/design/0002.4-ui-and-text.md (sections 1 and 8), tools/project_n.py (MODULES, FACADES), /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/procedural-world-map.md

## Findings

### X-ARCHITECTURE-01: game::Host is a god object: simulation, save/hash schema, player control, fog, routing and presentation caches in one class

- **Severity:** medium (reviewer said high) · **Verdict:** confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.hpp:67`

Host (571-line header, 1888-line .cpp, about 45 private methods and about 30 members) owns at once:
- the game world, its base and the scratch world;
- scene building;
- the tick order;
- player movement, routes (WalkTo, Move, FollowRoute) and the drawn-shift fade;
- exploration and fog versioning;
- combat dispatch;
- save, load and hash schemas (WriteGameWorld, ReadGameWorld, ReadScratch, WriteScene, HashWorld);
- data application;
- every publish (entities, journeys with LegWalkCache and KeptWalk caches, labels, route, fog, overhead bars, caster, portal).
src/AGENTS.md line 27 needs a 15,000-character paragraph just to describe its behaviour.

**Evidence:** host.hpp lines 375-568: private methods for Walk, WalkTo, Move, FollowRoute, ExploreRound/ExploreRoute, DrawnPlayer/RouteShift/ShiftDrawnPlayer/RestartDrawnShift, StepPlayerWalk, CastAction, DebugBurst, Leave, OnPortal, PublishPortal/Overhead/Caster/Route/Fog/Journeys, SizeKeptWalks, ApplyData. Members include m_drawList, m_monthStep, m_combat, m_travellerLabels, m_paths, m_roadCurves, m_legWalks, m_keptWalks, m_world, m_base, m_scratch, m_scratchBase, m_drawnShift, m_playerWalk, m_route, m_player, m_selected and m_loaded.

**Impact:** Every planned system (cultivation realms, sects, items, events, towns) needs per-tick work, save and hash entries and publish output, and today the only place for these is Host. Each addition touches Tick, Save, Load, StateHash, WorldHash, Publish and EnterScene. That makes omissions likely (for example, something saved but not hashed) and changes hard to review. Agents and reviewers already need huge context to change it.

**Recommendation:** Before adding more systems, split Host along its seams:
- a WorldSim that owns WorldState and the tick's system order;
- a PlayerController for routes, held movement and exploration;
- a SaveSchema or registry that each system adds its save, load and hash parts to;
- a Publisher on the logic thread that owns the presentation caches (LegWalkCache, KeptWalk, drawn shift, player walk, traveller labels).
Host then just composes them.

**Verification:** host.cpp is 1888 lines and host.hpp 571, matching the claim. I did not count the private methods or the 15,000-character paragraph. The seams named (save and hash schema, player control, publish caches) are real. It is a maintenance and extensibility risk, not a present bug. I found no recorded deliberate decision to keep it one class. The backlog only records splitting renderer.cpp's world-map upload block. Medium, not high, since it works and is tested.

### X-ARCHITECTURE-02: No architectural path for a per-seed procedural world map: the map is static data loaded once by main and handed to the renderer at start

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/main.cpp:112`

The recorded direction is that maps are generated per seed and world.yaml is only a test map. The current wiring assumes the opposite:
- main.cpp reads world.yaml once into GameData::world (a shared_ptr to an immutable map).
- App hands that pointer to the renderer once (App constructor, renderer SetWorld), and again only on a look reload.
- KeepWorld pins it through data reloads.
- GameData::BaseHash and Host::Load refuse any save whose map differs.
- The renderer, the fog fade, the cursor and the target tile all read m_options.data.world on the main thread.
No channel exists for the logic thread to make a map from a seed and get it to the main thread at a scene change or a load.

**Evidence:** main.cpp 112-116, 135-140; app.cpp 261 (SetWorld only at start) and 465-475; app.cpp 203-208 (KeepWorld); game_data.hpp 66-72 ('Read once at the start and kept through a reload'); host.cpp 288-296 (save refused for other world data).

**Impact:** Map generation can't be added without restructuring how the map flows from data through the simulation to gfx and app. Starting a new game with a new seed, or loading a save of another seed, is impossible by design today. Any code built on the static-map assumption (the renderer's world version, FogFade sized from the main thread's map, TargetCursor sized from the options map) will have to be reworked.

**Recommendation:** Generate the map inside the world-creation step (logic thread, with its derived data such as Paths and RoadCurves), and make it part of the world's identity: seed plus generator version plus generator data hash. Publish it to the main thread as an immutable shared_ptr in the snapshot (or the scene-change stage), versioned so the renderer rebuilds only on change. The main thread then takes the map from the drawn snapshot, not from Options. Settle this before the UI and towns are built on the current wiring.

**Verification:** The wiring facts hold: the map is loaded once from world.yaml into GameData::world and pinned through reloads. Specs 2026-11-10 chunk5 and 2026-11-15 chunk5b1 say 'Hand-made map for the demo; procedural generation later' and list generation as a non-goal, so the static map is deliberate for now. The finding's premise that the recorded direction is procedural comes from the memory note procedural-world-map, which I did not read here. That note is the user's direction, so the future rework is real. Because generation is explicitly deferred, calling it high is too strong. Medium: settle the map's flow to the main thread before the UI and towns are built on it.

### X-ARCHITECTURE-03: core::Paths keeps a full-map distance map per (profile, city): memory and build time grow as cities × profiles × tiles

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** scalability · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/paths.hpp:79`

Paths builds a DistanceMap holding one u32 per map tile for every city by every movement profile, and keeps it for the run (Host::m_paths; Warm builds every city). The host also keeps it across the whole session and rebuilds every journey through it on load.

**Evidence:** paths.hpp 79-93: 'The distance maps to the map's cities, each built on first use and kept'; DistanceMap stores std::vector<u32> m_cost for every tile; host.hpp 473 keeps m_paths 'for the run'.

**Impact:** The shipped test map is small, but procedural maps may be 2x to 50x larger. Take 1M tiles, 300 cities and 3 profiles: that is 3.6 GB of distance maps. Building each one is a full-map Dijkstra, run at world creation or load. This blocks large worlds outright.

**Recommendation:** Replace per-city full-map fields with a scheme that scales: hierarchical pathfinding (region graph plus local A*), distance maps limited to a reach (the memory note's 'reach limits'), or route tables between cities on a road graph with local legs. Measure the memory per map size and add a budget test.

**Verification:** paths.hpp confirms Paths keeps one full-map DistanceMap (std::vector<u32> m_cost) per (profile, city) slot, built on first use, with Warm building them all. The arithmetic scales as stated. The 3.6 GB example uses invented sizes (1M tiles, 300 cities). The shipped map is small, MAX_TILES is 2^24, and generation is deferred, so it is a future scalability risk with no current failure. The backlog's 'journeys share their routes' item and the 'measure first' policy cover the neighbouring concern. Medium.

### X-ARCHITECTURE-04: Fog of war does whole-map work every publish and every tile step, and one texel per tile caps map size by GPU texture limits

- **Severity:** medium · **Verdict:** partially-confirmed · **Category:** scalability · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1409`

Host::PublishFog copies the whole explored bitset into every snapshot, with no check that FogVersion changed. On the main thread, FogFade::Update calls SetTargets every time the player's tile changes: it reallocates and rebuilds a float array of 2 × tiles over the whole map, then FindMovers scans the whole map. Every frame in which any tile's byte changes during a fade bumps the revision, and FogTexture::Update then uploads the whole columns × rows × 2-byte texture. The fog texture and the ground field are each one texel per tile, so a map wider or taller than the device's maximum 2D texture size (commonly 8,192 or 16,384) fails to create the texture. core::MAX_TILES alone (2^24) doesn't stop this.

**Evidence:** host.cpp 1412: snapshot.fog.explored.assign(...) on every publish. fog_fade.cpp 20-37 (assign tiles*2, full loops) and 105-110 (SetTargets whenever source != m_source, which changes with playerTile). sprites.cpp 296-335 (whole-texture CopyFog per revision). world_map.cpp 16: MAX_TILES = 1<<24, with no limit on columns or rows.

**Impact:** On a 4096×4096 map:
- about 2 MB is copied into every snapshot (60/s);
- about 134 MB of floats are rebuilt and scanned on the main thread at each tile the cultivator crosses, a hitch of tens of ms;
- 33 MB is uploaded every frame during a fade.
Long, thin generated maps would lose the fog and the ground entirely. It also breaks 0002 §5's rule that data which rarely changes crosses as a shared_ptr<const>.

**Recommendation:** Publish explored as a shared_ptr<const> bitset replaced only when FogVersion changes, or as dirty-rectangle deltas. In FogFade, update targets only for tiles inside the old and new sight discs, plus explored deltas. Upload only dirty sub-rectangles of the fog texture. Tile the fog and ground textures (or cap the map's dimensions and check that at generation time).

**Verification:** PublishFog (host.cpp 1412) assigns the whole explored bitset into every snapshot with no FogVersion check, as claimed. FogFade lives in src/app/fog_fade.cpp, not gfx, and SetTargets is called from line 107. The claimed 4096x4096 costs and the texture-dimension limit are extrapolations. The shipped map is small, and MAX_TILES is 2^24 with no per-dimension cap (world_map.cpp 16 and 189). The point is valid but speculative while maps are small, and the backlog's avoid-wasted-work principle fits. I did not read the rest of the fog code to confirm the per-frame upload claim. Medium.

### X-ARCHITECTURE-05: Scenes are a fixed ring of stateless singletons with no parameters, unfit for towns, dungeons and encounters

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** extensibility · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/scenes.cpp:21`

A scene change can name only a SceneKey:
- CycleScene goes to NextScene (a ring).
- A portal goes to NextWalkableScene (the ring, skipping scenes without a portal).
- Host::m_requested is a std::optional<SceneKey>.
- A save stores the scene as one byte of the list's index.
- Scenes are compile-time singletons with no instance data, so 'enter the town of place 17', 'fight this encounter of these NPCs' or 'this sect's hall' can't be expressed.
SceneContext's interface is also shaped by the demo (SpawnFox, SpawnPlayer, MapPlace).

**Evidence:** scenes.cpp 21-23 (SCENE_LIST), 63-73 (NextScene/NextWalkableScene); host.hpp 528 (m_requested optional<SceneKey>); host.cpp 1611 (scene saved as u8); scene.hpp 120-127 (SpawnFox/SpawnPlayer in the context interface).

**Impact:** Towns and other sub-scenes reached from the world map (core to the target genre) need per-instance parameters: a place index, an encounter spec, a return point. These would have to be saved, hashed and passed to Build, Portal, View and Publish. Adding them later means changing the save format, Requested(), SceneFacts, the logic's change protocol and every scene's hooks.

**Recommendation:** Introduce a SceneRequest { key, params } (a place or encounter ID by stable ID, plus a return point) that is saved and hashed with the scene, passed to Build and every hook through the context, and used for portals and map interactions in place of the ring. Make SceneContext generic (spawn by data-defined kind) rather than per creature.

**Verification:** host.hpp shows Build, FactsOf and ScratchBase all take a core::SceneKey, and the scene is saved as a single byte. So scenes are parameterless keys, as claimed. That is a fair extensibility concern for towns and encounters. But the game-design brainstorm and towns are not yet planned, the design is demo-stage, and the pre-1.0 no-compat note means the save format can change freely. It is a known gap that the planned design work should cover, not a defect. Medium.

### X-ARCHITECTURE-06: Combat is hard-wired to one player caster, foxes as the only enemies and one global loadout

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** extensibility · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/combat.cpp:72`

Combat::Cast takes `player` as the only caster. Tag treats exactly CreatureKind::Fox as 'enemy' (no faction or allegiance component). The loadout and Qi limits are global in GameData::abilities, not per caster. CreatureKind has two values, mapped to the fields GameData::cultivator and GameData::fox (DataOf in host.hpp 465). PublishOverhead and DebugBurst also test for CreatureKind::Fox.

**Evidence:** combat.cpp 64-87 ('the foxes are the only enemies so far'; kind != CreatureKind::Fox skipped); combat.hpp 207-240 (Cast(..., player, ...)); game_data.hpp 61-66; components.hpp 91-94; host.cpp 1200, 1424.

**Impact:** NPCs that fight back, sect battles, NPC-versus-NPC fights, bosses and companions all need casters other than the player, targeting by allegiance, and per-entity abilities. With the current shape, each new combatant type means editing core GameData, the CreatureKind enum, combat targeting and the save format.

**Recommendation:** Before adding content, add an Allegiance or Faction component and target 'hostile to the caster' rather than 'is a fox'. Move the loadout and Qi limits into a per-entity component (Caster already exists). Replace CreatureKind and the GameData::cultivator/fox fields with a data-defined creature table keyed by ID. This fits the planned data-driven sweep, but the structural part (factions, per-caster loadout) is engine work that should come first.

**Verification:** combat.cpp line 76 skips any creature whose kind is not CreatureKind::Fox. Line 513 also branches on Fox. So the hard-wiring is confirmed. The sweep-up backlog's 'data-driven sweep' item records hard-coded content (AbilityKind, HitKind, effects named after abilities) as deferred, to be raised when chunk 7's combat grows. Factions and per-caster loadouts are not named there. So the content half is a known deferral, and the structural half (allegiance, per-entity casters) is new. Medium.

### X-ARCHITECTURE-07: The active NPC tier is unusable on the world map; travellers are journey records, not entities

- **Severity:** medium · **Verdict:** partially-confirmed · **Category:** extensibility · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/npcs.cpp:255`

Activate asserts the NPC has no home ('an NPC activated in a scene with the world map, whose cell isn't a grid cell'). Materialise and Group work only on the legacy 64-column grid (MAP_COLUMNS, CELL_SIZE). Npc::cell means a grid cell in dev scenes but a map tile index on the world map. Travellers on the map are drawn by Host straight from Travel's journey records; they have no active entity, Body or components.

**Evidence:** npcs.cpp 255 (PN_ASSERT person.home == NO_HOME); npcs.hpp 36-46 (MAP_COLUMNS, CELL_SIZE, CellCentre 'for scenes without the world map'), 59-66 (dual meaning of cell), 241-248; host.cpp 1265-1355 (travellers published from journeys).

**Impact:** 0002 §7.7's tiers (active NPCs near the player, aggregates materialised on approach) don't exist where the real game happens. Talking to, fighting, trading with or being attacked by a traveller or townsperson needs an entity, and nothing defines how a journeying NPC becomes active and goes back to its journey. The arena-only grid code is dead weight that adds confusion.

**Recommendation:** Design tier changes for the world map (activating an NPC from its journey or tile and deactivating it back) before towns or encounters. Retire the 64-column grid by making dev scenes use a small WorldMap, so Npc::cell has one meaning.

**Verification:** The PN_ASSERT at npcs.cpp:255 (person.home == NO_HOME) exists with the message 'an NPC activated in a scene with the world map...', so Activate does assume the dev-scene grid. I did not trace Host's traveller drawing or find a design for world-map tier changes. The gap is plausible, but it may be a known phase ordering rather than an oversight. Verify the 0002 §7.7 tier plan and the spec Outcomes before treating it as new.

### X-ARCHITECTURE-08: src/AGENTS.md breaks the repo's own 'one line per rule, about 100 lines' rule with paragraphs of 5,000 to 15,000 characters that copy the code's docs

- **Severity:** medium · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/AGENTS.md:27`

Line 27 is a single 15,404-character 'rule' describing Host's whole behaviour. Lines 4 (7,276), 24 (7,254: the renderer's full draw order) and 31 (5,386: every command-line flag and key) are similar. The same content is restated in the Doxygen of host.hpp (for example, the 40-line Publish comment) and in the design docs. The root AGENTS.md requires 'one line per rule'; tests/AGENTS.md line 69 (5,806) and tools/AGENTS.md have the same problem.

**Evidence:** awk length check: src/AGENTS.md line 4 has 7276 chars, line 24 has 7254, line 27 has 15404, line 31 has 5386; AGENTS.md 'Agent docs: an AGENTS.md holds what any task in its directory needs (~100 lines, one line per rule)'.

**Impact:** Every agent touching src/ loads about 45 KB of narrative. Three copies of each behaviour (AGENTS.md, Doxygen, specs) drift apart, and the actual rules (include order, logging, numbers) are buried. Updating the docs in the same commit as each change, as the rules require, gets very costly, so drift is likely.

**Recommendation:** Cut src/AGENTS.md to rules and pointers (for example, 'the host's behaviour: game/host.hpp; the draw order: gfx/renderer.hpp Draw()'). Leave behaviour to the Doxygen and design docs. Consider a lint check on line length or file size for AGENTS.md.

**Verification:** awk shows src/AGENTS.md has 49 lines, with line 27 at 15404 characters and lines 4, 24 and 31 at 7276, 7254 and 5386. This breaks the root rule of one line per rule (about 100 lines). The lint-check suggestion is reasonable.

### X-ARCHITECTURE-09: Presentation-only state lives in, and is mutated by, the deterministic Simulation's Tick

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** determinism-risk · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:850`

Host::Tick updates presentation-only state inside the simulation tick:
- ShiftDrawnPlayer (m_drawnShift), called from Move and FollowRoute;
- StepPlayerWalk (m_playerWalk);
- m_worldTicksBefore.
AdvanceCalendar also clears the LegWalkCache and KeptWalks. core::Simulation exposes presentation setters (SetDebugSight, SetAnimations), and Publish is non-const and mutates caches. Determinism holds today only because every one of these fields is carefully left out of save and hash, and each one is documented separately ('Presentation only, so never saved, hashed or read by the simulation') rather than kept apart by structure.

**Evidence:** host.cpp 1011-1028 (Move calls ShiftDrawnPlayer), 1101-1115 (StepPlayerWalk), 1549-1590 (AdvanceCalendar drops kept walks); logic.hpp, Simulation::SetDebugSight/SetAnimations; host.hpp 498-522, comments repeating 'Presentation only'.

**Impact:** Nothing stops a future change from reading m_playerWalk, m_drawnShift or the walk caches in simulation code. If that happens, a replay or save silently diverges, because none of these survive a load (a load restarts them). The rule is enforced by comments alone.

**Recommendation:** Move presentation state into a separate object the simulation can't reach (a Publisher fed by read-only views of the state plus per-tick deltas such as the body step). Or at least put it in a struct passed to Publish only, so that simulation code can't name it. Consider a test that ticks with the presentation object reset every tick and checks the hashes match.

**Verification:** The presentation fields (m_drawnShift, m_playerWalk, the walk caches) are carried in Host and mutated in Tick and Move, and nothing structural stops simulation code from reading them. The claim holds as a structural-enforcement concern. No leak exists today, and the comments say the fields are never saved or hashed. Existing determinism tests compare hashes across worker counts and runs. A test that resets the presentation state every tick is not in place, which I did not check exhaustively. This is a hardening suggestion, not a defect. Low.

### X-ARCHITECTURE-10: core mixes the engine kernel with the game's domain model and presentation config

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** layering · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `src/core/game_data.hpp:61`

Because `game` links only `core` and `data` writes into core's types, all game vocabulary lives in `core`:
- the demo's content schema (GameData::cultivator and GameData::fox, 'The foxes circling the cultivator');
- AbilityKind (FireBolt, PoisonLance, ...);
- CasterView with Qi;
- journeys, world map, calendar, names;
- presentation-only config that gfx alone needs (WorldLook, Seals, Effects, UiColours, Animations, LegWalkCache, RoadCurve).
`game` can therefore include presentation tables: scene.hpp includes core/seals.hpp and core/glow.hpp, and host.hpp includes core/animations.hpp, leg_walk_cache.hpp and road_curve.hpp. core is 12.6k lines, the largest module.

**Evidence:** game_data.hpp 61-66; abilities.hpp 85-91; frame.hpp 288-301 (CasterView); scene.hpp 14-17; host.hpp 13-25; the src/core listing (world_look.hpp, seals.hpp, effects.hpp, ui_colours.hpp next to jobs.hpp, handover.hpp, logic.hpp).

**Impact:** Engine headers change with every content and look decision. The planned ui and audio modules will pull in the same grab bag. Nothing in the module structure stops simulation code from depending on a hot-reloaded presentation table. The 'engine' can't be reused or tested apart from the game's content.

**Recommendation:** Split core into:
- `core` (engine kernel: types, log, profile, jobs, handover, logic, archive, replay, ecs, math, random);
- a `model` or `world` module (the shared game vocabulary: game data, world map, journeys, calendar, abilities, view models), which data, game, gfx and app may include;
- presentation config types (`look`) that only data, gfx and app see, with game forbidden from including them by `MODULES`.

**Verification:** The MODULES map in tools/project_n.py makes game depend only on core, and data on core, so core is the shared vocabulary by design. The facts are plausible: game includes core presentation headers. The module layout is lint-enforced and a recorded structure. The finding's own status says it contradicts a recorded decision. Splitting core is a taste-level refactor with churn, and I did not verify the line counts or the specific includes. The sub-point that game can reach presentation tables is real but mild. Low.
