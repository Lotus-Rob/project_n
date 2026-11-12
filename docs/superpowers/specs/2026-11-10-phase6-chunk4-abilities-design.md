# Phase 6, chunk 4: abilities and the flying swords

Date: 2026-11-10 · Status: **implemented 2026-11-11** (approved 2026-11-10; see Outcome) · Phase spec: `2026-11-10-phase6-design.md` · New: no earlier design

## In short

- **An ability loadout:** generator (left click), spender (right click), movement (Space), four specials (1–4, each a
  cooldown and a resource cost), an ultimate (5, a very long cooldown). Set in data for now; the UI to equip comes later.
- **The flying swords** (special 1): **1,000 swords form round the player in waves of still rings, one after another,
  each a regular sunburst of swords pointing straight out; then they leave at random times and flow, on curving paths,
  into the area under the mouse**, striking every enemy tagged there. Bright trails and a soft glow on each sword.
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
   - on the key press, 1,000 swords **form in waves of still rings, one after another, small to large** ("o then O"):
     each ring a **regular sunburst** round the player, its swords evenly spaced and packed side by side, each pointing
     straight out with its hilt on the circle, drawn in one quick clockwise sweep as they fade in where they stand; the
     rings hold still a moment, then each sword leaves after a short random delay, so a ring forms evenly and dissolves
     into flight unevenly. The user's decisions of 2026-11-11, recorded in the Outcome: first "waves of wide, scattered
     rings, one after another" (it was "burst out of the player in a ring"); then, after seeing them spiral out, "Can we
     have it more of a static circle like o O than a spiral?", and with a reference image (the caster at the centre of
     a sunburst of swords, each pointing straight out, hilts on the circle, evenly spaced and packed side by side,
     standing still as a formation): "My vision was more like this … Not the spiral we're getting";
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
  time, its enemy's position): appearing still at its place on its ring, pointing out, then along a curved path (a
  smooth spline whose control points sweep round the caster) whose end **follows its enemy**. The simulation schedules
  each sword's arrival tick when the volley starts; at that tick the hit lands on the sword's enemy if alive, else on
  another tagged enemy still alive (reassigned the moment its enemy dies, so the sword visibly turns), else nowhere: the
  sword plunges into the ground at its last aim point.
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

The user's decisions taken while building it (the swords' waves of rings, their tinted cores, their slower flight, their
rings as still sunbursts) are in the Outcome.

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

## Outcome

Implemented on `worktree-phase6-chunk4-abilities`, 2026-11-11, by the plan
`docs/superpowers/plans/2026-11-11-phase6-chunk4-abilities.md` (Tasks 1 to 10, with Task 9b added mid-run). This records
what was built, the user's decisions taken while building it, every ruling of the plan's ledger, the values chosen, the
measurements behind them, and what is left open. Commits are named by subject: the rebase merge rewrites every hash.

### What was built

- **Task 1, the abilities as data:** "read the abilities and the loadout"; "cap a volley and check the dash, share the
  number reader"; "cover the affliction ticks limit". `assets/data/abilities.yaml` (Qi, every ability's numbers, the
  loadout), `data::ParseAbilities` with its refusals, `core::Abilities`, `CreatureData::health`, an `abilities` yaml fuzz
  seed; the spec's Accessibility section ("make colours recolourable, add chunk 4 accessibility").
- **Task 2, the colour table:** "draw every UI colour from one table in the settings"; "show dark number colours, keep
  numbers off the table"; "drop the number placement's unused outline and shadow". `core::UiColours`, grouped by area as
  the accessibility menu will show them, read by every renderer path; no UI or effect colour is a constant in code or a
  shader.
- **Task 3, health and death:** "give creatures health, a dissolving death and a respawn"; "clear a removed player
  before respawns, refuse dying picks"; "save mid-arrival and refuse broken arrivals"; "check a three-billion number's
  value, not a string". i64 health, the aura's real damage through `game::HitAmount`, the blocky dissolve in the sprite
  shader, removal, a respawn at the ground's edge easing into its orbit.
- **Task 4, casting input:** "send a cast per press of an ability's control"; "find a replay's first action by its layout
  in one helper"; "read Space, 1 to 5, the right button and Alt from SDL"; "look a slot up in a table and aim keys at the
  cursor". `ActionKind::Cast` (slot and world point, replay format 4), the controls, one cast per press.
- **Task 5, Qi, cooldowns, the dash:** "spend and gain Qi, cool abilities down, dash"; "dash on the cast tick and test
  the cast's edge cases". `game::Caster`, `NotEnoughQi`, the dash.
