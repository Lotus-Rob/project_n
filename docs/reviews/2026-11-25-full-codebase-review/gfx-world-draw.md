# Review: gfx-world-draw

**Scope:** every top-level file in src/gfx/ not covered by these reviewers: renderer, uploads, image, camera, labels, text_layout, text_place, distance_field, shelf_packer, hud_draw, seal_draw, volley_draw, cloud_draw, stamp_draw, glow_table. I.e. river_draw, terrain_draw, road_draw, route_draw, ribbon*, ground*, fog, sprite_block, mesh_vertex and the rest (ls the directory). Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 17 standing (0 critical, 0 high, 1 medium, 11 low, 5 info), 0 raised but refuted on verification.

**Parts:** gfx-world-draw.md, gfx-world-draw-part2.md

## Assessment

## gfx-world-draw assessment (scope: river_draw, terrain_draw, road_draw, route_draw, ribbon*, ground*, fog, sprite_block, mesh_vertex, colour, tile_hash, module.dox, plus their unit tests)

**Strengths**
- The files are small, pure and device-free, and the headers say what each function promises, including edge cases. The `static_assert`s on the `GroundUniforms` and `FogUniforms` row layouts are good protection against drift from the HLSL.
- The rivers, ground field, fog, sprite block, route and ribbon-band tests cover many edge cases: map-edge water, branches, springs, buffer overflow and shape rounding. Several tests read the shipped map's real rivers and roads, which keeps the engine limits honest.
- The CPU-side fog sampling matches the GPU's linear filter with clamp-to-edge. Terrain is cached by `TerrainKey` (span, zoom, version), and the rivers, roads and ground field are built once per world version. That follows the repo's "avoid wasted work" rule in the main places.

**Weaknesses and risks**
- **Scaling to large procedural maps.** The fixed limits are 8,192 quads each for rivers and roads, and 8,192 terrain instances. The loops that would hit them are not bounded by the view, so beyond the limit rivers and roads are dropped by index rather than by visibility. Several steps are also quadratic or worse in map size: `GroundTexels` is 121 probes per tile, `FindJoin` is O(rivers × total points), and `HeadingNear` scans every road point per bridge.
- **Per-frame work.** `AddMarkers` walks every place on the map, comparing strings, whenever the span changes. `DrawRoute` re-lays and re-uploads the route every frame even when it has not changed.
- **Missing preconditions.** `WriteBlock` and `BlockOf` have no guards for zero-size sprites. `FogSample` has no guard for NaN. `FogSample` also treats a short values array as fully explored, which would hide a size mismatch.
- **Comments and naming.**
  - `tile_hash.hpp` has history-citing comments, which the repo's comment rule forbids.
  - Generic names like `Length` and `HeadingOf` sit in the public `gfx` namespace.
  - `near` and `far` are parameter names that Windows headers `#define`.
- **Test blind spots.** The tests would not catch the drawn-vs-stored midline mismatch or the cap-ordering behaviour. There is no scale or timing test for `GroundTexels`, `HeadingNear` or `FindJoin`. There is no test that decor varies with the world seed. The boundary behaviour of `DrawRoute` when `into` is already filled is also untested.

**Overall health:** good. I found no crash or UB in normal use. The main risks are scaling cliffs for very large procedural maps, which are silent apart from a log warning, and a few unguarded preconditions. Nothing here affects simulation determinism, since this scope is presentation only.

**Files read in full:** (not reported)

## Findings

