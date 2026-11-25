# Review: gap1-sim-boundary-completeness

**Scope:** Trace the whole boundary of the deterministic simulation and check that it is complete: (1) list every mutable member that affects future simulation results in game::Host (host.hpp), game::Combat, Travel, Npcs/Relationships, world_state, core::Logic, State/ComponentList and their caches, and for each one say whether Host's save (delta and scratch paths), the replay start state and the state hash all capture and restore it; flag members that are saved but not hashed, hashed but not saved, or neither (for example cooldown or use-ID counters, the RNG stream positions, kept walks, the selection, the route, fog, the journey journal). (2) List every channel through which the main thread or the environment can change what the simulation computes: recorded Actions, unrecorded action kinds, latest-value side channels (view, hover), the publish or view area, settings, data reloads, the manifest's sprite metrics (footY), the worker count, wall time. For each one say whether a replay captures it and whether GameData::Hash or BaseHash covers it. Earlier units found single instances of this (ComponentList omissions are silent, combat reads footY outside the data hash, the selection loads unchecked, presses dropped on special ticks). This unit does the systematic inventory, so it should not repeat those findings. It should find the members and channels that nobody has listed yet, and say which existing test (save_exactness, replay_tests) would or would not catch each one. (Why this unit exists: Per-module reviewers each saw part of the save, hash and replay schema, and x-determinism hunted classic nondeterminism sources (FP, unordered iteration). Nobody listed every piece of simulation state and every input channel to check the save/hash/replay boundary end to end. This boundary is enumerated by hand in several functions, so it is the most likely place for silent desyncs and lost state.)

**Findings:** 11 standing (0 critical, 0 high, 0 medium, 9 low, 2 info), 0 raised but refuted on verification.

**Parts:** gap1-sim-boundary-completeness.md, gap1-sim-boundary-completeness-part2.md

## Assessment

## Scope: the whole save / hash / replay boundary of the deterministic simulation

