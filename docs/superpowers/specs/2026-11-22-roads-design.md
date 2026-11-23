# Roads: rerouted along the land, drawn as ribbons, chosen by power

Status: **implemented 2026-11-22** (see Outcome; approved by the user 2026-11-22, builder check done; its questions answered by the user). Plan: `2026-11-22-roads.md`.

## Intent

The user wants roads that look and behave like real ones (2026-11-22). Today they are six straight-segment polylines
drawn as right-angle staircases of tile sprites. The user asked for:

- **Real rerouting.** "Really rerouted": walkers follow the new roads, and travel times change with them.
- **A natural look.** Ribbons "like the rivers, a bit wider".
- **Routes that read the land.** "Between mountains and around hazards".
- **Danger sites both ways.** "they can go through and around danger sites. the power level of the npc decides which
  they try".
- **What stays.** The same six cities; bridges and fords; the ribbons replace the 16 road sprites.

**Why:** the world map is where the player reads the world. Roads that bend around ranges and hazards make the land
legible, and danger sites start to mean something for travel before encounters exist.

## History

- **Generator** (`art/world_map.py`). `build_roads` (~1036) routes seven city pairs (`ROADS`) one after another:
  - Dijkstra over `plan_costs`, which is the kind's cost times fbm noise, `ROAD_PLAN_COST` 6 on a built road,
    `BRIDGE_PLAN_COST` 60 on a river and `CAUSEWAY_PLAN_COST` 90 in the shallows.
  - Later roads join earlier ones. `fit_line` pulls each path straight and simplifies it at `ROAD_TOLERANCE` 1.0.
  - `add_fords` (~1084) then places fords by detour.
  - `place_dangers` keeps every danger `DANGER_ROAD_CLEARANCE` 3 tiles from a road.
  - `main` regenerates everything and overwrites `world.yaml`.
- **`world.yaml`** has been hand-tuned since generation: kind-edge jogs, the profiles, the travel rules and their
  comments. A full rerun loses all of that (art/AGENTS.md).
  - Today it has 6 road polylines (24 points), 2 fords, 6 cities and 20 danger sites.
  - Measured 2026-11-22: every danger is at least 7.8 tiles from a road. The nearest are jade_cave (7.9) and
    bone_forest (7.8). The three mountain passes are 17 to 45 tiles away, west of the range, where no city lies.
- **Game rules.**
  - A road tile caps the terrain's cost at `road_cost` 4. A bridge also costs the road cost, a ford 30, and a bare
    river is impassable (`core::FeatureCosts`).
  - There is one profile, `on_foot`, at 16 ticks per cost. `MOST_PROFILES` is 16, and profiles are data:
    `ticks_per_cost`, `all_kinds`, `kinds` and `features` (road, river, bridge, ford).
  - Every NPC journey uses `core::WALKING`:
    - `Travel::Month` calls `RouteDown(paths.To(dest, WALKING))`.
    - `DestinationWeight` prices reach by WALKING.
    - `MakeTrip`, `PlanOf` and `Rebuild` assume WALKING.
  - The player's click routes use `FindShortestRoute`: distance only, over tiles WALKING can enter.
  - Danger sites affect no travel.
- **Drawing.**
  - Roads are 16 sprites chosen by their N/E/S/W neighbour mask (`gfx/terrain_draw.cpp` `AddLines`,
    `world_look.yaml` `roads:`).
  - Bridges are an `across` or `down` sprite, and fords are sprites.
  - Rivers are Catmull-Rom ribbons (`gfx/river_draw` over `gfx/ribbon.hpp`: `RIVER_STEPS` 4, `MAX_RIVER_QUADS` 8192,
    width 0.38 tile, an edge band under a core band, fogged).
- **Deferred to this.** The terrain spec's Decision 9 (2026-11-21) recorded, for later, "roads drawn from the road's
  tile chain (rounded corners, steady wobble, thin quiet ink, square to bridges) with travellers walking the same
  line". It also left out switchbacks and trails.
  - The backlog (2026-11-16) notes that travellers step 8-way while roads draw as staircases, so travellers cut the
    road's corners.
  - It also notes that everyone takes the one cheapest route and queues nose to tail. That is for the game-design
    review, not this work.
