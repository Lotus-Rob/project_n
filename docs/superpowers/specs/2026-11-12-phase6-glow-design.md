# Phase 6: data-driven glows

Date: 2026-11-12 · Status: **approved 2026-11-12** · Phase spec: `2026-11-10-phase6-design.md` · Comes
after chunk 4 (`2026-11-10-phase6-chunk4-abilities-design.md`) and before the seals (chunk 4b,
`2026-11-12-phase6-chunk4b-seals-design.md` on its own branch, revised after this lands)

## In short

- **Any sprite can glow, and the data decides which glow, with what look, when.** A glow follows the sprite's real
  outline, soft or hard, drawn as light or as ink, in a palette colour or the ability's element.
- **A distance field for every sprite**, made when its pixels are decoded at load, kept in a texture array beside the
  atlas. The glow reads one value from it per pixel.
- **Glows are named entries in `assets/data/effects.yaml`** (`glows:`): colour, blend, size, feather, opacity, how much
  they thin as the camera zooms in, and a **drive** that moves them over time: `steady`, `pulse`, `charge` (follows a
  value from 0 to 1, fades on release) or `progress` (follows a value, then takes a result's colour), each with its speed
  and curve.
- **First users:** the flying swords and the fire bolt (their halo and core sprites go: a sword is one sprite, not
  three), and **the selection and hover outlines**, which become glows. The arena shows one glow of every drive.
- **No measurable cost:** the 1,000-sword volley is profiled before and after; the glow should make it cheaper, not
  dearer.

## Intent

The user wants "a glow around the edge of the sword (objects)": today each sword and the bolt carry a round halo
sprite, a soft blob that ignores the blade's shape, and a tinted core under it. The user asked for one glow shader that
can be attached to anything, data-driven in colour, opacity, feathering and size, over a new sprite that would fix only
the swords. Weapons and projectiles come first; NPCs, the player and seals should be able to use it later with no new
code.

The user's cases, which the design must be able to express though none is built here:

- **A sword charging:** it glows brighter while charging, then fades.
- **An NPC casting a special ability:** it glows during the charge-up.
- **Crafting at a cauldron:** the cauldron's glow grows with the craft's progress, then takes a colour that shows the
  craft's quality.

And the user's constraints: "very little/no overhead to our render ms target", and room for "gpu simulation in the
future" (the backlog's GPU-projectiles item: each projectile a stateless function of its record and the time).

## Goals

1. **Every sprite has a distance field**, so any sprite can wear a glow with no art step.
2. **A glow is data:** named entries in `effects.yaml`, hot-reloaded as the rest of that file is, refused with their
   line when out of range (the old table stays), with the shipped values mirrored in `core::Effects{}`.
3. **Four drives**, each with its speed and curve in the data: `steady`, `pulse`, `charge`, `progress`.
4. **The swords and the bolt wear glows** in place of their halo and core; the selection and hover outlines become
   glows.
5. **The demo shows every drive** working in the arena, so each can be seen before a feature uses it.
6. **Cost:** a sprite without a glow pays nothing in the fragment shader; the volley is measured before and after.
7. **Stateless:** a glow's look at any moment is a function of its entry, the inputs the game supplies (which glow, a
   value, a start time, a result) and the time; nothing is remembered from frame to frame.

## Non-goals

- Charging abilities, NPC casts and crafting: their glows are data plus one number from the game, when those features
  come.
- Moving glow evaluation onto the GPU or generating the volley's instances there (both backlogged: "GPU projectiles at
  scale" and "A volley's 3,000 sprites").
- Large blooms: a glow reaches at most 16 texels past the sprite's edge (the distance field's reach).
- Curves drawn point by point: a short list of named curves is enough for now (the user: the drive and its "speed/curve"
  are data choices).
- Chunk 11's settings screen: this chunk adds the fields chunk 11 will drive (Accessibility), not the screen.

## Anti-goals (a failure even if it works)

