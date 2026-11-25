# Review: gap1-hot-reload-flow

**Scope:** Trace data hot reload end to end, across modules: DataWatch and ReloadOf in src/app/app.cpp, the ReloadData action, Logic::ReloadData (src/core/logic.cpp), Host::SetData, and on the main thread m_renderer.SetWorld plus every presentation table the renderer and app keep (strings, seals/glow/effects tables, glyph prewarm, labels, ui_colours, damage-number styles, the stamp, terrain, river and road layouts, the fog texture size, the camera bounds). For each data file in assets/data/, answer: what reloads it, on which thread, in what order relative to the simulation's switch, and what is left stale or mismatched in the meantime or permanently. Check that a frame can never draw a new world look against an old map, or old strings against a new StringId table, that the reload is consistent with a replay being recorded or played, and what happens when the reloaded data fails validation part way. Already found, so don't repeat them: the data race on m_options.data, SetData keeping the old base when the map changes, a reload during a replay's start change, SetWorld recomputing RoadCurves, DataWatch throwing or ignoring subfolders. (Why this unit exists: Hot reload crosses app, core, game and gfx. Each module reviewer saw only its own side, and only scattered single symptoms were reported. No unit followed one reload through both threads and every derived cache to find stale or torn presentation state.)

**Findings:** 14 standing (0 critical, 0 high, 0 medium, 11 low, 3 info), 0 raised but refuted on verification.

**Parts:** gap1-hot-reload-flow.md, gap1-hot-reload-flow-part2.md

## Assessment

## Scope: one hot reload of the data, followed through both threads

**How a reload runs.** `App::WatchData` runs on the main thread every 0.5 s in debug builds. When `DataWatch::Changed()` reports a change in the folder listing, it re-parses **every** table, whichever file changed. The steps, in order:
1. `effects.yaml` is parsed. It is applied only at the end of the step, through `ApplyEffects` (renderer glow table and HUD layout), and only if its hash changed.
2. `seals.yaml` is applied at once: `KeepSeals`, then `Renderer::SetSeals`, which repacks the seals. The upload is versioned and only counted once submitted.
3. `animations.yaml` is applied at once (`KeepAnimations`). The new table reaches the logic thread in the next drawn frame's `ViewReport` and takes effect at the next `Publish`.
4. `world_look.yaml` is checked against the running map and applied only on a hash change (`KeepLook`, then `SetWorld`). Bumping `worldVersion` re-lays the terrain buffer and rebuilds the ground field, the river and road ribbons and the ground uniforms on the next frame that shows the map. Every cache I found is keyed on that version, the fog texture's uniforms are rebuilt every frame, and `GroundRenderer` / `LineRibbonRenderer` stay ready on the frame they re-upload. So no frame mixes an old and a new look.
5. `abilities.yaml` and `creatures.yaml` (`LoadGameData`, plus `KeepWorld` and `CheckScenes`) replace `m_options.data` on the main thread at once and go to the logic thread as `ReloadData`. The logic thread applies them first thing on its next running tick, or inside `Wait()` during a scene change. That tick is frozen and ends the burst. A recording in progress is finished and written; a replay that is playing stops. `Host::SetData` / `ApplyData` update sprites, health, Qi and orbits of the live ACTIVE domain, and a held load gets `ApplyData` again at `EnterScene`.
6. Never reloaded: `world.yaml`, `names.yaml` and `strings.yaml` are read once in `main`, and `KeepWorld` fills the running map and names into the read data. `settings.yaml` (UI colours, text options, damage-number options) is not in the watched folder at all.

**What this means for the questions asked.** The map, the `StringId` table and the names never change during a run. So a frame cannot draw a new look over an old map, old strings against a new string table, or names against a stale prewarm (`SetNames` returns early on the same pointer). The fog texture size and the camera bounds come from the map, so they cannot go stale either.

