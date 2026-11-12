# Phase 6, chunk 4b: seals

Date: 2026-11-12 · Status: approved 2026-11-12 · Phase spec: `2026-11-10-phase6-design.md` · Builds on: chunk 4
(`2026-11-10-phase6-chunk4-abilities-design.md`), which must merge first

## In short

- **Seals** (formations, arrays): the glowing circles of script and geometry that cultivators raise. A seal is
  **described in data and put together from parts**: 1 to 9 rings, each turning at its own speed in its own
  see-through layer; script bands, lines, ticks, dots, stars, polygons, non-round frames and flourishes inside them; a
  central emblem; satellite seals on or around the rings; pillars of light, sparks and shards; one or two colours.
- **It can lie on the ground, stand upright, hang overhead or rise in tiers**, and its layers sit at different heights,
  so it reads in depth, not as a flat decal.
- **The first use:** a seal under the cultivator while the flying swords form. **The demo:** a few showcase seals in
  the arena that between them use every part.
- **Look only, for now.** The game knows "this seal, here, this size, for this long"; the renderer draws the rest. Seals
  that heal, defend or attack later fit the same record without changing it.

## Intent

The user wants seals to be one of the game's signature looks: in their words, "extremely large, like a whole screen
width, with 5–9 inner circles rotating at different speeds, all very pretty and detailed, with overlapping transparent
layers to give a 3d effect". Seals can have 1 to 9 rings; the large one is for the demo. They are meant to grow into a
**general system**: "a complex, generic, data-driven system to support all and any seals with various combinations of
these elements", which later carries meaning in play (marking where an attack lands, or being the attack, heal or
defence itself: a healing array, a defence array, an attack array) and may later be simulated on the GPU for very
large, many-layered seals.

