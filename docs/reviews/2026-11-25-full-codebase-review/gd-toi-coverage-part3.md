# Review: gd-toi-coverage (part 3)

### GD-TOI-COVERAGE-21: Combat reads presentation sprite metrics, but replays record only the game data hash

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** determinism · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/combat.cpp:44`

Hit tagging uses each creature's chest, `manifest.Sprite(look.sprite).footY * scale / 2` (HeightOf/ChestOf), so repainting or resizing a sprite in the atlas changes which enemies a cast tags. Replays record and check only `GameData::Hash()` (replay.hpp StartRecording(start, dataHash)), not the manifest's.

**Evidence:** combat.cpp:44-62; replay.hpp:48-50, 67-70.

**Impact:** After an art change, an old replay diverges with a state hash mismatch instead of being refused as 'recorded under other assets', which makes bug reports harder to triage. As art grows (hundreds of sprites), this will happen more often.

**Recommendation:** Either record a hash of the sim-relevant manifest fields (foot points and sizes of creature sprites) with the replay's data hash, or give creatures hit radii and heights in creature data so combat never reads the atlas.

**Verification:** combat.cpp:41-62 does read manifest.Sprite(look.sprite).footY * scale for HeightOf and ChestOf, which drives where volleys aim and so hit tagging. replay.hpp records only a dataHash (GameData::Hash) plus state hashes, so a repaint that changes footY would diverge as a hash mismatch, not a 'other assets' refusal. The mechanism holds. Impact is limited because footY changes only on resized or repainted sprites, and the state-hash check does catch the divergence, though with a poorer message. Not recorded as deliberate that I found.

### GD-TOI-COVERAGE-22: Combat exists only in a dev scene; the core game loop can't be played yet

- **Severity:** info · **Verdict:** deliberate · **Category:** design · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/game/scenes/world.cpp:11`

The only game scene (not `devScene`) is the world map, whose traits have no `combat`. Combat runs only in the arena, a dev scene on a scratch world reached by F2 or the portal cycle. Entering places from the map (5c2) is pending. Danger sites do nothing beyond adding route cost.

**Evidence:** world.cpp:11-18 WORLD_TRAITS; host.cpp CastAction ignores casts outside combat scenes; world_map.cpp:455 is the only Danger use.

**Impact:** No loop of explore → fight → reward → grow exists to tune or playtest. Combat feel, pace and balance are being polished in isolation from any progression.

**Recommendation:** Make the first GD milestone a thin vertical slice: enter a danger site from the map, fight, get a reward that changes the persistent character, return. Do it before more world-map polish.

**Verification:** The finding is itself marked known-deferred. Entering places from the map is pending in 5c2, and chunk 7 owns combat scenes. The chunk 7 spec describes the danger-site-to-dungeon loop as planned work. The vertical-slice recommendation is sensible, but the ordering is already set in the backlog's priority list (5c2, then the GD brainstorm).

### GD-TOI-COVERAGE-23: Effort is going into map infrastructure before the game design exists

- **Severity:** info · **Verdict:** unverifiable · **Category:** process · **Status:** new · **Reviewer confidence:** low
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-design.md:125`

The GD brainstorm is scheduled after chunk 5. Meanwhile, the recent and in-flight branches are world-map infrastructure and polish: roads, river heads, and shared-routes (a staged travel and pathfinding redesign; in-flight.md). The travel system is about 2,000 lines across journey, travel, paths, road_curve and leg_walk_cache, and it serves one NPC behaviour (city-to-city visits by power), which the GD may replace.

**Evidence:** git log (river heads, roads); memory in-flight.md 'shared-routes — stage 1'; game-design-gaps.md (no core loop).

**Impact:** The shared-routes spec explicitly anticipates points of interest and arbitrary trips, which lowers the risk. Still, each performance or visual iteration on placeholder behaviour adds sunk cost and constraints before the loop is known, and measured travel budgets may not reflect the final NPC behaviour.

**Recommendation:** Once shared-routes stage 1 lands, run the GD brainstorm before stage 2 or 5c2, so the remaining travel and entering work is shaped by what NPCs and the player actually do.

**Verification:** The phase6 table does schedule the GD after chunk 5 (line 111), and game-design-gaps lists the missing core loop. Whether travel work is sunk cost depends on what the GD decides. That is a process opinion, not decidable from code. The GD gating is a recorded user decision of 2026-11-16, so reordering it is the user's call.
