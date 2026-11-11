# Phase 6, chunk 4: abilities and the flying swords

Date: 2026-11-10 · Status: approved 2026-11-10 · Phase spec: `2026-11-10-phase6-design.md` · New: no earlier design

## In short

- **An ability loadout:** generator (left click), spender (right click), movement (Space), four specials (1–4, each a
  cooldown and a resource cost), an ultimate (5, a very long cooldown). Set in data for now; the UI to equip comes later.
- **The flying swords** (special 1): **1,000 swords form round the player in waves of wide, scattered rings, one after
  another, and flow, on curving paths, into the area under the mouse**, striking every enemy tagged there. Bright trails and a soft glow on each sword.
- **Two basic elemental attacks:** a fire bolt that burns (generator) and a poison lance that poisons (spender); a dash
  toward the mouse (movement).
- **Enemies have health and die** in a pixelated dissolve, then respawn at the arena's edge. They don't fight back yet.
- **Combat only:** abilities work in combat scenes (the arena); the world and towns are for moving, selecting and
  talking (chunks 5 and 6).

## Intent

The xianxia sword volley is the demo world's showpiece: the moment the engine is pushed hardest and has to still look
effortless. The user's references are the classic flying-sword images (a ring of glowing swords around the caster;
swords sweeping out in curved arcs and converging), **"but on a much larger 1000 sword scale."** The ability system
around it is the start of combat as the game will have it: inspired by Tale of Immortal, combat happens only in arenas
and dungeons the player enters, never in the open world or towns.

## Goals

1. **The loadout:** eight slots: generator (left click), spender (right click), movement (Space), specials 1–4 (keys
   1–4, each with a cooldown and a resource cost), ultimate (key 5, a very long cooldown). Specials 2–4 and the ultimate
   are **empty** in this chunk. The loadout is data, read at start-up.
2. **The resource, Qi:** the generator builds it; the spender and the specials cost it; casting without enough fails
   cleanly (nothing happens, a short cue in the HUD).