- **Task 6, the swords' path:** "shape flying swords as a function of time"; "swing a sword's aim to a new target when
  its target dies"; "keep a sword on its target near and after its arrival"; "cover the swing's arrival clamp and the
  commit boundary"; "fit a sword's retarget swing in a commit window per volley". `core::SwordAt`, `SwordAim`,
  `SwordTarget`, the stateless retarget chain and the commit window (rulings 22 to 37).
- **Task 7, the volleys in the simulation:** "pin the commit window's floor and a sword moving twice"; "cast the flying
  swords and the fire bolt"; "refuse a save whose volley aims at no creature"; "end a volley's use when its last sword
  resolves"; "profile the volleys' publish"; "cover a burn across a save and two overlapping volleys". `game::Combat`'s
  volleys, scheduled arrivals, the burn, saves of all of it (arena format 3).
- **Task 8, drawing the swords:** "draw flying swords with halos and trails at the render time"; "spiral the swords out
  and spread their aim over a target"; "tint the swords' glow and show health bars as their labels"; "leave a sword out
  whole when its trail has no room"; "check that the swords' aim spots spread evenly over the disc". `gfx::DrawVolleys`
  (tinted cores, additive halos, swords at the exact render time), the additive ribbon pipeline for trails, the enemies'
  health bars.
- **Task 9, the lance and the HUD:** "strike with the poison lance and show a temporary HUD"; "strike with the lance
  after the tick's due afflictions"; "refuse a save whose flash starts after its tick"; "ink the HUD's slot lines when
  text has no outline or shadow"; "pre-warm the HUD's texts as filled in with numbers"; "take the HUD's room first, so an
  overflow never drops it"; "name the HUD among the main thread's systems". The lance's strike, poison and burst
  (`core::Flash`), `app::Hud` (health and Qi bars, the slots, "Not enough Qi").
- **Task 9b, waves of rings** (the user's decision below): "form chunk 4's swords in waves of wide rings" (the plan);
  "form the flying swords in waves of wide, scattered rings"; "form the flying swords in waves of rings, as decided";
  "cover an uneven wave split and the shortest flight"; "refuse a missing or negative scatter, a negative jitter"; "name
  the abilities' number reader for what it checks"; "rewrap the swords' summary line".
- **Task 10, the recorded fight, the measurements, this record:** "scale a packed colour in one place" (one
  `core::ScaledColour`, with `core::OpacityOf`, for the HUD, text and volley drawings); "ink the HUD's slots as opaque as
  its text, as labels are"; "fuzz a save taken mid-fight, loaded at the largest tick"; "replay a recorded fight with every
  ability"; "cast the swords on a timer for profiling, log their latency" (`--cast-swords`, `app::TimedCast`,
  `app::CastLatency`); "slow the flying swords, as the user asked"; and this record ("record what chunk 4 built"). The
  branch's first full `check` then found what native builds can't: "build the HUD tests with GCC"; "construct the HUD
  with parentheses for MSVC" and "build the branch's tests with MSVC" (C4866, C4868, C5258); "copy the swords' shape for
  the pictures, bound their waits" (the volley pictures read the swords' shape through a reference into a temporary;
  linux-gcc's release read garbage and ticked towards tick 2.9 billion, hanging `check` for an hour; every wait in the
  reference suite now fails loudly instead); "name the abilities reader's refusal for its file" (a unity-build clash).

