# Review: gfx-world-draw (part 2)

### GFX-WORLD-DRAW-11: tile_hash.hpp comments cite history; TERRAIN_SEED is world-independent

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/tile_hash.hpp:13`

Comments refer to 'game/scenes/world.cpp's old noise' and a 'retired' purpose number 9 kept in game/random_purposes.hpp, which are history citations the repo's comment rule forbids. The seed is a fixed constant (0x5eed), independent of the map's seed.

**Evidence:** `/// The seed of the map's tile draws: that of game/scenes/world.cpp's old noise (game can't be seen from gfx).`

**Impact:** Rule violation, and cross-module coupling by convention (a later purpose 9 in game would collide silently). Every procedural world gets the same stamp and decor pattern at the same tile coordinates, which makes seeds look alike in presentation.

**Recommendation:** Rewrite the comments to say why. Consider mixing the map's seed into the draw, or decide deliberately that decor is seed-independent and say so. A static_assert or shared registry can protect the purpose number.

**Verification:** The comment 'game/scenes/world.cpp's old noise' and 'retired' do cite history, which AGENTS.md forbids. But src/game/random_purposes.hpp:36 records the purpose 9 reservation, so the collision claim is mostly guarded by convention. Whether the seed should be seed-independent is unrecorded; I found no decision either way.

### GFX-WORLD-DRAW-12: Tests do not cover scale behaviour or several defects above

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/gfx/route_draw_tests.cpp:126`

Tests use the shipped small map and synthetic small maps. Nothing exercises GroundTexels, HeadingNear or FindJoin cost on a large map, DrawTerrain on a dense view that overflows the cap, DrawRoute appending to a non-empty drawing, WriteBlock with a zero-size sprite, or FogFrame with a short values span.

**Evidence:** Test case lists for terrain_draw, route_draw, ground_field, sprite_block, fog contain none of these.

**Impact:** The scaling and robustness defects above would not be caught by the suite.

**Recommendation:** Add small representative tests for each: overflow ordering, relative cap in DrawRoute, zero-size sprite refused, FogFrame size mismatch asserts; and a measured spike for large-map cost.

**Verification:** The finding is vague, since it points at unspecified defects above, and part of it is wrong. route_draw_tests.cpp already covers DrawRoute appending to a non-empty drawing ("the route's batch covers only what was appended", lines 126-138). It also covers the cap, with a route far past MAX_ROUTE_INSTANCES that must keep its end marker (line 140). The remaining gaps are real: terrain_draw_tests.cpp has no overflow-cap case and no large-map case, and fog_tests.cpp has no short-values-span case. sprite_block_tests.cpp has only three cases and none for a zero-size sprite. I did not verify that the underlying defects exist. Several of these are scale-cost claims. Under this repo's rules, large-map cost is a measured spike, and small unit tests are the stated policy (the memory notes small-unit-tests and unknowns-are-measured-spikes). Low severity is right.

### GFX-WORLD-DRAW-13: Rivers' join points use stored midlines of unmodified waypoints but the drawn lines of other rivers differ

- **Severity:** info (reviewer said low) · **Verdict:** unverifiable · **Category:** design · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/gfx/river_draw.cpp:117`

Midlines are computed once from the raw waypoints, but each drawn river can have a pond start point inserted, a joined end moved, or a mouth point appended. A tributary joins the stored midline of the target, not the line that is actually drawn.

**Evidence:** `stored = core::Midlines(waypoints, ...)` before the loop; the loop then mutates `points` (insert at begin, back() = joined, push_back(mouth)) and draws that. FindJoin searches `stored`.

**Impact:** A tributary can end on a spot where the drawn target river's ends differ (near the target's pond start or mouth extension), leaving small gaps or overshoots near river ends. Tests only cover simple straight cases.

**Recommendation:** Document the approximation, or join against the final drawn lines in a second pass. Add a test of a tributary joining a river's pond-extended start or mouth.

