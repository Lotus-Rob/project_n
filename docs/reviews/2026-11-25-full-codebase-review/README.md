# Full codebase review, 2026-11-25: executive report

Scope: the whole repository on `main` at "docs(design): point the river heads' branch rule at its outcome", plus the project memory's backlog and the in-flight `shared-routes` worktree's spec and stage 1 plan. 62 review units, 1,157 standing findings, 6 refuted. Nothing was built, run or measured (section 10). Every claim below cites the finding IDs it rests on; the unit files hold the evidence and each finding's verification note.

## 1. Verdict

project_n is a well-engineered engine with a world-map demo on top, not yet a game. The deterministic core (fixed-tick logic, counter-based random numbers, fixed-size job chunks, delta saves, replays with state hashes, a fail-sticky archive reader, fuzzed loaders) is among the most careful code a project of this age could show. No reviewer found a crash, undefined behaviour or desync on the paths shipped data exercises, and no finding stands at critical or high. The weaknesses are of four other kinds:

- **Silent failure.** Silent failure is the rule at runtime. There is no crash capture, exceptions on job workers terminate the game unlogged or use freed memory, fatal errors exit with status 1 and no message, saves are not durable, and a failed save is never shown (X-RESILIENCE-01..06, X-CONCURRENCY-01). The delta-save scheme trusts a hand-bumped `GENERATOR_VERSION` and never checks the regenerated base, so a save can load silently onto a different world (X-DETERMINISM-01).
- **Windows never run.** Windows, the genre's main platform, has never been run, and the code has at least one Windows-only start-up failure and one save-time terminate (X-UNTRUSTED-INPUT-01, X-UNTRUSTED-INPUT-02).
- **Built per map, not per view.** Almost every world-derived structure is whole-map rather than per view or per region, which contradicts the 2026-11-25 decision that maps are procedural and may be 2-50x larger (CORE-WORLD-01, GFX-WORLD-DRAW-01, X-PERFORMANCE-03). No generator design exists (GD-WORLD-SIM-01).
- **Process and design.** Since 2026-11-17, every feat commit has been world-map presentation or performance, while the game design, the player character, enemy threat and the core loop are unstarted (GD-ROADMAP-SCOPE-01, GD-TOI-COVERAGE-01, GD-COMBAT-SYSTEMS-01). The docs that agents load break the repo's own size rule by a factor of several (X-ARCHITECTURE-08).

In short, engineering health is high and project health is at risk. Fix the silent-failure and Windows items before building more. Then hold the game-design brainstorm and the procedural-generator design before chunk 6 or shared-routes stage 2.

## 2. Totals

### By severity, verdict and status

| | Count |
|---|---|
| Critical | 0 |
| High | 0 (44 findings were raised as high and downgraded on verification: section 3) |
| Medium | 107 |
| Low | 721 |
| Info | 329 |
| **Standing** | **1,157** |
| Refuted on verification | 6 (REFUTED-01.md) |

| Verdict | Count | | Status | Count |
|---|---|---|---|---|
| confirmed | 458 | | new | 1,034 |
| partially-confirmed | 475 | | known-deferred | 85 |
| unverifiable (from reading) | 133 | | contradicts-recorded-decision | 38 |
| deliberate | 91 | | | |

### By area

| Area | Units | Medium | Low | Info | Total |
|---|---|---|---|---|---|
| Core simulation and world (`core-world`, `core-journey-volley`, `core-sim`, `core-infra`) | 4 | 3 | 57 | 29 | 89 |
| Persistence (`core-persist`) | 1 | 1 | 16 | 7 | 24 |
| Rendering and shaders (`gfx-*`, `shaders`) | 6 | 4 | 69 | 48 | 121 |
| Game layer (`game-*`) | 5 | 2 | 57 | 36 | 95 |
| Data layer, assets and art tools (`data-*`, `assets-data`, `art-tools`) | 5 | 2 | 47 | 24 | 73 |
| App and platform (`app-*`, `platform`) | 3 | 3 | 32 | 14 | 49 |
| Tests (`tests-*`) | 3 | 5 | 39 | 15 | 59 |
| Tooling, build, CI (`tools-*`, `build-env`, `ci`) | 4 | 0 | 53 | 40 | 93 |
| Agent docs (`agent-docs`) | 1 | 0 | 9 | 7 | 16 |
| Design docs (`design-docs`) | 1 | 6 | 24 | 6 | 36 |
| Specs and plans (`specs-*`) | 3 | 6 | 40 | 18 | 64 |
| Game design (`gd-*`) | 4 | 30 | 57 | 6 | 93 |
| Cross-cutting (`x-*`: determinism, concurrency, untrusted input, performance, architecture, C++ safety, resilience, accessibility, text and l10n, test strategy) | 10 | 31 | 124 | 35 | 190 |
| Gap passes (`gap1-*`, `gap2-*`) | 12 | 14 | 97 | 44 | 155 |
| **Total** | **62** | **107** | **721** | **329** | **1,157** |

The cross-cutting units and the gap passes overlap the module units by design. Section 4 merges the duplicates. Counted once per underlying issue, the total falls by roughly 250.

## 3. Critical and high findings

**None standing.** No unit's verified findings stand at critical or high.

These 44 findings were raised as high and downgraded to medium or low on verification. Each one's verification note gives the reason, usually "real, but the shipped map is small", "a planning gap, not a defect" or "a recorded deferral":

- **Core:** GAME-TRAVEL-NPCS-01 (low).
- **Rendering:** GFX-WORLD-DRAW-01.
- **App:** APP-INPUT-UI-02 (low).
- **Tests:** TESTS-GAME-03, TESTS-REST-01 (low).
- **Game design:** GD-COMBAT-SYSTEMS-01, -02, -09 (low); GD-ROADMAP-SCOPE-01, -02, -03, -04, -05, -08, -10 (low); GD-TOI-COVERAGE-01, -02, -03; GD-WORLD-SIM-01, -02, -03.
- **Specs:** SPECS-ROADMAP-01, -02.
- **Cross-cutting:** X-ACCESSIBILITY-01, -02, -04; X-ARCHITECTURE-01, -02, -03, -05, -06; X-PERFORMANCE-01, -02, -03; X-RESILIENCE-01, -02, -03; X-TEST-STRATEGY-01 (low); X-UNTRUSTED-INPUT-01, -02.
- **Gap passes:** GAP1-SCENE-TRANSITION-FLOW-01; GAP2-MEMORY-BUDGET-VS-0002-S13-01, -02; GAP2-REPO-CONFIG-FILES-01.

Two assessment texts still use the reviewers' original wording: gap1-scene-transition-flow says "one finding is high", and x-untrusted-input says "Windows path encoding (high)". Both findings stand at medium.

## 4. Duplicate clusters

Each cluster names one canonical ID (the most severe, or the most complete when severities match), then the other findings that describe the same issue. Where severities disagree, the canonical severity applies.

### Saves, replays and determinism

| Canonical | Same issue | Note |
|---|---|---|
| X-DETERMINISM-01 (m) delta saves apply to a regenerated base that is never verified | DESIGN-DOCS-04 (m), GD-WORLD-SIM-02 (m), CORE-PERSIST-02, GAP1-SIM-BOUNDARY-COMPLETENESS-01, GD-ROADMAP-SCOPE-12, GD-WORLD-SIM-05, GAME-TRAVEL-NPCS-07 | `GENERATOR_VERSION` is 1 (checked: `src/game/world_state.hpp:30`). The scratch base has no identity at all (GAP1-SIM-BOUNDARY-COMPLETENESS-01). |
| X-DETERMINISM-05 no golden-value change detector | TESTS-GAME-19, X-TEST-STRATEGY-13, TESTS-CORE-01 (m), TESTS-CORE-05 | Contradicts a recorded decision (section 7). |
| X-DETERMINISM-02 replays pin only the version string | CORE-PERSIST-22, GAP1-SIM-BOUNDARY-COMPLETENESS-08, X-UNTRUSTED-INPUT-09 | |
| CORE-SIM-03 Restore accepts any tick counters | CORE-PERSIST-17, X-UNTRUSTED-INPUT-07 | |
| CORE-PERSIST-18 replay `ReadTicks` reserve (Audit 2) | X-UNTRUSTED-INPUT-20, GAP1-SWEEP-UP-BACKLOG-VS-CODE-06, TESTS-CORE-07 | Rated info, low, low, low across the four. Related amplification paths: CORE-PERSIST-04, X-UNTRUSTED-INPUT-03, -04, -08. |
| CORE-PERSIST-19 Hasher UB for scalars over 8 bytes | X-CPP-SAFETY-11, X-UNTRUSTED-INPUT-16 | |
| CORE-PERSIST-13 `Handover::ReturnState` unbounded | X-CONCURRENCY-11, X-CPP-SAFETY-13 | |
| CORE-PERSIST-21 saves and replays share one magic | X-UNTRUSTED-INPUT-22 | |
| X-RESILIENCE-05 (m) save write not durable | DESIGN-DOCS-05 (m), CORE-PERSIST-10, X-RESILIENCE-09, X-UNTRUSTED-INPUT-12 | |
| DATA-YAML-CORE-01 (m) `SaveSettings` drops `edge_pan` | X-ACCESSIBILITY-05 (m), DATA-YAML-CORE-02, X-UNTRUSTED-INPUT-11 | Checked: `settings.cpp` reads `edge_pan` (line 137) and never writes it. The in-place rewrite is DATA-YAML-CORE-08 / X-RESILIENCE-10. |
| GAP1-SIM-BOUNDARY-COMPLETENESS-02 every load regenerates the whole world | GAME-HOST-A-05, X-PERFORMANCE-19, DESIGN-DOCS-24, GAME-HOST-A-14 | |
| GAME-HOST-A-03 dev-scene save keeps its whole scene as scratch base | GAP2-MEMORY-BUDGET-VS-0002-S13-09 | |
| GAME-HOST-B-04 `SetData` with a new map keeps the old world and base | GAP1-SIM-BOUNDARY-COMPLETENESS-04 | |
| GAME-HOST-B-09 loaded routes need not be walkable | GD-WORLD-SIM-22, X-UNTRUSTED-INPUT-19 | |
| GAME-TRAVEL-NPCS-05 `CellAt` float-to-u32 UB | X-CPP-SAFETY-06, X-DETERMINISM-06 | |
| X-CPP-SAFETY-03 `MakeTileLeg` u32 arrive ticks wrap | CORE-JOURNEY-VOLLEY-09 (Audit 13) | |
| X-DETERMINISM-03 combat reads manifest `footY`, outside every hash | GD-TOI-COVERAGE-21 | Related: GAP2-COORDINATE-SPACES-AND-PICKING-09. |
| X-DETERMINISM-04 FP environment unchecked on main thread and workers | CORE-INFRA-04, X-CONCURRENCY-04, X-CPP-SAFETY-08, DESIGN-DOCS-25, APP-MAIN-13, X-CONCURRENCY-17 | |
| X-DETERMINISM-07 `Paths::To` lazily written from parallel chunks | CORE-WORLD-07, X-CONCURRENCY-07, GAME-TRAVEL-NPCS-11 | |
| GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-01 (m) `Host::Load` refuses silently | GAME-HOST-A-04, GAP1-SWEEP-UP-BACKLOG-VS-CODE-07, CORE-PERSIST-11 | |
| X-PERFORMANCE-10 replay hashes the whole world every second | X-DETERMINISM-08, GAP1-SIM-BOUNDARY-COMPLETENESS-10 | |
| CORE-SIM-02 recording lost on quit, unbounded in memory | DESIGN-DOCS-30, GAP2-MEMORY-BUDGET-VS-0002-S13-08 | |
| CORE-SIM-08 presses dropped on frozen ticks | GAP1-HOT-RELOAD-FLOW-02, GAP1-SCENE-TRANSITION-FLOW-04, GAP1-SIM-BOUNDARY-COMPLETENESS-05 | |
| CORE-SIM-20 data reload during a replay's start change | GAP1-SCENE-TRANSITION-FLOW-08 | |
| GAME-HOST-B-12 stop mid-month leaves a half-stepped month | X-DETERMINISM-14 | |

