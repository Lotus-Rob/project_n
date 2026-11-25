# Review: gap1-sweep-up-backlog-vs-code

**Scope:** Read /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md, game-design-gaps.md and in-flight.md, and verify each backlog item against the code on main. Classify each one as already fixed (cite the code), still open as described, open but described wrongly (stale file names, line numbers or chunk names), or superseded by a later decision. Then cross-reference with this review's findings (docs/reviews/2026-11-25-full-codebase-review/*.md): list backlog items that the review re-found (so they can be merged), backlog items that no reviewer found (they may show a review blind spot, or the item may have been fixed), and serious review findings that are missing from the backlog. Read-only: report only, never edit the memory files. (Why this unit exists: The backlog is a 57 KB list of known defects kept outside the repo. gd-roadmap-scope judged it only as a process risk. Nobody checked its items against the current code, so this review can't tell which of its findings are already known or which backlog entries have rotted.)

**Findings:** 9 standing (0 critical, 0 high, 2 medium, 5 low, 2 info), 0 raised but refuted on verification.

## Assessment

## Backlog vs code on main (f1840be8), checked 2026-11-25

I read sweep-up-backlog.md (about 109 lines, 57 KB), game-design-gaps.md and in-flight.md, and checked each item that names code against main. I also searched all 123 review files for each item. The backlog is still mostly accurate, but it has rotted in four places. It reads as live in places where the work is done, and it has no per-item status or owner.

### Classification of the code items

