# Phase 6, chunk 5b3: fog and sight on the world map

Date: 2026-11-20 · Status: **approved** · Plan: none yet · Chunk spec: `2026-11-10-phase6-chunk5-world-map-design.md` · Phase
spec: `2026-11-10-phase6-design.md` · Builds on: `2026-11-15-phase6-chunk5b1-map-design.md`,
`2026-11-15-phase6-chunk5b2-moving-design.md` and `2026-11-16-phase6-chunk5c1-world-kept-delta-saves-design.md` (their
Outcomes)

## In short

- **Three states per tile:** never explored, explored but out of sight, in sight. A tile is explored for good once
  it falls within the cultivator's sight; the explored flags are world state (saved, hashed).
- **Sight** is a diamond (Manhattan distance in tiles) round the cultivator's tile, 3 tiles to start, kept in the world
  so later stats can grow it.
- **Unexplored land lies under stylised auspicious-curl clouds (祥云):** small clouds with an ink outline and ink
  spirals, overlapping like scales, the lower ones in front; about 2 across a tile from zoom 1 inward, merging into
  larger clouds as the camera zooms out so they stay about the same size on screen. Each cloud's place, size, outline
  and swirls are random from a fixed hash, so nothing reshuffles. They bob gently; when a tile is explored its clouds
  fade and drift apart over about half a second.
- **Explored land out of sight** is drained of colour towards sepia, under a thin pale mist. **In sight**, full colour.
- **Walkers** are seen only in sight and fade out over the last half tile before its edge; place names and markers
  under the clouds stay hidden.
- **Built as approach A:** a small per-tile fog texture (explored, in sight), eased on the main thread, sampled by the
  terrain's shader for the drain and the mist; the clouds drawn as one instanced sprite batch over the map. Very low
  render cost: never a draw call per cloud.

## Intent

The world map should invite exploring: what lies beyond the cultivator's sight is unknown, what was seen stays known
but goes quiet, and what is in sight is alive. The fog carries the game's look (ink and paper, the painted clouds of
Chinese art) rather than a game-engine grey, and it must cost next to nothing, since the world map is already the
game's heaviest scene. Sight is also the first stat the world map reads from the cultivator, so growing it later (a
cultivation realm, an item, a technique) must need no redesign. Inspired by Tale of Immortal's three-state fog, not a
copy of it (the user's ToI screenshots: flat grey, grey veil, full colour, hard square edges; the user chose soft
painted clouds over the hard squares).

## Approaches

**How the fog is built** (the user picked A, 2026-11-20):

| Approach | Trade-offs |
|---|---|
| **A. A small fog texture and a shader (picked)** | Each snapshot carries the explored flags, the cultivator's tile and the sight radius; the main thread eases each tile's two values (explored, in sight) towards their targets over about half a second and uploads them as a tiny two-channel texture; the terrain's shader samples it for the drain and the mist. Exactly the chosen look, the same cost at any zoom, tunable live; against: a new shader variant and a new texture, and the reference pictures need the clouds' motion frozen at a fixed time |
| B. Sprites only | Cloud puffs on unexplored tiles with 16 edge variants like the roads, pre-made sepia copies of every terrain sprite, fades by each sprite's alpha. No new shader; against: edges follow the tile grid (the hard squares the user turned down), drifting needs animation frames, the terrain sprites in the atlas double, and the terrain is laid out again on every frame of a fade |
| C. One full-screen pass after the world is drawn | Like A, but one screen-space pass works out each pixel's world position and applies cloud and drain there. One shader, the sprite shaders untouched; against: it drains everything drawn there (the cultivator, walkers, seals, route), not only the land |

**The cloud** (the user, 2026-11-20, after mockups of noise mist, papercut, ink-wash and curl clouds): A's first sketch
had a noise-shaded cloud pass; the user chose painted curl clouds instead, drawn as sprites laid out from the fog state
and the zoom (the "cloud variant" of A). The fog texture still does the drain, the mist, the shade under the clouds and
every fade. **Clouds as one sprite batch, or drawn by a shader**, never a draw call per cloud (the user); my call:
sprites (below).

## History

