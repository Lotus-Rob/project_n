# Phase 6, chunk 5b-look, part 1: the world map's terrain

Date: 2026-11-21 · Status: **implemented 2026-11-21** (see Outcome; approved 2026-11-21) · Plan:
`2026-11-21-phase6-chunk5b-look-terrain.md` · Chunk spec: `2026-11-10-phase6-chunk5-world-map-design.md` · Phase spec:
`2026-11-10-phase6-design.md` · Builds on: `2026-11-15-phase6-chunk5b1-map-design.md` and
`2026-11-20-phase6-chunk5b3-fog-design.md` (their Outcomes) · Followed by: 5b-look part 2 (travellers) and part 3
(sharpness), each its own spec

## In short

- **The ground becomes painted paper:** one continuous paper ground with soft, low-strength watercolour washes per
  terrain kind, blended across kinds along irregular noisy edges. No tile edge shows anywhere, at any zoom.
- **Sparse painted stamps** (trees, rocks, reeds, dunes, ripples) sit on the paper with soft mist at their feet, the
  "atmospheric" look the user picked (ToI-like, not the denser "middle" one).
- **Water, mountains and rivers are strong:** sea and lakes deepen smoothly away from a fine inked shore; mountains are
  loose overlapping ranges bigger than a tile; rivers are wide painted bands (about 0.4 tile) that run into the sea and
  lakes.
- **Places are painted:** a city is a painting about 2×2 tiles across, a danger site a painted stamp; a small marker
  stays on top so every place reads and clicks at the widest zoom.
- **The tile grid is much fainter.** The fog keeps its rules and soft edges; the new ground, rivers, stamps and
  paintings drain together, markers stay in colour; the clouds are repainted to sit on paper.
- **Built as approach A:** a ground shader reading a per-tile kind texture (like the fog's), rivers as ribbons from the
  map's polylines, stamps and paintings as sprites listed in `world_look.yaml`. All art is generated placeholders: an
  artist paints the final art later, so this part builds the pipeline.

## Intent

The world map should look like a painted map of a cultivation world, not a board of coloured squares: the user's words
were "make the world map tiles less ugly … Tale of Immortal's world map is what I want" (2026-11-14). ToI is the
inspiration, not a template. The look picked from the previews (2026-11-21) is calm and airy: mostly paper, the land's
kind told by a faint wash and a few painted marks, with water, mountains and rivers carrying the map's shape. The hard
tile edges also shimmer (world shimmer 0.048 today against the 0.02 text and seals hold), and the fix for that must come
from how the ground is built, not from tuning. Final art comes from an artist later, so what this part must get right is
the pipeline: every look value and sprite in `world_look.yaml`, every piece of art replaceable without code.

## Approaches

**How the ground is drawn** (the user picked A, 2026-11-21):

| Approach | Trade-offs |
|---|---|
| **A. A ground shader reading a kind texture (picked)** | One texel per tile holds its kind and its distance to the shore, built when the map loads; one full-view pass draws paper and washes, noise warping where one kind meets the next, water deepening with the distance. Rivers are ribbons from the map's polylines; stamps, ranges and place paintings are sprites. For: tiles never exist on screen, so no edge can shimmer; sharp at zoom 2; colours reload live; cheaper than today's thousands of tile sprites. Against: a new pipeline and shader; the painterly feel comes from noise and colour rather than painted pixels |
| B. Ground baked into large images at load | Painted on the CPU or GPU into region textures, drawn as sprites. For: any painterly effect, simple drawing. Against: sharp at zoom 2 costs about 200 MB of texture, so it would be blurry up close; a changed tile means re-baking; slower loading |
| C. Soft oversized tile sprites | Tiles stay sprites, bigger than their square with feathered edges. For: nothing new in the renderer. Against: the tile rhythm still shows and shimmers at far zoom (close to the edge-variant tiles already rejected), about triple the overdraw against the instance cap |

**The ground's look** (the user, 2026-11-21, from the previews in the project memory's `refs/5b-look-preview/`):
"atmospheric" (paper, faint washes, sparse stamps, mist), refined as `atmospheric2_*` (stronger water, rivers and
mountains), over "middle" (stronger washes, denser stamps, no mist).

