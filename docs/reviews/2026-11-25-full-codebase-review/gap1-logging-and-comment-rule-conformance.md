# Review: gap1-logging-and-comment-rule-conformance

**Scope:** Audit the roughly 211 PN_LOG call sites in src/ (grep PN_LOG and quill macros), plus the SDL log routing, against the user's logging rule in src/AGENTS.md (Logging section) and the memory note log-outcomes-not-steps.md: log the resulting value or event, never the attempt; no internals (hint strings, numeric categories, internal names); the right level out of the six; enough identifying context for a player's bug report; no per-frame or per-entity spam at INFO or above; every refusal or failure path logs its outcome once (earlier units found some unlogged Load refusals and a missing-seal key logged only once: find the rest systematically). In the same pass, sample the plain (non-Doxygen) comments in src/game, src/app and src/gfx against the root rule that a plain comment says only why and never narrates what the next lines do (lint checks only the design-reference half of this rule). Report the offending sites grouped by pattern, not one finding per line. (Why this unit exists: Both are explicit user rules. x-resilience judged logging infrastructure (async, no crash flush, DEBUG always on) but nobody checked the log call sites themselves, and comment conformance was checked only in shaders/. Lint enforces neither the outcome-logging rule nor the why-only comment rule.)

**Findings:** 12 standing (0 critical, 0 high, 2 medium, 8 low, 2 info), 0 raised but refuted on verification.

**Parts:** gap1-logging-and-comment-rule-conformance.md, gap1-logging-and-comment-rule-conformance-part2.md

## Assessment

Scope: the 211 PN_LOG matches (about 195 real call sites once log.hpp doc examples and AGENTS.md lines are dropped), the SDL routing in `src/platform/sdl_log.cpp`, and a sample of about 460 plain in-body comments plus the header member comments in src/game, src/app and src/gfx. I read the call sites in context for saves/loads, replay, Host, Logic, App, Renderer, window and the data loaders. Read-only, nothing built.

**Logging, strengths.**
- Levels are used well. Recoverable problems are WARNING, failures are ERROR, and fatal paths are CRITICAL.
- Repeating failures are deduplicated. `core::RepeatedFailure` logs the first failure and the first recovery for upload overflow, tiny seals, upload failures and frame failures. The missing-seal and missing-glyph warnings are logged once per key or character.
- Most messages report an outcome with a value. Examples are the display line, "scene X resident in N ms", "saved the game to PATH", and the save-refusal reasons in `ReadGameWorld` and `ReadScratch`, which say which check failed.
- There is no per-frame INFO or higher. `App::HandleInput`'s pause log fires only on a state change.
- Nothing is logged around SDL or Quill internals beyond the one routing shim.

**Logging, weaknesses.**
- Failure coverage is uneven.
  - Host's save-refusal path is mostly silent. The same Host::Load, Logic::Load and replay-start code logs outcomes elsewhere.
  - The per-tick combat DEBUG logs have no rate limit.
  - A player's bug report would show no game version, build, OS or GPU name.
- Several INFO lines log the attempt, not the result: "a save asked for", "a month asked to pass", "the world's pause asked for…", "the game data changed: reloading it".
- Tick numbers are inconsistent. Logic logs wall ticks, while Host and Combat log simulation ticks, and both just say "tick N". A reader can't line the two up.
- `Saves::Write` serves both saves and replays but always says "saved the game".

**Comments, overall.** Plain comments in src/game, src/app and src/gfx conform to the why-only rule very well. My sample was mostly why-comments: invariants, units, cautions and MSVC workarounds. The offenders are few and mild. They cluster in private member declarations that use `//` where `///` was meant, and in scene layout placement notes. I found no design-process citations.

**Risks.**
- The unlogged Host::Load refusals are the largest player-facing gap. A malformed or stale save yields only the generic "malformed or from another version" line.
- SDL routing is installed late, after `UserDataDirectory()` and settings loading, so early SDL output bypasses the log file.
- DEBUG is always on and the ring buffer is async, so the per-entity combat DEBUG lines are cheap but flood the file under F7 or stress.

**Files read in full:** (not reported)

## Findings

### GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-01: Host::Load refuses saves silently for most checks; Logic::Load's single generic ERROR hides the cause

