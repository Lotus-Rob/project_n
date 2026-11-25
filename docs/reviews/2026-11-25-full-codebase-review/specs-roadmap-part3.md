# Review: specs-roadmap (part 3)

### SPECS-ROADMAP-21: Chunk 11's typed search box contradicts the phase non-goal 'no text input'

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** spec-conflict · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk11-settings-design.md:139`

The phase spec's non-goals include 'Text input and an IME' (line 54). Chunk 11 builds a search box that 'takes Latin keystrokes', 'any letter starts a search'. That is text input (SDL text-input events, a caret, editing, RmlUi input fields) without an IME. The spec acknowledges the IME half but not the text-input half.

**Impact:** A small but real feature (an input field, keyboard focus rules that conflict with letter keybindings such as WASD while the settings window is open) that the phase scope says doesn't exist.

**Recommendation:** Amend the phase non-goal to 'no IME; Latin text in the settings search only', and specify how letter keys are routed while the search is focused.

**Verification:** The phase spec line 54 lists 'Text input and an IME' as a non-goal. Chunk11 lines 38, 95 and 139-140 build a search box taking Latin keystrokes, labelled 'Search without an IME (text input stays a phase non-goal)'. The spec does acknowledge the tension, but it treats typed Latin text as outside the non-goal, which is a semantic stretch. It does not say how letter keys are routed against keybindings while the search is focused (line 95 only says any letter starts a search). The conflict is real but the spec is aware of it, so low severity.

### SPECS-ROADMAP-22: Chunk 7's arena swaps and chunk 6's per-city towns don't fit the stateless, compile-time scene model

- **Severity:** low · **Verdict:** unverifiable · **Category:** hidden-dependency · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk7-combat-scenes-design.md:115`

Scenes are 'stateless classes listed in game/scenes.cpp (F2's order; a scene's key is its place there, checked at compile time)', and each scene change runs leaving, loading and entering stages with no ticks. Chunk 7 wants 'walking out of an open exit loads the neighbouring arena; this must not hitch ... a change is a swap, not a load', with all arenas generated at the run's start. Chunk 6 wants one generated layout per city. Neither says whether a run or town is one scene whose host state holds the generated layout and current arena (with the entered place in WorldState), or a scene per arena. It also doesn't say whether all arenas' enemies (up to hundreds each, about 15 arenas) exist from the start or are created on entry.

**Impact:** The parametrised-scene question lands on the 5c2 stand-in town and on chunk 7 without a design. Creating every arena's entities at run start may cost memory and publish time.

**Recommendation:** Settle in 5c2's spec: a place-parametrised scene (the entered place and generated layout kept in host state and saved with the active scene), arena changes as in-scene swaps with no scene-change stages, and enemies created on arena entry from the seed.

**Verification:** The facts hold: src/AGENTS.md says scenes are stateless classes, the host owns all state, and WorldState is never rebuilt by a scene change. Whether arenas or towns are place-parametrised scenes or in-scene swaps is a design choice the specs may leave to the 5c2 spec, and I did not read chunk7's text at line 115. A 5c2 spec or a spike would decide it. The suggestion to settle it there is reasonable.

### SPECS-ROADMAP-23: Chunk 6's debug 'regenerate the city from a new seed' changes simulated state without being recorded

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** determinism · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk6-towns-design.md:185`

'A debug key regenerates the current city from a new seed ... same building counts and people, a new layout; not saved.' The crowd runs in the logic, is saved and replayed, and is placed on the layout's walk graph. Regenerating the layout under it changes simulation inputs (paths, posts, homes). If the key isn't a recorded action, a replay or save taken afterwards no longer matches. If the layout isn't saved, a load regenerates the original layout while the saved crowd stands on the new one.

**Impact:** Desyncs and NPCs inside walls after debugging, which looks like a generator bug.

**Recommendation:** Make it a recorded debug action that rebuilds the layout and resets the crowd, and refuse saves while a regenerated city is active, or save the override seed.

**Verification:** Chunk6 line 185 says the debug key regenerates the city 'not saved', and line 149 says a load regenerates the layout. If the crowd stands on the old layout's walk graph, the desync described is plausible. The spec does not say whether the key is a recorded action or how the crowd is reset. It is a debug-only feature and the gap is genuine but minor.

### SPECS-ROADMAP-24: Backlog line still places bindings and rebinding in 'chunk 7'

- **Severity:** low (reviewer said info) · **Verdict:** confirmed · **Category:** docs-stale · **Status:** new · **Reviewer confidence:** high
- **Where:** `/home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md:23`

The backlog says 'Bindings as data, rebinding and the settings screen: now planned as phase 6's chunk 7', but the renumbered phase table puts them in chunk 11 (and chunk 7 is now combat scenes). The 'Presentation out of app' line already uses the new numbering.

**Impact:** Wrong cross-references when recall-history searches by chunk number.

**Recommendation:** Change it to chunk 11.

**Verification:** sweep-up-backlog.md line 23 says bindings, rebinding and the settings screen are 'phase 6's chunk 7'. The phase 6 design table (line 117) puts them in chunk 11, and chunk 7 is combat scenes. The line is stale. Line 45 uses 'chunk 7's combat', so the numbering there is correct. The fix is to change line 23 to chunk 11. It is a memory file, not the repo.

### SPECS-ROADMAP-25: Phase and chunk scope is far larger than the pace chunk 5 has shown

- **Severity:** info · **Verdict:** unverifiable · **Category:** scope-realism · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-design.md:111`

