# Finding index 9/11 (standing findings, most severe first)

The ID prefix is the unit file: e.g. CORE-WORLD-03 is in core-world.md (or its -partN files).

| ID | Severity | Verdict | Status | Where | Finding |
|---|---|---|---|---|---|
| CORE-JOURNEY-VOLLEY-17 | info | confirmed | new | `src/core/volley.hpp:200` | Volley ticks are u64 simulation ticks, journey ticks are i64 world ticks: two tick types in core |
| CORE-PERSIST-18 | info | confirmed | known-deferred | `src/core/replay.cpp:85` | ReadTicks reserves up to Remaining() ticks × 16 bytes (Audit 2) |
| CORE-PERSIST-19 | info | partially-confirmed | new | `src/core/archive.hpp:106` | Hasher: UB shift for scalars wider than 8 bytes, and value/size mixing that can collide |
| CORE-PERSIST-20 | info | confirmed | new | `src/core/archive.hpp:216` | Reader::GetSequence assumes every non-scalar element takes at least one byte |
| CORE-PERSIST-21 | info | partially-confirmed | new | `src/core/save_file.cpp:13` | Save and replay files share one magic, so file kind is told apart only by format numbers |
| CORE-PERSIST-22 | info | deliberate | contradicts-recorded-decision | `src/core/replay.cpp:16` | Replays are stamped only with the semantic version, so a different build of the same version reports a false divergence |
| CORE-PERSIST-23 | info | partially-confirmed | new | `src/core/delta.cpp:8` | Release builds rely on .at() throws, and UB shifts for >64 fields, where only PN_ASSERT guards |
| CORE-PERSIST-24 | info | confirmed | new | `src/core/saves.hpp:5` | Saves doc overclaims 'the logic thread never waits on the disk' |
| CORE-SIM-13 | info | partially-confirmed | new | `src/core/random.hpp:54` | ToBelow and ToBelowWide return 0 for a bound of 0, outside their stated range |
| CORE-SIM-14 | info | partially-confirmed | new | `src/core/logic.cpp:23` | RunBurst would publish a bogus snapshot for a burst of zero ticks |
| CORE-SIM-15 | info | partially-confirmed | new | `src/core/frame.hpp:471` | Event and Snapshot comments describe things that no longer exist |
| CORE-SIM-16 | info | confirmed | new | `src/core/frame.hpp:459` | AbilityUse doesn't enforce its 8-bit source |
| CORE-SIM-17 | info | confirmed | new | `src/core/frame.hpp:511` | ACTION_KINDS depends on WalkTo staying the last enumerator |
| CORE-SIM-18 | info | confirmed | new | `src/core/effects.hpp:233` | Effects::LookOf gives the flying swords' look to every ability but the fire bolt |
| CORE-SIM-19 | info | deliberate | known-deferred | `src/core/seals.hpp:15` | Presentation-only tables and the main-thread frame code live in core, next to the simulation |
| CORE-SIM-20 | info | partially-confirmed | new | `src/core/logic.cpp:349` | A data reload during a replay's start change leaves the live game at the replay's start state |
| CORE-SIM-21 | info | confirmed | new | `src/core/logic.cpp:313` | The logic state in a replay's start comes before the save-format tag |
| CORE-WORLD-18 | info | partially-confirmed | new | `src/core/world_map.hpp:323` | Docs call the base layer 'walking' cost, but the WALKING profile may differ from it |
| CORE-WORLD-19 | info | partially-confirmed | new | `src/core/road_curve.cpp:110` | SHORTEST_RUN is used both as an absolute world length and as a share of the tile size |
| CORE-WORLD-20 | info | confirmed | new | `src/core/road_curve.cpp:104` | FindJoin's tie goes to the later line, undocumented and unlike RoadCurveIndex's first-on-tie |
| CORE-WORLD-21 | info | confirmed | new | `tests/unit/core/leg_walk_cache_tests.cpp:67` | Test name says a walk table is 'made once per leg' but the cache may make it several times and keep one |
| CORE-WORLD-22 | info | partially-confirmed | new | `src/core/world_map.cpp:199` | Build accepts up to 255 terrain kinds, but the ground and look hold only MAX_GROUND_KINDS (16) |
| CORE-WORLD-23 | info | confirmed | new | `src/core/world_map.cpp:428` | Profile cost precedence makes a bridge override the river: a river-going profile can't pass under bridges unless its bridge cost is set |
| CORE-WORLD-24 | info | unverifiable | new | `src/core/world_map.cpp:580` | Travel rules and the world's population cap live in the map source; WithTravel copies every layer to change one rule |
| CORE-WORLD-25 | info | unverifiable | new | `src/core/world_map.cpp:734` | Hash includes TerrainKind::symbol, the grid's text encoding |
| CORE-WORLD-26 | info | deliberate | contradicts-recorded-decision | `src/core/world_map.cpp:634` | Region search and the reach check deliberately use base costs, so a city may be unreachable for WALKING travellers |
| DATA-COMBAT-09 | info | unverifiable | new | `src/data/seals.cpp:734` | Satellite nesting multiplies past the drawn-seal cap without any load-time check |
| DATA-COMBAT-10 | info | unverifiable | new | `src/data/seals.cpp:330` | Ticks part's length is not checked against its radius |
| DATA-COMBAT-11 | info | confirmed | new | `src/data/abilities.cpp:500` | Combat reader narrows u64 fallbacks to u32 with a cast |
| DATA-WORLD-06 | info | confirmed | new | `src/data/world_map.cpp:125` | The 255-kind limit in ReadKinds is unreachable |
| DATA-WORLD-07 | info | partially-confirmed | new | `src/data/world_map.cpp:39` | Duplicated numeric helpers: local `Whole`, `Point` and `Text` beside internal::WholeNumber and RequiredNumber |
| DATA-WORLD-08 | info | deliberate | known-deferred | `src/data/world_look.cpp:436` | world_look.cpp is a long run of repeated expected-unwrapping boilerplate |
| DATA-WORLD-09 | info | confirmed | new | `src/data/world_map.cpp:14` | Local constant named DELETE may collide with the Windows macro |
| DATA-WORLD-10 | info | confirmed | new | `src/data/world_look.cpp:853` | by_key painting lookup is O(paintings x places) |
| DATA-YAML-CORE-21 | info | confirmed | new | `src/data/game_data.cpp:16` | MAX_HEALTH comment says below 2^53 but the value is exactly 2^53 |
| DATA-YAML-CORE-22 | info | partially-confirmed | new | `src/data/yaml.cpp:7` | ReadText has no size cap and doesn't detect directories, and WriteText/ReadText failures are untested |
| DATA-YAML-CORE-23 | info | confirmed | new | `src/data/manifest.cpp:44` | Manifest sprite parse evaluates every field even after the first error, and sprites[i] indexing is repeated |
| DATA-YAML-CORE-24 | info | confirmed | new | `src/data/animations.cpp:18` | animations.yaml refuses unknown keys while the other readers warn and ignore them |
| DATA-YAML-CORE-25 | info | unverifiable | new | `src/data/animations.cpp:70` | Animation frames aren't checked against the standing sprite's size or foot, and a frame list may repeat or include the standing sprite |
| DESIGN-DOCS-31 | info | confirmed | new | `docs/design/0001-repository-build-environment.md:365` | Rule 14 points at a 'shortcut:' marker that doesn't exist, and the upstream report isn't tracked |
| DESIGN-DOCS-32 | info | confirmed | new | `docs/design/0001-repository-build-environment.md:449` | §9 says each test main uses DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN alone, contradicting rule 19 |
| DESIGN-DOCS-33 | info | partially-confirmed | new | `docs/design/0002-engine-architecture.md:29` | Non-goals contradict planned work: modding and localisation |
| DESIGN-DOCS-34 | info | deliberate | contradicts-recorded-decision | `docs/design/0002-engine-architecture.md:457` | DragonBones is still the planned skeletal runtime though its core is stale and S14 has never run |
| DESIGN-DOCS-35 | info | partially-confirmed | new | `docs/design/0002-engine-architecture.md:267` | The contraction decision is untested on x86-64 and silently changes if the ISA baseline rises |
| DESIGN-DOCS-36 | info | partially-confirmed | new | `docs/design/0002.3-input-data-scenes.md:60` | core::Action carries a shared_ptr<GameData> in every action, including recorded and queued ones |
| GAME-COMBAT-14 | info | partially-confirmed | new | `src/game/combat.cpp:340` | `cooldown: 0` lets two casts of one slot in one tick share a use ID and a volley seed |
| GAME-COMBAT-15 | info | unverifiable | new | `src/game/combat.cpp:314` | Lance tags at end-of-tick positions; the swords and the bolt tag at positions before the tick's moves |
| GAME-COMBAT-16 | info | confirmed | new | `src/game/combat.cpp:43` | HeightOf in combat.cpp duplicates HeadOf in components.cpp |
| GAME-COMBAT-17 | info | partially-confirmed | new | `src/game/combat.hpp:35` | combat.hpp uses std::to_underlying without including <utility> |
| GAME-HOST-A-10 | info | unverifiable | new | `src/game/host.hpp:67` | Host is a god class mixing simulation state with presentation-only caches |
| GAME-HOST-A-11 | info | partially-confirmed | new | `src/game/host.hpp:251` | Test-only mutators on the production Host API bypass input and replays |
| GAME-HOST-A-12 | info | confirmed | new | `src/game/host.cpp:426` | ClearScene's Deactivate branch can't be reached by current scenes, though the docs present it as core behaviour |
| GAME-HOST-A-13 | info | confirmed | new | `src/game/host.cpp:548` | RouteFits doesn't tie a loaded route to the player's position or the clock |
| GAME-HOST-A-14 | info | partially-confirmed | new | `src/game/host.hpp:487` | The game's world is kept twice for the whole run, and four times during a load |
| GAME-HOST-A-15 | info | confirmed | new | `src/game/host.hpp:232` | Portal(), View(), OnPortal() and OnMapPortal() are non-const queries because WorldContext needs a mutable world |
| GAME-HOST-A-16 | info | unverifiable | new | `src/game/host.hpp:80` | The Doxygen for Tick(), Load() and Publish() narrates the implementation and is already hard to keep accurate |
| GAME-HOST-A-17 | info | confirmed | new | `src/game/host.cpp:85` | ARENA_FORMAT names the whole host save format; ACTIVE_VERSION's comment is split by formatting |
| GAME-HOST-A-18 | info | confirmed | new | `src/game/host.cpp:708` | FactsOf() repeats the manifest's scene lookup by name on every Current() call although m_sceneId already holds it |
| GAME-HOST-B-11 | info | partially-confirmed | new | `src/game/host.cpp:1585` | AdvanceCalendar clears the leg-walk cache and kept walks even when the game's journeys did not change |
| GAME-HOST-B-12 | info | confirmed | new | `src/game/host.cpp:1571` | A stop request during AdvanceCalendar leaves a half-stepped month |
| GAME-HOST-B-13 | info | confirmed | new | `src/game/host.cpp:1197` | Overhead labels and fox health bars are not culled to the publish area |
| GAME-HOST-B-14 | info | confirmed | new | `src/game/host.cpp:1882` | The selected instance is found by a linear search of every published ID |
| GAME-HOST-B-15 | info | confirmed | new | `src/game/host.cpp:1611` | ARENA_FORMAT names the host's save format for every scene |
| GAME-HOST-B-16 | info | unverifiable | new | `src/game/host.cpp:1457` | Walk() repeats PlayerBody()'s lookup |
| GAME-HOST-B-17 | info | confirmed | new | `src/game/host.cpp:1805` | The camera follows the body while the cultivator is drawn shifted onto the road |
| GAME-HOST-B-18 | info | confirmed | new | `src/game/host.hpp:156` | host.hpp docs: a garbled clause in Load, and an overstated walk-keeping claim for M's jump |
| GAME-HOST-B-19 | info | partially-confirmed | new | `src/game/host.cpp:1679` | Load checks the kept map place against the portals of the running game's context, not the loaded world's |
| GAME-SCENES-15 | info | confirmed | new | `src/game/scenes/world.hpp:21` | WORLD_BUSIEST is a fixed coordinate in the shipped map, living in game code |
| GAME-SCENES-16 | info | deliberate | new | `art/scenes.toml:46` | Creature and animation sprite rules force unused sprites into scenes (fox into world, villager walk frames into arena and stress) |
| GAME-SCENES-17 | info | confirmed | new | `src/game/scenes/arena_layout.cpp:172` | Tablet caption disappears when the player dies, because of a player check left over from a refactor |
| GAME-SCENES-18 | info | confirmed | new | `src/game/scenes/stress.cpp:99` | StressScene::Respawn can never fire: foxes can't die in a non-combat scene |
| GAME-SCENES-19 | info | partially-confirmed | new | `src/game/scenes/world.cpp:60` | World place labels scan every place on each publish |
| GAME-SCENES-20 | info | confirmed | new | `src/game/scenes/arena_layout.hpp:33` | Tests duplicate scene constants instead of using them |
| GAME-SCENES-21 | info | partially-confirmed | new | `src/game/scenes/arena.cpp:78` | Aura emits the full heal amount even when the player is already at full health |
| GAME-TRAVEL-NPCS-14 | info | partially-confirmed | new | `src/game/npcs.hpp:67` | Npc::cell means a 64-column grid cell in some scenes and a world-map tile index in others |
| GAME-TRAVEL-NPCS-15 | info | confirmed | new | `src/game/travel.hpp:255` | m_travellers duplicates Npc::action == Action::Travel, adding a hash set and a lookup per NPC per month |
| GAME-TRAVEL-NPCS-16 | info | confirmed | new | `src/game/travel.cpp:298` | With assertions off, a journey without parts gets a journal entry built from made-up ticks |
| GAME-TRAVEL-NPCS-17 | info | confirmed | new | `src/game/travel.cpp:221` | A loaded NPC standing on another city's tile always 'visits' that city with zero-length legs |
| GAME-TRAVEL-NPCS-18 | info | partially-confirmed | contradicts-recorded-decision | `src/game/travel.hpp:122` | Members of a journey that ended mid-month stay Action::Travel until the next month step |
| GAME-TRAVEL-NPCS-19 | info | confirmed | new | `src/game/travel.cpp:231` | DestinationWeight uses std::array::at, which can throw |
| GAME-TRAVEL-NPCS-20 | info | confirmed | new | `src/game/world_state.cpp:53` | GenerateWorld's freshness assert checks only the world domain and the clock, not relationships, travel or the active domain |
| GAP1-CLOCK-DISCIPLINE-PAUSE-10 | info | partially-confirmed | new | `src/gfx/renderer.cpp:478` | The same render time is derived four different ways per frame |
| GAP1-CLOCK-DISCIPLINE-PAUSE-11 | info | partially-confirmed | new | `src/core/logic.hpp:174` | Replays ignore focus loss, and no clock is ready for playback at another speed |
| GAP1-HOT-RELOAD-FLOW-12 | info | confirmed | new | `src/app/app.cpp:363` | --cast-swords reads costs and cooldowns from the new data against a snapshot still made under the old data |
| GAP1-HOT-RELOAD-FLOW-13 | info | partially-confirmed | new | `src/core/frame.hpp:520` | Every Action carries a shared_ptr<const GameData> that only debug hot reload uses |
| GAP1-HOT-RELOAD-FLOW-14 | info | confirmed | new | `src/main.cpp:135` | With --travel-share the start-time map is held twice for the whole run, and the look is checked against the copy the game doesn't run |
| GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-11 | info | partially-confirmed | new | `src/app/app.cpp:652` | Duplicated and mis-levelled INFO lines on user actions |
| GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-12 | info | partially-confirmed | new | `src/app/app.hpp:223` | Plain-comment rule: what-style comments on private declarations written with // instead of Doxygen, and a few placement/describing comments |
| GAP1-SCENE-TRANSITION-FLOW-10 | info | confirmed | new | `src/app/hud.cpp:99` | The 'Not enough Qi' warning carries across a portal between combat scenes |
| GAP1-SCENE-TRANSITION-FLOW-11 | info | partially-confirmed | new | `src/app/app.cpp:760` | A camera drag in progress carries across a scene change and frees the new scene's camera at once |
| GAP1-SCENE-TRANSITION-FLOW-12 | info | confirmed | new | `src/gfx/labels.hpp:262` | Presentation state keyed by StableId assumes IDs are unique across scenes, but world-range IDs repeat between the game world and every scratch world |
| GAP1-SIM-BOUNDARY-COMPLETENESS-10 | info | unverifiable | new | `src/core/logic.cpp:289` | Replay hashing walks the entire world (NPC domain, every journey and journal, the explored bits) every second on the logic thread while recording or playing |
| GAP1-SIM-BOUNDARY-COMPLETENESS-11 | info | partially-confirmed | new | `src/core/game_data.hpp:97` | names.yaml is part of GameData::Hash and BaseHash though the simulation never reads names |
| GAP1-SWEEP-UP-BACKLOG-VS-CODE-08 | info | confirmed | known-deferred | `tools/project_n.py:2230` | Backlog audit items with no review finding: sanitizer wording, untested journal day clamp, paused-frame animation |
| GAP1-SWEEP-UP-BACKLOG-VS-CODE-09 | info | partially-confirmed | new | `/home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md` | Backlog line numbers and locations have drifted |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-14 | info | partially-confirmed | new | `src/core/log.cpp:39` | The quill backend thread polls every 100 µs for the whole run and is not counted against the job workers |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-15 | info | unverifiable | new | `src/core/ecs.hpp:29` | The EnTT facade is the raw registry: the 'no structural changes inside a job chunk' rule is unenforced |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-16 | info | unverifiable | new | `src/core/ecs.hpp:30` | Component reference stability is undocumented while code holds references across calls that could reorder the same pool |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-17 | info | unverifiable | new | `src/core/ecs.hpp:71` | Each entity costs two node-based unordered_map entries, multiplied by every world copy |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-18 | info | confirmed | new | `src/game/npcs.cpp:360` | MonthStep and SetOut look each entity up by sparse index to reach a value already at packed index i |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-19 | info | partially-confirmed | new | `src/core/log.cpp:67` | Quill timestamps use the TSC clock by default; System is simpler for a game's diagnostic log |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-20 | info | partially-confirmed | new | `src/core/log.cpp:67` | An unbounded blocking queue lets a runaway log loop take up to 2 GiB per thread before it blocks |
| GAP1-THIRD-PARTY-LIBRARY-USAGE-21 | info | confirmed | new | `src/core/thread_safety.hpp:36` | Lock waits and allocations are invisible in Tracy captures |
| GAP2-COORDINATE-SPACES-AND-PICKING-14 | info | partially-confirmed | new | `src/app/view.cpp:94` | Picking ignores the cloud shade, so with the debug sight a walker hidden under clouds is hoverable and selectable while its label is hidden |
| GAP2-COORDINATE-SPACES-AND-PICKING-15 | info | confirmed | new | `src/game/host.cpp:1385` | Route line starts at the end-of-tick drawn foot while the sprite is interpolated |
| GAP2-COORDINATE-SPACES-AND-PICKING-16 | info | confirmed | new | `src/core/draw_list.hpp:87` | Two constants named CULL_MARGIN with different units and meanings |
| GAP2-DESIGN-DOCS-0002X-DRIFT-08 | info | confirmed | new | `docs/design/0002.4-ui-and-text.md` | 0002.4.2 vertical text direction contradicts the code |
| GAP2-DESIGN-DOCS-0002X-DRIFT-09 | info | partially-confirmed | known-deferred | `docs/design/0002.4-ui-and-text.md` | 0002.4 §7 to §11 (RmlUi ui module, roster, pause menu, save slots, transition, prompts, bindings.yaml) are unbuilt but read as a live plan |
| GAP2-DESIGN-DOCS-0002X-DRIFT-10 | info | confirmed | new | `docs/design/0002.1-app-and-game.md` | 0002.1 says Logic takes `Saves` calls 'on the thread that created the job system', but the logic thread is registered, not the creator |
| GAP2-DESIGN-DOCS-0002X-DRIFT-11 | info | confirmed | known-deferred | `src/core/frame.hpp:520` | 0002.3's shared_ptr<GameData> in every Action is still in the code |
