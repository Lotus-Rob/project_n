# Review: game-scenes (part 2)

### GAME-SCENES-11: Ability-use source numbers are spread over three files, with no registry

- **Severity:** low · **Verdict:** confirmed · **Category:** maintainability · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/scenes/arena.cpp:29`

USE_AURA = 1 is in arena.cpp, USE_DEBUG_BURST = 2 in host.cpp (whose comment says 'the arena's aura is 1'), and CAST_SOURCE = 16 in combat.hpp ('Below it are the aura's and the burst's'). Random purposes have a single header (random_purposes.hpp) for exactly this reason; ability-use sources don't.

**Evidence:** arena.cpp:29; host.cpp:46; combat.hpp:31.

**Impact:** A new scene effect could reuse 1 or 2 and collide with an existing use's ID, mixing hits and use ends in the presentation.

**Recommendation:** Move the source numbers into one header (for example next to random_purposes.hpp), with a static_assert that they stay below CAST_SOURCE.

**Verification:** USE_AURA = 1 is at arena.cpp:29, USE_DEBUG_BURST = 2 at host.cpp:46, and CAST_SOURCE = 16 at combat.hpp:31. The numbers are scattered, with only comments tying them together. A registry would be a small maintainability gain.

### GAME-SCENES-12: Docs drifted from code in the scene API

- **Severity:** low · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/scenes.hpp:49`

(1) scenes.hpp:49: NextWalkableScene is documented as stopping at scenes 'whose traits have a portal (SceneTraits::portal)', but the code also accepts `portalFromMap` (the world). (2) scene.hpp:7 and scenes.hpp:5: 'Adding a scene is its class..., an instance..., its key constant' leaves out the art/scenes.toml entry and `./dev atlas`, without which CheckScenes refuses to start. (3) art/scenes.toml:1-2: 'in the order F2 cycles through them: a scene's place here is its SceneId'. F2's order is scenes.cpp's SCENE_LIST; the toml order only fixes the manifest's SceneId, which is looked up by name, so the two orders are unrelated. (4) host.hpp:333: 'Scene::PublishGround(): the painted ground, or the world's tiles'. The world scene publishes no ground; the renderer draws the map. (5) src/AGENTS.md: PublishGround is 'culled to the publish's area by the scene itself, drawn whole', but the default ignores `view` and draws the ground whole. (6) world.hpp:5: 'about 20 screens across', and WorldScene::Publish's doc leaves out the explored-tile condition. (7) random_purposes.hpp: PURPOSE_HOME_TILE says 'game/scenes/world.cpp', but it is used in game/world_state.cpp.

**Evidence:** See the lines quoted. scenes.cpp:71 has `!portal && !portalFromMap`; scene.cpp:31-43; world_state.cpp:40 uses PURPOSE_HOME_TILE.

**Impact:** Misleading guidance for whoever adds the next scene or changes the ground or portal rules.

**Recommendation:** Fix each statement: mention portalFromMap; add 'list its sprites in art/scenes.toml and run `./dev atlas`' to the steps; reword the toml header; update host.hpp and src/AGENTS.md; point PURPOSE_HOME_TILE at world_state.cpp; and say 'explored' in the world Publish doc.

**Verification:** Checked scenes.hpp:48 vs scenes.cpp:71 (portalFromMap accepted too), scene.hpp:7 and scenes.hpp:5 (no mention of scenes.toml or atlas), art/scenes.toml:1-2 (order claim), scene.cpp:31-43 (PublishGround ignores view, draws whole, skips only for worldMap), and random_purposes.hpp vs world_state.cpp:40. These match. I did not check items 4 and 6 (host.hpp:333, world.hpp:5) individually.

### GAME-SCENES-13: src/AGENTS.md describes scenes and the host in a single line thousands of words long

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** agents-md · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/AGENTS.md:27`

The root AGENTS.md rule is 'an AGENTS.md holds what any task in its directory needs (~100 lines, one line per rule)'. src/AGENTS.md line 27 packs scenes, host, saves, travel, fog, routes, labels, walk caches and portals into one paragraph of several thousand words.

**Evidence:** src/AGENTS.md:27 (seen via grep: the line covers 'The arena ... is the growing demo scene' through 'with a caption over it').

**Impact:** Hard for agents and people to find the rule they need; edits conflict; and it contradicts the repo's own format rule.

**Recommendation:** Split it into one line per rule, and move the detailed host and save mechanics into the Doxygen of game/host.hpp, or into a skill as the root guide suggests.

**Verification:** src/AGENTS.md line 27 is a single 15,404-character line in a 49-line file. That is roughly 2,500 words, not thousands, but it clearly breaks the one-line-per-rule guidance in the root AGENTS.md. The finding's real point holds.

### GAME-SCENES-14: Showcase test passes for a showcase key that seals.yaml lacks

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/scenes/arena.cpp:41`