Chunk 5 grew into about 10 specs and branches over 15 days (scenes, 5a, 5b1, 5b2, 5b3, terrain, roads, travellers, river heads, walk and camera, shared-routes in flight), and 5c2 is still open. Before chunk 6 can start, the remaining queue also holds: shared-routes stage 1 (waves B–F plus two spike rounds) and stage 2, 5c2, the game-design brainstorm, the data-driven sweep and localisation (backlog priority 9 says 'before chunk 6 adds content'), and the full project review. Chunks 6–11 are each specified at a scale similar to chunk 5: generated cities with staged crowds; generated dungeons with five themes and spawning; RmlUi with its own font engine, a Chinese line-break patch, a window manager and a reachability test; view models; seven screens with blur and a loading screen; eight settings categories with search, previews and rebinding.

**Impact:** Phase 6 as written is several months of work at the current pace. Without explicit re-scoping, later chunks will be squeezed and accessibility, which comes last, gets squeezed most.

**Recommendation:** After the game-design brainstorm, re-scope phase 6 into a demo-world core and a deferred list. Consider moving chunk 11's accessibility essentials forward, and record realistic chunk sizes in the phase table.

**Verification:** This is a scope-realism opinion. Its premises (chunk 5 grew into about 10 specs and branches, with 5c2 and the shared-routes work still open) are consistent with the repo's recent commit history and with the docs, but I did not count them. The sweep-up backlog and the planned game-design brainstorm and full project review already act as re-scoping checkpoints. Only the passage of time and measured chunk velocity would settle it. As a risk note it is reasonable.

### SPECS-ROADMAP-26: Shared-routes graph: within-sector edges grow with nodes squared as points of interest arrive

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** low
- **Where:** `.claude/worktrees/shared-routes/docs/superpowers/specs/2026-11-24-shared-routes-design.md:85`

Goal 1: 'Within a sector an edge joins each pair of nodes that the profile can walk between', with every place a node, and the spec expects 'prebuilt, non-generated points of interest' later. A sector has about 8–16 gate nodes today. Each place added to a sector adds edges to every other node, and each needs a sector walk to build. Sectors dense with points of interest (city surroundings) grow quadratically in edges and build time. The budgets are measured with places only (cities and dangers).

**Impact:** Not a problem at today's density. It may break the memory and build budgets once points of interest are many per sector.

**Recommendation:** Note in the spec that points of interest join the graph as arbitrary tiles (Goal 4) or link only to their sector's gates, not to every node, and add a point-of-interest density to the synthetic map maker when that work starts.

**Verification:** Spec text at line ~85 does say an edge joins each pair of nodes in a sector, so edges are quadratic in nodes per sector. The reviewer admits this is not a problem at current density, and it is a forward-looking risk with no evidence. Points of interest are not in the graph yet. Calling it a defect is overstated; at most a note for later.

### SPECS-ROADMAP-27: Chunk 8 bakes UI strings into strings.yaml under ui. names ahead of the planned localisation redesign

- **Severity:** info · **Verdict:** deliberate · **Category:** sequencing · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk8-ui-design.md:116`

Chunk 8 fixes the UI string format ('`strings.yaml` under their own `ui.` names ... RmlUi translates at load'). The backlog's 'Localisation to industry practice' (per-locale files, BCP 47, ICU or Fluent, pseudo-localisation) is to be raised 'before the UI chunk (chunk 8) adds many strings'.

**Impact:** If chunk 8 is planned as specified, every UI string is written in a format the localisation redesign then replaces.

**Recommendation:** Run the localisation design before chunk 8's plan and amend the spec's string section to match it.

**Verification:** The chunk 8 spec does put UI strings in strings.yaml under ui. names (line ~116). The backlog's localisation entry (sweep-up-backlog.md line 54) is a recorded, known deferral that says to raise it before chunk 8 adds many strings, or at the full project review. The sequencing risk is already tracked.