I traced every mutable member of `game::Host`, `game::Combat`, `game::Travel`, `game::Relationships`, `game::MonthStep`, `game::WorldState`, `core::State` / `core::Domain` / `ComponentList`, and `core::Logic`. I also traced every way the main thread or the environment can reach `Host::Tick` or `AdvanceCalendar`. For each, I checked three paths: the plain save (`Logic::Serialise(false)` → `Host::Save`), the replay start (`Serialise(true)`), and the hash (`Logic::Hash` → `Host::StateHash`). Findings already recorded elsewhere (ComponentList omissions are silent, combat reads `footY` outside the data hash, the selection loads unchecked, presses are dropped on special ticks, Audit 15's sight radius) are only marked "known" in the tables below. They are not reported again.

### Overall verdict
The boundary is in good shape, and better than most projects at this stage.
- **No RNG stream state:** every draw is counter-based, keyed on (seed, `counters.state`, key, purpose, index).
- **No use-ID or cooldown counters:** use IDs are `AbilityUse(simulation tick, source)` and cooldowns live in `Caster::readyTick`, so there is no hidden counter to lose.
- **Order is part of the save:** `SaveDomain` and `SaveDomainOrder` write storage order, and the load gives it back.
- **Presentation state is labelled:** every presentation-only member says so in a comment, and most have a test showing it leaves `StateHash` alone ("glows leave the state hash alone", "debug sight changes no state", "walking changes no hash", "the cultivator's walk changes no hash", "the view is never recorded and never hashed").

The weak points are not missing members. They are:
1. **The base identity is checked by a version number, not by content.** `GENERATOR_VERSION` is still 1 after several commits that touched generation, and the scratch base has no identity check at all.
2. **Every load and every replay start regenerates the whole game world**, although the host already holds an identical base.
3. **The checks are shallower in a few places:** the set-aside game world is never hashed during a replay, the replay header pins a version string rather than a binary, and `SetData` half-supports a map change.

### Inventory 1: mutable members and how the boundary treats them
Columns: S = plain save, R = replay start, H = `StateHash` / `Logic::Hash`.

| Member | S | R | H | Note |
|---|---|---|---|---|
| Logic `m_counters` (wall, simulation, state) | ✓ | ✓ | ✓ | |
| Logic `m_paused` | ✗ (a load keeps the game's own pause, by design) | ✓ | ✓ | |
| Logic `m_calendar` | ✗ (a load resets it to None) | ✓ | ✓ | A step asked for on a save tick is lost (finding) |
| Logic `m_heldPausing` | n/a | recorded as the next tick's action | – | Saves cannot happen during a wait |
| Logic `m_windowPaused`, `m_epoch`, `m_change`, `m_resident`, `m_restored`, `m_advanced` | ✗ | ✗ | ✗ | Not simulation state (pacing, presentation, or pending) |
| Host `m_scene` | ✓ (u8) | ✓ | ✓ | |
| Scene-ID counter | ✓ (header) | ✓ | ✓ | |
| Host `m_player`, `m_route` | ✓ | ✓ | ✓ | |
| Host `m_selected` | ✓ | ✓ | ✓ | Unchecked on load (known) |
| Host `m_world` while a dev scene runs | ✓ (delta) | ✓ | **✗** | Documented: "set aside". Test gap, finding |
| Host `m_world` / Live: seed, world-ID counter, `worldTicks`, `months`, `paused`, `mapPlace`, `sightRadius`, `explored`, world domain plus its order, relationships, journeys, journals | ✓ | ✓ | ✓ | |
| Host `m_base`, `m_baseHash`, `m_scratchBase`, `m_scratchBaseScene` | identity only | identity only | ✗ | Base content is never verified (finding) |
| Combat `m_respawns`, `m_flights` (record), `m_flashes`, `m_seals` | ✓ | ✓ | ✓ | |
| Combat Flight `order`, `arrivals`, `lastArrival` | derived | derived | – | `Prepare()` rebuilds them on load |
| Combat `m_rules` | from data | from data | data hash | |
| Combat `m_strikes` | – | – | – | Empty between ticks, asserted |
| Caster Qi and `readyTick` (cooldowns) | ✓ | ✓ | ✓ | |
| Travel `m_travellers` | rebuilt | rebuilt | – | Lookup only |
| Travel `m_stamp` | – | – | – | Presentation, global atomic |
| Travel scratch buffers; Relationships `m_merged`; MonthStep buffers | – | – | – | Cleared before each use |
| State `m_where`, Domain `m_entities`, EnTT handles and free list | derived | derived | – | Nothing iterates by handle; views follow pool order, which is saved |
| Host `m_paths` | cache | cache | – | Pure function of the map |
| Presentation: `m_roadCurves`, `m_legWalks`, `m_keptWalks`, `m_keptWalksStamp`, `m_drawnShift`, `m_playerWalk`, `m_worldTicksBefore`, `m_fogVersion`, `m_debugSight`, `m_animations`, `m_travellerLabels`, `m_drawList` | ✗ | ✗ | ✗ | Correctly presentation; reset on load |
| Host `m_requested`, `m_loaded` | – | – | – | Only exist during a scene change, when save and record are impossible |

### Inventory 2: channels into what the simulation computes

| Channel | Replay captures it? | Data hash or `BaseHash` covers it? |
|---|---|---|
| Actions Pause, Resume, AdvanceCalendar, Save, Select, CycleScene, DebugBurst, Cast, ToggleWorldPause, WalkTo | Recorded with their target, slot and point (truncated past 255 actions a tick: finding) | – |
| Held move | Recorded per tick | – |
| Load, ToggleRecording, PlayReplay, ReloadData | End or replace the timeline | – |
| SceneResident, SceneLeft, SceneEntered | Never recorded; they only gate waiting, and no counter moves | – |
| Latest-value view (area, change, debug sight, animations) | Not recorded | Presentation only; purity tested |
| Hover | Main thread only | – |
| Settings | Never reach the logic thread | – |
| `creatures.yaml`, `abilities.yaml` | Data hash at replay start; a reload ends recording or playback | Hash yes, Base no (by design) |
| `world.yaml`, `--travel-share` | as above | Hash and Base |
| `names.yaml` | as above | Hash and Base, though the simulation never reads names (info) |
| effects, seals, animations, world look | Not recorded | Presentation, not hashed (correct) |
| Manifest `footY` | Not recorded | No (known) |
| Sprite IDs and the atlas, code, scene list, `SEED`, `GENERATOR_VERSION` | Replay pins only the version string (finding) | – |
| Worker count | Fixed-size chunks everywhere | Replay is never played back on another worker count in a gate (finding) |
| Wall time and burst sizes | Affect only which held input a tick gets, and that is recorded per tick | – |
| FP environment | Set on every worker, asserted on the logic thread | x-determinism's scope |

### Which tests would catch what
- **save_exactness** compares `StateHash`, `WorldHash` and published bytes across saved, never-saved and loaded games, on 1 and 4 workers.
- **replay_tests** compare `Logic::Hash` through the real logic, but always on 2 workers, with one manifest, one binary, and no world → dev → world round trip.
- **Neither suite can detect** generator drift (both sides regenerate with the same code), a different manifest, or a cross-build replay.

### Risks
The highest-value fix is to write a hash of the base world into the save and compare it on load. It closes the silent-corruption hole that a forgotten `GENERATOR_VERSION` bump opens, and it costs nothing extra because the load already regenerates the base. Next is reusing `m_base` on load: every F9 and every replay start rebuilds a 50,000-NPC world, and that cost grows with map size.

**Files read in full:** src/game/host.hpp, src/game/host.cpp, src/core/state.hpp, src/core/logic.hpp, src/core/logic.cpp, src/game/combat.hpp, src/game/combat.cpp, src/game/travel.hpp, src/game/travel.cpp, src/game/npcs.hpp, src/game/npcs.cpp, src/game/world_state.hpp, src/game/world_state.cpp, src/game/scene.hpp, src/game/scenes.cpp, src/game/scenes/world.cpp, src/game/scenes/arena_layout.cpp, src/core/frame.hpp, src/core/replay.hpp, src/core/replay.cpp, src/core/tick_clock.hpp, src/core/game_data.hpp, src/core/ecs.hpp, src/core/ecs.cpp, src/core/save_file.hpp, tests/support/exactness.hpp, tests/unit/game/save_exactness_tests.cpp, tests/unit/game/replay_tests.cpp

## Findings

### GAP1-SIM-BOUNDARY-COMPLETENESS-01: The regenerated base world's content is never verified on load; GENERATOR_VERSION is a manual guard (still 1) and the scratch base has no identity at all

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** save-integrity / determinism · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:276`

A save stores the game's world as a delta against a base that the load regenerates (ReadGameWorld → GenerateWorld). The only identity checks are the seed, GENERATOR_VERSION and data.BaseHash(). GENERATOR_VERSION must be bumped by hand whenever any code feeding generation changes. That code includes StartingPower, SpawnPeople, Travel::Seed/SendOut, ChooseDestination, DestinationWeight, core::Visit, RouteDown, the Paths tie-breaks and SortCanonical. It was introduced in 9be1c40a and is still 1, although later commits changed generation-affecting code: 1b812356 'send travellers by the profile their power picks' and 0b383414 'step down a distance map only through cheaper tiles'. Those were covered only because WORLD_VERSION happened to be bumped at the same time. The scratch world's section (WriteScratch/ReadScratch) has no identity at all: no seed, no version, no data hash. It relies on BuildScratch, SEED, worldNpcs, ACTIVE_CELLS and SpawnArenaNpcs staying exactly as they were.

**Evidence:** world_state.hpp:30 `inline constexpr u32 GENERATOR_VERSION = 1;` (git log -S shows it unchanged since 9be1c40a). host.cpp:276-307 ReadGameWorld checks only `generator != GENERATOR_VERSION` and `baseHash != data.BaseHash()`, then `GenerateWorld(base, data, paths)`. host.cpp:141-149 WriteScratch writes no seed, version or base hash.

**Impact:** If generation changes without a bump, an old save loads 'successfully' onto a different base. Travel `gone` places index into a different journey list, NPC deltas apply to NPCs with other cells or powers, and kept-order bytes describe another order. The result is silently wrong state, not a refusal. Many such mismatches pass LoadDelta's structural checks (places in range, IDs present). No existing test detects it: save_exactness and save_tests regenerate the base with the same code on both sides, and the only GENERATOR_VERSION test patches the number.

**Recommendation:** Write a hash of the base each section was written against into the save: HashWorld(m_base), and for the scratch section a hash of the world tier of m_scratchBase. On load, compare it with the regenerated or kept base and refuse on mismatch, logging 'its base world differs'. This costs nothing extra because the load already builds the base, and it makes GENERATOR_VERSION a diagnostic rather than the only guard. Also add a golden test: WorldHash of GenerateWorld on test::SmallData() with a fixed seed, pinned beside GENERATOR_VERSION, so a generation change fails CI until both are updated in the same commit.

**Verification:** Checked host.cpp:270-307: the identity is seed, GENERATOR_VERSION and BaseHash, and GENERATOR_VERSION is still 1 (world_state.hpp:30). The spec (2026-11-16 chunk5c1 design) and design 0002 §293 record 'a constant raised whenever generation code changes' as the deliberate design, so a manual guard is intended. The gap is real: no automated check forces a bump, and the codebase review's game-travel-npcs finding already suggests a golden generation hash. The scratch section writes no identity. Hashing the base into the save is a reasonable hardening, but the claim of a defect is overstated.

### GAP1-SIM-BOUNDARY-COMPLETENESS-02: Every load and every replay start regenerates the game's whole world from the seed, although the host already holds that exact base in m_base

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** performance / wasted work · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:307`

ReadGameWorld always calls GenerateWorld(base, data, paths). That spawns every city's people (50,000 on the shipped map) and runs Travel::Seed, which is two months of SendOut, routing every departing NPC. The host already keeps m_base, 'the game's world as it was made, never ticked'. Every game uses the same SEED constant and the same in-process GENERATOR_VERSION, and m_baseHash is checked equal to data.BaseHash(). So the regenerated base is bit-identical to m_base in every case the app can reach. The scratch path already does the cheap thing: ScratchBase() reuses m_scratchBase when the scene matches. CopyOf()'s own comment says copying is 'far cheaper than making it again, which routes every journey'.

**Evidence:** host.cpp:307 `GenerateWorld(base, data, paths);` runs unconditionally. host.hpp:487 `WorldState m_base; // the game's world as it was made, never ticked`. host.cpp:727-736 ScratchBase reuses m_scratchBase. host.cpp:235-236: CopyOf is 'Far cheaper than making it again, which routes every journey'.

**Impact:** F9 and F10 on the logic thread pay a full world generation on a frozen tick: 'a large part of a second' for the shipped map in debug, per tests/AGENTS.md. Procedural maps are meant to grow, and the cost grows linearly with them. It also adds avoidable test time to every save and replay test on the shipped data. This conflicts with the user's 'avoid wasted work' rule.

**Recommendation:** In ReadGameWorld, when seed == m_base.state.Seed(), baseHash == m_baseHash and the generator matches, make the base and the running world from CopyOf(m_base) instead of calling GenerateWorld. Keep GenerateWorld for a save of another seed. If the base-hash check from the previous finding is added, it verifies the reuse for free.

**Verification:** host.cpp:307 does call GenerateWorld unconditionally on load. The m_base reuse idea is plausible and matches the scratch path's reuse. The design deliberately says 'a load makes the base again from the save's seed' (design 0002 §293, AGENTS.md). Reuse is only safe if the generator and data are provably equal, which today means seed, version and base hash all match. Load runs on a frozen tick, so this is a cost and not a correctness issue. I did not measure the cost, and the 'large part of a second' quote was not checked. Severity low.

### GAP1-SIM-BOUNDARY-COMPLETENESS-03: Replay hashes never cover the game's world while a dev scene runs, so a replay started in a dev scene never verifies the m_world its start state restored

- **Severity:** low · **Verdict:** confirmed · **Category:** replay coverage / test gap · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1622`

StateHash hashes Live() and the scene: 'everything Save() writes but the game's world while a dev scene runs'. The replay's start state (Serialise(true) → Host::Save) does contain m_world, and the playing game restores it through ReadGameWorld/LoadDelta. If a replay starts in the arena and never goes back to the world scene, no hash ever covers the restored m_world. If it does go back, a restore error shows only after the return, far from its cause. No replay test does world → dev → world: 'a replay recorded from a delta save' goes world → arena and stays there. Only save_exactness ('a save made in a dev scene restores both worlds', with WorldHash) checks this path, and only for Host-level loads.

**Evidence:** host.hpp:160-162: StateHash is 'A hash of everything Save() writes but the game's world while a dev scene runs'. host.cpp:1622-1629 hashes `HashWorld(hasher, Live())`. replay_tests.cpp:397-451 records world → arena and plays back in the arena only.

**Impact:** A bug in restoring the set-aside world (a delta, order or travel edge case) passes every replay hash check in a dev scene. Hash checks are the main safety net for replay determinism.

**Recommendation:** Hash the set-aside world once, when the dev scene is entered or a load is entered, and fold that cached u64 into StateHash while m_scratch exists. This is cheap: nothing changes m_world meanwhile. Alternatively, have Logic's swap check call WorldHash once. Add a replay test that records world → arena (portal or F2) → world, crossing a month step in the arena.

**Verification:** src/AGENTS.md and host.hpp state that StateHash hashes the running world and the scene, and that Host::WorldHash alone hashes the set-aside world. So a replay in a dev scene never hashes m_world. This is a documented trade-off, but the test gap is real: I did not find a replay test that goes world to dev to world. save_exactness covers the Host-level path only. Low severity.

### GAP1-SIM-BOUNDARY-COMPLETENESS-04: Host::SetData accepts a different world map, rebuilding paths and caches, but leaves m_world, m_base, m_baseHash and the explored bits built for the old map

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** api / invariants · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1705`

SetData handles `data.world != m_data.world` by resetting and rebuilding m_paths, m_roadCurves and m_legWalks for the new map. It does not regenerate or check m_world and m_base. Their NPC homes, cells and journeys index the old map's places and tiles, m_world.explored is sized for the old map's tiles, and m_baseHash still names the old data. FogFits() then asserts on the next ExploreRound. Journeys would be placed on the wrong map, and later saves would write the old baseHash with deltas against an old-map base. The app never does this (app::KeepWorld keeps the map and names), so the branch is code for a case the design forbids.

**Evidence:** host.cpp:1706-1717 rebuilds m_paths/m_roadCurves/m_legWalks only; m_world, m_base and m_baseHash are untouched. host.cpp:1494-1500 FogFits asserts the explored size matches the current map.

**Impact:** Any future caller that swaps the map (a mod reload, a tool, or a test that builds new data without KeepWorld) gets a host whose world and map disagree: asserts in debug, wrong placement or out-of-range reads in release, and saves that load into a different game.

**Recommendation:** Make the invariant explicit: PN_ASSERT(data.BaseHash() == m_baseHash, 'a data reload changed the world map or names') and drop the map-rebuild branch. If map swaps ever become a feature, route them through a new game (GenerateWorld), not SetData.

**Verification:** SetData at host.cpp:1705-1717 rebuilds only paths, curves and walks when data.world changes, and leaves m_world and m_base alone. The AGENTS.md text says the caches are dropped when the map changes (SetData), so the branch is deliberate. The finding's claim that the app never swaps the map was not checked beyond the description. A debug assert on BaseHash equality would be a cheap invariant. Low severity.

### GAP1-SIM-BOUNDARY-COMPLETENESS-05: A plain save drops a calendar step asked for on its tick; a ClockFits-accepted month-due save runs one frozen-clock tick before its step

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** save boundary · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/logic.cpp:111`

(1) If AdvanceCalendar (M, or the camera path's scripted step) arrives in the same tick as Save, TakeInput sets m_calendar = Requested. The Save branch serialises with Serialise(false), which leaves out m_calendar. The saving game then runs the month step on its next tick, but the loaded game never does; Restore leaves calendar None. (2) ClockFits explicitly accepts a world clock at its month's end with the step still due. Restore always sets m_calendar None, so a loaded month-due state runs one simulation tick with the clock standing still (`clockRuns` needs !MonthDue()) before the logic asks for the step. The saving game would have stepped first. The app never writes case (2), but Host-level harnesses and hand-built saves can.

**Evidence:** logic.cpp:278-280 sets m_calendar = Requested in TakeInput; logic.cpp:111-120 Save returns Stall; logic.cpp:313-316 Serialise(false) omits m_calendar; logic.cpp:324 `Calendar calendar = Calendar::None;` for loads. host.cpp:517-521 ClockFits accepts `month - months == 1 && worldTicks % TICKS_PER_MONTH == 0`.

**Impact:** A loaded game can differ from the saving game by one month step (case 1), or the calendar can lag by one simulation tick (case 2). Neither is caught: save_exactness ticks Host directly without the logic, and the logic tests cover only 'a calendar step asked for on a sound load's tick goes with the old state', not the save side.

**Recommendation:** Either persist m_calendar in plain saves, as replays already do, or document that a step pending on a save tick belongs to input and not to state. For (2), have Restore set calendar = Requested when the restored simulation reports MonthDue(), or refuse month-due saves in ClockFits.

**Verification:** Case 1 holds by code. Save and ToggleRecording return Stall at logic.cpp:111-120, and Serialise(false) omits m_calendar (logic.cpp:313-316), so Restore sets calendar to None. A step asked for on the Save tick therefore survives in the saving game and not in the loaded one. I did not trace whether TakeInput carries it into the next tick. Case 2 (the ClockFits month-due save) was not traced. The impact is a one-step difference from a rare coincidence. Low severity.

### GAP1-SIM-BOUNDARY-COMPLETENESS-06: A recording knowingly continues after Record() drops a tick's actions past 255, producing a replay that is certain to diverge

- **Severity:** low · **Verdict:** confirmed · **Category:** replay integrity · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/replay.cpp:153`

Record() keeps at most MAX_ACTIONS_PER_TICK (255) actions of a tick and logs a warning, but the live game executes all of them. Every later hash check then mismatches, and the replay file is written as if it were sound. Inputs pile up whenever the logic thread stalls (a month step on a big map, a save, a load), and 'a burst's presses go to its first tick', so an unlucky stall concentrates inputs.

**Evidence:** replay.cpp:153-160: `const std::size_t count = std::min(actions.size(), MAX_ACTIONS_PER_TICK); if (count < actions.size()) { // Playback will diverge from here; the log says why. PN_LOG_WARNING(...)`

**Impact:** The user records a replay that can never play back. The failure is found only at playback, as 'diverged', with nothing in the file explaining why.

**Recommendation:** When a tick has more than 255 actions, finish the recording at that tick with an error log (as a ReloadData does), or widen the per-tick count to a u16 and bump REPLAY_FORMAT. Add a replay unit test with 256 actions in one tick.

**Verification:** replay.cpp:145-149 caps a tick's actions at MAX_ACTIONS_PER_TICK, and the finding quotes a warning log. The live game still runs all the actions, so playback diverges. The recording continues without error. This is a real but narrow edge case, since more than 255 actions in one tick is unlikely. The fix suggested (end the recording with an error) is cheap. Low severity.

### GAP1-SIM-BOUNDARY-COMPLETENESS-07: Load runs ApplyData over the restored state; save == load holds only while every scene spawns creatures exactly as ApplyData would set them

- **Severity:** low · **Verdict:** unverifiable · **Category:** save boundary / fragility · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:762`

EnterScene after a load calls ApplyData(). For every Creature+Look, that rewrites look.sprite, health max (and clamps current), clamps Qi, and resets Orbit radius, wobble and speed, plus the centre to the scene's home when !orbitsPlayer. The saving game never ran this pass at save time, and the replay start runs it too. Under the same data it is the identity today only because SpawnFox, SpawnPlayer and RespawnFox take exactly those values from the data, and the arena's ARENA_START equals its traits' home. Any future creature with a non-data orbit, health or centre will be silently rewritten by every load. A replay start would then diverge at its swap hash; a plain save gives no signal at all.

**Evidence:** host.cpp:762 `ApplyData();` in the load branch of EnterScene; host.cpp:1722-1744 ApplyData overwrites sprite, health max, Qi and the Orbit fields, with `orbit->centre = Running().Traits().home`.

**Impact:** There is a latent class of 'loaded game differs from saved game' bugs that appear as soon as content deviates from the data defaults: bosses, buffs, scaled creatures, or a scene whose home differs from where its foxes circle.

**Recommendation:** Apply data-derived overrides only when the data actually differs from what the save was made under. For example, record GameData::Hash() in the scene section and skip ApplyData on load when it matches. Otherwise keep per-entity overrides as components that ApplyData respects. save_exactness already catches today's scenes; add a case with a creature whose orbit or health differs from the data defaults.

**Verification:** Confirmed that a load calls ApplyData() after restoring state (host.cpp:762) and that SetData also calls it. The claim is a latent fragility: today's creatures match the data defaults, and save_exactness would catch a deviation in today's scenes. No present bug is shown. It depends on future content. Whether ApplyData is the identity today was not verified in detail.

### GAP1-SIM-BOUNDARY-COMPLETENESS-08: The replay header pins only the version string, so any rebuild with the same version (code, atlas or manifest changes) is accepted and diverges silently until the first hash

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** replay identity · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/replay.cpp:14`

The recorded decision is 'replays deterministic only within one binary', and the manifest's footY is outside the data hash (known). StartPlaying accepts a replay whose VERSION string (xmake set_version) and GameData::Hash match, even if the code, the scene list, the generated atlas (sprite IDs saved in Look) or manifest metrics changed. Between version bumps every dev build shares one VERSION, so these replays start and only fail at the next hash check (up to 1 s later) with 'diverged'.

**Evidence:** replay.cpp:14-15 `constexpr u32 REPLAY_FORMAT = 5; constexpr std::string_view VERSION = PN_GAME_VERSION;`; replay.cpp:212-215 compares only the version string; the data hash covers no manifest data.

**Impact:** A misleading failure mode in day-to-day development: a replay from an older build fails as a determinism bug rather than being refused as 'recorded by another build'. That costs debugging time, and the flaky game_tests/replay history shows such reports are taken seriously.

**Recommendation:** Stamp the replay with a build identity: the commit hash plus a dirty flag from build_info, or a hash of the binary's generated atlas and the loaded manifest's sprite metrics. Refuse a mismatch with an explicit message. The same identity also closes the footY channel for replays without putting presentation into GameData::Hash.

**Verification:** replay.cpp:205-216 checks the version string and the data hash. The header holds nothing else, so same-version rebuilds are accepted. This matches the recorded decision that replays are deterministic only within one binary. The failure is a delayed 'diverged' message at the next hash, as described. A build-identity stamp is a usability improvement, not a defect. Low severity.

### GAP1-SIM-BOUNDARY-COMPLETENESS-09: No gated test plays a replay back on a different worker count, view, or animation table than it was recorded with

- **Severity:** low · **Verdict:** confirmed · **Category:** test coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/game/replay_tests.cpp:58`

Every Session records and plays on WORKERS = 2, with no view sent and no animations, so the replay path never exercises a playback game whose job count or presentation channels differ from the recorder's. Worker-count independence is gated only through Host-level save exactness (Unlike on 1 and 4 workers) and the skipped hour run, which compares two live games, not a recording against its playback. Publish purity is tested per feature at Host level (glows, debug sight, walks), not under the logic with a varying ViewReport.

**Evidence:** replay_tests.cpp:36 `constexpr u32 WORKERS = 2;` and Session's `u32 workers = WORKERS` is used for both recording and playing in every case; the Session never calls handover.SendView.

**Impact:** A regression that makes a month step, Travel::Month or a publish depend on the worker count or on the view would still pass every replay test in the gate.

**Recommendation:** Parameterise one existing replay case (the world scene's, which has month steps and travellers) to record on 1 worker and play on 4. During recording, send views with debugSight on, an animations table and a moving area; play back without them, and assert every hash matches.

**Verification:** replay_tests.cpp:35 has WORKERS=2 and the Session default is workers=WORKERS. Line 663-664 does run a Session on 1 worker and one on 4 (WORLD_SCENE), so the finding's claim that no Session varies the worker count is partly wrong. Both of those are recordings or plays compared to each other, not a recording replayed on a different count. The view/animation channels are never varied in replay cases. The coverage gap holds, but its impact is small.

### GAP1-SIM-BOUNDARY-COMPLETENESS-10: Replay hashing walks the entire world (NPC domain, every journey and journal, the explored bits) every second on the logic thread while recording or playing

- **Severity:** info (reviewer said low) · **Verdict:** unverifiable · **Category:** scaling · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/logic.cpp:289`

CheckReplay calls Logic::Hash every REPLAY_HASH_TICKS wall ticks during recording and playback. That runs StateHash → HashWorld: SaveDomain over all world NPCs, Travel::Save of every journey (tiles and arrive vectors) and every journal (up to 50 entries per met NPC), relationships, and the explored bit array. It runs inline in a tick burst. On the shipped world (50,000 NPCs) this is probably milliseconds, but it grows linearly with map size and travel share. It has not been measured as far as the backlog shows; measure counts ticks and the month step, not a recording session.

**Evidence:** logic.cpp:295-308 CheckReplay → Hash(); host.cpp:127-134 HashWorld covers SaveDomain(WORLD), relationships and the whole travel table; host.cpp:1622-1629.

**Impact:** On large procedural maps, recording or playing a replay could add a periodic once-a-second hitch to the logic thread, and replays are the main determinism debugging tool.

**Recommendation:** Measure StateHash on the shipped map in releasedbg; a single zone already exists via PN_PROFILE_ZONE. If it is significant, hash incrementally: cache per-section hashes invalidated by the journeys stamp, the month step or a domain change, or hash only the active domain each second and the world at month steps.

**Verification:** The code path is real: CheckReplay at logic.cpp:298 hashes every REPLAY_HASH_TICKS=60 wall ticks (replay.hpp:24), and the hash covers the whole world. The finding itself says it was not measured and has low confidence. Only a releasedbg measurement on the shipped map can settle it. Replays are a debug tool, so the impact is low.