Replays and recordings are safe for every presentation-only table: none of them enters `Hash()`, `DataHash()` or a save. Game-data reloads end the timeline correctly, and `NeverRecorded` keeps `ReloadData` out of replay files.

Each file is all-or-nothing, so a reload that fails validation leaves that file's table as it was. Across files it is mixed: seals, animations and look are applied before the game data is validated, which is the intended per-file behaviour.

The stale and mismatched states that remain are small:
- Seals take effect about one tick before the game data. With the once-only missing-key log, a coordinated edit of `abilities.yaml` and `seals.yaml` can hide later real problems.
- The kept scratch-world base is still the one built from the old data.
- Place names hang by a hard-coded marker size, which a look edit can break.
- The `--cast-swords` timer reads new costs against an old snapshot.
- On the reload's tick other live input is dropped, and the replay toggles flip.

**Strengths**
- The thread boundary is clean: one action carrying an immutable `shared_ptr<const GameData>`, and presentation tables that stay on the main thread.
- Each file is all-or-nothing, and failures are logged with their reason.
- Presentation and simulation are cleanly separated, and the replay and recording rules are explicit.
- Version-keyed GPU uploads that count as held only after submission.
- `Logic` has good unit coverage for `ReloadData` (frozen tick, waiting scene, mid-change, recording, playback).

**Weaknesses**
- `WatchData` is a 70-line block that cannot be unit-tested (it needs a window and a renderer). Its order, its partial-failure behaviour and its `CheckScenes` refusal are untested; only the pure helpers are.
- Change detection is crude: any file in the folder (editor swap and temp files included), mtime only, no debounce, no record of which file changed. Each trigger re-parses everything, repacks the seals, re-logs every unknown-key warning, and logs "read again" at INFO for tables that did not change.
- Edits to `world.yaml`, `names.yaml` and `strings.yaml` are silently ignored (DEBUG level only).
- Cross-file references (seal keys named in `abilities.yaml`) are checked neither at start nor on reload.
- Renderer and HUD setters for UI colours, text options and reduce-flashing exist but nothing in the game calls them, so those presentation settings (accessibility included) cannot change at runtime and partly cannot be set at all.

**Overall:** sound for a debug-only feature. Reloading the map, strings and names is designed out, which removes the worst torn-state risks. What is left is papercuts and edge cases, plus maintainability risks that matter more once the planned data-driven sweep and mod support build on this path.

**Files read:** in full, the files in `files_read`. In part: `src/gfx/renderer.cpp` (Impl state, world upload block, setters), `src/game/host.cpp` (`SetData`/`ApplyData`/`EnterScene`/`Build`/`ScratchBase`), `src/gfx/internal/features/seals.cpp` (`SetPack`/`UploadPack`/`Upload`), `src/gfx/internal/features/ground.cpp` (`Update`/`Draw`), `src/gfx/internal/features/sprites.cpp` (`FogTexture::Update`), `src/core/effects.hpp`, `src/core/frame.hpp` (Action), `src/core/replay.cpp` (`NeverRecorded`), `src/data/game_data.cpp`, `src/data/world_look.cpp` (`ReadMarkers`), `src/game/scenes.cpp` (`CheckScenes`), `src/game/scenes/world.cpp` (Publish), `src/game/travel.cpp` (`SetTravelShare`), `tests/unit/core/logic_tests.cpp` (`ReloadData` cases).

**Files read in full:** src/app/app.cpp, src/app/app.hpp, src/core/logic.cpp, src/core/logic.hpp, src/main.cpp, src/app/hud.cpp, src/app/game.cpp, src/gfx/renderer.hpp, src/core/world_look.hpp, tests/unit/app/data_watch_tests.cpp, src/core/game_data.hpp

## Findings