- **Decided before:** fog is a per-tile explored flag (saved), a sight radius round the player, explored tiles out of
  sight show terrain and places but no NPCs (chunk 5 Decision 5), **walkers are seen only within the sight radius**
  (Decision 13), and a **debug sight radius** as large as the view lets the measurements draw thousands of walkers
  (chunk 5's calls). 5b1 shaped the map's layers as flat per-tile arrays with "fog a per-tile flag". Design doc 0002 §2
  sketched "a tint per cell in the instance colour, or a small visibility texture sampled across the map for soft
  edges": A is the second.
- **What exists:** no fog code. `game::WorldState` (`src/game/world_state.hpp`) holds the world (state, relationships,
  travel, clock, pause, `mapPlace`), saved as its difference from the generated base and hashed (`WORLD_VERSION` 6 in
  `src/game/host.cpp`). The terrain is laid out on the main thread straight from the map (`gfx::DrawTerrain`,
  `src/gfx/terrain_draw.hpp`: tiles, overlays, crossings, place markers, one sprite buffer re-laid only when the view
  reaches other tiles) and drawn with the sprite pipeline. Walkers are culled to the view in `Host::PublishJourneys`
  (`src/game/host.cpp`); place names are published by the world scene for each place in view. The world is drawn into
  an offscreen target and composited (`shaders/composite.frag.hlsl`).
- **The map:** 150 × 85 tiles of 256 world units (12,750 tiles); zoom 0.25 to 2; a 1080-line window shows 1080 world
  units down at zoom 1.
- **The look references:** the user's 13 ToI world-map screenshots (memory "world map look references"): faint tile
  grid, painted vignettes, three-state fog with hard square edges, sight a Manhattan diamond of about 3 tiles.
  Inspiration, not a template.
- **Rejected:** hard square fog edges (A of the look question, 2026-11-20: they shimmer, which 5b-look has to fix on the
  tiles anyway, and the grid gets busy at the widest zoom); noise mist, papercut and ink-wash clouds (the mockups'
  other cloud styles); the clouds' first fixed layout without random draws (`cloud-scales`), for the random one.
- **Constraints recorded with the user:** accessibility first (settings never change difficulty; every option by mouse,
  keyboard and gamepad; every UI colour in `core::UiColours`); small unit tests (about 20 samples, never sweeps);
  showcases never overlap; world shimmer guarded at 0.06 (5b-look's target 0.02); logs say outcomes, never steps.
- **Mockups** (gitignored, `.superpowers/brainstorm/95112-1795204427/`): `content/cloud-random.html` and
  `rand-lod-*.png/gif` show the chosen look at zoom 1, 0.6, 0.4 and 0.25, the zoom-out and the uncover;
  `mockup-scripts/lod.py` is the layout they were drawn with (lattice, merge levels, random draws, uncover fade).

## Goals

1. **Explored flags in the world.** `WorldState` holds one explored flag per map tile and the sight radius. A new game
   starts with nothing explored and the radius from the cultivator's entry in `creatures.yaml` (`sight: 3`). Both are
   saved (the flags whole, as a bitset; the radius whole) and hashed with the world (`WORLD_VERSION` 7); a load refuses
   flags of the wrong size. A scratch world (dev scenes) has none.
2. **Exploring.** Whenever the cultivator stands on the world map, every tile within the sight radius of its tile is
   explored, in the tick: when it enters the world scene, at each tile it enters, and when the radius grows. Every tile
   its foot passes through counts, even several in one tick (M's month jump moves it along its route): no unexplored
   gaps behind it. Exploring is deterministic, recorded through the inputs that move the cultivator as today, and never
   depends on the view.
3. **Sight.** In sight: the tiles within the radius of the cultivator's tile (Manhattan distance), on the world map
   only. Derived from the cultivator's tile and the radius, never stored.
4. **The snapshot carries the fog** on the world map: the explored flags (with a version, so the main thread copies
   them only when they change), the cultivator's tile, the sight radius and whether the debug sight is on.
5. **Walkers only in sight.** `Host::PublishJourneys` publishes a traveller only on a tile in sight or one tile beyond
   (the ring beyond lets a walker on a tile that just left sight fade with it rather than vanish). Each walker, its
   shadow and its label are drawn at the opacity of its tile's eased in-sight value, remapped so it falls from full to
   nothing over the last half tile before sight's edge. The view's culling stays as it is on top. A walker drawn at
   under half opacity can't be picked (the main thread's picking skips it, so a click at the cloud's edge walks rather
   than selects something unseen); a selection is never dropped by sight (the tick doesn't change with it, and the
   debug sight, which isn't recorded, can't change state): a selected walker that leaves sight simply fades with its
   label, and shows again if it comes back.
6. **Places under cloud stay hidden.** A place's name is published only once its tile is explored, and it fades in with
   its tile; its marker is hidden under cloud by the terrain's shader (it draws in the terrain's buffer).
7. **The fog texture.** The main thread keeps one value pair per tile, explored and in sight, each easing towards its
   target (0 or 1) over about half a second (`world_look.yaml`), and uploads it as a two-channel texture the size of
   the map whenever a value moved, sampled with filtering so the states blend across tile edges. Entering the world
   scene, loading a save and switching the debug sight snap the values to their targets (no fade).
8. **The terrain's shader** samples the fog texture at each fragment's world position: in sight, the colour as it is;
   explored out of sight, drained towards a sepia tone and under a thin pale mist (the drain's strength, the sepia
   tone, the mist's colour and strength in `world_look.yaml`); unexplored, the clouds' shade colour, so no land and no
   marker ever shows between clouds. The shade's edge stays behind the edge row of clouds, so it never shows as a square
   tile edge. Only the terrain buffer (tiles, overlays, crossings, markers) uses it.
9. **The clouds.** Laid out on the main thread each frame from the eased fog values, the view, the zoom and the render
   time, and drawn as one instanced sprite batch over the world's entities (see the drawing order below):
   - **A lattice of cells:** at merge level 0, cells half a tile across, about 2 clouds across a tile (two anchors per
     cell, staggered like bricks, and in half of the cells a third); each level up doubles the cell. The level follows
     the zoom so the cells keep about 128 px on a 1080-line screen: a continuous level of log2(1 / zoom), floored at 0,
     so level 0 from zoom 1 inward, 1 at zoom 0.5 and 2 (a cloud per 2 tiles) at zoom 0.25.
   - **Merging:** a cell at the current level is drawn whole only when every tile it touches is unexplored; otherwise
     its four children are tried, down to level 0. So merged clouds never reach over explored land, and finer ones stay
     along the edge.
   - **No cloud over explored land:** an edge cloud is placed (moved in, shrunk) so its whole shape stays inside
     unexplored tiles, never clipped mid-outline.
   - **Random, from a fixed hash** of the cell, the level and the anchor: position jitter, width 0.85 to 1.45 of the
     base (so neighbours always overlap and no gap opens), one of the cloud sprites (each with its own silhouette of 2
     to 4 bumps on two base lobes and 1 to 2 ink spirals) and a mirror flip (turning its spirals the other way). Panning
     away and back, or zooming out and in, gives the same clouds.
   - **Lower in front:** drawn back to front by their foot.
   - **Bob:** each cloud moves up and down gently (amplitude and period in `world_look.yaml`, each cloud's phase from its
     hash), at the render time.
   - **Zoom steps cross-fade:** across the middle 0.35 to 0.65 of each step between two levels (as the mockups did), the
     outgoing level's clouds fade out as the incoming level's fade in; most zooms show one level cleanly (zoom 0.6 and
     0.4 both show level 1; the fade is halfway at zoom 0.71 and 0.35).
   - **Uncovering:** as a tile's explored value eases from 0 to 1, the clouds on it fade out and drift apart (each its
     own direction from its hash, a short distance), while the shade under them clears.
   - **No cloud pops:** whenever the layout changes (a tile explored splits a merged cloud into finer ones, a zoom step),
     the clouds that leave fade out and those that come fade in over the same half second.
10. **Debug sight.** `--debug-sight` (and a debug key, F3, toggling it) makes everything in the view count as in sight,
    for publishing and drawing only: it never explores, isn't recorded or hashed (it travels with the view report), and
    the clouds still cover unexplored land (so walkers there draw under them, which is what the measurement wants to
    pay for). `measure`'s world run adds `--debug-sight`, so its walker and label counts stay comparable.
11. **Very low render cost.** The fog adds one small texture upload when values move, one shader variant on the
    terrain's existing draws, and one cloud batch (a few hundred to about two thousand instances at any zoom). `measure`
    shows no frame-time change beyond noise on the world run against the main branch's numbers, the clouds' layout
    under 0.2 ms on the main thread at the widest view.

## Drawing order on the world map

Terrain (fog shader) → route → the snapshot's ground, decals and entities (walkers, the cultivator, seals) → **clouds** →
the target cursor → overlay, text, HUD. The route runs under the clouds where it enters unexplored land; the target
cursor draws over them, so stepping it into cloud with the keyboard or a gamepad always shows where it is.

## Non-goals

- Any other scene's fog (towns, combat scenes, dev scenes).
- A reduced-motion setting for the bob, and recolouring the fog in the accessibility menu: the settings screens
  (chunks 10 and 11) own options; the values sit in `world_look.yaml` ready for them.
- Growing sight from stats: only the hook (the radius in the world) is built.
- Final cloud art: placeholders now, repainted in 5b-look with the rest of the world's look.
- Hiding what unexplored land is from pathfinding: a route into cloud follows the real terrain.
- A map overview, minimap or "reveal all".

## Anti-goals (a failure even if it works)

- **Popping:** a cloud, walker, label, name, marker or drained tile appearing or vanishing from one frame to the next.
- **Square edges:** the tile grid showing in the fog at any zoom.
- **Gaps:** land showing between clouds over unexplored tiles.
- **Cost:** a draw call per cloud, a frame-time change `measure` can see, or a hitch from exploring or the texture
  upload.
- **Shuffling:** clouds changing shape or place when the camera pans away and back, or zooms out and in.
- **Breaking saves or determinism:** exploring that depends on the frame rate, the view or the worker count; a save that
  loads with different flags.

## Decisions

The user's, 2026-11-20:

1. **Approach A:** the per-tile fog texture, eased on the main thread, with a shader for the drain and the mist.
2. **Unexplored land under stylised curl clouds** (祥云): small outlined clouds with ink spirals, overlapping like
   scales, lower in front.
3. **About 2 clouds across a tile close up**, merging into larger ones as the camera zooms out, about the same size on
   screen; random position, size, silhouette and swirls from a fixed hash, always overlapping.
4. **The clouds bob gently; on uncover a tile's clouds fade and drift apart** over about half a second.
5. **Explored land out of sight: colour drained to sepia under a thin pale mist;** in sight, full colour.
6. **Walkers fade out over the last half tile before the edge of sight;** place names and markers under the clouds stay
   hidden.
7. **Sight is a diamond, 3 tiles to start, upgradable later.**
8. **Very low render overhead:** the clouds instanced (one sprite batch) or drawn by a shader, never a draw call per
   cloud.

## Calls made here (not the user's; check them)

- **The level-of-detail details:** about 2 clouds a tile holds from zoom 1 inward; merged clouds never reach over
  explored land (finer ones stay where a merged one would); each zoom step cross-fades. These came with the mockups and
  are recorded as mine.
- **Clouds are sprites, not shader-drawn:** the renderer already draws instanced sprite batches, the art can be
  repainted by hand in 5b-look, and ink outlines stay crisp through the atlas's mips. Variety comes from 16 cloud
  sprites × a mirror flip × the width draw. Placeholder art from `art/placeholders.py`, in the world scene's sprite list.
- **The sight radius lives in `WorldState`,** not on the cultivator: the cultivator is spawned afresh by every scene,
  the world lives on. Its start value comes from the cultivator's entry in `creatures.yaml`, not `world.yaml`: the map
  file is part of the save's base identity (`GameData::BaseHash`), so tuning sight there would orphan every save.
- **The debug sight changes publishing and drawing only, never exploring,** so a measurement leaves no trace in a save.
- **The snapshot carries the flags, the tile and the radius**, not a per-tile in-sight array: the main thread derives
  sight, so the snapshot stays small (1.6 KB of flags, copied only when they change).
- **Cloud sizes, merge zooms, bob, fades and the drain and veil colours go in `world_look.yaml`** (presentation, reloaded
  live), not in `core::UiColours`: they are world art like the terrain's sprites, not interface. Recolouring the world
  for accessibility is a later menu's question (Non-goals).
- **The route stays undrained and goes under the clouds; the target cursor goes over them** (the drawing order above).
- **Walking into cloud is allowed:** clicking an unexplored tile, or stepping the target cursor onto one, walks there
  as anywhere else; that is how the player explores. The route under the cloud is hidden.
- **Exploring happens in the tick, also at the world scene's entry**, so a new game shows its start diamond on the first
  frame and a save made anywhere loads with the same flags.
- **Walkers on the ring just outside sight are published** (one tile), so they fade with their tile; otherwise a step
  that moves sight would drop them at once.
- **The eased values snap on scene entry, load and debug sight switches**, so a load never shows the whole map clouding
  over.
- **F3 for the debug sight** (free among the debug keys).
- **Unseen walkers aren't pickable; sight never drops a selection** (Goal 5): picking is the main thread's, so hiding
  from it changes no state, while dropping a selection would be a tick change the unrecorded debug sight could steer.
- **The existing world pictures turn the debug sight on** and explore their area, keeping their travellers, labels and
  the shimmer baseline; the clouds get a shimmer case of their own, bound set from its first measurement.
- **The zoom levels follow the mockups' numbers** (a level per halving of zoom, the cross-fade over each step's middle
  0.35 to 0.65).

