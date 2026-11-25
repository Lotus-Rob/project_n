# Review: game-scenes

**Scope:** src/game/scene.{hpp,cpp}, scenes.{hpp,cpp}, src/game/scenes/*, art/scenes.toml. Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 21 standing (0 critical, 0 high, 0 medium, 14 low, 7 info), 0 raised but refuted on verification.

**Parts:** game-scenes.md, game-scenes-part2.md, game-scenes-part3.md

## Assessment

## Scope: game-scenes (src/game/scene.*, scenes.*, src/game/scenes/*, art/scenes.toml)

**Strengths**
- The scene abstraction is small and clean. Each scene is a stateless `const` singleton with constexpr traits, and everything it changes goes through `SceneContext` into the host's saved state. Static asserts tie each key constant to its place in the list and keep the key within one save byte. `Host::Load` checks the scene byte before using it.
- The simulation-side code here is careful about determinism. Random draws are keyed by purpose, seed, state counter, entity and index. Orbit and respawn angles are worked out in f64, using `core::math::Atan2` and the exact `std::remainder`. The aura picks all its hits first and applies damage afterwards, so a death can't disturb the loop.
- Tests cover a lot of behaviour: names, F2 and portal order, `CheckScenes` failing when a scene is missing, the world view, place labels and fog, dev scratch worlds leaving the game's world alone, the crowd each scene builds, map-place restore, the aura, respawns, captions, demo glows and showcase seal overlap.

**Weaknesses**
- **Build ignores the current tick.** Every orbiting entity (arena foxes and orbs, and stress's 100,000 crowd members and bullets) spawns where it would be at tick 0. The first tick after entering then moves it to where it should be at tick T, so each F2 or portal entry after start draws a one-tick streak. No test enters a scene at a later tick.
- **The showcase rule is broken in the arena layout.** Two of the six wandering arena NPCs have home cells inside showcase seal footprints (`showcase_grand` and `showcase_tiers`). This contradicts the code comment "nothing stands on it" and the user's "showcases never overlap" rule. The showcase test only checks seals against each other.
- **Two hand-kept lists can drift apart.** Each scene's sprites are listed in art/scenes.toml and again implied by its C++ layout constants. Nothing checks one against the other: `CheckScenes` only checks the cultivator and fox sprites. Because the renderer never evicts sprites, a missing entry is hidden when the scene is reached from another scene and only shows when it is started directly. The checks that do exist are too coarse in the other direction: the fox is forced into the world scene's list and villager walk frames into arena and stress, where nothing uses them.
- **World-scene constants only fit the shipped test map.** `WORLD_PORTAL_OFFSET` is a fixed offset from the start city, never checked against the map's bounds or terrain. `WORLD_BUSIEST` is a fixed coordinate. Both assume the shipped world.yaml, which conflicts with the recorded rule that maps are procedural per seed.
- **The scene model won't scale to Tale-of-Immortal-like play as it stands.** It is a fixed list of singletons whose index is at once the save key and the F2 order. Portals go to the next scene in the list, not to a chosen destination, and a scene can't be parameterised (a battle on tile X, the town of city Y). Dev and game scenes share one list, F2 isn't limited to dev builds, and the world's only portal leads into the dev arena.
- **Docs have drifted** in several places: `NextWalkableScene`'s doc, the "adding a scene" steps, the scenes.toml header, host.hpp's `PublishGround` text, `PURPOSE_HOME_TILE`'s pointer, and src/AGENTS.md's single enormous line. There are also smaller smells: ability-use source numbers spread over three files, an unexplained player check on the tablet caption left over from a refactor, a stress `Respawn` override that can never fire, and an aura range that wraps to "always the minimum" at the u32 limit.

**Overall health:** good for a demo and harness layer. The code is tidy, deterministic and well tested for what it claims. The real defects are visual or about layout (the entry streak and NPCs on showcase seals). The structural risks are the hand-kept sprite lists and the map-specific world constants. The scene model will need redesigning before game content such as towns, battles and sects arrives.

**Files read in full:** src/game/scene.hpp, src/game/scene.cpp, src/game/scenes.hpp, src/game/scenes.cpp, src/game/scenes/arena.hpp, src/game/scenes/arena.cpp, src/game/scenes/arena_layout.hpp, src/game/scenes/arena_layout.cpp, src/game/scenes/cells.hpp, src/game/scenes/cells.cpp, src/game/scenes/painted.hpp, src/game/scenes/painted.cpp, src/game/scenes/stress.hpp, src/game/scenes/stress.cpp, src/game/scenes/world.hpp, src/game/scenes/world.cpp, art/scenes.toml, tests/unit/game/scenes_tests.cpp, src/game/random_purposes.hpp

## Findings

### GAME-SCENES-01: Orbiting entities spawn at their tick-0 positions, so every scene entry after start streaks them across the screen for a tick

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** bug · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/scenes/arena_layout.cpp:115`

BuildArenaLayout spawns each fox at `OrbitPoint(orbit, 0)` (line 115) and each orb the same way (line 130). Stress's SpawnOnOrbit (stress.cpp:43) does the same for 90,000 crowd members and 10,000 bullets. `Scene::Build` receives no tick, but `counters.simulation` keeps counting across scene changes (TickCounters::Count). On the first tick after entry, Host::Tick sets `body.previous = body.current` (the tick-0 point) and `body.current = OrbitPoint(orbit, T)`. Every orbiting entity therefore jumps from its tick-0 angle to its tick-T angle within one tick, and the renderer interpolates that jump. The publishes made while the scene loads, before that first tick, also show the tick-0 layout.

**Evidence:** arena_layout.cpp:115 `context.SpawnFox(orbit, OrbitPoint(orbit, 0));`, :130 `context.Spawn(OrbitPoint(orbit, 0), look)`; stress.cpp:43 the same; host.cpp Tick: `body.previous = body.current; body.current = OrbitPoint(orbit, counters.simulation);`. RespawnFox (arena_layout.cpp:150-160) does take the current tick into account, so only Build is out of step. host_tests 'a tick moves the foxes...' only covers a fresh host at tick 1.

**Impact:** Every F2 or portal entry into arena or stress after start shows foxes, orbs and up to 100,000 stress entities sliding across their circles for one tick, which is visible at high refresh rates. It doesn't affect determinism.

**Recommendation:** After Build (and after the scratch base has been copied, so the shared base stays independent of the tick), have the host set every Orbit body's previous and current to OrbitPoint(orbit, current simulation tick). Alternatively pass the entry tick to Build. Add a test that enters the arena at a large tick and checks previous == current == OrbitPoint(orbit, T) for every orbit entity.

**Verification:** arena_layout.cpp:115,130 and stress.cpp:43 spawn at OrbitPoint(orbit,0). Host::Tick (host.cpp:887-889) sets previous=current then current=OrbitPoint(orbit,counters.simulation). Host::Build (host.cpp:798-840) takes no tick and does no fix-up after Build. The mechanism holds, so entry at a large tick gives a one-tick jump that is interpolated. It does not affect determinism and lasts one tick, so low rather than medium. I found no test for it (host_tests only covers a fresh host). I did not confirm that the counter keeps running across scene changes, but Build has no reset.

### GAME-SCENES-02: Two arena NPCs live and wander inside showcase seals, contradicting 'nothing stands on it' and the showcase rule

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/scenes/arena_layout.cpp:65`

ACTIVE_CELLS places NPCs at cell (2,3), centre (640, 896), and cell (3,3), centre (896, 896). Activate puts them on the cell centre, and DecideActive wanders them ±102 units around it (WANDER_SPAN 0.8 × 256 × ±0.5). With SEAL_VIEW_RATIO 0.375: the grand seal at (402, 935), size 290, is an ellipse 290 × 108.75, and (640, 896) gives 0.67 + 0.13 = 0.80 < 1, so inside. The tiers seal at (975, 890), size 100, is 100 × 37.5, and (896, 896) gives 0.62 + 0.03 < 1, so inside. The cell (6,1) NPC also stands on the pine's foot at (1660, 400).

**Evidence:** arena.cpp:42 comment: 'The largest ... in the open lower left below the fight: nothing stands on it.' arena_layout.hpp's ARENA_PORTAL doc says showcase_tests checks only seal against seal (and the portal and fight); it never checks props or NPCs. Memory rule: 'Showcases never overlap: every showcased item in its own clear space ... test footprints'.

**Impact:** The showcase is meant to show each seal alone in a clearing; villagers walk across two of them. The reference pictures and the overlap test can't catch it, because NPC wander is not part of the footprint check.

**Recommendation:** Move the two NPC cells (or the seals) so each wander box, cell centre ± WANDER_SPAN × CELL_SIZE / 2, stays clear of every showcase footprint. Extend app/showcase_tests to check prop feet and NPC wander boxes against the seal boxes, and fix the comment.

**Verification:** The finding assumes the seal's size is its semi-axis. showcase_tests.cpp:156 says the seal is twice its step-area radius, so size is the full width. The ellipses are then 145x54 (grand) and 50x19 (tiers). The NPC cell centres (640,896) and (896,896) are outside both, so the 'inside' arithmetic is wrong. The wander is +/-0.4 cell (npcs.cpp:18, :312), about +/-102 units. That reaches the grand ellipse's edge at x~538,y~935, and it clearly enters the tiers seal (centre dx 79, half-width 50). So NPCs can still walk over parts of two showcases, and the arena.cpp:42 'nothing stands on it' comment is shaky. The showcase test only checks seal against seal. Overlap is real but smaller than claimed. I did not check the pine foot claim.

### GAME-SCENES-03: Nothing checks that each scene's drawn sprites are in its art/scenes.toml list; CheckScenes only checks two creature sprites

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/scenes.cpp:90`

Each scene's sprite list in art/scenes.toml is kept by hand alongside the C++ constants that decide what it draws: PROPS, ARENA_PAINTED, PAINTED_SPRITES, CELL_SPRITES, the stress crowd's Fox/Cultivator/OrbBlue/OrbRed, Villager for NPCs, Shadow. CheckScenes only checks `data.cultivator.sprite` and `data.fox.sprite` for scenes with `creatures`. The renderer marks sprites resident and never evicts them (renderer.cpp ContinueScene), so a sprite missing from a scene's list still draws when an earlier scene loaded it. It is only broken when the game starts directly in that scene (`--painted` etc.) or when the order changes.

**Evidence:** scenes.cpp:86-109 checks only the world map and the two creature sprites; no unit test compares a scene's published instances, or its layout sprites, with `manifest.Scene(id).sprites`. The reference tests cover only what their cameras frame.

**Impact:** Adding a prop or effect sprite to a scene's C++ without updating scenes.toml passes `check` and draws an empty or undefined texture slot for players who start in that scene. This is the same class of gap as the known-deferred 'seal sprites not checked against the scene list', but for layout and creature sprites.

**Recommendation:** Add a unit test that builds each scene (Host per SceneKey), publishes with a whole-scene view, maps every instance back to its sprite (regionMin/page) and checks that the sprite is in the scene's manifest list. Better still, have each Scene declare its sprites (a constexpr span in SceneTraits) and have CheckScenes or the atlas tool check scenes.toml against it.

**Verification:** scenes.cpp:86-109 does check only the two creature sprites, and only for scenes with creatures. docs/design/0002.3.1-manifest-and-scene-loading.md:116-119 records that scope as built. The renderer's never-evict behaviour is as described (I did not re-read renderer.cpp). No test compares a scene's drawn sprites with its toml list. The gap is real and fits the known 'seal sprites not checked' deferral. Impact is limited to starting directly in a scene whose list lacks a sprite, so low-medium.

### GAME-SCENES-04: World portal is a fixed offset from the start city, never checked against the map's bounds or terrain

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/scenes/world.cpp:36`

WorldScene::Portal returns `StartCentre() + WORLD_PORTAL_OFFSET` (-560, -200). Nothing checks that this point lies on the map, on land the cultivator can walk, or away from places and roads. The doc's claims ('on the first screen, in the quiet country round the start ... off the strip ... `--camera-path` walks along') hold only for the shipped world.yaml. Held movement isn't clamped to the map (Host::Move), while WalkTo ignores off-map points, so a portal off the map can only be reached by held walking off the map's edge.

**Evidence:** world.hpp:25 `inline constexpr core::Vec2 WORLD_PORTAL_OFFSET{ .x = -560.0F, .y = -200.0F };` With tile_size 256, a start city in column 0-1 or row 0 puts the portal at negative coordinates. No test checks the portal tile against the map apart from the shipped one.

**Impact:** On a procedural map (the recorded direction: maps per seed, world.yaml only a test map), the portal can land off the map, in the sea, or on a place's marker. The only way out of the world scene would then be F2.

**Recommendation:** Derive the portal per map: pick the nearest walkable, empty tile in a preferred direction from the start (deterministic from the map), or have the generator place a portal site and store it in WorldMap. Check it in data::LoadWorldMap or GenerateWorld, and test that it lies on the map and on walkable ground.

**Verification:** world.cpp:36-40 returns StartCentre()+WORLD_PORTAL_OFFSET with no bounds or terrain check. The code is as described. Maps are meant to be procedural per seed (memory procedural-world-map), so the fixed offset can go wrong later. Today it is only a latent problem with world.yaml, which is a test map. The portal leads into the dev arena and F2 is the other way out, so low.

### GAME-SCENES-05: Scene model is a fixed list of singletons keyed by index; it can't express parameterised scenes or portal destinations

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/scenes.hpp:49`

A scene is identified only by its index in SCENE_LIST, which is at once the F2 order, the save key (one byte) and the portal chain (NextWalkableScene = next scene in the list that has a portal). A scene has exactly one portal, its destination is implied by list order, and a scene can't carry parameters (which tile's battle, which city's town, which sect's grounds). Dev scenes (arena, stress, painted, cells) and the one game scene share the list; F2 isn't limited to dev builds (bindings.cpp:99), and the world's only portal leads into the dev arena.

**Evidence:** scenes.cpp SCENE_LIST and NextWalkableScene; Host::Load reads `u8 scene`; SceneTraits::portal is a single optional point.

**Impact:** The target genre needs scenes entered from map locations with context: towns per city, encounters per tile, sect grounds, dungeons. Saves would need to store the scene's parameters, and portals or exits would need explicit destinations. Every new scene also renumbers later keys (accepted before 1.0, but the coupling remains).

**Recommendation:** Before chunk 6's content work, design scene instances as kind + parameters (saved), with exits naming their destination. Split dev scenes into a debug-only registry so F2 and portals in release only reach game scenes. Record the plan in the game-design brainstorm.

**Verification:** The facts hold: scenes are identified by SCENE_LIST index, the save stores one scene byte, and there is one optional portal per scene (Host::Load, SceneTraits::portal). The recommendation (scene instances as kind plus parameters) is a forward-looking design opinion, and it is reasonable ahead of the towns work in chunk 6 and the pending game-design brainstorm (game-design-gaps). No defect today, and pre-1.0 renumbering is accepted (memory pre-1-0-no-compat). I did not find a doc that rules on parameterised scenes.

### GAME-SCENES-06: Aura range wraps to 0 when max - min = u32 max, so the aura always deals its minimum

- **Severity:** low · **Verdict:** confirmed · **Category:** bug · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/scenes/arena.cpp:84`

`core::ToBelow(random, high - low + 1)` is computed in u32. abilities.cpp accepts amounts up to MOST_AMOUNT = u32 max, so damage_min: 0 with damage_max: 4294967295 (or healing likewise) makes the bound wrap to 0. ToBelow(random, 0) returns 0, so every hit is exactly `low`.

**Evidence:** arena.cpp:74-75,84; data/abilities.cpp:531 `constexpr i64 MOST_AMOUNT = std::numeric_limits<u32>::max();` and checks only min <= max.

**Impact:** A legal data edge case silently gives the wrong distribution. There's no UB, and the shipped data is far from the limit.

**Recommendation:** Compute the span in u64 and draw with a 64-bit bounded draw, or cap the amounts below u32 max in the data reader. Add a test for the full range.

**Verification:** core::ToBelow(u64, u32 bound) (random.hpp:54) takes a u32. arena.cpp:84 computes high-low+1 in u32, which wraps to 0 when low=0 and high=u32 max. The data reader's cap of u32 max is as cited and I did not re-read it. A wider bound (ToBelowWide exists at random.hpp:61) would fix it. The real risk is negligible: it is an extreme data edge case in a dev arena aura.

### GAME-SCENES-07: Host's default data makes F2 and portals reach the world scene without a map (assert, or null dereference with assertions off)

- **Severity:** low · **Verdict:** deliberate · **Category:** error-handling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/scenes/world.cpp:50`

NextScene and NextWalkableScene ignore what a scene needs from the data. WorldScene::Portal and View check `Data().world == nullptr`, but Build (line 50) and Publish (line 55) dereference it unchecked. Host's default `data = BuiltInData()` has no map (host.hpp:72 admits it fails CheckScenes). From the default arena host, the portal leads to stress, and stress's portal leads to world (NextWalkableScene counts portalFromMap whether or not a map exists). Host::Build then hits PN_ASSERT, and with assertions off WorldScene::Build dereferences null.

**Evidence:** scenes.cpp:71; world.cpp:50 `context.Data().world->StartCentre()`, :55 `*context.Data().world`; host.hpp:72-77 default argument.

**Impact:** The shipped game is unaffected because main and the data reload run CheckScenes. Tests and tools that use the default Host can crash by cycling or walking through portals, and the null checks are inconsistent across WorldScene.

**Recommendation:** Skip scenes whose needs the data doesn't meet in NextScene and NextWalkableScene (or have Host refuse them), or make Host's default data include a small map. Make WorldScene's handling of a missing map consistent.

**Verification:** host.hpp:72 documents that a Host must pass CheckScenes and that the default BuiltInData() lacks the map. host.cpp:823 asserts it. main.cpp:131 and app.cpp:479 run CheckScenes. NextWalkableScene (scenes.cpp:71) does not look at map availability, so the facts hold. Only default-data test hosts can hit this, and it is a documented precondition. The inconsistent null checks in WorldScene are a minor nit.

### GAME-SCENES-08: Stress profiling scene skips the demo glows and showcase seals, so their cost is never measured

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/scenes/stress.cpp:103`

Stress builds the arena layout (shrine, stele, crystal) but its traits have no `demoGlows`, and its Publish only adds the caption. The 7 showcase seals, the most expensive scene content for seal layout and particles, are absent, and the portal seal is hidden under the crowd (a documented limit).

**Evidence:** stress.cpp:14-23, 103-106; arena.cpp Publish pushes SHOWCASE.

**Impact:** `measure`'s and the profiler's stress numbers understate a real arena frame's glow and seal cost, while the known-deferred 'paused frames still re-lay seals' and seal-on-GPU work depend on such measurements.

**Recommendation:** Either give stress the arena's demo glows and showcase seals (accepting new replay hashes, which is allowed before 1.0), or document that stress measures entity throughput only and add a separate seal load to measure.

**Verification:** Stress Publish only calls PublishCaption (stress.cpp:103-106), so it adds no showcase seals. Whether that is intended for a throughput scene is unrecorded, and I did not search the docs for it. Real but a design choice, low impact.

### GAME-SCENES-09: Cells scene draws the whole painted ground under a grid that covers it completely

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/scenes/cells.cpp:15`

CellsScene doesn't override PublishGround, so the default draws the 1920×1080 painted ground whole, and scenes.toml loads `ground` for it. The 32×20 grid of 256-unit cells spans x -3136..5056 and y -2020..3100 and hides it entirely. CELLS_MIN also repeats the reference centre as the literals 960/540 instead of deriving it from GROUND.

**Evidence:** cells.cpp:15-16, 36-46; scene.cpp:31-43 default PublishGround; scenes.toml `[cells] sprites = ["ground", ...]`.

**Impact:** One full-screen quad of overdraw and one needless sprite upload in a scene meant for judging mips, plus a hidden coupling to the ground's size.

**Recommendation:** Override PublishGround in CellsScene to draw nothing and drop `ground` from its list. Derive CELLS_MIN from (GROUND.min + GROUND.max) / 2.

**Verification:** cells.cpp has no PublishGround override, and the 32x20 grid of 256 units covers the whole ground. CELLS_MIN hardcodes 960 and 540 (lines 15-16). Minor overdraw in a dev scene. I did not check scenes.toml for the ground entry.

### GAME-SCENES-10: SceneTraits is a set of correlated flags whose invariants are not enforced at compile time

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** api-design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/scene.hpp:62`

`portal` vs `portalFromMap`, `view` vs `viewFromMap`, `worldMap` (which today implies portalFromMap and viewFromMap and not devScene), and `devScene` vs `worldNpcs` are separate fields with prose rules ('Such a scene leaves `portal` empty'). Only devScene/worldNpcs is asserted, at runtime in Host::Build. Nothing stops a scene from setting both `portal` and `portalFromMap`, or `viewFromMap` with a non-empty `view`; Portal() and View() would then quietly prefer one.

**Evidence:** scene.hpp:62-99; host.cpp `PN_ASSERT(traits.devScene || traits.worldNpcs == 0, ...)`.

**Impact:** New scenes are easy to misconfigure, and the bool-per-feature pattern grows with each scene kind.

**Recommendation:** Add static_asserts in scenes.cpp over each instance's traits (cheap, since they are constexpr), or model the alternatives as variants (for example `std::variant<Point, FromMap>` for the portal and the view).

**Verification:** Only the devScene/worldNpcs assert exists (host.cpp:824), and CheckScenes covers worldMap and creatures only. The other combinations are not enforced, which matches the claim. Impact is small because the scene list is short. Static_asserts would be a cheap improvement.
