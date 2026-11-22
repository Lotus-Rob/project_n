# The player's walk and the world camera: one pace, a loose follow

Status: approved by the user 2026-11-22 (short design: a small change).

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
