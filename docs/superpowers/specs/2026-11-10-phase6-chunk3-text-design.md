# Phase 6, chunk 3: game text and damage numbers

Date: 2026-11-10 · Status: **implemented 2026-11-11** (approved 2026-11-10; see Outcome) · Phase spec: `2026-11-10-phase6-design.md` · Replaces:
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

## Accessibility

Phase decision 17 (accessibility first) and chunk 11's spec, which wires the settings into what this chunk builds. This
chunk builds the hooks, so chunk 11 adds settings without reworking text:

- **Text size:** every size goes through one `gfx::TextOptions::scale` (default 1), which multiplies the world sizes
  *and* the minimum readable sizes; chunk 11's "game text size" sets it. The label and damage-number minimum sizes are
  named constants chunk 11 turns into settings (`LABEL_MIN_PIXELS`, `NUMBER_MIN_PIXELS`).
- **Colour is never the only signal:** healing numbers carry a "+" sign; damage carries none. Poison and burn differ
  only by colour, and appear only in the debug burst, until chunk 11 adds the damage-kind icons it owns. The kind colours
  sit in two tables side by side (`app/damage_numbers.hpp`: `KIND_COLOURS`, and `DARK_KIND_COLOURS` for text without an
  outline or shadow), so chunk 11's colour-blind palettes replace them in one place.
- **Reduce motion:** chunk 11 adds a reduce-motion setting through these hooks. The **bounce** and rise are one pure
  function, `app::NumberMotionAt(NumberMotion, f32 seconds)`, where a still motion goes. The **scatter** (a number's start
  moved off its anchor, `app::ScatterOf`'s offset, `NUMBER_SCATTER_*`) and the **stagger** (its start delayed,
  `app::StaggerWindow` and `ScatterOf`'s delay) are each one function to turn off or narrow. Label fades and cross-fades
  change opacity only, but a **stack's glide moves position**: a stack's label slides to its new creature over
  `LABEL_FADE_SECONDS` (`gfx::LabelPass`'s `Glide`), so reduce motion needs a hook there to move it at once.
- **Reduce flashing:** nothing in this chunk flashes: every label change (coming, going, stacking, hiding) and a caption
  whose glyphs land fade over `LABEL_FADE_SECONDS` (0.15 s), never pop; numbers fade out, never blink.
- **High contrast:** the outline and shadow settings (on by default) keep light text readable on any ground. With both
  off, labels and captions switch to dark ink (`gfx::TEXT_INK`) and damage numbers to a dark variant of their kind's
  colour (`DARK_KIND_COLOURS`), each 4.6:1 or more against the parchment ground (Outcome, Values chosen).
- **Keyboard and gamepad:** the debug burst is a debug key (F7) like F2/F5/F9; labels and numbers need no input, and no
  player-facing control is added.

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

## Outcome

Implemented on `phase6-text-chunk3`, 2026-11-11. This records what was built, every ruling taken while building it, the
measurements behind the chosen values, and what is left open for the user.

### What was built

Commit hashes are after the squash (ruling 5): each task's review fix rounds are folded into its commit.

- **Strings and the mesh path** (before this plan): `559d01b` the strings table and language setting; `72fa922` the mesh
  path.
- **Task 1, settings** (`315b7bd`): `combined_damage_numbers`, `damage_number_motion`,
  `text_outline`, `text_shadow`, each with its default; an unknown value is a warning and the default.
- **Task 2, events** (`d3c6ddd`): the crystal's aura emits hit events (damage, and healing of the player), and
  F7's debug burst emits 4,000 hits; the game's random purposes live in one table.
- **Task 3, the glyph cache** (`a9720c4`): glyphs as distance fields, rasterised on a worker as
  low-priority work, after a start-up pre-warm.
- **Task 4, drawing** (`1ed40cc`): the field shader, text sized by the window's fit, supersampled edges, and
  the shimmer and jitter pictures.
- **Task 5, labels** (`590a97d`): stacking, priority,
  fades and hysteresis.
- **Task 6, damage numbers** (`2cca0a1`, `d3c6ddd`): every hit as text, by kind, in
  bounce or rise motion, with the scatter and stagger below.
- **Task 7, `--burst`** (`eb141b4`): `--burst` sends the debug burst once a second of wall time (`platform::NowNs`), so
  `project_n profile -- --burst` sees it with no key press; `app::HasFlag` reads the flag, so `main.cpp` has no logic of
  its own to test.
- **Task 8, cheap damage numbers** (`db01c39`): a number is composed from its characters laid out alone
  (`gfx::NumberFace`, `ComposeNumber`), with no string and no layout lookup, and placed on the job system in fixed
  chunks of 256 texts (`gfx::PlaceNumbers`), each writing its own slice of the vertices; the per-number profiler zones
  are gone. Drawn pixels are unchanged.
- **The final review's fixes** (`6c4bf6e`, `918c3bb`, `3af50c1`, `4914d5e`, `88574b1`, and this record):
  `core::Jobs::Pending` is done only once enkiTS is (ruling 47); labels fade on a frame clock that runs while the game is
  paused (43); labels never pop, and text still rasterising is held back, then faded in (44); damage numbers turn dark
  with the outline and shadow off (45); combined numbers starting on one target together scatter (46); the per-label
  profiler zones and per-frame text allocations are gone (Measurements); and this spec gains its Accessibility section.

### Rulings

In the order the plan ledger took them. Each is a call made where the plan or spec was silent or wrong, with its reason
and what it costs if wrong. The user's own decisions are marked as the user's, and "flag to user" marks a call the user
should look at.

1. **Salvage lives in the plan's own workspace's `salvage/`**, not `.superpowers/sdd/chunk3/salvage`: one workspace per
   plan. Costs nothing if wrong; the path was only a pointer.
2. **`Renderer::Capture` also takes `core::Jobs&`** (before `extra`), waits for background rasterising, then lays out: a
   capture must never miss a glyph, and the reference tests already hold a `Jobs`. If wrong, one parameter to remove.
3. **Task 3's base-pixel test** asserts the layout's height equals the shaping font's line height (`TTF_GetFontHeight` at
   `SDF_BASE_PIXELS`) plus the documented spread margin, within 1, and that "FoxFox" is twice "Fox" wide within
   2 × `SDF_SPREAD`: the plan's 1.2 em guessed the font's metrics (the line is about 1.45 em: 70 px at 48). If wrong, a
   test tolerance.
4. **Mesh overflow arithmetic is a pure function**, `MeshQuadsDropped(vertices)`, tested in `gfx_tests` (public if lint
   forbids tests including `internal/`): the plan named the test but not the seam. If wrong, the function moves headers.
5. **Fix-round commits stay separate during the run and are squashed into their task's commit** (fixup and autosquash
   rebase) before the PR: history shows one commit per feature, and the user's rule forbids hiding no finding, only
   noise in history. If wrong, the PR carries extra `fix(...)` commits for unreleased work.