SHOWCASE keys are hashed names. gfx's SealDrawOne returns nullptr for an unknown key, so app/showcase_tests' Footprint yields an empty Box (min = +max, max = lowest), which never overlaps anything, and the test passes. host_tests only compares the published keys with a copy of the same names. Only the showcase seals the reference pictures frame would show the gap.

**Evidence:** showcase_tests.cpp Box defaults and Overlaps; seal_draw.cpp:702-705; host_tests.cpp:1080 duplicates the 7 key names.

**Impact:** Renaming a look in seals.yaml without updating arena.cpp silently drops that showcase seal.

**Recommendation:** In showcase_tests, REQUIRE that each published seal key resolves in test::ShippedSeals() and that each footprint is non-empty.

**Verification:** seal_draw.cpp:702-705 returns nullptr when the key is not in the look table. In showcase_tests.cpp, Footprint then collects no vertices, and the test only checks pairwise overlaps (the REQUIRE is boxes.size() > 1), so a missing key adds an empty box and passes. I did not check whether the Box defaults make an empty box never overlap, or the host_tests duplicate at line 1080. The gap is plausible but low impact.

### GAME-SCENES-15: WORLD_BUSIEST is a fixed coordinate in the shipped map, living in game code

- **Severity:** info (reviewer said low) · **Verdict:** confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/scenes/world.hpp:21`

`WORLD_BUSIEST{33'000, 10'800}` is 'a point in the port's tile' of the shipped world.yaml. Game code exports it for app's camera_path, measure, and the reference and unit tests. It is a measurement fixture tied to one test map, not a property of the world scene.

**Evidence:** world.hpp:19-21; users in app/camera_path.cpp:22,29 and in tests; shipped_world_tests checks that it lies in the port's tile.

**Impact:** Procedural maps make it meaningless; the measurement path and the reference pictures would fly over arbitrary terrain.

**Recommendation:** Compute the busiest point from the map (for example the city with the most people or draws) in app/camera_path, or keep the constant in test support next to the shipped-map fixtures.

**Verification:** WORLD_BUSIEST is a fixed shipped-map coordinate that is also used by app and tests (shipped_world_tests checks it). It is a fixture of the shipped map living in game code. It only matters for measurement and reference pictures, not gameplay. A tidy-up at most.

### GAME-SCENES-16: Creature and animation sprite rules force unused sprites into scenes (fox into world, villager walk frames into arena and stress)

- **Severity:** info (reviewer said low) · **Verdict:** deliberate · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `art/scenes.toml:46`

CheckScenes requires both `data.cultivator.sprite` and `data.fox.sprite` in every scene with `creatures`. The world scene has no fox, yet must list `fox`; the toml comment admits it ('the fox too, as the creatures data draws both'). animations.yaml's rule, 'every scene that draws the standing sprite must draw each of [its frames]', forces villager_walk_0..7 into arena and stress. Yet DrawnPoseOf only animates the player ('Only the cultivator walks by its clip: the arena's wandering crowd stands in its sprite as ever').

**Evidence:** scenes.cpp:90-98; scenes.toml lines 10 and 27 (villager_walk_* in arena and stress) and 46 (`"fox"` in world); host.cpp DrawnPoseOf: `if (entity != player || animations == nullptr) return { .sprite = look.sprite };`

**Impact:** Nine sprites are decoded, distance-fielded and uploaded that are never drawn (fox in world, 8 villager frames in arena and stress), which adds to scene load time. The `creatures` trait is also too coarse to say which creatures a scene really has.

**Recommendation:** Replace `bool creatures` with the creature kinds a scene spawns (cultivator, fox), and check only those. Narrow the animation rule to sprites that actually animate in a scene (the player's sprite), or let the scene declare which figures walk.

**Verification:** The creatures rule is recorded as intended in docs/design/0002.3.1-manifest-and-scene-loading.md:116-119 ('every scene lists the creatures' data-table sprites'), and the scenes.toml comment admits the fox in world. The animation rule is real: animations.yaml:13 lists the villager_walk frames, and host.cpp DrawnPoseOf animates only the player. So a few sprites are loaded unused. That costs a little load time and is a coarse-rule trade-off. I did not verify the count of nine.

### GAME-SCENES-17: Tablet caption disappears when the player dies, because of a player check left over from a refactor

- **Severity:** info (reviewer said low) · **Verdict:** confirmed · **Category:** bug · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/scenes/arena_layout.cpp:172`

