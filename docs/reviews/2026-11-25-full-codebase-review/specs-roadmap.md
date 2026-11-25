# Review: specs-roadmap

**Scope:** docs/superpowers/specs/2026-11-10-phase6-design.md and the phase-6 chunk specs not yet built or only partly built (chunk5 world map, chunk6 towns, chunk7 combat scenes, chunk8 ui, chunk9 view models, chunk10 screens, chunk11 settings), plus the in-flight spec and plan at .claude/worktrees/shared-routes/docs/superpowers/specs/2026-11-24-shared-routes-design.md and .claude/worktrees/shared-routes/docs/superpowers/plans/2026-11-25-shared-routes-stage1.md if present: soundness, sequencing, hidden dependencies, underspecified parts, risks, whether the code on main is ready for them, scope realism.

**Findings:** 27 standing (0 critical, 0 high, 6 medium, 18 low, 3 info), 0 raised but refuted on verification.

**Parts:** specs-roadmap.md, specs-roadmap-part2.md, specs-roadmap-part3.md

## Assessment

## Specs and roadmap: phase 6, chunks 5 to 11, and the in-flight shared-routes spec and stage 1 plan

**Strengths**
- The chunk specs are disciplined. Each one has intent, the user's decisions kept apart from the builder's own calls, anti-goals and a testing section. "The user's call" versus "my call, check it" is tracked everywhere.
- The shared-routes spec is the best document in this scope. It is a layered HPA*-style design: costs grow with map area, not with places squared. It refuses locks on the publish path, has explicit scale budgets at 1×, 10× and 50×, and lists the rejected approaches with numbers. Its stage 1 plan is unusually rigorous: every open question is a spike with a decision rule, the edge cases are listed for review, and exact interfaces are given per task. The gate rule I spot-checked (Ruling 2) matches its test expectations (the 8×4 map, the road on row 3, the split run). Roads are rasterised 4-connected (`core/world_map.cpp` `Rasterise`), and a diagonal step needs both orthogonal neighbours open (`StepCost`). So orthogonal-only gates keep every connection, and a road crossing a border always offers a road-to-road pair.
- The code on main fits the near-term work well. `ViewReport`, the world/scratch split, delta saves, `Snapshot::transition` and `CalendarView`, `UI_COLOUR_INFO`/`COLOUR_AREA_INFO`, a `ui` slot in `MODULES`, and the Pause action are all in place.

**Weaknesses**
1. **The roadmap documents are stale and contradict each other.** The phase table still says 5b3 and 5b-look are "To do" and 5c is unsplit. The chunk 5 spec never got the updates it promised.
2. **Specs 5, 7 and 10 were approved on 2026-11-10 and are built on a hand-made map.** That contradicts the 2026-11-25 decision that maps are procedural per seed. No chunk or spec owns the procedural generator, yet `WorldMap` is still loaded-once, hashed `GameData`.
3. **Chunks 6 to 10 have no Accessibility section,** although phase decision 17 and the user's accessibility-first rule require one in every spec.
4. **Chunks 6 and 7 require byte-identical generation on every preset and pin generator hashes.** That contradicts the recorded "deterministic within one binary, never pinned hashes" decision.
5. **Hidden dependencies across specs:**
   - spawned and cleared dungeon sites, and their danger costs, against an immutable `WorldMap` and a route graph built once per world;
   - chunk 6's "most people live outside the walls" against today's 70% of people on the city tile;
   - the chunk 9 roster against tens of thousands of undrawn city residents within sight;
   - `settings.yaml` ownership split between chunks 8 and 11, plus the `ui` module, which cannot link `data`;
   - the gamepad Back button assigned to three different jobs;
   - blur, generation progress and settings previews, none of which exist yet.
6. **Sequencing creates throwaway work.** Chunks 5c2, 6 and 7 build game-text stand-ins (enter prompt, speech bubble, dungeon popup, results screen) that chunks 8 and 10 then rebuild.
7. **Shared-routes risks:** no path smoothing, and a quality bar (5% on cost, random long pairs) that can't see detours on short trips or zigzags in open country. A* termination with several possible end nodes is underspecified. Budgets are measured on 4 workers while the phase bar pins 2, and the game builds the graph without workers.

