# Phase 6, chunk 7: combat scenes and dungeons

Date: 2026-11-10 · Status: approved 2026-11-10 · Phase spec: `2026-11-10-phase6-design.md` · Follows: chunks 4
(abilities), 5 (world map) and 6 (towns) · New: no earlier design

## In short

- **Danger sites on the world map lead to dungeons:** a route of locked arenas, shown as a small node map. Some routes
  are linear chains, others sprawl. Lengths, difficulty, objectives and themes all vary.
- **Each arena** is about two screens across, themed by its site (forest, cave, ruin, shrine, mountain pass, corrupt
  town, cliffs), with **a random mix of enemies** (trash in the dozens to hundreds, regulars, a few elites) and
  **props you can destroy**. Its exits stay sealed until every enemy is dead.
- **Objectives:** clear every arena, kill the boss or bosses (which clears the dungeon however far you've got), or find
  an object or NPC.
- **Enemies drop coloured orbs** you pick up (they vanish, and are counted). XP, loot, items and levels come later.
- **Before:** a popup with the dungeon's size and danger, a chance to change your abilities, and Enter. **After:** a
  results screen (time, arenas cleared, enemies killed, orbs collected), then back to the world map, and the site is
  gone.
- **Sites keep appearing:** minor sites (caves, ruins, forests) often and in numbers; major dungeons rarely and few at
  once; plus a few fixed landmark dungeons, two of them the demo's big ones.
- **The failure to avoid,** in the user's words: dungeons that are **slow, laggy, badly balanced, a drudge**, or **all
  alike** in layout, length, objectives or enemy themes.

## Intent

Combat scenes are where the game's fighting lives (combat only in arenas and dungeons, a structure inspired by Tale of Immortal).
They must make chunk 4's abilities fun to use at scale (a thousand flying swords into a crowd of trash) and keep every
run different enough that clearing sites never becomes a chore. They are also the phase's combat load test: hundreds of
enemies, a thousand swords and every damage number on screen at once, still smooth.

## Goals

1. **Sites on the world map:**
   - **fixed landmark dungeons** placed on the hand-made map (chunk 5), including the demo's **two big dungeons**
     (chosen by hand: one long and linear, one sprawling with a boss);
   - **spawned sites:** **minor** ones (a cave, a ruin, a forest grove; low danger, short) appear often and several at
     once; **major** ones (larger, sprawling, bosses or objectives) appear rarely and only a few at once. They spawn as
     days pass, on suitable terrain (caves in mountains, groves in forest, ruins on plains), away from cities;
   - a **cleared site vanishes** from the map; new ones keep appearing, so the map never runs dry.
2. **Before entering:** stepping onto a site's tile opens a **popup**: its name and kind, its **size** (how many arenas,
   roughly) and **danger**, a way to **change the ability loadout** (chunk 4's), and Enter or Leave.
3. **The route:** each dungeon is a **graph of arenas** joined by paths, generated from the site's seed: **linear** (a
   chain), **branching**, or **sprawling** (loops and dead ends), of varied lengths. The **route map** (like the user's
   picture: nodes, lines, your position highlighted, cleared and uncleared marked) shows in a corner of the screen and
   larger on a key.
4. **Arenas:**
   - about **two screens** across, themed by the site's kind: ground, obstacles (rocks, trees, pillars, walls),
     destructible **props** (crates, urns, statues, shrubs, braziers, …) and enemy kinds;
   - **locked:** its exits are **sealed** (a visible seal across each opening) until every enemy is dead, then open;
     each exit leads to the neighbouring arena on the route, on the side matching the route map's direction;
   - **a random mix of enemies** in four tiers: **trash** (small, weak; dozens, up to hundreds), **regulars**,
     **elites** (tougher, few), **bosses** (big, unique art, many more hit points); counts random but weighted to the
     weaker tiers, and scaled by the site's danger;
   - **going back** to a cleared arena: its enemies stay dead; its **props come back**.
5. **Objectives,** one per dungeon, chosen with its route:
   - **clear all:** every arena cleared;
   - **bosses:** kill the boss or bosses (each in its own arena) and the dungeon is cleared, whatever else is left;
   - **find:** an object or an NPC placed in one arena; walking up to it and pressing the interact key clears the
     dungeon. The NPC is a figure of the dungeon only (a lost disciple, a trapped cultivator), not one of the world's
     NPCs, with a name and a placeholder line.
6. **Orbs:** killed enemies (and some props) drop **coloured orbs** by kind (the colours stand for XP, spirit stones,
   items and so on, for later); walking near one draws it to the player and it vanishes; the run counts each kind.
7. **Clearing:** a **results screen** shows the time taken, arenas cleared, enemies killed (by tier), props broken and
   orbs collected by colour; then the player is **teleported back** to the world map on the site's tile, and the site
   is gone.
