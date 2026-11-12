# Phase 6: data-driven glows

Date: 2026-11-12 · Status: **implemented 2026-11-12** (approved 2026-11-12; see Outcome) · Phase spec: `2026-11-10-phase6-design.md` · Comes
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

Implemented on `worktree-phase6-glow`, 2026-11-12, by the plan `docs/superpowers/plans/2026-11-12-phase6-glow.md`
(Tasks 1 to 11, the early ones in parallel waves). This records what was built, the user's decisions while building it,
every ruling of the plan's ledger, the values chosen, the measurements, and what the seals chunk needs from it. Commits
are named by subject: the rebase merge rewrites every hash. Each task's review fixes were folded into its commit; builds
are verified at each task's head (`test`, `lint`), `check` at the branch's.

### What was built

- **Prep and Task 1, the atlas's room:** "name the distance fields' reach" (`core::FIELD_REACH_TEXELS`, 16); "give the
  atlas an explicit 32-texel gutter" (`ATLAS_GUTTER`, `gutter:` in the manifest, `core::Manifest::Edge()` = 16,
  `gfx::BlockOf(sprite, edge)`; the manifest's reader refuses an odd gutter or one under twice the reach); "seed the
  dissolve from the sprite, not its place on the atlas" (ruling 9).
- **Task 2, the distance transform:** "compute a sprite's signed distance field" (`gfx::DistanceField`,
  `gfx::EncodeDistance`; Felzenszwalb and Huttenlocher's exact transform, linear in texels, run for the inside and the
  outside, alpha at least 128 inside).
- **Task 3, the field array:** "keep every sprite's distance field beside the atlas" (`SpriteArray::field` and
  `fieldSampler`; `DecodeSprite` computes the field on the decode jobs; `UploadSprite` writes both blocks in one copy pass;
  `gfx::FieldBlockOf`; the debug line per scene load).
- **Task 4, the glow data:** "add data-driven glows with their drives" (`core::GlowRole`, `GlowLook`, `GlowDrive`,
  `CurveAt`, `GlowStrength`, `Effects::glows`, `glows:` and `glow_roles:` in `effects.yaml` with their refusals, the
  shipped table, a fuzz seed).
- **Task 5, the instance and the table:** "carry glow inputs on sprite instances" (`Instance::glow`, `glowValue`,
  `glowStart`; `gfx::GlowRecord`, `BuildGlowTable`); its review's tests of the glow words and table.
- **Task 6, the demo:** "show every glow drive in the arena" (`game::DemoGlow`: the shrine pulses, the stele charges, the
  crystal shows progress and results, all from the tick counter, none in the state hash).
- **Task 7, drawing glows:** "draw glows from sprites' distance fields" (the vertex stage works out a glow's reach, tint
  and opacity per instance, the fragment stage reads one field value per pixel; the grown quad; three `glows_*` pictures);
  its review's "share the sprite shader's dissolve and pin its constants".
- **Task 8, drives over time:** "drive glows over time" (`GlowStrength` mirrored in HLSL line for line, the render clock as
  `gfx::GlowTimeAt`'s low 32 bits and fraction, reduce flashing's hook; reference cases measure each drive against
  `core::GlowStrength`).
- **Task 9, the outlines:** "draw the selection and hover outlines as glows" (the `selected` and `hover` glows drawn alone
  after the world; the 32-tap ring search and `OUTLINE_TEXELS` are gone).
- **Task 10, the swords and the bolt:** "give the swords and the bolt edge glows" (one instance a sword, `halo` and `core`
  gone from the overlay and the scenes, the swords drawn before the health bars, `trail_intensity`).
- **Task 11, this record,** the stale design docs' notes (0002 and 0002.3.1 point here) and the profiling gotcha.

### The user's decisions while building it

- **Parallel waves** (2026-11-12, "Yes" to running the plan's waves in parallel): independent tasks are planned and run as
  parallel waves, each in its own worktree. Recorded as an AGENTS.md rule ("plan independent tasks as parallel waves").
- **Each sword's pulse starts at its own launch** (asked after the builder check, decision 10): the beat ripples through
  the rings as they form. The bolt's starts at its cast.
- **The swords keep the user's look on the paper** (below): the user tuned on the dark ground; on the paper the light glow
  at 0.15 is nearly invisible. Shown the captures and four options, the user kept their numbers ("A").

### Rulings

Each is the ledger's, with its reason and what it costs if wrong.

1. **`EncodeDistance(1.5)` is 139, not 140.** The encoding formula in the plan's constraints is binding and round(255 ×
   0.546875) = round(139.45) = 139; the brief's 140 was an arithmetic slip. Wrong: one test literal.