- **A frame that gets slower.** The volley at its peak, at both zooms, must not lose time against the baseline taken
  on main before this branch; a regression is a review finding to fix (the phase bar).
- **A glow that isn't stateless** (a per-sprite history kept between frames): it would close the GPU route.
- **Logic depending on looks.** Glows are presentation: nothing in the simulation reads them, the hashed game data never
  names them, a replay doesn't change with them.
- **Outlines that change meaning.** The selection and hover outlines must still read as before: same colours from the
  palette, still drawn over the world.
- **A glow that flashes faster than three times a second.**

## Design

### Distance fields

- **Made at load, in C++, on the job system**, when each sprite's pixels are decoded (`internal::DecodeSprite`, which
  scene loading already runs on workers, design doc 0002.3.1 §5), not in `project_n atlas`. **My call** (the
  conversation said "in the atlas step"): the atlas step only places sprites and writes no pixels, and `project_n.py` is
  standard library only (tools/AGENTS.md); the game already builds each page's pixels at load.
- **An exact Euclidean distance transform** of the alpha channel thresholded at one half (a linear-time two-pass method,
  such as Felzenszwalb and Huttenlocher's), **signed**: negative inside, positive outside, clamped to ±16 texels and
  stored as R8 unorm with the edge at one half. Glows read only the outside; the sign costs nothing and gives the seals'
  runes and emblems their antialiased inside (my call).
- **A second texture array, R8**, with the colour array's shape (one 2048 page today, five mips), each field at its
  sprite's place on the page. About 5.6 MiB a page with mips; recorded in the Outcome.
- **The gutter holds the field.** A glow samples up to 16 texels beyond the sprite's region, so the field must reach
  that far without meeting a neighbour's: the packer's gutter grows from 16 to 32 texels (one gutter between sprites,
  so each side keeps 16), which `project_n atlas` re-packs. The colour array's gutter stays edge-filled as today; the
  sprite's colour is still read only inside its region.
- **Loading time** grows by the transforms (the 1920×1080 ground is the largest); measured and recorded with the scene
  load, a designed pause (phase spec).

### The glow data

`effects.yaml` gains a `glows:` table of named entries; ability looks and the outlines refer to them by name, and an
unknown name is refused with its line. The shipped table, as data (the user's tuned values are the swords'):

```yaml
glows:
  sword_aura:    { colour: element, blend: light, size: 8.5, feather: 0.75, opacity: 0.15, zoom_shrink: 0.6,
                   drive: { kind: pulse, period: 1.6, depth: 0.9, curve: sine } }
  bolt_aura:     { colour: element, blend: light, size: 8.5, feather: 0.75, opacity: 0.15, zoom_shrink: 0.6,
                   drive: { kind: pulse, period: 1.6, depth: 0.9, curve: sine } }
  selected:      { colour: selected_outline, blend: ink, size: 3, feather: 0, opacity: 1, zoom_shrink: 0,
                   drive: { kind: steady } }
  hover:         { colour: hover_outline, blend: ink, size: 3, feather: 0, opacity: 1, zoom_shrink: 0,
                   drive: { kind: steady } }
  demo_pulse:    { ... drive: { kind: pulse, period: 1.6, depth: 0.9, curve: sine } }
  demo_charge:   { ... drive: { kind: charge, curve: ease_in, fade: 0.4, fade_curve: ease_out } }
  demo_progress: { ... drive: { kind: progress, curve: linear, results: [healing_effect, damage_effect, poison_effect,
                   caption] } }
```

Each field:

| Field | Meaning | Range |
|---|---|---|
| `colour` | a palette colour by name (core::UiColour, snake_case), or `element`: the ability's element colour | a known name |
| `blend` | `light` adds light (best on dark ground); `ink` paints over (reads on the pale paper) | either |
| `size` | how far past the edge it reaches at zoom 1, in the sprite's texels | above 0, at most 16 |
| `feather` | 0 a hard ring, 1 a soft fade from the edge out | 0 to 1 |
| `opacity` | its strength at full drive | 0 to 1 |
| `zoom_shrink` | 0 grows with the sprite as the camera zooms in; 1 keeps its width on screen | 0 to 1 |
| `drive` | what moves it over time (below) | |

- **Size with zoom:** the reach in texels is `size × (1 / zoom)^zoom_shrink`, clamped to 16: zoomed in it thins,
  zoomed out a screen-steady glow stops at the field's reach. The user asked for the shrink and liked 0.6 on the
  mockup.
- **Colours are palette names, never hex** (my call; the conversation mentioned hex): every glow then follows the
  colour-blind palettes (Accessibility); a new colour is a new palette entry.
- **Defaults are the user's:** the user tuned `sword_aura` in the mockup (light, damage blue, which is the swords'
  element, size 8.5 at zoom 1, feather 0.75, opacity 0.15, pulse) and said "Keep my supplied as the defaults". The
  pulse's period 1.6 and depth 0.9 and the shrink 0.6 were the mockup's values when the user approved it. `bolt_aura`
  copies them as its own entry, so the two can be tuned apart (my call). **The user tuned on the dark ground; a light
  glow at 0.15 may be faint on the arena's paper**: the visual check shows it to the user, who decides; the numbers
  ship as given until then.