- **Task 10b, every tuning value as data** (the user's request, below): two tables. "read the abilities' motion and the
  combat rules from data": the projectile abilities' motion joins `core::VolleyShape` (read from each ability's `motion:`
  block in `abilities.yaml`, saved with the volley so a volley in flight keeps the motion it was cast with): `aim_scatter`,
  `short_bend`, `turn`, `arrival_speed`, `retarget`, `retarget_speed`; `CommitTicksFor` takes the shape, and
  `VolleyShape::RetargetPeak()` follows from `retargetSpeed`. A `combat:` section reads into the new `core::CombatRules`
  in `GameData` (hashed): the dissolve, respawn, arrival and affliction times, the lance burst's length (`Flash::ticks`,
  saved with the flash) and the crystal's aura (period, reach, damage and healing ranges). "read the effects' looks and the
  HUD's layout from data": `assets/data/effects.yaml` into `core::Effects` (`data::ParseEffects`; not hashed or saved):
  per ability by name the sword, halo and core sizes, the trail's reach, width and taper, the glow's intensity and the
  core's opacity, the lance burst's growth and intensity; the health bars' sizes; the HUD's layout and its warning. The
  data watch reloads it on the main thread and hands it to `gfx::Renderer::SetEffects` and `app::Hud::SetEffects`, so a
  change shows the next frame with no new timeline. Every value keeps its number as the default (`core::VolleyShape{}`,
  `core::CombatRules{}`, `core::Effects{}`, each checked against the shipped YAML), so no picture changed (`reference`,
  no `--update`); the only hash change is the game data's, from the new fields. The `save` fight seed was regenerated (a
  volley's shape and a flash gained fields; the arena format is this chunk's own 3, so no bump), and the `yaml` fuzz target
  also parses `effects.yaml`, with its seed. Stays in code, each documented where declared: engine limits
  (`MAX_VOLLEY_COUNT`, `MAX_OVERLAY_INSTANCES`, `MAX_RIBBON_QUADS`, `SWORD_INSTANCES`, `TRAIL_SEGMENTS`, which size
  buffers; `MIN_COMMIT_TICKS`, a floor), maths (`THIRD`, `HALF`, `HERMITE_*`, `FULL_TURN`, `SIDE_ARM`), formats and
  versions, random purposes, `CAST_SOURCE`, the debug burst and the arena's layout (scene data, chunk 7). The abilities'
  other numbers were data already. Two rulings: a time in `effects.yaml` is seconds (`trail`, `not_enough_qi`) as
  elsewhere, ticks in `core`; `MAX_ARRIVAL_SPEED` (3) bounds `arrival_speed`, past which the arrival's ease stops being
  monotonic.

### The user's decisions while building it

- **At the start (2026-11-11):** fast combat and slow long-term progression, so health, damage, Qi and every hit's value
  are i64 and every amount an ability deals passes through one function, `game::HitAmount`, where stats and levels plug in
  later; every UI colour in one table, by area (**Accessibility → Colours → area → element**); effects coloured by
  element, never by ability; health and Qi as two bars in the HUD; the specs amended to say so in Task 1.
- **A glow and a tinted core** (ruling 44, 2026-11-11): the plan's additive (ONE, ONE) halos and trails lost the
  element's hue on the pale paper, reading white. The glows stay additive, and each sword and the fire bolt gets a
  normal-blend core in its element's colour (`core::EffectColour`), with its own opacity, `gfx::CORE_OPACITY` (0.6),
  apart from `HALO_INTENSITY`. The reference picture `bolt_burn` shows the bolt's orange on the pale ground.