- **Compatibility.** Saves and sprite IDs may break freely before 1.0 (project memory). Rerouting changes
  `GameData::BaseHash()`, so old saves are refused, which is expected.

## Goals

1. **A rerouted network, regenerated without losing hand edits.** `art/world_map.py` gains a roads-only mode
   (`--roads`) that:
   - reads today's `world.yaml` (grid, kinds, rivers, places, profiles);
   - routes the same seven links between the same six cities, in the same order, so later roads join earlier ones;
   - rewrites only the `roads:` and `fords:` blocks. Every other byte stays the same, comments included.

   The mode is deterministic: the same input file gives the same output. It writes the usual previews to `build/`
   and prints which danger sites have a road through their reach. Bridges stay implicit: a road over a river.
2. **Between mountains.**
   - The road plan costs keep roads off rock and snow where the land allows.
   - They keep roads a tile or more from peaks, so roads run down valleys and through gaps rather than along range
     edges.
   - Rivers are crossed seldom and squarely, and the causeway stays only where the isle needs it.
   - Hills are allowed where there is no other way.
3. **Through and around dangers.**
   - Each link is routed twice: as a strong traveller would go (danger ignored) and as a weak one would (ground near a
     danger costly).
   - Where the two routes part, both become roads: a short road past the danger and a longer one around it.
   - Where they agree, there is one road.
   - Today every danger is too far from any link for a fork to happen by itself. So (the user, 2026-11-22) **about
     three danger sites move by hand, in their own commit, before the roads are routed.** Each goes onto a natural
     shortcut of a different link where a way round exists (for example a cave, a haunted forest and a cliff), so
     those three links fork.
   - The moved sites keep their keys, names and sorts, and the generator's spacing (`DANGER_SPACING`, 11 tiles from
     any other place). The other seventeen stay put. The roads-only mode itself never edits `places:`.
4. **Power picks the route.**
   - Tiles within `danger_reach` tiles of a danger site carry a new tile feature, `danger`.
   - A profile's `features: { danger: <cost> }` is an added cost on those tiles. It comes after the road, bridge,
     ford and river rules, so a road through a pass is still dear for the weak.
   - `travel:` gains power bands that map power to a profile. Shipped: below `cultivator_power` (5) walks
     `on_foot_wary` (danger costly); from 5 up walks `on_foot` (no danger cost).
   - The choice affects only routes and reach. No one is harmed; encounters belong to a later chunk.
5. **Travel follows the profiles.**
   - Each journey's legs carry the traveller's profile: `Travel::Month`, `DestinationWeight`, `MakeTrip`, `PlanOf`
     and `Rebuild`.
   - `Paths::Warm` warms every profile a band names.
   - Saves record the profile, and `WORLD_VERSION` is bumped.
   - Determinism is unchanged: the same results for any worker count.
   - The player keeps `FindShortestRoute` (distance only; danger ignored).
6. **Roads drawn as ribbons.**
   - Roads draw with the river ribbon machinery: Catmull-Rom splines, a narrow core over a soft edge band.
   - Widths and colours go in `world_look.yaml` `road:` (like `river:`): narrower than a river (0.38) but wider than
     today's sprite road, in a quiet ink.
   - A road's end that meets another road's middle joins it. A road ends at its city's tile centre.
   - Ribbons draw after the rivers and before the bridges, fords and stamps, under the fog as rivers are.
   - They replace the 16 road sprites and `world_look.yaml`'s `roads:` list. Those sprites leave `art/sprites.toml`.
7. **Travellers drawn on the road** (the user, 2026-11-22). Travellers keep walking tile centre to tile centre in the
   simulation (journeys, `core::Where`, saves and timings untouched), but while one is on a road tile it is *drawn* at
   the nearest point on that road's curve, easing on and off over the road's first and last tile and where it steps
   onto or off the road mid-way, so it never jumps. Presentation only: never saved, hashed or fed back to the
   simulation; the same for any worker count. The player's cultivator follows the same rule on a road (its click
   routes and held walk otherwise unchanged); its label, selection and pick box move with the drawn body.
