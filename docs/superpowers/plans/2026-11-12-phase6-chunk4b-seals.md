# Phase 6 chunk 4b: seals, implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A data-driven seal system: seals of 1 to 9 turning layers of script, lines, figures, emblems, satellites and
light, seen at the art's angle with an ink wash under their glow; the flying swords' seal and a showcase in the arena.

**Architecture:** The simulation holds one small record per seal (`core::Seal`: which, where, how large, when, which
element), saved and published in `Snapshot::seals`; the swords' cast raises one. The look is presentation data
(`assets/data/seals.yaml` → `core::Seals`, not hashed, hot-reloaded). The main thread turns records, looks and the
render time into band geometry and a packed parts buffer (pure functions in `gfx/seal_draw`, tested without a device);
two new pipelines draw the wash (normal blend) and the lines and light (additive), procedurally, from analytic distance
fields and distance-field sprites, in a fixed few draw calls per frame.

**Tech Stack:** C++23, SDL_GPU, HLSL (`project_n shaders`), yaml-cpp (only in `data`), doctest, Python placeholder art
(`art/placeholders.py`), all through Pixi.

**Spec:** `docs/superpowers/specs/2026-11-12-phase6-chunk4b-seals-design.md` (read it first; its mockups are beside it).

**Starts after chunk 4 merges.** This branch was cut from main before chunk 4; rebase it onto main once chunk 4's PR
merges, then start Task 1. Every file below exists on main from then on.

## Global Constraints

- **Determinism:** the simulation draws randomness only through `core::Random` with a purpose from
  `game/random_purposes.hpp`; game data is hashed; a recorded fight replays with every hash matching.
- **Look and record apart:** nothing about a seal's look is in the simulation or game data; game data names a seal only
  by its key. `seals.yaml` is not hashed and not saved.
- **Drawing is a pure function** of the seal record, `core::Seals` and the render time (`core::RenderTick(snapshot
  .simulationTick, alpha)`): no per-seal state kept between frames on the CPU or GPU.
- **Colours come from the table** (`core::UiColours`): no seal colour in code or a shader. A colour slot is `element`
  (the record's element's effect colour) or a table name.
- **The art's angle:** one ratio for every seal, `view_ratio`, default `0.375` (the blob shadow's 24 / 64). A point
  `(x, depth, up)` in a seal's frame lands on screen at world `(x, depth × view_ratio − up)` from the seal's point.
- **Ink wash, then glow:** each seal draws its wash with normal premultiplied blending, then its lines and light
  additively.
- **Limits:** 1 to 9 layers a seal; at most 16 parts a layer; satellites nest at most 2 deep; at most 256 particles a
  seal; at most 64 seals drawn a frame, satellites included. Past a limit at load: refused with the line. Past a buffer
  at draw: reported through `ReportOverflow` / `core::RepeatedFailure`, never dropped silently.
- **Draw order:** ground seals after the `Ground` and `Decals` layers, before `World`; upright and overhead seals after
  the overlay, before text and the HUD; an opacity cap on those (`above_opacity`, default `0.7`).
- **Accessibility hooks:** `seals.yaml` has `spin_scale` (default `1`) multiplying every spin, particle speed and the
  appear and fade times' inverse, and `brightness` (default `1`) multiplying every line and light; chunk 11 scales them.
