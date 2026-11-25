# Review: gd-roadmap-scope

**Scope:** PROJECT assessment: read git log (shape and pace of history), docs/design, all spec titles and the phase-6 master spec, memory files in /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory (MEMORY.md, sweep-up-backlog.md, game-design-gaps.md, in-flight.md). Judge: is there a coherent path from here to a playable vertical slice and to a ToI-scale game; what is over-engineered relative to proven fun; what essentials have no plan at all (audio, UI framework, inventory, progression, content pipeline, real art, save slots/menus, options, localisation, packaging/distribution, crash handling, modding); ordering risks; process overhead of the agent workflow; top risks to the project.

**Findings:** 23 standing (0 critical, 0 high, 9 medium, 13 low, 1 info), 0 raised but refuted on verification.

**Parts:** gd-roadmap-scope.md, gd-roadmap-scope-part2.md, gd-roadmap-scope-part3.md

## Assessment

## Project assessment: roadmap and scope (gd-roadmap-scope)

### Shape and pace of the history
- 975 commits in 27 days (2026-10-29 to 2026-11-25), one author working with agents, linear history, one release tag (v0.1.0 on day 2).
- Phases 1-5 of 0002 §16 (skeleton, SDL_GPU, instancing and jobs, simulation/saves/replays, input/data/scenes) took about 10 days. That is impressive, solid engine work.
- Phase 6 (from 2026-11-09) has taken 16 days so far. Chunk 5 was planned as one chunk ("the world map"). It has grown into about 12 branches: scenes, 5a, 5b1, 5b2, 5c1, 5b3, terrain, player walk, roads, travellers, river heads, and now shared-routes, which has two stages. It still isn't done: 5c2 is open. Chunks 6-11 and phase 7 haven't started.
- The mix of commits has moved away from features:
  - Days 1-10: 52 feat and 41 docs commits.
  - 11-09 to 11-16: 92 feat and 121 docs.
  - Since 11-17: 38 feat, 91 docs, 62 test and 51 fix.
  - Every feat since 11-17 is world-map presentation or performance: fog, painted terrain, roads, rivers, walk frames, river heads. None of it is gameplay.
- Overall, docs are 26% of commits. Specs and plans add up to about 21k lines, against 42.7k lines of src and 42k of tests.

### Strengths
- The engine core is unusually rigorous for a game this early:
  - deterministic fixed-tick simulation with replays and state hashes
  - an adaptive display delay
  - delta saves
  - fork-join jobs with tests comparing 1 and N workers
  - reference pictures on lavapipe
  - measured performance budgets
  - fuzzing and sanitizers
  - four presets in CI
- Tests are about as large as the source.
- Decisions are recorded with their reasons, and memory tracks known gaps honestly. game-design-gaps.md is a frank, complete list of what a Tale of Immortal-like game needs.
- Accessibility, localisation and modding are stated as values early, which is rare.

### Weaknesses
- **No game yet.** The player can walk the world map, and in the arena can cast at foxes that orbit and never attack. There is:
  - no fail state, progression, items or cultivation
  - no enemy AI
  - no UI module, menus or audio
- Phase 6's own plan keeps it that way. Chunk 7 lists "enemies attacking, combat AI, player death" as non-goals.
- The game-design brainstorm has been pushed back twice, each time behind world-map polish. The approved chunk 6, 7 and 10 specs (written 2026-11-10) predate both that brainstorm and the 2026-11-25 decision to make the map procedural.
- **Effort goes into scale and polish before fun is proven.** Examples:
  - Hierarchical routing for "very large procedural maps", which nothing generates. The only generator is an offline Python script whose output is then edited by hand.
  - A whole spec, plan and review cycle for where rivers start on a test map.
  - A nine-ring seal system before any combat loop exists.
- **Essentials with no plan at all:** runtime procedural world generation, autosave, crash handling, a vertical-slice milestone, and licensing for store integration. The game-design brainstorm is meant to cover the rest:
  - planned only as a single later phase: audio (phase 7)
  - planned late: UI (chunk 8), options (chunk 11)
  - on the backlog, unscheduled: localisation, modding
  - with no plan: inventory, progression, an art pipeline beyond placeholders