### Crash, exceptions and runtime failure

| Canonical | Same issue |
|---|---|
| X-RESILIENCE-02 (m) exceptions in job chunks: terminate unlogged, and use-after-free on the forking thread | DESIGN-DOCS-06 (m), X-CONCURRENCY-01, X-CPP-SAFETY-07, X-UNTRUSTED-INPUT-10, GAP2-REPO-CONFIG-FILES-02 |
| X-RESILIENCE-01 (m) no crash handler, async log lost | CORE-INFRA-01 (m), DESIGN-DOCS-14, X-RESILIENCE-14, APP-MAIN-11 |
| X-RESILIENCE-04 (m) fatal runtime failures exit 1 with no message | APP-MAIN-01 (m), GD-ROADMAP-SCOPE-08 (m, with no autosave), GAP1-SCENE-TRANSITION-FLOW-09 |
| X-RESILIENCE-03 (m) device loss never fatal or recovered | GFX-RENDERER-03 |
| X-CPP-SAFETY-04 release builds drop every invariant check | BUILD-ENV-01, GAP1-THIRD-PARTY-LIBRARY-USAGE-12, CORE-PERSIST-23, GFX-FEATURES-04 |
| CORE-INFRA-08 `.at()` throws despite "we throw nothing" | CORE-SIM-12, GAME-TRAVEL-NPCS-19 |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-11 log file has no size cap | CORE-INFRA-17, X-UNTRUSTED-INPUT-14 |
| APP-MAIN-02 data race on `m_options` | X-CONCURRENCY-06 |
| APP-MAIN-09 DataWatch can throw from directory iteration | X-UNTRUSTED-INPUT-18 |

### Windows and platform

| Canonical | Same issue |
|---|---|
| X-UNTRUSTED-INPUT-01 (m) font path given to SDL_ttf in the ANSI code page | X-CPP-SAFETY-02 (m), X-TEXT-L10N-02 (m) |
| X-UNTRUSTED-INPUT-02 (m) `path.string()` in log and error arguments throws on Windows | X-CPP-SAFETY-01 (m), CORE-PERSIST-05, DATA-YAML-CORE-03 |
| PLATFORM-01 (m) no HiDPI | X-ACCESSIBILITY-12, GFX-TEXT-04, APP-INPUT-UI-12 |
| PLATFORM-02 gamepads merged into one stream | X-ACCESSIBILITY-06 |
| SHADERS-01 only SPIR-V verified; real back ends never drawn | TESTS-REST-06, X-TEST-STRATEGY-02, X-TEST-STRATEGY-12, GD-ROADMAP-SCOPE-05 (m, known-deferred) |
| GFX-RENDERER-12 MSL fallback unchecked | SHADERS-09 |

### Scaling to procedural maps

| Canonical | Same issue | Note |
|---|---|---|
| CORE-WORLD-01 (m) distance map per (city, profile) | X-ARCHITECTURE-03 (m), X-PERFORMANCE-01 (m), GD-WORLD-SIM-04 (m, known-deferred), GAME-TRAVEL-NPCS-01, GAME-TRAVEL-NPCS-06 | The in-flight shared-routes branch addresses it. |
| CORE-WORLD-02 (m) whole-map A* per click on the logic thread | GAME-HOST-B-03, X-PERFORMANCE-06, GD-WORLD-SIM-11 | |
| GFX-WORLD-DRAW-01 (m) fixed 8,192-quad river and road buffers | GFX-RENDERER-01 (m), X-PERFORMANCE-02 (m), GFX-FEATURES-03 | Not silent: a warning is logged once. The cap is a recorded choice (verification of GFX-WORLD-DRAW-01). |
| X-PERFORMANCE-03 (m) fog work over the whole map | X-ARCHITECTURE-04 (m), APP-INPUT-UI-04, SPECS-BUILT-B-02, GD-WORLD-SIM-10, GAME-HOST-B-06, GAP2-MEMORY-BUDGET-VS-0002-S13-11, X-PERFORMANCE-16, X-PERFORMANCE-17 | |
| X-PERFORMANCE-22 one texel per tile caps map size at the GPU texture limit | GFX-FEATURES-08, SHADERS-10 | |
| CORE-INFRA-02 f32 world coordinates | X-DETERMINISM-09, GAP2-COORDINATE-SPACES-AND-PICKING-04 | |
| X-PERFORMANCE-04 (m) `PublishJourneys` walks every journey | GAME-HOST-B-02, GD-WORLD-SIM-12 | |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-02 (m) publish builds every instance before culling | X-PERFORMANCE-05 | |
| X-PERFORMANCE-13 `GroundTexels` 121 reads per tile on the main thread | GFX-FEATURES-09, GFX-WORLD-DRAW-02 | |
| CORE-WORLD-04 road joins O(roads x segments), computed twice | X-PERFORMANCE-12, GFX-RENDERER-14 | |
| X-PERFORMANCE-07 bridge heading scans every road point | GFX-WORLD-DRAW-03 | |
| CORE-JOURNEY-VOLLEY-04 sword plan recomputed ~12 times a frame | X-PERFORMANCE-08, CORE-JOURNEY-VOLLEY-03, GD-COMBAT-SYSTEMS-15 | |
| X-PERFORMANCE-11 whole atlas resident, mips regenerated per scene load | GFX-RENDERER-07 | |
| X-PERFORMANCE-14 full snapshot published and uploaded every tick while paused | X-CONCURRENCY-12, CORE-SIM-09, GAP1-CLOCK-DISCIPLINE-PAUSE-06, X-PERFORMANCE-23 | |
| CORE-INFRA-14 draw list copies instances twice or three times | X-PERFORMANCE-26, GAP2-MEMORY-BUDGET-VS-0002-S13-06 | |
| APP-MAIN-06 hover scans every instance every frame | X-PERFORMANCE-27 | |
| GAME-TRAVEL-NPCS-02 `ChooseDestination` scans places twice | X-PERFORMANCE-20 | |
| X-CONCURRENCY-09 world generation single-threaded | X-PERFORMANCE-19 | |
| GAME-COMBAT-11 `Holds` scans per ending use | X-PERFORMANCE-15 | |
| GFX-RENDERER-15 `Holds()` about 6-7 times a frame | X-PERFORMANCE-28 | |
| CORE-JOURNEY-VOLLEY-10 journeys copy routes several times | X-PERFORMANCE-24 | |
| GAP2-MEMORY-BUDGET-VS-0002-S13-03 relationships keep a stale full copy | SPECS-BUILT-B-14 | |
| SPECS-BUILT-B-13 walk-table cache cleared each month | GAME-HOST-B-11 | |
| GD-TOI-COVERAGE-15 world capped at 100,000 people | GAP2-MEMORY-BUDGET-VS-0002-S13-12, GD-ROADMAP-SCOPE-16 | |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-01 (m) Tracy callstacks and fibers on | GAP1-THIRD-PARTY-LIBRARY-USAGE-05, GAP2-LONG-SESSION-LIMITS-03 | |

### Procedural map, test-map coupling and roadmap

| Canonical | Same issue |
|---|---|
| GD-WORLD-SIM-01 (m) no generator design; the architecture assumes one data-defined map per process | GD-ROADMAP-SCOPE-04 (m), SPECS-ROADMAP-01 (m), X-ARCHITECTURE-02 (m), X-ARCHITECTURE-22, GAP1-SWEEP-UP-BACKLOG-VS-CODE-02 (m) |
| GD-ROADMAP-SCOPE-03 (m) approved chunk specs predate the game design and the procedural decision | GD-TOI-COVERAGE-09 (m), SPECS-ROADMAP-12 |
| GD-WORLD-SIM-18 constants fixed to the test map | GAME-SCENES-04, GAME-SCENES-15, GD-WORLD-SIM-16, DATA-WORLD-01, GAME-HOST-A-06, GD-WORLD-SIM-15, ASSETS-DATA-09, ART-TOOLS-02 |
| SPECS-ROADMAP-03 (m) phase table stale | GD-ROADMAP-SCOPE-13, GD-TOI-COVERAGE-19 |
| GD-ROADMAP-SCOPE-02 (m) no vertical slice; combat exists only in a dev scene | GD-TOI-COVERAGE-22 |
| GD-ROADMAP-SCOPE-11 min-spec GPU never measured | SPECS-BUILT-B-03 |
| DESIGN-DOCS-34 DragonBones unproven, S14 unrun | GD-ROADMAP-SCOPE-18 |
| DESIGN-DOCS-28 uncapped rendering by default | GFX-RENDERER-20, GAP1-CLOCK-DISCIPLINE-PAUSE-02 (m) |
| DESIGN-DOCS-33 non-goals contradict modding and localisation plans | GD-ROADMAP-SCOPE-17 |
| GD-WORLD-SIM-07 no years or seasons, no time-skip | ASSETS-DATA-07, X-TEXT-L10N-13, GD-TOI-COVERAGE-13 |
| GD-ROADMAP-SCOPE-19 backlog lives outside the repo | AGENT-DOCS-07, AGENT-DOCS-16 |
| GD-ROADMAP-SCOPE-15 process overhead | AGENT-DOCS-15 |
| SPECS-ROADMAP-10 gamepad Back button has three jobs | X-ACCESSIBILITY-10 |
| SPECS-BUILT-A-08 plans carry no completion marker | SPECS-BUILT-B-15 |

### Game systems and combat

