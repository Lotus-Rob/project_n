# Finding index 5/11 (standing findings, most severe first)

The ID prefix is the unit file: e.g. CORE-WORLD-03 is in core-world.md (or its -partN files).

| ID | Severity | Verdict | Status | Where | Finding |
|---|---|---|---|---|---|
| GAP2-MEMORY-BUDGET-VS-0002-S13-06 | low | unverifiable | new | `src/core/draw_list.hpp:118` | DrawList holds each instance three times; its staging alone exceeds the 16 MB per-tick line at 100k instances |
| GAP2-MEMORY-BUDGET-VS-0002-S13-07 | low | unverifiable | new | `src/core/logic.cpp:312` | Saving holds about three buffer copies, and the serialise vector grows by doubling with no reserve |
| GAP2-MEMORY-BUDGET-VS-0002-S13-08 | low | partially-confirmed | new | `src/core/replay.cpp:187` | Replay keeps its start state (a full raw save) and tick arrays after recording or playback ends |
| GAP2-MEMORY-BUDGET-VS-0002-S13-09 | low | confirmed | new | `src/game/host.cpp:731` | A loaded dev-scene save keeps the scene's whole active domain in m_scratchBase |
| GAP2-MEMORY-BUDGET-VS-0002-S13-10 | low | partially-confirmed | new | `docs/design/0002-engine-architecture.md:412` | §8.7's planned atlas size no longer fits the 1.5 GB GPU line once the distance-field array is counted |
| GAP2-MEMORY-BUDGET-VS-0002-S13-11 | low | confirmed | new | `src/game/host.cpp:1412` | Every publish copies the whole explored bitset into the snapshot instead of sharing it |
| GAP2-REPO-CONFIG-FILES-02 | low | partially-confirmed | contradicts-recorded-decision | `.clang-tidy:14` | bugprone-exception-escape disabled with no replacement for noexcept and job bodies |
| GAP2-REPO-CONFIG-FILES-03 | low | confirmed | new | `.clang-tidy:2` | cert-* family and several other relevant checks are not enabled |
| GAP2-REPO-CONFIG-FILES-04 | low | partially-confirmed | new | `.clang-tidy:2` | Enum-count drift has no static guard |
| GAP2-REPO-CONFIG-FILES-05 | low | partially-confirmed | new | `.clang-tidy:42` | HeaderFilterRegex 'src/.*' is unanchored and includes generated headers |
| GAP2-REPO-CONFIG-FILES-06 | low | partially-confirmed | new | `.clangd:1` | .clangd diagnostics are weaker than the build and lint |
| GAP2-REPO-CONFIG-FILES-07 | low | confirmed | new | `.editorconfig:3` | .editorconfig says LF for every file, but dev.cmd is committed with CRLF |
| GAP2-REPO-CONFIG-FILES-08 | low | partially-confirmed | new | `docs/Doxyfile:11` | Doxyfile INPUT covers src/*/generated and internal/ headers; only pch.hpp is excluded |
| GAP2-REPO-CONFIG-FILES-09 | low | confirmed | new | `src/gfx/module.dox:1` | gfx/module.dox feature list omits line_ribbons and is hard to maintain |
| GD-COMBAT-SYSTEMS-09 | low | deliberate | contradicts-recorded-decision | `src/game/combat.cpp:123` | Re-applying an affliction resets its clock, so the shipped generator and spender DoTs never tick in normal play |
| GD-COMBAT-SYSTEMS-10 | low | confirmed | new | `src/game/combat.hpp:163` | No status/effect model: two hard-coded DoTs, no buffs, debuffs, crowd control, resistances, crits or triggers |
| GD-COMBAT-SYSTEMS-11 | low | unverifiable | new | `assets/data/abilities.yaml:36` | Flying swords dominate the shipped balance by an order of magnitude; most of a cast is overkill |
| GD-COMBAT-SYSTEMS-12 | low | partially-confirmed | new | `src/data/abilities.cpp:338` | `element: healing` on an attack damages enemies while reading as healing |
| GD-COMBAT-SYSTEMS-13 | low | confirmed | new | `src/data/abilities.cpp:376` | The YAML `burn:` / `poison:` key is discarded; the affliction's kind comes from `element` |
| GD-COMBAT-SYSTEMS-14 | low | confirmed | new | `src/game/combat.cpp:318` | Kind-specific ability fields are accepted and silently ignored |
| GD-COMBAT-SYSTEMS-15 | low | partially-confirmed | new | `src/core/volley.cpp:227` | Per-frame sword retargeting is O(targets²) per sword, a scaling risk for chunk 7's hundreds of trash |
| GD-COMBAT-SYSTEMS-16 | low | partially-confirmed | known-deferred | `assets/data/creatures.yaml:17` | The arena has no threat: enemies orbit the player at a fixed radius, so movement and dash carry no tactics |
| GD-COMBAT-SYSTEMS-17 | low | deliberate | known-deferred | `src/game/combat.cpp:76` | Creature kinds are two named GameData fields, and `Tag` hard-codes Fox as the only enemy |
| GD-COMBAT-SYSTEMS-18 | low | unverifiable | new | `src/game/host.cpp:1830` | Afflicted enemies show no status: DoT state is readable only through tick numbers |
| GD-COMBAT-SYSTEMS-19 | low | unverifiable | new | `src/core/combat_rules.hpp:56` | Combat rules hold arena scene content and one ability's look-length as global hashed rules |
| GD-COMBAT-SYSTEMS-20 | low | unverifiable | new | `src/core/seal.hpp:41` | Seal lifetime is tied to the volley, and the 'gameplay seals fit the same record' claim is optimistic |
| GD-COMBAT-SYSTEMS-21 | low | partially-confirmed | new | `src/data/abilities.cpp:348` | A zero cooldown (allowed by data) lets two same-slot casts in one tick share a use ID and seed |
| GD-COMBAT-SYSTEMS-22 | low | unverifiable | new | `tests/unit/game/combat_tests.cpp:1` | No balance or time-to-kill tests or tooling for the shipped loadout |
| GD-ROADMAP-SCOPE-10 | low | deliberate | new | `docs/superpowers/specs/2026-11-10-phase6-chunk7-combat-scenes-design.md:82` | Dungeons and arenas will be built before combat has any challenge |
| GD-ROADMAP-SCOPE-11 | low | partially-confirmed | contradicts-recorded-decision | `docs/superpowers/specs/2026-11-10-phase6-design.md:72` | Minimum-spec GPU cost never measured while fragment-heavy world-map passes keep growing |
| GD-ROADMAP-SCOPE-12 | low | partially-confirmed | contradicts-recorded-decision | `src/game/host.cpp:288` | Delta saves need an identical base world across binaries, but nothing checks it |
| GD-ROADMAP-SCOPE-13 | low | confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-design.md:110` | Phase 6 master spec's chunk table is stale |
| GD-ROADMAP-SCOPE-14 | low | unverifiable | new | `docs/superpowers/specs/2026-11-24-river-heads-design.md:1` | Polish and engineering exceed proven fun (river heads, seals, scale) |
| GD-ROADMAP-SCOPE-15 | low | deliberate | contradicts-recorded-decision | `AGENTS.md:30` | Process overhead per change is very high |
| GD-ROADMAP-SCOPE-16 | low | unverifiable | new | `src/core/world_map.hpp:57` | Scale ambition isn't reconciled with population caps and memory budgets |
| GD-ROADMAP-SCOPE-17 | low | confirmed | known-deferred | `docs/design/0002-engine-architecture.md:29` | 0002 contradicts later decisions (modding non-goal, NPC actions as YAML) |
| GD-ROADMAP-SCOPE-18 | low | partially-confirmed | known-deferred | `docs/design/0002-engine-architecture.md:457` | Skeletal animation runtime (DragonBones, A12) unverified while the art direction depends on it |
| GD-ROADMAP-SCOPE-19 | low | confirmed | new | `/home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md:1` | Backlog and design gaps live in a 57 KB memory file outside the repo |
| GD-ROADMAP-SCOPE-20 | low | partially-confirmed | new | `README.md:61` | AGPL licence vs store integration and art contributions undecided |
| GD-ROADMAP-SCOPE-21 | low | partially-confirmed | new | `src/AGENTS.md:27` | Demo-arena rule and four dev scenes add per-feature upkeep that won't scale with content |
| GD-ROADMAP-SCOPE-22 | low | confirmed | known-deferred | `CHANGELOG.md:1` | No build has reached a player since v0.1.0 |
| GD-TOI-COVERAGE-10 | low | partially-confirmed | new | `src/game/npcs.hpp:64` | No ageing, death or birth. Population turnover will erode the saves-as-delta design |
| GD-TOI-COVERAGE-11 | low | partially-confirmed | new | `src/game/npcs.hpp:149` | Relationships are one opinion that only grows: no kinds, no negatives, no grudges, rivals or bonds |
| GD-TOI-COVERAGE-12 | low | partially-confirmed | new | `src/game/npcs.hpp:248` | The NPC tier machinery is dead in production and can't serve the planned towns |
| GD-TOI-COVERAGE-13 | low | partially-confirmed | new | `src/game/host.cpp:1571` | World time only runs on the world map; no player-driven time skip; the world sim depends on which scene is showing |
| GD-TOI-COVERAGE-14 | low | partially-confirmed | new | `src/game/host.cpp:1549` | Host is the only place systems are wired: no system registry, and every new world system grows Host |
| GD-TOI-COVERAGE-15 | low | partially-confirmed | new | `src/core/world_map.hpp:57` | The world population is capped at 100,000 individuals, with no aggregate tier in use, while maps may be 2-50x larger |
| GD-TOI-COVERAGE-16 | low | partially-confirmed | new | `src/core/names.hpp:60` | NPC names are a pure function of the ID, so families, clans, marriages and Dao names can't share or change a name |
| GD-TOI-COVERAGE-17 | low | partially-confirmed | new | `src/game/combat.cpp:435` | HitAmount, the stats hook, has no caster or target to read stats from |
| GD-TOI-COVERAGE-18 | low | unverifiable | new | `assets/data/world.yaml:309` | The world map has no difficulty gradient; danger sites have no level or realm |
| GD-TOI-COVERAGE-19 | low | confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-design.md:120` | Phase 6 roadmap table is stale: 5b3 and 5b-look still say 'To do' |
| GD-TOI-COVERAGE-20 | low | confirmed | new | `src/core/world_map.hpp:121` | DEFAULT_SHARE_PER_MILLE says it is the shipped share, but the data ships 58 |
| GD-TOI-COVERAGE-21 | low | partially-confirmed | new | `src/game/combat.cpp:44` | Combat reads presentation sprite metrics, but replays record only the game data hash |
| GD-WORLD-SIM-05 | low | partially-confirmed | new | `src/game/world_state.hpp:30` | GENERATOR_VERSION is bumped only by hand; generation changed without a bump |
| GD-WORLD-SIM-06 | low | deliberate | known-deferred | `src/game/host.cpp:978` | The world map gives the player no decisions: terrain, roads, rivers and danger serve only NPCs |
| GD-WORLD-SIM-07 | low | partially-confirmed | new | `src/core/calendar.hpp:14` | The time model has no years or seasons, no time-skip, and day length marked 'a setting for later' |
| GD-WORLD-SIM-08 | low | partially-confirmed | new | `src/game/npcs.hpp:5` | The NPC tier model (active, world, aggregate) is unused on the world map; every person is a fully simulated individual |
| GD-WORLD-SIM-09 | low | deliberate | known-deferred | `src/game/npcs.cpp:80` | Visits change nothing, and relationships are random edges from the tile partition |
| GD-WORLD-SIM-10 | low | confirmed | new | `src/game/host.cpp:1412` | Fog work scales with the whole map: the bitset is copied every publish and all targets rebuilt on each tile step |
| GD-WORLD-SIM-11 | low | unverifiable | new | `src/core/paths.cpp:196` | Each click runs an unbounded whole-map A* on the logic thread |
| GD-WORLD-SIM-12 | low | confirmed | new | `src/game/host.cpp:1298` | Publishing travellers evaluates every journey every frame before culling by view and sight |
| GD-WORLD-SIM-13 | low | unverifiable | new | `src/core/delta.hpp:10` | Saving world NPCs as a delta costs more than writing them whole, while doubling world memory and tying loads to regeneration |
| GD-WORLD-SIM-14 | low | partially-confirmed | new | `.claude/worktrees/shared-routes/docs/superpowers/specs/2026-11-24-shared-routes-design.md:108` | Shared-routes' place table assumes a fixed 'furthest reach', but reach has no bound |
| GD-WORLD-SIM-15 | low | confirmed | new | `assets/data/world.yaml:2` | Docs still call world.yaml the hand-maintained source, contrary to the procedural decision |
| GD-WORLD-SIM-16 | low | confirmed | new | `assets/data/world_look.yaml:186` | world_look.yaml names the place key `capital`, so any map without it is refused at start |
| GD-WORLD-SIM-17 | low | partially-confirmed | new | `src/game/npcs.hpp:36` | npcs.hpp docs describe world-map behaviour that doesn't exist |
| GD-WORLD-SIM-18 | low | partially-confirmed | new | `src/game/scenes/world.hpp:20` | WORLD_BUSIEST and WORLD_PORTAL_OFFSET are fixed to the test map |
| GD-WORLD-SIM-19 | low | partially-confirmed | new | `src/core/world_map.cpp:513` | Dense per-tile layers and quadratic place checks will not scale to large generated maps |
| GD-WORLD-SIM-20 | low | unverifiable | new | `src/core/world_map.cpp:636` | Every band reaching every city is guaranteed only by a shipped-map test |
| GD-WORLD-SIM-21 | low | partially-confirmed | new | `src/game/travel.cpp:163` | An NPC due to leave late in the month sits the whole month out before it leaves |
| GD-WORLD-SIM-22 | low | confirmed | new | `src/game/travel.cpp:176` | Save checks accept whole journeys and player routes whose steps aren't adjacent or walkable |
| GFX-COMBAT-DRAW-02 | low | partially-confirmed | new | `src/gfx/seal_draw.hpp:362` | SealParticleAt is a production-unused C++ copy of the HLSL particle placement; tests check the copy, not the shader |
| GFX-COMBAT-DRAW-03 | low | partially-confirmed | new | `src/gfx/seal_draw.cpp:746` | Overhead (or mixed-plane) satellites stack lifts: their shadow is laid at the parent's height, not on the ground |
| GFX-COMBAT-DRAW-04 | low | partially-confirmed | new | `src/gfx/seal_draw.cpp:433` | SealLight mixes world units and seal-size fractions: satellites and small seals get full-size pillars and rises, and shards can strobe |
| GFX-COMBAT-DRAW-05 | low | confirmed | new | `src/gfx/volley_draw.cpp:308` | Health bars and bursts are placed in the overlay before the swords and are uncapped, so a crowded fight can drop the player's swords |
| GFX-COMBAT-DRAW-06 | low | confirmed | new | `src/gfx/cloud_draw.cpp:415` | Clouds' zoom cross-fade draws both levels at partial opacity with over-blending, so coverage drops to about 75% and zoom 0.71 holds it |
| GFX-COMBAT-DRAW-07 | low | confirmed | new | `src/gfx/stamp_draw.cpp:316` | DrawStamps checks the span before widening it: an off-map span makes the reserve size wrap round and calls reserve(~4e9 × rows) |
| GFX-COMBAT-DRAW-08 | low | confirmed | new | `src/gfx/stamp_draw.cpp:181` | Stamp layout scans every place on the map and checks every stamp against every painting, on every relayout (every frame while zooming) |
| GFX-COMBAT-DRAW-09 | low | partially-confirmed | new | `src/gfx/cloud_draw.cpp:389` | Clouds are laid out, clamped and sorted from scratch every frame, though only the bob changes between frames |
| GFX-COMBAT-DRAW-10 | low | partially-confirmed | new | `tests/unit/gfx/seal_draw_tests.cpp:182` | Seal tests miss several paths: still (spin_scale 0) drawing, tinySeals, emblem/flourish packing, upright seals drawn end to end, depth-2 satellites |
| GFX-COMBAT-DRAW-11 | low | partially-confirmed | new | `src/gfx/volley_draw.cpp:207` | DrawHealthBars adds a zero-length fill instance at zero health (DrawHudBars skips it) and divides by thickness unchecked |
| GFX-COMBAT-DRAW-12 | low | confirmed | new | `src/gfx/volley_draw.cpp:181` | Health bars silently disappear whenever glyph pages fail, because barsShown is filled only in PlaceTexts |
| GFX-FEATURES-02 | low | confirmed | new | `src/gfx/internal/features/sprites.cpp:31` | Shader constants and cbuffer layouts are mirrored by hand; nothing checks them against the HLSL |
| GFX-FEATURES-03 | low | partially-confirmed | new | `src/gfx/internal/features/line_ribbons.cpp:73` | Fixed 8,192-quad river and road buffers never grow; on large procedural maps rivers and roads silently disappear, and every quad is drawn every frame |
| GFX-FEATURES-04 | low | confirmed | new | `src/gfx/internal/features/line_ribbons.cpp:74` | Overrunning maxQuads is guarded only by PN_ASSERT; in release, CopyToBuffer would write past the mapped transfer buffer |
| GFX-FEATURES-05 | low | confirmed | new | `src/gfx/internal/features/sprites.cpp:358` | Each of six SpriteRenderers builds its own identical sprite pipelines (12 pipelines, 24 shader objects) |
| GFX-FEATURES-06 | low | partially-confirmed | new | `src/gfx/internal/features/sprites.hpp:113` | The held-once-submitted logic is written out five times although gfx::UploadedSnapshot already does it |
| GFX-FEATURES-07 | low | partially-confirmed | new | `tests/unit/gfx/meshes_tests.cpp:1` | No tests reach internal/features; failure, cancel, resize and overflow paths are untested |
| GFX-FEATURES-08 | low | unverifiable | new | `src/gfx/internal/features/sprites.cpp:309` | Fog and ground textures use one texel per tile; maps wider than the GPU's texture limit lose the ground, and fog failure reveals the whole map |
| GFX-FEATURES-09 | low | confirmed | new | `src/gfx/internal/features/ground.cpp:146` | GroundTexels runs over the whole map on the main thread at every new version, look reloads and failed submits included |
| GFX-FEATURES-10 | low | confirmed | new | `src/gfx/internal/features/meshes.cpp:18` | Pipeline, blend, sampler, transfer-buffer and texture creation are duplicated across every feature; MeshRenderer and RibbonRenderer are the same class |
| GFX-FEATURES-11 | low | partially-confirmed | new | `src/gfx/internal/features/seals.cpp:316` | Seal light is drawn or dropped inconsistently with the quads; the quad-overflow path is unreachable |
| GFX-FEATURES-12 | low | partially-confirmed | new | `src/gfx/internal/features/seals.cpp:238` | After a failed SealRenderer::Reserve, the pack buffers can be left null yet counted as fitting an empty pack |
| GFX-FEATURES-13 | low | unverifiable | new | `src/gfx/internal/features/seals.cpp:364` | Seals ignore the fog; upright and overhead seals are drawn after the clouds |
| GFX-FEATURES-14 | low | confirmed | new | `src/gfx/internal/features/ground.cpp:33` | Some uniform structs have no size static_assert, unlike the others |
| GFX-RENDERER-02 | low | partially-confirmed | new | `src/core/logic.cpp:24` | Snapshot identity (epoch, wallTick) is reused by waiting bursts, so changed instances are not uploaded and new batches draw against a stale buffer |
| GFX-RENDERER-03 | low | partially-confirmed | new | `src/gfx/renderer.cpp:1319` | No escalation or recovery on device loss or persistent frame failure: the game runs on behind a frozen or black window |
| GFX-RENDERER-04 | low | confirmed | new | `src/gfx/renderer.cpp:1567` | A frame acquired before a failing LoadScene is never submitted; the renderer is destroyed holding an acquired swapchain image |
| GFX-RENDERER-05 | low | partially-confirmed | new | `tests/unit/gfx/uploads_tests.cpp:10` | The live frame path (Acquire/Draw/Frame/Submitted) has no test; all GPU tests go through Capture() |
| GFX-RENDERER-06 | low | unverifiable | new | `src/gfx/renderer.cpp:1519` | Scene load holds every decoded sprite in memory before the first upload |
| GFX-RENDERER-07 | low | deliberate | new | `src/gfx/internal/resources/sprite_array.cpp:286` | Whole atlas allocated in VRAM at start-up, never evicted; every scene load regenerates the mips of every page |
| GFX-RENDERER-08 | low | confirmed | new | `src/gfx/renderer.cpp:1158` | A failed scene-target fit presents an undrawn swapchain image |
| GFX-RENDERER-09 | low | confirmed | new | `src/gfx/renderer.cpp:467` | Texts, labels and the HUD are laid out every frame while the scene isn't held, though only the backdrop draws |
| GFX-RENDERER-10 | low | partially-confirmed | new | `src/gfx/renderer.cpp:1091` | Glyph uploads ignore the D3D12 alignment the sprite upload honours, and create a new transfer buffer every frame |
| GFX-RENDERER-11 | low | partially-confirmed | new | `src/gfx/renderer.cpp:1689` | Capture assumes a 4-byte RGBA/BGRA swapchain format without checking it |
| GFX-RENDERER-12 | low | confirmed | new | `src/gfx/internal/resources/shaders.cpp:10` | CreateShader falls back to MSL without checking the device supports it or that MSL bytes exist |
| GFX-RENDERER-13 | low | unverifiable | new | `src/gfx/renderer.cpp:1591` | A visible frame with a null swapchain still runs the label pass at a 0×0 target |
| GFX-TEXT-01 | low | confirmed | new | `src/gfx/text_layout.cpp:343` | WarnMissing loops forever on a text containing an embedded NUL |
| GFX-TEXT-02 | low | partially-confirmed | new | `src/gfx/renderer.cpp:1001` | Stack labels' real-count text is laid out without a budget at draw time, bypassing LAYOUT_BUDGET_NS |
| GFX-TEXT-03 | low | confirmed | new | `src/gfx/text_layout.cpp:145` | Glyph rasterisation reads every texel through SDL_ReadSurfacePixel, on the main thread at start-up |
