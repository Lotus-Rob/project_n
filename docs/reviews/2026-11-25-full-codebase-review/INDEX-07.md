# Finding index 7/11 (standing findings, most severe first)

The ID prefix is the unit file: e.g. CORE-WORLD-03 is in core-world.md (or its -partN files).

| ID | Severity | Verdict | Status | Where | Finding |
|---|---|---|---|---|---|
| TESTS-REST-03 | low | partially-confirmed | new | `tests/fuzz/yaml.cpp:58` | Fuzz YAML target feeds the same bytes to eleven parsers |
| TESTS-REST-04 | low | partially-confirmed | new | `tests/fuzz/save.cpp:1` | ReadSaveFile (header, zstd, checksum) is a reader of untrusted bytes with no fuzz target |
| TESTS-REST-05 | low | partially-confirmed | known-deferred | `tests/fuzz/replay.cpp:25` | Replay fuzz target has one 119-byte seed and nothing that proves it still parses |
| TESTS-REST-06 | low | partially-confirmed | new | `tests/reference/reference_tests.cpp:1` | Real GPU back ends (D3D12, Metal, real Vulkan drivers) are never rendered by any test |
| TESTS-REST-07 | low | partially-confirmed | new | `tests/unit/platform/window_tests.cpp:1` | Platform window tests leave the event paths for quit, resize, focus, visibility, wheel and gamepad hot-plug untested |
| TESTS-REST-08 | low | partially-confirmed | new | `tests/bench/headless.cpp:1` | Headless benchmark misses the scaling paths and is never a gate |
| TESTS-REST-09 | low | confirmed | new | `tests/support/manifest.hpp:22` | Silent std::abort() in shipped-data test helpers |
| TESTS-REST-10 | low | partially-confirmed | new | `tests/unit/gfx/text_layout_tests.cpp:295` | Timing-dependent race tests in text_layout_tests are probabilistic |
| TESTS-REST-11 | low | unverifiable | contradicts-recorded-decision | `tests/unit/data/world_map_tests.cpp:652` | Shipped-content pinning tests couple tests to a map that is only a test map |
| TESTS-REST-12 | low | partially-confirmed | known-deferred | `tests/unit/gfx/labels_tests.cpp:1` | Oversized test files and untested units in gfx and app |
| TESTS-REST-13 | low | partially-confirmed | new | `tests/unit/app/data_watch_tests.cpp:50` | DataWatch tests miss a file added, and the watch is blind to same-timestamp edits |
| TOOLS-A-01 | low | partially-confirmed | new | `tools/project_n.py:1386` | tidy_verdict passes a unit that has only ignored findings plus a bracketless compile error |
| TOOLS-A-02 | low | confirmed | new | `tools/project_n.py:1019` | NOLINT(*) and NOLINT(-*) count as naming the checks |
| TOOLS-A-03 | low | partially-confirmed | new | `tools/project_n.py:1401` | Local clang-tidy cache is never pruned or aged |
| TOOLS-A-04 | low | confirmed | new | `tools/project_n.py:1503` | tidy_key repeats global work for every unit |
| TOOLS-A-05 | low | partially-confirmed | new | `tools/project_n.py:599` | Missing external tool gives a traceback, not a doctor hint |
| TOOLS-A-06 | low | partially-confirmed | new | `tools/project_n.py:710` | build() rewrites build/compile_commands.json for any native preset, mode or toolchain |
| TOOLS-A-07 | low | partially-confirmed | new | `tools/project_n.py:1332` | Text lints use code_of, which ignores block comments and raw strings |
| TOOLS-A-08 | low | partially-confirmed | new | `tools/project_n.py:1641` | Shader tool pin check cannot see a tool pinned on only one platform |
| TOOLS-A-09 | low | confirmed | new | `tools/project_n.py:1194` | widens_scope omits several inputs that change every file's lint result |
| TOOLS-A-10 | low | confirmed | new | `tools/project_n.py:1216` | git path lists are read without -z, so non-ASCII paths are quoted and mismatch |
| TOOLS-A-11 | low | confirmed | new | `tools/project_n.py:1004` | source_files() scans the filesystem, not git, so ignored or stray files are linted |
| TOOLS-A-12 | low | partially-confirmed | new | `tools/project_n.py:758` | tests/xmake.lua is read without an encoding and by regex |
| TOOLS-A-13 | low | deliberate | new | `tools/AGENTS.md:7` | tools/project_n.py is not thin; the single-file rule is straining |
| TOOLS-A-14 | low | confirmed | known-deferred | `tools/project_n.py:973` | Reference compare ignores drawn pictures that have no committed reference, and the orphan case is only checked under --update |
| TOOLS-A-15 | low | confirmed | new | `tools/project_n.py:878` | reference_case_count and tidy preprocessing have no timeout |
| TOOLS-A-16 | low | partially-confirmed | new | `tools/project_n.py:57` | ASAN_OPTIONS detect_leaks=1 is set for every XMake call on macOS too |
| TOOLS-B-01 | low | confirmed | new | `tools/project_n.py:3076` | doctor's Python check is stale; the tool needs 3.13+ (3.14 is pinned) |
| TOOLS-B-02 | low | confirmed | new | `tools/project_n.py:1492` | tidy_key redoes invariant work for every unit |
| TOOLS-B-03 | low | confirmed | new | `tools/project_n.py:2117` | lint stops at the first failing category |
| TOOLS-B-04 | low | unverifiable | new | `tools/project_n.py:1332` | Regex linters have unhandled lexical cases and no tests for them |
| TOOLS-B-05 | low | partially-confirmed | new | `tools/project_n.py:1013` | Suppression check misses wildcard NOLINT, MSVC warning pragmas that suppress, and non-C++ suppressions |
| TOOLS-B-06 | low | confirmed | new | `tools/project_n.py:1939` | unit selection ignores changed generated headers |
| TOOLS-B-07 | low | partially-confirmed | new | `tools/project_n.py:3141` | clean --packages deletes package caches of other worktrees and branches |
| TOOLS-B-08 | low | confirmed | new | `tools/project_n.py:2968` | publish uploads whatever zips are in dist/, without checking they belong to the tagged commit |
| TOOLS-B-09 | low | partially-confirmed | new | `tools/project_n.py:2911` | release leaves a modified branch behind on failure and has no already-tagged or no-change guard |
| TOOLS-B-10 | low | partially-confirmed | new | `tools/project_n.py:3009` | commit-check gaps: type/scope rule for game code unenforced, `#` lines stripped from stored messages, footers ignored |
| TOOLS-B-11 | low | confirmed | contradicts-recorded-decision | `tools/project_n.py:1719` | shaders removes shaders/bin before compiling, so a tool failure leaves a half-empty bin |
| TOOLS-B-12 | low | confirmed | new | `tools/project_n.py:1605` | shader_resources ignores push constants and other unsupported SPIR-V resource classes |
| TOOLS-B-13 | low | partially-confirmed | new | `tools/project_n.py:1848` | Atlas generator: scene names and some errors are unvalidated or unhelpful |
| TOOLS-B-14 | low | partially-confirmed | new | `tools/project_n.py:2609` | measure's export parsing and recorder handling are fragile |
| TOOLS-B-15 | low | partially-confirmed | contradicts-recorded-decision | `tools/project_n.py:1` | project_n.py is no longer thin: one 3.4k-line file with many unrelated concerns |
| TOOLS-B-16 | low | partially-confirmed | new | `tools/project_n.py:3356` | main(): missing programs and Ctrl-C produce raw tracebacks; make_parser does file I/O on every invocation |
| TOOLS-B-17 | low | partially-confirmed | new | `tools/env/activate.sh:6` | activate.sh edits the shared gdbinit on every `pixi run` without locking; gdbinit has no error handling |
| X-ACCESSIBILITY-06 | low | confirmed | new | `src/app/bindings.cpp:108` | Axis events from several gamepads share one stick slot |
| X-ACCESSIBILITY-07 | low | unverifiable | new | `src/core/ui_colours.hpp:131` | Default palette separates damage kinds and element effects by colour alone |
| X-ACCESSIBILITY-08 | low | unverifiable | new | `assets/data/effects.yaml:45` | Hover and selection outlines are distinguished by hue only, and the hover ring is near-white on a pale ground |
| X-ACCESSIBILITY-09 | low | partially-confirmed | new | `src/core/glow.hpp:69` | Photosensitivity: no global limit on flash rate or additive stacking, and no warning |
| X-ACCESSIBILITY-10 | low | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk8-ui-design.md:152` | Chunk 8 and 10 specs promise a Back-button focus toggle and a pause menu that clash with the shipped bindings |
| X-ACCESSIBILITY-11 | low | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk11-settings-design.md:98` | No screen-reader, TTS or high-contrast mode in any spec; first-launch page assumes the player can already read it |
| X-ACCESSIBILITY-12 | low | confirmed | new | `src/platform/window.cpp:289` | No high-DPI handling: the window is not created with a pixel density flag and text sizes are in swapchain pixels |
| X-ACCESSIBILITY-13 | low | partially-confirmed | known-deferred | `src/app/bindings.hpp:128` | Held-input model has no hold-or-toggle, sticky keys or tuneable dead zone, and its key bitmask will not fit a rebindable table |
| X-ACCESSIBILITY-14 | low | partially-confirmed | new | `src/platform/window.hpp:129` | Position-based key codes and the Alt modifier chord assume a US layout and two hands |
| X-ACCESSIBILITY-15 | low | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk11-settings-design.md:43` | Display change revert timer is fixed at 15 seconds and its confirmation needs input from a possibly unseen screen |
| X-ACCESSIBILITY-16 | low | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk11-settings-design.md:65` | Search in settings is Latin-keystroke only; gamepad has no text path |
| X-ARCHITECTURE-09 | low | partially-confirmed | new | `src/game/host.cpp:850` | Presentation-only state lives in, and is mutated by, the deterministic Simulation's Tick |
| X-ARCHITECTURE-10 | low | partially-confirmed | contradicts-recorded-decision | `src/core/game_data.hpp:61` | core mixes the engine kernel with the game's domain model and presentation config |
| X-ARCHITECTURE-11 | low | deliberate | new | `src/core/frame.hpp:370` | View models drift from 0002 A13: ad hoc Snapshot fields are filled on every publish, with no subscriptions or versioning |
| X-ARCHITECTURE-12 | low | partially-confirmed | known-deferred | `docs/design/0002-engine-architecture.md:241` | 0002's systems-and-commands model, spatial hash grid and per-tick arenas are not implemented; the docs describe machinery that doesn't exist |
| X-ARCHITECTURE-13 | low | unverifiable | new | `src/game/host.cpp:85` | The save and hash schema is enumerated by hand in host.cpp: each new system edits several functions and version constants |
| X-ARCHITECTURE-14 | low | unverifiable | new | `src/app/app.hpp:62` | App mixes the main loop, input routing, hot reload and a profiling harness, with no input-context layering for upcoming UI screens |
| X-ARCHITECTURE-15 | low | unverifiable | known-deferred | `src/gfx/renderer.cpp:199` | Renderer::Impl is a monolith with about 110 members and a hard-coded pass order; each feature adds a member, an upload function, flags and a failure counter |
| X-ARCHITECTURE-16 | low | partially-confirmed | new | `src/app/bindings.cpp:98` | Debug and cheat controls are bound in every build, release included |
| X-ARCHITECTURE-17 | low | unverifiable | new | `src/core/frame.hpp:488` | ActionKind is a closed core enum and Action a fat struct: every gameplay verb means a core edit |
| X-ARCHITECTURE-18 | low | partially-confirmed | new | `src/core/ecs.hpp:29` | The ECS facade is nominal: it exports entt::registry whole and gives everyone a mutable registry that bypasses State's ID invariants |
| X-ARCHITECTURE-19 | low | confirmed | known-deferred | `docs/design/0002-engine-architecture.md:5` | 0002 has drifted from the code in ways that mislead readers |
| X-ARCHITECTURE-20 | low | partially-confirmed | known-deferred | `src/game/host.cpp:566` | Content is compiled in: the SpriteId enum, AbilityKind, StringId and built-in creatures sit in headers that core and game logic include |
| X-CONCURRENCY-01 | low | confirmed | new | `src/core/jobs.cpp:222` | Exceptions thrown inside job chunks terminate workers silently or leave enkiTS holding a destroyed stack task (use-after-free) |
| X-CONCURRENCY-02 | low | partially-confirmed | new | `src/core/jobs.cpp:230` | Main and logic threads steal each other's high-priority chunks: frame time is hostage to month-step and send-out chunks |
| X-CONCURRENCY-03 | low | confirmed | new | `src/core/jobs.cpp:213` | Waits for background and Pending jobs run work of any priority: the logic thread can decode images or rasterise glyphs, and the main thread can write a save |
| X-CONCURRENCY-04 | low | partially-confirmed | new | `src/core/jobs.cpp:40` | Floating-point environment never re-checked on the main thread or on workers, though both run simulation chunks |
| X-CONCURRENCY-05 | low | deliberate | contradicts-recorded-decision | `tests/unit/core/handover_tests.cpp:368` | TSan never runs automatically: the partitioned-chunk invariants are checked only by comments and on-request sanitizer runs |
| X-CONCURRENCY-06 | low | confirmed | new | `src/app/app.cpp:285` | Data race on App::m_options at logic-thread start |
| X-CONCURRENCY-07 | low | partially-confirmed | new | `src/game/travel.cpp:397` | Paths::To lazily builds distance maps and is shared across SetOut chunks; safety depends on an unchecked warm-up |
| X-CONCURRENCY-08 | low | confirmed | new | `src/game/host.cpp:1571` | Shutdown blocks the main thread's join for the length of world generation or an uninterruptible month-step phase |
| X-CONCURRENCY-09 | low | confirmed | new | `src/game/travel.cpp:447` | Travel::Seed (world generation) runs its chunks serially although the logic thread holds a job system |
| X-CONCURRENCY-10 | low | confirmed | new | `src/core/saves.hpp:35` | Docs say these run 'on the thread that created the job system', but they run on the registered logic thread |
| X-CONCURRENCY-11 | low | partially-confirmed | new | `src/core/handover.hpp:156` | Handover::ReturnState has no bound or ownership check |
| X-CONCURRENCY-12 | low | partially-confirmed | new | `src/core/logic.cpp:44` | Every burst republishes the whole snapshot even while frozen or paused and nothing changed |
| X-CONCURRENCY-13 | low | confirmed | new | `src/gfx/text_layout.cpp:180` | Fonts::RasteriseRequests pops from the front of a vector under the queue mutex |
| X-CONCURRENCY-14 | low | unverifiable | new | `src/gfx/text_layout.cpp:191` | Glyph rasterisation on worker threads is outside SDL3_ttf's documented threading contract |
| X-CONCURRENCY-15 | low | confirmed | new | `src/app/game.cpp:17` | Game's tick clock starts before world generation, so startup is counted as a hitch |
| X-CONCURRENCY-16 | low | confirmed | new | `tests/unit/core/jobs_tests.cpp:159` | No test exercises two threads forking on one Jobs at once, or Game::Run on a jthread with stop |
| X-CPP-SAFETY-03 | low | partially-confirmed | new | `src/core/journey.cpp:299` | MakeTileLeg wraps the u32 arrive ticks in release builds; Where() then reads out of bounds and the game refuses its own saves |
| X-CPP-SAFETY-04 | low | partially-confirmed | new | `src/core/assert.hpp:28` | Release builds turn memory-safety invariants into silent UB: PN_ASSERT compiles out and STL hardening is debug-only |
| X-CPP-SAFETY-05 | low | partially-confirmed | new | `tests/xmake.lua:132` | Fuzz targets only build under AddressSanitizer: UB reachable from save and replay bytes goes undetected |
| X-CPP-SAFETY-06 | low | confirmed | new | `src/game/npcs.cpp:133` | CellAt converts an unbounded float to u32 (UB) and wraps the cell index |
| X-CPP-SAFETY-07 | low | partially-confirmed | new | `src/core/jobs.cpp:226` | A throw inside a ParallelFor body is a use-after-free on the forking thread and a terminate on workers |
| X-CPP-SAFETY-08 | low | partially-confirmed | new | `src/app/app.cpp:265` | The main thread runs simulation chunks, but nothing checks or restores its floating-point environment |
| X-CPP-SAFETY-09 | low | confirmed | new | `src/gfx/labels.cpp:35` | LabelPass::CellOf converts a possibly-NaN float straight to size_t |
| X-DETERMINISM-02 | low | deliberate | new | `src/core/replay.cpp:15` | Replays identify their build by version string only (0.1.0 for every commit, preset and mode), so another binary is reported as a divergence instead of refused |
| X-DETERMINISM-03 | low | partially-confirmed | new | `src/game/combat.cpp:45` | Combat results depend on the atlas manifest (sprite footY), which is neither part of the replay's data hash nor game data |
| X-DETERMINISM-04 | low | unverifiable | new | `src/core/jobs.cpp:40` | FP environment is set once per thread and checked only on the logic thread by a release-compiled-out assert; the main thread runs month-step chunks |
| X-DETERMINISM-05 | low | unverifiable | new | `tests/unit/game/shipped_world_tests.cpp:414` | No golden-value regression test: generator, compiler-flag or code-generation changes that alter the sim go unnoticed |
| X-DETERMINISM-06 | low | confirmed | new | `src/game/npcs.cpp:132` | `CellAt` converts unbounded floats to u32 (UB) for huge but finite positions accepted from saves |
| X-DETERMINISM-07 | low | unverifiable | new | `src/core/paths.cpp:230` | `Paths::To` lazily writes its cache and is called from parallel chunks; safety rests on a warm-up convention checked only by assertions |
| X-DETERMINISM-08 | low | unverifiable | new | `src/core/logic.cpp:289` | Hashing the whole state every 60 wall ticks while recording or playing a replay may hitch the logic thread on a 50k-NPC world |
| X-DETERMINISM-09 | low | confirmed | new | `src/core/game_data.hpp:24` | f32 world positions quantise slow movement on large maps: walking at MIN_WALK_SPEED stalls beyond about 1,000 tiles |
| X-PERFORMANCE-05 | low | unverifiable | new | `src/game/host.cpp:1829` | Publish builds every entity's instance (two f64 std::remainder calls, a glow lookup, a dissolve) before DrawList::Add culls it |
| X-PERFORMANCE-06 | low | unverifiable | new | `src/core/paths.cpp:136` | FindShortestRoute runs a full-map A* with three full-map arrays inside the logic tick on every click |
| X-PERFORMANCE-07 | low | confirmed | new | `src/gfx/terrain_draw.cpp:59` | Bridge heading scans every point of every road curve on the map for each bridge tile, on every terrain re-layout (each zoom frame) |
| X-PERFORMANCE-08 | low | unverifiable | new | `src/gfx/volley_draw.cpp:92` | Volleys recompute each sword's plan about 12 times a frame, and the target walk is O(T^2) per sword |
| X-PERFORMANCE-09 | low | confirmed | new | `src/game/components.cpp:14` | Orbiting entities update single-threaded on the logic thread with three deterministic software trig calls each per tick |
| X-PERFORMANCE-10 | low | partially-confirmed | new | `src/core/logic.cpp:298` | Replay hash checks hash the whole world every 60 ticks while recording or playing |
| X-PERFORMANCE-11 | low | unverifiable | new | `src/gfx/internal/resources/sprite_array.cpp:31` | Whole sprite atlas allocated in VRAM up front with no eviction; mips are regenerated for the whole array on every scene load |
| X-PERFORMANCE-12 | low | unverifiable | new | `src/core/road_curve.cpp:251` | RoadCurves' end joins are O(roads x all curve points) and are computed twice (host and renderer); RoadCurveIndex is a dense per-tile optional array |
| X-PERFORMANCE-13 | low | confirmed | new | `src/gfx/ground_field.cpp:47` | GroundTexels does 121 neighbour reads per tile for the shore field, on the main thread at start and on every world-look reload |
| X-PERFORMANCE-14 | low | partially-confirmed | new | `src/core/logic.cpp:80` | The logic publishes and the renderer uploads a full snapshot every tick, even while the game is paused and nothing changed |
| X-PERFORMANCE-15 | low | confirmed | new | `src/game/combat.cpp:636` | EndReleased calls Holds() per ending use, and each scans all flights and all afflicted entities |
| X-PERFORMANCE-16 | low | confirmed | new | `src/game/host.cpp:1412` | The snapshot copies the whole explored bitset every publish, though its version is published beside it |