## Testing

- **Exploring:** a new game explores the start diamond (radius 3: 25 tiles) and nothing else; walking a route explores
  every tile within sight of every tile on it, M's jump included; a radius grown explores the new ring; the flags
  survive save and load, and a replay recorded while walking plays back with the same hashes; a load with flags of the
  wrong size is refused.
- **Sight:** a tile at Manhattan distance 3 is in sight, at 4 isn't; the map's edges and corners clip the diamond.
- **Publishing:** walkers beyond sight's ring aren't published, those in sight and on the ring are; with the debug sight
  every walker in view is; a place's name is published only once its tile is explored.
- **The fog's easing:** a value reaches its target in the configured time and holds; snaps on entry and load.
- **The cloud layout** (pure, tested without a device, like `DrawTerrain`): the same view gives the same clouds;
  panning away and back gives the same clouds; a cell touching an explored tile splits; no cloud's shape reaches into an
  explored tile; the levels at zoom 1, 0.6, 0.4 and 0.25 are 0, 1, 1 and 2, each shown alone, and zoom 0.71 and 0.35
  cross-fade two levels half and half; the instance count at the widest view of an all-unexplored map stays within the
  cloud buffer's limit.
- **Pictures** (`reference`, software Vulkan, fixed render time): the start of a new game at zoom 1, 0.71 (the zoom
  step's cross-fade halfway), 0.4 and 0.25; an explored path out of sight (the drain and mist); a tile half uncovered.
  The existing world pictures (`world_close`, `world_middle`, `world_widest`, `world_places`, `world_places_widest`,
  `world_route`) keep what they show today, full-colour land with its travellers and labels: each case explores its
  area in its setup and turns the debug sight on (the harness gains that input). So the shimmer guard (0.06, tiles and
  travellers at the widest zoom) keeps its meaning and its baseline.
- **Cloud shimmer:** a second shimmer case over unexplored land at zoom 0.25 (clouds at rest, fixed render time,
  sub-pixel camera moves as the existing case does); its first measurement sets its bound, with the same margin the
  existing guard has over its measurement, and `measure` reports it beside the world's.
- **Measurements:** `measure`'s world run (with `--debug-sight`) against main's numbers; the clouds' layout time at the
  widest view; recorded in the Outcome.

## Left to the builder

Where the easing and the layout live (an `app` fog fade beside the camera; `gfx/cloud_draw.hpp` pure like
`terrain_draw.hpp`; the renderer owning the texture and the cloud buffer); the texture's format and how the terrain
pipeline gets its sampler; the cloud sprites' size and how the placeholders are drawn; the exact jitter, anchor and drift
numbers (starting from `mockup-scripts/lod.py`); how walker and label opacity reaches the GPU (the main thread scaling
the flagged instances' colour before upload, or the vertex shader reading the fog texture); the save layout of the
flags; how the debug sight travels with the view report; commit split and order.