- **Severity:** medium · **Verdict:** confirmed · **Category:** logging-failure-paths · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1688`

Host::Load() returns false without any log for: a wrong ARENA_FORMAT, an unknown scene key, a scene needing the world map when the game has none, ACTIVE_VERSION mismatch, a trailing-bytes archive (`archive.Remaining() != 0`), `Consistent()`, `WorldFits()`, `Within()`, the OnMapPortal check, `playerFound`, `RouteFits`, `AimsAtCreatures`, `FlashesStartBy` and `SealsStartBy`. ReadGameWorld and ReadScratch also fail silently on WORLD_VERSION mismatch and on `State::CountersFit`, while their sibling refusals do log. Only the generator-version, base-hash, next-ID, domain-delta, domain-order and journey-delta refusals log. Logic::Load then logs only 'not loaded: <slot> is malformed or from another version of the game'.

**Impact:** A player or developer with a refused quicksave cannot tell which check failed from project_n.log. The user's rule is that every refusal logs its outcome once, and this is the same class as the unlogged-Load findings recorded earlier. The Host::Load silent relationship-table refusal is only the known-deferred tip of this.

**Recommendation:** Make Load collect the first failing reason and log it once, for example by naming each check and logging at WARNING 'save refused: <reason>' in a single place at the end of Load. Cover the format, scene, versions, trailing bytes and each fit check. Add a test per reason.

**Verification:** Read Host::Load (host.cpp:1637-1710): format/scene/map, ACTIVE_VERSION, Remaining, worldsFit, playerFound, routeFits and the Combat checks all end in archive.Fail() or a bare `return false` with no PN_LOG. The sweep-up-backlog entry (line 86) records only the relationship table and the world section version, so the wider set is new, not deferred. The reasoning holds.

### GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-02: A bug report's log carries no game version, build, OS, CPU or GPU name

- **Severity:** medium · **Verdict:** confirmed · **Category:** logging-context · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/main.cpp:167`

The first lines are `PN_LOG_NOTICE("project_n starting")` and 'SDL x.y.z on <platform>', then the GPU line 'GPU driver: {}, presenting with {}' (the driver name only, not the device) at renderer.cpp:1396. There is no project_n version or commit, no build mode or compiler, no OS version, no CPU, no GPU model or Vulkan/D3D/Metal API version, no worker count before app.cpp:266, and no data or save-folder path other than the log's own. Settings and data lines print paths but not the build.

**Impact:** A player's attached project_n.log cannot be tied to a build, so a reported rendering or timing bug cannot be reproduced or deduplicated. 'Enough identifying context for a bug report' is a stated criterion.

**Recommendation:** Log one NOTICE banner at start: version (and commit) from the build, preset and mode, OS, CPU count, and the GPU device name from `SDL_GetGPUDeviceProperties`.

**Verification:** grep shows only two PN_LOG_NOTICE calls, 'project_n starting' (main.cpp:167) and the stop line. main.cpp has no version, build, OS or CPU banner. I did not check the SDL and GPU lines the finding cites. The 'enough context for a bug report' criterion is not met. Medium is fair: it is cheap to fix and limits triage of player reports.

### GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-03: Saves::Write logs replay files as 'saved the game' and 'no folder for saves'

- **Severity:** low · **Verdict:** confirmed · **Category:** logging-misleading · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/saves.cpp:24`

Game owns `m_saves` and `m_replays`, both `core::Saves`. `Logic::FinishRecording` calls `m_replays->Write()`, so the message 'saved the game to {}' (INFO), 'not saved: there is no folder for saves' (WARNING) and the bare `PN_LOG_ERROR("{}", written.error().message)` also describe a replay file. `Saves::Read` returns 'there is no folder for saves' for the replay slot too, which Logic then prefixes with 'no replay played:'.

**Impact:** The log claims a game save happened when a replay was written. A bug report with 'saved the game' lines after F6 misleads diagnosis. The ERROR line carries no context about what was being written.

**Recommendation:** Give Saves a noun ('game' or 'replay') or have the callers log. Include the slot path in the ERROR line.

**Verification:** core/saves.cpp:13-26 logs 'saved the game to' and the bare ERROR with no noun or path. Logic::FinishRecording uses a second core::Saves for replays, which I took from the finding and did not open. The ERROR line has no context.

### GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-04: Attempt-logging: INFO lines record that something was asked for, not what resulted

- **Severity:** low · **Verdict:** confirmed · **Category:** logging-rule-attempts · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/app.cpp:561`

App.cpp ACTION_CONTROLS has an `.asked` string per control, for example 'a save asked for', 'a load asked for', 'a month asked to pass', 'the next scene asked for', 'a debug burst of hits asked for', 'a replay recording started or finished', and logs it at INFO (line 561). Line 546 logs 'the world's pause asked for by the date line'. Line 488 logs 'the effects changed: reloading them', line 492 'the game data changed: reloading it', and line 513 'the world's pause'. The results are logged elsewhere, in Logic, Host and Saves.

