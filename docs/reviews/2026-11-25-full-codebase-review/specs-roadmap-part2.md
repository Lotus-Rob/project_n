# Review: specs-roadmap (part 2)

### SPECS-ROADMAP-11: Stand-in screens built in 5c2, 6 and 7 are rebuilt in 8 and 10: throwaway work from the chunk order

- **Severity:** low (reviewer said medium) · **Verdict:** deliberate · **Category:** sequencing · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-design.md:146`

Decisions 12 and 15 put the demo-world chunks (5–7) before the UI chunks (8–11). As a result: chunk 5 builds the enter prompt 'as game text until the screens chunk' (chunk 5 line 179); chunk 6 builds speech bubbles as game text 'until the UI chunks'; chunk 7 builds the dungeon popup, results panel, preset-loadout cycling and route map large view as 'stand-in screens until chunk 10' (lines 136-147), each with its own gamepad path and device prompts, which chunk 10 builds later. Chunk 10 then rebuilds the enter prompt, popup (with full loadout editing) and results (lines 67-72). Device-prompt glyphs are needed by the stand-ins but arrive in chunk 10.

**Impact:** About four interactive stand-ins, each with gamepad navigation, tests and reference pictures, are written, reviewed, measured and then deleted. Accessibility and gamepad work on them is duplicated.

**Recommendation:** Revisit the order after the game-design brainstorm: build chunk 8 (the ui module plus device prompts) before chunks 6 and 7, so their dialogs are real windows from the start. Or shrink the stand-ins to the bare minimum with no gamepad or prompt work. This reopens the user's decisions 12 and 15 on cost grounds.

**Verification:** Phase6 design decision 12 (line 143) and 15 put the demo world before the UI chunks, and the table marks 5c as using stand-in town and enter prompt. The ordering is the user's recorded choice, and the finding admits it contradicts that. The cost claim is plausible but it is an opinion, and the duplicated work cannot be measured by reading.

### SPECS-ROADMAP-12: Chunks 6, 7 and 9 encode game systems ahead of the game-design brainstorm that is meant to define them

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** plan-flaw · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk9-view-models-design.md:56`

The game-design brainstorm row runs 'before chunk 6' and covers cultivation, sects, social simulation, combat depth and the core loop. Yet the approved chunk specs already fix these systems: chunk 6 has sects and guilds as one record kind with a colour and home city, fixed roles, and keepers who never travel; chunk 9 has a saved, unchanging seeded attitude field (save version bump) and cultivation tiers named from `power`; chunk 7 has orbs 'for later' XP and loot, four enemy tiers 'differing in data only', and pace targets tuned with enemies that don't fight back. Memory game-design-gaps.md says '"power" is a placeholder' and 'towns (chunk 6) and combat (chunk 7) get designed in a vacuum'.

**Impact:** If the specs are planned as approved, the save fields, world records and data tables built for placeholders get reworked once the game design lands. Balance targets ('an arena clears in under a minute') are tuned against a combat model that will change.

**Recommendation:** Make the game-design brainstorm's output include a pass over chunks 6–11. Mark each spec 'to be revised after the game design', and re-approve them before planning, rather than treating the 2026-11-10 approvals as current.

**Verification:** The phase6 table row GD (line 111) runs the game-design brainstorm before chunk 6, and chunks 6, 7 and 9 do already fix systems. A reviewer re-checking specs after GD is implied by the process, as each chunk is brainstormed and specified on its own. 'Specs approved 2026-11-10' may be stale, but whether they are revised is unrecorded. Mostly a process risk, not a defect.

### SPECS-ROADMAP-13: Shared-routes: no path smoothing, and a quality bar that can't see zigzags or short-trip detours

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** design-risk · **Status:** new · **Reviewer confidence:** medium
- **Where:** `.claude/worktrees/shared-routes/docs/superpowers/specs/2026-11-24-shared-routes-design.md:98`

