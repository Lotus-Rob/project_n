# Phase 6, chunk 10: screens

Date: 2026-11-10 · Status: approved 2026-11-10 · Phase spec: `2026-11-10-phase6-design.md` · Replaces:
`docs/design/0002.4-ui-and-text.md` §9–10 (kept as history; what it decided and this spec keeps is marked below)

## In short

- **The main menu** over **full-screen stylised artwork** of the game, a different picture each launch: **Continue**
  (your latest save, one click), **Start** (a brand-new world), **Load**, **Settings**, **Exit**. No splash screens.
- **Start makes a brand-new world:** new people, households, sects, city layouts and dungeons on the hand-made map,
  generated on the worker threads behind a progress bar with **comical loading messages**.
- **Loading is optimised first;** a loading screen is the fallback, shown only when a load takes long enough to need
  one.
- **Pause menu** over the blurred scene; **three save slots** with a picture, place, date and play time; the **month
  transition**; **device prompts**; and the real versions of chunk 5's enter prompt and chunk 7's dungeon popup and
  results screen.
- **Nothing you can't skip,** and **few clicks:** one to continue, two to load any save.

## Intent

Screens are the frame around play: how you get in, step out, save and come back. They should be quick and quiet: get
the player into the game in as few clicks as possible, never make them wait on something that isn't doing work, never
make them sit through a screen they've seen, and look like the rest of the UI (chunk 8's ink and paper).

## Goals

1. **The main menu:**
   - **backdrop:** full-screen stylised artwork of the game (a city, a sect's mountain gate, flying swords, the world
     map, …), chosen at random each launch, drifting slowly (a gentle pan and zoom), fading in once loaded;
     **placeholders** from `art/placeholders.py` in the ink-wash look until real art replaces them file for file;
   - **the menu** in an ink-and-paper panel to one side: **Continue** (shown when a save exists: loads the most recent
     one), **Start**, **Load**, **Settings** (chunk 11), **Exit**;
   - the game opens straight to it: **no splash or intro screens**.