**Overall health:** the engine and process are in good shape, and shared-routes stage 1 is sound and ready to continue. The roadmap past chunk 5 is not. Chunks 6 to 11 were written before the game-design brainstorm, before the procedural-map decision and before 5a, 5b and 5c1 changed the architecture. They should be revisited as a set (after the game-design brainstorm and before chunk 6 is planned) rather than planned one by one as approved.

**Risks**
- Scope: six more chunks, each about chunk 5's size, which itself took about 10 branches.
- Placeholder systems (attitude, cultivation tiers, sects, orbs) built ahead of the game-design brainstorm and then reworked.
- Static map structures colliding with dynamic world content.
- Accessibility debt building up until chunk 11.

**Files read in full:** docs/superpowers/specs/2026-11-10-phase6-design.md, docs/superpowers/specs/2026-11-10-phase6-chunk5-world-map-design.md, docs/superpowers/specs/2026-11-10-phase6-chunk6-towns-design.md, docs/superpowers/specs/2026-11-10-phase6-chunk7-combat-scenes-design.md, docs/superpowers/specs/2026-11-10-phase6-chunk8-ui-design.md, docs/superpowers/specs/2026-11-10-phase6-chunk9-view-models-design.md, docs/superpowers/specs/2026-11-10-phase6-chunk10-screens-design.md, docs/superpowers/specs/2026-11-10-phase6-chunk11-settings-design.md, .claude/worktrees/shared-routes/docs/superpowers/specs/2026-11-24-shared-routes-design.md, .claude/worktrees/shared-routes/docs/superpowers/plans/2026-11-25-shared-routes-stage1.md, /tmp/claude-1000/-home-lotus-Development-project-n/5e986e63-9457-419a-a1f3-2b39adbcdc44/scratchpad/review-history.md, /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/in-flight.md, /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/game-design-gaps.md, /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/procedural-world-map.md, /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/accessibility-first.md, src/AGENTS.md, src/core/frame.hpp (lines 300-612)

## Findings

### SPECS-ROADMAP-01: Specs for chunks 5, 7 and 10 assume a hand-made map, contradicting the procedural-per-seed decision; no spec owns the map generator

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** plan-flaw · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk10-screens-design.md:34`

Chunk 10 Goal 2 says Start uses 'the hand-made map (chunk 5) ... The map's places stay hand-made (their tiles and names, until procedural world generation), and the two big demo dungeons keep their pinned seeds'. Chunk 7 places 'fixed landmark dungeons placed on the hand-made map (chunk 5), including the demo's two big dungeons', each a picked seed pinned by a hash test (lines 34-35, 101-107, 179). Chunk 5 lists 'Procedural world generation' as a non-goal (line 78) and Decision 9 'Hand-made map'. The phase spec Goal 3 assumes the same. On 2026-11-25 the user ruled the map procedural per seed, with world.yaml only a test map (memory procedural-world-map.md; the shared-routes spec, Intent). In code the map is `GameData::world`: read once at start, hashed into `BaseHash`, kept across reloads by `app::KeepWorld`, and handed to `Renderer::SetWorld` once (src/core/game_data.hpp:73-99). The architecture is one map per process, and no chunk in the phase table builds a generator.

**Impact:** Chunk 10's 'Start makes a brand-new world' is specified against the wrong map model. Making the map change at run time touches a lot: GameData identity, base-hash saves, renderer world state (ground field, road curves, river ribbons), fog sizing, the route graph and city sizes. It would land unplanned in whichever chunk first meets it. Chunk 7's landmark dungeons and pinned seeds have no meaning on a per-seed map.

**Recommendation:** Add a roadmap row for procedural world generation (its own brainstorm and spec), placed before chunk 10 and probably before chunk 7. Mark the hand-made parts of chunks 5, 7 and 10 as superseded. Decide where a generated map lives (world state built from the seed, not GameData), how saves identify it (seed + GENERATOR_VERSION already, which suffices), and how the renderer receives a new map at run time.

**Verification:** Facts check out: the memory file procedural-world-map.md (2026-11-25) says the map is procedural and world.yaml is only a test map. The chunk 5/7/10 specs, dated 2026-11-10, assume a hand-made map, and the phase table has no generator row. But the specs predate the ruling and the user hasn't asked for them to be rewritten. Chunk 5 lists procedural generation as a non-goal ('until procedural world generation'), so it was already anticipated. The real gap is a missing roadmap row. 'High' overstates it because nothing is broken today. I did not check the GameData::world details at src/core/game_data.hpp:73-99.

### SPECS-ROADMAP-02: Chunks 5–10 lack the Accessibility section phase decision 17 requires

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** process-violation · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-design.md:151`