### GAP1-HOT-RELOAD-FLOW-01: Any file change in the data folder re-parses every table, repacks and re-uploads the seals, and logs 'read again' at INFO even when nothing changed

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** performance / logging · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/app.cpp:448`

WatchData reloads everything whenever DataWatch::Changed() is true, and Changed() compares a listing of every directory entry. KeepSeals and KeepAnimations, unlike KeepLook and ReloadOf, never compare against the running table: they always replace it and return true. So SetSeals -> PackSeals -> SetPack (a new pack version) runs on every trigger, and so does a new Animations pointer. Each also logs 'the seals were read again' / 'the animations were read again' at INFO. Every loader also re-logs all of its unknown-key warnings on each trigger.

**Evidence:** `else if (KeepSeals(m_options.seals, std::move(seals))) { m_renderer.SetSeals(m_options.seals); PN_LOG_INFO("the seals were read again: ...")` while KeepSeals returns true for any readable table (app.cpp:210-216). The same holds for KeepAnimations (218-224). Seals and Animations have no Hash(), whereas WorldLook and Effects do.

**Impact:** Editing strings.yaml or world.yaml (which reload nothing) still repacks and re-uploads the seal buffers and floods the log with INFO lines claiming reloads. Editor swap and backup files (vim's .swp and 4913 probe, `~` backups, atomic-save temp files) appear in the listing and trigger the whole cycle repeatedly while someone types. That makes the log misleading when diagnosing a real reload.

**Recommendation:** Give Seals and Animations a Hash() (via Fields()) and have KeepSeals and KeepAnimations return false when it is unchanged, as KeepLook does. Restrict the DataWatch listing to `*.yaml`. Better still, return which files changed so only the affected loaders run and unchanged tables log nothing.

**Verification:** KeepSeals (app.cpp ~210) and KeepAnimations (~218) return true for any readable table with no hash compare, unlike KeepLook. Each trigger therefore re-sets the seals and logs INFO. The listing in DataWatch::List covers every entry, so editor temp files trigger it. It is debug-only (HOT_RELOAD) and cheap, so low rather than medium.

### GAP1-HOT-RELOAD-FLOW-02: The ReloadData tick drops the other live actions of that tick and inverts ToggleRecording/PlayReplay sent with it

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** correctness / input · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/logic.cpp:96`

RunTick runs ReloadData() first, then goes through the other branches. If ToggleRecording comes in the same tick while recording, ReloadData() has already finished the recording, so ToggleRecording starts a new one. The next F6 then overwrites the replay file the reload just wrote. If PlayReplay comes in the same tick while a replay plays, TakeInput has already stopped playback, so TogglePlayback tries to start the replay again under the new data. While a replay plays, the ReloadData tick also keeps every live action (TakeInput only stops playing and does not drop them), so a live Save or Load pressed during playback, normally ignored, takes effect on that tick. Without those, the tick returns at line 121-124 and silently drops casts, Select, WalkTo, CycleScene, ToggleWorldPause and DebugBurst.

**Evidence:** logic.cpp:96-124: `if (asked & ReloadData) ReloadData(); if (PlayReplay) {...TogglePlayback()} ... if (ToggleRecording|Save) {... ToggleRecording()} if (asked & ReloadData) { Count(true,false); return EndBurst; }`. Also 254-258: `if (any ReloadData) StopPlaying(...)` with m_input left as live input.

**Impact:** Rare in practice: the actions must land in the same 1/60 s tick as a reload. When it happens, the player's input is lost or reversed, and a written replay can be overwritten. Debug builds only.

**Recommendation:** Treat ReloadData as consuming the tick's timeline toggles: ignore ToggleRecording or PlayReplay when ReloadData already ended that timeline in the same tick. Either drop the other live actions with a debug log, as Wait() does, or let them tick under the new data.

**Verification:** logic.cpp:96-124 matches: ReloadData() runs first, then PlayReplay, Save and ToggleRecording are handled, and the tick ends with EndBurst. A same-tick ToggleRecording would restart a recording. The TakeInput path (~254) stops playback and leaves m_input intact. The scenario needs same-tick coincidence in debug builds, and I did not trace each sub-claim (such as Save during playback) in full.

### GAP1-HOT-RELOAD-FLOW-03: Seal keys named by abilities.yaml are never checked against seals.yaml, and a missing key is logged only once per run, never cleared on reload

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** validation / hot reload · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/renderer.cpp:719`

abilities.yaml names seal keys by text (data/abilities.cpp:173, core::SealKeyOf), and the arena and host name more in code. Nothing checks that seals.yaml has them, neither at start nor in WatchData. The renderer logs a missing key once and then never again (sealsMissing is never cleared, not even by SetSeals). WatchData also applies seals.yaml before it reads and sends the game data, so a coordinated edit (rename a seal in both files) briefly draws the old data's key against the new table. That transient miss is logged and then suppresses every later warning for that key.

**Evidence:** `!std::ranges::contains(sealsMissing, seal.key)) { sealsMissing.push_back(seal.key); ...log }`; Renderer::SetSeals only sets sealLooks and the pack. app.cpp:448-455 applies the seals before lines 476-498 send the game data.

**Impact:** A typo in a seal key, or a reload that removes a seal, makes an ability's seal silently invisible. After the first occurrence the log no longer says why.

**Recommendation:** Check every seal key the game data names against the seals table: in main (refuse to start) and in WatchData (keep the old tables when they disagree, or validate the pair together). Clear sealsMissing in SetSeals so a new table logs its own misses.

**Verification:** sealsMissing is a renderer-side vector (renderer.cpp:436, 719) that logs each key once, and I found no clearing in SetSeals. The ordering is as described: seals are applied before the game data is sent. I did not check whether other validation of seal keys exists. The impact is limited to a diagnostics gap.

### GAP1-HOT-RELOAD-FLOW-04: Edits saved while the game starts up are never reloaded: the watch takes its first listing after the files were read

- **Severity:** low · **Verdict:** confirmed · **Category:** hot reload / race · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/app.cpp:268`

