# Phase 6, chunk 5b1: the world map as data

Date: 2026-11-15 · Status: approved 2026-11-15 · Plan: `2026-11-15-phase6-chunk5b1.md` · Chunk spec: `2026-11-10-phase6-chunk5-world-map-design.md` · Phase
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
