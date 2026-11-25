# Review: gap2-coordinate-spaces-and-picking

**Scope:** Trace every coordinate space end to end and check that conversions agree across modules: tile indices and tile size (core::WorldMap, Npc::cell), world units (core::Vec2, f32), sprite anchors and feet (manifest footY, sprite_block, instances.cpp MakeInstance, lifts and heights in seal_draw and combat's HeightOf), camera space (gfx/camera.hpp, app/camera_control.cpp, camera_path), target pixels and window coordinates (platform/window.cpp, hud.cpp, labels' screen anchors) and the shader transforms (shaders/sprite.vert.hlsl, ground, ribbons, seal, mesh). Check specifically that what the player clicks or hovers is what is drawn there. app/view.cpp PickInstance and Pick (alpha interpolation, fog, dissolve, rotation, lifted or shifted sprites such as the cultivator drawn shifted onto the road), click.cpp's WalkTo world point, target_cursor's tile, and the camera bounds and zoom limits should all agree with the vertex shader's placement and the y-sort. Also check rounding at tile edges (floor versus truncation for negative coordinates), y-axis direction consistency, and behaviour at the map edge. Earlier units already found: no HiDPI (window size used as pixel size), zoom centred on the screen not the cursor, hover pick scanning every instance (performance), the camera following the body while the sprite is shifted, CellAt's unbounded float to u32 cast, and the labels' NaN cast. Don't repeat those. Look for mismatches between modules that a per-file review couldn't see. (Why this unit exists: Each module reviewer checked its own maths. Nobody verified that the CPU-side picking, click-to-walk, the target cursor and the camera use the same transform as the shaders and the draw code. That seam spans core, app, gfx and HLSL, and a mismatch there shows up as misclicks and wrong selections that no unit test (all per module) would catch.)

**Findings:** 16 standing (0 critical, 0 high, 2 medium, 11 low, 3 info), 0 raised but refuted on verification.

**Parts:** gap2-coordinate-spaces-and-picking.md, gap2-coordinate-spaces-and-picking-part2.md

## Assessment

## Coordinate spaces and picking: the seam across core, app, gfx and HLSL

**Strengths.** The main transform has a single source. `gfx::WorldToClip` / `ClipToWorld` (camera.hpp) feeds every vertex shader (sprite, ground, river/ribbon, seal, seal_light) as `clip = world * xy + zw`, and the CPU side uses the same struct. That covers `app::WindowToWorld`, `CullArea`, the renderer's `ViewRect`, `PixelOf` for labels, and `ScreenBoxOf`. I checked these against each other:
- **Y axis.** World y points down, clip y points up and pixel y points down. Every module agrees: WindowToWorld uses `1 - 2y/h` and PixelOf uses `(1 - clipY)/2*h`.
- **Rotation.** The shader rotates clockwise: `turned = (ox*c - oy*s, ox*s + oy*c)`. `PickInstance` inverts it correctly (`along = (dx*c + dy*s, dy*c - dx*s)`). Both read the same packed f16 halves and use the same lerp form (`a + (b-a)*t`).
- **Mirroring.** A mirrored sprite (negative width) is handled the same way in MakeInstance, the shader, PickInstance (abs) and ScreenBoxOf (abs).
- **Sight point and fog.** `SightPoint` matches the shader's `sightPoint`. The CPU `SightFade` matches the shader's `smoothstep(0.5, 1, FogAt().y)`, and `FogSample` reproduces the GPU's clamp-to-edge bilinear filter with texel centres half a tile in.
- **Tile indices.** `TileAt`, `TileCentre`, `TilesIn`, `TileExplored` and the ground shader all use `floor(world / tileSize)`, never truncation. The negative and NaN cases are clamped before the cast, so tile edges are consistent.
- **Draw order.** The y-sort (`QuantiseDepth` relative to the view top, stable radix sort) gives the instance array order. PickInstance scans that array from the end, so "picked" means "drawn on top". The player's on-top sort on the world map holds for picking too.
- **Shifted and dying sprites.** The road-shifted cultivator and travellers are picked where they are drawn, because picking reads the instance and not the body. Dying entities publish no ID, so a dissolving sprite can't be picked.