Phase decision 17 (and memory accessibility-first.md, 'Every chunk's spec gets an Accessibility section') says every chunk spec covers reduce motion, reduce flashing, colour-blind palettes and text size, and that colour is never the only signal. A grep finds no Accessibility section (and no mention of 'accessib', reduce motion or colour-blind) in the chunk 5, 6, 7, 8, 9 or 10 specs. Concrete gaps: chunk 7 orbs are 'coloured orbs by kind' (line 61) with colour as the only signal, and only chunk 11 adds shapes; chunk 7's exit seals and elite auras flash; chunk 10's backdrop 'drifting slowly' and loading messages 'rotating every few seconds' have no reduce-motion path; chunk 6's speech bubbles and crowd have no text-size note.

**Impact:** Accessibility, the user's top priority, becomes retrofit work bunched into chunk 11, the last chunk. Features built in chunks 6–10 ship without hooks, so chunk 11 must reopen each of them.

**Recommendation:** Before each of chunks 6–10 is planned, add its Accessibility section (reduce motion, reduce flashing, palettes, text size, and the non-colour signal for every coloured meaning). Make the section a planning gate in the phase spec.

**Verification:** Phase decision 17 does require an Accessibility section in every chunk spec (phase6-design.md:151). A grep for 'accessib' finds it only in the chunk 3, 4 and 11 specs, so chunks 5-10 have none. The 'top priority' framing and the retrofit-into-chunk-11 impact are judgement. Chunks 5-10 are still unbuilt, so the gap can be closed before each is planned. Chunk 5 is partly built. The specific examples (orb colours, flashing seals) were not checked.

### SPECS-ROADMAP-03: Phase table and chunk 5 spec statuses are stale (5b3, 5b-look, 5c split)

- **Severity:** medium · **Verdict:** confirmed · **Category:** docs-contradict-code · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-design.md:108`

The phase table lists 5b3 'Fog and sight' as 'To do' with the chunk 5 spec as its spec, although `2026-11-20-phase6-chunk5b3-fog-design.md` was implemented on 2026-11-21 (PR #47). It lists 5b-look as 'To do (to be designed)', although the terrain (2026-11-21), roads, travellers' walk and river heads specs are implemented. 5c is still one row, although it was split on 2026-11-16 and 5c1 was implemented on 2026-11-19; 5c2 has no row. Line 15 still says 'eleven chunks', while the table has about 20 rows (4b, 5a, 5b1–3, 5b-look, 5c, GD). The chunk 5 spec promises '5b and 5c update this spec when they start' (lines 11-12) but still reads as the original design: Status 'approved', with no superseded marks for the hand-made map, fog or names.

**Impact:** The history-first workflow (recall-history reads docs/design and specs) and every new session see wrong status. The next planner may re-spec done work or miss that 5c2 is the remaining gate before the game-design brainstorm and chunk 6.

**Recommendation:** Update the phase table: 5b3 done, 5b-look done (list its specs), a 5c1 done row and a 5c2 to-do row, plus a shared-routes row. Fix 'eleven chunks'. Add a header note to the chunk 5 spec pointing to the sub-specs that supersede each section.

**Verification:** The phase table at phase6-design.md lines ~108-110 shows 5b3 as 'To do' with the chunk 5 spec, 5b-look as 'To do (to be designed)', and a single 5c row 'To do'. The chunk 5 spec header still reads 'Status: approved'. The src/AGENTS.md text describes fog, sight and the kept walks as built, so the table is stale. I didn't check the PR #47 or 5c1 dates.

### SPECS-ROADMAP-04: Chunk 9's world-map roster includes thousands of undrawn, unclickable city residents

- **Severity:** medium · **Verdict:** partially-confirmed · **Category:** design-flaw · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk9-view-models-design.md:33`

The roster lists 'on the world map, every NPC within the player's sight radius around the player', from 'the logic's own query of NPCs within the sight radius' (line 93). The chunk 5 spec says 'Cities' resident NPCs aren't drawn on the map' (line 175), and the publish draws only journey members in sight. With about 10,500 NPCs on the capital's tile and the player starting there, the roster would list over 10,000 people, most with no instance. 'Clicking a row selects that person in the world (outline)' then has nothing to outline. The spec's intent ('the people around you, not the whole world') and its anti-goal 'a hitch as a crowd enters the sight radius' are both at risk.

**Impact:** Either the roster is a 10,000-row list at the start city, with a per-change rebuild of a large `NearbyRoster` view and selection that fails silently, or implementers improvise a rule the user never approved.

**Recommendation:** Define 'nearby' on the world map as the walkers drawn within sight (journey members), and optionally a per-city summary row for residents. Or say that residents appear only inside the town scene. Make the view's size bound explicit.

**Verification:** Chunk 9 line 33 says the roster includes 'every NPC within the player's sight radius' on the world map. Lines 119 and 133 repeat that 'nearby' is the sight radius, and line 93 says the logic queries for it. src/AGENTS.md says world map publishing draws only journey members within sight, and the 7/10 city-tile residents are not drawn. So the roster would plausibly list thousands of residents on the start city. The spec doesn't address this. It is a real underspecification, but the size is unmeasured and the chunk is unbuilt.

### SPECS-ROADMAP-05: settings.yaml ownership conflicts between chunk 8 (window layout) and chunk 11 (table-checked overrides); the ui module can't reach data

- **Severity:** medium · **Verdict:** confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk11-settings-design.md:129`

Chunk 11: '`settings.yaml` holds only the player's values that differ from the defaults ... an unknown key in the player's file is logged and dropped' against the settings table. Chunk 8 stores 'positions, scales and which windows are open, per player in `settings.yaml`', written 'on change (debounced) and on exit' by the `ui::Windows` registry (chunk 8 lines 45-46, 133-135). Chunk 11 also stores binding overrides there (line 85). None of these are settings-table rows. Separately, `MODULES` gives `ui` the deps core, gfx and platform but not `data` (tools/project_n.py:1236), and only `data` sees yaml-cpp, so the registry cannot write settings.yaml itself.

**Impact:** If chunk 11's rule is implemented as written, it silently drops the window layout and the bindings on every load. Chunk 8 implementers will either break the layering or invent an app-mediated path without a design.

**Recommendation:** Specify settings.yaml's sections (options, window layout, bindings), each with its own validation, with 'unknown key dropped' scoped to the options section. Specify that ui hands its layout to app as core types and app persists it through data.

**Verification:** Chunk 11 line 129-131 says settings.yaml holds only table overrides and unknown keys are dropped. Chunk 8 (lines 45, 135) stores window layout there, and MODULES (tools/project_n.py ~1236) gives ui no data dependency. The spec gap is real and none of the specs reconciles it. Impact is a design gap, not a bug yet.

### SPECS-ROADMAP-06: Window moving and scaling are mouse-only, and no chunk adds a keyboard or gamepad way

- **Severity:** medium · **Verdict:** confirmed · **Category:** accessibility · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk8-ui-design.md:63`

Chunk 8: 'nothing is mouse-only except dragging windows (chunk 11 may add a gamepad way to move them)'; 'Windows can't be dragged by gamepad'; per-window scale is 'Ctrl and the wheel over a window's header' (lines 147, 153). Chunk 11 never adds window moving or per-window scale for keyboard or gamepad; it only puts 'reset window layout' under Display. The user's rule (memory accessibility-first.md): 'Every option is controllable easily and fluidly by mouse, keyboard and gamepad'.

**Impact:** Keyboard-only and gamepad players cannot arrange or scale windows, which is the chunk's central selling point ('windows move and scale'). This breaks a top-priority user rule.

**Recommendation:** Add to chunk 8 (or explicitly to chunk 11) a move/scale mode for the focused window: arrows or d-pad move it with snapping, shoulders or bumpers scale it, and A or B confirms or cancels. Include it in the reachability test.

**Verification:** Chunk 8 lines 64, 154 and 202 explicitly say no gamepad dragging and defer to chunk 11 'may add'. Chunk 11 only lists 'reset window layout' (line 64), so there is no keyboard or gamepad move or scale. It is a recorded deferral, but it is unresolved and conflicts with the accessibility-first rule. Keyboard moving is not even mentioned.

### SPECS-ROADMAP-07: Chunks 6 and 7 require byte-identical generation on every preset and pin hashes, against the within-one-binary determinism decision

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** determinism · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk6-towns-design.md:111`

Chunk 6 Goal 1 says 'deterministically, on every platform'. Its Design asks for 'integer and fixed-point arithmetic ... so every preset gives the same bytes', and its Testing says 'the same seed gives the same layout on every preset (a hash compared in tests)' (line 201). Chunk 7 has 'a test pins a hash of the generated route and arenas' for the big dungeons (lines 104-107) and 'the same seed gives the same route ... on every preset' (line 198). The recorded decision says replays are deterministic only within one binary, with NO cross-platform determinism goal, and the long-run-determinism work says 'compare runs of one build, never pinned hashes'. Beyond floats, std::sort's unspecified order among equal elements, unordered containers and container growth differ between libstdc++, libc++ and MSVC STL, so a pinned cross-preset hash would also need a stable-sort and no-unordered rule.

**Impact:** Either the tests fail on one toolchain the first time a generator uses std::sort with ties or a library container order, or the project quietly takes on a cross-platform determinism guarantee it rejected. Separately, the save doesn't store the town layout but does store crowd positions and assignments in the active scene. A city generator change without a GENERATOR_VERSION bump would load NPCs inside walls.

**Recommendation:** Align the specs with the decision: same layout within one binary for any worker count and any build order, compared in-process, with no pinned hashes. If curated dungeons need a change detector, keep a golden hash per preset or state a deliberate narrower rule (integer-only generator, stable sorts, no unordered iteration) with its reason. State that town and dungeon generators fall under GENERATOR_VERSION, which saves are checked against.

**Verification:** Chunk 6 line 112 asks for integer and fixed-point arithmetic 'so every preset gives the same bytes', and line 201 says 'same layout on every preset (a hash compared in tests)'. docs/design/0002-engine-architecture.md:265 says cross-platform replays would need fixed point and nobody has asked for them. So the specs do reach for a stronger guarantee than the stated decision. That is a spec choice, not a flaw in any code. Both specs are unbuilt and will be re-brainstormed. The std::sort and unordered-container risk is real but speculative. I didn't verify the chunk 7 line numbers or the GENERATOR_VERSION claim.

### SPECS-ROADMAP-08: Chunk 7's spawned and cleared sites collide with the immutable WorldMap and the once-per-world route graph

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** hidden-dependency · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk7-combat-scenes-design.md:101`

Chunk 7 makes sites dynamic world records: they spawn on days passing, vanish when cleared, and the chunk 5 danger places 'become the world's starting sites'. Today danger places are `WorldMap` places (hashed GameData), and `danger_reach` marks `TileFlag::Danger` tiles at map build, which raise each profile's cost (src/AGENTS.md world.yaml paragraph). The shared-routes spec makes 'each place (city, danger, any later kind) a node in its sector' of a graph 'built per world, once' (Goal 1, Goal 2), with the place table over places at world build (Goal 7). Its only dynamic-map note is a non-goal about a changing map (a bridge falls). Nothing covers sites appearing or vanishing, or danger cost moving with them.

**Impact:** Clearing a starting danger site leaves its danger cost and graph node in place forever. Spawned sites get no danger cost, no node and no place-table row. Travellers keep skirting cleared sites and walk through new ones. Fixing this late means rebuilding sectors and place tables in the month step, which stage 2 isn't designed for.

**Recommendation:** Settle this before stage 2's plan: either sites are never graph nodes (route to them as arbitrary tiles, Goal 4) and danger cost moves out of the static `WorldMap` into a per-world overlay the graph reads per sector with sector rebuilds; or the spec adds dynamic places with a rebuild rule. Record the choice in the shared-routes spec's non-goals or Decisions.

**Verification:** The claim is about the interaction of the unbuilt chunk 7 with the unbuilt shared-routes stage 2. Both are drafts. The question is a valid one for stage 2's planning, but I did not read the shared-routes spec or the chunk 7 text. Whether it is a defect depends on a design decision still to come.

### SPECS-ROADMAP-09: Chunk 6's 'most people live in the region' contradicts today's 70% on the city tile

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** plan-flaw · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk6-towns-design.md:128`

Chunk 6 says 'only enough households to fill its houses live inside the walls; the rest (most of them, as about 50,000 share six cities) live in its region, outside'. A large city holds a few hundred people. `GenerateWorld` spawns 'seven tenths on the city's tile, the rest on a drawn tile of its region' (src/AGENTS.md), so about 10,500 of the capital's 15,000 live on the city tile. The shared-routes spec (Goal 10) now sends everyone from the city tile anyway. Neither spec reconciles the counts. The spec also assumes 'about six cities', which no longer holds on procedural maps.

**Impact:** Chunk 6 would redistribute home tiles, which changes the world generator, saves and the base hash, and moves travellers' origins and the chunk 9 roster populations. It would do this right after shared-routes stage 2 settles travel on the current distribution.

**Recommendation:** Decide once (in the game-design brainstorm or chunk 6's brainstorm) what `Npc::cell` means for townsfolk versus region folk, and update the generator rule and src/AGENTS.md together. Size cities from data per place kind, not from 'six cities'.

**Verification:** src/AGENTS.md confirms that GenerateWorld puts seven tenths of each city's people on the city's tile and the rest on a drawn tile of its region. Chunk 6's 'most live in the region' wording does conflict with that. But chunk 6 is unbuilt, will be brainstormed again, and its brainstorm is the natural place to settle it. The impact claim that it would move the hash and saves is plausible. The shared-routes Goal 10 and 'six cities' details were not checked.

### SPECS-ROADMAP-10: The gamepad Back button gets three conflicting jobs across code and chunks 8 and 11

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** spec-conflict · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk8-ui-design.md:152`

Today `PadBack` is bound to `Control::PauseWorld` (src/app/bindings.cpp:93; src/AGENTS.md 'P or the gamepad's Back pauses the world'). Chunk 8 makes 'the Back button move focus from the game to the front window and back'. Chunk 11 makes rebind capture cancel 'by holding the Back button' and makes Start 'always open the pause menu' (lines 90, 143-144). None of the specs mentions the existing world-pause binding or where it moves. There are also two 'pause' concepts: the world clock's `ToggleWorldPause` (saved, shown as 'Paused' on the date line) and the tick freeze `ActionKind::Pause` that chunk 10's modals use. No spec reconciles them.

**Impact:** Gamepad users, whom phase decision 18 says every UI must serve, would lose world pause or get a context-dependent Back. Chunk 11's data check ('one input on two controls of one context') will reject the defaults unless contexts are designed. Players will be confused by two different pauses.

**Recommendation:** Give the controller layout one table across chunks 8, 10 and 11, with input contexts (game, window focus, capture). Move world pause explicitly (for example to a face button or Start's menu). State how the world-clock pause and the modal tick freeze relate and which one the date line shows.

**Verification:** src/app/bindings.cpp:93 binds PadBack to Control::PauseWorld, which matches the claim. I did not read chunks 8 and 11 to confirm they give Back other jobs. If they do, a controller-layout table across chunks is a sound ask. The two kinds of pause (world clock and tick freeze) are plausible. This is a cross-chunk consistency gap to settle at chunk 8 or 11 planning, not a bug.