| Canonical | Same issue |
|---|---|
| GD-TOI-COVERAGE-07 (m) `healing` damages enemies; the `burn:`/`poison:` key is ignored | GAME-COMBAT-02, GAME-COMBAT-03, GD-COMBAT-SYSTEMS-12, GD-COMBAT-SYSTEMS-13. Kind fields ignored: GD-COMBAT-SYSTEMS-14, GAME-COMBAT-04 |
| GD-COMBAT-SYSTEMS-02 (m, known-deferred) closed ability enum and hard-wired creatures | GAME-COMBAT-10, X-ARCHITECTURE-06 (m), GD-TOI-COVERAGE-06 (m), GD-COMBAT-SYSTEMS-17, ASSETS-DATA-01, X-ARCHITECTURE-20, X-ARCHITECTURE-17. Compiled SpriteId: GD-TOI-COVERAGE-08 (m) |
| GD-TOI-COVERAGE-03 (m) no free projectiles or collision | GD-COMBAT-SYSTEMS-05 (m), GD-COMBAT-SYSTEMS-06 (m), DESIGN-DOCS-21 |
| GD-WORLD-SIM-03 (m) unbounded power growth | GD-TOI-COVERAGE-04 (m), GD-WORLD-SIM-14. Consequence: GAP2-LONG-SESSION-LIMITS-02 |
| GAME-TRAVEL-NPCS-04 NPC tier machinery dead in production | GD-TOI-COVERAGE-12, GD-WORLD-SIM-08, X-ARCHITECTURE-07 (m), GD-WORLD-SIM-17, X-PERFORMANCE-21 |
| GD-COMBAT-SYSTEMS-04 (m) `HitAmount` has no caster or target | GD-TOI-COVERAGE-17 |
| GAME-COMBAT-14 zero cooldown shares a use ID | GD-COMBAT-SYSTEMS-21 |
| GAME-COMBAT-08 random purposes coordinated by comment | CORE-INFRA-11, X-DETERMINISM-13 |
| GAME-HOST-A-08 ability-use source numbers spread over files | GAME-SCENES-11 |
| CORE-SIM-04 aura range wraps to 0 | GAME-SCENES-06 |
| CORE-SIM-10 affliction doc says "once a second" | GD-COMBAT-SYSTEMS-24 |
| GAME-SCENES-01 orbits spawn at tick 0 | GAP2-LONG-SESSION-LIMITS-01, GAP2-LONG-SESSION-LIMITS-05, GAP1-CLOCK-DISCIPLINE-PAUSE-08 |
| SHADERS-05 glow age loses its fraction from 2^23 ticks | GAP1-CLOCK-DISCIPLINE-PAUSE-08 (same root: absolute-tick clocks) |
| X-TEXT-L10N-12 people names collide | ASSETS-DATA-04 |

### Architecture

| Canonical | Same issue |
|---|---|
| X-ARCHITECTURE-01 (m) `game::Host` god object | GAME-HOST-A-10, GD-TOI-COVERAGE-14, X-ARCHITECTURE-13, GAME-HOST-A-16 |
| X-ARCHITECTURE-15 `Renderer::Impl` monolith | GFX-RENDERER-17 |
| X-ARCHITECTURE-05 (m) scenes are a parameterless singleton ring | GAME-SCENES-05, SPECS-ROADMAP-22 |
| X-ARCHITECTURE-10 `core` mixes kernel and game domain | CORE-INFRA-19, CORE-SIM-19 |
| X-ARCHITECTURE-18 ECS facade is the raw registry | GAP1-THIRD-PARTY-LIBRARY-USAGE-15 |
| APP-MAIN-05 command-line parsing duplicated | X-ARCHITECTURE-21, X-UNTRUSTED-INPUT-17 |
| GAP1-HOT-RELOAD-FLOW-13 `shared_ptr<GameData>` in every Action | DESIGN-DOCS-36, GAP2-DESIGN-DOCS-0002X-DRIFT-11 |
| CORE-WORLD-11 `WorldLook::Hash` kept by hand | GAP1-HOT-RELOAD-FLOW-10 |
| CORE-INFRA-09 enum counts kept by hand | GAP2-REPO-CONFIG-FILES-04, X-CPP-SAFETY-10, GAME-COMBAT-09, GAME-TRAVEL-NPCS-09, CORE-SIM-17 |
| GAME-SCENES-03 scene sprite lists unchecked | ART-TOOLS-08 |
| CORE-WORLD-08 `FindRoute` test-only in the library | SPECS-BUILT-B-11 |

### Input, presentation and accessibility

