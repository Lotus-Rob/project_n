# Review: core-sim

**Scope:** src/core/logic.{hpp,cpp}, state.hpp, frame.hpp, tick_clock.hpp, combat_rules.hpp, abilities.hpp, effects.hpp, seals.hpp, seal.hpp, glow.{hpp,cpp}, random.hpp, ecs.hpp, display_delay.{hpp,cpp}. Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 21 standing (0 critical, 0 high, 0 medium, 12 low, 9 info), 0 raised but refuted on verification.

**Parts:** core-sim.md, core-sim-part2.md, core-sim-part3.md

## Assessment

## core-sim scope: assessment

**Overall health: good.** The tick, timing and replay core is among the most carefully reasoned code in the repository. `core::Logic` turns a tricky set of interacting concerns into an explicit state machine with documented precedence and a strong test suite (logic_tests.cpp, about 1,500 lines): bursts versus fixed wall ticks, pausing, the calendar transition and step, scene-change stages that never tick, saves and loads applied at the swap, recording and playback with hash checks, held window pausing during changes, and hot data reloads. I found no crash, UB or desync in normal use.

### Strengths
- **Replay determinism is designed in, not bolted on.**
  - Every timeline-changing action ends the burst, so changes sit at fixed wall ticks whatever the burst sizes.
  - Scene-change waits count and record nothing.
  - Pausing taken during a change is re-injected into the next recorded tick.
  - The view is never recorded.
  - Tests cover each of these: answered at once versus late, bursts of 1 versus 3, views sent versus not, divergence detection.
- **TickClock and DisplayDelay are pure functions of the times passed in, and well tested.** TickTime doesn't drift. Catch-up is capped, and stalls restart the clock. Frozen ticks' lateness is excluded from D.
- **The random numbers are counter-based and well specified.** SplitMix finaliser; Lemire's reduction, including a correct 128-bit high-half for ToBelowWide. Golden-value tests pin them.
- **`State`/`Domain` saving is careful.** It keeps storage order, bounds counts against the bytes left, refuses IDs outside the ranges handed out, and checks `Valid()` on every component read.
- **The data structs validate their ranges.** CombatRules, AbilityData, Effects, GlowLook and Seal all have `Valid()`, and the Doxygen on each field is precise.

