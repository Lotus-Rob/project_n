# Phase 6 chunk 5b3: fog and sight on the world map, implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The world map gets three fog states per tile (unexplored under curl clouds, explored out of sight drained to
sepia under a thin mist, in sight in full colour), a sight diamond round the cultivator that explores as it walks,
walkers seen only in sight, and a debug sight for the measurements, at next to no render cost.

**Architecture:** `core` gains the fog rules (`core/fog.*`: the explored bitset, Manhattan sight, exploring round a tile)
and the snapshot's `core::FogView`. `game` keeps the explored bits and the sight radius in `WorldState` (saved, hashed,
`WORLD_VERSION` 7), explores in the tick, and publishes the fog, culls walkers to sight and flags what fades with it.
`app::FogFade` eases each tile's two values (explored, in sight) on the main thread and hands them to the renderer as a
`gfx::FogFrame` in `gfx::View`. `gfx` uploads them as a two-channel texture the sprite shaders sample (terrain drain,
mist and shade; walker fades), lays the clouds out each frame (`gfx/cloud_draw.*`, pure) and draws them as one sprite
batch between the entities and the target cursor.

**Tech Stack:** C++23, SDL_GPU behind `gfx`, HLSL through `project_n shaders`, yaml-cpp behind `data`, doctest, Python 3
(`art/placeholders.py`), XMake through `project_n`, all from Pixi.

**Status:** approved 2026-11-20 (the user); executed subagent-driven.

**Spec:** `docs/superpowers/specs/2026-11-20-phase6-chunk5b3-fog-design.md` (read it first: Goals, Drawing order,
Anti-goals, Calls). The mockup layout the cloud numbers come from is
`.superpowers/brainstorm/95112-1795204427/mockup-scripts/lod.py` (gitignored, in the main checkout).

## Global Constraints

- Sight is a Manhattan diamond, radius 3 to start (`sight: 3` on `cultivator` in `creatures.yaml`); a tile at distance 3
  is in sight, at 4 isn't; the map's edges clip it. A new game's start diamond is 25 tiles.
- Exploring happens only in the tick (and at the world scene's build), never from the view, the frame rate or the worker
  count; the debug sight never explores, and is never recorded, hashed or saved.
- The eased values move over `fog.fade_seconds` (0.5) of `world_look.yaml`; scene entry, a load (a new epoch) and a debug
  sight switch snap them.
- Walkers fade over the last half tile before sight's edge: opacity = smoothstep(0.5, 1, the filtered in-sight value at
  the foot); one drawn at under half opacity can't be picked; sight never drops a selection.
- Clouds: level = max(0, log2(1 / zoom)); level-0 cells half a tile across; zoom-step cross-fade over each step's middle
  0.35 to 0.65 (zoom 1, 0.6, 0.4, 0.25 → levels 0, 1, 1, 2 alone; zoom 0.71 and 0.35 half and half); widths 0.85 to 1.45 of
  the base; bob amplitude 0.03 of a cell, period 4 s; never a draw call per cloud.
- Every fog look value (cloud sprites and sizes, bob, drift, fade, drain, sepia, mist, shade) lives in `world_look.yaml`
  (presentation, reloaded live), never in `core::UiColours` or code.
- The terrain buffer (tiles, overlays, crossings, markers) is the only thing the drain, mist and shade touch; the route
  stays undrained and draws under the clouds; the target cursor draws over them.
- Saves: the world section's `WORLD_VERSION` goes 6 → 7 once (Task 3); regenerate the four `save` fuzz seeds then
  (tests/AGENTS.md's command); a load refuses explored bits of the wrong size.
- src/AGENTS.md rules (sized types, `pch.hpp`, Doxygen on every public name, no `switch` over a whole enum,
  `PN_PROFILE_ZONE` on per-frame functions but not per-item ones, logs say outcomes only, `core/math.hpp` in simulation
  code); shaders/AGENTS.md; tests follow tests/AGENTS.md (about 20 named samples, never sweeps; a worst value asserted
  once).
- Each task updates the AGENTS.md lines its change makes untrue (src/AGENTS.md's world, rendering and command lines;
  tests/AGENTS.md's reference and fuzz lines; assets/AGENTS.md's `world_look.yaml` and `creatures.yaml` lines) in the
  same commit.
- Gates: an edit round runs `pixi run project_n test --suite <s> [--test <t>]`; a task ends with
  `pixi run project_n test` and `pixi run project_n lint --range <task base>..HEAD`, plus `pixi run project_n reference`
  when it touches the atlas, shaders, publishing or drawing.

## Review Focus

1. A save made while tiles are mid-uncover, loaded: the flags come back exactly, the fade snaps (no map-wide uncover),
   and the snapshot's fog version changes so the main thread re-reads them. Task 3 (flags round-trip) and Task 4 (snap
   on a new epoch) add the tests.
2. The cultivator at the map's corner and edges: the diamond clips without writing past the bitset, and a load refuses
   a bitset with bits set past the map's last tile. Task 1 (corner, edge) and Task 3 (stray trailing bits refused) add the
   tests.
3. Leaving the world map (F2, the portal) and coming back: the flags stay, the dev scene's snapshot shows no fog, the
   return explores round where the cultivator lands, and the fade snaps. Task 3 and Task 4 add the tests.
4. A view partly off the map, or the widest view of a 32:9 window at zoom 0.25 over an unexplored map: no cloud past the
   map's edge, the count within `MAX_CLOUD_INSTANCES`. Task 6 adds the tests.
5. Debug sight switched on and off mid-route with a walker selected: the state's hash after the same ticks is identical
   with it on and off, the selection kept. Task 5 adds the test.

## Rulings made while planning