**Weaknesses.** Most of the problems sit between modules rather than in one formula:
1. **No camera bounds anywhere.** The camera pans without limit. Together with f32 world coordinates and map limits up to 2^24 tiles of up to 10^6 units, precision on the far side of a large procedural map is a real scaling problem. Nothing rebases the coordinates around the camera.
2. **Keyboard and gamepad parity.** The target cursor can be steered off screen and the camera never follows it. Its confirm can only walk, so selecting an entity is mouse-only. That falls short of the project's accessibility-first rule.
3. **Logical and drawn positions are mixed.** Publishing culls travellers by their logical foot and tile, while drawing, fading and picking use the road-shifted point. The fog's sight centre, the target cursor's start tile and the camera all use the body, while the player sees the shifted sprite. These are small offsets, usually under a tile, but they are systematic.
4. **Small mismatches between what is pickable and what shows.** With the debug sight on, picking ignores the cloud shade that the label fade respects. Additive light orbs and props carry IDs, so they steal hover and clicks. A click uses the alpha at input-handling time rather than the alpha of the picture on screen.
5. **Content geometry is hard-wired to the shipped art.** The cursor outline, markers and stamps are 256-unit sprites drawn at scale 1, while `tile_size` is data accepted from 1 to 10^6. Two different ground-squash ratios coexist (VIEW_TILT 0.6 against the seal and shadow ratio 0.375).
6. **Tests stay inside one module.** The rotation pick test uses a symmetric quarter turn, so it can't catch a sign error. No test builds an instance with `MakeInstance` (foot offset, widthScale, rotation) and checks that `PickInstance` hits the pixels the vertex shader's formula covers. The seam this unit exists for has no test.

**Overall health.** Good. The core transform maths is consistent and well documented, and none of the findings is a crash or a desync. The real risks are scaling, since maps are meant to grow and procedural maps leave the test map's comfortable 64k-unit range, and input parity. The rest are polish-level mismatches that will matter more as the UI grows, for example once labels or HUD elements become clickable.

The scope's other leads come out clean. The click's WalkTo point, `TileAt`, target-cursor tiles and the cursor outline all agree for `tile_size` 256. `--zoom` and the camera path are clamped to MIN_ZOOM and MAX_ZOOM. Hover and draw use the same fog frame and alpha. There is exactly one ID-carrying instance per entity, so `selected` (first match) and pick (last match) agree.

**Files read in full:** src/app/view.cpp, src/app/view.hpp, src/app/click.cpp, src/app/click.hpp, src/app/target_cursor.cpp, src/app/target_cursor.hpp, src/app/camera_control.cpp, src/app/camera_control.hpp, src/gfx/camera.cpp, src/gfx/camera.hpp, src/core/view_limits.hpp, src/core/instances.cpp, src/core/instances.hpp, src/gfx/fog.cpp, src/gfx/fog.hpp, src/gfx/route_draw.cpp, src/gfx/route_draw.hpp, shaders/sprite.vert.hlsl, shaders/sprite.frag.hlsl, shaders/sprite.hlsli, shaders/fog.hlsli, shaders/ground.vert.hlsl, shaders/river.vert.hlsl, shaders/mesh.vert.hlsl, shaders/seal.vert.hlsl, shaders/seal_light.vert.hlsl, tests/unit/app/view_tests.cpp (lines 1-112), src/app/app.cpp (lines 480-907), src/game/host.cpp (WalkTo, Move, FollowRoute, DrawnPlayer, ShiftDrawnPlayer, PublishOverhead, PublishJourneys, PublishRoute, Publish), src/gfx/text_place.cpp (lines 1-260), src/gfx/renderer.cpp (ViewRect, UploadTerrain, DrawWorld), src/platform/window.cpp (Poll, Cursor), src/core/frame.hpp (Instance, Snapshot, PresentFrame), src/core/tick_clock.hpp (Alpha), src/core/draw_list.cpp/.hpp (Add, QuantiseDepth), src/core/world_map.cpp (TileOf, TileAt, TileCentre, TilesIn), src/core/fog.cpp, src/game/npcs.cpp (CellAt, CellCentre), src/game/combat.cpp (HeightOf, ChestOf, Tag, Launch, Lance), src/game/components.cpp/.hpp (HeadOf, VIEW_TILT), src/game/scenes/world.cpp, src/game/scenes/arena_layout.cpp (lines 90-140), src/gfx/seal_draw.cpp (SealToWorld), src/gfx/labels.cpp (FogOpacity), src/app/camera_path.cpp

