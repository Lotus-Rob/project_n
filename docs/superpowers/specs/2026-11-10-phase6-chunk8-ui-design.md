# Phase 6, chunk 8: the UI module

Date: 2026-11-10 · Status: approved 2026-11-10 · Phase spec: `2026-11-10-phase6-design.md` · Replaces:
`docs/design/0002.4-ui-and-text.md` §7 (kept as history; what it decided and this spec keeps is marked below)

## In short

- **Many small windows, not one big screen** (the user: WoW's approach, not Tale of Immortal's single crammed sheet):
  each window has one job and its own key, several can be open at once, and none owns the screen.
- **The world opens the UI:** interacting with something (a cauldron, a forge, a notice board) opens the window that
  belongs to it; what can be done in the world is done in the world.
- **The look is Tale of Immortal's ink and paper:** pale rice-paper panels with ink-wash edges, black brush-stroke
  heading banners, round gold-rimmed icon medallions; one shared style sheet for every window.
- **Windows move and scale:** drag them, snap them, scale the whole UI or one window; the layout is remembered.
- **Text is crisp at every scale** (UI text at its exact pixel size, its own path apart from game text), and windows
  open instantly.
- **What it proves:** the HUD rebuilt in it (replacing chunk 4's temporary one) and a notice board in each town's square
  that opens its window. The roster, panels, menus and settings come in chunks 9–11.

## Intent

The UI should stay out of the way. Tale of Immortal puts everything in one dense full-screen sheet of tabs within tabs
(the user's screenshots: six side tabs, inner tabs, the same stats block under every page); the user wants the opposite:
small focused windows the player arranges, and UI that appears because of what the player is doing in the world.
Chunk 8 builds the machinery and the style every later window uses, so each one is cheap to add and looks like the
rest.

## Goals

1. **RmlUi, checked first (S13):** builds for all four presets; documents in English and Chinese render through our
   renderer; a long list scrolls smoothly (chunk 9's roster); and S13 **chooses the UI font engine** (below).
2. **UI text at its exact size:** glyphs rasterised at the pixel size they're drawn at, after the UI scale, so text is
   sharp at every scale; changing the scale re-rasterises; Chinese breaks lines correctly; no hitch when new glyphs
   appear (pre-warmed, then rasterised on a worker, as chunk 3's game text). Separate from chunk 3's distance-field
   cache (the user's decision 4).
3. **Windows:**
   - each a document with an ID, a title, an optional key, and how it opens (by key, by interaction, or always: the
     HUD);
   - **several open at once;** clicking one brings it to the front; **Escape closes the front window** (and with none
     open, chunk 10's pause menu);
   - **dragged** by the header; **snapped** to the screen edges and to other windows; never off-screen, even after the
     window size changes;
   - **scaled:** a global UI scale setting (default: follows the window's height over 1080, kept from 0002.4 §7) and a
     per-window scale;
   - **remembered:** positions, scales and which windows are open, per player in `settings.yaml` (not in saves); a
     **reset layout** action restores the defaults.
4. **Opened by the world:** an entity can carry an **interactable** that names a window; walking up to it and pressing
   the interact key (or clicking it) opens that window, and walking away closes it. This is how professions' windows
   (alchemy at a cauldron, smithing at a forge) will open later.
5. **The style:** one shared theme (`assets/ui/theme.rcss`) holds the colours, fonts, the paper panel frame (a
   nine-slice texture), heading banners, buttons, icon medallions, bars and tooltips; every window uses only its classes.
   Placeholder art for the frames and medallions comes from `art/placeholders.py`.
6. **Input goes to the UI first** (kept from 0002.4 §7): App hands each input event to `Ui`, which says whether it was
   consumed; the mouse belongs to the window under it; what isn't consumed goes to the game as now. **Gamepad
   navigation** through RmlUi's `nav-*` properties (d-pad and A/B as arrows, Enter and Escape).
7. **The HUD, rebuilt:** the action bar (the loadout's slots with their keys, cooldown sweeps and Qi costs), health
   and Qi bars, each a small movable piece; replaces chunk 4's temporary HUD. The route map (chunk 7) stays game drawing.
8. **The notice board:** a board in each town's market square (chunk 6) opens a "Notice board" window with placeholder
   text: the first window opened by the world.
9. **Gamepad friendly by construction** (phase decision 18): every window built on this module is fully usable by
   gamepad and by keyboard alone. The theme gives every interactive class a visible focus style and default `nav-*`
   order; a **reachability test** walks every document under `assets/ui/` with simulated d-pad presses and fails if
   any focusable element can't be reached or left, or if a window can't be closed with B; nothing is mouse-only except
   dragging windows (chunk 11 may add a gamepad way to move them).
10. **Developer comforts** (kept from 0002.4 §7): documents in `assets/ui/` reload live in debug builds; RmlUi's debugger
   opens on a debug key.

## Non-goals

- The real windows: the roster and details panel (chunk 9), the main and pause menus, save slots, loading, prompts
  (chunk 10), settings and rebinding (chunk 11).
- Character, abilities, bags and profession windows: nothing to show yet (items, stats and professions are future
  gameplay).
- RmlUi clip masks, layers and filters (a panel blurring other panels); text input; UI mods or add-ons.
- The route map as a UI window.

## Anti-goals (a failure even if it works)

The user's:
- **Blurry text** at any UI scale or window scale.
- **Slow to open:** any window taking more than a frame to appear, or a hitch when it does.
- **A cluttered screen:** windows that can't be closed or moved out of the way; HUD pieces bigger than they need to be.
- **Inconsistent styles:** windows that don't share one frame, heading, button and font set.
- **Too much UI:** a window for something that could be done in the world.
- **One big UI for everything** (from the screenshots): tabs within tabs, a master screen, the same block repeated on
  every page.

## Design

### The font engine (S13 decides)

- **(a) Ours, proposed:** an `Rml::FontEngineInterface` on SDL3_ttf, laying text out with its shaping and rasterising
  plain (not distance-field) glyphs at the exact pixel size into a UI glyph cache of R8 pages, the same page model as
  chunk 3 (shelf packer, uploads in the copy pass). One font load serves both caches.
- **(b) RmlUi's FreeType engine:** no code, but a second font load, no shaping, and Chinese needing `word-break:
  break-all` (RmlUi breaks lines only at white space).
- S13 picks by the anti-goals (sharpness at fractional scales, Chinese line breaking, no hitch) and records why; the
  RmlUi package needs the override 0002.4 §3 describes only for (a). **S13 is a gate:** the plan's first task is S13,
  and the rest is planned after its result. Goals 2 and the anti-goals are the bar either engine must meet; if (b) can't
  (it rasterises on the main thread and loads the font twice), (a) is built.
- **S13's list check** is a throwaway spike: a virtualised list over 50,000 synthetic rows (only the visible rows as
  elements), its frame time recorded while scrolling; chunk 9 builds the real roster.

### Text details

- **Line breaking in Chinese** follows the common punctuation rule: a line never starts with closing punctuation
  (，。、！？；：）」』》 and the like) or ends with opening punctuation. Line breaking is RmlUi's own layout
  (`ElementText.cpp`), not the font engine's, so whichever engine wins, the override in `xmake/repo/` patches it if
  RmlUi lacks the rule, with an upstream fix recorded (fix-xmake-package skill).
- **No gaps on a scale change:** when the UI or a window's scale changes (a window resize included), text keeps
  drawing with its old-size glyphs, stretched, until the new size lands from the workers (a frame or two of softness,
  never missing text).
- **No gaps on first use:** a window's static text is pre-warmed when its document preloads; dynamic text (an NPC's
  name) with only a few unseen glyphs is rasterised on the main thread at once (a few glyphs cost microseconds); a
  large batch goes to the workers. A budget per frame keeps the main thread's share under the phase bar.
- **UI strings** live in `strings.yaml` under their own `ui.` names, not in `core::StringId` (no C++ names them);
  documents name them and RmlUi translates at load (`TranslateString`). The data check covers both languages; a test
  fails a document naming an unknown string. **Changing the language reloads every document** (rare, so a reload is
  fine).
- **Per-window scale stays sharp:** no RmlUi transform stretching rasterised text; how (a context per window, or
  another way) is the builder's, with focus, snapping and gamepad navigation across windows working either way.

### Drawing (kept from 0002.4 §7)

- `ui/internal/` implements RmlUi 6's render interface on a public `gfx` UI API (compiled geometry, textures, scissor,
  transforms). SDL_GPU can't upload inside a render pass, so `ui` **records** a UI draw list (uploads, then draws in
  submission order) and the renderer replays it: uploads in the copy pass, draws in the final pass after the composite.
  A `ui` pipeline and shader pair, committed with `shaders`.
- **Main thread only;** the UI reads the drawn snapshot and sends decisions as actions; it never touches the simulation.

### Windows

- A `ui::Windows` registry: every window's document is **loaded at start and kept hidden**, so opening is showing (the
  "slow to open" anti-goal); its open and closed state, position and scale live in the registry and are written to
  `settings.yaml` on change (debounced) and on exit.
- Dragging uses RmlUi's drag events on the header; snapping within a few pixels of an edge or another window's edge;
  positions stored relative to the nearest screen corner, so they survive a window resize.
- **Interactables** are a component (what it does: open a window, talk, or a game effect such as chunk 7's find
  objective; and its reach) on world entities. **One interact key** for all of them (talking, finding, boards); with
  several in reach, the nearest wins, and the one that would be used is highlighted. Pressing it is **a recorded
  action** naming the target: the logic resolves it and either applies its effect (finding clears a dungeon) or emits an
  event to open a window, which the main thread opens; leaving its reach emits the close. Clicking an interactable out
  of reach walks to it, then uses it.

### Controls before chunk 11

- **Per-window scale:** Ctrl and the wheel over a window's header. **Global scale:** `settings.yaml` (chunk 11 adds the
  slider). **Reset layout:** a key (chunk 11 adds the button).
- **Escape:** closes the front window, prompt or popup (chunk 5's enter prompt and chunk 7's popup count as windows);
  with nothing open it does **nothing** until chunk 10's pause menu (today's quit moves to the window's close button and
  a debug key), so pressing Escape twice can't quit by accident.
- **Gamepad:** the Back button moves focus from the game to the front window and back; while a window has focus, the
  d-pad and A/B drive it and the shoulder buttons cycle windows, B closes; otherwise the pad drives the game. Windows
  can't be dragged by gamepad (default positions; mouse to move).

### The HUD

- The **player's** health and Qi bars and the **action bar**, shown **in combat scenes only** (abilities work only
  there); on the world map and in towns no combat HUD shows. Enemies' health bars stay world drawing under their
  labels (hundreds of them; not UI).

### The style

- `theme.rcss` is the only place colours, fonts, frames and spacing are set; window documents use its classes. A test
  checks that no document under `assets/ui/` sets a colour, font or image outside the theme (the "inconsistent styles"
  anti-goal).

## Decisions

The user's, 2026-11-10:

1. **The look is Tale of Immortal's** (ink and paper), **much less complex.**
2. **No single big UI:** WoW's approach of small windows, or UI that opens from relevant interactions (an alchemy
   cauldron opens alchemy crafting; the same for smithing and any future profession).
3. **Windows can be dragged, the UI scaled, and the layout is remembered.**
4. **Anti-goals:** blurry text, slow to open, a cluttered screen, inconsistent styles, too much UI for what could be
   in-world interactions (and, from the screenshots, busy and crammed).

Kept from 0002.4 §7 (accepted 2026-11-10): S13 first; the recording render interface; input to the UI first; gamepad
navigation through `nav-*`; documents in `assets/ui/` with hot reload and the debugger; scale following the window's
height; main thread only. Amended: UI text is exact-size and separate from game text (phase decision 4), so the font
engine choice is between (a) and (b) above.

## Calls made here (not the user's; check them)

- **The HUD is rebuilt in this chunk** as movable pieces (action bar, health and Qi), replacing chunk 4's temporary HUD.
- **A notice board** in each town's square is the first world-opened window, with placeholder text.
- **Walking away closes** a world-opened window.
- **Escape closes the front window first;** with none open it opens the pause menu (chunk 10).
- **Snapping** to edges and other windows; never off-screen; positions kept relative to the nearest corner.
- **Per-window scale** beside the global one; layout stored in `settings.yaml`, not in saves; a reset-layout action.
- **Every window's document is preloaded** and hidden, so opening never loads.
- **A test enforces the shared theme** across `assets/ui/`.
- **Font engine (a)** is the proposal; S13 decides, as a gate, by the goals; S13's list check is a 50,000-row
  throwaway spike.
- **Chinese line breaking** follows the common punctuation rule, patched in RmlUi's override if it lacks it.
- **On a scale change** old glyphs stretch until the new ones land; **on first use** a few glyphs rasterise on the main
  thread, more on the workers.
- **UI strings** in `strings.yaml` under `ui.` names, translated at load; a language change reloads documents.
- **Controls:** Ctrl+wheel scales a window; global scale in `settings.yaml`; a reset-layout key; Escape closes the
  front window and does nothing with none open (quit by the close button or a debug key) until chunk 10.
- **Gamepad:** Back toggles focus between game and windows; shoulders cycle; no gamepad dragging.
- **One interact key** for boards, talking and finding; nearest wins; a recorded action; clicking one out of reach
  walks there first.
- **The HUD** is the player's bars and action bar, in combat scenes only; enemy health bars stay world drawing.

## Testing

- **S13:** builds on all four presets; a Chinese and English document renders (reference picture); the font engine's
  choice and its measurements recorded.
- **Text:** reference pictures of the HUD and a test window at UI scales 0.75, 1, 1.25, 1.5 and 2, checked for
  sharpness; a new Chinese glyph appears without a hitch.
- **Windows:** dragging, snapping and clamping on fixed inputs; positions survive a window resize; layout written and
  read back from `settings.yaml`; reset restores defaults; Escape closes the front window; several open at once and
  focus order.
- **Input:** an event over a window is consumed, one elsewhere reaches the game; gamepad navigation moves focus.
- **Interactables:** in reach, the key opens the window; out of reach, it closes; out of reach, the key does nothing.
- **Theme:** the document check rejects a stray colour or font.
- **Speed:** open time per window (target: the same frame); the HUD's frame cost recorded against the phase bar
  (chunk 5's measurements).

## Left to the builder

The window registry's API; snapping distance; the debounce interval; nine-slice sizes; default positions; the HUD's
layout; the interact key's binding (chunk 11 makes it rebindable); commit split and order.
