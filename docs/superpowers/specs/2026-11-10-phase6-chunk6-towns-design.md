# Phase 6, chunk 6: towns

Date: 2026-11-10 · Status: approved 2026-11-10 · Phase spec: `2026-11-10-phase6-design.md` · Follows: chunk 5
(`2026-11-10-phase6-chunk5-world-map-design.md`) · New: no earlier design

## In short

- **Every city on the world map is generated** from its own seed: a square or circular outline with walls and gates,
  main roads from the gates to a market square, streets and alleyways, and plots filled with houses, shops, inns,
  markets and sect and guild halls. The same city comes back every time; the save never stores its map.
- **Three sizes:** a town (1–2 screens across, about 50–80 people), a small city (2–3 screens, about 150) and a large
  city (about 4 screens, a few hundred).
- **The people are the world's own:** the NPCs chunk 5 gives each city, now with **a household, a job or a sect or
  guild**. Each has a home and, if it has one, a workplace or hall in the generated city.
- **Busy, not simulated:** the world's clock stands still while you're in town, but the streets never do. Shopkeepers,
  innkeepers and traders keep their posts; everyone else goes between home, work, the market, the inn and their hall,
  browses, chats and trains.
- **Walk, select, talk:** click to walk the streets; click an NPC to select them; talk to see their name and a greeting
  for their role. Walk out of a gate to return to the world map.
- **The failure to avoid,** in the user's words: towns that feel **small, stale, not lived in, boring, empty or
  "placeholder"**.

## Intent

Towns are where the world's people live. The world map shows them as travellers on the roads; a town shows them at
home: streets full of people with families, work and sects, in a city that looks built up over time rather than laid
out by a program. The demo's job is the **map, its generation and the people assigned to it**. The buildings' gameplay
(shops, sects, the adventure guild, quests) comes later and will slot into the places this chunk generates.

## Goals

1. **Generated cities:** each city on the world map generates its scene from a seed (the world's seed and the city's
   place ID), deterministically, on every platform: the same streets, buildings and assignments every visit. Leaving
   and returning changes nothing; saves don't store the map.