**Places** (the user, 2026-11-21): A, painted places plus a small marker, over markers only (places look pasted on) and
paintings only (a cave or shrine is a few pixels at zoom 0.25, against accessibility first).

## History

- **Decided before:** 5b1 kept the road and river polylines "for 5b-look to draw as smooth lines" and noted blending
  "needs the kind grid in the renderer anyway" (5b1 spec). 5b3 chose a per-tile fog texture sampled by the terrain's
  shader and placeholder clouds "repainted in 5b-look with the rest of the world's look", and rejected hard square fog
  edges because they shimmer. The backlog's "World map look" entry (2026-11-14) set the acceptance: world shimmer at
  0.02 or under, measured at several zooms, the guard's bound tightened.
- **Kept:** 8-way routes stay (5b2); roads stay polylines and keep their current drawing in this part (smoothed roads
  are part 2's, with the travellers who walk them).
- **Rejected:** edge-variant terrain tiles (16 per kind, like the roads) and pre-made sepia copies of terrain sprites
  (5b3: edges follow the grid, the atlas doubles); LDtk or tile-editor transition rules (5b1: transitions are this
  part's design call, not an editor's); the "middle" look (above).
- **What exists:** `gfx::DrawTerrain` (`src/gfx/terrain_draw.hpp`) lays out one sprite per tile in view (a variant per
  kind, `world_look.yaml`'s `kinds:`), river and road overlays by neighbour mask, bridges, fords and place markers, all
  flagged `core::INSTANCE_FOGGED_LAND`, as one sprite buffer (at most `MAX_TERRAIN_INSTANCES`, 8,192) re-laid when the
  view reaches other tiles. `shaders/sprite.frag.hlsl`'s `Fogged()` samples the fog texture (`fogField`, one texel per
  tile, linear, explored in R, in sight in G) for drain, mist and shade. A ribbon pipeline (additive, sword trails) and a
  mesh pipeline (premultiplied over, screen space) exist. The map is static data (`core::WorldMap`: 150 × 85 tiles of
  256 world units, 11 kinds, `Roads()`, `Rivers()` polylines of tile points, 6 cities and 20 danger sites, each one
  tile). Zoom 0.25 to 2. The shimmer guards (`tests/reference/reference_tests.cpp`): world 0.048 measured against a 0.06
  bound at zoom 0.25 (tiles and travellers), clouds 0.0205 against 0.0256.
- **Constraints recorded with the user:** accessibility first; small unit tests (about 20 samples, never sweeps);
  showcases never overlap; logs say outcomes; prefer an established permissive library over our own where one fits; ToI
  is inspiration, not a template.

## Goals

1. **The kind texture.** When the world scene loads its map, the renderer builds a texture of one texel per tile
   holding the tile's kind index and its signed distance to the nearest shore (water against land, in tiles, negative
   in water, clamped to a few tiles), and binds it to the ground pass. The map doesn't change at run time; the texture
   is rebuilt only with the map.
2. **The ground pass** draws the whole view once, before anything else on the world map, in world space:
   - **Paper:** a warm paper colour with a faint fibre grain (procedural noise in world units).
   - **Washes:** each kind's wash colour laid over the paper at that kind's strength (both in `world_look.yaml`; the
     preview's `atmospheric2` values as the start). Where kinds meet, the boundary is warped by noise (about a third of
     a tile) and softened (about half a tile), so it reads as watercolour bleeding, never as a straight line along the
     tile grid; washes are slightly blotchy (low-frequency noise on their strength) like pooled watercolour.
   - **Water:** deep water and shallows deepen smoothly with the shore distance (no tile steps between depths), with a
     fine inked line along the shore and the washes softer over water.
   - **The grid:** a much fainter tile grid than today (its colour and strength in `world_look.yaml`), anti-aliased,
     fading out as the camera zooms out so it never gets busy or shimmers at zoom 0.25.
   - **No shimmer:** every noise octave and the grid fade out before they get finer than about a pixel at the current
     zoom, so camera moves of a fraction of a pixel don't make the ground crawl.
   - **The fog:** the ground applies the same drain, mist and shade as `Fogged()` does for land today (shared shader
     code, not a copy).