## Findings

### GAP2-COORDINATE-SPACES-AND-PICKING-01: Target cursor can be steered off screen; the camera never follows it

- **Severity:** medium · **Verdict:** confirmed · **Category:** accessibility/input · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/target_cursor.cpp:53`

TargetCursor::Steer moves the tile anywhere on the map (clamped only at the map's edge). App::SteerCursor only tells the renderer where to draw the outline. CameraInput has no field for the cursor, and CameraControl::Update never centres on or follows the cursor tile. While the cursor shows, Move feeds the cursor (TargetCursor::Walk returns zero), and in the Roam world scene Move doesn't pan anyway. A keyboard or gamepad player who steers more than about half a screen away loses sight of the cursor. They then have to pan separately (IJKL or the right stick), which frees the camera from the cultivator.

**Evidence:** App::SteerCursor: `m_cursor.Steer(m_bindings.Move(), seconds); m_renderer.SetTargetTile(map->TileCentre(...));`. MoveCamera passes `.move = m_held.move` (zero while the cursor shows) and nothing about the cursor. The 5b2 spec item 15 is silent on keeping it in view.

**Impact:** The keyboard and gamepad route to click-to-walk becomes blind past the first screen. This conflicts with the accessibility-first rule that every option works fluidly by keyboard and gamepad.

**Recommendation:** Feed the cursor tile's centre into CameraInput (e.g. `cursorTarget`). Have the Roam camera keep it inside the follow box (FollowedWithin with the cursor instead of the foot) while the cursor shows, and snap back on hide. Add a camera_control test.

**Verification:** TargetCursor has no link to the camera. CameraInput has no cursor field, and I found no follow of the cursor tile. The accessibility-first rule makes this a real gap for keyboard and gamepad. I did not check whether a spec records it as deferred.

### GAP2-COORDINATE-SPACES-AND-PICKING-02: Selecting an entity is mouse-only: the target cursor's Confirm always walks

- **Severity:** medium · **Verdict:** confirmed · **Category:** accessibility/input · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/target_cursor.cpp:90`

A left click picks (PickInstance) and sends Select when something is under the pointer, else WalkTo (ClickAction). The keyboard and gamepad equivalent, TargetCursor::Confirm, always sends WalkTo to the tile centre. It never picks a traveller standing on the tile, and Host::WalkTo then *lets go of* any selection. No other keyboard or gamepad path selects or inspects an entity, and hover is suppressed for gamepads (App::Hovered).

**Evidence:** `core::Action action{ .kind = core::ActionKind::WalkTo, .point = map.TileCentre(*m_tile) };`. Host::WalkTo: `if (m_selected != core::StableId{}) { ... m_selected = {}; }`.

