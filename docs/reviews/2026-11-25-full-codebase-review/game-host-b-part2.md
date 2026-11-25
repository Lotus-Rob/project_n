# Review: game-host-b (part 2)

### GAME-HOST-B-11: AdvanceCalendar clears the leg-walk cache and kept walks even when the game's journeys did not change

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** performance/presentation · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1585`

DropKeptWalks() and m_legWalks->Clear() run unconditionally. They also run when the step ran on a dev scene's scratch world (M in the arena: the game's world and its journeys are untouched), when travel was skipped (past TRAVEL_END, stop requested), and when the running scene does not show the map. The tables belong to the game's world's journeys, which did not change in those cases.

**Evidence:** Line 1571 guards Finish/Month with `Running().Traits().worldMap && m_paths && ...`; lines 1585-1588 clear regardless.

**Impact:** Coming back to the world map after an M in a dev scene rebuilds every walk table for the same journeys: a presentation hitch for no reason. Minor today.

**Recommendation:** Clear only when Travel's JourneysStamp changed (compare before and after the travel block), or move the clear inside the travel block.

**Verification:** The unconditional DropKeptWalks and Clear at 1585-1588 are as described. src/AGENTS.md documents that the caches are dropped at each month step, and KeptWalks are re-sized whenever JourneysStamp differs, so the drop is partly redundant with the stamp. The extra rebuild after an M in a dev scene is real but trivial.

### GAME-HOST-B-12: A stop request during AdvanceCalendar leaves a half-stepped month

- **Severity:** info (reviewer said low) · **Verdict:** confirmed · **Category:** correctness/robustness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:1571`

If `stop.stop_requested()`, the travel block (Finish and Month) is skipped, yet `++world.months` has already run (and the clock may have jumped). MonthStep::Run is still called and may stop part-way. The world is left with the month counted but journeys over by the clock not finished, so their members stay on Action::Travel past their journey's end.

**Evidence:** `++world.months;` at 1567 runs before the `!stop.stop_requested()` test at 1571; m_monthStep.Run(..., stop) follows.