3. **Rivers as painted bands.** Each river polyline is smoothed (a centripetal Catmull-Rom spline or similar through
   its tile centres) and drawn as a ribbon about 0.4 tile wide with a darker soft edge, ending inside the sea or lake it
   runs into; a new premultiplied-over ribbon variant in world space with anti-aliased edges and the fog's land
   treatment. Joins where rivers meet are smooth. Bridges and fords stay sprites, sized to span the wider band.
4. **Stamps.** Each tile may carry painted stamps for its kind, laid out like today's terrain (pure, on the main thread,
   re-laid when the view reaches other tiles) and drawn as sprites with the fog's land treatment:
   - **Per kind in `world_look.yaml`:** the stamp sprites to pick from, a density (the chance a tile has one), a size
     range and a jitter; the preview's `atmospheric2` densities as the start (forest 0.45, rock and peaks 0.6, deep
     water and shallows 0.08, meadow 0.03 …).
   - **Fixed by a hash** of the tile (sprite, mirror, offset, size): panning away and back, or zooming out and in, gives
     the same stamps.
   - **Back to front by foot,** so stamps overlap like a painting.
   - **Mountains as ranges:** peaks, snow and rock stamps are wider than a tile (about 1.2 to 1.7 tiles) with more
     jitter, so neighbouring stamps overlap into loose ranges rather than one per square.
   - **Mist:** a soft pale puff at the foot of most land stamps (one batch of mist sprites drawn after the stamps), the
     "atmospheric" mist; its colour and strength in `world_look.yaml`.
   - **The veil:** over the stamps, puffs and paintings, a second full-view pass lays a pale veil in broad patches
     (low-frequency noise about 3 tiles across, up to about 0.55 strength, near-white), as the picked preview does; it
     takes the fog's land treatment, and its colour, scale and strength are in `world_look.yaml`.
   - **Thinning at far zoom:** each kind's stamps can name a zoom below which small stamps (tufts, flowers, ripples,
     reeds) fade out, ranked by their hash so the same ones go first; forests and mountain ranges stay. Fading is
     continuous with the zoom, never a pop.
5. **Places painted.** Each city draws a painting centred on its tile; each danger site a painted stamp. The painting's
   own size decides how far it spreads (placeholders: 2×2 tiles for cities, 1 tile for danger sites), so the capital
   can be larger than the oasis by art alone. `world_look.yaml` names a painting per place kind (`city`, and each danger
   sort) and lets any place name its own by key. Terrain stamps whose foot falls inside a painting's footprint are
   skipped, so stamps make way. Paintings sort with the stamps by foot. Gameplay is unchanged: a place is one tile for
   walking, picking and travel.
6. **Markers on top.** A small marker stays over every place, above stamps, paintings and mist, as today's sprites.
   Markers are hidden under the clouds like land but **not drained** out of sight, so places already discovered stay
   easy to find.
7. **The fog's look on the new ground.** The fog's rules, texture and soft edges are unchanged. Ground, rivers, stamps,
   mist and paintings drain toward sepia and take the mist together when out of sight; everything spilling into
   unexplored land is hidden by the clouds and the shade. The cloud placeholders are repainted to sit on paper (softer
   ink, paper-toned fill), keeping their count, sizes and layout.
8. **Every look value live.** Wash colours and strengths, edge warp and softness, water depth colours, the shore line,
   the grid, stamp lists and densities, mist and paintings are in `world_look.yaml` (presentation: not hashed, not saved,
   reloaded live by debug builds as today). A broken edit keeps the old look.
9. **The art pipeline.** Every new sprite (stamps per kind, mountain ranges, mist puffs, place paintings, wider bridges
   and fords, repainted clouds) is a generated placeholder from `art/placeholders.py`, listed in `art/sprites.toml` and
   the world scene's list in `art/scenes.toml`, so an artist replaces a PNG and reruns `atlas`, never code.