main reads every data file, then opens the renderer. Renderer::Create prewarms every glyph of strings.yaml in every language. Only after that does App's constructor build DataWatch, whose first listing becomes the baseline. A file saved between its read in main and that listing is in the baseline, so it never counts as a change.

**Evidence:** main.cpp:85-139 reads the tables; main.cpp:142-155 creates the renderer and the App; app.cpp:267-269 `m_dataWatch = DataWatch{ m_options.dataFolder };` takes the first listing in its constructor.

**Impact:** Edits saved during start-up (which can take seconds) silently do not apply until the file is saved again. A confusing papercut while iterating on data.

**Recommendation:** Build the DataWatch, or take its listing, in main before reading the files and pass it in through Options. Any edit after the baseline then counts as a change.

**Verification:** The DataWatch baseline is built in App's constructor (app.cpp ~267), after main has read the files and created the renderer. Edits saved in that window are absorbed into the baseline. The window is real but small, and the effect is a papercut.

### GAP1-HOT-RELOAD-FLOW-05: DataWatch compares names and mtimes only, with no debounce: a non-atomic save seen mid-write can leave the broken read in force

- **Severity:** low · **Verdict:** confirmed · **Category:** hot reload / robustness · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/app/app.cpp:112`

The listing is (filename, last_write_time) only. A poll that lands between an editor's truncate and its write reads a half-written file. The loader logs an error and the old table stays, but m_seen already holds the new mtime. If the completing write gets the same mtime (Linux stamps files from a coarse clock, about 1-10 ms granularity, and some filesystems are coarser), the next poll sees no change, so the completed edit is never reloaded. Size is not part of the listing.

**Evidence:** `listing.emplace_back(entry.path().filename(), entry.last_write_time(unreadable));` and `m_seen = std::move(listing); return true;` even when every loader then fails.

**Impact:** Rarely, a correct edit is ignored after a transient parse error, and the game keeps the old data with no further message.

**Recommendation:** Add file_size to the listing. Debounce by waiting for one quiet poll (the listing unchanged) before reloading. Or keep a content hash per file and retry while the last read failed.

**Verification:** List() records only the filename and last_write_time. Changed() stores m_seen even when the loaders then fail. The same-mtime race is theoretical and rare, since the failure needs a coarse clock and a poll landing mid-write. Adding file size to the listing would be a cheap fix.

### GAP1-HOT-RELOAD-FLOW-06: Place-name labels hang by MarkerCity's hard-coded size while world_look.yaml (and its hot reload) may pick any marker sprite

- **Severity:** low · **Verdict:** unverifiable · **Category:** presentation / data coupling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/scenes/world.cpp:58`