### GFX-WORLD-DRAW-01: Fixed 8,192-quad river/road buffers silently drop later rivers and roads on large procedural maps

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/river_draw.cpp:150`

DrawRivers and DrawRoads lay out every river and road of the whole map and stop at a fixed quad count (MAX_RIVER_QUADS / MAX_ROAD_QUADS = 8192). The rest are dropped whole, later index first, with only a log warning. The limit is a compile-time constant and is not tied to map size or view.

**Evidence:** `if (quads + taken > MAX_RIVER_QUADS) { PN_LOG_WARNING(...); break; }` in river_draw.cpp, and the same in road_draw.cpp. A test only checks that the shipped test map fits. Each river costs 2*(steps)*... quads at 4 steps per tile per band, so about 8 quads per tile of river, and 8192 quads cover roughly 1000 tiles of river or road in total.

**Impact:** The maps are procedural and may be very large. Past roughly 1000 tiles of river or road, the highest-index ones vanish from the map, so the world has missing roads or rivers while the simulation still treats them as present. Which ones vanish depends on generation order, not on what is visible.

**Recommendation:** Size the buffers from the map (or chunk them), or cull ribbons to the view and upload per view. At minimum make this a generation-time limit that the map generator checks, and add a test with a large generated map.

**Verification:** The cap is real: both loops break at 8192 quads and drop later rivers or roads whole (river_draw.cpp:150, road_draw.cpp:21). It is not silent, since PN_LOG_WARNING fires. The headers document it and tests cover it (river_draw_tests:435, road_draw_tests:49). The terrain spec records the 8192 value as a user-accepted choice (2026-11-21). It also notes the shipped map uses 42% of the cap. Nothing ties the cap to map size, and memory says maps are procedural and scale, so the scaling risk is genuine. Drawn rivers also never reach the simulation's state, so the 'simulation still treats them as present' impact holds. I rate it medium because nothing yet produces maps that large. Recommendation stands: a generation-time check or a sized buffer.

### GFX-WORLD-DRAW-02: GroundTexels is O(tiles x 121) on the main thread with no early-out

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/ground_field.cpp:19`

Every tile probes an 11x11 window for the nearest tile of the other wetness. There is no early-out when the whole window is one wetness and no separable distance transform. It uses bounds-checked `.at()` in the inner loop.

**Evidence:** for-loops over otherRow/otherColumn in ShoreTiles, called once per tile from GroundTexels, invoked from internal/features/ground.cpp:146 on a world change.

**Impact:** A 4096x4096 map is about 16.7M tiles, so about 2 billion probes (several seconds of hitch) on every new map or look. Also a texture of one texel per tile will hit the GPU's maximum texture dimension on very large maps; nothing checks it here.

**Recommendation:** Skip tiles whose window cannot contain the other wetness (a quick pre-pass or a summed-area table), or use a two-pass chamfer or exact EDT. Parallelise per row. Validate map size against the device's texture limit.

**Verification:** The code matches: an 11x11 window per tile, no early-out, bounds-checked .at(). The 4096x4096 estimate is hypothetical because no map that size exists. The cost runs only on a world or look change, not per frame. The texture-limit point is plausible, but I did not find a check here. This is a scaling note rather than a present defect.

### GFX-WORLD-DRAW-03: Bridge orientation scans every road curve point per bridge tile

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/terrain_draw.cpp:58`

HeadingNear iterates all points of all road curves of the map for each bridge tile in view, and DrawTerrain re-runs it whenever the tile span or zoom changes (every tile-boundary crossing while panning).

**Evidence:** `for (const std::vector<core::Vec2>& curve : curves) for (index...)` inside AddCrossings's ForTiles lambda; called in UploadTerrain on key change.

**Impact:** With N total curve points and B bridges in view the cost is B*N on a path the camera triggers. On a big procedural map this is millions of operations per tile crossing, causing a hitch while panning.

**Recommendation:** Precompute each bridge tile's orientation once per world version (core::RoadCurveIndex already indexes curves per tile), or use a spatial grid of curve points. Also note the nearest point has no radius limit, so an unrelated far road can decide the orientation when none is near.

**Verification:** HeadingNear (lines 58-71) scans every point of every road curve for each bridge tile in view. It is O(B*N) and has no radius limit. It reruns whenever DrawTerrain is called, which the finding says happens on span or zoom change. The cost is only felt on large maps, so low. The unbounded nearest point is a minor correctness oddity.

### GFX-WORLD-DRAW-04: AddMarkers walks every place and string-compares sort per redraw instead of the tiles in view

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/terrain_draw.cpp:95`