6. **The re-reviewer's "`ReadName` call may exceed 120 columns" needs no change**: `.clang-format`'s `ColumnLimit` is 128
   and the commit hook passed `fmt`. Costs nothing if wrong (`fmt` would reflow it).
7. **`KeyLine` returning the first of duplicate keys stands** (the old loop returned the last): duplicate keys in
   `settings.yaml` are malformed YAML whose handling yaml-cpp leaves unspecified. If wrong, a warning names the other
   duplicate's line.
8. **`KeyLine`'s `as<std::string>()` on a non-scalar key stays**: `ParseSettings`' existing try/catch of `YAML::Exception`
   (`settings.cpp:99` and `:151`) turns it into the reader's error as for any malformed file. If wrong, a malformed
   settings file with a list as a key fails to read rather than warns.
9. **The reviewer's MSVC C4866 worry (temporaries inside `CHECK` in the new tests) is settled by the branch's `check`**
   (windows-msvc) before the PR, not per task: the same shape already builds in `arena_tests.cpp`. If wrong, a test
   tweak found at `check`.
10. **Task 3's shaping font stays plain (no SDF); only the worker's rasterising font uses `TTF_SetFontSDF`**, and each
    field is placed at its glyph's plain-layout rect grown by the spread: SDL3_ttf 3.2.2 misplaces SDF glyphs in
    vertical layout and widens lines with SDF on (3 tests failed with it). If wrong, glyph edges sit off by a texel or
    two, visible in Task 4's pictures; one line plus the offset switches back.
11. **Two `TextLayout`s opening or closing fonts concurrently on different threads stay unguarded**: the game has one
    cache and doctest runs serially. If wrong, a race in a future multi-cache use; the header documents it.