10. **Shimmer to 0.02.** The terrain holds a shimmer of 0.02 or under at zoom 1, 0.5 and 0.25, each a guard case of
    its own with its bound set from its first measurement plus the guards' usual margin (about 1.25 times), and never
    above 0.02. **In the frame:** the ground, the veil, rivers, stamps, mist puffs, place paintings, bridges, fords and
    markers, over the busiest region explored and in sight (the debug sight, as the world case); **hidden:** travellers
    and labels (part 2's and 3's) and the road overlays (redrawn in part 2). **Measured against flat paper** (the same
    frame drawn as plain paper colour), not the dark backdrop, so the paper's large even ink doesn't dilute the crawl of
    the grid, shore, river and stamp edges. The existing world guard (tiles and travellers at zoom 0.25) is tightened
    to its new measurement plus margin. The cloud guard's bound is re-set from its new measurement plus the same margin
    but never above today's 0.0256; if the repainted clouds would need more, the placeholder art is fixed instead.
    `measure` reports the new cases beside the old.
11. **No cost.** `measure`'s world run (with `--debug-sight`) shows no frame-time change beyond noise against main's
    numbers, and the terrain's main-thread layout at the widest view stays under 0.2 ms median.

## Drawing order on the world map

**Ground pass** (paper, washes, water, grid; fog) → **rivers** (ribbons) → road overlays, bridges, fords (as today) →
**stamps, mountain ranges and place paintings** (one sorted batch) → **mist puffs** → **veil pass** → **markers** →
route → the
snapshot's ground, decals and entities → clouds → target cursor → overlay, text, HUD.

## Non-goals

- Smooth roads, walk animation and facing, spreading the travellers' queues, label placement: part 2.
- Art scale, offline mips, atlas page size: part 3.
- Final art: an artist paints it later; placeholders now.
- Roads that switchback up mountains and branch into trails (a later goal, recorded).
- Any change to the map's data, routes, costs, saves, hashes or determinism: this is presentation only.
- Recolouring the world in an accessibility menu: the settings screens own options (chunks 10 and 11); the values sit
  in `world_look.yaml` ready for them.
- Animated water or drifting mist.
- Other scenes' ground (towns, combat, dev scenes).

## Anti-goals (a failure even if it works)

- **Tile edges:** a straight line along the tile grid visible in washes, water or stamps at any zoom (the faint grid
  itself excepted).
- **Shimmer:** the ground crawling under sub-pixel camera moves; any new guard case over 0.02.
- **Popping:** a stamp, painting, mist puff or river segment appearing or vanishing from one frame to the next while
  panning or zooming.
- **Shuffling:** stamps changing place, size or sprite when the camera pans away and back or zooms out and in.
- **Unreadable places:** a place you can't find or click at zoom 0.25.
- **Cost:** a visible frame-time change in `measure`, a draw call per stamp, or a hitch when the view reaches new tiles.
- **Art locked in code:** a look value or sprite the artist can't change without a code change.

## Decisions

The user's, 2026-11-21:

1. **5b-look splits into three specs and branches,** in order: terrain (this one), travellers, sharpness.
2. **The ground is paper with soft per-kind washes and sparse painted stamps with mist** ("atmospheric", ToI-like),
   not "middle".
3. **Rivers wider** (a painted band about 0.4 tile, running into the sea and lakes); **lakes, sea and mountains
   strong**, mountains as loose overlapping ranges bigger than a tile.
4. **The square grid much fainter.**
5. **Final art by an artist later:** this part builds the pipeline, stamp sprites per kind in `world_look.yaml`, with
   generated placeholders.
6. **Approach A:** a ground shader reading a kind texture, rivers as ribbons, stamps and paintings as sprites.
7. **Places painted plus a small marker** (A): cities about 2×2 tiles, danger sites a painted stamp, gameplay unchanged.
8. **The fog keeps its rules;** the new ground drains with everything else, markers stay in colour, clouds repainted.
9. **Recorded for later:** roads that switchback up mountains and branch into trails; roads drawn from the road's tile
   chain (rounded corners, steady wobble, thin quiet ink, square to bridges) with travellers walking the same line, in
   part 2.

## Calls made here (not the user's; check them)

- **No tile edges anywhere; the shimmer target applies to the ground without travellers** at zoom 1, 0.5 and 0.25.
  Travellers' own shimmer (their sprites and labels) belongs to parts 2 and 3; the existing tiles-and-travellers guard
  only tightens to its new measurement here.
- **Water depth blends softly** from a shore distance field rather than the two water kinds' hard boundary.
- **At zoom 0.25 small stamps fade to washes, leaving forests and ranges**; marsh and hills become harder to tell
  apart, accepted since click-to-walk plans the route.