The world scene anchors each place name below `SpriteId::MarkerCity`'s height and foot, because game never sees the look. Which marker is drawn, though, comes from world_look.yaml (`cityMarker`, `dangerMarkers`). ReadMarkers accepts any sprite in the world scene and never checks that its extent below the foot matches MarkerCity. Only a test on the shipped data does ('a test checks they do').

**Evidence:** world.cpp:56-69: `const core::SpriteInfo& marker = context.Manifest().Sprite(core::atlas::SpriteId::MarkerCity); const f32 markerBelow = ...; anchor = centre + {0, markerBelow + PLACE_NAME_GAP}`. data/world_look.cpp:333-370 ReadMarkers checks only presence and sort coverage.

**Impact:** A look edit or hot reload that picks a taller or differently footed marker makes every place name overlap its marker until the code changes. The reload is accepted with no warning.

**Recommendation:** Have LoadWorldLook refuse markers whose (height - footY) differs from MarkerCity's. Or have the renderer place place-name labels by the look's marker, keeping the presentation offset in gfx.

**Verification:** I did not open world.cpp:58 or ReadMarkers. The claim depends on a hard-coded MarkerCity extent against a look-chosen marker. The finding itself says a test on the shipped data covers it, so the risk is only for a future look edit. A reading of ReadMarkers would decide it.

### GAP1-HOT-RELOAD-FLOW-07: After a data reload, a dev scene re-entered keeps the scratch base built under the old data

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** saves / stale state · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/game/host.cpp:826`

Host::Build keeps m_scratchBase when it is for the same scene ('the one kept from an earlier visit, which is the same world'). After ReloadData, re-entering that dev scene builds the scratch world under the new creatures and abilities data, but the kept base is the old-data build. Saves then diff against a base that is not the scene's entry build. Fields ApplyData skips (for example a dead creature's Health.max, which keeps its old value) equal the old base, so they are omitted from the delta and come back as the new base's values on a load in a later session.

**Evidence:** host.cpp Build: `if (!m_scratchBase || m_scratchBaseScene != scene) { m_scratchBase = ...CopyOf(world) }`. SetData/ApplyData never reset m_scratchBase. The ScratchBase() comment and the Build doc assume the kept base is the same world.

**Impact:** Larger dev-scene saves after a reload, and a possibly inexact restore across sessions. Debug builds and dev scenes only.

**Recommendation:** Reset m_scratchBase (unless a save of it is held) in Host::SetData when creatures or abilities change, or key the kept base on the data hash as well as the scene.

**Verification:** host.cpp:828 keeps m_scratchBase when the scene is the same, and I saw no reset in SetData. Whether the old-data base then makes the save delta inexact depends on ApplyData internals, which I did not trace. It affects debug dev scenes only.

### GAP1-HOT-RELOAD-FLOW-08: Edits to world.yaml, names.yaml and strings.yaml are silently ignored, logged only at debug

- **Severity:** low · **Verdict:** confirmed · **Category:** usability / logging · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/app/app.cpp:499`

LoadGameData reads only creatures.yaml and abilities.yaml, and KeepWorld fills the running map and names, so these edits reload nothing by design. The only message is a DEBUG line 'the game data is as it was'. Because DataWatch returns a bool, WatchData cannot tell which file changed and so cannot tell the user that a restart is needed. The KeepWorld doc and the test 'a reload keeps the map as loaded' also describe reading a different map, which never happens: LoadGameData always returns world == nullptr.