12. **`TextLayout` starts each worker batch with `jobs.Start(1, …)` and holds the returned `Jobs::Pending`** in its
    implementation (polling `Done()` instead of a `running` flag), not `Jobs::Background` as the plan said:
    `Background`'s one slot belongs to the logic thread's saves (the same-thread contract) and waiting on it would
    hitch the main thread. If wrong, a batch could sit behind other low-priority work (scene decodes), delaying a new
    glyph by a frame or two.
13. **`TextLayout` gains `Flush(jobs)`**, waiting until every requested glyph has landed (tests and `Capture`, never in a
    frame): with `Jobs::Start`, `WaitForBackground` no longer covers glyph batches. If wrong, one extra public function.
14. **Vertical columns start at their first character's ink** (SDL3_ttf pads one- and two-character columns' tops by up
    to an em; from three characters it already does this); horizontal layout is unchanged: a caption hung from its
    anchor must not gap. If wrong, captions sit a few pixels off their anchor, visible in Task 4's pictures.
15. **The re-reviewer's "add `#include <algorithm>` to `text_layout_tests.cpp`" is rejected**: tests/AGENTS.md says test
    `.cpp` files never include standard headers (`unit/pch.hpp` holds them; lint fails on one). Costs nothing.
16. **Exact float equality in the vertical placement checks stands**: the values are whole numbers from integer rects,
    which `f32` represents exactly. If wrong, a test flake fixable with `Approx`.
