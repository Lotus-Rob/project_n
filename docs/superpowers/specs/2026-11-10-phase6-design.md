# Phase 6: UI, text and the demo world

Date: 2026-11-10 · Status: approved 2026-11-10, amended 2026-11-10 (decision 4; chunks 4–7 added; decisions 16–18; goal 6) · Replaces, for the work still to come: `docs/design/0002.4-ui-and-text.md`
(which stays as the record of chunks 1 and 2)

## In short

- **What it's for:** by the end of phase 6 the arena is a **large, complete demo and test scene** that shows every
  engine and game feature built so far, working together: text, a UI, menus, rebinding, on a map big enough to work
  the camera and culling hard.
- **The bar:** a steady 60 fps with no hitches on the minimum spec (GTX 960 / RX 470, 4 threads, 8 GB), measured as
  a frame-time budget on the development machine.
- **The failures to avoid:** a jittery or stuttering camera, lag, 2D shimmering when zooming, and performance that falls
  off as the scene fills.
- **How:** eleven chunks, each its own spec, plan, branch and PR. Chunks 1 and 2 are done; 3 is half built; 4 to 7
  (abilities, the world map, towns, combat scenes and dungeons) are new; the UI chunks follow as 8 to 11.

## Intent

Phase 6 was planned as "UI and text". Its real measure, in the user's words, is a **large, well built out, complete
demo/test scene containing examples of all engine and game features**: not a feature list ticked off, but one scene
where every system runs at once, under load, and still feels smooth. Each chunk adds its features to that scene and is
measured in it.

## Goals

1. **Text:** game text in English and Simplified Chinese (shaped, vertical where needed), name labels over creatures and
   NPCs, and damage numbers, through a distance-field glyph cache; UI text has its own exact-size path (chunk 8).
2. **Abilities:** real, animated xianxia flying swords that burst out and fly at their targets (plain damage), and a couple
   of basic single-element attacks (burn, poison), with health to take the damage.
3. **The demo world, inspired by Tale of Immortal** (the user's flow, 2026-11-10): the main menu (Start, Load, Settings,
   Exit) leads to **the world map**, a tiled map with fog of war, about **20 screens across** at zoom 1, where the
   player and **NPCs travel between cities** on their life simulations, the **50,000 simulated world NPCs** live on
   its cells, and **culling follows the camera's view**. Town and city tiles lead to **town scenes** (a town, a small
   city and a large city, full of NPCs); "bad" tiles (dungeons, forests, mountains, shrines, corrupt towns, cliffs and
   more) lead to **combat scenes**: one-off arenas, and **at least two dungeons** of several rooms (clear every enemy,
   then choose a path; usually linear, sometimes larger, with every room to clear or an objective to find).
   Procedural placeholder art, far more varied.
4. **Click to select, inspect and name:** a click selects a creature or NPC (picking, with its outline); an NPC opens a
   details panel fed by a view model; labels name them.
5. **A UI:** RmlUi documents drawn through the engine, input to the UI first, gamepad navigation.
6. **View models:** the nearby roster (the people in the current scene, or within sight on the world map: name,
   profession, cultivation level, attitude; *amended 2026-11-10 by chunk 9, was all 50,000 world NPCs*) and the
   details panel, fed by subscriptions that never touch the simulation.
7. **Screens:** a pause menu over the blurred scene, three save slots, the loading screen, the month transition, and
   device-specific prompts.
8. **Rebinding:** bindings as data, a settings screen (window, language, bindings), every input readable.
9. **All four scenes kept and built out** (arena, stress, painted, cells).

## Non-goals

