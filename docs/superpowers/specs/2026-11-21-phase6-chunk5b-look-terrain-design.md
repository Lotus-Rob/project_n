# Phase 6, chunk 5b-look, part 1: the world map's terrain

Date: 2026-11-21 · Status: **approved 2026-11-21** · Chunk spec: `2026-11-10-phase6-chunk5-world-map-design.md` · Phase spec:
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
