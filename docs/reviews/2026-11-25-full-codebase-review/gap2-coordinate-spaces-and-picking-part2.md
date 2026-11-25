# Review: gap2-coordinate-spaces-and-picking (part 2)

### GAP2-COORDINATE-SPACES-AND-PICKING-11: Target cursor outline, markers and stamps are fixed 256-unit sprites while tile_size is accepted from 1 to 1,000,000

- **Severity:** low · **Verdict:** confirmed · **Category:** data/geometry coupling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/route_draw.cpp:79`

DrawTargetTile places `target_tile` (256x256, foot 128,128) at the tile centre with scale 1, and terrain_draw places stamps, markers and paintings with scale 1 too. The outline matches a tile only because world.yaml's tile_size is 256. The world map reader accepts any tile_size in [1, 1e6], and every gameplay conversion (TileAt, TileCentre, fog, WalkTo) scales with it, so another value silently desynchronises the drawn cursor and terrain art from the tiles that clicks and walks resolve to. npcs.hpp's CELL_SIZE = 256 for the arena grid is the same coincidence.

**Evidence:** `Add(look.cursor, *cursorTile, manifest, into);` with `MakeInstance(manifest, { .sprite = sprite, .previous = foot, .current = foot })`. data/world_map.cpp:735 range `{ .low = LEAST_TILE_SIZE, ..., .high = MOST_TILE_SIZE }`.

**Impact:** A procedural generator or data change to tile_size would draw a cursor outline that doesn't frame the tile it targets, and misplace markers and stamps, with no error.

**Recommendation:** Either pin tile_size to the art (refuse any value but the art's tile size, from a shared constant) or scale the cursor, markers and stamps by tileSize / ART_TILE_SIZE. Add a test that the cursor's drawn quad equals the tile's rect.

**Verification:** The cursor, markers and stamps are fixed-size art drawn at scale 1, while tile_size is accepted in a range. Nothing ties the art size to tile_size. Only latent while the data stays at 256.

### GAP2-COORDINATE-SPACES-AND-PICKING-12: WIDEST_HALF_VIEW is not the widest view: windows wider than 32:9 or narrower than 8:9 show more at MIN_ZOOM

- **Severity:** low · **Verdict:** confirmed · **Category:** culling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/view_limits.hpp:25`

The publish culls to `start ± WIDEST_HALF_VIEW` (REFERENCE_SIZE / MIN_ZOOM = 7680 x 4320) before the first view arrives, documented as 'at its widest'. WorldToClip fits REFERENCE_SIZE by the smaller axis ratio, so a 48:9 window (5760x1080, triple monitor) shows 23,040 units across at zoom 0.25 (half 11,520). A 9:16 portrait window shows ~13,650 high (half 6,827). Both exceed the constant.

**Evidence:** `inline constexpr Vec2 WIDEST_HALF_VIEW = REFERENCE_SIZE * (1.0F / MIN_ZOOM);` and the comment 'a 32:9 window shows 15,360 world units across'.

**Impact:** On such windows the first snapshot(s) before the view report are missing entities at the edges, a brief pop. shipped_world_tests also uses this rect as 'the widest view' for its traveller-load bound.

**Recommendation:** Either reword the comment (widest up to 32:9 / 8:9) or make the first-publish cull unbounded on the long axis. Have the app send its view before the first publish where possible.

**Verification:** view_limits.hpp:25 sets WIDEST_HALF_VIEW = REFERENCE_SIZE / MIN_ZOOM. Its own comment limits it to 32:9 and 8:9. The camera fits REFERENCE_SIZE by the shorter axis, so a 48:9 or narrower-than-8:9 window shows more. Only the first publish before the view report is affected, so it is a brief pop. The comment's 'at its widest' is wrong.