- **Ordering risks.** The riskiest foundations are still unproven:
  - RmlUi (spike S13)
  - DragonBones (spike S14)
  - D3D12 and Metal at runtime (spike S9)
  - a GPU at the minimum spec

  Meanwhile, work built on assumptions (stand-in prompts and popups, hand-made map places) piles up.
- **Process overhead is heavy and growing.** Each task gets:
  - an implementer and an Opus review
  - one fix pass and a re-review
  - test and lint, then two release builds

  Each branch then adds a final review, a review by four external models, `check`, nine CI checks and an Outcome section. Every Minor finding must be fixed.
  - The agent docs that every agent loads are large: src/AGENTS.md alone is 45 KB, with one 15,404-character "line".
  - The 57 KB backlog lives outside the repo.

### Overall health and top risks
Engineering health is high; project health is at risk. In order:
1. Fun is never validated: the core loop and combat challenge come after months of infrastructure, scale and presentation work.
2. Approved specs and the routing work rest on assumptions the game-design brainstorm or the procedural-map decision may overturn, so there will be rework.
3. Windows (the genre's main market) and minimum-spec GPU behaviour have never been seen at runtime.
4. Delta saves need a byte-identical regenerated base across patches and platforms. That is stronger than the recorded "deterministic within one binary" goal.
5. Process cost per feature is high enough to make reaching a Tale of Immortal-scale game unlikely at the current ratio of ceremony to content.

**Recommendation:**
1. Run the game-design brainstorm now: pause shared-routes after stage 1.
2. Define a vertical slice: about 30 minutes of the loop, with combat that fights back, one progression axis, a minimal UI, and save/continue.
3. Re-brainstorm chunks 6, 7 and 10 against it.
4. Run spikes S13, S9 and S14 and a minimum-spec GPU check early.
5. Give presentation and test-map work a lighter process path.

**Files read in full:** /tmp/claude-1000/-home-lotus-Development-project-n/5e986e63-9457-419a-a1f3-2b39adbcdc44/scratchpad/review-history.md, /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/MEMORY.md, /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/in-flight.md, /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/game-design-gaps.md, /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md, /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/accessibility-first.md, /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/toi-inspiration-not-template.md, /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/plan-before-building.md, /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/procedural-world-map.md, /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/unknowns-are-measured-spikes.md, /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/handover-after-each-task.md, /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sdd-rereview-models.md, /home/lotus/Development/project_n/docs/superpowers/specs/2026-11-10-phase6-design.md, /home/lotus/Development/project_n/docs/superpowers/specs/2026-11-10-phase6-chunk5-world-map-design.md, /home/lotus/Development/project_n/docs/design/0002-engine-architecture.md, /home/lotus/Development/project_n/AGENTS.md, git log (all 975 commits: types, scopes, dates, churn), docs/design/* status headers; docs/superpowers/specs/* titles and status lines (all 37), docs/superpowers/specs/2026-11-10-phase6-chunk6-towns-design.md (first 60 lines), docs/superpowers/specs/2026-11-10-phase6-chunk7-combat-scenes-design.md (summary, goals, non-goals), docs/superpowers/specs/2026-11-10-phase6-chunk10-screens-design.md (first 40 lines), docs/superpowers/specs/2026-11-10-phase6-chunk8-ui-design.md (first 40 lines), .claude/worktrees/shared-routes/docs/superpowers/specs/2026-11-24-shared-routes-design.md (first 60 lines), art/world_map.py (header), src/game/scenes/arena.hpp (header doc), src/game/npcs.hpp (Action enum, MonthStep)

## Findings

### GD-ROADMAP-SCOPE-01: Game-design brainstorm keeps slipping behind world-map polish and scale work

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** roadmap-ordering · **Status:** contradicts-recorded-decision · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-design.md:111`

The GD row (design pillars, core loop, systems map, game roadmap) was added on 2026-11-16 to run right after 5b2, then moved to 'after all of chunk 5'. Since then, branches that were never in the chunk table went in ahead of it: 5b3 fog, 5b-look terrain, player walk and camera, roads, travellers, river heads, and now shared-routes (stage 1 has 10 tasks and 8 spikes; stage 2 follows). 5c2 is still open, so the gate isn't met and keeps moving.

**Evidence:** Every feat/perf commit since 2026-11-17 is world-map presentation or performance (fog, painted paper, ribbons for rivers and roads, walk frames, river springs and ponds, walk tables). in-flight.md: shared-routes wave B next, then C-F and spike rounds, and stage 2 'after stage 1's measurements'. game-design-gaps.md: 'first proposed for right after 5b2; the user moved it'.

**Impact:** Weeks of engineering go into a world whose purpose for the player is undefined. Routing for 'missions, hunting, gathering' is designed before anyone has decided whether those activities exist or how they work. Each slip makes rework more likely and keeps fun unproven.

**Recommendation:** Reopen the ordering. Run the GD brainstorm next: finish shared-routes stage 1 at its spike round, then park stage 2 until GD has said what travel serves. Treat 5c2 as no longer a precondition.

**Verification:** The phase 6 table (phase6-design.md) lists 5b3 and 5b-look inside chunk 5, so 'never in the chunk table' is wrong for those. game-design-gaps.md shows the user chose and moved the GD slot themselves ('do it before chunk 6 but after we finish chunk 5'), so it is a recorded decision, not a drift. What holds: roads, travellers, river heads and shared-routes were added past that list, so the gate keeps moving. Recommending a reorder is a legitimate scope-creep point, but the user owns it.

### GD-ROADMAP-SCOPE-02: No vertical slice defined; the first playable loop sits after phase 7

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** roadmap · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002-engine-architecture.md:561`

0002 §16 ends at phase 7 (animation, particles, audio). Phase 6's measure is 'a large complete demo/test scene', not a playable loop. The GD brainstorm is to write 'a rough game roadmap after phase 7'. No document names a vertical slice: a short, complete, fun loop that proves the game (start, explore, fight something that fights back, gain something, save and continue).

**Evidence:** Phase 6 spec Intent: 'a large, well built out, complete demo/test scene containing examples of all engine and game features'. Chunk 7 non-goals: 'Enemies attacking, combat AI, player death and defeat'; 'XP, loot, items... levels, realms, cultivation, breakthroughs (the user: all future gameplay)'. The arena's foxes orbit and never attack (arena.hpp header).

**Impact:** At chunk 5's pace (about 11 days and 12 branches), the remaining chunks 5c2 and 6-11, then phase 7, mean months before anyone can tell whether the game is fun. Engine decisions (scale, routing, look) get locked in without gameplay evidence.

**Recommendation:** Make 'vertical slice' a named milestone in the GD output, defined by player experience, not features: 15-30 minutes of loop with combat that has a threat, one progression axis (a placeholder cultivation realm), a minimal real UI, and save/continue. Order the remaining phase 6 chunks by what the slice needs, and defer scale and polish chunks until after it.

**Verification:** No document names a vertical slice. grep for 'vertical slice' in docs/design and the phase 6 spec finds only vertical text. The phase 6 GD row asks for a core loop and a game roadmap after phase 7, so the slice is plausibly meant to come out of GD. The phase 6 goal is a demo scene, not a playable loop, as stated. The pace and months-to-fun estimate is opinion.

### GD-ROADMAP-SCOPE-03: Approved chunk 6, 7 and 10 specs predate the game design and the procedural-map decision

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** spec-staleness · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk10-screens-design.md:36`

Chunks 6 (towns), 7 (combat scenes and dungeons), 8 and 10 (screens) were approved on 2026-11-10, before the GD brainstorm existed and before the user ruled on 2026-11-25 that the map is procedural per seed. They hard-code assumptions that no longer hold: 'The map's places stay hand-made' (chunk 10); 'fixed landmark dungeons placed on the hand-made map (chunk 5), including the demo's two big dungeons' (chunk 7 Goal 1); 'Hand-made map for the demo' (chunk 5 Decision 9). They also fix gameplay structure (households, roles, sects as world records, orb drops) ahead of the systems map.

**Evidence:** chunk10 line 36: 'The map's places stay hand-made (their tiles and names, until procedural world generation)'. chunk5 spec line 161: '9. Hand-made map for the demo; procedural generation later.' Memory procedural-world-map.md (2026-11-25): 'world.yaml is only a test map'.

**Impact:** A future session following AGENTS.md ('each approved before its code') will plan straight from these 'approved' specs and build towns, dungeons and screens on outdated assumptions, then rework them after GD.

**Recommendation:** Change the status of chunks 6, 7, 9, 10 and 11 to 'approved 2026-11-10; to be re-brainstormed after GD and the procedural-map decision'. Have the GD brainstorm explicitly re-scope each one.

**Verification:** The chunk specs are 'approved 2026-11-10' with 'To do' status, and they predate GD and the 2026-11-25 procedural ruling. They are not unaware of it, though. Chunk 10 line 36 says 'stay hand-made ... until procedural world generation', and chunk 5 decision 9 says 'procedural generation later'. So the assumption is flagged as temporary. The risk that a future session plans straight from them is real but modest. A status note would be a cheap fix.

### GD-ROADMAP-SCOPE-04: Runtime procedural world generation has no spec or plan, yet routing is being built for it

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** missing-plan · **Status:** new · **Reviewer confidence:** high
- **Where:** `art/world_map.py:1`

The user decided on 2026-11-25 that maps are generated per seed, possibly 2-50x larger, and shared-routes is designed for that case. The only generator is an offline Python script (1,745 lines) with a hand-shaped continent, hand-placed city sites and hand-drawn range spines. Its output is then 'hand-tuned', and each river's source was 'placed by hand in world.yaml after generation'. Nothing in src generates a map. No spec covers porting it to deterministic C++, nor how roads, fords, rivers, places, danger sites, names and city draws would come out of a seed.

**Evidence:** art/world_map.py docstring: 'a hand-shaped continent... cities at the hand sites below'. art/AGENTS.md: 'the YAML is hand-tuned after and is the source from then on'; 'each river's source was placed by hand in world.yaml'. Chunk 5 spec non-goal: 'Procedural world generation (future...)'.

**Impact:** A large, risky feature that blocks the stated direction has no owner. Routing, river-head drawing and road layout are being tuned against a hand-edited map that will be replaced. The generator must also be deterministic across builds, because saves regenerate the base world.

**Recommendation:** Write a world-generation spec (or a GD systems-map entry) before shared-routes stage 2. Cover: runtime C++ generation per seed, integer/fixed-point noise for cross-build stability, the target size and population, generation time in the 'creating world' step, and which hand-tuned features (rivers, roads, sites) must be generated. Until then, stop investing in hand edits to world.yaml.

**Verification:** The procedural-world-map memory records the decision, and the generator is an offline Python script. I found no C++ generation spec in the chunk table. Whether a spec or plan exists elsewhere is not settled by what I read. The stage-2 shared-routes ordering concern is reasonable. 'High' overstates it, since GD is still ahead and the map is a test map.

### GD-ROADMAP-SCOPE-05: Windows D3D12 and macOS Metal never run; the genre's main platform is unverified

- **Severity:** medium (reviewer said high) · **Verdict:** deliberate · **Category:** platform-risk · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `docs/design/0002-engine-architecture.md:546`

Spike S9 (SDL_GPU on every target) and S10 (presentation) have only passed on Linux/Vulkan. CI's Windows jobs build and run tests, not the renderer on D3D12. The renderer has since grown a great deal (glows, seals, painted terrain shaders, ribbons, clouds, fog).

**Evidence:** 0002 Status: 'phase 2's spikes S9 and S10 wait for other machines'. The backlog lists 'Needs Windows: ... S9 on D3D12'.

**Impact:** Tale of Immortal-like games sell mostly on Windows. Backend-specific shader, binding or present-mode issues (DXIL resource layout, tier-2 binding, MAILBOX availability) are found late and cost more as the shader count grows.

**Recommendation:** Run the Windows build on real Windows hardware (a borrowed PC or a cloud GPU VM) soon, and repeat at each renderer-heavy branch. Record S9 and S10 results.

**Verification:** sweep-up-backlog.md lists 'Needs Windows: ... S9 on D3D12' and 0002's status says S9 and S10 are waiting for other machines. It is a known deferral, and the risk grows with each shader. The finding's own status says known-deferred.

### GD-ROADMAP-SCOPE-06: UI framework is unproven and late, so stand-in UIs pile up and accessibility waits

- **Severity:** medium · **Verdict:** partially-confirmed · **Category:** ordering-risk · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk8-ui-design.md:30`

RmlUi (spike S13: builds on four presets, CJK, our render interface, font engine choice) hasn't been tried, and chunk 8 comes after towns (6) and combat scenes (7). Meanwhile 5c2's enter prompt, chunk 7's popup, route map and results screen, and chunk 4's HUD are built as game-text stand-ins that chunk 10 is to replace. Settings, recolouring and the reduce-motion and flashing options (the 'top priority' accessibility work) are chunk 11, last.

**Evidence:** Chunk 7 non-goals: 'The real UI for the popup, the route map's large view and the results screen (chunk 10 replaces the stand-ins)'. Chunk 8 Goal 1: 'RmlUi, checked first (S13)'. Memory accessibility-first: 'Accessibility is always, always the top priority'.

**Impact:** Throwaway UI work in three chunks. If S13 fails (font engine, SDL_GPU backend gaps, stencil format), every screen plan changes late. Accessibility settings stay unbuilt through most of development.

**Recommendation:** Run S13 now as a standalone spike, and move chunk 8 (and the core of 11) ahead of 6 and 7, so later chunks build real windows rather than stand-ins.

**Verification:** Facts hold: S13 is a gate at the start of chunk 8, after chunks 6 and 7 (phase6 table lines 108-114), and chunk 10 explicitly lists autosave and real-UI replacement as later work. The ordering is a deliberate choice, since chunk 8 depends on the town and combat scenes it will display. The 'accessibility waits' impact is weaker: the memory says every option must work by mouse, keyboard and gamepad now. Running S13 early as a standalone spike is a sound suggestion. The risk of stand-in rework is real, but I could not measure it by reading.

### GD-ROADMAP-SCOPE-07: src/AGENTS.md is 45 KB with a 15,404-character 'rule' line

- **Severity:** medium · **Verdict:** confirmed · **Category:** agent-docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/AGENTS.md:27`

The root rule says an AGENTS.md is '~100 lines, one line per rule'. src/AGENTS.md has 49 lines but about 7,000 words: line 27 alone is a full narrative of Host's internals (worlds, scenes, saves, travel, walk tables, fog, the player route). tests/AGENTS.md (19 KB, a 5,806-character line) and tools/AGENTS.md (21 KB) follow the same pattern. Every agent working in src loads root + src (+ tests): about 80 KB, roughly 20k tokens, before reading any code.

**Evidence:** Measured with wc and awk: AGENTS.md 16,095 B; src/AGENTS.md 45,264 B, longest line 15,404 chars; tests/AGENTS.md 19,275 B; tools/AGENTS.md 21,137 B.

**Impact:** Context cost on every subagent, which the context-budget rule is trying to save. The text duplicates Doxygen in host.hpp and drifts from the code. Real rules get buried in narrative, and the docs become a merge-conflict hotspot.

**Recommendation:** Move design narrative into Doxygen on the classes or into design docs. Keep AGENTS.md to short, checkable rules with pointers. Add a lint limit on AGENTS.md line length and size.

**Verification:** src/AGENTS.md is 45,264 bytes (checked with wc), against the root rule of about 100 lines with one line per rule. The line-27 narrative claim is plausible, though I did not re-measure its 15k characters. The size contradicts the project's own stated rule and the context-budget goal. Not deliberate, since no record sanctions it. The impact is context cost, not correctness.

### GD-ROADMAP-SCOPE-08: No autosave anywhere in the plans; logic-thread failure exits silently

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** missing-plan · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/app.cpp:325`

Chunk 10 lists autosave as a non-goal, and 5c1 lists 'Save slots, autosave, a save menu' as non-goals. No later plan picks it up. When the logic thread throws, App::Run returns 1 with only a Critical log: no message box (ShowError is used only for start-up failures), no emergency save, no crash dump. There is no signal or minidump handler, and PN_ASSERT compiles out in release.

**Evidence:** app.cpp:301-308 sets m_logicFailed; 325-327 'if (m_logicFailed) { return 1; }'. main.cpp catch blocks only log. grep finds no minidump, crashpad, sigaction or set_terminate.

**Impact:** A long Tale of Immortal-style run can be lost to one crash, with the window vanishing and no explanation, and reports from testers would have no dump to debug.

**Recommendation:** Add autosave (on month steps and scene changes, using the existing worker save path) to the GD and slice scope. On logic failure: show ShowError naming the log file, and try an emergency save to a separate slot. Plan opt-in crash dumps (e.g. crashpad or sentry-native, both permissive) before external playtests.

**Verification:** Checked app.cpp:325-327: when m_logicFailed is set, Run returns 1. Autosave is a recorded non-goal in chunk 10 (line 82) and in 5c1 (line 165), and no plan picks it up. game-design-gaps.md line 55 does list opt-in crash reporting under Shipping, so a crash dump is a known gap and not a missing consideration. ShowError exists in platform/window.cpp, but I did not trace whether the logic-failure path calls it, and the finding says it doesn't. Autosave as 'no plan' holds, but it is deferred pre-1.0 scope, not an oversight. A gap that is listed but unscheduled is the accurate description.

### GD-ROADMAP-SCOPE-09: Time model chosen before the game design: continuous days vs time passing per action

- **Severity:** medium · **Verdict:** confirmed · **Category:** game-design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk5-world-map-design.md:49`

0002 A6 says world time advances 'when the player acts, travels or skips, as in Tale of Immortal'. Chunk 5 Decision 6 made time flow continuously on the world map ('about a day every few seconds') with a pause key. That is a core-loop decision (lifespan pressure, pacing, what a month means) made inside a rendering and travel chunk, and the two docs now disagree.

**Evidence:** 0002 line 84 (A6); chunk 5 spec lines 50 and 159 ('Time flows continuously, about a day every few seconds').

**Impact:** Lifespan and ageing, breakthroughs and the NPC sim's monthly cadence all depend on it, and changing it later touches travel, fog, saves and replays.

**Recommendation:** Put the time model on the GD agenda explicitly. Then align 0002 A6 with the decision.

**Verification:** Checked: chunk 5 spec item 4 says time 'flows continuously on the world map, about a day every few seconds' with the month step as a designed pause. 0002 A6 (line 84) says time advances in calendar steps 'when the player acts, travels or skips'. These differ in wording, and 0002 was not amended. The reconciliation is partial: travelling is one of A6's triggers, so continuous flow while walking is arguably consistent, but flowing while the player stands still is not. The pause key and the GD agenda both exist, and game-design-gaps line 20 lists lifespan as a pending GD item. Real doc inconsistency, medium at most.

### GD-ROADMAP-SCOPE-10: Dungeons and arenas will be built before combat has any challenge

- **Severity:** low (reviewer said high) · **Verdict:** deliberate · **Category:** ordering-risk · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk7-combat-scenes-design.md:82`

Chunk 7 builds dungeon graphs, themed arenas, locked exits, objectives, bosses, spawned sites and a results screen. Its non-goals exclude 'Enemies attacking, combat AI, player death and defeat', yet its anti-goals are 'slow, laggy, badly balanced, a drudge'. Balance and drudgery can't be judged when enemies never threaten the player.

**Evidence:** Chunk 7 line 82 (non-goals) and line 182: 'Enemies move about without attacking'. game-design-gaps.md 'Combat depth: enemy variety and AI, elements interacting, dodging and defence, game feel... Phase 7 covers only the presentation side.'

**Impact:** Real-time combat is the heart of the genre, yet its feel and challenge come last. Dungeon pacing, arena size, enemy counts and ability tuning would be set in a vacuum and redone once enemies fight back.

**Recommendation:** Before chunk 7, prototype combat with stakes in the arena: one enemy that attacks, player health and death or retreat, dodge and defence, hit-stop and screen shake. Then size the dungeon work from what feels good.

**Verification:** Chunk 7's non-goals state that enemies do not attack and that there is no player death. game-design-gaps.md lists combat depth as a tracked gap for GD. The ordering concern is a fair opinion, but it is a recorded scoping choice. GD is also scheduled before chunk 7 and can re-scope it.