**Verification:** The structure matches the claim: stored midlines are computed from raw waypoints before the loop, and the drawn points are then modified. Whether this leaves a visible gap depends on how JoinedEnd and the pond or mouth extensions behave geometrically. Reading alone does not settle it. A look-at-the-game picture or a targeted test would decide it. The finding's own confidence is low, and I give it no more.

### GFX-WORLD-DRAW-14: RiverStarts has no production caller

- **Severity:** info · **Verdict:** confirmed · **Category:** dead-code · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/river_draw.hpp:37`

RiverStarts() recomputes waypoints and midlines for the whole map and is only referenced by tests.

**Evidence:** A grep over src/ finds no use outside river_draw.cpp; the river_draw tests call it.

**Impact:** Public API and compile cost that production never uses. It also duplicates StartOf's work at test time.

**Recommendation:** Remove it or move it to the test support code, or use it in the renderer so DrawRivers shares the result.

**Verification:** grep of src finds RiverStarts only in river_draw.cpp:98 and the header. StartOf's comment says it is shared so the two never disagree, which is the deliberate reason for the test hook. The public API is test-only.

### GFX-WORLD-DRAW-15: Fade handling inconsistent between fade-in (capped at half the line) and fade-out (not capped)

- **Severity:** info · **Verdict:** confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/gfx/ribbon_band.cpp:50`

Fade-in length is `min(fadeIn, along.back()*0.5)`, and taper likewise, but the end fade uses `laid.fade` uncapped.

**Evidence:** Shares(): fadeInLength is capped; the end loop divides by laid.fade directly.

**Impact:** A line shorter than `fade` never reaches full strength, unlike its start. Only a visual oddity for very short rivers.

**Recommendation:** Cap the end fade the same way, or document it.

**Verification:** Shares() caps fadeIn at half the line via MOST_SHAPED_SHARE, but the end loop (lines 54-58) divides by laid.fade uncapped. A line shorter than fade never reaches full share at its start end. The asymmetry is real and only visual.

### GFX-WORLD-DRAW-16: Ribbon helper names are generic in gfx and `near`/`far` are Windows macros

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** naming · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/gfx/ribbon.hpp:89`

`gfx::Length(core::Vec2)` and `gfx::HeadingOf` are generic names in a public header. `WriteRibbonQuad` names parameters `near` and `far`, which minwindef.h defines as macros.

**Evidence:** `inline void WriteRibbonQuad(..., const RibbonEnd& near, const RibbonEnd& far, u32 colour)`.

**Impact:** Only a risk if windows.h is ever included before this header in an MSVC TU, which would break the build in a confusing way. A generic `Length` can clash with a core::Length found by ADL.

**Recommendation:** Rename the parameters (`start`, `stop`) and give the helpers more specific names or move them to core/vec2.

**Verification:** WriteRibbonQuad does take parameters named `near` and `far`, which windows.h defines as macros. In practice SDL-based TUs rarely include windows.h before this header, and the project builds on MSVC today. The claim about generic Length/HeadingOf names is only speculation.

### GFX-WORLD-DRAW-17: Quad-index and vertex precision limits are not checked for very large worlds

- **Severity:** info · **Verdict:** unverifiable · **Category:** scaling · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/gfx/ribbon.hpp:23`

RibbonVertex positions are f32 world units from the origin, and u/v are unorm16. With a large map and big tile size, world coordinates far from the origin lose sub-pixel precision in the baked vertices. GroundUniforms::mapSize is f32 as well.

**Evidence:** MakeRibbonVertex stores `position.x/y` as f32 and the buffers are built once per world.

**Impact:** Visible jitter or wobble of rivers and roads at the far side of huge maps; river `u` quantisation is fine. Not seen on the shipped map.

**Recommendation:** Build ribbons relative to a per-chunk origin or measure at the target maximum map size (a spike).

**Verification:** Ribbon vertices are f32 world positions, which is true. Whether jitter appears depends on the target maximum map size and tile size, which only a measured spike can decide.
