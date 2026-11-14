# Phase 6 chunk 5a: the measurements, the world scene and the camera, implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `measure` command that records the phase's smoothness and cost numbers, a world scene about 20 screens
across to prove the camera and culling on, a camera that follows, pans freely and snaps back outside combat, and
culling against the window in every scene.

**Architecture:** The main thread sends the logic its view (the window's world rectangle plus a margin, named by its
scene change) through `core::GameHandover` as a latest value, never as tick input; `core::Logic` hands the current
change's view to `Simulation::Publish`, and `game::Host` culls to it. A pure `app::CameraControl` owns the camera's
modes by the scene's `Snapshot::camera`. `world` is a scene class whose tiles are scene content found by grid
position, with a crowd of orbit-driven walkers. `measure` runs the game on scripted paths with the job system pinned,
exports Tracy captures with `tracy-csvexport` and prints the numbers.

**Tech Stack:** C++23, EnTT through `core/ecs.hpp`, SDL3 behind `platform`/`gfx`, doctest, Python 3 for
`tools/project_n.py` (unittest self-tests), Tracy 0.14.1, XMake through `project_n`, all from Pixi.

**Spec:** `docs/superpowers/specs/2026-11-14-phase6-chunk5a-measure-camera-design.md` (read it first: its History,
Goals, Anti-goals and Calls are this plan's ground). The scenes spec's Outcome
(`2026-11-14-phase6-chunk5-scenes-design.md`) describes the layer this builds on.

## Global Constraints

- Other scenes don't change: the arena, `stress`, `painted` and `cells` keep their reference pictures
  (`pixi run project_n reference`) and replay test hashes; combat scenes keep following the cultivator only.
- The view rectangle is never tick input: never recorded in a replay, never in `Logic::Hash()` or a save.
- No save format change: `ARENA_FORMAT`, `ACTIVE_VERSION`, `WORLD_VERSION`, `SAVE_FORMAT` stay; a save already accepts
  any listed scene.
- The world: about 38,400 × 21,600 world units (20 screens at zoom 1); zoom range stays 0.25 to 2 (`gfx::MIN_ZOOM`,
  `gfx::MAX_ZOOM`).
- Every camera action works by mouse, keyboard and gamepad (accessibility first); defaults in `app/bindings.cpp`.
- Pan speed is in screens per second (the same at every zoom); no jitter, popping or shimmer (spec, Anti-goals).
- `measure` pins the job system to 2 workers, is never a gate, and is not part of `check` or CI.
- `game` links only `core`; simulation code stays deterministic (`core/math.hpp`, `core/random.hpp`).
- src/AGENTS.md rules apply (sized types, `pch.hpp`, Doxygen on public declarations, no `switch` over a whole enum,
  `PN_PROFILE_ZONE` on per-frame functions, log outcomes only); tests follow tests/AGENTS.md (~20 named samples).
- Each task updates the AGENTS.md lines its change makes untrue (flags, keys, commands) in the same commit.
- Gates: an edit round runs `pixi run project_n test --suite <s> [--test <t>]`; a task ends with
  `pixi run project_n test` and `pixi run project_n lint --range <task base>..HEAD`, plus `pixi run project_n reference`
  when it touches publishing, the atlas or drawing.

## Review Focus

1. The window resized or toggled fullscreen mid-pan: the next frame's view rectangle follows the new size, so nothing
   at the new edges is missing. Task 2 adds the test.
2. The window hidden or minimised, then restored: the logic keeps culling to the last view it had, and the first frame
   back sends a fresh one. Task 2 adds the test.
3. Focus lost or the window left mid-drag (the button released outside the window): the drag ends; edge pan stops
   while the cursor is outside the window. Task 6 adds the tests.
4. A gamepad's right stick resting inside its dead zone (`app::STICK_DEAD_ZONE`, 0.2): no pan, and the camera stays
   following. Task 3 adds the test.
5. A scene change while the camera is free: the new scene starts from its own view, following again (a `world` → F2 →
   … → `world` round trip included). Task 6 adds the test.

## Rulings made while planning

- **`core::CameraKind::Roam`** is the third kind: follows `Snapshot::follow` until the player pans, then stays free
  until snap back. Added by the controller as a prep commit before wave A (both Task 4 and Task 6 consume it).
- **The view travels as a latest value,** `GameHandover::SendView()` / `ReadView()` beside the held input, not as an
  `Action`: a per-frame value needs no queue, and it can never reach `m_input`, so it can never be recorded.
- **The fallback's numbers come from core:** `REFERENCE_SIZE`, `MIN_ZOOM` and `MAX_ZOOM` move from `gfx/camera.hpp` to
  a new `core/view_limits.hpp` (gfx keeps its names as aliases), so `game` can derive today's `FOLLOW_HALF_VIEW`
  (7,680 × 4,320) as `REFERENCE_SIZE / MIN_ZOOM` without linking gfx.
- **Panning keys:** I, J, K, L and the right stick pan; WASD, the arrows and the left stick stay Move (in the still
  scenes Move still pans, as today). Snap back: C and the right stick's click. Drag: the middle button, and the right
  button outside combat scenes (in combat it casts the spender).
- **World tiles are new seamless placeholders** (the user, 2026-11-14): ten kinds, two variants each, 256 world units
  square (Task 10); `cells` stays the mip test with its bordered cells.
- **Walkers are active-domain entities with `Body`, `Look` (the villager sprite) and `Orbit`,** moved by the host's
  existing orbit system, so they are a pure function of their orbit and the tick and save with existing components.
  Every eighth carries a placeholder name label (`StringId::WalkerName`), published by the world scene for walkers in
  view.
- **Input latency** is measured from a scripted Move press to the first drawn frame whose followed foot moved, logged
  as the cast latency is today.
- **The GPU's share** is the CPU's wait for the next swapchain image while presenting unpaced (`--unpaced`), in its own
  Tracy zone, `WaitForGpu`; Task 9 records in the Outcome why, and its limits.

## Waves

Each task's implementer works in its own worktree branched from the feature branch's head, rebased onto it as its
review passes (AGENTS.md: Working with superpowers). One task is dispatched per session (AGENTS.md).

| Wave | Tasks | What each needs |
|---|---|---|
| prep | `CameraKind::Roam` (controller, before wave A) | nothing |
| A | 1 (measuring flags, full), 2 (culling against the window, full), 3 (camera inputs, full), 10 (terrain tiles, light) | no shared file (1: `main.cpp`, `app.hpp`'s `Options`, the renderer, pixi; 2: core, `host.cpp`, `app.cpp`; 3: platform, `bindings.*`, settings; 10: `art/`, `assets/sprites/`, the atlas outputs, drifted reference pictures) |
| B | 4 (the world scene, full), 6 (camera control, full) | 4: Task 2's `Publish` view and Task 10's sprites; 6: Tasks 2 and 3. 4 is `src/game`, art and assets; 6 is `src/app` |
| C | 5 (world pictures and shimmer, full), 7 (scripted path and latency, full) | 5: Task 4; 7: Tasks 1 and 6 |
| D | 8 (`measure`, full) | Tasks 1, 5 and 7 |
| E | 9 (the first measurement and budgets, light) | everything; runs alone (profiling), when the user says the display is free |

---

### Prep: the roaming camera kind (controller)

- [ ] Add `Roam` to `core::CameraKind` in `src/core/frame.hpp`, documented: "It follows what the snapshot follows
  until the player pans it, then stays where the player leaves it until snap back." No scene publishes it yet.
- [ ] `pixi run project_n test --suite core_tests`, then commit `refactor(core): add the roaming camera kind`.

### Task 1: measuring flags

**review: full** (tooling that gives a verdict)

**Files:**
- Modify: `src/main.cpp`, `src/app/app.hpp` (`Options`, `m_jobs`), `src/app/app.cpp` (`WorkersOf`), `src/gfx/renderer.hpp`,
  `src/gfx/renderer.cpp` (present mode), `pixi.toml`, `pixi.lock`, `src/AGENTS.md` (Commands), `tools/AGENTS.md` if it
  lists the profiler packages
- Test: `tests/unit/app/command_line_tests.cpp`, a gfx test for the present-mode order

**Interfaces:**
- Produces: game flags `--workers N` (1 to 64; `Options::workers`, 0 = `core::Jobs::DefaultWorkers()`), `--unpaced`
  (`Options::unpaced`: prefer immediate presentation, then mailbox) and `--fullscreen` (this run fullscreen at the
  display's native size, whatever `settings.yaml` says); an info log line at start naming the display mode
  (`display: <w>x<h> at <hz> Hz`) for `measure` to read; `tracy-csvexport` in Pixi's `profiler` environment.

- [ ] **Step 1: Write the failing tests** in `tests/unit/app/command_line_tests.cpp`, beside `ZoomOf`'s:
  `TEST_CASE("--workers takes a count from 1 to 64")` (`{"game", "--workers", "2"}` → 2; absent → 0) and
  `TEST_CASE("--workers refuses a missing, non-numeric or out-of-range count")` (`"0"`, `"65"`, `"two"`, missing → an
  error starting `"--workers: "`). For the present mode, a pure `gfx::PresentModeOrder(bool unpaced)` returning
  `std::span<const gfx::PresentMode>` (our own enum: `Immediate`, `Mailbox`, `Vsync`):
  `TEST_CASE("unpaced presentation prefers immediate, then mailbox")` and
  `TEST_CASE("by default presentation prefers mailbox, then immediate")`.
- [ ] **Step 2: Run** `pixi run project_n test --suite app_tests --test command_line` and
  `pixi run project_n test --suite gfx_tests`: FAIL (undeclared).
- [ ] **Step 3: Implement** `std::expected<u32, core::Error> app::WorkersOf(std::span<const char* const> args)` (as
  `ZoomOf`), `Options::workers` and `Options::unpaced`, `App`'s `m_jobs` built with `options.workers` when not 0, and
  the renderer's mode choice walking `PresentModeOrder(unpaced)` (the renderer constructor takes the flag; the log line
  says which mode was chosen, as today). `main.cpp` reads the three flags (`--fullscreen` overrides the settings'
  window before it opens), and `platform::Window` logs the display mode it opened on.
- [ ] **Step 4: Add** `tracy-profiler-csvexport = "==0.14.1"` to `[feature.profiler.dependencies]` in `pixi.toml` and
  update `pixi.lock` (`pixi install`); check `pixi run -e profiler tracy-csvexport --help` runs.
- [ ] **Step 5: Run** the suites again (PASS), then `pixi run project_n run -- --workers 2 --unpaced --quit-after 3` and
  check the log names 2 workers and immediate presentation.
- [ ] **Step 6: Commit** `feat(app): pin the job system and present unpaced on request` (code, docs) and
  `build(env): pin tracy-csvexport for the measurements` (pixi files). The PR's container jobs are this change's
  container gate.

### Task 2: culling against the window

**review: full** (publishing, replays, determinism)

**Files:**
- Create: `src/core/view_limits.hpp`
- Modify: `src/gfx/camera.hpp` (aliases), `src/core/frame.hpp` (`ViewReport`), `src/core/handover.hpp`,
  `src/core/logic.hpp`, `src/core/logic.cpp`, `src/game/host.hpp`, `src/game/host.cpp`, `src/app/view.hpp`,
  `src/app/view.cpp`, `src/app/app.cpp` (send the view each drawn frame; no new member), every `Simulation` fake in
  the tests, `docs/design/0002.3-input-data-scenes.md` (§11's note closed), `src/AGENTS.md`
- Test: `tests/unit/core/logic_tests.cpp`, `tests/unit/core/handover_tests.cpp`, `tests/unit/game/host_tests.cpp`,
  `tests/unit/app/view_tests.cpp`

**Interfaces:**
- Produces:
  - `core::REFERENCE_SIZE`, `core::MIN_ZOOM`, `core::MAX_ZOOM`, `core::WIDEST_HALF_VIEW` (`REFERENCE_SIZE / MIN_ZOOM`,
    static-asserted to be {7,680, 4,320}) in `core/view_limits.hpp`; `gfx::` keeps its names.
  - `struct core::ViewReport { core::Rect area; u64 change = 0; }` (the change it was taken in).
  - `void GameHandover::SendView(const ViewReport&)` (main thread) and `std::optional<ViewReport> ReadView() const`
    (logic thread; none before the first; reading doesn't consume).
  - `virtual void Simulation::Publish(Snapshot&, Jobs&, const std::optional<Rect>& view) = 0;`: the current change's
    view, none until one arrives.
  - `[[nodiscard]] core::Rect app::CullArea(const gfx::Camera&, i32 width, i32 height)`: the window's world rectangle
    grown by `app::CULL_MARGIN` (a fraction of the view's size per side, the builder's value, documented with the
    reasoning below).

- [ ] **Step 1: Write the failing tests.**
  - logic: `TEST_CASE("the logic publishes with the current change's view")` (a `ViewReport` with `change` 1 sent →
    the fake's `Publish` receives its area); `TEST_CASE("a view taken in an earlier change is ignored after the swap")`
    (send a view for change 1, F2, settle → `Publish` receives none until a change-2 view arrives);
    `TEST_CASE("the view is never recorded and never hashed")` (record with views sent each burst and without, the
    replay bytes and `Logic::Hash()` equal); `TEST_CASE("the logic keeps the last view while none is sent")` (Review
    Focus 2: no view for 10 bursts → the last one still reaches `Publish`).
  - handover: `TEST_CASE("ReadView gives the latest view, none before the first")`.
  - host: `TEST_CASE("a publish with a view holds only what lies within it")` (arena, a 400 × 300 view round the
    cultivator: every instance's foot within the view; a creature outside it absent);
    `TEST_CASE("with no view, a publish culls to the widest view round the followed cultivator")` (instances inside
    `WIDEST_HALF_VIEW` round the cultivator kept and one just outside it absent, as `FOLLOW_HALF_VIEW` did; the
    unchanged reference pictures in Step 4 prove the rest);
    `TEST_CASE("with no view and no player, a publish culls to the widest view round the scene's view centre")`.
  - app: `TEST_CASE("the cull area holds the window's view and its margin")` at zooms 0.25, 1 and 2 in 1280 × 720;
    `TEST_CASE("the cull area follows the window's size")` (Review Focus 1: 1280 × 720 → 2560 × 1080 widens it).
- [ ] **Step 2: Run** `pixi run project_n test --suite core_tests`, `--suite game_tests`, `--suite app_tests`: FAIL.
- [ ] **Step 3: Implement.** The logic reads `ReadView()` in `RunBurst` before `Publish` and passes the area only when
  `change == m_change.change`. The host's `Publish` culls to the view when given, else to `WIDEST_HALF_VIEW` round the
  followed foot or round the centre of `SceneTraits::view`; `FOLLOW_HALF_VIEW` and its comment go. App calls
  `SendView({ CullArea(m_camera, width, height), drawn->sceneChange.change })` each frame it draws, after the camera
  is placed. `CULL_MARGIN`: wide enough that a wheel notch out (`gfx::ZoomedBy` by -1) and a pan at full speed for
  `app::FRAME_STEP_LIMIT_NS` stay inside; a test asserts both.
- [ ] **Step 4: Run** the three suites (PASS), `pixi run project_n test`, and `pixi run project_n reference` (every
  picture unchanged: the harness sends no view, and the fallback covers what `FOLLOW_HALF_VIEW` did).
- [ ] **Step 5: Docs:** 0002.3 §11's culling note marked done (as built: 5a), src/AGENTS.md's description of the
  loading protocol gains the view; the controller removes the culling item from the sweep-up backlog memory.
- [ ] **Step 6: Commit** `refactor(core): move the view limits into core`, then
  `feat(core): cull each publish to the window's view`.

### Task 3: camera inputs

**review: full** (input on every device)

**Files:**
- Modify: `src/platform/window.hpp`, `src/platform/window.cpp` (new `InputCode`s from SDL), `src/app/bindings.hpp`,
  `src/app/bindings.cpp`, `src/core/settings.hpp`, `src/data/settings.cpp`, `tests/fuzz/corpus/yaml/` settings seed,
  `src/AGENTS.md` (keys)
- Test: `tests/unit/app/bindings_tests.cpp`, `tests/unit/data/settings_tests.cpp`, `tests/unit/platform/window_tests.cpp`
  if the SDL mapping is reachable there (else the mapping moves into a pure function first)

**Interfaces:**
- Produces:
  - `platform::InputCode` gains `MouseMiddle`, `PadRightX`, `PadRightY` (`value` -1 to 1 like the left stick),
    `PadRightStick` (its click), `KeyI`, `KeyJ`, `KeyK`, `KeyL`, `KeyC`.
  - `app::Control` gains `PanCamera` (held axis pair, as Move), `DragCamera` (a button) and `SnapBack` (a button).
  - `core::Vec2 Bindings::Pan() const`: the held pan, as `Move()` (keys add up, the right stick past
    `STICK_DEAD_ZONE`, length at most 1).
  - `bool core::Settings::edgePan = true;`, read from `settings.yaml`'s `edge_pan`.

- [ ] **Step 1: Write the failing tests.**
  - `TEST_CASE("I, J, K and L pan the camera, and the right stick past its dead zone")`;
  - `TEST_CASE("the right stick inside its dead zone doesn't pan")` (Review Focus 4: 0.15 on both axes → `Pan()` zero);
  - `TEST_CASE("the middle button drags the camera, C and the right stick's click snap it back")`;
  - `TEST_CASE("WASD and the arrows still move")` (unchanged `Move()`);
  - settings: `TEST_CASE("edge_pan reads true and false, and defaults to true")`, and a non-boolean warns and keeps the
    default, as the other booleans.
- [ ] **Step 2: Run** `pixi run project_n test --suite app_tests --test bindings` and `--suite data_tests`: FAIL.
- [ ] **Step 3: Implement** the codes (SDL's middle button, right stick axes and click, the five keys), the controls
  and default bindings, `Pan()`, and `edge_pan` in the settings reader (and the shipped settings fuzz seed).
- [ ] **Step 4: Run** the suites (PASS) and `pixi run project_n test`.
- [ ] **Step 5: Commit** `feat(app): bind keys, the right stick and the middle button to the camera`.

### Task 4: the world scene

**review: full** (simulation, publishing, saves)

**Files:**
- Create: `src/game/scenes/world.hpp`, `src/game/scenes/world.cpp`
- Modify: `src/game/scene.hpp`, `src/game/scene.cpp` (hooks), `src/game/host.cpp` (calls them), `src/game/scenes.cpp`
  (`SCENE_LIST` after `CellsScene`), `src/game/scenes.hpp` (`WORLD_SCENE`), every scene's `Publish` override,
  `art/scenes.toml` (Task 10's `[world]` gains `shadow`, `cultivator` and `villager`), `assets/manifest.yaml`, `src/core/generated/atlas.hpp`, `art/atlas.sha256`
  (`pixi run project_n atlas`), `assets/data/strings.yaml` and `src/core/strings.hpp` (`walker_name`, both languages),
  `src/AGENTS.md` (`--world`, F2's list)
- Test: `tests/unit/game/scenes_tests.cpp`, `tests/unit/game/host_tests.cpp`, `tests/unit/game/save_tests.cpp`

**Interfaces:**
- Consumes: `core::CameraKind::Roam` (prep); `Simulation::Publish`'s view (Task 2); the `terrain_<kind>_<variant>`
  sprites and `[world]` in `art/scenes.toml` (Task 10).
- Produces:
  - `virtual void Scene::PublishGround(const SceneContext&, core::Rect view, core::DrawList&, u32 part) const;`
    (ground-layer content added after `DrawList::Begin`; nothing by default) and `Scene::Publish` gains
    `core::Rect view` (the area the host culled to).
  - `game::WORLD_SCENE`, `game::WorldScene` with traits `{ .name = "world", .view = {{0, 0}, {38'400, 21'600}},
    .home = the map's centre, .camera = core::CameraKind::Roam }`.
  - `struct TileSpan { u32 firstColumn, lastColumn, firstRow, lastRow; }` and
    `[[nodiscard]] TileSpan TilesIn(core::Rect view)` in `world.hpp`: the tiles overlapping a view, clamped to the map.
  - `StringId::WalkerName` ("Wanderer"; the second language's word in strings.yaml).

- [ ] **Step 1: Write the failing tests.**
  - `TEST_CASE("TilesIn finds the tiles a view overlaps, clamped to the map")`: a view inside one tile → one tile; a
    view straddling four → 2 × 2; a view past the map's corner → clamped; the whole map → 150 × 85 (256-unit tiles).
  - `TEST_CASE("a world publish holds only the tiles in view")` (a 1,920 × 1,080 view → the ground batch's instance
    count equals `TilesIn`'s area).
  - `TEST_CASE("a walker's foot is the same for the same tick, built twice")` (two hosts in `world`, tick 1 and 500:
    every walker's `Body` equal).
  - `TEST_CASE("the world's busiest region holds thousands of walkers at the widest view")` (the densest 7,680 × 4,320
    window: at least 2,000).
  - `TEST_CASE("a save made in world loads back into it")`.
  - `TEST_CASE("every terrain kind appears somewhere on the map, and a tile's kind is the same asked twice")`.
  - Re-point `"F2's CycleScene visits every scene in order and back"` and `"each scene is found by its name"` to include
    `world` after `cells`; `CheckScenes` covers it.
- [ ] **Step 2: Run** `pixi run project_n test --suite game_tests`: FAIL.
- [ ] **Step 3: Implement** the hooks (the host calls `PublishGround` right after `Begin`, part 0, and passes its cull
  area to `Publish`), `WorldScene` (tiles: each tile's kind from a fixed low-frequency value noise over the map,
  thresholded so the kinds form regions (deep water, shallows and sand at the low end; meadow, grassland, forest and
  marsh in the middle; hills, rock and snow at the high end), its variant from a hash of its column and row, both pure
  functions of the tile's position, tested in Step 1 for the same answer twice; walkers: spawned by `Build` with `Body`, `Look` (villager) and `Orbit` (centre,
  radius, wobble, speed from `core/random.hpp` keyed for the world scene's purpose), denser round a few gathering points
  so one region is busiest; labels for every eighth walker within `view`), `WORLD_SCENE`, the strings, `art/scenes.toml`
  and `pixi run project_n atlas`.
- [ ] **Step 4: Run** `pixi run project_n test` (PASS) and `pixi run project_n reference` (every existing picture
  unchanged: the atlas gains a scene, not sprites).
- [ ] **Step 5: Commit** `feat(game): add a world scene of tiles and wandering walkers` (code, atlas outputs, strings,
  docs).

### Task 5: world pictures and shimmer

**review: full** (reference verdicts)

**Files:**
- Modify: `tests/reference/reference_tests.cpp`, `tests/reference/*.png` (new), `tests/AGENTS.md`

**Interfaces:**
- Consumes: `game::WORLD_SCENE` (Task 4).
- Produces: reference cases `world_widest` (zoom 0.25 over the busiest region), `world_middle` (zoom 0.6, labels
  showing), `world_close` (zoom 2); `TEST_CASE("the world's tiles and walkers at the smallest zoom: no shimmer")`, which
  prints its per-tile ink change (the metric `measure` reads, one line `world shimmer: <value>`).

- [ ] **Step 1: Write the cases** after the cells cases, built like them (a host in `world`, a fixed camera, no view
  sent); the shimmer case moves the camera a quarter pixel as the seals' case does, over the tiles and the walkers.
- [ ] **Step 2: Run** `pixi run project_n reference --update`, look at the three new pictures (tiles cover the view, no
  seams, walkers and labels where expected) and check every other picture is unchanged.
- [ ] **Step 3: Set the shimmer bound** from three runs with the seals case's margin, commented with the measured value.
- [ ] **Step 4: Run** `pixi run project_n reference` (PASS) and `pixi run project_n test`.
- [ ] **Step 5: Commit** `test(gfx): picture the world scene and check its shimmer`.

### Task 6: camera control

**review: full** (the camera every frame)

**Files:**
- Create: `src/app/camera_control.hpp`, `src/app/camera_control.cpp`
- Modify: `src/app/app.hpp`, `src/app/app.cpp` (`Pan`, `Present`, `HandleInput`), `src/app/view.hpp`,
  `src/app/view.cpp` (`ShowScene` goes), `src/app/xmake` sources if listed, `src/AGENTS.md` (keys, edge pan)
- Test: `tests/unit/app/camera_control_tests.cpp`

**Interfaces:**
- Consumes: `Bindings::Pan()`, `Control::DragCamera`, `Control::SnapBack`, `Settings::edgePan` (Task 3); `CullArea`
  and `SendView` (Task 2); `CameraKind::Roam` (prep).
- Produces:
  - `struct app::CameraInput { core::CameraKind kind; u64 change; std::optional<core::Vec2> followed;
    core::Vec2 move; core::Vec2 pan; std::optional<core::Vec2> cursor; std::optional<core::Vec2> dragFrom;
    bool snapBack; bool focused; bool edgePan; i32 width; i32 height; f32 seconds; }` (`followed` at the render
    alpha; `dragFrom` the pointer where this frame's drag movement started, none when not dragging)
  - `class app::CameraControl { explicit CameraControl(f32 startZoom); void Update(const CameraInput&);
    const gfx::Camera& Camera() const; bool Free() const; void Zoom(f32 notches); }`: Follow kinds centre on
    `followed`; Pan kinds pan with `move` and `pan`; Roam follows until any pan input, then stays free until
    `snapBack`; a new `change` resets the camera, following, at `startZoom` (the run's `--zoom`) on `followed` or, with
    nothing followed, on `gfx::REFERENCE_CENTRE` (what `ShowScene` does today).
  - `app::PAN_SCREENS_PER_SECOND`, `app::EDGE_BAND` (fraction of the shorter side), documented.

- [ ] **Step 1: Write the failing tests** (pure, no device):
  - `TEST_CASE("dragging moves the world with the pointer")` (drag 100 px → the centre moves 100 px' worth of world at
    zooms 0.5 and 2);
  - `TEST_CASE("edge pan starts only inside the edge band and moves the same screens per second at every zoom")`;
  - `TEST_CASE("edge pan stops while the cursor is outside the window, or edge pan is off")` (Review Focus 3);
  - `TEST_CASE("a drag ends when focus is lost")` (Review Focus 3);
  - `TEST_CASE("in a roaming scene, panning frees the camera and snap back follows again")`;
  - `TEST_CASE("walking doesn't re-attach a free camera")`;
  - `TEST_CASE("a following scene ignores pan, drag, edge and snap back")` (combat scenes keep following);
  - `TEST_CASE("a still scene pans with Move and the pan keys")`;
  - `TEST_CASE("each scene change starts from the scene's view, following, between scenes sharing sprites too")`
    (Review Focus 5: free in change 3, change 4 arrives → following, centred on `followed`);
  - `TEST_CASE("zoom stays within 0.25 and 2")`.
- [ ] **Step 2: Run** `pixi run project_n test --suite app_tests --test camera_control`: FAIL.
- [ ] **Step 3: Implement** `CameraControl`, then wire `App`: `HandleInput` feeds drag (the middle button, and the right
  button when `!drawn->combat`), snap back and zoom; each frame `Present` builds `CameraInput` from the drawn snapshot
  (`camera`, `sceneChange.change`, `follow` at the render alpha), the bindings and the window, and draws with
  `Camera()`; `App::Pan` and `ShowScene` go.
- [ ] **Step 4: Run** the suite (PASS), `pixi run project_n test`, `pixi run project_n reference`, and look at the game
  (`pixi run project_n run -- --world`): follow, drag, edge pan, I/J/K/L, snap back, F2 round trip (look-at-the-game
  skill for screenshots).
- [ ] **Step 5: Commit** `feat(app): pan the camera freely outside combat and snap it back`.

### Task 7: scripted path and latency

**review: full** (the measurement's input)

**Files:**
- Modify: `src/app/app.hpp` (`Options::cameraPath`), `src/app/app.cpp`, `src/app/camera_control.*` (a scripted input
  source), `src/main.cpp`, `src/gfx/renderer.cpp` (`WaitForGpu` zone round the swapchain acquire's wait),
  `src/AGENTS.md` (flags)
- Test: `tests/unit/app/camera_control_tests.cpp`

**Interfaces:**
- Consumes: `CameraControl` (Task 6), `--workers`/`--unpaced` (Task 1).
- Produces: `--camera-path`: from 2 s after the scene runs, a fixed script (`app::CAMERA_PATH`, a table of timed
  segments: follow 4 s, pan across 3 screens 4 s, zoom in to 2 then out to 0.25 over 4 s, hold the widest view over the
  busiest region 6 s, hold zoom 0.6 with labels 4 s, then quit) with a scripted Move press at the start of the follow
  segment. Each segment's start logs `camera path: <segment name>` at info and as a Tracy message; the latency logs
  `input latency: <ms> ms from a Move press to its first drawn frame` at info.

- [ ] **Step 1: Write the failing tests:** `TEST_CASE("the camera path gives each segment's camera at its times")`
  (start, middle and end of each segment); `TEST_CASE("the camera path ends with a quit after its last segment")`.
- [ ] **Step 2: Run** `pixi run project_n test --suite app_tests --test camera_control`: FAIL.
- [ ] **Step 3: Implement** the path as an input source to `CameraControl` (it replaces the player's pan inputs while
  set), the scripted Move press and its latency (as `MeasureLatency` does for casts), the segment messages (a
  `PN_PROFILE_MESSAGE` in `core/profile.hpp` if none exists, compiled out outside releasedbg), the `WaitForGpu` zone.
- [ ] **Step 4: Run** the suite (PASS), `pixi run project_n test`, and
  `pixi run project_n profile --seconds 30 -- --world --camera-path --workers 2`: the capture opens in
  `pixi run project_n profiler` with the segment messages and `WaitForGpu`.
- [ ] **Step 5: Commit** `feat(app): drive the camera along a scripted path for measuring`.

### Task 8: `measure`

**review: full** (tooling that gives a verdict)

**Files:**
- Modify: `tools/project_n.py` (`cmd_measure`, its parser, helpers), `tools/test_project_n.py`, `tools/AGENTS.md`,
  root `AGENTS.md` (Commands)
- Test: `tools/test_project_n.py`

**Interfaces:**
- Consumes: `--workers`, `--unpaced`, `tracy-csvexport` (Task 1); `world shimmer:` (Task 5); `--camera-path`, the
  segment messages, `input latency:`, `WaitForGpu` (Task 7).
- Produces: `pixi run project_n measure [--runs world,burst,swords,seals] [--keep]`: builds releasedbg, runs each named
  run fullscreen at native size with `--workers 2 --unpaced` under `tracy-capture` (as `profile`), exports with
  `tracy-csvexport -u`, and prints one table: per run and per camera segment, frame time median/p99/worst (whole frame
  and per thread), hitches (frames whose work, frame time less `WaitForGpu`, is over `HITCH_MS` = 16.7, each with its
  three largest zones), input latency, the GPU's share (`WaitForGpu` / frame time), shimmer (from
  `pixi run project_n reference`'s world shimmer case), the display size and refresh, and chunks 3, 4 and 4b's earlier
  unpinned numbers beside the arena runs (burst 0.43 ms median, 2.6 ms worst; 1,000 swords 1.31/2.31/2.60 ms; swords
  with seals ~685 µs median frame).
  Runs: `world` (`--world --camera-path`), `burst` (`--burst`), `swords` (`--cast-swords`), `seals` (`--cast-swords
  --zoom 2`, the showcase shown).

- [ ] **Step 1: Write the failing self-tests** on small hand-written CSV text: `test_percentiles` (median, p99, worst of
  a known list), `test_hitches_use_work_not_wall_time` (a frame long only by its GPU wait isn't a hitch),
  `test_segments_split_by_messages`, `test_latency_read_from_log`, `test_shimmer_read_from_reference_output`,
  `test_measure_refuses_unknown_runs`.
- [ ] **Step 2: Run** `pixi run project_n selftest`: FAIL.
- [ ] **Step 3: Implement** `cmd_measure` and its pure helpers (parsing, statistics, the table), reusing `profile`'s
  capture code (factor it into a shared function rather than copy it).
- [ ] **Step 4: Run** `pixi run project_n selftest` (PASS), `pixi run project_n doctor --docs`, and one short real run,
  `pixi run project_n measure --runs world`, to see the table print.
- [ ] **Step 5: Commit** `build(tools): add measure, the phase's smoothness and cost numbers`.

### Task 9: the first measurement and budgets

**review: light** (docs and measured values)

**Files:**
- Modify: `docs/superpowers/specs/2026-11-14-phase6-chunk5a-measure-camera-design.md` (Outcome: Measurements)

- [ ] **Step 1: Ask the user** for a time the display is free (each run takes the screen fullscreen), then run
  `pixi run project_n measure` three times on the development machine, nothing else animating (src/AGENTS.md Gotchas).
- [ ] **Step 2: Record** in the spec's Outcome: the machine, display size and refresh, every number of the three runs,
  the noise between them, the earlier unpinned numbers beside the pinned ones, the GPU method and its limits, and the
  first budget numbers with their reasons.
- [ ] **Step 3: Commit** `docs(design): record 5a's first measurements and budgets`.

### Task 10: terrain tiles

**review: light** (generated art and its outputs)

**Files:**
- Modify: `art/placeholders.py` (a seamless terrain painter, registered in `PLACEHOLDERS`), `art/sprites.toml` (each
  tile, `foot = [0, 0]`), `art/scenes.toml` (`[world]`, last, with `ground` and the twenty tiles), `assets/sprites/`
  (the new PNGs), `assets/sprites/PROVENANCE.md` (a row each), `assets/manifest.yaml`, `src/core/generated/atlas.hpp`,
  `art/atlas.sha256`, any reference picture the re-pack drifts
- Test: `tools/test_project_n.py` (beside any placeholder tests; the module imported as the other tools tests do)

**Interfaces:**
- Produces: sprites `terrain_<kind>_<variant>` for kinds `meadow`, `grass`, `forest`, `hills`, `rock`, `snow`, `sand`,
  `marsh`, `shallows`, `water` and variants `0`, `1`: 256 × 256, opaque, `SpriteId::TerrainMeadow0` and so on in the
  generated header; `[world]` in `art/scenes.toml`.

- [ ] **Step 1: Write the failing test** `test_terrain_tiles_are_seamless`: for each kind and variant, the mean
  absolute colour difference across the wrap (column 255 to column 0, row 255 to row 0) is at most 1.5 times the mean
  difference between neighbouring interior columns and rows; and across a variant pair's shared edge the same holds.
- [ ] **Step 2: Run** `pixi run project_n selftest`: FAIL (no painter).
- [ ] **Step 3: Implement** `terrain(kind, variant)`: a kind's base colour and texture (grass strokes, tree dabs, hill
  shading, rock cracks, snow drifts, sand ripples, marsh pools, water ripples) from noise sampled with a period of 256
  so every edge wraps; the variant changes the seed of the detail, never the base, so variants join. Same ink-and-wash
  look as the other placeholders; distinct enough by kind to read at zoom 0.25.
- [ ] **Step 4: Generate** with `pixi run project_n atlas --placeholders` (it redraws every placeholder byte for byte),
  then `pixi run project_n selftest` (PASS) and `pixi run project_n reference`: any picture named "within tolerance but
  changed" is looked at and committed with this change (tests/AGENTS.md Gotchas).
- [ ] **Step 5: Look** at the tiles side by side and tiled 3 × 3 (a throwaway image in the scratch folder) for seams and
  readability at small size.
- [ ] **Step 6: Commit** `build(tools): paint seamless terrain tiles for the world` (the painter, sprites, atlas
  outputs, provenance, refreshed pictures).