- **Small unit tests** (the user's rule): about 20 chosen samples, each named for why it is there; a loop's worst value
  is asserted once; never one `CHECK` per sample in a big loop.
- **Repo rules** (AGENTS.md, src/AGENTS.md, tests/AGENTS.md): sized types, `std::expected` and no throws, Doxygen on
  every public declaration, `PN_PROFILE_ZONE()` in per-frame functions (not per-item ones), logging outcomes only,
  standard headers via `pch.hpp`, no third-party types in headers, `TEST_CASE`s in an anonymous namespace with
  `constexpr` test values, the MSVC gotchas.
- **Each task ends** with `pixi run project_n test` passing and, once after implementation and once after review fixes,
  `pixi run project_n lint --range <task base>..HEAD`. A task that moves pixels runs `pixi run project_n reference
  --update`, looks at the pictures (look-at-the-game skill) and commits the PNGs with it. Shader changes: `pixi run
  project_n shaders`, committing `shaders/bin/` and `src/gfx/generated/`. Art changes: `pixi run project_n atlas`,
  committing `assets/manifest.yaml`, `src/core/generated/atlas.hpp` and `art/atlas.sha256`.
- **Commits:** Conventional Commits, scopes from `SCOPES` in tools/project_n.py, `feat`/`fix` only for game code; docs
  cite commits by subject; AGENTS.md files change in the same commit as what they describe.

## Review Focus

1. **A broken `seals.yaml` edit mid-run** (a satellite loop, a missing glyph set, a 10th layer): the old table keeps
   drawing and the error names the line. Test in Task 2 ("a broken seals table keeps the old one") and Task 6 (reload).
2. **A save loaded mid-formation:** the swords' seal shows at its life's stage at once, without appearing again, and
   ends on time. Test in Task 4 ("a seal survives a save and load mid-formation").
3. **Zoomed out, a screen-wide seal's fine lines:** at the smallest zoom, lines thinner than a pixel fade instead of
   crawling as the rings turn. Test in Task 6 (the shimmer measure on two sub-pixel offsets).
4. **Many seals at once** (casts overlapping, every showcase seal and their satellites): past 64 drawn, the rest are
   reported, never a crash or a silent drop. Test in Task 5 ("more seals than the frame holds are counted").
5. **A scene change or load mid-seal** (F2, F9): no seal outlives its scene. Test in Task 4 ("a scene change ends every
   seal").

---

### Task 1: Placeholder seal art: runes, flourishes and emblems as distance fields

**Files:**
- Modify: `art/placeholders.py`, `art/sprites.toml` (append only), `art/scenes.toml` (`arena` list)
- Generated: `assets/sprites/*.png`, `assets/manifest.yaml`, `src/core/generated/atlas.hpp`, `art/atlas.sha256`
- Modify: `assets/sprites/PROVENANCE.md`

**Interfaces:**
- Produces (the only glyph set this chunk ships: font characters as a glyph set are left for later): sprites `rune_00` … `rune_23` (an invented rune set), `flourish_flame_0` … `flourish_flame_2`,
  `emblem_lotus`, `emblem_swords`, `emblem_eye`, `emblem_spiral` in the arena scene, each white with its alpha holding a
  signed distance field: alpha = clamp(0.5 + d / (2 × SDF_SPREAD), 0, 1), `d` in pixels positive inside, `SDF_SPREAD` =
  8 pixels, so alpha 0.5 is the edge. Runes 48 × 64 pixels, flourishes 256 × 128, emblems 256 × 256.

- [ ] **Step 1:** in `placeholders.py`, a distance-field writer (stroke shapes rasterised at 4× and reduced to a field)
  and the generators: runes as 2 to 5 strokes on a 3 × 4 grid (seeded, as the mockup's `glyph()`), flame flourishes as
  tapering curls, four abstract emblems (no creature from the references). Deterministic from fixed seeds.
- [ ] **Step 2:** append the sprites to `sprites.toml` and the arena's list in `scenes.toml`; run
  `pixi run project_n atlas --placeholders`, then `pixi run project_n atlas`; expected: the manifest lists them.
- [ ] **Step 3:** look at three of the PNGs (a rune, a flourish, an emblem): the alpha ramps across each stroke's edge.
- [ ] **Step 4:** `pixi run project_n test`; PROVENANCE.md names them as procedural placeholders.
- [ ] **Step 5: Commit** `feat(art): placeholder runes, flourishes and emblems for seals` (scope per `SCOPES`).

### Task 2: The seal looks: `core::Seals` and `seals.yaml`

**Files:**
- Create: `src/core/seals.hpp`, `src/data/seals.hpp`, `src/data/seals.cpp`, `assets/data/seals.yaml`,
  `tests/unit/data/seals_tests.cpp`, `tests/fuzz/corpus/yaml/seals`
- Modify: `tests/fuzz/yaml.cpp`, `src/AGENTS.md`, `src/data/module.dox`

**Interfaces:**
- Consumes: `data::internal` helpers (`TunedNumber`, `TunedWhole`, `NoteUnknownKeys`), `core::Manifest` (sprite names),
  `core::UiColour` names (Task 3 adds two; this task accepts any existing name).
- Produces (all in `core`):
  - `using SealKey = u64;` and `[[nodiscard]] constexpr SealKey SealKeyOf(std::string_view name)`: FNV-1a 64 of the
    name; stable across runs and platforms.
  - `enum class SealPlane : u8 { Ground, Upright, Overhead };`
  - `enum class SealPartKind : u8 { Circle, Ticks, Dots, Script, Star, Polygon, Flourish };`
  - `struct SealColourSlot { bool element = true; UiColour colour = UiColour::DamageEffect; };`
  - `struct SealPart { SealPartKind kind; u8 slot; f32 radius; f32 width; f32 size; u32 count; u32 step; f32 dash;
    f32 gap; f32 rotation; f32 spill; bool inward; u32 glyphSet; std::array<atlas::SpriteId, 3> sprites; };` (radius,
    width and size as fractions of the seal's size; unused fields stay zero)
  - `struct SealLayer { f32 inner; f32 outer; f32 height; f32 spin; f32 opacity; u32 frameSides; f32 frameRotation;
    std::vector<SealPart> parts; };` (`frameSides` 0 = round; spin in degrees a second, negative turns the other way)
  - `struct SealEmblem { atlas::SpriteId sprite; f32 size; f32 spin; u8 slot; };`
  - `enum class SealMount : u8 { Fixed, WithLayer, Orbit };`
    `struct SealSatellite { SealKey seal; u32 layer; f32 angle; f32 size; SealMount mount; f32 orbit; };`
  - `struct SealLight { u32 pillars; f32 pillarHeight; f32 pillarWidth; u32 sparks; f32 sparkRise; f64 sparkLife;
    u32 shards; f32 shardDrift; f32 shardSpin; u8 slot; };`
  - `struct SealLook { SealPlane plane; f32 lift; f32 washDarkness; f32 washReach; f32 washSoftness;
    std::array<SealColourSlot, 2> colours; u8 colourCount; f64 appear; f64 fade; bool innerFirst;
    std::vector<SealLayer> layers; std::optional<SealEmblem> emblem; std::vector<SealSatellite> satellites;
    SealLight light; };`
  - `struct Seals { f32 viewRatio = 0.375F; f32 aboveOpacity = 0.7F; f32 spinScale = 1.0F; f32 brightness = 1.0F;
    std::vector<std::vector<atlas::SpriteId>> glyphSets; std::vector<std::pair<SealKey, SealLook>> looks;
    [[nodiscard]] const SealLook* Find(SealKey key) const; };` (`looks` sorted by key)
  - Limits: `MAX_SEAL_LAYERS = 9`, `MAX_SEAL_PARTS = 16`, `MAX_SATELLITE_DEPTH = 2`, `MAX_SEAL_PARTICLES = 256`.
  - In `data`: `[[nodiscard]] std::expected<core::Seals, core::Error> ParseSeals(std::string_view text, const
    core::Manifest& manifest)` and `LoadSeals(const std::filesystem::path& folder, const core::Manifest& manifest)`.
- YAML shape (key names may be adjusted to match the other tables, the structure may not):
  ```yaml
  view_ratio: 0.375
  above_opacity: 0.7
  spin_scale: 1
  brightness: 1
  glyph_sets:
    runes: [rune_00, rune_01, ...]
  seals:
    sword_formation:
      plane: ground          # ground | upright | overhead
      lift: 0                # world units above the ground
      wash: { darkness: 0.6, reach: 1.15, softness: 0.25 }
      colours: [element, seal_gold]
      life: { appear: 0.4, fade: 0.3, order: outer_first }
      layers:
        - { inner: 0.86, outer: 1.0, height: 0, spin: 9, opacity: 0.9, parts: [ ... ] }
      emblem: { sprite: emblem_swords, size: 0.18, spin: 0, colour: 1 }
      satellites: [ { seal: small_star, layer: 0, angle: 0, size: 0.2, mount: with_layer } ]
      light: { pillars: { count: 0 }, sparks: { count: 24, rise: 40, life: 1.2 }, shards: { count: 0 } }
  ```
  Times in seconds, angles in degrees, sizes as fractions of the seal's size except `lift` and heights (world units).

- [ ] **Step 1: Write the failing tests** in `seals_tests.cpp`:
  - `TEST_CASE("the shipped seals table loads clean")`: `LoadSeals` of `assets/data` with the shipped manifest returns a
    table where `Find(SealKeyOf("sword_formation"))` is not null and no warning is logged.
  - `TEST_CASE("a seal key is the same for the same name")`: `SealKeyOf("a") == SealKeyOf("a")`,
    `SealKeyOf("a") != SealKeyOf("b")`, and a `constexpr` known value for `"sword_formation"` (computed once, pinned).
  - `TEST_CASE("the seals reader refuses out-of-range values with their line")`: one sample each, checking the message
    names the line: 0 layers; 10 layers; 17 parts; a size ≤ 0; `inner ≥ outer`; an unknown colour name; an unknown
    sprite; an unknown glyph set; a satellite naming its own seal; two seals naming each other; satellites 3 deep;
    `view_ratio` outside (0, 1]; a negative appear time; `sparks.count` over 256.
  - `TEST_CASE("the seals reader warns on unknown keys and reads the rest")`.
  - `TEST_CASE("a broken seals table keeps the old one")`: `ParseSeals` of a broken text returns an error, and the
    caller's previous `core::Seals` is untouched (the reload contract `App` relies on).
- [ ] **Step 2:** `pixi run project_n test --suite data_tests --test seals`; expected: fails to compile (no
  `data/seals.hpp`).
- [ ] **Step 3:** implement `core/seals.hpp` and `data/seals.cpp` as the Interfaces say, sharing the yaml helpers;
  write `assets/data/seals.yaml` with one seal, `sword_formation` (script look: two rune bands, a star, ticks, an emblem;
  `colours: [element]`; ground), every value commented with its unit and effect, as `effects.yaml` does.
- [ ] **Step 4:** the `yaml` fuzz target parses `seals.yaml` text too (with the shipped manifest), seed
  `tests/fuzz/corpus/yaml/seals` = the shipped file; run `pixi run project_n test --suite data_tests`; expected: pass.
- [ ] **Step 5: Commit** `feat(data): read the seals' looks from data`.

### Task 3: The seals' accent colours

**Files:**
- Modify: `src/core/ui_colours.hpp` (and its `.cpp` if any), `assets/data/strings.yaml`, `src/data/settings.cpp` if
  the settings file lists colours by name, `tests/unit/core/ui_colours_tests.cpp`

**Interfaces:**
- Produces: `core::UiColour::SealGold` (default sRGB `#F2C14E`) and `core::UiColour::SealViolet` (default `#B48CF0`),
  both in the **Effects** area, `UI_COLOURS` 24 → 26, their names in English and Simplified Chinese in `strings.yaml`,
  settable by name in `seals.yaml` as `seal_gold` and `seal_violet`.

- [ ] **Step 1:** failing test `TEST_CASE("the seal accents have defaults and names")`: the two packed defaults
  (converted as the neighbours are) and both names resolving in both languages.
- [ ] **Step 2:** `pixi run project_n test --suite core_tests --test ui_colours`; expected: fails to compile.
- [ ] **Step 3:** add the two entries where every colour is listed (enum, defaults, area, names, settings keys).
- [ ] **Step 4:** run the suites that list colours (`core_tests`, `data_tests`); expected: pass.
- [ ] **Step 5: Commit** `feat(core): gold and violet accents for seals`.

### Task 4: Seals in the simulation: the record, the swords' seal, saves

**Files:**
- Modify: `src/core/frame.hpp` (`Seal`, `Snapshot::seals`), `src/core/abilities` types (where `AbilityData` lives),
  `src/data/abilities.cpp`, `assets/data/abilities.yaml`, `src/game/combat.hpp`, `src/game/combat.cpp`,
  `src/game/arena.cpp` (`ARENA_FORMAT` 3 → 4), `tests/unit/game/combat_tests.cpp`, `tests/unit/game/save_tests.cpp`,
  `tests/unit/data/abilities_tests.cpp`, fuzz seeds `tests/fuzz/corpus/save/*`, `tests/fuzz/corpus/replay/*`,
  `tests/fuzz/corpus/yaml/abilities`

**Interfaces:**
- Consumes: `core::SealKey`, `core::SealKeyOf` (Task 2); `core::DepartureTick` (chunk 4).
- Produces:
  - `struct core::Seal { SealKey key; Vec2 point; f32 size; u64 startTick; u64 endTick; HitKind element; bool
    hasElement; bool showcase; }` with `Fields()`; `endTick == NEVER_ENDS` (`u64` max) for one that never ends;
    `showcase` is set only by the arena's scene content (Task 8), never saved.
  - `std::vector<core::Seal> Snapshot::seals` (refilled in place).
  - `struct core::SealCast { SealKey key; f32 size; }` and `std::optional<SealCast> AbilityData::seal`, in the hashed
    game data; read from an ability's `seal: { name: sword_formation, size: 420 }`.
  - `Combat` raises a `Seal` when a volley whose ability has a `seal` is cast: `point` = the volley's origin, `startTick`
    = the cast tick, `endTick` = the last sword's `DepartureTick`, `element` = the volley's element. It is removed at
    `endTick`, saved with the combat state, and cleared with everything else on a scene change or load.

- [ ] **Step 1: Write the failing tests:**
  - `TEST_CASE("casting the swords raises one seal at the cast point until the last sword leaves")`: after a cast at
    tick T, `snapshot.seals` holds one seal with the ability's key and size, `startTick == T`, `endTick ==` the last
    sword's departure; at `endTick` it is gone.
  - `TEST_CASE("an ability without a seal raises none")`: the fire bolt.
  - `TEST_CASE("a seal survives a save and load mid-formation")`: save halfway to `endTick`, load, publish: the same
    record, ending at the same tick.
  - `TEST_CASE("a scene change ends every seal")`.
  - `TEST_CASE("the abilities reader reads a seal and refuses a bad one")`: a size ≤ 0 and an empty name, with lines.
  - The game data hash changes when an ability's seal name changes (one sample beside the existing hash test).
- [ ] **Step 2:** run `combat_tests`, `save_tests`, `abilities_tests`; expected: fail to compile.
- [ ] **Step 3:** implement as the Interfaces say; `abilities.yaml`'s `flying_swords` gains
  `seal: { name: sword_formation, size: 420 }` (a commented value: the seal's outer radius in world units).
- [ ] **Step 4:** bump `ARENA_FORMAT` to 4 with its history comment; regenerate the `save` fight seed (tests/AGENTS.md's
  skipped generator case) and the `replay` seeds as documented; refresh the `yaml/abilities` seed; run
  `pixi run project_n test`; expected: pass, the recorded fight's hashes regenerated where the format requires it.
- [ ] **Step 5: Commit** `feat(game): raise a seal under the flying swords`.

### Task 5: Laying seals out: the angle, the bands, the parts, the life

**Files:**
- Create: `src/gfx/seal_draw.hpp`, `src/gfx/seal_draw.cpp`, `tests/unit/gfx/seal_draw_tests.cpp`

**Interfaces:**
- Consumes: `core::Seal`, `core::Seals`, `core::SealLook` (Tasks 2, 4), `core::UiColours`, `core::Manifest`.
- Produces (pure, no device):
  - `[[nodiscard]] Vec2 SealToWorld(const core::Seal& seal, core::SealPlane plane, f32 lift, f32 viewRatio, SealLocal local)`, with `struct SealLocal { f32 x; f32 depth; f32 up; };`:
    `local` in world units in the seal's frame (`Ground`/`Overhead`: x and depth on the plane, up the
    layer's height plus `lift`; `Upright`: x and up on the plane, depth the layer's height); applies the Global
    Constraints' projection.
  - `[[nodiscard]] f32 SealLifeOpacity(const core::SealLook& look, std::size_t layer, f64 secondsSinceStart, f64
    secondsToEnd)`: layers appear one after another over `look.appear` (each over `appear / layers`, outer or inner
    first), all fade together over the last `look.fade` seconds before the end; 1 while held.
  - `[[nodiscard]] f32 SealLayerAngle(const core::SealLayer& layer, f64 seconds, f32 spinScale)` in radians.
  - `struct SealGpuPart` (16-byte aligned, the shader's `StructuredBuffer` layout: kind, slot, radii, width, counts,
    dash, sprite UV rects for scripts, flourishes and emblems) and `struct SealGpuLayer` (centre, axes for the projection,
    angle, opacity × life × brightness, two premultiplied colours, first part and part count, inner and outer radius).
  - `struct SealVertex` (world position, the band's local position, the layer index).
  - `struct SealGpuParticles` (a seal's light: its key hash, start seconds, the particle counts and sizes, centre and
    axes, colour), one per seal with light; the particle draw's instances index into them.
  - `struct SealDrawing { std::vector<SealVertex> groundWash, groundLines, aboveWash, aboveLines; std::vector<SealGpuLayer>
    layers; std::vector<SealGpuPart> parts; std::vector<SealGpuParticles> particles; std::size_t droppedSeals; void
    Clear(); }` and `void DrawSeals(std::span<const core::Seal> seals, const core::Seals& looks, const core::UiColours&
    colours, f64 renderSeconds, u64 renderTick, bool showcase, SealDrawing& out)`: expands satellites (to their depth
    limit), skips `showcase` seals when `showcase` is false, builds each layer's band (an annulus of 48 segments from
    `inner` to `outer`, or the frame polygon's band), its wash quad (to `washReach`), its parts sorted by radius, and the
    light's particle spans; counts seals past 64 in `droppedSeals`. A key missing from `looks` is skipped (the caller
    logs it once).

- [ ] **Step 1: Write the failing tests** (`gfx_tests`), about 20 samples across:
  - `TEST_CASE("a ground seal lies flat at the art's angle")`: the points at 0°, 90°, 180°, 270° of a radius-100 ring
    land at (±100, 0) and (0, ±37.5) from the point; a layer at height 20 lands 20 higher.
  - `TEST_CASE("an upright seal stands as a circle and its heights come forward")`.
  - `TEST_CASE("an overhead seal is lifted by its lift")`.
  - `TEST_CASE("layers appear one after another and fade together")`: outer-first, 3 layers, `appear` 0.3: layer 0 at
    0.1 s is 1, layer 2 at 0.1 s is 0; every layer at `fade / 2` before the end is 0.5.
  - `TEST_CASE("a layer turns at its spin, scaled by spin_scale")`: −90 °/s at 1 s is −π/2; `spin_scale` 0 holds it still.
  - `TEST_CASE("a satellite on a turning ring follows it")`: `WithLayer` at 0° on a layer spinning 90 °/s is at 90°
    after 1 s; `Fixed` stays; `Orbit` moves at its own speed.
  - `TEST_CASE("a seal's parts are sorted by radius")`.
  - `TEST_CASE("an element slot takes the record's element colour")` and a named slot takes the table's.
  - `TEST_CASE("hidden showcase seals draw nothing")`.
  - `TEST_CASE("more seals than the frame holds are counted")`: 70 seals → 64 drawn, `droppedSeals == 6`.
- [ ] **Step 2:** `pixi run project_n test --suite gfx_tests --test seal_draw`; expected: fails to compile.
- [ ] **Step 3:** implement the Interfaces in `seal_draw.cpp`; one `PN_PROFILE_ZONE()` in `DrawSeals` only.
- [ ] **Step 4:** run the test again; expected: pass.
- [ ] **Step 5: Commit** `feat(gfx): lay seals out at the art's angle`.

### Task 6: Drawing seals: the wash and the lines

**Files:**
- Create: `shaders/seal.vert.hlsl`, `shaders/seal.frag.hlsl`, `shaders/seal.hlsli`,
  `src/gfx/internal/features/seals.hpp`, `src/gfx/internal/features/seals.cpp`
- Modify: `src/gfx/renderer.hpp`, `src/gfx/renderer.cpp`, `src/core/draw_list.cpp` and `src/core/frame.hpp`
  (`Snapshot::groundBatches`), `src/app/app.cpp` (load and reload `seals.yaml`), `src/main.cpp`,
  `tests/reference/reference_tests.cpp`, `tests/reference/*.png`, `tests/unit/core/draw_list_tests.cpp`,
  `src/AGENTS.md`, `src/gfx/module.dox`

**Interfaces:**
- Consumes: Task 5's `SealDrawing` and `DrawSeals`; Task 1's sprites.
- Produces:
  - `u32 Snapshot::groundBatches`: batches `[0, groundBatches)` hold only the `Ground` and `Decals` layers (the draw
    list splits a batch at that boundary).
  - `class gfx::internal::SealRenderer` with `Create(device, format)`, `Upload(copy, const SealDrawing&)` returning the
    count past its buffers, and `DrawGround(commands, pass, worldToClip)` / `DrawAbove(...)`: each the wash (normal
    premultiplied blend, `ONE, ONE_MINUS_SRC_ALPHA`) then the lines (additive, `ONE, ONE`), one draw each.
  - `void Renderer::SetSeals(const core::Seals& seals)`; `App` loads `seals.yaml` at start (after the manifest) and its
    data watch reloads it like `effects.yaml`, keeping the old table on an error; a seal key missing from the table is
    logged once per key.
  - The fragment shader draws each part as an analytic distance field (circle, dashed circle, ticks, dots, star,
    polygon, frame) or by sampling the atlas's distance-field sprites (script, flourish, emblem), anti-aliased with
    `fwidth`, testing only parts whose band holds the pixel.

- [ ] **Step 1: Write the failing tests:**
  - `TEST_CASE("the ground batches hold only the ground and decals")` in `draw_list_tests.cpp`.
  - Reference pictures: `TEST_CASE("a seal on the pale ground: coloured lines glowing on their wash")` (the
    `sword_formation` look at a fixed time, raised on the arena with the showcase hidden, close up) and
    `TEST_CASE("a screen-wide seal at the smallest zoom: no shimmer")` (two captures a quarter pixel apart; the ink
    difference stays under the text shimmer measure's bound).
  - `TEST_CASE("new seals change the next picture of the same snapshot")` (as the effects' reload picture test).
- [ ] **Step 2:** `pixi run project_n test --suite core_tests --test draw_list` fails; `pixi run project_n reference`
  fails for lack of the new pictures.
- [ ] **Step 3:** write the shaders, `pixi run project_n shaders`; implement `SealRenderer` and wire it into
  `DrawWorld`: ground seals between `instances.Draw` of the ground batches and of the rest; above seals after the
  overlay, before text; upload in the frame's copy pass beside the ribbons, reporting overflow as they do.
- [ ] **Step 4:** `pixi run project_n reference --update`, look at the new pictures (the wash darkens the paper, the
  lines read coloured, not white; the rings turn smoothly between two times), and confirm no existing picture moved;
  `pixi run project_n test`; expected: pass.
- [ ] **Step 5: Commit** `feat(gfx): draw seals with an ink wash under their glow` (with shaders/bin, generated
  headers and PNGs).

### Task 7: Seal light: pillars, sparks and shards

**Files:**
- Modify: `shaders/seal.*.hlsl`, `src/gfx/internal/features/seals.*`, `src/gfx/seal_draw.*`,
  `tests/unit/gfx/seal_draw_tests.cpp`, `tests/reference/*`

**Interfaces:**
- Consumes: `SealLight` (Task 2), `SealDrawing::particles` (Task 5).
- Produces: one instanced additive draw for all particles of a frame: the vertex shader places particle `i` of a seal
  from its seal's start time, the render time and a hash of (seal key, start tick, i): pillars as tall quads falling
  from the plane, sparks rising and fading over `sparkLife`, shards drifting and turning. No particle state anywhere.

- [ ] **Step 1:** failing tests: `TEST_CASE("a seal's particle count is its light's, capped")` and
  `TEST_CASE("particles are placed the same for the same time")` (a C++ mirror of the shader's placement function,
  `SealParticleAt(...)`, sampled at 3 times for 3 particles, and twice for the same inputs); a reference picture of an
  overhead seal with pillars and sparks.
- [ ] **Step 2:** run them; expected: fail.
- [ ] **Step 3:** implement `SealParticleAt` in `seal_draw.cpp` and the same function in `seal.hlsli`; the draw.
- [ ] **Step 4:** `reference --update`, look, `test`; expected: pass.
- [ ] **Step 5: Commit** `feat(gfx): light seals with pillars, sparks and shards`.

### Task 8: The showcase and the swords' seal in the arena

**Files:**
- Modify: `assets/data/seals.yaml` (the showcase looks), `src/game/arena.cpp` and `arena.hpp` (the showcase's places,
  published with `showcase = true`; the header's feature list), `src/gfx/camera.hpp` (`View::showcase`, default true),
  `src/app/bindings.cpp` and `app.cpp` (a debug key toggling it), `tests/reference/reference_tests.cpp` and PNGs,
  `src/AGENTS.md` (the key), `tests/unit/game/arena_tests.cpp` (or where the arena's publish is tested)

**Interfaces:**
- Consumes: everything above.
- Produces: the showcase seals the spec lists, authored in `seals.yaml`: `showcase_grand` (ground, screen-wide, 9
  layers, two rune bands at two sizes, a star, ticks, dots, emblem, `[seal_gold, damage-coloured]` two colours),
  `showcase_upright` (upright before the cultivator's start, satellites `showcase_small_1` and `showcase_small_3` on its
  ring), `showcase_overhead` (overhead, pillars and sparks), `showcase_tiers` (diamond and square frames at rising
  heights), `showcase_flame` (flourishes spilling, shards, violet accent); and `showcase_small_1` (1 layer),
  `showcase_small_3` (3 layers). Their places and sizes are constants beside `PROPS`; they are published every tick,
  never saved, `endTick == NEVER_ENDS`.

- [ ] **Step 1:** failing tests: `TEST_CASE("the arena publishes its showcase seals, unsaved")` (the count and keys;
  a save and load leaves them as scene content); existing arena reference tests set `View::showcase = false`; new
  pictures: the showcase whole at zoom 1; the swords' seal mid-formation under the rings.
- [ ] **Step 2:** run them; expected: fail.
- [ ] **Step 3:** author the looks, place them so each stays readable and the fight still plays, add the key.
- [ ] **Step 4:** `reference --update`; look at every new picture and the swords' seal by eye in a run
  (`pixi run project_n run`): each part and placement in the spec's list is visible somewhere; existing pictures
  unchanged; `test`; expected: pass.
- [ ] **Step 5: Commit** `feat(game): show seals of every kind in the arena`.

### Task 9: Measurements, docs and the Outcome

**Files:**
- Modify: `docs/superpowers/specs/2026-11-12-phase6-chunk4b-seals-design.md` (Outcome, Status),
  `docs/superpowers/specs/2026-11-10-phase6-design.md` (chunk 4b's status), `src/AGENTS.md`, `tests/AGENTS.md` (the
  seals' fuzz seed), `AGENTS.md` (map: `seals.yaml`)

- [ ] **Step 1:** `pixi run project_n profile --seconds 30` with the showcase shown and hidden, and
  `pixi run project_n profile --seconds 30 -- --cast-swords`; at the window's usual size and at 3840 × 2160 (the window
  size in `settings.yaml`); record median / 99th / worst
  frame time, `DrawSeals` and `SealRenderer` upload zones and `SubmitFrame` against chunk 4's numbers.
- [ ] **Step 2:** the Outcome: what was built, the measurements, every ruling with what it costs if wrong; the spec's
  Status `implemented`; the phase table's chunk 4b `Done`.
- [ ] **Step 3:** `pixi run project_n check`; expected: pass.
- [ ] **Step 4: Commit** `docs(design): record what chunk 4b built`.
