# Finding index 3/11 (standing findings, most severe first)

The ID prefix is the unit file: e.g. CORE-WORLD-03 is in core-world.md (or its -partN files).

| ID | Severity | Verdict | Status | Where | Finding |
|---|---|---|---|---|---|
| CORE-SIM-03 | low | confirmed | new | `src/core/logic.cpp:333` | Restore accepts any tick counters from a save or a replay's start |
| CORE-SIM-04 | low | confirmed | new | `src/core/combat_rules.hpp:90` | CombatRules::Valid lets an aura range wrap to a bound of 0 |
| CORE-SIM-05 | low | confirmed | new | `src/core/logic.cpp:40` | The snapshot's field-by-field reset is a hand-kept list that a new Snapshot field can miss |
| CORE-SIM-06 | low | unverifiable | new | `src/core/state.hpp:260` | A component missing from a ComponentList is silently neither saved nor hashed |
| CORE-SIM-07 | low | confirmed | new | `src/core/logic.cpp:373` | A Load finishes the replay being recorded before it knows the load will work |
| CORE-SIM-08 | low | confirmed | new | `src/core/logic.cpp:111` | Presses sent in the same tick as a Save, ReloadData, AdvanceCalendar or recording toggle are silently dropped |
| CORE-SIM-09 | low | unverifiable | new | `src/core/logic.cpp:26` | The whole scene is published again on every burst while a scene change waits |
| CORE-SIM-10 | low | confirmed | new | `src/core/abilities.hpp:105` | Affliction's documentation says its ticks are once a second; the period is CombatRules::afflictionPeriod |
| CORE-SIM-11 | low | confirmed | new | `src/core/glow.hpp:109` | GlowDrive::Valid requires a pulse's period for every drive kind |
| CORE-SIM-12 | low | confirmed | new | `src/core/state.hpp:72` | State's domain access throws despite the no-exceptions rule |
| CORE-WORLD-03 | low | confirmed | new | `src/core/world_map.cpp:126` | Region Dijkstra pushes every relaxation without a cost check, fills a cost array no caller reads, and a second full Dijkstra runs only to test reachability |
| CORE-WORLD-04 | low | unverifiable | new | `src/core/road_curve.cpp:75` | RoadCurves is O(roads x all road segments), computed twice (renderer and host), and again on every look reload |
| CORE-WORLD-05 | low | confirmed | new | `src/core/world_map.cpp:695` | Diagonal cost cost*7/5 rounds down, so cheap tiles price diagonals too low (road 4 -> 5; costs 1 and 2 make a diagonal as cheap as a straight step) |
| CORE-WORLD-06 | low | partially-confirmed | new | `src/core/paths.cpp:54` | Route costs are u32 while MAX_TILES x MAX_WALK_COST x 7/5 exceeds it; overflow silently becomes 'unreachable' |
| CORE-WORLD-07 | low | partially-confirmed | new | `src/core/paths.hpp:85` | Paths::To is a mutating lazy builder called from parallel month-step chunks; it is safe only because Warm() ran first |
| CORE-WORLD-08 | low | partially-confirmed | new | `src/core/paths.cpp:190` | FindRoute is shipped but used only by tests, and indexes Profiles() without a bounds check |
| CORE-WORLD-09 | low | confirmed | new | `src/core/paths.cpp:17` | A* heuristic uses the cheapest cost any listed kind could have, not the cheapest tile on the map |
| CORE-WORLD-10 | low | unverifiable | new | `src/core/road_curve.cpp:264` | Road joins are found against the other roads' unjoined midlines, so a joined end can sit slightly off the other road's final curve |
| CORE-WORLD-11 | low | confirmed | new | `src/core/world_look.hpp:260` | WorldLook::Hash is a hand-kept field list with inline copies of its own helpers and no guard for new fields |
| CORE-WORLD-12 | low | confirmed | new | `src/core/world_map.cpp:736` | WorldMap::Hash rehashes the whole grid on every call; it could be cached at Build |
| CORE-WORLD-13 | low | partially-confirmed | new | `src/core/world_map.hpp:394` | Full-map arrays for sparse data: m_place, RoadCurveIndex's optional<Vec2>, and a u16 per tile per profile |
| CORE-WORLD-14 | low | deliberate | known-deferred | `src/core/leg_walk_cache.hpp:46` | LegWalkCache grows with distinct legs drawn per month (per-leg keying, origin per NPC tile) |
| CORE-WORLD-15 | low | partially-confirmed | new | `src/core/fog.cpp:31` | ExploreAround doesn't assert the radius cap or the centre that MAX_SIGHT's comment says keep it safe |
| CORE-WORLD-16 | low | confirmed | new | `src/core/world_map.cpp:513` | Duplicate place-key check is O(P^2) |
| CORE-WORLD-17 | low | partially-confirmed | new | `tests/unit/core/road_curve_tests.cpp:1` | Test gaps: JoinedEnd/JoinedHead/Midlines untested in core; no stricter-WALKING-than-base case; no large-map or large-cost cases |
| DATA-COMBAT-02 | low | partially-confirmed | new | `src/data/effects.cpp:20` | Three divergent field-reader abstractions (Fields, EffectFields, ad hoc expected+FirstError) |
| DATA-COMBAT-03 | low | confirmed | new | `src/data/seals.cpp:98` | Closed-range values may narrow to a denormal; doc says 'a value above 0 its f32 would hold as 0 or a denormal' is refused |
| DATA-COMBAT-04 | low | partially-confirmed | new | `src/data/abilities.cpp:349` | No validation of cross-field ability semantics (qi_cost above qi.max, zero-tick affliction, element/affliction mismatch) |
| DATA-COMBAT-05 | low | unverifiable | new | `src/data/abilities.cpp:501` | Duplicate keys in abilities, glows, glyph sets and loadout are silently merged or overwritten |
| DATA-COMBAT-06 | low | unverifiable | new | `src/data/effects.cpp:298` | Misspelt or unused new glow is silently accepted |
| DATA-COMBAT-07 | low | partially-confirmed | new | `src/data/abilities.cpp:13` | AfflictionAt and damage amounts allow up to 2^53 per hit with i64 accumulation |
| DATA-COMBAT-08 | low | unverifiable | new | `tests/unit/data/effects_tests.cpp:175` | Tests do not cover narrowing, duplicates, semantic combos or parse-then-Valid for abilities/effects |
| DATA-WORLD-01 | low | deliberate | new | `src/data/world_look.cpp:829` | Look refuses a map whose place keys it has no painting for, and every map kind needs a look: coupled to a fixed hand-written map |
| DATA-WORLD-02 | low | unverifiable | new | `src/data/world_map.cpp:166` | Grid literal-block check relies on yaml-cpp `Mark().pos` being a byte offset into the original string |
| DATA-WORLD-03 | low | confirmed | new | `src/data/world_map.cpp:99` | Kind cost limit in the reader (65534) disagrees with Build's (MAX_WALK_COST = 1000) |
| DATA-WORLD-04 | low | unverifiable | new | `src/data/world_map.cpp:374` | The reader accepts duplicate YAML keys silently |
| DATA-WORLD-05 | low | partially-confirmed | new | `tests/unit/data/world_map_tests.cpp:159` | Tests would not catch the grid-mark fragility, duplicate keys, or reader/Build bound disagreements |
| DATA-YAML-CORE-02 | low | confirmed | new | `tests/unit/data/settings_tests.cpp:183` | The settings round-trip test doesn't cover edgePan, so the omission above is invisible |
| DATA-YAML-CORE-03 | low | partially-confirmed | new | `src/data/settings.cpp:196` | Path::string() in messages and logs can throw on Windows for non-ASCII user folders |
| DATA-YAML-CORE-04 | low | confirmed | new | `src/data/game_data.cpp:24` | Several numbers are read with a bare as<T>(), giving a field-less 'bad conversion' message |
| DATA-YAML-CORE-05 | low | partially-confirmed | known-deferred | `src/data/game_data.cpp:80` | Orbit radius, wobble and speed are unranged, and orbit shape isn't validated |
| DATA-YAML-CORE-06 | low | unverifiable | new | `src/data/settings.cpp:17` | yaml-cpp's integer reader accepts octal and hex, so `width: 0720` is read as 464 |
| DATA-YAML-CORE-07 | low | confirmed | new | `src/data/settings.cpp:138` | A bad window value discards the whole settings file, unlike the warn-and-default policy for other settings |
| DATA-YAML-CORE-08 | low | confirmed | new | `src/data/yaml.cpp:19` | WriteText overwrites in place, so a crash mid-save leaves a truncated settings file |
| DATA-YAML-CORE-09 | low | deliberate | known-deferred | `src/data/yaml.cpp:244` | NoteUnknownKeys and KeyLine throw on non-scalar keys, and NoteUnknownKeys is exposed to alias amplification |
| DATA-YAML-CORE-10 | low | partially-confirmed | new | `src/data/yaml.cpp:177` | RefusalAt / Mark().line + 1 reports 'line 0' for null or programmatic nodes, and an alias reports its anchor's line |
| DATA-YAML-CORE-11 | low | deliberate | new | `src/data/strings.cpp:11` | Five private re-implementations of the 'what line N: why' refusal |
| DATA-YAML-CORE-12 | low | partially-confirmed | new | `src/data/names.cpp:36` | Duplicate keys are silently accepted in settings, creatures, names, manifest and every key lookup except strings and animations |
| DATA-YAML-CORE-13 | low | partially-confirmed | new | `src/data/manifest.cpp:139` | Manifest geometry (page_size, pages) is not validated |
| DATA-YAML-CORE-14 | low | partially-confirmed | new | `src/data/manifest.cpp:44` | Manifest accepts zero-size sprites, duplicate scene names, and `file` strings unchecked |
| DATA-YAML-CORE-15 | low | partially-confirmed | new | `src/data/names.cpp:36` | names.yaml entries aren't checked for duplicates or valid UTF-8, and the weight total isn't bounded |
| DATA-YAML-CORE-16 | low | confirmed | new | `src/data/strings.cpp:59` | strings.yaml text isn't checked for UTF-8, whitespace-only text, or an empty key, and 'null'-looking text is a Null node |
| DATA-YAML-CORE-17 | low | confirmed | new | `src/data/internal/yaml.hpp:137` | ReadNamed leaves the field untouched when the key is absent, and gives an empty name for a non-scalar |
| DATA-YAML-CORE-18 | low | partially-confirmed | new | `src/data/internal/yaml.hpp:1` | internal/yaml.hpp helpers have no direct tests, and TunedTicks edge cases are only covered through other tables |
| DATA-YAML-CORE-19 | low | deliberate | known-deferred | `tests/fuzz/yaml.cpp:49` | The fuzz harness has a fixed input budget and never exercises the Load* paths or alias bombs |
| DATA-YAML-CORE-20 | low | partially-confirmed | new | `tests/unit/data/game_data_tests.cpp:118` | Test gaps for Parse* error paths in game_data and animations |
| DESIGN-DOCS-07 | low | confirmed | new | `docs/design/0001-repository-build-environment.md:469` | Doc-tier rules contradict practice: accepted design docs are edited in place, and facts are deliberately duplicated |
| DESIGN-DOCS-08 | low | partially-confirmed | new | `docs/design/0001-repository-build-environment.md:408` | 0001 §8 command table is missing commands and options that exist |
| DESIGN-DOCS-09 | low | confirmed | new | `docs/design/0001-repository-build-environment.md:635` | 0001 §11.4 commit example uses a scope commit-check rejects |
| DESIGN-DOCS-10 | low | confirmed | new | `docs/design/0001-repository-build-environment.md:77` | 0001 §3 says libcxx is Linux-only; macOS links and ships conda's libc++ |
| DESIGN-DOCS-11 | low | confirmed | new | `docs/design/0001-repository-build-environment.md:374` | Rule 17 says Clang has LeakSanitizer; macos-clang does not |
| DESIGN-DOCS-12 | low | confirmed | new | `docs/design/0001-repository-build-environment.md:204` | §5.2 image tag inputs are stale |
| DESIGN-DOCS-13 | low | partially-confirmed | known-deferred | `docs/design/0001-repository-build-environment.md:713` | S2 says Windows artefacts are now reproducible, though MSVC builds still differ |
| DESIGN-DOCS-14 | low | partially-confirmed | new | `docs/design/0001-repository-build-environment.md:387` | Symbols are kept 'for crash reports' but no crash is ever captured, and terminate/signals don't flush the log |
| DESIGN-DOCS-15 | low | partially-confirmed | new | `docs/design/0001-repository-build-environment.md:388` | Hardening rule claims PIE, but nothing sets or verifies it |
| DESIGN-DOCS-16 | low | partially-confirmed | new | `docs/design/0001.1-git-lfs.md:73` | 0001.1 says 'six jobs' pull LFS; there are now eight |
| DESIGN-DOCS-17 | low | partially-confirmed | new | `docs/design/0001-repository-build-environment.md:643` | CI cancels in-progress runs on main, contradicting 'pushes to main run them again' |
| DESIGN-DOCS-18 | low | confirmed | known-deferred | `docs/design/0002-engine-architecture.md:5` | 0002 header is stale: 'Draft', 'phases 1 to 5', and 'Proposed' on decisions long built |
| DESIGN-DOCS-19 | low | confirmed | known-deferred | `docs/design/0002-engine-architecture.md:79` | 0002 module list and module table drift from the code (app missing from A1; zstd in core; stale sdl_log note) |
| DESIGN-DOCS-20 | low | confirmed | new | `docs/design/0002-engine-architecture.md:360` | §8.2 says only internal/gpu calls SDL_GPU; renderer.cpp and every feature call it directly |
| DESIGN-DOCS-21 | low | confirmed | new | `docs/design/0002-engine-architecture.md:44` | The spatial hash grid promised for collision doesn't exist; hit tests scan every enemy |
| DESIGN-DOCS-22 | low | confirmed | new | `docs/design/0002-engine-architecture.md:430` | Statistics via Tracy plots and a debug overlay are documented but not built |
| DESIGN-DOCS-23 | low | partially-confirmed | new | `docs/design/0002-engine-architecture.md:469` | §9's 'stable asset ID' is really a sprites.toml position, and saves don't record the manifest |
| DESIGN-DOCS-24 | low | partially-confirmed | new | `docs/design/0002-engine-architecture.md:535` | Delta saves keep a whole second world in memory and regenerate the world on every load; neither is in the §13 budget |
| DESIGN-DOCS-25 | low | confirmed | new | `docs/design/0002-engine-architecture.md:262` | FP environment is said to be 'checked on every worker', but only the logic thread checks, and only in assert builds |
| DESIGN-DOCS-26 | low | confirmed | known-deferred | `docs/design/0002.2-replays.md:44` | 0002.2 replay doc describes an API and file format three versions old and contradicts itself on sorting |
| DESIGN-DOCS-27 | low | confirmed | known-deferred | `docs/design/0002-engine-architecture.md:536` | Snapshot buffer count and instance size disagree within 0002 |
| DESIGN-DOCS-28 | low | deliberate | contradicts-recorded-decision | `docs/design/0002-engine-architecture.md:224` | Uncapped rendering by default, with vsync and frame cap settings unbuilt and S10 unrun |
| DESIGN-DOCS-29 | low | confirmed | new | `docs/design/0002-engine-architecture.md:225` | Pause on focus loss is described as 'a setting' but is hard-wired |
| DESIGN-DOCS-30 | low | deliberate | new | `docs/design/0002.2-replays.md:106` | Replay recordings grow without limit in memory and are lost on close |
| GAME-COMBAT-02 | low | confirmed | new | `src/game/combat.cpp:580` | An ability with element `healing` damages its targets while its events say Healing |
| GAME-COMBAT-03 | low | confirmed | new | `src/game/combat.cpp:100` | The `burn:`/`poison:` key is ignored; the element alone decides the affliction slot and its tick kind |
| GAME-COMBAT-04 | low | partially-confirmed | new | `src/game/combat.cpp:368` | A seal on the fire bolt is accepted by the data but can never show |
| GAME-COMBAT-05 | low | confirmed | new | `src/game/combat.cpp:141` | Hits on a creature that is already dying: emitted inconsistently, and their values overstate damage dealt |
| GAME-COMBAT-06 | low | partially-confirmed | new | `src/game/combat.cpp:443` | DissolveAt contradicts its doc when dyingTicks is 1 |
| GAME-COMBAT-07 | low | confirmed | new | `src/game/combat.cpp:822` | Combat::Load checks loaded volleys less strictly than flashes and seals |
| GAME-COMBAT-08 | low | partially-confirmed | new | `src/game/random_purposes.hpp:36` | Random purposes are coordinated across modules only by comments, and the game and gfx share seed 0x5eed |
| GAME-COMBAT-09 | low | confirmed | new | `src/game/components.hpp:63` | Enum Valid() checks rely on the last enumerator |
| GAME-COMBAT-10 | low | unverifiable | known-deferred | `src/game/combat.cpp:244` | Combat's ability model is a closed if-chain with hard-coded enemies and two affliction slots |
| GAME-COMBAT-11 | low | partially-confirmed | new | `src/game/combat.cpp:586` | `Holds` costs a scan of every flight and every afflicted entity per ending use |
| GAME-COMBAT-12 | low | confirmed | new | `tests/unit/game/combat_tests.cpp:1646` | Test name claims the lance's burst but only checks the aura; another lance-rule test asserts only `>= 1` |
| GAME-COMBAT-13 | low | partially-confirmed | new | `tests/unit/game/combat_tests.cpp:393` | Missing tests for combat edge cases |
| GAME-HOST-A-02 | low | partially-confirmed | new | `src/game/host.cpp:869` | A walk-to route that crosses the portal's step area sends the cultivator through the portal |
| GAME-HOST-A-03 | low | confirmed | new | `src/game/host.cpp:727` | Loading a dev-scene save builds the whole scene as its scratch base and keeps it (stress: about 100k active entities) for the session |
| GAME-HOST-A-04 | low | partially-confirmed | known-deferred | `src/game/host.cpp:298` | Most of Load's refusals are not logged |
| GAME-HOST-A-05 | low | partially-confirmed | new | `src/game/host.cpp:307` | Every load regenerates the game's world even when it is identical to the base already in memory |
| GAME-HOST-A-06 | low | partially-confirmed | new | `src/game/host.cpp:48` | The world seed is hard-coded; a Host can't be made for another seed |
| GAME-HOST-A-07 | low | partially-confirmed | new | `src/game/host.cpp:471` | A save can put a homed world NPC in the world scene's active domain, where no system handles it |
| GAME-HOST-A-08 | low | confirmed | new | `src/game/host.cpp:46` | Ability-use source numbers are spread across files with no central registry |
| GAME-HOST-A-09 | low | deliberate | known-deferred | `src/game/host.cpp:566` | The host contains game content: fox labels and health bars, the debug burst, the built-in data tables, the demo glows and the traveller sprite |
| GAME-HOST-B-01 | low | confirmed | new | `src/game/host.cpp:1561` | A route held when the player's cultivator is removed: PlayerBody() on a missing entity in AdvanceCalendar, and the next save is refused by its own game |
| GAME-HOST-B-02 | low | confirmed | new | `src/game/host.cpp:1290` | PublishJourneys visits every journey in the world each publish; culling to the view happens per member after Where() |
| GAME-HOST-B-03 | low | partially-confirmed | new | `src/game/host.cpp:979` | WalkTo runs a full-map-sized A* synchronously inside the tick for every click |
| GAME-HOST-B-04 | low | partially-confirmed | new | `src/game/host.cpp:1706` | SetData with a different world map keeps the world, its base and m_baseHash made for the old map |
| GAME-HOST-B-05 | low | deliberate | new | `src/game/host.cpp:1460` | Held walking on the world map has no bounds: the cultivator walks off the map and over impassable ground |
| GAME-HOST-B-06 | low | confirmed | new | `src/game/host.cpp:1412` | PublishFog copies the whole explored-tile bitset every publish, though FogVersion exists to say when it changed |
| GAME-HOST-B-07 | low | confirmed | new | `src/game/host.cpp:1527` | ExploreRoute rescans the route from its first tile every tick |
| GAME-HOST-B-08 | low | confirmed | new | `src/game/host.cpp:1663` | Load accepts any saved selection, which Select() would refuse |
| GAME-HOST-B-09 | low | partially-confirmed | new | `src/game/host.cpp:1685` | RouteFits accepts routes WalkTo could never make (non-adjacent or impassable steps, an end past TRAVEL_END) |
| GAME-HOST-B-10 | low | confirmed | new | `src/game/host.cpp:1409` | Each-frame publish helpers without PN_PROFILE_ZONE |