**Impact:** This is the rule's rejected pattern (log the result, not the attempt), and it duplicates the downstream outcome lines. The `.asked` text table is dead weight in the binary and adds an INFO line per debug keypress. The 'recording started or finished' text is also ambiguous and is not an outcome.

**Recommendation:** Drop the `.asked` field and these INFO lines. If kept for input tracing, make them DEBUG. Log only outcomes, which Logic and Host already do. For effects, log once the effects have been applied, with what changed.

**Verification:** app.cpp:46-61 has the `.asked` strings ('a save asked for', 'a load asked for') and line 561 logs them at INFO. This is the attempt pattern the rule rejects. Low severity: it is one line per keypress, and it does show the input arrived.

### GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-05: Per-entity and per-use DEBUG logs in the combat tick can flood the file in stress scenes

- **Severity:** low · **Verdict:** unverifiable · **Category:** logging-volume · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/combat.cpp:480`

`Combat::Hit` logs 'entity {} dies' per death (line 480), and `Combat::EndUses`/tick logs 'ability use {:#x} ends' per use (line 643). Cast, dash, lance and seal DEBUG lines also run per action. With the F7 debug burst ('hits every fox thousands of times'), the stress scene (~100k instances) and respawning crowds, these fire hundreds of times a tick. DEBUG is always on and always written to the rotating file (10 files kept).

**Impact:** The log file rotates away the earlier startup and error lines a bug report needs. The formatting happens on the logic thread, and the formatted bytes go through the Quill ring buffer; this is slower than not logging at all inside the 60 ticks/s budget. 'entity N' with a bare stable ID is not meaningful to a player.

**Recommendation:** Aggregate per tick, for example '{} creatures died this tick', as line 524 already does for removals. Drop the per-entity death line, or log it only for the player and bosses, and name the creature kind.

**Verification:** I did not read the combat.cpp call sites. Volume at DEBUG in stress scenes depends on actual rates, which only a log from an F7 burst or a stress run would show. The 'bare entity ID' point is plausible. Measure how many lines a stress run writes.

### GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-06: Tick numbers in log lines mix two clocks without saying which

- **Severity:** low · **Verdict:** confirmed · **Category:** logging-context · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/logic.cpp:152`

Logic logs `m_counters.wall` as 'at tick {}' (scene changes, saves, loads, replay lines). Host and Combat log `counters.simulation` as 'tick {}:' (portal, pause, walk-to, casts, dashes, deaths). Replay divergence says 'wall tick' explicitly, but the others do not. Wall ticks include paused and loading ticks while simulation ticks do not.

**Impact:** A reader correlating 'saving the game at tick 5000' with 'tick 4200: the cultivator steps through the portal' cannot place them in order. Bug reports are harder to line up with saves and replays.

**Recommendation:** Pick one prefix convention. Call them 'wall tick' and 'sim tick', or log both in a shared prefix. Document it in the Logging section of src/AGENTS.md.

**Verification:** logic.cpp logs m_counters.wall as 'at tick {}' (lines 153-223, 356-402) and host.cpp logs counters.simulation as 'tick {}:' (lines 847-871). Only the replay divergence line (logic.cpp:305) says 'wall tick'. The src/AGENTS.md Logging section does not document the distinction.

### GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-07: Internals leak into messages: seal key hashes, scene-change counters, timeline epochs, raw entity IDs, slot numbers

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** logging-rule-internals · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/renderer.cpp:721`

The missing-seal warning prints 'a seal (key {:#018x})', a 64-bit hash of the name, so the player or developer cannot tell which seal is meant (the earlier unit's note concerned logging it once; this concerns what it logs). Other lines use internal numbers: logic.cpp:152-223 'scene change {} ... leaving scene {} for scene {}' (the change counter and std::to_underlying scene keys), 'timeline {} begins' (`m_epoch`), combat.cpp:381 'raises seal {:#x}', combat.cpp:643 'ability use {:#x} ends', host 'slot {}', 'the selected entity {} is gone', 'a walk-to tile {}' (a tile index), and renderer.cpp:84 '(a size that isn't a normal float)'.

**Impact:** The user's rule is 'no internals (hint strings, numeric categories, internal names)'. These need the source to decode. The seal-key line is the one most likely to appear in a player's report ('draws nothing') and it names no seal.

**Recommendation:** Log the seal's name where the table is keyed by name, or log the hex key plus the owning ability name. Name scenes and abilities by their names (`SceneName`, `ABILITY_NAMES`). Keep counters at DEBUG only, or drop them. Show tile indexes as column and row.

**Verification:** renderer.cpp:721 does print the raw 64-bit seal key, and Logic/Host use raw scene numbers ('scene change {}', 'timeline {}'). The seal key is the strongest case. The renderer only has seal.key, so naming the seal needs a reverse lookup or a name in the snapshot, which makes it more than a log-text fix. Host already uses SceneName in several lines, so the scene-number claim is partly overstated. I did not check every cited line.

### GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-08: SDL log routing is installed late and drops SDL's category; early SDL output bypasses the log file

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** logging-sdl · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/platform/sdl_log.cpp:34`