17. **The field shader takes its edge width from the UV's screen derivative** (scaled to field units), not `fwidth(d) ×
    0.7` as the plan said. Measured: shimmer 3.1% to 0.68%, jitter steps 0.91–1.72 px to 1.29–1.36 px on a 1.333 px
    step; thin strokes break the field's own derivative. If wrong, edges slightly too soft or hard at extreme scales,
    visible in the zoom pictures.
18. **The shader uses FreeType's exact encoding** (edge 128/255, 16/255 per base pixel; measured on the field of "I":
    the byte is 128 + 16·d) instead of the plan's 0.5 and 0.125, which were approximations. Costs nothing if wrong.
19. **The jitter picture test runs at zoom 1** (1.333 px steps) instead of the plan's literal 2 px: a whole-pixel step
    can't catch snapping. If wrong, a test parameter.
20. **`MeshVertex` and `MeshQuadsDropped` move to the public `gfx/mesh_vertex.hpp`**: lint forbids tests including
    `internal/`. Costs nothing if wrong.
21. **Captions also fade below the readable size** (by size, no overlap rules), added to Task 5: an 8 px caption at zoom
    0.25 merges into a block, which is the spec's "blur" anti-goal, and the spec's fade rule names labels only. If
    wrong, the caption disappears when zoomed far out where it was unreadable anyway.
22. **`TextPixels` takes pixels per world unit** (`min(w/1920, h/1080) × zoom`, exposed once from `gfx/camera`), not the
    bare zoom as the plan's "world size × zoom × scale" said: the spec defines sizes in world units, so text must keep
    its proportion to the world on every window; `LABEL_MIN_PIXELS` and `NUMBER_MIN_PIXELS` stay true screen pixels. If
    wrong, text is a fixed pixel size per zoom regardless of window; one parameter to revert.
23. **The field shader supersamples** (four rotated-grid samples, a 0.75 px linear edge ramp): at the window-fitted 13 px
    size the 0.55 px outline shimmered 3.0% over pale ground (limit 2%); now 0.95% shimmer and 0.083 px worst jitter.
    If wrong, a little GPU cost per text pixel and slightly softer edges; the measurements table records it.
24. **The shimmer metric is signed darkness** (1 − luminance) with against without the label, per the plan's own
    definition (|ΔL| over-counted where fill and outline share a pixel). Costs nothing if wrong.
25. **Flag to user: `LABEL_WORLD_SIZE` stays 20 world units.** Labels fade below about zoom 0.68 on a 720p window and
    about 0.45 at 1080p; the spec leaves sizes to the builder and chunk 11's text size scales them. If wrong, a constant
    to change.
26. **A stack's box covers all its members, and a label bridging two stacks merges them**: the plan's literal rule drew
    three overlapping "Fox (+N)" in the crowd picture. If wrong, stacks group more widely than strict pairwise overlap.
27. **A stack keeps its anchor creature until another is 4 px nearer the group's middle** (hysteresis): otherwise it hops
    between orbiting foxes, the jitter anti-goal. If wrong, the anchor lags the true middle by up to 4 px.
28. **The `labels_crowd` picture is at zoom 0.75 with the fox orbit shrunk to 50**: at 0.5 on 720p labels are 6.7 px and
    fade out. Costs nothing if wrong.
29. **Captions fade by size with smoothstep from 6.75 to 9 px** (no time state). Costs nothing if wrong.
30. **Stack changes never pop** (spec decision 2, "changes fade, never pop"): a count change cross-fades old and new
    stack text over `LABEL_FADE_SECONDS`, and a stack's drawn anchor glides to its new creature over the same time; the
    plan's one-opacity `PlacedLabel` couldn't express it. If wrong, a little extra state per stack and two layouts drawn
    during a cross-fade.
31. **`LabelPass::Anchor(entity)` is defined for every label this frame**: its placed or stack position or, when hidden
    by priority, its own projected anchor. The spec's zoomed-out numbers start "from where its creature's label is
    placed, even when faded out", and a hidden label has no other place. If wrong, a number at a hidden label starts at
    the creature's own label spot instead of nothing.
32. **Flag to user: stacks appear in the live arena only where foxes bunch** (rarely: 12 foxes on a 300-unit ring); the
    crowd reference scene shows them, and chunk 5's world is where they matter, so no arena content was added in this
    chunk. If wrong, a small arena addition later.
33. **Tiny per-label helpers called inside a zoned per-frame function carry no `PN_PROFILE_ZONE`** (`Glide`, `Count`,
    `Collect`, `OverlapsOthers`, `Cells`, `Cover`, `Overlaps`): a zone per label per helper distorts releasedbg profiles
    more than it informs, and the enclosing `Place` zone covers them. If wrong, finer profile detail is missing; zones
    are one line each to add.
34. **Flag to user: stack count changes cross-fade two whole texts**, so the shared name ghosts at about 75% for 0.15 s:
    a consequence of the no-pop ruling (30); a digits-only cross-fade is possible later if it shows. If wrong, a
    visible 0.15 s ghost on count changes.
35. **Task 5's fix-round breaker tripped on a comment-wording Minor** (real, nothing builds on it): the fix was carried
    into Task 6's dispatch (same module) so it was fixed and reviewed there, per the user's fix-every-finding rule. If
    wrong, a comment stays slightly narrow.
36. **The user's decision (2026-11-11, from a burst screenshot): damage numbers scatter and stagger.** Scatter is ±0.75
    em across and ±0.35 em up and down; stagger is a delay window of `clamp(n × 20 ms, 60 ms, 500 ms)` per target per
    tick; both are deterministic from a hash of the hit, and neither applies in combined mode. Sent to Task 6's
    implementer mid-task. If wrong, constants to tune.
37. **The user's decision (2026-11-11): many hits on one creature in one tick read as a fountain.** Scatter and stagger
    both widen with that creature's hit count in the tick (scatter up to about ±3 em, stagger up to about 1.5 s); one or
    two hits are unchanged. Done in Task 6's fix round.
38. **Count-scaled spread values**: scatter half-widths × `clamp(sqrt(n/4), 1, 4)` (n of 64 or more gives ±3 em across and
    ±1.4 em up and down) and stagger `clamp(n × 20 ms, 60 ms, 1.5 s)`: first guesses at the user's "up to about ±3 em,
    about 1.5 s" in 37. Tune after the user looks.
39. **The re-reviewer's "add `<cmath>` and `<algorithm>` to `damage_numbers.cpp`" is rejected**: src/AGENTS.md says a
    `.cpp` never includes a standard header (`pch.hpp` holds them; lint fails on one). Costs nothing.
40. **The re-reviewer's "doc line may exceed 120 columns" needs no change**: `ColumnLimit` is 128 and the commit hook
    checks `fmt`. Costs nothing.
41. **`--burst` follows `--quit-after`'s pattern**: parsed in `main.cpp` into `app::Options`, with the logic moved into a
    testable `src` function (`app::HasFlag`) per tests/AGENTS.md, not in `core/command_line` as the plan assumed (that
    is where game flags live). Costs nothing.
42. **Task 8 is added: make damage-number placement cheap.** Profile, then compose numbers from cached digit layouts with
    no per-number string key, and place them on the job system deterministically; the target is a main-thread frame of
    4 ms median or less at the burst's peak (the phase spec's example quarter-frame) and a worst frame under 16.7 ms.
    The reason is the measurement below: the spec's anti-goal of damage numbers costing the frame budget is failed at
    13.85 ms median. If wrong, a day's optimisation the measurement didn't strictly require. **Result:** 0.43 ms median
    and 2.6 ms worst at the burst's peak (Measurements). The profile showed most of the 2.4 µs a number was the
    profiler's own nested zones, about nine a number (about 0.25 µs each to their caller), the rest a `std::format`, a string-keyed
    layout lookup and the quads; composition, chunked placement and dropping the per-number zones (src/AGENTS.md's new
    gotcha) took it to about 0.07 µs of work a number, spread over the workers. Composition is exact, not within the
    0.5 base pixel allowed: Noto Sans SC's digits advance 27 base pixels, "+" 27 and "-" 17, and nothing kerns, so the
    composed glyphs equal the shaped ones bit for bit (a test pins it) and every reference picture is pixel-identical.
    A number running top to bottom (none exists) is still shaped, drawn after those across.

43. **Labels fade on a main-thread frame clock** (`app::FrameClock`, wall time from `platform::NowNs`, each frame's step
    held to 0.1 s), and damage numbers stay on the render clock: the render clock stops while the window is unfocused or
    minimised, during a calendar step and while a scene loads, but zoom and hover still work then, so labels must keep
    fading; numbers are the simulation's events and freeze with it. If wrong, labels animate during a pause.
44. **Labels never pop** (decision 2): a label new to a running pass fades in, one whose item leaves the snapshot fades
    out where it was last drawn over `LABEL_FADE_SECONDS` (the pass keeps the label, its text item included, until it
    has), and only the pass's very first frame (no history: a capture) starts labels at their targets. A label or caption
    whose glyphs are still rasterising is held at nothing until they all land, then fades in (`LabelInput::complete`,
    `gfx::CaptionFades`). The tests that pinned the old behaviour changed as intended. If wrong, a 0.15 s fade on spawn
    and death.
45. **With the outline and shadow both off, damage numbers use a dark variant of each kind's colour**
    (`DARK_KIND_COLOURS`, Values chosen): the light colours were about 1.3:1 on the parchment. `DamageNumbers` picks the
    table from the settings it already reads, as the renderer's text options come from the same ones, because the
    renderer can't tell a number's kind from its colour. If wrong, colours to retune with chunk 11's palettes.
46. **Combined numbers starting on one tick on one target scatter, sized by their count as hits are, with no
    stagger:** they printed over each other (the burst's four kinds a fox; two uses ending on one tick); a combined
    number already waited for its use to end. Costs nothing.
47. **The `core::Jobs` Pending-reset race is fixed in this branch** (its own `fix(core)` commit): `Done()` read a counter
    set before enkiTS marked the task complete, so `~Pending`'s wait could run another low-priority task (a scene
    decode) inside a frame; `Done()` now also requires enkiTS's completion. A test runs the race 150,000 times, yielding
    while it waits (before the fix a run lost it 18 to 103 times). Checking it under ThreadSanitizer found `core_tests` already failing there
    (14 reports): `Start()` and `Background()` gave TSan no fork edge, as `ParallelFor` does, so they now do
    (`88574b1`); `core_tests`, `gfx_tests` and `app_tests` pass under it. If wrong, a small core change outside the
    chunk's scope.
48. **Flag to user: numbers on normally sized creatures start over that creature's own name label** (the reviewer
    declined to judge it; aesthetic) and stay so. If wrong, numbers offset above labels.
49. **Builds rewriting `xmake-requires.lock` stays out of this branch** (sweep-up backlog); the PR carries no rewritten
    lock. Costs nothing here.
50. **The ThreadSanitizer fork edges (`88574b1`) stay in this branch** rather than a branch of their own: they were found
    while verifying ruling 47's race fix, which needs `core_tests` clean under TSan to be believed, and `core_tests`
    already failed there before this branch. If wrong, a core fix that could have been reviewed on its own.
51. **The dark number colours live in `app::DamageNumbers`** (`DARK_KIND_COLOURS` beside `KIND_COLOURS`, chosen when
    the numbers are built), not in `gfx::TextColour`'s dark-ink fallback: a number's colour comes from app's kind
    table, so the dark variants sit with it. If wrong, chunk 11's live text-option changes must reach `DamageNumbers`
    as well as the renderer.
52. **One more small fix batch after the final review's fix wave**, though superpowers allows one wave: AGENTS.md's
    rule that every finding is fixed, Minor included, overrides it. It fixed three comment errors, made the race test
    yield (it spun on one worker, slow on a one-core runner), and corrected the frame clock's doc. Two observations
    stayed as they are: a gone label fades at its last screen position, so for 0.15 s it doesn't follow a pan
    (ruling 44 accepts it), and `Capture` resets the live label pass (only tests call it). If wrong, one more short
    review round than the process asks for.

Not ledger rulings, but calls made in measuring: the run is unpinned (see Measurements), and Tracy's CSV exporter
(`tracy-profiler-csvexport` 0.14.1) isn't in Pixi's `profiler` environment, so it was read from Pixi's package cache with
`pixi.toml` unchanged; chunk 5 should add it so the measurement is a `project_n` command.

### Values chosen

- **`SDF_BASE_PIXELS` 48, `SDF_SPREAD` 8, `GLYPH_PAGE_SIZE` 2048.** At zoom 2 labels are 40 px and the caption 64 px, drawn
  from 48 px fields with no blobbiness; a larger base can't help the caption at 8 px (two Chinese characters in 8 px is a
  pixel budget, not a field one). The spread is 8 so a 0.55 px outline and the shadow (up to the spread) stay inside the
  field; the byte steps by exactly 16 per base pixel, which confirmed it.
- **World sizes:** labels 20, captions 32, numbers 24 world units; the text's pixel size is `size × pixels-per-unit ×
  TextOptions::scale`.
- **Minimums:** `LABEL_MIN_PIXELS` 9 (below it labels fade; at 5 px they are illegible blobs, at 10 px readable);
  `NUMBER_MIN_PIXELS` 12 (numbers scale down to it and hold, so they stay readable zoomed out);
  `VISIBLE_CREATURE_PIXELS` 8.
- **Frame clock:** `app::FRAME_STEP_LIMIT_NS` 0.1 s, the most labels' fades move on between two frames: a window shown
  again after a while moves its fades on by two thirds of `LABEL_FADE_SECONDS` at most in its first frame, so a fade
  under way still shows its last third rather than ending at once.
- **Dark number colours** (`DARK_KIND_COLOURS`, outline and shadow off): damage ink `#20242A` (the labels' `TEXT_INK`),
  healing `#145A22`, poison `#5E2A9E`, burn `#823200`. Against the arena's bare parchment, measured on
  `numbers_one.png` from its dimmest patch (relative luminance 0.529, the median of a patch left of centre) to its
  brightest (0.726, the 90th percentile of the middle), their contrast is damage 8.7 to 11.7:1, healing 4.6 to 6.2:1,
  poison 5.0 to 6.7:1 and burn 4.8 to 6.5:1 (WCAG's ratio, from the packed linear bytes; a test checks 4.5:1 at both
  ends); they lie 52 to 114 apart in CIELAB. The light colours read about 1.3:1 (white) there. The reference picture
  `numbers_bare` shows them.
