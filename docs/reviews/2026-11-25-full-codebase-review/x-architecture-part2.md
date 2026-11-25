# Review: x-architecture (part 2)

### X-ARCHITECTURE-11: View models drift from 0002 A13: ad hoc Snapshot fields are filled on every publish, with no subscriptions or versioning

- **Severity:** low (reviewer said medium) · **Verdict:** deliberate · **Category:** architecture · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/frame.hpp:370`

0002 §8.14 and 0002.4 §8 specify UI data as subscribed views (ActionKind::Subscribe, Snapshot::views, shared_ptr<const> with versions, rebuilt only on change). Instead, every UI-facing datum has become its own Snapshot field that Host fills unconditionally on every publish: caster (CasterView), calendar (CalendarView), fog (FogView with a vector copy), route (vector), healthBars. Each new screen or HUD element adds a field to core::Snapshot and code to Host::Publish.

**Evidence:** frame.hpp 412-446 (healthBars, caster, calendar, fog, route fields); host.cpp 1789-1797 and 1876 (filled every publish); docs/design/0002.4-ui-and-text.md §8 (Subscribe, Snapshot::views, core/views.hpp); grep finds no 'Subscribe' or 'views' in src.

**Impact:** The planned NPC roster, sect, inventory, cultivation and relationship screens would each add a field and a per-publish fill. That cost grows with the number of screens rather than with what's shown. Snapshot becomes a god struct, and the A13 promise ('the UI can show any part of a large simulation without the snapshot carrying the whole state') is lost.

**Recommendation:** Build the view-model mechanism before the first real screen: a view registry keyed by (kind, target), Subscribe and Unsubscribe actions (never recorded), and versioned shared_ptr<const> payloads. Move CasterView, CalendarView, FogView and route onto it.

**Verification:** The 0002.4 §8 views, Subscribe and core/views.hpp are not in src (grep for Subscribe found nothing), but 0002.4 line 35 schedules them as phase6-ui chunk 5 (view models and the NPC roster), before the screens in chunk 6. The ad hoc Snapshot fields are the interim state, not a drift. The caution that new fields add per-publish cost is fair, but the fix is already planned.

### X-ARCHITECTURE-12: 0002's systems-and-commands model, spatial hash grid and per-tick arenas are not implemented; the docs describe machinery that doesn't exist

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** docs-vs-code · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `docs/design/0002-engine-architecture.md:241`

0002 §7.2 says systems are functions over explicit views, and that cross-entity writes are commands merged in an apply step after each system. §7.5 rule 3 repeats this. §2 and §4 list a spatial hash grid in core for collision, and §7.2 per-tick arenas. In the code:
- Host::Tick calls ad hoc functions in a hand-written order.
- Combat::Damage writes the target's Health directly.
- No command type or apply step exists, apart from MonthStep's local merge.
- There is no spatial grid; combat targeting scans every creature linearly (combat.cpp Tag), and bodies never collide.
- No arenas exist.

**Evidence:** grep finds no Command or apply-step code in src, no spatial grid, and core::Rate/RunsOn unused (schedule.hpp RunsOn has no caller; only InSlice is used, in npcs.cpp 304); host.cpp 850-917 (tick order inline); combat.cpp 72-87 (linear tag scan).

**Impact:** Parallelising the active tick or adding bullet-heavy combat (thousands of projectiles against hundreds of creatures) needs both the command apply step, for deterministic parallel writes, and a spatial index. Readers of 0002 will assume they exist. Retrofitting the apply step after many systems write directly is costly.

**Recommendation:** Either implement a minimal command buffer and apply step plus a deterministic spatial hash in core now, with a test, while there are few systems, or amend 0002 to say these are deferred and when they become required (for example, before the first system that writes across entities from a parallel chunk, or before projectiles at scale).

**Verification:** Confirmed that 0002 §7.2 (lines 241-244) and §7.5 describe a command and apply step and per-tick arenas, and that §2 describes a spatial hash. RunsOn in schedule.hpp has no caller. The design text reads as a target skeleton, not a description of finished code, so 'docs describe machinery that doesn't exist' is overstated. The point that 0002 should mark what is deferred is valid. I did not find a recorded deferral in the sweep-up backlog.

### X-ARCHITECTURE-13: The save and hash schema is enumerated by hand in host.cpp: each new system edits several functions and version constants

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** maintainability · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:85`

