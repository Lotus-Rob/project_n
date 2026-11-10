# Phase 6, chunk 3: game text and damage numbers

Date: 2026-11-10 · Status: approved 2026-11-10 · Phase spec: `2026-11-10-phase6-design.md` · Replaces:
`docs/design/0002.4.2-text-and-mesh-path.md` (on the `phase6-text-chunk3` branch, kept as history; what it decided and this spec keeps is marked below)

## In short

- **Labels** name creatures and NPCs in the player's language. They **scale with the zoom**, stay **crisp and smooth at
  every size** (distance-field glyphs), and **never pollute the screen**: below a readable size they fade out,
  overlapping labels of the same name stack as "Cultivator (+11)", and the more important label wins the rest.
- **Damage numbers** show **every hit** by default, coloured by kind (damage, healing, poison, burn, …), bouncing or
  rising as they fade, readable at every zoom, and can be **combined into one total per ability use** by a setting.
- **The arena proves it:** the crystal's aura (damage and healing) and a debug burst of thousands of hits; chunk 4's
  real flying swords and elemental attacks then become the numbers' real source.
- **Already built:** the strings table and language setting; the mesh path. **Half built:** the glyph cache and
  layout (uncommitted). This spec re-specifies the rest.

## Intent

Text is how the demo world tells you what you're looking at: who a creature is, what just hit it. It must read
cleanly at every zoom, never crowd the screen, and never cost smoothness. The user: **"Text rendering must remain crisp
and smooth at all zoom levels, this is very important to me."**

## Goals