### GAP2-COORDINATE-SPACES-AND-PICKING-13: Place names hang below the tile and can spill into the next tile; labels are never click targets

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** picking/UX · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/scenes/world.cpp:69`

A place label's top sits 56 units below the tile centre (marker half height 48 + PLACE_NAME_GAP 8), with PLACE_WORLD_SIZE 72 times the text-size setting. At text scale 1 it ends exactly at the tile's bottom edge. At any larger accessibility text size it covers the tile below. Labels aren't pickable, so clicking a city's name sends WalkTo to whatever tile lies under the click, possibly the row below the city. In the same way, clicking a traveller's name label walks rather than selects.

**Evidence:** `const core::Vec2 anchor = centre + core::Vec2{ .x = 0.0F, .y = markerBelow + PLACE_NAME_GAP };` with `.align = core::TextAlign::TopCentre`.

**Impact:** Clicking a name reads naturally as 'go there' or 'select them' but sends the player to the wrong tile or walks them away, and it gets worse with the accessibility text scale.

**Recommendation:** Make labels click targets: a label hit resolves to its entity (traveller) or its place's tile. At least, map a click inside a place label's screen box to the place's tile.

**Verification:** world.cpp:69 anchors the place label at centre + markerBelow + PLACE_NAME_GAP with TopCentre alignment, so it hangs below the tile as described. PickInstance only looks at instances, never at text, so labels are not click targets. Whether the label spills into the next tile depends on text scale and I did not compute it. This is a UX gap rather than a bug.

### GAP2-COORDINATE-SPACES-AND-PICKING-14: Picking ignores the cloud shade, so with the debug sight a walker hidden under clouds is hoverable and selectable while its label is hidden

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** picking/fog · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/view.cpp:94`

PickInstance skips a SIGHT_FADE instance only by `gfx::SightFade(...) < 0.5`. FogOpacity for its label multiplies `SightFade * ExploredFade` precisely because the debug sight shows walkers on unexplored land, where the cloud shade and clouds (drawn after the instances) hide them. Picking has no such factor. So with the debug sight on, the hover outline and selection reach invisible walkers under clouds. The outline glow is drawn before the clouds and is hidden too, which makes the hover invisible.

**Evidence:** view.cpp: `if ((instance.flags & core::INSTANCE_SIGHT_FADE) != 0 && gfx::SightFade(fog, core::SightPoint(instance, alpha)) < SIGHT_PICK_OPACITY)`. labels.cpp:620: `return SightFade(fog, foot) * ExploredFade(fog, foot);`.

**Impact:** Debug-only today. It does show that the pick and draw visibility rules live in two places with different formulas.

**Recommendation:** Share one 'visible opacity at sight point' helper between labels and picking (SightFade * ExploredFade), and test it with debugSight on unexplored land.

**Verification:** view.cpp:94 tests only SightFade, and I found no ExploredFade in view.cpp. The cloud-shade factor is therefore absent from picking, as claimed. It only matters under the debug sight, so it is debug-only and trivial.

### GAP2-COORDINATE-SPACES-AND-PICKING-15: Route line starts at the end-of-tick drawn foot while the sprite is interpolated

- **Severity:** info · **Verdict:** confirmed · **Category:** presentation · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:1385`

PublishRoute pushes `DrawnPlayer(PlayerBody()).current` as the line's first point, and DrawRoute places the dots with previous == current. The cultivator's sprite is drawn at lerp(previous, current, alpha). So the line's start leads the sprite by up to one tick's movement and visibly steps each tick.

**Evidence:** `snapshot.route.push_back(DrawnPlayer(PlayerBody()).current);`. route_draw.cpp Add uses `.previous = foot, .current = foot`.

**Impact:** Small at walk speed. It is a one-tick mismatch between the line and the figure it starts from, visible after M's jump or at higher walk speeds.

**Recommendation:** Publish both the previous and current drawn foot for the route's first point and interpolate the first segment in DrawRoute, or document the step.

**Verification:** host.cpp:1385 pushes DrawnPlayer(PlayerBody()).current as the route's first point. The sprite is interpolated between previous and current, so the line start leads the sprite by up to one tick. It is a cosmetic one-tick offset.

### GAP2-COORDINATE-SPACES-AND-PICKING-16: Two constants named CULL_MARGIN with different units and meanings

- **Severity:** info · **Verdict:** confirmed · **Category:** naming · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/draw_list.hpp:87`

app::CULL_MARGIN (view.hpp) is a fraction of the view (0.25) used to grow the published area. core::DrawList::CULL_MARGIN is 512 world units, used for sprite extents around instance centres. Both cull the same publish, at different stages.

**Evidence:** `static constexpr f32 CULL_MARGIN = 512.0F;` and `inline constexpr f32 CULL_MARGIN = 0.25F;`.

**Impact:** A reader reasoning about pop-in at the view's edge can confuse the two. The 512 is also independent of zoom and of actual sprite sizes (fox_spirit is 260 tall).

**Recommendation:** Rename the draw list's to SPRITE_REACH (or similar) and tie it to the largest sprite extent in the manifest, checked by a test.

**Verification:** Two CULL_MARGIN constants exist: app/view.hpp:39 (0.25, a fraction of the view) and DrawList::CULL_MARGIN in draw_list.hpp:87 (512 world units). The names clash. They are in different namespaces, so it compiles and is only a naming issue.