What is saved and hashed is spread over hand-written lists:
- ActiveComponents and WorldComponents (type lists);
- WriteClock, HashWorld, WriteScratch, WriteGameWorld, ReadGameWorld, ReadScratch, WriteScene and Load;
- the per-section version constants ARENA_FORMAT, ACTIVE_VERSION and WORLD_VERSION, with changelogs in comments.
Save and StateHash share WriteScene but not the world parts (HashWorld and WriteGameWorld are separate functions that must list the same fields). Look (sprite, layer, pipeline, scale) is presentation but is saved and hashed as an active component.

**Evidence:** host.cpp 78-109 (versions and component lists), 113-171 (Write/Hash functions listing the same fields twice), 1597-1635.

**Impact:** Adding sects, items or cultivation state means editing four or more functions in step. If a field is added to the save but not to HashWorld (or the reverse), replays and saves silently disagree. Render-only changes to Look force save-format bumps.

**Recommendation:** Make each system register one Fields-style description used by Save, Load, Hash and Delta alike: a save schema registry of (section, version, components, extra state). Add a test that mutating any saved field changes StateHash. Consider moving Look's layer and pipeline out of saved state (derive them from the creature kind).

**Verification:** I did not read host.cpp 85-171 closely. The hand-written Write/Hash/Read lists are plausible. Whether a test already covers 'mutating a saved field changes StateHash' would need a read of tests/. Hash and save drift is a real risk, but the severity depends on that test coverage.

### X-ARCHITECTURE-14: App mixes the main loop, input routing, hot reload and a profiling harness, with no input-context layering for upcoming UI screens

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** architecture · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/app.hpp:62`

Options mixes player settings, data tables and measurement scaffolding (burst, castSwords, cameraPath, monthStepAt, unpaced, quitAfterNs). App holds about 50 members, including the latency meters, path state and month-step harness. HandleInput is a fixed if/else priority chain: camera controls, quit, display toggles, cursor, date-line click, cast or select, then the action table. The date line is 'the HUD's one clickable area'. Nothing lets a modal UI layer (pause menu, dialogue, inventory) consume input before the world. 0002.1 §4 says App isn't unit-tested.

**Evidence:** app.hpp 62-117 and 258-307; app.cpp 510-570 (HandleInput chain), 345-431 (RepeatBurst, CastOnTimer, FollowPath, MeasureLatency inside App).

**Impact:** Every screen and every interaction mode (dialogue, trade, cultivation minigame, talisman tracing) will add branches to HandleInput and members to App. Untested routing bugs (input reaching the world through a menu, or the reverse) are likely. Harness code ships in the game's main class.

**Recommendation:** Add an input-context stack (UI layers, then cursor, then camera, then world) with each layer able to consume a ControlEvent, and unit-test the router without a window. Move the profiling and measurement drivers (burst, cast-swords, camera path, month-step-at, latency) into an `Autopilot` or `Harness` component built only from its options.

**Verification:** I did not open app.cpp or app.hpp. The UI input-layer concern overlaps with phase6-ui chunk 4 (the `ui` module), which is planned and would presumably own input consumption. The harness code living in App is a separate, valid cleanup.

### X-ARCHITECTURE-15: Renderer::Impl is a monolith with about 110 members and a hard-coded pass order; each feature adds a member, an upload function, flags and a failure counter

- **Severity:** low · **Verdict:** unverifiable · **Category:** architecture · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `src/gfx/renderer.cpp:199`

Impl has members for every feature (terrain, route, clouds, cursor, overlay, rivers, roads, seals, ribbons, glow table, fog, labels and stack caches). It also has 14 RepeatedFailure counters and per-feature 'shown', 'held' and 'recorded' bookkeeping, with DrawWorld's order written out by hand. Holds(scene), which scans the scene's whole sprite list and opens a profile zone, is called six or more times per frame (Upload, UploadTerrain, UploadGround, UploadRoute, UploadClouds, DrawWorld).

**Evidence:** renderer.cpp 199-454 (Impl members), 456-576 (Upload calls every feature's upload in turn), 1179-1276 (DrawWorld order), 1331-1336 (Holds).

**Impact:** The planned UI pass, bloom and blur post passes, minimap, DragonBones and GPU projectiles will each add more of the same. Ordering bugs and missed 'Submitted' bookkeeping (cf. known audit item 1) become likelier.

**Recommendation:** Give features a common interface (Prepare/Upload/Submitted/Draw with its own overflow reporting) and keep an ordered list of them. The no-render-graph decision can stand: an ordered list isn't a graph. Cache Holds() per frame.

**Verification:** I did not read renderer.cpp's Impl. The monolith claim is plausible and the finding is already tagged known-deferred. A real finding only for the Holds() per-frame caching suggestion, which is cheap to check.

### X-ARCHITECTURE-16: Debug and cheat controls are bound in every build, release included

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** product-risk · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/bindings.cpp:98`