`RouteSdlLogs()` is called at the top of `Run()` in main.cpp, but `UserDataDirectory()` (SDL_GetPrefPath) and the settings load run before it, so any SDL message from them goes to SDL's default stderr output and never reaches project_n.log. The callback also ignores the `category` argument, so 'SDL: ...' lines cannot be told apart. `SDL_SetLogPriorities(DEBUG)` plus the always-on file level routes all SDL INFO to the console (INFO is the console threshold), and SDL's GPU, video and input subsystems can be chatty. The priority mapping is clean otherwise.

**Impact:** Early SDL failures (pref path, base path) are lost from the file, the one place the user asked everything to go. Category-less messages are harder to attribute. Console noise appears at INFO.

**Recommendation:** Call `RouteSdlLogs()` right after `core::log::Start()` and before the first SDL call. Prefix the message with SDL's category name (SDL_LOG_CATEGORY_* names) in the log text, in the user's words, not the number. Consider routing SDL INFO to DEBUG.

**Verification:** Confirmed: main.cpp:68 calls RouteSdlLogs() at the top of Run(), after UserDataDirectory() (line 162, SDL_GetPrefPath at paths.cpp:19) and after the settings load, so SDL output from those goes to stderr only. I did not open the callback to check whether it ignores the category, and I did not check the SDL INFO-to-console claim. Impact is low, since SDL rarely logs in those two calls.

### GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-09: Silent drop paths: Combat::Cast, window/gamepad, and several refusals log nothing

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** logging-failure-paths · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/combat.cpp:199`

`Combat::Cast` returns silently when the player is not in ACTIVE, the point is not finite, there is no Caster or it is Dying, the slot is out of range, the ability is None, or the cooldown has not elapsed. Only the dash and not-enough-Qi paths log (DEBUG). Other silent paths: `Host::CastAction` for a missing scene is logged but `Host::Select` logs INFO for every click. `Logic::Restore` is silent and only returns false. `Logic::TogglePlayback`'s 'its start state is malformed' does not say which check. The replay `ReadTicks` refuses on a bad action kind without a log (the Replay::StartPlaying error carries a message, but this inner path is generic).

**Impact:** A player who reports 'my cast did nothing' has no line for cooldown or Dying causes. Replay or save incompatibilities show a generic reason. Compare Host::Load above.

**Recommendation:** Add a DEBUG line per ignored-cast reason (a single 'cast ignored: <reason>' helper), and give Restore/ReadTicks failures a reason, logged once.

**Verification:** Combat::Cast (combat.cpp:199-230) does return silently on not-ACTIVE, non-finite point, no Caster/Dying, slot>=SLOTS, None ability and cooldown; only dashing and not-enough-Qi log at DEBUG. Cooldown and Dying silence is normal gameplay and per-tick spam risk makes logging them debatable; the 'my cast did nothing' point is valid but weak. The Host::Select INFO remark is a muddle. I did not check Restore/ReadTicks.

### GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-10: Sparse context in ERROR lines: bare error messages with no subject

- **Severity:** low · **Verdict:** confirmed · **Category:** logging-context · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/main.cpp:181`

main.cpp:181-201 logs `PN_LOG_ERROR("{}", quitAfterNs.error().message)` (and zoom, workers, travelShare, monthStepAt), and saves.cpp:22 logs `PN_LOG_ERROR("{}", written.error().message)`. The text depends entirely on whoever built the Error. Window.cpp:384 and similar prefix a `what`. The data-warning lines (`PN_LOG_WARNING("{}", warning)` in settings, strings, game_data, seals, effects, world_look, world_map, names) also print a bare string.

**Impact:** If an Error message lacks the file or argument (for example 'it must be a number'), the log line is uninformative. The data-warning sites rely on the loader naming the file and line; none is checked by a test.

**Recommendation:** Prefix with the subject ('--zoom: ...', 'save write failed: ...', '<file>: ...'). Add a test that each loader's warning names its file.

**Verification:** main.cpp:181-201 logs bare '{}' of Error messages for quitAfterNs, zoom, workers, travelShare and monthStepAt, and core/saves.cpp:22 logs bare written.error().message. Whether the text names the argument depends on the builder, which I did not check. The finding is accurate but speculative on impact.
