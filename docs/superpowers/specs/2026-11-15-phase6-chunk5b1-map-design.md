# Phase 6, chunk 5b1: the world map as data

Date: 2026-11-15 · Status: **implemented 2026-11-15** (see Outcome; approved 2026-11-15) · Plan: `2026-11-15-phase6-chunk5b1.md` · Chunk spec: `2026-11-10-phase6-chunk5-world-map-design.md` · Phase
spec: `2026-11-10-phase6-design.md` · Builds on: `2026-11-14-phase6-chunk5a-measure-camera-design.md` (its Outcome)

## In short

- **A hand-made world map read from data:** `assets/data/world.yaml` holds the terrain grid, the roads and rivers, the
  cities and danger sites and what each kind of ground costs to walk. The renderer draws it instead of 5a's noise.
- **What happens is kept apart from what is drawn:** `world.yaml` is game data (hashed); a second file,
  `world_look.yaml`, is presentation (not hashed, reloaded live) and says how each kind is drawn. Tile blending and
  large painted assets (5b-look) grow only the second file.
- **Ready for pathfinding:** whole-number walk costs, roads and rivers turned into per-tile flags, places as named tiles,
  and each tile's region (its nearest city by walking cost), so 5b2 can build its distance map per place on top.
- **The 50,000 world NPCs move onto the map,** each with a home city, living in it or in its region; the world save's
  version goes up.
- **About six cities and twenty danger sites,** each with a marker and a name label, Chinese in Chinese and the meaning
  in English.

## Intent

5a proved the camera, culling and measurements on a stand-in of noise and circling walkers. 5b makes the map real, and
5b1 is its foundation: a map that exists as data, shaped so the parts after it (paths and travel, fog, a new look) build
on it without reshaping it. It must be cheap to author and change as text now, and must not block what is planned
later: blended terrain, painted areas bigger than a tile, smooth painted roads and rivers, paths that prefer roads,
and perhaps a map painted in an editor one day.

## The split (approaches)

Chunk 5's 5b (the map as data, fog, time, travel, names and labels, click to walk) plus the three backlog items raised
for its design (the world map's look, walking and facing, sprite sharpness) is more than chunk 4b. Split (the user,
2026-11-15: "B", the parts my call):

| Way | What it trades |
|---|---|
| One branch for all of it | One very long branch; a look regression hides among simulation changes |
| **Living map, then look (picked)** | **5b1** this document; **5b2** paths, days, travel, names and labels, click to walk; **5b3** fog and sight; **5b-look** blended terrain, walk frames and facing, sharp art at zoom (perhaps two parts). The look then draws real map data, and fog's cloud is designed with the new tiles |
| Look first | Prettier sooner, but tuned on 5a's noise map and redone when the hand-made map lands |

Each part gets its own spec, plan, branch and PR, and records `measure`'s numbers. 5b1's spec updates the chunk spec's
split paragraph and the phase table.

## Approaches: the map's format

| Way | What it trades |
|---|---|
| **Our own YAML (picked, the user, 2026-11-15)** | A text grid (one character per tile, with a legend), roads and rivers as polylines, places as a list; read by yaml-cpp, which `data` already uses. No new dependency; diffs line by line; authored and fixed as text. No visual editor, but an importer can write the same in-memory model later |
| Tiled's JSON through yaml-cpp | Paintable in Tiled one day, but its layers are one long line of numbers (unreadable diffs) and its shape is Tiled's, not the game's |
| LDtk with LDtkLoader | Best for painting and its rules place transition tiles, but a new package, a JSON hard to author as text, and transitions decided in the editor rather than by 5b-look's design |

**The map is authored by Claude for now** (the user, 2026-11-15), so a text format wins; the user may paint later.

**Pathfinding, decided now for 5b2** (the user, 2026-11-15, "B"), because 5b1's representation must suit it:

| Way | What it trades |
|---|---|
| A search per walker | Simple; thousands of searches at each month step |
| **A distance map per place, plus one search per click (picked)** | At load, one map per place (about 26 of 12,750 entries, ~0.7 MB) giving each tile's cheapest step towards it; a traveller follows its destination's map, no search per traveller; the player's click runs one search. Deterministic with whole-number costs and fixed tie-breaks |
| A navigation mesh (Recast/Detour, zlib, in xmake-repo) | Built for continuous geometry, not a grid with costs per kind; big, float-heavy (a determinism risk), overkill here. Weighed again for towns (chunk 6) and combat scenes (chunk 7) |

**Libraries checked** (the user's rule, 2026-11-15: lean on established non-copyleft FOSS before reimplementing; build our
own when it is needed or more desirable, saying why):

| Piece | Checked | Verdict |
|---|---|---|
| Reading the map | yaml-cpp (have); tmxlite, tmxparser, LDtkLoader (zlib, xmake-repo) | yaml-cpp; the editor loaders are the way in later as an importer |
| Weighted pathfinding (5b2) | Central64 (Autodesk, MIT), tinypile's Jump Point Search (public domain), bensuperpc/astar (MIT), Boost.Graph (BSL-1.0), LEMON (xmake-repo), Recast/Detour (zlib); igraph (GPL) and CXXGraph (AGPL) ruled out | Our own (5b2 re-checks): Central64 and JPS assume uniform cost per walkable tile, so can't prefer roads; Boost.Graph and LEMON take costs but adapting our grid to their graph types is as much code as the search (~60 lines), Boost is heavy, and determinism would rest on their queues' tie-breaks |
| Regions | the same | The same search seeded from every city at once (5b1) |
| Roads and rivers into tiles | Clipper2 (BSL-1.0, xmake-repo) | Our own: marking the tiles a segment crosses is a few lines; Clipper2 is polygon operations we don't need |
| Noise for blending (5b-look) | FastNoiseLite (MIT), perlinnoise (MIT, xmake-repo) | 5b-look decides; likely shader noise |
| Generating the map | Python's opensimplex (MIT), numpy/scipy (BSD) on conda-forge; placeholders.py's own value noise and fbm | placeholders.py's noise: already ours and pure Python, ample for 12,750 tiles; no new pins for a seldom-run tool |