All of map.Places() is iterated and range-tested against the span, and each danger place does a linear `std::ranges::find` by std::string over dangerMarkers.

**Evidence:** `for (const core::Place& place : pass.map->Places())` then `std::ranges::find(pass.look->dangerMarkers, place.sort, &std::pair<std::string, SpriteId>::first)`.

**Impact:** O(places) per terrain redraw, and string compares for visible places. Small for a test map, wasteful for a map with tens of thousands of places.

**Recommendation:** Index places by tile or sector, and resolve each sort to a marker sprite once per look.

**Verification:** AddMarkers iterates all Places() and does a linear string find in dangerMarkers for each visible danger place (lines 95-115). Both facts hold. It is redraw-only and cheap for current maps. A real but minor scaling cost.

### GFX-WORLD-DRAW-05: DrawTerrain does not enforce MAX_TERRAIN_INSTANCES; overflow silently drops markers last

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/terrain_draw.cpp:113`

The constant is documented as the buffer's limit, but DrawTerrain pushes without a cap and relies on the upload to truncate. Batches and `markersFrom` still describe the full list.

**Evidence:** MAX_TERRAIN_INSTANCES is used only for buffer creation and ReportOverflow in renderer.cpp, and the one test uses the shipped map. The batches are drawn from terrainDrawing.batches after Upload truncated the instances.

**Impact:** On a dense view the trailing instances (the place markers, the most important) are cut first, and batches may name instances the buffer never received. Depending on how SpriteRenderer::Draw clamps, this reads stale data or the draw is clipped.

**Recommendation:** Cap in DrawTerrain (reserve room for markers, drop stamps first) and clamp the batches; test with a synthetic dense map.

**Verification:** DrawTerrain applies no cap, true. But the claim of stale reads or unclamped draws is wrong. The sprite renderer's Draw (features/sprites.cpp:426-435) skips batches with first >= m_count and clamps count to m_count - first. The upload truncates to capacity, and renderer.cpp:528 calls ReportOverflow, so the overflow is not silent. The view is bounded by the widest view, per the header comment. Markers are drawn last, so they are the first cut, as stated. It is a design trade-off, not a bug.

### GFX-WORLD-DRAW-06: DrawRoute's cap counts into.instances.size() absolutely, contradicting 'appends to into'

- **Severity:** low · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/route_draw.cpp:23`

The documented behaviour is that DrawRoute appends to `into`. AddDots stops when `into.instances.size() >= room`, an absolute size, rather than counting dots added since `first`. The end marker is always added regardless.

**Evidence:** `for (...; into.instances.size() < room)`; `Add(look.marker, ...)` unconditional; the renderer happens to clear the buffer first.

**Impact:** Latent bug: if the function is called on a non-empty drawing (as the doc allows), dots are cut early or the marker pushes the total past MAX_ROUTE_INSTANCES.

**Recommendation:** Count relative to `first`, or document and assert that `into.instances` is empty.

**Verification:** AddDots tests into.instances.size() < room as an absolute size (route_draw.cpp:23,31,36), and the marker is added unconditionally. This does contradict an 'appends' reading when into is non-empty. The only caller (renderer.cpp:~680) clears first, and the 'longer than buffer' test passes an empty drawing. The bug is latent and has no present impact.