- **Outlines ship as today's look:** a hard ring 3 texels wide (`OUTLINE_TEXELS`), not thinning with zoom, ink over
  the world, in the palette's outline colours.

### Drives and curves

A drive turns the inputs the game supplies into the glow's **strength** (0 to 1, multiplying `opacity`) and, for
`progress`, its colour:

- **`steady`:** strength 1.
- **`pulse`** (`period` seconds a beat, at least 1/3; `depth` 0 to 1; `curve`): over each beat the strength falls from
  1 to `1 − depth` and rises back, the curve shaping each half; the phase counts from the start time.
- **`charge`** (`curve`, `fade` seconds above 0, `fade_curve`): while charging, strength is the game's value (0 to 1)
  through `curve`; from the release time it falls to 0 over `fade` through `fade_curve`.
- **`progress`** (`curve`, `results`: palette names): while in progress, strength is the value through `curve`, in the
  entry's `colour`; once the game gives a result `n`, strength 1 in `results[n]`.
- **Curves:** `linear`, `ease_in`, `ease_out`, `smooth` (smoothstep), `sine` (half a cosine): each maps 0..1 to 0..1.

**The game's side:** a sprite draw (`core::SpriteDraw`) names its glow's **role** (none by default) and gives the
drive's inputs where its kind needs them: a value from 0 to 1, a start or release time, a result. The game never
computes a look.