8. **Travel retuned to its stated targets** (my call). New routes and the wary profile change `DestinationWeight`'s
   costs for 80% of travellers, so `travel:` (`share_per_mille`, `reach_base`, `reach_per_power`) is retuned to keep the
   targets its comment states: about 2,500 to 3,300 people on the roads at once, mortals reaching the next town, the
   strong crossing the map; the comment's example costs are rewritten from the new map. "A cut-off city" means both
   that every band's profile reaches every city and that those targets hold.
9. **Bridges and fords kept.** A bridge keeps its sprite and picks `across` or `down` by the ribbon's heading at the
   crossing. Fords keep theirs.
10. **Tested.**
   - core tests for the `danger` feature and its costs.
   - Band selection, including the boundaries.
   - Journeys by profile, and save/load round trips.
   - A Python test of the roads-only mode: the file outside the two blocks is unchanged, the output is deterministic,
     every band's profile reaches every city, and the three moved sites' links fork.
   - The ribbon layout, and the drawn-on-the-curve position (it moves steadily forward along a leg and never jumps
     on or off the road), in pure tests.
   - The travel targets, checked by a test over the shipped world.
   - Updated reference pictures, each viewed, one of them showing a fork.

## Non-goals

- **The cities don't change.** No new cities, links or trails, and no switchbacks up mountains (still recorded for
  later).
- **No harm on the road.** No damage, encounters or risk; danger is a route preference only.
- **No dispersal.** NPCs leaving roads, and spreading the queues, go to the game-design review.
- **The player's walk is unchanged.** It stays distance-only; whether terrain or danger should slow the player is a
  game-design question.
- **No full regeneration.** No change to the terrain, rivers, coastline or places, except the hand move of about three
  danger sites (Goal 3).
- **No save migration** (before 1.0).

## Anti-goals (a failure even if it works)

- **Lost hand edits:** the roads-only mode changing anything outside `roads:` and `fords:` in `world.yaml`.
- **Off-road walkers:** travellers visibly walking beside the drawn road along its length.
- **Staircases:** a visible tile-grid step in a road ribbon at any zoom.
- **Shimmer or popping:** ribbon segments crawling or vanishing while panning or zooming. A shimmer guard case stays
  under 0.02, as for rivers.
- **Cost:** a visible frame-time change in `measure`, or a month step noticeably slower from the extra profiles.
- **A cut-off city:** a city that a band can't reach.
- **Art locked in code:** a road width or colour that only code can change.

## Approaches

The user chose A, 2026-11-22.

- **A. A roads-only mode in the generator (chosen).**
  - It reads today's map, routes the roads with the land- and danger-aware plan costs, and rewrites only roads and
    fords.
  - It keeps the hand edits and can be repeated when the map changes.
  - It costs a careful text rewrite of two YAML blocks.
- **B. Full regeneration with new road rules.** The smallest code change, but it wipes the hand-tuned coastline jogs,
  profiles and travel rules, which would then be redone by hand.
- **C. Hand-drawn routes.** No tool, but slow, hard to check, and redone whenever the map changes.

The power bands use the existing profiles (up to 16, already data), plus one tile feature. Proposed with A, not
separately confirmed; listed for the user.

## Left to the builder

- The road plan costs' exact form: the mountain and peak-proximity terms, noise kept or dropped, and danger cost in
  planning. These are judged by the previews and the reachability print.