The default bindings table always includes:
- M (skip a month);
- F2 (cycle through the dev scenes: arena, stress, painted, cells);
- F3 (debug sight, revealing travellers through the fog);
- F5 and F9 (quicksave and quickload);
- F6 and F10 (replays);
- F7 (4,000-hit burst);
- F8.
Escape quits at once, with no confirmation or save. Nothing is gated by NDEBUG or a developer setting.

**Evidence:** bindings.cpp 91-106; app.cpp 536-538 (Quit sets m_quit immediately).

**Impact:** Release builds let players into dev scenes, reveal the fog, stress the game and lose progress with a single key press. Fine for now, but easy to forget at release time.

**Recommendation:** Gate the debug controls behind a debug build or a `developer` setting in the bindings table, and route Escape to a pause menu once the UI exists. Add a test that release bindings contain no debug controls.

**Verification:** bindings.cpp:99-104 binds F2 (CycleScene), F3 (ToggleDebugSight) and F7 (DebugBurst) unconditionally. grep shows NDEBUG used only in renderer.cpp and app.cpp, not in bindings. So the debug keys are not gated by build type. Whether this is a release risk or intended for a pre-1.0 dev game I could not decide. Low severity either way.

### X-ARCHITECTURE-17: ActionKind is a closed core enum and Action a fat struct: every gameplay verb means a core edit

- **Severity:** low · **Verdict:** unverifiable · **Category:** extensibility · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/frame.hpp:488`

All tick input is core::ActionKind (17 kinds, mixing engine control such as Pause, Save, SceneResident and ReloadData with gameplay such as Cast, WalkTo and Select) plus one Action struct whose fields serve some kinds. These include a shared_ptr<const GameData> used only by ReloadData. ActionSet is a u32 bitmask, which static_asserts 32 kinds at most.

**Evidence:** frame.hpp 488-529; logic.hpp, using ActionSet = u32 and static_assert(ACTION_KINDS <= digits).

**Impact:** Talk, trade, recruit, join a sect, start a breakthrough, use an item and the UI decisions will each add a core enum value and Action fields. The replay format grows with them, and the 32-kind cap will be reached.

**Recommendation:** Split the engine-control actions (kept in core and logic) from gameplay commands. Carry gameplay commands as a tagged payload that `game` defines and serialises itself (for example, a GameCommand variant in the model module), recorded by replays as opaque bytes. Widen ActionSet or key it by engine kinds only.

**Verification:** I did not open frame.hpp 488-529. The closed ActionKind enum and the u32 bitmask with a 32-kind cap are plausible, and the finding is architectural opinion. Its premise (17 kinds, static_assert) was not checked, and the 32-kind limit is far off.

### X-ARCHITECTURE-18: The ECS facade is nominal: it exports entt::registry whole and gives everyone a mutable registry that bypasses State's ID invariants

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** api-design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/ecs.hpp:29`