Goal 5 bounds route cost within 5% of the cheapest on randomly sampled pairs (plan Task 7: '10 pairs drawn by core::Random among passable tiles and places', Task 9: 20 pairs). Random pairs on a 150×85 map are mostly long, where a few tiles of detour at each gate are under 5%. Through open country, every route must pass through a gate tile on every border it crosses; with spacing 8, that can mean a lateral jog of up to about 4 tiles per sector. The anti-goal ('Travellers funnelled along sector borders or through gate-to-gate zigzags a player can see in open country') is checked only 'by eye on the reference pictures', and nothing smooths the path. The spec's stated future users (wandering, hunting, gathering, trips to arbitrary spots) are exactly short off-road trips, where both the relative detour and the visible zigzag are largest.

**Impact:** Once activities arrive, NPCs leaving the roads will walk visibly bent paths and pile up on gate tiles, while the bar passes. The quality cost of the design is hidden by how the samples are chosen.

**Recommendation:** Stratify the quality samples by distance (short, under 2 sectors; medium; long) and report the worst ratio per band. Add a shape check, such as off-axis deviation against `FindRoute` or the share of steps on gate tiles. Plan either a local direct search for trips within a few sectors or a smoothing pass (string-pulling within sectors) for stage 2 or the activities work, recorded as a measured spike.

**Verification:** Spec goal 5 (line 98) uses sampled pairs, and goal 14 and the plan define no smoothing or shape check. The stratification gap is plausible. The spec's stated future users are not verified here, and whether the zigzag matters is a measured question, so it is partly speculative. Stratifying samples by distance is a cheap improvement.

### SPECS-ROADMAP-14: Stage 1 plan: A* end condition with several candidate end nodes is underspecified

- **Severity:** low · **Verdict:** confirmed · **Category:** underspecified · **Status:** new · **Reviewer confidence:** medium
- **Where:** `.claude/worktrees/shared-routes/docs/superpowers/plans/2026-11-25-shared-routes-stage1.md:584`

'End: `target` a node ends there; otherwise each node of its sector may end at `WalkTo(target)`'s cost more.' The plan doesn't say when the search stops. Stopping at the first popped node of the target's sector is wrong: its total (cost + WalkTo) may exceed another candidate's. The correct form is a virtual goal pushed with key cost + WalkTo and popped like any node. The estimate must also stay at or below the true cost to the virtual goal, which needs stating because `WalkTo` costs are bounded inside the sector. 'Both ends in one sector ... wins unless the graph's is strictly cheaper' also depends on this being right.

**Impact:** An Opus implementer will likely get it right, but a plausible wrong implementation passes most hand-map tests (few candidates) and fails only on the synthetic 5% check, which costs a review round. A tie-break between candidates with equal totals must also be fixed for determinism.

**Recommendation:** State the virtual-goal formulation and its tie-break (total, then node ID) in the plan's search rules, and add a test where the first-popped target-sector node is not the cheapest end.

**Verification:** Plan line 584-585 says each node of the target's sector 'may end at WalkTo(target)'s cost more' and the search bullets give no stop rule or virtual goal. The A* stop condition and tie-break among end candidates are indeed unspecified. The note about the Opus implementer is fair, so severity stays low.

### SPECS-ROADMAP-15: Gate rule can't guarantee 'roads always have their own' gate

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** plan-flaw · **Status:** new · **Reviewer confidence:** medium
- **Where:** `.claude/worktrees/shared-routes/docs/superpowers/plans/2026-11-25-shared-routes-stage1.md:35`

Spec Decision 2 promises 'roads always have their own' gate. Plan Ruling 2 cuts a run into ceil(L/8) pieces and keeps one cheapest pair per piece. When two roads (or a road and a ford or bridge) cross a border within one 8-pair piece, only one gets a gate. The other's travellers leave the road to reach it, which counts against Task 7's 'routes keep to its roads' bound. Ruling 2 can also place two gates next to each other (offsets 7 and 8 at a piece boundary).

**Impact:** Junctions near sector borders, which are common where roads fork close to cities, produce off-road detours. The shipped-roads test may fail for a structural reason, not a tuning one.

