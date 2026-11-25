# Review: gfx-combat-draw

**Scope:** src/gfx/seal_draw.{hpp,cpp}, volley_draw.{hpp,cpp}, cloud_draw.{hpp,cpp}, stamp_draw.{hpp,cpp}, glow_table.{hpp,cpp}. Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 20 standing (0 critical, 0 high, 1 medium, 11 low, 8 info), 0 raised but refuted on verification.

**Parts:** gfx-combat-draw.md, gfx-combat-draw-part2.md

## Assessment

## gfx-combat-draw: seal_draw, volley_draw, cloud_draw, stamp_draw, glow_table

**Strengths**
- All five modules are pure layout functions with no GPU state. Each takes its inputs, writes plain vectors and keeps its scratch buffers' capacity. That makes them testable without a device, and the tests use this well: about 2,600 lines with exact checks on geometry, times, wraps and counts.
- GPU struct layouts (`SealGpuPart/Sprite/Layer/Particles`, `GlowRecord`) are held by `static_assert`s on size and key offsets. I compared them by hand against `shaders/seal.hlsli` and `shaders/sprite.vert.hlsl`, and they match.
- Long-session precision is handled carefully. Spins are wrapped in f64 before narrowing, particle streams are sent to the GPU as wrapped ages plus a lap count, and `GlowTime` splits the tick into its low 32 bits and a fraction. Tests cover 100 hours and laps past 2^64.
- Overflow is bounded and reported: seals with their satellites (pre-counted per depth in `SealPackedLook::seals`), particles, swords (whole blade and trail), and clouds (farthest back first). Every limit has a test against the worst case the data allows.
- `DrawVolleys` writes the same bytes whatever the worker count: each chunk writes only its own slots, and swords that don't show are zeroed.
- The cloud lattice is careful: no state between frames, hash-stable per cell, a bounded explicit stack in `Descend`, and clamping driven only by the fog so a cloud never jumps.