**Evidence:** app.cpp:499-501 `else if (gameData) { PN_LOG_DEBUG("the game data is as it was: ...") }`. data/game_data.cpp ParseGameData returns `.world = nullptr`. app.hpp:180-182 KeepWorld doc.

**Impact:** Someone tuning the map or translations in a debug run sees no change and no INFO or WARNING explaining why.

**Recommendation:** Make DataWatch report the changed file names. Log at INFO or WARNING, naming the file, that it is read at start only and needs a restart. Align KeepWorld's doc with what it does (it supplies the start-time map and names to data read without them).

**Verification:** app.cpp:499-501 logs only at DEBUG ('the game data is as it was'), and KeepWorld copies the running world and names into the read data. The status known-deferred is consistent. I did not locate the deferral record, so deliberate-ness is unchecked. The impact is a usability papercut.

### GAP1-HOT-RELOAD-FLOW-09: Presentation settings (UI colours, text options, damage-number options, reduce flashing, text scale) can't change at runtime, and two are never set at all

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** accessibility / dead API · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/renderer.cpp:1648`

Renderer::SetUiColours, SetTextOptions and SetReduceFlashing, Hud::SetColours and SetTextOptions, and DamageNumbers::SetColours exist and are documented as working 'from the next frame on, with no restart', but only tests call them. settings.yaml is not watched. core::Settings has no reduce-flashing option and no text scale, so the renderer's accessibility support for both (TextOptions::scale, reduceFlashing) can never be switched on in the game.

**Evidence:** rg finds no non-test caller of SetReduceFlashing, SetUiColours, SetTextOptions or SetColours. main.cpp:147 passes `{ .language, .outline, .shadow }` without `.scale`. core/settings.hpp has no reduceFlashing field.

**Impact:** Accessibility options the renderer already supports are unreachable, and colour and text tuning needs a restart. This conflicts with the project's accessibility-first priority.

**Recommendation:** Add reduce_flashing and text_scale to settings.yaml and pass them through main. Either watch settings.yaml as the data folder is, or wire the setters to the coming settings screen. Until then, trim the setters' 'from the next frame' contract or test that the app calls them.

**Verification:** grep finds SetTextOptions, SetUiColours and SetReduceFlashing declared in renderer.hpp, with no non-test caller. main.cpp:147 passes no .scale, and core/settings.hpp has no reduce-flashing or scale field. The claim is true, but it describes unbuilt settings plumbing (the settings screen is still to come), not a regression. The accessibility-first rule gives it some weight. It is low, closer to a backlog item.

### GAP1-HOT-RELOAD-FLOW-10: WorldLook::Hash is hand-maintained: a field left out of it silently stops that field's edits from reloading

- **Severity:** low · **Verdict:** confirmed · **Category:** maintainability · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/world_look.hpp:218`

KeepLook reloads only when Hash() differs. Effects hashes itself through its Fields() archive, but WorldLook's Hash lists every member by hand (about 60 values). It is complete today. A new field added to any of the nested structs without updating Hash() would make edits to only that field 'read as it was', so the look would never reload. Its tests check only a few fields (drift, shade, clouds, one edit).

**Evidence:** world_look.hpp:216-291 is a hand-written hasher over each member. app.cpp:226-232 `if (!read || read->Hash() == current.Hash()) return false;`.

**Impact:** A latent reload bug that will surface quietly as the look grows (the data-driven sweep will add fields).

**Recommendation:** Give WorldLook and its nested looks Fields() methods and hash through the archive, as Effects does, so a forgotten field becomes a compile or archive error rather than a silent skip.

**Verification:** world_look.hpp:221-291 hashes every member by hand, and I found no member missing today. The tests (lines 299, 486-488, 757) check only a few fields. A new field left out of Hash() would silently skip reloads. This is a real maintainability risk, but a latent one.