- **Waves of rings** (2026-11-11, after seeing the swords form a single ring: "Can we make the sword spread much wider so
  the 1000 swords don't just end up looking like a circle? Much wider, much more spread, much more randomness in the
  creation? Make the creation semi-sequenced with rings of swords one after each other"; it was the spec's decision 11):
  **the swords form in waves of wide, scattered rings, one after another,** the innermost first, each further out than
  the one before and each a loose band: a sword's place in and out of its ring and round it, its launch within its wave
  and how much its flight bends are drawn at random. This replaces decision 2's and Goal 4's single ring. Shipped: 5
  waves 0.2 s apart, rings from 160 to 560 world units (100 apart), scatter 0.25 of the radius, 0.5 rad of angle jitter,
  sweep jitter 0.6, launches staggered over 0.15 s within a wave. Task 6's rules and smoothness bounds are unchanged.
- **Slower swords** (2026-11-11, during Task 10: "I think the swords should be slower"; ruling 56): each sword's flight
  1.1 → **1.7 s** (102 ticks) and its burst out to its ring 0.25 → **0.35 s** (21 ticks), about 1.5 times as long; the
  waves, their gap, the stagger, the rings, the jitters and the fire bolt unchanged. A volley now lasts about 3.3 s
  (was 2.3), inside the swords' 6 s cooldown. Every test and picture derives its ticks from the shape and passes as it
  was.
- **Every tuning value as data** (2026-11-11, during Task 10, for a future ImGui tuning UI): "Maybe we could plan in a
  debug UI with imgui in the future and all these values could be tweakable so we can tune it visually. Just ensure all
  the values like the distance, speed, range, shape, convergence, everything etc is data-driven and able to be tuned."
  Built as Task 10b, below; the ImGui UI itself is a later plan.

### Rulings

In the order the plan ledger took them. Each is a call made where the plan or spec was silent or wrong, with its reason
and what it costs if wrong. "The user's" marks the user's own; "implementer's" marks a builder's call the review
accepted.

1. **The text ink comes from the table's packed 8-bit value;** the shader's old float `INK` differs by under 0.002
   linear, so existing pictures had to pass within the existing tolerance without `--update`: exact sameness isn't
   possible with an 8-bit table entry. If wrong, one reference refresh.
2. **Task 9 may change `gfx/volley_draw` (`DrawFlashes`) and `core/frame.hpp` (`Flash`, `Snapshot::flashes`),** which its
   file list omitted: its interfaces need them. Costs nothing.
3. **The renderer's per-frame HUD input is `gfx::HudDraw { lines; bars }`,** not `gfx::Hud`, so it doesn't share a name with
   `app::Hud`: readability. If wrong, a rename.
4. **`Event::value` and `TextItem::value` stayed i32 in Task 1;** Task 3 widened them as the plan said. Costs nothing.
5. **A volley's count is capped at `core::MAX_VOLLEY_COUNT` = 2,000:** the overlay then held 4,096 instances, two per
   sword (now 8,192 for three per sword: core, halo, blade). If wrong, raise it with the buffer.
6. **sRGB round-trips "within one linear step"** for every default, and exactly for the new defaults named by an sRGB
   colour: an 8-bit linear table can't round-trip every sRGB byte. **This corrects the plan's "exact sRGB round trip",
   which was impossible:** several sRGB bytes share one linear byte in the dark, and some bright linear bytes are
   reachable from no sRGB byte. If wrong (exactness wanted), a wider table in chunk 11.
7. **`TextItem::colour`'s doc needed no change:** it already says "Of a Number". Costs nothing.
8. **`ui_colours.cpp`, missing from Task 2's file list, stays:** `FromSrgb` and `ToSrgb` need a body. Costs nothing.
9. **`HitAmount` has an i64-only overload for the aura,** which the ability version calls: one place for stats.
   Costs nothing.
10. **The dissolve is (tick − start) / (`DYING_TICKS` − 1),** so it runs 0 to 65,535 over `DYING_TICKS` and the fox is
    gone on its last drawn tick: the brief's formula contradicted its own test. If wrong, one constant.
11. **The dissolve steps per tick** (no render-clock interpolation), like an instance's size and region: 30 steps over
    half a second, and an instance has no previous-dissolve field. If it looks stepped, a field and a shader lerp.
12. **Player death stays undefined in this chunk** (the spec: the player takes no damage); the removal path is only made
    safe and logged at Info. What death does belongs to a later chunk's design. (Open below.)
13. **A Pause, a Cast and a Resume in one batch let the Cast run:** the logic's rule is that the batch's last Pause or
    Resume decides the tick, so that tick isn't frozen; the review focus was casts on frozen ticks. If the user wants a
    cast sent while unfocused always ignored, a drop in the logic.
14. **The editor's `pro-type-member-init` on `cast_tests.cpp`'s `Click` is a false positive:** both members have
    initialisers, and clang-tidy with the repo's config is clean. Costs nothing.
15. **Key casts aim at the cursor whenever it is over the window;** the dead "gamepad in use" guard goes, as a key press
    always makes the keyboard the device in use. When gamepad aiming lands, a device check before the press.
16. **`CastAction` keeps its `pressed` parameter** though App filters releases: the brief specifies it and its tests use
    it. Costs nothing.
17. **The re-review's "include `<array>`, `<span>`, `<algorithm>` in `cast.cpp`" is wrong for this repo:** a `.cpp` never
    includes a standard header; `pch.hpp` holds them and lint fails on one. Costs nothing.
18. **A dash moves the player on its cast tick:** a tick's delay is input lag, a spec anti-goal. If wrong, one easing
    tweak.
19. **A cast during a dash is ignored,** logged at debug: the dash is 0.15 s, so fast combat stays responsive. If the user
    wants bolt-while-dashing, a queue or an allow rule.
20. **A loaded Qi above the most is lowered by `ApplyData` at the end of `Arena::Load`,** so `Valid()` needn't know the
    data's most; a test pins it. Costs nothing.
21. **No `<cmath>` in `combat.cpp`:** the `pch.hpp` rule, as ruling 17. Costs nothing.
22. **Swords burst out eased to rest on their ring, then turn in place over the out phase's last 6 ticks** to face their
    flight (at most about 0.1 rad per eighth of a tick), instead of the plan's first control point along the burst
    direction: the literal plan made a cusp (a 3.0 rad jump) for some sword in every ring, and resting on the ring
    matches the reference ring image. If the user wants no pause, a reshaped curve.
23. **Swords arrive at half their mean speed** (`ARRIVAL_SPEED`), so they strike rather than settle: feel. If wrong, one
    constant.
24. **The dead-target spread test asserts {334, 0, 333, 333}:** what the brief's own formula gives (its 83/83/84 was an
    arithmetic slip). Costs nothing.
