# Review: gap1-scene-transition-flow

**Scope:** Trace one scene change end to end (portal step or F-key, the core::Logic scene-change request, Host ClearScene and Build, handover and snapshot epochs, gfx::Renderer LoadScene including its failure path, and app-side state). Inventory every piece of per-scene presentation and input state in src/app (camera_control, camera_path, target_cursor, cast and CastLatency, damage_numbers, fog_fade, hud, view hover and selection, bindings' held inputs) and in gfx (label caches, layout budget, uploaded-snapshot holders, the route, fog, cloud and seal layouts). For each one say whether it is reset, carried over correctly, or leaks into the next scene, for example: damage numbers waiting on a UseEnded from the old scene, a target cursor on a map that no longer exists, fog fade targets of the wrong size, a camera path or zoom out of the new scene's limits, held keys that keep walking. Also cover a load that switches scenes and a scene change while a replay plays. Earlier units already found: a month due on the change tick, a frame acquired before a failing LoadScene, orbiting entities streaking on entry, the resident report naming an intermediate scene. Don't repeat those. (Why this unit exists: Scene changes touch every module, but reviewers looked at them only from inside one module. The app and gfx presentation state that must be reset on a scene change was never listed as a whole, and integration bugs at that seam would not show up in any per-file review.)

**Findings:** 12 standing (0 critical, 0 high, 1 medium, 8 low, 3 info), 0 raised but refuted on verification.

**Parts:** gap1-scene-transition-flow.md, gap1-scene-transition-flow-part2.md

## Assessment

## Scope: one scene change end to end (portal or F2, load, replay start), and every piece of per-scene presentation and input state in app and gfx

**Strengths**
- The core protocol is well designed. `core::Logic` runs every change through Leaving, Loading and Entering without ticking, so replays stay exact. `SceneReports` answers each stage once. `ShownChange` keeps a view of the old scene from culling the new one. Load and replay swaps happen at SceneLeft, with the epoch bump and the replay start hash check in the same place. `logic_tests.cpp` covers the stages well.
- Most gfx per-scene state is rebuilt every frame from the snapshot, so nothing leaks:
  - route, cursor tile and clouds (all gated on `worldMap && Holds(scene)`)
  - seal layout (a pure function of the render time)
  - volleys, health bars, bars shown and number origins
  - `measured` and `measuredNames` (reset each frame)
  - `nameTexts`
- Other gfx state is keyed on things that don't change between scenes: terrain, ground, rivers and roads on `worldVersion`; the fog texture on revision and size; the text layout cache expires by the frame clock.
- On the app side these are correct:
  - `CameraControl` resets centre, zoom, notches, snap back, free and path segment on each new `ShownChange`, and is tested for it.
  - `FogFade` snaps on a new change, a new epoch, or a size or debug-sight change, and drops its seed when the fog goes off. Since the map is fixed for the run, its targets can't end up the wrong size.
  - `TargetCursor` hides as soon as a drawn snapshot isn't on the world map, and the renderer gates the tile on `worldMap` anyway.
  - Host `Build` clears selection, route, drawn shift and player walk.
  - Combined damage numbers get their UseEnded on portal and F2 changes (`Leave` calls `EndUses`). On loads and replay starts they are cleared by the epoch.

**Weaknesses**
- **No single reset point.** Each piece of per-scene state uses its own key, and the keys disagree:
  - camera: `ShownChange`
  - fog fade: the raw change number, so it already snaps at Leaving, plus the epoch
  - damage numbers and HUD warning: the epoch only
  - target cursor: the `worldMap` flag of each frame
  - renderer instance upload: (epoch, wallTick)
- **Most serious bug, in that last key:** `wallTick` repeats across the change, because waiting bursts publish without counting. Every portal or F2 change therefore draws the new scene's batches, selection and hover indices against the old scene's instance buffer, for the Entering frames and the first Running frame. This gets much worse once the way in is actually animated.
- **Damage numbers and the "Not enough Qi" warning** carry into the new scene at the old scene's coordinates.
- **Held movement** carries through the change. Arriving on the world map next to the portal with the same direction held walks straight back through it.
- **Inputs sent during a change are dropped with only a debug log**, including Save, recording and replay toggles, and a request to stop a replay.
- **Smaller issues:**
  - the label pass fades the old scene's labels out over the new one
  - the drag carries across the change
  - the cursor stays shown on a tile chosen before a load
  - the full label pass runs while nothing is drawn during loading

**Tests**
The seam itself (App::Present's prepare and draw lambdas) has no test at all. The four known items (month due, acquire before a failing LoadScene, orbit streaking, intermediate resident report) are left out.

**Overall health:** good architecture with integration bugs at the seam. One finding is high and visible on every portal; the rest are medium or low and need little code. The main recommendation is to stamp a publish sequence number on each snapshot, and to have one presentation reset keyed on (epoch, ShownChange) that resets numbers, HUD, drag and cursor and requires held movement to be released, with tests.

**Files read in full:** src/app/app.cpp, src/app/app.hpp, src/app/camera_control.cpp, src/app/camera_control.hpp, src/app/camera_path.cpp, src/app/camera_path.hpp, src/app/cast.cpp, src/app/cast.hpp, src/app/damage_numbers.cpp, src/app/damage_numbers.hpp, src/app/fog_fade.cpp, src/app/fog_fade.hpp, src/app/hud.cpp, src/app/hud.hpp, src/app/target_cursor.cpp, src/app/target_cursor.hpp, src/app/view.cpp, src/app/view.hpp, src/app/bindings.cpp, src/app/bindings.hpp, src/app/game.cpp, src/app/game.hpp, src/core/frame.hpp, src/core/handover.hpp, src/core/logic.cpp, src/core/logic.hpp, src/core/scene_reports.cpp, src/core/scene_reports.hpp, src/gfx/renderer.cpp, src/gfx/renderer.hpp, src/gfx/uploads.hpp, src/game/host.cpp (lines 700-900, 1115-1240: Requested, EnterScene, Build, Tick, Leave, OnPortal, labels), src/gfx/labels.hpp (pass contract and state), src/gfx/text_layout.hpp (cache API), src/gfx/internal/features/sprites.cpp (FogTexture::Update, SpriteRenderer::Draw clamps), src/core/tick_clock.hpp (Alpha, RenderTick), src/game/scenes/arena_layout.hpp, stress.cpp, world.cpp (spawn and portal points), tests/unit/core/logic_tests.cpp (scene change cases), tests/unit/app test case lists, docs/reviews/2026-11-25-full-codebase-review/gfx-renderer.md (GFX-RENDERER-02), memory sweep-up-backlog.md (asset loading item)

## Findings

### GAP1-SCENE-TRANSITION-FLOW-01: Every portal or F2 change draws the new scene against the old scene's instance buffer: the Leaving snapshot already claimed the same (epoch, wallTick)

- **Severity:** medium (reviewer said high) · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/renderer.cpp:460`

Upload() decides whether to upload instances with `newInstances = !uploaded.Holds(SnapshotId{epoch, wallTick}) && Holds(scene)`, and uploads.hpp:16 claims 'wall ticks never repeat within an epoch'.

That claim fails at every non-load scene change:
- The tick that asks for the change (wall T) publishes the Leaving snapshot, which shows the old scene. The renderer holds that scene, so it uploads the old instances and records {e,T}.
- The waiting bursts never count (Logic::RunBurst calls Wait() and no RunTick). So the Loading publish after SceneLeft (the new scene, already built by EnterScene) is also stamped wall T. So are the Entering publish and the stall publish that ends the change (Running stage, no tick).
- logic_tests.cpp's 'a scene asked for is entered after the tick that asks' asserts this: the changed snapshot has `wallTick == 1`, the same as the asking one.
- When the new scene becomes drawable, `uploaded.Holds({e,T})` is already true, so its instances are never uploaded. DrawWorld then draws the new snapshot's batches, `selected` and hover `outlined` indices from the GPU buffer that still holds the old scene. SpriteRenderer clamps to the old m_count, so this is not an out-of-bounds read.
- Loads and replay starts escape, because their epoch changes at the swap.

The same review's GFX-RENDERER-02 found the key reuse but was downgraded on the grounds that 'after Entering, the first publish with the new scene's instances is the one that gets uploaded'. That is wrong: the Leaving upload at the same id blocks it.

**Evidence:** renderer.cpp:459-460 `const SnapshotId shownId{ .epoch = snapshot.epoch, .wallTick = snapshot.wallTick }; const bool newInstances = !uploaded.Holds(shownId) && Holds(snapshot.scene);`

logic.cpp:26-28: a waiting burst runs Wait() and no tick, then logic.cpp:42 `snapshot.wallTick = m_counters.wall;`

logic.cpp:208-219: TakeReport(SceneLeft) calls EnterScene() before the next publish.

logic_tests.cpp:527-534 `CHECK(changed.wallTick == 1); CHECK(changed.scene == OTHER_SCENE);`

reference_tests.cpp:884 already works around the same key ('`wallTick` is one no other ...').

**Impact:** On every portal step and F2, the Entering frames and the first Running frame draw the old scene's sprites with the new scene's batch layout and pipelines, or draw nothing where the new scene has sprites. The selection and hover outlines land on the wrong instances. That is about two publishes, 33 to 50 ms, or more frames on a 165 Hz display.

It stays short today only because SceneReports answers Entering at once. Once the way in is drawn (the stated plan for the Entering stage), the stale buffer would show for the whole entry animation. arena→stress shares coordinates, so the old sprites appear on screen. arena→world draws the world without its villagers or cultivator for those frames.

**Recommendation:** Have core::Logic stamp a publish sequence number on every Snapshot (never saved, recorded or hashed), and key UploadedSnapshot on it instead of (epoch, wallTick). Correct the uploads.hpp:16 contract and Renderer::Draw's doc.

Add tests:
- in logic_tests: two publishes of a waiting burst, and the Leaving and Loading publishes, carry distinct ids
- in gfx: an UploadedSnapshot-level case showing a different snapshot at the same wall tick still uploads.

**Verification:** Checked logic.cpp:23-46 and 205-219: a waiting burst runs Wait() with no RunTick, so wall stays at T. The Leaving, Loading, Entering and ending-Running publishes all carry wallTick T. logic_tests.cpp:515-534 asserts changed.wallTick == 1, the same as the asking snapshot. The Leaving frame shows the old scene, which the renderer holds, so it uploads and records {e,T}. Once the new scene is resident, uploaded.Holds({e,T}) is true, so newInstances is false. The uploads.hpp:16 'never repeat' claim is false here. reference_tests.cpp:884 works around the key. Nothing in gfx resets `uploaded`. Real bug, but it lasts only a few publishes, so I rate it medium, not high. It would last longer once the way in is drawn.

### GAP1-SCENE-TRANSITION-FLOW-02: Damage numbers (and their stagger) from the old scene keep drawing over the new scene at the old coordinates

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/app.cpp:840`

App clears damage numbers and the HUD warning only when the drawn snapshot's epoch changes, and a portal or F2 change keeps the epoch.

Host::Leave runs EndUses, which emits UseEnded for every open use on the leave tick T. Those events play during Leaving and move every waiting combined number into m_playing, starting at T. Numbers from hits in the last second, and those still waiting out a stagger of up to NUMBER_STAGGER_MAX_SECONDS (1.5 s), stay in m_playing.

The render clock holds at T through the change (frozen snapshots have alpha 1), then runs on in the new scene. So those numbers keep aging and drawing for up to about 2.5 s of the new scene. Their TextItem positions are old-scene world coordinates and their fog is TextFog::None, so they float over unrelated ground in the new scene. The renderer draws `extra` once it holds the scene.

**Evidence:** app.cpp:840-844 `if (m_numbersEpoch != drawn.epoch) { m_numbers.Clear(); m_hud.Clear(); ... }`

host.cpp:1117-1118 Leave(): `m_combat.EndUses(Live().state, events);`

damage_numbers.cpp:117-131: UseEnded starts the waiting numbers at its tick.

damage_numbers.hpp:39-46: stagger up to 1.5 s plus NUMBER_SECONDS 1 s.

**Impact:** After F7 (thousands of hits) followed by F2 or a portal, a fountain of arena numbers appears over stress or the world map. Any fight that ends in a portal step leaves stray numbers at meaningless points of the next scene.

**Recommendation:** Key the presentation reset on (epoch, ShownChange(drawn.sceneChange)) rather than the epoch alone. Clear numbers and the HUD warning when the shown change moves on, which happens at the Loading stage, so the UseEnded numbers still play during Leaving. Alternatively drop numbers whose events belong to an earlier shown change. Extract the prepare lambda so this can be unit tested.

**Verification:** app.cpp:840 clears only on an epoch change. Logic::TakeRestored bumps the epoch only for loads and replay starts, not for portals or F2. Host::Leave (host.cpp:1118) calls EndUses, and damage_numbers.cpp:117-131 moves waiting numbers into m_playing. DamageNumbers::Texts has no scene filter and the render clock continues after the change, so old numbers float in the new scene for up to about 2.5 s. This is real but cosmetic, and only the last second of hits or a debug burst triggers it. I rate it low.

### GAP1-SCENE-TRANSITION-FLOW-03: Held movement carries through a scene change: arriving on the world map beside the portal with the approach key still held walks straight back through it

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/app.cpp:572`

App never resets or requires release of held movement at a scene change. Bindings::m_heldKeys and the sticks persist, SendHeld sends only on change, and the logic reads the newest held input on the first running tick of the new scene.

The world scene re-places the cultivator at WorldState::mapPlace, which for a portal exit is `body.previous`, the point just before it stepped onto the world portal on the side it approached from. The geometry makes the round trip likely:
- The arena and stress portals (110,115) lie up and to the left of their start (960,620).
- The world portal is the start city's tile centre less (560,200), also up and to the left, and is approached from the city to the south-east.
- A player walking left or up-left into the arena or stress portal arrives on the world map south-east of the world portal, still holding left or up-left.
- The first tick's step crosses the step ellipse (Host::OnPortal tests the whole previous→current path), so the cultivator leaves again at once.

TargetCursor already solves the same problem after Confirm, with m_awaitRelease.

**Evidence:** app.cpp:572-587 SendHeld() sends m_cursor.Walk(m_bindings.Move()) whenever it changes; nothing on a scene change.

host.cpp:1117-1133 Leave() stores `throughPortal ? body.previous : body.current`.

world.cpp:50 `context.SpawnPlayer(context.MapPlace().value_or(...StartCentre()))`

arena_layout.hpp:21,25 ARENA_START (960,620), ARENA_PORTAL (110,115); stress.cpp:17,22 uses the same.

host.cpp:1146-1158 OnPortal() checks the path crossing.

**Impact:** Holding a direction through the portals can loop arena→stress→world→arena, with the world shown for only a frame or two. The player cannot reach the map without first releasing the key. Any re-entry from the approach side is fragile.

**Recommendation:** After any shown scene change, treat held movement as unpressed until it has been at rest once, as TargetCursor::Walk does after Confirm (for example, a Bindings or App flag set when ShownChange moves on). Or make the portal re-arm only after the foot has been seen outside an enlarged area since the scene was entered. Add a host test: re-enter the world through a portal with the approach direction held, and the cultivator must not leave on the first tick.

**Verification:** Nothing resets held input on a scene change (Logic m_held is read each tick, Bindings are untouched). Host::Leave stores body.previous for a portal exit, so the world scene re-places the cultivator just outside the step area. A key held through the change can re-trigger the portal. The 'likely' claim is overstated, though. The player must hold the same direction continuously across a long walk in the arena and again in stress (start (960,620) to portal (110,115)). The mapPlace is only set on a return visit. A first entry starts at the start city, 560 units from the portal. It is a plausible edge case, not a common loop. TargetCursor's m_awaitRelease is a fair precedent for a fix. I did not find it recorded as a decision. I rate it low.

### GAP1-SCENE-TRANSITION-FLOW-04: Save, recording, replay toggles and other actions pressed during a scene change are silently dropped (debug log only)

- **Severity:** low (reviewer said medium) · **Verdict:** deliberate · **Category:** error-handling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/logic.cpp:189`

Logic::Wait() takes live input during every waiting burst and drops everything except SceneResident, Pause and Resume, ReloadData and the stage reports. Only a debug-level count records the drop.

The dropped kinds include:
- Save (F5) and ToggleRecording (F6)
- PlayReplay (F10), including the request to stop a replay playing through a change
- AdvanceCalendar (M) and ToggleWorldPause (P or the date line)
- WalkTo, Select and Cast

A first visit to a heavy scene can keep the logic waiting for seconds while sprites decode. The logic's doc mentions 'a second change asked for, a Load, a stale report', not saves. App gives no feedback either: it logs 'a save asked for' at INFO for the press, and nothing says it was dropped.

**Evidence:** logic.cpp:184-192: `else if (action.kind != ActionKind::ReloadData) { ++dropped; }`

logic.cpp:197-199: `PN_LOG_DEBUG("{} actions dropped at tick {}...")`

app.cpp:561 logs each press at INFO.

**Impact:** A player who presses F5 while the world map loads, or during the portal's brief change, believes the game saved; the INFO log even says 'a save asked for', but no save is written. The same goes for starting a recording, or stopping a replay mid-change. --month-step-at can also lose its one month step without an error.

**Recommendation:** Queue the stateful requests (Save, ToggleRecording, PlayReplay, AdvanceCalendar, ToggleWorldPause) for the first running tick, as m_heldPausing already does for pausing. Or at least log each dropped Save, recording or replay request at WARNING, naming it, so App or a future UI can say so. Update the RunBurst doc to list every kind that is dropped.

**Verification:** The drop is documented as designed. docs/design/0002.3-input-data-scenes.md:134 says any action other than SceneResident, Pause and Resume is dropped. docs/design/0002.3.1-manifest-and-scene-loading.md:138 and 195 say it is dropped with a debug line. logic.hpp:158 describes it, and :234 records ReloadData as the one deliberate exception. The finding is right that Save, record and replay presses are lost silently, with INFO 'asked' at the press and no warning. That is a UX gap, not an undocumented bug. Waits are short, since most scenes are resident and the Leaving/Entering reports are answered at once. A WARNING-level log for dropped Save, record and replay requests would be reasonable. The RunBurst doc does state the rule.

### GAP1-SCENE-TRANSITION-FLOW-05: Per-scene presentation state is reset by five different keys, with no single reset point and no test of the seam

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/app.cpp:832`

What must reset at a scene change is decided piece by piece, each by its own key:
- CameraControl: ShownChange (camera_control.cpp:152)
- FogFade: the raw `sceneChange.change` and the epoch (fog_fade.cpp:98). It therefore snaps at the start of Leaving, while the old scene still shows.
- DamageNumbers and Hud: the epoch only (app.cpp:840)
- TargetCursor: each frame's `worldMap` flag (app.cpp:737)
- SceneReports: change and stage
- the renderer's instance upload: (epoch, wallTick)
- Drag, the held input and LabelPass: nothing

The reset logic lives inside lambdas in App::Present, which has no unit test because App needs a window. Only the pieces are tested in isolation: the camera, fog fade and cursor tests. The integration bugs at this seam (the stale instance buffer, leaked numbers, held movement) come from these mismatched keys.

**Evidence:** camera_control.cpp:152 `if (input.change != m_change)` (ShownChange)

fog_fade.cpp:98 `snapshot.sceneChange.change != m_change`

app.cpp:840 `m_numbersEpoch != drawn.epoch`

renderer.cpp:459 SnapshotId{epoch, wallTick}

tests/unit/app has no Present or prepare test.

**Impact:** Each new piece of per-scene presentation state (particles, audio, a transition animation) has to rediscover which key to use, and getting it wrong passes every existing test. The fog snap keyed on the raw change will visibly pop the fog at the start of a drawn way out, a frame or more before the old scene is gone.

**Recommendation:** Define one presentation identity, (epoch, ShownChange), in core, next to ShownChange. Move App's per-snapshot bookkeeping (LoadScene, reports, the numbers and HUD reset, cursor, drag and held-release, fog, camera) into a window-free class with an OnDrawn(snapshot) method that resets everything keyed on that identity. Unit test it with hand-built snapshots that run through Leaving, Loading and Entering for a portal, a load and a replay start. Have FogFade key its snap on ShownChange too.

**Verification:** The facts check out. CameraControl keys on ShownChange (camera_control.cpp:152), FogFade on the raw sceneChange.change plus the epoch (fog_fade.cpp:98), numbers and HUD on the epoch (app.cpp:840), and the renderer on (epoch, wallTick). The seam has no test, because App needs a window. The fog snap on the raw change would fire at the start of Leaving, but fog only updates while a world-map snapshot is shown, and the snap sets the fog to its target. A visible pop is plausible but minor. This is an architecture opinion, and the sound part is that mismatched keys caused findings 0 and 1. The recommended window-free OnDrawn class is a sensible refactor, not a defect. I rate it low.

### GAP1-SCENE-TRANSITION-FLOW-06: The label pass fades the old scene's labels out over the new one, and runs fully (spending the layout budget) on frames that draw nothing while a scene loads

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/renderer.cpp:473`

LabelPass keeps every entity's label state and fades out 'a label whose item has gone ... where it was last drawn'. It has no notion of a scene change. So when the new scene is already resident (arena↔stress, revisits), the old scene's labels (the cultivator's, the foxes') fade over LABEL_FADE_SECONDS at their old screen positions over the new scene. Each fading label is drawn at its own opacity times FogOpacity under the new frame's fog, not the fog it was placed under.

Upload() also calls PlaceTexts(), which runs the label pass, TryLay and the stack measuring, even when `!Holds(snapshot.scene)`, where DrawWorld draws nothing but the backdrop. The new scene's labels then finish fading in during frames nobody sees, and pop in at full opacity on the first visible frame, which defeats the fade.

**Evidence:** renderer.cpp:473 `PlaceTexts(shown, jobs)` sits outside the `if (Holds(snapshot.scene))` at 477.

renderer.cpp:1191-1195: DrawWorld returns early when the scene isn't held.

labels.hpp:134-137 describes gone labels fading where they were last drawn.

**Impact:** Ghost labels for about 0.15 s on resident-scene changes. Labels pop in rather than fade after a load. CPU and layout budget are spent on undrawn frames during loading.

**Recommendation:** Give LabelPass a Reset(), or a 'first Place' flag, called when the presentation identity's shown change moves on. Skip PlaceTexts, or at least the label pass, when the scene isn't held, as the seals and volleys already are.

**Verification:** Upload() calls PlaceTexts at line 473 outside the Holds(scene) guard. DrawWorld returns early at 1191-1195, before the meshes.Draw at about 1269. So the label pass does run on frames that draw nothing, and new-scene labels can finish fading during Loading and pop in. labels.hpp:134-137 does say gone labels fade where they were last drawn. The 'ghost labels over the new scene' part needs a resident scene. Leaving's last frame is drawn and Loading is skipped when the new scene is already resident, so the ghost lasts about LABEL_FADE_SECONDS. All of this is cosmetic. I rate it low.

### GAP1-SCENE-TRANSITION-FLOW-07: The target cursor stays shown on its old tile across a load that stays on the world map

- **Severity:** low · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/app.cpp:727`

SteerCursor hides the cursor only when the drawn snapshot doesn't show the map. A load (F9) or replay start from the world map into a world-map save keeps the cursor shown at the tile picked in the abandoned timeline, which may now be far from the restored cultivator. It is not re-centred, unlike ToggleCursor's CursorStart. Enter then sends a WalkTo to a tile chosen in another timeline.

**Evidence:** app.cpp:735-741: the only hide condition is `drawn == nullptr || !drawn->worldMap || map == nullptr`.

**Impact:** A confusing outline after a load, and a walk to an unintended tile on confirm.

**Recommendation:** Hide the cursor (HideCursor) whenever the presentation identity (epoch, ShownChange) changes.

**Verification:** SteerCursor (app.cpp:732-741) hides the cursor only when drawn is null, not worldMap, or there is no map. A grep of HideCursor shows the other callers are click, toggle and confirm. None is tied to a load or replay start. A world-map to world-map load therefore keeps the cursor on its old tile, and Confirm sends a WalkTo to that tile. I did not check whether the drawn snapshot passes through a non-worldMap frame during the change. If it does, the cursor is hidden by accident and the impact is smaller. Low severity is right.

### GAP1-SCENE-TRANSITION-FLOW-08: A data reload during a replay start's scene change stops the replay but still swaps in its start state

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/logic.cpp:166`

Wait() handles ReloadData during any waiting burst: if a replay plays, StopPlaying, then ReloadData. When this happens in the Leaving stage of a replay start, m_restored (the replay's start state and counters) is kept.

The next SceneLeft runs TakeRestored. The game swaps to the replay's start state, bumps the epoch and continues live from there, though the replay has already been stopped. The player meant to watch a replay and ends up playing from its start under the new data. The PlayReplay stop request is dropped during the change anyway (see the dropped-input finding).

**Evidence:** logic.cpp:166-171 stops playing and reloads without touching m_restored; logic.cpp:214-216 swaps it in regardless.

**Impact:** Rare, but the timeline unexpectedly jumps to the replay's start as live play after a hot reload mid-change.

**Recommendation:** When playback stops before its start's swap, drop m_restored and leave the change as a plain re-entry. Or document the behaviour and log it at INFO.

**Verification:** The code matches the description. Wait() (logic.cpp:~163-170) stops the replay and calls ReloadData without touching m_restored. TakeReport (~212-216) then swaps m_restored in at SceneLeft. The reachability is thin. It needs a ReloadData, a dev hot-reload action, arriving in the Leaving window of a replay start. Whether TakeRestored for a replay start carries the replay state is unclear without reading it. The claim that live play continues from the start state is plausible but I did not trace it. Rare and dev-only, so low.

### GAP1-SCENE-TRANSITION-FLOW-09: A scene load failure mid-game quits with the session unsaved; the logic has no way to cancel a change

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** design · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `src/app/app.cpp:806`

When ContinueScene fails (a decode, upload or mip error), App::LoadScene returns false and Run returns 1. By then the logic has already swapped to the new scene (EnterScene runs at SceneLeft, before loading), and there is no Simulation or Logic path to abort a change and go back. So a damaged sprite in a rarely visited scene ends the session the moment a portal is walked, losing everything since the last save. There is no autosave on that path.

**Evidence:** app.cpp:805-809 PN_LOG_CRITICAL then return false.

logic.cpp:212-218 builds the new scene before any residency.

sweep-up-backlog.md records 'failures stop the game cleanly' as deliberate.

**Impact:** A single bad asset or I/O hiccup becomes a crash with lost progress, in a game whose maps and scenes will grow.

**Recommendation:** Reopen the 'stop cleanly' ruling for mid-session loads: at least write an emergency save before quitting. Better, add a Logic path that reverts a change whose scene never becomes resident (keep the old scene built until Entering, or rebuild it from a save taken at Leaving).

**Verification:** Facts hold. LoadScene (app.cpp:806-809) logs critical and returns false, which ends the run. EnterScene runs at SceneLeft, before the load starts. The sweep-up-backlog.md line 27 records 'failures stop the game cleanly' for asset loading, so this partly contradicts a recorded decision. That entry was written about the loader as a whole, not about a mid-session loss of progress. An emergency save or a revert path is a design opinion. The fact that no save is made before quitting is true. Severity is low, because a missing or corrupt asset is a build or packaging defect and not a runtime I/O event. Assets are committed and hashed.

### GAP1-SCENE-TRANSITION-FLOW-10: The 'Not enough Qi' warning carries across a portal between combat scenes

- **Severity:** info (reviewer said low) · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/hud.cpp:99`

Hud::m_warnedUntil is cleared only by Hud::Clear(), and App calls that only on an epoch change. The render clock runs on across a portal or F2. So a NotEnoughQi refused on the old scene's last ticks keeps the Qi bar reading 'Not enough Qi' in HudWarning for up to notEnoughQiSeconds in the next combat scene (arena→stress), where no cast was refused.

**Evidence:** hud.cpp:93-101 Play/Clear; app.cpp:840-843 clears only when the epoch changes.

**Impact:** A misleading HUD warning for a moment after entering the next fight.

**Recommendation:** Clear it together with the damage numbers on the shown-change reset (see the damage-number finding).

**Verification:** hud.cpp:93-101 and app.cpp:840-843 match. m_warnedUntil is cleared only on an epoch change, and the render clock holds during the change, so the warning can outlast the portal by up to notEnoughQiSeconds. The effect is a sub-second misleading HUD text, and it shares finding 1's fix. I rate it info.