- **The painting's size sets a place's spread**, with a default per place kind and a per-place override by key.
- **Markers stay undrained** out of sight but hidden under cloud (a fog flag variant: shade only).
- **Paper grain and wash noise are procedural** in the shader, not a paper texture: no tiling repeat to hide, and no
  texture to make sharp at zoom 2. A paper texture from the artist can replace it later if wanted.
- **Mountain ranges are oversized, jittered per-tile stamps that overlap**, not a clustering pass that places one
  sprite per range: simple, stable under panning, and the preview showed it reads as ranges.
- **Mist is a sprite puff per stamp plus a full-view veil pass,** both as the picked preview draws them (the veil is
  much of its airy look); the veil reuses the ground's pipeline in a second mode rather than a new one.
- **The ground's shimmer cases leave out the road overlays and travellers** (parts 2 and 3 redraw them) **and are
  measured against flat paper**, so 0.02 tests the ground's own detail rather than passing on the paper's even ink.
- **The cloud guard may re-baseline but not loosen** past today's 0.0256: the new ground under the clouds is part of
  its number, and the repaint must not make the clouds crawl more than they do.
- **The cloud repaint is part of this part** (5b3 left it to "5b-look"); placeholders still.
- **The old terrain tile sprites and the old bridge and ford sprites are removed** from `art/sprites.toml`,
  `art/placeholders.py`, `assets/sprites/` and the world scene's list (the user, 2026-11-21: sprites and their IDs may
  change or go until release 1.0.0). `art/sprites.toml`'s header comment changes to say so (IDs stable from 1.0.0 on),
  in the same commit. Save compatibility binds no one before 1.0.0 either (the user, 2026-11-21), so a save or replay
  holding sprite IDs needs no migration; the commit marks the break only if one does.

## Testing

- **The kind texture** (pure, without a device): a tile's kind index and shore distance at a handful of tiles of a
  small test map (a lake, a coast, an inland tile); distances clamp; water negative.
