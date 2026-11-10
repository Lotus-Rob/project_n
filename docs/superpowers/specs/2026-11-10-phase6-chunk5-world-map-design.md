# Phase 6, chunk 5: the world map

Date: 2026-11-10 · Status: approved 2026-11-10 · Phase spec: `2026-11-10-phase6-design.md` · New: no earlier design

## In short

- **A hand-made world map** inspired by Tale of Immortal: tiles of plains, forest, mountains, snow, desert, swamp, rivers,
  lakes, coast and sea, with roads, **about six cities and towns** and **about twenty danger sites**, about **20 screens
  across** at zoom 1, too big to see at once (the widest view is about a fifth of it across).
- **Fog of war:** a radius around the player uncovers tiles for good; out of sight, explored tiles show their land but
  not who's on them.
- **Time flows:** about a day every few seconds; NPCs **travel between cities day by day** on their life simulations,
  drawn as walkers with **generated names** (characters in Chinese, pinyin in English).
- **Click to walk, pan and zoom freely,** a key to snap back; walking onto a city or danger tile **prompts to enter** its
  scene (towns in chunk 6, combat in chunk 7).
- **The phase's measurements are built here:** frame time along a scripted camera path, hitches, input latency,
  shimmer, recorded per chunk from now on.

## Intent

The world map is where the demo world is largest and busiest, the stress test for the camera and culling, and the hub
every other scene hangs off. It should feel alive (people travelling the roads as days pass) and stay perfectly
smooth however far you pan or zoom. It's also where the phase's smoothness and performance get measured, so every
chunk after it can show it didn't regress.

## Goals

1. **The map:** hand-made for the demo, as tiles: plains, forest, mountains, snow, desert, swamp, rivers, lakes,
   coastline and sea; roads linking the cities; about six cities and towns (each entering one of chunk 6's three town
   scenes) and about twenty danger sites of varied kinds (dungeons, haunted forests, mountain passes, shrines, corrupt
   towns, cliffs; each entering one of chunk 7's combat scenes). About 20 screens across at zoom 1; the zoom range stays
   0.25 to 2, so the widest view is about a fifth of the map across.
2. **Moving:** click a tile and the player walks there along a path; the camera follows by default; drag or push the
   screen edge to pan freely; zoom with the wheel; a key snaps the camera back to the player.
3. **Fog of war:** unexplored tiles lie under cloud; a radius around the player uncovers them for good as it travels;
   explored tiles out of sight show their terrain and landmarks but not the NPCs on them. **Walkers are seen only within
   the sight radius**, which future stats will grow.
4. **Time:** flows continuously on the world map, about a day every few seconds; the month step comes when a month's days
   run out (a designed pause, behind its transition screen).
5. **Travelling NPCs:** the 50,000 world NPCs **belong to the cities and towns and their surrounding regions** (today
   they're spread evenly over the map's cells); they keep their monthly life simulations, which gain a **travel**
   decision: those it sends to another city **walk there day by day**, preferring roads, drawn as walkers within the
   player's sight. Fuller life simulation (work, trade, sects) and small non-enterable landmarks (labourers' houses, job
   sites, tiny villages with NPCs assigned) are future work.
6. **Names:** every NPC has a name generated from its seed (a surname and a given name from tables of common Chinese
   surnames and name characters), shown in the player's language: characters in Chinese, pinyin in English. Labels
   follow chunk 3's rules (scaling, fading, stacking, priority).
