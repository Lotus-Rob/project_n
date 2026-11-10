# Phase 6, chunk 9: view models, the nearby roster and the details panel

Date: 2026-11-10 · Status: approved 2026-11-10 · Phase spec: `2026-11-10-phase6-design.md` (goal 6 amended by this
chunk) · Replaces: `docs/design/0002.4-ui-and-text.md` §8 (kept as history; what it decided and this spec keeps is
marked below)

## In short

- **View models** are how windows see the simulation without touching it: a window subscribes to a view, the logic
  publishes it in the snapshot when it changes, and nothing about the state or replays changes (kept from 0002.4 §8).
- **The roster lists the people around you,** not the whole world: everyone in the current town or dungeon, or, on the
  world map, everyone within your sight. Columns: **name, profession (or sect), cultivation level, attitude toward
  you**; sorted, filtered, never jumping under your cursor.
- **The details panel** opens when you select someone (in the world or in the roster): **their stats** and **what you
  can do with them**: Talk today; more interactions join as the game grows.
- Both are small windows of chunk 8's kind, in its ink-and-paper style.

## Intent

The roster answers "who's here?": who these people are, what they do, how strong they are and how they feel about you.
The details panel answers "who is this?" and is where interacting with someone will grow from. Neither should be a
spreadsheet of the world: the user wants UI that earns its place (chunk 8), so the roster covers what's around you and
the panel shows what matters about one person. The 50,000 world NPCs remain the life simulation's load test; the
roster scales to what's nearby.

## Goals

1. **Subscriptions** (kept from 0002.4 §8): an action `Subscribe` carries a view request (kind, target), `Unsubscribe`
   ends it; both change only what is published, so they are **never recorded** in replays and the state hash ignores
   them. Views travel in `Snapshot::views`, plain structs behind `shared_ptr<const>` with a version each, replaced only
   when their data changed.
