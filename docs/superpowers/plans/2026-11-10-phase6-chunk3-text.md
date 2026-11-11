# Phase 6 Chunk 3: Game Text and Damage Numbers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Name labels and damage numbers drawn as distance-field text that scales with the zoom, stays crisp and
smooth at every size, stacks and gives way instead of crowding, and shows every hit, proved in the arena by the
crystal's aura and a debug burst of thousands of hits.

**Architecture:** The logic publishes `core::TextItem`s (labels, captions) in the snapshot and `core::Event` hits
(kind, ability use, target) through the handover. On the main thread, `app::DamageNumbers` turns played hits into
number `TextItem`s; `gfx` lays every item out from one signed-distance-field glyph cache (`gfx::TextLayout`, new glyphs
rasterised on a job-system worker), places labels through a screen-space stacking and priority pass
(`gfx::LabelPass`), and draws all of it through the mesh path, whose fragment shader turns the field into coverage,
outline and shadow in one pass.

**Tech Stack:** C++23, SDL3_ttf (`TTF_SetFontSDF`, FreeType's SDF renderer), SDL_GPU, HLSL shaders through
`./dev shaders`, yaml-cpp (via `data`), doctest, the reference pictures on lavapipe.

**Spec:** `docs/superpowers/specs/2026-11-10-phase6-chunk3-text-design.md` (with the phase spec
`2026-11-10-phase6-design.md`, decisions 16 to 18).

## Global Constraints

- The user's bar: **"Text rendering must remain crisp and smooth at all zoom levels, this is very important to me."**
  No blur, shimmer, jitter, popping, hitch on new glyphs, label pollution, or lost damage numbers (the spec's anti-goals).
- Zoom range `gfx::MIN_ZOOM` 0.25 to `gfx::MAX_ZOOM` 2; at zoom 1 the window shows the 1920-wide reference, so a world
  unit is about a pixel at 1080p.
- Glyph pages stay R8, `GLYPH_PAGE_SIZE` 2048², shelf-packed, uploaded in the frame's own copy pass.
- Glyph quads keep sub-pixel positions: **no rounding** of text positions anywhere (the uncommitted `std::round` goes).
- Labels in the chosen language only; game text in one overhead layer after the world; numbers are text through the
  glyph cache (no digit strip, no `core::Batch::texture`).
- Events are output only: saves, replays and `StateHash()` don't change; nothing random outside `core/random.hpp`.
- Nothing dropped silently: anything over a buffer limit is logged as instances are (`ReportOverflow`, `RepeatedFailure`).
- Settings keys and defaults: `combined_damage_numbers: false`, `damage_number_motion: bounce` (or `rise`),
  `text_outline: true`, `text_shadow: true`; an unknown value is a warning and the default.
- Repo rules (AGENTS.md, src/AGENTS.md, tests/AGENTS.md): sized types, `std::expected` and no throws (`std::format`
  only with literal format strings), Doxygen on every public declaration, `PN_PROFILE_ZONE()` in per-frame functions,
  logs of outcomes only, standard headers through `pch.hpp`, third-party types out of headers, `TEST_CASE`s in an
  anonymous namespace with `constexpr` test values, MSVC gotchas (double braces on `std::array`, named temporaries
  inside `CHECK`).
- Each task ends with `pixi run project_n test` and `pixi run project_n lint` passing; every commit builds and passes
  on its own. Tasks that move pixels run `pixi run project_n reference --update`, look at the new pictures
  (look-at-the-game skill) and commit the PNGs with the change.
- Shaders: after editing `shaders/`, `pixi run project_n shaders` and commit `shaders/bin/` and
  `src/gfx/generated/` with it.
- Commits: Conventional Commits, scopes from `SCOPES`; one commit per task unless the task says otherwise.

## Review Focus

1. **Chinese:** with `language: zh`, labels, stacks ("狐（+3）") and captions draw every glyph from the pre-warm, none
   missing on the first frame (Task 3 test `pre-warm covers every string in both languages and the digits`; Task 5
   test `a Chinese stack label reads name（+N）`).
2. **The debug burst outside the arena** (F2 to the stress, painted or cells scene, then F7): no hits, no crash, a log
   line saying nothing was hit (Task 2 test `the debug burst hits nothing in a scene without foxes`).
3. **Load or replay mid-flight** (a new epoch): numbers from the old timeline vanish at once instead of floating over
   entities that may not exist (Task 6 test `a new epoch clears the live numbers`).
4. **Repeated bursts past the mesh buffer:** F7 held or pressed five times in a second; the quads that don't fit are
   reported once per run, never silently lost, and the frame survives (Task 4 test
   `mesh quads past the buffer are counted, not drawn`; Task 7 measurement).
5. **The selected fox in a pack of foxes:** it keeps its own label, never folded into "Fox (+N)", and the stack's
   count leaves it out (Task 5 test `a selected label never joins a stack`).

## Accessibility

Moved, corrected, into the spec's Accessibility section (phase decision 17 asks the spec for it): the stack glide moves
position, so reduce motion needs a hook there too.

## File Structure

- `assets/data/strings.yaml`, `src/core/strings.hpp`: `fox_name`, `label_stack` (with `{name}` and `{count}` tokens).
- `src/core/settings.hpp`, `src/data/settings.cpp`: the four settings.
- `src/core/frame.hpp`: `TextItem` reworked (kinds, world offsets, entity, name key, priority); `Event` gains a hit;
  `ActionKind::DebugBurst`.
- `src/core/logic.{hpp,cpp}`: `Simulation::Tick` gains the event vector; Logic stamps and publishes.
- `src/game/arena.{hpp,cpp}`: the aura, the debug burst, labels on the player and foxes, the caption.
- `src/platform/window.{hpp,cpp}`, `src/app/bindings.{hpp,cpp}`, `src/app/app.cpp`: F7 and `Control::DebugBurst`.
- `src/gfx/shelf_packer.{hpp,cpp}` (salvaged, padding becomes the field's spread), `src/gfx/text_layout.{hpp,cpp}`
  (salvaged, reworked to distance fields, pre-warm and a worker): the glyph cache.
- `src/gfx/labels.{hpp,cpp}` (new): the pure label pass.
- `src/gfx/internal/features/meshes.{hpp,cpp}`, `shaders/mesh.*`: u32 indices, more quads, the field shader.
- `src/gfx/renderer.{hpp,cpp}`, `src/gfx/camera.hpp`: placing text, `TextOptions`, `View::clock`, extra texts.
- `src/app/damage_numbers.{hpp,cpp}` (new): the live numbers, combined mode and motion.
- Tests: `tests/unit/{data,core,game,gfx,app}/…`, `tests/reference/reference_tests.cpp` and its PNGs.

---

### Task 0: Park the uncommitted work

The uncommitted tree predates the spec and doesn't build. Nothing is lost: it's kept as a salvage patch beside the
plan ledger and brought back piece by piece in Tasks 3 to 5.

- [ ] **Step 1: Save the salvage.** `mkdir -p .superpowers/sdd/chunk3/salvage`, then
  `git diff --binary > .superpowers/sdd/chunk3/salvage/tracked.patch` and copy the six untracked files
  (`src/gfx/shelf_packer.*`, `src/gfx/text_layout.*`, `tests/unit/gfx/shelf_packer_tests.cpp`,
  `tests/unit/gfx/text_layout_tests.cpp`) into the same folder.
- [ ] **Step 2: Restore the tree.** `git restore --staged --worktree .` and remove the six untracked files.
- [ ] **Step 3: Verify.** `git status --short` prints nothing; `pixi run project_n test` passes (the baseline).

No commit (the ledger is gitignored).

### Task 1: Strings and settings for text and numbers

**Files:**
- Modify: `assets/data/strings.yaml`, `src/core/strings.hpp`, `src/core/settings.hpp`, `src/data/settings.cpp`
- Test: `tests/unit/data/strings_tests.cpp`, `tests/unit/data/settings_tests.cpp`; fuzz seed
  `tests/fuzz/corpus/yaml/strings` regenerated from the new table

**Interfaces:**
- Produces: `core::StringId::FoxName`, `core::StringId::LabelStack` (`STRINGS` = 4); `core::NumberMotion { Bounce, Rise }`;
  `core::Settings::combinedDamageNumbers` (`bool`, false), `damageNumberMotion` (`NumberMotion`, Bounce),
  `textOutline` (`bool`, true), `textShadow` (`bool`, true).

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the text settings default to every hit, bouncing, outlined and shadowed") {
    const std::expected<core::Settings, core::Error> settings = data::ParseSettings("{}\n");
    REQUIRE(settings);
    CHECK_FALSE(settings->combinedDamageNumbers);
    CHECK(settings->damageNumberMotion == core::NumberMotion::Bounce);
    CHECK(settings->textOutline);
    CHECK(settings->textShadow);
}
TEST_CASE("the text settings are read") {
    // combined_damage_numbers: true, damage_number_motion: rise, text_outline: false, text_shadow: false
    // → true, Rise, false, false
}
TEST_CASE("an unknown number motion gives bounce with a warning") {
    // damage_number_motion: wobble → Bounce, one warning naming line 1 and "wobble"
}
TEST_CASE("a text setting that isn't a boolean keeps its default with a warning") {
    // text_outline: maybe → true, one warning
}
TEST_CASE("the strings table has the fox's name and the stack label in both languages") {
    // the shipped strings.yaml: Text(FoxName, English) == "Fox", Text(FoxName, Chinese) == "狐",
    // Text(LabelStack, English) == "{name} (+{count})", Text(LabelStack, Chinese) == "{name}（+{count}）"
}
```

- [ ] **Step 2: Run them to see them fail.** `pixi run project_n test --suite data_tests` → compile errors on the
  missing members.
- [ ] **Step 3: Implement.** Add the strings to `strings.yaml` (the uncommitted mixed-language `cultivator_name` stays
  reverted) and `STRING_NAMES`; the settings as top-level keys in `ParseSettings`, added to `NoteUnknownKeys`' list;
  unknown values logged and defaulted as `language` is.
- [ ] **Step 4: Run.** `pixi run project_n test --suite data_tests` passes; `pixi run project_n lint`.
- [ ] **Step 5: Commit.** `feat(data): read the text and damage number settings`

### Task 2: Hits from the crystal's aura and a debug burst

**Files:**
- Modify: `src/core/frame.hpp` (`Event`, `ActionKind`), `src/core/logic.{hpp,cpp}`, `src/game/arena.{hpp,cpp}`,
  `src/platform/window.{hpp,cpp}` (`InputCode::KeyF7`), `src/app/bindings.{hpp,cpp}`, `src/app/app.cpp`
- Test: `tests/unit/core/logic_tests.cpp`, `tests/unit/game/arena_tests.cpp` (or the arena's existing test file),
  `tests/unit/app/bindings_tests.cpp`

**Interfaces:**
- Produces:
  - `enum class EventKind : u8 { Hit, UseEnded };` `enum class HitKind : u8 { Damage, Healing, Poison, Burn };`
    `inline constexpr std::size_t HIT_KINDS = 4;`
  - `core::Event` gains `EventKind kind = EventKind::Hit; HitKind hit = HitKind::Damage; u64 use = 0;
    StableId target{}; Vec2 position{}; i32 value = 0;` (`tick`, `epoch`, `cosmetic` stay; hits and ends are
    **not** cosmetic: never dropped). Its comment says what exists now.
  - `virtual void Simulation::Tick(const TickCounters& counters, const TickInput& input, std::vector<Event>& events) = 0;`
    the simulation appends; `Logic` stamps `tick = counters.simulation` and `epoch` on what was appended and publishes
    them with the snapshot as today.
  - `ActionKind::DebugBurst` after `SceneResident`, `ACTION_KINDS` follows it; recorded in replays (harmless: events
    only). `Control::DebugBurst` bound to `InputCode::KeyF7`.
  - The ability-use ID: `core::AbilityUse(u64 tick, u32 source) -> u64` (`tick << 8 | source`) in `frame.hpp`, sources
    `USE_AURA = 1`, `USE_DEBUG_BURST = 2` in `arena.cpp`. Every use emits its hits and then one `UseEnded` with the
    same `use`, on the same tick.

Arena rules (the spec's; values are `constexpr` in `arena.cpp`):
- The aura: every 60th simulation tick, each fox whose body is within `AURA_REACH` 320 world units of `CRYSTAL.foot`
  takes a `Damage` hit of 5 to 40; the player, if within reach, a `Healing` hit of 5 to 20. Values from
  `core::Random(seed, counters.state, stableId, PURPOSE_AURA, 0)`. Position: the target's head (foot minus its sprite's
  `footY`). Arena scene only.
- The debug burst: on the action, `DEBUG_BURST_HITS` 4,000 hits spread round-robin over the arena's foxes, `hit` kind
  cycling Damage, Healing, Poison, Burn by hit index, values 1 to 99 from `PURPOSE_BURST` with the hit index as the
  draw. No foxes (other scenes): no hits, logged once at info.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the aura hits a fox in reach once a second and none between") {
    // arena ticked to simulation tick 59 with a fox placed 100 units from the crystal: no events;
    // tick 60: one Hit (Damage, target = the fox, value in [5, 40]) followed by one UseEnded with the same use
}
TEST_CASE("the aura misses a fox out of reach") { /* fox 400 units away at tick 60: only the UseEnded */ }
TEST_CASE("the aura heals the player in reach") { /* player 50 units from the crystal: a Healing hit in [5, 20] */ }
TEST_CASE("the aura's hits are the same on a rerun") { /* two arenas, same ticks: identical event vectors */ }
TEST_CASE("the debug burst leaves the state hash alone") { /* after a burst tick, StateHash() equals an arena ticked without it */ }
TEST_CASE("the debug burst hits every fox with all four kinds") {
    // DebugBurst action: 4,000 Hits, then one UseEnded; each fox gets 4,000 / 12 (±1); kinds cycle by index
}
TEST_CASE("the debug burst hits nothing in a scene without foxes") { /* Painted scene: one UseEnded, no Hits */ }
TEST_CASE("logic stamps and publishes a tick's events") {
    // logic_tests' fake simulation appends one event per tick; after a burst of 3 the handover holds 3 events,
    // ticks 1..3, the burst's epoch
}
TEST_CASE("F7 sends the debug burst") { /* bindings: KeyF7 down → Control::DebugBurst pressed */ }
```

- [ ] **Step 2: Run them to see them fail.** `pixi run project_n test --suite game_tests`, `core_tests`, `app_tests`.
- [ ] **Step 3: Implement.** Find every `Simulation::Tick` override and caller with the `LSP` tool
  (`findReferences`), not grep. App maps `Control::DebugBurst` to the action through its `ActionControl` table. The
  arena holds no pointer to the vector: `Tick` writes into its parameter. Update the
  arena header's feature list, src/AGENTS.md's debug keys (F7) and 0002 §5's note on events if it names the old shape.
- [ ] **Step 4: Run.** The three suites pass; `pixi run project_n lint`.
- [ ] **Step 5: Commit.** `feat(game): emit hits from the crystal's aura and a debug burst`

### Task 3: The distance-field glyph cache

**Files:**
- Create (from the salvage): `src/gfx/shelf_packer.{hpp,cpp}`, `src/gfx/text_layout.{hpp,cpp}`,
  `tests/unit/gfx/shelf_packer_tests.cpp`, `tests/unit/gfx/text_layout_tests.cpp`
- Modify: `src/gfx/internal/gpu.hpp` (the salvaged `SpanAt`, with `BytesAt` built on it)

**Interfaces:**
- Consumes: `core::Strings`, `core::Jobs::Background`, `core::Jobs::WaitForBackground`.
- Produces (all in `gfx/text_layout.hpp` unless noted):
  - `ShelfPacker(u32 pageSize, u32 padding)` (`shelf_packer.hpp`): the padding was a fixed texel; now it's
    `SDF_SPREAD` + 1 so neighbouring fields never bleed.
  - `inline constexpr f32 SDF_BASE_PIXELS = 48.0F;` the size every glyph's field is rasterised at (a starting value: a
    clean 修/道 at 48 is checked in Task 4's pictures; changing it is the builder's call, recorded in the Outcome).
  - `inline constexpr u32 SDF_SPREAD = 8;` FreeType's default SDF spread in texels (SDL3_ttf doesn't change it).
  - `struct PlacedGlyph { f32 x; f32 y; f32 width; f32 height; u32 page; u32 u; u32 v; };` x, y, size in **base
    pixels** from the text box's top left (including the spread margin); u, v its texel on the page.
  - `struct Layout { f32 width; f32 height; std::vector<PlacedGlyph> glyphs; bool complete; };` in base pixels;
    `complete` false while a glyph is still on the worker.
  - `class TextLayout`:
    - `static std::expected<TextLayout, core::Error> Open(const std::filesystem::path& fonts);` opens
      `NotoSansSC-Regular.otf` at `SDF_BASE_PIXELS` twice with `TTF_SetFontSDF(font, true)`: one font shapes on the
      main thread, the other rasterises on the worker (a `TTF_Font` is never shared between threads).
    - `void Prewarm(const core::Strings& strings);` rasterises, on the calling thread, every glyph of every string in
      both languages (with `{name}`/`{count}` removed from `LabelStack`) plus `0123456789+-（）() `: start-up's designed
      pause. Logs how many glyphs and how long.
    - `const Layout& Lay(std::string_view text, core::TextDirection direction);` cached by direction and text; a
      glyph not yet rasterised is requested from the worker and left out (`complete = false`); an incomplete layout
      is made again on the next `Lay` after its glyphs land.
    - `void EndFrame(core::Jobs& jobs);` forgets layouts unused for `LAYOUT_FRAMES` (300), moves glyphs the worker
      finished into `Pending()`, and if glyphs are waiting and no batch is running, starts one batch with
      `jobs.Background` (one batch at a time: the worker's font is used by one thread).
    - `u32 Pages() const; std::span<const GlyphUpload> Pending() const; void Uploaded(std::size_t count);` as salvaged.
  - The worker's state (its font, the request and result queues under a `core::Mutex` with `PN_GUARDED_BY`) lives in
    a `std::shared_ptr` the background job holds, so a `TextLayout` destroyed mid-batch is safe.

- [ ] **Step 1: Write the failing tests.** Keep the salvaged packer tests, adapted to the padding parameter, and these:

```cpp
TEST_CASE("the packer pads each glyph by the field's spread") {
    // ShelfPacker{256, 9}: Pack(10, 10) at (0,0); the next Pack(10, 10) at x = 19
}
TEST_CASE("a glyph's field is rasterised once and is a distance field") {
    // Prewarm(table with "修"): one Pending upload; its border texels (spread margin) are below 128 and its
    // centre stroke texels at or above 128 (inside); a second Lay("修") adds nothing pending
}
TEST_CASE("pre-warm covers every string in both languages and the digits") {
    // after Prewarm(shipped strings): Lay of each string in each language, of "{name}"-free LabelStack,
    // and of "-1234567890+" is complete with no new glyph requested
}
TEST_CASE("a glyph not pre-warmed is drawn once the worker has rasterised it") {
    // Lay("鹤") → complete false, no glyph for 鹤; jobs.WaitForBackground(); EndFrame(jobs);
    // Lay("鹤") → complete true, one glyph, one Pending upload
}
TEST_CASE("a layout is in base pixels whatever size it is drawn at") {
    // Lay("Fox").height within 1 of SDF_BASE_PIXELS * 1.2 + 2 * SDF_SPREAD (line height plus margins)
}
// kept from the salvage, adapted: laid out once; uploaded glyphs leave the queue oldest first;
// a vertical caption runs down, first character on top; a character the font lacks draws nothing and is logged once
```

- [ ] **Step 2: Run them to see them fail.** `pixi run project_n test --suite gfx_tests --test text_layout`.
- [ ] **Step 3: Implement.** Bring the salvaged files back and rework them as above; drop the per-`FontId` font table
  and its `shortcut:` comment (sizes now live with the text kinds, Task 4). Fix the salvaged `-Weffc++` finding at
  the `Impl` (initialise every member) and the MSVC one in the test (name the temporary before `CHECK`, tests/AGENTS.md).
  Nothing in the renderer uses the cache yet.
- [ ] **Step 4: Run.** `pixi run project_n test --suite gfx_tests`; `pixi run project_n lint`.
- [ ] **Step 5: Commit.** `feat(gfx): cache glyphs as distance fields`

### Task 4: Draw labels and captions as distance fields

**Files:**
- Modify: `src/core/frame.hpp` (`TextItem`, `Snapshot::texts`), `src/core/logic.cpp` (clears `texts`),
  `src/gfx/internal/features/meshes.{hpp,cpp}`, `shaders/mesh.{vert,frag}.hlsl`, `shaders/mesh.hlsli`,
  `src/gfx/camera.hpp` (`View::clock`), `src/gfx/renderer.{hpp,cpp}`, `src/game/arena.{hpp,cpp}`, `src/main.cpp`,
  `src/app/app.cpp`, `tests/reference/reference_tests.cpp`
- Test: `tests/unit/gfx/meshes_tests.cpp` (new, CPU parts only), `tests/unit/gfx/text_place_tests.cpp` (new),
  `tests/unit/game/…` (the arena's texts), reference PNGs

**Interfaces:**
- Consumes: Task 3's `TextLayout`, `Layout`, `PlacedGlyph`, `SDF_BASE_PIXELS`.
- Produces:
  - In `core/frame.hpp` (replacing the salvaged `FontId`):
    `enum class TextKind : u8 { Label, Caption, Number };`
    `enum class LabelPriority : u8 { Rest, Named, Player, Hovered, Selected };` (higher wins)
    `enum class NumberSign : u8 { Plain, Plus };`
    `struct TextItem { TextKind kind; StringId string; i32 value; NumberSign sign; Vec2 previous; Vec2 current;
    Vec2 offset /* world units */; f32 lift /* Number: ems above the anchor */; f32 opacity /* 1 */;
    StableId entity; f32 height /* Label: the named entity's height, world units */; u32 nameKey;
    LabelPriority priority; u32 colour; TextAlign align; TextDirection direction; };`
    plus the salvaged `TextAlign`, `TextDirection`, and `Snapshot::texts`.
  - In `gfx/renderer.hpp`: `struct TextOptions { core::Language language = core::Language::English;
    bool outline = true; bool shadow = true; f32 scale = 1.0F; };`
    `Renderer::Create(window, assets, manifest, std::shared_ptr<const core::Strings> strings, const TextOptions& text)`
    (pre-warms), `void SetTextOptions(const TextOptions& text);`
    `void Draw(const core::Snapshot&, const View&, core::Jobs& jobs, std::span<const core::TextItem> extra = {});`
    `Capture(snapshot, view, width, height, std::span<const core::TextItem> extra = {})` (waits for the worker first,
    so a capture never misses a glyph).
  - `gfx::View::clock` (`f64`, render-clock seconds; App passes the render tick over 60 ticks a second).
  - Sizes, in `gfx/text_place.hpp` (new, pure, tested without a device):
    `inline constexpr f32 LABEL_WORLD_SIZE = 20.0F, CAPTION_WORLD_SIZE = 32.0F, NUMBER_WORLD_SIZE = 24.0F;`
    `inline constexpr f32 LABEL_MIN_PIXELS = 9.0F, NUMBER_MIN_PIXELS = 12.0F;`
    `[[nodiscard]] f32 TextPixels(core::TextKind kind, f32 zoom, f32 scale);` (world size × zoom × scale; numbers
    clamp up to `NUMBER_MIN_PIXELS × scale`; labels and captions don't clamp)
    `void PlaceGlyphs(const Layout& layout, core::Vec2 topLeft, f32 pixels, u32 colour, std::vector<MeshVertex>& out);`
    (`pixels / SDF_BASE_PIXELS` scales the base-pixel layout; no rounding).
  - Mesh path: `MAX_MESH_QUADS = 65'536` (4 MiB of vertices), `u32` indices; `MeshRun` unchanged. A fragment uniform
    `struct TextStyle { f32 outline; f32 shadow; f32 shadowOffsetU; f32 shadowOffsetV; }` (1 or 0 for on/off; the
    shadow offset in page fractions for 2 base pixels down-right).
  - The field shader: `d = page.Sample(...)`; `w = max(fwidth(d) * 0.7, 1e-4)`; fill = `smoothstep(0.5 - w, 0.5 + w, d)`;
    outline = `smoothstep(0.5 - OUTLINE - w, 0.5 - OUTLINE + w, d)` with `OUTLINE` = 2 base pixels in field units
    (`2 / (2 * SDF_SPREAD)`, 0.125); shadow = a second sample at the offset through a softer `smoothstep(0.2, 0.6, d)`.
    Composite premultiplied: shadow under outline under fill, the outline and shadow dark ink `#20242A`
    (linear 0.0144, 0.0176, 0.0232) at the fill's alpha.
  - The arena publishes: the player's label (`CultivatorName`, `Player`, `nameKey` = its `StringId`), a label per fox
    (`FoxName`, `Rest`), each anchored at the head with `offset` `{0, -6}` world units and `height` = the sprite's
    `footY` × scale; the tablet caption (`Caption`, `TopToBottom`, offset `{24, 0}`). Label colour: light
    `#F4EFE6`; with outline and shadow both off, dark ink `#20242A` (the renderer swaps it: the logic doesn't know
    the settings). Caption colour: pale gold `#F2D9A0`, same swap.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("text grows with the zoom") {
    CHECK(gfx::TextPixels(core::TextKind::Label, 1.0F, 1.0F) == doctest::Approx(20.0F));
    CHECK(gfx::TextPixels(core::TextKind::Label, 2.0F, 1.0F) == doctest::Approx(40.0F));
    CHECK(gfx::TextPixels(core::TextKind::Label, 0.25F, 1.0F) == doctest::Approx(5.0F)); // below readable: faded later
}
TEST_CASE("numbers stop shrinking at their minimum, scaled with the text size") {
    CHECK(gfx::TextPixels(core::TextKind::Number, 0.25F, 1.0F) == doctest::Approx(12.0F));
    CHECK(gfx::TextPixels(core::TextKind::Number, 0.25F, 1.5F) == doctest::Approx(18.0F));
}
TEST_CASE("glyph quads keep sub-pixel positions") {
    // PlaceGlyphs(one-glyph layout at x=4,y=2 base px, size 24x30), topLeft {10.25, 7.5}, pixels 24 (scale 0.5):
    // the four vertices at x 12.25 / 24.25, y 8.5 / 23.5 exactly, uv from the glyph's page texels
}
TEST_CASE("mesh quads past the buffer are counted, not drawn") {
    // the CPU half of MeshRenderer::Upload's arithmetic: 65,537 quads → 1 quad (4 vertices) reported dropped
}
TEST_CASE("the arena labels the player and every fox, and captions the tablet") {
    // Publish: texts holds 1 Player label, 12 Rest fox labels with distinct entities and one nameKey, 1 caption
}
```

  Reference pictures (`tests/reference/reference_tests.cpp`), each a `TEST_CASE` in the existing style:
  - `text_zoom_quarter`, `text_zoom_half`, `text_zoom_one`, `text_zoom_two`: the arena around the tablet and the
    player at 0.25, 0.5, 1 and 2.
  - **Shimmer:** "a label's ink is the same at sub-pixel camera offsets": capture zoom 1 at camera offsets 0, 1/3 and
    2/3 pixel in x and y; the summed ink (1 − luminance) inside the player's label box differs by under 2% between
    any two.
  - **Jitter:** "a label following its creature doesn't step": the player label captured at alpha 0, 0.25, 0.5, 0.75,
    1 between two ticks where the player moves 8 world units in x; the ink centroid's x advances by 2 ± 0.15 pixels
    each step.

- [ ] **Step 2: Run them to see them fail.** `pixi run project_n test --suite gfx_tests`, `game_tests`;
  `pixi run project_n reference` (new pictures missing).
- [ ] **Step 3: Implement.** The renderer's text placement salvaged from the patch (`PlaceTexts`, `UploadGlyphs`,
  `Submitted`, `CreateGlyphPage`, the member regrouping), reworked: world-unit offsets, `PlaceGlyphs`, no rounding,
  the field shader and `TextStyle`, `EndFrame(jobs)` after the copy pass. Every label is drawn at full opacity in this
  task (Task 5 adds the pass). `main.cpp` builds `TextOptions` from the settings. Drop the remaining `shortcut:` tags
  the salvage carried (arena.cpp, main.cpp, text_layout.cpp). Then `pixi run project_n shaders`.
- [ ] **Step 4: Run.** Unit suites pass; `pixi run project_n reference --update`; look at every new and changed picture
  (look-at-the-game skill): edges about a pixel wide at all four zooms, no fringes, outline and shadow visible, Chinese
  strokes clean at 0.25. If 48 base pixels shows blobby strokes at zoom 2 or thin strokes break at 0.25, change
  `SDF_BASE_PIXELS` and note it for the Outcome. `pixi run project_n reference` passes; `lint`.
- [ ] **Step 5: Commit.** `feat(gfx): draw labels and captions as distance fields` (shaders, binaries, PNGs with it).

### Task 5: Stack, prioritise and fade labels

**Files:**
- Create: `src/gfx/labels.{hpp,cpp}`
- Modify: `src/gfx/renderer.cpp`
- Test: `tests/unit/gfx/labels_tests.cpp`, a reference picture

**Interfaces:**
- Consumes: `core::TextItem` (Label kind), `TextPixels`, `LABEL_MIN_PIXELS`.
- Produces (`gfx/labels.hpp`, pure, no SDL):
  - `struct LabelInput { core::StableId entity; u32 nameKey; core::LabelPriority priority; core::Vec2 anchor /* screen px */;
    f32 width; f32 height /* box, screen px */; f32 pixels /* TextPixels */; f32 entityPixels /* height × zoom */; };`
  - `struct PlacedLabel { std::size_t input; core::Vec2 anchor; u32 count /* the others stacked into it */; f32 opacity; };`
  - `inline constexpr f32 LABEL_FADE_SECONDS = 0.15F; inline constexpr f32 LABEL_HYSTERESIS_PIXELS = 4.0F;
    inline constexpr f32 LABEL_GRID_PIXELS = 64.0F;`
  - `class LabelPass { public: std::span<const PlacedLabel> Place(std::span<const LabelInput> labels, f64 clock, f32 scale);
    [[nodiscard]] std::optional<core::Vec2> Anchor(core::StableId entity) const; };`
    `Anchor` gives where an entity's label, or the stack it joined, is placed this frame **even when faded out**
    (Task 6's zoomed-out numbers).
  - `[[nodiscard]] std::string StackText(std::string_view stack, std::string_view name, u32 count);` replaces
    `{name}` and `{count}` in the `LabelStack` string (no `std::vformat`: a bad table can't throw).

The pass, in order (a body only where the tests don't settle it):
1. Sort indices by priority, highest first, then by `entity` value, so the order is stable frame to frame.
2. For each label, test its box against already placed boxes through the grid (cells of `LABEL_GRID_PIXELS`); a box of
   a label shown last frame shrinks by `LABEL_HYSTERESIS_PIXELS` on each side, a hidden one grows by it.
3. No overlap: place it. Overlap with a placed label of the same `nameKey` and neither `Selected` nor `Hovered`: join
   its stack. Otherwise: hidden.
4. Each stack's anchor moves to the member nearest the mean of the members' anchors; its count is members − 1.
5. Target opacity 1 for placed labels whose `pixels ≥ LABEL_MIN_PIXELS × scale`, else 0; each entity's opacity moves
   toward its target by `elapsed / LABEL_FADE_SECONDS`; an entity seen for the first time starts at its target (no
   fade on spawn or on the first frame of a capture).

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("labels apart are each shown") { /* two boxes 100 px apart: two placed, counts 0 */ }
TEST_CASE("overlapping labels of one name stack over the creature nearest the middle") {
    // three Fox boxes at x 100, 110, 120: one placed, count 2, anchor x 110
}
TEST_CASE("overlapping labels of different names give way by priority") {
    // Player box over a Rest box with another name: the Player's placed, the other hidden
}
TEST_CASE("a selected label never joins a stack") {
    // three overlapping Fox boxes, the middle one Selected: it is placed alone with count 0; the other two are
    // hidden (they overlap the selected one) — never counted into it
}
TEST_CASE("a hovered label beats the player's") { /* Hovered vs Player overlapping: Hovered placed */ }
TEST_CASE("a label below the readable size fades out but stays placed") {
    // pixels 5 with scale 1: opacity steps 1 → 0 over 0.15 s of clock; Anchor(entity) still returns its anchor
}
TEST_CASE("a label on the edge of overlapping doesn't flicker") {
    // two boxes overlapping by 2 px, alternated with 2 px apart, frame after frame: the second stays hidden
    // (hysteresis); apart by 10 px it's shown
}
TEST_CASE("a label seen for the first time doesn't fade in") { /* first Place: opacity already 1 */ }
TEST_CASE("the stack label replaces its name and count") {
    CHECK(gfx::StackText("{name} (+{count})", "Fox", 11) == "Fox (+11)");
}
TEST_CASE("a Chinese stack label reads name（+N）") {
    CHECK(gfx::StackText("{name}（+{count}）", "狐", 3) == "狐（+3）");
}
```

  Reference picture `labels_crowd`: the arena at zoom 0.5 with the foxes bunched round the player (a fixed tick where
  they overlap): one "Fox (+N)" stack, the player's label shown.

- [ ] **Step 2: Run them to see them fail.** `pixi run project_n test --suite gfx_tests --test labels`.
- [ ] **Step 3: Implement** `LabelPass` and wire it into the renderer's text placement: labels go through the pass
  (hovered from `view.outlined`, selected from `snapshot.selected`, both through `snapshot.ids`, raise the priority),
  stacks draw `StackText`, opacity multiplies the premultiplied colour; captions bypass the pass.
- [ ] **Step 4: Run.** Unit tests pass; `reference --update`, look at the pictures (the arena pictures change where
  foxes stack), `reference`, `lint`.
- [ ] **Step 5: Commit.** `feat(gfx): stack, prioritise and fade labels`

### Task 6: Damage numbers

**Files:**
- Create: `src/app/damage_numbers.{hpp,cpp}`
- Modify: `src/app/app.{hpp,cpp}` (owns `DamageNumbers`, replaces the empty event player), `src/gfx/renderer.cpp`
  (numbers: size clamp, `lift`, zoomed-out start), `tests/reference/reference_tests.cpp`
- Test: `tests/unit/app/damage_numbers_tests.cpp`, a reference picture

**Interfaces:**
- Consumes: Task 2's `Event`, `HitKind`, `EventKind`; Task 4's `TextItem`, `Renderer::Draw(..., extra)`; Task 5's
  `LabelPass::Anchor`; Task 1's settings.
- Produces (`app/damage_numbers.hpp`):
  - `struct NumberPose { f32 lift; f32 opacity; };`
    `[[nodiscard]] NumberPose NumberMotionAt(core::NumberMotion motion, f32 seconds);` over `NUMBER_SECONDS` 1.0:
    Rise: lift eases out from 0 to 1.5 ems; Bounce: hops to 1.0 em by 0.15 s and settles to 0.6 em by 0.4 s; both
    hold opacity 1 until 0.6 s, then fade linearly to 0 at 1.0 s.
  - `inline constexpr std::array<u32, core::HIT_KINDS> KIND_COLOURS` (premultiplied linear; damage `#FFFFFF`,
    healing `#7CDB6A`, poison `#B07CE8`, burn `#FF9A3C`, each converted to linear in the comment's style of
    arena.cpp).
  - `class DamageNumbers { public: explicit DamageNumbers(const core::Settings& settings);
    void Play(const core::Event& event); void Clear();
    void Texts(f64 renderTick, std::vector<core::TextItem>& out); [[nodiscard]] std::size_t Live() const; };`
    `Play` takes Hits and UseEnded; a Hit starts a live number (`entity` = target, anchor = position, `sign` Plus for
    Healing); with `combinedDamageNumbers`, Hits of one `use`, one target and one kind add into one number (in that kind's
    colour: a real ability deals one kind; the burst mixes them) held at age 0 until that use's `UseEnded`, whose tick
    starts its motion. `Texts` writes one `Number` `TextItem` per live number at
    its pose and forgets those past `NUMBER_SECONDS`. No cap: the vector reserves `NUMBER_RESERVE` 8,192 and grows.
  - App: plays events into `DamageNumbers` (the epoch check is `PlayDue`'s), calls `Clear()` when the drawn
    snapshot's epoch changes, passes `Texts` as `extra` to `Draw`.
  - Renderer, numbers: size `TextPixels(Number, …)`; the anchor rises by `lift × pixels`; when the target's label
    item says `height × zoom < VISIBLE_CREATURE_PIXELS` (8) and `LabelPass::Anchor(entity)` has a place, the number
    starts from there instead of its own anchor.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("every hit of a burst is played") {
    // Play 4,000 Hits at tick 100, then UseEnded: Texts(100.0) writes 4,000 numbers; Live() == 4000
}
TEST_CASE("a number is gone after a second") { /* Hit at tick 0: Texts(59.0) writes one, Texts(61.0) none */ }
TEST_CASE("combined numbers add one use's hits per target") {
    // combined on: Damage Hits 5, 7 on fox A and 3 on fox B in use U, then UseEnded(U): two numbers, values 12 and 3
}
TEST_CASE("combined numbers keep kinds apart") {
    // combined on: Damage 5 and Burn 4 on fox A in use U: two numbers, 5 in the damage colour and 4 in the burn colour
}
TEST_CASE("a combined number waits for its use to end") {
    // combined on: Hit 5 at tick 10, no UseEnded: Texts(40.0) shows it at lift 0, opacity 1; UseEnded at tick 40:
    // Texts(46.0) shows it lifted
}
TEST_CASE("healing is signed and coloured apart from damage") {
    // Healing Hit: sign Plus, colour KIND_COLOURS[Healing]; Damage Hit: sign Plain
}
TEST_CASE("rise lifts and fades a number") {
    // NumberMotionAt(Rise, 0) == {0, 1}; at 0.5: lift in (0, 1.5), opacity 1; at 1.0: opacity 0
}
TEST_CASE("bounce hops, settles and fades") {
    // NumberMotionAt(Bounce, 0.15).lift > NumberMotionAt(Bounce, 0.5).lift > 0; at 1.0 opacity 0
}
TEST_CASE("a new epoch clears the live numbers") { /* 3 live, Clear(): Texts writes none, Live() == 0 */ }
```

  Reference picture `numbers`: a fixed snapshot plus fixed number items, every kind, both motions at ages 0.1 and 0.5,
  at zoom 1 and at `MIN_ZOOM` (minimum size); a third capture with a label item whose `height × zoom` is under 8
  checks the number starts at the label (picture judged by eye in this task, then compared).

- [ ] **Step 2: Run them to see them fail.** `pixi run project_n test --suite app_tests --test damage_numbers`.
- [ ] **Step 3: Implement**, then run the game (`./dev run`), walk to the crystal and press F7 with each setting;
  check by eye that numbers bounce and rise, combine, and stay readable at zoom 0.25.
- [ ] **Step 4: Run.** Unit tests, `reference --update` + look, `reference`, `lint`.
- [ ] **Step 5: Commit.** `feat(app): show damage numbers for every hit`

### Task 7: Measure the burst and record the outcome

**Files:**
- Modify: `src/core/command_line.{hpp,cpp}`, `src/app/app.cpp` (`--burst`), `src/AGENTS.md` (the command and F7),
  `docs/superpowers/specs/2026-11-10-phase6-chunk3-text-design.md` (Status: implemented; **Outcome**),
  `docs/superpowers/specs/2026-11-10-phase6-design.md` (chunk 3's row: done),
  `docs/design/0002.4.2-text-and-mesh-path.md` (Status: superseded by the spec)
- Test: `tests/unit/core/command_line_tests.cpp`

**Interfaces:**
- Produces: `--burst` on the game's command line: App sends `DebugBurst` once a second of render clock, so a profile
  run sees the burst without a key press (`./dev profile --seconds 20 -- --burst`).

- [ ] **Step 1: Write the failing test.** `TEST_CASE("--burst asks for a debug burst every second")`: parsing
  `--burst` sets `Options::burst`; without it, false.
- [ ] **Step 2: Run it to see it fail**, implement, run it to pass.
- [ ] **Step 3: Measure.** `pixi run project_n profile --seconds 20 -- --burst` (pinned to 2 workers as the phase
  bar asks) and without `--burst`; read main-thread frame time (median, 99th percentile, worst) and the text zones
  (`PlaceTexts`, `LabelPass::Place`, `DamageNumbers::Texts`) in `pixi run project_n profiler <file>`; record the
  pre-warm's length from the log.
- [ ] **Step 4: Write the Outcome** in the spec: the measurements and the run that produced them; the values chosen
  for `SDF_BASE_PIXELS`, the sizes, minimums, fade, hysteresis, grid, `MAX_MESH_QUADS`, `NUMBER_RESERVE` and why; every
  ruling from the plan ledger; anything over the phase bar flagged for the user.
- [ ] **Step 5: Commit.** `feat(app): repeat the debug burst for profiling` (code, AGENTS.md) and
  `docs(design): record what chunk 3 built` (the specs and 0002.4.2).
- [ ] **Step 6: Gate.** `pixi run project_n check` passes; then AGENTS.md's Finishing a branch (review, PR).