2. **The long-session check drives the glow tick near 2^32** through the arena's tick counters if it accepts the jump,
   otherwise through the renderer's glow tick with a synthetic snapshot whose start ticks are near 2^32. Task 8 took the
   synthetic way (the arena published at simulation ticks 2^32 − 10 and 2^32 + 38). Wrong: the check covers less of the
   wraparound's path.
3. **Parallel implementers in waves A and B**, against superpowers' "never in parallel": the user chose it, and separate
   worktrees remove the conflicts the rule guards against. Wrong: a rebase conflict to resolve by hand.
4. **Every review finding is fixed, Minor included** (the user's rule), over the skill's "Minor → deferred". Costs a few
   fix rounds.
5. **Review models:** full tasks on Opus, fix re-reviews on Sonnet (Haiku for comment-only), light reviews on Sonnet, the
   final review on the strongest model. Wrong: cost only.
6. **One fix pass per review**, re-reviewed on the fix diff alone, over the skill's five rounds; a finding left open after
   it is adjudicated at the breaker. Wrong: a finding parked earlier than the skill would.
7. **The parallel-waves rule is an AGENTS.md rule and a memory** (the user's, 2026-11-12), and this plan already ran that
   way.
8. **The plan's commit scope `render` is not in `SCOPES`:** tasks use the module they change (`gfx`, `core`, `data`,
   `game`, `tools`). Wrong: a reworded commit.
9. **The dissolve hashes the sprite's own UV (`uv − region.min`), not the page UV.** Growing the gutter re-packed the atlas
   and reshuffled the block noise; the brief's "pixels don't move" didn't foresee a hash of the page UV. The pattern no
   longer depends on atlas placement; `fox_dissolving` and `volley_plunge` were updated once, after a look at the diff.
   Wrong: two pictures and one shader line.
10. **`core::Instance` is 56 bytes, not the plan's 52** (a reserved `u32`). DXC's SPIR-V gives a `StructuredBuffer` of
    structs holding a `float2` a stride rounded to 8; at 52 the CPU and GPU strides disagreed and 22 of 25 pictures lost
    their sprites. Wrong: 4 bytes an instance (400 KB at 100,000), measured in the stress profile below.
11. **Task 7 updated 20 existing arena pictures** (arena_halfway, fox_dissolving, volley_ring, volley_converging,
    volley_plunge, bolt_burn, lance_burst, hud_zoom_one, outlines, text_zoom_*, labels_crowd, numbers_*, recoloured,
    recoloured_bare): Task 6 put demo glows on the shrine, stele and crystal, which those pictures show. A cluster diff
    of each found changes only at those props, and a run with the old fragment shader and the new vertex shader passed
    every old case, so the glow-free path is unchanged. Wrong: pictures to re-check.
12. **A glow's per-instance maths (reach, colour, opacity, blend, and Task 8's drives) runs in the vertex stage;** the
    fragment stage only reads the field. Cheaper per pixel and still stateless: nothing is remembered between frames.
    Wrong: a shader move.
13. **The glows pictures render at tick 492, not 24:** at tick 24 the stele and crystal were at 5% and 13% of their
    strength and the zoom pictures showed almost nothing. Wrong: one constant and three pictures.
14. **A glow's age is an `f32` of a wrapped 32-bit tick difference.** Whole ticks stay exact for 2^24 ticks (about three
    days) after a glow's start; only a pulse started days earlier (the demo's, at tick 0) steps coarser after that. It is
    presentation only. Wrong: a pulse's phase wraps once.
15. **Tasks 9 and 10 ran in parallel:** they share no source file, only `src/AGENTS.md`'s rendering line, merged by hand.
    Wrong: a rebase conflict (Task 10's report found one, in that line, and kept both wordings).
16. **Withdrawn, then measured.** The controller first ruled zoom 0.25 needn't be re-recorded ("the same work on fewer
    screen pixels"); the final review showed the reasoning wrong (the reach clamps at 16 texels, so a sword's quad grows
    about 7.5 times at zoom 0.25 against 2.7 at zoom 2) and the spec asks for both zooms. A back-to-back A/B at zoom 0.25
    followed (below): no regression.

