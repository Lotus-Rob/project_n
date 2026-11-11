# Phase 6, chunk 11: settings and rebinding

Date: 2026-11-10 · Status: approved 2026-11-10 · Phase spec: `2026-11-10-phase6-design.md` (decision 17 added by this
chunk) · Replaces: `docs/design/0002.4-ui-and-text.md` §11 (kept as history; what it decided and this spec keeps is
marked below)

## In short

- **Change close to everything:** eight categories, **Accessibility first**, then Display, Graphics, Audio, Gameplay,
  Camera, Controls and Rebinds. Each shows its **standard options** plainly, with an **Advanced** section collapsed
  below.
- **Accessibility is always the top priority** (the user's): the first category, a one-page essentials screen on first
  launch, and from now on every feature says how it honours reduce motion, reduce flashing, colour-blind palettes and
  text size.
- **Gameplay settings are preference, quality of life and visibility, never difficulty.**
- **Every change applies at once, no restart, ever;** display changes that could leave a black screen revert unless
  confirmed.
- **Every option explains itself:** plain names, help text, and for anything visual a **live preview** that shows the
  difference; no unexplained jargon.
- **Rebinding** every control on keyboard and mouse and on gamepad, with conflicts, resets, and prompts that follow
  at once (kept from 0002.4 §11).
- **Audio has a slider per channel,** from a channel table that grows with the game.
- **Mouse, keyboard and gamepad** each drive every option easily and fluidly, as every UI must (phase decision 18).

## Intent

Settings are where players make the game theirs, and where accessibility lives or dies. The user wants as much as
possible to be adjustable without changing how hard the game is, laid out so a newcomer finds the common options at
once and an enthusiast can drill into the rest, and every option understandable by someone who has never heard of the
technique behind it.

## Goals

1. **The settings window** (chunk 8's style), opened from the main and pause menus (chunk 10 enables its buttons):
   - **categories in this order:** Accessibility, Display, Graphics, Audio, Gameplay, Camera, Controls, Rebinds; it
     opens on Accessibility the first time, then on the last category used;
   - each category: **standard options** first, then **Advanced**, collapsed by default (remembered once opened);
   - a **search box** that finds any option by name or help text, in either language, Advanced ones included;
   - **reset** per category and **reset everything**, each asking once.
2. **Instant, no restart:** every option applies the moment it changes, with no Apply button and nothing needing a
   restart. **Display changes** that could leave the player without a picture (mode, resolution, monitor) apply, then
   revert after **15 seconds** unless confirmed.
3. **Explained:** every option has a plain-language **name** and **help text** (both languages) shown beside it when
   hovered or focused. Options are named by **what they do**, not the technique ("Edge smoothing", not "SMAA"); a
   technical name, where one exists, sits in the help text. Options that change the picture or motion have a **live
   preview** in the help pane: a small looping scene drawn with the option's current value, and where useful a
   before-and-after split (text outline, damage number motion, colour-blind palettes, outline thickness, effect
   density, reduce motion).
4. **Accessibility** (the first category):
   - **standard:** game text size; **interface size** (chunk 8's global UI scale: windows and their text grow together,
     sharp at every size, so no document reflows on its own); colour-blind palette (off, protanopia, deuteranopia,
     tritanopia); reduce motion; reduce flashing; text outline and shadow (chunk 3's, moved here); high-contrast labels
     and selection outlines; hold-or-toggle for held inputs (moving, drag pan); cursor size (a drawn cursor, built
     here);
   - **advanced:** per-effect motion and flash controls; label minimum size (chunk 3's readable size); damage-number
     minimum size; colour per meaning (damage kinds, attitudes) adjustable by hand;
   - **colour is never the only signal,** for every colour that carries meaning: damage kinds (a small icon each),
     attitudes (words), selection and hover outlines (thickness and pattern), **orbs** (a shape per kind; placeholder
     art), health and Qi bars (position and icon); the palettes cover all of them. Sect robe colours are decoration
     (the sect is named in labels and the roster) and stay as they are.
5. **First launch:** before the main menu, one page with the accessibility essentials (text size, colour-blind palette,
   reduce motion, reduce flashing), skippable in one click, shown once.
6. **Display:** window mode (windowed, borderless, fullscreen), resolution, monitor, vsync, frame-rate cap, brightness,
   **reset window layout** (chunk 8's); advanced: frame pacing details.
7. **Graphics:** only what the engine has: render scale, outline thickness, effect density (sword trails and halos),
   label density; each named by effect. New techniques add their options when they arrive. (Texture filtering stays one
   fixed, shimmer-free choice: the phase's anti-goal.)
8. **Audio:** **one slider per channel** (master, music, sound effects, damage and spell effects, voices, ambience, UI
   interactions, …) built from a **channel table** in `assets/data/`; master and the main channels standard, finer
   ones advanced; mute when the window loses focus. **Shown now** (the user asked for the category), each slider's help
   saying sound arrives in a later update and the level is kept; they take effect when phase 7 brings sound. The table
   marks these rows as waiting on a system, the one exemption from "every row is read by code".
9. **Gameplay** (preference and visibility only): language; combined damage numbers and number motion (chunk 3);
   label rules (who gets a label and when); enemy health bars; confirmation prompts; advanced: label priorities and
   fade timings.
10. **Camera:** edge pan (on or off, speed), drag pan, zoom speed, follow smoothing, snap-back key behaviour, invert drag;
    advanced: smoothing curves.
11. **Controls:** stick deadzones, gamepad prompt style (chunk 10); advanced: deadzone curves. Vibration and
    sensitivities join when there's rumble and a relative pointer to act on.
12. **Rebinds** (kept from 0002.4 §11):
    - **bindings become data:** `assets/data/bindings.yaml` holds the defaults, each control's inputs per device, read
      and checked by the `data` module (unknown controls or inputs, one input on two controls of one context);
      `app/bindings.cpp`'s code table goes; debug keys stay code;
    - **overrides save over the defaults** in `settings.yaml`, so a later version's new defaults reach controls the
      player never touched;
    - **`platform` reads every input:** all keys by scancode, mouse buttons, the gamepad's standard buttons and axes,
      each with a stable name;
    - **rebinding:** pick a control, press the new input (Escape cancels); a conflict asks to swap; reset one or all;
      a **primary and a secondary** input per control per device; prompts (chunk 10) follow at once; on a gamepad,
      capturing cancels by **holding** the Back button or after five seconds idle (so B can itself be bound).
13. **Every option by every device, fluidly** (the user's):
    - **mouse:** click toggles and choices, drag sliders (or the wheel over one), click a category, scroll the list;
    - **keyboard:** up and down move between options, left and right change the focused one (sliders step, holding
      speeds up; choices cycle), Enter toggles or opens, Tab and Shift+Tab or Page keys switch categories, Escape backs
      out (closes Advanced, then search, then the window), any letter starts a search;
    - **gamepad:** the d-pad or left stick moves between options, left and right change them (holding speeds up),
      A toggles or opens, B backs out, the shoulder buttons switch categories, Y resets the focused option, X expands
      or collapses Advanced;
    - focus is always visible (a clear highlight), never lost when a section collapses, and the help pane follows it;
      the device that moved last is the one whose prompts the window shows (chunk 10).
14. **The setting list is data:** every option is a row in a **settings table** in `assets/data/` (its key, category,
    standard or advanced, type and range, default, name and help string IDs, preview kind); the window is built from
    it, so adding an option is a row and the code that reads it.

## Non-goals

- Difficulty options, or any gameplay setting that makes the game objectively easier or harder (the user's rule).
- Graphics options for techniques the engine doesn't have (anti-aliasing, bloom, …): they come with their technique.
- Subtitles and voice options beyond the volume slider (no voices yet).
- Settings profiles, cloud sync, per-save settings.
- **Full gamepad play** (walking with the stick, an aim point, abilities on buttons): a future task (the user's). The
  gamepad drives every menu, window and setting now; gameplay controls that need a pointer have no gamepad binding
  yet, and the Rebinds list shows their gamepad column as "comes with gamepad play".

## Anti-goals (a failure even if it works)

The user's:
- **Missing options:** something a player would reasonably want to change that can't be.
- **Poor organisation:** options in the wrong place, the common buried under the rare.
- **Changes that don't apply instantly;** **restarting the game to apply** anything.
- **Lack of explanation:** an option without help text.
- **Options that don't make sense to non-technical players** (the user's example: anti-aliasing types like SMAA,
  CMAA, TAA): name by effect, **explain, show, demonstrate**.

## Design

### The settings table and the player's file

- `assets/data/settings_table.yaml` (name the builder's) lists every option; `settings.yaml` holds only the player's
  values that differ from the defaults (as overrides, like bindings), so new defaults arrive. The `data` module reads
  both and checks them: an unknown key in the player's file is logged and dropped; a value out of range uses the
  default (kept from chunk 3).
- **Coverage tests:** every option the code reads is in the table, and every row in the table is read by some code
  (rows marked as waiting on a system, today only audio, excepted) and has its name and help in both languages, so no
  option is silently missing, dead or unexplained.
- **Options arrive with their features:** an option appears when the feature it controls exists (Audio, at the user's
  request, the exception); this chunk builds the small accessibility features its options need (hold-or-toggle, the
  drawn cursor, orb shapes, damage-kind icons).
- **Search without an IME** (text input stays a phase non-goal): the box takes Latin keystrokes; each row in the table
  has search keywords in English and in pinyin, so Chinese option names are found by their pinyin.
- **What's rebindable:** every game control on the keyboard and mouse, and every game control that doesn't need a
  pointer on the gamepad (the rest wait for gamepad play). UI navigation (chunk 8's and goal 13's keys and buttons)
  is rebindable under Rebinds' Advanced, with a guard: Escape on the keyboard and Start on the gamepad always open the
  pause menu and back out, whatever else is bound, so no player can lock themselves out of the settings.

### Applying live

- Each option names what it touches; changing one sends it to its owner on the main thread (renderer, window, UI, text,
  camera, input) or as an action to the logic when the simulation reads it (none of today's do). The window mode,
  resolution and monitor go through SDL and the scene target refits (chunk 2); language reloads the UI's documents
  (chunk 8) and switches game text; text sizes re-rasterise UI text and re-scale game text.
- An option the engine can't apply live isn't offered until it can (the "no restart" anti-goal is a rule, not an aim).

### Previews

- The help pane draws a small preview through the renderer: a fixed tiny scene (a cultivator, a fox, a label, a few
  damage numbers, a sword trail) into an offscreen target, with the hovered option's value applied, looping; split
  before-and-after where the table asks for it.

### Accessibility across the game (decision 17)

- From this chunk on, every chunk's spec has an **Accessibility** section: how its features honour reduce motion,
  reduce flashing, the colour-blind palettes and text size. This chunk wires the settings into what chunks 3–10 built:
  numbers' bounce and the camera's easing calm under reduce motion; hit flashes and the sword halo tone down under
  reduce flashing; damage colours, attitude colours and selection outlines take the palette.
- **The colour menu** (the user, 2026-11-11): the menu builds Accessibility → Colours → area → element from
  `core::COLOUR_AREA_INFO` and `UI_COLOUR_INFO`, so every UI colour and every effect's colour by element can be edited.
  The colour-blind palettes become presets of that table.

## Decisions

The user's, 2026-11-10:

1. **Change close to everything,** without changing or dumbing down gameplay, especially for accessibility.
2. **Categories:** display, graphics, audio, gameplay, accessibility, camera, controls, rebinds.
3. **Gameplay settings are personal preference, quality of life and visibility,** never objectively easier or harder.
4. **Well organised:** clear standard options, with advanced ones collapsed by default.
5. **Accessibility is always the top priority.**
6. **Audio: a slider per channel,** as many as the game ends up with.
7. **Every option is easily and fluidly controllable by mouse, keyboard and gamepad.**
8. **Full gamepad support (playing with a gamepad) is a future task;** this phase's gamepad drives menus, windows and
   settings.
9. **Anti-goals:** missing options, poor organisation, changes that don't apply instantly, restarting to apply, no
   explanation, options that don't make sense to non-technical players; explain, show, demonstrate.

Kept from 0002.4 §11 (accepted 2026-11-09): bindings as data; overrides over defaults; `platform` reads every input;
pick and press to rebind, swap on conflict, reset; prompts follow at once.

## Calls made here (not the user's; check them)

- **Accessibility is the first category** and the first page settings open on; **a first-launch essentials page**,
  skippable, shown once.
- **Decision 17 for the phase:** every chunk's spec has an Accessibility section from now on, and this chunk wires the
  settings into what chunks 3–10 built.
- **Colour is never the only signal** (icons for damage kinds, words for attitudes).
- **The contents of each category** as listed in Goals 4–12 (the user named the categories, not their contents).
- **A search box,** resets per category and for everything, Advanced remembered once opened.
- **Display changes revert after 15 seconds** unless confirmed.
- **Live previews** in a help pane, from a tiny fixed scene.
- **Graphics shows only what the engine has.**
- **Primary and secondary bindings** per control per device.
- **The settings table and the channel table are data;** `settings.yaml` holds overrides only.
- **Text outline and shadow move to Accessibility** from chunk 3's gameplay group.
- **Interface size is chunk 8's UI scale** (in Accessibility); reset window layout is in Display.
- **Audio shows now** with its help saying sound comes later; every other option appears only with its feature
  (no texture filtering, vibration or sensitivity yet); hold-or-toggle, the drawn cursor, orb shapes and damage-kind
  icons are built here.
- **Colour never alone** covers damage kinds, attitudes, outlines, orbs and bars; sect colours are decoration.
- **Search** by English words and pinyin keywords (no IME).
- **Everything is rebindable,** UI navigation under Advanced, with Escape and Start always reaching the pause menu.
- **The device controls** as in goal 13 (keys and buttons per action); search is typed (keyboard), and on a gamepad
  the shoulders and the category list reach everything search would; gamepad rebinding cancels by holding Back or
  timing out.

## Testing

- **Table and coverage:** parsing and refusals; every option read by code is in the table and vice versa; every row
  has a name and help text in both languages.
- **Instant apply:** each option, changed at runtime, reaches its owner within a frame (one test per owner); no option
  needs a restart.
- **Display revert:** an unconfirmed change reverts after the countdown; a confirmed one stays.
- **Overrides:** the player's file holds only changed values; a changed default reaches an untouched option.
- **Rebinding:** capture an input for a control; a conflict swaps; reset one and all; primary and secondary; overrides
  survive a changed default; prompts follow at once.
- **Search:** finds options by name and help, in both languages, Advanced included.
- **Every device:** each kind of option (toggle, choice, slider, binding) changed by mouse, by keyboard and by gamepad
  alone, scripted; focus survives collapsing Advanced and switching categories; holding a direction accelerates a
  slider.
- **Accessibility:** under reduce motion, the numbers' bounce and the camera's easing are off (positions at a tick);
  under reduce flashing, hit flashes are toned down; each palette changes every meaningful colour; colour-blind
  pictures of a fight in each palette (reference pictures).
- **First launch:** the essentials page shows once, skips in one click, and its choices are saved.
- **Pictures:** the settings window on each category at UI scales 1 and 1.5, both languages.

## Left to the builder

Each option's exact range, step and default; the order within categories; the help texts; the preview scene; the
search's matching; the colour-blind palettes' colours (from published palettes); commit split and order.