## History

- **Chunk 5's spec (2026-11-10)** decided: a hand-made map inspired by Tale of Immortal's, about 20 screens across, about
  six cities and twenty danger sites, roads, varied terrain; the map in `assets/data/` (format the builder's) with a
  table of tile kinds (terrain, cost to walk, which scene a site enters) and the places, read and checked by `data`; NPCs
  belong to cities and their regions; names in both languages from one choice; travel as a function of route and the
  day's fraction; fog a per-tile flag. Where it and 5a differ, 5a wins.
- **5a (merged 2026-11-15, PR #30)** built the stand-in this replaces (`game/scenes/world.*`): 150 × 85 tiles of 256
  units over `WORLD_MAP` (38,400 × 21,600), ten `Terrain` kinds from low-frequency noise (`TerrainAt`), two seamless
  variants each by a hash (`TileSprite`, `art/placeholders.py`), tiles published per view (`TilesIn`) as ground, hard
  edges between kinds. 5,625 stand-in walkers circle points (`WalkerOrbit`, `OrbitPoint`) in a spread crowd and four
  gatherings, the busiest round `WORLD_BUSIEST` (33,000, 10,800) for `measure`, none within the widest view of the start
  (the map's centre). Every eighth walker has a name label. The world scene has no world NPCs; `stress` has 50,000 and
  the arena 5,000, on a 64-column grid of 4,096 cells (`npcs.hpp` `MAP_COLUMNS`, `CellCentre`, `CellAt`; the host spawns
  them `npc % WORLD_CELLS`).
- **5a's numbers and limits** that bind 5b1: world work median per segment ≤ 2.5 ms (1.58 ms measured), world logic p99
  ≤ 5.5 ms (3.25 ms); world shimmer 0.0309 under a 0.06 guard (the backlog's ≤ 0.02 belongs to the new look, not 5b1);
  labels fade out below about zoom 0.45 at 1080p (chunk 3, Ruling 25); captions have no overlap rules (5a known limit).
- **Game data versus presentation** is already the project's split: `creatures.yaml` and `abilities.yaml` are game data
  (hashed into `core::GameData::Hash`, a replay plays only under the same hash; `game::BuiltInData()` mirrors them for
  runs that read no files); `effects.yaml` and `seals.yaml` are presentation (not hashed, reloaded by debug builds' data
  watch). Tests read shipped files through `tests/support/` (as `seals.hpp` reads `seals.yaml`).
- **Determinism (design doc 0002 §7.4, principle 5):** deterministic within one binary, not across compilers or platforms
  (the user's call, 2026-10-31); no fast-math; transcendental maths through `core::math`. No `-ffp-contract` flag exists
  in xmake.lua; the backlog asked 5b to decide before route maths.
- **The calendar** has only the month step today (M, `ActionKind::AdvanceCalendar`); days within a month are 5b2's.
- **Backlog items left for later parts:** world map look, walking and facing, sprite sharpness (5b-look); the asset
  loading gaps (no decode timeout, whole-atlas mip rebuilds, nothing unloads) matter once 5b-look's art grows.

## Goals

1. **`assets/data/world.yaml`, the map as game data.** Read by the `data` module into a `core` type (call it
   `core::WorldMap`), part of the game data: hashed, so a replay recorded on one map refuses another. Debug builds' data
   watch reloads `creatures.yaml` and `abilities.yaml` live (`ActionKind::ReloadData`, `Host::SetData`); **the map stays
   out of that reload**: the reloaded data keeps the map as loaded (so the hash stays consistent), and a `world.yaml`
   edit shows after a restart, since 50,000 NPCs' homes and tiles point into it. It holds:
   - **The size and the grid:** columns and rows (150 × 85, as 5a, so `measure` stays comparable), the tile size (256
     units), and one text row per tile row, one character per tile.
   - **The kinds of terrain,** each with its character, its name (the key the look file and code use) and its walk
     cost: a whole number, or impassable. Today's ten kinds plus **peaks** (impassable high mountain), eleven in all.
   - **Roads and rivers as polylines** of tile points. A tile a road crosses gets the road flag, which caps its cost to
     the road's; a tile a river crosses gets the river flag, impassable except where a road crosses it (a bridge) or at a
     ford the data names. Lakes and the sea are kinds (deep water, shallows), not lines. **A road never makes deep water
     or peaks passable** (the reader refuses one over them); mountain passes are gaps of rock or snow through the peaks,
     and the isle town is joined across shallows, which a road over them caps like any passable tile (a causeway).
   - **The places:** each with a key, a kind (city or danger site; a danger site also names its sort: haunted forest,
     pass, shrine, cave, cliff, corrupt village…), its tile and its name (a string key in `strings.yaml`, both
     languages). A city also gives how many world NPCs call it home; one city is marked as the start. **No scene yet:**
     5c, which enters places, maps each kind and sort to a scene when there are scenes to name (the `data` module can't
     see game's scene list, and the town and danger scenes don't exist).
   - **The reader refuses,** naming the line: a row of the wrong length or a character the legend lacks; a kind without
     a cost; a polyline point off the map, or a road over an impassable tile; a place off the map, on an impassable tile
     or sharing a tile; not exactly one start; a city that can't be reached from the start over passable tiles. Unknown
     keys warn, as the other readers do. A place name with no string is refused where both tables are in hand (at
     start-up, after the strings load; the builder places the check).
2. **What is derived at load, never authored.** The `core` type holds flat per-tile layers, row by row: the kind (one
   byte), the flags (road, river, bridge, ford), the place on the tile (or none), the walk cost (whole numbers), and the
   **region**: the city each tile belongs to, the nearest by walking cost (one search seeded from every city at once,
   ties to the lower city index, then tile order). Derived from the data alone, so the same every run; not saved. The
   polylines are kept too, for 5b-look to draw as smooth lines.
3. **`assets/data/world_look.yaml`, presentation.** How each kind is drawn (today's tile sprites and variants), the road,
   river, bridge and ford overlays, and each place sort's marker sprite. Read into a `core` type of its own, checked
   against the manifest, not hashed or saved, reloaded by the debug data watch like `effects.yaml` (the old table kept on
   an error). Its sprites come from `art/placeholders.py` (new: peaks, road, river, bridge and ford overlays, a city
   marker, a marker per danger sort) through `project_n atlas`. **Room for 5b-look:** free-placed painted assets
   ("stamps": a sprite, a position in world units, a scale, a layer, ground or upright) and the blending method belong
   in this file, so neither touches the game data; 5b1 adds neither.
4. **The map is drawn on the main thread, from the map and the look.** Like seals (named by key in game, drawn from
   their table in gfx), the static map is presentation's to draw: `gfx` gets a terrain feature that draws the tiles by
   kind (5a's two seamless variants by hash), the road, river, bridge and ford overlays and the place markers straight
   from the immutable `core::WorldMap` (which App already holds with the game data) and the look table, culled to the
   camera's view and drawn whole, under the snapshot's ground. The logic stops publishing tiles (the world scene's
   `PublishGround` goes back to drawing nothing for it), so a look reload needs nothing from the logic thread, and
   5b-look's blending, which needs the kind grid in the renderer anyway, starts from here. The snapshot says when a
   scene shows the map (the builder picks how: a scene trait carried in the snapshot). The drawing must match 5a's tile
   quads, so the reference pictures change only by content and shimmer stays under its guard.
5. **The logic publishes the place labels.** Each place in view gets its name label over its marker (chunk 3's label
   rules: scaling, fading, stacking, priority, in the player's language). The cultivator starts on the start city (the
   capital, the map's centre), read from the map; the portal stays a short walk from it.
6. **Text named by data, not by code.** Today every label's text is a `core::StringId` (a hand-written enum,
   `core/strings.hpp`), so a place name would need a code edit per place. 5b1 adds strings named by a key from data:
   `strings.yaml` entries no code names are kept by their key, a place's name resolves to one at start-up, and a
   `TextItem` can carry it. Built once here; 5b2's generated NPC names (composed from name tables) extend the same path.
   The builder shapes it (an index into a runtime table beside the enum is enough).
7. **The world NPCs live on the map.** The world scene spawns the sum of the cities' shares (50,000 in the shipped map,
   pinned by a test as `measure`'s load). Each city's share lives in the city's tile or the tiles of its region, by a
   draw from `core/random.hpp`; an NPC keeps its home city.
   Their position becomes a map tile: the world domain's components change, so the world save version goes up
   (`WORLD_VERSION` 2) with what it saves. `stress` and the arena keep their own grid and counts. Their monthly
   simulation is unchanged (socialising meets NPCs on the same tile).
8. **The stand-in walkers stay until 5b2,** re-homed so `measure` stays comparable: the busiest gathering and three town
   gatherings sit on four cities, the busiest kept at `WORLD_BUSIEST` (the port), none within the widest view of the
   start, and the spread crowd on passable tiles only. **What stays in code until then:** the map's size
   (`WORLD_COLUMNS`, `WORLD_ROWS`, `WORLD_MAP`, which the stand-ins' compile-time checks use) and `WORLD_BUSIEST` (the
   measurement path's anchor in `app/camera_path`); tests pin the shipped map to both and say why, and 5b2, when the
   stand-ins go, can make the size data.
9. **The map's content.** Loosely xianxia (Tale of Immortal as inspiration only): snow peaks and foothills in the west
   with a sect mountain town; desert and steppe in the north with an oasis town; central plains with the capital at the
   map's centre; a great river from the western mountains to the eastern sea, crossed by bridges; the port on the east
   coast at `WORLD_BUSIEST`; marsh and jungle in the south with a marsh town; islands off the coast with an isle town.
   Roads join every city. About twenty danger sites, mostly off the roads and more of them at the wild edges. Place names
   in Chinese (characters) and in English (their meaning: 青云城, "Azure Cloud City").
   **Organic, not gridded (the user, 2026-11-15: "organic and aesthetic, matches real world biome/generation and isn't
   just overly square, stale or grid-like").** The layout comes from how real land forms, not from drawn rectangles:
   - **height** from layered, domain-warped noise under a hand-shaped continent mask (the western range as a ridge, the
     eastern coast and islands), so coasts are ragged and ranges branch;
   - **rivers** that run downhill from the heights to the sea or a lake (flow accumulation over the height field), so they
     meander, join and widen into deltas; the great river is the largest of them;
   - **biomes** from height, temperature (cooler north and high) and moisture (wetter near water and on the windward side,
     a rain shadow behind the range), a Whittaker-style table: snow and peaks high, rock and hills below, forest where
     wet, grassland and meadow on the plains, steppe and desert in the dry north, marsh and jungle in the wet south, sand
     on coasts;
   - **cities where people would settle** (on rivers, coasts, plains and passes), **roads** as least-cost paths over the
     terrain between them, simplified to polylines, so they bend round hills and cross rivers at narrow points, and
     **danger sites** where their sort belongs (passes in gaps of the range, caves in hills, haunted forests deep in
     woods, shrines on heights and cliffs, the corrupt village in the marsh);
   - no straight edges, rectangles or isolated single tiles of a kind (a smoothing pass removes specks), checked by eye on
     a preview picture (one coloured pixel per tile, enlarged) before committing.
   Generated by a seeded script, `art/world_map.py` (kept, beside `art/placeholders.py`, reusing its pure-Python noise;
   no new dependency), whose hand-shaped inputs (continent mask, city sites, seed) live in the script; it writes
   `world.yaml`, which is then hand-tuned and is the source from then on (regenerating starts over, and says so). The
   256-unit tiles still show hard steps between kinds until 5b-look's blending; the shapes themselves must already read
   as natural land.
10. **Floating-point contraction, decided.** The guarantee is determinism within one binary (0002 principle 5, the
   user's 2026-10-31 call); a binary's fused multiply-adds are fixed in its code, so contraction can't make one binary
   disagree with itself, and the route maths in 5b2 (whole-number costs; floats only to place a walker along its route)
   needs no flag. Record the decision in 0002 §7.4 and close the backlog item; revisit only if replays or saves ever
   have to match across builds.
11. **The measurements:** `measure` on the world before and after, the same sitting; record the numbers and the month
   step's cost with 50,000 world NPCs on the map in the Outcome. 5a's budgets hold.

## Non-goals

- Paths, click to walk, days within the month, travel, NPC names and labels (5b2); fog and sight (5b3).
- Blending, stamps, smooth road and river drawing, walk frames, sharper art (5b-look).
- Entering places, the world-and-scene split, the save version for that (5c); places' scenes are only checked.
- A map editor importer (Tiled, LDtk), procedural generation, a minimap.

## Anti-goals (a failure even if it works)

- **Art deciding the rules:** anything in `world_look.yaml` changing what a walker can do, or the game data hash.
- **A map change needing a code change:** moving a city, a road or a coast, or adding or renaming a place, is a
  `world.yaml` (and `strings.yaml`) edit only, sprites aside. The two exceptions until 5b2 (the map's size and the
  port's place as the measurement anchor) are pinned by tests that say why.
- **A second copy of the map kept by hand,** in code or anywhere else.
- **`measure` regressing** past 5a's budgets, or world shimmer past its guard.
- **A map that doesn't read:** cities without roads between them, labels piled on each other at the middle zoom, danger
  sites on top of cities.

## Decisions

The user's, 2026-11-15:

1. **Split B:** living map (5b1, 5b2, 5b3) then the look (5b-look); the parts and their number Claude's call.
2. **Claude authors the map for now;** efficient storage and existing libraries considered.
3. **Our own YAML format** (approach A), revised so the map supports future tile blending and larger painted
   tiles, areas and assets: game data and presentation in two files.
4. **Pathfinding by a distance map per place,** plus one search per click (approach B, for 5b2).
5. **Lean on established non-copyleft FOSS libraries** before reimplementing; reimplement when needed or more desirable.
6. **The 5b1 playback** (size, geography, translated place names, NPCs in cities and regions, `stress` keeping its own
   NPCs): "Sounds good".
7. **The map is organic and aesthetic, matching real-world biomes and land forming,** never square, stale or grid-like
   (Goal 9).
8. **Diagonal steps stay** (asked after the user's look at Tale of Immortal's orthogonal-only travel): the step rule
   as built, diagonal = the entered tile's cost × 7 / 5, only past two passable orthogonal neighbours; for 5b2's paths
   too.

The chunk spec's decisions 2, 3, 5, 9, 10 and 14 carry into 5b1 (the map, its scale, hand-made content, NPCs in
cities); the rest wait for 5b2, 5b3 and 5c.

## Calls made here (not the user's; check them)

- **150 × 85 tiles of 256 units,** unchanged from 5a, so the numbers compare.
- **Eleven kinds:** today's ten plus impassable peaks. Walk costs, relative to grassland 10: road 4, meadow 10, sand 14,
  forest 16, hills 20, marsh 25, shallows 30 (only at fords and coasts), snow 30, rock 40, deep water and peaks
  impassable. Tunable in the data.
- **Rivers block except at bridges and named fords;** lakes and the sea are deep water.
- **Regions by walking cost,** not straight distance, so a mountain range divides them.
- **Place names translated in English,** pinyin kept for NPC names (the chunk spec's decision 8).
- **The capital at the map's centre,** where the cultivator starts; the port at `WORLD_BUSIEST`.
- **NPC shares per city in the data,** the capital largest; about 30% of each city's share in its region's tiles rather
  than its own tile.
- **No `-ffp-contract` flag** (Goal 10). This revises what the 5b1 playback said (contraction off), after reading 0002's
  guarantee.
- **No built-in mirror of the map:** `BuiltInData()` carries none, and the world scene requires a map (building it
  without one is a broken invariant, not an empty scene). Every test that builds or cycles into the world scene (host,
  save, scenes, camera path, showcase, reference) takes the shipped data through a `tests/support/` helper (as seals
  do); unit tests of the reader and the derived layers use small maps written in the test.
- **The map is drawn by `gfx` on the main thread** (Goal 4), not published tile by tile by the logic: the seals'
  precedent, a live look reload with no new channel to the logic, and the kind grid already in the renderer for
  5b-look's blending. The risk is a new gfx feature against the reference pictures and the shimmer guard; matching 5a's
  quads exactly bounds it.
- **Strings named by data** (Goal 6), built here because place names need them and 5b2's NPC names need the same path.
- **The map stays out of the debug data reload** (Goal 1): a `world.yaml` edit needs a restart.
- **Roads never cross deep water or peaks;** passes are rock or snow gaps, the isle town a causeway over shallows.
- **Places carry no scene until 5c.**

## Testing

- **The reader:** parses the shipped file; each refusal above, with its line; unknown keys warn; a place name with no
  string refuses at start-up.
- **Derived layers:** a road caps cost, a river blocks, a bridge and a ford pass; regions follow cost across a ridge;
  ties break the same way every run (a small map with an exact tie).
- **Look file:** parses the shipped file; a sprite the manifest lacks refuses; a broken reload keeps the old table.
- **The terrain feature (gfx):** the tiles in view drawn by the map's kinds and the look's sprites; overlays on road and
  river tiles; a marker per place; a look reload changes the next frame's sprites; nothing drawn for a scene without
  the map.
- **The world scene:** a label per place in view, both languages, by its data key; the start on the start city; the
  logic publishes no tiles; culling as 5a's tests.
- **Strings by key:** a key from data resolves to its text in both languages; an unknown key refuses at start-up.
- **World NPCs:** 50,000 spawned, each homed in a city, standing in its city or region; save and load round-trip at the
  new version; an old-version world save refuses.
- **Stand-ins:** gatherings on their cities, clear of the start, on passable tiles.
- **Determinism:** the game data hash changes when the map does; a replay on the world scene plays back with the same
  hashes.
- **Pictures:** `world_close`, `world_middle` and `world_widest` refreshed for the new map; a picture at a middle zoom
  where place labels read.

## Left to the builder

The YAML's exact shape and the legend's characters; the `core` types' names and where the map hangs off the game data;
the polyline rasterisation rule (every tile a segment touches, by a supercover line); the danger sorts' list; the map's
exact layout within Goal 9; marker art; the label style for places; the measurement sitting; commit split and order.

## Outcome

### Measurements (2026-11-15)

Taken by plan Task 13, one sitting, display free, nothing else running: `pixi run project_n measure --runs world --keep`
once on main (a worktree of main inside the repo, at "feat(app): zoom at a steady rate, holding notches through
transitions") and once on the branch head (at "test(gfx): picture the world map's places, roads and rivers"). Both
exited 0, trusted, no hitches, every row `paced` (the 165.1 Hz DP-3 display, as 5a's sitting; same machine). Times in
ms, each cell main / branch. Frames: 82 in each first-0.5 s segment, 577 to 579 in the rest, 908 or 909 in the widest
view. Main's numbers sit inside 5a's three runs (work medians within the 0.15 ms noise line), so main is a valid base.

| Segment | Frame median | Frame p99 | Frame worst | Work median | Work p99 | Work worst | GPU share |
|---|---|---|---|---|---|---|---|
| follow: first 0.5 s | 6.01 / 6.08 | — | 7.57 / 7.65 | 0.60 / 0.62 | — | 0.77 / 0.96 | 90 / 90 % |
| follow: rest | 6.04 / 6.05 | 7.89 / 8.57 | 8.79 / 10.07 | 0.75 / 0.76 | 1.77 / 3.45 | 3.55 / 4.24 | 87 / 86 % |
| pan: first 0.5 s | 6.05 / 6.06 | — | 9.26 / 8.63 | 1.04 / 1.05 | — | 4.12 / 3.89 | 79 / 79 % |
| pan: rest | 6.06 / 6.10 | 9.23 / 9.05 | 10.49 / 10.93 | 1.10 / 1.07 | 4.29 / 4.09 | 4.47 / 5.18 | 79 / 79 % |
| zoom in and out: first 0.5 s | 6.09 / 6.12 | — | 9.96 / 9.14 | 1.00 / 1.04 | — | 5.17 / 3.55 | 80 / 80 % |
| zoom in and out: rest | 6.06 / 6.08 | 8.81 / 8.83 | 10.08 / 10.14 | 1.03 / 1.06 | 3.65 / 3.65 | 4.44 / 5.07 | 80 / 79 % |
| widest view: first 0.5 s | 6.04 / 6.13 | — | 9.38 / 8.98 | 1.45 / 1.40 | — | 4.35 / 4.51 | 74 / 75 % |
| widest view: rest | 6.02 / 6.08 | 9.35 / 8.89 | 12.03 / 10.76 | 1.47 / 1.50 | 4.43 / 4.35 | 6.07 / 6.27 | 73 / 72 % |
| labels: first 0.5 s | 6.04 / 6.11 | — | 8.70 / 9.43 | 1.16 / 1.29 | — | 3.57 / 4.43 | 81 / 75 % |
| labels: rest | 6.03 / 6.04 | 9.33 / 9.53 | 10.07 / 9.84 | 1.12 / 1.20 | 4.03 / 4.33 | 5.50 / 4.93 | 80 / 76 % |

The other threads (median / p99 / worst, main / branch). The main thread's work equals *work* within 0.01 ms in every
cell. Logic medians are mostly 0 (a tick starts in few frames) and mean nothing; the workers' medians are 0.

| Segment | Logic p99 | Logic worst | Worker 3 p99 | Worker 4 p99 |
|---|---|---|---|---|
| follow: rest | 2.32 / 2.31 | 2.47 / 2.39 | 0.98 / 0.95 | 0.91 / 1.00 |
| pan: rest | 2.32 / 2.40 | 2.39 / 2.91 | 1.05 / 1.00 | 0.90 / 0.98 |
| zoom in and out: rest | 2.52 / 2.31 | 2.62 / 2.55 | 0.90 / 0.97 | 1.00 / 0.90 |
| widest view: rest | 3.21 / 2.94 | 3.34 / 3.17 | 1.02 / 1.06 | 1.00 / 1.03 |
| labels: rest | 2.71 / 2.58 | 3.21 / 2.84 | 0.93 / 0.95 | 0.99 / 1.03 |

Also: **input latency** 24.7 / 23.1 ms (5a: 18.5 to 23.8); **world shimmer** 0.0309 / 0.0397 (guard 0.06; see Ruling S).
Raw output kept locally while the branch lives (`.superpowers/sdd/`, gitignored); the capture folders were
`build/measure/20261115-194444` (main) and `20261115-194750` (branch).

**Reading them.** 5a's work and logic budgets hold on the branch, with no hitches: the heaviest work median is 1.50 ms
(widest view; budget 2.5), the heaviest work p99 is 4.35 ms (widest view; budget 5.5) and the heaviest logic p99 is
2.94 ms (budget 5.5). One of 5a's budgets does not hold as written: world shimmer, ≤ 0.031 in 5a's budgets table, is
0.0397 on the branch. That is the approved 5b1 spec's own change, not a regression: its goals line (written in "docs(design):
design 5b1, the world map as data") replaces the 0.031 ceiling with a 0.06 guard, because the map's kind edges and
overlays add shimmer the noise ground did not have (Ruling S). Against main, the work medians move by at most 0.13 ms (`labels: first
0.5 s`, 82 frames, so noisy; `labels: rest` moves 0.08), inside 5a's 0.15 ms line. One p99 moves by more than 5a's 0.5 ms line: `follow: rest` work p99 (1.77 to 3.45 ms), inside the spread 5a saw
between its own runs of the same unchanged scene (2.99, 3.13 and 1.25). That is a single run, unconfirmed: no second
`measure` run was taken (the user's call at finishing, 2026-11-15), so it is neither shown to be noise nor shown to be a
regression; it sits inside 5a's 5.5 ms work p99 budget either way. The rest of the world's per-frame cost is unchanged
within noise, as expected, since the map's tiles are drawn by the same quads as 5a's noise (Decision: matching 5a's quads
exactly) and the labels add a handful of names.

**Ruling M: the world run's walker counts changed, so main against the branch is not like for like.** The busiest view
retuned from 58x50 to 60x52 tiles (3,120 walkers) and walkers whose orbit would stand on sea or peaks redraw onto
land (Ruling L), so the branch's world run draws different crowds from main's at the same camera positions. The
branch's numbers are the world run's new baseline for 5b2 onward; main's are kept only to show the cost did not move.

**The month step: 1.78 ms median.** `measure` sends no input and no flag presses the calendar key (a debug
control, `Control::AdvanceCalendar`), so the step was timed by hand: `pixi run project_n profile --seconds 40 --
--world --workers 2` (releasedbg, windowed), the user focusing the window and pressing M, then the `MonthStep::Run`
zone (`src/game/npcs.cpp`, the logic thread) read from the capture with `tracy-csvexport -u -e`. 23 steps with the
50,000 world NPCs homed in cities: min 1.24 ms, median 1.78 ms, max 8.48 ms. 20 of the 23 took 1.24-3.45 ms; the
three slow ones (6.15, 6.40, 8.48 ms) each came 0.12-0.15 s after the step before (a quick double press), though a
fourth, 0.11 s after the one before, took 1.78 ms. Either way a month step costs well under one 60 Hz frame of logic time, and the
logic tick that runs it stalls by design (`Logic::RunTick`). Making the step scriptable, so `measure` can time it
unattended, is left for 5b2, whose days within a month give the calendar a real driver.

### Rulings made while building

The plan ledger's calls, each with its reason and what it costs if wrong. Commits are named by subject.

**The map and its reader.**

- **A ford on a tile without a river, or on an impassable river tile, is refused (A, and the review's Important).** A
  ford is by definition a river crossing and an unchecked one silently does nothing or opens deep water. A ford on a
  bridged tile is refused too. Cost if wrong: one refusal to remove.
- **Duplicate place keys are refused (C).** Keys are identities later tasks look up (capital, port).
- **Walk costs are bounded (MAX_WALK_COST 1000, path sums in u64) and rasterising a road is direction independent
  (Task 1's review).** An overflow or a tie decided by direction would change the hash.
- **`WorldMapSource` carries optional line numbers (D),** so `Build`'s refusals read "line L: ..." and the reader fills
  them instead of parsing text. `road_cost` and `ford_cost` refusals carry no line (F); the reader prefixes them with
  its key's line.
- **Danger sites are exempt from reachability (E);** the spec asks it of cities only. Cost: 5c adds the check.
- **Strings named by data are capped (MOST_STRINGS) and refuse past it,** so the u16 id cannot wrap (Task 2's review).
  Ruling B: Task 2 rewrote the old "unknown top-level key warns" case to the new rule (a key with both languages is a
  data string, no warning); not a loosened test, the behaviour changed.
- **The look may give kinds and markers the map doesn't use (G);** only a kind or sort the map uses and the look lacks
  refuses, but every sprite the look names is checked against the manifest. A reload keeps the old look on a refusal
  (`KeepLook`, tested in `data_watch_tests`).
- **Shipped-map tests needing game constants live in the game suite (I);** data sits below game and includes no game
  header (`tests/unit/game/shipped_world_tests.cpp`).
- **Names: "Immortal Isle" (K, then the user).** First kept "Penglai Isle" as the mythic proper name; the user chose a
  name by meaning, as the other places (`chore(data): name the eastern isle Immortal Isle in English`). 幽灯林 is
  "Ghostlight Woods".
- **The user's call on the first map preview:** the north-east river ends in the sea or a lake, and the oasis lake gets a
  deep-water middle; the generator then smoothed straight shores in three rounds (longest straight shore 10 to 5 tiles,
  the port's coast pushed by noise up to ~7 tiles).
- **Diagonal steps stay on the world map** (`docs(design): keep diagonal steps on the world map`).

**The world scene.**

- **Gatherings and spread walkers redraw off impassable tiles (L).** The plan named only spread walkers because 5a's
  gatherings sat inland; the spec's "on passable tiles" governs. Cost: denser crowds on land, which is why the busiest
  view retuned to 60x52 (3,120 walkers): the redraws left 1,887 walkers in the busiest view, so the grid was retuned from 58x50 to 60x52.
- **The port's pile limit is 210 (the user's call).** 5a's two bounds conflicted at the coastal port: the busiest
  16-screen window needs 2,000 walkers or more (125 a screen on average) while no screen may hold more than 150, with
  the window about 35 to 40% sea. No retune satisfied both (a sweep found 206 at (31240, 9580)). The user raised the
  port's limit (`PORT_PILE` 210; the crowd of the port alone); the 2,000 in the busiest view stays so measurements
  compare, and the pile test samples the real densest screen.
- **Place names sit below the tile centre (T).** Two defects hid the start city's name: a label faded for size still
  took room in the label pass (so the cultivator's invisible label hid the capital's name at the widest zoom), and the
  anchor 60 units above the tile lay on the label of a cultivator standing on the city. Fixed test-first in two commits,
  `fix(gfx): let labels too small to read give way to the rest` (such a label takes no room, still fades out) and
  `fix(game): name each city under its tile, clear of a cultivator there` (anchor below, marker size + 8). Cost if
  wrong: a reverted constant and rule, pictures refreshed.
- **`world_places` at LABELS_ZOOM 0.6 over the capital, 1920x1080 (R); `world_places_widest` over the capital too (Q,
  V).** The widest picture was first to pair the capital with its nearest other place (amended Q, U); the review found no
  pair of names fits a 1080p widest view with margin on the shipped map, so it is centred on the capital and pins fix
  (a); names beside names stay covered by the label-pass unit tests. `world_widest` stays over `WORLD_BUSIEST`, the view
  `measure` and the shimmer case share. Cost: a second picture if a pair ever fits.
- **Shimmer is recorded (S):** the world scene's shimmer is 0.0397 (5a: 0.0309), under the guard of 0.06, which replaces 5a's
  ≤ 0.031 shimmer budget (5b1's goals line); the map's kind edges and overlays add a little. The backlog item's acceptance check stays "world shimmer <= 0.02" (5b-look).

**World NPCs.**

- **Each city's NPCs home on a tile index (N, O, P).** In a scene with the map, `Npc::cell` is a `WorldMap` tile index
  (column + row x `WORLD_COLUMNS`); arena and stress keep the 64-column grid. `home` is a city's place index; a load
  refuses a home that is not a city's, or a cell past the map, and any home but `NO_HOME` in a scene without it. The
  world scene's `worldNpcs` trait stays 0 and its Build spawns the cities' shares in place order, the first
  floor(share x 7 / 10) on the city's tile and the rest on its region at a keyed draw (`PURPOSE_HOME_TILE` 10;
  purpose 9 retired). Because `Npc` is in the active components too, `ACTIVE_VERSION` went 4 to 5 and `WORLD_VERSION`
  to 2; both save fuzz seeds regenerated, an old save refuses. Cost: a conversion.
- **The three towns round the sect, the oasis and the marsh town (J):** the port already holds the busiest gathering,
  so "the first three other cities" skips both the start and the port.

**Review and process.**

- The reader is fuzzed (`tests/fuzz/corpus/`), as tests/AGENTS.md asks of every reader (Task 5's review). Its 60 s run
  met an ASan container-overflow inside yaml-cpp's `ScanDirective`, reached from `ParseGameData` too: pre-existing and
  already in the sweep-up backlog (chunk 4b seals, Ruling 9); no action here.
- `KeepLook` takes its `std::expected` by value, a sink parameter moved into `current` on a change. The variant test
  keeps its own copy of 5a's seed and purpose constants as an independent oracle; a failure reports a count, not the
  tile.
- The version-4 refusal test has no version-5 positive control of its own: the save round-trip tests load the same
  shapes.
- "Route costs are whole numbers" in the docs states 5b2 work already true of 5b1's map (Task 4).
- **At finishing:** a world whose cities' people sum past `MOST_WORLD_PEOPLE` (100,000, twice the shipped 50,000, the
  tier phase 4's gates were set for) is refused, so no hand edit makes `SpawnPeople` allocate without bound (GLM's
  finding); cost if wrong: one constant. The seals' twin of the look's sprite-list gap goes to the sweep-up backlog
  (pre-existing, `seals.yaml` is 4b's). Lines over 120 columns in `art/` stay: no tool or rule sets 120 for Python or
  TOML (clang-format's 128 is C++'s, ruff's defaults skip E501). The knot where road and river arms meet stays with the
  placeholder art (smooth lines are 5b-look's). `Host::Portal`, `OnPortal` and `PublishPortal` stay non-const: `Context`
  takes `Host&`, as `Publish` already does. `Strings::text` stays public: the key index covers only data keys, which
  only `Add` writes, while `text` also holds the code-named strings the reader fills by id.

### Fixed at finishing

The final whole-branch review's findings and the gate's build failures, each fixed on the branch (commits by subject):

- **GCC release builds:** `-Weffc++` wanted initializers on the world map's, the look's and the game data's members
  ("refactor(core): initialise the world map's members for GCC's -Weffc++").
- **MSVC release builds** (only in `check`): `Strings::Find` searches a named span (an array iterator is a class there),
  the host's NPC check deduces its array with `std::to_array` ("refactor(core): pass MSVC's warnings in the strings lookup
  and NPC check"); the new tests name every value before `CHECK` (C4866) and brace `Strings::Add`'s arrays twice (C5246)
  ("test: name values before CHECK so MSVC's release build passes").
- **Unity builds** (only in `check`): the map reader's anonymous `WHAT` and `Refuse` clashed with the seals' and the
  manifest's in one batch; the readers' constants are named apart ("refactor(data): name the world readers' file
  constants apart").
- **ASan in CI** (macOS and Windows address jobs): two tests bound a `SpriteInfo&` into the manifest a temporary
  `shared_ptr` from `test::TestManifest()` freed at the end of the line; both hold the manifest now ("test(game): hold
  the manifest a test's sprite points into").
- **The look reader is fuzzed** (Important): the `yaml` target reads each input as a look against the shipped manifest and
  the `world` seed's map, seeded with a small look (`world_look`); a 60 s run found nothing ("test(tests): fuzz the world
  look reader").
- **A replay on the world scene** (Important; the Testing section's): a walk across a month step on the shipped map plays
  back with every hash matching ("test(game): replay the world scene with every hash matching").
- **A danger with people, two kinds of one name, and cities homing more than `MOST_WORLD_PEOPLE` (100,000, twice the
  shipped 50,000) are refused**, each naming its line; the shipped-map test sums the cities only ("fix(core): refuse
  dangers with people, twin kind names, overfull worlds").
- **A look sprite outside the world scene's sprite list is refused** (both reviewers): it would draw from a page that
  isn't resident; checked against the manifest scene `data::WORLD_LOOK_SCENE` names, which an app test pins to game's
  world scene ("fix(data): refuse a world look sprite the world scene doesn't load").
- **One owner for a point's tile and the start city's centre:** `WorldMap::TileAt` and `WorldMap::StartCentre` replace
  the world scene's, the scenes tests' and the reference tests' copies ("refactor(core): give the world map one owner for
  tiles of points").
- **`Strings` keeps a key index** (deferred minor): Add fills it and Find reads it, the reader's own set is gone, and a
  `ParseStrings` overload taking the cap lets a test reach the "too many strings" refusal ("refactor(core): index data
  strings by key in Strings"). The overload is public: the review asked for one in `src/data/internal/`, but `lint`
  refuses an internal header outside its module, the tests included.
- **The seals' keep-or-replace decision is tested in app** (deferred minor): `app::KeepSeals`, as `KeepLook`; the seals
  test that checked a table that could never change now checks only the refusal, under a name that says so
  ("refactor(app): test the seals' keep-or-replace decision in app").
- **Docs:** the world scene's Traits and Build name the cities' people ("docs(game): say the world scene spawns the
  cities' people"); a danger sort is "a key the data names", not a data string ("docs(core): call a danger sort a key the
  data names, not a data string"); the grid-cell NPC functions say they apply only without the map, and Activate and
  Deactivate assert a homeless NPC ("refactor(game): keep grid-cell NPC functions to scenes without the map"); the
  shimmer bound's ratio to its measure is stated right ("test(tests): state the world shimmer bound as 1.5 times its
  measure", then 1.4 times after the harbour).
- **No straight kind edge of 8 tiles or more** (Goal 9, deferred minor): twelve runs of 8 to 11 tiles jogged by hand and
  the harbour's deep pool reshaped (the user's call); the busiest view keeps 2,051 walkers (2,000 needed) and the port's
  densest screen 206 (PORT_PILE 210); world shimmer 0.0436 (0.0397 before, guard 0.06), `world_widest` and
  `world_middle` refreshed ("chore(data): jog the world map's straight kind edges").
- **`follow: rest` work p99** is marked a single unconfirmed run above, no second run taken (the user's call).

### Known limits

- **A month step is timed by hand only** (above): no flag or camera-path step drives the calendar yet.
- **A place name can still lose to a creature label** where the label pass finds no room (Player outranks Place in
  priority); Ruling T only cleared the start city's name.
- **Placeholder art:** a faint knot where road and river arms meet at a tile centre; pre-existing over-120-column lines
  in `art/placeholders.py` and `art/scenes.toml`. No straight edge between two kinds runs 8 tiles or more (checked at
  finishing, the harbour's deep pool reshaped by hand); runs of 6 and 7 remain, which 5b-look's blending softens.
- **A seals table may name a sprite outside the scene it draws in,** the gap the look's reader now closes for the world
  (the seals' reader checks only the manifest): pre-existing (chunk 4b's), left to the sweep-up backlog.