**Recommendation:** Make every maximal run of road-to-road pairs (or every local cost minimum) a gate in addition to the spacing pieces. Or record that roads within 8 tiles share a gate and accept it measurably in S1 and S2.

**Verification:** Plan line 36 cuts a run into ceil(L/gateSpacing) pieces and keeps one pair per piece, and spec line 214 says 'roads always have their own'. Two roads crossing a border within one piece plausibly share a gate, so the promise is not strictly guaranteed. Whether it fails the shipped-roads test is untested, and the plan treats it as tuned, so impact is speculative.

### SPECS-ROADMAP-16: Shared-routes budgets measured on 4 workers while the phase bar pins 2, and the game builds the graph with no workers

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** measurement · **Status:** new · **Reviewer confidence:** medium
- **Where:** `.claude/worktrees/shared-routes/docs/superpowers/specs/2026-11-24-shared-routes-design.md:144`

Goal 14's budgets are 'development machine, release build, 4 workers'. The phase spec pins measurement runs 'to 2 workers, as on the minimum spec's 4 threads' (phase spec lines 78-79). Decision 11 says world creation, `Host::Load` and `SetData` 'have none [no job system] today, so the game builds it in turn'. So the 1 s budget at 50× is about 4 s on the logic thread in the shipped game, before place tables. Chunk 10's load aims (save under 2 s, new world under 5 s) are not mentioned.

**Impact:** The benchmark proves a configuration the game doesn't run. At larger maps, the load and new-world aims fail, or the logic thread stalls during a load, without any number having flagged it.

**Recommendation:** Report the build row also at 2 workers and with no workers. Cross-reference chunk 10's load aims. Give `GenerateWorld` and `Host::Load` the job system (Travel::Seed would gain too) as a named prerequisite before maps grow.

**Verification:** Spec line 144 says budgets are measured on 4 workers, and decision 11 (line 238-241) says the game builds the graph in turn with no workers, so the shipped config is not benchmarked as the finding says. The spec does note that workers arrive with the 'creating world' screen, which is a recorded deferral. The phase's 2-worker pin and chunk 10's load aims were not checked in detail.

### SPECS-ROADMAP-17: Chunk 10 relies on features that don't exist yet: blur passes, progress during generation, the save picture and play time

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** underspecified · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk10-screens-design.md:118`

(1) Modals draw 'over the blurred scene (0002.4 §4's blur passes)', but no blur exists in gfx or shaders (grep finds only labels.hpp). (2) New-world loading 'publishes its progress (stages and fractions) as a small view', but `GenerateWorld` runs synchronously in the Host constructor on the logic thread (src/game/world_state.cpp:52), and the logic doesn't publish while it runs. (3) The save picture is 'the scene target downscaled when the save is made', but Save is a logic-thread action written by a background job, so the main thread's GPU readback has to be joined to it, and the spec doesn't say how. (4) 'Play time' and 'unsaved progress' have no definition (ticks? wall time while unpaused? hashed or not?).

**Impact:** Each item is hidden work, and (2) and (3) are cross-thread designs left to an implementer. Chunk 10 is already one of the largest chunks.

**Recommendation:** List these as named design items in chunk 10's revision: the blur pipeline, incremental or forked generation with a progress publish, a picture handoff (the main thread captures, then attaches through an action or a side file), and play time as a saved, unhashed tick count.

**Verification:** Chunk10 spec lines 112-119 does say new-world generation moves onto the job system and publishes progress as a view, and the blur is called out as 0002.4 §4's blur passes (planned work, not an oversight). Items 1 and 2 are therefore declared work rather than hidden. Item 3 holds: the spec only says the picture is the scene target downscaled when saved, with no logic-to-main-thread handoff. Item 4 holds: play time and unsaved progress are named at lines 14, 55-57 but not defined. Real gaps are 3 and 4 only.

### SPECS-ROADMAP-18: Chunk 11's live previews need a second, synthetic scene through a single-scene renderer

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** scope · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk11-settings-design.md:154`