**Impact:** Harmless while a stop only means quitting. If anything ever saves, hashes or records after a stop (an autosave on quit, a replay's final hash), it captures a state no uninterrupted game reaches.

**Recommendation:** Check the stop token once at the start of AdvanceCalendar and return before changing anything, or document that the state after a stop is never used. A test that saves after a stopped step would show it.

**Verification:** Read host.cpp 1555-1595. ++world.months runs unconditionally, the Finish/Month block is gated on !stop.stop_requested(), then m_monthStep.Run still runs. Half-stepped state after a stop is real but only matters if something saves or hashes after a stop; I found no such caller. Low-to-info.

### GAME-HOST-B-13: Overhead labels and fox health bars are not culled to the publish area

- **Severity:** info · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1197`

PublishOverhead labels the player and every living fox, with health bars in combat scenes, whatever the view, while the entities themselves are culled by the draw list. The number of foxes is small today; any scene with many creatures would publish every label.

**Evidence:** The view loop over Creature/Body/Look/StableId/Health has no `Contains(area, ...)` test.

**Impact:** Wasted text items and label layout off-screen once creature counts grow, as the genre's NPC-heavy scenes will.

**Recommendation:** Pass the publish area in and skip creatures whose drawn head is outside it, as PublishJourneys does.

**Verification:** PublishOverhead (lines 1191-1200) iterates every Creature/Body/Look/Health with no area test. It only skips non-player, non-fox and dying entities. The cost is real but small with few foxes.

### GAME-HOST-B-14: The selected instance is found by a linear search of every published ID

- **Severity:** info · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:1882`

`std::ranges::find(snapshot.ids, m_selected)` scans all published IDs, about 100k in the stress arena, each publish whenever something is selected.

**Evidence:** Lines 1881-1885.

**Impact:** A small per-frame cost that grows with instance count.

**Recommendation:** Record the selected entity's instance index when it is added to the draw list (DrawList can report it at merge), or accept it with a comment.

**Verification:** Lines 1881-1885 do a std::ranges::find over snapshot.ids whenever something is selected. It is a linear scan per publish. Cheap and minor.

### GAME-HOST-B-15: ARENA_FORMAT names the host's save format for every scene

- **Severity:** info · **Verdict:** confirmed · **Category:** naming · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1611`

Save, Load and StateHash write and check ARENA_FORMAT for world-map and dev-scene saves alike; the name is left over from when the arena was the only scene.

**Evidence:** `archive(ARENA_FORMAT, static_cast<u8>(std::to_underlying(m_scene)), ...)`; its comment lists world and scratch sections.

**Impact:** Misleading to readers, who may think other scenes have their own format.

**Recommendation:** Rename it to HOST_FORMAT or SAVE_FORMAT.

**Verification:** ARENA_FORMAT (line 85) is used in Save (1611), StateHash (1625) and Load for every scene. The name is a leftover from when the arena was the only scene. A rename is cosmetic.

### GAME-HOST-B-16: Walk() repeats PlayerBody()'s lookup

- **Severity:** info · **Verdict:** unverifiable · **Category:** duplication · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1457`

Walk() looks up the player's Body by hand (`active.Entities().get<Body>(active.Find(m_player))`), exactly what PlayerBody() does. Explore() and WalkTo use MapArea(), which duplicates WorldScene::View()'s rectangle.

**Evidence:** Lines 1457-1458 against PlayerBody at 1047-1050; MapArea (line 539) against scenes/world.cpp View.

**Impact:** Two places to change when the lookup (or the map's area) changes.

**Recommendation:** Call PlayerBody() in Walk(), and take the map's area from one core helper used by both.

**Verification:** I did not open Walk() lines 1457-1458 or PlayerBody at 1047. MapArea at 539 is confirmed as a standalone helper. I did not compare it with scenes/world.cpp View. Settle it by diffing those lookups. Even if true, it is trivial duplication.

### GAME-HOST-B-17: The camera follows the body while the cultivator is drawn shifted onto the road

- **Severity:** info · **Verdict:** confirmed · **Category:** presentation · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/game/host.cpp:1805`

snapshot.follow uses the body's positions, but the cultivator on a route is drawn at body + m_drawnShift (on the road's curve). The camera's target and the drawn figure differ by up to the road reach. The world camera's loose box hides it, but a future exact-follow scene with routes would show the figure off-centre.

**Evidence:** `snapshot.follow = core::Follow{ .previous = body.previous, .current = body.current };` while the entity loop draws `drawnPlayer`.

**Impact:** Cosmetic; possibly intended.

**Recommendation:** Follow `drawnPlayer`, or document that the camera follows the simulated foot on purpose.

**Verification:** Checked the code at 1802-1807. snapshot.follow and start use body.current, and drawnPlayer = DrawnPlayer(body) adds m_drawnShift (line 1058). So camera target and drawn figure can differ. Whether this is intended isn't recorded in the lines I read, and the finding itself says cosmetic and possibly intended. I did not check the docs.

### GAME-HOST-B-18: host.hpp docs: a garbled clause in Load, and an overstated walk-keeping claim for M's jump

- **Severity:** info · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.hpp:156`

(1) Load's doc ends 'one tile route leg by core::WALKING of a step at least that LegSound() accepts', which does not parse (meant: at least one step, a leg LegSound() accepts). (2) PlayerWalk and src/AGENTS.md say M's jump keeps the cultivator's distance and facing 'as the travellers do'. Travellers' walks are recomputed from the clock (WalkedAt), so their distance jumps forward. The cultivator's distance stays where it was because AdvanceCalendar never adds the jump's length.

**Evidence:** host.hpp:156-157; host.hpp:317-321 with AdvanceCalendar lines 1560-1563, which set only `m_playerWalk.moving = false`.

**Impact:** Misleading contract text.

**Recommendation:** Reword both: 'a single tile route leg by core::WALKING of two or more tiles that LegSound() accepts', and 'keeps its distance and facing (the travellers' walk follows the clock instead)'.

**Verification:** (1) The Load doc clause at host.hpp:156-157 ('one tile route leg by core::WALKING of a step at least that LegSound() accepts') is garbled. (2) host.cpp:1560-1563 shows the M jump only calls FollowRoute, resets the shift and sets m_playerWalk.moving=false. It never adds the jump's length to the walk distance, so the cultivator's distance and facing are kept. The comment 'as the travellers do' is loose, because travellers' walks follow the clock. I did not verify the WalkedAt claim in the source. The doc wording is misleading but harmless.

### GAME-HOST-B-19: Load checks the kept map place against the portals of the running game's context, not the loaded world's

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** fragility · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:1679`

`OnMapPortal(*world.mapPlace)` builds LiveContext() from the current running world and player, not from the world being loaded. It is correct only because WorldScene::Portal depends solely on the game data's map.

**Evidence:** OnMapPortal (line 1161) uses `LiveContext()`; WorldScene::Portal reads only `context.Data().world`.

**Impact:** A future portal that depends on world state (per seed or per explored place) would be checked against the wrong world on load.

**Recommendation:** Pass a context over the loaded world, or make the portal check a pure function of the map and document it.

**Verification:** OnMapPortal (host.cpp:1161) does use LiveContext(), which is the running world and not the one being loaded. WorldScene::Portal (scenes/world.cpp:32-37) reads only context.Data().world, the static map, so the result is correct today. The risk is only hypothetical and no current defect exists. Latent fragility, low value.