**Weaknesses**
- `SealParticleAt` (about 100 lines in production code) is used only by tests. It is a hand-kept C++ copy of the HLSL particle placement. The particle tests check that copy, not the shader, so the two can drift apart with nothing failing except possibly a reference picture.
- `spin_scale`, which reduce motion is meant to lower, also stretches each seal's appear and fade. Lowering it below 1 makes short-lived seals dimmer and makes them fade earlier, while exactly 0 snaps to instant. The result does not change steadily with the setting, and it cuts against the "accessibility first; settings never change difficulty" rule.
- `SealLight` mixes units: pillar height, spark rise and shard drift are in world units, while widths and sizes are fractions of the seal's size. Satellites and smaller seals therefore get full-size pillars and rises, and a tiny seal's shard cycle can become shorter than a frame, which makes its shards strobe.
- Mixed-plane or stacked overhead satellites are not validated: a satellite's point already includes its parent's lift, so its shadow and draw order come out wrong.
- Health bars and bursts are placed before swords with no cap of their own, so a crowded fight can push the player's swords out of the overlay buffer.
- The clouds' zoom cross-fade draws two layers at half opacity, about 75% coverage, and zoom 0.71 holds that state indefinitely. All clouds are also laid out again every frame although only the bob changes.
- Small robustness gaps: `DrawStamps` checks its span before widening it, so an off-map span makes the size passed to `reserve` wrap round (not reachable from today's caller). Float-to-u32 casts can be undefined for degenerate sprites. `GlowRecord` uses colour 0 to mean "the element's colour", which a transparent palette colour would also produce.
- The seal tests leave several paths untested: the still path (`spin_scale` 0) through `DrawSeals`, `tinySeals`, emblem and flourish packing, upright seals drawn end to end, and depth-2 satellites.

**Overall health:** good. The code is careful, well documented and well tested for what it covers. I found no crash or undefined behaviour reachable through current callers and shipped data. The risks are design ones: scaling (the hard caps of 64 seals and 2,048 swords, already known), the reduce-motion coupling, and the duplicated C++/HLSL particle function. None of these modules is part of the simulation, so determinism is not at stake here.

**Files read in full:** src/gfx/seal_draw.hpp, src/gfx/seal_draw.cpp, src/gfx/volley_draw.hpp, src/gfx/volley_draw.cpp, src/gfx/cloud_draw.hpp, src/gfx/cloud_draw.cpp, src/gfx/stamp_draw.hpp, src/gfx/stamp_draw.cpp, src/gfx/glow_table.hpp, src/gfx/glow_table.cpp, tests/unit/gfx/seal_draw_tests.cpp, shaders/seal.hlsli (lines 1-260), src/AGENTS.md, tests/AGENTS.md

## Findings

### GFX-COMBAT-DRAW-01: spin_scale (the reduce-motion lever) also stretches seals' appear and fade: dimmer, earlier-fading seals at low settings, instant at exactly 0

- **Severity:** medium · **Verdict:** confirmed · **Category:** accessibility/design · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/seal_draw.cpp:613`

SealDrawLook multiplies the seal's time by looks.spinScale before computing life: `lifeSince = times.since * spinScale`, `lifeToEnd = times.toEnd * spinScale`. seals.yaml documents spin_scale as the setting reduce motion lowers. At spin_scale s in (0,1), a seal's appear takes appear/s seconds and its fade starts fade/s seconds before its end. At s == 0 the `still` branch switches to SEAL_FOREVER, so the seal shows whole at once. The response is therefore not monotonic: 1 → 0.1 makes appearing ten times slower, then 0 makes it instant.

**Evidence:** `const bool still = !(looks.spinScale > 0.0F); const f64 lifeSince = still ? SEAL_FOREVER : times.since * static_cast<f64>(looks.spinScale);` and the same for lifeToEnd. seals.yaml line 11: "spin_scale: scales the seals' time: spins, particle motion and life, and the appear and fade (0 to 10; reduce motion lowers it; ...)". The shipped looks use appear 0.4–1.5 s and fade 0.3–0.5 s.

**Impact:** At spin_scale 0.1, a seal that lives 3 s (for example a telegraphed formation) needs 4–15 s to appear fully, so it never reaches full opacity, and it fades from early in its life. A player who asks for reduced motion sees fainter seals, which the accessibility rule (settings never change difficulty) forbids whenever a seal carries gameplay information. The jump at exactly 0 is also surprising.

**Recommendation:** Decouple life from spin_scale. Either keep appear/fade in real seconds, or have reduce motion shorten them (min(appear, appear*s), with snapping at low s) and scale only spins and particle motion. Add a test that a seal's peak opacity under reduced motion is never below its peak at spin_scale 1.

**Verification:** seal_draw.cpp:613-615 multiplies since/toEnd by spinScale; seals.yaml line 11 says the scale covers 'life, and the appear and fade', so it is documented. The non-monotonic jump at 0 follows from the still branch. Whether the documented behaviour is wise for gameplay-carrying seals is a design question, and the facts hold. Severity medium is fair, perhaps low.

### GFX-COMBAT-DRAW-02: SealParticleAt is a production-unused C++ copy of the HLSL particle placement; tests check the copy, not the shader

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** testing/duplication · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/seal_draw.hpp:362`

SealParticleAt and its helpers (SealHash, SealUnit, SealStaggered, SealPillarAt, SealSparkAt, SealShardAt, about 100 lines) are compiled into the gfx library, but nothing in src/ calls them. Only seal_draw_tests.cpp does. The GPU runs `SealParticleAt` in shaders/seal.hlsli, which the comments say must be kept identical by hand. The tests 'particles are placed the same for the same time' and 'a spark rises on through its stream's wrap' therefore test the C++ copy.

**Evidence:** rg for SealParticleAt outside seal_draw.* finds only tests/unit/gfx/seal_draw_tests.cpp. seal_draw.hpp:355: "`SealParticleAt` in shaders/seal.hlsli is the same function, with the same constants, and the two must change together."

**Impact:** A change to the HLSL (a constant, the stagger, the lap hash, the shard axes) that is not mirrored in C++ leaves every unit test green. Only a reference picture might catch it, and only if the change moves pixels beyond tolerance in a captured scene. Meanwhile dead code ships in the game binary.

**Recommendation:** Make one source authoritative. Options: move the C++ copy to tests/support as a documented oracle, and add a GPU readback or lavapipe case that evaluates the shader's SealParticleAt for a few records and compares; or generate both from one table of constants. At minimum, cross-check the constants in lint (as is done for stale shaders).

**Verification:** SealParticleAt (seal_draw.cpp:783) is not called from production code. It is used by tests/unit/gfx/seal_draw_tests.cpp and tests/unit/app/seal_table_tests.cpp, and it is documented as the oracle that mirrors shaders/seal.hlsli. The claim that tests check the copy and not the shader is true, so a drift in the HLSL would go unnoticed. Calling it dead code is only partly right, since the test and table suites use it. It is an acknowledged duplication. Reference pictures give partial cover.

### GFX-COMBAT-DRAW-03: Overhead (or mixed-plane) satellites stack lifts: their shadow is laid at the parent's height, not on the ground

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/seal_draw.cpp:746`

SealSatelliteOf sets `moon.point = place.At(..., ring.height)` using the parent's plane and lift, so the point is already a lifted screen position. The satellite is then drawn by its own look: its own lift is added on top, and SealAddShadow lays its 'ground' shadow at `moon.point`, which is the parent's lifted point. A ground-plane satellite on an overhead parent is drawn in groundLines/groundWash (under creatures) at a mid-air screen position. The seals reader does not constrain a satellite's plane.

**Evidence:** SealAddShadow: `const SealPlace ground{ .seal = &seal, .plane = core::SealPlane::Ground, .lift = 0.0F, ... }` with seal = the moon, whose point includes the parent lift. data/seals.cpp has no plane check for satellites.

**Impact:** For an overhead seal with overhead satellites that have wash shadows, each satellite's shadow floats at the parent's height. Mixed planes give the wrong draw order (a 'ground' satellite hidden under sprites while drawn in the air). This is data-dependent and possible from seals.yaml today.

**Recommendation:** Either require a satellite's plane to match its parent's (refuse in data::LoadSeals), or carry the ground point separately in the moon (base point plus lift) so shadows and lifts compose correctly. Add a test with an overhead parent and an overhead satellite with a shadow.

**Verification:** seal_draw.cpp:746 builds moon.point with place.At(..., ring.height), a lifted position, and the moon is then drawn with its own look. data/seals.cpp has no plane check for satellites. The mixed-plane and double-lift effects follow from this and are data-dependent. I did not check whether the shipped seals.yaml triggers it, so impact depends on data.

### GFX-COMBAT-DRAW-04: SealLight mixes world units and seal-size fractions: satellites and small seals get full-size pillars and rises, and shards can strobe

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/seal_draw.cpp:433`

SealAddLight scales pillarWidth, sparkSize and shardSize by seal.size, but passes pillarHeight, sparkRise and shardDrift as world units. shardCycle = seal.size / shardDrift, so a small seal (a satellite at size 0.1 or less of a small parent) gets a cycle of milliseconds. Once the cycle is shorter than a frame, each shard is reborn at a random place every frame.

**Evidence:** `.pillarHeight = light.pillarHeight, .pillarWidth = light.pillarWidth * seal.size, .sparkRise = light.sparkRise, .sparkSize = light.sparkSize * seal.size, ... shardCycle = seal.size / shardDrift`; core/seals.hpp documents pillarHeight, sparkRise and shardDrift 'in world units'.

**Impact:** The same look used as a satellite (scaled by satellite.size) has pillars as tall as its parent's and sparks rising at the parent's speed, so proportions break. Tiny drifting shards flicker randomly every frame, which is also a flashing-accessibility concern.

**Recommendation:** Express pillar height, spark rise and shard drift as fractions of the seal's size (scaled at layout), or document and validate the intended mix. Put a lower bound on shardCycle, for example at least a few frames, or fade shards out when the cycle is shorter than a frame.

**Verification:** seal_draw.cpp:404 sets shardCycle = seal.size / shardDrift with drift in world units, so a small seal gets a short cycle. The code caps the cycle only from above (SEAL_LONGEST_CYCLE), with no lower bound. A sub-frame cycle would make shards strobe. The size scaling of pillarHeight and sparkRise in world units is as described. I did not measure whether any shipped data reaches a sub-frame cycle.

### GFX-COMBAT-DRAW-05: Health bars and bursts are placed in the overlay before the swords and are uncapped, so a crowded fight can drop the player's swords

- **Severity:** low · **Verdict:** confirmed · **Category:** design/scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/volley_draw.cpp:308`

The renderer calls DrawHudBars, DrawFlashes and DrawHealthBars before DrawVolleys. DrawVolleys gets only `SwordRoom(out.instances.size(), ...)`, the room the others leave in MAX_OVERLAY_INSTANCES (8,192). Health bars (2 instances each, one per placed label) and flashes have no cap of their own.

**Evidence:** renderer.cpp:481-484 call order; DrawVolleys: `const auto total = static_cast<u32>(std::min(all, SwordRoom(out.instances.size(), out.ribbons.size())));`

**Impact:** In a large fight (the genre is bullet-heavy, with many labelled enemies), about 3,000 placed bars plus bursts leave no room for the 1,000-sword volley, so the player's own attack vanishes while far health bars stay.

**Recommendation:** Reserve the swords' room first (place them before bars and bursts, or cap bars and bursts at MAX_OVERLAY_INSTANCES − MAX_DRAWN_SWORDS − HUD), or drop the farthest health bars first. Add a test with many bars and one full volley.

**Verification:** renderer.cpp:481-484 calls DrawHudBars, DrawFlashes and DrawHealthBars before DrawVolleys. DrawVolleys only gets the room the others leave. The overlay budget ordering is as described, but the scenario needs thousands of bars. Low severity.

### GFX-COMBAT-DRAW-06: Clouds' zoom cross-fade draws both levels at partial opacity with over-blending, so coverage drops to about 75% and zoom 0.71 holds it

- **Severity:** low · **Verdict:** confirmed · **Category:** rendering · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/cloud_draw.cpp:415`

Lay(lower, 1 - blend) and Lay(lower + 1, blend) are composited with normal premultiplied blending. Where both cover a point, the combined alpha is 1 - (1 - b)(b): 0.75 at b = 0.5, not 1. The zoom is continuous and can rest mid-step (zoom 0.71 gives level 0.5, blend 0.5), so this is not only a transition. The same dip happens in Descend's parent/child hand-off while the fog eases (parent at reach*(1-m), children at reach*m).

**Evidence:** `const f32 blend = std::clamp((level - lower - FADE_START) / FADE_SPAN, 0.0F, 1.0F); Lay(pass, view, lowerLevel, 1.0F - blend); if (blend > 0.0F) { Lay(pass, view, lowerLevel + 1, blend); }`

**Impact:** At some zooms the cloud layer looks visibly thinner (the opaque shade beneath hides the land, so this is look, not a leak), and coverage pulses while the user zooms.

**Recommendation:** Use alphas whose over-composite reaches 1, for example finer = 1 - blend and coarser = blend / (1 - (1 - blend) * blend)-style compensation, or draw the finer level at full opacity until the coarser one is complete. Or confirm the current look on purpose and record it.

**Verification:** cloud_draw.cpp:415-418 lays the lower level at 1-blend and the upper at blend with normal blending. Combined coverage is 1-(1-b)b, which is 0.75 at b=0.5. This is a look issue and not a leak. I did not find a record that it was chosen on purpose.

### GFX-COMBAT-DRAW-07: DrawStamps checks the span before widening it: an off-map span makes the reserve size wrap round and calls reserve(~4e9 × rows)

- **Severity:** low · **Verdict:** confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/stamp_draw.cpp:316`

The early return checks `span.firstColumn > span.lastColumn` on the input span. Widened() then clamps lastColumn to map.Columns() - 1 but not firstColumn. A span with first ≤ last that lies beyond the map's last column (or row) becomes first > last after widening, and `std::size_t{ span.lastColumn - span.firstColumn + 1 }` wraps as u32.

**Evidence:** `things.reserve(paintings.size() + (std::size_t{ span.lastColumn - span.firstColumn + 1 } * (span.lastRow - span.firstRow + 1)));` with `Widened` only clamping the last column/row.

**Impact:** The game cannot reach this today (DrawTerrain passes WorldMap::TilesIn, which clamps). Any other caller, or a future world swap where the span is computed against the old map, gets std::length_error/bad_alloc → terminate. The public header documents only 'Does nothing for an empty span'.

**Recommendation:** Run the empty-span check after Widened(), or clamp firstColumn/firstRow to the map too. Add a test with a span entirely past the map.

**Verification:** stamp_draw.cpp:316 checks emptiness before Widened (line 168). Widened clamps only lastColumn and lastRow. A span entirely past the map gives first > last, and the u32 subtraction in the reserve at line 316-318 wraps. Nothing in the game reaches it today, since TilesIn clamps, so this is a robustness issue only.

### GFX-COMBAT-DRAW-08: Stamp layout scans every place on the map and checks every stamp against every painting, on every relayout (every frame while zooming)

- **Severity:** low · **Verdict:** confirmed · **Category:** performance/scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/stamp_draw.cpp:181`

Paintings() iterates `pass.map->Places()` in full to find those near the span. Covered() is a linear any_of over the paintings for each stamp. The terrain is laid out again whenever the zoom changes, so this runs every frame during zoom animations. StampReach and LargestHalf also recompute per call from the look and the manifest, though they only change with SetWorld.

**Evidence:** `for (const core::Place& place : pass.map->Places())`; `std::ranges::any_of(paintings, ...)` per tile.

**Impact:** Procedural maps scale with seed and size. With thousands of places, each relayout costs O(places + tiles × nearby paintings) on the main thread, against the 'avoid wasted work' rule.

**Recommendation:** Index places by tile or region once per SetWorld (WorldMap may already map tiles to places), and cache StampReach/LargestHalf per (look, manifest). Check painting coverage per tile through the index.

**Verification:** Paintings() (stamp_draw.cpp ~181) iterates all map->Places() per layout, and Covered() is a linear any_of. StampReach/LargestHalf are recomputed per pass. The cost claim is true by reading. Whether it runs every frame during zoom, and how large it is, was not measured. The repo's own rule says to measure, so this is a spike candidate, not a defect.

### GFX-COMBAT-DRAW-09: Clouds are laid out, clamped and sorted from scratch every frame, though only the bob changes between frames

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/cloud_draw.cpp:389`

DrawClouds runs every frame (frameClock changes). It walks every cell, works out SpreadOf/MostExplored strips for every cloud, clamps, builds instances and sorts the whole list. Positions, scales and order depend only on fog values, view and zoom. The bob (and the drift while easing) are the only per-frame changes.

**Evidence:** Pass carries `bobTurn` computed from `seconds`; everything else is a pure function of fog/view/zoom.

**Impact:** Repeated main-thread work every frame on the world map (about 1–3k clouds and a sort), against the 'compute only what changes' guidance. It is cheap today, but it adds up with other per-frame layouts.

**Recommendation:** Cache the laid, sorted clouds keyed on (fog revision, cell range, level, blend), and each frame apply only the bob offset (or do the bob in the shader from a per-instance phase).

**Verification:** DrawClouds does re-lay and sort every frame, as stated. Per-frame cost was not measured. The 'avoid wasted work' rule favours caching, but the bob is baked into positions, so a cache needs a design change. Treat it as an optimisation candidate.

### GFX-COMBAT-DRAW-10: Seal tests miss several paths: still (spin_scale 0) drawing, tinySeals, emblem/flourish packing, upright seals drawn end to end, depth-2 satellites

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/gfx/seal_draw_tests.cpp:182`

Only SealLayerAngle is tested at spin_scale 0. Nothing tests DrawSeals' `still` path (seal whole from start to end), SealDrawing::tinySeals, SealPackEmblem's emblemReach, a flourish's spill in SealPackedLayer::spill or in the band, an upright seal drawn through DrawSeals (aboveLines/aboveParticles, pillarFall -1), or a satellite of a satellite (depth 2, salts differing).

**Evidence:** grep for tinySeals, emblemReach, Flourish, spill and Upright in DrawSeals tests finds none (Upright appears only in SealToWorld cases).

**Impact:** Regressions in these paths, such as the satellite shadow and lift issue above, the emblem's disc cutting its field, or the still path's infinity arithmetic, would pass the unit suite.

**Recommendation:** Add a small case for each, as tests/AGENTS.md asks: a named sample for each path.

**Verification:** seal_draw_tests.cpp has no tinySeals match. 'still' appears only as a comment, with SealLayerAngle tested at spin_scale 0 (line ~186). Upright appears only in SealToWorld cases (lines 148-151). I did not check emblem/flourish/depth-2 coverage exhaustively. The claim of a gap in DrawSeals end-to-end coverage looks right, but its force is a judgment call.