7. **Clicking a walker** selects it (label priority, outline); its details panel comes with the view models (chunk 9).
8. **Entering, for real:** walking onto a city or danger tile prompts to enter (game text over the tile until the
   screens chunk's dialog); entering loads its scene; leaving returns to the world map on that tile. Until chunks 6 and
   7, **danger tiles enter today's arena and city tiles a placeholder town**, each with a way back (an exit, and a key).
   This needs the **world kept alive behind the entered scene** (below).
9. **The measurements** (the phase bar), built here and recorded by every chunk after:
   - a **scripted camera path** over the world map (follow, pan, zoom in and out, at the widest view over the busiest
     region, with the debug sight radius so thousands of walkers and labels are on screen), recorded in releasedbg with
     Tracy: frame-time percentiles and the worst frame per thread, the job system pinned to 2 workers;
   - **the GPU's share,** by a method this chunk chooses and records (SDL_GPU has no timestamp queries);
   - **hitches:** any frame over the budget outside the designed pauses, listed with what it was doing;
   - **input latency:** from a click or key to the first frame showing it;
   - **shimmer:** pictures at several zooms and sub-pixel camera offsets, compared frame to frame;
   - each as a `project_n` command on the development machine; this chunk sets the budget numbers from a first
     measurement and records why.

## Non-goals

- Procedural world generation (future: real-world terrain techniques weighted for xianxia geography: seas, mountains,
  lakes).
- Town and combat scenes themselves (chunks 6 and 7); random encounters while travelling (chunk 7 decides).
- The NPC details panel and roster (chunk 9); the main menu and the enter dialog as UI (chunk 10).
- Deeper life simulation: jobs, trade, sects, relationships beyond what exists.
- A whole-map view or a minimap.

## Anti-goals (a failure even if it works)

- **Camera jitter or stutter** while following, dragging, edge-panning or zooming.
- **Shimmer** of tiles, icons or walkers at any zoom or sub-pixel offset.
- **Popping:** walkers or labels appearing at the edge of the view, or at the fog's edge, from one frame to the next.
- **Hitches** when the camera reaches new ground, when many walkers come into view, or as days pass (only the month
  step may pause).
- **Lag** from a click to the player setting off.
- **The world feeling dead:** roads without travellers.

## Design

### The map as data

- The map is a hand-made tile grid in `assets/data/` (its format the builder's: an image whose colours are tile kinds,
  or text), with a table of tile kinds (terrain, cost to walk, which scene a site enters) and the places: each city and
  danger site's tile, kind, name (string IDs, both languages) and scene. Read and checked by the `data` module like the
  other tables.
- Today's `cells` scene grows into the world map; its sprites become the tile set (placeholder art, far more varied).

### The camera and culling

- The main thread owns the camera (follow, drag, edge pan, zoom, snap back). Each frame it sends the logic its **view
  rectangle plus a margin** (the 0002 §7.2 world-map case); the logic publishes only what lies inside it. The rectangle
  changes what's published, never the state, so like `Subscribe` it's **not recorded** in replays and the state hash
  ignores it. The margin is wide enough that a fast pan or zoom never shows the edge of what was published (no popping).

### Time and travel

- The world map's clock advances a day every few seconds of real time (a setting for later; a constant now). A month's
  days running out triggers the month step as today.
- **Travel:** when a world NPC's monthly simulation sends it to another city, it gets a route (tile path, roads
  preferred) and walks it over the month's days; its position is a function of its route and the day's fraction, so it
  moves smoothly at any frame rate. Walkers are drawn only where the player can see them (inside the view and outside the
  fog). The player walks the same way.

### Fog of war

- A per-tile explored flag (saved state) and a sight radius around the player; drawn as cloud over unexplored tiles,
  with a soft edge that eases as tiles uncover (no pop).

### Names

- Generated once per NPC from its seed (deterministic, saved with it): a surname and a given name chosen from tables in
  `assets/data/` (characters with their pinyin), so both languages come from one choice.

### Entering scenes: the world stays alive