- Hand-drawn art: the map stays procedural placeholders (`art/placeholders.py`), just far more varied.
- Audio and particles (phase 7), bloom and the low settings' 8-bit scene target, distortion, the minimap pass.
- Text input and an IME, so no player-typed names; languages beyond English and Simplified Chinese; right-to-left text.
- RmlUi clip masks, layers and filters (a panel blurring other panels).
- Gamepad picking in the world, and full gamepad play (a future task, the user's call in chunk 11); the gamepad drives
  menus, windows and settings.
- Matching Tale of Immortal's minimum spec below the GPU: the engine's threads (main, logic, two workers) and its 4 GB
  process budget stay; only "a dedicated GPU of that era" is the comparison.

## Anti-goals (a failure even if every feature works)

- **Camera jitter or stutter:** panning, following and zooming must be smooth at every zoom.
- **Lag:** input to screen must stay as quick as it is now as the scene fills.
- **Shimmering:** 2D sprites and text must not shimmer or crawl when zooming or through the mips.
- **Performance falling off:** the full scene, zoomed all the way out, must hold the frame budget.

## The bar and how it's measured

- **Target:** a steady 60 fps (16.7 ms a frame) with no hitches on the minimum spec of 0002 §13: GTX 960 / RX 470
  class GPU with 2 GB, 4 hardware threads (Skylake era), 8 GB.
- **No minimum-spec hardware is available,** so the bar is a **frame-time budget on the development machine with
  headroom**: the full demo world, at its heaviest view, stays well under 16.7 ms on the main thread, the logic thread
  and the GPU, as measured in releasedbg (Tracy for the CPU threads). Chunk 5 sets the numbers from a first
  measurement, with the reasoning recorded (the user's example: under 4 ms, a quarter of the frame).
- **Measured and recorded, not a gate** (the user's call): chunk 5 builds the measurements as `project_n` commands run
  on the development machine (shared CI runners are too noisy for frame times); every chunk after it records its
  before-and-after numbers in its spec's Outcome, and a regression is a review finding to fix. Runs pin the job system
  to 2 workers, as on the minimum spec's 4 threads.
- **Designed pauses are exempt** (the user's call): the month step behind its transition screen, a save, and a scene
  load are measured apart and don't count as hitches. **Their work still runs on the worker threads wherever it can**
  (the month step across the job system, a save serialised on a worker, a scene's sprites decoded on workers), so the
  pauses stay short; each chunk that touches one records its length. Every other stall counts, including the first
  time a page of Chinese glyphs is drawn.
- **The GPU side needs its own method:** SDL_GPU has no timestamp queries (0002 §8.9), so Tracy can't time the GPU,
  and the development machine's GPU is far faster than a GTX 960. Chunk 5 chooses how the GPU is measured and how its
  numbers stand in for the minimum spec, and records why.
- **Each anti-goal becomes a measurement**, built in chunk 5 and recorded by every chunk after it:
  - a scripted camera path (pan, follow, zoom in and out) over the full world, recorded with Tracy: frame-time
    percentiles and the worst frame against the budget;
  - input-to-present latency, measured the same way before and after a chunk;
  - shimmer: reference pictures at several zoom levels and sub-pixel camera offsets, compared frame to frame;
  - the earlier phases' checks stay (sprite seams, outlines, the calendar step, saves, replays).

## The chunks

| # | Chunk | Status | Spec |
|---|---|---|---|
| 1 | The libraries and the font (SDL3_ttf, Noto Sans SC through Git LFS) | **Done** (PR #15–16) | `docs/design/0002.4-ui-and-text.md` §3 |
| 2 | The scene target and the composite (sRGB encoded by the final pass) | **Done** (PR #16) | `docs/design/0002.4.1-scene-target-and-composite.md` |
| 3 | Game text and damage numbers: distance-field glyphs, labels that scale and declutter, numbers by kind | **Done** (branch `phase6-text-chunk3`; see its spec's Outcome) | `2026-11-10-phase6-chunk3-text-design.md` |
| 4 | **Abilities:** flying swords, basic single-element attacks, health | **Done** (branch `worktree-phase6-chunk4-abilities`; see its spec's Outcome) | `2026-11-10-phase6-chunk4-abilities-design.md` |
| 4b | **Seals:** a data-driven system for seals (formations) of 1–9 turning rings in layers, script, emblems, satellites and light; the swords' seal and a showcase (added 2026-11-12, the user's) | To do | `2026-11-12-phase6-chunk4b-seals-design.md` |
| 5 | **The world map** and travel between scenes: the large tiled map with fog of war, NPCs travelling between cities, culling by the view, labels at scale, entering tiles, the performance and shimmer measurements | To do | `2026-11-10-phase6-chunk5-world-map-design.md` |
| 6 | **Towns:** a town, a small city and a large city, full of NPCs; move, select, inspect, talk | To do | `2026-11-10-phase6-chunk6-towns-design.md` |
| 7 | **Combat scenes and dungeons:** one-off arenas, two dungeons of rooms, paths and objectives | To do | `2026-11-10-phase6-chunk7-combat-scenes-design.md` |
| 8 | The `ui` module (S13 first: RmlUi, and UI text's exact-size path) | To do | `2026-11-10-phase6-chunk8-ui-design.md` |
| 9 | View models: the NPC roster and the inspect panel | To do | `2026-11-10-phase6-chunk9-view-models-design.md` |
| 10 | Screens: the main menu, the pause menu and blur, save slots, loading, the transition, prompts | To do | `2026-11-10-phase6-chunk10-screens-design.md` |
| 11 | Rebinding, bindings as data, the settings screen | To do | `2026-11-10-phase6-chunk11-settings-design.md` |

Each chunk is brainstormed and specified on its own, planned with superpowers writing-plans when it starts, built on
its own branch and worktree, and merged through its own PR. Each adds its features to the demo world and passes the
chunk 5 checks from then on.

## Decisions carried over (the user's, 2026-11-09 and 2026-11-10; unchanged)

1. **Git LFS** for large binary assets, the font first (done).
2. **Escape opens a pause menu** that pauses the game, modal over the blurred scene, with **three save slots**; F5 and F9
   stay as debug keys on the first slot.
3. **Rebinding, bindings as data and a settings screen** in this phase.
4. ~~One glyph cache for game text and the UI~~ **amended 2026-11-10:** game text and UI text are **separate** for
   performance and looks: distance fields for game text (chunk 3), exact-size text for the UI (chunk 8, with S13).
5. **A strings table** in English and Simplified Chinese, and a language setting.
6. **Damage numbers as text**, game text in **one overhead layer**, game text **sized on screen** (chunk 3's decisions).

## Decided in this rewrite (the user's, 2026-11-10)

7. **The measure of the phase is the demo world** (Intent), at the scale in Goal 2, with the anti-goals above.
8. **The minimum spec stays 0002 §13's**, Tale of Immortal's class with a dedicated GPU, at a steady 60 fps.
9. **Frame time on the development machine, with headroom,** stands in for minimum-spec hardware, **measured and
   recorded per chunk**, not a gate.
10. **Designed pauses are exempt** (the month step behind its transition, saves, scene loads), with their work on
    worker threads wherever possible; every other stall counts.
11. **Clicking selects, inspects and names.**
12. **The demo world is a new chunk**, right after text and abilities; the UI chunks follow it.
13. **Placeholder art**, much more varied; no hand-drawn art in this phase.
14. **Abilities are their own chunk 4**, right after text: real flying swords and basic single-element attacks.
15. **The demo world is three chunks** (5 the world map, 6 towns, 7 combat scenes and dungeons), inspired by Tale of
    Immortal's structure: combat only in combat scenes, towns and the world map for moving, selecting and talking;
    the main menu comes with the screens chunk (10), and until then the game starts on the world map.
16. **Tale of Immortal is an inspiration, not a template:** draw on it where it's relevant, but the game is heading
    in its own direction; no chunk copies it for its own sake.
17. **Accessibility is always the top priority** (chunk 11): every chunk's spec has an Accessibility section saying
    how its features honour reduce motion, reduce flashing, the colour-blind palettes and text size; colour is never
    the only signal. Every UI colour, and every combat effect's colour by element (never by ability), can be changed
    in Accessibility → Colours, organised by area (Combat Text, Effects, Names and Captions, Selection, HUD) then
    element; colours live in one table the main thread reads, never in the simulation (amended 2026-11-11, the user).
18. **Every UI is gamepad friendly** (the user, 2026-11-10): every window, screen, dialog, popup and panel in every
    chunk is fully and fluidly usable with a gamepad (and with the keyboard alone): every control reachable by the d-pad
    or stick, focus always visible, A to act and B to back out, the device's prompts shown. Each chunk's spec says how
    its UI does it. (Playing the game itself with a gamepad is a future task: non-goals.)

## Constraints

- The engine's rules stand: the simulation is deterministic and replayable, the UI and view models never change state,
  every tool comes from Pixi, and builds and `check` are verified at the end of each completed task, chunk or phase,
  never commit by commit.
- Each chunk's spec says what it adds to the demo world and which checks it must pass there.
- Old design documents stay in `docs/design/` as history; their "as built" notes for chunks 1 and 2 are the record.

## Calls made in this document (not the user's; check them)

- ~~The roster lists all 50,000 world NPCs~~ superseded by the user's chunk 9 answer: the roster lists who's nearby.
- Chunk 3 builds text and the label capability; chunk 5 puts labels on movers at the world's scale.
- Measurement runs pin the job system to 2 workers.

## Left to each chunk's spec

How each chunk is built, its own goals and anti-goals, its tests, and the numbers it records. Chunk 5's brainstorm must
settle, from this document's builder check:
- the camera: following the cultivator, panning freely like a map, or both, and where the cull rectangle comes from
  (the logic around the followed entity, or the main thread's view sent as input, and whether that is recorded);
- which movers tick every tick, whether the 50,000 world NPCs are drawn on their cells, and which can be clicked;
- who gets a name label (all, near the camera, above a zoom, hovered and selected) and where NPC names come from;
- the zoom range (0.25 to 2 today, or a whole-map view);
- whether the arena's one-screen layout survives inside the big map, and what "built out" means for the stress,
  painted and cells scenes (their benchmark and reference roles);
- the GPU measurement method and the frame-time numbers.
