# Phase 6 chunk 5b-look, part 1: the world map's painted terrain, implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The world map's square tiles give way to painted paper: soft per-kind washes with noisy edges, water deepening
from an inked shore, wide painted rivers, sparse stamps with mist and a veil, painted places under small markers, a much
fainter grid, at 0.02 shimmer or under and no visible cost.

**Architecture:** `gfx` builds a per-tile texture (kind, shore distance) from the map and the look (`gfx/ground_field.*`,
pure) and draws it with a new ground pipeline (`shaders/ground.*`) as one full-view pass at the bottom of the world map,
and again in a veil mode over the stamps. Rivers become ribbons built once from the map's polylines
(`gfx/river_draw.*`, pure) and drawn by a river pipeline. Stamps, mist puffs and place paintings are laid out per view
like today's terrain (`gfx/stamp_draw.*`, pure) into the terrain's sprite buffer. The fog's land treatment moves into a
shared `shaders/fog.hlsli` that the sprite, ground and river shaders all use. Every look value lives in
`world_look.yaml`; every sprite is a generated placeholder.

**Tech Stack:** C++23, SDL_GPU behind `gfx`, HLSL through `project_n shaders`, yaml-cpp behind `data`, doctest, Python 3
(`art/placeholders.py`), XMake through `project_n`, all from Pixi.

**Status:** approved 2026-11-21 (the user); executed subagent-driven.

**Spec:** `docs/superpowers/specs/2026-11-21-phase6-chunk5b-look-terrain-design.md` (read it first: Goals, Drawing
order, Anti-goals, Calls). The look's starting values and the picked pictures come from the project memory's
`refs/5b-look-preview/` (`look.py`, variant `atmospheric2`; `atmospheric2_*.png`), at
`~/.claude/projects/-home-lotus-Development-project-n/memory/refs/5b-look-preview/`.

## Global Constraints

- Presentation only: no change to `world.yaml`, routes, costs, saves, hashes or determinism; `world_look.yaml` stays
  unhashed, unsaved and live-reloaded (a broken edit keeps the old look).
- No straight line along the tile grid shows in washes, water or stamps at any zoom; only the faint grid follows it.
- Every noise octave and the grid fade out before they get finer than about a pixel (from the pixel footprint,
  `fwidth` of the world position), so sub-pixel camera moves don't make the ground crawl.
- Stamps, puffs and paintings are fixed by a hash of their tile (never the view), drawn back to front by foot, and fade
  continuously with the zoom: nothing pops, nothing reshuffles.
- A place is still one tile for walking, picking and travel; its painting is drawing only.
- The fog's rules are unchanged. Ground, veil, rivers, stamps, puffs and paintings take the land treatment (drain, mist,
  shade). Markers take the shade only: hidden under cloud, never drained.
- Drawing order: ground → rivers → road overlays, bridges, fords → stamps, ranges and paintings (one sorted batch) →
  mist puffs → veil → markers → route → the snapshot's ground, decals and entities → clouds → target cursor → overlay.
- Every new sprite is a placeholder from `art/placeholders.py`, listed in `art/sprites.toml` and the world scene's list
  in `art/scenes.toml`; `pixi run project_n atlas` after each art change, its outputs committed with it.
- Sprites and their IDs may change or go until 1.0.0, and saves need no migration (the user, 2026-11-21). No save or
  replay holds a sprite ID today (checked while planning: saves hold world state, replays hold inputs), so removing
  sprites is no breaking change.
- Shimmer: three ground cases (zoom 1, 0.5, 0.25) each at 0.02 or under, bounds about 1.25 times their first
  measurement and never above 0.02; the world guard tightened to its new measurement × 1.25; the cloud guard re-set the
  same way but never above 0.0256.
- src/AGENTS.md rules (sized types, `pch.hpp`, Doxygen on every public name, `PN_PROFILE_ZONE` on per-frame functions,
  logs say outcomes only); shaders/AGENTS.md (one file per stage, shared code in `*.hlsli`, `project_n shaders` after a
  change with `shaders/bin/` and `src/gfx/generated/` committed); tests/AGENTS.md (about 20 named samples, never sweeps,
  a worst value asserted once).
- Each task updates the AGENTS.md lines its change makes untrue (assets/AGENTS.md's `world_look.yaml` line, art/AGENTS.md,
  src/AGENTS.md's world-map rendering lines, tests/AGENTS.md's reference lines) in the same commit.
- Gates: an edit round runs `pixi run project_n test --suite <s> [--test <t>]`; a task ends with
  `pixi run project_n test` and `pixi run project_n lint --range <task base>..HEAD`, plus `pixi run project_n reference`
  when it touches the atlas, shaders or drawing.

## Review Focus

1. The widest view of a 32:9 window at zoom 0.25 over the densest part of the shipped map: the terrain buffer (roads,
   bridges, fords, stamps, paintings, puffs, markers) stays within `MAX_TERRAIN_INSTANCES`. Task 5 adds the test.
2. A view partly or wholly off the map: the ground pass covers only the map's part of the view (the backdrop shows past
   the map as today), and nothing is drawn when the view misses the map. Task 6 adds the test.
3. A place on the map's corner tile, its painting reaching off the map: it is laid out whole, and the stamps under its
   on-map part are skipped without reading past the map. Task 5 adds the test.