- **Today a scene change throws the whole state away** (`game::Arena::Build`) and a save holds one scene. This chunk
  splits the state in two: **the world** (the 50,000 NPCs, the fog, the calendar, the player's tile and travel) lives on
  for the whole game, and **the active scene** (the world map's view, a town, a combat scene) is swapped as the player
  enters and leaves. A save holds the world and the active scene; the save version goes up.
- Stepping onto a place's tile shows the prompt; confirming starts the scene load (the existing scene protocol and its
  loading screen); leaving returns the player to the world map on that tile, time having stood still while away (my
  call, below).
- Stand-ins until chunks 6 and 7: every danger tile enters today's arena (a combat scene since chunk 4); every city tile
  enters a placeholder town (a small empty scene with an exit).

### The measurements

- A `project_n` command (name the builder's) runs the game on the world map with a scripted camera path and records a
  Tracy capture, then prints the frame-time percentiles, the worst frames, the hitches and the latency, and the shimmer
  comparison. Each later chunk records its numbers in its spec's Outcome.

## Decisions

The user's, 2026-11-10:

1. **The flow:** main menu (Start, Load, Settings, Exit; with the screens chunk) → world map → towns and combat scenes.
2. **The world map is inspired by Tale of Immortal's:** tiles, fog of war, city and danger tiles you enter.
3. **The world map carries the scale:** about 20 screens across, too big to see at once (several widest views across).
4. **Click to walk; the camera follows, pans freely, zooms; a key snaps back.**
5. **Fog of war:** uncovered for good around the player; out of sight, terrain shows but NPCs don't.
6. **Time flows continuously,** about a day every few seconds.
7. **NPCs travel between cities** day by day on their life simulations; fuller simulation is future work.
8. **Generated names:** characters in Chinese, pinyin in English.
9. **Hand-made map** for the demo; procedural generation later.
10. **Content:** varied terrain, roads, about six cities, about twenty danger sites.
11. **Entering asks first** (a prompt).
12. **The world-and-scene restructure happens here**, with stand-in scenes until chunks 6 and 7.
13. **Walkers only within the sight radius,** which future stats grow.
14. **NPCs belong to cities and towns and their regions;** small landmarks with NPCs come later.

## Calls made here (not the user's; check them)

- **The zoom range stays 0.25 to 2;** the widest view is about a fifth of the map across.
- **The view rectangle isn't recorded** in replays (it changes what's published, not the state).
- **Travel share:** the travel decision sends enough NPCs each month that **a few thousand are on the roads at once**
  (the user named no number).
- **A debug sight radius** as large as the view, so the measurements can draw and label thousands of walkers at once;
  normal play shows only those within the player's sight. Cities' resident NPCs aren't drawn on the map.
- **The arena stays on F2** as a test scene too, beside `stress` and `painted`; the game starts on the world map.
- **Time stands still** while the player is inside a town or combat scene (it flows only on the world map), until
  gameplay decides otherwise.
- **The enter prompt is game text** until the screens chunk (10) gives it a dialog; it names its confirm and cancel
  controls with the device's prompts, and the gamepad's A and B answer it (phase decision 18).
- **Today's `cells` scene becomes the world map;** the `stress` and `painted` scenes stay as test scenes on F2 for the
  benchmark and the reference pictures.

## Testing

- **The map data:** parsing and refusals (an unknown tile kind, a place off the map, a place without a scene).
- **Travel:** a route prefers roads; a walker's position along it is smooth and deterministic; a month's travel ends in
  the destination city.
- **Fog:** tiles within the sight radius uncover and stay uncovered; saved and loaded.
- **Names:** the same seed gives the same name; both languages from one choice.
- **Culling:** what's published lies within the rectangle plus margin; a replay recorded while panning plays back with the
  same hashes.
- **Entering:** stepping on a place prompts; confirming loads its scene; leaving returns to its tile with the world as
  it was (the NPCs, fog and calendar untouched while away); a save made inside a town or the arena loads back into it
  with the world intact.
- **Sight:** walkers outside the sight radius aren't published; the debug radius shows them all in view.
- **Pictures:** the world map at several zooms with fog and walkers, at fixed time.
- **Measurements:** the first run's numbers recorded as the phase's baseline.

## Left to the builder

The map's format and its exact layout; the tile size in world units (so the map is about 20 screens across); the sight
radius; the day's length in seconds; the culling margin; the GPU measurement method; the budget numbers; commit split
and order.