3. **Targeting:** every cast aims at the **world point under the mouse**; a **hit area** around that point tags the
   enemies in it when the cast starts. **One cast per click** (a held button doesn't repeat).
4. **The flying swords** (special 1, plain damage only):
   - on the key press, 1,000 swords **form in waves of wide, scattered rings, one after another** (the user's decision
     of 2026-11-11, decision 11; it was "burst out of the player in a ring"), staggered so they stream out like
     particles;
   - each then flies on a **curved path** that sweeps round and **converges into the target area**, flowing into it;
   - **homing:** the swords split among the enemies tagged in the area and **follow each one as it moves**; when an
     enemy dies, its swords turn to the tagged enemies still alive; **once every tagged enemy is dead, the remaining
     swords plunge into the ground and harm nothing else** (and with none tagged at the cast, they all do);
   - each sword has a **trail** (a ribbon on chunk 3's mesh path) and a **soft glow** (an additive halo sprite);
   - each hit is a damage event for chunk 3's numbers, and the cast is one **ability use**, ended when the last sword
     lands (combined numbers rely on that end).
5. **The fire bolt** (generator, left click): a quick bolt to the target point; a hit deals fire damage and leaves a
   short **burn** that ticks.
6. **The poison lance** (spender, right click): a heavier strike that deals damage and leaves a **poison** that ticks.
7. **The dash** (movement, Space): a short, quick move toward the mouse, on a short cooldown.
8. **Health and death:** creatures have health; at zero they die in a **pixelated dissolve** in the sprite shader
   (the sprite breaks into blocky pixels that fade out over about half a second) and **respawn** a few seconds later at
   the arena's edge, so the demo never runs out of targets.
9. **Combat only:** abilities work in combat scenes; in a combat scene left click is the generator, there's no
   click-to-select, and hovering an enemy raises its label to "hovered" priority (chunk 3).
10. **A temporary HUD** until the UI chunks: a game-text readout in a screen corner (Qi and each slot's cooldown), and
    thin health bars under enemies' labels, drawn as sprites, and the player's own health and Qi as two bars in the
    HUD (the user, 2026-11-11).

## Non-goals

- Enemies fighting back, combat AI, player death, and enemy kinds beyond the arena's creatures: later.
- Abilities for specials 2–4 and the ultimate, and equipping abilities in a UI (chunk 8 onward).
- Sparkles, real particle shatter and bloom: phase 7's particles (the swords' trails and halos are this chunk's stand-in).
- The open world, towns, encounters and entering combat maps (chunks 5 to 7).
- A skill tree, levels, items.
- **Melee combos** (slash, slash, stab: one per click within a short window): not built, but the one-cast-per-click input
  is shaped so a combo window can be added later without changing the action format.

## Anti-goals (a failure even if it works)

- **Frame drops** during a 1,000-sword cast, at any zoom.
- **Jittery swords:** a sword stepping, wobbling or snapping between frames.
- **Stiff swords:** swords moving rigidly in straight lines; the flight must flow and curve.
- **Lag** between the key press and the swords appearing.
- **Divergence:** a recorded replay of a fight that plays back differently (the engine rule: the simulation is
  deterministic).

## Design

### Abilities in the simulation

- **A cast is an action** (`ActionKind::Cast`: slot, target world point), one per click, recorded in replays like any
  input. The main thread turns the mouse into the world point through the camera. Input-to-screen time is today's
  (no prediction layer); it is recorded as an anti-goal check.
- **The player's ability state** (Qi, each slot's cooldown, active casts) is simulation state, saved; the arena's save
  version goes up.
- **Abilities are data:** `assets/data/abilities.yaml` holds each ability's slot kind, cooldown, Qi cost or gain, damage,
  element, hit-area radius and its own parameters (the swords' count, ring radius, timings); `loadout` in the same file
  says which ability sits in which slot. Read and checked by the `data` module like the creature table.
- **Combat scenes** are marked as such in the scene list; outside one, `Cast` actions are ignored.

### The flying swords at 1,000

- **A volley is one record, not 1,000 entities:** its origin, target point, start tick, seed, and each sword's assigned
  enemy (tagged at the cast, split evenly). Each sword's position at any time is a function of (volley, sword index,
  time, its enemy's position): out along its ring direction with a stagger, then along a curved path (a smooth spline
  whose control points sweep round the caster) whose end **follows its enemy**. The simulation schedules each sword's
  arrival tick when the volley starts; at that tick the hit lands on the sword's enemy if alive, else on another tagged
  enemy still alive (reassigned the moment its enemy dies, so the sword visibly turns), else nowhere: the sword plunges
  into the ground at its last aim point.
- **The main thread draws them** (`core::` holds the path function so the logic and the renderer share it): the
  snapshot carries the volley records and each sword's current enemy, and the renderer evaluates every sword at the
  exact render time against its enemy's interpolated position, so swords, trails and the enemies they follow line up.
- **Smooth, not stiff:** paths curve and ease (no straight segments, no sudden turns); the renderer interpolates by the
  frame's alpha as it does instances, and because a sword's position is a function of time it can also be evaluated at
  the exact render time, so swords glide at any frame rate.
- **Trails** are sampled from the same function a little back in time, drawn as ribbons on the mesh path; **halos**
  are an additive sprite under each sword. Swords and trails draw over the world (in the overhead layer, under text).
- **Hits** are scheduled deterministically when the volley starts; each lands as a damage event with the volley's
  ability use; the use ends when the last sword lands.

### Elements over time

- A burn or poison is a short effect on the creature (element, damage per tick, ticks left), ticking on the simulation's
  second; each tick is an event of its kind, sharing its cast's ability use. **An elemental attack's hit and its ticks
  are one kind** (the fire bolt's hit is "burn"), so combined numbers stay one colour per use. A second application
  **refreshes** the effect (no stacks); its later ticks belong to the latest use.
- **Who they hit:** the fire bolt strikes the one enemy in its area nearest the target point; the poison lance, the
  stronger spender, strikes every enemy in its smaller area. Both are scheduled hits (no collision), the bolt drawn
  flying to its target.

### Death and respawn

- At zero health a creature becomes **dying** for about half a second: its instance carries a dissolve progress the
  sprite shader turns into blocky pixels fading out (a pixel-grid noise threshold, in screen pixels so the blocks stay
  crisp at every zoom). Then it's removed, and a respawn is scheduled a few seconds later at a point on the arena's edge;
  the respawned creature **eases back into an orbit** from there over a few seconds (no snap).
- **The player has health** (it takes no damage yet; the aura heals it). The crystal's aura now does real damage and can
  kill; chunk 3's debug burst stays numbers only.

### The temporary HUD

- Screen-anchored game text (chunk 3's text, placed in screen pixels instead of at a world anchor) shows Qi and the
  cooldowns; health bars are two sprites (frame and fill) placed under each enemy's label.

### Art

Placeholder sprites from `art/placeholders.py`: a sword, a halo, a fire bolt, a poison lance's strike; through `./dev
atlas` as usual.

## Accessibility

Phase decision 17 requires this section.

- **Every UI colour can be changed** (the user, 2026-11-11: "we should be able to recolour basically any UI element in
  the accessibility settings menu"). Every UI colour, chunk 3's and this chunk's, lives in one table of named colours
  in the player's settings (`core::UiColours`), grouped by area as the menu shows them (**Accessibility → Colours →
  area → element**):
  - **Combat Text:** each damage-number kind, with its variant for text without outline and shadow.
  - **Effects:** one colour per element (damage, healing, poison, burn), never per ability. Every ability's halos,
    trails, glows and bursts take their element's colour.
  - **Names and Captions:** labels, the caption, the text ink.
  - **Selection:** the hover and selection outlines.
  - **HUD:** the player's health bar, the Qi bar, enemies' health bars, the bars' background, and the HUD's text,
    cooling and warning colours.

  The main thread reads the table where each element is drawn, and a change shows from the next frame. The
  simulation never colours anything. Chunk 11's menu edits the table, and its colour-blind palettes are presets of it.
- **Text size:** the HUD's lines go through `gfx::TextOptions::scale`, as all game text does.
- **Colour is never the only signal:** health bars show health by the fill's length. A slot on cooldown shows its
  seconds left as a number, not just dimmed. Too little Qi shows the words "Not enough Qi", not just a red tint. The
  fire bolt and the poison lance differ in shape as well as colour (a flying bolt with a trail; a burst at the point).
- **Reduce flashing (chunk 11 wires it):** the halos and the bolt's glow take their brightness from one constant,
  `gfx::HALO_INTENSITY`, and the lance's burst from `gfx::FLASH_INTENSITY`. Chunk 11 lowers both under reduce
  flashing. The dissolve fades and never flashes.
- **Reduce motion (chunk 11 wires it):** the trail length is one constant, `gfx::TRAIL_TICKS`, which chunk 11 shortens
  under reduce motion. The swords' flight is the ability itself and stays. Nothing shakes the camera.
- **Input:** every ability is on the mouse and keyboard. Playing with a gamepad is a phase non-goal (decision 18 covers
  UI only), so casts have no gamepad bindings yet.

## Decisions

The user's, 2026-11-10:

1. **The loadout:** generator (left click), spender (right click), movement (Space), specials 1–4 with cooldowns and Qi
   costs, ultimate (5) with a very long cooldown.
2. **The flying swords** are a strong special: 1,000 swords burst out of the player in a ring and flow, curving, into the
   target, plain damage only, in the style of the user's three reference images.
3. **Targeting:** wherever the mouse points, with a hit area that tags enemies.
4. **Effects split:** trails as mesh-path ribbons and a halo glow now; sparkles, particle shatter and bloom in phase 7.
5. **The demo loadout:** fire bolt (generator, burn), poison lance (spender, poison), dash (movement), flying swords
   (special 1); specials 2–4 and the ultimate empty; set in data.
6. **Combat only in combat scenes** (arenas, dungeons, entered by encounter or by choice, inspired by Tale of Immortal); no
   click-to-select in combat, hover raises a label; the world and towns get move, select and interact (chunks 5, 6).
7. **Death:** a pixelated dissolve now (particles in phase 7); enemies respawn; no fighting back or AI yet.
8. **A temporary HUD** (Qi, cooldowns, health bars) until the UI chunks.
9. **Homing:** swords split among the tagged enemies and follow them; a dead enemy's swords turn to the tagged ones still
   alive; once all are dead the rest plunge into the ground, harming nothing.
10. **One cast per click;** melee combos (one per click within a short window) come later, and the input leaves room
    for them.

The user's, 2026-11-11, after seeing the swords form a single ring ("Can we make the sword spread much wider so the 1000
swords don't just end up looking like a circle? Much wider, much more spread, much more randomness in the creation? Make
the creation semi-sequenced with rings of swords one after each other"):

11. **The swords form in waves of wide, scattered rings, one after another,** the innermost first, each further out than
    the one before and each a loose band: a sword's place in and out of its ring and round it, its launch within its
    wave and how much its flight bends are drawn at random. This replaces decision 2's and Goal 4's single ring.

## Calls made here (not the user's; check them)

- **The main thread draws swords** from volley records at the exact render time; the logic only schedules hits.
- **No prediction:** today's input-to-screen time is the bar, recorded.
- **Elements:** an elemental attack's hit and ticks are one kind; a reapplication refreshes, no stacks.
- **Bolt and lance targets:** the bolt hits the enemy nearest the point; the lance hits all in its smaller area.
- **The arena becomes a combat scene; the stress scene doesn't.** A debug-only **Alt+click** keeps selection (and the
  outline demo and `outlines.png`) in combat scenes until chunks 5 and 6.
- **Order:** chunk 3 is merged before chunk 4 starts.
- **The player gets health**; the aura's damage is real and can kill; the debug burst stays numbers only.
- **A respawned creature eases back into an orbit** from the arena's edge.

- The resource is called **Qi**; casting without enough Qi does nothing but cue the HUD.
- **A volley is one record** whose swords are a pure function of time (not 1,000 simulated entities): it keeps the
  simulation cheap and deterministic, and lets the renderer place swords at the exact render time.
- Enemies are tagged when the cast starts; swords are spread among them; with none tagged, the swords strike the point.
- The dissolve is computed in screen pixels, so its blocks stay crisp at every zoom.
- Burn and poison tick once a simulation second.

## Testing

- **Determinism:** a recorded fight (casts of every ability, deaths, respawns) replays with the same state hashes; the
  save round-trips mid-volley.
- **Abilities:** Qi gained and spent; cooldowns block a recast; a cast without Qi does nothing; a cast outside a combat
  scene is ignored; the loadout's parsing and its refusals.
- **Swords:** the path function is smooth (no corner: the direction changes by less than a bound between samples) and
  its end follows a moving enemy; every sword's hit is scheduled once; the use ends with the last; hits split evenly over
  the tagged enemies; a dead enemy's swords move to the living; with all dead, swords plunge and no event is emitted;
  a click casts once, a held button doesn't repeat.
- **Elements:** a burn and a poison tick their count and stop; each tick an event of its kind.
- **Death:** health to zero gives a dying state, then removal, then a respawn on the edge.
- **Pictures:** a reference picture mid-volley (the ring and the converging paths) at fixed time; the dissolve at half
  progress.
- **Frame time:** a 1,000-sword cast at its peak, recorded against the phase bar; input-to-first-sword latency recorded.

## Left to the builder

The path's exact curve and timings (within "form in waves of rings, then sweep and converge"); the stagger; the hit-area
radius; the abilities' numbers; the dissolve's block size; the trail length and how its ribbon is built; the HUD's
layout; commit split and order.
