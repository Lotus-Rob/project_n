# Phase 6, chunk 4: abilities and the flying swords

Date: 2026-11-10 · Status: **implemented 2026-11-12** (approved 2026-11-10; see Outcome) · Phase spec: `2026-11-10-phase6-design.md` · New: no earlier design

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
- **Reduce flashing (chunk 11 wires it):** the halos and the bolt's glow take their brightness from one value per
  ability, `halo_intensity` in `assets/data/effects.yaml` (`core::SwordLook::haloIntensity`), and the lance's burst
  from its `flash_intensity` (`core::BurstLook::intensity`). Chunk 11 lowers them under reduce flashing. The dissolve
  fades and never flashes.
- **Reduce motion (chunk 11 wires it):** the trail length is one value per ability, `trail` in `effects.yaml`
  (`core::SwordLook::trailTicks`), which chunk 11 shortens under reduce motion. The swords' flight is the ability
  itself and stays. Nothing shakes the camera.
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
rings as still sunbursts almost tip to heel, every tuning value as data, small unit tests, a deeper damage colour) are in
the Outcome.

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

Implemented on `worktree-phase6-chunk4-abilities`, 2026-11-11 and 2026-11-12, by the plan
`docs/superpowers/plans/2026-11-11-phase6-chunk4-abilities.md` (Tasks 1 to 10, with Tasks 9b, 10b, 10c, 10d and 10e
added mid-run for the user's decisions). This records what was built, the user's decisions taken while building it,
every ruling of the plan's ledger, the values chosen, the measurements behind them, and what is left open. Commits are
named by subject: the rebase merge rewrites every hash. Before the pull request, each late fix was folded into the commit
whose code it fixed; builds are verified at the head, not commit by commit (the user's decision below). The subjects
below are the commits as they landed.

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
  health bars. The spiral out is gone since Task 10c's still rings; the aim spread stays.
- **Task 9, the lance and the HUD:** "strike with the poison lance and show a temporary HUD"; "strike with the lance
  after the tick's due afflictions"; "refuse a save whose flash starts after its tick"; "ink the HUD's slot lines when
  text has no outline or shadow"; "pre-warm the HUD's texts as filled in with numbers"; "take the HUD's room first, so an
  overflow never drops it"; "name the HUD among the main thread's systems". The lance's strike, poison and burst
  (`core::Flash`), `app::Hud` (health and Qi bars, the slots, "Not enough Qi").
- **Task 9b, waves of rings** (the user's decision below): "form chunk 4's swords in waves of wide rings" (the plan);
  "form the flying swords in waves of wide, scattered rings"; "form the flying swords in waves of rings, as decided";
  "cover an uneven wave split and the shortest flight"; "refuse a missing or negative scatter, a negative jitter"; "name
  the abilities' number reader for what it checks"; "rewrap the swords' summary line". Task 10c keeps the waves and
  their fields, and ships the scatter and the angle jitter that made the rings loose at 0.
- **Task 10, the recorded fight, the measurements, this record:** "scale a packed colour in one place" (one
  `core::ScaledColour`, with `core::OpacityOf`, for the HUD, text and volley drawings); "ink the HUD's slots as opaque as
  its text, as labels are"; "fuzz a save taken mid-fight, loaded at the largest tick"; "replay a recorded fight with every
  ability"; "cast the swords on a timer for profiling, log their latency" (`--cast-swords`, `app::TimedCast`,
  `app::CastLatency`); "slow the flying swords, as the user asked"; and this record ("record what chunk 4 built", "add
  check's fixes to chunk 4's outcome"). The branch's first full `check` then found what native builds can't: GCC's
  `-Weffc++` and `-Wrange-loop-construct` in the HUD tests; MSVC's C4866 (temporaries inside `CHECK`), C4868 (braces
  round a copied shared pointer, in the App's HUD and its tests) and C5258 (a needless capture); a unity-build clash
  between the data readers' file-local `Fail`s (abilities.cpp's is `RefuseAbilities` now); and a hang: the volley
  pictures read the swords' shape through a reference into the temporary `game::BuiltInData()` returns, and linux-gcc's
  release read garbage and ticked towards tick 2.9 billion, hanging `check` for an hour. Each fix was folded into the
  commit whose code it fixed; what stands on its own is "bound every wait in the reference suite" (a picture more than
  600 ticks after its cast, or a scene not resident within a minute, now fails loudly, naming what never happened).
  Task 10's review round: "pin the recorded fight's hash count and its dash"; "shrink the mid-fight save seed to its
  combat" (209,421 → 4,421 bytes: the world tier and relationships cut out); "match chunk 4's rulings to the ledger".
- **Task 10c, still rings** (the user's decision below): "plan still rings, small tests and tunable data" (the plan for
  10c, 10d and 10b); "form the swords' rings as still circles that hold, then fly"; "describe the swords' rings as still
  sunbursts"; "share the tick a sword leaves its ring" (`core::DepartureTick`, for the logic, the renderer and the
  tests); "sample the formation tests, and the widest turn"; "give each ring swords in proportion to its circumference";
  "stack ten quick rings of swords almost tip to heel"; "draw the flying swords half as large again". A sword waits
  unseen at its own place on its ring, fades in there pointing straight out (no burst, no spiral, no trail), each ring
  drawing itself in one clockwise sweep as its swords appear in index order; it holds, leaves after a drawn delay
  (`holdTicks`, `departJitterTicks`, both data), and turns from outwards to its flight over the flight's first
  `TURN_TICKS` (8) as it eases off from rest. A wave's share of the swords follows its radius, so every ring has the
  same spacing. A sword's trail starts where it left its ring, so a holding sword draws none.
- **Task 10d, small unit tests** (the user's rule below): "sample the volley's behaviour, not every sword and tick";
  "generate the fight save seed reproducibly"; "keep unit tests to a handful of samples" (the rule in tests/AGENTS.md);
  "write the fight save seed from a skipped case" (the generator case skipped by default and run by name, and
  tests/AGENTS.md's one way for a test to write a repo file); "derive the sampled swords from the waves". The sweeping
  volley, volley drawing and combat tests now check chosen samples (each wave's boundary swords, the first and last, the
  shortest chord, the widest turn) and assert each loop's worst value once, with the same bounds.
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
  monotonic. Its review's findings are in those commits: `abilities.yaml` is parsed once for both tables
  (`data::ParseAbilitiesFile`); every default is a named `core::shipped` constant and `BUILT_IN_DATA.combat` is
  `core::CombatRules{}`; a `motion:` block on an ability that flies nothing (neither swords nor a speed) is refused with
  its line, as a dash without its distance is; a test shows a swing following the shape's `retarget` and
  `retarget_speed`; `MIN_COMMIT_TICKS` is twice the shipped retarget ticks, never a tuned one. Recorded here by "record
  chunk 4's tuning values as data".
- **Task 10e, a deeper damage colour** (the user's decision below): "plan a deeper damage colour"; "deepen the damage
  effect's colour". The table's default and its test, the plan's colour list, and the three volley pictures.
- **The final review's fixes** (2026-11-12; rulings 72 to 77), each folded into the commit whose code it fixes, test
  first: a fox's respawn is queued in order, so one that new game data shortened comes on its own tick and the save
  takes it ("read the abilities' motion and the combat rules from data"); a creature with health below 1 is refused
  with its line ("read the abilities and the loadout", then "cap a volley and check the dash, share the number
  reader"); the data watch reloads the game data only when its hash changed, so an `effects.yaml` edit no longer ends a
  replay, decided by the tested `app::ReloadOf` ("read the effects' looks and the HUD's layout from data"); the abilities
  reader's own number and seconds readers gave way to `data::internal`'s, with one seconds limit ("read the abilities'
  motion and the combat rules from data"); stale comments and agent docs (the arena's rings, `--cast-swords`' timer, the
  `yaml` seeds) went to the commits that made them stale; and the HUD's parenthesised construction moved into "strike
  with the poison lance and show a temporary HUD", where its braces were first written.
  One new commit, "start every scene at a chosen zoom for profiling" (`--zoom`, ruling 77), made the frame profile at
  both ends of the zoom possible (Measurements). The review's first finding, an affliction that never ticks while its
  target is hit faster than its period, was not changed: the user's decision below.

### The user's decisions while building it

- **At the start (2026-11-11):** fast combat and slow long-term progression, so health, damage, Qi and every hit's value
  are i64 and every amount an ability deals passes through one function, `game::HitAmount`, where stats and levels plug in
  later; every UI colour in one table, by area (**Accessibility → Colours → area → element**); effects coloured by
  element, never by ability; health and Qi as two bars in the HUD; the specs amended to say so in Task 1.
- **A glow and a tinted core** (ruling 44, 2026-11-11): the plan's additive (ONE, ONE) halos and trails lost the
  element's hue on the pale paper, reading white. The glows stay additive, and each sword and the fire bolt gets a
  normal-blend core in its element's colour (`core::EffectColour`), with its own opacity (0.6; `core_opacity` in
  `effects.yaml` since Task 10b), apart from the glow's intensity. The reference picture `bolt_burn` shows the bolt's
  orange on the pale ground.
- **Waves of rings** (2026-11-11, after seeing the swords form a single ring: "Can we make the sword spread much wider so
  the 1000 swords don't just end up looking like a circle? Much wider, much more spread, much more randomness in the
  creation? Make the creation semi-sequenced with rings of swords one after each other"; it was the spec's decision 11):
  **the swords form in waves of wide, scattered rings, one after another,** the innermost first, each further out than
  the one before and each a loose band: a sword's place in and out of its ring and round it, its launch within its wave
  and how much its flight bends are drawn at random. This replaces decision 2's and Goal 4's single ring. Shipped: 5
  waves 0.2 s apart, rings from 160 to 560 world units (100 apart), scatter 0.25 of the radius, 0.5 rad of angle jitter,
  sweep jitter 0.6, launches staggered over 0.15 s within a wave. Task 6's rules and smoothness bounds are unchanged.
  **The still rings below replaced the loose bands** the same day: the waves stayed, their scatter and angle jitter
  went to 0, and the sweep jitter (how much each flight bends) stayed.
- **Slower swords** (2026-11-11, during Task 10: "I think the swords should be slower"; ruling 56): each sword's flight
  1.1 → **1.7 s** (102 ticks) and its burst out to its ring 0.25 → **0.35 s** (21 ticks), about 1.5 times as long; the
  waves, their gap, the stagger, the rings, the jitters and the fire bolt unchanged. A volley now lasts about 3.3 s
  (was 2.3), inside the swords' 6 s cooldown. Every test and picture derives its ticks from the shape and passes as it
  was. The flight stays 1.7 s; the burst out became Task 10c's fade-in at the ring, 0.12 s since ruling 63.
- **Every tuning value as data** (2026-11-11, during Task 10, for a future ImGui tuning UI): "Maybe we could plan in a
  debug UI with imgui in the future and all these values could be tweakable so we can tune it visually. Just ensure all
  the values like the distance, speed, range, shape, convergence, everything etc is data-driven and able to be tuned."
  Built as Task 10b, below; the ImGui UI itself is a later plan.
- **Still rings, "o O"** (2026-11-11, during Task 10, after seeing the waves spiral out: "Can we have it more of a static
  circle like o O than a spiral?", then with a reference image of the caster at the centre of a still sunburst of swords,
  each pointing straight out with its hilt on the circle, evenly spaced: "My vision was more like this … Not the spiral
  we're getting"): built as Task 10c. **Each ring is a regular sunburst that appears where it stands,** fading in as it
  draws itself in one clockwise sweep, the rings one after another, inner first; it holds still, then each sword leaves
  after its own short random delay, turns to its flight and flies. This **supersedes ruling 41's spiral** and ruling 22's
  burst and turn on the ring (ruling 61 has the turn now). Shipped first as 5 rings, 100 apart, of 200 swords each;
  ruling 62 changed the rings and ruling 63 the times. A glowing seal under the caster was offered with it, not added
  (below).
- **The ring look** (2026-11-12, after seeing the first still rings, where at zoom 1 a ring of 200 small swords read as
  a fine fringe: "Make the swords slightly large, much less space between the rings, so the swords are almost touching
  tip to heel, and increase the ring count. The rings should appear one after each other, fading in and then moving";
  then "The ring fade/completion should be faster too"; rulings 62 and 63): **ten rings, 38 world units apart** (a
  drawn sword's 36 and a gap of 2), from 160 to 502; each ring's share of the 1,000 in proportion to its radius (48 to
  152 swords, about 20.8 units of arc apart on every ring); swords drawn at 0.9 of their art (was 0.6); a ring every
  0.15 s, each fading in over 0.12 s in a 0.05 s clockwise sweep, holding 0.3 s and leaving within 0.25 s. The last
  sword arrives about 3.7 s after the cast, inside the 6 s cooldown.
- **Small unit tests** (2026-11-12): `check` took about 30 minutes, against 3 to 4.5 before; the user asked that a unit
  test check about 20 representative samples, never sweep every item and fine time step, with timing left to profiling
  and breadth to fuzzing. Built as Task 10d, and the rule is in tests/AGENTS.md. What it found: the 30 minutes was
  entirely the hang Task 10 fixed (Task 10's passing `check` took 291 s), and the assertions were never `check`'s bulk:
  cutting the core, gfx and game suites' from about 2.2 million to 213,000 saved 24 s (267 s). The rest is clang-tidy
  over the branch's diff, the MSVC build and its tests under Wine, the GCC build and the debug unity build's
  `game_tests`, which tick 1,000-sword arenas (Measurements; a plan of its own after this chunk).
- **Lint at milestones** (2026-11-12): lint once after a task's implementation and once after its review's fixes, with
  only the affected suite in between, and only the task's own changes (`lint --range <task base>..HEAD`). A changed
  header still lints every unit that includes it; narrowing that is the next branch after this chunk.
- **A deeper damage colour** (2026-11-12, Task 10e): the damage element's default, `#CFE3FF`, was near-white, so the
  swords' tinted cores added only a faint blue cast on the pale ground. The user: "Yes, make it deeper." It is now
  `#6FA8FF` (packed `0xFFFF'6429`), a clear azure at about twice the saturation; the swords' cores read blue and the
  halos still glow. No text uses the effect colours, so no contrast changed. Kept, as the user decided.
- **An affliction's clock** (2026-11-12, on the final review's first finding): A new hit refreshes an affliction's clock
  (the user, 2026-11-12), so a target hit faster than once a second doesn't tick until the hits stop. This supersedes
  ruling 74; `Afflict` and its tests stay as they were.
- **Builds are verified per task, chunk or phase, never commit by commit** (2026-11-12, during the final fix wave's
  per-commit builds): `check` (every preset) runs at the end of each completed task, chunk or phase; root AGENTS.md and
  the phase spec say so. The per-commit builds the final review asked for (ruling 72) were stopped. Before they
  stopped, 30 of the 80 commits had built on windows-msvc and linux-gcc release, and three had not built on MSVC:
  "swing a sword's aim to a new target when its target dies", "keep a sword on its target near and after its arrival"
  and "cover the swing's arrival clamp and the commit boundary", whose `volley_tests.cpp` puts a temporary inside
  `CHECK` (MSVC C4866), in lines a later commit rewrote. They are left as they are, by this decision.
- **A cast during the dash** (2026-11-12, on ruling 76): "Leave it, we shouldn't cast during a dash. We could consider
  queuing it to execute as soon as the dash ends though." A cast during the dash stays dropped (ruling 19); queuing the
  press to go off as the dash ends is a possible follow-up, on the user's backlog.
- **A glowing seal under the caster** (2026-11-12): yes, as a generic, data-driven seal system (layers and rings of its
  own). It is its own chunk, **4b** (spec `2026-11-12-phase6-chunk4b-seals-design.md`, on branch
  `worktree-phase6-chunk4b-seals`), and is not built here.

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
    matches the reference ring image. If the user wants no pause, a reshaped curve. **Superseded by the still rings**
    (Task 10c): there is no burst, and the turn is ruling 61's.
23. **Swords arrive at half their mean speed** (`ARRIVAL_SPEED`), so they strike rather than settle: feel. If wrong, one
    value (`arrival_speed`, data since Task 10b).
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
    smoothness bounds unchanged: the spec's "no straight segments". If it reads as too twisty, a retune. **Superseded
    by the user's still rings** (Task 10c): the swords appear on their ring and no longer burst out at all.
42. **A per-sword aim offset from the volley's seed, a fraction of the creature's size, always applied** (never only after
    a death): it spreads strikes and plunges over the body instead of one bright disc; hits and ticks are unaffected.
    Costs nothing.
43. **Health bars follow their creature's label:** they fade with "too small to read" and hide when the label is stacked,
    so health by length stays readable. If bars should outlive labels, a rule of their own.
44. **The user's: glows stay additive, and each sword and the bolt gets a tinted normal-blend core** in its element's
    colour, with its own opacity (see the user's decisions above). This changes the plan's ONE, ONE-only effects.
45. **`AIM_SCATTER` stays a fixed 18 world units** (about a quarter of a fox), not a fraction of each target: foxes are the
    only enemies, and a per-target reach would change the save and the hash. **Revisit when a later chunk adds bigger
    enemies.**
46. **A halo fades as its sword's shown²,** so a plunged sword's glow fades faster than its blade. Costs nothing.
47. **`app::Hud` took a text scale** (implementer's): settings have no text size until chunk 11, which passes one scale
    to the HUD and the renderer; Task 9's fixes turned it into `gfx::TextOptions`. Costs nothing.
48. **HUD lines over the ground follow the labels' rule** (`TextInk` when the outline and the shadow are both off); the
    words on the bars keep `HudText`, on their bars: readable on pale ground with the accessibility options as they
    exist. Costs a revisit with chunk 11's high-contrast option.
49. **In ink mode a cooling slot's line is `TextInk` at a named, reduced opacity** (0.55; `cooling_ink_opacity` in
    `effects.yaml` since Task 10b) and still shows its seconds: cooling is never colour only. Costs nothing.
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
56. **The user's: slow the swords about 1.5 times** (flight 1.7 s, out 0.35 s, which ruling 63 made a 0.12 s fade-in;
    above). If the user wants them slower or faster, one data line.
57. **`--cast-swords` tops the Qi up with the generator** (Task 10): the swords cost 60 Qi and the player starts with 50,
    so a timer cast alone would fail with `NotEnoughQi`. `app::TimedCast` casts special 1 when it is ready and the Qi
    covers it, and otherwise, while the Qi is short, the fire bolt when it is ready, as a player would: no game rule or
    data changes for the profile run. The timer casts the swords 4 s after the last, or as soon after as they are ready,
    so with their 6 s cooldown it casts about every 6 s. Its bolts (about three a cycle) are in the profile, as in
    play. If a swords-only profile is wanted, a documented Qi grant for the run.
58. **`app::CastLatency` watches one ability's volleys and forgets a press after `CAST_LATENCY_LIMIT_NS` (1 s)**
    (Task 10): `--cast-swords`' bolts are volleys too, and a refused press (cooldown, Qi, pause) would otherwise lengthen
    the next press's latency. A new volley is one whose use the previous frame didn't draw. Key presses of special 1 are
    measured as well as the timer's. Costs nothing.
59. **The `save` fuzz target loads every input as saved at u64's largest tick but one** and ticks and publishes at the
    largest (Task 10): any flash a save can name then loads, so fuzzed flashes reach the code after the load (at tick 0
    only a flash starting at 0 could). Volleys and respawns in the seeds are all overdue at that tick, so they exercise
    the catch-up path; mid-flight states are reached by mutation. Costs nothing.
60. **The Outcome's later decisions were brought up to date once, at the end** (this Outcome), after Tasks 10c, 10d and
    10b, not at Task 10's review: those tasks hadn't run, the Outcome must describe what was built, and rewriting it
    twice wastes a round. Costs nothing, as the refresh happened.
61. **The turn from outwards to the flight happens over the flight's first `TURN_TICKS` (8),** not in a phase of its
    own (implementer's; the review accepted it): `ArrivalTick` and the user's 1.7 s flight stay exact. The sword pivots
    as it eases off from rest, sliding up to about 25 world units by the turn's end and under 7 while it faces more than
    90° away from its flight; the worst turn is 0.074 rad per eighth of a tick (bound 0.25). If a "turn, then go" look is
    wanted, an 8-tick phase added to every arrival.
62. **The user's ring look:** swords drawn at 0.9 of their art (was 0.6); `ring_step` a drawn sword's length and a small
    gap (38); ten waves (160 to 502); a wave's share of the count in proportion to its radius, so the arc between swords
    is the same on every ring (about 20.8 units); `wave_gap` 0.15 s; the hold 0.3 s. All data. If wrong, data lines.
63. **The user's faster ring fade:** `out` (the fade-in) 0.35 → 0.12 s and `stagger` (the clockwise sweep) 0.1 →
    0.05 s, the hold kept at 0.3 s. If wrong, data lines.
64. **The retarget smoothness test bounds sword 433's two swings in mid-flight by the swing bound** of rulings 34 and 35
    (4.1 times its median step, under the median plus `RETARGET_PEAK`, against the plain bound's 3 times): the bound
    applied where it belongs, not a loosening. Costs nothing.
65. **The save seed's generator takes no environment variable:** "the mid-fight save loads, and so does the fuzz seed cut
    from it" writes nothing, and "write the fight save seed" is skipped by default (`doctest::skip()`), run by name with
    `--no-skip`, and writes through `PN_FUZZ_CORPUS`, a define tests/xmake.lua sets from the project's folder, checking
    the write. No `getenv`, MSVC pragma or `<cstdlib>`. tests/AGENTS.md names a skipped generator run by name as the one
    way a test may write a repo file: it isn't a behaviour test, so skipping it by default doesn't break "never skip a
    test". Costs nothing.
66. **The statistical tests that compute over every sword and assert once stay:** the user's complaint was the number of
    assertions, and a distribution needs its population (20 samples can't show one without wider bounds). Costs nothing.
67. **The user's deeper damage colour:** `#CFE3FF` → `#6FA8FF`, a clear azure in the same hue family at about twice the
    saturation, with the pictures updated and looked at: a colour pick, not a guess at behaviour, and the table's to
    change. If wrong, one table edit.
68. **Task 10b's brief named a `TURN_START` constant that doesn't exist:** the turn is `turnTicks`, read from `turn:` in
    seconds. Costs nothing.
69. **`SIDE_ARM` stays in code as maths** (the 4/3 that makes a bend exactly the sweep), documented in volley.cpp:
    tuning the bend is `short_bend`'s job. Costs nothing.
70. (Revised by ruling 75.) **`App::WatchData` has no test of its own** (it needs a window); what it calls is tested:
    `data::LoadEffects`, `gfx::Renderer::SetEffects` changing the next picture, `app::Hud::SetEffects` on the next
    build. A wiring slip in `WatchData` itself would show only in a live run.
71. **Task 10b's review round had no `check` of its own:** the next full `check` on the same branch covers it, as a check
    a round doubles five-minute runs for the same coverage; a failure in 10b's code lands in 10b's commit. Task 10e's
    `check` stopped at the commit check on the pending fixup, so the wrap-up's `check`, after the fold, is that run.
    Costs nothing if it passes.
72. (Changed by the user's decision on verifying builds.) **Every commit was to build on every preset this host
    builds**, not only natively, as a commit MSVC can't build breaks bisecting on Windows. The HUD's parenthesised
    construction (`m_hud(...)`, the HUD tests' `app::Hud(...)`) moved into "strike with the poison lance and show a
    temporary HUD", where the braces were first written, and stays there; the per-commit builds were stopped by the
    user's decision, which verifies builds at the head of each task, chunk or phase instead.
73. **The wrap-up's fold choices stand:** a reference-suite bound with no branch origin is its own test commit ("bound
    every wait in the reference suite"); a fixup splits where its file first exists; and what matters is the
    verification (an empty end diff, the per-commit builds, the gates), not the tool. If wrong, a commit carries a
    neighbour's change, visible in review.
74. (Superseded by the user's decision on an affliction's clock.) **A refresh would have kept an affliction's pending
    tick**, renewing its ticks, damage and use, so a fox hit faster than the period still burns once a second. The user
    chose the opposite: a new hit refreshes the clock. Nothing was changed.
75. **The reload decision is a pure, tested function** (revising ruling 70): `app::ReloadOf` compares the effects and the
    game data read again with those in use by `Hash()`; `ReloadData` goes to the simulation only when the game data's
    hash changed, an unchanged game data is logged at debug level, and a part that fails to read changes nothing.
    `WatchData` itself still needs a window. Costs nothing.
76. **A cast during the dash stays dropped** (ruling 19), as the user decided (their decision on a cast during the dash,
    above): no cast during a dash. Queuing the press to go off as the dash ends is a possible follow-up, on the user's
    backlog.
77. **`--zoom <value>` starts every scene at a zoom** from 0.25 to 2 (`app::ZoomOf`, refusing anything else with a
    message), a profiling aid like `--cast-swords`, so the frame profile runs at the closest and the farthest zoom with
    nothing touched. Costs nothing.

Not ledger rulings, but calls made in measuring: the runs are unpinned (30 workers, as chunk 3's), and Tracy's CSV
exporter was again read from Pixi's package cache with `pixi.toml` unchanged.

### Notes for later chunks

- **Overkill numbers on a death tick** (ruling 38): swords arriving on the tick their target dies, after the killing
  blow, still show their numbers; and when a burn tick kills, a poison tick due on the same tick still hits and shows.
- **Overkill swords within the commit window plunge and are wasted** (rulings 28, 33): a sword whose target dies less
  than the commit window (33 ticks for the swords) before its arrival drives into the corpse and hits nothing.
- **The lance strikes in `Strike`, after the tick's due afflictions** (ruling 50), not at the cast: a refresh never
  swallows a tick that was due.
- **An affliction under hits faster than its period never ticks** (the user's decision): the bolt (0.25 s cooldown)
  held on one fox keeps its burn from ever ticking, and two lances 0.5 s apart do the same to a poison, so a target's
  damage over time comes only when the hits stop. Balance (and any stacking design) starts from that.
- **`AIM_SCATTER` is fixed at 18 world units while foxes are the only enemies** (ruling 45).
- **The plan's "exact sRGB round trip" was impossible** (ruling 6): within one linear step for every default, exact for
  the defaults named by an sRGB colour.
- **Chunk 11's accessibility scaling must apply after `effects.yaml` reloads:** a reload hands the renderer the file's
  raw `halo_intensity`, `core_opacity`, `trail` and `flash_intensity`, so reduce flashing and reduce motion scale them
  on the main thread after reading, never in the file.
- **Floating-point contraction isn't pinned off** (`-ffp-contract`): the path maths is deterministic within one binary,
  as the engine requires, but a fused multiply-add could make two compilers' binaries differ. For the sweep-up list.
- **`check`'s time** (Measurements): clang-tidy over a branch's diff, the MSVC build and its tests under Wine, and the
  debug unity build's `game_tests` (about 18 s of ticking 1,000-sword arenas) are its bulk; a plan of its own. Sweeping
  tests older than this branch (core's "draws spread evenly over a range", 100,020 assertions; its unit values, radix
  sort and handover; gfx's number placement, 68,948) are left for the sweep-up.
- **The `save` fuzz seed is generated, not kept by hand:** whenever the save layout changes, regenerate
  `tests/fuzz/corpus/save/fight` with the skipped case (tests/AGENTS.md has the command).

### Values chosen

- **Abilities** (`assets/data/abilities.yaml`): Qi 100 at most, 50 at the start. Fire bolt: generator, 0.25 s cooldown,
  +10 Qi, 140 burn damage, radius 120, 1,500 units a second, a burn of 3 ticks × 40. Poison lance: spender, 0.5 s,
  25 Qi, 360 poison damage, radius 90, a poison of 4 ticks × 60. Dash: 1 s, 260 units over 0.15 s. Flying swords:
  special, 6 s, 60 Qi, 20 damage a sword, radius 260, 1,000 swords in 10 rings from 160 to 502 (38 apart, 48 to 152
  swords each) 0.15 s apart, each fading in over 0.12 s in a 0.05 s clockwise sweep, holding 0.3 s and leaving within
  0.25 s; scatter and angle jitter 0; flight 1.7 s, sweep 420, sweep jitter 0.6, fade 0.3 s. Fox health 1,200; the
  player's 1,000.
- **The path** (each projectile ability's `motion:`, the same for the bolt and the swords): `aim_scatter` 18 world units,
  `short_bend` 0.5, `turn` 0.1333 s (8 ticks), `arrival_speed` 0.5, `retarget` 0.1333 s (8 ticks), `retarget_speed`
  960 units a second (16 a tick), so the swing's peak is 24 a tick; in code, `MIN_COMMIT_TICKS` 16 (the swords' window
  33) and `MAX_VOLLEY_COUNT` 2,000.
- **Combat rules** (`abilities.yaml`'s `combat:`): dying 0.5 s (30 ticks), respawn 3 s (180), arrival 3 s (180), an
  affliction tick every second, the lance's burst 0.2 s; the crystal's aura every second, reaching 320, damage 50 to
  400, healing 50 to 200. The dissolve's blocks are 4 screen pixels (the sprite shader).
- **Drawing** (`assets/data/effects.yaml`): the swords at 0.9 of their art (halo 1.125, core 1.5), the bolt at 1.0
  (halo 0.75, core 2.0); trails 0.1 s (6 ticks) over `TRAIL_SEGMENTS` 8, 5 wide, taper 0.6; glow intensity 1,
  core opacity 0.6; the lance's burst grows from 0.5 to 1.9, intensity 1; health bars 44 × 6 (fill 40 × 4), 5 above
  the label. In code: `MAX_OVERLAY_INSTANCES` 8,192 (three a sword), `MAX_RIBBON_QUADS` 16,384.
- **The HUD** (`effects.yaml`): 18 px text, 16 px margins, bars 240 × 18 and 6 apart, their words 6 in and raised 7,
  lines 24 apart, "Not enough Qi" for 1 s, cooling ink at 0.55.

### Measurements

**`DrawVolleys` while building** (Tasks 8 and 9b, release, a temporary doctest in the reference target, deleted before
each commit: the arena's cast, 1,040 calls over 130 ticks × 8 render times, 1,000 swords, timed with
`platform::NowNs()`; per frame, mean / worst):

| `DrawVolleys` | Task 8 | Task 8's fixes (cores, aim spread, ribbon room) | Task 9b (waves of rings) | Task 10c (still rings) | Task 10c's ring look |
|---|---|---|---|---|---|
| 30 workers | 166 / 392 µs | 201–205 / 469–475 µs | 243–247 / 449–460 µs | 195–209 / 424–515 µs | 212 / 525 µs |
| 1 worker and the main thread | 281 / 662 µs | 328–330 / 758–859 µs | 363–365 / 780–792 µs | 283–284 / 619–686 µs | 285 / 687 µs |

Each step stayed under the review's 1.5× threshold (about 1.2× and 1.1×, then level or lower). Over the whole 9b volley
(160 ticks) it was 225–229 / 427–482 µs (all workers) and 326–338 / 649–650 µs (1 worker); over the whole volley of
the still rings (230 ticks) 200–204 / 468–502 and 295–298 / 656–675 µs, and of the ring look 223 / 565 and 306 /
807 µs. The still rings' mean is 15 to 20% below 9b's: a sword holding on its ring draws no trail, which saves 8 of its 9
path calls. The upload is about 3,000 overlay instances and 8,000 ribbon quads a frame; the GPU's side was not measured
on its own.

**The swords' smoothness** (Task 10c, a temporary probe, removed): the worst turn of a heading is 0.070 rad per eighth
of a tick (0.074 in the review's run) against the bound of 0.25; a sword on its ring moves exactly 0; the worst plain
flight step is 2.78 times the median (bound 3).

**Frame time with the slower swords** (Task 10, before Task 10c's still rings and the ring look, which are profiled
below; `DrawVolleys` above stayed level through both):
`pixi run project_n profile --seconds 70 -- --cast-swords` and a baseline without the flag; releasedbg, linux-clang, the arena, 1280×720, Wayland, NVIDIA RTX 4090, presenting
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

**Frame time with the final rings** (the final review, ruling 77; the still rings and the ring look as shipped):
`pixi run project_n profile --seconds 30 -- --cast-swords --zoom 2` (the closest zoom) and `--zoom 0.25` (the farthest),
captures `20261112-143310` and `20261112-143351`, on the same host and settings as Task 10's runs (releasedbg,
linux-clang, the arena, 1280×720, Wayland, NVIDIA RTX 4090, mailbox, 30 workers, unpinned) and read the same way
(`tracy-csvexport -u`; a frame is one iteration of `App::Run`'s loop; the first two seconds skipped). **The window lost
its focus again in both runs** (someone used the desktop), so the paused stretches are left out with half a second
either side: the closest run keeps 2.2-12.9 s and 24.8-28.0 s (three volleys, the last cut off after its peak), the
farthest 2.2-19.4 s (three volleys). A volley counts as drawn for 4.0 s from its first frame (the last sword
arrives by about 3.7 s and fades); its peak is 1.5 to 2.0 s after that first frame, when every ring has formed and
none has arrived, where `DrawVolleys`' median per 0.25 s is within 10% of its highest (it climbs from 46 µs in the
first quarter second to 353 µs at 1.75 s at the closest zoom, 380 µs at the farthest, and falls after 3.25 s).

| Main thread, one loop iteration (median / 99th / worst, ms) | Closest zoom (2) | Farthest zoom (0.25) |
|---|---|---|
| Without volleys | 0.382 / 2.03 / 3.23 (4,327 frames) | 0.196 / 1.98 / 2.72 (10,071) |
| With a volley drawn | 1.32 / 2.21 / 3.88 (9,030) | 1.20 / 2.18 / 4.17 (10,797) |
| At a volley's peak | 1.34 / 2.26 / 3.88 (1,168) | 1.28 / 2.17 / 2.78 (1,254) |

| Per call at a volley's peak (median / 99th / worst, µs) | Closest zoom (2) | Farthest zoom (0.25) |
|---|---|---|
| `DrawVolleys` | 327 / 528 / 663 | 371 / 519 / 622 |
| `SubmitFrame` (waits on the driver and the GPU) | 904 / 1,811 / 3,261 | 739 / 1,620 / 2,231 |
| `SubmitFrame` without volleys | 209 / 1,932 / 3,126 | 106 / 1,883 / 2,636 |

No frame came near the 16.7 ms bar: the worst of both runs is 4.2 ms, with a volley drawn at the farthest zoom, and no
run logged an overflow or a warning. The final rings cost about what Task 10's waves did: about 1 ms on the median frame
at the peak (0.96 ms at the closest zoom, 1.09 at the farthest, against 1.1 then), `DrawVolleys`' median at the peak
327 to 371 µs against Task 10's 298, and most of the rest in `SubmitFrame`. Between the zooms, `DrawVolleys` costs about
13% more at the farthest and `SubmitFrame` about 18% less, and frames without volleys take 0.38 ms at the median at the
closest zoom against 0.20 at the farthest; why was not profiled further (more swords on screen and fewer pixels each at
the farthest zoom would fit).

**Input to the first sword** (`swords first drawn N ms after the press`, the log at debug level): eleven presses with
the shipped swords, eight by `--cast-swords`' timer and three by hand (key 1, in the runs above): **16.8, 16.9, 17.2,
17.3, 17.3, 18.0, 25.1, 26.3, 26.8, 26.9 and 27.3 ms**; median 18.0, mean 21.4. The first swords cast of each run
takes 25 to 27 ms and every later one 17 to 18 ms; the floor is the display delay (10 ms) plus the wait for the logic's
next tick (up to 16.7 ms); the first cast's extra 8 to 10 ms is likely its buffers' first growth and cold caches (not
profiled further). An earlier trial before the slowing gave 28.1 ms for a first cast. The final rings' two profile runs agree: 28.1 and
27.2 ms for each run's first cast, 15.2 to 17.4 ms for the five after.

**Fuzzing** (linux-clang with AddressSanitizer, no crash in any run): Task 10, 30 s each: `save` 3,080 runs with the
209 KB mid-fight seed (about 100 a second), then 8,537 runs with the 4,421-byte one; `replay` 719,137; `yaml` 18,897.
Task 10d: `save` 6,723 runs in 21 s with the generated seed.

**`check`** (all presets but macos-clang, which this host can't build): Task 10's first passing run took **291 s**
(the hour before it was the hang). Its steps, from line counts every 2 s (±2 s):

| Step | Time |
|---|---|
| self-tests, fmt, doxygen, LFS, ruff, native debug build (cached) | ~4 s |
| clang-tidy, 125 units | 65 s |
| linux-gcc release build / tests | 40 s / 2 s |
| linux-clang release build / tests | 12 s / 2 s |
| windows-msvc (container) build / tests under Wine | 46 s / 42 s |
| reference pictures (linux-gcc release, lavapipe) | 28 s |
| unity build / tests (linux-clang debug) | 2 s / 20 s |
| releasedbg build | 22 s |

Task 10d's small tests then took it to **267 s** (one MSVC finding on the way). Assertions and time of the `test` step
(linux-clang debug), before and after:

| Suite | Assertions | Time |
|---|---|---|
| `core_tests` | 2,039,806 → 138,581 (`volley_tests.cpp` about 1.9 million → 514) | 2.06 → 0.34 s |
| `gfx_tests` | 140,956 → 72,379 (`volley_draw_tests.cpp` 737 after) | 2.59 → 1.72 s |
| `game_tests` | 45,017 → 2,080 | 21.4 → 17.6 s |

Six mutations of the volley's maths (no commit window, a jumping swing, an arrival a tick late, a flipped turn, a
40-unit jump in flight, no bend) each still fail the sampled tests. Task 10b's `check` passed after three fixes only
`check` finds (MSVC's C4868 and C4866, a unity-build clash among `data/effects.cpp`'s file-local names), folded into its
commits; Task 10e's stopped at the commit check on a pending fixup, before any build. After the fold, each of the
branch's 79 commits built on its own (native preset), `check` passed in **310 s**, `check --sanitizers` (address,
undefined, thread and leak on linux-gcc and linux-clang, with the fuzz seeds; MSVC's address build) in 874 s with no
finding, and `lint` over the branch's diff was clean. After the final review's fixes were folded (the end tree matched
the unfolded one exactly), `lint` over the branch was clean and `check` at the head passed in **314 s** (commit check
81 commits, clang-tidy on 127 units, every test suite on linux-gcc and linux-clang release, windows-msvc under Wine and
the unity build, the 25 reference pictures).

**Pictures:** Task 10b moved none (`reference` passed without `--update`); Task 10e's deeper colour moved the three
volley pictures (`volley_ring` 22,000 pixels, `volley_converging` 123,000, `volley_plunge` 86,000) and nothing else.

### Open for the user

- **Player death is undefined** (ruling 12): nothing damages the player yet; if something did, the cultivator would be
  removed and the camera would stop following. What death does is a later chunk's design.

The rings in motion, open until the PR, were judged live by the user on 2026-11-12: "the current sword rings look fine
for the demo". Every lever stays data in `abilities.yaml` and `effects.yaml` for later tuning.