| Canonical | Same issue |
|---|---|
| GAP1-SCENE-TRANSITION-FLOW-01 (m) snapshot ID `(epoch, wallTick)` reused across a scene change | GFX-RENDERER-02 (rated low there) |
| X-ACCESSIBILITY-01 (m) stick state stuck after a pad is removed | APP-INPUT-UI-02, APP-INPUT-UI-03, PLATFORM-16, APP-INPUT-UI-10, GAP1-CLOCK-DISCIPLINE-PAUSE-07 |
| X-ACCESSIBILITY-02 (m, known-deferred) no keyboard-only or gamepad casting | APP-INPUT-UI-01 (m), GD-COMBAT-SYSTEMS-08 (m), TESTS-REST-14 |
| X-TEXT-L10N-03 (m) HUD names mouse and keyboard only | APP-INPUT-UI-05 |
| X-ACCESSIBILITY-03 (m) Escape quits instantly; no gamepad quit | APP-MAIN-10 |
| GAP1-CLOCK-DISCIPLINE-PAUSE-01 (m) P pause leaves the simulation tick running | GAP1-CLOCK-DISCIPLINE-PAUSE-04, -03, X-ACCESSIBILITY-04 (m, combat can't be paused) |
| GAME-HOST-A-01 (m) held movement leaves the map; load accepts any map place | GAME-HOST-B-05, SPECS-BUILT-B-10, GD-WORLD-SIM-06 |
| CORE-INFRA-03 `WIDEST_HALF_VIEW` not the widest | GAP2-COORDINATE-SPACES-AND-PICKING-12 |
| GFX-RENDERER-09 labels laid out while a scene loads | GAP1-SCENE-TRANSITION-FLOW-06 |
| GAP1-HOT-RELOAD-FLOW-03 seal keys never cross-checked; missing keys logged once | GFX-RENDERER-25 |
| GFX-TEXT-07 NaN anchor reaches a float-to-size_t cast | X-CPP-SAFETY-09 |
| GFX-TEXT-19 glyph queue pops from the front | X-CONCURRENCY-13 |
| X-TEXT-L10N-06 three placeholder fillers | GFX-TEXT-10 |
| X-TEXT-L10N-04 one font, no fallback | GFX-TEXT-15 |

### Data, tests, tooling and docs

| Canonical | Same issue |
|---|---|
| DATA-YAML-CORE-12 duplicate YAML keys silently accepted | DATA-COMBAT-05, DATA-WORLD-04 |
| X-UNTRUSTED-INPUT-05 manifest page geometry unvalidated; u32 transfer size | DATA-YAML-CORE-13, DATA-YAML-CORE-14 |
| GFX-FEATURES-01 (m, known-deferred) quad indices marked uploaded at record time (Audit 1) | GAP1-SWEEP-UP-BACKLOG-VS-CODE-05 |
| SHADERS-02 HLSL layouts tied to C++ by comment | GFX-FEATURES-02 |
| CORE-PERSIST-07 fuzz targets skip the container, Restore and round trips; ASan only | TESTS-REST-04, X-UNTRUSTED-INPUT-06, X-CPP-SAFETY-05, TESTS-REST-20 |
| TESTS-GAME-04 (m) committed fuzz seeds never read | CORE-PERSIST-06, TESTS-REST-05 |
| X-TEST-STRATEGY-01 sanitizers, fuzz and bench not in CI | CORE-PERSIST-08, X-CONCURRENCY-05, X-TEST-STRATEGY-08, X-UNTRUSTED-INPUT-24, CI-05 |
| X-TEST-STRATEGY-04 app, main and the input path untested | APP-MAIN-03, GAP1-HOT-RELOAD-FLOW-11, GAP1-SCENE-TRANSITION-FLOW-05, TESTS-REST-07, GFX-RENDERER-05 |
| TESTS-REST-01 reference tolerance looser than its evidence | X-TEST-STRATEGY-03, TESTS-REST-21, SPECS-BUILT-B-09 |
| TESTS-GAME-10 shipped-data balance pinned in unit tests | SPECS-BUILT-B-07, TESTS-REST-11, TESTS-GAME-16 |
| TESTS-GAME-09 fixed temp names collide across parallel runs | TESTS-REST-02, TESTS-CORE-08, APP-MAIN-14, TOOLS-A-22 |
| TESTS-GAME-01 (m) replay sessions build the 50,000-NPC world | TESTS-GAME-05, TESTS-GAME-06 |
| TESTS-GAME-08 `ArenaSave` with 14 positional parameters | TESTS-REST-15 |
| GAME-COMBAT-12 test name mismatch | TESTS-GAME-15 |
| CORE-INFRA-06 sweeps against tests/AGENTS.md | TESTS-CORE-10 |
| CORE-INFRA-18 draw-list tests use one worker | TESTS-CORE-04 |
| X-ARCHITECTURE-08 (m) src/AGENTS.md 45 KB with 15,404-character lines | DESIGN-DOCS-02 (m), GD-ROADMAP-SCOPE-07 (m), AGENT-DOCS-02, CORE-INFRA-20, SPECS-BUILT-A-09, GAME-SCENES-13, TOOLS-A-20 |
| TOOLS-B-15 project_n.py is 3.4k lines | TOOLS-A-13 |
| DESIGN-DOCS-03 (m) 0002 presents unbuilt rules as followed | X-ARCHITECTURE-12, DESIGN-DOCS-21, DESIGN-DOCS-22, DESIGN-DOCS-25, DESIGN-DOCS-29 |
| X-ARCHITECTURE-19 0002 header and module table drift | DESIGN-DOCS-18, DESIGN-DOCS-19, AGENT-DOCS-04 |
| GAP2-MEMORY-BUDGET-VS-0002-S13-02 (m) no memory statistic exists | DESIGN-DOCS-22 |
| X-CONCURRENCY-10 Saves docs name the wrong thread | X-UNTRUSTED-INPUT-23, GAP2-DESIGN-DOCS-0002X-DRIFT-10 |
| AGENT-DOCS-01 doc examples use scopes commit-check rejects | DESIGN-DOCS-09 |
| BUILD-ENV-02 no CET, stack-clash or explicit PIE | DESIGN-DOCS-15 |
| BUILD-ENV-22 AGPL compatibility undocumented | GD-ROADMAP-SCOPE-20 |
| CI-06 `cancel-in-progress` on main | CI-02, DESIGN-DOCS-17 |
| CI-01 release doesn't check the tag passed the gate | TOOLS-B-08 |
| TOOLS-A-04 `tidy_key` repeats work per unit | TOOLS-B-02 |
| TOOLS-A-02 `NOLINT(*)` passes | TOOLS-B-05 |
| TOOLS-A-07 text lints ignore block comments and raw strings | TOOLS-B-04 |

## 5. Themes: recurring root causes

1. **Built per map, not per view or region.** Distance maps, ribbons, fog, the ground field, road curve joins, fog textures, journey publishing and click routing all scale with the whole map. The test map is 150x85 = 12,750 tiles (checked). The x-performance assessment's "150x90" and "14k tiles" are wrong. Section 4's scaling table lists the evidence. The procedural decision turns every one of these into a cliff, and GAP2-MEMORY-BUDGET-VS-0002-S13-01 shows that together they already break the 50 MB game-state line at the shipped scale after 1-10 h.
2. **Runtime failure is log-only.** Start-up failures are handled well, with a dialog. Everything after start-up only logs: crashes, worker exceptions, device loss, a failed save, a refused load and the logic thread dying (X-RESILIENCE-01..06, GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-01, -02). A player's bug report would carry no version, OS or GPU name.
3. **Identity by version number, not by content.** `GENERATOR_VERSION`, the replay's semantic version, positional SpriteIds and the unhashed manifest `footY` all trust a human to bump or align something (X-DETERMINISM-01, X-DETERMINISM-02, DESIGN-DOCS-23, X-DETERMINISM-03, CORE-PERSIST-03).
4. **Invariants held by convention.** Hand-kept lists keep invariants: ComponentList (CORE-SIM-06), the Snapshot reset (CORE-SIM-05), `WorldLook::Hash` (CORE-WORLD-11), enum counts (CORE-INFRA-09), `ACTION_KINDS` (CORE-SIM-17), the test copies of the save layout (TESTS-GAME-03), scene sprite lists (GAME-SCENES-03) and C++/HLSL mirrors (SHADERS-02). Comment-only thread rules do the same (X-CONCURRENCY-18). So do `PN_ASSERT`s that compile out in release while guarding dereferences (X-CPP-SAFETY-04). The if-chain rule removes the compiler's enum exhaustiveness check (X-CPP-SAFETY-10).
5. **Windows, macOS and real GPUs never run.** Path encoding (X-UNTRUSTED-INPUT-01, -02), DXIL and MSL paths (SHADERS-01), HiDPI (PLATFORM-01) and a min-spec GPU (GD-ROADMAP-SCOPE-11) have never been exercised, and only lavapipe draws in tests (X-TEST-STRATEGY-02).
6. **Accessibility exists as hooks the player can't reach.** The colour table, text scale and reduce-motion and reduce-flashing levers exist but have no settings fields (X-ACCESSIBILITY-05, GAP1-HOT-RELOAD-FLOW-09). Combat needs the mouse (X-ACCESSIBILITY-02, GAP2-COORDINATE-SPACES-AND-PICKING-01, -02). Specs omit the required Accessibility section (SPECS-ROADMAP-02). The reduce-motion lever changes seal lifetimes (GFX-COMBAT-DRAW-01).
7. **The demo's shape is hard-wired.** One player caster, foxes as the only enemy, one global loadout in hashed data, a parameterless scene ring and a throwaway player entity (X-ARCHITECTURE-05, -06, GD-TOI-COVERAGE-01, -02, GD-COMBAT-SYSTEMS-02).
8. **Presentation clocks and the simulation tick are tangled.** The P pause stops the world clock but not the simulation tick (GAP1-CLOCK-DISCIPLINE-PAUSE-01). Presentation phases count from absolute tick 0 (GAP2-LONG-SESSION-LIMITS-01, -05). Presentation-only types live in `core` and mutate inside the deterministic tick (CORE-SIM-19, X-ARCHITECTURE-09). Per-scene presentation state resets on five different keys (GAP1-SCENE-TRANSITION-FLOW-05).
9. **Untrusted input still amplifies.** The reader caps by bytes left, but a 1-byte action becomes 56 bytes (CORE-PERSIST-04), plans rebuild whole routes before checks (X-UNTRUSTED-INPUT-04), targets resize to the claimed count (X-UNTRUSTED-INPUT-03), and the 1 GiB cap is reachable from about 32 KiB (X-UNTRUSTED-INPUT-08). Fuzzing runs under ASan only and on request only (CORE-PERSIST-07, X-TEST-STRATEGY-08).
10. **Duplication instead of shared abstractions.** Three number-reading disciplines in the data readers (DATA-COMBAT-02), five private refusal helpers (DATA-YAML-CORE-11), three placeholder fillers (X-TEXT-L10N-06), five copies of command-line parsing (APP-MAIN-05), duplicated pipelines and held-once-submitted logic (GFX-FEATURES-06, -10), and duplicated test helpers with different meanings (TESTS-GAME-02).
11. **Docs drift and density.** AGENTS.md files hold paragraphs of 5-15 KB (X-ARCHITECTURE-08). 0002 describes unbuilt machinery (DESIGN-DOCS-03) and 0001 a removed process (DESIGN-DOCS-01). The phase table is stale (SPECS-ROADMAP-03), plans carry no completion marker (SPECS-BUILT-A-08) and Outcome numbers drift (SPECS-BUILT-A-11, -12, -13). The backlog has no status column (GAP1-SWEEP-UP-BACKLOG-VS-CODE-03).
12. **Gates are strong for logic, weak elsewhere.** `check` gates logic well. Sanitizers, fuzzing, the bench, long runs, coverage thresholds, real GPUs and the app loop are outside every gate (X-TEST-STRATEGY-01, -04, -06, -07, TESTS-REST-08).
13. **Work order puts presentation and scale ahead of design.** World-map polish, river heads, nine-ring seals and hierarchical routing for maps nothing generates precede any core loop (GD-ROADMAP-SCOPE-01, -14, GD-TOI-COVERAGE-23).

## 6. Assessments

### Architecture
The module DAG is real and lint-enforced, with no upward include anywhere. The two-thread model shares only `GameHandover`, and the frame contract is plain data (x-architecture). The shape is still a test bed's, though:
- `game::Host` is about 2,460 lines holding simulation, save and hash schema, player control, routing, fog and presentation caches (X-ARCHITECTURE-01).
- `Renderer::Impl` has about 110 members (X-ARCHITECTURE-15).
- `App` mixes the loop, input routing, hot reload and profiling (X-ARCHITECTURE-14).
- `core` is both the engine kernel and the game's whole vocabulary (X-ARCHITECTURE-10).

Every planned system would land in one of these. Scenes can't take parameters (X-ARCHITECTURE-05). The map is immutable, loaded once by `main` and shared with the renderer (X-ARCHITECTURE-02, X-ARCHITECTURE-22). The world map has no active NPC tier (X-ARCHITECTURE-07). View models drifted from 0002 A13 into ad hoc Snapshot fields (X-ARCHITECTURE-11). Debug controls are bound in release (X-ARCHITECTURE-16). The x-architecture list (a)-(g) is the restructuring the code needs before cultivation, sects, items or towns.

### Core simulation and determinism
Within one binary, determinism is solid and tested on 1 against N workers. RNG is counter-based, the maths is written in-house with exact reductions, orders are total and storage order survives a save (x-determinism, core-sim). The gaps sit at the edges:
- The FP environment is checked only on the logic thread, by an assert that release builds drop, although the main thread runs month-step chunks (X-DETERMINISM-04).
- `Paths::To` is written lazily from parallel chunks (X-DETERMINISM-07).
- Combat reads the atlas manifest (X-DETERMINISM-03).
- Nothing detects a change to what the simulation produces (X-DETERMINISM-05, by recorded decision).
- `CellAt` and `MakeTileLeg` carry float-to-int and u32 hazards reachable from saves (GAME-TRAVEL-NPCS-05, X-CPP-SAFETY-03).

Two real ordering bugs remain:
- A month falling due on a scene-change tick carries the calendar step across the change (CORE-SIM-01).
- The P pause leaves the simulation tick running, so orbits, spins, `Combat::Tick` and respawns run while the world is "paused" (GAP1-CLOCK-DISCIPLINE-PAUSE-01). This contradicts both the 5b2 spec and the pause rule in memory.

Scaling is the main exposure (CORE-WORLD-01, -02, CORE-INFRA-02). Diagonal-cost rounding skews routes (CORE-WORLD-05).

### Persistence (saves and replays)
The byte-level code is some of the best in the repo. Every count is bounded by the bytes left, the delta and order loads reject every malformed layout reviewers built, and the container checks magic, sizes, ratio and checksum (core-persist). What is weak is identity, not parsing:
- The base world is never fingerprinted, so a forgotten `GENERATOR_VERSION` bump or a build difference loads a save onto a different world without error (X-DETERMINISM-01).
- Component layouts have no automatic version (CORE-PERSIST-03).
- Replays accept any binary carrying the same version string (X-DETERMINISM-02).

Durability is weak too: no fsync, no backup, a fixed temp name, close errors ignored, no single-instance guard (X-RESILIENCE-05, X-UNTRUSTED-INPUT-12). Failures are invisible to the player (X-RESILIENCE-06). Every load and replay start regenerates a 50,000-NPC world the host already holds (GAP1-SIM-BOUNDARY-COMPLETENESS-02). The checksum-mismatch path is untested (CORE-PERSIST-01). Refusal tests can pass for the wrong reason (TESTS-GAME-03). Committed fuzz seeds are never loaded (TESTS-GAME-04). On Windows, the success log of every background save can call `std::terminate` (X-CPP-SAFETY-01).

### Rendering and shaders
SDL_GPU usage is careful: uploads count only once submitted, the swapchain rules are followed, sRGB is handled correctly, and every HLSL struct matches its C++ twin field by field under SPIR-V layout (gfx-renderer, shaders). The defects:
- **Snapshot identity.** The snapshot ID repeats across a scene change, so every portal and F2 draws the new scene's batches against the old instance buffer (GAP1-SCENE-TRANSITION-FLOW-01).
- **Failure handling.** Device loss and persistent frame failure are never escalated (X-RESILIENCE-03), and a frame acquired before a failing `LoadScene` is never submitted (GFX-RENDERER-04).
- **Known-deferred index uploads.** Quad index buffers are still marked uploaded at record time (GFX-FEATURES-01).
- **Unverified paths.** Only SPIR-V layouts and bindings are verified. DXIL registers and the MSL path never draw anywhere (SHADERS-01). Shader compile flags don't mark binaries stale (SHADERS-03).
- **Scale.** River and road buffers have fixed caps (GFX-WORLD-DRAW-01). The ground shader recomputes about 11 octaves of noise per pixel per frame (SHADERS-04). The atlas stays resident (X-PERFORMANCE-11).
- **Text.** A NUL in a string hangs start-up (GFX-TEXT-01). HUD text has no DPI scaling (GFX-TEXT-04). Glyph pages grow without bound (GFX-TEXT-14).
- **Accessibility.** The reduce-motion lever also changes seal lifetimes (GFX-COMBAT-DRAW-01).
- **Tests.** No unit test reaches `internal/features` or the live frame path (GFX-FEATURES-07, GFX-RENDERER-05).

### Game layer
The code is correct for shipped content and very heavily tested: about 135 host cases and about 80 save-refusal cases (game-host-a, game-host-b). Defects:
- **Lance on the portal tick.** A lance cast on the portal tick never sends UseEnded, and its leftover strike trips `PN_ASSERT` in the replay hash (GAME-COMBAT-01).
- **Held movement.** It walks off the map, and a load accepts any finite map place (GAME-HOST-A-01). Crossing impassable terrain is recorded as deliberate in the player-walk spec; leaving the map is not.
- **Route invariant.** A held route outlives a removed player, and the next save is refused by its own game (GAME-HOST-B-01).
- **Portal through a route.** A walk-to route through the portal's area sends the player through it (GAME-HOST-A-02).
- **NPC removal.** There is no single entry point for removing an NPC (GAME-TRAVEL-NPCS-03).
- **Scenes.** Orbit streaks on every scene entry (GAME-SCENES-01). Arena NPCs wander on showcase seals, against the user's rule (GAME-SCENES-02).

Structurally, combat is a closed if-chain hard-wired to foxes (GD-COMBAT-SYSTEMS-02, X-ARCHITECTURE-06). The NPC tier machinery is dead in production (GAME-TRAVEL-NPCS-04). Power grows without limit (GD-WORLD-SIM-03). The world seed is a constant (GAME-HOST-A-06).

### Data layer and assets
The readers fail closed, name the refusing line, keep integers below 2^53 and are fuzzed (data-yaml-core, data-world). Discipline is uneven, though:
- Only `seals.cpp` guards f64-to-f32 narrowing; abilities and effects can hit UB or a silent zero and never call `Valid()` (DATA-COMBAT-01).
- `SaveSettings` drops `edge_pan` (DATA-YAML-CORE-01) and rewrites the file in place (DATA-YAML-CORE-08).
- Duplicate keys are accepted almost everywhere (DATA-YAML-CORE-12).
- Manifest geometry is unchecked, a latent heap overflow (X-UNTRUSTED-INPUT-05).
- The look reader is coupled to the hand-written map's place keys (DATA-WORLD-01).

As data for a Tale of Immortal-like game, the schema is almost empty: two creatures, no realms, items, sects, events or calendar (ASSETS-DATA-01, -02, -07). `world_map.py` is a 1,700-line single-seed test-map tool that mirrors C++ rules with no cross-check (ART-TOOLS-01, -02).

### App and platform
- **Strengths.** The input modules are pure and well tested. Scancode key mapping is right for AZERTY and Dvorak. Start-up failures show a dialog (app-input-ui, platform).
- **Gamepad state.** A removed pad leaves stick state latched (X-ACCESSIBILITY-01), several pads share one stick (PLATFORM-02), and nothing resets held input on focus loss (APP-INPUT-UI-03).
- **Platform features.** HiDPI is absent (PLATFORM-01). There is no app metadata, minimum window size or IME (PLATFORM-05, -06, -09).
- **Debug data race.** A debug-build race on `m_options` exists at logic start (APP-MAIN-02).
- **Logic-thread failure.** Its death ends the window with no message, and a simultaneous quit masks the exit status (APP-MAIN-01).
- **Escape.** It quits instantly (X-ACCESSIBILITY-03).
- **Untested app loop.** The roughly 600 lines of `App` orchestration and `main` have no tests (APP-MAIN-03).

### Tests
- **Strengths.** About 1,630 doctest cases, each file in its own process. Saved, never-saved and loaded triples on 1 and 4 workers. Thorough refusal matrices. 53 reference pictures with shimmer metrics (x-test-strategy, tests-rest).
- **Golden anchors.** Replay-critical maths and hashes are anchored only by relations, never by pinned values (TESTS-CORE-01, TESTS-CORE-05).
- **Refusals and seeds.** Refusal tests assert only a bool (TESTS-GAME-03), and committed fuzz seeds are never read (TESTS-GAME-04).
- **Reference tolerance.** It allows 92 pixels beyond tolerance despite byte-identical output (TESTS-REST-01).
- **Isolation.** Fixed temp names break isolation under the repo's own parallel-worktree workflow (TESTS-GAME-09, TESTS-REST-02).
- **Fixture cost.** Replay sessions build the 50,000-NPC world for every scene (TESTS-GAME-01). `host_tests.cpp` is 3,709 lines (TESTS-GAME-07).
- **Coverage holes.** Helpers are duplicated with different meanings (TESTS-GAME-02). Sweeps break tests/AGENTS.md (CORE-INFRA-06). Nothing gates the app loop, real GPUs, sanitizers, long runs or coverage (X-TEST-STRATEGY-01, -04, -06, -07).

### Tooling, build and CI
- **Strengths.** Pinning is disciplined: exact Pixi pins, base image by digest, apt snapshot, SHA-pinned actions. CI permissions are least-privilege with no `pull_request` path to secrets. The clang-tidy cache key is careful (build-env, ci, tools-a).
- **Release hardening.** Release builds have no hardened STL, no path-prefix map and no CET or stack-clash protection (BUILD-ENV-01, -02, -03).
- **Release process.** The release job doesn't check that the tag passed the gate (CI-01). `publish` uploads any zip in `dist/` (TOOLS-B-08). Releases have no attestation or SBOM (CI-08).
- **Script size.** `project_n.py` is 3,372 lines, and its self-test file is 3,740 (TOOLS-B-15).
- **Lint gaps.** The regex lints miss block comments and raw strings (TOOLS-A-07). `NOLINT(*)` passes (TOOLS-A-02). Lint stops at the first failing category (TOOLS-B-03).
- **Stale or missing guidance.** `doctor` checks for Python 3.11 while 3.13+ is needed (TOOLS-B-01). A missing tool ends in a traceback, not a hint (TOOLS-A-05).
- **Shared state.** `clean --packages` deletes other worktrees' package caches (TOOLS-B-07).
- **Profiling.** Tracy is built with callstacks and fibers on, which skews every `measure` number (GAP1-THIRD-PARTY-LIBRARY-USAGE-01).
- **CI gaps.** CI lacks Linux sanitizers, fuzz runs and a scheduled job (CI-05). `cancel-in-progress` cancels main runs before their caches are saved (CI-06).

### Agent process and docs
The docs are accurate. Reviewers found one hard error, the git-commit skill's scope examples (AGENT-DOCS-01). The skill ownership map is clear. Size is the problem:
- **Size.** src/AGENTS.md is 45,264 bytes in 49 lines, one line 15,404 characters long, and loads on every task under `src/`. tools/ is 21 KB and tests/ is 19 KB (X-ARCHITECTURE-08, AGENT-DOCS-02).
- **Content.** Much of the text is changelog narrative and copies of Doxygen.
- **Outside dependencies.** The process depends on things outside the repo: the context-budget hook, the backlog and in-flight memory, and an unpinned superpowers plugin (AGENT-DOCS-07, -08).
- **Rule conflicts.** The global rule "low = Minor, note it" conflicts with the project's "fix every finding" (AGENT-DOCS-03). The README and `doctor` use `pacman` against the user's shelly rule (AGENT-DOCS-11). A skill fetches an unlocked tool at runtime (AGENT-DOCS-05).
- **Ceremony.** Per task: implementer, Opus review, fix, re-review, lint, test, two release builds. Per branch: a final review, four external models, `check` and nine CI checks (GD-ROADMAP-SCOPE-15, AGENT-DOCS-15). That is the user's recorded choice (section 7). The cost is that content throughput is falling (gd-roadmap-scope: 38 feat commits against 91 docs since 2026-11-17).

### Design docs
The rationale is excellent: alternatives, measurements and spike criteria are recorded, and the core runtime matches 0002 closely (design-docs). The drift is substantial:
- **0001.** It still describes the removed agents and skills and the pre-superpowers process (DESIGN-DOCS-01).
- **0002.** It is still "Draft, phases 1 to 5" (DESIGN-DOCS-18). It presents as followed rules that were never built: per-tick and per-worker arenas, command merging, the spatial hash, Tracy statistics, focus-pause as a setting, worker FP checks (DESIGN-DOCS-03, -21, -22, -25, -29).
- **Overstated robustness.** It claims crash-proof saves, exceptions ending at main's handlers, and symbols "for crash reports" (DESIGN-DOCS-05, -06, -14).
- **Budgets.** The §13 game-state line was never checked against the code and is broken at shipped scale (GAP2-MEMORY-BUDGET-VS-0002-S13-01). Memory has no statistic at all (GAP2-MEMORY-BUDGET-VS-0002-S13-02).
- **Superseded sub-docs.** 0002.4 and 0002.4.2 are superseded but still read as live (GAP2-DESIGN-DOCS-0002X-DRIFT-01, -03, -09). 0002.2 describes a replay format three versions old (DESIGN-DOCS-26).

### Specs and plans
- **Outcome sections.** They are honest and measured. About 40 checked claims held on `main` (specs-built-a, specs-built-b).
- **Shared-routes stage 1.** It is the strongest document in scope (specs-roadmap), with flaws: no path smoothing (SPECS-ROADMAP-13), an underspecified A* end (SPECS-ROADMAP-14), worker-count mismatches (SPECS-ROADMAP-16), and a place table bounded by an unbounded reach (GD-WORLD-SIM-14).
- **Status markers.** Plans carry no completion markers and every checkbox is unticked (SPECS-BUILT-A-08, SPECS-BUILT-B-15).
- **Unclosed promises.** dev-loop-speedups Goal 6 (SPECS-BUILT-A-01). The fog cost rise left unexplained (SPECS-BUILT-B-01). The 5c1 16 ms save aim (SPECS-BUILT-A-06).
- **Unratified budgets.** 5b2 broke 5a's budgets and never said so (SPECS-BUILT-A-02).
- **The roadmap past chunk 5** is not sound as a set:
  - Chunks 5, 7 and 10 assume the hand-made map (SPECS-ROADMAP-01).
  - Chunks 5-10 lack Accessibility sections (SPECS-ROADMAP-02).
  - The phase table is stale (SPECS-ROADMAP-03).
  - Chunks 6 and 7 pin generator hashes against the within-one-binary decision (SPECS-ROADMAP-07).
  - Chunks 8 and 11 conflict over `settings.yaml` (SPECS-ROADMAP-05).
  - The chunk 9 roster would hold thousands of undrawn residents (SPECS-ROADMAP-04).
  - Window moving and scaling are mouse-only in every chunk (SPECS-ROADMAP-06).
  - Stand-in screens are built and then thrown away (SPECS-ROADMAP-11).

### Game design against Tale of Immortal
The volley model (one record per cast, each sword a pure function) is excellent groundwork, and 50,000 deterministic NPCs could be a real differentiator (gd-toi-coverage, gd-combat-systems). As design, nearly everything is placeholder:
- **The player character.** The player's cultivator is a throwaway scene entity (GD-TOI-COVERAGE-01), and character state lives in hashed global data (GD-TOI-COVERAGE-02). This is the single biggest structural mismatch with the genre.
- **Threat.** Enemies have no verbs and the player can't be hurt (GD-COMBAT-SYSTEMS-01, GD-TOI-COVERAGE-03).
- **Qi.** It is gained on cast, so it never constrains play (GD-COMBAT-SYSTEMS-03).
- **DoTs.** The shipped generator and spender re-apply faster than an affliction's period, so they never tick (GD-COMBAT-SYSTEMS-09, by user decision).
- **Balance.** The swords out-damage everything else by an order of magnitude (GD-COMBAT-SYSTEMS-11).
- **NPC power.** It grows without limit and undoes the travel tuning within about a year of map time (GD-WORLD-SIM-03, an unmeasured estimate).
- **Visits.** They change nothing (GD-WORLD-SIM-09).

| System | State | Planned in | Findings |
|---|---|---|---|
| Cultivation realms, breakthroughs | Absent; `Npc::power` +1 a month as placeholder | Chunk 9 names a tier from power | GD-TOI-COVERAGE-04, GD-WORLD-SIM-03 |
| Attributes, destinies, talents | Absent | Nothing | GD-TOI-COVERAGE-01, -17 |
| Spirit roots, elements | Absent; elements are only hit kinds | Data-driven sweep (backlog) | GD-TOI-COVERAGE-07 |
| Skills, manuals, arts | Present in part: 4 hard-coded abilities, 8 slots, global loadout | Chunk 7 preset loadouts | GD-COMBAT-SYSTEMS-02, GD-TOI-COVERAGE-02 |
| Items, inventory, equipment | Absent | Chunk 7 counts orbs only | gd-toi-coverage table |
| Alchemy, forging, talismans, formations | Absent; seals are visual only | Chunk 8 mentions a cauldron UI | gd-toi-coverage table |
| Economy | Absent | Chunk 6 places shops with no gameplay | gd-toi-coverage table |
| Sects, factions | Absent; combat targets only foxes | Chunk 6: sects as world records | X-ARCHITECTURE-06 |
| NPC life simulation | Present in part: needs, 3 actions, travel, relationships that only grow; no age, death or birth | Nothing for ageing | GD-TOI-COVERAGE-05, -10, -11, -12 |
| World events, calamities, secret realms | Absent | Chunk 7 spawns sites | GD-TOI-COVERAGE-18, SPECS-ROADMAP-08 |
| Towns, facilities | Absent; entering pending (5c2) | Chunk 6 | GD-TOI-COVERAGE-09 |
| Quests | Absent | "Later" in chunks 6, 7 | gd-toi-coverage table |
| Exploration, fog, regions by realm | Fog and sight present; no difficulty gradient | Generator must zone it (no spec) | GD-TOI-COVERAGE-18 |
| Combat | Player side present (swords, bolt, lance, dash); no enemy attacks, collision or player death | Chunk 7 (attacks and death are non-goals) | GD-COMBAT-SYSTEMS-01, -05, -06 |
| Death, reincarnation, inheritance | Absent | Nothing | gd-toi-coverage table |
| Time model, calendar | Present in part: months and days on the map only; no years, seasons or time-skip | "Day length a setting for later" | GD-WORLD-SIM-07, GD-TOI-COVERAGE-13 |
| Procedural world | Absent at runtime; offline Python test-map tool | Decided 2026-11-25, no spec | GD-WORLD-SIM-01, ART-TOOLS-02 |
| Save and continue | Quicksave present (delta) | No autosave anywhere | GD-ROADMAP-SCOPE-08 |
| UI, menus | Stand-ins only | Chunks 8, 10, 11 (RmlUi S13 unrun) | GD-ROADMAP-SCOPE-06 |
| Audio | Absent | Phase 7 | GD-ROADMAP-SCOPE-23 |
| Localisation | Present in part: en and zh hard-coded, one font | Backlog | X-TEXT-L10N-04, -07 |
| Modding | Absent (YAML is a start; compiled enums and SpriteId) | Data-driven sweep (backlog) | GD-TOI-COVERAGE-08 |

### Roadmap and scope
- **Pace.** The repo has 975 commits in 27 days. Phases 1-5 took about 10 days. Phase 6 has taken 16 days so far, and chunk 5 alone grew into about 12 branches (gd-roadmap-scope).
- **What exists.** No fail state, progression, enemy AI, UI module, menus or audio. Phase 6's chunk 7 keeps enemy attacks and player death as non-goals (GD-ROADMAP-SCOPE-10).
- **The game-design brainstorm.** The user placed it after chunk 5, but chunk 5 kept growing (GD-ROADMAP-SCOPE-01).
- **Missing plans.** No vertical slice is defined (GD-ROADMAP-SCOPE-02). Runtime procedural generation has no spec, yet routing is being built for it (GD-ROADMAP-SCOPE-04).
- **Unproven foundations.** RmlUi (S13), DragonBones (S14), D3D12 and Metal (S9) and a min-spec GPU are all unproven (GD-ROADMAP-SCOPE-05, -06, -11, -18).
- **Shipping.** No build has reached a player since v0.1.0 (GD-ROADMAP-SCOPE-22). The AGPL licence is undecided against store integration (GD-ROADMAP-SCOPE-20).
- **Scope against pace.** Six more chunks, each the size of chunk 5, at chunk 5's pace (SPECS-ROADMAP-25).

The engine can carry the game. What is unproven is the fun, and the specs now being executed rest on assumptions that the brainstorm or the procedural decision may overturn.

## 7. Findings that contradict recorded decisions: the user's rulings

### 7a. Real tensions that need a ruling
Each line names the decision, the finding and the question.

| Findings | Recorded decision | Question for the user |
|---|---|---|
| X-DETERMINISM-01, GD-WORLD-SIM-02, GD-ROADMAP-SCOPE-12 | 0002 §7.4: determinism within one binary, contraction on, "revisit only if replays or saves must ever match across builds"; pre-1.0 saves may break | Delta saves make the base's bit-exact regeneration a correctness requirement. Is a base-world fingerprint in the save (cheap; the load already regenerates the base) the accepted way to turn silent mis-loads into refusals? |
| X-DETERMINISM-05, TESTS-GAME-19, X-TEST-STRATEGY-13 | Compare runs of one build, never pinned hashes (backlog 4b) | Allow golden values per preset, or for `core::math` bit patterns only (TESTS-CORE-01), as a change detector? |
| SPECS-ROADMAP-07 | The same within-one-binary decision | Chunks 6 and 7 require byte-identical generation on every preset and pinned hashes. Amend the specs or the decision. |
| CORE-PERSIST-22 | 0002.2: "same binary only" | Add a build ID so another build is refused, not reported as diverged? |
| CORE-PERSIST-08, X-CONCURRENCY-05, X-TEST-STRATEGY-01 | 0001 rule 17: sanitizers on request, never part of the gate | Run fuzz corpus replays in CI, or a scheduled TSan and fuzz job? |
| GD-COMBAT-SYSTEMS-09 | Affliction refresh resets the clock (the user, 2026-11-12) | The shipped generator and spender then never tick in normal play. Reopen? |
| GD-COMBAT-SYSTEMS-07 | Chunk 4 decision 10: one cast per click, a held button doesn't repeat | The verifier says an input buffer and hold-to-repeat are new proposals, not a reversal. Allow them on accessibility grounds (4 clicks a second)? |
| X-ACCESSIBILITY-18 | Settings never change difficulty | Do game speed or assist options count as difficulty? Decide before combat scenes ship. |
| GD-ROADMAP-SCOPE-01 | Game-design brainstorm after chunk 5, before chunk 6 | Chunk 5 kept growing (roads, travellers, river heads, shared-routes). Hold it now, before shared-routes stage 2? |
| GD-ROADMAP-SCOPE-15, AGENT-DOCS-15 | The superpowers process, fix every finding, cross-model milestone reviews | A lighter path for presentation and test-map work? |
| SPECS-ROADMAP-11 | Phase 6 decisions 12 and 15: the demo world comes before the UI chunks | Accept throwaway stand-in screens, or reorder? |
| GD-ROADMAP-SCOPE-11 | Phase 6 decision 9: judge frame time on the dev machine | Measure on min-spec hardware before the world-map passes grow further? |
| DESIGN-DOCS-28, GFX-RENDERER-20, GAP1-CLOCK-DISCIPLINE-PAUSE-02 | 0002: vsync and frame cap become settings; S10 decides the default | Ship uncapped by default? The decision is still open. |
| DESIGN-DOCS-34, GD-ROADMAP-SCOPE-18 | DragonBones planned, S14 in phase 7 | Run S14 earlier, or record a fallback? |
| X-CPP-SAFETY-10 | src/AGENTS.md: use if-chains (the switch-warning clash) | Add `static_assert` enum counts at each dispatch as the mitigation? |
| X-ARCHITECTURE-10 | MODULES: `game` and `data` link only `core` | Split the engine kernel from the game model? |
| X-PERFORMANCE-19 | src/AGENTS.md: generation has no job system, and a load regenerates the base | Keep this, given procedural maps? |
| GAP1-SCENE-TRANSITION-FLOW-09 | Backlog: asset failures "stop the game cleanly" | Mid-session, that loses progress. Add an emergency save or a refusal path? |
| CORE-WORLD-26 | 5b2 RF1: region and reach use base costs | Some NPCs never travel, with no warning. Keep? |
| X-DETERMINISM-10 | 0002 §7.4 contraction claim | Reword: LTO and inlining can fuse per call site. |
| BUILD-ENV-19 | 0001 rule 15: `-Wpadded` excluded (the user, 2026-11-05) | An optional separate padding report? |
| GAME-TRAVEL-NPCS-18 | The month-step design | Members of finished journeys stay Travel until the month ends, which skews socialising. Accept? |
| GAP2-DESIGN-DOCS-0002X-DRIFT-02 | 0002.4.2 superseded by the chunk 3 spec | Mark which sections are overridden. |
| SPECS-BUILT-B-07 | River-heads Outcome: traffic floor dropped to 2,400 (user ruling) | The roads and river-heads spec text still say 2,500. Annotate. |

### 7b. Code or docs that break a recorded rule (fix; no ruling needed)
- **Pause freezes game animation** (memory) and the 5b2 spec: GAP1-CLOCK-DISCIPLINE-PAUSE-01, -05.
- **Showcases never overlap** (memory): GAME-SCENES-02, GAME-SCENES-14.
- **No compatibility before 1.0** (memory; dead sprites are removed): ART-TOOLS-07.
- **AGENTS.md about 100 lines, one line per rule** (root AGENTS.md): X-ARCHITECTURE-08 cluster, TOOLS-A-20.
- **No design-process citations in comments; plain comments say why**: GFX-WORLD-DRAW-11 (`tile_hash.hpp`), SHADERS-07, GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-12.
- **Avoid wasted work** (memory): GAP1-THIRD-PARTY-LIBRARY-USAGE-02, X-PERFORMANCE-14, GFX-WORLD-DRAW-07.
- **Settings never change difficulty / accessibility first**: GFX-COMBAT-DRAW-01.
- **Exact pins**: BUILD-ENV-21 (clang `==22.1`), AGENT-DOCS-05 (an unlocked tool fetched at runtime).
- **Small unit tests, no sweeps** (tests/AGENTS.md): CORE-INFRA-06, TESTS-CORE-10.
- **Cite commits by subject**: SPECS-BUILT-A-17.
- **Don't call podman, xmake or git-cliff directly in docs**: AGENT-DOCS-13.
- **Logged outcomes, not attempts** (memory): GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-04.
- **The user's global shelly rule**: AGENT-DOCS-11. **The global "low = note it" rule against the project's "fix every finding"**: AGENT-DOCS-03 (unverifiable: it depends on how the two rules are meant to rank).
- **0002.4 §3's promised font-coverage test** is missing: X-TEXT-L10N-01.
- **Phase decision 17: every chunk spec carries an Accessibility section**: SPECS-ROADMAP-02.
- **Hard-coded scope examples against commit-check**: AGENT-DOCS-01, DESIGN-DOCS-09.

### 7c. Marked contradicts-recorded-decision, but verification found no contradiction
Kept for the record, with the reason from each verification note:
- ART-TOOLS-02 and ASSETS-DATA-09: world.yaml is a test map, consistent with the procedural decision.
- GAP2-REPO-CONFIG-FILES-02: it proposes additions to the user's `.clang-tidy`.
- GD-COMBAT-SYSTEMS-23: a forward-looking note.
- SPECS-BUILT-A-14: the three non-building MSVC commits were left by recorded decision.
- TOOLS-A-18 and TOOLS-B-21: the lint claims only one-binary determinism.
- TOOLS-B-11: no decision on non-atomic writes exists.
- TOOLS-B-15: "thin" means delegating work, not line count.
- GFX-RENDERER-20: a recorded plan.
- TESTS-REST-11: the verifier could not judge it.

## 8. Known-deferred items the review re-confirmed
All 85 known-deferred findings are listed in the INDEX files. Grouped by backlog item:

- **Audit 1, quad-index uploads** (meshes, ribbons and seal indices marked at record time; the seal pack is now guarded): GFX-FEATURES-01, GAP1-SWEEP-UP-BACKLOG-VS-CODE-05.
- **Audit 2, replay `ReadTicks`** (partly fixed: the reserve is bounded by the bytes left, about 16 B per byte): CORE-PERSIST-18, X-UNTRUSTED-INPUT-20, GAP1-SWEEP-UP-BACKLOG-VS-CODE-06, TESTS-CORE-07.
- **Audit 4, casts aim at the cursor; no gamepad or keyboard casting**: APP-INPUT-UI-01, X-ACCESSIBILITY-02, GD-COMBAT-SYSTEMS-08, TESTS-REST-14.
- **Audit 5, held stick pan ignores focus**: APP-INPUT-UI-10.
- **Audit 13, `MakeTileLeg` u32 assert**: CORE-JOURNEY-VOLLEY-09.
- **Unlogged save refusals**: GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-01, GAME-HOST-A-04, GAP1-SWEEP-UP-BACKLOG-VS-CODE-07, CORE-PERSIST-11.
- **`NoteUnknownKeys`**: DATA-YAML-CORE-09.
- **Data-driven sweep: closed enums and compiled content**: GD-COMBAT-SYSTEMS-02, -17, GAME-COMBAT-10, ASSETS-DATA-01, -02, X-ARCHITECTURE-20.
- **Distance maps per (city, profile), and `LegWalkCache`** (to be replaced by shared-routes): GD-WORLD-SIM-04, CORE-WORLD-14, SPECS-BUILT-B-13, CORE-JOURNEY-VOLLEY-10, X-PERFORMANCE-24.
- **Localisation**: X-TEXT-L10N-04, -06, -07, -08, -15, -16, SPECS-ROADMAP-27.
- **D3D12 and Metal unverified; Windows reproducibility**: GD-ROADMAP-SCOPE-05, DESIGN-DOCS-13, BUILD-ENV-15.
- **0002 and 0002.x drift**: DESIGN-DOCS-18, -19, -26, -27, X-ARCHITECTURE-12, -19, GD-ROADMAP-SCOPE-17, GAP2-DESIGN-DOCS-0002X-DRIFT-09, -11, AGENT-DOCS-04.
- **Oversized files: `renderer.cpp`, `world_look.cpp`, test files; `Renderer::Impl`**: GFX-RENDERER-17, X-ARCHITECTURE-15, DATA-WORLD-08, TESTS-REST-12.
- **`journey.hpp` includes `calendar.hpp`**: CORE-JOURNEY-VOLLEY-15.
- **Presentation types in `core`**: CORE-SIM-19.
- **Scene lists hand-duplicated**: ART-TOOLS-08.
- **Game content in the host**: GAME-HOST-A-09.
- **Hot reload ignores world, names and strings**: GAP1-HOT-RELOAD-FLOW-08.
- **Paused frames still animate (Audit 12; the backlog text is wrong and incomplete)**: GAP1-CLOCK-DISCIPLINE-PAUSE-03.
- **Game-design gaps** (no threat, no core loop, no decisions on the map, inert visits, no audio): GD-COMBAT-SYSTEMS-16, GD-TOI-COVERAGE-22, GD-WORLD-SIM-06, -09, GD-ROADMAP-SCOPE-23.
- **Reference drift guard; orphan pictures; shimmer thresholds**: X-TEST-STRATEGY-03, TOOLS-A-14, TESTS-REST-21, SPECS-BUILT-B-09.
- **Recorded spec deferrals**: SPECS-BUILT-A-04 (shimmer check 0.02), SPECS-BUILT-A-06 (5c1 save time), SPECS-BUILT-B-04 (`measure` counts `SubmitFrame`), SPECS-BUILT-B-10 (held movement has no collision), SPECS-BUILT-B-12 (river 10's head).
- **Other**:
  - PLATFORM-09 (no IME)
  - GFX-COMBAT-DRAW-13 (sword and seal caps)
  - X-PERFORMANCE-23 (full instance upload per snapshot)
  - DATA-YAML-CORE-05 (orbit ranges)
  - DATA-YAML-CORE-19 (YAML fuzz budget)
  - X-ACCESSIBILITY-13 (no hold-or-toggle)
  - TESTS-REST-05 (replay fuzz seed)
  - X-UNTRUSTED-INPUT-20
  - GAP2-REPO-CONFIG-FILES-15 and GD-ROADMAP-SCOPE-22 (CHANGELOG and no player build since v0.1.0)
  - APP-INPUT-UI-11 (cast dropped during a dash)
  - APP-MAIN-13 (main thread runs logic chunks)
  - GAP1-SWEEP-UP-BACKLOG-VS-CODE-08 (Audits 3, 11 and 12 found by no other reviewer).

The backlog audit (gap1-sweep-up-backlog-vs-code) also found:
- **Already fixed or superseded**: Audit 6 `walk_speed`, the warnings branch, unity builds, the `arena_tests` split (GAP1-SWEEP-UP-BACKLOG-VS-CODE-03).
- **Drifted line numbers** (GAP1-SWEEP-UP-BACKLOG-VS-CODE-09).
- **Superseded "next design" items**, now the shared-routes branch (GAP1-SWEEP-UP-BACKLOG-VS-CODE-04).
- **Whole classes missing from the backlog**: crash handling, save durability, Windows paths, HiDPI and procedural scaling (GAP1-SWEEP-UP-BACKLOG-VS-CODE-01, -02).

## 9. Recommended order of work

### Before building more (correctness, safety, silent failure)
1. **Windows paths.** Convert every path for SDL through `u8string`. Add an `activeCodePage` UTF-8 manifest, or remove `path.string()` from log arguments (X-UNTRUSTED-INPUT-01, -02 clusters). Without this, the game fails to start or terminates on save for any non-ASCII install or profile folder.
2. **Exception barrier and crash capture.**
   - Catch in job chunk bodies and background jobs; this also removes the use-after-free (X-RESILIENCE-02 cluster).
   - Add a terminate and signal handler that flushes the log (X-RESILIENCE-01).
   - Put a version, build, OS and GPU banner in the log (GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-02).
3. **Save identity.** Add a base-world fingerprint (X-DETERMINISM-01; ruling 7a), an automatic layout fingerprint (CORE-PERSIST-03) and a replay build ID (X-DETERMINISM-02). Make saves durable: fsync, a unique temp name, a checked close (X-RESILIENCE-05).
4. **The concrete bugs, test-first:**
   - GAME-COMBAT-01: lance on the portal tick.
   - GAP1-SCENE-TRANSITION-FLOW-01: stale instance buffer on every portal; stamp a publish sequence number.
   - GAP1-CLOCK-DISCIPLINE-PAUSE-01: P pause.
   - CORE-SIM-01: month due on a scene change.
   - GAME-HOST-A-01: off-map walking and load check.
   - GAME-HOST-B-01: route outliving the player.
   - X-ACCESSIBILITY-01: stuck pad.
   - DATA-YAML-CORE-01 with DATA-YAML-CORE-08: `edge_pan` and the atomic settings write.
   - GD-TOI-COVERAGE-07: healing damages; burn and poison key.
   - DATA-COMBAT-01: f32 narrowing UB.
   - GAME-TRAVEL-NPCS-05: `CellAt` UB.
   - X-CPP-SAFETY-03: u32 wrap.
   - GFX-TEXT-01: NUL hangs start-up.
   - X-UNTRUSTED-INPUT-05: latent heap overflow.
   - X-UNTRUSTED-INPUT-03, -04 and CORE-PERSIST-04: amplification.
   - APP-MAIN-02: data race.
   - GAME-SCENES-02: NPCs on showcases.
   - GAME-HOST-A-02: portal through a route.
5. **Player-visible failure.** Show an error dialog on runtime fatal paths (X-RESILIENCE-04, APP-MAIN-01). Report save success or failure to the player (X-RESILIENCE-06). Log every load refusal (GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-01).
6. **Test holes behind correctness claims:**
   - the checksum mismatch (CORE-PERSIST-01);
   - refusal reasons (TESTS-GAME-03);
   - loading committed fuzz seeds (TESTS-GAME-04);
   - unique temp paths (TESTS-GAME-09 cluster).
7. **Docs that mislead every reader:**
   - Cut src/AGENTS.md, tools/AGENTS.md and tests/AGENTS.md to one line per rule and move the narrative into specs (X-ARCHITECTURE-08).
   - Mark 0002's unbuilt rules and soften its robustness claims (DESIGN-DOCS-03, -05, -06, -14).
   - Sweep 0001 (DESIGN-DOCS-01).
   - Fix the phase table (SPECS-ROADMAP-03).
   - Prune the backlog and give it a status column (GAP1-SWEEP-UP-BACKLOG-VS-CODE-03).

### Next, before chunk 6 and before shared-routes stage 2
8. **Game-design brainstorm** (GD-ROADMAP-SCOPE-01, -02). It should settle:
   - the persistent player character and where character state lives (GD-TOI-COVERAGE-01, -02);
   - enemy verbs, threat and the effect and status model (GD-COMBAT-SYSTEMS-01, -04, -10);
   - collision (GD-TOI-COVERAGE-03);
   - the Qi loop (GD-COMBAT-SYSTEMS-03);
   - NPC progression, lifecycle and tiers (GD-WORLD-SIM-03, GD-TOI-COVERAGE-10, GAME-TRAVEL-NPCS-04);
   - the time model and time-skip (GD-WORLD-SIM-07, GD-ROADMAP-SCOPE-09);
   - a vertical slice.
9. **Procedural generator spec**, covering world identity, runtime delivery to the renderer and per-view or per-region data (GD-WORLD-SIM-01, X-ARCHITECTURE-02, GD-WORLD-SIM-14).
10. **Re-brainstorm chunks 6-11 as a set** against items 8 and 9 (SPECS-ROADMAP-01, -02, -04, -05, -06, -07, -10, -11; GD-ROADMAP-SCOPE-03).

### Schedule
- **Scaling**, inside the generator and shared-routes work: the per-view publish, fog, ribbons, textures, A* and ground field (section 4's scaling table); a memory statistic and the §13 budget (GAP2-MEMORY-BUDGET-VS-0002-S13-01, -02).
- **Architecture**: split Host, add scene parameters, factions and per-caster loadouts, view models and an input-context stack (x-architecture (a)-(g)).
- **Spikes**: S9 (D3D12 and Metal), S13 (RmlUi), S14 (DragonBones) and a min-spec GPU check (GD-ROADMAP-SCOPE-05, -06, -11, -18).
- **Input parity and accessibility**: target-cursor selection and camera follow (GAP2-COORDINATE-SPACES-AND-PICKING-01, -02), gamepad casting (X-ACCESSIBILITY-02), HiDPI (PLATFORM-01), settings fields (X-ACCESSIBILITY-05), confirm on Escape (X-ACCESSIBILITY-03), a combat pause (X-ACCESSIBILITY-04).
- **Gates** (after the 7a ruling): fuzz corpora in CI, a scheduled TSan and fuzz job, UBSan for the fuzzers (X-TEST-STRATEGY-01, X-CPP-SAFETY-05), a tighter reference tolerance (TESTS-REST-01), a release check that the tag passed the gate (CI-01).
- **Hardening**: release-mode checks on the asserts that guard dereferences (X-CPP-SAFETY-04), CET and stack-clash protection (BUILD-ENV-02), the EnTT entity limit (GAP1-THIRD-PARTY-LIBRARY-USAGE-04), float-cast-overflow sanitizing (GAP2-REPO-CONFIG-FILES-01; the verifier notes cert-flp30-c is the wrong check).
- **Tooling**: Tracy callstacks off and a new `measure` baseline (GAP1-THIRD-PARTY-LIBRARY-USAGE-01), the `doctor` Python version (TOOLS-B-01), worktree-safe `clean` (TOOLS-B-07), lint that runs every category (TOOLS-B-03).
- **Duplication cleanup**: theme 10.

### Accept, and record as accepted
- The 91 "deliberate" verdicts, where the reviewers agree the trade-off is recorded. Examples: BUILD-ENV-19, TOOLS-B-15's single file, SPECS-BUILT-A-14, GFX-RENDERER-07 at today's 4 pages, X-PERFORMANCE-29.
- Most info-level findings in tooling and CI. Examples: CI-09, CI-11, TOOLS-B-18, TOOLS-B-31.
- The section 7c items.
- Under the "fix every finding, Minor included" rule, each accepted item needs a written ruling with its reason. "Accept" here means "rule, don't code".

## 10. Coverage

**Reviewed: 62 units.**
- **Module units** (src by area, tests, tools, build, CI, art and assets, agent docs):
  - core: `core-world`, `core-journey-volley`, `core-sim`, `core-persist`, `core-infra`
  - gfx: `gfx-renderer`, `gfx-features`, `gfx-text`, `gfx-combat-draw`, `gfx-world-draw`, `shaders`
  - game: `game-host-a`, `game-host-b`, `game-combat`, `game-travel-npcs`, `game-scenes`
  - data: `data-yaml-core`, `data-world`, `data-combat`, `assets-data`
  - app and platform: `app-main`, `app-input-ui`, `platform`
  - tests: `tests-game`, `tests-core`, `tests-rest`
  - tools: `tools-a`, `tools-b`, `art-tools`
  - build and CI: `build-env`, `ci`
  - docs: `agent-docs`
- **Ten cross-cutting units** (`x-*`).
- **Four document units**: `design-docs`, `specs-roadmap`, `specs-built-a`, `specs-built-b`.
- **Four game-design units** (`gd-*`).
- **Seven first gap passes** (`gap1-*`): sim boundary, hot reload, scene transition, clocks and pause, third-party libraries, backlog against code, logging and the comment rule.
- **Five second gap passes** (`gap2-*`): 0002.x drift, the memory budget, long-session limits, coordinate spaces and picking, repo config files.

The scope included the project memory's `sweep-up-backlog.md`, `game-design-gaps.md` and `in-flight.md`, and the `shared-routes` worktree's spec and stage 1 plan.

**Nothing was built, run, profiled or measured.**
- Every finding comes from reading code, docs and data.
- Two units ran read-only scripts over data: assets-data checked sprite, place and string cross-references, and x-text-l10n checked that every shipped non-ASCII character is in the font's cmap (all are).
- Every performance figure, memory estimate and timing is arithmetic from the code and the specs' recorded measurements. Examples: the ~1.5 GB distance maps (CORE-WORLD-01), the 68-83 MB game state (GAP2-MEMORY-BUDGET-VS-0002-S13-01), "mortals become cultivators within about a year" (GD-WORLD-SIM-03).

**Read in part only**, as the units state:
- tools-a: the first half of `project_n.py`.
- tests-rest: the large gfx test files were skimmed.
- x-cpp-safety: sampled outside core.
- data-world and app-input-ui: tests by case list.
- gap2-design-docs-0002x-drift: `text_label` and `labels.cpp` not read.
- gd-combat-systems: `data/seals.cpp`, `data/effects.cpp`, `core/seals.hpp` and `core/glow.cpp` not read in depth.
- specs-built-a and specs-built-b: plans not read task by task.
- app-main: other `app/` files not read in full.
- x-untrusted-input: several loaders only by grep.

**Never exercised by anyone:** Windows and macOS runtime, D3D12, Metal, real Vulkan drivers, HiDPI displays, gamepads, min-spec hardware, large or generated maps, multi-hour sessions.

**Conflicts between findings, resolved by reading:**
- **Test map size.** It is 150x85 = 12,750 tiles (checked), not the x-performance assessment's "150x90, 14k".
- **"Silent" cap.** The 8,192-quad cap logs a warning, so it is not silent (GFX-WORLD-DRAW-01 verification against X-PERFORMANCE-02's wording).
- **Line ribbons against Audit 1.** gfx-features says `LineRibbonRenderer` already tracks submission. GFX-FEATURES-01 and GAP1-SWEEP-UP-BACKLOG-VS-CODE-05 say meshes, ribbons and seal indices do not. Both hold: `LineRibbonRenderer` (rivers and roads) and `RibbonRenderer` are different classes.
- **Held walking.** No terrain and no collision is recorded in the player-walk spec (GAME-HOST-B-05, deliberate). Leaving the map and loading an off-map place are not recorded (GAME-HOST-A-01 stands).
- **Same bug, different severities.** GFX-RENDERER-02 (low) vs GAP1-SCENE-TRANSITION-FLOW-01 (medium). GAME-COMBAT-02 (low) vs GD-TOI-COVERAGE-07 (medium). APP-INPUT-UI-02 (low) vs X-ACCESSIBILITY-01 (medium). GAME-TRAVEL-NPCS-01 (low) vs CORE-WORLD-01 (medium). The canonical (higher) severity applies.

**Unverifiable findings (133).**

*Need a run, profile or measurement to decide:*
- **Time:** APP-MAIN-06, APP-MAIN-13, CORE-SIM-09, CORE-WORLD-04, GAP1-SIM-BOUNDARY-COMPLETENESS-10, GD-WORLD-SIM-11, X-DETERMINISM-08, X-PERFORMANCE-05, -06, -08, -12, -26, -27, TESTS-GAME-17, -18, TOOLS-B-25, SPECS-BUILT-A-06.
- **Memory or bytes:** GAP2-MEMORY-BUDGET-VS-0002-S13-04, -06, -07, GAP2-LONG-SESSION-LIMITS-03, GFX-RENDERER-06, X-PERFORMANCE-11, GD-WORLD-SIM-13, GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-05.
- **Hardware or platform:** SPECS-BUILT-B-03, X-DETERMINISM-04, X-UNTRUSTED-INPUT-25, BUILD-ENV-23, GFX-FEATURES-08, -15, GFX-RENDERER-13, PLATFORM-07, -14, X-CONCURRENCY-14.
- **Behaviour (a run or a library's actual behaviour):** ART-TOOLS-03, -04, -05, -09, DATA-COMBAT-05, -06, -09, DATA-WORLD-02, -04, DATA-YAML-CORE-06, GAME-COMBAT-15, CORE-JOURNEY-VOLLEY-13, GD-WORLD-SIM-20, GFX-WORLD-DRAW-17, GFX-TEXT-17, X-UNTRUSTED-INPUT-04, TOOLS-A-22, TOOLS-B-04, TOOLS-B-26, BUILD-ENV-05, CORE-INFRA-23, GAP1-SIM-BOUNDARY-COMPLETENESS-07.
- **Look (a picture or a colour-blindness simulation):** ASSETS-DATA-05, CORE-WORLD-10, DATA-COMBAT-10, DATA-YAML-CORE-25, GAP1-HOT-RELOAD-FLOW-06, GAP2-COORDINATE-SPACES-AND-PICKING-05, GFX-FEATURES-13, GFX-TEXT-18, GFX-WORLD-DRAW-13, SPECS-BUILT-B-18, X-ACCESSIBILITY-07, -08, X-TEXT-L10N-14.
- **Balance (a play session):** GD-COMBAT-SYSTEMS-11.

*Judgement, design or opinion; a run would not decide them:*
- AGENT-DOCS-03, -11, APP-MAIN-04, BUILD-ENV-22, CI-08, CORE-INFRA-16, CORE-SIM-06, CORE-WORLD-24, -25, DATA-COMBAT-08
- GAME-COMBAT-10, GAME-HOST-A-10, -16, GAME-HOST-B-16
- GAP1-THIRD-PARTY-LIBRARY-USAGE-15, -16, -17
- GD-COMBAT-SYSTEMS-18, -19, -20, -22, GD-ROADMAP-SCOPE-14, -16, GD-TOI-COVERAGE-18, -23
- SHADERS-08, -13, -14
- SPECS-BUILT-A-15, SPECS-BUILT-B-17, SPECS-ROADMAP-08, -22, -25
- TESTS-CORE-03, -05, -06, TESTS-GAME-11, -14, -21, -22, TESTS-REST-11
- TOOLS-B-19, -30
- X-ARCHITECTURE-13, -14, -15, -17, -22, X-CONCURRENCY-18, X-DETERMINISM-05, -07, -13
- X-TEST-STRATEGY-05, -10, -14, X-UNTRUSTED-INPUT-06, -17

*Not checked against their sources:*
- GAP1-SWEEP-UP-BACKLOG-VS-CODE-01, -02, -04: the verifier did not search the backlog or the review files.
- SPECS-BUILT-A-16 and SPECS-BUILT-B-16: the evidence lies outside the repo or in gitignored files.

**Refuted (6, REFUTED-01.md):** ASSETS-DATA-13, TOOLS-A-27, BUILD-ENV-24, CI-14, X-RESILIENCE-15, X-RESILIENCE-16.

## 11. File guide

| File | What it holds |
|---|---|
| `README.md` | This report. |
| `<unit>.md` (62 files, for example `core-world.md`) | The unit's scope, its finding counts, its parts, the **Assessment** (strengths, weaknesses, overall health, risks, files read) and the first findings. |
| `<unit>-part2.md` … `-part4.md` | The rest of that unit's findings, in the same format. |
| `INDEX-01.md` … `INDEX-11.md` | Every standing finding, one row each (ID, severity, verdict, status, where, title), most severe first across all units. The ID prefix names the unit file: `CORE-WORLD-03` lives in `core-world.md` or its parts. |
| `REFUTED-01.md` | The 6 findings raised and refuted on verification. Each unit file gives the refutation reason. |

**Finding format.** `### ID: title`, then:
- **Severity**, with "(reviewer said X)" when verification changed it.
- **Verdict:** confirmed, partially-confirmed, unverifiable or deliberate.
- **Category.**
- **Status:** new, known-deferred or contradicts-recorded-decision.
- **Reviewer confidence.**
- **Where.**

The body follows, then **Evidence**, **Impact**, **Recommendation** and **Verification**. Verification holds the independent check and is the part to trust when it disagrees with the body.

**Area to unit files:**
- **Core:** `core-*`.
- **Rendering:** `gfx-*`, `shaders*`.
- **Game:** `game-*`.
- **Data:** `data-*`, `assets-data*`, `art-tools*`.
- **App and platform:** `app-*`, `platform*`.
- **Tests:** `tests-*`.
- **Tooling:** `tools-*`, `build-env*`, `ci*`.
- **Docs and process:** `agent-docs*`, `design-docs*`, `specs-*`.
- **Game design:** `gd-*`.
- **Cross-cutting:** `x-*`.
- **Gap passes:** `gap1-*`, `gap2-*`.