- **Stamp layout** (pure, like `DrawTerrain`'s tests): the same view gives the same stamps; panning away and back gives
  the same; a kind with density 0 gets none and 1 gets one per tile; stamps under a place painting's footprint are
  skipped; back-to-front order by foot; thinning keeps the same stamps at the same zoom and drops the lowest-ranked
  first; the instance count at the widest view of the shipped map stays within the buffer's limit.
- **River ribbons** (pure): a polyline's ribbon passes through its tile centres, has the configured width, and ends
  inside the water it runs into; a join of two rivers leaves no gap.
- **The look's reader:** `world_look.yaml`'s new sections read, and refuse with the line a missing kind, an unknown
  sprite, a sprite missing from the world scene's list, a density outside 0 to 1, a bad colour; the fuzz seed covers the
  new keys.
- **Pictures** (`reference`, software Vulkan): the existing world pictures refresh (`world_close`, `world_middle`,
  `world_widest`, `world_places`, `world_places_widest`, `world_route` and the fog's), plus a coast with a river mouth,
  a mountain range and a city painting at zoom 1.
- **Shimmer:** the three ground cases (zoom 1, 0.5, 0.25; Goal 10's frame and baseline), the tightened world case, the
  cloud case re-baselined within 0.0256.
- **Measurements:** `measure`'s world run against main's; the terrain layout's time at the widest view; recorded in
  the Outcome.

## Left to the builder

How the shader blends kinds that can't be filtered as values (for example a few warped point taps of the kind texture
looked up in a colour table, or weights over the 2×2 neighbourhood); the noise functions and their octaves; the kind
texture's format and the shore distance's clamp; how wash colours reach the shader (a uniform table sized for the
kinds, with a limit tested against the shipped map); the ribbon variant's vertex layout and the spline; where the stamp
layout lives (a pure `gfx` function beside `DrawTerrain`, or inside it); the exact stamp sizes, jitters and the
placeholder art's drawing (starting from `look.py` in `refs/5b-look-preview/`); the `world_look.yaml` layout of the new
sections; the shimmer cases' regions; commit split and order.

## Outcome

Measured on the dev PC (2560x1440 at 165.1 Hz, fullscreen, 2 workers, unpaced, releasedbg), the world run of `measure`
(with `--debug-sight`), both runs trusted on the first try. Main's run was taken by switching the main checkout to main
and back, with main's own `measure`. Every figure is this branch / main, in ms.

| segment | frame median | frame p99 | frame worst | main-thread work median | work p99 | work worst | GPU wait |
|---|---|---|---|---|---|---|---|
| follow: first 0.5 s | 6.07 / 6.09 | - | 7.70 / 7.49 | 3.71 / 3.23 | - | 4.43 / 4.55 | 39% / 45% |
| follow: rest | 5.94 / 6.05 | 9.24 / 9.56 | 10.68 / 10.28 | 4.28 / 3.68 | 7.42 / 6.83 | 8.77 / 7.49 | 30% / 36% |
| pan: first 0.5 s | 5.96 / 6.05 | - | 9.86 / 10.05 | 4.20 / 3.52 | - | 7.02 / 6.35 | 29% / 40% |
| pan: rest | 5.99 / 6.02 | 9.41 / 9.28 | 9.87 / 10.02 | 3.40 / 2.67 | 7.00 / 5.98 | 7.53 / 6.53 | 39% / 54% |
| zoom in and out: first 0.5 s | 6.09 / 6.01 | - | 9.36 / 8.72 | 2.83 / 2.46 | - | 5.85 / 4.70 | 50% / 57% |
| zoom in and out: rest | 6.05 / 6.02 | 9.30 / 10.03 | 10.72 / 11.21 | 3.11 / 2.50 | 7.88 / 6.47 | 9.27 / 8.07 | 43% / 54% |
| widest view: first 0.5 s | 6.09 / 6.12 | - | 9.30 / 8.14 | 3.98 / 3.39 | - | 7.20 / 5.90 | 30% / 44% |
| widest view: rest | 6.01 / 6.09 | 9.31 / 10.08 | 10.22 / 10.87 | 4.02 / 3.37 | 6.88 / 6.27 | 8.10 / 7.63 | 34% / 44% |
| month step: first 0.5 s | 6.05 / 6.10 | - | 55.23 / 56.54 | 4.25 / 3.75 | - | 7.38 / 6.28 | 26% / 33% |
| month step: rest | 5.99 / 6.03 | 9.16 / 9.50 | 9.92 / 10.20 | 4.26 / 3.62 | 7.07 / 6.82 | 7.51 / 7.19 | 27% / 37% |
| labels: first 0.5 s | 6.05 / 6.08 | - | 9.44 / 8.83 | 4.25 / 3.34 | - | 7.30 / 7.78 | 25% / 38% |
| labels: rest | 5.98 / 6.08 | 9.57 / 9.31 | 10.31 / 10.08 | 4.19 / 3.19 | 7.65 / 5.70 | 8.66 / 6.03 | 30% / 52% |

**Frame time (Goal 11): unchanged on screen, the main thread's work is not.** Every median sits at the display's 6.06 ms
refresh (both runs are paced), the p99 differs by under 0.8 ms either way (lower on this branch in four segments:
follow, zoom in and out, widest view and month step; higher in pan by 0.13 ms and labels by 0.26), and the only hitch
is the month step's one, which both runs show alike (55.23 / 56.54 ms, carried by the logic and worker threads; the
main thread's worst there is 7.38 / 6.28). The main thread's work is higher in all 12 segments, by 0.4 to 1.0 ms at the
median (widest view, rest: 4.02 / 3.37; labels, rest: 4.19 / 3.19) and by up to 2 ms at the p99 (labels, rest: 7.65 /
5.70). That rise is real and consistent, not noise. The layout accounts for about 0.03 ms of it (below); the rest is not
attributed, as no profile of the rest was taken. The paced frame time shows no effect (the GPU's wait share is lower in
every segment, by 6 to 22 points), but the rise would cut the main thread's headroom if the frame budget tightened.
Input latency was 12.6 ms median (11.9 to 17.8 over 8 presses) against main's 14.2 ms (11.3 to 20.7); the display delay
the same (3.7 ms, 3.0 to 10.0 / 3.1 to 10.0).

**Layout time** (Goal 11: under 0.2 ms median at the widest view; measured over the zoom segment, which covers it).
`DrawTerrain` relays the terrain buffer on every zoom
change, so every frame of a smooth zoom, and holds it while the view and zoom stay put. A 30 s `profile` run at the fixed
`--world --debug-sight --zoom 0.25` therefore holds one call each (`DrawTerrain` 0.035 ms, `DrawStamps` 0.029 ms): no
median to take. `profile` takes no zoom-changing game args, so the per-call times come from `measure`'s capture (one
Tracy trace with the camera path's zoom in and out, then the widest view), read with `tracy-csvexport -u` (each zone's
calls): in its 4 s zoom segment `DrawTerrain` is called 659 times, at a median of 0.026 ms (p99 0.060, worst 0.101) and
`DrawStamps`, which it contains, 0.022 ms (p99 0.050, worst 0.091); the whole capture's 693 calls give the same (median
0.026, p99 0.063, worst 0.101 ms). The median is a seventh of the target and the worst frame half of it. The same
capture's `UploadTerrain` is 0.002 ms at the median (p99 0.062, worst 0.19).