- **Fade and hysteresis:** `LABEL_FADE_SECONDS` 0.15 (the accessibility floor: nothing flashes), `LABEL_HYSTERESIS_PIXELS` 4
  (a stack keeps its anchor and a label its band until something is 4 px better).
- **Grid:** `LABEL_GRID_PIXELS` 64, a flat grid of ⌈w/64⌉ × ⌈h/64⌉ cells reused each frame: a label's box is up to a few
  cells wide, so an overlap test touches a handful of neighbours instead of every label.
- **`MAX_MESH_QUADS` 65,536** (4 MiB of vertices, u32 indices): a burst of 4,000 two-digit numbers is about 8,000 quads,
  so it has about eight times the room. The measured steady burst below places about 3,900 numbers a frame and logged no
  overflow.
- **`NUMBER_RESERVE` 8,192** numbers in the pool and the per-frame texts: twice the burst's live numbers; it grows past
  it, so the figure is only an allocation hint.
- **Numbers:** `NUMBER_SECONDS` 1, fade from 0.6; rise 1.5 em; bounce peak 1 em at 0.15 s, rest 0.6 em at 0.4 s.
- **Scatter and stagger:** `NUMBER_SCATTER_X_EMS` 0.75 and `_Y_EMS` 0.35; growth from `NUMBER_SCATTER_GROWTH_HITS` 4,
  capped at `NUMBER_SCATTER_GROWTH_MAX` 4; stagger `NUMBER_STAGGER_PER_HIT_SECONDS` 0.02, minimum 0.06 s, maximum 1.5 s.
  The user's request, with the builder's guessed values (rulings 36 to 38).