Inspiration (the user's): the spiritual arrays of *The Great Ruler* and the formations of *Battle Through the Heavens*.
The user picked **script rings** (bands of glyphs between circles, around a central figure) as the closest style, over
pure geometry or filigree; then **the art's angle** for the view and **an ink wash under a glowing seal** for the pale
ground (Design: "The view and the ground"). The mockups they chose from are beside this spec, in
`2026-11-12-phase6-chunk4b-seals-mockups/` (open in a browser: `ring-detail.html` is mockup A among the three styles;
`view-and-ground.html` the two questions; they are fragments of the brainstorming companion's page, so its card layout
is missing, but the seals draw). As with Tale of Immortal (phase decision 16), these are inspiration, not templates: no seal
copies a design from them.

What the user's references share, and this chunk supports:
- script bands as the backbone, often two scripts at different sizes in neighbouring rings;
- a central emblem (a creature motif in one);
- satellite seals sitting on the big ring or orbiting it;
- placement on the ground, upright in front of the caster, overhead, or stacked in tiers at different heights;
- non-round frames (diamonds, squares) and flame-like flourishes spilling past the rings;
- light around the seal: pillars falling from it, rising sparks, drifting shards;
- two colours at once (blue script with gold flourishes; gold with violet).

## Goals

1. **Seals as data.** A new table, `assets/data/seals.yaml`, describes every seal from the parts below. A new seal is
   written, not coded. Debug builds reload it within half a second of an edit, as `effects.yaml` reloads, and a broken
   edit is logged with its line while the old table stays.
2. **Every part the references show** (Intent's list): rings (1 to 9 per seal), and inside a ring any mix of circles
   (solid or dashed), tick marks, dots, script bands, star figures, polygons, non-round frames and flourishes; an
   emblem; satellites (each a seal by name, sitting on a ring or orbiting); light (pillars, sparks, shards); one or two
   colours.
3. **Depth.** Seals are seen at the art's angle (Design: "The view and the ground"). A seal lies on the ground, stands
   upright or hangs overhead, and each layer has a height above its seal's plane, so its layers separate in depth and
   tiers stack visibly.
4. **A life.** A seal appears (rings drawing in or fading in, outer first or inner first, as its data says), holds while
   it is up, and fades.
5. **The swords' seal.** Casting the flying swords raises a seal under the cultivator for as long as the rings form and
   hold, gone as the last swords leave. Which seal, and for how long, is the ability's data.
6. **The showcase.** The arena shows a few seals that together use every part in Goal 2 and every placement in Goal 3,
   including one screen-wide seal with 9 rings.
7. **Smooth and steady.** Seals hold the phase bar (60 fps on the minimum spec, measured on the development machine
   with headroom) and never shimmer as they turn or the camera zooms.

## Non-goals

- Seals that act in play (heal, defend, attack, mark where an attack will land) and enemy telegraphs. The seal record
  leaves room for them (Design: "In the simulation").
- Simulating seals on the GPU. This chunk keeps drawing a pure function of the seal, its data and the render time, so a
  GPU version can replace it later without touching the rules.
- Hand-drawn art. Glyphs, flourishes and emblems are procedural placeholders (phase decision 13), replaced later.
- A seal editor or the ImGui tuning UI (backlog). Hot reload is the editing loop.

## Anti-goals (a failure even if it works)

- **Shimmer:** fine lines and glyphs that crawl, sparkle or alias as rings turn or the camera zooms (a phase anti-goal).
- **A hitch:** the first seal of a kind stalling a frame while something is built or uploaded.
- **A flat decal:** a seal that reads as one painted circle rather than layers in depth.
- **Hiding the fight:** a seal that buries creatures, damage numbers, health bars or the HUD.
- **A seal that's code:** any seal's look (its parts, proportions, speeds, colours, life) written in C++ rather than
  data. Where a scene puts its showcase seals follows the scene's other content (see "The showcase").
- **Washed out:** a seal on the pale ground that reads as a white smear instead of coloured, glowing lines.
- **Copied art:** a seal that reproduces a design from the references.

## Design

### In the simulation

A seal in play is one small record, as a volley is: which seal (its name, from the game data), where (a fixed point),
how large, when it started and when it ends, and its element. It lives in the arena's state, is saved and replayed
(`ARENA_FORMAT` bumps; the `save` and `replay` seeds and fixtures are regenerated the documented way), and reaches the
main thread in the snapshot as `Snapshot::seals`. Nothing about its look is in it.

- **The swords' seal:** `abilities.yaml`'s flying swords gain a `seal:` entry: the seal's name, its size, and that it
  lasts from the cast until the last sword leaves its ring (the volley already knows that tick). Game data, hashed. It
  stays at the cast point, centred on the rings, as the volley's origin does, even if the cultivator walks or dashes
  away (my call).
- **The showcase:** see "The showcase" below. Its seals are scene content, not state.
- **Later:** a seal that follows an entity (a `StableId` in the record, drawn at the entity's smoothed position), and a
  seal that acts, with rules in game data (a reach, a period, an amount through `game::HitAmount`) and logic that reads
  the same record. Neither is built here; the record is shaped so both add fields rather than change existing ones.

### In the data: `assets/data/seals.yaml`

Presentation, like `effects.yaml`: read by `data` into a new `core::Seals`, not hashed, not saved, and reloaded on the
main thread. Each seal is named. A seal names in its data:

- **Its plane:** ground, upright or overhead, and how high above the ground it sits.
- **Its ink wash:** how dark, how far past the outer ring and how soft (Design: "The view and the ground").
- **Its colours:** one or two slots. Each slot is either `element`, which takes the colour of the record's element (a
  seal raised in play, such as the swords' seal: the damage element's colour), or a name in the colour table
  (`core::UiColours`). The showcase's seals have no element and name table entries. New entries for the accents the
  showcase needs (a gold, a violet) join the table under **Effects**, with their names in both languages, so chunk 11's
  menu and presets recolour them (my call). Every part says which slot it takes.
- **Its life:** how long it takes to appear and to fade, and in what order its rings appear.
- **Its layers (1 to 9 rings, plus any non-round frames):** each with its inner and outer radius as fractions of the
  seal's size, its height above the plane, its spin (degrees a second, either way), its opacity, and its parts:
  - **circle:** at a radius, a width, solid or dashed (dash and gap);
  - **ticks:** a count, a length, a width;
  - **dots:** a count, a size;
  - **script:** a glyph set, a count (or a spacing), a size, facing outward or inward;
  - **star:** points and step (a hexagram is 6 and 2), a radius, a width;
  - **polygon / frame:** sides and rotation (a square, a diamond), a radius, a width;
  - **flourish:** a placeholder sprite, a count, and how far it may spill past the layer's edge.
- **Its emblem:** a placeholder sprite at the centre, its size and spin.
- **Its satellites:** other seals by name, each with a size, and a place on a layer's ring (fixed, turning with the
  layer, or orbiting at its own speed).
- **Its light:** pillars (count, height, width, how they fade), sparks (how many, how fast they rise, how long they
  live), shards (how many, how they drift and turn).

The reader refuses out-of-range values with their line (a ring count outside 1–9, a size ≤ 0, an unknown colour name,
a satellite naming itself or forming a loop, satellites nested deeper than a limit the builder sets) and warns on
unknown keys, as the other tables' readers do. A seal name the game data uses but `seals.yaml` lacks is logged once and
draws nothing. The `yaml` fuzz target parses `seals.yaml` too, with a seed.

### Glyph sets and placeholder art

A script band draws from a **glyph set**. This chunk ships an invented rune set (procedural strokes on a small grid,
like the mockup's) and, if it fits the existing text path, a set of real characters from the game's font (Noto Sans SC)
drawn through chunk 3's distance-field glyph cache. Flourishes and emblems are procedural placeholder sprites made by
`art/placeholders.py` (flame-like flourishes; a few abstract emblem motifs), listed in `art/sprites.toml` and packed
by `atlas` as every sprite is. Glyphs, flourishes and emblems are drawn as **distance fields**, so they stay sharp at
any size and zoom.

### The view and the ground (the user's decisions)

- **The art's angle.** The world is drawn straight down today, but the art is drawn as if seen from above at an angle
  (blob shadows are flat ellipses, 64 by 24). Seals take that angle, one shared by every seal: a ground seal lies flat
  as an ellipse squashed by the art's ratio; an upright seal stands facing the viewer as a circle; an overhead seal is a
  flat ellipse lifted high; a layer's height lifts it straight up the screen, so a seal's layers part and tiers rise
  visibly. The ratio is presentation data (`effects.yaml` or `seals.yaml`'s global section), derived from the shadows.
  The swords' rings stay as they are (standing sunbursts), with their seal lying flat beneath them.
- **Ink wash, then glow.** On the pale paper ground, light added on top washes out to white. Each seal therefore draws
  a soft dark wash of ink under itself (normal blending, premultiplied, darkening the ground), then its lines glow
  additively on top of the wash. How dark, how wide and how soft the wash is are data per seal. An upright or overhead
  seal's wash is drawn behind it in its own plane, not on the ground.

### Drawing

Drawing a seal is a **pure function of its record, its data and the render time** (`core::RenderTick`, as the swords).
No seal keeps per-frame state on the CPU or GPU, so the same frame always draws the same picture, a save or replay
loaded mid-seal looks right at once, and a GPU-simulated version can later replace this one behind the same inputs.

The approach (**my call**; the builder may change it with measurements that beat it):

- **One band per layer, drawn procedurally.** Each layer is geometry covering only its band (from its inner to its
  outer radius, an annulus of a few dozen quads, not a full disc) on the seal's plane, placed on screen at the art's
  angle. Its fragment shader draws the layer's parts from a compact list (circles,
  ticks, dots, stars and polygons as analytic distance fields; script, flourishes and emblems by sampling distance-field
  pages), anti-aliased by screen-space derivatives. This is resolution-independent (a screen-wide seal stays sharp),
  never shimmers when well filtered, costs no per-seal textures, and is already the shape a GPU simulation would take.
- **Cost by design.** Each layer has a hard cap on its parts, and its parts are sorted by radius, so a pixel tests only
  the parts whose band it lies in; band geometry keeps overlapping layers from shading the same empty middle nine
  times.
- **In the world pass: the wash (normal blending), then the lines (additive), premultiplied.** A seal on the ground
  draws under the creatures and their text; an upright or overhead one draws above the creatures but under damage
  numbers, health bars and the HUD, with an opacity cap so the fight stays readable.
- **Light is stateless too.** Pillars are quads; sparks and shards are instances whose positions are functions of their
  seal's start, the render time and a per-particle seed, computed in the vertex shader. Nothing is simulated step by
  step.
- **Overflow:** layers and particles past their buffers are reported through `ReportOverflow` /
  `core::RepeatedFailure`, as instances and quads are. Nothing is dropped silently.
- **No hitch:** every seal's parts are packed when `seals.yaml` loads or reloads, never on first draw.

### The showcase

The arena gains a few showcase seals placed so that each stays readable and the fight still plays (exact places and
count are the builder's). Together they use every part and every placement:

- a screen-wide ground seal with 9 rings, two scripts, a star and an emblem, in two colours (the user's demo picture);
- an upright seal before the cultivator with satellite seals sitting on its ring;
- an overhead seal with pillars of light falling from it and sparks rising;
- a seal in tiers: diamond and square frames stacked at rising heights;
- a seal with flame-like flourishes spilling past its rings, and drifting shards;
- small seals with 1 and 3 rings, to show the low end.

Plus the swords' seal under the cultivator on each cast.

- **Where they are placed** (seal names, places, sizes) is scene content, as the arena's props are today: C++ constants
  beside `PROPS` in `game/arena.cpp`, until chunk 7 moves scene layouts into data. Like the props, they are rebuilt from
  the scene, not saved (my call).
- **Shown by default** in the arena. A debug key hides and shows them, so the arena can be judged with and without
  them. The existing arena reference pictures are taken with the showcase hidden, so they keep testing what they test;
  the seals get pictures of their own (Testing).

## Accessibility

Phase decision 17 requires this section.

- **Colours:** every seal colour is a name in `core::UiColours` (element colours, plus the new accent entries under
  **Effects**), so chunk 11's menu and colour-blind presets recolour seals. No seal colour is in code or a shader.
- **Colour is never the only signal:** seals are only a look in this chunk. When seals carry meaning later, each kind
  must differ in shape (its frames, script or emblem) as well as colour.
- **Reduce motion (chunk 11 wires it):** one scale on every seal's spin, the particles' speed and the appear and fade
  times, which chunk 11 lowers (to a slow turn, or still) under reduce motion. Nothing shakes the camera.
- **Reduce flashing (chunk 11 wires it):** one brightness scale on every seal and its light, which chunk 11 lowers.
  Seals fade in and out; nothing strobes or pulses faster than a slow breath.
- **Readability:** seals stay under damage numbers, health bars and the HUD, with an opacity cap (Drawing).
- **Input:** nothing new to press except the debug key; no UI.

## Decisions

The user's, 2026-11-12:

1. **The swords get a glowing seal under the caster**, and it is built as a **generic, data-driven seal system** that
   supports "all and any seals with various combinations of these elements", possibly simulated on the GPU later.
2. **Seals are extremely large at their grandest** (a whole screen wide), with see-through overlapping layers that give
   depth; **1 to 9 rings**, each turning at its own speed.
3. **Script rings** are the closest style (mockup A over geometry and filigree); *The Great Ruler*'s spiritual arrays
   and *Battle Through the Heavens*' formations are the inspiration.
4. **Seals may later mark attacks or be attacks, heals or defences** (attack, healing and defence arrays).
5. **The demo shows a few examples that between them cover every part.**
6. The damage element's default colour deepens to `#6FA8FF` (chunk 4's Task 10e), which the swords' seal takes.
7. **Seals are seen at the art's angle** (mockup "1 · B"): ground seals flat as ellipses, upright ones as circles,
   heights up the screen.
8. **An ink wash, then glow** on the pale ground (mockup "2 · B"), over glow alone (washes out) or dark ink lines (loses
   the shine).

## Calls made here (not the user's; check them)

- **Its own chunk, 4b**, on its own branch after chunk 4's PR merges, added to the phase spec's table after chunk 4.
- **The record and the look are separate:** the simulation holds only what, where, how large, when and which element;
  the look is presentation data (`seals.yaml`), not hashed. A seal's name is game data.
- **Seals that act come later**; this chunk only leaves room for them.
- **Procedural placeholder art** for glyphs, flourishes and emblems, drawn as distance fields.
- **One procedural band per layer** with analytic distance fields, capped and radius-sorted parts, and **stateless
  particles** (Drawing).
- **Draw order:** ground seals under creatures; upright and overhead ones above creatures, under text and the HUD.
- **Colours:** a slot is `element` (the record's element) or a table name; the showcase names new accent entries
  (gold, violet) added to the table under Effects.
- **The swords' seal** stays at the cast point, centred on the rings, from the cast to the last sword's departure;
  following an entity comes later.
- **The showcase** is scene content (C++ beside the props until chunk 7), not saved, shown by default, toggled by a
  debug key, and hidden in the existing arena reference pictures.
- **GPU cost** is measured as the frame-time difference with the showcase shown and hidden (at the window's size and
  4K); a GPU timing method stays chunk 5's.

## Testing

- **Readers:** `seals.yaml` and every refusal and warning, with the line; the shipped table loads clean; the `yaml` fuzz
  target parses it with a seed. Small tests (about 20 chosen samples, worst value asserted once: the user's rule).
- **Maths:** the art's angle and a layer's height (a point on a ground, upright or overhead plane lands where
  expected); each part's
  placement (the first and last glyph of a band, a star's points, a satellite on a turning ring at a few times); the
  life's appear and fade at its edges.
- **Simulation:** casting the swords raises one seal and ends it at the right tick; a seal survives a save and load and
  a replay; the hash covers the seal's name in game data.
- **Hot reload:** a changed `seals.yaml` reaches the next frame without touching the simulation.
- **Pictures:** reference pictures (`pixi run project_n reference`) of each showcase seal at a fixed time, and of the
  swords' seal mid-formation, looked at by eye (look-at-the-game skill): coloured lines glowing on their wash, never a
  white smear.
- **Performance:** a releasedbg profile of the arena with the showcase shown and hidden, and while casting the swords,
  recorded in the Outcome against chunk 4's numbers (frame time median / 99th / worst, the seals' CPU zones,
  `SubmitFrame`). The GPU's share shows as the difference in frame time and `SubmitFrame` with the showcase shown and
  hidden, at the window's size and at 4K. A GPU timing method and a stand-in for the minimum spec stay chunk 5's (the
  phase spec); this chunk designs for cost up front instead (Drawing: bands, caps, sorted parts) (my call).

## Left to the builder

The exact YAML schema and its key names; the shader's part list format and how parts reach it; the limits (parts per
layer, satellites' nesting depth, particles per seal); the rune set's look; the placeholder flourishes and emblems;
whether font characters fit as a glyph set in this chunk; the showcase's places and sizes; the debug key; the art's
ratio as measured from the shadows; and the wash's default darkness, as long as the Goals, Anti-goals and
Accessibility hold.