4. A look naming more terrain kinds than the ground's colour table holds (`MAX_GROUND_KINDS`, 16), or a map kind
   missing from the look: refused, naming the kind. Task 4 adds the test.
5. Zooming slowly across a kind's `thin_below`: a stamp's opacity changes by at most a small step between two zooms
   1% apart, and the same stamps go first every time. Task 5 adds the test.

## Rulings made while planning

- **The look grows in place, then sheds the old parts:** Task 4 turns each `kinds:` entry into a map (wash, stamps)
  that still carries today's tile sprites as `variants:` and keeps `rivers:`' 16 overlay sprites, so the map draws as
  today until Task 7 switches over; Task 7 removes `variants:`, `rivers:` and their sprites.
- **Water depth comes from two named kinds:** `ground.water` names the shallow and the deep kind
  (`shallows`, `deep_water`); tiles of either are water for the shore distance, and water's colour blends from the
  shallow kind's wash to the deep kind's with the distance from the shore, over `depth_tiles`. Other kinds have no water
  flag.
- **The ground texture is two bytes per tile:** the kind index, then the shore distance as
  `clamp(round(128 + 32 × d), 0, 255)` with `d` in tiles, negative in water, measured between tile centres to the nearest
  tile of the other wetness minus half a tile (so a shore's two sides read −0.5 and +0.5), clamped to ±`MAX_SHORE_TILES`
  (4). The kind is point-sampled (indices can't be filtered), the shore linearly (the same texture under two samplers).
- **Bridges and the ford are redrawn under their names** (`bridge_h`, `bridge_v`, `ford`) to span the wider band,
  rather than removed and added: one change, the same look keys.
- **Rivers are ribbons in `gfx::RibbonVertex`'s layout** (16 bytes: world x, y; uv as two unorm16, v across 0 to 1;
  premultiplied colour), all rivers' edge bands first and then all cores, so a tributary's join shows no seam; built once
  per map and look, not per frame.
- **The terrain buffer says where its parts start:** `TerrainDrawing::roadBatches` (the leading batches that draw road
  overlays) and `TerrainDrawing::markersFrom` (the first marker batch), so the renderer draws the veil between the puffs
  and the markers, and the shimmer harness can leave the roads out.
- **Stamp hashes reuse the terrain's seed and purpose** (`TERRAIN_SEED`, `TERRAIN_PURPOSE`, moved to
  `gfx/tile_hash.hpp`) with draw indices of their own after `VARIANT_DRAW`.
- **The harness hides parts through `gfx::View::terrainParts`** (`TerrainParts::All` by default, `AllButRoads`,
  `PaperOnly`): only the reference cases set it; the game never does.
- **`world_look.yaml`'s new keys** (Task 4) carry the preview's `atmospheric2` values; the user tunes them live later.

## Waves