### Measurements

**Shimmer and jitter** (reference tests on lavapipe, 1280×720, zoom 1, over a flat pale ground where the dark outline is
most of the ink):

| | Before the fixes | After |
|---|---|---|
| Shimmer (ink across camera offsets {0, 1/3, 2/3}² px; the limit is 2%) | 3.1% with `fwidth(d)`; 3.0% over pale ground without supersampling | 0.68%, then 0.95% over pale ground with the outline |
| Jitter (a label following a creature, expected step 1.333 px; the limit is 0.15 px) | steps 0.910, 1.375, 1.715, 0.910 px | steps 1.293, 1.358, 1.350, 1.293 px (worst error 0.040); worst 0.083 px with supersampling |

**Pre-warm:** the shipped table pre-warms **44 glyphs in 111 ms** in the releasedbg runs below (113 ms in debug), about
2.6 ms a glyph; the scene is resident 139 ms after. A new glyph costs about 3.8 ms to rasterise on a worker in debug (400
glyphs in 1.54 s) and shaping 400 new glyphs costs 2.3 ms on the main thread, so unseen glyphs never hitch a frame. A
label or caption waiting for them is held back until every one has landed, up to about 1.5 s for 400 new Chinese glyphs,
then fades in (ruling 44): before, its characters appeared piecemeal over that time. A larger Chinese table lengthens
the pre-warm linearly.