25. **Retargeting is a stateless chain:** `VolleyTarget` gains `diedTick` (its position frozen from its death); a dead
    link's swords pick among the targets alive at its death tick, spread evenly, repeating at each later death; with
    none alive the chain ends at the last link's frozen point, where the swords plunge; `core::SwordAim` eases the aim
    from the dead link's frozen point to the new link over `RETARGET_TICKS` (about 8). It gives the spec's "visibly
    turns" and "plunges at its last aim point" with no per-sword state, shared by the logic and the renderer. If the user
    wants instant switches, a different retarget rule.
26. **The heading flip near a sword's ring point is irreducible for any stateless rule** (a winding argument): documented,
    not fixed. Task 8's pictures and numbers found no snap (worst heading change 0.097 rad per eighth of a tick). If it
    shows, a rate limit in the renderer.
27. **The swing takes max(`RETARGET_TICKS` = 8, distance / `RETARGET_SPEED` = 16 units a tick)** (implementer's): a fixed
    8 ticks darted 3.4 times the median step across long swings late in flight. Costs nothing.
28. **A commit window and a clamp:** a sword moves on only if its link died at least the commit window before its
    arrival; otherwise it drives into the corpse and plunges, with no hit. The swing is clamped to end by the arrival,
    so the aim at arrival equals `SwordTarget` at arrival bit for bit, and a tick's arrivals resolve before its deaths:
    the hit and the picture always agree, and nothing slides during the fade. **Overkill swords within the commit window
    are wasted:** they plunge into the dead target (see Notes). If overkill waste matters, a different commit rule.
29. **The spread digit became a per-sword, per-link `core::Random` draw** (no `sword / n`): the old digits ran out with many
    targets and piled onto the first living one. Refined by ruling 30. Costs nothing.
30. **The spread keeps `sword / n` digits plus a drawn offset per remaining target** (implementer's): moved groups still
    split exactly evenly (84/83/83), and small groups no longer pile onto the first living target. Costs nothing.
31. **A swing clamped by the arrival may exceed `RETARGET_SPEED`** when tagged targets are more than about 500 apart (the
    tag radius of 260 bounds it at about 33 units a tick, still continuous): accepted, judged by eye in Task 8's
    pictures. If it reads as a dart, a longer commit window.
32. **A struck sword following a still-living target through its fade needs no change:** Task 8 hides a sword at its
    arrival when it hits; only plunged swords stay and fade. Costs nothing.
33. **The commit window is per volley, resolved at the cast like `flightTicks`:** `VolleyShape::commitTicks` =
    max(2 × `RETARGET_TICKS`, ⌈2 × radius / `RETARGET_SPEED`⌉) (the swords: 33 ticks), so every swing fits at
    `RETARGET_SPEED` and the plain smoothness bound holds in every test: jittery swords are a spec anti-goal, and a 5×
    dart near impact is one. It costs more late-death plunges for wide-radius abilities (swords within about 0.55 s of
    arrival are committed).
34. **During a retarget swing the step bound is max(3 × the sword's median step, `RETARGET_PEAK`, the eased swing's
    designed peak),** the plain bound elsewhere, and a fixed 16-tick window (the old dart) must fail it: the anti-goal is
    darting faster than the designed pace, not being faster than one slow sword's own flight. Rejected: a
    `RETARGET_SPEED` of 10 (only the first 14 or so flight ticks could retarget) and moving the test's targets. If the
    swing reads as a dart, a slower retarget speed.
35. **The swing bound is max(3 × median, median + `RETARGET_PEAK`)** (implementer's): the sword's own motion continues
    under the swing; `RETARGET_PEAK` = 1.5 × `RETARGET_SPEED` = 24 units a tick. Costs nothing.
36. **The 800-apart test** (targets beyond the tag radius on purpose, to reach the clamp) **bounds its swing by the same
    ease over the commit window**, not `RETARGET_PEAK` (implementer's). Costs nothing.
37. **Task 6's last two review findings** (the commit floor tested only at 0; the double-move case perhaps no longer
    exercised) were real and not load-bearing; at the fix-round cap they went into Task 7 as two tests rather than
    being parked (the user's rule: fix every finding). Costs nothing.
38. **Swords landing on the tick their target dies, after the killing blow, still emit their hits** (overkill numbers),
    though `Damage` ignores a dying creature: Task 6's rule that a tick's arrivals resolve against the targets as at the
    tick's start, and the picture shows them striking. **Likewise a killing affliction tick lets the other element's tick
    on the same tick still hit** (see Notes). If the user wants no overkill numbers, suppress hits after a kill within a
    tick.
39. **A load emits no `UseEnded`:** `Simulation::Load` takes no events, and the new epoch clears the damage numbers and
    drops the old epoch's events, so nothing waits on the old uses; the plan's "or a load" is met by the epoch rule.
    Costs nothing.
40. **`DrawVolleys` takes (simulation tick, alpha)**, as `SwordAim` does, and struck-or-plunged is
    `core::VolleyTarget::Struck`, shared with the logic's arrivals (implementer's): one rule for both threads. Costs
    nothing.
41. **The burst spirals out** (a tangential part in the swirl's sense) instead of a straight radial line, with Task 6's
    smoothness bounds unchanged: the spec's "no straight segments". If it reads as too twisty, a retune.
42. **A per-sword aim offset from the volley's seed, a fraction of the creature's size, always applied** (never only after
    a death): it spreads strikes and plunges over the body instead of one bright disc; hits and ticks are unaffected.
    Costs nothing.
43. **Health bars follow their creature's label:** they fade with "too small to read" and hide when the label is stacked,
    so health by length stays readable. If bars should outlive labels, a rule of their own.
44. **The user's: glows stay additive, and each sword and the bolt gets a tinted normal-blend core** in its element's
    colour, with its own `CORE_OPACITY` (see the user's decisions above). This changes the plan's ONE, ONE-only effects.
45. **`AIM_SCATTER` stays a fixed 18 world units** (about a quarter of a fox), not a fraction of each target: foxes are the
    only enemies, and a per-target reach would change the save and the hash. **Revisit when a later chunk adds bigger
    enemies.**
46. **A halo fades as its sword's shown²,** so a plunged sword's glow fades faster than its blade. Costs nothing.
47. **`app::Hud` took a text scale** (implementer's): settings have no text size until chunk 11, which passes one scale
    to the HUD and the renderer; Task 9's fixes turned it into `gfx::TextOptions`. Costs nothing.
48. **HUD lines over the ground follow the labels' rule** (`TextInk` when the outline and the shadow are both off); the
    words on the bars keep `HudText`, on their bars: readable on pale ground with the accessibility options as they
    exist. Costs a revisit with chunk 11's high-contrast option.
49. **In ink mode a cooling slot's line is `TextInk` at a named, reduced opacity** (`HUD_COOLING_INK_OPACITY`, 0.55) and
    still shows its seconds: cooling is never colour only. Costs nothing.
50. **The lance applies its hits and afflictions in `Strike`, after the tick's due afflictions,** as the bolt and the
    swords do: one refresh rule for every ability (see Notes). Costs nothing.
51. **The HUD reserves its text quads and overlay room first;** an overflow drops world effects, never the HUD. Costs
    nothing.
52. **`Arena::Load` refuses a flash that starts after the loaded simulation tick** (`core::Simulation::Load` takes the
    save's tick). Costs nothing.
53. **A sword's wave is a run of indices, not drawn** (implementer's): it keeps each wave spread over every target. Costs
    nothing.
54. **`sweep_jitter` must be below 1** (implementer's): it keeps every bend on the swirl's side. Costs nothing.
55. **"The path curves" is measured against min(chord, sweep)** (implementer's; the review found it principled): an outer
    ring's swords form near the point, so their chord can be shorter than the sweep. Costs nothing.
56. **The user's: slow the swords about 1.5 times** (flight 1.7 s, out 0.35 s; above). If the user wants them slower or
    faster, one data line.
57. **`--cast-swords` tops the Qi up with the generator** (Task 10): the swords cost 60 Qi and the player starts with 50,
    so a timer cast alone would fail with `NotEnoughQi`. `app::TimedCast` casts special 1 when it is ready and the Qi
    covers it, and otherwise, while the Qi is short, the fire bolt when it is ready, as a player would: no game rule or
    data changes for the profile run. The timer casts the swords 4 s after the last, or as soon after as they are ready,
    so with their 6 s cooldown it casts about every 6 s. Its bolts (about three a cycle) are in the profile, as in play; if a swords-only profile is wanted, a documented
    Qi grant for the run.
58. **`app::CastLatency` watches one ability's volleys and forgets a press after `CAST_LATENCY_LIMIT_NS` (1 s)**
    (Task 10): `--cast-swords`' bolts are volleys too, and a refused press (cooldown, Qi, pause) would otherwise lengthen
    the next press's latency. A new volley is one whose use the previous frame didn't draw. Key presses of special 1 are
    measured as well as the timer's. Costs nothing.
59. **The `save` fuzz target loads every input as saved at u64's largest tick but one** and ticks and publishes at the
    largest (Task 10): any flash a save can name then loads, so fuzzed flashes reach the code after the load (at tick 0
    only a flash starting at 0 could). Volleys and respawns in the seeds are all overdue at that tick, so they exercise
    the catch-up path; mid-flight states are reached by mutation. Costs nothing.

Not ledger rulings, but calls made in measuring: the runs are unpinned (30 workers, as chunk 3's), and Tracy's CSV
exporter was again read from Pixi's package cache with `pixi.toml` unchanged.

### Notes for later chunks

- **Overkill numbers on a death tick** (ruling 38): swords arriving on the tick their target dies, after the killing
  blow, still show their numbers; and when a burn tick kills, a poison tick due on the same tick still hits and shows.
- **Overkill swords within the commit window plunge and are wasted** (rulings 28, 33): a sword whose target dies less
  than the commit window (33 ticks for the swords) before its arrival drives into the corpse and hits nothing.
- **The lance strikes in `Strike`, after the tick's due afflictions** (ruling 50), not at the cast: a refresh never
  swallows a tick that was due.
- **`AIM_SCATTER` is fixed at 18 world units while foxes are the only enemies** (ruling 45).
- **The plan's "exact sRGB round trip" was impossible** (ruling 6): within one linear step for every default, exact for
  the defaults named by an sRGB colour.

### Values chosen

- **Abilities** (`assets/data/abilities.yaml`): Qi 100 at most, 50 at the start. Fire bolt: generator, 0.25 s cooldown,
  +10 Qi, 140 burn damage, radius 120, 1,500 units a second, a burn of 3 ticks × 40. Poison lance: spender, 0.5 s,
  25 Qi, 360 poison damage, radius 90, a poison of 4 ticks × 60. Dash: 1 s, 260 units over 0.15 s. Flying swords:
  special, 6 s, 60 Qi, 20 damage a sword, radius 260, 1,000 swords in 5 waves (above), out 0.35 s, flight 1.7 s, sweep
  420, fade 0.3 s. Fox health 1,200; the player's 1,000.
- **The path:** `RETARGET_TICKS` 8, `RETARGET_SPEED` 16 units a tick, `RETARGET_PEAK` 24, `MIN_COMMIT_TICKS` 16 (the
  swords' window 33), `AIM_SCATTER` 18, `MAX_VOLLEY_COUNT` 2,000; swords drawn at 0.6 of their art.
- **Death:** `DYING_TICKS` 30 (half a second), `RESPAWN_TICKS` 180, `ARRIVE_TICKS` 180; the dissolve's blocks 4 screen
  pixels; afflictions tick every 60 ticks.
- **Drawing:** `TRAIL_TICKS` 6 over `TRAIL_SEGMENTS` 8, `HALO_INTENSITY` 1, `FLASH_INTENSITY` 1, `CORE_OPACITY` 0.6;
  `MAX_OVERLAY_INSTANCES` 8,192 (three a sword), `MAX_RIBBON_QUADS` 16,384; health bars 44 × 6 (fill 40 × 4), 5 above
  the label.
- **The HUD:** 18 px text, 16 px margins, bars 240 × 18, lines 24 apart, "Not enough Qi" for 1 s, cooling ink at 0.55.

### Measurements

**`DrawVolleys` while building** (Tasks 8 and 9b, release, a temporary doctest in the reference target, deleted before
each commit: the arena's cast, 1,040 calls over 130 ticks × 8 render times, 1,000 swords, timed with
`platform::NowNs()`; per frame, mean / worst):

| `DrawVolleys` | Task 8 | Task 8's fixes (cores, aim spread, ribbon room) | Task 9b (waves of rings) |
|---|---|---|---|
| 30 workers | 166 / 392 µs | 201–205 / 469–475 µs | 243–247 / 449–460 µs |
| 1 worker and the main thread | 281 / 662 µs | 328–330 / 758–859 µs | 363–365 / 780–792 µs |

Each step stayed under the review's 1.5× threshold (about 1.2× and 1.1×). Over the whole 9b volley (160 ticks) it was
225–229 / 427–482 µs (all workers) and 326–338 / 649–650 µs (1 worker). The upload is about 3,000 overlay instances and
8,000 ribbon quads a frame; the GPU's side was not measured on its own.

**Frame time with the shipped (slower) swords** (Task 10): `pixi run project_n profile --seconds 70 -- --cast-swords`
and a baseline without the flag; releasedbg, linux-clang, the arena, 1280×720, Wayland, NVIDIA RTX 4090, presenting
with mailbox (unthrottled, about 1,000 frames a second). **Unpinned: the game ran 30 workers**, as chunk 3's runs did
(the game takes `Jobs::DefaultWorkers()`; `taskset` doesn't change it, as `hardware_concurrency` ignores the affinity), so
the phase bar's 2-worker run still waits for chunk 5. Read with `tracy-csvexport -u`; a frame is one iteration of
`App::Run`'s loop on the main thread. The first two seconds are skipped, and so are the stretches where the game was
paused: **the game window lost its focus during every run** (someone used the desktop; one run was closed early, and
the baseline run caught two casts by hand), so the figures come from the unpaused stretches of capture
`20261111-233050` (21 s, 3 volleys; frames with and without volleys from the same run) and `20261111-233739` (18 s,
3 volleys), which agree. "A volley's peak" is the stretch where all 1,000 swords fly: from 1.0 to 2.0 s after the
volley is first drawn (the last wave launches by 0.95 s, the first sword arrives at 2.05 s).

| Main thread, one loop iteration | Median | 99th percentile | Worst |
|---|---|---|---|
| Without volleys (13,971 frames; baseline run 32,843 frames: 0.286 / 2.04 / 3.25 ms) | 0.210 ms | 2.00 ms | 3.19 ms |
| With a volley drawn (8,209 frames; second run 7,619: 1.32 / 2.17 / 3.29 ms) | 1.31 ms | 2.20 ms | 2.92 ms |
| At a volley's peak (2,373 frames; second run 2,459: 1.32 / 2.17 / 3.29 ms) | 1.31 ms | 2.31 ms | 2.60 ms |

No frame came near the 16.7 ms bar (the worst of all, with or without volleys, is 3.3 ms); no run logged an overflow,
and no frame is a finding. Per call at a volley's peak, microseconds, median / 99th / worst (the second run in
brackets):

| Zone | At the peak | Without volleys |
|---|---|---|
| `DrawVolleys` | 298 / 472 / 585 (291 / 504 / 739) | — |
| The ribbons' `Upload` (the trails' 8,000 quads) | 28.9 / 57.7 / 162 (29.1 / 60.3 / 153) | 0.08 / 1.1 / 53 |
| The renderer's `Upload` (the overlay, ribbons, text and HUD, `DrawVolleys` included) | 368 / 552 / 706 | 23 / 49 / 179 |
| `SubmitFrame` (waits on the driver and the GPU) | 895 / 1,841 / 2,215 | 122 / 1,921 / 3,117 |
| The logic's tick, `Arena::Tick` | 11.0 / 29.2 / 41.5 (10.4 / 21.3 / 22.0) | 10.2 / 25.2 / 146 |
| The logic's whole tick and publish, `RunTick` | 13.2 / 33.0 / 43.3 (12.5 / 31.1 / 33.7) | 12.5 / 44.0 / 149 |

A volley adds about 1.1 ms to the median frame: 0.35 ms of the main thread's own work (the overlay and the ribbons) and
the rest in `SubmitFrame`, waiting on the GPU for the extra 3,000 instances and 8,000 quads at an unthrottled frame rate.
The logic thread doesn't notice a volley (one record, its arrivals scheduled): about 11 µs a tick with or without one.

**Input to the first sword** (`swords first drawn N ms after the press`, the log at debug level): eleven presses with
the shipped swords, eight by `--cast-swords`' timer and three by hand (key 1, in the runs above): **16.8, 16.9, 17.2,
17.3, 17.3, 18.0, 25.1, 26.3, 26.8, 26.9 and 27.3 ms**; median 18.0, mean 21.4. The first swords cast of each run
takes 25 to 27 ms and every later one 17 to 18 ms; the floor is the display delay (10 ms) plus the wait for the logic's
next tick (up to 16.7 ms); the first cast's extra 8 to 10 ms is likely its buffers' first growth and cold caches (not
profiled further). An earlier trial before the
slowing gave 28.1 ms for a first cast.

### Open for the user

- **Player death is undefined** (ruling 12): nothing damages the player yet; if something did, the cultivator would be
  removed and the camera would stop following. What death does is a later chunk's design.
- **The damage element's default colour, `#CFE3FF`, is near-white,** so the swords' tinted cores add only a faint blue
  cast on the pale ground. Deepen the default? The user's call (the table makes it one value).
- **Whether the waves read as separate rings in motion** (Task 9b's review): a still reads as one wide swirl. Levers, all
  data: lower `scatter`, a longer `wave_gap`, a short hold on the ring.
- **Every tuning value as data** (Task 10b, planned): the motion constants above, the combat rules and the drawing's
  looks move into the data tables.
