---
name: look-at-the-game
description: See what project_n actually draws, to check a rendering change by eye or by pixel diff. Use when asked to look at, screenshot or run the game, to confirm a gfx, shader, sprite, atlas or scene change on screen, to compare before and after pictures, or to judge seams, mips, blending or colours.
---

# Looking at the game

Owned by: src/AGENTS.md.

## Pick the way to look
- **A fixed scene, no display needed (preferred):** `pixi run project_n reference`. Each run writes its pictures to `build/reference/<view>.png` (views: `painted`, `cells`, `cells_far`, `arena_halfway`, `outlines`, `fox_dissolving`, foxes halfway through their death's dissolve close up, `volley_ring` and `volley_converging`, the flying swords as their third ring forms (the waves before it in flight) and halfway to the foxes, `volley_plunge`, swords plunged into the foxes they killed, close up, `bolt_burn`, a fire bolt in flight, close up, `lance_burst`, the poison lance's burst at its point, close up, `hud_zoom_one`, the temporary HUD (health and Qi bars, the slots, the lance's cooling down) at zoom 1, and the game text at each zoom, `text_zoom_quarter`, `text_zoom_half`, `text_zoom_one`, `text_zoom_two`, `labels_crowd`, the foxes bunched round the player so their labels stack, and the damage numbers: `numbers_one` and `numbers_quarter`, a grid of every kind and motion at zoom 1 and the smallest, `numbers_bare`, that grid with the outline and the shadow off, and `numbers_label`, a number on a creature too small to see starting from its label; tests/reference/reference_tests.cpp). Open them with the Read tool. They come from software Vulkan, so they match CI, not necessarily the user's GPU.
- **The real window, on the user's GPU:** when the change concerns presentation, input, zoom or the live loop, or the user asks to see it running.

## The real window
1. Start the scene in the background: `pixi run project_n run -- --painted` (or `--cells`, `--stress`, nothing for the arena; src/AGENTS.md Commands).
2. Find the window and capture it. On niri: `niri msg -j windows` gives each window's `id` and `title` (`project_n`), then `niri msg action screenshot-window --id <id> --path <scratchpad>/<name>.png`. Elsewhere, ask the user for a screenshot.
3. Read the PNG; then stop the game (the background task, or close the window). Never kill processes this session didn't start.
4. The log: `project_n.log` under `logs/` in SDL's per-user folder for `project_n` (on Linux `~/.local/share/project_n/logs/`): what was chosen (driver, scene, workers) and any failure.

## Comparing two pictures
Judge a change by diff, not by sampling colours: render the same view before and after (stash or switch the change), then
`pixi exec --spec pillow==13.0.0 python -I <script> a.png b.png`, with a script in the scratchpad that prints the bounding box and count of `ImageChops.difference` pixels above a small threshold. Samples taken at different sub-pixel positions misled once, the diff didn't.

## What the window can't do
- Nothing here injects wheel or key events: to check a zoom, change the starting zoom temporarily (or add a reference view) and revert it.
- Keep the window focused while capturing: losing focus pauses the simulation.

## Gotchas