| Wave | Tasks | What each needs |
|---|---|---|
| A | 1 (placeholder art, light), 2 (ground field, full), 3 (river ribbons, full) | nothing; no shared file (1: `art/`, atlas outputs; 2: `gfx/ground_field.*`; 3: `gfx/river_draw.*`) |
| B | 4 (the look's new sections, full) | Task 1's sprite names (`core/world_look.hpp`, `data/world_look.cpp`, `world_look.yaml`, `gfx/terrain_draw.cpp`, fuzz seed) |
| C | 5 (stamps, puffs and paintings, full), 6 (ground and river pipelines, full) | 5: Task 4 (`gfx/stamp_draw.*`, `gfx/tile_hash.hpp`, `gfx/terrain_draw.cpp`'s constants only); 6: Tasks 2, 3, 4 (`shaders/`, `gfx/ground.*`, `gfx/internal/features/ground.*`, `gfx/internal/features/rivers.*`, `gfx/renderer.*`) |
| D | 7 (the switch-over, full) | Tasks 5, 6 (`gfx/terrain_draw.*`, `gfx/renderer.cpp`, `shaders/`, `core/frame.hpp`, the look, `art/`, pictures) |
| E | 8 (shimmer guards, full) | Task 7 (`tests/reference/`, `gfx/camera.hpp`'s `View`, `tools/project_n.py`) |
| F | 9 (measurements and Outcome, light) | everything; runs alone, when the user says the display is free |

---

### Task 1: placeholder stamps, mist, paintings, crossings and clouds

**review: light** (generated outputs) · **implementer: sonnet**

**Files:**
- Modify: `art/placeholders.py`, `art/sprites.toml`, `art/scenes.toml` (`world`), atlas outputs (`assets/sprites/`,
  `assets/manifest.yaml`, `src/core/generated/atlas.hpp`, `art/atlas.sha256`), `tests/reference/*.png` (fog and
  crossing pictures), `art/AGENTS.md`

**Interfaces:**
- Produces (appended to `sprites.toml` and the world scene's list; foot at the bottom centre unless said):
  - `stamp_<kind>_0` … `_3` for `forest`, `hills`, `peaks`, `snow`, `rock`, `marsh`, `sand`, `grass`, `meadow`,
    `water` (40 sprites), each as wide in pixels as 256 × its kind's largest size in Task 4's table (forest 333,
    peaks and snow 435, rock 282, hills 307, marsh, sand, grass and meadow 230, water 230), drawn as `look.py`'s
    `stamps()` draws that kind (forest: 3 to 5 trees and pines; peaks: two overlapping mountains, a third snow-capped;
    snow: a snow mountain; rock: rocks; hills: two hills; marsh: reeds; sand: dunes; grass: tufts; meadow: flowers;
    water: ripples), transparent round the mark.
  - `mist_0` … `mist_3`: soft white ellipses 384 × 128 with a blurred edge (alpha from 1 at the centre to 0), white so
    the look tints them.
  - `painting_city` 512 × 512 and `painting_capital` 768 × 768 (walls, roofs and a gate in ink and wash, foot at the
    bottom centre); `painting_<sort>` 256 × 256 for `haunted_forest`, `mountain_pass`, `shrine`, `cave`, `cliff`,
    `corrupt_village`.
  - `bridge_h`, `bridge_v`, `ford` redrawn in place (same names and list positions) so the deck and stones span a band
    0.5 tile (128 px) wide.
  - `cloud_0` … `cloud_15` repainted in place: paper-toned fill `(240, 235, 222)`, softer ink `(120, 104, 90)` at the
    same outline width; same sizes, foot and silhouettes.

- [ ] **Step 1:** Add the generators to `placeholders.py` (one function per family, `look.py`'s shapes as the start)
  and the sprite entries; redraw the crossings and clouds.
- [ ] **Step 2:** Run `pixi run project_n atlas`: it writes the outputs; `pixi run project_n lint` reports no stale atlas.
- [ ] **Step 3:** Run `pixi run project_n reference`: only the pictures showing a bridge, a ford or clouds differ. Look
  at each (`build/reference/`), then `pixi run project_n reference --update`.
- [ ] **Step 4:** Commit `build(tools): paint the terrain's stamps, mist and places as placeholders` with the atlas
  outputs and pictures.

### Task 2: the ground field

**review: full** (what the shader reads) · **implementer: sonnet**

**Files:**
- Create: `src/gfx/ground_field.hpp`, `src/gfx/ground_field.cpp`, `tests/unit/gfx/ground_field_tests.cpp`

**Interfaces:**
- Produces (`namespace gfx`, `ground_field.hpp`):
  - `inline constexpr f32 MAX_SHORE_TILES = 4.0F;`
  - `[[nodiscard]] u8 ShoreByte(f32 tiles);` (`clamp(round(128 + 32 × tiles), 0, 255)`)
  - `void GroundTexels(const core::WorldMap& map, std::span<const u8> waterKinds, std::vector<u8>& into);` two bytes
    per tile, row by row: the kind index, then `ShoreByte` of the signed shore distance (the ruling's definition); with
    no water kind, or no tile of either wetness, every distance is the clamp (`+4` on land, `−4` in water).

- [ ] **Step 1: Write the failing tests** in `ground_field_tests.cpp` on a 6 × 4 map built in the test (a 2 × 2 lake
  inside meadow): `TEST_CASE("a shore's two sides read half a tile either way")` (land next to the lake → 144, lake
  next to land → 112); `TEST_CASE("a land tile three centres from the water reads 2.5")` (→ 208);
  `TEST_CASE("a diagonal neighbour of the lake is sqrt 2 minus a half away")` (→ 157);
  `TEST_CASE("distances clamp at four tiles")` (a 12 × 1 map of land with water at one end → the far tile reads 255);
  `TEST_CASE("the kind byte is the map's kind index")`; `TEST_CASE("a map without water reads the clamp everywhere")`.
- [ ] **Step 2: Run** `pixi run project_n test --suite gfx_tests --test ground_field`: FAIL (no header).
- [ ] **Step 3: Implement.** For each tile, search the window of ±5 tiles for the nearest tile of the other wetness
  (Euclidean between centres); 12,750 tiles × 121 is cheap and runs once per map and look.
- [ ] **Step 4: Run** `pixi run project_n test --suite gfx_tests`: PASS.
- [ ] **Step 5: Commit** `feat(gfx): build the ground's kind and shore field`.

### Task 3: river ribbons

**review: full** (geometry) · **implementer: sonnet**

**Files:**
- Create: `src/gfx/river_draw.hpp`, `src/gfx/river_draw.cpp`, `tests/unit/gfx/river_draw_tests.cpp`

**Interfaces:**
- Consumes: `gfx::RibbonVertex` (`gfx/volley_draw.hpp`), `QUAD_VERTICES`, `core::WorldMap::Rivers()`.
- Produces (`namespace gfx`, `river_draw.hpp`):
  - `inline constexpr u32 MAX_RIVER_QUADS = 4'096;` `inline constexpr u32 RIVER_STEPS = 8;` (spline steps per tile step)
  - `struct RiverShape { f32 coreWidth = 0.0F; f32 edgeWidth = 0.0F; u32 coreColour = 0; u32 edgeColour = 0; };`
    (widths in world units, colours premultiplied, red in the low byte, as `RibbonVertex::colour`)
  - `void DrawRivers(const core::WorldMap& map, const RiverShape& shape, std::vector<RibbonVertex>& into);` replaces
    `into` with every river's edge band, then every river's core, as whole quads in `MeshVertex` corner order; each
    river a centripetal Catmull-Rom spline through its tile centres (ends included, arriving along the end segments),
    `RIVER_STEPS` quads per tile step, `v` 0 on one side and 1 on the other; at most `MAX_RIVER_QUADS` (a test holds the
    shipped map to it).

- [ ] **Step 1: Write the failing tests** in `river_draw_tests.cpp` on a small map built in the test:
  `TEST_CASE("a straight river's ribbon has the core's width")` (a river along one row: every core quad's two sides
  `coreWidth` apart); `TEST_CASE("a ribbon passes through each tile centre")` (an L-shaped river: the midline at each
  point's step is within 1 unit of its tile centre); `TEST_CASE("edges come before cores")`;
  `TEST_CASE("a tributary's end lies inside the main river's core")` (its last midline point within `coreWidth / 2` of
  the main river's midline); `TEST_CASE("the shipped rivers fit the river buffer")` (the shipped map, read as `terrain_draw_tests.cpp`'s
  `ShippedMap()` reads it; move that helper to a shared test header rather than copy it);
  `TEST_CASE("a one-point river draws nothing")`.
- [ ] **Step 2: Run** `pixi run project_n test --suite gfx_tests --test river_draw`: FAIL.
- [ ] **Step 3: Implement** (`curve()` in `art/placeholders.py` is the same spline in Python).
- [ ] **Step 4: Run** `pixi run project_n test --suite gfx_tests`: PASS.
- [ ] **Step 5: Commit** `feat(gfx): lay rivers out as smooth ribbons`.

### Task 4: the look's ground, stamps, mist, veil, river and places

**review: full** (a reader that refuses data) · **implementer: sonnet**

**Files:**
- Modify: `src/core/world_look.hpp`, `src/data/world_look.cpp`, `assets/data/world_look.yaml`,
  `src/gfx/terrain_draw.cpp` (reads `kinds[k].variants`), `tests/fuzz/corpus/yaml/world_look*`, `assets/AGENTS.md`
- Test: `tests/unit/data/world_look_tests.cpp`

**Interfaces:**
- Consumes: Task 1's sprite names.
- Produces (`core/world_look.hpp`; colours sRGB `std::array<u8, 3>`, lengths in tiles unless said; all in `Hash()`):
  - `inline constexpr u32 MAX_GROUND_KINDS = 16;`
  - `struct StampLook { std::vector<atlas::SpriteId> sprites; f32 density = 0.0F; std::array<f32, 2> size{ 1.0F, 1.0F };
    f32 jitter = 0.0F; f32 thinBelow = 0.0F; bool mist = false; };` (size: the stamp's width range in tiles;
    `thinBelow` 0 = never thins)
  - `struct KindLook { std::array<u8, 3> wash{}; f32 strength = 0.0F; StampLook stamps{}; std::vector<atlas::SpriteId>
    variants; };` (`variants` goes in Task 7)
  - `struct GroundLook { std::array<u8, 3> paper{}; f32 grain = 0.0F; f32 edgeWarp = 0.0F; f32 edgeSoft = 0.0F;
    f32 blotch = 0.0F; u8 shallowKind = 0; u8 deepKind = 0; f32 depthTiles = 0.0F; std::array<u8, 3> shore{};
    f32 shoreStrength = 0.0F; f32 shoreWidth = 0.0F; std::array<u8, 3> grid{}; f32 gridStrength = 0.0F;
    std::array<f32, 2> gridFade{}; };` (`gridFade`: the zoom where the grid is whole, then where it is gone)
  - `struct VeilLook { std::array<u8, 3> colour{}; f32 scale = 0.0F; f32 strength = 0.0F; };`
  - `struct MistLook { std::vector<atlas::SpriteId> sprites; std::array<u8, 3> colour{}; f32 strength = 0.0F;
    f32 chance = 0.0F; std::array<f32, 2> width{}; };` (width: a puff's range as a share of its stamp's width)
  - `struct RiverLook { f32 width = 0.0F; f32 edge = 0.0F; std::array<u8, 3> colour{}; std::array<u8, 3> edgeColour{};
    f32 edgeStrength = 0.0F; };`
  - `struct PlaceLook { atlas::SpriteId city{}; std::vector<std::pair<std::string, atlas::SpriteId>> dangers;
    std::vector<std::pair<std::string, atlas::SpriteId>> byKey; };`
  - `WorldLook::kinds` becomes `std::vector<KindLook>`; `WorldLook` gains `ground`, `veil`, `mist`, `river`, `places`.
- `world_look.yaml` (the comment block gains a line per section; every key required, a missing or broken one refused
  naming it and its line):
  ```yaml
  ground:
    paper: [240, 235, 222]
    grain: 0.04              # the paper fibre's strength
    edge_warp: 0.45          # how far noise moves the edge between two kinds, in tiles
    edge_soft: 0.5           # how wide that edge blends, in tiles
    blotch: 0.5              # how unevenly a wash pools, 0 to 1
    water: { shallow: shallows, deep: deep_water, depth_tiles: 3.0 }
    shore: { colour: [70, 104, 112], strength: 0.55, width: 0.12 }
    grid: { colour: [90, 80, 70], strength: 0.09, fade: [0.6, 0.35] }
  veil: { colour: [246, 244, 238], scale: 3.0, strength: 0.55 }
  mist: { sprites: [mist_0, mist_1, mist_2, mist_3], colour: [246, 244, 238], strength: 0.9, chance: 1.0, width: [1.0, 1.4] }
  river: { width: 0.38, edge: 0.5, colour: [128, 172, 176], edge_colour: [70, 104, 112], edge_strength: 0.67 }
  places:
    city: painting_city
    dangers: { haunted_forest: painting_haunted_forest, mountain_pass: painting_mountain_pass, shrine: painting_shrine,
               cave: painting_cave, cliff: painting_cliff, corrupt_village: painting_corrupt_village }
    by_key: { capital: painting_capital }
  kinds:
    forest: { wash: [66, 102, 74], strength: 0.12, variants: [terrain_forest_0, terrain_forest_1],
              stamps: { sprites: [stamp_forest_0, stamp_forest_1, stamp_forest_2, stamp_forest_3], density: 0.45,
                        size: [1.0, 1.3], jitter: 0.15, thin_below: 0, mist: true } }
    # … each kind likewise
  ```
  Per kind (wash; strength; stamp family; density; size; jitter; thin_below; mist): deep_water (88,124,148; 0.68;
  water; 0.08; 0.7–0.9; 0.15; 0.5; no) · shallows (138,178,178; 0.48; water; 0.08; 0.7–0.9; 0.15; 0.5; no) · sand
  (216,192,142; 0.25; sand; 0.15; 0.6–0.9; 0.15; 0.5; yes) · marsh (124,138,98; 0.16; marsh; 0.35; 0.6–0.9; 0.15; 0.5;
  yes) · meadow (196,204,146; 0.0; meadow; 0.03; 0.6–0.9; 0.15; 0.5; yes) · grassland (152,178,112; 0.03; grass; 0.08;
  0.6–0.9; 0.15; 0.5; yes) · forest (66,102,74; 0.12; forest; 0.45; 1.0–1.3; 0.15; 0; yes) · hills (172,146,110; 0.12;
  hills; 0.4; 0.9–1.2; 0.15; 0; yes) · rock (128,124,120; 0.2; rock; 0.6; 0.8–1.1; 0.2; 0; yes) · snow (236,239,242;
  0.2; snow; 0.35; 1.2–1.7; 0.3; 0; yes) · peaks (88,132,126; 0.28; peaks; 0.6; 1.2–1.7; 0.3; 0; yes).
  Limits: strengths, densities, chance, blotch in [0, 1]; sizes and widths > 0 with min ≤ max; jitter in [0, 1];
  `thin_below`, `gridFade` in [0, 2] with fade's first ≥ second; `depth_tiles` in (0, 4]; `scale` > 0; `water` names two
  kinds of the map; sprites in the manifest and the world scene's list; every map kind has an entry; at most
  `MAX_GROUND_KINDS` kinds; a `dangers` sort the map lacks is ignored (as markers are); a `by_key` key no place has is
  refused.

- [ ] **Step 1: Write the failing tests** in `world_look_tests.cpp`: `TEST_CASE("the shipped look's ground, veil, mist,
  river and places read as written")` (a sample of each section: paper, `deepKind`, grid fade, veil scale, mist
  width, river edge, the capital's painting); `TEST_CASE("the shipped forest's stamps read as written")`;
  `TEST_CASE("a look with more kinds than the ground's table is refused")`; `TEST_CASE("a map kind missing from the look
  is refused, naming it")`; `TEST_CASE("ground values out of range are refused, each naming its key")` (strength 1.5,
  size [0.9, 0.6], depth_tiles 0, a water kind the map lacks, a colour of 256); `TEST_CASE("a painting for a place key
  no place has is refused")`; `TEST_CASE("the look's hash changes with a stamp density")`.
- [ ] **Step 2: Run** `pixi run project_n test --suite data_tests --test world_look`: FAIL.
- [ ] **Step 3: Implement** the structs, the reader and the YAML; `terrain_draw.cpp` reads `kinds[k].variants`; the
  fuzz seed gains every new section.
- [ ] **Step 4: Run** `pixi run project_n test --suite data_tests`, `--suite gfx_tests`, and
  `pixi run project_n reference`: PASS, no picture moves.
- [ ] **Step 5: Commit** `feat(data): give the world look washes, stamps, mist, rivers and places`.

### Task 5: stamps, mist puffs and place paintings

**review: full** (layout stability, no pops) · **implementer: sonnet**

**Files:**
- Create: `src/gfx/stamp_draw.hpp`, `src/gfx/stamp_draw.cpp`, `src/gfx/tile_hash.hpp`,
  `tests/unit/gfx/stamp_draw_tests.cpp`
- Modify: `src/gfx/terrain_draw.cpp` (its seed and purpose constants move to `tile_hash.hpp`)

**Interfaces:**
- Consumes: Task 4's `StampLook`, `MistLook`, `PlaceLook`, `KindLook`.
- Produces (`gfx/tile_hash.hpp`): `TERRAIN_SEED`, `TERRAIN_PURPOSE`, `VARIANT_DRAW` and
  `[[nodiscard]] f32 TileUnit(u32 column, u32 row, u32 draw);` (0 to 1 from `core::Random` with that seed and purpose,
  the same for a tile whichever view finds it)
- Produces (`gfx/stamp_draw.hpp`):
  - `void DrawStamps(const core::WorldMap& map, const core::WorldLook& look, const core::Manifest& manifest,
    core::TileSpan span, f32 zoom, TerrainDrawing& into);` appends to `into` one batch of stamps and paintings sorted by
    foot (y, then x, then tile index), then one batch of mist puffs; each instance flagged `core::INSTANCE_FOGGED_LAND`,
    standing still.
  - Rules: a tile of `span` has a stamp when `TileUnit(c, r, 3) < density`; sprite from draw 4, mirror (as
    `cloud_draw.cpp` flips, `widthScale = -1`) from draw 5, width `lerp(size, TileUnit(c, r, 6)) × tileSize`, foot at
    `((c + 0.5 + jitter × (2u₇ − 1)) × tile, (r + 0.75 + 0.5 × jitter × (2u₈ − 1)) × tile)`; thinning when
    `thinBelow > 0`: `shown = clamp((zoom − 0.5 × thinBelow) / (0.5 × thinBelow), 0, 1)`, opacity
    `smoothstep(rank, rank + 0.125, shown)` with `rank = TileUnit(c, r, 9) × 0.875`, none drawn at 0; a mist puff for a
    drawn stamp of a `mist` kind when `TileUnit(c, r, 10) < mist.chance`, centred on the stamp's foot, width
    `lerp(mist.width, u₁₁) × stamp width`, the look's colour at `strength × the stamp's opacity`.
  - Paintings: each place whose tile `span` reaches (with a margin of the largest painting's half-size, so a painting
    spilling into view is drawn): its `byKey` sprite, else its sort's (danger) or `city`; centred on the tile's centre;
    footprint its drawn rect. Stamps whose foot lies in any footprint are skipped.

- [ ] **Step 1: Write the failing tests** in `stamp_draw_tests.cpp` on a small map and look built in the test:
  `TEST_CASE("the same span gives the same stamps")`; `TEST_CASE("a stamp is the same seen from two overlapping
  spans")`; `TEST_CASE("density 0 draws no stamp and density 1 one per tile")`; `TEST_CASE("stamps are drawn back to
  front by foot")`; `TEST_CASE("stamps under a city's painting make way")`; `TEST_CASE("a place's own painting wins over
  its kind's")`; `TEST_CASE("a painting on the corner tile is laid out whole and its stamps skipped")`;
  `TEST_CASE("thinning drops the lowest-ranked stamps first, the same ones each time")`; `TEST_CASE("a stamp's opacity
  moves by at most 0.1 between zooms 1% apart")` (a few zooms round `thinBelow`); `TEST_CASE("a kind without mist gets
  no puffs")`; `TEST_CASE("the widest 32:9 view of the shipped map fits the terrain buffer")` (stamps, paintings and
  puffs plus `DrawTerrain`'s own count for that span under `MAX_TERRAIN_INSTANCES`).
- [ ] **Step 2: Run** `pixi run project_n test --suite gfx_tests --test stamp_draw`: FAIL.
- [ ] **Step 3: Implement**; `DrawStamps` carries `PN_PROFILE_ZONE`.
- [ ] **Step 4: Run** `pixi run project_n test --suite gfx_tests`: PASS.
- [ ] **Step 5: Commit** `feat(gfx): lay out the terrain's stamps, mist and place paintings`.

### Task 6: the ground and river pipelines

**review: full** (rendering, fog, shimmer) · **implementer: opus**

**Files:**
- Create: `shaders/fog.hlsli`, `shaders/ground.hlsli`, `shaders/ground.vert.hlsl`, `shaders/ground.frag.hlsl`,
  `shaders/river.vert.hlsl`, `shaders/river.frag.hlsl`, `src/gfx/ground.hpp`, `src/gfx/ground.cpp`,
  `src/gfx/internal/features/ground.hpp`, `src/gfx/internal/features/ground.cpp`,
  `src/gfx/internal/features/rivers.hpp`, `src/gfx/internal/features/rivers.cpp`, `tests/unit/gfx/ground_tests.cpp`
- Modify: `shaders/sprite.frag.hlsl` (fog code moves to `fog.hlsli`), `src/gfx/renderer.cpp`, `shaders/bin/` and
  `src/gfx/generated/shaders.hpp` (`project_n shaders` finds every `shaders/*.hlsl` itself), `src/AGENTS.md`

**Interfaces:**
- Consumes: Task 2's `GroundTexels`, Task 3's `DrawRivers`, `RiverShape`, Task 4's `GroundLook`, `VeilLook`,
  `RiverLook`, `KindLook`; `FogTexture::Binding()` and `FogUniforms`.
- Produces (`gfx/ground.hpp`, pure):
  - `struct GroundUniforms` (the ground shader's constant buffer, rows of 16 bytes, size asserted like `FogUniforms`):
    the map's size and tile size; paper and grain; edge warp, softness and blotch; the shallow and deep kind indices
    and depth; shore colour, strength and width; grid colour, strength and fade zooms; veil colour, scale and strength;
    a `mode` (0 ground, 1 veil, 2 paper only); and `std::array<std::array<f32, 4>, MAX_GROUND_KINDS> washes`
    (linear wash rgb, strength).
  - `[[nodiscard]] GroundUniforms GroundUniformsOf(const core::WorldMap& map, const core::WorldLook& look, u32 mode);`
  - `[[nodiscard]] std::optional<core::Rect> GroundQuad(core::Rect view, const core::WorldMap& map);` (the view's
    overlap with the map's rect; none when they miss)
  - `[[nodiscard]] RiverShape RiverShapeOf(const core::WorldMap& map, const core::RiverLook& look);`
- Produces (`internal/features`): `GroundRenderer` (pipeline, the field texture rebuilt when the map version or the
  look's hash changes, its point and linear samplers, `Draw(commands, pass, worldToClip, quad, uniforms, fog)`) and
  `RiverRenderer` (premultiplied-over pipeline in world units, the vertex buffer rebuilt with the field,
  `Draw(commands, pass, worldToClip, fog)`), both binding the fog texture and uniforms as the sprites do.
- Shaders: `fog.hlsli` holds `FogAt`, the land treatment (`FoggedLand(rgb, alpha, world)`) and the shade-only one
  (`FogShaded`), used by `sprite.frag.hlsl` unchanged in effect. `ground.frag.hlsl`: paper with fibre grain; the wash by
  kind with the edge warped by value-noise fBm (about a third of a tile) and blended over `edgeSoft` (a few warped point
  taps of the kind byte, weighed, looked up in `washes`), blotch on the strength; water's colour from shallow to deep by
  the linear shore sample over `depthTiles`, the shore line where the shore distance crosses 0, softer washes over water;
  the anti-aliased grid faded by zoom; every octave and line faded by its size against `fwidth(world)`; mode 1 draws
  only the veil (fBm at `scale` tiles, `clip((n − 0.45) × 3, 0, 1) × strength` of its colour, premultiplied over);
  mode 2 draws only the paper colour; then `FoggedLand`. `river.frag.hlsl`: the band's colour with an anti-aliased edge
  from `fwidth(v)`, then `FoggedLand`.
- Renderer: the ground pass draws first on the world map (mode 0), then the rivers, then today's terrain buffer, which
  still covers both (its tiles are opaque), so no picture moves in this task.

- [ ] **Step 1: Write the failing tests** in `ground_tests.cpp`: `TEST_CASE("the ground's uniforms carry each kind's
  linear wash and strength")` (meadow strength 0, peaks 0.28, the deep kind index); `TEST_CASE("the ground's uniforms
  are 16-byte rows the size the shader reads")`; `TEST_CASE("the ground quad is the view's overlap with the map")`
  (inside, straddling the left edge, wholly off the map → none); `TEST_CASE("a river's shape is its look in world
  units")` (0.38 × 256, 0.5 × 256, edge colour at alpha 0.67 premultiplied).
- [ ] **Step 2: Run** `pixi run project_n test --suite gfx_tests --test ground`: FAIL.
- [ ] **Step 3: Implement** the pure parts, the shaders (`pixi run project_n shaders`), the two renderers and the
  renderer's calls; `PN_PROFILE_ZONE` on the field rebuild.
- [ ] **Step 4: Run** `pixi run project_n test` and `pixi run project_n reference`: PASS with no picture moved (the
  tiles still cover the ground). Then check the ground by eye with the look-at-the-game skill, drawing the world with
  the terrain buffer turned off locally (not committed): washes, shore, rivers, grid, veil mode.
- [ ] **Step 5: Commit** `feat(gfx): draw the world map's painted ground and rivers` with `shaders/bin/` and
  `src/gfx/generated/`.

### Task 7: the switch-over

**review: full** (rendering, fog, drawing order) · **implementer: opus**

**Files:**
- Modify: `src/gfx/terrain_draw.hpp`, `src/gfx/terrain_draw.cpp`, `src/gfx/renderer.cpp`, `src/core/frame.hpp`,
  `shaders/fog.hlsli`, `shaders/sprite.frag.hlsl`, `src/core/world_look.hpp`, `src/data/world_look.cpp`,
  `assets/data/world_look.yaml`, `tests/fuzz/corpus/yaml/world_look*`, `art/placeholders.py`, `art/sprites.toml`
  (header comment and removals), `art/scenes.toml`, atlas outputs, `shaders/bin/`, `src/gfx/generated/`,
  `tests/reference/*.png`, `src/AGENTS.md`, `assets/AGENTS.md`, `art/AGENTS.md`, `tests/AGENTS.md`
- Test: `tests/unit/gfx/terrain_draw_tests.cpp`, `tests/unit/data/world_look_tests.cpp`,
  `tests/reference/reference_tests.cpp` (two new pictures)

**Interfaces:**
- Consumes: Task 5's `DrawStamps`, Task 6's renderers and `GroundUniformsOf`.
- Produces: `inline constexpr u32 INSTANCE_FOG_SHADE = 4U;` in `core/frame.hpp` (hidden under cloud, not drained),
  mirrored in `fog.hlsli`; `DrawTerrain(map, look, manifest, view, zoom, into)` (a new `f32 zoom`) drawing, in order:
  road overlays (`TerrainDrawing::roadBatches` leading batches), bridges and fords, `DrawStamps`' two batches, then
  markers (`TerrainDrawing::markersFrom`, flagged `INSTANCE_FOG_SHADE`); no tiles, no river overlays. The renderer's
  terrain key gains the zoom; it draws ground → rivers → terrain batches before `markersFrom` → veil (mode 1) → the
  rest.
- Removed: `KindLook::variants`, `WorldLook::rivers` and their YAML keys; sprites `terrain_*` (22) and `river_0` …
  `river_15` from `placeholders.py`, `sprites.toml`, `assets/sprites/` and the world scene's list. `sprites.toml`'s
  header reads: "A sprite's place in this list is its ID. Until release 1.0.0 sprites may be changed, reordered or
  removed; from 1.0.0 on, add new ones at the end and never reorder or remove." (The glow sprites "kept for their
  stable ID" stay: not this branch's.)

- [ ] **Step 1: Write the failing tests** in `terrain_draw_tests.cpp`: `TEST_CASE("the terrain draws no tiles and no
  river overlays")`; `TEST_CASE("road overlays lead and markers close the terrain's batches")` (`roadBatches`,
  `markersFrom`); `TEST_CASE("markers are hidden under cloud but not drained")` (their flag is `INSTANCE_FOG_SHADE`);
  update the existing cases that counted tiles. In `world_look_tests.cpp`: `TEST_CASE("a kind with variants is
  refused")` (the key is gone, so it's an unknown key).
- [ ] **Step 2: Run** `pixi run project_n test --suite gfx_tests --test terrain_draw`: FAIL.
- [ ] **Step 3: Implement**; run `pixi run project_n shaders` and `pixi run project_n atlas`.
- [ ] **Step 4: Run** `pixi run project_n test`, then `pixi run project_n reference`: the world and fog pictures
  differ. Look at every one by eye (look-at-the-game skill) against the spec's anti-goals and the picked
  `atmospheric2_*.png` (no tile edge, washes faint, water deepening, rivers wide into the sea, ranges, city paintings,
  markers in colour where drained, nothing under cloud), then `pixi run project_n reference --update`. Two new world
  pictures join them, drawn like `world_close` (explored, debug sight, zoom 1): `world_coast` centred where a river
  meets the sea near the port (a river mouth, the shore, deep water, the port's painting) and `world_mountains` over
  the ranges near the sect (overlapping ranges, the sect's painting, mist and veil).
- [ ] **Step 5: Commit** `feat(gfx): draw the world map as painted paper` with every output and picture.

### Task 8: shimmer guards at three zooms

**review: full** (a verdict-giving test) · **implementer: opus**

**Files:**
- Modify: `src/gfx/camera.hpp` (`View`), `src/gfx/renderer.cpp`, `tests/reference/reference_tests.cpp`,
  `tools/project_n.py`, `tools/test_project_n.py`, `tools/AGENTS.md`, `tests/AGENTS.md`; the ground shader and
  `world_look.yaml` only if a case measures over 0.02
- Test: `tests/reference/reference_tests.cpp`, `tools/test_project_n.py`

**Interfaces:**
- Produces: `enum class TerrainParts : u8 { All, AllButRoads, PaperOnly };` and `gfx::View::terrainParts`
  (`TerrainParts::All`): `AllButRoads` skips the leading `roadBatches`; `PaperOnly` draws the ground in mode 2 and
  nothing else of the terrain (no rivers, terrain buffer or veil). Doxygen says only the shimmer measure sets it.
- Reference cases `"the ground at zoom 1: shimmer held to its bound"`, `"… at zoom 0.5 …"`, `"… at zoom 0.25 …"`:
  over `game::WORLD_BUSIEST`, explored, debug sight on, the snapshot's instances, batches and texts cleared (no
  travellers, cultivator or labels), `terrainParts = AllButRoads` against the same frame with `PaperOnly`; each prints
  `ground shimmer <zoom>: <value>`; bound about 1.25 × its first measurement, never above 0.02. The world case's
  `MOST_CHANGE` becomes about 1.25 × its new measurement and its comment loses the "tiles shimmer today" history; the
  cloud case's becomes about 1.25 × its new measurement, at most 0.0256.
- `project_n measure` reads the three `ground shimmer` lines beside `world shimmer` and `cloud shimmer` and prints
  them in its header line.

- [ ] **Step 1: Write the failing tests:** the three cases with a placeholder bound of 0.02;
  `tools/test_project_n.py`: the measure header carries the three ground values (a canned reference output).
- [ ] **Step 2: Run** `pixi run project_n reference` and `pixi run python -m pytest tools/test_project_n.py -k shimmer`:
  FAIL (no `terrainParts`, no parsing).
- [ ] **Step 3: Implement** `terrainParts` and the parsing. If a ground case measures over 0.02, find the crawling
  detail (the worst tile's position in the case's `INFO`) and fix it in the shader (an octave or line fading too late)
  or the look; never by loosening a bound.
- [ ] **Step 4: Set each bound** from its measurement (the comment records the measurement); run
  `pixi run project_n reference` and `pixi run project_n test`: PASS.
- [ ] **Step 5: Commit** `test(tests): hold the painted ground's shimmer at three zooms`.

### Task 9: measurements and the Outcome

**review: light** (docs) · **implementer: sonnet** · runs alone, when the user says the display is free

**Files:**
- Modify: `docs/superpowers/specs/2026-11-21-phase6-chunk5b-look-terrain-design.md` (Outcome, status)

- [ ] **Step 1:** `pixi run project_n measure --runs world` on this branch and on main (switching the main checkout as
  5b3's Outcome did), and a 30 s `pixi run project_n profile --seconds 30 -- --world --debug-sight --zoom 0.25` for
  `DrawTerrain`'s and `DrawStamps`' times and the ground field's rebuild.
- [ ] **Step 2:** Write the Outcome: the table branch / main, the layout times against the 0.2 ms median target, the
  shimmer values against their bounds, the ledger's rulings with their measurements; mark the spec implemented.
- [ ] **Step 3: Commit** `docs(design): record the painted terrain's outcome`.