core/ecs.hpp aliases Registry = entt::registry and exports EnTT's header. Domain::Entities() returns a mutable registry, so any code can call create() or destroy() directly, skipping State::Create (StableId, domain map, ID ranges). Game code uses raw EnTT API everywhere (storage<T>(), view<...>().each(), emplace), and the Publish code must fetch storages before forking because 'a storage asked for the first time is made'.

**Evidence:** ecs.hpp 29-31, 49-56; host.cpp 1772-1779 (the storage-creation race note), 208-224 (direct emplace into a target registry).

**Impact:** The facade can't enforce invariants, such as every entity having a StableId and structural changes never happening inside a chunk, and it can't hide EnTT. Mistakes show up only at load or hash time.

**Recommendation:** Give Domain read and component-write access (views, get, emplace on existing entities) but route create and destroy only through State. Pre-create every component storage at Domain construction (a fixed component list) so read-only access never mutates. Assert in debug that no structural change happens inside ParallelFor.

**Verification:** Domain::Entities() returns a mutable Registry (core/delta.hpp 282-313 use it for the delta apply), so the facade is thin. The claim that anyone can bypass State::Create is true in principle. I did not check whether callers actually do it. The storage-creation note is real. Low severity: the repo is pre-1.0, and the delta code needs the raw registry.

### X-ARCHITECTURE-19: 0002 has drifted from the code in ways that mislead readers

- **Severity:** low · **Verdict:** confirmed · **Category:** docs-vs-code · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `docs/design/0002-engine-architecture.md:5`

The status still says 'Draft' with phases 1 to 5 implemented. §5's state channel lists 'armature clips, view models, the static layers, the manifest version', none of which the Snapshot carries. §7.6 says hot reload 'builds a new manifest ... the snapshot carries the new manifest version', but the manifest is never reloaded. §4 lists a spatial hash grid in core and §7.2 per-tick arenas and commands, none of which exist. §7.3's per-system Rate is unused. 0002.1 §2's tree still says 'arena' for Host.

**Evidence:** 0002 line 5 (Status Draft), line 159 (§5 table), line 297 (§7.6 hot reload), lines 106-108 (§4 core contents), lines 241-249; schedule.hpp RunsOn has no caller; 0002.1 lines 24-28.

**Impact:** New designs and agents (recall-history reads docs/design first) will build on machinery that isn't there, or reinvent it differently.

**Recommendation:** Update 0002 with an 'as built' column or section per decision (built, deferred to when, replaced by which spec), and fix 0002.1's ownership tree.

**Verification:** The 0002 Status line still says Draft. schedule.hpp RunsOn exists but grep finds no caller in src. 0002.1 line 15 still says 'arena'. The finding is marked known-deferred, and the doc's own revision log shows it is being updated incrementally. Low severity.

### X-ARCHITECTURE-20: Content is compiled in: the SpriteId enum, AbilityKind, StringId and built-in creatures sit in headers that core and game logic include

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** extensibility · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:566`

The generated core/generated/atlas.hpp enum is included by eight src headers, including core/game_data.hpp and game/components.hpp. Game logic names sprites directly (TRAVELLER_SPRITE = Villager, the Shadow sprite, the Activate look Villager in npcs.cpp:262). BuiltInData (host.cpp 566-632) duplicates creatures.yaml and abilities.yaml in C++. AbilityKind's behaviours are switched in combat.cpp.

**Evidence:** host.cpp 566-632, 685; npcs.cpp 262; game_data.hpp 15; components.hpp 15.

**Impact:** Adding or renaming a sprite rebuilds most of the tree and changes saved values. Mods and data-only content are impossible. Two sources of truth (BuiltInData and the YAML) must be kept in step by a test.

**Recommendation:** Fold this into the planned data-driven sweep: runtime sprite and ability IDs from the manifest and data (string keys hashed or interned at load), no sprite names in game logic, and test fixtures that load the shipped YAML instead of BuiltInData.

**Verification:** The generated atlas enum and BuiltInData in host.cpp are real, as the cited lines show. The impact (no mods, two sources of truth) is a fair reading. The memory notes record procedural, data-driven maps as planned. I did not re-check the include count of eight or the npcs.cpp line. This belongs to the planned sweep, not a new defect.
