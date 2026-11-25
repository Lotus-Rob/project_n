# Finding index 1/11 (standing findings, most severe first)

The ID prefix is the unit file: e.g. CORE-WORLD-03 is in core-world.md (or its -partN files).

| ID | Severity | Verdict | Status | Where | Finding |
|---|---|---|---|---|---|
| APP-INPUT-UI-01 | medium | deliberate | known-deferred | `src/app/cast.cpp:43` | Held-pad cast aims at the mouse cursor / key casts have no keyboard/gamepad aiming |
| APP-MAIN-01 | medium | confirmed | new | `src/app/app.cpp:325` | Logic thread failure ends the game with no user-visible message and may be masked by a simultaneous quit |
| CORE-INFRA-01 | medium | confirmed | new | `src/core/log.cpp:39` | Asynchronous logging with no crash/terminate handler: the lines explaining a crash are lost |
| CORE-PERSIST-01 | medium | confirmed | new | `tests/unit/core/save_file_tests.cpp:119` | The save checksum's mismatch path is untested |
| CORE-WORLD-01 | medium | confirmed | new | `src/core/paths.hpp:92` | Distance maps are kept per (city, profile) over the whole map: memory and build time grow as tiles x cities x profiles |
| CORE-WORLD-02 | medium | confirmed | new | `src/core/paths.cpp:136` | A player's click runs a whole-map A* on the logic thread, allocating three full-map arrays and exploring the whole connected area for an unreachable target |
| DATA-COMBAT-01 | medium | confirmed | new | `src/data/abilities.cpp:47` | f64 to f32 narrowing in abilities and effects readers has no range guard (out-of-range UB, silent zero) |
| DATA-YAML-CORE-01 | medium | confirmed | new | `src/data/settings.cpp:164` | SaveSettings never writes edge_pan, so a save would reset it |
| DESIGN-DOCS-01 | medium | confirmed | new | `docs/design/0001-repository-build-environment.md:116` | 0001 still documents removed skills/agents and the pre-superpowers agent process |
| DESIGN-DOCS-02 | medium | confirmed | new | `docs/design/0001-repository-build-environment.md:467` | AGENTS.md size rule (≤~100 lines, one line per rule) is met only on paper; src/AGENTS.md is 45 KB |
| DESIGN-DOCS-03 | medium | confirmed | new | `docs/design/0002-engine-architecture.md:244` | Per-tick/per-worker arenas and the command-merge rule are presented as followed, but neither exists |
| DESIGN-DOCS-04 | medium | partially-confirmed | new | `docs/design/0002-engine-architecture.md:293` | Delta saves trust a hand-bumped GENERATOR_VERSION; generator code changes can mis-apply a save without error |
| DESIGN-DOCS-05 | medium | confirmed | new | `docs/design/0002-engine-architecture.md:295` | 'Saves can't be corrupted by a crash' is overstated: no fsync and close errors unchecked |
| DESIGN-DOCS-06 | medium | confirmed | new | `docs/design/0002-engine-architecture.md:500` | Exceptions on job-system workers terminate the game unlogged, contrary to §12 |
| GAME-COMBAT-01 | medium | confirmed | new | `src/game/combat.cpp:676` | Lance cast on the portal tick: no UseEnded, and a leftover strike trips PN_ASSERT in the replay hash |
| GAME-HOST-A-01 | medium | confirmed | new | `src/game/host.cpp:528` | Held movement walks the cultivator off the world map, and a load accepts any finite place to return to |
| GAP1-CLOCK-DISCIPLINE-PAUSE-01 | medium | confirmed | new | `src/core/logic.cpp:125` | The world's P pause leaves the simulation tick (and so the render clock and every per-tick system) running |
| GAP1-CLOCK-DISCIPLINE-PAUSE-02 | medium | partially-confirmed | new | `src/platform/window.cpp:314` | Rendering stays uncapped (mailbox) while the window is unfocused and the game is paused |
| GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-01 | medium | confirmed | known-deferred | `src/game/host.cpp:1688` | Host::Load refuses saves silently for most checks; Logic::Load's single generic ERROR hides the cause |
| GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-02 | medium | confirmed | new | `src/main.cpp:167` | A bug report's log carries no game version, build, OS, CPU or GPU name |
| GAP1-SCENE-TRANSITION-FLOW-01 | medium | confirmed | new | `src/gfx/renderer.cpp:460` | Every portal or F2 change draws the new scene against the old scene's instance buffer: the Leaving snapshot already claimed the same (epoch, wallTick) |
| GAP1-SWEEP-UP-BACKLOG-VS-CODE-01 | medium | unverifiable | new | `/home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md` | Serious review classes absent from the backlog: crash and exception handling, save durability, Windows non-ASCII paths |
| GAP1-SWEEP-UP-BACKLOG-VS-CODE-02 | medium | unverifiable | new | `/home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md` | Procedural-map scaling findings are not in the backlog, which only lists distance maps |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-01 | medium | confirmed | new | `xmake.lua:266` | Tracy builds define TRACY_CALLSTACK=1 and TRACY_FIBERS: every zone and message captures a callstack |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-02 | medium | confirmed | new | `src/game/host.cpp:1829` | Host::Publish builds every entity's instance, pose and glow before DrawList::Add culls it |
| GAP2-COORDINATE-SPACES-AND-PICKING-01 | medium | confirmed | new | `src/app/target_cursor.cpp:53` | Target cursor can be steered off screen; the camera never follows it |
| GAP2-COORDINATE-SPACES-AND-PICKING-02 | medium | confirmed | new | `src/app/target_cursor.cpp:90` | Selecting an entity is mouse-only: the target cursor's Confirm always walks |
| GAP2-MEMORY-BUDGET-VS-0002-S13-01 | medium | partially-confirmed | new | `docs/design/0002-engine-architecture.md:535` | The 50 MB game-state budget is broken at shipped scale once the world has run 1-10 hours |
| GAP2-MEMORY-BUDGET-VS-0002-S13-02 | medium | confirmed | new | `src/core/profile.hpp:11` | Memory budgets are unmeasurable: no Tracy plot, allocation or GPU-memory statistic exists, contrary to §8.9 and §13 |
| GAP2-REPO-CONFIG-FILES-01 | medium | partially-confirmed | new | `.clang-tidy:1` | No check or sanitizer catches float-to-integer cast UB |
| GD-COMBAT-SYSTEMS-01 | medium | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk7-combat-scenes-design.md:82` | Chunk 7's balance anti-goals cannot be met while enemies have no verbs |
| GD-COMBAT-SYSTEMS-02 | medium | deliberate | known-deferred | `src/core/abilities.hpp:85` | Abilities are a closed enum with per-identity code dispatch: no path to hundreds of skills or build variety |
| GD-COMBAT-SYSTEMS-03 | medium | confirmed | new | `src/game/combat.cpp:230` | Qi is gained on cast, not on hit, so the resource loop is never a real constraint |
| GD-COMBAT-SYSTEMS-04 | medium | confirmed | new | `src/game/combat.cpp:435` | HitAmount and Damage carry no caster, target or source: stats, defence and attribution cannot plug in |
| GD-COMBAT-SYSTEMS-05 | medium | confirmed | new | `src/game/combat.cpp:72` | Hits test a single chest point: no creature hit radius, so large enemies (chunk 7 bosses) are hit unreliably |
| GD-COMBAT-SYSTEMS-06 | medium | confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk7-combat-scenes-design.md:131` | Chunk 7 requires path collision that the scheduled-hit volley model cannot express |
| GD-COMBAT-SYSTEMS-07 | medium | partially-confirmed | contradicts-recorded-decision | `src/game/combat.cpp:219` | No input buffer and no hold-to-cast: the generator needs 4 clicks a second and early presses are lost |
| GD-COMBAT-SYSTEMS-08 | medium | deliberate | known-deferred | `src/app/bindings.cpp:78` | Casting has no gamepad bindings, and keyboard casts need the mouse cursor |
| GD-ROADMAP-SCOPE-01 | medium | partially-confirmed | contradicts-recorded-decision | `docs/superpowers/specs/2026-11-10-phase6-design.md:111` | Game-design brainstorm keeps slipping behind world-map polish and scale work |
| GD-ROADMAP-SCOPE-02 | medium | partially-confirmed | new | `docs/design/0002-engine-architecture.md:561` | No vertical slice defined; the first playable loop sits after phase 7 |
| GD-ROADMAP-SCOPE-03 | medium | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk10-screens-design.md:36` | Approved chunk 6, 7 and 10 specs predate the game design and the procedural-map decision |
| GD-ROADMAP-SCOPE-04 | medium | partially-confirmed | new | `art/world_map.py:1` | Runtime procedural world generation has no spec or plan, yet routing is being built for it |
| GD-ROADMAP-SCOPE-05 | medium | deliberate | known-deferred | `docs/design/0002-engine-architecture.md:546` | Windows D3D12 and macOS Metal never run; the genre's main platform is unverified |
| GD-ROADMAP-SCOPE-06 | medium | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk8-ui-design.md:30` | UI framework is unproven and late, so stand-in UIs pile up and accessibility waits |
| GD-ROADMAP-SCOPE-07 | medium | confirmed | new | `src/AGENTS.md:27` | src/AGENTS.md is 45 KB with a 15,404-character 'rule' line |
| GD-ROADMAP-SCOPE-08 | medium | partially-confirmed | new | `src/app/app.cpp:325` | No autosave anywhere in the plans; logic-thread failure exits silently |
| GD-ROADMAP-SCOPE-09 | medium | confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk5-world-map-design.md:49` | Time model chosen before the game design: continuous days vs time passing per action |
| GD-TOI-COVERAGE-01 | medium | confirmed | new | `src/game/world_state.cpp:240` | The player's cultivator is a throwaway scene entity, not a persistent character in the world |
| GD-TOI-COVERAGE-02 | medium | confirmed | new | `src/core/abilities.hpp:187` | Loadout, Qi capacity and creature stats live in hashed global game data, not per-entity state |
| GD-TOI-COVERAGE-03 | medium | partially-confirmed | new | `src/game/combat.cpp:72` | No free-projectile or collision system: every hit is pre-assigned, so enemies can't attack and dodge combat can't exist |
| GD-TOI-COVERAGE-04 | medium | partially-confirmed | new | `src/game/npcs.cpp:99` | Every NPC cultivates at the same rate, so 'mortals' become 'cultivators' within ~10-15 months and the travel tuning drifts |
| GD-TOI-COVERAGE-05 | medium | partially-confirmed | new | `src/game/npcs.cpp:23` | NPC behaviour is two hard-coded deciders, not the data-driven utility AI the engine doc promises |
| GD-TOI-COVERAGE-06 | medium | confirmed | new | `src/game/components.hpp:91` | Creature kinds are hard-coded to the player and foxes throughout the code |
| GD-TOI-COVERAGE-07 | medium | confirmed | new | `src/data/abilities.cpp:371` | Ability element data bugs: 'healing' abilities damage enemies, and the burn/poison key is ignored |
| GD-TOI-COVERAGE-08 | medium | confirmed | new | `src/core/generated/atlas.hpp:12` | The sprite set is a compiled enum, so mods and data can't add creatures or items with new art without a rebuild |
| GD-TOI-COVERAGE-09 | medium | confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk7-combat-scenes-design.md:97` | The chunk 6, 7 and 9 specs assume the hand-made test map and predate the game design |
| GD-WORLD-SIM-01 | medium | partially-confirmed | new | `src/main.cpp:112` | The procedural-per-seed world has no generator design, and the architecture assumes one data-defined map per process |
| GD-WORLD-SIM-02 | medium | partially-confirmed | contradicts-recorded-decision | `src/game/host.cpp:164` | Delta saves trust that the base regenerates bit for bit, but nothing checks it |
| GD-WORLD-SIM-03 | medium | partially-confirmed | new | `src/game/npcs.cpp:99` | Unlimited power growth turns most mortals into cultivators within about one in-game year and undoes the travel tuning |
| GD-WORLD-SIM-04 | medium | deliberate | known-deferred | `src/core/paths.hpp:89` | Whole-map distance maps per (city, profile) grow with places x tiles |
| GFX-COMBAT-DRAW-01 | medium | confirmed | new | `src/gfx/seal_draw.cpp:613` | spin_scale (the reduce-motion lever) also stretches seals' appear and fade: dimmer, earlier-fading seals at low settings, instant at exactly 0 |
| GFX-FEATURES-01 | medium | confirmed | known-deferred | `src/gfx/internal/features/meshes.cpp:155` | Quad index buffers are marked uploaded when recorded, not when submitted (meshes, ribbons, seals) |
| GFX-RENDERER-01 | medium | partially-confirmed | new | `src/gfx/river_draw.hpp:22` | Rivers and roads are built for the whole map into fixed 8,192-quad buffers and drawn uncull'd every frame: doesn't scale to large procedural maps |
| GFX-WORLD-DRAW-01 | medium | partially-confirmed | new | `src/gfx/river_draw.cpp:150` | Fixed 8,192-quad river/road buffers silently drop later rivers and roads on large procedural maps |
| PLATFORM-01 | medium | confirmed | new | `src/platform/window.cpp:289` | No HiDPI support: window opened without SDL_WINDOW_HIGH_PIXEL_DENSITY, window size used as pixel size |
| SPECS-ROADMAP-01 | medium | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk10-screens-design.md:34` | Specs for chunks 5, 7 and 10 assume a hand-made map, contradicting the procedural-per-seed decision; no spec owns the map generator |
| SPECS-ROADMAP-02 | medium | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-design.md:151` | Chunks 5–10 lack the Accessibility section phase decision 17 requires |
| SPECS-ROADMAP-03 | medium | confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-design.md:108` | Phase table and chunk 5 spec statuses are stale (5b3, 5b-look, 5c split) |
| SPECS-ROADMAP-04 | medium | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk9-view-models-design.md:33` | Chunk 9's world-map roster includes thousands of undrawn, unclickable city residents |
| SPECS-ROADMAP-05 | medium | confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk11-settings-design.md:129` | settings.yaml ownership conflicts between chunk 8 (window layout) and chunk 11 (table-checked overrides); the ui module can't reach data |
| SPECS-ROADMAP-06 | medium | confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk8-ui-design.md:63` | Window moving and scaling are mouse-only, and no chunk adds a keyboard or gamepad way |
| TESTS-CORE-01 | medium | confirmed | new | `tests/unit/core/math_tests.cpp:26` | core::math functions only compared with platform libm within 4 ulps; no pinned golden values |
| TESTS-GAME-01 | medium | confirmed | new | `tests/unit/game/replay_tests.cpp:58` | Replay Session defaults to the shipped 50,000-NPC data for every scene |
| TESTS-GAME-02 | medium | confirmed | new | `tests/unit/game/combat_tests.cpp:31` | Helper functions duplicated across test files, some with divergent semantics |
| TESTS-GAME-03 | medium | partially-confirmed | new | `tests/unit/game/save_tests.cpp:421` | Save-refusal tests assert only that a load failed, so they can pass for the wrong reason |
| TESTS-GAME-04 | medium | confirmed | new | `tests/unit/game/save_exactness_tests.cpp:486` | Fuzz-seed test never reads the committed corpus files |
| X-ACCESSIBILITY-01 | medium | confirmed | new | `src/platform/window.cpp:257` | Stuck movement or pan after a gamepad is removed, because stick state is never zeroed |
| X-ACCESSIBILITY-02 | medium | deliberate | known-deferred | `src/app/bindings.cpp:75` | Casting generator and spender has no keyboard-only binding, and no gamepad binding at all |
| X-ACCESSIBILITY-03 | medium | partially-confirmed | new | `src/app/app.cpp:536` | Escape quits instantly with no confirmation; the gamepad cannot quit or open any menu |
| X-ACCESSIBILITY-04 | medium | partially-confirmed | new | `src/game/host.cpp:940` | Combat cannot be paused by the player |
| X-ACCESSIBILITY-05 | medium | confirmed | new | `src/data/settings.cpp:165` | Settings have no accessibility fields, and edge pan cannot be switched off in-game or saved |
| X-ARCHITECTURE-01 | medium | confirmed | new | `src/game/host.hpp:67` | game::Host is a god object: simulation, save/hash schema, player control, fog, routing and presentation caches in one class |
| X-ARCHITECTURE-02 | medium | partially-confirmed | new | `src/main.cpp:112` | No architectural path for a per-seed procedural world map: the map is static data loaded once by main and handed to the renderer at start |
| X-ARCHITECTURE-03 | medium | partially-confirmed | new | `src/core/paths.hpp:79` | core::Paths keeps a full-map distance map per (profile, city): memory and build time grow as cities × profiles × tiles |
| X-ARCHITECTURE-04 | medium | partially-confirmed | new | `src/game/host.cpp:1409` | Fog of war does whole-map work every publish and every tile step, and one texel per tile caps map size by GPU texture limits |
| X-ARCHITECTURE-05 | medium | partially-confirmed | new | `src/game/scenes.cpp:21` | Scenes are a fixed ring of stateless singletons with no parameters, unfit for towns, dungeons and encounters |
| X-ARCHITECTURE-06 | medium | partially-confirmed | new | `src/game/combat.cpp:72` | Combat is hard-wired to one player caster, foxes as the only enemies and one global loadout |
| X-ARCHITECTURE-07 | medium | partially-confirmed | new | `src/game/npcs.cpp:255` | The active NPC tier is unusable on the world map; travellers are journey records, not entities |
| X-ARCHITECTURE-08 | medium | confirmed | new | `src/AGENTS.md:27` | src/AGENTS.md breaks the repo's own 'one line per rule, about 100 lines' rule with paragraphs of 5,000 to 15,000 characters that copy the code's docs |
| X-CPP-SAFETY-01 | medium | confirmed | new | `src/core/saves.cpp:19` | path::string() can throw on Windows; a success-path log inside the background save job turns every save into std::terminate |
| X-CPP-SAFETY-02 | medium | confirmed | new | `src/gfx/text_layout.cpp:452` | Font path passed to SDL_ttf as ANSI bytes, not UTF-8 |
| X-DETERMINISM-01 | medium | partially-confirmed | new | `src/game/host.cpp:307` | Delta saves apply to a regenerated base that is never verified: generator or code changes, or another compiler or platform, corrupt the world silently |
| X-PERFORMANCE-01 | medium | confirmed | new | `src/core/paths.cpp:230` | Paths keeps a full-map distance map per city per profile, all warmed at once on one thread: memory and generation time scale as tiles x cities x profiles |
| X-PERFORMANCE-02 | medium | confirmed | new | `src/gfx/road_draw.hpp:19` | Road and river ribbons for the whole map share fixed 8,192-quad buffers; larger maps silently drop whole roads and rivers |
| X-PERFORMANCE-03 | medium | confirmed | new | `src/app/fog_fade.cpp:20` | FogFade rebuilds whole-map targets and scans every tile each time the player's tile changes, and the whole-map fog texture is re-uploaded every frame of a fade |
| X-PERFORMANCE-04 | medium | confirmed | new | `src/game/host.cpp:1290` | PublishJourneys walks every journey and every member each publish; no spatial index |
| X-RESILIENCE-01 | medium | confirmed | new | `src/core/log.cpp:34` | No crash handler: fatal signals, SEH and std::terminate lose queued log lines and leave no crash record |
| X-RESILIENCE-02 | medium | confirmed | new | `src/core/jobs.cpp:105` | Exceptions inside enkiTS tasks and background jobs are not caught: std::terminate with nothing logged |
| X-RESILIENCE-03 | medium | partially-confirmed | new | `src/gfx/renderer.cpp:1313` | GPU device loss or persistent frame failure is never treated as fatal or recovered; the game spins forever with a dead window |
| X-RESILIENCE-04 | medium | confirmed | new | `src/app/app.cpp:806` | Fatal runtime failures (scene load failure, logic thread exception) exit with status 1 and no message to the player |
| X-RESILIENCE-05 | medium | confirmed | new | `src/core/save_file.cpp:79` | Save write is not durable: no fsync, no previous-save backup, and overwrite only by rename |
| X-RESILIENCE-06 | medium | confirmed | new | `src/core/saves.cpp:19` | Save and load failures are log-only: the player is never told a save failed (disk full, unwritable folder) or a load was refused |
| X-TEXT-L10N-01 | medium | confirmed | new | `tests/unit/gfx/ttf_tests.cpp:41` | Promised font-coverage test for all shipped strings does not exist |
| X-TEXT-L10N-02 | medium | confirmed | new | `src/gfx/text_layout.cpp:452` | Font path built with .string(): breaks on Windows with non-ASCII install path |
| X-TEXT-L10N-03 | medium | partially-confirmed | new | `src/app/hud.cpp:15` | HUD control names hard-code mouse and keyboard, no gamepad or rebinding text |
| X-UNTRUSTED-INPUT-01 | medium | confirmed | new | `src/gfx/text_layout.cpp:452` | Font path passed to SDL_ttf in the Windows ANSI code page, not UTF-8: game fails to start from any non-ASCII install folder |
| X-UNTRUSTED-INPUT-02 | medium | confirmed | new | `src/data/settings.cpp:196` | path.string() in log/error arguments throws on Windows for paths outside the ANSI code page: startup aborts, saves terminate the process |
| AGENT-DOCS-01 | low | confirmed | new | `.claude/skills/git-commit/SKILL.md` | git-commit skill examples use scopes that commit-check rejects |
| AGENT-DOCS-02 | low | confirmed | new | `src/AGENTS.md:4` | AGENTS.md files far exceed the stated size and format rule |
| AGENT-DOCS-03 | low | unverifiable | new | `AGENTS.md:79` | Global rule 'low = Minor, note it' conflicts with the project's 'fix every finding, Minor included' |