**Roles, not names, in code** (my call, from the builder check): the logic thread never reads `effects.yaml`, so code
can't hold a glow's name. Code knows a closed set of roles, `core::GlowRole` (like `core::UiColour`): `selected`,
`hover`, `fire_bolt`, `flying_swords`, `demo_pulse`, `demo_charge`, `demo_progress`; `effects.yaml` maps each role to
a glow by name (`glow_roles:`, or the ability looks' `glow:` field), and the glows themselves are free-named entries.
Every role must map to an existing glow: a table missing one is refused with its line and the old table stays, so a
reload can never leave code pointing at nothing. A later feature (an NPC's cast, the cauldron) adds its role, a line of
code, and everything else is data.

### Drawing

- **In the sprite's own draw, under the sprite.** A glowing instance's quad grows by its reach; the fragment shader
  reads the field, makes the glow's coverage from the distance (feathered between `size × (1 − feather)` and `size`),
  and composites the sprite over it. A sprite without a glow takes no extra read and its quad doesn't grow.
- **One pipeline does both blends** with premultiplied alpha: ink is colour × a with alpha a; light is colour × a with
  alpha 0, which the normal pipeline's ONE, ONE_MINUS_SRC_ALPHA adds. (A hint; the builder may find better.)
- **Where strength and colour are worked out: my recommendation, left to the builder** (Left to the builder): one C++
  function evaluates a drive (unit-tested) on the main thread, where `effects.yaml` lives and reloads, as instances
  are uploaded: the volley's overlay is already built there each frame, and the glowing world sprites are few. The
  shader then needs only the shape (from a small glow table on the GPU, re-uploaded on reload) and the instance's
  strength and colour. The logic thread never reads `effects.yaml`.
- **The instance grows** by the glow's fields (which glow, strength, colour): the builder keeps `core::Instance` as
  small as it can and records its new size; sprites without a glow pay only those bytes.
- **Draw order:** a glow is drawn with its sprite, so later sprites cover it and it lies over earlier ones, as a glow
  should.

### The selection and hover outlines

- They become the `selected` and `hover` glows, drawn **as today after the world**, so they stay on top: the selected
  or hovered instance is drawn again showing only its glow (no sprite). Its own glow, if any, still draws in the world
  pass, so a sprite can wear two glows at once (its own and the outline).
- The 32-tap ring search in `sprite.frag.hlsl` goes; one field read replaces it.
- `outlines.png` should match within the reference tolerance; if it can't, the difference is looked at, explained and
  the picture updated with the reason.

### The swords and the bolt

- Each sword and the bolt draw **one instance**: the blade wearing `sword_aura`, the bolt `bolt_aura` (both
  `colour: element`). Their `halo` and `core` instances go.
- **Each sword's pulse starts at its own launch** (the user's call): the beat ripples through the rings as they form,
  rather than the volley throbbing as one. The bolt's starts at its cast.
- **The `halo` and `glow_core` sprites stay listed** (my call, from the builder check): `art/sprites.toml`'s place is
  a sprite's stable ID ("never reorder or remove", design doc 0002 A14), so they stay in the list and images, marked
  unused, and leave the scenes in `art/scenes.toml`, so no scene loads them.
- **The health bars stay over the swords** (my call, from the builder check): today the cores and halos go under the
  bars so they don't wash them out, and the blades over the bars. With the glow in the blade's own draw, the swords
  now draw **before** the bars, so neither glows nor blades cover an enemy's health (accessibility first: the bars must
  read).
- `effects.yaml`'s ability looks lose `halo_scale`, `core_scale` and `core_opacity`, gain `glow: <name>`, and
  `halo_intensity`, which also lit the trails, becomes `trail_intensity`. The overlay's room shrinks from three
  instances a sword to one (`core/volley.hpp`).
- The changed pictures (`volley_ring`, `volley_converging`, `volley_plunge`, `bolt_burn`, `arena_halfway`) are looked
  at by eye (look-at-the-game) and updated with the reason.

### The demo

The arena gains three things wearing `demo_pulse`, `demo_charge` and `demo_progress` (existing props, such as the
stele and shrine, or new placements). Their inputs come from the tick counter: a charge that rises over 1.8 s, holds,
releases and repeats; a progress that fills over 3 s, shows a result colour for 2 s, and moves to the next result.
Deterministic, presentation only.

## Accessibility