**Already fixed, or superseded:**
- **Audit 6 (`walk_speed` unranged):** fixed. `src/data/game_data.cpp:60` now refuses a walk speed that is neither 0 nor at least `core::MIN_WALK_SPEED`.
- **Audit 1, seals part:** `SealRenderer::Submitted` (`seals.cpp:337`) now holds the pack until submit. The backlog does not say this, and the quad indices are still unguarded (see Audit 1 below).
- **"Warnings branch" item and priority list:** effc++ is gone (`xmake.lua:164-166` has `-Wno-effc++`). The unity build is gone too: no `unity` setting in `xmake.lua`, and `tools/test_project_n.py:514` asserts `check` runs no unity step. The controller gates for linux-gcc and windows-msvc release builds are in the root AGENTS.md. The whole item, including its unity sub-plan (a lint check for duplicated file-local names, merging the data readers' `Refuse`/`Fail` helpers), is moot or done.
- **Split `tests/unit/game/arena_tests.cpp`:** the file no longer exists. It was removed in 5665ae74, "refactor(game): name the arena class...". The CI-time rationale (a 104-187 s process) is stale. Whether the slow Windows job was fixed is not checked.
- **Priority-order line 13:** every numbered item up to 5b3 is marked merged. The line gives no status for 5b-look, 5c2 or shared-routes, so it is half-stale.
- **Item "NEXT DESIGN" (per-destination walk tables) and "Journeys share their routes":** these are now the `shared-routes` branch (in-flight.md). The branch has a spec, a plan and tasks 1-3 done, at head 3322ac9f in `.claude/worktrees/shared-routes`. `main..shared-routes` is 17 commits, and in-flight.md says "23 commits behind" main. The backlog still describes them as "next design". They should be merged into one item or removed once the stage-1 spec Outcome is written.
- **`LegWalkCache`:** it still exists (`src/core/leg_walk_cache.hpp`) and shared-routes will replace it.

**Still open as described (code verified):**
- **Audit 1 (quad-index uploads):** `m_indicesUploaded = true` is set at record time in `meshes.cpp:159`, `ribbons.cpp:116` and `seals.cpp:275`. `Impl::Submitted` (`renderer.cpp:1129-1136`) forwards to uploaded, seals, fog, ground, rivers and roads, but not to meshes or ribbons. Seals forwards only the pack, not the indices.
- **Audit 2 (replay `ReadTicks`):** the check is now `ticks > Remaining()` with a bounded reserve (`replay.cpp:85-89`). This is only partly fixed: it still reserves up to Remaining() × 16 bytes, about 16 GiB for a 1 GiB replay, where each tick needs at least 2 bytes. The review files already say this (CORE-PERSIST-18).
- **Audit 3 (sanitizer message):** `tools/project_n.py:2230` still prints "with every supported sanitizer".
- **Audit 4 (casts aim at the cursor):** `App::Cast` (`app.cpp:619-620`) uses `m_window.Cursor()` for non-mouse devices.
- **Audit 5 (held pan ignores focus):** `camera_control.cpp:196` uses `input.pan` ungated, and `app.cpp:770` feeds it from `m_bindings.Pan()`.
- **Audit 7 (one transfer buffer and submit per sprite):** still in `sprite_array.cpp` (create, submit).
- **Audit 9 (`reference` orphan PNGs):** the orphan check runs only under `if update:` (`project_n.py:963-968`).
- **Audit 11 (journal day-clamp test):** `MOST_DAYS_AWAY` (`game/travel.cpp:19`) is not referenced in tests, and no review file covers it.
- **Audit 12 (paused frames re-lay seals):** `LaySeals(snapshot, renderTick, ...)` is still called each frame (`renderer.cpp:480`).
- **Audit 13 (journey arrive ticks):** still only a `PN_ASSERT` (`journey.cpp:302`).
- **Audit 15 (sight radius on reload):** `ApplyData` refreshes health but not `sightRadius`.
- **Unrefused saves:** `Host::Load` refuses a malformed relationship table or an unfit world with no log. `host.cpp:1678` and `:1688` run `worldsFit` with no message, and the other refusals log at `:321-387`.
- **`NoteUnknownKeys`:** it still adds one warning per occurrence (`yaml.cpp:232-251`).
- **Seal sprites against the scene list:** `src/data/seals.cpp` checks sprites against the manifest only (`SpriteAt`).
- **No decode timeout:** none in `renderer.cpp` scene loading, though `GenerateMips` is still a whole-array call.
- **Cast dropped during a dash:** `combat.cpp:210-212` ignores it with a debug log.
- **Other open items:** the `core/journey.hpp` include of `calendar.hpp`, `RendererFor`/`TestManifest` re-parsing, `renderer.cpp` (1751 lines), `world_look.cpp` (978 lines, in `src/data`, not `src/gfx` as the backlog implies), the hard-coded `HitKind`/`AbilityKind` (`HIT_KINDS = 4`, `ABILITY_KINDS = 5`), the `libcapstone` `capstone_dev` channel (`pixi.toml:120-132`), and `MAX_VOLLEY_COUNT = 2000`.

**Open but described wrongly or loosely:**
- **Audit 1:** the backlog names `Renderer::Impl::Submitted ~1096` and seals as unguarded. It is now ~1129, and the seals pack is guarded while the seals indices are not.
- **Audit 2:** the backlog says "reserves per claimed tick before reading". The reserve is now bounded by Remaining() rather than by the claim, but is still too loose (about 16 B per byte).
- **Line numbers in the audit items:** most have drifted (`app.cpp ~601` is now ~619, `camera_control.cpp ~167` is now ~196, `host.cpp ~1502` is now ~1728). The code is the same, only the offsets are wrong.
- **Backlog item on the delta's head floats (river 10):** the river-heads branch (PR #54) has just merged and wrote its own Outcome. SPECS-BUILT-B-12 calls it a recorded backlog item. It should be re-read against that Outcome.
- **Item on `measure` counting SubmitFrame:** I could not find `SubmitFrame` in `tools/project_n.py`, so the item's location is stale or unverified.
- **0002.4 stale chunk numbers (a docs item in the review history):** still stale. 0002.4 §7 still calls the ui module chunk 4.

### Cross-reference with the review

**Backlog items the review re-found, to be merged:**
- Audit 1 (GFX-FEATURES-01, marked known-deferred).
- Audit 2 (CORE-PERSIST-18).
- Audit 4 and the gamepad aim item (APP-INPUT-UI-01, GD-COMBAT-SYSTEMS-08, X-ACCESSIBILITY-02, all known-deferred).
- Audit 5 (two files).
- Audit 7 (about four files).
- Audit 9 (TOOLS-A-14).
- Audit 13 (about 11 files mention the u32 assert).
- Audit 15 (sight radius, about six files).
- `NoteUnknownKeys` (seven files).
- The seal-sprites-against-scene-list item (GAME-SCENES-03).
- The `journey.hpp` include (CORE-JOURNEY-VOLLEY-15).
- `TestManifest` re-parsing (TESTS-GAME-05).
- The renderer.cpp split.
- The data-driven sweep and the closed ability enum (GD-COMBAT-SYSTEMS-02, GD-TOI-COVERAGE-06 and -08).
- Distance maps per (city, profile) (GD-WORLD-SIM-04, X-ARCHITECTURE-03, X-PERFORMANCE-01, CORE-WORLD-01).
- Localisation (nine files).
- The D3D12/Metal unverified item (GD-ROADMAP-SCOPE-05).
- The game-design brainstorm and the missing core loop (GD-ROADMAP-SCOPE-01 and -02, plus the whole gd-* group). The game-design-gaps.md list is covered in depth by the gd-toi-coverage, gd-combat-systems, gd-world-sim and gd-roadmap-scope reviews.

**Backlog items no reviewer found (a blind spot, or fixed or ignored):**
- Audit 3 (the sanitizer wording).
- Audit 11 (the untested day clamp).
- Audit 12 (paused frames still animate). The reviews touch seal layout only as culling (X-PERFORMANCE-18). The pause-freezes-animation rule in memory is not checked against code anywhere.
- The "0-byte binaries after a stopped build" item.
- Audit 6 (a stale-item check only; it is fixed).
- The item on a cast dropped during a dash (partly: GD-COMBAT-SYSTEMS-07 on the input buffer covers the same ground).
- "Travel follows relationships", "Danger shapes routes", "Mounts", "Skeletal animation" and "Debug tuning UI". These are planned features, so a review need not find them.
- The MSVC reproducibility item (69-byte difference), apart from a mention in five files that was not checked.
- The remaining items are hardware- or user-gated and not reviewable: macOS lock entries, code signing, the Windows items, the upstream reports, the context-budget re-measurement, and "global `push.followTags`".

**Serious review findings missing from the backlog:**
- **Resilience and crash handling:**
  - APP-MAIN-01, X-RESILIENCE-01 to -06, DESIGN-DOCS-06: no crash handler and lost log lines; a logic-thread or job exception exits silently or `std::terminate`s; GPU device loss is never fatal; saves are not durable (no fsync, no backup) and failures are log-only.
  - GD-ROADMAP-SCOPE-08: no autosave anywhere in the plans.
- **Delta saves and determinism:** DESIGN-DOCS-04, GD-WORLD-SIM-02 and X-DETERMINISM-01. Saves trust a hand-bumped `GENERATOR_VERSION` and a regenerated base that nothing verifies. A generator change, compiler change or platform change could corrupt a load without an error.
- **Scaling of procedural maps:**
  - Fixed 8,192-quad river and road buffers drop whole roads on large maps (GFX-WORLD-DRAW-01, GFX-RENDERER-01, X-PERFORMANCE-02).
  - Whole-map fog work every publish (X-PERFORMANCE-03, X-ARCHITECTURE-04).
  - A whole-map A* per click (CORE-WORLD-02).
  - `PublishJourneys` walks every journey (X-PERFORMANCE-04).
  - Procedural world generation has no spec (GD-ROADMAP-SCOPE-04, GD-WORLD-SIM-01, SPECS-ROADMAP-01).
- **Windows correctness:** `path::string()` can throw, and the SDL_ttf font path is not UTF-8 (X-CPP-SAFETY-01 and -02, X-TEXT-L10N-02, X-UNTRUSTED-INPUT-01 and -02), so the game fails from a non-ASCII install folder and saves can terminate. No HiDPI support (PLATFORM-01).
- **Real bugs:**
  - GAME-COMBAT-01 (lance on the portal tick trips an assert in the replay hash).
  - GAME-HOST-A-01 (held movement walks off the map).
  - GD-TOI-COVERAGE-07 (healing abilities damage enemies, burn/poison key ignored).
  - DATA-YAML-CORE-01 (`SaveSettings` never writes `edge_pan`).
  - X-ACCESSIBILITY-01 (stuck pan or move after a gamepad is removed).
- **Game-design blockers:** unlimited power growth turns mortals into cultivators within about a year (GD-WORLD-SIM-03, GD-TOI-COVERAGE-04). Combat has no free projectiles, so enemies cannot attack (GD-TOI-COVERAGE-03). There is no input buffer and no hit radius (GD-COMBAT-SYSTEMS-05 and -07).
- **Doc contradictions:** the AGENTS.md size rule is broken (src/AGENTS.md is 45 KB with a 15,404-character line; DESIGN-DOCS-02, X-ARCHITECTURE-08, GD-ROADMAP-SCOPE-07). Design doc 0002 describes arenas and a command-merge rule that do not exist (DESIGN-DOCS-03).
- The backlog does not mention the Windows non-ASCII path failures, the crash-handler gap or save durability at all.

### Overall health of the backlog as a process
- It is a real tool and mostly accurate (about 70% of the code-referencing items still hold). It has no status column, so a reader cannot tell open from done from stale without reading the code.
- Several items are written as designs, such as the data-driven sweep, localisation, the GPU projectiles item and the 3,000 sprites item. They should be specs in `docs/`, not memory entries.
- The backlog tracks features, performance and polish well. It misses resilience, durability, Windows-path, HiDPI and untrusted-input items, which suggests the audit-based approach finds some classes of defect and not others.
- The two items it marks "being fixed" (`shared-routes`) have already moved into an in-flight branch, so the backlog and in-flight.md now overlap.
- A pruning pass is overdue. The user's schedule says to walk it "after phase 6", but the stale entries (warnings branch, unity, `arena_tests`) are already misleading.

**Files read in full:** /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md, /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/game-design-gaps.md, /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/in-flight.md, /tmp/claude-1000/-home-lotus-Development-project-n/5e986e63-9457-419a-a1f3-2b39adbcdc44/scratchpad/review-history.md

## Findings

### GAP1-SWEEP-UP-BACKLOG-VS-CODE-01: Serious review classes absent from the backlog: crash and exception handling, save durability, Windows non-ASCII paths

- **Severity:** medium · **Verdict:** unverifiable · **Category:** process · **Status:** new · **Reviewer confidence:** medium
- **Where:** `/home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md`

The backlog has nothing on: no crash handler, lost async log lines, exceptions on job workers or the logic thread ending the game silently (APP-MAIN-01, X-RESILIENCE-01 to -04, DESIGN-DOCS-06); non-durable saves and log-only save failures (X-RESILIENCE-05 and -06, DESIGN-DOCS-05); path::string() throwing and non-UTF-8 font paths on Windows (X-CPP-SAFETY-01 and -02, X-UNTRUSTED-INPUT-01 and -02); no HiDPI (PLATFORM-01). Delta saves trusting an unverified base (DESIGN-DOCS-04, X-DETERMINISM-01) is also unrecorded.

**Impact:** These can lose a player's data or stop the game from starting on Windows. The backlog is the user's only deferred list, so they have no owner or priority.

**Recommendation:** Add them to the backlog or the spec for the next phase, ahead of polish items. The Windows path failures and the silent exits are the cheapest to fix.

**Verification:** The claim is about backlog contents and review finding IDs. I did not search the backlog or the review files for crash handling, save durability or Windows paths.

### GAP1-SWEEP-UP-BACKLOG-VS-CODE-02: Procedural-map scaling findings are not in the backlog, which only lists distance maps

- **Severity:** medium · **Verdict:** unverifiable · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `/home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md`

Review findings on fixed 8,192-quad river and road buffers (GFX-WORLD-DRAW-01, X-PERFORMANCE-02), whole-map fog work (X-PERFORMANCE-03, X-ARCHITECTURE-04), whole-map A* per click (CORE-WORLD-02), PublishJourneys over every journey (X-PERFORMANCE-04) and the missing generator spec (GD-ROADMAP-SCOPE-04, SPECS-ROADMAP-01) have no backlog entry. The 'map is procedural' decision makes all of them real.

**Impact:** The shared-routes plan closes only part of the performance gap, and these items will surface when a generator exists.

**Recommendation:** Record them, with a scaling target in tiles and places, as spikes per the unknowns-are-measured rule.

**Verification:** I did not check the backlog for scaling items or the review files for the cited IDs. The reasoning (procedural maps make these real) matches the recorded decision in MEMORY.md, but coverage is untested.

### GAP1-SWEEP-UP-BACKLOG-VS-CODE-03: Backlog contains items already fixed or superseded (warnings branch, unity, arena_tests split, walk_speed range)

- **Severity:** low · **Verdict:** confirmed · **Category:** process · **Status:** new · **Reviewer confidence:** high
- **Where:** `/home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md`

Several entries describe work that is done or no longer meaningful. The 'warnings branch' item (drop -Weffc++, unity-build measurement and its lint sub-plan) is complete: xmake.lua:166 has -Wno-effc++, no unity setting remains in xmake.lua, and tools/test_project_n.py:514 asserts check runs no unity step. The 'split arena_tests.cpp' item refers to a file removed in 5665ae74 'refactor(game): name the arena class...'. Audit item 6 (creatures walk_speed unranged) is fixed at src/data/game_data.cpp:60. The priority-order line stops at 5b3 and gives no status for 5b-look, 5c2 or shared-routes.

**Impact:** A sweep-up walk, or an agent reading the backlog as the known-issue list, wastes time on dead items and under-trusts the rest. Review consumers are told these defects exist when they do not.

**Recommendation:** Prune these entries and add an open/fixed/stale marker and a last-verified date to each remaining item.

**Verification:** Checked: xmake.lua:166 has -Wno-effc++, with no unity setting. tools/test_project_n.py:514 asserts check runs no unity step. src/data/game_data.cpp:60 now range-checks walk_speed, so backlog line 94 is stale. tests/unit/game has no arena_tests.cpp. The effc++ NOLINT pairs are gone from src. Backlog lines 15, 35 and 94 are still there as the finding says. I did not verify the removal commit 5665ae74 or the priority-order line.

### GAP1-SWEEP-UP-BACKLOG-VS-CODE-04: Backlog's 'next design' items (shared walk tables, journeys share routes) are now the shared-routes branch

- **Severity:** low · **Verdict:** unverifiable · **Category:** process · **Status:** new · **Reviewer confidence:** high
- **Where:** `/home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md`

Backlog lines 107-108 present per-destination walk tables and shared journey routes as a future design. in-flight.md records a shared-routes branch with an approved spec (2026-11-24-shared-routes-design.md), a plan, tasks 1-3 done, and a 17-commit branch (in-flight.md says '23 behind main'). Line 77's month-step item also overlaps. The data in the backlog (LegWalkCache, ~2,300-2,700 tables a month) matches code (src/core/leg_walk_cache.hpp), so the content is right but the status is wrong.

**Impact:** Two sources of truth: the backlog says not started, in-flight.md says in progress. The shared-routes Outcome section has to carry the rulings, and the backlog entries will go stale when the branch merges.

**Recommendation:** Replace the backlog entries with a pointer to the shared-routes spec, and delete them when the branch merges (as in-flight.md's own rule does for its sections).

**Verification:** I did not re-read backlog lines 107-108 or in-flight.md for the shared-routes branch status. The overlap is plausible, but it is a process claim that reading the code cannot settle.

### GAP1-SWEEP-UP-BACKLOG-VS-CODE-05: Quad-index upload flags still set at record time (Audit 1), meshes and ribbons not forwarded by Submitted

- **Severity:** low · **Verdict:** confirmed · **Category:** correctness · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/gfx/internal/features/meshes.cpp:159`

MeshRenderer::Upload (meshes.cpp:159), RibbonRenderer::Upload (ribbons.cpp:116) and SealRenderer::Upload (seals.cpp:275) set m_indicesUploaded = true as soon as the copy is recorded. Renderer::Impl::Submitted (renderer.cpp:1129-1136) forwards to uploaded, seals (pack only), fog, ground, rivers and roads, not meshes or ribbons. Re-found by GFX-FEATURES-01.

**Impact:** A command buffer that is cancelled or fails to submit on the first recording frame leaves text, sword trails or seals drawing from never-filled index buffers until restart. Narrow, but untested.

**Recommendation:** As the backlog says: hold the flag in a recorded state and commit it in Submitted(bool), forwarded from Impl::Submitted. Test-first with a cancelled capture.

**Verification:** meshes.cpp:155-159 and ribbons.cpp:111-116 set m_indicesUploaded = true while recording. The Impl::Submitted function is at src/gfx/renderer.cpp:1129-1136, not in an internal/ directory, so the cited path is wrong. It forwards to uploaded, seals, fog, ground, rivers and roads, but not to meshes or ribbons. The claim holds. Impact is narrow.

### GAP1-SWEEP-UP-BACKLOG-VS-CODE-06: Replay ReadTicks reserve still loose (Audit 2 partly fixed; backlog text stale)

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** untrusted-input · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/core/replay.cpp:85`

The bound is now ticks > Remaining() with reserve of ticks (replay.cpp:85-89), where the backlog says the reserve was unbounded. A tick costs at least 2 bytes, so Remaining()/2 would be the tight bound, and each tick reserves a size_t plus a HeldInput.

**Impact:** A forged 1 GiB replay can still reserve many GiB and end in bad_alloc on the logic thread. The backlog text is out of date.

**Recommendation:** Fail when ticks > Remaining()/2 and drop or cap the reserves; update the backlog entry. Matches CORE-PERSIST-18.

**Verification:** replay.cpp:85-89 does fail when ticks > Remaining() and reserves ticks otherwise, so the unbounded-reserve backlog text is stale. The bound is loose by a factor of at least 2 bytes per tick, so a forged count can still over-reserve. I did not check the per-tick byte cost or the size of the reserve.

### GAP1-SWEEP-UP-BACKLOG-VS-CODE-07: Host::Load still refuses an unfit world with no log

- **Severity:** low · **Verdict:** confirmed · **Category:** error-handling · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1688`

Backlog 'saves refused without a reason' is still open. Consistent/WorldFits/relationships.Within at host.cpp:1678-1688 returns false with no message, while the malformed difference and order cases log (321-387).

**Impact:** A player whose save is refused sees nothing saying why.

**Recommendation:** Log each refusal, test-first.

**Verification:** The final refusal at host.cpp:1685-1688 does `return false` with no log. It covers archive.Failed (including a malformed relationship table), trailing bytes, !worldsFit (Consistent, WorldFits, relationships.Within, OnMapPortal), !playerFound, !routeFits and the combat checks. Logging exists only earlier, at 281-387, for generator, data, deltas and order. The backlog entry (sweep-up-backlog.md:86) describes the same gap. Its file reference is slightly stale: it names ReadGameWorld/ReadScratch, but the unlogged refusals sit in Load's tail at about 1678-1688. The finding's line 1678 is the worldsFit start, and the `return false` is at 1688. Impact is low: nothing is logged, though the player-facing symptom is only a refused save.

### GAP1-SWEEP-UP-BACKLOG-VS-CODE-08: Backlog audit items with no review finding: sanitizer wording, untested journal day clamp, paused-frame animation

- **Severity:** info · **Verdict:** confirmed · **Category:** process · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:2230`

Audit 3 (check --sanitizers prints 'with every supported sanitizer', project_n.py:2230), Audit 11 (travel.cpp MOST_DAYS_AWAY clamp not referenced in tests) and Audit 12 (renderer.cpp:480 LaySeals runs while paused, against the pause-freezes-animation rule) are still open in code and appear in none of the 123 review files. X-PERFORMANCE-18 covers seal culling only.

**Impact:** Review gap: the reviewers did not test code against the user's recorded pause rule, and a few small audit items were not re-found. Either the reviewers' scopes missed them or they judged them too small.

**Recommendation:** Keep these backlog items as the only record, and treat the pause rule as a review criterion for gfx and game scopes in future reviews.

**Verification:** project_n.py:2230 still prints 'with every supported sanitizer'. MOST_DAYS_AWAY at src/game/travel.cpp:19 and 36 has no test: travel_tests.cpp checks only small day counts. LaySeals is called at src/gfx/renderer.cpp:480, not :480 of an internal/ path. I did not check whether it runs while the game is paused. I did not grep the review files for these items, so the claim that no review found them is unchecked.

### GAP1-SWEEP-UP-BACKLOG-VS-CODE-09: Backlog line numbers and locations have drifted

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `/home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md`

Audit items cite app.cpp ~601 (now ~619), camera_control.cpp ~167 (now ~196), host.cpp ~1502 (now ~1728), renderer.cpp ~1096 (now ~1129), sprite_array.cpp ~162-206 and project_n.py ~963/~851. World_look.cpp is named without its directory and is in src/data. No code behind them changed except as listed elsewhere.

**Impact:** Cheap to follow by name, but misleading for someone using the line numbers as an index.

**Recommendation:** Cite symbols, not line numbers, when an item is written.

**Verification:** I checked only Submitted, which is now at renderer.cpp:1129 and in src/gfx/, not src/gfx/internal/. That is consistent with line drift. I did not check the other line numbers.