**The ground field's rebuild** has no zone of its own; it runs inside `UploadGround` (the field with the rivers' ribbons)
once per map or look change. That call's worst in both captures is 1.34 to 1.35 ms, the first frame of the world scene;
afterwards `UploadGround` is 0.004 ms at the median at a fixed zoom (0.020 ms across the camera path, where it also sets
the zoom uniform). The ground pass `DrawGround` takes 0.002 ms and `GroundUniformsOf` 0.002 ms a frame at a fixed zoom,
and the rivers' draw 0.001 ms.

**Shimmer (Goal 10)** (`reference`, run first by `measure`; each bound is the first measurement plus the guards' usual
margin, about 1.25 times, never above 0.02 for the three ground cases):

| case | main | measured | bound |
|---|---|---|---|
| world, zoom 0.25 | 0.0481872 | 0.00204363 | 0.0026 (was 0.06) |
| clouds | 0.0204705 | 0.016509 | 0.0206 (cap 0.0256; 0.0291 after the switch-over) |
| ground, zoom 1 | - | 0.01167 | 0.0146 |
| ground, zoom 0.5 | - | 0.00606704 | 0.0076 |
| ground, zoom 0.25 | - | 0.00396805 | 0.0050 |

All five are within the spec's goal, 0.02, and the world case fell by a factor of 24: the ground is no longer a grid of
tile edges.

**Markers on drained land** (the by-eye check: no reference picture shows one, because travellers cover every marker): a
temporary reference case (explored everywhere, the debug sight off, zoom 0.5 over the capital; not committed) shows the
drained land as sepia paper with each danger site's marker, a red or amber pin, in its own colour over it, and the portal
in colour, while the ground, river and stamps around them are drained. Markers take the fog's shade only, never the drain
(Calls made here: "Markers stay undrained"). The markers are tiny at that size, so the verdict rests on the picture and
three crops of it judged by eye.

### Rulings made while building

