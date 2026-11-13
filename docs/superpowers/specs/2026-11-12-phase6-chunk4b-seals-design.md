# Phase 6, chunk 4b: seals

Date: 2026-11-12 · Status: **implemented 2026-11-13** (approved 2026-11-12; revised 2026-11-13 to build on the glows,
approved 2026-11-13; the showcase changed by the user's decisions of 2026-11-13; see Outcome) · Phase spec: `2026-11-10-phase6-design.md` · Builds on: chunk 4
(`2026-11-10-phase6-chunk4-abilities-design.md`) and the glows (`2026-11-12-phase6-glow-design.md`, its Outcome's
"Notes for the seals chunk"), both merged

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
   including one grand seal with 9 rings. (Changed 2026-11-13 by the user's decision, Outcome: the grand seal is about a
   third of the arena across, in a clearing of its own, not screen-wide under the fight.)
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
  (`core::UiColours`), written `<area>.<key>` as `effects.yaml`'s glows write it. A slot is a `core::GlowColour`, the
  glows' type for exactly this choice, read by the same helpers. The showcase's seals have no element and name table entries. New entries for the accents the
  showcase needs (a gold, a violet) join the table under **Effects**, with their names in both languages, so chunk 11's
  menu and presets recolour them (my call). Every part says which slot it takes.
- **Its life:** how long it takes to appear and to fade, along which curve each (the glows' curves: linear, ease_in,
  ease_out, smooth, sine, through `core::CurveAt`), and in what order its rings appear.
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

**The fields are the glows'** (revised 2026-11-13). Since the glow chunk, every sprite already has a distance field,
made from its alpha as it loads, in the R8 field array beside the colour atlas (`gfx::SpriteArray::field`, at
`gfx::FieldBlockOf(sprite)`, signed: negative inside, reach 16 texels each way). So the seals' art is drawn as ordinary
white sprites with a soft alpha edge, with no field written by hand, and the seal shader samples the field array as the
sprite shader's glows do: the negative half gives a rune's antialiased inside, the positive half its halo. A sprite
part's halo reaches at most 16 of its own texels (the field's reach); the analytic parts' halos have no such limit. The
seals' art is sized so its strokes are several texels wide at its own resolution, well inside that reach.

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
- **Cost by design.** Each layer has a hard cap on its parts, and its parts are sorted by where their bands start
  (Outcome, Ruling 32), so a pixel stops at the first part whose band starts past it; band geometry keeps overlapping
  layers from shading the same empty middle nine times.
- **In the world pass: the wash (normal blending), then the lines (additive), premultiplied.** A seal on the ground
  draws under the creatures and their text; an upright or overhead one draws above the creatures but under damage
  numbers, health bars and the HUD, with an opacity cap so the fight stays readable.
- **Light is stateless too.** Pillars are quads; sparks and shards are instances whose positions are functions of their
  seal's start, the render time and a per-particle seed, computed in the vertex shader. Nothing is simulated step by
  step.
- **Time stays precise on the GPU.** The CPU works in `f64` seconds and hands the shaders only small numbers: each
  layer's angle already turned, and each particle stream's age wrapped by its own period. The glows found that an `f32`
  age counted from a seal's start loses sub-tick precision after about 5 hours (their Outcome's notes); the showcase
  seals never end, so their time must not grow without bound on the GPU.
- **Overflow:** layers and particles past their buffers are reported through `ReportOverflow` /
  `core::RepeatedFailure`, as instances and quads are. Nothing is dropped silently.
- **No hitch:** every seal's parts are packed when `seals.yaml` loads or reloads, never on first draw.

### The showcase

The arena gains a few showcase seals placed so that each stays readable and the fight still plays (exact places and
count are the builder's). Together they use every part and every placement. As built, after the user looked at the
running game (2026-11-13, the user's change; Outcome, "The user's decisions while building it"): **no showcase seal
overlaps another or the area where the swords' seal rises at the player's start**, which a test holds, so a cast from
the player's start overlaps none.

- a grand ground seal with 9 rings, two scripts, a star and an emblem, in two colours (the user's demo picture), about a
  third of the arena across in its own clearing below the fight, with nothing on it (the small rock moved to the corner
  for it);
- an upright seal on its own spot, with satellite seals sitting on its ring;
- an overhead seal with pillars of light falling from it and sparks rising, larger and bolder than first built, casting
  a light ink shadow on the ground under it so it reads as floating (`wash.shadow`, the user's change);
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
  4K; on the development machine fullscreen at 2560×1440 in place of 4K, Outcome); a GPU timing method stays chunk 5's.
- **Built on the glows** (the 2026-11-13 revision): the seals sample the glows' field array rather than drawing fields
  into their art; a colour slot is the glows' `core::GlowColour`, read by the same helpers (moved where both readers
  share them), with palette names written `<area>.<key>`; the appear and fade take the glows' curves (`core::CurveAt`);
  the GPU sees only small times (Drawing). A seal does not wear a glow role: its halo comes from its own shader, which
  already has each part's distance.

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
  hidden, at the window's size and at 4K (fullscreen at 2560×1440 on the development machine, whose monitors are
  2560×1440: Outcome). A GPU timing method and a stand-in for the minimum spec stay chunk 5's (the
  phase spec); this chunk designs for cost up front instead (Drawing: bands, caps, sorted parts) (my call).

## Left to the builder

