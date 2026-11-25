# Finding index 4/11 (standing findings, most severe first)

The ID prefix is the unit file: e.g. CORE-WORLD-03 is in core-world.md (or its -partN files).

| ID | Severity | Verdict | Status | Where | Finding |
|---|---|---|---|---|---|
| GAME-SCENES-01 | low | confirmed | new | `src/game/scenes/arena_layout.cpp:115` | Orbiting entities spawn at their tick-0 positions, so every scene entry after start streaks them across the screen for a tick |
| GAME-SCENES-02 | low | partially-confirmed | new | `src/game/scenes/arena_layout.cpp:65` | Two arena NPCs live and wander inside showcase seals, contradicting 'nothing stands on it' and the showcase rule |
| GAME-SCENES-03 | low | partially-confirmed | new | `src/game/scenes.cpp:90` | Nothing checks that each scene's drawn sprites are in its art/scenes.toml list; CheckScenes only checks two creature sprites |
| GAME-SCENES-04 | low | confirmed | new | `src/game/scenes/world.cpp:36` | World portal is a fixed offset from the start city, never checked against the map's bounds or terrain |
| GAME-SCENES-05 | low | partially-confirmed | new | `src/game/scenes.hpp:49` | Scene model is a fixed list of singletons keyed by index; it can't express parameterised scenes or portal destinations |
| GAME-SCENES-06 | low | confirmed | new | `src/game/scenes/arena.cpp:84` | Aura range wraps to 0 when max - min = u32 max, so the aura always deals its minimum |
| GAME-SCENES-07 | low | deliberate | new | `src/game/scenes/world.cpp:50` | Host's default data makes F2 and portals reach the world scene without a map (assert, or null dereference with assertions off) |
| GAME-SCENES-08 | low | partially-confirmed | new | `src/game/scenes/stress.cpp:103` | Stress profiling scene skips the demo glows and showcase seals, so their cost is never measured |
| GAME-SCENES-09 | low | confirmed | new | `src/game/scenes/cells.cpp:15` | Cells scene draws the whole painted ground under a grid that covers it completely |
| GAME-SCENES-10 | low | partially-confirmed | new | `src/game/scene.hpp:62` | SceneTraits is a set of correlated flags whose invariants are not enforced at compile time |
| GAME-SCENES-11 | low | confirmed | new | `src/game/scenes/arena.cpp:29` | Ability-use source numbers are spread over three files, with no registry |
| GAME-SCENES-12 | low | confirmed | new | `src/game/scenes.hpp:49` | Docs drifted from code in the scene API |
| GAME-SCENES-13 | low | partially-confirmed | new | `src/AGENTS.md:27` | src/AGENTS.md describes scenes and the host in a single line thousands of words long |
| GAME-SCENES-14 | low | partially-confirmed | new | `src/game/scenes/arena.cpp:41` | Showcase test passes for a showcase key that seals.yaml lacks |
| GAME-TRAVEL-NPCS-01 | low | partially-confirmed | new | `src/game/travel.cpp:396` | Paths warms an unbounded whole-map distance map for every city and every band profile: memory and time grow as cities × profiles × tiles |
| GAME-TRAVEL-NPCS-02 | low | partially-confirmed | new | `src/game/travel.cpp:243` | ChooseDestination scans every place twice for every traveller, with a distance-map lookup each time |
| GAME-TRAVEL-NPCS-03 | low | partially-confirmed | new | `src/game/travel.cpp:316` | Removing an NPC has no single entry point; destroying one without Travel::Forget makes the game's next save unloadable |
| GAME-TRAVEL-NPCS-04 | low | partially-confirmed | new | `src/game/npcs.hpp:5` | Group / Materialise / Fold and the three-tier model are dead in production, and the header documents tiers that don't exist |
| GAME-TRAVEL-NPCS-05 | low | confirmed | new | `src/game/npcs.cpp:133` | CellAt converts unbounded floats to u32: UB for a finite but huge Body position from a save, and rows are never clamped |
| GAME-TRAVEL-NPCS-06 | low | confirmed | new | `src/game/travel.cpp:396` | Warm(WALKING) is always built even when no band uses WALKING |
| GAME-TRAVEL-NPCS-07 | low | partially-confirmed | new | `src/game/world_state.hpp:28` | GENERATOR_VERSION is bumped by hand, with no golden test, though travel code is part of what generation makes |
| GAME-TRAVEL-NPCS-08 | low | confirmed | new | `src/game/travel.cpp:615` | Journals are loaded for any entity ID, not only NPCs; relationships likewise accept Group IDs |
| GAME-TRAVEL-NPCS-09 | low | confirmed | new | `src/game/travel.cpp:178` | LegSound checks the shape with `<= LegShape::TileRoute`, which silently breaks when a shape is added |
| GAME-TRAVEL-NPCS-10 | low | partially-confirmed | new | `src/game/travel.hpp:254` | Journals stored as 100k separately allocated vectors, saved and hashed whole |
| GAME-TRAVEL-NPCS-11 | low | partially-confirmed | new | `src/game/travel.cpp:248` | ChooseDestination's thread-safety relies on a comment: Paths::To is non-const and builds lazily |
| GAME-TRAVEL-NPCS-12 | low | partially-confirmed | new | `src/game/travel.cpp:222` | Pull's integer scale rounds small or distant towns to zero weight on large maps |
| GAME-TRAVEL-NPCS-13 | low | confirmed | new | `tests/unit/game/travel_tests.cpp:1` | Tests miss CellAt bounds, journal merging across interleaved NPCs, Travel::Seed itself, and the generated world's identity |
| GAP1-CLOCK-DISCIPLINE-PAUSE-03 | low | partially-confirmed | known-deferred | `/home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md:99` | Backlog audit item 12 is inaccurate and incomplete about what animates and uploads while paused |
| GAP1-CLOCK-DISCIPLINE-PAUSE-04 | low | confirmed | new | `src/gfx/camera.hpp:53` | Comments claim the render clock holds while the game is paused; under the world pause it does not |
| GAP1-CLOCK-DISCIPLINE-PAUSE-05 | low | partially-confirmed | new | `src/app/app.cpp:878` | Fog easing, and the cloud drift that follows it, keeps animating through both pauses on the frame clock |
| GAP1-CLOCK-DISCIPLINE-PAUSE-06 | low | partially-confirmed | new | `src/gfx/renderer.cpp:465` | Sprite instances are re-uploaded every tick while paused, because the upload key is the wall tick |
| GAP1-CLOCK-DISCIPLINE-PAUSE-07 | low | partially-confirmed | new | `src/app/app.cpp:727` | The target cursor keeps stepping while the window is unfocused when the left stick was held at focus loss |
| GAP1-CLOCK-DISCIPLINE-PAUSE-08 | low | partially-confirmed | new | `src/game/host.cpp:1747` | DemoPulse counts its pulse from tick 0, so the shader's f32 limit depends on the session's tick count, not the pulse's age |
| GAP1-CLOCK-DISCIPLINE-PAUSE-09 | low | partially-confirmed | new | `src/app/app.cpp:520` | Profile helpers keep firing on wall time while paused, and only the move latency forgets a press on pause |
| GAP1-HOT-RELOAD-FLOW-01 | low | confirmed | new | `src/app/app.cpp:448` | Any file change in the data folder re-parses every table, repacks and re-uploads the seals, and logs 'read again' at INFO even when nothing changed |
| GAP1-HOT-RELOAD-FLOW-02 | low | partially-confirmed | new | `src/core/logic.cpp:96` | The ReloadData tick drops the other live actions of that tick and inverts ToggleRecording/PlayReplay sent with it |
| GAP1-HOT-RELOAD-FLOW-03 | low | partially-confirmed | new | `src/gfx/renderer.cpp:719` | Seal keys named by abilities.yaml are never checked against seals.yaml, and a missing key is logged only once per run, never cleared on reload |
| GAP1-HOT-RELOAD-FLOW-04 | low | confirmed | new | `src/app/app.cpp:268` | Edits saved while the game starts up are never reloaded: the watch takes its first listing after the files were read |
| GAP1-HOT-RELOAD-FLOW-05 | low | confirmed | new | `src/app/app.cpp:112` | DataWatch compares names and mtimes only, with no debounce: a non-atomic save seen mid-write can leave the broken read in force |
| GAP1-HOT-RELOAD-FLOW-06 | low | unverifiable | new | `src/game/scenes/world.cpp:58` | Place-name labels hang by MarkerCity's hard-coded size while world_look.yaml (and its hot reload) may pick any marker sprite |
| GAP1-HOT-RELOAD-FLOW-07 | low | partially-confirmed | new | `src/game/host.cpp:826` | After a data reload, a dev scene re-entered keeps the scratch base built under the old data |
| GAP1-HOT-RELOAD-FLOW-08 | low | confirmed | known-deferred | `src/app/app.cpp:499` | Edits to world.yaml, names.yaml and strings.yaml are silently ignored, logged only at debug |
| GAP1-HOT-RELOAD-FLOW-09 | low | partially-confirmed | new | `src/gfx/renderer.cpp:1648` | Presentation settings (UI colours, text options, damage-number options, reduce flashing, text scale) can't change at runtime, and two are never set at all |
| GAP1-HOT-RELOAD-FLOW-10 | low | confirmed | new | `src/core/world_look.hpp:218` | WorldLook::Hash is hand-maintained: a field left out of it silently stops that field's edits from reloading |
| GAP1-HOT-RELOAD-FLOW-11 | low | confirmed | new | `tests/unit/app/data_watch_tests.cpp:1` | WatchData's composition (order, partial failure, CheckScenes refusal, what ReloadData carries) has no test |
| GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-03 | low | confirmed | new | `src/core/saves.cpp:24` | Saves::Write logs replay files as 'saved the game' and 'no folder for saves' |
| GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-04 | low | confirmed | new | `src/app/app.cpp:561` | Attempt-logging: INFO lines record that something was asked for, not what resulted |
| GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-05 | low | unverifiable | new | `src/game/combat.cpp:480` | Per-entity and per-use DEBUG logs in the combat tick can flood the file in stress scenes |
| GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-06 | low | confirmed | new | `src/core/logic.cpp:152` | Tick numbers in log lines mix two clocks without saying which |
| GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-07 | low | partially-confirmed | new | `src/gfx/renderer.cpp:721` | Internals leak into messages: seal key hashes, scene-change counters, timeline epochs, raw entity IDs, slot numbers |
| GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-08 | low | partially-confirmed | new | `src/platform/sdl_log.cpp:34` | SDL log routing is installed late and drops SDL's category; early SDL output bypasses the log file |
| GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-09 | low | partially-confirmed | new | `src/game/combat.cpp:199` | Silent drop paths: Combat::Cast, window/gamepad, and several refusals log nothing |
| GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-10 | low | confirmed | new | `src/main.cpp:181` | Sparse context in ERROR lines: bare error messages with no subject |
| GAP1-SCENE-TRANSITION-FLOW-02 | low | confirmed | new | `src/app/app.cpp:840` | Damage numbers (and their stagger) from the old scene keep drawing over the new scene at the old coordinates |
| GAP1-SCENE-TRANSITION-FLOW-03 | low | partially-confirmed | new | `src/app/app.cpp:572` | Held movement carries through a scene change: arriving on the world map beside the portal with the approach key still held walks straight back through it |
| GAP1-SCENE-TRANSITION-FLOW-04 | low | deliberate | new | `src/core/logic.cpp:189` | Save, recording, replay toggles and other actions pressed during a scene change are silently dropped (debug log only) |
| GAP1-SCENE-TRANSITION-FLOW-05 | low | partially-confirmed | new | `src/app/app.cpp:832` | Per-scene presentation state is reset by five different keys, with no single reset point and no test of the seam |
| GAP1-SCENE-TRANSITION-FLOW-06 | low | partially-confirmed | new | `src/gfx/renderer.cpp:473` | The label pass fades the old scene's labels out over the new one, and runs fully (spending the layout budget) on frames that draw nothing while a scene loads |
| GAP1-SCENE-TRANSITION-FLOW-07 | low | confirmed | new | `src/app/app.cpp:727` | The target cursor stays shown on its old tile across a load that stays on the world map |
| GAP1-SCENE-TRANSITION-FLOW-08 | low | partially-confirmed | new | `src/core/logic.cpp:166` | A data reload during a replay start's scene change stops the replay but still swaps in its start state |
| GAP1-SCENE-TRANSITION-FLOW-09 | low | partially-confirmed | contradicts-recorded-decision | `src/app/app.cpp:806` | A scene load failure mid-game quits with the session unsaved; the logic has no way to cancel a change |
| GAP1-SIM-BOUNDARY-COMPLETENESS-01 | low | partially-confirmed | new | `src/game/host.cpp:276` | The regenerated base world's content is never verified on load; GENERATOR_VERSION is a manual guard (still 1) and the scratch base has no identity at all |
| GAP1-SIM-BOUNDARY-COMPLETENESS-02 | low | partially-confirmed | new | `src/game/host.cpp:307` | Every load and every replay start regenerates the game's whole world from the seed, although the host already holds that exact base in m_base |
| GAP1-SIM-BOUNDARY-COMPLETENESS-03 | low | confirmed | new | `src/game/host.cpp:1622` | Replay hashes never cover the game's world while a dev scene runs, so a replay started in a dev scene never verifies the m_world its start state restored |
| GAP1-SIM-BOUNDARY-COMPLETENESS-04 | low | partially-confirmed | new | `src/game/host.cpp:1705` | Host::SetData accepts a different world map, rebuilding paths and caches, but leaves m_world, m_base, m_baseHash and the explored bits built for the old map |
| GAP1-SIM-BOUNDARY-COMPLETENESS-05 | low | partially-confirmed | new | `src/core/logic.cpp:111` | A plain save drops a calendar step asked for on its tick; a ClockFits-accepted month-due save runs one frozen-clock tick before its step |
| GAP1-SIM-BOUNDARY-COMPLETENESS-06 | low | confirmed | new | `src/core/replay.cpp:153` | A recording knowingly continues after Record() drops a tick's actions past 255, producing a replay that is certain to diverge |
| GAP1-SIM-BOUNDARY-COMPLETENESS-07 | low | unverifiable | new | `src/game/host.cpp:762` | Load runs ApplyData over the restored state; save == load holds only while every scene spawns creatures exactly as ApplyData would set them |
| GAP1-SIM-BOUNDARY-COMPLETENESS-08 | low | partially-confirmed | new | `src/core/replay.cpp:14` | The replay header pins only the version string, so any rebuild with the same version (code, atlas or manifest changes) is accepted and diverges silently until the first hash |
| GAP1-SIM-BOUNDARY-COMPLETENESS-09 | low | confirmed | new | `tests/unit/game/replay_tests.cpp:58` | No gated test plays a replay back on a different worker count, view, or animation table than it was recorded with |
| GAP1-SWEEP-UP-BACKLOG-VS-CODE-03 | low | confirmed | new | `/home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md` | Backlog contains items already fixed or superseded (warnings branch, unity, arena_tests split, walk_speed range) |
| GAP1-SWEEP-UP-BACKLOG-VS-CODE-04 | low | unverifiable | new | `/home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md` | Backlog's 'next design' items (shared walk tables, journeys share routes) are now the shared-routes branch |
| GAP1-SWEEP-UP-BACKLOG-VS-CODE-05 | low | confirmed | known-deferred | `src/gfx/internal/features/meshes.cpp:159` | Quad-index upload flags still set at record time (Audit 1), meshes and ribbons not forwarded by Submitted |
| GAP1-SWEEP-UP-BACKLOG-VS-CODE-06 | low | partially-confirmed | known-deferred | `src/core/replay.cpp:85` | Replay ReadTicks reserve still loose (Audit 2 partly fixed; backlog text stale) |
| GAP1-SWEEP-UP-BACKLOG-VS-CODE-07 | low | confirmed | known-deferred | `src/game/host.cpp:1688` | Host::Load still refuses an unfit world with no log |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-03 | low | partially-confirmed | new | `src/core/log.hpp:34` | PN_LOG_* format strings are not checked at compile time (quill 13 stores them as a plain const char*) |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-04 | low | partially-confirmed | new | `src/core/ecs.cpp:8` | Domain::Create ignores EnTT's 2^20 live-entity limit, which is undefined behaviour in release builds |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-05 | low | partially-confirmed | new | `xmake.lua:266` | Tracy collects from start-up with no viewer connected (on_demand off), so releasedbg runs buffer profile data without bound |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-06 | low | confirmed | new | `src/core/profile.hpp:202` | No thread is named for Tracy or the OS; measure guesses threads from zone source files |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-07 | low | partially-confirmed | new | `src/core/profile.hpp:217` | Disabled PN_PROFILE_* macros drop their arguments unchecked, so mistakes surface only in releasedbg |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-08 | low | confirmed | new | `src/core/log.cpp:39` | quill's default check_printable_char escapes all UTF-8: Chinese text and non-ASCII paths are logged as \xNN |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-09 | low | partially-confirmed | new | `src/core/assert.cpp:12` | An assertion failure calls flush_log, which hangs if quill's backend has already stopped |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-10 | low | confirmed | new | `src/main.cpp:163` | log::Start runs outside main's try block, and quill's start-up can throw |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-11 | low | confirmed | new | `src/core/log.cpp:54` | The rotating log file never rotates by size, so one run's debug log grows without limit |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-12 | low | confirmed | new | `src/core/ecs.cpp:16` | Domain/State Create and Destroy carry on after a failed PN_ASSERT: undefined behaviour or orphaned entities in release |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-13 | low | confirmed | new | `src/gfx/labels.cpp:606` | CaptionOpacity, a few-flop pure function, carries a profiler zone |
| GAP2-COORDINATE-SPACES-AND-PICKING-03 | low | confirmed | new | `src/app/camera_control.cpp:205` | Camera has no bounds: pans without limit off the map and the scene in every Pan/Roam scene |
| GAP2-COORDINATE-SPACES-AND-PICKING-04 | low | partially-confirmed | new | `src/core/world_map.cpp:16` | World positions are f32 everywhere (snapshot, shader, picking) while the map limits allow coordinates where f32 breaks down |
| GAP2-COORDINATE-SPACES-AND-PICKING-05 | low | unverifiable | new | `src/game/host.cpp:1300` | Travellers are culled and sight-tested by their logical foot but drawn, faded and picked at the road-shifted point |
| GAP2-COORDINATE-SPACES-AND-PICKING-06 | low | confirmed | new | `src/app/app.cpp:643` | A click picks at the alpha of input-handling time, not the alpha of the picture the player clicked |
| GAP2-COORDINATE-SPACES-AND-PICKING-07 | low | confirmed | new | `tests/unit/app/view_tests.cpp:74` | Rotation pick test can't detect a sign error between PickInstance and the vertex shader; no end-to-end MakeInstance-to-pick test |
| GAP2-COORDINATE-SPACES-AND-PICKING-08 | low | confirmed | new | `src/game/host.cpp:1867` | Additive light orbs and scenery props carry IDs, so they are hovered and picked over creatures |
| GAP2-COORDINATE-SPACES-AND-PICKING-09 | low | confirmed | new | `src/game/combat.cpp:43` | Heads, labels, health bars and chest aim use the base Look sprite's foot, while the drawn instance uses the pose sprite |
| GAP2-COORDINATE-SPACES-AND-PICKING-10 | low | partially-confirmed | new | `src/game/components.hpp:117` | Two different ground-plane squash ratios: VIEW_TILT 0.6 (orbits, arena layout) and SEAL_VIEW_RATIO 0.375 (seals, shadow, portal) |
| GAP2-COORDINATE-SPACES-AND-PICKING-11 | low | confirmed | new | `src/gfx/route_draw.cpp:79` | Target cursor outline, markers and stamps are fixed 256-unit sprites while tile_size is accepted from 1 to 1,000,000 |
| GAP2-COORDINATE-SPACES-AND-PICKING-12 | low | confirmed | new | `src/core/view_limits.hpp:25` | WIDEST_HALF_VIEW is not the widest view: windows wider than 32:9 or narrower than 8:9 show more at MIN_ZOOM |
| GAP2-COORDINATE-SPACES-AND-PICKING-13 | low | partially-confirmed | new | `src/game/scenes/world.cpp:69` | Place names hang below the tile and can spill into the next tile; labels are never click targets |
| GAP2-DESIGN-DOCS-0002X-DRIFT-01 | low | confirmed | new | `docs/design/0002.4.2-text-and-mesh-path.md` | 0002.4.2 states mesh limits and index type the code does not use |
| GAP2-DESIGN-DOCS-0002X-DRIFT-02 | low | partially-confirmed | contradicts-recorded-decision | `docs/design/0002.4.2-text-and-mesh-path.md` | 0002.4.2 decision 3 (text keeps its screen size) is reversed in code with no note |
| GAP2-DESIGN-DOCS-0002X-DRIFT-03 | low | partially-confirmed | new | `docs/design/0002.4.2-text-and-mesh-path.md` | 0002.4.2/0002.4 describe a custom TTF_TextEngine and R8 coverage glyph cache that were never built |
| GAP2-DESIGN-DOCS-0002X-DRIFT-04 | low | confirmed | new | `docs/design/0002.4.2-text-and-mesh-path.md` | Damage-number mechanics in 0002.4/0002.4.2 are replaced by a different event and text design |
| GAP2-DESIGN-DOCS-0002X-DRIFT-05 | low | confirmed | new | `docs/design/0002.4-ui-and-text.md` | Dangling references to nonexistent 0002.4.3 |
| GAP2-DESIGN-DOCS-0002X-DRIFT-06 | low | partially-confirmed | new | `docs/design/0002.1-app-and-game.md` | 0002.1 ownership diagram and Game description name the arena, but Game owns game::Host |
| GAP2-DESIGN-DOCS-0002X-DRIFT-07 | low | confirmed | new | `docs/design/0002.3-input-data-scenes.md` | 0002.3 contradicts itself on settings and has stale format and name details |
| GAP2-LONG-SESSION-LIMITS-01 | low | confirmed | new | `src/game/host.cpp:1741` | Orbit angle is computed from the absolute simulation tick, so any speed change jumps every orbiter by Δspeed × tick |
| GAP2-LONG-SESSION-LIMITS-02 | low | confirmed | new | `src/game/travel.cpp:36` | Travel journal daysAway (u8, clamped at 255) saturates as NPC reach grows with world age on large maps |
| GAP2-LONG-SESSION-LIMITS-03 | low | unverifiable | new | `xmake.lua:266` | Tracy in releasedbg is not on-demand: the client buffers every zone, sample and context switch in memory until a viewer connects |
| GAP2-MEMORY-BUDGET-VS-0002-S13-03 | low | confirmed | new | `src/game/npcs.hpp:226` | Relationships keep a full stale copy of the edge table permanently (and Travel keeps its journal shells), doubling the largest game-state item |
| GAP2-MEMORY-BUDGET-VS-0002-S13-04 | low | unverifiable | new | `src/core/state.hpp:182` | StableId lookup maps cost about 91 B per entity, three times the NPC's own components |
| GAP2-MEMORY-BUDGET-VS-0002-S13-05 | low | confirmed | new | `docs/design/0002-engine-architecture.md:536` | §13's snapshot-buffer line contradicts §5 and is broken 2x at 100k instances |