Rivers and the ground:
- Rivers' spline steps are per tile of segment length: `RIVER_STEPS = 4` quads per band per tile, `MAX_RIVER_QUADS =
  8,192`. The plan's per-segment reading left quads up to 1.7 tiles long, which kink at bends at zoom 1, and per tile at
  8 steps overflowed the cap. 4 per tile gives 64 px quads and 3,448 quads for the shipped map (42% of the cap); a
  constant change and a 0.8 MB buffer if wrong. (The user accepted this, 2026-11-21.)
- "Tile centres" in the river spec means the polyline's stored waypoints' tiles, not every tile the river crosses: rivers
  are stored as sparse waypoints.
- The reader's limits narrow to what the shader can show: `edge_soft` (0, 1], `depth_tiles` (0, 3.5] (the field clamps at
  4 tiles and the shore at -0.5); `edge_warp` [0, 2], shore width and river width and edge (0, 2]; a stamp's size at most
  4 tiles and a mist width at most 2 (shipped maxima 1.7 and 1.4) so stamp layout can rely on a bounded footprint;
  `water`'s shallow and deep kinds must differ. (The user accepted the limits.)
- A river's edge band fades softly outward (its alpha falls across the ribbon from the core to the outer side, still
  anti-aliased): the spec asks for a darker soft edge, and a flat rim read as a hard grey stripe.
- River ends are fixed in presentation (`river_draw`), `world.yaml` untouched: an end on a joined river's middle, and a
  run that stops at most 3 tiles short of water carried a tile in and faded. A tributary meeting the main river within a
  tile of the main river's own end keeps its waypoint end, without a join: rivers carry no flow direction, so that meeting
  can't be told from the main river joining the tributary; it costs at most a tile's gap or overshoot at a confluence
  right at a mouth, and no shipped river has one (if wrong, rivers need a flow order, a data change). The shipped-rivers
  test allows one tile per river and still fails on a band cut short by more.
- Rims darkening where two rivers' edge bands overlap was not visible in the pictures once the tiles went.

Reader and refusals:
- A danger sort the map uses with no painting is refused, as markers are: a place with no painting would draw nothing once
  the tiles go; the two readers stay alike.
- A kind with `variants:` is no longer refused but warned as an unknown key while the look still reads: every look section
  notes unknown keys and reads on, and the plan assumed a refusal the reader never makes.
- `world_look.cpp`'s growth (about 450 lines) is noted, not split.

Stamps:
- A stamp's thinning fade spans 0.3 of `shown` and ranks spread over the rest (`rank = TileUnit(c, r, 9) * 0.7`, opacity
  `smoothstep(rank, rank + 0.3, shown)`); the test stays at zooms 1% apart and at most 0.1 of opacity. `shown` moves by up
  to 0.02 per 1% of zoom and smoothstep's steepest slope is 1.5 / width, so 0.1 needs a width of 0.3; the test is not
  loosened.
- `DrawStamps` lays stamps and puffs out over the span widened by a reach computed from the look and manifest (the tallest
  stamp's height and half-width at its largest size, plus jitter, plus the widest puff's half-size), clamped to the map,
  and the paintings' cull widens by the same reach: the plan's one tile of span popped tall stamps in at every view edge.
- The terrain buffer is relaid on each zoom change (the layout key gains the zoom), the cost measured above.

Shimmer and art:
- The cloud case overran its cap (0.0291 against 0.0256) after the switch-over; the cause was forest and peak stamps' art
  cut by the sides of their quads, crawling at zoom 0.25. The placeholder art (`art/placeholders.py`) now keeps 16
  texels clear of the sides for stamps, 2 for mist and 9 for paintings (the paintings' bottom row and the stamps' foot row
  too); no shader or look change. The cloud case fell to 0.0165 and was re-set to 0.0206.
- `test_terrain_tiles_are_seamless` (tools self-test) is replaced by a guard on the margins above (every stamp, mist and
  painting keeps its art clear of its sprite's left and right sides, and the painted marks their bottom edge as well),
  not just deleted: its subject, the tile sprites, is gone. Removing a test for removed art is not loosening one.
- Forest clumps are drawn smaller to fit the margin (81% for three trees, 58% for five); 13 reference pictures moved and
  were judged by eye.
- The world cases' names that still said "tiles" were renamed to what they draw.

Build and checks:
- The six single-braced fog comparisons at `world_look_tests.cpp:414-434` stay: they come from main (`feat(data): give
  the world look clouds and fog colours`), MSVC has built them green on main and on this branch, and the warning
  (C5246) fires on member initialisers, not on prvalue temporaries in a comparison; if wrong, MSVC's next build names
  them.
- Task 6's by-eye check used `reference` pictures with the terrain buffer switched off locally (uncommitted), never the
  live game, because the display was not freed for this work until the measurements; if wrong, one more look.

Stood as built (noted, not fixed):
- The capital's stacked roofs show a double outline near the spire, and the shrine's torii posts overlap the hut
  (placeholder cosmetics).
- The tributary test's x bounds are loose: it proves "not a waypoint", not "near x = 4".
- `renderer.cpp`'s world-map upload block (terrain, ground, route, clouds) could move to its own file; `ribbons.hpp` in
  the features includes two headers clangd reports unused.
- Gotcha for AGENTS.md: GCC and Clang accept single-braced `std::array` and `std::array`-like initialisers (a
  `StampLook::size{ 1.0F, 1.0F }`) that MSVC's C5246 rejects; run the windows-msvc release build before reporting.
- The `fog_explored_path` picture's drained paper reads strongly sepia: a look value (`drain.strength`) for the user.
- Not visible in stills: that nothing pops while panning or zooming; the unit tests hold it.
