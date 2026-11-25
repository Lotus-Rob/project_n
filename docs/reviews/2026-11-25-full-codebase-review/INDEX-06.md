# Finding index 6/11 (standing findings, most severe first)

The ID prefix is the unit file: e.g. CORE-WORLD-03 is in core-world.md (or its -partN files).

| ID | Severity | Verdict | Status | Where | Finding |
|---|---|---|---|---|---|
| GFX-TEXT-04 | low | confirmed | new | `src/gfx/hud_draw.hpp:18` | HUD text is a fixed 18 target pixels with no DPI or resolution scaling |
| GFX-TEXT-05 | low | confirmed | new | `src/gfx/text_place.cpp:193` | Two cache keys pack language/direction into one bit with no static_assert |
| GFX-TEXT-06 | low | partially-confirmed | new | `src/gfx/labels.cpp:77` | LabelPass assumes unique entities but neither asserts nor handles duplicates (entity 0 is legal in TextItem) |
| GFX-TEXT-07 | low | confirmed | new | `src/gfx/labels.cpp:35` | NaN anchors reach a float-to-size_t cast (UB) in the label grid |
| GFX-TEXT-08 | low | partially-confirmed | new | `src/gfx/text_place.cpp:143` | Incomplete number and stack-count layouts are drawn with gaps |
| GFX-TEXT-09 | low | confirmed | new | `src/gfx/text_layout.cpp:396` | A glyph whose rasterisation fails is marked Missing forever |
| GFX-TEXT-10 | low | confirmed | new | `src/gfx/text_layout.cpp:59` | Placeholder filling is implemented three times with different meanings |
| GFX-TEXT-11 | low | confirmed | new | `src/gfx/distance_field.cpp:112` | DistanceField indexes the rgba span with no size check |
| GFX-TEXT-12 | low | confirmed | new | `tests/unit/gfx/text_layout_tests.cpp:507` | The layout eviction test is masked by its own probes |
| GFX-TEXT-13 | low | partially-confirmed | new | `tests/unit/gfx/text_layout_tests.cpp:440` | Missing tests for text failure paths and label invariants |
| GFX-WORLD-DRAW-02 | low | partially-confirmed | new | `src/gfx/ground_field.cpp:19` | GroundTexels is O(tiles x 121) on the main thread with no early-out |
| GFX-WORLD-DRAW-03 | low | confirmed | new | `src/gfx/terrain_draw.cpp:58` | Bridge orientation scans every road curve point per bridge tile |
| GFX-WORLD-DRAW-04 | low | confirmed | new | `src/gfx/terrain_draw.cpp:95` | AddMarkers walks every place and string-compares sort per redraw instead of the tiles in view |
| GFX-WORLD-DRAW-05 | low | partially-confirmed | new | `src/gfx/terrain_draw.cpp:113` | DrawTerrain does not enforce MAX_TERRAIN_INSTANCES; overflow silently drops markers last |
| GFX-WORLD-DRAW-06 | low | confirmed | new | `src/gfx/route_draw.cpp:23` | DrawRoute's cap counts into.instances.size() absolutely, contradicting 'appends to into' |
| GFX-WORLD-DRAW-07 | low | confirmed | new | `src/gfx/renderer.cpp:677` | The route is re-laid and re-uploaded every frame even when unchanged |
| GFX-WORLD-DRAW-08 | low | partially-confirmed | new | `src/gfx/river_draw.cpp:79` | River joins are O(rivers x all points) per end |
| GFX-WORLD-DRAW-09 | low | confirmed | new | `src/gfx/sprite_block.cpp:24` | WriteBlock and BlockOf have unchecked preconditions (zero-size sprites, buffer sizes) |
| GFX-WORLD-DRAW-10 | low | partially-confirmed | new | `src/gfx/fog.cpp:24` | Fog texels missing from `values` read as fully explored |
| GFX-WORLD-DRAW-11 | low | partially-confirmed | new | `src/gfx/tile_hash.hpp:13` | tile_hash.hpp comments cite history; TERRAIN_SEED is world-independent |
| GFX-WORLD-DRAW-12 | low | partially-confirmed | new | `tests/unit/gfx/route_draw_tests.cpp:126` | Tests do not cover scale behaviour or several defects above |
| PLATFORM-02 | low | confirmed | new | `src/platform/window.cpp:108` | Multiple gamepads merge into one input stream with no device identity |
| PLATFORM-03 | low | partially-confirmed | new | `src/platform/window.cpp:342` | visible=false (occluded/hidden) while focused does not pause the game but makes Poll block |
| PLATFORM-04 | low | confirmed | new | `src/platform/window.cpp:314` | hiddenWaitNs/1e6 cast to i32 can wrap negative, meaning wait forever |
| PLATFORM-05 | low | confirmed | new | `src/platform/window.cpp:274` | No SDL_SetAppMetadata / app id: window class, Wayland app_id and pref path identity are SDL defaults |
| PLATFORM-06 | low | partially-confirmed | new | `src/platform/window.cpp:289` | No minimum window size; resizable window can reach 0x0 or tiny sizes |
| PLATFORM-07 | low | unverifiable | new | `src/platform/clock.cpp:16` | Clock zero is not stable across SDL_Quit/SDL_Init |
| PLATFORM-08 | low | partially-confirmed | new | `src/platform/window.cpp:226` | Window is not guarded against more than one live instance; SDL_Quit in Impl destructor |
| PLATFORM-09 | low | deliberate | known-deferred | `src/platform/window.cpp:182` | No text input / IME support and no modifier or trigger inputs |
| PLATFORM-10 | low | confirmed | new | `src/platform/window.cpp:119` | Alt state sampled at poll time, not at event time |
| PLATFORM-11 | low | partially-confirmed | new | `src/platform/window.cpp:101` | Reconnect: a pad present at start or pad events on a disconnected ID |
| PLATFORM-12 | low | partially-confirmed | new | `src/platform/paths.cpp:19` | Paths: user data in roaming AppData, assets folder never checked, no override for tests or portable mode |
| PLATFORM-13 | low | partially-confirmed | new | `tests/unit/platform/paths_tests.cpp:15` | Paths tests are thin: UserDataDirectory untested, Windows ARM/macOS bundle and UTF-8 not covered |
| SHADERS-01 | low | partially-confirmed | new | `tools/project_n.py:1733` | Shader layout/binding verification covers SPIR-V only; DXIL registers and the MSL/DXIL render paths are never checked or exercised |
| SHADERS-02 | low | confirmed | new | `src/gfx/generated/shaders.hpp:12` | HLSL struct layouts are tied to C++ only by comments; no generated or reflected size/offset check |
| SHADERS-03 | low | confirmed | new | `tools/project_n.py:1654` | Shader manifest ignores the compile flags, so changing DXC/SPIRV-Cross options leaves stale binaries that lint accepts |
| SHADERS-04 | low | partially-confirmed | new | `shaders/ground.frag.hlsl:242` | World-map ground recomputes heavy procedural noise per pixel every frame, plus two more full-view passes that are often no-ops |
| SHADERS-05 | low | confirmed | new | `shaders/sprite.vert.hlsl:179` | Glow age loses its sub-tick fraction from 2^23 ticks (~39 h), earlier than the comment's 'three days' |
| SHADERS-06 | low | confirmed | new | `shaders/mesh.frag.hlsl:61` | Text ink alpha is uploaded but ignored; premultiplied ink rgb is treated as opaque |
| SHADERS-07 | low | confirmed | new | `shaders/seal_light.frag.hlsl:26` | In-body comments that state what the next lines do, against shaders/AGENTS.md and the root comment rule |
| SHADERS-08 | low | unverifiable | new | `shaders/seal.vert.hlsl:4` | Vertex input semantics are tied to SDL attribute locations only by convention |
| SHADERS-09 | low | confirmed | new | `src/gfx/internal/resources/shaders.cpp:10` | CreateShader falls back to MSL without checking the device supports it |
| SPECS-BUILT-A-01 | low | confirmed | new | `docs/superpowers/specs/2026-11-12-dev-loop-speedups-design.md:275` | dev-loop-speedups Goal 6 (glow chunk's time per task) was never recorded |
| SPECS-BUILT-A-02 | low | partially-confirmed | new | `docs/superpowers/specs/2026-11-15-phase6-chunk5b2-moving-design.md:340` | 5b2 breaks several of 5a's measure budgets but its Outcome never says so |
| SPECS-BUILT-A-03 | low | confirmed | new | `docs/superpowers/specs/2026-11-15-phase6-chunk5b2-moving-design.md:300` | 5b2's measure run uses --travel-share 200, so it is not the shipped game's cost and not like-for-like with main |
| SPECS-BUILT-A-04 | low | partially-confirmed | known-deferred | `docs/superpowers/specs/2026-11-15-phase6-chunk5b1-map-design.md:418` | World shimmer budget replaced by a looser guard; acceptance check 0.02 still outstanding |
| SPECS-BUILT-A-05 | low | partially-confirmed | new | `src/game/travel.cpp:308` | Cost-model hazard: Travel::Finish scans every live journey and merges the whole journal table each call |
| SPECS-BUILT-A-06 | low | unverifiable | known-deferred | `docs/superpowers/specs/2026-11-16-phase6-chunk5c1-world-kept-delta-saves-design.md:316` | 5c1 aim 'saving no slower than today's ~16 ms' was missed and not revisited |
| SPECS-BUILT-A-07 | low | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-superpowers-workflow-design.md:187` | Open question about which checkout's clangd and compile database the LSP uses in a worktree was never closed |
| SPECS-BUILT-A-08 | low | partially-confirmed | new | `docs/superpowers/plans/2026-11-15-phase6-chunk5b2.md:1` | All 17 plans lack a completion marker; checkboxes are all unchecked and 12 have no Status line |
| SPECS-BUILT-A-09 | low | confirmed | new | `src/AGENTS.md:4` | src/AGENTS.md has single lines of 5 to 15 KB, breaking the repo's 'one line per rule' standard |
| SPECS-BUILT-B-01 | low | confirmed | new | `docs/superpowers/specs/2026-11-20-phase6-chunk5b3-fog-design.md:295` | Fog's main-thread work rise is recorded as unexplained and never resolved |
| SPECS-BUILT-B-02 | low | partially-confirmed | new | `src/app/fog_fade.cpp:19` | Fog easing and targets are O(total tiles) on the main thread, and the texture is uploaded whole |
| SPECS-BUILT-B-03 | low | unverifiable | new | `docs/superpowers/specs/2026-11-21-phase6-chunk5b-look-terrain-design.md:312` | Terrain ground shader cost measured only on the dev GPU; minimum-spec GPU unmeasured |
| SPECS-BUILT-B-04 | low | deliberate | known-deferred | `docs/superpowers/specs/2026-11-21-phase6-chunk5b-look-terrain-design.md:308` | measure counts SubmitFrame as main-thread work, so it blames the CPU for GPU time |
| SPECS-BUILT-B-05 | low | deliberate | new | `docs/superpowers/specs/2026-11-21-phase6-chunk5b-look-terrain-design.md:340` | Walker labels hide under cloud shade, so measure's labels segment no longer exercises what it did |
| SPECS-BUILT-B-06 | low | confirmed | new | `docs/superpowers/specs/2026-11-19-relationships-bounded-design.md:165` | Relationship cell-locality claim is unmeasured and may mean 30% of NPCs never form relationships |
| SPECS-BUILT-B-07 | low | confirmed | contradicts-recorded-decision | `tests/unit/game/shipped_world_tests.cpp:319` | Traffic-count bounds moved when rivers moved: the demo-load tests are coupled to hand content |
| SPECS-BUILT-B-08 | low | partially-confirmed | new | `tools/project_n.py:895` | Reference timeout of 80 s rests on one 7.4 s sample from a 32-core container |
| SPECS-BUILT-B-09 | low | deliberate | known-deferred | `tools/project_n.py:995` | Reference runner's pass/fail leaves picture drift only as a printed note |
| SPECS-BUILT-B-10 | low | deliberate | known-deferred | `docs/superpowers/specs/2026-11-22-player-walk-and-camera-design.md:23` | Held movement has no collision while click routes refuse impassable tiles, so the two walks can disagree |
| SPECS-BUILT-B-11 | low | partially-confirmed | new | `src/core/paths.cpp:184` | FindRoute is dead in production code and survives for tests only |
| SPECS-BUILT-B-12 | low | deliberate | known-deferred | `docs/superpowers/specs/2026-11-24-river-heads-design.md:218` | A river's head outside river 0's band (delta, river 10) is a recorded backlog item, and the new test does not cover it |
| SPECS-BUILT-B-13 | low | deliberate | known-deferred | `docs/superpowers/specs/2026-11-23-phase6-chunk5b-look-travellers-design.md:420` | Walk-table cache is cleared at every month step, so each step triggers a rebuild burst under a lock |
| SPECS-ROADMAP-07 | low | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk6-towns-design.md:111` | Chunks 6 and 7 require byte-identical generation on every preset and pin hashes, against the within-one-binary determinism decision |
| SPECS-ROADMAP-08 | low | unverifiable | new | `docs/superpowers/specs/2026-11-10-phase6-chunk7-combat-scenes-design.md:101` | Chunk 7's spawned and cleared sites collide with the immutable WorldMap and the once-per-world route graph |
| SPECS-ROADMAP-09 | low | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk6-towns-design.md:128` | Chunk 6's 'most people live in the region' contradicts today's 70% on the city tile |
| SPECS-ROADMAP-10 | low | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk8-ui-design.md:152` | The gamepad Back button gets three conflicting jobs across code and chunks 8 and 11 |
| SPECS-ROADMAP-11 | low | deliberate | contradicts-recorded-decision | `docs/superpowers/specs/2026-11-10-phase6-design.md:146` | Stand-in screens built in 5c2, 6 and 7 are rebuilt in 8 and 10: throwaway work from the chunk order |
| SPECS-ROADMAP-12 | low | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk9-view-models-design.md:56` | Chunks 6, 7 and 9 encode game systems ahead of the game-design brainstorm that is meant to define them |
| SPECS-ROADMAP-13 | low | partially-confirmed | new | `.claude/worktrees/shared-routes/docs/superpowers/specs/2026-11-24-shared-routes-design.md:98` | Shared-routes: no path smoothing, and a quality bar that can't see zigzags or short-trip detours |
| SPECS-ROADMAP-14 | low | confirmed | new | `.claude/worktrees/shared-routes/docs/superpowers/plans/2026-11-25-shared-routes-stage1.md:584` | Stage 1 plan: A* end condition with several candidate end nodes is underspecified |
| SPECS-ROADMAP-15 | low | partially-confirmed | new | `.claude/worktrees/shared-routes/docs/superpowers/plans/2026-11-25-shared-routes-stage1.md:35` | Gate rule can't guarantee 'roads always have their own' gate |
| SPECS-ROADMAP-16 | low | partially-confirmed | new | `.claude/worktrees/shared-routes/docs/superpowers/specs/2026-11-24-shared-routes-design.md:144` | Shared-routes budgets measured on 4 workers while the phase bar pins 2, and the game builds the graph with no workers |
| SPECS-ROADMAP-17 | low | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk10-screens-design.md:118` | Chunk 10 relies on features that don't exist yet: blur passes, progress during generation, the save picture and play time |
| SPECS-ROADMAP-18 | low | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk11-settings-design.md:154` | Chunk 11's live previews need a second, synthetic scene through a single-scene renderer |
| SPECS-ROADMAP-19 | low | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk9-view-models-design.md:28` | Chunk 9 sends subscriptions as unrecorded actions though 5a built a latest-value side channel for exactly this |
| SPECS-ROADMAP-20 | low | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk8-ui-design.md:157` | Chunk 8 HUD scope ignores the world map's date line, the HUD's one clickable area |
| SPECS-ROADMAP-21 | low | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk11-settings-design.md:139` | Chunk 11's typed search box contradicts the phase non-goal 'no text input' |
| SPECS-ROADMAP-22 | low | unverifiable | new | `docs/superpowers/specs/2026-11-10-phase6-chunk7-combat-scenes-design.md:115` | Chunk 7's arena swaps and chunk 6's per-city towns don't fit the stateless, compile-time scene model |
| SPECS-ROADMAP-23 | low | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk6-towns-design.md:185` | Chunk 6's debug 'regenerate the city from a new seed' changes simulated state without being recorded |
| SPECS-ROADMAP-24 | low | confirmed | new | `/home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md:23` | Backlog line still places bindings and rebinding in 'chunk 7' |
| TESTS-CORE-02 | low | partially-confirmed | new | `tests/unit/core/volley_tests.cpp:861` | "same volley bit for bit" test compares a volley with a copy of itself |
| TESTS-CORE-03 | low | unverifiable | new | `tests/unit/core/world_map_tests.cpp:205` | WorldMap::Build shape/limit refusals untested (size, MAX_TILES, tile size, kind count, grid kind, place count) |
| TESTS-CORE-04 | low | confirmed | new | `tests/unit/core/draw_list_tests.cpp:150` | DrawList publish never tested with more than one worker or part |
| TESTS-CORE-05 | low | unverifiable | new | `tests/unit/core/archive_tests.cpp:140` | Hasher/Names/GameData hashes tested only by relations; no pinned values for a format that replays depend on |
| TESTS-CORE-06 | low | unverifiable | new | `tests/unit/core/archive_tests.cpp:100` | Archive Reader hardening cases missing: huge vector size, invalid enum, f32 NaN/-0 round trip |
| TESTS-CORE-07 | low | deliberate | known-deferred | `tests/unit/core/replay_tests.cpp:262` | Replay forged-count tests do not exercise the reserve amplification boundary |
| TESTS-CORE-08 | low | partially-confirmed | new | `tests/unit/core/saves_tests.cpp:13` | Test infrastructure duplicated: temp-folder RAII class (x3) and save magic (x3), fixed shared temp names |
| TESTS-CORE-09 | low | partially-confirmed | new | `tests/unit/core/names_tests.cpp:51` | Statistical tests with tiny samples and unpinned outputs (names, weights) |
| TESTS-CORE-10 | low | partially-confirmed | new | `tests/unit/core/jobs_tests.cpp:93` | Sweep-style and unbounded-spin tests cost time and can hang |
| TESTS-CORE-11 | low | partially-confirmed | new | `tests/unit/core/glow_tests.cpp:140` | Core Valid() predicates and Vec2/view_limits/assert have no direct core tests |
| TESTS-GAME-05 | low | confirmed | new | `tests/support/manifest.hpp:19` | TestManifest() and the Shipped*() fixtures re-parse YAML on every call |
| TESTS-GAME-06 | low | partially-confirmed | new | `tests/unit/game/save_tests.cpp:83` | Shipped data used where small data suffices in save, exactness and scene tests |
| TESTS-GAME-07 | low | partially-confirmed | new | `tests/unit/game/host_tests.cpp:1` | host_tests.cpp is a 3,709-line, 135-case monolith (combat and save tests are similar) |
| TESTS-GAME-08 | low | partially-confirmed | new | `tests/support/saves.hpp:232` | ArenaSave takes 14 positional parameters and is called with repeated std::nullopt |
| TESTS-GAME-09 | low | partially-confirmed | new | `tests/unit/game/replay_tests.cpp:144` | Fixed temp folder and file names collide across concurrent runs |
| TESTS-GAME-10 | low | partially-confirmed | new | `tests/unit/game/shipped_world_tests.cpp:319` | Shipped-data balance and demo-load values asserted in unit tests |
| TESTS-GAME-11 | low | unverifiable | new | `tests/unit/game/shipped_world_tests.cpp:628` | MeasureLongSave re-implements Travel::SaveDelta's classification and derives 'rest' by subtraction |
| TESTS-GAME-12 | low | partially-confirmed | new | `tests/unit/game/npcs_tests.cpp:558` | Tautological determinism test for StartingPower |
| TESTS-GAME-13 | low | partially-confirmed | new | `tests/unit/game/npcs_tests.cpp:159` | Weak assertions that only prove a load or a run didn't fail |
| TESTS-GAME-14 | low | unverifiable | new | `tests/unit/game/npcs_tests.cpp:124` | Missing negative and edge cases for NPC activation, folding and relationship loading |
| TESTS-GAME-15 | low | partially-confirmed | new | `tests/unit/game/combat_tests.cpp:1646` | Test name does not match its content and a duplicate case follows it |
| TESTS-GAME-16 | low | partially-confirmed | new | `tests/unit/game/combat_tests.cpp:17` | Shipped data values hard-coded across tests with only partial linkage |
| TESTS-GAME-17 | low | unverifiable | new | `tests/unit/game/combat_tests.cpp:903` | Volley tests run 1,000-sword volleys with per-tick publish |
| TESTS-GAME-18 | low | unverifiable | new | `tests/unit/game/host_tests.cpp:456` | Slow stress-arena cases use up to 100,000 instances and 12 months of 50,000 NPCs |
| TESTS-GAME-19 | low | deliberate | contradicts-recorded-decision | `tests/unit/game/save_exactness_tests.cpp:49` | Determinism tests only compare two runs in the same binary, with no change detector |
| TESTS-GAME-20 | low | partially-confirmed | new | `tests/support/road_curves.hpp:18` | OffCurve divides by zero on a degenerate segment and returns f32 max on an empty curve |
| TESTS-REST-01 | low | partially-confirmed | new | `tests/reference/reference_tests.cpp:58` | Reference compare allows 92 pixels of difference beyond tolerance, contradicting the byte-identical claim |
| TESTS-REST-02 | low | confirmed | new | `tests/unit/gfx/image_tests.cpp:35` | Fixed temp paths shared between concurrent test runs |