**Frame time.** Two runs, `pixi run project_n profile --seconds 20 -- --burst` and the same without `--burst`: releasedbg,
linux-clang, arena scene, 1280×720, Wayland, NVIDIA RTX 4090, presenting with mailbox (so unthrottled). **Unpinned: the
game ran 30 workers**; there is no way to pin the job system to 2 workers (the game takes `Jobs::DefaultWorkers()`, and
`profile` passes no option), so the phase bar's 2-worker run waits for chunk 5. Read with `tracy-csvexport -u`; the
frame is one iteration of `App::Run`'s loop on the main thread (the zone `Run`), skipping the first two seconds (loading
and the burst's ramp-up). Times are the main thread; the logic thread and the GPU were not read.

| Main thread, one loop iteration | Median | 99th percentile | Worst |
|---|---|---|---|
| Without `--burst` (88,680 frames, about 5,700 fps) | 0.175 ms | 1.25 ms | 1.61 ms |
| With `--burst` (1,296 frames, about 72 fps) | 13.85 ms | 16.99 ms | 18.91 ms |
| After Task 8, without `--burst` (81,985 frames) | 0.174 ms | 1.20 ms | 2.35 ms |
| After Task 8, with `--burst` (35,872 frames, about 1,950 fps) | **0.434 ms** | 1.46 ms | **2.56 ms** |

Per second, the burst settles by its third second at 69 to 76 frames a second and 13.2 to 14.2 ms medians, with about
3,900 numbers placed a frame (280,000 a second); every second's worst frame is between 15.4 and 18.9 ms. The text zones
(per call, microseconds unless stated):

| Zone | Without `--burst`, median / 99th / worst | With `--burst`, median / 99th / worst |
|---|---|---|
| `PlaceTexts` (renderer, all game text) | 69 / 126 / 352 | 64 / 12,155 / 13,177 (steady seconds: about 11.7 ms median) |
| `LabelPass::Place` | 1.8 / 3.9 / 67 | 2.1 / 5.3 / 26 |
| `DamageNumbers::Texts` | 1.4 / 3.1 / 45 | 0.9 / 1,371 / 2,468 |
| `PlaceNumber` (one number) | 2.5 / 5.2 / 64 | 2.4 / 5.4 / 998 |
| `UploadGlyphs` | 0.0 / 0.1 / 140 | 0.0 / 0.2 / 146 |

26 labels are placed a frame in both runs. Text's cost is nearly all `PlaceNumber`: about 2.4 µs a number, on the main
thread, and 3,900 numbers is 9.4 ms of the 11.7 ms. The glyph cache never stalled a frame (`UploadGlyphs` stays under
150 µs), no run logged an overflow or a drop, and the log shows 0 late snapshots in the run without the burst and in the
burst's.