**Impact:** Mouse and keyboard/gamepad diverge as selection gains uses (chunk 9's inspect profile, travel journals). A gamepad player can't select the travellers that a mouse user can, which breaks the project's mouse/keyboard/gamepad parity rule.

**Recommendation:** Design the keyboard/gamepad selection path now. For example, Confirm selects the topmost pickable entity on or near the cursor tile (reuse PickInstance at the tile centre, or a 'cycle entities on tile' control), with a separate control or a second confirm for walking. Record the decision in the spec.

**Verification:** target_cursor.cpp:94 always builds a WalkTo to the tile centre and does no pick. A mouse click uses ClickAction, which selects first. The parity gap is real. It matters mostly as selection gains uses, so medium is slightly high but defensible given the accessibility-first priority.

### GAP2-COORDINATE-SPACES-AND-PICKING-03: Camera has no bounds: pans without limit off the map and the scene in every Pan/Roam scene

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** design/scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/camera_control.cpp:205`

CameraControl::Update moves `m_camera.centre` by pan, edge pan and drag with no clamp to the world map's area (MapArea / Scene::View) or any scene rectangle. No limit exists anywhere in the camera chain: grep of camera_control.cpp, camera_path.cpp and gfx/camera.cpp finds only the zoom clamp, and docs/design 0002 §436 records only the zoom range. CullArea, the renderer's ViewRect, TilesIn and FogSample all clamp or cope off the map, so the draw survives. But the player can scroll at 1 screen/s into empty backdrop indefinitely. There, a click silently does nothing: Host::WalkTo logs 'off the map is ignored' at debug only, and there is nothing to pick.

**Evidence:** `m_camera.centre = m_camera.centre + (pan * (PAN_SCREENS_PER_SECOND * ScreenOf(m_camera, input) * seconds)) + dragged;`: no clamp follows. The world scene is Roam, so any pan frees the camera.

**Impact:** The player can lose the map entirely (holding a pan key for a few seconds at zoom 0.25 moves ~30k units). Clicks in the void give no feedback. Combined with f32 coordinates, unbounded panning eventually reaches magnitudes where positions and picking degrade.

**Recommendation:** Clamp the camera centre so the view keeps at least part of the scene's area (Snapshot or SceneTraits view / map rect) on screen. For example, clamp the centre to the map rect grown by half the view, applied after zoom and after pan/drag. Test it in camera_control_tests. Consider a visual or log cue for a WalkTo off the map.

**Verification:** camera_control.cpp has only zoom and frame-step clamps (lines 172-204) and the follow-box clamp at line 66. Nothing limits the pan centre, and no doc or backlog entry records unbounded pan as a decision. Real, but a UX gap rather than a defect. Medium is too high, since there is no crash and the cultivator can be re-followed.

### GAP2-COORDINATE-SPACES-AND-PICKING-04: World positions are f32 everywhere (snapshot, shader, picking) while the map limits allow coordinates where f32 breaks down

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** scaling/precision · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/world_map.cpp:16`

MAX_TILES = 2^24 with any aspect (columns*rows <= 2^24), and tile_size is accepted from 1 to 1,000,000 (data/world_map.cpp:21-22). Instance positions, the follow point, WalkTo points, the camera centre and the shader transform are all f32 world units. The transform is applied as `world * scale + offset` with `offset = -centre * scale`, so the GPU computes a difference of two large numbers. Already at a 4096x4096-tile map of 256-unit tiles (1,048,576 units) the f32 ulp is 0.125 units. At zoom 2 on a 4K target (4 px/unit) sprites snap to 0.5 px steps. The clip-space cancellation adds about another 0.5 px (world*scale ~ 2184, ulp 2.4e-4 clip units, ~0.47 px). A 16384x1024 map at the same tile size reaches 4.2M units: 0.5-unit ulp and ~2 px jitter. A 2^24x1 map reaches 4.3e9 units, where everything breaks. Picking inverts the same transform (ClipToWorld) and inherits the error. QuantiseDepth's `footY - viewTop` and SightPoint lose precision likewise.

**Evidence:** `ClipTransform WorldToClip(...) { ... .offsetX = -camera.centre.x * scaleX, .offsetY = -camera.centre.y * scaleY }`. sprite.vert.hlsl:200 `output.position = float4(world * draw.worldToClip.xy + draw.worldToClip.zw, 0, 1);`. `constexpr u32 MAX_TILES = 1U << 24;`. `MOST_TILE_SIZE = 1'000'000.0`.

**Impact:** On the large procedural maps the project targets, sprites, roads, text and picking jitter or crawl far from the origin. The shimmer measure only runs over the test map near the origin, so it won't catch this. Misclicks grow with distance from the origin.

**Recommendation:** Make the transform camera-relative. Subtract the camera centre on the CPU (ideally in f64) before upload, or upload instance positions relative to a per-snapshot origin near the view (the publish already culls to the view), and have the shader apply only scale. Alternatively, bound the map's world extent (columns*tileSize, rows*tileSize <= ~2^20) at load and document the limit. Add a test that picks an instance at a far map coordinate.

**Verification:** MAX_TILES = 1<<24 (world_map.cpp:16) and MOST_TILE_SIZE = 1e6 (data/world_map.cpp:22) are real, and the f32 transform is as described. Extreme extents are legal but unrealistic. The precision numbers are plausible but I did not measure them. Practical maps are far smaller, so the limit is theoretical today. Worth bounding the extent or going camera-relative before large procedural maps.

### GAP2-COORDINATE-SPACES-AND-PICKING-05: Travellers are culled and sight-tested by their logical foot but drawn, faded and picked at the road-shifted point

- **Severity:** low · **Verdict:** unverifiable · **Category:** cross-module mismatch · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:1300`

PublishJourneys decides whether to publish a traveller by `core::Contains(area, foot)` and `core::InSight(map, playerTile, map.TileAt(foot), sightRadius + 1)`, where `foot` comes from core::Where. The instance is then placed at core::DrawnWhere, which may lie up to ROAD_DRAWN_REACH (1.5 tiles) away on a road's curve. Its opacity comes from the shader's SIGHT_FADE at the drawn sight point, and PickInstance tests the same drawn point. The +1 tile of sight margin covers the fade, not the shift. A traveller whose logical tile is at radius+2 but which is drawn up to 1.5 tiles closer can sit where the fog shows full sight, yet it isn't published. It pops in at full opacity when its logical tile crosses radius+1. Likewise, the area test has no margin for the shift: at zoom 2 the CULL_MARGIN of 0.25 is only ~240 units, less than 384.

**Evidence:** `const core::Vec2 foot = core::Where(map, journey, now, member); if (!core::Contains(area, foot) || (!m_debugSight && !core::InSight(map, playerTile.value_or(0), map.TileAt(foot), shown)))`, then `const core::Vec2 current = core::DrawnWhere(...)`. Host::PlayerTile also uses the body, so the sight diamond is centred on the body's tile while the cultivator is drawn shifted.

**Impact:** Occasional pop-in of walkers on roads at the edge of sight, and rarely at the view's edge at high zoom. The fog's sight centre and the target cursor's start tile (CursorStart uses follow.current, the body) sit off the drawn cultivator by up to a tile.

**Recommendation:** Test sight and area at the drawn point too, or widen `shown` and the area test by ceil(ROAD_DRAWN_REACH) tiles so every walker that the fade could show is published. Consider computing the player's sight centre and CursorStart from the drawn position, or document why the logical tile is right. Add a test with a road-shifted walker at the sight edge.

**Verification:** host.cpp:1276 and :1301 show culling and sight by the logical foot with a +1 margin. I did not trace DrawnWhere or the 1.5-tile reach. The pop-in at the sight edge is plausible but needs a runtime or test check to decide. The impact is cosmetic.

### GAP2-COORDINATE-SPACES-AND-PICKING-06: A click picks at the alpha of input-handling time, not the alpha of the picture the player clicked

- **Severity:** low · **Verdict:** confirmed · **Category:** picking · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/app.cpp:643`

App::Click recomputes alpha with `platform::NowNs()` at event-handling time on the last drawn snapshot. The image on screen was drawn by the previous Present with that frame's own nowNs, and hover (App::Hovered) uses that frame's alpha. Click and hover therefore evaluate positions at different times: up to one frame later, clamped to 1 past the tick. Present latency adds a further gap. The comment claims 'the picture the player clicked on: the drawn snapshot at the render clock'.

**Evidence:** `const f64 alpha = core::Alpha(drawn->scheduledNs, platform::NowNs(), m_delay.Current(), drawn->advanced);` against PresentFrame's `Alpha(drawn->scheduledNs, nowNs, delayNs, ...)`, computed at draw time and passed to Hovered.

**Impact:** For fast movers (arena foxes, dashes) the highlighted (hovered) entity can differ from the one the click selects near quad edges. The effect is small at current speeds.

**Recommendation:** Keep the alpha (and camera) of the last drawn frame, e.g. store it in App when the draw lambda runs, and use it in Click and Cast so that click, hover and picture agree. Alternatively reuse the last frame's hovered index for the click.

**Verification:** app.cpp:643 recomputes alpha with platform::NowNs() at click time, while hover uses the alpha of the drawn frame. The difference is under one frame, and the comment overstates the match. The impact is tiny.

### GAP2-COORDINATE-SPACES-AND-PICKING-07: Rotation pick test can't detect a sign error between PickInstance and the vertex shader; no end-to-end MakeInstance-to-pick test

- **Severity:** low · **Verdict:** confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/app/view_tests.cpp:74`

'picking turns with a rotated instance' uses a centred 100x10 quad turned by exactly a quarter turn. That shape is identical for clockwise and anticlockwise rotation, so flipping the sign of `sine` in PickInstance (or in the shader) passes the test. The other tests use hand-made instances with the quad centre set directly. None goes through core::MakeInstance (foot to centre offset, widthScale about the foot, mirrored toCentre, rotation wrapping to the shorter arc) and checks that PickInstance hits a point that the shader formula `centre + R(angle)*(corner-0.5)*size` covers. That is the seam between core, app and HLSL.

**Evidence:** `.rotation = core::PackPair(core::PackHalf(0.0F), core::PackHalf(QUARTER_TURN))` with ALONG_X/ALONG_Y probes at 40 units on the axes.

**Impact:** A regression in the rotation direction or the foot offset would pass every unit test and show only as misclicks on rotated or offset sprites (e.g. the spinning crystal).

**Recommendation:** Add a case at an asymmetric angle (e.g. pi/6) with a probe that hits only under clockwise rotation. Add one built with MakeInstance from a manifest sprite with a non-centred foot, widthScale -1 and scale != 1, probing a point computed with the shader's formula (a C++ mirror of the vertex placement).

**Verification:** view_tests.cpp:74-89 uses a centred rect rotated by exactly pi/2. That shape is symmetric under a sign flip, so the test cannot detect one. The suggested asymmetric-angle and MakeInstance end-to-end tests are a sound improvement.

### GAP2-COORDINATE-SPACES-AND-PICKING-08: Additive light orbs and scenery props carry IDs, so they are hovered and picked over creatures

- **Severity:** low · **Verdict:** confirmed · **Category:** picking · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:1867`

Every Look entity gets its StableId on its instance (`dying ? core::StableId{} : stableId`), including the Effects-layer additive orbs (arena_layout.cpp ORBS, ARENA_GLOW, stress and painted lights) and static props. The Effects layer sorts after the entire World layer, so a 32-unit light orb orbiting through a fox wins the pick. Transparent quad corners count as hits by design. Hover outlines and Alt+click selection therefore land on lights and trees.

**Evidence:** arena_layout.cpp:124-131: orbs spawned with `.layer = core::Layer::Effects, .pipeline = core::Pipeline::Additive` through context.Spawn, which gives a StableId. Publish passes `stableId` for every non-dying Look.

**Impact:** Hover flicker and wrong selections in the arena and stress scenes. Pure light should not be a click target.

**Recommendation:** Add a Look flag (or derive it from the layer: Effects, additive) that publishes the instance with no ID, as shadows and the ground already do. Add a test that an orb over a fox lets the fox be picked.

**Verification:** host.cpp ~1867 passes stableId for every non-dying Look regardless of layer or pipeline. PickInstance (view.cpp:65) walks back to front, checks only the quad rect (transparent corners hit) and has no layer or pipeline filter. Effects sorts after World, so an orb over a fox wins the pick. Shadows and the ground are published without an ID, so there is precedent for the fix.

### GAP2-COORDINATE-SPACES-AND-PICKING-09: Heads, labels, health bars and chest aim use the base Look sprite's foot, while the drawn instance uses the pose sprite

- **Severity:** low · **Verdict:** confirmed · **Category:** cross-module mismatch · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/combat.cpp:43`

HeightOf, HeadOf, PublishOverhead's `head` and PublishJourneys' `height` all read `manifest.Sprite(look.sprite).footY * look.scale`. The instance is drawn with `pose.sprite` (a walk frame chosen by DrawnPoseOf or PoseOn). Nothing checks that walk frames share the base sprite's size and foot: data/animations.cpp has no foot check. Today all cultivator_walk_* and villager_walk_* frames happen to match (foot 36,122 and 32,112).

**Evidence:** `return static_cast<f32>(manifest.Sprite(look.sprite).footY) * look.scale;` against `core::MakeInstance(*m_manifest, { .sprite = pose.sprite, ...})`.

**Impact:** A new or retouched walk frame with a different height or foot would float labels and health bars off the head and move the chest that Tag() and volleys aim at. The pick would still follow the drawn quad, so the label, the hit point and the clickable area would disagree.

**Recommendation:** Validate in data::LoadAnimations (or a manifest test) that every frame of a clip has the base sprite's size and foot, or compute heads from the drawn pose sprite.

**Verification:** combat.cpp HeightOf reads manifest.Sprite(look.sprite).footY * look.scale, while the instance is built from pose.sprite (host.cpp ~1855). data/animations.cpp has no check that walk frames match the base sprite's size or foot. Latent only, because the current frames match.

### GAP2-COORDINATE-SPACES-AND-PICKING-10: Two different ground-plane squash ratios: VIEW_TILT 0.6 (orbits, arena layout) and SEAL_VIEW_RATIO 0.375 (seals, shadow, portal)

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** consistency · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/components.hpp:117`

`VIEW_TILT = 0.6` ('how much flatter than wide a circle on the ground looks') squashes the fox and orb orbits and the arena layout's ellipse (arena_layout.cpp:151). Ground seals, the portal's step area (PORTAL_DEPTH_RATIO) and the blob shadow (24/64) use 0.375 for the same notion. Separately, Tag() tests an unsquashed circle of `ability.radius` around the clicked point against chest points, while the cast's ground seal (sword_formation, size 420) is drawn squashed by 0.375. The drawn ground marking doesn't show the tagged area.

**Evidence:** `inline constexpr f32 VIEW_TILT = 0.6F;` against `SEAL_VIEW_RATIO = 24.0F / 64.0F`. `Tag(...)`: `SquaredDistance(point, chest) <= radius*radius`.

**Impact:** Orbits read as seen from a steeper camera than the seals and shadows under them. Players can't read an ability's reach from its ground seal: an enemy 200 units below the click is hit while one 300 units across inside the drawn seal is not.

**Recommendation:** Use one camera-angle constant for every ground-plane ellipse. If the area abilities should show their reach, draw the reach indicator from `ability.radius` with the same metric that Tag uses (or make Tag use the ground ellipse at foot level). Record which one is intended.

**Verification:** Both constants exist as cited: VIEW_TILT 0.6 (components.hpp:117, used for orbits and the arena ellipse) and SEAL_VIEW_RATIO 24/64 (seals.hpp:60, also used for the portal). They are two different squash ratios for the same idea. The claim that Tag tests an unsquashed circle is plausible from the Tag(point, radius) signature and SquaredDistance. The drawn-versus-hit mismatch for area abilities is a design question, not a defect I could settle by reading.