PublishCaption returns early when there is no player. Before the 'make each scene a class of its own' refactor, the caption code sat at the end of Arena::PublishOverhead, which returns early for no player. The move kept that condition, though the tablet's caption has nothing to do with the player. The header doc ('while there is a player') records the behaviour but gives no reason.

**Evidence:** git show acb23c2f: the caption block was removed from Arena::PublishOverhead after `if (m_player == core::StableId{}) return;` and re-added with the same guard.

**Impact:** Once the cultivator dies in the arena or stress, the tablet loses its caption while the tablet itself is still drawn.

**Recommendation:** Drop the player check (and update the header and host_tests), or document why the caption depends on the player.

**Verification:** PublishCaption (lines 172-175) returns early on no player, and nothing in the function uses the player. Cosmetic: the caption disappears after death. I did not check git blame for the refactor or the host tests.

### GAME-SCENES-18: StressScene::Respawn can never fire: foxes can't die in a non-combat scene

- **Severity:** info · **Verdict:** confirmed · **Category:** dead-code · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/scenes/stress.cpp:99`

Stress has no `combat` trait, so casts are ignored; it has no aura Tick; and F7's burst only produces numbers. No stress fox ever reaches zero health, so m_combat.Tick never returns respawns there. The override (and its doc) only matters for a hand-crafted save holding dying foxes.

**Evidence:** stress.cpp:14-23 traits without `.combat`; host.cpp CastAction checks IsCombatScene; the arena's Aura is only in ArenaScene::Tick.

**Impact:** A small amount of misleading API: readers may assume foxes die in stress.

**Recommendation:** Keep it as a safeguard for loaded saves, with a comment saying so, or remove it and let the default no-op apply.

**Verification:** Stress has Respawn overridden at lines 99-101 and StressScene traits at lines 14-23. The no-combat claim matches what I read, but I did not trace every path that could reduce a fox's health. It is harmless as a safeguard for loaded saves, so at most a comment is needed.

### GAME-SCENES-19: World place labels scan every place on each publish

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/scenes/world.cpp:60`

WorldScene::Publish loops over all `map.Places()` on each publish, testing each tile centre against the view and the fog. That's fine for the shipped map's few dozen places, but it is linear in the map's size, with no spatial index, for procedural maps that may be very large.

**Evidence:** world.cpp:60-83.

**Impact:** Per-publish cost grows with total places rather than visible places. That is minor today but runs against the 'hot paths compute only what is drawn' rule at large map sizes.

**Recommendation:** Bucket the places by tile row or a coarse grid once per map (WorldMap already indexes tiles), and visit only the buckets in view.

**Verification:** The loop at world.cpp:60-83 is linear in map.Places() with a cheap Contains test first, so the per-publish cost is small. The claim is speculative since the procedural map's place count is unknown. It is a scaling note, not a defect.

### GAME-SCENES-20: Tests duplicate scene constants instead of using them

- **Severity:** info · **Verdict:** confirmed · **Category:** duplication · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/scenes/arena_layout.hpp:33`

tests/reference/reference_tests.cpp:946 defines its own `CRYSTAL_FOOT{420, 620}` although game::CRYSTAL_FOOT is public. scenes_tests.cpp:172 copies PLACE_NAME_GAP = 8 from world.cpp's anonymous namespace.

**Evidence:** reference_tests.cpp:946; scenes_tests.cpp:172.

**Impact:** Moving the crystal breaks the reference test's distance logic without a compile error. The PLACE_NAME_GAP copy may be a deliberate pin.

**Recommendation:** Use game::CRYSTAL_FOOT in the reference test. Keep pinned values only where pinning is the point, and say so in a comment.

**Verification:** reference_tests.cpp:946 defines a local CRYSTAL_FOOT{420,620}. scenes_tests.cpp:172 copies PLACE_NAME_GAP=8 from the anonymous namespace in world.cpp:24, which is not public, so that copy is arguably a deliberate pin. I did not check whether game::CRYSTAL_FOOT is public.