**After Task 8** (the same commands and machine, profiles `20261111-125848` with `--burst` and `20261111-125726`
without; the first after-run, `20261111-125449`, before a lone chunk ran inline, gave 0.48 ms with the burst and cost
0.09 ms without it, the two `ParallelFor`s waking 30 workers for nothing). From its third second the burst places 16
chunks of 256 texts a frame (3,841 to 4,096 numbers) and every second's median is 0.43 to 0.49 ms, its worst 1.5 to 2.6
ms. Per call, microseconds, median / 99th / worst, with `--burst`:

| Zone | After Task 8 |
|---|---|
| `PlaceTexts` (all game text) | 173 / 268 / 452 (was about 11,700 median) |
| `PlaceNumbers`, the numbers' call (the snapshot's own, with no numbers, is 0.5 median) | 86 / 169 / 308 |
| `PlaceNumberChunk` (256 numbers, on any thread) | 17.2 / 25.6 / 158 |
| `CountNumberChunk` | 2.5 / 15 / 91 |
| `AddNumberRuns` | 5.8 / 9.8 / 75 |
| `DamageNumbers::Texts` | 52 / 83 / 208 (was 1,035 median in the steady burst) |

Without the burst the same zones cost `PlaceTexts` 68 / 126 / 273, `PlaceNumbers` 0.6 and `NumberFaceOf` (twelve
lookups) 3.1 median. The run is still unpinned (30 workers); placed alone on one thread the numbers would take about
16 × 17 µs ≈ 0.3 ms. Its worst frame, 2.35 ms against 1.61 before, is a single frame and not text's: its 99th
percentile improved (1.25 to 1.20 ms) and `PlaceTexts`' worst in that run is 0.27 ms.

**After the final review's fixes** (profiles `20261111-134042` before the zone and allocation commit, `20261111-134403`
and `20261111-134633` after, without the burst; `20261111-134532` after, with it; same machine and method). The
per-label zones were most of `PlaceTexts`: without the burst it went from **74.1 to 10.5 µs median** (132 to 21 µs p99,
369 to 164 µs worst; a second run 12.0 / 28 / 121) for the same 26 labels, while `LabelPass::Place` stayed about 3 to
4 µs. With the burst `PlaceTexts` is 143 / 256 / 979 µs (Task 8's 173 / 268 / 452). The `Run` iteration's median moved
between runs of one build (570 and 235 µs without the burst) with the present's `SubmitFrame`, which waits on the
driver and compositor: its median was 80 µs in Task 8's run and 160 µs or more in these. So the comparable figure is the
main thread's work, the iteration less `SubmitFrame`: without the burst 91 µs median in Task 8's run, 110 µs before this
commit (with the frame clock and gone labels) and **52 to 58 µs after**; with the burst 293 µs in Task 8's run and
298 µs after.

### Open for the user

- **The phase bar, met after Task 8** (ruling 42). The bar is "well under 16.7 ms on the main thread" and the user's
  example "under 4 ms". The burst (a stress view: 4,000 hits a second, not a game's real load) cost 13.85 ms median
  and 18.9 ms worst; after Task 8 it costs **0.43 ms median and 2.6 ms worst**, and without the burst the frame is as
  before (0.174 ms median). Not measured: pinned to 2 workers or on a minimum-spec machine (chunk 5 sets the real
  numbers); sequential placement alone would add about 0.3 ms.
- **Label world size 20** (ruling 25). Labels fade below about zoom 0.68 at 720p and about 0.45 at 1080p. Chunk 11's text size
  scales them; the user may want larger labels or a lower minimum.
- **Stack count changes cross-fade whole texts** (ruling 34), so the shared name ghosts at about 75% for 0.15 s (a consequence of
  "changes fade, never pop"); a digits-only cross-fade is possible if it shows.
- **Stacks rarely appear in the live arena** (ruling 32) (12 foxes on a 300-unit ring); the crowd reference scene shows them, and
  chunk 5's world is where they matter. No arena content was added.
- **The F7 burst reads as a dense ring of numbers** round the cultivator, edges legible, middles dense: it is the stress
  view, and the scatter and stagger values (rulings 36 to 38) are first guesses.
- **Numbers on a normally sized creature start over its own name label** (ruling 48): left as is; the user may prefer
  them offset above the label.
- **The dark number colours** (ruling 45) are first choices that clear 4.5:1 on the parchment; chunk 11's palettes
  replace both colour tables.
