# Phase 6, chunk 5b2: moving on the world map

Date: 2026-11-15 · Status: **implemented 2026-11-17** (see Outcome; approved 2026-11-15) · Plan: `2026-11-15-phase6-chunk5b2.md` · Chunk spec: `2026-11-10-phase6-chunk5-world-map-design.md` · Phase spec:
`2026-11-10-phase6-design.md` · Builds on: `2026-11-15-phase6-chunk5b1-map-design.md` (its Outcome)

## In short

- **Days pass on the world map:** a day every 3 seconds, 30 days a month; the month step runs by itself when a month's
  days run out (M stays as a debug key). A date line on the HUD shows the month and day; clicking it, or a key or
  button, pauses the world's clock.
- **People travel between cities:** each month the NPC simulation sends some of the 50,000 world NPCs on a **visit**:
  they walk to another city preferring roads, wander round it for a few days, walk home and take up their routine
  again. Where they go is weighed by the cities' **pull** (size and distance), the NPC's **reach** (its power) and each
  city's **character** (what kind of visitor it draws). A few thousand are on the roads at once. They replace 5a's
  circling stand-ins.
- **A journey is a record** (approach A): who travels, its legs, when it starts and how long the stay is, decided at
  departure; where a traveller stands is a pure function of the record and the world's clock, worked out only for the
  travellers in view. Ticks do nothing for travellers.
- **Built for transport to come:** cost is time; ways of travelling are **movement profiles** in data (terrain and tile
  feature costs, a pace); a journey is a list of **legs**, each with a profile and a **shape** (5b2 builds the tile
  route; flight and timed jumps are later shapes); a journey carries **one or more travellers** (a convoy later). 5b2
  ships walking only.
- **A travel journal:** each NPC's last 10 journeys (50 for those the player has met), written as journeys end, for
  chunk 9's inspect profile.
- **Click to walk:** a left click on open ground walks the cultivator there along the cheapest route; on keyboard and
  gamepad a target cursor does the same; any movement press cancels it and free walking carries on as today. The route
  shows as a line.
- **NPCs start with a spread of power:** most mortals, some cultivators, a rare few strong, drawn when the world is
  built, so reach and city character show from the first month.
- **Every NPC has a name,** generated from its identity: characters in Chinese, pinyin in English; every traveller in
  view gets a label, thinned by chunk 3's rules.

## Intent

The world map is the game's hub and its largest, busiest scene. 5b1 made the map real; 5b2 makes it alive: roads with
people on them, days going by, the player getting around by pointing at where to go. The traffic should say something
about the world at a glance: where its centre is (pull), that power is freedom in a cultivation world (reach), and what
each city is for before you enter it (character). The design must not block what is planned later: mounts, carriages,
merchant convoys, boats, flight and teleport arrays (the user, 2026-11-15), encounters on the road (chunk 7), fog and
sight (5b3), the world kept alive behind entered scenes (5c) and the NPC inspect profile (chunk 9).

## Approaches

**How travellers exist in the game state** (the user picked A, 2026-11-15):