- **Debug sight reaches the host by `core::Simulation::SetDebugSight(bool)`**, which `core::Logic` calls before each
  publish with the latest `ViewReport::debugSight` (false until a view arrives), rather than a new `Publish` parameter:
  `Publish` has 37 call sites, and the flag is news like the view, never tick input. Harnesses call it directly.
- **The explored bits are `std::vector<u64>` in `WorldState::explored`** (bit `t % 64` of word `t / 64` for tile `t`),
  sized to the map's tiles by `GenerateWorld`, empty in a scratch world; `WorldState::sightRadius` (u32) beside them.
  Both are written in `WriteClock`, so the hash, the game world's save and the scratch section all carry them.
- **Exploring runs every tick on the world map** round the foot's tile (25 bit tests at radius 3: cheaper than tracking
  tile changes, and a grown radius takes effect the next tick); a route also explores round every tile its leg reaches
  between the clock before and after (M's jump), read before `FollowRoute` can end the route.
- **The fog version** (`Host::m_fogVersion`, not saved or hashed) goes up whenever exploring sets a new bit and at every
  `EnterScene`, so the main thread re-reads the bits only then; the publisher copies the 1.6 KB of words every publish
  (snapshots are reused in turn, so each must hold them).
- **Fade flags travel in the data the main thread already draws:** `core::Instance::reserved` becomes `flags`
  (`INSTANCE_FOGGED_LAND` = 1 on every terrain instance, `INSTANCE_SIGHT_FADE` = 2 on walkers and their shadows), and
  `core::TextItem::fog` (`core::TextFog::None`, `Sight` for walker labels, `Explored` for place names). The sprite
  fragment shader reads the fog texture for both instance flags; the renderer's label pass scales a label's opacity on
  the CPU from the same values.
- **A walker's sight point is its foot:** for an instance, the bottom centre of its interpolated quad
  (`core::SightPoint(instance, alpha)`, mirrored in the shader); for a label, its anchor plus `(0, height)`. A place name
  fades by its own tile's eased explored value (nearest, not filtered), so a neighbour never dims it.
- **Clouds come and go without state:** each cloud's opacity comes from the eased explored values alone (the recursion in
  Task 6), so a merge, a split, an uncover and a zoom step all cross-fade and nothing is remembered between frames.
- **Off the map counts as explored for clouds** (no cloud reaches past the map's edge) and as unexplored for nothing:
  the backdrop shows there as today.
- **The arena doesn't show the fog:** it has no world map (the spec's Non-goals); the world scene is where map features
  show, as 5b1's and 5b2's did.

## Waves