- How the two-pass routing decides that two routes "part" (minimum divergence length), and how fords are re-placed
  (today's `add_fords`, rerun over the new roads).
- `danger_reach`'s distance metric and default, and the shipped `danger` cost for `on_foot_wary`. Both are values in
  `world.yaml`, chosen so that a weak traveller detours when a reasonable way round exists.
- Generalising `DrawRivers` into a shared polyline-ribbon layout, or a sibling `DrawRoads`, and the quad budget.
- How the text rewrite of `world.yaml` finds its blocks, and keeps the `roads:` comment line and the flow style.

## Calls made for the builder check (listed for the user)

- **Which danger sites move, and where.** Picked by hand from the previews: about three, each on a different link,
  each with a way round.
- **Two shipped bands, split at `cultivator_power`.** Below it, `on_foot_wary`; from it up, `on_foot`. They are data,
  so more bands are a values change.
- **Travel retuned to its comment's targets** (Goal 8).
- **The player is drawn on the curve too** (Goal 7). The danger cost never applies to the player.

## Outcome

**Measurements** (`measure --runs world`, 2 workers, unpaced, fullscreen at 2560×1440 on the 165 Hz display, travel
share 200, every run trusted). Run back to back, this branch then main: frame medians 6.0 ms on both; the month step's
hitch, on the main thread, 53.0 against 53.4 ms (main has it too); the month step's logic worst 27.4 against 25.0 ms and
its logged stall 25 against 22 ms, about 10%, within run-to-run noise. An earlier pair, not back to back, read 29.2
against 11.7 ms; the investigation that followed found the branch's month step costs the same as main's without a
window (8.5 ms for both at share 200; 6,891 departures against 6,929, 5,474 of them by the wary profile; 42.8 route
tiles a departure against 41.0), the extra profile's distance maps being built once at start (Seed 37 → 46 ms), and the
captures showing code this branch doesn't touch slowed by the same factor near that step: the 11.7 was a quiet run. The
cost anti-goal holds. The investigation kept profiling zones for the month step's parts and one cut that keeps every
route: `RouteDown` asks a step's cost only of neighbours nearer the goal (`perf(core): step down a distance map only
through cheaper tiles`: route finding 6.5 → 4.3 ms of CPU a month, main's logic gaining alike). The busiest-view load
changed with the bands (the fewest travellers there 2,304 → 2,083 at share 200), so frame costs compare like for like
only roughly. Task 9's earlier runs were untrusted (the user's game held focus).

**Shimmer** (`reference`): the rerouted roads' shimmer measured 0.00500 with the first road look (a 1.3 px road, the
guard's own failing case, measured 0.0366). The paler road measures 0.00608, because the metric is crawl as a share of
the road's ink, so a quieter road scores higher for the same crawl; the bound was re-set from the shipped look by the
house rule (about 1.25 times: 0.0076, far inside this spec's 0.02), and a deliberately hard road fails it at 0.0623.

**Crowd** (Goal 8): with `danger_reach` 4, the wary profile's `danger` cost 40 and the share at 58, the crowd runs
2,517 to 3,235 against the bounds 2,500 to 3,300 (were 2,000 to 6,000). Share 59 gives 2,549 to 3,281; a whole-number
share can't centre the range, and a later retune that trips the floor is a fair signal. Game routes agree with the
generator: capital to oasis is 173 for the wary profile and 152 for the strong one (pinned).

### Rulings made while building

Routing and data:
- The generator reads the weak pass's danger cost from the profile the band at `from_power` 0 names; with no profile
  carrying a danger cost the weak pass equals the strong one, and no forks are made.
- A fork's joins come from the built road's rasterised tiles, not the strong path's raw tiles; then each fork's meeting
  tile becomes a waypoint of the road it leaves, so the curve passes through it (`build(tools): lay each fork's ends on
  waypoints of the road it leaves`). Before that, the Bone Forest fork joined at neither end: the trunk's curve bulged
  about 0.8 tile, past the join's reach. A shipped-world test now checks every non-city end lies on another curve
  (within the reach plus 3: fixed ends measure up to 8, broken ones 16 to 22).
- The wary route is planned after its link's own strong road is built, so it sees that road as cheap; planned first, Jade
  Cave's way round ran as a second road two tiles beside the main one for about ten rows (`build(tools): plan the wary
  way over its link's own road`).
- The rewrite of `world.yaml` refuses stray lines (comments or blanks at column 0) inside a block it rewrites, and reads
  only plain decimals as numbers (`inf`, `nan` and `1_0` stay text).
- Sites moved: Jade Cave to (73, 24), Bone Forest to (86, 42), Ghostlight Woods to (126, 56). None is a cliff: the
  spec's "a cave, a haunted forest and a cliff" was an example.
- `world.yaml`'s roads are checked against a `world_map.py --roads` pass (`test(tools): fail when world.yaml's roads are
  stale`), and its header says where they come from.
- Data commits are `chore(data)`, not the plan's `feat(data)`: feat and fix are kept for game code.

Rules and saves:
- Profile 0 (walking) never carries a danger cost, and a `danger_reach` too large for the mark's range is refused.
- The band table reports unknown keys. Danger stays passable for every profile: every band reaches every city (a test
  guards it).
- Sending travellers by their band's profile changes the world's version, so saves break: the commit is marked breaking
  (`feat(game)!:`), following the git-commit skill's rule over the plan's subject. Four save seeds were regenerated; no
  expected value changed. A many-chunk test with 2,148 travellers checks 1 and 4 workers agree across bands.

Drawing:
- Roads join at both ends, and an end on a city's tile is never moved: the spec says a road's end joins, and that a road
  ends at its city's tile centre.
- Joining both ends needed three more rules, each found by a failing case: a meeting near another road's start is
  skipped only where that end itself flows in; of two roads whose ends would each join the other, the lower index yields,
  so one of them joins; and when both of a road's joins would leave its ends closer than a short run, neither moves (the
  road is drawn crossing, as authored). `Midline` skips repeated waypoints, so it never returns a NaN curve.
- The road sprites, `LINE_MASKS` and the YAML `roads:` sprite list stayed until the ribbons drew roads, so every commit
  built.
- "Passes through every waypoint" means every distinct point of the road's polyline, within 0.001 units: those are the
  spline's knots.
- In-sight roads read quieter than rivers: the road colours moved towards the paper (zoom 0.6 road at (149, 132, 109)),
  judged by eye against the river. The junction by the bridge at (40, 26) was looked at in three zooms: smooth, and the
  bridge square across it.

Travellers on the road:
- The drawn shift is the nearest-point rule taken at each leg point (the origin and each tile centre) and blended
  linearly between them: taken at every position it jumped 0.31 tile in a tick at a staircase fork, which breaks "never
  jumps". A journey's own first and last points get no shift.
- Held walking isn't snapped onto the road (only routes are). When held input or a new walk-to takes over a route
  mid-road, the shift left over fades to zero across the body's next tile of movement, so a cultivator standing still
  never slides. It resets on load, on entering a scene, and on a clock jump that carries the cultivator along its route.
  With no route, a jump leaves the leftover alone: the body doesn't move, so resetting it would itself jump the drawn
  cultivator (a cross-model review asked for the reset; rejected for that reason).
- The route line ahead follows the road too: each remaining road tile at its point on the curve, the clicked end at its
  tile's centre, as the drawn walker will pass them (`fix(game): draw the route line along the road's curve`).
- The camera follows the cultivator's actual body, not its drawn position (the user, 2026-11-22); the label, selection
  and pick box move with the drawn body.
- The drawn-traveller tests build their own small road maps, never the shipped one, so rerouting the shipped roads moves no
  pinned curve.

From the final review (Opus, plus GLM-5.3-Flash and DeepSeek V4.1 Flash):
- Rivers and roads share one look and shape type (`RibbonLook`, `RibbonShape`) and the renderer's header is named for
  what it draws (`line_ribbons`); the road ribbon keeps the river's shader pair.
- The full generator writes the danger reach, the wary profile and the bands as the shipped file has them and routes its
  roads through `--roads`' code, so a regenerated world still forks round danger; art/AGENTS.md and the root AGENTS.md
  say that a land edit to world.yaml means running `--roads`, which a self-test enforces.
- The travel comment's example costs are all pinned (126, 162, 173/152, 562).
- `StepCost` promises an allowed step costs at least 1, which `RouteDown`'s cut relies on.

Process:
- A test that pins a hash, a shipped value or a save seed that a change legitimately moves is updated to the new value
  and named, never loosened.
- The power bands' tests were written after the code (their failing run was a compile error); the reviewer checked them
  as sound.