2. **Start: a brand-new world** from a new random seed: the hand-made map (chunk 5) with newly generated NPC names,
   people, households, sects, attitudes (chunk 9), city layouts (chunk 6, seeded from the world's seed) and dungeon
   contents (chunk 7). **The map's places stay hand-made** (their tiles and names, until procedural world generation),
   and **the two big demo dungeons keep their pinned seeds** (chunk 7's curated showpieces); every other site's seed
   comes from the world's. Generation runs across the
   worker threads (a designed pause, the phase rule). A `--seed` command-line option reproduces a world for testing and
   bug reports, and the seed shows in the debug overlay.
3. **Loading:**
   - **optimised first:** every load (a new world, a town, a dungeon, a save) is measured and its work spread over the
     workers; targets on the development machine: a town or dungeon in **under a second**, a save in **under two**, a
     new world in **under five**. They are aims, not a gate (the phase's rule): the chunk measures every load, makes
     **one optimisation pass** on any load over its aim (profile it, spread its largest stage over the workers), and
     records the numbers in the Outcome; a load still over its aim after that pass is reported to the user with its
     profile rather than chased open-endedly;
   - **the loading screen only when needed:** a load shorter than about **half a second** is a quick fade; longer ones
     fade to the loading screen: the backdrop art, the place's name, a **progress bar** from real progress (weighted
     stages: generation, sprites decoded and uploaded), and **comical loading messages** in xianxia flavour ("Persuading
     the mountain spirits to stay put…", "Counting the sect elder's grudges…"), rotating every few seconds, from the
     strings table in both languages, a general pool plus some per kind of place.
4. **The pause menu** (kept from 0002.4 §9): Escape (with no window open, chunk 8) opens it and pauses the game; Escape
   or Resume closes it. **Resume, Save, Load, Settings, Quit to menu, Quit to desktop.** Modal, over the **blurred
   scene** (0002.4 §4's blur passes, only while a modal is open). Quitting with unsaved progress asks once.
5. **Save slots:** **three**, each showing a **small picture** taken when it was saved, **where** (the world map, a
   town, a dungeon), the **in-game date**, **play time** and **when it was saved**. Loading is one click on a slot; saving
   onto a used slot asks first. **Loading never asks,** even over unsaved progress (choosing a slot is the choice; the
   click budget wins); quitting asks once. F5 and F9 stay as quick save and load on the first slot (decision 2).
6. **The month transition** (kept from 0002.4 §9, with the half-second rule): a month passing always shows a small
   **notice** in a corner ("A month passes: the fourth month"), which fades by itself and blocks nothing. Only if the
   month step outlasts a short fade does the full ink-and-paper banner show over the blurred last frame, for as long as
   the step works; no full-screen flicker for a fast step.
7. **Device prompts** (kept from 0002.4 §10): wherever a control is named, the key cap or gamepad button glyph (Xbox,
   PlayStation, Nintendo, generic) of the device used last, switching the moment another is used; a `<prompt>` element
   reading the binding table.
8. **The stand-ins, made real:**
   - **the enter prompt** (chunk 5): a small dialog at the tile, Enter or Cancel, with the interact key to confirm;
   - **the dungeon popup** (chunk 7): the site's name, size and danger, and **full loadout editing** (abilities you
     know dragged or picked into the slots; chunk 4's four for now), then Enter;
   - **the results screen** (chunk 7): time, arenas cleared, enemies killed by tier, props broken, orbs collected;
     any key or click continues.
9. **Gamepad friendly** (phase decision 18): every screen here (the main menu, pause menu, slots, confirmations, the
   enter dialog, the dungeon popup, results, the first-launch page's neighbours) is fully usable by gamepad and keyboard
   alone, passing chunk 8's reachability test. The dungeon popup's loadout editing works by picking (A on an ability,
   A on a slot) as well as by dragging, so it never needs a mouse. Any button skips a skippable screen.
10. **Skippable:** every screen that isn't waiting on work closes on a key or click; nothing animates the player's time
   away.

## Non-goals

- Character creation; world options (size, seed entry in the menu); more than three slots; cloud saves; autosave.
- Credits, achievements, news, a splash screen.
- Hand-drawn art: the backdrops are placeholders.
- Settings and rebinding themselves (chunk 11).

## Anti-goals (a failure even if it works)

The user's:
- **Slow loading,** and **loading not optimised:** a loading screen hiding work that could have been fast; a loading
  screen should be the worst case.
- **Inconsistent styles:** screens that don't match chunk 8's theme.
- **Blurry text** on any screen, at any UI scale.
- **Screens you can't skip.**
- **Too many clicks** to get into the game or load a save.

## Design

### Screens as windows

- Every screen is a chunk 8 window using its theme. **Modal** screens (the pause menu, confirmations, the enter dialog,
  the dungeon popup) take input until closed, turn on the blur and **pause the game** (the recorded `Pause` and resume
  actions, as the pause menu's), so time doesn't flow and no month step lands behind a dialog; the others (the main
  menu, loading, the transition, results) fill the screen.
- **Settings** in both menus shows disabled until chunk 11 adds its window. The seed shows in a **minimal debug
  overlay** (debug builds; a key toggles it), added here.
- **Click budget:** Continue: one click from launch to play; Start: one click (plus the load); a save from the menu or
  the pause menu: two (Load, then the slot).

### Loading

- **New world:** the logic generates on the job system and publishes its progress (stages and fractions) as a small
  view; the main thread draws the screen. **Scene loads** keep 0002.4 §9's way: the main thread knows it's decoding and
  uploading sprites and reports `Renderer::ContinueScene`'s progress; generation inside a scene load (towns, dungeons)
  adds its stage.
- **The half-second rule:** a load starts with a fade; if it isn't done when the fade is, the loading screen shows; if
  it is, the new scene fades in. No loading screen flashes up for a frame.
- **Saving** writes on the background job (as today) and never shows a loading screen; the picture is the scene target
  downscaled when the save is made.

### The transition

- Shown while `Snapshot::transition` is set, the month from the small `Calendar` view (0002.4 §9); the month step runs
  across the job system (phase rule), so the banner lasts as long as that work.

## Decisions

The user's, 2026-11-10:

1. **The main menu's backdrop is full-screen stylised artwork of random parts of the game.**
2. **Start makes a brand-new world;** a pause to generate is fine, behind a loading bar with comical loading messages.
3. **Anti-goals:** slow loading, inconsistent styles, blurry text, screens you can't skip, loading not optimised (a
   loading screen only in the worst case), too many clicks to get into the game or load a save.

Carried from the phase (decision 2): Escape opens a pause menu over the blurred scene with three save slots; F5 and F9
stay on the first slot. Kept from 0002.4 §9–10 (accepted 2026-11-10): the loading screen driven by the main thread's
own progress for scene loads; the transition from `Snapshot::transition` and a `Calendar` view; prompts per device
family from the binding table.

## Calls made here (not the user's; check them)

- **Continue** on the main menu (the latest save, one click), beside Start, Load, Settings and Exit.
- **No splash or intro screens.**
- **The backdrop** drifts slowly, changes each launch, and is placeholder art until real art.
- **A `--seed` option** and the seed in the debug overlay, to reproduce a world.
- **Load targets:** under a second for a town or dungeon, two for a save, five for a new world (set from measurement).
- **The half-second rule** before a loading screen shows.
- **Loading messages** in a general pool plus some per kind of place, rotating every few seconds.
- **Pause menu** with Quit to menu and Quit to desktop; quitting with unsaved progress asks once.
- **Slots** show a picture, place, in-game date, play time and save time; one click loads; overwriting asks.
- **The month banner** lasts exactly as long as the month step.
- **The dungeon popup gains full loadout editing** (replacing chunk 7's preset cycling).
- **Results and other screens** close on any key or click.
- **Start re-rolls** NPCs, households, sects, attitudes, city layouts and dungeon contents; the places' tiles and names
  stay hand-made and the two big dungeons keep their pinned seeds.
- **Load times are aims:** one optimisation pass on any load over its aim, then recorded; a load still over is reported,
  not chased.
- **The month notice:** a corner notice every month; the full banner only when the step outlasts a fade.
- **Every modal pauses the game.**
- **Loading a save never asks;** quitting with unsaved progress asks once.
- **Settings shows disabled** until chunk 11; a minimal debug overlay shows the seed.

## Testing

- **Menus:** Continue appears only with a save and loads the newest; the click budgets hold (a scripted run counts
  clicks from launch to play); Escape opens and closes the pause menu and pauses the logic.
- **New world:** two seeds give different worlds; the same seed gives the same world (`--seed`); progress rises
  monotonically to complete.
- **Loading:** a fast load never shows the loading screen; a slow one (a test delay) shows it after the fade; messages
  rotate and exist in both languages; load times per kind recorded against the targets.
- **Slots:** save, list (picture, place, date, play time), load each of three; overwrite asks; F5 and F9 on slot 1.
- **Transition:** a fast month step shows only the corner notice; a slow one (a test delay) shows the banner after the
  fade, ending when the step does.
- **Modals:** opening any modal pauses the logic; closing resumes it; a replay with modals opened plays back the same.
- **Prompts:** the glyph switches with the last device; every family has its glyphs.
- **Stand-ins:** the enter dialog, dungeon popup (loadout edits saved) and results screen each open, act and close as
  specified.
- **Pictures:** the main menu, the pause menu over the blurred arena, the slot list and the loading screen at UI scales
  1 and 1.5.

## Left to the builder

The backdrop list and drift; the message list (at least thirty); the fade length; the stage weights; the save picture's
size; the slot layout; the prompt glyph set's art; commit split and order.