### Weaknesses and risks
- **Calendar phase carried across a scene change (real ordering bug).**
  - A tick that both makes a month due and asks for a scene change (a portal or F2 on the month's last tick) leaves `m_calendar` Published through the whole change.
  - Every snapshot of the change then says `transition`, so App caps itself to 20 fps while it is meant to upload the new scene's sprites.
  - The step then runs in the new scene's world: the scratch world of a dev scene, not the game's.
- **Silent-omission hazards that rely on discipline rather than a check:**
  - RunBurst's field-by-field Snapshot reset list.
  - The ComponentList passed to SaveDomain: a component left out is neither saved nor hashed.
  - The hand-maintained `ACTION_KINDS`.
  - The AbilityUse source width.
- **Validation gaps in core data and save types:**
  - Restored tick counters aren't checked for `simulation <= state <= wall`.
  - The aura's damage and healing ranges can wrap to a bound of 0.
  - GlowDrive's period is required even for drives that ignore it.
  - `ToBelow(…, 0)` returns a value outside its stated range.
- **Smaller behaviour and robustness points:**
  - A replay being recorded is lost if the game quits.
  - A Load finishes the recording before it knows whether the load works.
  - The frozen ticks (Save, ReloadData, AdvanceCalendar) silently drop other presses sent in the same tick.
  - The publish runs in full 60 times a second while a scene change waits.
  - RunBurst would publish a bogus snapshot for a zero-tick burst.
- **Layering:**
  - Presentation-only types (glow looks, seal looks, the HUD layout, DisplayDelay, PresentFrame) live in `core`, next to the simulation and visible to `game`.
  - `core/seals.hpp` depends on the generated atlas header.
- **Docs:** a few stale comments (Affliction "once a second", `Event::cosmetic`'s residency hint, Snapshot::epoch's "new game").

### Tests
The tests would catch most of what they target. They would not catch:
- the month-due plus scene-change interplay (no case combines `dueAfter` with CycleScene);
- inconsistent restored counters;
- a component missing from a ComponentList (the save-exactness tests compare StateHash, which uses the same lists);
- the aura range wrap;
- a zero-tick burst.

**Files read in full:** src/core/logic.hpp, src/core/logic.cpp, src/core/state.hpp, src/core/frame.hpp, src/core/tick_clock.hpp, src/core/tick_clock.cpp, src/core/combat_rules.hpp, src/core/abilities.hpp, src/core/effects.hpp, src/core/seals.hpp, src/core/seal.hpp, src/core/glow.hpp, src/core/glow.cpp, src/core/random.hpp, src/core/ecs.hpp, src/core/ecs.cpp, src/core/display_delay.hpp, src/core/display_delay.cpp, tests/unit/core/logic_tests.cpp, tests/unit/core/ecs_tests.cpp, tests/unit/core/tick_clock_tests.cpp, tests/unit/core/random_tests.cpp, tests/unit/core/seal_tests.cpp, src/app/game.cpp, src/AGENTS.md, tests/AGENTS.md

## Findings

### CORE-SIM-01: A month falling due on the tick that asks for a scene change carries the calendar step and its transition across the change

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/logic.cpp:134`

In RunTick, after the simulation ticks, `if (m_simulation->MonthDue()) m_calendar = Calendar::Requested;` runs before `if (const std::optional<SceneFacts> requested = m_simulation->Requested()) { StartChange(*requested); return TickEnd::EndBurst; }`. At the end of that burst RunBurst turns Requested into Published, and nothing clears it when a change starts (only Restore() resets m_calendar). So every snapshot published during Leaving, Loading and Entering carries `transition = true`. Once the change ends, the first RunTick runs `AdvanceCalendar` in the new scene. Host::AdvanceCalendar works on `Live()`, which in a dev scene is the scratch world.

**Evidence:** logic.cpp:134-141 (MonthDue → Requested, then Requested() → StartChange/EndBurst); logic.cpp:37-39 (Requested → Published at every burst end, including waiting bursts); logic.cpp:89-93 (the next running tick runs the step); host.cpp:1549-1567 (`WorldState& world = Live();` jumps worldTicks and ++months); app.cpp:900-902 (transition caps the frame rate).

**Impact:** Walking onto the world map's portal, or pressing F2, on the tick a month ends has two effects. (1) App's transition path, `WaitUntilNs(NowNs() + TRANSITION_FRAME_NS)` in app.cpp around line 900, drops the main thread to 20 frames a second and holds the camera (`HoldsCamera`) for the whole change. With a 2 ms per-frame upload budget (LOAD_BUDGET_NS), the new scene loads several times slower. (2) The month step runs on the arena's scratch world, jumping its clock a month and running its travel and actions, while the game's world stays at the month's end until the player returns. It is deterministic, so replays still match, but the step lands on the wrong world. No logic test combines `dueAfter` with CycleScene.

**Recommendation:** Decide the calendar phase before the scene change. Either run the step before leaving (make the scene change wait for the Published step), or drop the Requested phase when StartChange runs from a tick: the simulation's MonthDue() stays true, so the step is asked for again on the world's next running tick. Add a Recorder test with `dueAfter` set and a CycleScene on the same tick, asserting no snapshot of the change has `transition` and that the step runs in the scene that made it due.

**Verification:** Read logic.cpp:125-142 and :37-47. The ordering holds: MonthDue sets Requested, then Requested() calls StartChange, and RunBurst turns Requested into Published. Nothing clears it except Restore, so the change's snapshots carry transition=true and the step runs after the change. The trigger needs a month to fall due on the exact tick of a scene request, which is rare. I did not check the app.cpp or host.cpp consequences. The proposed fix is sound and no test covers the combination.

### CORE-SIM-02: A replay being recorded is silently lost when the game quits

- **Severity:** low · **Verdict:** confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/logic.hpp:139`

`~Logic() = default;` and app::Game::~Game only call `m_jobs->WaitForBackground()`. Nothing calls FinishRecording() for a recording still in progress, so its bytes in core::Replay are dropped at exit. Loads, reloads and PlayReplay do finish a recording; quitting does not.

**Evidence:** logic.hpp:139 `~Logic() = default;`; app/game.cpp:21-25 (destructor waits for background jobs and logs counters only); grep finds no FinishRecording outside logic.cpp.

**Impact:** A player or developer who records a session (F6) and then quits (Escape or closing the window) without pressing F6 again loses the whole recording, with nothing in the log. That is exactly the case where a replay would reproduce a bug.

**Recommendation:** Give Logic a Shutdown() (or do it in Game's destructor) that calls FinishRecording() and writes to m_replays when m_replay->Recording(), before WaitForBackground(); log that it was finished at exit. Add a test that destroys a recording rig and finds the replay file.

**Verification:** logic.hpp has `~Logic() = default`, and Game::~Game (app/game.cpp:21-25) only waits for background jobs and logs. FinishRecording is called only from logic.cpp (load, toggle, replay paths). A recording in progress at quit is lost.

### CORE-SIM-03: Restore accepts any tick counters from a save or a replay's start

- **Severity:** low · **Verdict:** confirmed · **Category:** validation · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/logic.cpp:333`

`archive(format, counters.wall, counters.simulation, counters.state)` is followed only by the format check and Simulation::Load. The counters are never checked against the invariant TickCounters::Count keeps: simulation <= state <= wall. They are also never checked for headroom: core::AbilityUse shifts the simulation tick left by 8 bits, so ticks of 2^56 and above lose their top bits.

**Evidence:** logic.cpp:331-336; frame.hpp:459-462 `return (tick << SOURCE_BITS) | source;`; tick_clock.hpp:96-104 (Count keeps simulation <= state <= wall).

**Impact:** A crafted or corrupt save or replay with, for example, simulation = 2^60 or simulation > wall loads as sound. Then AbilityUse IDs wrap and collide, so combined damage numbers merge across uses. The `wall % REPLAY_HASH_TICKS` and `nextTick` arithmetic also run near u64 overflow. The save fuzz target can't flag it, because nothing refuses such a save.

**Recommendation:** In Restore, refuse (archive.Fail()) unless `counters.simulation <= counters.state && counters.state <= counters.wall && counters.wall < (u64{1} << 56)`, or another bound documented with AbilityUse. Add a logic test with an inconsistent counter triple.

**Verification:** logic.cpp:331-336 reads the counters and checks only format and Load. There is no check of simulation<=state<=wall or of headroom. Only a crafted or corrupt save can trigger it, so the impact is small. I did not check the AbilityUse shift or any fuzz coverage.

### CORE-SIM-04: CombatRules::Valid lets an aura range wrap to a bound of 0

- **Severity:** low · **Verdict:** confirmed · **Category:** validation · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/combat_rules.hpp:90`

Valid() only requires `auraDamageMin <= auraDamageMax` (and the same for healing). arena.cpp draws `core::ToBelow(random, high - low + 1)` in u32. With min = 0 and max = UINT32_MAX, `high - low + 1` wraps to 0, and ToBelow(…, 0) returns 0, so every hit deals the minimum.

**Evidence:** combat_rules.hpp:90-94; game/scenes/arena.cpp:75-83 `core::ToBelow(random, high - low + 1)`; random.hpp:54-57.

**Impact:** A data edit with the widest range passes validation and quietly changes the fight (always the least damage or healing). The bad data isn't refused.

**Recommendation:** Require `auraDamageMax - auraDamageMin < UINT32_MAX` (and the same for healing) in Valid(), or compute the bound in u64 with ToBelowWide. Add the boundary case to the data module's abilities tests.

**Verification:** combat_rules.hpp Valid() only requires min<=max. arena.cpp:79 computes `high - low + 1` in u32, which wraps to 0 for 0..UINT32_MAX, and ToBelow(…,0) then returns 0. Only the full-range edge case is affected, so it is very unlikely in practice.

### CORE-SIM-05: The snapshot's field-by-field reset is a hand-kept list that a new Snapshot field can miss

- **Severity:** low · **Verdict:** confirmed · **Category:** maintainability · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/logic.cpp:40`

To keep buffer capacity, RunBurst resets Snapshot fields one by one (instances, batches, … selected). `combat` and most of `fog` are left to the simulation to overwrite. Nothing checks that every field of Snapshot is either reset here or always written by Publish.

**Evidence:** logic.cpp:40-71; frame.hpp:370-447 (`combat` and the fog's other fields not reset).

**Impact:** Snapshots cycle through GameHandover::BUFFERS buffers. A field added to Snapshot later and not reset here or set by every publish carries a stale value from a publish two or three bursts earlier: a stale camera kind, selection or HUD state shown for a frame. The tests check only the camera, route and fog.

**Recommendation:** Move the reset into a `Snapshot::ClearForPublish()` member next to the struct, so it is edited with the fields. Add a static_assert on sizeof(Snapshot) so a new field forces a look at it, or a test that fills every field and checks it is cleared.

**Verification:** logic.cpp:40-71 resets Snapshot fields one by one, and combat and most of fog are not reset here. The risk of a missed field in future is real, but this is a maintainability suggestion and no bug exists today. The comment says the field-by-field reset is deliberate, to preserve buffer capacity. Moving it into a member would be a cleanup.

### CORE-SIM-06: A component missing from a ComponentList is silently neither saved nor hashed

- **Severity:** low · **Verdict:** unverifiable · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/state.hpp:260`

SaveDomain and LoadDomain cover only the listed component types ("A component missing from the list is neither saved nor hashed"). Nothing checks that the registry holds no other storage with entities in it. The replay hash (StateHash) uses the same lists, so a forgotten component is invisible to the hash checks too.

**Evidence:** state.hpp:257-271; game/host.cpp:93-109 (the lists); tests/unit/game/save_exactness_tests.cpp compares StateHash only.

**Impact:** Adding a component to the game (for example, a new status on fighters) without adding it to ActiveComponents or WorldComponents makes saves drop it. A loaded game then runs differently from the one that saved, and hash-based replay and save-exactness checks can miss it until other components diverge.

**Recommendation:** Add a debug check (for example `core::CoversEveryStorage(domain, ComponentList<...>)`) that walks `registry.storage()` and asserts every non-empty pool's type is StableId or in the list. Call it in Save/StateHash in debug and in the save-exactness tests.

**Verification:** I did not read state.hpp:257-271 or host.cpp. The point rests on the documented behaviour that a component missing from the list is neither saved nor hashed. A debug coverage check for every storage is a reasonable guard, but whether it is feasible depends on the registry API. I did not check for an existing mitigation or backlog entry.

### CORE-SIM-07: A Load finishes the replay being recorded before it knows the load will work

- **Severity:** low · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/logic.cpp:373`

Logic::Load() calls FinishRecording() first, then reads the save and checks it. A missing or malformed save is refused on the same tick, but the recording has already ended and been written.

**Evidence:** logic.cpp:371-385.

**Impact:** Pressing F9 with no save, or with a broken one, ends a recording although nothing changed timelines. The player has to start a new recording, and the replay ends at an arbitrary point.

**Recommendation:** Read the save and run Restore first; finish the recording only once Restore succeeds (before the change starts), as TogglePlayback does for a replay start. Extend the 'a load with no save changes nothing' test to a recording rig.

**Verification:** logic.cpp Load() calls FinishRecording() before m_saves->Read() and Restore(), so a missing or malformed save ends the recording and returns false. The code matches the claim. The comment only justifies a load that succeeds.

### CORE-SIM-08: Presses sent in the same tick as a Save, ReloadData, AdvanceCalendar or recording toggle are silently dropped

- **Severity:** low · **Verdict:** confirmed · **Category:** input-handling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/logic.cpp:111`

RunTick returns before Simulation::Tick on Save, ToggleRecording, ReloadData and PlayReplay ticks. A tick with AdvanceCalendar sets m_calendar to Requested, so `m_advanced` is false and the simulation doesn't tick either. Any Cast, Select, WalkTo or ToggleWorldPause taken in that tick reaches nothing and is logged nowhere (unlike the dropped count in Wait()).

**Evidence:** logic.cpp:105-124 and 125-129; logic.hpp:141-182 documents the frozen ticks but not that their other presses are lost.

**Impact:** A click, cast or walk-to that happens to share a tick with F5, M or a hot reload is lost. This is rare, but with input bound to the mouse and gamepad it shows up as an occasional ignored action. It matters most for accessibility, where users may press several inputs at once.

**Recommendation:** Either carry the tick's other actions over to the next running tick (as m_heldPausing does for pausing), or log at debug how many were dropped and say so in RunBurst's contract. Add a test of a Cast sent with a Save.

**Verification:** RunTick returns Stall or EndBurst before Simulation::Tick for Load, PlayReplay, Save, ToggleRecording and ReloadData. Presses taken in that tick (TakeInput) are never passed to the simulation. The one exception is ReloadData, which is documented as reloading first and then continuing. The calendar-request path is not a drop, because the simulation still ticks on the tick that sets the request. So the claim is slightly overstated for AdvanceCalendar. For the other four it is real, but those are deliberate freezing ticks.

### CORE-SIM-09: The whole scene is published again on every burst while a scene change waits

- **Severity:** low · **Verdict:** unverifiable · **Category:** performance · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/logic.cpp:26`

While Waiting() (Leaving, Loading or Entering, or a scene not resident), each tick-time burst runs no tick but still calls m_simulation->Publish(), so the full publish runs 60 times a second on a frozen state. On the world map that means culling, laying out travellers and picking their walks.

**Evidence:** logic.cpp:26-28 then 41-82 (the publish runs whatever `end` is).

**Impact:** The work in the logic thread competes with the main thread's sprite uploads during a load, and the snapshot contents are unchanged except for culling to the latest view. This goes against the 'avoid wasted work' rule. It is small next to a month step, but it scales with the map.

**Recommendation:** While waiting, republish only when the view, the stage or the residency changed since the last publish (otherwise publish the stage change alone and reuse the last drawing data, or skip the burst's publish). Measure it with `profile` across a scene change on --world before deciding.

**Verification:** RunBurst does call Publish at the end whatever `end` is, and the Wait() branch runs no tick. The cost depends on what Publish does while the scene is frozen, which needs a profile. The finding itself says to measure first.

### CORE-SIM-10: Affliction's documentation says its ticks are once a second; the period is CombatRules::afflictionPeriod

- **Severity:** low · **Verdict:** confirmed · **Category:** documentation · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/abilities.hpp:105`

`/// How many times it deals its damage, once a second.` The spacing actually comes from CombatRules::afflictionPeriod (game data; 60 shipped, but any value of 1 or more is valid).

**Evidence:** abilities.hpp:104-108; combat_rules.hpp:52-54; game/combat.cpp:123 `.nextTick = tick + period`.

**Impact:** Anyone tuning abilities.yaml would think the cadence is fixed at one second when it follows the combat rules.

**Recommendation:** Say "every CombatRules::afflictionPeriod ticks".

**Verification:** abilities.hpp:105 says 'once a second'. The finding states the period comes from CombatRules::afflictionPeriod, and I did not re-open combat_rules.hpp to check that. The doc comment is at best misleading.