### GFX-WORLD-DRAW-07: The route is re-laid and re-uploaded every frame even when unchanged

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/renderer.cpp:677`

UploadRoute calls DrawRoute and DrawTargetTile and uploads each frame regardless of whether route, spacing, look or cursor changed. DrawRoute runs hypot per segment over the whole route and may write up to 4096 instances.

**Evidence:** routeDrawing.instances.clear(); DrawRoute(shown.snapshot->route, ...); unconditional, unlike UploadTerrain's key check. The comment says 'laid out again each frame'.

**Impact:** Wasted per-frame CPU and transfer bandwidth with an unchanged route, against the repo's avoid-wasted-work rule.

**Recommendation:** Key on the route's identity or revision and the world version, as terrain does. Keep the target tile per frame since the cursor moves.

**Verification:** UploadRoute clears and re-lays the route and target every frame, with no key check, unlike terrain. DrawRoute is capped at 4096 instances. The cost is small, but it does cut against the avoid-wasted-work memory. I did not find a record of this as deliberate. Low.

### GFX-WORLD-DRAW-08: River joins are O(rivers x all points) per end

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/river_draw.cpp:79`

StartOf (JoinedHead) and the end (JoinedEnd) each scan every other river's midline segments. StartOf is also computed in RiverStarts for all rivers. For R rivers this is quadratic in map size. Midlines of all rivers are built up front, even those later dropped by the quad cap.

**Evidence:** core::FindJoin loops `for other in midlines` over all points (core/road_curve.cpp). The `reversed` vector is copied per river.

**Impact:** A world-load hitch that grows quadratically with river count on very large maps. RoadCurves has the same shape.

**Recommendation:** Add a spatial grid or bounding-box rejection in FindJoin. Avoid the per-river `reversed` copy.

**Verification:** FindJoin (core/road_curve.cpp:75-100) does loop every other midline's points per end, and StartOf is computed twice in tests only (RiverStarts). The scan is O(R x points), so quadratic overall, but it's a one-off world-load cost with no measurement. The `reversed` copy is small. Unmeasured, so low.

### GFX-WORLD-DRAW-09: WriteBlock and BlockOf have unchecked preconditions (zero-size sprites, buffer sizes)

- **Severity:** low · **Verdict:** confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/gfx/sprite_block.cpp:24`

`lastLine = sprite.height - 1` underflows for a zero-height sprite, and `source.first(4)` / `last(4)` are out of range for a zero width. There is no check that `destination` is exactly the block's size or that `pixels` is long enough. The manifest reader does not appear to reject a zero-size region.

**Evidence:** data/manifest.cpp validates edge/page bounds and foot but not width/height > 0. WriteBlock then uses `subspan` with computed offsets without checks.

**Impact:** A hand-edited or corrupt manifest with a 0-size sprite leads to out-of-range spans (UB without hardened libstdc++) or a huge loop at load.

**Recommendation:** Reject zero width or height in the manifest reader, and add PN_ASSERTs in WriteBlock on destination.size() and pixels.size().

**Verification:** WriteBlock computes `sprite.height - 1` unsigned and calls first(4)/last(4) with no zero-size guard. data/manifest.cpp:85-101 checks bounds, page and foot, but not width or height > 0. The input is a committed generated manifest, so the realistic impact is low.

### GFX-WORLD-DRAW-10: Fog texels missing from `values` read as fully explored

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/gfx/fog.cpp:24`

Texel() returns 1.0 (explored and in sight) when the index is outside `values`, so a FogFrame whose `values` is shorter than 2*columns*rows hides the fog rather than failing. FogSample on NaN `world` casts NaN to u32.

**Evidence:** `return index < fog.values.size() ? ... : 1.0F;`; `static_cast<u32>(left)` where left = floor(clamp(NaN)).

**Impact:** A size mismatch between the fog producer and the map gives no fog with no diagnostic, and reveals the whole map. NaN input is UB (an unspecified cast).

**Recommendation:** Assert values.size() == 2*columns*rows at frame entry. Guard non-finite input.

**Verification:** Texel does return 1.0F for an index past values.size(), and FogSample does cast floor(clamp(NaN)) to u32. Both are as stated. The 1.0 fallback looks like a chosen fail-open and the callers feed finite input, so it is a robustness nit and not a live bug.
