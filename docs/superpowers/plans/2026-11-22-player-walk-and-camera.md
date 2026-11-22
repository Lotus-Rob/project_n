# The player's walk and the world camera Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A click route walks the player at held movement's pace by the most direct way, and the world camera follows
the player loosely inside a box half the view's size.

**Architecture:** `core` gains a shortest-distance route search and a leg timed by distance at a speed; `Host::WalkTo`
uses both for the player alone, the leg still `core::WALKING`'s shape so saves, hashes and `RouteFits` hold. The dead
zone lives in `app::CameraControl`'s follow, for `CameraKind::Roam` only.

**Tech Stack:** C++23, XMake through `pixi run project_n`, doctest.

**Spec:** `docs/superpowers/specs/2026-11-22-player-walk-and-camera-design.md`

## Global Constraints

- The travellers are untouched: every NPC journey keeps `core::WALKING`, `core::FindRoute` and `core::MakeTileLeg`.
- The player's route stays a `core::Journey` of one leg with `profile == core::WALKING` and `shape == TileRoute`, so
  `RouteFits` / `LegSound` (src/game/travel.cpp) accept it unchanged; no save version changes.
- Pace: `CreatureData::walkSpeed` world units a tick of distance walked (4.0 shipped), diagonals by their true length.
- Dead zone: half the view's width and half its height, centred, in `CameraKind::Roam` while not freed; combat scenes
  (`CameraKind::Follow`) and `--camera-path` (`FollowPath`) unchanged.
- Simulation code is deterministic (src/AGENTS.md): maths through `core/math.hpp`, no clocks; sized types, `pch.hpp`,
  `///` on every public name, comments say why and never cite the design process.
- Tests: about 20 named samples between the two tasks (tests/AGENTS.md); existing tests whose expected route times
  change are updated to the new pace, never loosened or deleted.
- Each task updates src/AGENTS.md's lines its change makes untrue, in the same commit.
- Gates: edit rounds `pixi run project_n test --suite <s> [--test <t>]`; each task ends with `pixi run project_n test`
  and `pixi run project_n lint --range <task base>..HEAD`.

## Review Focus

1. A click on a tile next to the foot, the foot already near that tile's centre: the leg's first step takes at least
   1 tick (arrive ticks strictly rise), so `LegSound` accepts it. Task 1 tests it.
2. A straight line blocked by deep water or peaks, including a diagonal between two impassable tiles: the shortest
   route goes round and never cuts the corner, as `FindRoute` doesn't. Task 1 tests it.
3. A save taken mid-route and loaded: the paced route round-trips and the cultivator arrives on the same tick. Task 1
   tests it.
4. A zoom out, or a window resize, that leaves the foot outside the new box: the camera moves only by the overshoot,
   never past the foot. Task 2 tests it.
5. A window of 0 × 0 (minimised): no division by zero, the camera centres on the foot. Task 2 tests it.

## Waves

| Wave | Tasks | What each needs |
|---|---|---|
| A | 1 (the player's pace and route, full), 2 (the camera's dead zone, full) | nothing shared: 1 `src/core/paths.*`, `src/core/journey.*`, `src/game/host.cpp`; 2 `src/app/camera_control.*`; both touch different lines of src/AGENTS.md |

---

### Task 1: one pace and the most direct route for the player

**review: full** (simulation, saves, determinism) · **implementer: sonnet**

**Files:**
- Modify: `src/core/paths.hpp`, `src/core/paths.cpp` (the shortest route)
- Modify: `src/core/journey.hpp`, `src/core/journey.cpp` (the paced leg)
- Modify: `src/game/host.cpp` (`Host::WalkTo`), `src/AGENTS.md` (the world scene's WalkTo sentence: the route by the
  shortest way at the cultivator's walk speed)
- Test: `tests/unit/core/paths_tests.cpp`, `tests/unit/core/journey_tests.cpp`, `tests/unit/game/host_tests.cpp`,
  `tests/unit/game/save_tests.cpp`

**Interfaces:**
- Produces: `[[nodiscard]] std::vector<u32> core::FindShortestRoute(const WorldMap& map, u32 from, u32 target);` — the
  shortest route by distance over tiles `core::WALKING` can step onto (each step priced 5 orthogonally and 7
  diagonally: `DIAGONAL_NUMERATOR` / `DIAGONAL_DENOMINATOR`, whatever the kind), A* with `FindRoute`'s tie rule (lower
  tile index), `FindRoute`'s neighbour and corner rules, `{from}` when `from == target`, empty when `target` is
  impassable or unreachable.
