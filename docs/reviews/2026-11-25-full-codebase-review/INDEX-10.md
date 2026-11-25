# Finding index 10/11 (standing findings, most severe first)

The ID prefix is the unit file: e.g. CORE-WORLD-03 is in core-world.md (or its -partN files).

| ID | Severity | Verdict | Status | Where | Finding |
|---|---|---|---|---|---|
| GAP2-DESIGN-DOCS-0002X-DRIFT-12 | info | partially-confirmed | new | `docs/design/0002.3.1-manifest-and-scene-loading.md` | Scene lists in the docs stop at four scenes; the manifest and F2 cycle now include `world` |
| GAP2-DESIGN-DOCS-0002X-DRIFT-13 | info | partially-confirmed | new | `docs/design/0002.4.2-text-and-mesh-path.md` | 0002.4 text API and settings claims: hot reload and settings now broader than documented |
| GAP2-DESIGN-DOCS-0002X-DRIFT-14 | info | confirmed | new | `tools/project_n.py:1236` | MODULES in tools/project_n.py lists ui and audio modules that do not exist |
| GAP2-DESIGN-DOCS-0002X-DRIFT-15 | info | confirmed | new | `docs/design/0002.3.1-manifest-and-scene-loading.md` | 0002.3.1 §5: renderer comment on single-slot scene loading and ReportFindings of 'uploads start once all decodes are done' diverge slightly from the optimisation the code made |
| GAP2-LONG-SESSION-LIMITS-04 | info | partially-confirmed | new | `src/core/tick_clock.hpp:37` | TickTime and TickClock::Advance overflow after about 9.75 years of continuous scheduling without a Restart |
| GAP2-LONG-SESSION-LIMITS-05 | info | partially-confirmed | new | `src/game/host.cpp:1176` | Long-lived presentation clocks count from simulation tick 0 (portal seal, DemoPulse, orbits), so their state depends on campaign length, not their own age |
| GAP2-LONG-SESSION-LIMITS-06 | info | partially-confirmed | new | `src/core/ecs.hpp:31` | EnTT's 12-bit entity version wraps after 4,096 recycles of a slot; only an unenforced convention keeps handles from aliasing |
| GAP2-MEMORY-BUDGET-VS-0002-S13-12 | info | partially-confirmed | new | `src/core/world_map.hpp:57` | MOST_WORLD_PEOPLE caps a world at 100,000 people while maps are meant to be 2-50x the shipped one |
| GAP2-REPO-CONFIG-FILES-10 | info | confirmed | new | `.clang-tidy:41` | ExtraArgsBefore -Wno-unknown-pragmas hides pragma typos, with no comment |
| GAP2-REPO-CONFIG-FILES-11 | info | partially-confirmed | new | `dev:3` | dev wrapper breaks under a symlink and passes the caller's cwd, not the repo, to project_n |
| GAP2-REPO-CONFIG-FILES-12 | info | partially-confirmed | new | `dev.cmd:2` | dev.cmd: %* mangles & ^ % in arguments and no explicit exit code |
| GAP2-REPO-CONFIG-FILES-13 | info | partially-confirmed | new | `src/data/module.dox:1` | data/module.dox claims readers 'never throw' but manifest.cpp and world_map.cpp catch YAML::Exception inside |
| GAP2-REPO-CONFIG-FILES-14 | info | partially-confirmed | new | `src/platform/module.dox:1` | platform vs core both own a paths module |
| GAP2-REPO-CONFIG-FILES-15 | info | deliberate | known-deferred | `CHANGELOG.md:3` | CHANGELOG.md is 839 commits behind and the header says 'Do not edit' |
| GAP2-REPO-CONFIG-FILES-16 | info | confirmed | new | `.claude/marketplace/.claude-plugin/marketplace.json:1` | marketplace.json declares an owner name and no version or metadata |
| GD-COMBAT-SYSTEMS-23 | info | partially-confirmed | contradicts-recorded-decision | `assets/data/abilities.yaml:37` | The showcase special takes ~2 s from press to first impact, against 'fast combat' |
| GD-COMBAT-SYSTEMS-24 | info | confirmed | new | `src/core/abilities.hpp:105` | Stale docs: the affliction cadence is 'once a second', and chunk 4 notes name effect keys that no longer exist |
| GD-ROADMAP-SCOPE-23 | info | deliberate | known-deferred | `docs/superpowers/specs/2026-11-10-phase6-chunk11-settings-design.md:71` | Audio absent until the last phase; audio settings ship before any sound |
| GD-TOI-COVERAGE-22 | info | deliberate | known-deferred | `src/game/scenes/world.cpp:11` | Combat exists only in a dev scene; the core game loop can't be played yet |
| GD-TOI-COVERAGE-23 | info | unverifiable | new | `docs/superpowers/specs/2026-11-10-phase6-design.md:125` | Effort is going into map infrastructure before the game design exists |
| GD-WORLD-SIM-23 | info | partially-confirmed | new | `src/game/travel.cpp:253` | Weighted destination draw can saturate and favour low-index cities |
| GFX-COMBAT-DRAW-13 | info | deliberate | known-deferred | `src/gfx/volley_draw.hpp:54` | Swords and seals have hard per-frame caps (2,048 swords, 64 seals) and are laid out per item on the CPU; hidden swords still use room |
| GFX-COMBAT-DRAW-14 | info | partially-confirmed | new | `src/gfx/stamp_draw.cpp:164` | StampReach and LayStamp divide by a sprite's width and cast the result to u32 unchecked: undefined for a zero-width sprite |
| GFX-COMBAT-DRAW-15 | info | confirmed | new | `src/gfx/glow_table.cpp:19` | Glow record colour 0 means 'use the element colour', which a transparent palette colour would also produce |
| GFX-COMBAT-DRAW-16 | info | confirmed | new | `src/gfx/glow_table.cpp:50` | GlowTimeAt: fraction can round to 1.0F, alpha > 1 drops its fraction, NaN alpha gives a NaN fraction |
| GFX-COMBAT-DRAW-17 | info | partially-confirmed | new | `src/gfx/stamp_draw.cpp:337` | Mist puffs are a separate batch drawn over every stamp, so a far tile's puff covers nearer trees |
| GFX-COMBAT-DRAW-18 | info | confirmed | new | `src/gfx/seal_draw.cpp:671` | SealLookIndex duplicates core::Seals::Find's lower_bound |
| GFX-COMBAT-DRAW-19 | info | partially-confirmed | new | `src/gfx/cloud_draw.hpp:33` | cloud_draw.hpp uses std::size_t without including <cstddef>; glow_table.cpp's include blocks differ from the rest of gfx |
| GFX-COMBAT-DRAW-20 | info | confirmed | new | `src/gfx/seal_draw.cpp:707` | SealDrawing::tinySeals also counts negative and infinite sizes under 'tiny' |
| GFX-FEATURES-15 | info | unverifiable | new | `src/gfx/internal/features/sprites.cpp:325` | A cancelled cycled copy can leave the fog texture bound to undefined contents while it is counted as held |
| GFX-FEATURES-16 | info | confirmed | new | `src/gfx/internal/features/line_ribbons.hpp:14` | FogTexture lives in sprites.hpp; ground and line ribbons include the whole sprite renderer header for it |
| GFX-FEATURES-17 | info | confirmed | new | `src/gfx/internal/features/sprites.hpp:140` | Misleading names and comments in the feature layer |
| GFX-FEATURES-18 | info | confirmed | new | `src/gfx/internal/features/meshes.cpp:191` | Single-brace std::array initialisations, against the MSVC C5246 gotcha |
| GFX-FEATURES-19 | info | partially-confirmed | new | `src/gfx/internal/features/sprites.cpp:426` | SpriteRenderer::Draw rebinds the pipeline and re-pushes 80 bytes of uniforms for every batch |
| GFX-RENDERER-14 | info | partially-confirmed | new | `src/gfx/renderer.cpp:1633` | SetWorld recomputes RoadCurves and rebuilds every map-derived buffer on a look-only change, on the main thread |
| GFX-RENDERER-15 | info | confirmed | new | `src/gfx/renderer.cpp:1331` | Holds() walks the scene's sprite list about 6 times a frame, each time with a profile zone |
| GFX-RENDERER-16 | info | partially-confirmed | new | `src/gfx/renderer.cpp:900` | Per-frame rebuild of the named-label stack-width hash map |
| GFX-RENDERER-17 | info | confirmed | known-deferred | `src/gfx/renderer.cpp:199` | Renderer::Impl is a god object; label budgeting lives in the renderer |
| GFX-RENDERER-18 | info | partially-confirmed | new | `src/gfx/renderer.cpp:1672` | Capture() mutates persistent renderer state and is not safe between Acquire() and Draw() |
| GFX-RENDERER-19 | info | confirmed | new | `src/gfx/renderer.cpp:1393` | Frames in flight rely on SDL's default; the logs lack the GPU adapter name and driver version |
| GFX-RENDERER-20 | info | deliberate | contradicts-recorded-decision | `src/gfx/renderer.cpp:1377` | No player-facing present-mode or frame-cap choice |
| GFX-RENDERER-21 | info | partially-confirmed | new | `src/gfx/image.cpp:68` | SavePng and DifferingPixels trust Image::rgba's size |
| GFX-RENDERER-22 | info | partially-confirmed | new | `src/gfx/internal/resources/sprite_array.cpp:108` | Sprite array checks R8 colour-target support but not sRGB RGBA8 array support; sampler max_lod underflows on mipLevels 0 |
| GFX-RENDERER-23 | info | partially-confirmed | new | `src/gfx/renderer.cpp:594` | Terrain is laid out again after a failed submit even when the layout for the key is already there |
| GFX-RENDERER-24 | info | partially-confirmed | new | `src/gfx/renderer.hpp:101` | Draw() documentation misstates when instances upload |
| GFX-RENDERER-25 | info | confirmed | new | `src/gfx/renderer.cpp:718` | A seal key logged as missing is never logged again after a reload |
| GFX-TEXT-14 | info | confirmed | new | `src/gfx/shelf_packer.hpp:30` | Glyph cache and pages grow without bound; no eviction, cap or warning |
| GFX-TEXT-15 | info | confirmed | new | `src/gfx/text_layout.cpp:269` | Single font, glyph cache keyed by glyph index only, no script itemisation or bidi |
| GFX-TEXT-16 | info | confirmed | new | `src/gfx/renderer.cpp:1701` | Capture resets the layout budget to the default instead of restoring the previous one |
| GFX-TEXT-17 | info | unverifiable | new | `src/gfx/text_layout.cpp:297` | Vertical text and layout boxes depend on undocumented SDL3_ttf 3.2.2 behaviour |
| GFX-TEXT-18 | info | unverifiable | new | `src/gfx/text_layout.cpp:38` | Tiny fading labels can sample neighbouring glyph fields |
| GFX-TEXT-19 | info | confirmed | new | `src/gfx/text_layout.cpp:180` | Worker request queue pops from the front of a vector (O(n^2)) |
| GFX-TEXT-20 | info | confirmed | new | `src/gfx/text_layout.cpp:534` | Layout contents can change under a held reference if Flush() is called mid-frame |
| GFX-TEXT-21 | info | partially-confirmed | new | `src/gfx/shelf_packer.cpp:14` | Shelf packer wastes page space (exact-height shelves, abandoned page tails) |
| GFX-TEXT-22 | info | confirmed | new | `src/gfx/distance_field.hpp:9` | distance_field.hpp hard-codes 32/16 and uses the opposite sign convention to glyph fields |
| GFX-TEXT-23 | info | confirmed | new | `src/gfx/distance_field.cpp:21` | f32 FAR in the distance transform swallows small squared offsets |
| GFX-TEXT-24 | info | deliberate | new | `src/gfx/text_layout.cpp:538` | The layout budget counts all wall time since the frame's first make, not layout time |
| GFX-WORLD-DRAW-13 | info | unverifiable | new | `src/gfx/river_draw.cpp:117` | Rivers' join points use stored midlines of unmodified waypoints but the drawn lines of other rivers differ |
| GFX-WORLD-DRAW-14 | info | confirmed | new | `src/gfx/river_draw.hpp:37` | RiverStarts has no production caller |
| GFX-WORLD-DRAW-15 | info | confirmed | new | `src/gfx/ribbon_band.cpp:50` | Fade handling inconsistent between fade-in (capped at half the line) and fade-out (not capped) |
| GFX-WORLD-DRAW-16 | info | partially-confirmed | new | `src/gfx/ribbon.hpp:89` | Ribbon helper names are generic in gfx and `near`/`far` are Windows macros |
| GFX-WORLD-DRAW-17 | info | unverifiable | new | `src/gfx/ribbon.hpp:23` | Quad-index and vertex precision limits are not checked for very large worlds |
| PLATFORM-14 | info | unverifiable | new | `src/platform/window.cpp:207` | Impl destructor logs after SDL_Quit and when gamepad subsystem init failed |
| PLATFORM-15 | info | confirmed | new | `src/platform/window.cpp:148` | Per-event FamilyOf lookup and raw-axis events with no deadzone or normalised clamp for -32768 |
| PLATFORM-16 | info | partially-confirmed | new | `src/platform/window.cpp:356` | Window loses focus: held pad stick or buttons are not released |
| PLATFORM-17 | info | partially-confirmed | new | `src/platform/window.cpp:298` | Window tests cannot catch real-driver behaviour; Open/fullscreen/display logic is tested only on the dummy driver |
| PLATFORM-18 | info | confirmed | new | `src/platform/sdl_log.cpp:36` | SDL log routing: priorities forced to DEBUG, overriding SDL_LOGGING; callback is noexcept but logger may throw or block |
| PLATFORM-19 | info | partially-confirmed | new | `src/platform/window.cpp:132` | Wheel input drops horizontal scroll and uses fractional notches with no accumulation policy |
| SHADERS-10 | info | partially-confirmed | new | `shaders/sprite.vert.hlsl:198` | Absolute f32 world coordinates and one-texel-per-tile fields cap how large a procedural map can be drawn |
| SHADERS-11 | info | deliberate | new | `shaders/composite.frag.hlsl:14` | Composite clips HDR light per channel with no tone curve and no dither |
| SHADERS-12 | info | partially-confirmed | new | `src/gfx/internal/features/sprites.cpp:34` | Stale or incomplete cross-references in comments |
| SHADERS-13 | info | unverifiable | new | `shaders/sprite.frag.hlsl:16` | Duplicated helpers and constants across shaders |
| SHADERS-14 | info | unverifiable | new | `shaders/ground.frag.hlsl:186` | Ground frame-limited assumptions: grid-fade ordering and water-kind indices aren't defended in the shader |
| SHADERS-15 | info | partially-confirmed | new | `shaders/sprite.frag.hlsl:32` | Dissolve block pattern reshuffles every frame while the camera zooms |
| SHADERS-16 | info | confirmed | new | `shaders/AGENTS.md:3` | shaders/AGENTS.md omits the binding conventions every new shader must follow |
| SPECS-BUILT-A-10 | info | deliberate | new | `docs/superpowers/specs/2026-11-14-phase6-chunk5a-measure-camera-design.md:389` | 5a's budgets exist only as prose in a spec Outcome and are not machine-checked |
| SPECS-BUILT-A-11 | info | confirmed | new | `docs/superpowers/specs/2026-11-15-phase6-chunk5b2-moving-design.md:445` | Outcome figures drifted after the spec was written: travel share 60 now 58 |
| SPECS-BUILT-A-12 | info | confirmed | new | `docs/superpowers/specs/2026-11-17-warnings-design.md:10` | warnings Outcome's counts are already stale: '24 members in 10 files' vs 27 NOLINT lines in 12 files |
| SPECS-BUILT-A-13 | info | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk4-abilities-design.md:735` | Chunk 4 Outcome's profiling window ('1280x720') was shown wrong by 4b but never corrected |
| SPECS-BUILT-A-14 | info | deliberate | contradicts-recorded-decision | `docs/superpowers/specs/2026-11-10-phase6-chunk4-abilities-design.md:438` | Three chunk 4 commits do not build on MSVC and were deliberately left |
| SPECS-BUILT-A-15 | info | unverifiable | new | `docs/superpowers/specs/2026-11-10-phase6-chunk5-world-map-design.md:3` | The original chunk 5 world-map spec and the chunks 6 to 11 specs carry no note that the work was split or that their facts are unverified |
| SPECS-BUILT-A-16 | info | unverifiable | new | `docs/superpowers/specs/2026-11-13-history-first-design.md:146` | history-first Outcome records state of non-repo global files that cannot be verified and are already out of date |
| SPECS-BUILT-A-17 | info | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk3-text-design.md:227` | Chunk 3 Outcome cites commits by hash, in tension with the later 'cite by subject' rule |
| SPECS-BUILT-A-18 | info | partially-confirmed | new | `docs/superpowers/specs/2026-11-10-phase6-chunk3-text-design.md:418` | Chunk 3's text outcome lists ruling 47-50 core Jobs/TSan fixes outside the chunk scope without a test pointer for later regressions |
| SPECS-BUILT-B-14 | info | partially-confirmed | new | `src/game/npcs.cpp:173` | Relationships::Trim and Merge copy the whole table serially every month step |
| SPECS-BUILT-B-15 | info | confirmed | new | `docs/superpowers/plans/2026-11-24-river-heads.md:1` | All 12 plans in the range have no ticked checkboxes though every one is implemented |
| SPECS-BUILT-B-16 | info | unverifiable | new | `docs/superpowers/specs/2026-11-19-input-latency-design.md:31` | Specs cite gitignored evidence that cannot be rechecked |
| SPECS-BUILT-B-17 | info | unverifiable | new | `docs/superpowers/plans/2026-11-19-input-latency.md:180` | Input-latency spec's Design 8 lives inside the approved spec, but the plan orders Task 7 before Task 6 |
| SPECS-BUILT-B-18 | info | unverifiable | new | `docs/superpowers/specs/2026-11-23-phase6-chunk5b-look-travellers-design.md:81` | Travellers in a journey walk member 0's distance, so groups move in lockstep |
| SPECS-BUILT-B-19 | info | partially-confirmed | new | `docs/superpowers/specs/2026-11-19-input-latency-design.md:255` | Timeline-harness constants and "fails on old code" controls are described but their regression value is not guarded in CI |
| SPECS-ROADMAP-25 | info | unverifiable | new | `docs/superpowers/specs/2026-11-10-phase6-design.md:111` | Phase and chunk scope is far larger than the pace chunk 5 has shown |
| SPECS-ROADMAP-26 | info | partially-confirmed | new | `.claude/worktrees/shared-routes/docs/superpowers/specs/2026-11-24-shared-routes-design.md:85` | Shared-routes graph: within-sector edges grow with nodes squared as points of interest arrive |
| SPECS-ROADMAP-27 | info | deliberate | known-deferred | `docs/superpowers/specs/2026-11-10-phase6-chunk8-ui-design.md:116` | Chunk 8 bakes UI strings into strings.yaml under ui. names ahead of the planned localisation redesign |
| TESTS-CORE-12 | info | deliberate | new | `tests/unit/core/delta_tests.cpp:246` | Refused delta/order loads never checked for leaving the state untouched or half-applied |
| TESTS-CORE-13 | info | partially-confirmed | new | `tests/unit/core/ui_colours_tests.cpp:45` | Misleading or time-dependent test names and overloaded counting asserts |
| TESTS-GAME-21 | info | unverifiable | new | `tests/unit/game/scenes_tests.cpp:20` | Overlapping test cases between host_tests and scenes_tests |
| TESTS-GAME-22 | info | unverifiable | new | `tests/support/exactness.hpp:25` | Unlike() cannot tell that the compared games advanced; the ticks-0 pass relies on the caller |
| TESTS-GAME-23 | info | confirmed | new | `tests/unit/game/scenes_tests.cpp:93` | Default-sized Jobs used where the rest of the suite fixes worker counts |
| TESTS-GAME-24 | info | partially-confirmed | new | `tests/unit/game/scenes_tests.cpp:222` | Magic scene-list and scale assertions couple tests to incidental layout |
| TESTS-GAME-25 | info | partially-confirmed | new | `tests/unit/game/travel_tests.cpp:631` | Statistical assertions ('near > far * 2', share bands) depend on one fixed seed's draw |
| TESTS-REST-14 | info | deliberate | known-deferred | `tests/unit/app/cast_tests.cpp:123` | Cast tests encode keyboard and gamepad casts needing a mouse cursor |
| TESTS-REST-15 | info | partially-confirmed | new | `tests/support/saves.hpp:190` | ArenaSave hand-mirrors the Host save layout through 14 positional parameters |
| TESTS-REST-16 | info | partially-confirmed | new | `tests/unit/gfx/meshes_tests.cpp:15` | Tautological and third-party tests that pin constants rather than behaviour |
| TESTS-REST-17 | info | confirmed | new | `tests/unit/data/strings_tests.cpp:152` | Duplicate and boilerplate-heavy data tests |
| TESTS-REST-18 | info | deliberate | new | `tests/unit/data/seals_tests.cpp:22` | Parser tests assert exact full error text, bundled in table cases |
| TESTS-REST-19 | info | confirmed | new | `tests/unit/log_startup/fallback_tests.cpp:10` | log_startup and platform tests leave files in the working directory |
| TESTS-REST-20 | info | confirmed | new | `tests/fuzz/save.cpp:20` | Fuzz save target starts every input at the largest representable tick |
| TESTS-REST-21 | info | partially-confirmed | known-deferred | `tests/reference/reference_tests.cpp:2101` | Reference shimmer thresholds are tuned magic numbers with no drift guard |
| TOOLS-A-17 | info | partially-confirmed | new | `tools/project_n.py:166` | lock_entries parser is a hand-written Lua table reader with silent edge cases |
| TOOLS-A-18 | info | partially-confirmed | contradicts-recorded-decision | `tools/project_n.py:1317` | Platform-maths lint justifies itself with cross-platform determinism |