| Wave | Tasks | What each needs |
|---|---|---|
| A | 1 (fog rules, snapshot and flags, full), 2 (cloud art and the look's fog section, full) | nothing; no shared file (1: `core/fog.*`, `core/frame.hpp`, `core/instances.*`, `core/logic.*`, `core/game_data.hpp`, `data/game_data.cpp`, `creatures.yaml`; 2: `art/`, atlas outputs, `core/world_look.hpp`, `data/world_look.*`, `world_look.yaml`) |
| B | 3 (exploring in the world, saved, full), 4 (fog field and fade, full) | 3: Task 1 (`game/world_state.*`, `game/host.*`, save seeds); 4: Task 1 (`gfx/fog.*`, `gfx/camera.hpp`, `app/fog_fade.*`) |
| C | 5 (publishing the fog, full), 6 (cloud layout, full), 7 (fog texture, terrain shading and fades, full), 8 (the main thread's fog and debug sight, full) | 5: Task 3 (`game/host.*`, `game/scene.hpp`, `game/world_state.hpp`, `game/scenes/world.cpp`); 6: Tasks 2, 4 (`gfx/cloud_draw.*`); 7: Tasks 1, 2, 4 (`gfx/renderer.cpp`, `gfx/internal/`, `shaders/`, `gfx/terrain_draw.cpp`, `gfx/labels.*`); 8: Tasks 1, 4 (`app/app.*`, `app/view.*`, `app/bindings.*`, `main.cpp`) |
| D | 9 (clouds drawn, the cursor over them, full) | Tasks 6, 7 (`gfx/renderer.*`, `gfx/route_draw.*`, `gfx/internal/`) |
| E | 10 (pictures, cloud shimmer and the measure run, full) | everything above (`tests/reference/`, `tools/project_n.py`) |
| F | 11 (measurements and Outcome, light) | everything; runs alone, when the user says the display is free |

---

### Task 1: the fog rules, the snapshot's fog and the fade flags

**review: full** (determinism, the snapshot) · **implementer: sonnet**

**Files:**
- Create: `src/core/fog.hpp`, `src/core/fog.cpp`, `tests/unit/core/fog_tests.cpp`
- Modify: `src/core/frame.hpp`, `src/core/instances.hpp`, `src/core/instances.cpp`, `src/core/logic.hpp`,
  `src/core/logic.cpp`, `src/core/game_data.hpp`, `src/data/game_data.cpp`, `src/game/host.cpp` (`BuiltInData` only),
  `assets/data/creatures.yaml`
- Test: `tests/unit/core/fog_tests.cpp`, `tests/unit/core/instances_tests.cpp`, `tests/unit/core/logic_tests.cpp`,
  `tests/unit/data/game_data_tests.cpp`

**Interfaces:**
- Produces (`namespace core`, `fog.hpp`):
  - `[[nodiscard]] constexpr std::size_t FogWords(std::size_t tiles)` (words a bitset of `tiles` needs)
  - `[[nodiscard]] bool IsExplored(std::span<const u64> bits, u32 tile)` (false past the bitset)
  - `[[nodiscard]] u32 TileDistance(TilePoint a, TilePoint b)` (Manhattan)
  - `[[nodiscard]] bool InSight(const WorldMap& map, u32 from, u32 tile, u32 radius)`
  - `u32 ExploreAround(std::span<u64> bits, const WorldMap& map, u32 centre, u32 radius)` (sets every tile within
    `radius` of `centre`, clipped to the map; returns how many were new; asserts the span holds the map's tiles)
  - `[[nodiscard]] bool FogBitsFit(std::span<const u64> bits, std::size_t tiles)` (right word count, no bit past `tiles`)
- Produces (`frame.hpp`):
  - `struct FogView { bool on = false; u64 version = 0; std::vector<u64> explored{}; std::optional<u32> playerTile =
    std::nullopt; u32 sightRadius = 0; bool debugSight = false; };` and `Snapshot::fog` (a `FogView`, refilled in place;
    `Logic` sets `on = false` before each publish and keeps the vector's capacity)
  - `ViewReport::debugSight` (`bool`, false)
  - `enum class TextFog : u8 { None, Sight, Explored };` and `TextItem::fog` (`TextFog::None`)
  - `Instance::reserved` renamed `flags`; `inline constexpr u32 INSTANCE_FOGGED_LAND = 1U;`
    `inline constexpr u32 INSTANCE_SIGHT_FADE = 2U;`
- Produces (`instances.hpp`): `SpriteDraw::flags` (u32, 0, copied into `Instance::flags` by `MakeInstance`);
  `[[nodiscard]] Vec2 SightPoint(const Instance& instance, f64 alpha)` (the bottom centre of its quad, unrotated, at
  `previous + (current - previous) × alpha`)
- Produces (`logic.hpp`): `virtual void Simulation::SetDebugSight(bool on)` with an empty default body; `Logic` calls it
  before each `Publish` with the latest view's `debugSight`, false while none has arrived (whatever the view's change)
- Produces: `CreatureData::sight` (u32 tiles, 0 default, in `Fields`); `creatures.yaml` `sight:` read for any creature
  (0 when absent, refused when negative or over 64); `cultivator: sight: 3`; `BuiltInData()`'s cultivator `sight = 3`

- [ ] **Step 1: Write the failing tests**
  - `fog_tests.cpp`: `TEST_CASE("exploring round a tile in the middle of the map marks the 25 tiles of the diamond")`
    (a 20 × 20 map built in the test; a second call returns 0); `TEST_CASE("exploring round the corner tile marks the
    10 tiles the map holds")`; `TEST_CASE("exploring round an edge tile marks the 16 tiles the map holds")`;
    `TEST_CASE("a radius of 0 explores the one tile")`; `TEST_CASE("a tile at distance 3 is in sight and one at 4
    isn't")` (orthogonal and diagonal samples); `TEST_CASE("fog bits fit only with the right word count and no stray bit")`
    (150 × 85 = 12,750 tiles → 200 words; a bit at 12,750 refused; 199 words refused).
  - `instances_tests.cpp`: `TEST_CASE("a sight point is the bottom centre of the quad between its ticks")` (a 64 × 128
    quad centred at (100, 100) → (100, 164) at alpha 1; halfway between two centres at 0.5).
  - `logic_tests.cpp`: `TEST_CASE("the publish hears the latest view's debug sight, and none before a view")` (a fake
    Simulation records `SetDebugSight`; before `SendView` false; after a view with `debugSight = true`, true, also when
    the view names another change); `TEST_CASE("a publish starts with the fog off and keeps its buffer")`.
  - `game_data_tests.cpp` (data): `TEST_CASE("a creature's sight is read, absent is 0, and a negative one refused")`.
- [ ] **Step 2: Run** `pixi run project_n test --suite core_tests --test fog`: FAIL (no `core/fog.hpp`).
- [ ] **Step 3: Implement** the interfaces above. `ExploreAround` walks rows `centre.row ± radius` and in each the
  columns within the remaining distance, clipped to the map.
- [ ] **Step 4: Run** `pixi run project_n test --suite core_tests`, `--suite data_tests`: PASS.
- [ ] **Step 5: Commit** `feat(core): add the fog rules, the snapshot's fog and the sight data`.

### Task 2: placeholder clouds and the look's fog section

**review: full** (a reader that refuses data) · **implementer: sonnet**

**Files:**
- Modify: `art/placeholders.py`, `art/sprites.toml`, `art/scenes.toml` (`world`), `src/core/world_look.hpp`,
  `src/data/world_look.cpp`, `assets/data/world_look.yaml`, `tests/fuzz/corpus/yaml/world_look*` (the seed), atlas
  outputs (`assets/sprites/`, `assets/manifest.yaml`, `src/core/generated/atlas.hpp`, `art/atlas.sha256`),
  `assets/AGENTS.md`
- Test: `tests/unit/data/world_look_tests.cpp`

**Interfaces:**
- Produces (`core/world_look.hpp`): `struct FogLook { std::vector<atlas::SpriteId> clouds; f32 cloudWidth = 1.15F; f32
  bobAmplitude = 0.03F; f32 bobPeriod = 4.0F; f32 drift = 0.3F; f32 fadeSeconds = 0.5F; f32 drain = 0.85F;
  std::array<u8, 3> sepia{}; std::array<u8, 3> mist{}; f32 mistStrength = 0.25F; std::array<u8, 3> shade{}; };`
  (`cloudWidth` and `bobAmplitude` in cell spacings, `drift` in tiles, colours sRGB) and `WorldLook::fog`, in `Hash()`.
- `world_look.yaml` gains (required; a missing or broken key refused naming it):
  ```yaml
  fog:
    clouds: [cloud_0, cloud_1, ..., cloud_15]
    cloud_width: 1.15        # a cloud's width in cell spacings, before its 0.85 to 1.45 draw
    bob: { amplitude: 0.03, period: 4.0 }
    drift: 0.3               # how far an uncovered cloud drifts, in tiles
    fade_seconds: 0.5
    drain: { strength: 0.85, sepia: [160, 138, 106] }
    mist: { colour: [239, 233, 220], strength: 0.25 }
    shade: [214, 206, 192]   # under the clouds: no land shows between them
  ```
  Limits: at least one cloud, each in the manifest and the world scene's list; `cloud_width` in (0, 4]; `amplitude` in
  [0, 0.5]; `period` > 0; `drift` in [0, 2]; `fade_seconds` in (0, 5]; strengths in [0, 1]; colours three integers 0–255.
- 16 cloud sprites `cloud_0` … `cloud_15`, 256 × 176, foot at the centre (`[128, 88]`), appended to `sprites.toml` and the
  world scene's list (appending moves no other sprite): each two base lobes and 2 to 4 bumps on top, a cream body
  `(242, 236, 222)` with an ink outline `(92, 78, 66)` about 2.2% of the width, and 1 or 2 ink spirals; the variety drawn
  from the sprite's index as `lod.py`'s `cloud_parts` and spirals do. The width the layout gives a cloud is the sprite's
  width; its height follows the sprite's aspect.

- [ ] **Step 1: Write the failing tests** in `world_look_tests.cpp`: `TEST_CASE("the shipped look's fog has 16 clouds and
  the written values")`; `TEST_CASE("a look without fog is refused")`; `TEST_CASE("a fog cloud outside the world scene's
  sprites is refused")`; `TEST_CASE("fog values out of range are refused, each naming its key")` (cloud width 0,
  period 0, strength 1.5, a colour of 256, a colour of two numbers); `TEST_CASE("the look's hash changes with a fog
  value")`.
- [ ] **Step 2: Run** `pixi run project_n test --suite data_tests --test world_look`: FAIL.
- [ ] **Step 3: Implement** the clouds in `placeholders.py` and the lists, run `pixi run project_n atlas`; the
  `FogLook` reader in `data/world_look.cpp`; the section in `world_look.yaml` with the comment block's line for it;
  update the fuzz seed so it carries a fog section.
- [ ] **Step 4: Run** `pixi run project_n test --suite data_tests` and `pixi run project_n reference`: PASS (no picture
  moves: the new sprites append).
- [ ] **Step 5: Commit** `feat(data): give the world look clouds and fog colours` (the atlas outputs with it).

### Task 3: exploring in the world, saved and hashed

**review: full** (saves, determinism) · **implementer: opus**

**Files:**
- Modify: `src/game/world_state.hpp`, `src/game/world_state.cpp`, `src/game/host.hpp`, `src/game/host.cpp`,
  `tests/fuzz/corpus/save/*` (regenerated), `src/AGENTS.md`
- Test: `tests/unit/game/host_tests.cpp`, `tests/unit/game/save_tests.cpp`, `tests/unit/game/replay_tests.cpp`

**Interfaces:**
- Consumes: Task 1's `core::ExploreAround`, `core::FogBitsFit`, `CreatureData::sight`.
- Produces: `WorldState::explored` (`std::vector<u64>`), `WorldState::sightRadius` (u32); `GenerateWorld` sizes
  `explored` to the map's tiles (none set) and sets `sightRadius = data.cultivator.sight`; `BuildScratch` leaves both
  empty and 0.
- Produces (`Host`): `[[nodiscard]] std::span<const u64> ExploredTiles() const;` `[[nodiscard]] u32 SightRadius() const;`
  `void SetSightRadius(u32 radius);` (for what grows sight later and tests: the next tick explores with it)
  `void Explore(const core::Rect& area);` (for harnesses: explores every tile `area` overlaps; the game never calls it)
  `[[nodiscard]] u64 FogVersion() const;` (Task 5 publishes it)
- `WriteClock` writes `sightRadius`, the explored word count and the words after `mapPlace`; `ReadClock` reads them;
  `Host::Load` refuses a game world whose bits don't fit its map (`FogBitsFit`) and a scratch world with any;
  `WORLD_VERSION` 7 (its comment: "7: the explored tiles and the sight radius").
- Exploring (only with the world map shown and a player): at the end of `Build` for a world-map scene, round the foot's
  tile; in `Tick` after `Move` (before the portal check), round the foot's tile, and round every tile the route's leg
  reaches in (clock before, clock after]; in `AdvanceCalendar`'s jump, the same for the route's tiles reached by the new
  clock. `m_fogVersion` goes up when any of these set a new bit and at each `EnterScene`.

- [ ] **Step 1: Write the failing tests** (small map `test::SmallData()` unless the case needs the shipped one):
  - `host_tests.cpp`: `TEST_CASE("a new game explores the start diamond and nothing else")` (25 tiles on the small
    map's start, assuming it lies 3 tiles from every edge; else the clipped count, computed in the test from the
    diamond); `TEST_CASE("walking a route explores within sight of every tile on it")`;
    `TEST_CASE("a month's jump along a route explores every tile it passes")` (WalkTo far, then `AdvanceCalendar`:
    every route tile up to the foot within sight of an explored one, none beyond the route's reach + 3);
    `TEST_CASE("a grown sight radius explores the new ring at the next tick")`; `TEST_CASE("a dev scene's world has no
    explored tiles and leaves the game's alone")`; `TEST_CASE("coming back to the world map explores round where the
    cultivator lands")`; `TEST_CASE("the world's hash changes with an explored tile and with the radius")`.
  - `save_tests.cpp`: `TEST_CASE("explored tiles and the sight radius survive a save and load")`;
    `TEST_CASE("a load refuses explored bits of the wrong size")` (one word short; a bit past the last tile).
  - `replay_tests.cpp`: `TEST_CASE("a replay recorded while walking plays back exploring the same tiles")` (the hashes
    match, and the explored bits after playback equal those after recording).
- [ ] **Step 2: Run** `pixi run project_n test --suite game_tests --test host`: FAIL.
- [ ] **Step 3: Implement** as the interfaces say; then regenerate the four `save` seeds with tests/AGENTS.md's command
  and run their check.
- [ ] **Step 4: Run** `pixi run project_n test --suite game_tests` and `pixi run project_n test --preset linux-clang
  --sanitizer address --suite fuzz_save`: PASS.
- [ ] **Step 5: Commit** `feat(game): explore the world map within the cultivator's sight` (the seeds with it).

### Task 4: the fog field and its fade

**review: full** (the look's timing, sampling both sides share) · **implementer: sonnet**

**Files:**
- Create: `src/gfx/fog.hpp`, `src/gfx/fog.cpp`, `src/app/fog_fade.hpp`, `src/app/fog_fade.cpp`,
  `tests/unit/gfx/fog_tests.cpp`, `tests/unit/app/fog_fade_tests.cpp`
- Modify: `src/gfx/camera.hpp` (`View::fog`)

**Interfaces:**
- Consumes: Task 1's `core::FogView`, `core::IsExplored`, `core::InSight`.
- Produces (`namespace gfx`, `fog.hpp`):
  - `struct FogFrame { u32 columns = 0; u32 rows = 0; f32 tileSize = 0.0F; std::span<const u8> values{}; u64 revision =
    0; };` (`values`: two per tile in tile order, explored then in sight, 0–255; `columns == 0` means no fog: nothing is
    faded or shaded; `revision` changes whenever any value does)
  - `[[nodiscard]] f32 FogSample(const FogFrame& fog, core::Vec2 world, u32 channel)` (bilinear between tile centres,
    clamped at the map's edge, exactly as the GPU's linear filter with clamp-to-edge samples a texel per tile; 1 with no
    fog)
  - `[[nodiscard]] f32 SightFade(const FogFrame& fog, core::Vec2 world)` (smoothstep(0.5, 1, the in-sight sample))
  - `[[nodiscard]] f32 TileExplored(const FogFrame& fog, core::Vec2 world)` (the tile under `world`'s own explored value,
    0 to 1; 1 with no fog or off the map)
  - `gfx::View::fog` (`FogFrame`, none by default)
- Produces (`namespace app`, `fog_fade.hpp`): `class FogFade { public: void Update(const core::Snapshot& snapshot,
  const core::WorldMap& map, f32 seconds, f32 fadeSeconds); [[nodiscard]] gfx::FogFrame Frame() const; };`
  - targets per tile: explored = the bit; in sight = 1 when `debugSight`, else `InSight(playerTile, tile, radius)`
    (0 everywhere without a player); recomputed only when `version`, `playerTile`, `sightRadius` or `debugSight` change
  - each value moves towards its target by `seconds / fadeSeconds` of the full range (linear; the shaders shape it);
    nothing is touched while no value is moving
  - snaps to the targets on the first fog it sees, on a new `snapshot.epoch` (a load, a replay, a new game), on a new
    `snapshot.sceneChange.change` and when `debugSight` switches; `Frame()` has no columns while `snapshot.fog.on` is false

- [ ] **Step 1: Write the failing tests**
  - `gfx/fog_tests.cpp` on a 4 × 4 field written in the test: `TEST_CASE("a fog sample at a tile's centre is its
    value")`; `TEST_CASE("a fog sample on the edge between an explored and an unexplored tile is half")`;
    `TEST_CASE("sight fades from full half a tile inside its edge to nothing at the edge")` (1 at the in-sight tile's
    centre, 0.5 a quarter tile in from the edge — the sample there 0.75 — and 0 at the edge); `TEST_CASE("a place's tile
    fades by its own value, whatever its neighbours")`; `TEST_CASE("no fog fades and shades nothing")`.
  - `app/fog_fade_tests.cpp` on the small test map: `TEST_CASE("a newly explored tile eases to explored over the fade
    time and holds")` (0 → about 0.5 at a quarter second, 1 at half a second, 1 a second later);
    `TEST_CASE("the first fog, a new epoch, a scene change and a debug sight switch snap the values")`;
    `TEST_CASE("debug sight puts every tile in sight and explores none")`; `TEST_CASE("leaving sight eases out while
    explored stays")`; `TEST_CASE("no fog in the snapshot gives a frame with no columns")`.
- [ ] **Step 2: Run** `pixi run project_n test --suite gfx_tests --test fog`: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `pixi run project_n test --suite gfx_tests`, `--suite app_tests`: PASS.
- [ ] **Step 5: Commit** `feat(app): ease the world map's fog on the main thread`.

### Task 5: publishing the fog, walkers in sight and places once explored

**review: full** (publishing, determinism) · **implementer: opus**

**Files:**
- Modify: `src/game/host.hpp`, `src/game/host.cpp`, `src/game/scene.hpp`, `src/game/world_state.hpp` (`WorldContext`),
  `src/game/scenes/world.cpp`, `src/AGENTS.md`
- Test: `tests/unit/game/host_tests.cpp`, `tests/unit/game/scenes_tests.cpp`

**Interfaces:**
- Consumes: Task 1 (`FogView`, `INSTANCE_SIGHT_FADE`, `TextFog`, `SpriteDraw::flags`, `SetDebugSight`), Task 3
  (`explored`, `sightRadius`, `FogVersion`).
- Produces: `Host::SetDebugSight(bool on)` (override; kept for publishing only); on the world map each publish fills
  `snapshot.fog` (`on`, the version, the words, the foot's tile when a player stands there, the radius, the debug sight).
- `Host::PublishJourneys`: a traveller is published only when the debug sight is on or its tile is within
  `sightRadius + 1` of the player's tile (none without a player); its villager and shadow instances carry
  `INSTANCE_SIGHT_FADE`; its label `fog = TextFog::Sight` and `height` the villager's foot height, so anchor + height is
  its foot.
- `SceneContext::Explored(u32 tile) const` (virtual, true by default); `WorldContext` answers from its world's bits (true
  when it has none). `World::Publish` names a place only once its tile is explored, with `fog = TextFog::Explored`.

- [ ] **Step 1: Write the failing tests**
  - `host_tests.cpp`: `TEST_CASE("walkers beyond sight's ring aren't published, those in sight and on the ring are")`
    (travellers placed by a hand-built journey at distances 3, 4 and 5 from the cultivator's tile);
    `TEST_CASE("with the debug sight every walker in view is published")`; `TEST_CASE("walkers and their shadows and
    labels are flagged to fade with sight")`; `TEST_CASE("the fog in the snapshot follows exploring")` (version up after
    a step onto a new tile, unchanged after a step within explored land; the words equal `ExploredTiles()`);
    `TEST_CASE("debug sight changes no state: the same ticks hash alike with it on and off")` (WalkTo, a selection, 300
    ticks each way, `StateHash` equal, the selection kept).
  - `scenes_tests.cpp`: `TEST_CASE("a place's name is published only once its tile is explored")`;
    `TEST_CASE("a dev scene publishes no fog")`.
- [ ] **Step 2: Run** `pixi run project_n test --suite game_tests --test host`: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `pixi run project_n test --suite game_tests`: PASS.
- [ ] **Step 5: Commit** `feat(game): publish the fog and show walkers only in sight`.

### Task 6: the cloud layout

**review: full** (rendering logic, the anti-goals' pops and gaps) · **implementer: opus**

**Files:**
- Create: `src/gfx/cloud_draw.hpp`, `src/gfx/cloud_draw.cpp`, `tests/unit/gfx/cloud_draw_tests.cpp`

**Interfaces:**
- Consumes: Task 2's `core::FogLook`; Task 4's `gfx::FogFrame`.
- Produces (`namespace gfx`):
  - `inline constexpr u32 MAX_CLOUD_INSTANCES = 8'192;`
  - `struct CloudDrawing { std::vector<core::Instance> instances; std::vector<core::Batch> batches; };`
  - `[[nodiscard]] f32 CloudLevel(f32 zoom);` (max(0, log2(1 / zoom)))
  - `void DrawClouds(const FogFrame& fog, const core::FogLook& look, const core::Manifest& manifest, core::Rect view, f32
    zoom, f64 seconds, CloudDrawing& into);` (replaces `into`; nothing with no fog or no clouds; one normal-blend batch;
    instances back to front by foot y, `previous == current`; at most `MAX_CLOUD_INSTANCES`, the rest dropped with the
    farthest-back first)
- The layout, in tiles (a cell at level `l` is `0.5 × 2^l` tiles across, from the map's origin):
  - `W(cell)` = 1 − the largest eased explored value of the tiles it touches (tiles off the map count as explored).
  - Each top-level cell in view (with a margin of one cell) is reached with weight 1; a reached cell draws its clouds at
    opacity `R × W` and passes `R × (1 − W)` to its four children; a level-0 cell draws at `R × W` and passes nothing;
    nothing below opacity 1/255 is drawn or recursed into. So merges, splits and uncovers all cross-fade with no state.
  - Zoom: with `level = CloudLevel(zoom)`, `lo = floor(level)`, `blend = clamp((level − lo − 0.35) / 0.3, 0, 1)`; the
    layout from level `lo` at opacity × (1 − blend) and, when blend > 0, from `lo + 1` at opacity × blend.
  - A cell's anchors (from `lod.py`): (0.32, 0.28) and (0.68, 0.76) of the cell, and in half the cells a third at
    (0.5 ± 0.3, 0.5 ± 0.2); each jittered by up to ±0.225 × and ±0.175 × the cell; width `cloudWidth × cell × (0.85 + 0.6 ×
    draw)`; one of `look.clouds` and a mirror flip (a negative width) by draw. Every draw is a hash of (column, row,
    level, anchor, purpose) from `core/random.hpp`, so panning and zooming back give the same clouds.
  - Edge clamp: on each side of a cloud's box where the tiles beyond its cell's own are explored or off the map (eased
    value above 0), the box moves in, then shrinks, so its whole box stays inside the cell's tiles.
  - Bob: the foot moves by `bobAmplitude × cell × sin(2π × seconds / bobPeriod + phase)`, the phase from the cloud's hash
    (through `core::math`'s sine for one result on every platform).
  - Uncover: as the cell's largest explored value `e` grows, each cloud moves `drift × e` tiles in its own hashed
    direction (its opacity already falls with `W`).

- [ ] **Step 1: Write the failing tests** on small fields built in the test, with a test look of 4 clouds:
  `TEST_CASE("the levels at zoom 1, 0.6, 0.4 and 0.25 are 0, 1, 1 and 2, and 0.71 and 0.35 cross-fade half and half")`
  (`CloudLevel`, then the summed opacity of each level's clouds over one unexplored region);
  `TEST_CASE("the same view gives the same clouds")`; `TEST_CASE("panning away and back gives the same clouds")`;
  `TEST_CASE("an all-unexplored map at zoom 0.25 draws whole level-2 clouds only")`; `TEST_CASE("a cell touching an
  explored tile splits into finer clouds")`; `TEST_CASE("no cloud's box reaches into an explored tile or past the map")`;
  `TEST_CASE("a tile halfway uncovered draws its clouds at half opacity, drifted half their distance")`;
  `TEST_CASE("a fully explored view draws no clouds")`; `TEST_CASE("clouds are drawn back to front by their foot")`;
  `TEST_CASE("a 32:9 view at zoom 0.25 over the shipped map, all unexplored, stays within the buffer")` (the count, and
  the worst over zoom 1 too, asserted once); `TEST_CASE("the bob moves a cloud by at most its amplitude")`.
- [ ] **Step 2: Run** `pixi run project_n test --suite gfx_tests --test cloud_draw`: FAIL.
- [ ] **Step 3: Implement** with `PN_PROFILE_ZONE` on `DrawClouds` only.
- [ ] **Step 4: Run** `pixi run project_n test --suite gfx_tests`: PASS.
- [ ] **Step 5: Commit** `feat(gfx): lay the fog's clouds out from the eased fog`.

### Task 7: the fog texture, the terrain's shading and the sight fades

**review: full** (rendering, shaders) · **implementer: opus**

**Files:**
- Modify: `shaders/sprite.hlsli`, `shaders/sprite.vert.hlsl`, `shaders/sprite.frag.hlsl`, `shaders/bin/*`,
  `src/gfx/generated/*`, `src/gfx/internal/features/sprites.*`, `src/gfx/renderer.cpp`, `src/gfx/terrain_draw.cpp`,
  `src/gfx/labels.*` (or wherever the label pass sets a label's opacity), `src/AGENTS.md`
- Test: `tests/unit/gfx/terrain_draw_tests.cpp`, `tests/unit/gfx/labels_tests.cpp`

**Interfaces:**
- Consumes: Task 1 (`Instance::flags`, its two flags, `TextFog`, `SightPoint`), Task 2 (`FogLook` colours and
  strengths), Task 4 (`View::fog`, `FogSample`, `SightFade`, `TileExplored`).
- The renderer keeps a two-channel 8-bit texture the size of the map (a 1 × 1 one, explored and in sight, when the view
  has no fog), uploaded only when `View::fog.revision` changes, sampled linearly with clamp-to-edge, bound to the sprite
  fragment stage of every sprite draw.
- The sprite draw uniform gains the fog's map size and tile size, whether fog is on, and the look's linear colours: the
  sepia tint (the sepia colour over its own luminance) with the drain strength, the mist colour with its strength, the
  shade colour.
- Fragment, for an instance with `INSTANCE_FOGGED_LAND` while fog is on (premultiplied colour `c`, `e` explored and `s`
  in sight sampled at the fragment's world position):
  ```
  drained = lerp(c.rgb, luminance(c.rgb) * sepiaTint, drain)
  rgb     = lerp(c.rgb, drained, e * (1 - s))
  rgb     = lerp(rgb, mist * c.a, mistStrength * e * (1 - s))
  rgb     = lerp(rgb, shade * c.a, 1 - smoothstep(0.0, 0.5, e))
  ```
  (the shade reaches nothing at an unexplored tile's edge, so its edge stays under the edge row of clouds).
- For an instance with `INSTANCE_SIGHT_FADE` while fog is on: its whole colour × smoothstep(0.5, 1, `s` at its sight
  point), the point taken in the vertex stage as `core::SightPoint` and passed flat.
- `DrawTerrain` sets `INSTANCE_FOGGED_LAND` on every instance it writes. The label pass multiplies a `TextFog::Sight`
  label's opacity by `SightFade(fog, anchor + (0, height))` and a `TextFog::Explored` one's by `TileExplored(fog,
  anchor)`.

- [ ] **Step 1: Write the failing tests**: `terrain_draw_tests.cpp`: `TEST_CASE("every terrain instance is flagged as
  fogged land")`; `labels_tests.cpp` (or the test file of the pass that places labels): `TEST_CASE("a walker's label
  fades with sight at its foot")`, `TEST_CASE("a place's name fades in with its own tile")`, `TEST_CASE("a label with
  no fog keeps its opacity")`.
- [ ] **Step 2: Run** `pixi run project_n test --suite gfx_tests --test terrain_draw`: FAIL.
- [ ] **Step 3: Implement**; `pixi run project_n shaders`.
- [ ] **Step 4: Run** `pixi run project_n test --suite gfx_tests` and `pixi run project_n reference`: PASS, no picture
  moved (no fog reaches a reference case until Task 10).
- [ ] **Step 5: Commit** `feat(gfx): drain, veil and shade the world map by the fog` (shaders and generated headers with
  it).

### Task 8: the main thread's fog, picking and the debug sight

**review: full** (input, picking) · **implementer: sonnet**

**Files:**
- Modify: `src/app/app.hpp`, `src/app/app.cpp`, `src/app/view.hpp`, `src/app/view.cpp`, `src/app/bindings.hpp`,
  `src/app/bindings.cpp`, `src/main.cpp`, `src/AGENTS.md` (Commands: `--debug-sight`, F3)
- Test: `tests/unit/app/view_tests.cpp`, `tests/unit/app/bindings_tests.cpp`, `tests/unit/app/command_line_tests.cpp`

**Interfaces:**
- Consumes: Task 1 (`ViewReport::debugSight`, `INSTANCE_SIGHT_FADE`, `SightPoint`), Task 4 (`FogFade`, `FogFrame`,
  `SightFade`).
- Produces: `app::Options::debugSight` (`--debug-sight`); `Control::ToggleDebugSight` bound to F3 (a debug key);
  `App` owns a `FogFade`, updates it each drawn frame from the drawn snapshot, the map and the look's `fadeSeconds`
  (frame seconds from the frame clock), puts `Frame()` in `gfx::View::fog`, and sends its debug sight in every
  `ViewReport`; F3 flips it and logs the outcome at info.
- `PickInstance(const core::Snapshot&, f64 alpha, core::Vec2 world, const gfx::FogFrame& fog)` (and `Pick` likewise):
  an instance with `INSTANCE_SIGHT_FADE` whose `SightFade` at its `SightPoint` is under 0.5 is skipped.

- [ ] **Step 1: Write the failing tests**: `view_tests.cpp`: `TEST_CASE("a walker drawn at under half opacity can't be
  picked and one over half can")`, `TEST_CASE("without fog every instance is pickable as before")`;
  `bindings_tests.cpp`: `TEST_CASE("F3 toggles the debug sight")`; `command_line_tests.cpp`: `TEST_CASE("--debug-sight
  turns the debug sight on")`.
- [ ] **Step 2: Run** `pixi run project_n test --suite app_tests --test view`: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `pixi run project_n test --suite app_tests`: PASS; `pixi run project_n run -- --world --quit-after
  20` starts and quits cleanly (the clouds don't draw until Task 9).
- [ ] **Step 5: Commit** `feat(app): ease the fog, skip unseen walkers when picking, add debug sight`.

### Task 9: the clouds drawn, the target cursor over them

**review: full** (rendering, draw order) · **implementer: sonnet**

**Files:**
- Modify: `src/gfx/renderer.hpp`, `src/gfx/renderer.cpp`, `src/gfx/route_draw.hpp`, `src/gfx/route_draw.cpp`,
  `src/gfx/internal/features/*` as needed, `src/AGENTS.md` (the rendering line)
- Test: `tests/unit/gfx/route_draw_tests.cpp`

**Interfaces:**
- Consumes: Task 6 (`DrawClouds`, `CloudDrawing`, `MAX_CLOUD_INSTANCES`), Task 7 (the fog texture and uniforms).
- On the world map, when `View::fog` has columns, the renderer lays the clouds out each frame for the camera's view, zoom
  and `View::frameClock`, uploads them into a sprite buffer of `MAX_CLOUD_INSTANCES` and draws them as one batch after
  the snapshot's entities and their outlines and before the seals overhead, the swords, the overlay, text and HUD.
- The route's buffer splits: the dots and marker draw where they do (under everything), the target tile draws after the
  clouds; `DrawRoute` writes the cursor into its own drawing (`RouteDrawing::cursor` or a second output: the
  implementer's choice, documented).

- [ ] **Step 1: Write the failing test**: `route_draw_tests.cpp`: `TEST_CASE("the target tile is laid out apart from the
  route's dots")`.
- [ ] **Step 2: Run** `pixi run project_n test --suite gfx_tests --test route_draw`: FAIL.
- [ ] **Step 3: Implement**, with `PN_PROFILE_ZONE` round the layout and the upload.
- [ ] **Step 4: Run** `pixi run project_n test --suite gfx_tests` and `pixi run project_n reference`: PASS; look at the
  game (look-at-the-game skill: `--world`, zoom 1 and 0.25, a walk east) and report what shows.
- [ ] **Step 5: Commit** `feat(gfx): draw the fog's clouds over the world map`.

### Task 10: pictures, cloud shimmer and the measure run

**review: full** (tooling that gives a verdict) · **implementer: sonnet**

**Files:**
- Modify: `tests/reference/reference_tests.cpp`, `tests/reference/*.png` (new and updated), `tools/project_n.py`
  (`MEASURE_RUNS` world args, measure's report), `tools/AGENTS.md`, `tests/AGENTS.md`
- Test: `tests/reference/reference_tests.cpp`, tools' self-tests

**Interfaces:**
- Consumes: Task 3 (`Host::Explore`), Task 5 (`Host::SetDebugSight`), Task 8 (`app::FogFade`), Task 9 (the clouds).
- The harness's `View` gains `exploreAll` (explore the picture's view in setup) and `debugSight`; every case on the world
  scene updates an `app::FogFade` from its snapshot (snapped) and sets `gfx::View::fog`, with `frameClock` fixed at 0.
- The six existing world pictures (`world_close`, `world_middle`, `world_widest`, `world_places`, `world_places_widest`,
  `world_route`) explore their view and turn the debug sight on: they keep what they show today (allowed pixel drift
  only from the fog texture's sampling, at most `ALLOWED_PIXELS`); the world shimmer guard does the same and keeps 0.06.
- New pictures: `fog_start_close` (a new game at zoom 1 on the start), `fog_start_step` (zoom 0.71), `fog_start_middle`
  (zoom 0.4), `fog_start_widest` (zoom 0.25), `fog_explored_path` (after a route east and back, zoom 0.6: the drain and
  mist), `fog_uncovering` (a step onto a new tile, drawn a quarter second into its fade).
- `TEST_CASE("the fog's clouds at the smallest zoom: shimmer held to its bound")`: over unexplored land at zoom 0.25,
  clouds at rest, sub-pixel camera moves as the world case does; prints `cloud shimmer: <value>`; its bound is the first
  measurement × 1.25, written in the case with the measurement in its comment.
- `measure`'s world run adds `--debug-sight`; measure reads and reports `cloud shimmer` beside `world shimmer`.

- [ ] **Step 1: Write the new cases and harness inputs**; run `pixi run project_n reference`: the new cases FAIL (no
  pictures), the old ones PASS.
- [ ] **Step 2: Run** `pixi run project_n reference --update`; look at every new and changed picture (look-at-the-game
  skill) and describe each in the report; set the cloud shimmer bound from the printed value.
- [ ] **Step 3: Change** the measure world run and its report, with self-tests for both in `test_project_n.py`; run
  `pixi run project_n selftest`: PASS.
- [ ] **Step 4: Run** `pixi run project_n reference`: PASS.
- [ ] **Step 5: Commit** `test(reference): picture the fog and guard the clouds' shimmer` and
  `build(tools): measure the world run with the debug sight` (two commits).

### Task 11: measurements and the Outcome

**review: light** · **implementer: sonnet** · runs alone, when the user says the display is free

- [ ] **Step 1:** `pixi run project_n measure --runs world` on this branch and on main (main's numbers from a fresh run
  the same way); `pixi run project_n profile --seconds 30 -- --world --debug-sight --zoom 0.25` for the clouds' layout
  zone at the widest view.
- [ ] **Step 2:** Write the spec's **Outcome**: the measured tables, the cloud layout time (target under 0.2 ms), the
  cloud shimmer's first measurement and bound, every ruling made while building with its reason, and mark the spec
  implemented.
- [ ] **Step 3: Commit** `docs(design): record the fog and sight outcome`.
