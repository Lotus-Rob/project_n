# The Travellers' Walk Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The world map's travellers and the player's cultivator, in every scene, walk with drawn step frames. Frames
are picked by distance walked and mirrored to face the way the figure moves. The crowd's names fade off the
cultivator's body.

**Architecture:**
- **core.** It works out walking: `core::WalkedAt` gives a journey's distance walked, whether it moves and its facing at
  a tick, all pure. `core::Animations` holds the walk clips, keyed by standing sprite, and `core::PoseSprite` picks
  a frame.
- **data.** It reads `assets/data/animations.yaml`.
- **app.** It hands the table to the logic thread in `core::ViewReport`, a latest value like the debug sight.
  `game::Host` picks each figure's frame at publish and mirrors it with the existing `SpriteDraw::widthScale = -1`.
  The cultivator's distance and facing are presentation-only host state.
- **gfx.** The label pass keeps `Named` labels off the cultivator's on-screen box.

**Tech Stack:** C++23, XMake through `pixi run project_n`, doctest; Python 3 (Pixi's) for `art/placeholders.py`.

**Spec:** `docs/superpowers/specs/2026-11-23-phase6-chunk5b-look-travellers-design.md`

## Rulings made while planning (for the user to check)

1. **Mirroring uses the existing path, not a new flag** (this replaces spec Decision 2).
   - `core::MakeInstance` with `widthScale = -1` already places the quad about the mirrored foot, at
     `(footX − width/2) × scale` from the foot.
   - The sprite shader then draws it flipped, because the negative width reverses the quad while the UVs stay put.
   - The map's stamps (`gfx/stamp_draw.cpp`) and clouds (`gfx/cloud_draw.cpp`) already draw this way.
   - So there is no `INSTANCE_MIRROR`, no shader change, and the snapshot gains nothing.
   - **Bug this exposes:** `app::PickInstance` (`app/view.cpp`) compares against the signed half-width. A mirrored
     figure could never be clicked, so Task 3 fixes that test-first.
2. **The table reaches the logic thread in `core::ViewReport::animations`.** This is the "how the host keeps the
   clips" left to the builder.
   - It is a `std::shared_ptr<const core::Animations>`, sent with every view as `debugSight` is.
   - The logic calls `Simulation::SetAnimations` before each `Publish`.
   - It does not go in `GameData`, which is hashed and recorded by replays.
3. **No clip, no mirror.** A figure whose standing sprite has no clip draws as today, unmirrored. That includes every
   figure when no table has arrived, as in the host tests that set none.
4. **Reference pictures are refreshed once, in Task 8.** They are checked only by `check` at the branch head. Parallel
   tasks therefore never collide on PNGs, and every changed picture is viewed together.
5. **The body box comes from the snapshot.**
   - The renderer finds the entity of the `LabelPriority::Player` label.
   - It takes that entity's first instance in `Snapshot::ids` and passes its on-screen box to `LabelPass::Place`.
   - The host publishes nothing new.

## Global Constraints

- **Determinism.** Nothing here is saved, hashed or read by the simulation: not the walk clips, nor the cultivator's
  distance and facing, nor any traveller's frame.
  - `StateHash`, `WorldHash`, saves, replays and the travel journals are unchanged.
  - A traveller's frame and facing are a pure function of its journey and the world clock, so they match on 1 and 4
    workers and after a save and load.
- **Sprites.** Each figure has a standing pose and 8 walk frames, side-on and facing right, all on the same canvas and
  foot point as today's sprite:
  - `villager`: 64×116, foot (32, 112), frames `villager_walk_0` … `villager_walk_7`;
  - `cultivator`: 72×128, foot (36, 122), frames `cultivator_walk_0` … `cultivator_walk_7`.
- **Strides** (world units per frame; a 4-frame step is 4 × stride): villager 12, cultivator 14. The placeholder
  frames are painted to match them (Task 1).
- **Frame rule.** Frame `i` shows while `fmod(walked, frames × stride)` lies in `[i × stride, (i + 1) × stride)`. A
  figure that isn't moving draws its standing sprite.
- **Table limits.** `core::MAX_WALK_FRAMES = 32`. A stride must be finite and above 0.
- **Pause.** Travellers' frames follow the world clock and the cultivator's its body's movement, so a paused world
  draws unchanged frames.
- **Shadows are never mirrored.**
- Commits: `feat(<scope>)` for game code, `build(tools)` for placeholder art, `docs(agents)` for AGENTS.md-only
  edits. No hashes in docs.

## Review Focus

1. **Clicking a left-facing traveller selects it.** Today's hit test misses every mirrored instance (Ruling 1).
   Task 3 tests a mirrored instance picked at its drawn place, and a point on its unmirrored side missed.
2. **Hovering or selecting a traveller over the cultivator keeps its label.** Only `Named` gives way, and hover
   raises the priority to `Hovered`. Task 4 tests a hovered label over the body staying.
3. **A cultivator dashing in the arena never slides backwards.** It turns to the dash, and its frame holds rather than
   advancing about two frames a tick. Task 7 tests a dash to the right from a left-facing cultivator.
4. **A traveller standing at a city between days.** A stay's first and last point is the tile centre, so its frame
   doesn't flicker between standing and walking on a zero-length segment. Task 2 tests that a stay's zero-length day
   reports not moving.
5. **A hot reload that breaks `animations.yaml` keeps the old walks**, and one that removes an entry makes that figure
   stand. Task 5 tests the keep. Task 6 tests that swapping or emptying the table changes the next publish.

## Waves

| Wave | Tasks | Needs |
|---|---|---|
| A | 1 art, 2 core walk maths, 3 mirrored picking, 4 labels keep off the body | none; no shared file |
| B | 5 the table and its way to the logic, 6 travellers walk | 5 needs 1 (frames in the manifest) and 2 (types); 6 needs 2, and 1's frames for its tests |
| C | 7 the cultivator walks | 6 (both edit `game/host.*`) |
| D | 8 reference pictures, eye checks, measure | all |

AGENTS.md edits are owned so that parallel tasks never share the file:
- Task 1: `art/AGENTS.md`.
- Task 5: `assets/AGENTS.md`, and `src/AGENTS.md`'s data paragraph.
- Task 7: `src/AGENTS.md`'s publish text, for both travellers and the cultivator.
- Task 8: `tests/AGENTS.md`.

---

### Task 1: side-on placeholders with walk frames

`implementer: sonnet` · `review: light`

**Files:**
- Modify: `art/placeholders.py` (`villager()`, `cultivator()`, new `villager_walk(frame)`, `cultivator_walk(frame)`,
  `PLACEHOLDERS`), `art/sprites.toml` (16 entries after their standing sprite, same foot), `art/scenes.toml` (the
  frames in every scene listing `villager` or `cultivator`: arena, stress, world), `art/AGENTS.md`
- Generated (commit with it): `assets/sprites/*.png`, `assets/manifest.yaml`, `src/core/generated/atlas.hpp`,
  `art/atlas.sha256`

**Interfaces:**
- Produces: `core::atlas::SpriteId::VillagerWalk0` … `VillagerWalk7`, `CultivatorWalk0` … `CultivatorWalk7`, resident
  wherever their standing sprite is.

- [ ] **Step 1: Paint the standing poses side-on, facing right.** Keep each figure's colours and canvas, and keep its
  feet on the foot point. The cultivator wears a robe with a vermilion sash and a hair bun; the villager an indigo
  tunic, dark trousers and a straw hat. Seen from the side: one arm and leg in front of the other, and the head in
  profile, its face to the right.
- [ ] **Step 2: Paint 8 walk frames per figure** as a two-step cycle:
  - frames 0 and 4 are the contact poses, with the legs apart;
  - frames 2 and 6 are the passing poses, with the legs together.
  - **No sliding.** The planted foot moves left by exactly the stride per frame (12 texels for the villager, 14 for
    the cultivator), so the feet don't slide when the game moves the figure right by the stride.
  - **Arms and body.** The arms swing opposite the legs, and the body bobs up 1 to 2 texels at the passing poses.
  - **Margins.** Keep the legs within the canvas with the outline's margin. Widen nothing.
- [ ] **Step 3: Generate the atlas.** Run `pixi run project_n atlas --placeholders`. Expected: it writes the PNGs,
  `assets/manifest.yaml`, `src/core/generated/atlas.hpp` and `art/atlas.sha256`.
- [ ] **Step 4: Look at the art.** Use the look-at-the-game skill: `./dev run -- --world`, and the arena.
  - The figures stand side-on and face right.
  - The frames aren't drawn yet: nothing uses them before Task 6.
  - Save the 16 frames side by side as a contact sheet in the scratchpad, and look at it for a smooth cycle.
- [ ] **Step 5: Gates and commit.**
  - Run `pixi run project_n test` and `pixi run project_n lint --range <base>..HEAD`. Do not run `reference`
    (Ruling 4).
  - `art/AGENTS.md`: placeholders.py also draws side-on figures with 8-frame walk cycles, each frame's planted foot
    moving back one stride.
  - Commit: `build(tools): paint the villager and cultivator side-on with walk frames`.

### Task 2: walking maths in core

`implementer: opus` · `review: full`

**Files:**
- Create: `src/core/animations.hpp`, `src/core/animations.cpp`
- Modify:
  - `src/core/journey.hpp`, `src/core/journey.cpp` (`Walked`, `WalkedAt`);
  - `src/core/logic.hpp` (`Simulation::SetAnimations`, a default that does nothing, documented like
    `SetDebugSight`).
- Test: `tests/unit/core/journey_tests.cpp`, `tests/unit/core/animations_tests.cpp` (picked up by the suite's glob)

**Interfaces:**
- Produces:
  - `core::MAX_WALK_FRAMES` (`std::size_t`, 32)
  - `struct core::WalkClip { atlas::SpriteId standing; std::vector<atlas::SpriteId> frames; f32 stride = 0.0F; }`
  - `struct core::Animations { std::vector<WalkClip> walks; [[nodiscard]] const WalkClip* WalkOf(atlas::SpriteId standing) const; }`
    (null when none)
  - `[[nodiscard]] atlas::SpriteId core::PoseSprite(const WalkClip* clip, atlas::SpriteId standing, f64 walked, bool moving)`
  - `struct core::Walked { f64 distance = 0.0; bool moving = false; bool facesLeft = false; }`
  - `[[nodiscard]] Walked core::WalkedAt(const WorldMap& map, const Journey& journey, i64 tick)`: member 0's walk.
    The others keep fixed offsets from it, so they share it.
  - `virtual void core::Simulation::SetAnimations(std::shared_ptr<const Animations> animations)`: does nothing by
    default.

- [ ] **Step 1: Write the failing `PoseSprite` tests** in `animations_tests.cpp`. Use an 8-frame clip with stride 12,
  built from any 8 distinct existing sprite IDs:
  - walked 0 → frame 0; 11.99 → 0; 12 → 1; 95.99 → 7; 96 → 0; 1,000,000 → frame `(1,000,000 mod 96) / 12`;
  - `moving` false → the standing sprite;
  - a null clip → the standing sprite.
  - `WalkOf`: finds the entry, and gives null for an unlisted sprite.
- [ ] **Step 2: Write the failing `WalkedAt` tests** in `journey_tests.cpp`, on the meadow test map
  (`tests/support/meadow.hpp`).
  - **An east-then-south leg** `MakeTileLeg` over (0,0) → (1,0) → (1,1):
    - halfway through the first step, `distance` is half a tile, `moving` is true and the figure faces right;
    - halfway through the second, `distance` is 1.5 tiles and it still faces right (a north–south step keeps the
      facing).
  - **A west leg** (1,0) → (0,0) faces left.
  - **Before and after.** At the journey's first tick, `distance` is 0 and `moving` false. At its end tick `moving` is
    true. A tick after the end, `distance` is the whole length and `moving` false.
  - **First movement vertical.** On a leg (1,0) → (1,1) → (0,1), at a tick in the first step the figure faces left: the
    first horizontal movement to come.
  - **Never horizontal.** A leg (0,0) → (0,1) faces right.
  - **A part boundary is continuous.** On a `Visit`, at the out leg's end tick, `distance` equals the length of the out
    leg's segments. One tick into the stay it has grown by that tick's stay drift.
  - **A stay standing on its tile centre is not moving** (Review Focus 4). Use a one-day stay, whose only segment runs
    from the tile centre to the tile centre. At every tick inside it, `moving` is false and `distance` doesn't grow.
  - **Pure.** The same journey and tick give bit-identical results across two calls. This is the paused span: the
    same tick gives the same distance.
- [ ] **Step 3: Run** `pixi run project_n test --suite core_tests --test "*walk*"`. Expected: they fail to compile.
- [ ] **Step 4: Implement.**
  - **`WalkedAt` walks the journey's straight segments in order.**
    - Each leg step runs from `LegPoint(i)` to `LegPoint(i + 1)`, between the ticks `startTick + arrive[i]` and
      `startTick + arrive[i + 1]`.
    - Each stay day runs from `StayPoint(day)` to `StayPoint(day + 1)`, from the day's start to the earlier of the
      next day and the stay's end.
    - `distance` sums each segment's length × `clamp((tick − start) / (end − start), 0, 1)`, in f64.
    - `moving` is whether some segment of nonzero length has `start < tick <= end`.
    - `facesLeft` follows the sign of `dx` of the last segment with `start < tick` and `dx != 0`. With none, it
      follows the first segment with `dx != 0`. With neither, the figure faces right.
    - It stops once a segment starts at or after `tick` and the facing is known.
  - **`PoseSprite` follows the frame rule.** Clamp the index to `frames.size() − 1` against floating-point edges.
- [ ] **Step 5: Run** `pixi run project_n test --suite core_tests`. Expected: pass.
- [ ] **Step 6: Gates and commit.** Run `pixi run project_n test` and the lint range. Commit:
  `feat(core): work out how far a traveller has walked and which way it faces`.

### Task 3: a mirrored figure can be clicked

`implementer: sonnet` · `review: full`

**Files:**
- Modify: `src/app/view.cpp` (`PickInstance`)
- Test: `tests/unit/app/view_tests.cpp`, `tests/unit/core/instances_tests.cpp`

- [ ] **Step 1: Write the failing tests.**
  - `instances_tests.cpp`, "a mirrored sprite stands on its mirrored foot": `MakeInstance` with `widthScale = -1`
    on a sprite whose foot is off-centre gives:
    - the width's half negative and the height unchanged;
    - its centre at `foot + (footX − width/2, height/2 − footY)`, mirrored about the foot (the unmirrored centre's x
      offset negated).
  - `view_tests.cpp`, "picking hits a mirrored instance where it is drawn" (Review Focus 1):
    - a point inside the mirrored quad picks it;
    - a point the unmirrored quad would cover but the mirrored one doesn't misses.
- [ ] **Step 2: Run** `pixi run project_n test --suite app_tests --test view`. Expected: the picking case fails.
- [ ] **Step 3: Fix** `PickInstance`: compare against the absolute half-width.
- [ ] **Step 4: Run** the app and core suites. Expected: pass.
- [ ] **Step 5: Gates and commit.** Commit: `fix(app): pick a mirrored sprite where it is drawn`.

### Task 4: names keep off the cultivator's body

`implementer: sonnet` · `review: full`

**Files:**
- Modify:
  - `src/gfx/labels.hpp`, `src/gfx/labels.cpp`: `LabelPass::Place` gains a last parameter
    `std::optional<core::Rect> keepClear = std::nullopt`, and the class comment says what it does.
  - `src/gfx/text_place.hpp`, `src/gfx/text_place.cpp`: `ScreenBoxOf`.
  - `src/gfx/renderer.cpp`: `PlaceTexts` passes the player's box.
- Test: `tests/unit/gfx/labels_tests.cpp`, `tests/unit/gfx/text_place_tests.cpp` (or wherever `TextAnchor` is
  tested)

**Interfaces:**
- Produces:
  - `std::span<const PlacedLabel> gfx::LabelPass::Place(std::span<const LabelInput>, u32 width, u32 height, f64 clock, f32 scale, std::optional<core::Rect> keepClear = std::nullopt)`
  - `[[nodiscard]] std::optional<core::Rect> gfx::ScreenBoxOf(const core::Snapshot& snapshot, core::StableId entity, const ClipTransform& clip, f32 alpha, u32 width, u32 height)`:
    - the screen-pixel box of the first instance `snapshot.ids` gives `entity`, unrotated;
    - centred at its interpolated centre, its absolute width and its height across;
    - none when it has no instance.

- [ ] **Step 1: Write the failing label tests:**
  - **"a named label over the kept-clear box fades out, and back once clear":** a `Named` label overlapping
    `keepClear`.
    - Its opacity falls to 0 over `LABEL_FADE_SECONDS`.
    - Moved clear by more than `LABEL_HYSTERESIS_PIXELS`, it fades back in.
  - **"a named label at the kept-clear box's edge holds its state":** overlapping by less than the hysteresis.
    - One shown last frame stays shown.
    - One hidden stays hidden.
  - **"only named labels give way to the kept-clear box":** `Selected`, `Hovered` (Review Focus 2), `Player`, `Place`
    and `Rest` labels over it all stay shown.
  - **"a label hidden by the kept-clear box takes no room":** a lower-priority label of another name beside it, which
    only the hidden one's box overlapped, is shown.
  - **`ScreenBoxOf`:**
    - a snapshot with one mirrored and one plain instance gives the expected pixel boxes at alpha 0.5;
    - an entity without an instance gives none.
- [ ] **Step 2: Run** `pixi run project_n test --suite gfx_tests --test labels`. Expected: fail to compile.
- [ ] **Step 3: Implement.**
  - **In `Resolve`, before a `Named` label collects stacks:** if its box overlaps `keepClear`, it is hidden (`NONE`)
    and covers nothing. Test the box grown by `Grow(seen, width, height)`.
  - **In `PlaceTexts`:** take the entity of the first label item with `LabelPriority::Player`. Pass
    `ScreenBoxOf(snapshot, it, textFrame.clip, textFrame.alpha, ...)`, or none without one.
- [ ] **Step 4: Run** `pixi run project_n test --suite gfx_tests`. Expected: pass.
- [ ] **Step 5: Gates and commit.** Commit: `feat(gfx): fade travellers' names off the cultivator's body`.

### Task 5: `animations.yaml` and its way to the logic thread

`implementer: sonnet` · `review: full`

**Files:**
- Create: `src/data/animations.hpp`, `src/data/animations.cpp`, `assets/data/animations.yaml`,
  `tests/fuzz/corpus/yaml/animations` (a seed: the shipped file)
- Modify:
  - `tests/fuzz/yaml.cpp`: the animations reader, as the other tables are.
  - `src/core/frame.hpp`: `ViewReport::animations`, a `std::shared_ptr<const Animations>`, null by default.
  - `src/core/logic.cpp`: `m_simulation->SetAnimations(view ? view->animations : nullptr)` beside `SetDebugSight`.
  - `src/main.cpp`: read it after the seals; a broken one stops the game.
  - `src/app/app.hpp`, `src/app/app.cpp`:
    - `Options::animations`;
    - `KeepAnimations`;
    - the data watch reloads it like the seals, keeping the old one on an error;
    - each `SendView` carries `m_options.animations`.
  - `tests/support/world.hpp`: `test::ShippedAnimations(const core::Manifest&)`.
  - `src/AGENTS.md`: the data paragraph.
  - `assets/AGENTS.md`: the table's entry, presentation and not hashed.
- Test: `tests/unit/data/animations_tests.cpp`, `tests/unit/app/data_watch_tests.cpp`, `tests/unit/core/logic_tests.cpp`

**Interfaces:**
- Consumes: `core::Animations`, `core::WalkClip`, `core::MAX_WALK_FRAMES`, `Simulation::SetAnimations` (Task 2);
  Task 1's sprites.
- Produces:
  - `[[nodiscard]] std::expected<core::Animations, core::Error> data::ParseAnimations(std::string_view yaml, const core::Manifest& manifest)`
  - `[[nodiscard]] std::expected<core::Animations, core::Error> data::LoadAnimations(const std::filesystem::path& folder, const core::Manifest& manifest)`
  - `bool app::KeepAnimations(std::shared_ptr<const core::Animations>& current, std::expected<core::Animations, core::Error> read)`
  - `std::shared_ptr<const core::Animations> test::ShippedAnimations(const core::Manifest& manifest)`
  - YAML: `<standing sprite>: { walk: { frames: [<sprite>, ...], stride: <number> } }`, shipped with the Global
    Constraints' frames and strides, with a header comment naming every key.

- [ ] **Step 1: Write the failing reader tests:**
  - **"the shipped animations read":** `villager` and `cultivator` have 8 frames each, with strides 12 and 14.
  - **"an empty table reads as no walks."**
  - **"the reader refuses a bad table, naming the line":** one case each, and each error names the line.
    - An unknown top-level entry key: `run:`.
    - An unknown `walk:` key.
    - A standing sprite the manifest lacks.
    - A frame the manifest lacks.
    - A frame missing from a scene that holds its standing sprite. Use a test manifest whose scene lists `villager`
      but not `villager_walk_3`.
    - `frames: []`.
    - 33 frames.
    - `stride: 0`, `stride: -1` and `stride: .nan`.
    - A missing `stride`.
- [ ] **Step 2: Write the failing reload and handover tests:**
  - **`KeepAnimations`:** an error keeps the current pointer and returns false. A read table replaces it with a new
    pointer and returns true.
  - **`logic_tests`, "the view's animations reach the simulation before it publishes":** a fake simulation records
    the pointer `SetAnimations` got before `Publish`. A new view's pointer replaces it on the next publish (the
    hot-reload swap).
- [ ] **Step 3: Run** `pixi run project_n test --suite data_tests --test animations`. Expected: fail to compile.
- [ ] **Step 4: Implement** the reader, the shipped file, the plumbing and the fuzz hookup listed under Files. Use
  `internal/yaml.hpp`'s helpers (`ReadSprite`, `RefusalAt`, `TunedNumber`). Unknown keys are refused, not warned:
  the spec's choice for this table.
- [ ] **Step 5: Run** the data, app and core suites, and `pixi run project_n fuzz yaml --time 30`. Expected: pass, no
  crash.
- [ ] **Step 6: Gates and commit.** Update the two AGENTS.md files.
  - `src/AGENTS.md`: what the table holds; read at start after the seals, and a broken one stops the game. Debug
    builds reload it and keep the old one on an error. It reaches the logic thread in `ViewReport::animations` and
    `Simulation::SetAnimations`, and is never hashed, saved or ticked on.
  - Commit: `feat(data): read the walk clips and hand them to the logic thread`.

### Task 6: travellers walk

`implementer: opus` · `review: full`

**Files:**
- Modify:
  - `src/game/host.hpp`, `src/game/host.cpp`:
    - `SetAnimations` override, kept as `m_animations`;
    - `PublishJourneys`: once per journey, `core::WalkedAt(map, journey, now)`; for each member drawn,
      `PoseSprite(m_animations ? m_animations->WalkOf(Villager) : nullptr, Villager, walked.distance, walked.moving)`,
      with `widthScale = -1` when a clip exists and `walked.facesLeft`. The shadow stays unmirrored.
- Test: `tests/unit/game/host_tests.cpp`

**Interfaces:**
- Consumes: `core::WalkedAt`, `core::Walked`, `core::PoseSprite`, `core::Animations::WalkOf` (Task 2);
  `SpriteId::VillagerWalk0..7` (Task 1).
- Produces: `void game::Host::SetAnimations(std::shared_ptr<const core::Animations>) override`.

- [ ] **Step 1: Write the failing tests** on the travel test world (`TravelWorldOf`, `InstanceOf`, `SameInstance`).
  Use a table holding the villager's 8 frames at stride 12.
  - **"a traveller draws the walk frame its distance picks, facing its way":** at a tick on an eastward step, the
    traveller's instance is `MakeInstance` of `PoseSprite(...)`'s frame at its `DrawnWhere` foot.
    - Its `widthScale` is 1 eastward.
    - On a westward journey it is −1, and its shadow is unmirrored.
  - **"a traveller with no clip draws as before":** with no table set, or an empty one, it draws the standing
    `villager`, unmirrored.
  - **"a swapped table changes the next publish":** set a table whose frames are in another order, and the drawn
    sprite follows. Then set an empty table, and the villager stands (Review Focus 5).
  - **"travellers' frames are alike on 1 and 4 workers and after a load":**
    - Publish the shipped world's travellers with debug sight on 1 and `MANY_WORKERS` workers. Every instance is
      equal.
    - Save, load and publish at the same tick: equal again.
  - **"walking changes no hash":** `StateHash` and `WorldHash` are the same with and without a table, before and after
    a publish.
  - **"a paused world draws the same frames":** pause, tick 10 times and publish. The instances equal the ones before
    the pause.
- [ ] **Step 2: Run** `pixi run project_n test --suite game_tests --test host`. Expected: the new cases fail.
- [ ] **Step 3: Implement** as under Files. Look up the clip once per chunk, outside the member loop.
- [ ] **Step 4: Run** `pixi run project_n test --suite game_tests`. Expected: pass, the existing traveller cases
  unchanged (no table, Ruling 3).
- [ ] **Step 5: Gates and commit.** Do not edit `src/AGENTS.md`: Task 7 owns the publish text. Commit:
  `feat(game): walk the travellers with step frames, facing their way`.

### Task 7: the cultivator walks

`implementer: opus` · `review: full`

**Files:**
- Modify:
  - `src/game/host.hpp`, `src/game/host.cpp`:
    - a private `core::Walked m_playerWalk`, which is presentation only;
    - `StepPlayerWalk(bool dashing)`;
    - `Tick` keeps `Combat::Dash`'s result and calls `StepPlayerWalk` after the movement;
    - the entity publish draws the player's `look.sprite` through `PoseSprite`, mirrored by
      `m_playerWalk.facesLeft` when a clip exists;
    - `m_playerWalk` is reset to `{}` wherever `RestartDrawnShift()` runs or `m_drawnShift` is reset (a load, a scene
      build).
  - `src/AGENTS.md`: the publish text for travellers and cultivator.
- Test: `tests/unit/game/host_tests.cpp`

**Interfaces:**
- Consumes: Task 6's `m_animations`; `core::PoseSprite`, `core::Walked`.

- [ ] **Step 1: Write the failing tests.** Use the arena unless a case says the world, with a table holding the
  cultivator's 8 frames at stride 14, `walkSpeed` 4.
  - **"the cultivator's held walk picks frames by distance":** held right for 3 ticks, `walked` is 12 and it draws
    frame 0. After 4 ticks, `walked` is 16 and it draws frame 1. It is unmirrored.
  - **"it turns with held movement and keeps its facing":**
    - Held left for one tick, it is mirrored.
    - Then held up, it is still mirrored.
    - Then released, it draws the standing sprite, still mirrored.
  - **"a cast doesn't turn it":** facing left, a generator cast at a point to its right leaves it mirrored.
  - **"a dash turns it and holds its frame"** (Review Focus 3): facing left mid-walk, a dash to the right.
    - Through every dash tick it is unmirrored.
    - Its sprite is the walk frame it had before the dash.
    - `walked` is unchanged.
  - **"on the world map it walks a route and stands while paused":**
    - A `WalkTo` west: it is mirrored, and its frame advances by the route's distance.
    - Paused: two publishes draw the standing sprite, identically.
  - **"a load starts the walk afresh":** after a save and load, it draws the standing sprite, facing right, until it
    moves.
  - **"the cultivator's walk changes no hash":** two hosts given the same inputs, one with a table and one without,
    have equal `StateHash` after 20 ticks of walking.
- [ ] **Step 2: Run** `pixi run project_n test --suite game_tests --test host`. Expected: the new cases fail.
- [ ] **Step 3: Implement `StepPlayerWalk`.**
  - `step = body.current − body.previous`.
  - `moving` is whether the step's length is above 0.
  - Unless `dashing`, `walked` grows by the step's length.
  - When `step.x != 0`, `facesLeft` is `step.x < 0`.
  - It does nothing without a player.
  - Other entities naming a clip's standing sprite (the `Wander` crowd) draw it as today.
- [ ] **Step 4: Run** `pixi run project_n test --suite game_tests`. Expected: pass.
- [ ] **Step 5: Gates and commit.** Update `src/AGENTS.md`'s publish text: travellers' frames and facing from
  `core::WalkedAt` at the clock, and the cultivator's from presentation-only host state that the dash turns but
  doesn't step. Commit: `feat(game): walk the cultivator with step frames, facing its way`.

### Task 8: pictures, eyes and cost

`implementer: sonnet` · `review: light`

**Files:**
- Modify:
  - `tests/reference/reference_tests.cpp`:
    - every case's host gets `host.SetAnimations(test::ShippedAnimations(manifest))`;
    - a new case `world_walk`.
  - `tests/reference/*.png`
  - `tests/AGENTS.md`: `world_walk` in the world pictures' list.

- [ ] **Step 1: Add `world_walk`.** It is centred on the capital at zoom 1, `asInGame`, `exploreAll` and
  `debugSight`. A `Walk` at tick 1 goes to the tile 4 west of the capital, captured partway, so the cultivator walks
  mirrored. Travellers walk both ways on the roads leaving the capital.
- [ ] **Step 2: Update the references.** Run `pixi run project_n reference --update`.
  - View every changed picture: every one showing a villager or the cultivator changes, to side-on.
  - Check `world_walk` for both facings and for traveller names faded off the cultivator.
- [ ] **Step 3: Judge by eye** with look-at-the-game: `./dev run -- --world --zoom 0.25`, then 0.5 and 1. Walk the
  cultivator along a road and watch the travellers.
  - **Feet don't slide** (spec Decision 7): on the road and off it, at zoom 1.
  - **Steps at 0.25 read as walking, not flicker** (Decision 8).
  - Report what you saw. A slide or flicker goes back to the controller as a ruling, not a fix in this task.
- [ ] **Step 4: Measure** `pixi run project_n measure --runs world`, alone on the machine, against a baseline of
  `main` taken the same way. The publish and frame percentiles must stay within the ~10% run-to-run noise. Record both
  tables in the task report.
- [ ] **Step 5: Gates and commit.** Run `pixi run project_n test`, the lint range and `pixi run project_n reference`.
  Commit: `test(tests): picture the travellers and the cultivator walking`.

After Task 8, finishing the branch moves the ledger's rulings, with Task 8's measurements, into the spec's
**Outcome**, and marks the spec implemented. The controller also prunes the sweep-up backlog's "Walk animation and
facing" item and item (3) of "Travellers on the world map look".
