# The player's walk and the world camera: one pace, a loose follow

Status: implemented 2026-11-22 (see Outcome; approved by the user 2026-11-22; short design: a small change). Plan:
`2026-11-22-player-walk-and-camera.md`.

## Intent

Two things about moving the player on the world map feel wrong to the user (2026-11-22):

- "the click-to-move walk speed is a lot slower than the wasd, they should just use the same". Held movement (WASD,
  arrows, d-pad, stick: `Host::Walk`) moves the cultivator `creatures.yaml`'s `walk_speed` (4 units a tick) on any
  ground. A click (or the target cursor's Enter: `Host::WalkTo`) sets a route by `core::WALKING`, which pays each
  tile's cost at `ticks_per_cost` 16: a road tile (cost 4) takes 64 ticks, the same pace, but meadow (10) is 2.5 times
  slower, forest (16) 4 times, rock (40) 10 times.
- "the camera should follow the player with sort of a 50% screen size buffer". Following, the camera sits on the
  cultivator's foot every frame (`CameraControl::Update`), so every step scrolls the whole map.

## History

- The player's route came with 5b2 (`2026-11-15-phase6-chunk5b2-moving-design.md`): a click walks a `core::Journey`
  leg by `core::WALKING`, the profile every traveller walks by, so the player's route and the crowd's matched. That
  match is what goes: the travellers keep it.
- Held movement has never paid terrain and has no collision: it walks over deep water and peaks. The user was told
  (2026-11-22) and left it as it is.
- Whether the land should slow the player at all (roads mattering to the player) is a game-design question, recorded
  for the game-design review; this change doesn't decide it, it only makes the two ways of walking agree.

## Goals

1. **One pace for the player.** A click route moves the cultivator at held movement's speed, `walk_speed` units a
   tick of distance walked, on every kind of ground, diagonal steps taking their longer distance's time.
2. **The most direct route.** With terrain costing the player no time, the route is the shortest walk over tiles the
   player can enter (deep water and peaks stay impassable, as today), not the cheapest by cost: it no longer bends onto
   roads.
3. **The travellers are untouched.** Every NPC journey keeps `core::WALKING` and its costs: the world's simulation,
   its hashes and saved journeys don't change, beyond the player's route itself.
4. **A loose follow on the world map.** While the camera follows the cultivator on `world` (`CameraKind::Roam`, not
   freed by panning), the cultivator walks freely inside a box centred on the view, half the view's width and half its
   height; when the foot would leave the box, the camera moves just enough to keep it on the box's edge. Snapping back
   (C, the right stick's click, a middle click) puts the cultivator back at the centre, and so does a scene change.
   Zoom and hand panning are unchanged; a zoom that leaves the foot outside the shrunken box pulls the camera to the
   edge the same way.

## Non-goals

- Collision for held movement; terrain slowing the player; any change to the travellers' routes or pace.
- The combat scenes (`CameraKind::Follow`) stay centred on the cultivator: aiming there is relative to the player.
- `--camera-path` keeps following the foot exactly, so `measure`'s segments stay comparable with earlier runs.

## Approaches

There was one sensible way for each part, given in the conversation and approved as the design:

- The pace: time the player's route by distance at `walk_speed` (rejected: making held movement pay terrain too,
  which slows every key press off-road; the user chose one flat pace for the player only).
- The follow: a dead zone in `app::CameraControl`, which already owns following and freeing; nothing in `game` or the
  snapshot changes (rejected: a spring that eases the camera after the player, which the user didn't ask for and which
  lags input).

## Left to the builder

- How the player's route gets its flat cost and pace: a second movement profile in code, or a leg timed by distance
  (`core::MakeTileLeg` today times tiles by the profile's costs); it must stay saved, hashed and replayed exactly as
  the route is today, and `Host::Load`'s check of the route follows whatever it becomes.
- The box's edge in world units at the current zoom, from the view's size (`CameraInput::width`, `height`).
- Tests: test-first, about 20 named samples between them (tests/AGENTS.md): a route's end tick over meadow, forest
  and road all at `walk_speed`; a route that once bent onto a road now going straight; the camera not moving while the
  foot stays inside the box, moving by the overshoot when it leaves on each side, re-centring on snap back and scene
  change.
- Docs: src/AGENTS.md's world-scene lines (the route's profile, the camera's follow) in the same commits.

## Outcome

Implemented 2026-11-22 on `player-walk-camera`, subagent-driven in two parallel tasks, each with a full review and one
fix round, then a final whole-branch review (with a GLM second opinion) and one fix pass.

- **The pace.** The click route is `core::FindShortestRoute` (A* over the tiles `WALKING` can enter, a straight step
  priced 5 and a diagonal 7, the same neighbour and corner rules as `FindRoute`), timed by `core::MakePacedLeg`: the
  ceiling of the walked length over `walk_speed`, summed in floating point from the start, each arrive tick at least one
  after the last. It stays a `WALKING` tile-route leg, so `Host::Load`'s check, saves, hashes and replays take it
  unchanged ("fix(game): walk a clicked route at the held walk's pace"). One straight tile is 64 ticks, a diagonal 91.
- **Pricing and timing differ on purpose.** The search prices a diagonal 7/5 of a straight step so it stays integer and
  exact everywhere; the timing uses the true √2 length. Every octile-shortest route walks the same length, so the
  approximation never changes the time.
- **The route's shape.** With terrain no longer bending it, many routes tie for shortest; the lower-tile-index tie rule
  alone took every straight step and then every diagonal, an L that still looked road-bent. `Search` now orders by
  (length, aside, tile), where the aside is the sum over the entered tiles of each one's integer cross product with the
  line from `from` to `target` (its distance off the line times the line's length); the aside takes part in relaxation,
  so a later way of the same length that strays less replaces the parent. Length still decides first, so routes stay
  octile-shortest; `FindRoute` passes a zero aside and keeps its old routes ("fix(core): keep a clicked route close to
  its straight line").
- **`FindRoute` has no caller in the game now.** Its only production caller was the click; travellers route through
  `Paths` (distance maps and `RouteDown`). It stays, documented as such, because the test fixtures and `paths_tests` use
  it as the cost-cheapest reference (the proof that the click route differs from it lives there); removing it would port
  about a dozen samples for no change in behaviour. The plan's line that every NPC journey keeps `FindRoute` was wrong:
  they never used it.
- **`walk_speed` is bounded.** A tiny positive value made a long route's tick count overflow u32, undefined in release
  where the assert is compiled out. `MakePacedLeg` returns no leg when the speed isn't positive or any arrive tick
  wouldn't fit u32 (checked in f64 before narrowing, the one-tick bump in u64); `WalkTo` refuses that, or an end past
  `TRAVEL_END`; `creatures.yaml` allows 0 (doesn't walk) or at least `core::MIN_WALK_SPEED` = 0.01, at which a tile
  takes about seven minutes and a walk of over 100,000 tiles, diagonal or straight, still fits ("fix(core): refuse a
  walk whose ticks don't fit a leg"). The end-past-`TRAVEL_END` refusal is tested from a world loaded at the last timed
  tick less one; the travel test world now counts the months its clock implies, which loading a clock past the first
  month needs. The NaN speed case pins the behaviour, not one line: NaN is refused by the speed check and again by the
  u32 fit check.
- **Pictures.** `world_route`, `fog_explored_path` and `fog_uncovering` were refreshed for the new route and pace, and
  the fog test's `FOG_UNCOVER_TICKS` went from 190 to 201 for the same reason. The straight-line tie rule then changed
  `world_route` and `fog_uncovering` again: after the bridge the route drops back to the capital's row rather than
  running on along the road; `FOG_UNCOVER_TICKS` didn't move (the way to the bridge is the same). Those targets lie due
  east, so the pictures can't show a staircase; the paths tests do.
- **The follow.** `app::CameraControl` keeps the foot inside a centred box `FOLLOW_BOX_SHARE` (half) of the view on
  `CameraKind::Roam`, moving by the overshoot after this frame's zoom, so a zoom in pulls the camera by just enough
  ("feat(app): follow the cultivator loosely on the world map"). Snap back and a scene change re-centre; `Follow` scenes
  and `--camera-path` still centre exactly; a minimised window gives an empty box, no NaN.
- **The zoom test zooms in.** The plan said a zoom notch out gives a smaller box; that was a slip (zooming out grows the
  box, and the test could never fail). The spec's "shrunken box" means zooming in, which the test does from the box's
  edge.
- **No window-resize test.** At a fixed zoom the box's half is w·0.25 / min(w/1920, h/1080) world units per axis, never
  below 480×270, so a resize can't put the foot outside it; zooming in is the only way to shrink the box, and it is
  tested. A future change to `gfx::PixelsPerUnit` that breaks this would need that test.
- **The meadow test.** The host's meadow-pace test walks off the road (an earlier version walked it and passed on the
  old code). It dropped its "differs from `FindRoute`" check: on the small test map both give the same tiles, and the
  divergence is proven in `paths_tests`.
- **Minors.** Every Minor from the three reviews was fixed; the final review's duplicate host test was trimmed to the
  `LegSound` check only it can make, and two commit bodies were rewrapped at 72. One was ruled out: `Host::Tick`'s
  Doxygen paragraph reflows below the walk-to sentence the end-tick refusal added words to, as filled text must; keeping
  the old line breaks would leave lines over the 120-column limit.