1. **Distance-field text:** one glyph cache of signed-distance-field glyphs (SDL3_ttf's `TTF_SetFontSDF`), drawn through
   the mesh path with edges kept sharp at every scale and sub-pixel positions kept smooth.
2. **Labels that scale with the zoom**, anchored over their creature, in the player's chosen language only, fading out
   below a minimum readable size.
3. **No label pollution:** labels that would overlap on screen and show the same name **stack** into one, "Name (+N)",
   over the creature nearest the group's middle; overlapping labels with different names give way by **priority**:
   selected, hovered, the player, named creatures, then the rest; labels **fade** in and out over a few frames, never pop.
4. **Damage numbers:**
   - **every hit shown** by default, however many (1,000 swords from one ability are 1,000 numbers), drawn efficiently;
   - **coloured by kind** (damage, healing, poison, burn; the table grows with the game);
   - **scaled with the zoom down to a minimum readable size**; once creatures are too small to see, their numbers rise
     from their label or stack label instead;
   - **motion:** bounce and fade, or rise and fade (a setting).
5. **Settings** (in `settings.yaml`; chunk 11's settings screen adds their menu):
   - `combined_damage_numbers`: one total per ability use instead of every hit, for all abilities (default off);
   - `damage_number_motion`: `bounce` (default) or `rise`;
   - `text_outline` and `text_shadow`: a dark outline and a soft shadow on game text (both default on).
6. **The arena shows it:** name labels on the cultivator and the creatures; the stone tablet's vertical caption; the
   crystal's aura hitting creatures and healing the player; and a **debug burst** key that emits thousands of hits on
   the foxes at once (events only: no projectiles, no state), cycling through the kinds, so numbers, colours, both
   motions and the combined setting can be seen under load before chunk 4's real abilities exist.

## Non-goals

- UI text: it has its own exact-size path, not distance fields (the user's call; chunk 8). The settings screen
  (chunk 11), unit frames (later).
- Real abilities, health and projectiles (chunk 4: flying swords, elemental attacks).
- NPC names for thousands of movers, and labels at the full world's scale (chunk 5, which measures what this builds).
- Both languages in one label; text input; right-to-left text.
- Text sorted into the world by its foot: all game text stays in one overhead layer after the world (kept from 0002.4.2,
  decision 2).

## Anti-goals (a failure even if it works)

- **Blur or shimmer** of text at any zoom, or while it moves.
- **Jitter:** labels stepping or wobbling against the creature they follow, at any zoom or camera speed.
- **A hitch when new glyphs appear** (a page of Chinese for the first time, a zoom bringing many labels in).
- **Label pollution:** a wall of overlapping text when zoomed out or crowded.
- **Popping:** labels or stacks appearing and vanishing from one frame to the next.
- **Damage numbers lost** under load (no silent cap), or costing the frame budget.

## Already built (on `phase6-text-chunk3`)

- `feat(data): read the strings table and the language setting`: `assets/data/strings.yaml` (English and Simplified
  Chinese per string ID), `core::StringId`, `data::ParseStrings`, `language: en | zh` in `settings.yaml`. Kept as is.
- `feat(gfx): add the mesh path`: indexed, textured, coloured quads written each frame and drawn in the world pass.
  Kept; its limits are revisited below.
- **Uncommitted:** a `ShelfPacker` and its tests, the start of text layout. The packer and page model carry over; what a
  page stores becomes distance fields.

## Design

### The glyph cache: distance fields

- Fonts open with `TTF_SetFontSDF(font, true)`: SDL3_ttf rasterises each glyph as a signed distance field. One field per
  glyph serves every size: it's rasterised at a **base size** (large enough for clean Chinese strokes) and scaled.
- Pages stay R8, 2048², shelf-packed, grown by page, uploaded in the frame's own copy pass, as the uncommitted code has
  them; the padding grows to the field's spread.
- **No hitch from new glyphs:** at start-up the cache pre-rasterises every glyph the strings table uses in both
  languages, plus the digits and signs; a glyph seen for the first time later is rasterised **on a worker thread** and
  drawn from the frame its upload lands (the phase rule: work on workers wherever possible).
- **A missing glyph** draws nothing and is logged once per character, at warning level (kept).

### Drawing: crisp at every size

- The mesh fragment shader turns the field into coverage with a smoothstep whose width comes from the screen-space
  derivative, so an edge stays about one pixel wide at every scale; glyph quads keep sub-pixel positions (no snapping),
  so moving text glides.
- **Outline and shadow** come from the same field: the outline a second threshold, the shadow a second sample offset
  and softened, each on or off by setting, drawn in one pass.
- **Size with the zoom:** each kind of text has a size in world units; on screen that is the size times the zoom.
  Below a **minimum readable size** labels fade out; damage numbers stop shrinking at it instead.

### Labels: placement, stacking and priority

- The logic publishes what to label (`core::TextItem`, kept from 0002.4.2: a string ID or number, an anchor at the
  previous and this tick, an offset, a font kind, colour, alignment, direction) plus each item's **name key** (which
  labels may stack) and **priority**.
- **Each frame on the main thread:** project every label to the screen, then resolve overlaps in priority order through
  a screen-space grid: an overlapping label with the same name key joins the stack of the one already placed (its count
  grows, its anchor moves to the creature nearest the group's middle); one with a different key is hidden.
- **Fading:** each label's opacity eases toward shown or hidden over a few frames, with a little hysteresis, so a label
  on the edge of overlapping doesn't flicker.

### Damage numbers

- **Events:** `core::Event` gains a **kind** (damage, healing, poison, burn, …) and the **ability use** it belongs to (an
  ID per use of an ability; the aura's tick is one use), beside its value and position. Output only: saves and replays
  don't change.
- **Every hit plays:** App keeps live numbers in a pool sized for thousands (no cap that drops hits); each becomes text
  quads through the same cache and mesh path.
- **Combined:** with the setting on, hits of one ability use add into one number per creature that updates as hits
  arrive, in that ability's colour (every attack deals one kind, the user's design), and plays out when the use ends.
  An event marks the end of a use, so the rule needs no timer; the debug burst and the aura's tick are uses that end on
  the tick they start.
- **Motion:** bounce (a short hop and settle, then fade) or rise (up and fade), over about a second of render-clock time.
- **Zoomed out:** numbers keep the minimum readable size; when the zoom puts creatures below a visible size, a number
  starts from where its creature's label, or its stack's label, is placed, **even when that label is faded out**: the
  label pass places and stacks every label, and only draws those above the readable size. The rule is built and tested
  here at any zoom; the arena's zoom range may not reach it (a fox is still about 28 px at 0.25), chunk 5's world does.
- **Colours** per kind in a small table (data later, when the game has more kinds).

### Settings

`settings.yaml` gains `combined_damage_numbers`, `damage_number_motion`, `text_outline`, `text_shadow`, read by the
`data` module with the existing language setting: an unknown value is logged and the default used.

### Limits

The mesh path, the label pass and the number pool are sized for the demo world's load (thousands of labels and
numbers on screen at once), not the one-screen arena: the builder sets the numbers from a measurement and records
them; nothing is dropped silently, and anything over a limit is reported as instances are (`overflows`).

## Decisions

The user's, 2026-11-10:

1. **Text scales with the zoom**, through **distance-field glyphs**, crisp and smooth at every size; labels fade out
   below a readable size. *(Reverses 0002.4.2 decision 3, "text keeps its size on screen".)*
2. **Stacking and priority:** same-name overlaps stack as "Name (+N)" over the creature nearest the middle; otherwise
   selected > hovered > player > named creatures > the rest; changes fade, never pop.
3. **Every damage number shown** by default, efficiently, coloured by kind; a general setting combines them into one
   total per ability use.
4. **Number motion is a setting:** bounce and fade, or rise and fade.
5. **Zoomed out, numbers stay readable:** scaled down to a minimum size, then rising from the creature's label or stack.
6. **Dark outline and soft shadow** on game text, each a game setting.
7. **Labels in the player's chosen language only.**
8. **The test bed:** the aura (damage, and healing the player) plus a debug burst for load; **real flying swords and
   single-element attacks are their own chunk 4** (the swords deal plain damage only).
9. **Game text and UI text are separate:** distance fields here; UI text gets an exact-size path in chunk 8.

Kept from 0002.4.2 (accepted 2026-11-10): numbers are text through the glyph cache (no digit strip, no
`core::Batch::texture`); all game text in one overhead layer; string IDs as a hand-written enum the table must cover in
both languages; `Simulation::Tick` emits events Logic stamps and publishes.

## Calls made here (not the user's; check them)

- A number whose label has faded rises from where the hidden label is placed (the label pass places every label).
- **Label colour:** light text with the dark outline and shadow; with both turned off, labels switch to dark ink so they
  read on the pale ground.
- **"Named" creatures** (the priority order) are those whose data entry gives them their own name: in the arena the
  player and any creature given one; chunk 5 brings named NPCs.
- The uncommitted `strings.yaml` change that mixes both languages in one label, and pixel label offsets, give way to
  this spec (chosen language only, offsets in world units, scaling with the zoom).

- Defaults: combined numbers off, motion bounce, outline and shadow on.
- New glyphs are rasterised on a worker thread after a start-up pre-warm of the strings table and digits.
- The aura's healing of the player is how healing appears in the arena.
- Colours per kind live in code until the game has more kinds.

## Testing

- **The cache:** packing with distance-field padding; a glyph rasterised once; pre-warm covers the strings table; a new
  glyph rasterised off the main thread and drawn once uploaded.
- **Crispness:** reference pictures of the arena's labels and caption at several zooms (0.25, 0.5, 1, 2) and at
  sub-pixel camera offsets, compared frame to frame for shimmer; a label following a moving creature drawn at successive
  frames, checked for steps.
- **Stacking and priority:** fixed labels on fixed screen positions give the expected stacks, counts and winners; a
  label on the edge of overlapping doesn't flicker over a camera sweep.
- **Numbers:** every hit of a debug burst of thousands is played (none dropped); combined mode gives one total per
  creature per use, ended by the use's end event; both motions' positions and fades at a render tick; the zoomed-out
  start from a faded label; colours per kind.
- **Settings:** each new key's default and an unknown value.
- **Frame time:** the debug burst at its peak, recorded against the phase bar (chunk 5 builds the full measurement).

## Left to the builder

The base size of the distance fields and their spread; the minimum readable size; the fade and hysteresis timings; the
pool and buffer sizes from measurement; how the label pass's grid is sized; commit split and order.