8. **One-off arenas** are minor sites with a route of one arena (or two or three in a line).
9. **Enemies don't fight back** (chunk 4): they move about (wander, drift towards the player, mill in groups) so the
   arena isn't static, take damage, die in chunk 4's pixel dissolve and drop orbs. **No respawns in dungeons**; the F2
   test arena keeps chunk 4's respawning.
10. **Balance and pace** (the user's anti-goals "slow", "a drudge", "badly balanced"): hit points, counts and arena
    sizes are tuned in data so that with the demo loadout an arena clears in **under about a minute**, a minor site in
    **a few minutes** and a big dungeon in **about ten to fifteen**; each run's times are recorded and the numbers set
    from play.
11. **Variety** (the anti-goal "all alike"): at least **five themes**, each with its own ground, obstacles, props and
    enemy roster (trash, regular, elite and a boss each); route shapes, lengths, objectives and enemy mixes vary per
    site; placeholder art (`art/placeholders.py`) grows to match.

## Non-goals

- What XP, loot and orbs are worth: items, equipment, inventory, levels, realms, cultivation, breakthroughs (the user:
  all future gameplay).
- Enemies attacking, combat AI, player death and defeat.
- Ambushes while travelling: "random encounters" are spawned sites (the user's call).
- The real UI for the popup, the route map's large view and the results screen (chunk 10 replaces the stand-ins).
- Equipment slots in the popup (only the ability loadout exists).

## Anti-goals (a failure even if it works)

The user's, for combat scenes:
- **Slow:** long walks between fights, enemies with too many hit points, waiting on seals or screens.
- **Laggy:** any hitch or frame drop, including hundreds of trash, a thousand swords and every damage number at once,
  or entering the next arena.
- **Badly balanced:** fights that end before they start, or drag; elites and bosses that feel no different.
- **Boring, a drudge:** the same fight again and again; clearing feels like chores.
- **All alike:** the same layouts or lengths; the same objective every time; the same enemies in every theme.

## Design

### Sites in the world

- Sites are world records (saved): kind, tile, size, danger, objective, seed, fixed or spawned. **Chunk 5's
  hand-placed danger sites become the world's starting sites**: each keeps its tile and gains the site fields; the two
  big dungeons are two of them. Every site, fixed or spawned, vanishes when cleared (the user's rule), the big ones
  included; to see them again, **F2 lists them as test scenes** beside the arena. The places table's entries for
  danger sites give the generator's settings (shape, length, objective, theme, danger) and a **picked seed**; for the
  two big dungeons a test pins a hash of the generated route and arenas, so a generator change that alters them fails
  until it's accepted on purpose.
- Spawned sites come from the world's seed and calendar, checked on each day
  passing: a minor site's chance and cap, a major site's (smaller) chance and cap, and the terrain each kind needs.
  Deterministic, so replays and saves agree. Fog of war hides a site until its tile is seen.
- Clearing a site deletes it from the world.

### The dungeon run

- **Entering** generates the route and its arenas from the site's seed (on a worker, inside the scene load: a designed
  pause): the graph, each arena's layout, theme, enemies and props, the objective's placement. The run (route, cleared
  arenas, current arena, the clock, the counts) is the **active scene** (chunk 5's split), saved with the world.
- **Moving between arenas:** walking out of an open exit loads the neighbouring arena; this must not hitch (arenas are
  generated at the run's start and their sprites already loaded, so a change is a swap, not a load).
- **Leaving early:** walking back out of the first arena's entrance asks to abandon the run; the site stays, with its
  progress reset.
- **The world's time stands still** in combat scenes (chunk 5).

### Enemies and props

- `creatures.yaml` grows: each theme's enemies with their **tier**, hit points, size, sprite, movement, and orb drops;
  bosses with their own art. Danger scales counts and the elite share, as data.
- Props are destructible entities with hit points: chunk 4's abilities damage them (no damage numbers for props, my
  call below), they break with the same dissolve and may drop orbs.
- **Targeting:** abilities tag and home on **enemies first**; props are tagged only when a hit area holds no enemy, so a
  volley never splits onto crates mid-fight. Anything an ability's path touches is still hit (a bolt meeting a crate
  breaks it).
- **Tiers differ in data only:** size, hit points, art, a heavier label, a faint aura on elites, more orbs; behaviour
  differences (phases, shields, summons, resistances) come with combat AI.

### Stand-in screens until chunk 10

- The **popup** and the **results screen** are panels of game text (chunk 3) over the scene; the popup's loadout
  change cycles through **a few preset loadouts** by key: arrangements of chunk 4's abilities (no new abilities). The
  chosen loadout becomes saved player state, changed by a recorded action; the pace targets are tuned against chunk 4's
  default loadout. Chunk 10 rebuilds both as real screens (with full loadout
  editing).
- The **route map** is drawn through the mesh path (lines and circles) in a screen corner; it's game drawing, not UI,
  and may stay.
- **Gamepad friendly** (phase decision 18): the stand-in popup and results screen are driven by the gamepad as well as
  the keyboard (A enters or continues, B leaves, the shoulders cycle the preset loadouts), the route map's large view
  opens on a gamepad button too, and each names its controls with the device's prompts.

### Performance

- Chunk 5's measurement commands gain a **combat path**: the largest trash arena, a thousand swords into it, every
  number shown, recorded against the phase bar.

## Decisions

The user's, 2026-11-10:

1. **A popup before entering:** size and danger, change loadout, enter.
2. **Dungeons are routes of arenas,** shown as a node map (the user's picture), of various lengths and difficulty,
   **linear or sprawling**.
3. **Arenas are locked** until every enemy is dead; each has a random mix of enemies and destructible props.
4. **Back in a cleared arena,** enemies stay dead, props return.
5. **Objectives:** clear all, kill the boss or bosses (clears regardless of arenas), or find an object or NPC.
6. **Enemies drop XP and loot as coloured orbs** you pick up (they vanish); what they're worth comes later, with
   items, levels, realms, cultivation and breakthroughs.
7. **A results screen** (time, arenas cleared, enemies killed, loot and XP), then back to the world map; the site is
   gone.
8. **Bosses are big, with unique art and many more hit points.**
9. **Arenas are about two screens;** dozens of enemies, up to hundreds of small trash; tiers of trash, regular, elite
   (and bosses); random counts weighted to the weaker.
10. **Random encounters are spawned sites:** minor ones (cave, ruin, forest) often and many; major ones rarely and few.
11. **Anti-goals:** slow, laggy, badly balanced, boring or a drudge, too alike in layout, length, objectives or themes.

## Calls made here (not the user's; check them)

- **Routes, arenas, enemies and props are generated from the site's seed** (the same site is the same dungeon); a new
  site has a new seed.
- **Themes by site kind,** at least five, each with its own roster and a boss; **danger** scales counts and elites.
- **Fixed landmark dungeons** on the hand-made map (the two big ones among them) beside the spawned sites.
- **Pace targets:** under about a minute an arena, a few minutes a minor site, ten to fifteen minutes a big dungeon.
- **Orbs are drawn to the player** when near (less walking; the "slow" anti-goal); some props drop orbs too.
- **Enemies move about** without attacking; no respawns in dungeons; the F2 arena keeps respawning.
- **Leaving early** abandons the run (the site stays, reset).
- **Stand-in screens** (game text) until chunk 10; the popup cycles preset loadouts.
- **Props show no damage numbers** (only creatures do), to keep the numbers about the fight.
- **The route map** is a corner overlay drawn through the mesh path, larger on a key.
- **The world's clock stands still** in combat scenes.
- **Chunk 5's hand-placed danger sites are the starting sites;** all vanish when cleared, the two big ones included,
  which stay reachable from F2 as test scenes; the big ones are generator settings plus a picked seed, pinned by a hash
  test.
- **Elites and bosses differ in data only** (size, hit points, art, label, aura, orbs) until combat AI.
- **Preset loadouts** are arrangements of chunk 4's abilities; the chosen one is saved player state.
- **Abilities target enemies before props.**
- **The "find" NPC belongs to the dungeon only;** finding is by walking up and pressing an interact key.

## Testing

- **Generation:** the same seed gives the same route, arenas and enemies on every preset; linear, branching and
  sprawling shapes all occur over many seeds and lengths fall in each size's range; every arena is reachable; exits
  match the route's directions; the objective is reachable; counts weighted to the weaker tiers.
- **Locking:** exits sealed while an enemy lives, open when the last dies; a cleared arena revisited has no enemies
  and all its props.
- **Objectives:** each clears the dungeon exactly when it should (bosses dead with arenas left; the object reached; the
  NPC talked to).
- **Orbs:** dropped on death, drawn in, counted, gone.
- **Sites:** spawning is deterministic from the seed and calendar; caps hold; terrain rules hold; a cleared site is
  removed and saved so; fog hides unseen sites.
- **Runs:** save mid-run and load back into the same arena with the same state; a replay recorded in a dungeon plays
  back with the same hashes; abandoning resets the site.
- **Pictures:** an arena of each theme at a fixed seed; the route map of each shape.
- **Measurements:** the combat path recorded against the phase bar; each arena change checked for hitches; clear times
  of a run per site size recorded in the Outcome.

## Left to the builder

The route and arena generation algorithms; theme list beyond five and each roster; hit points, counts and caps (tuned
to the pace targets); orb kinds and colours; spawn chances; how the seals look; the stand-in panels' layout; commit
split and order.