- **Reduce flashing** (chunk 11's setting; this chunk adds the hooks): it holds pulses at their beat's average
  strength and scales `light` glows' opacity down, as it already will the halo's intensity. **Steady ink glows, the
  selection and hover outlines among them, are untouched** (my call, from the builder check): they don't flash, and
  dimming them would hurt the readability the setting serves. The hook is a renderer-side input with today's look as
  its default (no `settings.yaml` field yet: chunk 11 adds the setting and wires it), covered by a test.
- **No flashing faster than three times a second:** `pulse.period` below 1/3 s is refused with its line.
- **Colour-blind palettes:** glow colours are palette names or `element`, so each palette recolours every glow.
- **Text size and input:** nothing changes; there are no new options.

## Decisions

The user's, in their words where given (2026-11-12):

1. **A generic glow attachable to anything, not a new sprite:** "The glows could be data driven to decide the colour,
   opacity, feathering, size".
2. **It follows the object's edge:** "A glow around the edge of the sword (objects)".
3. **The data decides what glows and when:** "anything, the data decides what glows and when. The focus will be on
   weapons and projectiles, but possibly also NPCs, player character, seals".
4. **Glows change over time, driven by the game:** the charging sword, the NPC cast and the cauldron (Intent).
5. **Near-zero overhead, with GPU simulation in mind:** "We need to ensure it adds very little/no overhead to our render
   ms target, possibly again looking at gpu simulation in the future".
6. **The glow thins as the camera zooms in:** "It would be nice to make the size decrease dynamically as the camera
   zooms in, if possible".
7. **The drive is a data choice, with its speed and curve configurable:** "I would just like the driven by option to
   be one of the data choices we can make, with the speed/curve being configurable".
8. **The user's tuned values are the defaults:** "Keep my supplied as the defaults though".
9. **The selection and hover outlines move onto glows now:** "move them onto the glow system now".
10. **Each sword's pulse starts at its own launch time**, not the volley's cast or a shared clock (asked after the
    builder check: "Its own launch time").
11. **Order:** after the dev-loop speed-ups, before the seals, whose spec and plan are revised to reuse these distance
    fields.

## Calls made here (not the user's; check them)

- **Distance fields made at load in C++, not in `project_n atlas`** (the conversation said the atlas step): the atlas
  step writes no pixels and `project_n.py` is standard library only.
- **Signed fields**, for the seals.
- **The gutter grows from 16 to 32 texels**, so each sprite's field reaches 16 texels clear of its neighbours.
- **Colours by palette name only, no hex**, so every glow follows the colour-blind palettes.
- **The bolt gets its own entry**, a copy of the swords' values.
- **The halo and core go entirely** (the user tuned the glow without them).
- **The outlines are drawn after the world, as today**, as a second, glow-only draw of the instance.
- **Five named curves**; pulses at least 1/3 s a beat.
- **Drive evaluation on the main thread in C++** (recommended; left to the builder).
- **Roles in code, names in data** (`core::GlowRole`), every role required in the table.
- **`halo` and `glow_core` stay listed**, unused and out of every scene, keeping the stable IDs.
- **The health bars over the swords**: the swords now draw before the bars.
- **Reduce flashing leaves steady ink glows alone**, the outlines among them.

## Testing

Small, representative tests (about 20 samples each, never sweeps), test-first:

- **Distance transform:** a single texel, a square, a diagonal edge, a hole, an empty and a full sprite, the clamp at
  ±16 texels, against hand-worked distances.
- **Drives and curves:** each curve at its ends and middle; a pulse's strength at a beat's start, quarter, half; a
  charge rising, held, and fading to 0 after `fade`; a progress before and after its result; reduce flashing's hold.
- **The data:** the shipped table loads and equals `core::Effects{}`; each out-of-range field and unknown name or
  colour is refused with its line; a reload swaps the table.
- **The atlas:** `project_n atlas`'s packer keeps 32 texels between sprites and from the page's edges (selftest).
- **Reference pictures:** the outlines; the volley's three; the bolt; the arena; a new one of the three demo glows at
  fixed times (a pulse mid-beat, a charge fading, a progress showing a result), at zoom 1 and zoom 2 to show the
  shrink.
- **Fuzzing:** the effects loader's fuzz target, if it has one, takes the new table.

## Left to the builder

- How fields are laid out and uploaded beside the colour pages, and their mips.
- The exact instance layout and the GPU glow table's.
- Where drives are evaluated (recommended above), and how a start time keeps its precision over a long session.
- How the shader's pipelines and batches change.
- Which props carry the demo glows.

## Outcome

To be written when it is built: what was built, the user's decisions while building it, rulings, the before-and-after
measurements (frame time at the volley's peak at both zooms, `SubmitFrame`, the field array's memory, the scene's load
time), and notes for the seals chunk.