- Produces: `[[nodiscard]] Leg core::MakePacedLeg(const WorldMap& map, i64 startTick, Vec2 origin, std::span<const u32> tiles, f32 unitsPerTick);`
  — a `TileRoute` leg with `profile = WALKING`, `arrive[0] = 0`, and `arrive[i]` the ceiling of the walked distance
  (origin to `tiles[1]`'s centre, then centre to centre) up to tile `i`, divided by `unitsPerTick`, summed in `f64`
  from the start (no per-step rounding drift), and at least `arrive[i-1] + 1`. Precondition (`PN_ASSERT`):
  `unitsPerTick > 0`, `tiles` non-empty.

- [ ] **Step 1: Write the failing tests**
  - paths_tests.cpp: `"the shortest route goes straight across open ground past a road"` (the map of `"a route takes
    the road over a shorter line across open ground"`: `FindShortestRoute` returns the straight line that test's
    `FindRoute` avoids); `"the shortest route never cuts a corner past impassable ground"` (that test's map: the route
    goes round, same as `FindRoute`'s); `"the shortest route to an impassable or unreachable tile is empty"`.
  - journey_tests.cpp: `"a paced leg takes the same ticks a tile on every kind"` (meadow, forest, road routes of 3
    orthogonal steps from a centre at 4.0: each arrive is 64, 128, 192); `"a paced leg's diagonal steps take their
    length's time"` (two diagonal steps at 4.0: arrive 0, 91, 182 — ceil(256√2/4) = 91, ceil(2·256√2/4) = 182);
    `"a paced leg's first step from a foot near the next centre takes at least a tick"` (origin 1 unit from
    `tiles[1]`'s centre: `arrive[1] == 1`, `LegSound` true).
  - host_tests.cpp: `"a walk-to crosses meadow at the held walk's pace"` (a walk-to 5 tiles east over meadow ends at
    `startTick + ceil(distance / walkSpeed)`, the same distance held movement covers in that many ticks);
    `"a walk-to goes straight rather than along a road"` (on a meadow map with a road bend, the route's tiles are the
    straight line). Update the expected ticks of existing route tests that relied on `MakeTileLeg` (host_tests.cpp's
    helper at the `MakeTileLeg(map, core::WALKING, ...)` call) to `MakePacedLeg` with the cultivator's `walkSpeed`.
  - save_tests.cpp: `"a paced route saved mid-walk loads and arrives on the same tick"`.
- [ ] **Step 2: Run** `pixi run project_n test --suite core_tests` and `--suite game_tests`: the new cases fail to
  compile or fail (no `FindShortestRoute`, `MakePacedLeg`; WalkTo still by cost). Capture the output for the report.
- [ ] **Step 3: Implement** `FindShortestRoute` (share `FindRoute`'s A* by passing the step price in, rather than a
  copy) and `MakePacedLeg`; in `Host::WalkTo` use `FindShortestRoute(map, map.TileAt(foot), target)` and
  `MakePacedLeg(map, worldTicks, foot, tiles, m_data.cultivator.walkSpeed)`; a cultivator whose `walkSpeed` is 0 or
  less ignores the walk-to, logged at debug like the other refusals. Update `RouteFits`' doc comment if its wording
  names the cost timing; update src/AGENTS.md's WalkTo sentence.
- [ ] **Step 4: Run** `pixi run project_n test` (all pass), `pixi run project_n reference` (reference_tests.cpp walks
  the cultivator by WalkTo: a picture that changes because the route now goes straight is looked at by eye and
  updated with `reference --update`, said in the report) and `pixi run project_n lint --range <task base>..HEAD`
  (clean).
- [ ] **Step 5: Commit** `fix(game): walk a clicked route at the held walk's pace` (with the tests and the docs line).

### Task 2: the world camera's dead zone

**review: full** (the camera every frame) · **implementer: sonnet**

**Files:**
- Modify: `src/app/camera_control.hpp`, `src/app/camera_control.cpp` (`CameraControl::Update`'s follow)
- Modify: `src/AGENTS.md` (Commands: on `world` the camera follows the cultivator loosely, inside a box half the
  view's size; C and the rest snap it back to centre)
- Test: `tests/unit/app/camera_control_tests.cpp`

**Interfaces:**
- Produces: `inline constexpr f32 app::FOLLOW_BOX_SHARE = 0.5F;` (camera_control.hpp, documented: the share of the
  view's width and height the followed foot walks in before a Roam camera moves) and
  `[[nodiscard]] core::Vec2 app::FollowedWithin(core::Vec2 centre, core::Vec2 followed, core::Vec2 halfBox);` — the
  centre moved by the least amount that puts `followed` within `halfBox` of it on each axis (a `halfBox` of 0 on an
  axis gives `followed`'s coordinate).
- Consumes: `gfx::PixelsPerUnit(camera, width, height)` (src/gfx/camera.hpp) for the box in world units:
  `halfBox = (width, height) × FOLLOW_BOX_SHARE / 2 / PixelsPerUnit`.

- [ ] **Step 1: Write the failing tests** (camera_control_tests.cpp)
  - `"FollowedWithin moves the centre only by the foot's overshoot"`: centre (0,0), halfBox (100,50): followed (60,20)
    → (0,0); (130,0) → (30,0); (0,-80) → (0,-30); (-150,70) → (-50,20); halfBox (0,0) → followed.
  - `"a roaming camera holds still while the cultivator walks inside the box"`: Roam, 1600 × 900 at zoom 1, foot moved
    300 units right of the start: centre unchanged.
  - `"a roaming camera moves by the overshoot when the cultivator leaves the box"`: foot moved 500 right: centre moved
    by 500 − 400 × (units per pixel at zoom 1) on x only; same on the left, top and bottom.
  - `"zooming out keeps the foot inside the smaller box without jumping past it"`: foot at the box's edge, zoom
    notches out: the foot stays within the new box and the centre moves no further than the foot.
  - `"a snap back and a scene change put the cultivator at the centre"`.
  - `"a minimised window centres the camera on the cultivator"`: width and height 0: centre == followed, no NaN.
  - `"a combat scene's camera still centres on the cultivator every frame"`: `CameraKind::Follow`, foot moved 300:
    centre == followed.
- [ ] **Step 2: Run** `pixi run project_n test --suite app_tests --test camera_control`: the new cases fail. Capture it.
- [ ] **Step 3: Implement** `FollowedWithin` and use it in `Update`'s follow branch for `CameraKind::Roam` only, after
  the zoom has moved this frame so the box is the frame's; `Follow` and `FollowPath` keep setting the centre to the
  foot. Update src/AGENTS.md.
- [ ] **Step 4: Run** `pixi run project_n test` (all pass; `--camera-path`'s tests in camera_path_tests.cpp unchanged),
  `pixi run project_n reference` (no picture changes: every case starts centred) and
  `pixi run project_n lint --range <task base>..HEAD`.
- [ ] **Step 5: Commit** `feat(app): follow the cultivator loosely on the world map`.
