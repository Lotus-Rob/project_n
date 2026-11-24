# River Heads Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every river on the world map rises on high ground, out of a pond, or from another river. Its band narrows to a
point at a high-ground source and fades in out of a pond's water.

**Architecture:**
- **gfx.** The ribbon band learns to shape its start: a taper and a fade-in, beside the end fade it already has.
- **One shared classifier.** `gfx::RiverStarts` sorts each river's start into a branch, a pond or a spring. `DrawRivers`
  and the shipped-data test both call it, so the test checks what is drawn.
- **Data.** `world.yaml` gets upstream points and ponds by hand. `--roads` reroutes over them, and the pinned travel
  numbers follow the user's rule.

**Tech Stack:** C++23, XMake through `pixi run project_n`, doctest; Python 3 (Pixi's) for `art/world_map.py --roads`.

**Spec:** `docs/superpowers/specs/2026-11-24-river-heads-design.md`

## Global Constraints

- **Water kinds.** Water is `shallows` and `deep_water`, as `renderer.cpp` passes them
  (`worldLook.ground.shallowKind`, `deepKind`). Ponds are 2 to 4 `shallows` tiles. No new kind, sprite or format.
- **Source rule (spec goal 1).** A river's start is one of three:
  - **branch:** its first point lies within a tile of another river's line;
  - **pond:** its first point's tile is water, or water lies within three tiles straight behind it, back along its
    first segment;
  - **spring:** anything else.
  On the shipped map, every spring's first tile is `hills`, `rock` or `snow`.
- **Drawing.**
  - **Pond:** the line starts `MOUTH_INTO_TILES` (1) past the water's edge, inside the water, and fades in over that
    stretch. A first point already in water keeps its place and fades in over `MOUTH_INTO_TILES`.
  - **Spring:** both bands narrow from nothing to full width over `taper`.
  - **Branch:** drawn as today.
  - **Roads:** their `taper` and fade-in are 0, so they draw as today.
- **Taper.** It is a look value: `world_look.yaml`'s `river:` and `road:` take an optional `taper` in tiles, 0 to 4,
  0 when absent. The shipped river's is `1.5`; the road's is absent. A band never tapers over more than half its
  length, so a short river never vanishes.
- **Travel numbers (the user's rule, option A).**
  - **Update freely:** pinned costs in `tests/unit/game/shipped_world_tests.cpp` take their new values.
    `MORTAL_TO_OASIS` was 173, `STRONG_TO_OASIS` 152, `nearest` 126, `sectNearest` 162, `farSide` 562. Old and new
    values go in the report for the spec's Outcome.
  - **Move the river first:** a city-to-city cost moving more than about 5% means moving that river's new stretch or
    pond and rerunning.
  - **Stop and report to the controller, never loosen** when:
    - a city or danger site is unreachable;
    - "three links fork around a danger" loses a fork or changes its list of dangers on roads;
    - the traffic bounds fail (2,500 to 3,300 on the roads; at least 2,000 in the busiest view).
- **Code.** Sized types, `pch.hpp`, and `///` on every public name. Comments say why and never cite the design process
  (`lint` checks).
- **Tests.** About 20 samples per test (tests/AGENTS.md). Never delete, skip or loosen a test; values that change with
  the map are updated.
- **Docs.** Each task updates, in the same commit, every line its change makes untrue: `river_draw.hpp`,
  `ribbon_band.hpp`, `world_look.yaml`'s header, and the AGENTS.md files of `assets/` and `art/`.
- **Gates.** Edit rounds run `pixi run project_n test --suite <s> [--test <t>]`. Each task ends with
  `pixi run project_n test` and `pixi run project_n lint --range <task base>..HEAD`. `pixi run project_n reference`
  goes red at Task 2 and is only looked at there. Task 3 updates the pictures.

## Review Focus

1. **A river shorter than twice its taper** (a two-point river one tile long): it still reaches full width at its middle
   and never draws zero-width. Task 1 tests a short band.
2. **A short river that tapers at its start and fades into the sea at its end:** both shapings apply, multiplied, and
   neither undoes the other. Task 1 tests a band with taper and both fades.
3. **A river whose first point is already in a pond:** classed as a pond, not lengthened further into the water. Task 2
   tests it.
4. **A pond start at the map's edge, where looking back runs off the map:** classed as a spring, never read off the
   grid. Task 2 tests it.
5. **Roads unchanged:** a road look without `taper` loads 0, and road ribbons are vertex-for-vertex as before. Task 1
   tests the loader default, and Task 1's band tests use `taper` 0 for the old behaviour.

## Waves

| Wave | Tasks | Needs |
|---|---|---|
| 1 | Task 1: shaping a band's start | — |
| 2 | Task 2: classifying and drawing river starts | Task 1's `Laid` fields and `RibbonShape::taper` |
| 3 | Task 3: the shipped rivers' sources | Task 2's `RiverStarts` |

These run in sequence. Each task needs the one before it, and the tasks are small.

---

### Task 1: shaping a band's start

`review: full` · `implementer: sonnet`

**Files:**
- Modify: `src/gfx/ribbon_band.hpp`, `src/gfx/ribbon_band.cpp` (`Laid`, `AddBand`, `Shares`)
- Modify: `src/gfx/ribbon.hpp` (`RibbonShape`), `src/gfx/ground.cpp` (`RibbonShapeOf`)
- Modify: `src/core/world_look.hpp` (`RibbonLook`), `src/data/world_look.cpp` (the ribbon reader, about line 742)
- Modify: `assets/data/world_look.yaml` (header comment for `river:`, and `taper: 1.5` on the `river:` line)
- Test: `tests/unit/gfx/ribbon_band_tests.cpp` (create it if absent, beside the other gfx unit tests),
  `tests/unit/data/world_look_tests.cpp`

**Interfaces:**
- Produces:
  - `gfx::Laid` gains `f32 fadeIn = 0.0F;`: world units from the start over which the band rises from nothing to its
    colour.
  - `gfx::Laid` gains `f32 taper = 0.0F;`: world units from the start over which both sides widen from 0 to full
    width.
  - `gfx::RibbonShape` gains `f32 taper = 0.0F;` in world units, and `RibbonShapeOf` sets it to
    `look.taper * map.TileSize()`.
  - `core::RibbonLook` gains `f32 taper = 0.0F;` in tiles.

- [ ] **Step 1: Write the failing tests**
  - **`ribbon_band_tests.cpp`**, with a straight line of 9 points 64 units apart along x (512 long), width 20:
    - "a band tapers from nothing at its start": with `taper = 128`, the first point's two corners are within 0.01 of
      each other; at 64 the width is 10 ± 0.01; from 128 on it is 20 ± 0.01.
    - "a band fades in from nothing at its start": with `fadeIn = 128`, corner 0 of quad 0 has colour 0, and the far
      corners of the quad ending at 128 have the full colour.
    - "a short band tapers over at most half its length": a 2-point line 64 long with `taper = 256` has full width
      (20 ± 0.01) at its end point.
    - "start and end shaping multiply": with `taper = 128`, `fadeIn = 128` and `fade = 128`, the middle point's corners
      have the full colour and full width, and both end points have colour 0.
    - "an unshaped band is as before": with every length 0, every corner's colour is the input colour and every width
      is 20.
  - **`world_look_tests.cpp`**:
    - "a ribbon's taper is read in tiles, 0 when absent": `river: { ..., taper: 1.5 }` gives `river.taper == 1.5F`;
      a `road:` without `taper` gives `0.0F`; `taper: 5` and `taper: -1` are refused, with a message naming `taper`.

- [ ] **Step 2: Run to see them fail**

  Run: `pixi run project_n test --suite gfx` and `pixi run project_n test --suite data`
  Expected: compile errors, because `fadeIn`, `taper` and `RibbonLook::taper` don't exist yet.

- [ ] **Step 3: Implement**
  - **`AddBand`** scales each point's side offset by its taper share, from 0 at the start to 1 at the taper length,
    linearly by distance along the line. The taper length is capped at half the line's length.
  - **Colour.** `AddBand` multiplies the colour share by a fade-in share, measured the same way, before `Faded`. Fade
    all corners whenever any of `fade` and `fadeIn` is positive.
  - **Loader.** It reads `taper` as optional on both ribbons: add it to the key list, range 0 to 4 tiles inclusive.
  - **`world_look.yaml`.** Gets `taper: 1.5` on `river:`. The header line for `river:` says what `taper` does.
  - **Docs.** `ribbon_band.hpp`'s `Laid` and `AddBand` docs describe the start shaping.

- [ ] **Step 4: Run the suites to see them pass**

  Run: `pixi run project_n test --suite gfx` and `pixi run project_n test --suite data`
  Expected: all pass, including the existing river and road tests, whose `SHAPE` has `taper` 0.

- [ ] **Step 5: Gate and commit**

  Run `pixi run project_n test` and `pixi run project_n lint --range <task base>..HEAD`.
  Commit: `feat(gfx): taper and fade in a ribbon's start`. The look file and loader go in the same commit, or as a
  `feat(data)` commit before it.

---

### Task 2: classifying and drawing river starts

`review: full` · `implementer: sonnet`

**Files:**
- Modify: `src/gfx/river_draw.hpp`, `src/gfx/river_draw.cpp`
- Test: `tests/unit/gfx/river_draw_tests.cpp`

**Interfaces:**
- Consumes: Task 1's `Laid::fadeIn`, `Laid::taper` and `RibbonShape::taper`.
- Produces (in `river_draw.hpp`):
  ```cpp
  /// How a river begins: on another river, out of water, or at a spring.
  enum class RiverStart : u8 { Branch, Pond, Spring };
  /// How each of `map`'s rivers begins, in the map's order, with `water` the kinds that count as water.
  /// A river of fewer than two distinct points is a Spring.
  [[nodiscard]] std::vector<RiverStart> RiverStarts(const core::WorldMap& map, std::span<const u8> water);
  ```

- [ ] **Step 1: Write the failing tests** (`river_draw_tests.cpp`, with `SHAPE` given `.taper = TILE` for the new cases)
  - "a river starting on open ground tapers in": a meadow river from (1,2) to (6,2) gives `RiverStarts == {Spring}`,
    and its first core quad's near corners are within 0.01 of each other.
  - "a river starting by a lake starts inside it, fading in":
    - The map is the "ends short of water" lake, mirrored: lake columns 0 and 1, and a river from (3,1) to (6,1).
    - `RiverStarts == {Pond}`.
    - The first midline point's x is `0.75F * TILE` (Approx), a tile past the first wet sample at x = 1.75 tiles.
      Looking back from 3.5 in quarter tiles, the sample at x = 2.0 still lies on column 2.
    - The first core corner's colour is 0.
  - "a river starting in a lake keeps its start": the river's first point is on a lake tile, so `{Pond}`, and the first
    midline point is that tile's centre.
  - "a branch's start is drawn as it was": a main river (1,4)→(9,4) and a branch (5,4)→(5,1) give
    `{Spring, Branch}`, and the branch's first corners are `CORE` apart (± 0.01).
  - "a river at the map's west edge is a spring": a river from (0,1) to (4,1), with no lake tiles and `LAKE_WATER`
    passed, gives `{Spring}`. Looking back runs off the map after half a tile.
  - "the shipped rivers fit the river buffer" stays as it is and must still pass.

- [ ] **Step 2: Run to see them fail**

  Run: `pixi run project_n test --suite gfx --test "*start*"`
  Expected: `RiverStarts` is undefined.

- [ ] **Step 3: Implement**
  - **One helper for both callers.** A private helper classifies each start for `RiverStarts` and `DrawRivers` alike,
    so the two never disagree.
  - **Branch.** `core::JoinedEnd` on the river's waypoints reversed, with `startsFlowIn` false, returns a point.
  - **Pond.** The first point's tile is water. Otherwise, `Mouth(map, water, points[1], points[0])` finds water
    behind; then prepend its point, set `fadeIn = MOUTH_INTO_TILES * TileSize()`.
  - **Spring.** Everything else sets `taper = shape.taper`.
  - **`DrawRivers`'s doc.** It gains the start in the same style as its end.

- [ ] **Step 4: Run the suite to see it pass**

  Run: `pixi run project_n test --suite gfx`
  Expected: all pass.

- [ ] **Step 5: Gate and commit**

  Run `pixi run project_n test` and `pixi run project_n lint --range <task base>..HEAD`. `reference` is expected to
  differ now, so don't update it. Commit: `feat(gfx): draw a river's start as a spring, a pond or a branch`.

---

### Task 3: the shipped rivers' sources

`review: full` · `implementer: opus`

**Files:**
- Modify: `assets/data/world.yaml` (`grid:`, `rivers:`, then `roads:` and `fords:` by script)
- Modify: `tests/unit/gfx/river_draw_tests.cpp` (the source-rule test)
- Modify: `tests/unit/game/shipped_world_tests.cpp` (pinned costs, by the rule)
- Modify: `tests/reference/*.png` (by `reference --update`), `assets/AGENTS.md`, `art/AGENTS.md`

**Interfaces:**
- Consumes: Task 2's `gfx::RiverStarts`.

- [ ] **Step 1: Write the failing test**
  - "every shipped river rises on high ground, out of water or from another river": on `test::ShippedWorld()`, with
    the water kinds found by name as in "the shipped rivers fit the river buffer", every river's `RiverStarts` entry is
    one of:
    - `Branch`;
    - `Pond`;
    - `Spring`, with the kind at its first tile named `hills`, `rock` or `snow`.

    Each failure names the river's index and first tile.

- [ ] **Step 2: Run it to see it fail**

  Run: `pixi run project_n test --suite gfx --test "*rises*"`
  Expected: FAIL, naming rivers 0, 1, 3, 5, 6, 7, 8 and 9. River 2 (oasis), 4 (hills) and 10 (delta) pass.

- [ ] **Step 3: Edit `world.yaml`, following the spec's recon table**
  - **Hills.** Prepend upstream points so the first point of rivers 0, 1, 3, 5 and 8 lands on hills, rock or snow.
  - **Ponds.** River 9 gets a pond of 2 to 4 `-` tiles placed behind its first segment.
  - **Rivers 6 and 7.** Either a hills extension of about 6 tiles or a pond: whichever stays clear of the roads.
  - **Keep stretches short.** About 2 to 7 tiles, inland, and off road corridors where possible.
  - **Check by eye.** Look at the result on the map (look-at-the-game skill: the world scene).

- [ ] **Step 4: Reroute and test**
  - Run `pixi run python art/world_map.py --roads`, then `pixi run project_n selftest` (stale roads fail it).
  - Run `pixi run project_n test --suite gfx` and `pixi run project_n test --suite game`.
  - Apply the travel-number rule from Global Constraints. Update the pinned values, and keep a table of old → new
    values. On any stop condition, stop and report.

- [ ] **Step 5: Reference pictures**

  Run `pixi run project_n reference`, look at every changed picture against the old one (look-at-the-game), then
  `pixi run project_n reference --update`. Report which pictures changed, and why.

- [ ] **Step 6: Docs**
  - `assets/AGENTS.md`: a river's first point follows the source rule, and the shipped test checks it.
  - `art/AGENTS.md`: sources are placed by hand after generation, because the generator doesn't know the rule.

- [ ] **Step 7: Gate and commit**

  Run `pixi run project_n test` and `pixi run project_n lint --range <task base>..HEAD`.
  - **Data commit:** `feat(game): give every river a source on high ground or in a pond`, with `world.yaml`, the
    tests, the pinned values and the AGENTS.md lines.
  - **Picture commit:** `test(tests): update the reference pictures for the river heads`.
  - **Report:** the old → new table, and each river's new source.