'The help pane draws a small preview through the renderer: a fixed tiny scene (a cultivator, a fox, a label, a few damage numbers, a sword trail) into an offscreen target ... looping.' The renderer draws one `Snapshot` at a time. Swords, trails and damage numbers come from the snapshot's volleys and events, laid out at the render tick, and sprites load per atlas scene. A preview means a fake snapshot and event stream with its own clock (which must keep running while the game is paused, per pause-freezes-game-animation), a second render target, and sprites resident outside the current scene.

**Impact:** A large hidden engine feature inside the settings chunk. Its scope is likely to slip or be cut late.

**Recommendation:** Make previews their own sub-chunk or spike with a design (a preview snapshot builder, its own clock, a resident sprite set), or start with static before-and-after images.

**Verification:** Chunk11 spec lines 46 and 154-156 do specify a looping offscreen preview scene through the renderer. The scope concern is plausible, since the renderer takes one Snapshot and src/AGENTS.md describes per-scene sprite loading. The spec does not say how the preview clock runs while paused or how its sprites stay resident. This is a risk and an underspecification, not a factual error.

### SPECS-ROADMAP-19: Chunk 9 sends subscriptions as unrecorded actions though 5a built a latest-value side channel for exactly this

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk9-view-models-design.md:28`

Chunk 9 (kept from 0002.4 §8): 'an action `Subscribe` carries a view request ... both change only what is published, so they are never recorded in replays and the state hash ignores them'. Since then, 5a added `GameHandover::SendView` / `core::ViewReport`, 'a latest value that is never tick input (never recorded, hashed or saved)' (src/AGENTS.md), which already carries the cull rectangle and animations. Using actions means adding kinds to `NeverRecorded` (src/core/replay.cpp:20) and queuing presentation requests through the tick's input. The spec also says leaving sight 'clears the selection' on the world map, while today 'a selection out of sight is kept, undrawn' (src/AGENTS.md). That is a behaviour change to the recorded selection state that the spec doesn't flag.

**Impact:** Two mechanisms for 'publish-only' requests, and a growing never-recorded list. The selection change may surprise implementers.

**Recommendation:** Revise chunk 9 to carry subscriptions in `ViewReport` (latest set of views wanted). Call out the selection-clearing change explicitly, and say whether clearing happens in the simulation (deterministic, from sight) or only in the panel.

**Verification:** Chunk9 line 28 does have Subscribe/Unsubscribe as actions, kept from 0002.4 §8. I did not re-check that ViewReport/SendView exists. The selection-clearing change is confirmed: chunk9 lines 52 and 135 say leaving sight clears the selection, while src/AGENTS.md says a selection out of sight is kept, undrawn. The spec does not flag this as a change, and it is a legitimate gap. Whether subscriptions should use ViewReport is a design opinion, and the reasoning is sound.

### SPECS-ROADMAP-20: Chunk 8 HUD scope ignores the world map's date line, the HUD's one clickable area

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** underspecified · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk8-ui-design.md:157`

Chunk 8 rebuilds the HUD as 'the player's health and Qi bars and the action bar, shown in combat scenes only; on the world map and in towns no combat HUD shows'. Today `app::Hud` also lays out the world map's date line (`Hud::BuildDate`, `DateLineArea`), which is clickable (`DateLineClick` sends `ToggleWorldPause`). The spec doesn't say whether it moves to RmlUi (as a window, with gamepad focus) or stays as drawn HUD. The backlog item 'Presentation out of app' also expects hud and damage_numbers to move into `ui`.

**Impact:** After chunk 8 there are two HUD systems. The date line's click stays mouse-only, outside the gamepad reachability test.

**Recommendation:** Include the date line (and world-pause control) in chunk 8's HUD scope as a focusable RmlUi piece, and fold the 'Presentation out of app' move into the chunk's plan.

**Verification:** src/AGENTS.md confirms the world-map date line (Hud::BuildDate, DateLineArea) is the HUD's one clickable area, sending ToggleWorldPause through DateLineClick. My grep of the chunk8 spec for date-line and pause terms found no match, so it is silent on it. That makes it a real underspecification. The claim that the backlog expects hud and damage_numbers to move into ui was not checked.