| Approach | Trade-offs |
|---|---|
| **A. A journey is a record; position is computed from it (picked)** | The month step writes one record per journey (members, legs, start, stay), the whole visit decided at departure; the publish places each traveller in view from its record at the snapshot's tick, as `core::SwordAt` places a sword from its volley record. No per-tick work, small saves, exact replays and hashes; an interruption later (an ambush, a change of mount) ends the leg at that tick and starts another |
| B. Travellers become active entities stepped every tick | Most flexible for reactions mid-road, but thousands of entities stepped each tick, bigger saves, and state that can drift from its route |
| C. A, plus travellers near the player materialised as active entities (0002 §7.7's tiers) | Ready for encounters, which don't exist yet; chunk 7 can add it on top of A without undoing anything |

**Player movement** (the user picked A, 2026-11-15): A, free walking and routes side by side, over B, routes only
(Tale of Immortal's way: a different feel from the scenes, and `measure`'s scripted walk rewritten).

**What the traffic says** (the user picked 1-3, 2026-11-15): 1 pull, 2 reach, 3 city character in 5b2; 4 (people visit
people they like, from the relationship table) and 5 (danger shapes routes) backlogged until relationships show in the
UI and danger sites do something.

**Pathfinding** was decided in 5b1 (its Decision 4): a distance map per place plus one search per click, our own code
(5b1's approaches table: the libraries assume uniform costs or cost as much code to adapt; re-checked here: nothing
changed, and profiles make costs vary per traveller, which the uniform-cost libraries can't do at all).

## History

- **5b1** built what this builds on: `core::WorldMap` (whole-number costs, `IMPASSABLE_COST`, `MAX_WALK_COST`,
  `StepCost` with diagonals at 7/5 past two passable orthogonal neighbours, tile flags for road, river, bridge and ford,
  `RegionAt` and `RegionTiles`), the 50,000 world NPCs homed in cities and their regions (`Npc::home`, `Npc::cell` a
  tile index; `WORLD_VERSION` 2), strings named by data (`core::Strings::Add`, `MOST_STRINGS` 0xFFFF), and one
  Dijkstra from every city at once for the regions (`src/core/world_map.cpp`, fixed tie-breaks): the distance maps reuse
  its search.
- **5b1 left for 5b2:** the stand-in walkers (`WalkerOrbit`, `src/game/scenes/world.cpp`; `OrbitPoint` stays, the
  arena, `stress` and the host use it), the map's size in code (`WORLD_COLUMNS`, `WORLD_ROWS`, `WORLD_MAP`, kept only
  for the stand-ins' compile-time checks), a month step timed only by hand (its Known limits: "no flag or camera-path step
  drives the calendar yet"), and the world run's baseline (Ruling M: the branch's numbers are the baseline for 5b2).
- **Decided and carried:** diagonals stay (5b1 Decision 8); route costs are whole numbers, floats only to place a walker
  along its route; determinism holds within one binary (0002 §7.4; no contraction flag, 5b1 Goal 10); the view
  rectangle is never recorded or hashed (5a); `stress` keeps its own grid and 50,000.
- **Rejected before:** a search per walker each month and per-click search only (5b1's approaches); JPS, Central64,
  Boost.Graph, LEMON, Recast (uniform costs or no gain); GPL and AGPL graph libraries.
- **Backlog items raised at 5b's design** (sweep-up backlog): walk animation and facing, sprite sharpness at every zoom
  and the world map look stay with 5b-look (the split table; confirmed in this brainstorm: the user's "jitter" came from
  the orbits' wobble, which travellers walking their routes end). The "determinism across architectures" item is closed
  by 5b1 Goal 10 and leaves the backlog with this branch.
- **Constraints recorded with the user:** accessibility first (every option by mouse, keyboard and gamepad; settings
  never change difficulty); small unit tests (about 20 samples, never sweeps); world shimmer 0.0436 against a 0.06 guard
  (5b-look's target 0.02).

## Goals

1. **The world's clock.** A world clock in ticks that advances only while a scene showing the world map runs and the
   clock isn't paused (time stands still in other scenes, the chunk spec's call); saved, hashed. A day is 180 ticks (3 s
   at 60 Hz), a month 30 days; both constants for now (a setting later is the chunk spec's note). The calendar (months
   passed, day of the month) derives from the clock, replacing the separate months counter (the save's format goes up).
   When a month's last day ends the month step runs by itself, through the same frozen tick and transition as today's
   `AdvanceCalendar`; M (a debug key) still asks for it at once and moves the clock to the next month's first day.
2. **Pause the world.** A control (keyboard and gamepad, in `app/bindings.cpp`) or a click on the date line (Goal 20)
   pauses and resumes the world's clock on the world map: travellers stand still and the cultivator can't move, while the camera still pans and zooms. It is an
   input action, recorded in replays. It does nothing else and changes no difficulty.
3. **Movement profiles.** `world.yaml` gains `profiles:`, each with a key, a pace (`ticks_per_cost`: ticks per unit of
   cost) and optional overrides: a cost per terrain kind and per tile feature (road, river, bridge, ford), or impassable.
   The map's existing costs (`kinds`, `road_cost`, `ford_cost`) are the base every profile starts from; a profile with
   no overrides walks exactly 5b1's costs. 5b2 ships one profile, `on_foot`, with no overrides and a pace of 16 ticks
   per cost unit, so a road tile (cost 4) takes 64 ticks, as long as the cultivator takes to walk 256 units at full
   stick today (240 units a second): open meadow is 2.5 times slower, rock 10 times. Regions (5b1) stay on 5b1's costs.
   The reader refuses an unknown kind or feature, a pace of 0, and an override above `MAX_WALK_COST`.
4. **Cost is time.** A route's cost, in whole numbers, times its profile's pace gives the ticks each step takes; where a
   traveller stands at a tick is found from the steps' cumulative ticks and placed between tile centres with floats.
5. **Distance maps** per (city, profile): the cost from every tile to the city by that profile, from one search from the
   city outward with 5b1's tie-breaks, built when first asked for and kept for the run (they derive from game data,
   never saved). The on-foot maps of every city are built when the world scene starts, so the first month step doesn't
   pay for them. A route to a city is read off its map by stepping downhill from the start tile, ties broken in a fixed
   order.
6. **Journeys.** A journey record in the world's state: its members (stable IDs, one or more), its legs in order, the
   tick it starts, and what happens between legs (a stay of some days in a city). A leg has a profile and a **shape**;
   all the code asks a leg is "where is this member at tick t, and when does the leg end?", so a new shape (a straight
   flight line, a timed jump between two places) is one more case behind that question and nothing that uses legs
   changes. 5b2 builds one shape, the tile route (its tiles and cumulative ticks), and journeys with one member.
7. **Visits.** At the month step a share of the world NPCs at home and not on a journey set out on a visit: a leg from
   home to the destination city, a stay of a few days (drawn, 2 to 5), a leg home. Departures are spread over the
   month's days; an NPC waiting to leave stays home. While away, an NPC's month is "travelling" (a new `Action`), and it
   doesn't socialise. When a journey ends the month step writes its journal entry and drops it. A visit can run over a
   month step. The whole visit is decided at departure from `core/random.hpp` draws keyed on the state counter and the
   NPC; the share is tuned so a few thousand are on the roads at once (the chunk spec's call).
8. **Where they go.** Each candidate city other than home is weighed by three factors multiplied:
   - **pull:** the destination's people over the on-foot cost from home, squared (bigger and nearer draws more);
   - **reach:** a falloff with that cost set by the NPC's power: mortals reach the next town, the strongest cross the
     map;
   - **character:** each city in `world.yaml` gains a `draws:` weight per kind of visitor; 5b2 has two kinds,
     cultivators and traders, chosen from the NPC's power (the threshold Claude's call). The capital draws both, the sect
     town cultivators, the port and the oasis traders, the rest in between.
   Power comes from the starting spread (Goal 19) plus what the month step adds.
   Whole-number or fixed-point weights, so the draw is the same on every machine.
9. **In the city.** A visitor stays on and round its destination's tile, drifting slowly between a few points drawn per
   journey and day, a pure function of the record and the tick: no orbit wobble, nothing that reads as jitter. A city's
   residents are still not drawn (the chunk spec's call).
10. **Roads busy from the first frame.** The world scene's Build seeds journeys as if the world had been running a month
    (departure ticks drawn over the month before as well), so `measure` and a new game see travellers at once.
11. **The travel journal.** A table in the world's state, separate from the NPCs' hot data, holding each NPC's last 10
    journeys (50 for NPCs with `Met`), oldest dropped first: the destination city, the departure day (days since the
    world began), days away, the profile; about 8 bytes an entry, saved and hashed. Written only when a journey ends.
    Shown nowhere until chunk 9's inspect profile; the debug log notes a journey's end at debug level.
12. **Names.** A new `assets/data/names.yaml` (game data, hashed): surnames and given-name characters, each with its
    pinyin, surnames weighted by how common they are. An NPC's name is a surname and one or two given characters,
    drawn from `core/random.hpp` keyed on its stable ID and the world's seed, never saved (a pure function, so nothing
    grows the save). The text system composes it from the table's indices in the player's language (characters in
    Chinese; pinyin, capitalised, without tone marks, in English), so 50,000 names never enter the strings table
    (`MOST_STRINGS`). The cultivator keeps its own name.
13. **Labels at scale.** Every traveller in view (inside the published rectangle) carries a name label at `Named`
    priority; chunk 3's label pass scales, fades, stacks and thins them; a selected traveller's label always shows. 5a's
    "every eighth walker" goes with the stand-ins.
14. **Click to walk.** On the world map a left click on open ground sends a new recorded action (walk to a world point);
    the host finds the cheapest on-foot route from the cultivator's tile to the clicked tile (one A* search, its
    heuristic the octile distance times the cheapest step) and the cultivator walks it as a journey of its own (Goal 6),
    the camera following. A click on a walker selects it instead (selection first). A click on an impassable or
    unreachable tile does nothing (logged at debug). Any movement input cancels the route and free walking carries on
    at today's speed; free walking itself is unchanged (it doesn't read the terrain).
15. **The target cursor.** For keyboard and gamepad: a control shows a highlighted tile at the cultivator's; the
    movement keys or the d-pad move it a tile at a time (held: repeating), confirm walks there (the same action as a
    click), cancel hides it. While the cursor shows, movement input moves the cursor, not the cultivator. Bindings in
    `app/bindings.cpp`; no on-screen control hints until chunk 10's menus.
16. **The route line.** While the cultivator walks a route, a plain line along the route's remaining tile centres to a
    marker on the destination, under the entities; placeholder look in `world_look.yaml` (presentation), restyled by
    5b-look (smooth lines are 5b-look's).
17. **The stand-ins go.** `WalkerOrbit`, the gatherings, `WORLD_SPREAD_WALKERS` and their tests are removed; the map's
    size comes from the map (`WORLD_COLUMNS`, `WORLD_ROWS`, `WORLD_MAP` gone, the checks they fed made at run time or
    pinned by the shipped-map tests). `WORLD_BUSIEST` stays as `measure`'s anchor, pinned to the port's tile as now.
18. **The measurements keep their meaning.** The busiest view (the port, the widest zoom) must still hold at least 2,000
    drawn walkers in `measure`'s world run, travellers and visitors together; if the shipped balance can't give that, a
    measurement flag raises the travel share for `measure` only (the flag Claude's to name), and the spec's Outcome
    says which. A flag also runs a month step during the camera path, so `measure` times it unattended. The world run
    is measured before and after on the same machine in one sitting, and the Outcome records the new baseline.
19. **A starting spread of power.** Today every world NPC starts at power 0 and gains 1 a month cultivating, so all
    50,000 would be equally weak and travel would show pull alone. The world scene's Build draws each world NPC's
    starting power from `core/random.hpp` keyed on its stable ID: a long tail, most mortals near 0, some cultivators,
    a rare few strong (the shape Claude's call, below), in its own early task, since reach, visitor kind and the travel
    share are tuned against it. `stress` and the arena keep their NPCs as they are.
20. **The date line.** A small HUD line on the world map: the month and the day of the month in the player's language
    (strings in `strings.yaml`, both languages), with a mark while paused; its layout in `effects.yaml`'s HUD section as
    the HUD's other lines. Clicking it pauses or resumes (Goal 2): the HUD's one clickable area, hit-tested before world
    clicks, so a click on it never walks the cultivator or selects anyone. Hidden in scenes without the world map.

## Non-goals

- Mounts, carriages, convoys, boats, flight, teleport arrays and their gameplay (renting, prices, stamina, docks): the
  shape is built (Goals 3 and 6), the content is backlogged as "mounts and convoys", pointing at this spec.
- Travel by relationships and danger-shaped routes (backlogged, above).
- Fog, sight and walkers seen only within sight (5b3); everything in view is drawn in 5b2.
- Walk frames, facing the direction of travel, smooth route lines, sharper art (5b-look).
- Entering places, the world kept alive behind scenes, the save's world-and-scene split (5c).
- Encounters on the road (chunk 7); the inspect profile and the journal shown (chunk 9); a pause menu (chunk 10).
- Free walking reading the terrain (blocked by sea, slowed by rock): free walking stays as today.
- On-screen control hints per device (chunk 10's menus); a power growth model beyond today's month step.

## Anti-goals (a failure even if it works)

- **Jitter, popping or teleporting:** a traveller jumping between frames, appearing at the view's edge, or snapping at a
  leg's end, a stay's start or a month step.
- **Hitches as days pass:** only the month step may pause, behind its transition.
- **Lag** from a click or a cursor confirm to the cultivator setting off.
- **A dead map:** empty roads at the start, at a month's start, or after a load.
- **A shape that blocks transport:** anything assuming one profile, one leg, one member per journey, or tile routes as
  the only shape.
- **Names or travel that differ between runs** of the same binary and seed, or between a game and its replay.
- **An option one device can't reach:** click to walk, the cursor and the pause each by mouse, keyboard and gamepad.

## Decisions

The user's, 2026-11-15:

1. **Free walking and routes side by side** (movement approach A): keyboard and stick walk freely; a click, or a target
   cursor on keyboard and gamepad, sets a route; any direction press cancels it.
2. **Visits:** travellers walk to another city, wander it a little, then walk home and take up their routine.
3. **Traffic by pull, reach and city character** (1-3); relationships and danger backlogged (4, 5).
4. **Mounts and transport accounted for:** personal mounts, rented carriages and merchant convoys, and beyond them
   boats, flight and teleport arrays, as profiles, legs, leg shapes and group journeys; profiles price tile features as
   well as terrain; a leg's shape sits behind one question. Shape only in 5b2.
5. **A journey is a record** (approach A).
6. **The travel journal:** the last 10 journeys per NPC, 50 for met NPCs, written from 5b2 on.
7. **NPCs start with a spread of power** (asked after the builder check: with everyone at 0, reach and character
   wouldn't show), drawn when the world is built.
8. **A date line on the HUD,** clicking it pauses; keyboard and gamepad bindings pause too; no on-screen control hints
   until chunk 10.
9. **Walk frames and facing stay in 5b-look;** the order stays 5b2, 5b3, 5b-look (confirmed 2026-11-15 when 5b1 was
   building).

The chunk spec's decisions 4 (click to walk, camera), 6 (time flows), 7 (NPCs travel), 8 (generated names) and 14
(NPCs belong to cities) are built here.

## Calls made here (not the user's; check them)

- **3 seconds a day, 30 days a month** (about 90 s a month); a road tile on foot at the cultivator's walking pace, so
  the capital to the port (about 53 tiles of road) is about 19 days, and a visit there and back about six weeks.
- **The pause** stops the whole world on the world map (clock, travellers, the cultivator), not only the clock; world
  map only until chunk 10's pause menu.
- **The starting power's shape:** about 80% mortals (power 0 to 5), 18% cultivators (5 to 50), 2% strong (50 to
  500), the cultivator/trader threshold at 5; tuned with the travel share and recorded in the Outcome.
- **Two kinds of visitor** (cultivators and traders, by power) for city character; more kinds with chunk 6's towns.
- **Stays of 2 to 5 days;** a visitor's in-city drift is a few points round the tile, not a walk through streets.
- **Names aren't saved:** a pure function of the stable ID and seed (the chunk spec said "saved with it"; nothing is
  lost, as the function and `names.yaml` are hashed game data). One pool of given characters, no gender.
- **Pinyin without tone marks** in English (font coverage, and labels read at small sizes).
- **A click on an impassable or unreachable tile does nothing;** no nearest-reachable fallback.
- **Free walking ignores terrain,** as today; only routes read it.
- **Distance maps for cities only;** danger sites aren't destinations until 5c or chunk 7.
- **A measurement flag** keeps `measure`'s busiest-view load if the shipped balance doesn't (Goal 18).

## Testing

Small unit tests, about 20 representative samples each, never sweeps (the user's rule):

- **Profiles:** parsing and refusals (unknown kind or feature, pace 0, an override past `MAX_WALK_COST`); a profile with
  no overrides costs exactly 5b1's steps; a road-only profile refuses an off-road step; a water profile (built in a
  test) crosses a river tile cheaply.
- **Distance maps and routes:** a route prefers a road over a shorter open-country line; it is the same each run (fixed
  tie-breaks); it never crosses an impassable tile or cuts a diagonal past an impassable neighbour; the A* route from a
  click costs the same as the distance map's route to the same city tile.
- **Legs and journeys:** position at a leg's start and end are its first and last tile centres; position is continuous
  across a leg's steps, across leg ends and stays (no jump past a small epsilon between consecutive ticks); a journey of
  two members places both; a test leg of a second shape (built only in the test) slots in behind the same question.
- **Visits:** a month step sends travellers whose destinations follow the weights (pull, reach, character) on a small
  test map; every traveller returns home; nobody leaves twice at once; a visit across a month step continues;
  departures spread over the month.
- **The journal:** an ended journey adds an entry; the 11th drops the oldest; a met NPC keeps 50; saved and loaded.
- **The clock:** days and months from ticks; the month step runs at a month's end; M jumps to the next month; the clock
  doesn't move in other scenes or while paused.
- **Starting power:** the spread's shares on the shipped map within a few percent of the shape; the same seed gives the
  same powers.
- **The date line:** its text for a few ticks in both languages; a click inside it pauses and neither walks nor
  selects; a click outside walks.
- **Names:** the same ID and seed give the same name; Chinese and English from one draw; spot-checked samples.
- **Click to walk and the cursor:** a click walks a route, a click on a walker selects, a movement press cancels; the
  cursor moves a tile per press and confirms a walk; replays of all of these play back with the same hashes.
- **Determinism:** a world-scene replay with travel and a month step passes with every hash matching.
- **Pictures:** the reference pictures of the world refreshed with travellers and a route line at fixed time; world
  shimmer measured again against its guard.
- **Fuzz:** the world map reader's fuzz target covers `profiles:` and `draws:`; a names reader fuzz target.
- **Measurements:** `measure`'s world run before and after; the month step with visits timed; the cost of placing
  travellers per frame; save size with a full journal.

## Left to the builder

The journey record's layout and storage (a pool of route tiles, cumulative ticks as offsets); the exact pull, reach and
character formulas and the travel share (within Goals 7-8 and the few thousand on the roads); the power threshold
between visitor kinds; `names.yaml`'s size and contents; the cursor's controls and look; the route line's look; how the
pause combines with today's focus pause; the measurement flags' names; commit split and order.

## Outcome

### Measurements (2026-11-17)

Taken by plan Task 19, with the display free and nothing else running: `pixi run project_n measure --runs world --keep`
fullscreen at 2560x1440 on the 165.1 Hz DP-3 display (Dell S2721DGF, the machine of 5a's and 5b1's sittings), 2 workers,
unpaced, one sitting for each build. Three builds, and the first is **not like for like**:

- **Main** at `d224d83` (a commit on main: the one this branch was rebased onto), no flags beyond `--camera-path`. Main
  has no traveller-count flag, so it ran with the shipped share and far fewer travellers: the stand-in walkers of 5a and
  5b1 (3,120 stand-in walkers in the busiest view).
- **The branch before the fix** (at "fix(app): pass the month step's Tracy message as a string view"), `--travel-share
  200 --month-step-at 15`: the busiest view holds 2,304 to 2,813 travellers, named, and a month step runs at 15 s.
- **The branch after the fix** (at "fix(gfx): keep the widest count from wrapping, its tests in a namespace"), the same
  flags, measured twice in a row.

Because the branch's world run now takes a month step at 15 s, its "widest view" segment is cut in two from this branch
on (Ruling R18b): "widest view" covers 12 to 15 s and "month step" the 15 to 18 s after it, whose first 0.5 s row is the
step's own timing. Main's "widest view: rest" is the whole 12 to 18 s (909 frames), the branch's two rows 414 and 413.

Times in ms. Each cell is main / branch run 1 / branch run 2 (the branch after the fix); *work* is the frame less its
wait for the next swapchain image. Rows of the rest of each segment (576 to 579 frames; widest view and month step 413
to 414 on the branch, 909 on main for the widest view):

| Segment | Frame median | Frame p99 | Frame worst | Work median | Work p99 | Work worst | GPU share |
|---|---|---|---|---|---|---|---|
| follow: rest | 6.05 / 6.04 / 6.05 | 7.79 / 9.30 / 9.30 | 8.46 / 9.83 / 11.06 | 0.72 / 3.14 / 3.11 | 2.45 / 6.21 / 6.35 | 3.07 / 6.73 / 8.86 | 86 / 48 / 47 % |
| pan: rest | 6.05 / 6.01 / 6.06 | 8.81 / 9.45 / 9.42 | 10.53 / 10.70 / 12.91 | 1.01 / 2.02 / 1.97 | 3.76 / 4.96 / 5.08 | 5.10 / 5.44 / 8.31 | 80 / 63 / 64 % |
| zoom in and out: rest | 6.05 / 6.03 / 6.06 | 9.12 / 10.73 / 9.84 | 9.35 / 16.60 / 15.92 | 1.02 / 1.55 / 1.45 | 3.99 / 6.76 / 6.35 | 4.20 / 16.58 / 12.39 | 78 / 65 / 68 % |
| widest view: rest (main 12-18 s; branch 12-15 s) | 6.04 / 6.06 / 5.98 | 9.22 / 9.87 / 9.83 | 9.72 / 13.26 / 10.79 | 1.41 / 3.80 / 3.81 | 4.40 / 7.43 / 7.09 | 4.92 / 9.30 / 8.39 | 73 / 37 / 36 % |
| month step: rest (branch only) | — / 6.05 / 6.07 | — / 9.37 / 9.96 | — / 13.21 / 11.54 | — / 3.72 / 3.56 | — / 7.02 / 6.97 | — / 10.90 / 8.92 | — / 37 / 39 % |
| labels: rest | 6.06 / 6.08 / 6.05 | 9.23 / 10.15 / 9.86 | 9.52 / 10.66 / 13.37 | 1.16 / 3.08 / 2.92 | 4.31 / 6.63 / 6.57 | 4.55 / 7.91 / 8.41 | 77 / 46 / 50 % |

The first 0.5 s rows (81 to 83 frames, no p99) in the same order, worst frame: follow 6.50 / 8.41 / 7.78, pan 8.11 /
17.97 / 15.02, zoom 8.92 / 9.08 / 9.91, widest view 9.35 / 15.23 / 14.27, labels 9.11 / 10.11 / 9.82. The branch's
first-0.5 s work worst in pan is 14.50 and 11.51 ms, against main's 3.01. The month step's first 0.5 s (71 and 64
frames, below) is the one row main has no counterpart for.

The other threads on the branch: logic p99 1.5 to 2.2 ms (main 2.3 to 2.9 ms) and workers 3
and 4 p99 1.0 to 1.8 ms (main 0.9 to 1.0).

**Summary of the world run against main.**

| | Main (d224d83, default share) | Branch before the fix | Branch after the fix (run 1 / run 2) |
|---|---|---|---|
| Frame median | 6.0 ms | about 6.0 ms | about 6.0 ms |
| Frame p99, rest segments | 7.8 to 9.2 | 11.5 to 15.7 | 9.3 to 10.7 / 9.3 to 10.0 |
| Frame worst outside the month step | 10.5 | 23.2 | 17.97 (pan, first 0.5 s; its work 14.5) / 15.9 |
| Hitches (work over 16.7 ms) | 0 | 8 | 2 / 3, every one in the month step's first 0.5 s |
| Input latency | 24 ms | 23 ms | 33 ms / 30.3 ms |
| GPU share (the median wait for the swapchain) | 73 to 94 % | 17 to 57 % | 26 to 78 % |
| World shimmer | 0.0436 | 0.0482 | 0.0482 (guard 0.06) |

The median held at the display's 6.05 ms refresh interval everywhere: the game still keeps pace, only the margin shrank.
That margin is what the travellers cost: main's work median was 0.7 to 1.4 ms, the branch's is 1.5 to 3.8 ms, and the GPU
share fell from about 80 % to about 45 %, the main thread now having more to do before it waits. Against 5a's lines, p99
moved far more than the 0.5 ms the plan allowed before taking a second run (work p99 4.4 on main to 7.4 on the branch in the
widest view), which is why every number above is from a pair of runs. Shimmer is the travellers' dots on the map: 0.0482
now, 0.0436 with the stand-in walkers, 0.0397 before the harbour reshaping, 0.0309 on the flat world; it stays under the
0.06 guard.

Kept captures: `build/measure/20261117-003212` (the branch at the fix head, a third run after the two above); the raw logs lived in `.superpowers/sdd/`
(gitignored) while the branch did.

### The `Make` hitches this measurement found

The branch's first run (before the fix) had 8 hitches over 16.7 ms, four of them outside the month step (two in `follow`, one in the widest view, one in `labels`), each led by the
`Make` zone (8 to 17 ms self time) with `MeasureStacks` (2 to 3 ms) and `EndFrame` (2 to 4 ms): p99 11.5 to 15.7 ms and a
worst frame of 23 ms. The user's ruling (2026-11-17): find the cost before accepting the numbers, test first.

**Root cause.** `Renderer::Impl::MeasureStacks` laid out a stack's text "{name} (+{count})" with `count` the number of
labels that stack itself, `labelInputs.size()`, though its comment meant only that number's digits. On the world map that
count changes by one or two in most frames, so every visible label's stack text was a string never laid out: in an
offscreen lavapipe run of the camera path at share 200 (1024x768), 258 of about 3,300 frames each made 1,100 to 2,400
new layouts (all `measureStacks`, none evicted or incomplete), 2.9 ms per 1,100 there. Side effect: the layout cache
swelled to about 146,000 entries before `LAYOUT_FRAMES` evicted them, and `EndFrame`'s `erase_if` over it took 2 to 10 ms
a frame. The defect was latent since labels began to stack ("feat(gfx): stack, prioritise and fade labels"); 5b2's thousands
of named labels exposed it.

**Fix** (Ruling R19a): "fix(gfx): measure label stacks by their count's digits, not its value" measures a stack with the widest
count of that many digits (9, 99, 999, ...) through a pure, tested helper (`WidestCount`), so the text changes only when the
label count gains or loses a digit. The digits are tabular, so widths are the same and no picture moved (reference pass,
shimmer unchanged at 0.0481786). A digit crossing still lays out every visible stack once (about 1,000 at 999/1,000, about
3 ms on the lavapipe run): once per crossing, not per frame. Its review fix (the widest count must not wrap at ten digits;
its tests in a namespace) is squashed into it before the PR.

**Before and after, at the same flags** (run 1 and 2 after): hitches 8 to 2 and 3, none outside the month step; rest-segment
frame p99 11.5 to 15.7 ms down to 9.3 to 10.7 and 9.3 to 10.0; worst frame outside the step 23.2 ms down to 17.97 and
15.9 (work 16.58 and 12.39, under the 16.7 line); `EndFrame` self time no longer in the hitch lists (mean 0.18 ms, max
1.8 ms in the kept capture). The median did not move.

### The month step with visits

The step's first 0.5 s row (the step fires at 15 s on the camera path, the widest view over the port) has 71 and 64
frames; its work worst is 56.89 and 55.70 ms (frame 60.02 and 55.73), and it holds the remaining hitches (work): run 1, 56.9 ms
and 25.3 ms; run 2, 54.3, 55.7 and 22.2 ms. (The unfixed branch had the same shape: 52.1, 60.8 and 25.7 ms.) The rest of
the step's segment is clean (work p99 7.0 ms, worst 10.9 and 8.9). Two causes, both kept (Ruling R19b):

- **The 54 to 60 ms frames** (two or three, around 17.3 s on the timeline) have small main-thread self times (SubmitFrame
  and PlaceTexts 1 to 3 ms): the main thread is waiting through the transition while the logic thread runs the step, which
  the spec lets hide behind the transition. In the kept capture `Travel::Month` took 12.0 ms for the one step.
- **The 22 to 25 ms frame with `Make` 13.4 to 18.4 ms self:** the step replaces the journeys, so the names in view are
  new and each is laid out for the first time, once per month, not per frame. Prewarming the new names' layouts (the glyphs
  are already prewarmed) would remove it; not done, as the step hides behind its transition (R19b).

The world's build seeds the travel of the month before the clock's and of the clock's own: `Travel::Seed` took 37.7 ms (in
the kept capture), once, at the scene's build, on the calling thread (Ruling R11a).

### The cost of placing travellers per frame

From the kept capture (the branch at the fix head, 1,456 publishes, about 2,300 to 2,800 travellers in the busiest view):
`Host::Publish` on the logic thread, which places the entities and the journeys in chunks over the 2 workers, has mean 1.36
ms and max 2.41 ms per publish. On the main thread `PlaceTexts` has mean 1.29 ms, `MeasureStacks` mean 0.50 ms (max 13.5
ms: the month step) and `EndFrame` mean 0.18 ms (max 1.8 ms). These are the whole of the new per-frame cost: the main
thread's work median went from 0.7-1.4 ms to 1.5-3.8 ms above, of which `PlaceTexts` and `MeasureStacks` are about 1.8 ms, every
visible traveller carrying a name. A traveller is a pure function of its journey and the clock, culled to the view, so
the cost follows the view and not the 100,000 people the world could hold.

### The save size with a full journal

Measured with a temporary doctest, deleted and never committed, run as `pixi run project_n test --suite game_tests --test
travel` in a debug build (the test's name matched `TEMP`), in two parts:

- **The journals.** A test world of `game::SpawnNpc` NPCs (the `travel_tests.cpp` test map and its `Journeys` helper), each
  sent on `JOURNAL_LENGTH` (10) visits that each ended, so its journal is full; the table's `Travel::Save()` bytes were
  read for 200 and for 400 NPCs, once with none of them met and once with all met (`MET_JOURNAL_LENGTH`, 50). The size is
  exactly linear: 96 bytes per NPC with a full journal of 10 (10 entries of 8 bytes, the NPC's id and a length: 19,216
  and 38,416 bytes for 200 and 400 NPCs, less the empty table's 16), 416 bytes per NPC with a full journal of 50 (83,216 and
  166,416). The test could not run at 50,000 NPCs: `Travel::Finish` merges the whole journal table on each call, which
  is quadratic when journals are built one journey at a time (fine at the month step, where thousands end together), so
  50,000 is the linear extrapolation: **4,800,000 bytes (about 4.6 MiB) for 50,000 NPCs none met, 20.8 MB if every one were met.**
  Only the NPCs the player has met keep 50, so the first is the realistic worst case.
- **The rest of the save.** A `Host` on the shipped world scene (`ShippedData()`, share 60, the 50,000 world NPCs) saved
  at its build: **6,618,616 bytes**, of which the travel part is 4,468,183 (5,145 journeys held, under way or waiting to leave: the two months the
  build seeds, and the journals of 135 NPCs whose seeded journeys had already ended), so everything else is about 2.15 MB.
  Replacing those 135 journals (at most 12,960 bytes) with every NPC's full journal gives **about 11.4 MB** for the
  whole world save with a full journal everywhere (11,405,656 to 11,418,616 bytes).

The journal is the larger part of a late-game world save, but it is bounded (10 entries, 50 for the few the player has met).

### Journeys underway, the measured crossing time and the tuned values

- **Journeys underway.** Task 11's tuning gave 2,525 to 3,262 journeys under way across the first three months at share 60
  (counted on the shipped map with the state's 50,000 NPCs). At share 200 (`--travel-share 200`, which `measure` passes
  from this branch on) the whole-map count wasn't recorded; what was measured is the busiest view, 2,304 to 2,813 members
  placed round the port at the widest zoom (the busiest-view test, `shipped_world_tests`), where the shipped 60 gives only
  650 to 884. The flag exists because of that: the spec's Goal 18 asked for at least 2,000 in `measure`'s widest view.
- **The crossing time** (Ruling R11b). Walking the map on foot takes 50 days from the sect to the isle and 30 days from the
  capital to its farthest city, at the spec's fixed pace of 16 ticks per cost unit (a road tile, cost 4, 64 ticks) and
  180 ticks a day. The brief's "about a month" was downstream of the spec's own number (the capital to the port about 19 days),
  and the pace is not a `travel:` value, so it stayed; a faster pace is a data change in `world.yaml`, and hashed.
- **The tuned `travel:` values** (`assets/data/world.yaml`): `share_per_mille: 60` (6 % of a city's people set out each
  month: about 2,500 to 3,300 of the 50,000 on the roads at once from the first tick), `stay_days: [2, 5]` (the spec's
  call, a visit about a week), `reach_base: 300` and `reach_per_power: 20` (cost units: a mortal of power 0 to 5
  reaches the next town, whose nearest costs about 130 from the capital and 225 from the sect, and 300 + 20 x power
  gives the strong, power 50 to 500, the far side of the map: the sect to the isle costs 562), and `cultivator_power: 5`
  (the threshold between traders and cultivators, which with the starting spread of about 80 % mortals, 18 %
  cultivators and 2 % strong gives each city's draw a mix of both kinds from the first month). They were tuned against
  Task 11's counts, with the spread of starting power drawn in Task 3.

### Input latency

23 to 24 ms on main and on the branch before the fix, 33, 30.3 and 31.1 ms (the kept capture) after it. It is one sample
per run, never a gate, and no cause was looked for: recorded as observed, an open question for the next `measure`, with no
explanation offered.

### Rulings made while building

The plan ledger's calls, each with its reason and what it costs if wrong. Commits are named by subject, never by hash.

**Before the plan ran (pre-flight).**

- **Task 7's test of a leg's shape (P1):** a journey places its legs exactly as `Where` of the leg does replaces the spec's
  "a leg of a second shape built only in the test", since `LegShape` is a closed enum that needs a test-only hook in
  production code; the guarded rule (a leg is reached only through `Where` and `EndTick`) is what the test holds. Cost: one
  extra test later.
- **The names reader's fuzz coverage (P2):** a seed in the existing `yaml` target, the repo's pattern for every data reader.
  Cost: a small extra target.
- **Wave tasks on different lines of one AGENTS.md run in parallel (P3);** conflicts are resolved at rebase. Cost: a manual
  rebase resolve.
- **Each task regenerates the save fuzz seeds and hashed expectations its change moves (P4),** and again on the rebased head.
  Cost: a stale seed, caught by the fuzz smoke run in `test`.
- **Task 15 follows Task 14 (P5);** both edit `game/host.cpp`. Cost: one wave of wall-clock.

**Profiles, names, power, the calendar (wave A).**

- **`core::MOST_NAMES` is 1,024 per table (R2a),** not the plan's 4,096: a name key is a u32, distinct names need distinct keys
  and three 10-bit parts with a no-second flag fit in 31 bits. Cost: a u64 name key through `frame.hpp` and the labels.
- **`ParseNames` has a `warnings` out-parameter like `ParseWorldMap` (R2b):** a typo in hashed data must be seen.
- **Walkers orbit while the world is paused (R4a),** interim: Task 15 removes the orbit walkers. Cost: a paused world showed
  moving stand-ins for a few tasks.

**The map's profiles and routes (wave B).**

- **A passable cost of 0 is refused at the source (R6a):** cost is time, a zero-tick step breaks `RouteDown`'s descent and
  would divide by zero in `Where`. Cost: one refusal to relax.
- **A named label's key has bit 31 set (R8a):** `LabelKey` keeps names apart from `StringId` keys, so a named label never
  stacks with a plain one; the spec's "different names never stack" outranks the plan's literal "`NameKey` as `nameKey`".
  Cost: one constant.
- **`core::TICKS_PER_DAY` lives in `core/calendar.hpp` (R7a)** and the host's constant derives from it: one definition of
  the day. Cost: a rename.

**Journeys, visits, the date line (waves C and D).**

- **`Travel::Finish` takes `core::State&` (R9a),** not const: it sets members' actions back to Rest. Cost: none (a signature).
- **`Travel::Seed` takes no `core::Jobs&` (R11a):** it runs `Month`'s chunks in order on the calling thread through
  `SceneContext::SeedTravel()`, because the world build has no job system; the chunk-order merge keeps the journeys
  identical. Cost: threading `Jobs` into scene builds later.
- **The crossing is 50 days (R11b),** see above.
- **Seeding uses state counter 0 (R11c):** a build has no counters; months still differ by the month's first tick in the key.
  Cost: a key change, seeds regenerated.
- **Travellers are not kept across scene builds (R11e, over R11d):** `Host::Build` replaces the whole state on every scene
  build (power, needs and relationships too, known since 5b1), so keeping only travel would name NPCs the state lacks;
  "travellers resume where they were" narrowed to "the route is gone, the clock stood still". Persistence across scene
  builds is a state refactor of its own (sweep-up backlog, the chunk that first enters a town scene). Cost: the user wants
  persistence sooner and it becomes its own plan.

**Travellers on the map, walking, input (waves E to G).**

- **`LabelPriority::Selected` is used as it is (R13a),** no new enum value: it already outranks `Place`.
- **The player's cultivator always draws above every traveller (R13b):** accessibility first, the foot-y order made the
  player vanish on the first screen. Cost: one draw-order change.
- **The shipped-world 1-vs-4 case sits in `shipped_world_tests.cpp` (R13c);** **reference cases prewarm name glyphs only for
  world scenes (R13d):** only they draw NPC names (about 1.7 s a case saved).
- **A walk-to to the cultivator's own tile alone ends any route and starts none (R14a):** a one-tile leg would jump the foot
  or be a zero-step leg. Cost: a short walk to the centre later.
- **Task 14 tests a month step under a route for the player (R14b)** (the plan's Review Focus named it, its steps omitted it).
- **Held movement cancels the route while paused too (R14c):** a paused player calling off a click must be able to. Cost:
  one condition.
- **`host.cpp` gets no extraction yet (R14d):** the cursor is `app/` and Task 15 shrinks `host.cpp`. Cost: a later split into
  `game/player_route.*`.
- **The busiest-view test counts journey members in view at about 20 named ticks (R15a),** asserting the lowest count once, at
  the share `main` uses.
- **"A click on a walker selects it" is "a click on a traveller selects it" (R16a):** the stand-ins are gone.
- **`InputCode::KeyP` and `PadBack` are added in `window.*` (R16b),** and KeyT, KeyEnter, the gamepad's A and Y and the d-pad
  (R17a): the bindings can't exist without the codes. Cost: none.

**The cursor and the month-step flag (waves H and I).**

- **The d-pad is four `Control::Move` bindings (R17b)** so it steps the cursor and walks freely alike; **the cursor toggles on
  T and Y (R17c);** no separate cancel (Escape is Quit, B is free for chunk 10's menus).
- **The cursor shows only on the world map (R17d)** and hides when a click walks or selects; a Confirm with none shown does
  nothing.
- **A stick axis past 0.5 counts as a press (R17e);** **a long frame steps the cursor at most once (R17f)** so a hitch can't
  overshoot; **after a confirm the held walk stays zero until Move is released once (R17g)** (the two-thumb grip); showing
  the cursor again before the direction is released clears that latch (R17h, kept: two deliberate presses).
- **The flag is `--month-step-at SECONDS` (R18a),** refused without `--camera-path` and past its end (R18c); **a step exactly
  at the path's end is accepted (R18d):** `CameraPathSegment` counts `seconds <= end`. **The month-step message splits the
  "widest view" segment on purpose (R18b)** so the step's frames are isolated; documented in `tools/AGENTS.md`, covered by
  a `measure_segments` self-test. Cost: one constant to exclude the message.

**The `Make` fix.**

- **R19a (the widest count by digits) and R19b (the month step's two hitches stay),** as above. R19b's cost if wrong:
  prewarm the step's new names' layouts.

**Tooling fix found by the measurement.** Task 18's `PN_PROFILE_MESSAGE("literal")` did not build in `releasedbg` (the
macros are empty elsewhere, so `test` and `lint` passed): "fix(app): pass the month step's Tracy message as a string view"
and a `src/AGENTS.md` gotcha.

### Rulings made in the comment cleanup (run on this branch before wave C)

The user's rule (code comments never cite the design process; plain comments only explain why) was applied to the whole
tree by a six-task plan, `2026-11-16-code-comment-cleanup.md`; its calls, with their reasons:

- **F1:** one owner per `src/*/module.dox` (Task 3). **F2:** only Task 5 commits `src/*/generated/` (the template owner).
  **F3:** `pixi.toml`'s comments are code too (no pin or lock change). **F4:** every pass also catches design-doc section
  marks written as `S<digits>`. Cost if wrong: none to a regenerate or three rewords.
- **H1:** in a `.cpp`, a plain comment directly above a function, method, helper type or lambda-holding variable that states
  its contract becomes `///`, worded as before; one that only restates the body goes; comments inside a body stay only if they
  give a why (also in HLSL). Cost: a mechanical revert of `///` to `//`.
- **H2:** a comment saying what a constant or data table holds is that name's documentation and stays. Cost: a few comments.
- **U1:** "the user's" is flagged only as a pointer into the design process (tuned, call, rule), never as a role.
  **U2:** "the user's choice" and "the user's value" stay unflagged (role uses can't be told apart). Cost: a pointer with
  those nouns slips past `lint` to review.
- **The `(O6)`-label pattern was dropped:** a bare "(O2)" or "(d6)" in prose is indistinguishable from a sensor name or
  dice; `lint` flags `§`, `S<digits>` after a document number, the "design process" words and "the user's" pointers instead
  (`design_references` in `tools/project_n.py`), and a section-heading comment over obvious code goes.