Smaller calls from the tasks' reports, each in its commit: the field's offset in the shared transfer buffer is rounded to
512 bytes (D3D12's placement alignment); a glow's fade ends at its reach, not at the reach plus a pixel, since the quad
is cut there and at a clamped reach of 16 the field reads exactly 16 past its edge; `texelUv` joins the draw uniform (the
vertex stage binds no texture to ask the page's size); ink over a sprite's partly transparent edge is weighted by one
minus the sprite's alpha, so no gap shows between a sprite and its outline; a role's glow in `effects.yaml` is found
through `glow_roles:` and the ability looks have no `glow:` key (the spec's wording said they would); `effects.yaml`'s
palette colours are `area.key` names (`selection.selected`), not the spec's `selected_outline`.

### Values chosen

- **The shipped glows** (`assets/data/effects.yaml`, mirrored by `core::Effects{}`): `sword_aura` and `bolt_aura` are the
  user's tuned values (light, `element`, size 8.5, feather 0.75, opacity 0.15, zoom shrink 0.6, pulse 1.6 s, depth 0.9,
  sine); `selected` and `hover` are ink, size 3, feather 0, opacity 1, no shrink, steady, in the palette's outline colours;
  `demo_pulse` (ink, `effects.damage`, 8.5, 0.75, 0.6), `demo_charge` (ink, `effects.burn`, 10, 0.8, 0.7, ease in, fade
  0.4 s ease out) and `demo_progress` (ink, `names_and_captions.caption`, 10, 0.8, 0.7, linear, results healing, damage,
  poison, burn).
- **The demo's clock** (`game::DemoGlow`): the charge loops every 192 ticks, rising over 108, held to 144, released at 144,
  fading over 24; the progress fills over 3 s and shows each result for 2 s, in order.
- **Limits:** reach at most 16 texels; pulse period at least 1/3 s; reduced flashing holds a pulse at its beat's average,
  `1 − depth × (1 − mean)` with the curve's mean over 0..1 (linear, smooth and sine 1/2, ease in 1/3, ease out 2/3), and
  halves light glows' opacity (`REDUCED_LIGHT_OPACITY` 0.5), leaving ink glows alone.
- **Sizes:** `core::Instance` 56 bytes (`INSTANCE_BYTES`), `gfx::GlowRecord` 96 bytes (eight roles, a 768-byte table), the
  draw uniform 80 bytes, the atlas gutter 32 texels.
- **Pictures changed:** `volley_ring`, `volley_converging`, `volley_plunge`, `bolt_burn` (the halos and cores are gone, so
  the volley lost its blue wash on the paper); `outlines`, `recoloured`, `recoloured_bare` (a crisper, slightly rounder
  3-texel ring); `fox_dissolving`, `volley_plunge` (the dissolve's hash); the 20 of ruling 11; new: `glows_zoom_one`,
  `glows_zoom_two`, `glows_zoom_quarter`. `arena_halfway` changed only through ruling 11 (it has no volley).

### Measurements

Releasedbg, native preset (linux-clang), the arena, 1280×720, `pixi run project_n profile`; the host and settings are the
earlier chunk's (RTX 4090, Wayland, mailbox). Read as the chunk 4 spec did: `tracy-csvexport -u`; a frame is one iteration
of `App::Run`'s loop (the `Run` zone on thread 1); median / 99th percentile (nearest rank) / worst; the first two seconds
skipped. A volley counts as drawn for 4.0 s from its first `DrawVolleys` call over 10 us after 0.2 s without one, its
peak 1.5 to 2.0 s after; five complete volleys in every swords capture, every 6.0 s.

**Baseline** (before the glows; `--cast-swords`, 30 s each, and `--stress`, 20 s):

| Main thread, one loop iteration (median / 99th / worst, ms) | Closest zoom (2) | Farthest zoom (0.25) |
|---|---|---|
| Without volleys | 0.132 / 1.13 / 2.45 | 0.119 / 1.10 / 2.50 |
| With a volley drawn | 0.513 / 1.48 / 2.53 | 0.476 / 1.44 / 2.38 |
| At a volley's peak | 0.570 / 1.59 / 2.40 | 0.519 / 1.49 / 2.17 |
| `DrawVolleys` at the peak, us | 296 / 435 / 607 | 294 / 424 / 646 |
| `SubmitFrame` at the peak, us | 164 / 1,160 / 2,015 | 122 / 1,052 / 1,774 |

`--stress` (100,000 instances): the loop 0.354 / 1.37 / 2.77 ms, `SubmitFrame` 155 / 1,146 / 2,237 us.

**An invalid first "after".** The first captures of the glow head showed frames four to eight times slower than the
baseline. They are void: the brainstorm companion's browser tab was animating a glow canvas, which contended for the GPU.
With the page parked, a back-to-back A/B settled it, so only that A/B is the comparison (a Gotcha in `src/AGENTS.md`
records the cause).

**A/B, back to back** (the window left alone, the browser parked; before = the commit before the glows, built in its own
worktree; after = the glow head; `--cast-swords --zoom 2`, 30 s, and `--stress`, 20 s; the numbers in brackets are frames
or calls):

| Zoom 2, main thread, one loop iteration (median / 99th / worst, ms) | Before | After | Difference (median; 99th; worst) |
|---|---|---|---|
| Without volleys | 0.121 / 0.604 / 2.16 (60,332) | 0.122 / 0.637 / 2.16 (59,423) | +0.001 (+1%); +0.033 (+5%); 0.00 |
| With a volley drawn | 0.503 / 1.46 / 2.34 (39,600) | 0.463 / 1.48 / 2.36 (42,934) | -0.040 (-8%); +0.02 (+1%); +0.02 (+1%) |
| At a volley's peak | 0.558 / 1.52 / 2.09 (4,083) | 0.499 / 1.47 / 2.06 (4,532) | -0.059 (-11%); -0.05 (-3%); -0.03 (-1%) |

| Zoom 2, per call (median / 99th / worst, us) | Before | After | Difference |
|---|---|---|---|
| `DrawVolleys` at the peak | 302 / 473 / 660 | 280 / 407 / 577 | -22 (-7%); -66 (-14%); -83 (-13%) |
| `SubmitFrame` at the peak | 151 / 1,110 / 1,684 | 123 / 1,096 / 1,671 | -28 (-19%); -14 (-1%); -13 (-1%) |
| `SubmitFrame` without volleys | 80 / 337 / 2,112 | 80 / 384 / 2,115 | 0; +47 (+14%); +3 |

| `--stress` (median / 99th / worst) | Before | After | Difference |
|---|---|---|---|
| Main thread, one loop iteration, ms | 0.301 / 1.28 / 2.27 (52,558) | 0.302 / 1.30 / 2.56 (52,465) | +0.001 (0%); +0.02 (+2%); +0.29 |
| `SubmitFrame`, us | 143 / 1,127 / 2,168 | 147 / 1,146 / 2,259 | +4 (+3%); +19 (+2%); +91 (+4%) |

**A/B at zoom 0.25** (the same way, after the final review; before = the commit before the glows, after = the final head;
`--cast-swords --zoom 0.25`, 30 s each; five complete volleys each, no pause):

| Zoom 0.25, main thread, one loop iteration (median / 99th / worst, ms) | Before | After | Difference (median; 99th; worst) |
|---|---|---|---|
| Without volleys | 0.119 / 1.02 / 2.40 (57,349) | 0.118 / 1.00 / 2.76 (57,635) | -0.001 (-1%); -0.02 (-2%); +0.36 (+15%) |
| With a volley drawn | 0.483 / 1.49 / 2.99 (39,849) | 0.459 / 1.44 / 3.10 (42,060) | -0.024 (-5%); -0.05 (-3%); +0.11 (+4%) |
| At a volley's peak | 0.536 / 1.58 / 2.27 (4,182) | 0.505 / 1.44 / 2.06 (4,432) | -0.031 (-6%); -0.14 (-9%); -0.21 (-9%) |

| Zoom 0.25, per call (median / 99th / worst, us) | Before | After | Difference |
|---|---|---|---|
| `DrawVolleys` at the peak | 300 / 450 / 587 | 282 / 418 / 636 | -18 (-6%); -32 (-7%); +49 (+8%) |
| `SubmitFrame` at the peak | 124 / 1,129 / 1,856 | 118 / 1,032 / 1,616 | -6 (-5%); -97 (-9%); -240 (-13%) |
| `SubmitFrame` without volleys | 77 / 766 / 2,321 | 77 / 763 / 2,666 | 0 (0%); -3 (0%); +345 (+15%) |

**Noise:** the pre-glow build against the baseline taken
earlier differs by 0.011 ms (8%) in the no-volley median and 0.012 ms (2%) at the peak; the 99th of frames without
volleys swings between 0.60 and 1.13 ms across runs of one build, so that column and the worst frames (one outlier each)
are noise. **Verdict:** the glow head is not slower than the build before it. The medians at the peak (-11% at zoom 2, -6% at
zoom 0.25), with a volley (-8%, -5%), without (+1%, -1%) and in the stress scene (+0.3%) are within the noise; the volley is a hair faster because a
sword is one instance instead of three (`DrawVolleys` -7%, despite the larger instances). The anti-goal ("a frame that
gets slower") holds.

**Memory and load:**

- **The field array:** 5,586,944 bytes on the GPU for the arena's atlas (one 2048² page, five mips of R8), logged at debug
  level at each scene load.
- **Computing the fields:** 61 ms of CPU summed over the decode jobs for the stress scene's 21 sprites in releasedbg
  (60.7 ms), on the job system, not the main thread; the wall time of a scene load was not measured apart from that. A
  debug build took 613 to 967 ms for the arena's 27 sprites.
- **`core::Instance`, 40 → 56 bytes:** `glow` (role, result, element, released), `glowValue` (two unorm16, the previous and
  current value) and `glowStart` (the start tick's low 32 bits) are 12 bytes, 52 in all, and a reserved `u32` makes 56
  for the shader's 8-aligned stride (ruling 10). A snapshot's instance array at 100,000 instances grows from 4.0 MB to
  5.6 MB; the stress frame did not slow (0.301 against 0.302 ms), and the sprites without a glow pay only those bytes.

### Notes for the seals chunk

- **The field array** is an R8 unorm 2D texture array beside the colour atlas, with the same layers, size and mips:
  `gfx::SpriteArray::field`, sampled through `fieldSampler` (linear, trilinear, clamped). It is cleared to 255 (far
  outside) and filled as each sprite loads.
- **Encoding:** `byte = round(255 × clamp(0.5 + d / 32, 0, 1))`, **signed**: negative inside, positive outside, so one half
  (about 128) is the edge; the reach is 16 texels each way (`core::FIELD_REACH_TEXELS`). The edge is where alpha crosses
  one half (alpha at least 128 is inside). Decode with `d = (sample − 0.5) × 32`, as the shader does. The antialiased
  inside for runes and emblems is the negative half.
- **Where each field is:** a sprite's field is a block of (width + 32) × (height + 32) texels at `gfx::FieldBlockOf(sprite)`
  (its region grown by 16 on every side); the colour block is `BlockOf(sprite, manifest.Edge())`, the same rectangle at
  today's gutter. The 32-texel gutter keeps a neighbour's field out of that block. The field's mips are generated with the
  colour's (`GenerateMips`).
- **Shader bindings** (SDL_GPU's order, design doc 0002 §8.8): the sprite vertex stage has the instances at `t0 space0`, the
  glow table at `t1 space0` and the `Draw` uniform at `b0 space1` (80 bytes); the fragment stage has the colour atlas at
  `t0/s0 space2` (`[[vk::binding(0, 2)]]`) and the field at `t1/s1 space2` (`[[vk::binding(1, 2)]]`), both combined image
  samplers.
- **A glow's age** is an `f32` of a wrapped 32-bit tick difference: sub-tick precise for about 2^20 ticks (about 5 hours)
  after its start and whole-tick for 2^24 (about 77 hours), so a persistent glow should pass a recent start tick (or a
  pulse's phase could move to integer ticks modulo its period). The demo pulse, started at tick 0, steps in a very long
  session.
- **Reuse:** a glow needs only a role and a `glows:` entry. The fragment path reads one field value per pixel, only for a
  glowing instance (a flat per-instance branch).
- **To remember:** the dissolve now hashes the sprite's own UV; the swords draw before the health bars; the field's mip
  sampling when zoomed out looked clean in `glows_zoom_quarter` but is not argued, and D3D12 and Metal were checked only by
  the shaders' reflection, not by a picture (CI's pictures are software Vulkan).

### The swords' look on the paper (decided)

**The user kept A** (2026-11-13, "A", after the options below were explained): the swords and the bolt ship the user's
tuned light glow at 0.15, knowing it is nearly invisible on the arena's paper. The visual check the spec asked for: The user tuned the glow on the dark ground. On
the arena's pale paper the light glow at 0.15 is close to invisible: against a no-glow frame at zoom 1, the median change
is 7/255 and only 3,600 pixels change by more than 32/255; at zoom 2, 2,400. What reads is the steel blade alone, and the
volley lost its blue identity (the old tinted cores existed for the paper). The bolt's glow is likewise faint. Captures
and a comparison page were prepared for the user. The options:

- **A. Yours, unchanged:** light, opacity 0.15, right for dark scenes, nearly invisible on the paper (what ships).
- **B. Ink, same shape:** the user's size, feather and pulse, painted as ink at opacity 0.45, a blue wash that reads on the
  paper.
- **C. Light, much stronger:** light at opacity 0.8, still faint on the paper, bright at night.
- **D. Ink wash under a light rim:** two glows, a soft ink wash for the paper and a light rim for the shine; this needs a
  second glow per sprite, a later change.

A, B and C are a data change in `effects.yaml` (`sword_aura`, `bolt_aura`) and a refresh of the four volley pictures; D
is new code. Any of them stays a later tuning of `effects.yaml`, live in debug builds.