2. **Realistic layouts** (the user's: "try and follow more realistic layouts"):
   - an outline, a **square or a circle** for the demo (varied shapes later), with a wall and **gates**;
   - **main roads** from each gate to a central **market square**; **streets** branching from them; **alleyways**
     filling the blocks between;
   - **plots** along the streets, with buildings chosen by where they are: shops and inns along the main roads and the
     square, market stalls in the square, houses on the back streets and alleys, sect and guild halls on large plots of
     their own with a courtyard;
   - open ground, trees, wells and small yards where blocks leave room.
3. **Three sizes, set in data:**

   | Size | Across | People in the scene | Gates |
   |---|---|---|---|
   | Town | 1–2 screens | about 50–80 | 2 |
   | Small city | 2–3 screens | about 150 | 3–4 |
   | Large city | about 4 screens | a few hundred (300–500) | 4 |

   Chunk 5's six or so cities each have a size; each generates its own layout, so the demo has six different towns.
4. **The people:** world NPCs gain, in the world state and saved:
   - a **household** (a family sharing a home, generated with the NPC's region so families live together);
   - a **role:** a job (shopkeeper, innkeeper, market trader, labourer, …) or the life of a **cultivator** who belongs
     to a **sect or guild**;
   - sects and guilds are **world records** (a name, generated like NPC names, and a home city).
   Entering a city assigns each household a house and each worker a building of the matching kind, by a deterministic
   rule from the city's generation, and every sect or guild homed there a hall.
5. **Who is in the scene:** the city's role holders always (every shop, inn and stall has its keeper), plus a
   deterministic choice of the city's other residents up to its size's number. They become active NPCs for the visit,
   keeping their stable IDs (the existing tiers), and fold back into the world when you leave.
6. **Busy streets while time stands still:** the world clock, the calendar, travel and the monthly life simulation stop
   in town (chunk 5's rule, which the user confirmed). The crowd runs on **staged activities** (the user's: "more
   artificial and less living world simulated"):
   - role holders stay at their posts (behind the counter, at the stall, at the inn door), with small idle motions;
   - everyone else picks activities that fit their role and the city: go home, go to work, browse the market, sit at the
     inn, chat in small groups, train in their sect's courtyard, wander the streets; walk there along the streets, stay
     a while, choose again;
   - going "inside" a building: walk to its door and fade out; come out later the same way.
7. **Walking and the camera:** as on the world map: click a point and the player walks there along the streets; the
   camera follows, pans, zooms, snaps back; chunk 5's view-rectangle culling applies.
8. **Select and talk:** clicking an NPC selects it (outline; label priority, chunk 3). **Talking** (clicking a selected
   NPC, or a talk key) shows a **speech bubble** over them: their **name** and a **placeholder greeting** for their role
   (a few lines per role in the strings table, both languages). The NPC stops, faces the player while the bubble
   shows, then carries on.
9. **Leaving:** walking out through any gate returns the player to the city's tile on the world map (chunk 5).
10. **Varied enough not to look placeholder:** the placeholder art (`art/placeholders.py`) grows a city set with many
    variants: houses of several sizes and roof colours, shopfronts with awnings and signs, inns with lanterns, market
    stalls, sect and guild halls with gates and banners, walls and gatehouses, ground (paved roads, dirt streets,
    alleys, the square's flagstones, grass), and props (lanterns, carts, crates, barrels, wells, trees, benches). NPCs
    get variants by role (a cultivator's robe in their sect's colour, a trader's apron, …).

## Non-goals

- Shop, inn, sect, guild and quest gameplay; building interiors (later chunks or phases; this chunk leaves the places).
- Real dialogue; the NPC details panel (chunk 9, which can then show household, role and sect).
- Irregular city outlines, cities shaped by the terrain (rivers, hills, coast), and districts by wealth or trade.
- A day and night cycle, or routines following the world's clock.
- Combat in towns (combat stays in combat scenes, chunk 7).
- Small non-enterable landmarks and villages (future, with the world map).

## Anti-goals (a failure even if it works)

The user's, for towns:
- **Small:** a large city that fits on a screen or two, or streets so wide the town feels sparse.
- **Stale:** the same building repeated down a street; every NPC doing the same thing; NPCs moving in step.
- **Not lived in, empty, boring:** quiet streets, a market without traders or buyers, halls without members,
  bare plots.
- **"Placeholder":** a grid of identical boxes; layouts that look generated rather than grown.

And from the phase and the world map:
- **NPCs walking through walls or buildings,** sliding, or jittering as they turn or meet.
- **Camera jitter, shimmer, popping, hitches** in the densest street at any zoom; a hitch beyond the scene load when
  entering (generation is part of the load, a designed pause, on worker threads).

## Design

### The city generator

- A pure function from a seed and the city's size data to a **city layout**: the outline, walls and gates; a street
  graph (main roads, streets, alleys) with widths; plots; a building or prop per plot with its kind and variant; and the
  ground tiles. Integer and fixed-point arithmetic (or the project's deterministic random numbers) so every preset
  gives the same bytes.
- **The layout steps** (the approach is the builder's; this is the shape): outline and gates → main roads from the gates
  to the square → secondary streets subdividing the large blocks → alleys through the deep ones → plots along every
  frontage → building kinds by frontage (main road, square, street, alley) and plot size → variants chosen so neighbours
  differ → props and greenery in the leftovers.
- **Runs on a worker thread** during the scene load (the phase rule for designed pauses); its time is recorded per size.
- The layout also gives the **walk graph** the crowd and the player path on (streets, alleys, doors, the square, hall
  courtyards), so nobody crosses a wall or building.

### People in the world

- `Npc` (or components beside it) gains a household ID, a role and, for cultivators, a sect or guild ID; households,
  sects and guilds are world records. All of them are generated with the world, deterministic, saved; the save version
  goes up (chunk 5 has already raised it once).
- Chunk 5's assignment of NPCs to cities and regions decides who can be in a city; this chunk decides their roles.
- **Townsfolk and region folk:** of a city's NPCs, only enough households to fill its houses live **inside the walls**;
  the rest (most of them, as about 50,000 share six cities) live in its **region**, outside. The size data says how many
  of each building kind a city has; world creation gives the city exactly that many keepers and halls' members and that
  many town households, and the generator places exactly those counts. Both read the same size data, so neither waits on
  the other, and "every shop has its keeper" holds by construction.
- **Fixed for the demo:** households, roles and memberships don't change after world creation. **Keepers and their
  households don't travel** (chunk 5's travel decision skips them), so a shop never loses its keeper; other townsfolk
  may travel, and a visit's crowd is the townsfolk at home that day plus any travellers stopping there.
- **Sects and guilds** are one kind of world record with a kind (sect or guild), a generated name, a colour (robes are
  tinted, not one sprite each) and a home city; members live in their home city, so "train at the hall" always has a
  hall to go to. Each city hosts a few by size (about 1 in a town, 2–3 in a small city, 4–6 in a large one). The
  adventure guild and its gameplay come later.

### The visit

- **Entering:** the load generates the layout, assigns homes, workplaces and halls, picks who is in town, and makes them
  active NPCs at plausible starting places (keepers at posts, others spread over homes, streets and the market), so the
  first frame is already busy.
- **The crowd** runs in the logic as active NPCs (deterministic, replayable, saved with the active scene): each picks a
  staged activity from its role's list, walks the walk graph to the place, stays, and picks again. Decisions are
  time-sliced as active NPCs' are today. Light separation keeps people from walking through each other.
- **Saving in town** saves the world and the active scene (chunk 5); loading regenerates the layout and restores the
  crowd as it was.
- **Leaving** folds the visitors back into the world (stable IDs kept) and returns to the world map.

### Drawing

- Ground as tiles, through the same path as the world map's tiles; buildings, props and NPCs as sprites sorted by their
  foot (the existing sorting), so people pass in front of and behind buildings correctly.
- Labels, selection outlines and speech bubbles follow chunk 3.

## Decisions

The user's, 2026-11-10:

1. **Cities are generated,** with roads, streets, alleyways, houses, shops, sects and guilds, inns and markets.
2. **NPCs have lives:** households, jobs and work, or cultivators belonging to sects and guilds, going about their day.
3. **The demo is the city map, its generation and the NPCs assigned to it;** buildings' gameplay comes later.
4. **The world's time stands still in town,** but NPCs stay busy with staged activities, "more artificial and less
   living world simulated"; role holders such as shopkeepers keep their posts.
5. **Outlines are squares or circles** for the demo, with realistic layouts inside; varied shapes later.
6. **Large and living:** the largest city several screens across, with a few hundred NPCs.
7. **Talking shows the NPC's name and a placeholder greeting.**
8. **Anti-goals:** small, stale, not lived in, boring, empty, "placeholder".

## Calls made here (not the user's; check them)

- **Sizes:** town 1–2 screens and 50–80 people; small city 2–3 screens and about 150; large city about 4 screens and
  300–500.
- **Every world-map city generates its own layout** (six towns rather than three fixed ones), sized town, small or large.
- **The crowd is the world's NPCs,** made active for the visit: the people you click are the ones the simulation tracks.
- **The crowd runs in the logic** (deterministic, saved and replayed) rather than as a cosmetic layer on the main
  thread, so clicking, saving and replays treat it like any other NPC.
- **Talking:** a speech bubble as game text until the UI chunks; the NPC stops and faces you; greetings vary by role.
- **No interiors:** NPCs going inside fade at the door.
- **Leaving** is by walking out of any gate.
- **Sects and guilds are world records** with generated names and a home city.
- **A debug key regenerates the current city from a new seed,** to check variety by eye (the stale and placeholder
  anti-goals): same building counts and people, a new layout; not saved.
- **Townsfolk and region folk:** only enough households to fill a city's houses live inside it; most of its NPCs live in
  its region.
- **Building counts come from the size data**, which also sets the roles at world creation.
- **Keepers and their households don't travel;** roles and memberships are fixed for the demo.
- **Sects and guilds:** one record kind; members live in the home city; about 1, 2–3 and 4–6 per town, small and large
  city.

### Building on chunk 5

Chunk 6 is planned when it starts, after chunks 4 and 5 merge (each chunk is planned when it starts), so its plan names
chunk 5's world/scene split, places table, NPC assignment, tile path, culling and measurement commands as built.

## Testing

- **The generator:** the same seed gives the same layout on every preset (a hash compared in tests); every plot reaches
  a street; every door is on the walk graph; no building overlaps a street, wall or another; each size's dimensions and
  counts fall in its range; neighbouring buildings differ in variant.
- **Assignment:** every household has a house, every keeper a building of its kind, every sect or guild homed there a
  hall; the same seed gives the same assignment; a city never has a shop without a keeper.
- **The crowd:** activities only reach places on the walk graph; nobody's path crosses a building or wall; a replay
  recorded in town plays back with the same hashes; save in town, load, and the crowd and layout are as they were;
  leaving and re-entering gives the same city, and the world's calendar hasn't moved.
- **Talking:** the bubble shows the name and a greeting of the NPC's role in the chosen language.
- **Pictures:** each size at a fixed seed, zoomed out and in, against `tests/reference/`.
- **Measurements:** chunk 5's commands gain a camera path through the large city's busiest streets; frame time,
  hitches and the generation's time recorded in the Outcome.

## Left to the builder

The generator's algorithm within the shape above; tile and building sizes in world units; the walk graph's form and
the path search; the activity lists per role and their timings; the separation method; the number of greeting lines;
how the scene's people are chosen beyond the rule above; commit split and order.