2. **The nearby roster** (`NearbyRoster` view):
   - **who:** in a town, every NPC in it; on the world map, every NPC within the player's **sight radius** around the
     player, wherever the camera is (chunk 5's rule for which walkers are seen; the debug radius widens it); **not in
     combat scenes** (no people there but chunk 7's find NPC, which is found in the world, and clicks attack);
   - **columns:** name (chunk 5's generated names, in the player's language), **profession** or, for cultivators,
     their **sect or guild** (chunk 6), **cultivation level**, **attitude toward you**;
   - **sort** by any column; **filter** by profession or sect and by attitude; a count of how many are listed;
   - **stable under the cursor:** while the mouse is over the list, rows keep their places (newcomers join at the end,
     leavers grey out); once the mouse leaves, the list re-sorts;
   - **clicking a row selects** that person in the world (outline, label priority) and opens their details panel; a
     **focus key** moves the camera to the selected person (chunk 5's snap key still goes to the player);
   - opened and closed by its own key (chunk 8's windows), remembered like any window.
3. **The details panel** (`NpcDetails` view), following the selection:
   - **stats:** name; profession or sect; home city and household (the other members' names); cultivation level and
     power; attitude toward you; what they're doing (their current activity in a scene, or last month's action on the
     world map); their needs (rest, company) as bars;
   - **interactions:** a row of actions; today **Talk**, **in towns only** (chunk 6's greeting: if they're out of
     reach, the player walks to them first, as chunk 8's interactables do); on the world map, walkers show their stats
     and no actions (stopping a traveller would change the world's travel). The list comes from a table of interactions, each with when it
     applies, so later ones (trade, spar, invite, gift, …) are a row in data and a handler, not a new panel;
   - updates live while open; closes when the selection clears. **Leaving sight clears the selection** on the world map
     (a walker passing beyond the sight radius or into the fog), with the panel fading out; in a town, someone going
     indoors stays selected (still in the scene) and their activity reads "inside".
4. **Placeholders for what doesn't exist yet** (the user: "we can add to it as we go"):
   - **cultivation level:** a tier named from the NPC's power by a table in `assets/data/` (strings in both
     languages), until realms and breakthroughs are designed;
   - **attitude:** each world NPC gets an **attitude toward the player** at world creation (a small signed value from
     its seed, leaning by its sect or guild), saved with it (the save version goes up), **unchanging for the demo**;
     shown as a word (hostile, wary, neutral, friendly, devoted) by thresholds in data, so every word appears. It is a
     field on the NPC rather than relationship edges (50,000 edges for one subject would bloat the table); what changes
     it (talking, deeds, reputation) is future gameplay.

5. **Gamepad friendly** (phase decision 18): the roster's column headers and filters are focusable (A sorts by a column
   or opens a filter), the d-pad moves through rows (holding scrolls faster, the shoulders page), A on a row selects
   that person and opens the panel; the panel's interactions are focusable buttons; B backs out; prompts name the
   controls. Both pass chunk 8's reachability test.

## Non-goals

- A list of all 50,000 world NPCs (amends phase goal 6), and a world-wide search.
- Realms, cultivation, breakthroughs, reputation as real systems; interactions beyond Talk.
- Enemies and creatures in the roster.
- Comparing two NPCs side by side; several details panels at once.

## Anti-goals (a failure even if it works)

From the user's UI anti-goals (chunk 8) and the phase's:
- **A cluttered panel:** every field the simulation has, dumped; tabs within tabs.
- **A list that jumps** under the cursor, or flickers as people come and go.
- **Slow:** a panel or list taking more than a frame to open or update; a hitch as a crowd enters the sight radius.
- **Stale data:** a panel showing what someone was doing a minute ago.
- **The simulation touched:** a view that changes the state, or a replay that differs because a window was open.

## Design

### Views (kept from 0002.4 §8, re-scoped)

- `core/views.hpp` holds each view kind as a struct in the frame contract. **`NearbyRoster`**: one row per person in
  range (stable ID, name ID, profession or sect, cultivation tier, attitude), rebuilt when someone enters or leaves or a
  shown field changes; positions aren't in it. **`NpcDetails`**: one NPC's fields, household names and attitude,
  rebuilt when they change.
- On the world map the roster's members come from the logic's own query of NPCs within the sight radius around the
  player (not the camera-culled publish); in towns, the scene's active NPCs.
- **Main thread:** sorting, filtering and the "stable under the cursor" rule run over the view's rows; the list is
  **virtualised** (only the rows in view plus a margin are elements, a spacer keeps the scroll bar true), kept from
  0002.4 §8 so a debug radius of thousands still scrolls smoothly.

### Interactions as data

- A table in `assets/data/` lists interactions: a string ID for the button, when it applies (always, in reach, by role,
  by attitude, …: a small fixed set of conditions), and the action it sends. Talk is the only entry; its action is chunk
  8's recorded interact action aimed at the NPC.

## Decisions

The user's, 2026-11-10:

1. **The roster lists the people in the current area,** or nearby on the world map.
2. **Its columns:** name, profession, cultivation level, attitude toward you.
3. **The details panel shows their stats and lets you choose interactions** with them; what those are grows as the game
   does.

Kept from 0002.4 §8 (accepted 2026-11-10): subscriptions as unrecorded actions; views as versioned shared pointers in
the snapshot; the virtualised list.

## Calls made here (not the user's; check them)

- **"Nearby" on the world map is the sight radius** (the same rule as which walkers you see).
- **People only:** enemies and creatures aren't listed.
- **Sort by any column, filter by profession or sect and attitude;** rows hold still under the cursor.
- **Clicking a row selects** the person in the world and opens the panel.
- **The panel's stats:** name, profession or sect, home and household, cultivation level and power, attitude, current
  activity, needs.
- **Talk from the panel walks you to them** if out of reach.
- **Interactions are a data table** with a fixed set of conditions; Talk is the only entry.
- **Placeholders:** cultivation tiers named from power by a data table; attitude words from opinion thresholds.
- **One details panel,** following the selection.
- **Attitude is seeded at world creation,** leaning by sect, saved, unchanging for the demo; a field on each NPC, not
  relationship edges.
- **Talk is town-only;** world-map walkers show stats without actions.
- **No roster in combat scenes.**
- **The roster's range is the sight radius around the player,** queried by the logic apart from the camera's culling, so
  panning never churns it.
- **Leaving sight clears the selection** on the world map; indoors in a town keeps it.
- **A focus key** moves the camera to the selected person; the snap key stays on the player.
- **The phase spec's goal 6 is amended:** the roster covers who's nearby, not all 50,000.

## Testing

- **Subscriptions:** a view appears in the next snapshot and stops after unsubscribing; an unchanged view keeps its
  pointer; a replay recorded with windows open plays back with the same hashes.
- **The roster:** in a town lists the scene's NPCs; unavailable in combat scenes; on the world map lists exactly those within the
  sight radius; sort and filter over fixed rows; rows hold still while hovered and re-sort after; the virtual window
  for a scroll offset; a debug radius of thousands scrolls within the frame budget (recorded).
- **The panel:** shows the fields of a fixed NPC; updates when its activity changes; Talk out of reach walks then
  greets; closes when the selection clears.
- **Placeholders:** tiers and attitude words at their thresholds, both languages; seeded attitudes are deterministic,
  saved and loaded, and every word occurs across the demo world.
- **Range:** panning the camera away doesn't change the world map's roster; a walker leaving the sight radius leaves the
  roster and clears its selection.
- **Pictures:** the roster and the panel at UI scale 1 and 1.5.

## Left to the builder

Column widths and order; the roster's key; tier names and thresholds; attitude thresholds; how the "stable under the
cursor" rule animates; commit split and order.