The exact YAML schema and its key names; the shader's part list format and how parts reach it; the limits (parts per
layer, satellites' nesting depth, particles per seal); the rune set's look; the placeholder flourishes and emblems;
whether font characters fit as a glyph set in this chunk; the showcase's places and sizes; the debug key; the art's
ratio as measured from the shadows; and the wash's default darkness, as long as the Goals, Anti-goals and
Accessibility hold.

## Outcome

Implemented on `worktree-phase6-chunk4b-seals`, 2026-11-12 and 2026-11-13, by the plan
`docs/superpowers/plans/2026-11-12-phase6-chunk4b-seals.md` (Tasks 1 to 9, in the plan's parallel waves A to G). This
records what was built, the user's decisions while building it, every ruling of the plan's ledger with what it costs if
wrong, the values chosen, the measurements and what is left open. Commits are named by subject: the
rebase merge rewrites every hash. Each task's review fixes were folded into its commit; builds are verified at each
task's head (`test`, `lint`, `reference`), `check` at the branch's.

### What was built

In the order the tasks were built, wave by wave (the plan's waves A to G), not by task number:

- **Task 4, the record:** "raise a seal under the flying swords". `core::Seal` (`core/seal.hpp`: key, point, size,
  start and end tick, element, `showcase`, never saved) and `core::SealKeyOf` (FNV-1a 64 of the name, `constexpr`);
  `AbilityData::seal` (`core::SealCast`, hashed game data; `abilities.yaml`'s `seal: { name, size }`); `game::Combat`
  raises one at the volley's origin on the cast and ends it at the last sword's departure; `Snapshot::seals`;
  `ARENA_FORMAT` 3 → 4, the `save` seeds regenerated (the fight seed by its generator, the painted seed by hand).
- **Task 1, the art:** "placeholder runes, flourishes and emblems for seals" (`art/placeholders.py`: 24 runes of 48×64,
  three flame flourishes of 256×128, four emblems of 256×256: lotus, swords, eye, spiral). The atlas grew a second
  page; ten pictures were refreshed in the same commit (Ruling 6).
- **Task 3, the accents:** "gold and violet accents for seals" (`UiColour::SealGold` `#F2C14E` and `SealViolet`
  `#B48CF0` under Effects, named in both languages, so chunk 11 recolours them).
- **Task 2, the looks:** "share the palette and curve readers" (`ReadGlowColour`, `ReadPalette`, `ReadNamed`,
  `CURVE_NAMES` and `RefusalAt` moved into `data::internal`) and "read the seals' looks from data" (`core::Seals`,
  `data::ParseSeals` and `LoadSeals`, `assets/data/seals.yaml` with `sword_formation`, the refusals with their line, a
  `yaml` fuzz seed read against the shipped manifest).
- **Task 5, the layout:** "read a seal's pillar cycle from data" (Ruling 12) and "lay seals out at the art's angle"
  (`gfx/seal_draw.hpp`: `SealToWorld`, `SealLifeOpacity`, `SealLayerAngle`, `PackSeals` once per table into a
  `gfx::SealPack`, and `DrawSeals`, a pure function of the snapshot's seals, the table, the pack and the render time
  that lays out bands, washes, satellites and light records with small times only).
- **Task 6, the drawing:** "draw seals with an ink wash under their glow" (`shaders/seal.{vert,frag}.hlsl`,
  `seal.hlsli`, `field.hlsli`; `gfx::internal::SealRenderer`; the analytic parts and the sprite parts sampled from the
  glows' field array; the ground seals after the ground batches, the upright and overhead ones before the trails
  (Ruling 18); `Renderer::SetSeals` packs on load and reload; the shimmer measure; `seal_ground.png`).
- **Task 7, the light:** "light seals with pillars, sparks and shards" (`shaders/seal_light.{vert,frag}.hlsl`;
  `gfx::SealParticleAt` mirrored in HLSL; each particle's ink under its light; `seal_light.png`).
- **Task 8, the showcase:** "show seals of every kind in the arena" (seven showcase seals beside `PROPS`, published
  each tick in the arena only; `gfx::View::showcase`, F8 and `InputCode::KeyF8`; `wash.shadow` (Ruling 27); the
  non-overlap test in app_tests; the small rock moved; `test::RaisedSeals`; six seal pictures).
- **Task 9, this record**, the phase table and `AGENTS.md`'s map line for `seals.yaml`.

Not built, as the spec allowed: a glyph set of the font's characters (only the invented runes ship); seals that act or
follow an entity; a GPU simulation.

### The user's decisions while building it

- **Build on the glows** (2026-11-13, the spec's revision, approved before Task 1): the seals use the glows' field
  array, colours and curves.
- **"I don't like this all overlapping"** (2026-11-13, after looking at the running game): no showcase seal overlaps
  another, and none reaches the area where the swords' seal rises at the player's start, so a cast from the player's
  start overlaps none. The grand seal is large, in its own clearing away from the fight. A test holds it ("the
  showcase seals keep apart and clear of the swords' seal at the player's start, so none hides another"). This replaces
  the spec's "screen-wide" grand seal under the fight and its upright seal "before the cultivator".
- **The overhead seal** (2026-11-13, what the user ticked): its detail was "too small and murky" and it "doesn't read as
  floating". It became larger, with fewer, bolder parts, and casts a light ink shadow on the ground (Ruling 27). The user
  did not tick its dark wash or the pillars' style, which stay.
- **"Move prop and go full"** (2026-11-13): asked whether to keep the grand seal at a quarter of the arena (round 3, the
  only clear spot) or move a prop, the user chose to move the prop: the grand seal is back to about a third of the arena,
  and the small rock moved from (220, 960) to the corner (48, 1050). Older pictures that show the rock moved with it.
- **Fullscreen at 2560×1440 in place of 4K** (2026-11-13): the development machine's monitors are 2560×1440 and niri, a
  tiling compositor, sizes windows itself, so a 3840×2160 window can't be made; the large run is fullscreen at
  2560×1440, the largest this host shows.
- **Profiling waits for the user's go** (2026-11-13): another game was running on the machine with the GPU about 80%
  busy, so the profiling (and `check`, Ruling 31) waited until the user had closed it and said go.

### Rulings

Each is the ledger's, with its reason and what it costs if wrong.

1. **Task 6's arena seal picture has no showcase to hide:** the plan orders Task 6 before Task 8, which adds
   `View::showcase` and hides it in the older pictures. Wrong: one picture re-taken in Task 8.
2. **`Seal::showcase` is not among the saved fields:** showcase seals are republished by the scene each tick, never
   saved (the spec: "rebuilt from the scene, not saved"). Wrong: one field in the save format.
3. **The fade's midpoint sample uses a linear curve (0.5).** Revised at Task 5's review: a fade is
   `1 − CurveAt(curve, fraction passed)`, the glows' convention, so ease_in gives 0.75 at its midpoint, not the 0.25
   first ruled. Wrong: one test value.
4. **`SealParticleAt` exists in C++ and in HLSL** by the plan's design: the C++ copy is how placement is tested without a
   device. Both keep identical constants, each with a comment pointing at the other. Wrong: a drift the reference
   pictures would catch.
5. **Task worktrees live under the feature worktree's `.worktrees/`** (gitignored, allowed by AGENTS.md): the session
   could not write under the main checkout's `.claude/worktrees/`. They and their `seals-t*` branches were removed after
   their tasks landed. Wrong: `prune-worktrees` would miss such worktrees, which would then need removing by hand.
6. **Task 1's repack and its ten refreshed pictures are accepted.** Page 0 was full; the shelf packer sorts tallest
   first, so the new sprites interleave and existing ones move, and mip filtering depends on a block's alignment, so
   minified edges shift (scattered, at most 5 levels; the three quarter-zoom pictures about 2,000 pixels at most 51,
   the same scene by eye). Stable packing is out of scope. `arena_tests`' `Draws` helper now compares the atlas page too,
   a latent test bug the second page exposed. Wrong: a packer change and ten pictures.
7. **A loaded seal is refused when it starts after the save's tick or ends before it starts**, as the flashes are: saves
   are untrusted input. First ruled to refuse an already-ended seal too; revised, because such a seal is harmless (gone
   the next tick) and refusing it forced a change to the `save` fuzz input's format. Wrong: a stale seal lives one tick.
8. **Task 4 as built:** a `seal:` on an ability that flies nothing is refused (as `motion:` is); `SealCast` sits in
   `core/seal.hpp`; `core::Logic` clears `snapshot.seals` each publish; the replay seed is unchanged (its format did not
   move); the painted `save` seed was edited to format 4 by hand (it has no generator). Wrong: small moves.
9. **The `yaml` fuzz target's ASan container-overflow** in yaml-cpp's `ScanDirective`, reached from `ParseGameData`
   before `ParseSeals`, predates this chunk (clean with that check off: 24,527 runs). It went to the sweep-up backlog.
   Wrong: `check --sanitizers` fails when asked for.
10. **`SealLight::pillarFade`** (`light.pillars.fade`, 0 to 1): the spec lists how pillars fade, and a fade fixed in the
    shader is a look in code. Wrong: one unused field.
11. **The reader's `SECTIONS` is a `constexpr std::array`**, not a namespace-scope `initializer_list` (an MSVC risk; the
    repo's pattern). Wrong: none.
12. **The pillars' cycle is data** (`SealLight::pillarCycle`, `light.pillars.cycle`): a code constant would be a look
    in code (the "A seal that's code" anti-goal). Wrong: one field.
13. **Task 5 as built:** `DrawSeals` takes the manifest; `SealDrawing::sprites` holds the glyph sets' sprites; the
    frame and wash fields ride on `SealGpuLayer`; the wash's ink is the palette's `TextInk` (recoloured with it);
    `spin_scale` 0 makes appear and fade instant (reduce motion's "still" never hides a seal); an `element` slot on a seal
    without an element shows Damage's colour; `core::detail::Mix` is reused for the seeds. Wrong: small moves.
14. **The bands' margins for anti-aliasing and the halo are Task 6's.** Wrong: clipped edges, which Task 6's pictures
    would catch.
15. **Parts are packed once per table** (`gfx::PackSeals` into a `SealPack`), the spec's "packed when seals.yaml loads",
    rather than each frame. Wrong: an extra type.
16. **`spin_scale` also scales particle time:** reduce motion slows the light as it slows the rings. Wrong: wording.
17. **Task 5's second fix (a comment rewrap) was re-reviewed by a mechanical check** (the word sequence identical, no
    line over 125 columns) instead of a model: a rewrap has nothing to judge. Wrong: none, the check is stricter.
18. **Upright and overhead seals draw under the swords' trails, the overlay, text and the HUD**: after the selection and
    hover outlines, before the trails. The spec's Drawing ("under damage numbers, health bars and the HUD") and the
    "Hiding the fight" anti-goal bind over the plan's and the recalled history's "after the overlay"; the trails and
    bursts belong to the swords. Flagged to the user, since the history marked that order as decided; the approved
    spec's own text ("under damage numbers, health bars and the HUD") decides it, and the user, told on 2026-11-13, did
    not ask for the old order back. Wrong: one draw call moved.
19. **The halo is table data:** `halo: { reach, strength }` at the top of `seals.yaml` (0.012 and 0.3, refused out of
    range with the line). The band margin is derived as the halo's reach plus a fixed anti-aliasing allowance (0.018), a
    mechanism constant, not a look. Wrong: two fields.
20. **Dashes wrap from the ring's first dash**, then by the period, so a dash and gap that don't divide 360 leave their
    seam at the ring's start, turning with it, instead of at atan2's ±180°, fixed on screen. Authored values are kept.
    Wrong: one shader line.
21. **Bands grow about a pixel in the vertex shader** (`SEAL_GROW_PIXELS`, through the new `pixelsPerClip` uniform),
    so a small seal's anti-aliased edge is never cut flat by its geometry. Wrong: a vertex-shader step.
22. **Each particle draws its seal's ink under its light** (normal blend, then additive): the spec's "ink wash, then
    glow" and the "Washed out" anti-goal on the pale paper. Two draws a plane instead of the plan's one. Wrong: two draw
    calls a plane.
23. **Pillars fall from an overhead seal and rise from a ground or upright one**, and a ground seal's light draws in the
    ground pass, so creatures stand over it: the spec's "pillars falling from it" and Ruling 18. Wrong: one sign.
24. **The light's proportions are data:** `light.pillars.rest` (0.35), `light.pillars.band` (0.15, at most 0.5),
    `light.shards.thin` (0.35) and `light.shards.edge` (0.5; it was in shard units, not pixels), as Rulings 10, 12 and
    19. `SEAL_LEAST_PIXELS` (2), the anti-shimmer floor in pixels, stays a mechanism constant. Wrong: four fields.
25. **Particle streams carry their lap**, a small integer worked out in `f64` on the CPU, so each spark or shard is born
    somewhere new each life: still stateless. Accepted with the files outside Task 7's brief. The vertex shader's binary
    search for a particle's record (at most 64 records, about 6 steps, per vertex) stands until the measurements say
    otherwise (Measurements). Wrong: a per-particle index buffer.
26. **`seals.yaml`'s dash comment says what the shader does:** a period that doesn't divide 360 leaves its remainder
    joined to the ring's first dash, one longer or shorter run there, turning with the ring. That satisfies Ruling 20;
    the comment was wrong. Wrong: one comment.
27. **An overhead seal casts a ground cue**: `wash.shadow` (0 to 1; refused on a ground or upright seal), a light ink
    shadow on the ground plane under it, drawn in the ground pass through the wash's own path (no new shader or
    pipeline), its pillars landing in it. The user's feedback and the "A flat decal" anti-goal. Wrong: one key and one
    ground draw per overhead seal.
28. **Round 3 moved the grand seal off the small rock rather than the rock** (and so shrank it to a quarter of the
    arena, the only clear spot). Superseded by the user's "move prop and go full". Wrong: superseded.
29. **Task 8's round 4 resumed the same implementer** (Opus, already the strongest tier): the round carried a new user
    decision, not a fix that had failed; the escalation rule targets stuck loops. Wrong: none.
30. **The showcase-hidden profiles run on a temporary build** with `App::m_showcase` defaulting to false, never
    committed, reverted and rebuilt afterwards: F8 is the only switch, a profile run takes no input, and the host has no
    key-injection tool. Captures are read with `tracy-csvexport -u` 0.14.1 (the pinned recorder's version), from
    Pixi's `pixi exec` cache. Wrong: the hidden runs measure a build one default apart.
31. **`check` waits for the user's go, as the profiling does:** it saturates the CPU while the user is playing. Wrong:
    none; it runs before the pull request.
32. **A layer's parts are sorted by where their bands start, and the shader stops at the first part whose band starts
    past the pixel** (the final review): sorted by radius they bought nothing, since the shader tested every part's band
    and a band's start isn't monotonic in radius (a star's starts at its apothem). `PackSeals` sorts by the band's inner
    bound before the shared margin (the halo and a pixel), worked out as `PartBand()` in `seal.frag.hlsl` works it out,
    ties in the look's order, so the spec's "Cost by design" holds. No reference picture moved. Wrong: one shader loop.
33. **The above-opacity cap applies after brightness** (the final review): brightness reaches 10, and capping before
    it let raised seals pass the "Hiding the fight" cap. A layer's, emblem's and light's strength is now
    `min(opacity × life × brightness, above_opacity)` off the ground and uncapped on it, so brightness above 1 stays
    available to chunk 11's accessibility settings while the cap holds. At the shipped brightness of 1 nothing moves.
    Wrong: one `min` per strength.
34. **`seals.yaml`'s untrusted values are bounded against the overflows the final review found** (the final review;
    its re-review found two more, Ruling 35): a stream's
    period (`light.pillars.cycle`, `light.sparks.life`) is at least a tick (1/60 s), so its laps stay a count an `f64`
    holds; a value above 0 that its `f32` field would hold as 0 or a denormal (`1e-50`) is refused as too small for an
    `f32`, as `abilities.yaml`'s seal size is; and a table has at most 1,024 seals, 64 glyph sets of at most 256
    sprites each, and a look at most 63 satellites (with its seal, one frame's 64), each refused with its line. With
    those caps the largest pack fits the `u32` sizes of its GPU buffers (a `static_assert`), the pack's upload checks
    its sizes instead of narrowing them (`gfx::SealBufferRoom`, which also no longer wraps doubling its room), and the
    layout keeps a stream's lap as its count modulo 2^32 worked out in `f64` (a shard cycle comes from the seal's size,
    so a tiny satellite's still passes 2^64 laps). Once a frame holds `MAX_SEALS_DRAWN` seals, `DrawSeals` stops
    walking satellites and counts each left-out seal with all it would have drawn, from counts packed with the table.
    The glyph sets' count cap is the fixer's: without it the pack's sprites have no bound. A test draws a table read at
    the reader's extremes (`seal_table_tests.cpp`, whose layout overflowed a `u64` under UBSan before), and the `yaml`
    fuzz target has a seed past the satellites' cap (`seals_caps`). Wrong: refused tables that were harmless.
35. **A shard's cycle and a satellite's size are bounded too** (the final review's re-review, the same kind as Ruling 34:
    in-range values the layout still turned into inf, NaN or an f32 narrowing past its range). The reader refuses, with
    the line, a shard spin that isn't 0 but turns slower than 0.001°/s (one turn in 100 hours) and a satellite smaller
    than 0.001 of its seal, so nested twice it stays a normal `f32` on any seal above about 1.2e-32 across. What depends
    on a seal's size at run time is guarded in the layout: a shard's cycle is worked in `f64` and held to an hour (the
    reader's longest period) before it narrows, so a drift tiny next to its seal's size still gives a finite cycle, and
    `SealWrap` takes no period an `f32` can't hold; a seal or satellite whose size isn't a normal `f32` is laid out not
    at all, counted with its satellites in `SealDrawing::tinySeals`, which the renderer logs once until they draw again.
    Tests in `seals_tests.cpp` and `seal_table_tests.cpp`, failing first. The user's rule (every finding fixed) over the
    skill's single fix wave. Wrong: two bounds and a skip.

Smaller calls from the tasks' reports, each in its commit: the reader's key names and bounds (counts 1 to 360, frame
sides up to 64, spins ±720°/s, times up to 3,600 s, periods at least a tick, radii and sizes in (0, 1] of the seal's
size and normal as an `f32`); a flourish's 1 to 3
sprites repeat in turn to fill three slots; two names hashing to one key are refused; sprite parts anti-alias over the
longer axis of the pixel (Task 6's review: the geometric mean was too narrow along the squashed axis; the fix halved
the worst shimmer tile, 2.05% to 1.05%); a frame layer's round parts are cut at the frame's own polygons with a one-pixel fade; the
small showcase seals are upright, because a satellite takes its own look's plane and stands in an upright ring; the
seal tests count only seals raised in play (`test::RaisedSeals`), with the same strictness.

### Values chosen

- **`seals.yaml`'s table:** `view_ratio` 0.375 (the blob shadow's 24 / 64), `above_opacity` 0.7, `spin_scale` 1,
  `brightness` 1, `halo: { reach: 0.012, strength: 0.3 }`, one glyph set (`runes`, 24 sprites).
- **Limits:** 1 to 9 layers a seal, 16 parts a layer, frames of 3 to 64 sides, satellites 2 deep and 63 a look, each
  0.001 to 1 of its seal's size (Ruling 35), 256 particles a seal, 1,024 seals and 64 glyph sets of 1 to 256 sprites a
  table, periods of a tick (1/60 s) to 3,600 s (Ruling 34), shard spins of 0 or 0.001°/s and more either way and shard
  cycles of at most an hour (Ruling 35), 64 seals drawn a frame, satellites included (`gfx::MAX_SEALS_DRAWN`). The
  buffers hold the worst case: 704 layers (64 × (9 + an emblem + a shadow)) and 40,064 quads (64 × (9 × 64 + the
  emblem's 48 + a wash + a shadow)); a test
  draws that worst case and checks it fits (Task 8's review found the shadow missing from the first bounds).
- **Sizes:** `SealGpuPart` 64 bytes, `SealGpuSprite` 32, `SealGpuLayer` 88, `SealVertex` 24, `SealGpuParticles` 160;
  uniforms of 32 (band vertex), 48 (light vertex) and 16 bytes (band fragment). A round band is 48 quads; the band
  margin 0.03 of the seal's size (halo 0.012 + allowance 0.018).
- **The swords' seal:** `sword_formation`, size 420 (outer radius, world units), ground, `element` colour, three
  layers (24 outward runes and a rim; 72 ticks, 18 inward runes and a dashed ring, turning the other way; a ring and a
  hexagram), the swords emblem, 24 sparks; appear 0.4 s ease out, fade 0.3 s smooth.
- **The showcase** (`game/arena.cpp`'s `SHOWCASE`; point and outer radius, world units; reach as laid out by the
  non-overlap test):

  | Seal | Point | Size | Reach | What it shows |
  |---|---|---|---|---|
  | `showcase_grand` | (402, 935) | 290 | (95, 820)–(709, 1050) | ground, 9 layers rising to 76, gold and blue, two rune bands at two sizes, a 7°/4° dashed ring, ticks, dots, an 8-point star, a hexagon, a 5-point star, the lotus; no light |
  | `showcase_upright` | (275, 700) | 70 | (187, 532)–(363, 708) | upright, lift 80, a 25°/10° dashed ring, three satellites (with the layer, orbiting at −12°/s, fixed) |
  | `showcase_overhead` | (715, 340) | 120 | (581, 70)–(849, 397) | overhead, lift 180, bold runes and dots, the spiral, a shadow of 0.3, 6 pillars of 180 and 30 sparks |
  | `showcase_tiers` | (975, 890) | 100 | (795, 823)–(1155, 958) | ground, four frames (square, diamond, square, diamond) at heights 0, 18, 36, 54, gold circles cut at their sides |
  | `showcase_flame` | (1650, 968) | 105 | (1414, 863)–(1886, 1073) | violet and gold, 9 flame flourishes spilling 0.4 past the ring, the eye, 28 drifting shards |
  | `showcase_small_1` | (1300, 140) | 40 | (1256, 96)–(1344, 184) | upright, 1 layer |
  | `showcase_small_3` | (1300, 290) | 55 | (1240, 230)–(1361, 351) | upright, 3 layers |

  The swords' seal's start area, kept clear, is (490, 406)–(1430, 796). Ten seals draw before a cast (seven and three
  satellites), eleven with the swords' seal.
- **Washes on the paper:** gold and violet read more weakly than the damage blue on the pale ground, so the washes were
  raised until they read (grand 0.45, overhead 0.7, tiers 0.75, flame 0.6, upright 0.6).
- **Pictures:** new `seal_ground`, `seal_light`, `seal_swords` (a real cast, the showcase hidden) and the showcase's
  `seal_showcase` (zoom 1) with the close-ups `seal_showcase_above`, `seal_showcase_left`, `seal_showcase_tiers` and
  `seal_showcase_flame` (zoom 2), each looked at; between them every showcase item. Changed: ten by the atlas's second
  page (Ruling 6), `recoloured` and `recoloured_bare` (the garish test palette assigns colours by index), and 21 by the
  small rock's move (each diff only at its old and new places). The old arena pictures take `.showcase = false`.
- **The shimmer measure** (Task 6, in `reference_tests.cpp`): two captures a quarter pixel apart at the smallest zoom,
  ink summed under Gaussian windows (σ 16 px, every 32 px) that move with the content, the worst tile's change against
  the labels' bound of 2%. Over the backdrop it fails hard-edged lines (9.37%) and passes the shipped seal (1.05%); over
  the pale ground the wash dominates every tile (1.60% hard-edged, 0.32% shipped), so that case alone doesn't
  discriminate and the backdrop case is the one that guards.

### Measurements

**Conditions.** Measured 2026-11-13, 15:52 to 16:05, on the development machine: NVIDIA RTX 4090, Wayland (niri),
Vulkan presenting with mailbox, releasedbg, linux-clang, 30 workers (unpinned), the arena at its starting zoom unless
noted, `pixi run project_n profile --seconds 30` each. The other game had been closed; RuneLite (a Java game client)
stayed open through every run, and the GPU's load before each run, with the desktop otherwise idle, was 4 to 23%. No run
paused (the game logged no lost focus). Window runs: `settings.yaml` asks for 1280×720, and niri tiled the window at
**1256×1368** (`niri msg windows` during each run), so chunk 4's and the glows' "1280×720" were very likely this size
too. Fullscreen runs: 2560×1440 (the user's decision in place of 4K), on a 165 Hz monitor, where the game's frames were
**paced at the display's refresh** (6.06 ms) despite mailbox, so their frame time measures the display, not the work:
for them "busy" is the loop's time less its wait in `AcquireFrame`. The showcase-hidden runs used Ruling 30's temporary
build. Read with `tracy-csvexport -u` 0.14.1, the copy in Pixi's `pixi exec` cache (a fresh `pixi exec` solve now fails
on its pre-release `libcapstone`); a frame is one iteration of `App::Run`'s loop on the main thread; median / 99th
percentile (nearest rank) / worst; the first two seconds skipped; frame counts in brackets. A volley starts at its first
`DrawVolleys` call over 100 µs after 1.5 s without one (each cast found once, about 6 s apart); it counts as drawn from 0.5 s
before that to 3.5 s after, its peak 1.5 to 2.0 s after; four complete volleys in every cast run. To tell this chunk's
changes from the session's conditions, `main` (the branch's base) was built in a scratch worktree and
profiled the same way between the branch's runs, and the branch's hidden cast runs were repeated after it (both runs
shown).

**The showcase's cost** (seven showcase seals and three satellites, ten seals a frame):

| Main thread, one loop iteration (median / 99th / worst, ms) | Window, 1256×1368 | Fullscreen 2560×1440 (busy) |
|---|---|---|
| Showcase hidden, no cast | 0.107 / 0.328 / 1.80 (234,339) | 0.983 / 3.75 / 5.65 (4,621) |
| Showcase shown, no cast | 0.174 / 0.418 / 13.5 (140,869) | 1.35 / 4.18 / 5.59 (4,621) |
| Difference (the showcase) | +0.067; +0.090 | +0.363; +0.43 |
| `--cast-swords`, hidden: without volleys | 0.109 / 0.755 / 2.67 (65,817) | — |
| `--cast-swords`, hidden: with a volley drawn | 0.460 / 2.19 / 3.72 (31,627) | — |
| `--cast-swords`, hidden: at a volley's peak | 0.551 / 1.65 / 2.63 (4,131) | — |
| `--cast-swords`, shown: without volleys | 0.185 / 1.45 / 29.7 (37,305) | — |
| `--cast-swords`, shown: with a volley drawn | 0.630 / 2.50 / 3.92 (22,680) | — |
| `--cast-swords`, shown: at a volley's peak | 0.860 / 2.24 / 2.84 (2,623) | — |
| `--cast-swords`, whole run, busy: hidden; shown | — | 2.28 / 5.32 / 6.47; 2.44 / 5.57 / 7.75 (4,618; 4,623) |

| Per call (median / 99th / worst, µs) | Window, 1256×1368 | Fullscreen 2560×1440 |
|---|---|---|
| `DrawSeals`, showcase shown | 24.0 / 55.6 / 137 | 123 / 163 / 357 |
| `LaySeals` (`DrawSeals` and the missing-key check), shown | 24.4 / 56.3 / 139 | 126 / 167 / 362 |
| `SealRenderer::Upload`, shown | 13.4 / 28.3 / 106 | 60.6 / 103 / 278 |
| `SealRenderer::DrawGround`; `DrawAbove` (recording), shown | 1.6 / 4.5 / 74.0; 1.4 / 3.4 / 41.4 | 12.5 / 34.4 / 615; 7.8 / 19.6 / 69.7 |
| `SealRenderer::UploadPack` | one call a run, at the start | one call a run |
| `SubmitFrame`, showcase hidden | 64.3 / 251 / 1,410 | 653 / 3,463 / 5,292 |
| `SubmitFrame`, showcase shown | 84.9 / 276 / 1,511 | 797 / 3,639 / 5,070 |
| The swords' seal alone, while up (about 2.0 s a cast): `DrawSeals`; `Upload`; `DrawGround` | 4.0 / 19.9 / 223; 3.5 / 19.7 / 58.6; 2.2 / 17.8 / 47.4 | 18.2 / 33.8 / 120; 16.7 / 37.0 / 126; 14.0 / 35.9 / 60.0 |
| `DrawSeals`, `--cast-swords` shown, at a volley's peak | 44.7 / 131 / 331 | 139 / 180 / 229 |
| `SubmitFrame`, `--cast-swords` shown, at a volley's peak | 158 / 391 / 1,098 | 798 / 3,616 / 4,152 |
| `DrawVolleys`, at a volley's peak: hidden; shown | 308 / 989 / 1,668; 444 / 1,302 / 1,627 | 1,429 / 1,794 / 2,592; 1,421 / 1,718 / 1,855 |

**Against `main` and the earlier chunks** (`--cast-swords`; `main` and this branch measured in this session; the
glows' head and chunk 4's final rings copied from their Outcomes, whose volley windows were set a little differently):

| Main thread, one loop iteration (median / 99th / worst, ms) | Chunk 4, final rings | The glows' head | `main`, this session | Chunk 4b, showcase hidden (two runs) |
|---|---|---|---|---|
| Zoom 2, without volleys | 0.382 / 2.03 / 3.23 | 0.122 / 0.637 / 2.16 | 0.109 / 0.846 / 3.46 | 0.114 / 1.08 / 12.4; 0.114 / 1.01 / 3.43 |
| Zoom 2, with a volley drawn | 1.32 / 2.21 / 3.88 | 0.463 / 1.48 / 2.36 | 0.478 / 2.08 / 23.4 | 0.480 / 2.18 / 25.8; 0.509 / 2.18 / 24.0 |
| Zoom 2, at a volley's peak | 1.34 / 2.26 / 3.88 | 0.499 / 1.47 / 2.06 | 0.487 / 1.03 / 1.49 | 0.655 / 2.06 / 2.54; 0.594 / 1.50 / 24.0 |
| Starting zoom, without volleys | 0.210 / 2.00 / 3.19 (Task 10) | — | 0.106 / 0.612 / 11.5 | 0.109 / 0.755 / 2.67; 0.110 / 0.844 / 3.91 |
| Starting zoom, with a volley drawn | 1.31 / 2.20 / 2.92 (Task 10) | — | 0.468 / 2.13 / 25.2 | 0.460 / 2.19 / 3.72; 0.468 / 2.22 / 35.1 |
| Starting zoom, at a volley's peak | 1.31 / 2.31 / 2.60 (Task 10) | — | 0.485 / 0.910 / 1.63 | 0.551 / 1.65 / 2.63; 0.567 / 2.07 / 2.70 |
| `DrawVolleys` at the peak, zoom 2, µs | 327 / 528 / 663 | 280 / 407 / 577 | 269 / 639 / 859 | 370 / 1,274 / 1,539; 321 / 886 / 1,436 |
| `SubmitFrame` at the peak, zoom 2, µs | 904 / 1,811 / 3,261 | 123 / 1,096 / 1,671 | 121 / 324 / 1,123 | 139 / 346 / 1,465; 137 / 370 / 958 |

**What they say:**

- **The phase bar holds with room to spare.** At the window's size the showcase costs 0.067 ms of a median frame
  (0.107 → 0.174 ms) and 0.09 ms of its 99th; at 2560×1440 0.36 ms of the busy time (0.98 → 1.35 ms), whose 99th
  stays under 4.2 ms, and under 5.6 ms while casting, against the 16.7 ms of 60 fps. Every frame over 12 ms (none to
  two a run, 12 to 35 ms) is almost all a wait in `AcquireFrame` for the compositor's image, and `main`'s runs have
  them too: none is the seals' work. `UploadPack` ran once a run, at the start: no first-draw hitch.
- **Most of it is the CPU's layout.** At the window's size `DrawSeals` (24 µs) and the upload (13 µs) are about 40 of
  the 67 µs; `SubmitFrame`, which waits on the driver and the GPU, grows 21 µs. At 2560×1440 `SubmitFrame` grows
  144 µs, part of which may follow the pixels, but the CPU zones grow there too (`DrawSeals` 123 µs for the same work,
  laid out in world units): every zone ran three to five times slower with the frame paced at 165 Hz (`DrawVolleys`
  1,420 µs against 300 to 450 µs, in `main`'s code as in this branch's), so the fullscreen per-call times are not
  comparable with the window's, and the GPU's share can't be split from them. A GPU timing method stays chunk 5's.
- **The swords' seal is cheap:** about 10 µs a frame of CPU (layout 4.0, upload 3.5, recording 2.2) while it is up,
  about 2.0 s a cast.
- **Nothing else got slower.** With the showcase hidden this branch matches `main` measured the same afternoon: over a
  whole volley 0.460 and 0.468 ms against 0.468 (starting zoom) and 0.480 and 0.509 against 0.478 (zoom 2), without
  volleys 0.109 to 0.114 against 0.106 to 0.109. At the peak's half second the branch's medians are 14 to 35% higher
  in all four runs, but the seals' own zones have medians under 1 µs there (the swords' seal is gone by then) and the
  difference is in `DrawVolleys`, which this chunk doesn't change and whose 99th at the peak (886 to 1,353 µs against
  `main`'s 515 and 639) shows its launch-time spike falling into that half second; with four volleys a run, that
  window is the noisiest. The glows' head and `main` agree within 3% with a volley and 11% without; chunk 4's own
  numbers were taken while the window kept losing its focus.
- **With the swords and the showcase together** the frame at a volley's peak is 0.31 ms over the hidden run (0.551 →
  0.860 ms), more than the showcase alone (0.067 ms): the seals' zones add about 70 µs there, and `DrawVolleys`, which
  the seals don't touch, 136 µs more (444 against 308 µs). From one run each, this is not settled; it stays far under
  the bar either way.
- **The particle vertex shader's binary search** (Ruling 25) is not visible: the showcase's 64 particles and the
  swords' 24 sparks add nothing measurable to `SubmitFrame` beyond the 21 µs of the whole showcase, and a search is at
  most 6 steps per vertex over at most 64 records. It stands; a per-particle index buffer waits for a case that shows
  it.

### Open and observed

- **D3D12 and Metal** are checked for the seal and light shaders only by the shaders' reflection and the `shaders`
  tool's layout check, not by a picture: CI's pictures are software Vulkan (lavapipe). The fragment storage buffers at
  `t1..t3 space2` and the vertex stage's layers at `t0 space0` are the new bindings to watch.
- **Reference drift:** 15 committed pictures differ from what lavapipe draws on the development machine, within the
  tolerance (most by 1 to 3 levels, `fox_dissolving` up to 9, `volley_converging` up to 45 in a few pixels), already at
  the base this chunk started its drawing from; not this chunk's doing. `reference --update` rewrites them, so every
  task restored them by hand. For the sweep-up.
- **The `yaml` fuzz target's ASan container-overflow** in yaml-cpp (Ruling 9) is in the sweep-up backlog.
- **No glow at the pillars' feet:** the shadow's ink takes them, which reads, but a glow where each pillar lands would
  need a shader change.
- **The grand seal's lower left corner lies under the HUD** in the game's opening view, as the rock did; the HUD stays
  readable over it.
- **Props aren't in the non-overlap test** (the arena's props are private and the published instances mix them with
  wandering villagers); nothing stands on a showcase seal by eye, and villagers can cross a seal's edge as creatures
  cross every ground seal.
- **The flame seal is small** (105): its shards drift one seal size out, so its reach is twice its size, which bounds it
  in its corner.
- **Window sizes on this host:** under niri the swapchain's size is the compositor's, not `settings.yaml`'s: the
  "1280×720" window was tiled at 1256×1368, and fullscreen is paced at the monitor's 165 Hz (Measurements; a gotcha in
  `src/AGENTS.md`).

### Notes for later chunks

- **Seals that act** add fields to `core::Seal` (a following entity's `StableId`; a reach, a period and an amount
  through `game::HitAmount` in game data) without changing the existing ones; drawing stays a pure function of the
  record, the table and the render time, so a GPU simulation can replace `DrawSeals` behind the same inputs.
- **Chunk 7** moves the showcase's places (`SHOWCASE`) into scene data with the props.
- **Chunk 11** wires `spin_scale` (spin, particle time, appear and fade) and `brightness` (lines and light) to reduce
  motion and reduce flashing, and recolours `effects.seal_gold` and `effects.seal_violet` with the palette.
- **Real art** replaces the placeholder runes, flourishes and emblems; any sprite works, since its field comes from its
  alpha. Keep strokes several texels wide at the sprite's own size: a halo reaches at most 16 of its texels.
