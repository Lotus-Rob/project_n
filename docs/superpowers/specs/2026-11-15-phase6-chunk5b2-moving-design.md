# Phase 6, chunk 5b2: moving on the world map

Date: 2026-11-15 · Status: approved 2026-11-15 · Plan: `2026-11-15-phase6-chunk5b2.md` · Chunk spec: `2026-11-10-phase6-chunk5-world-map-design.md` · Phase spec:
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
