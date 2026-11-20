# Text layout budget: new labels laid out a little each frame, none too small to read

Status: approved by the user 2026-11-20 (short design: a small change).

## Intent

A frame that meets many new names lays every one of them out at once, on the main thread: `measure --runs world`'s
kept capture (`build/measure/20261120-002525/world.tracy`) has a frame 0.26 s into the widest view making 2,760
layouts (`TextLayout::Impl::Make`, 14.35 ms self, about 5 µs each; the median frame makes 4), work 20.8 ms, when the
camera path cuts onto `WORLD_BUSIEST` at zoom 0.25. Every one of those labels is too small to read there and never
draws. The month step does the same at any zoom (new journeys, new names: 5b2's 22 to 25 ms frame, `Make` 13.4 to
18.4 ms self). The user wants such frames gone: new text arrives over a few frames, at a bounded cost a frame, and
text too small to read costs nothing.

## History

- 5b2's spec (`2026-11-15-phase6-chunk5b2-moving-design.md`, "The `Make` hitches"): R19a fixed per-frame stack-text
  churn (stack text measured by its count's digits, `WidestCount`); R19b accepted the month step's `Make` hitch as
  hidden by its transition, naming "prewarm the new names' layouts" as the fix if that proved wrong. This branch fixes
  it another way.
- The sweep-up backlog's "Text layout hitch when many new names enter the view at once" (2026-11-20) weighed (A) skip
  layouts too small to read, (B) a per-frame layout budget, (C) shaping on a worker; the user chose B, then B with A
  (2026-11-20). C stays out: the shaping font is used by one thread, and the glyph worker already holds the other.
- What this builds on: `Layout::complete` (a glyph still being rasterised: the label is placed but fades out until it
  lands, `LabelPass`, `CaptionFades`), the only existing per-frame budget (`LOAD_BUDGET_NS`, 2 ms, the scene loader's),
  and `Renderer::Capture`, which lays out its texts and waits for their glyphs so a picture never misses one.

## Goals

1. A label too small to read (`pixels < LABEL_MIN_PIXELS × text scale`, known before any layout) is never laid out:
   it uses its cached layout when there is one (so one shrinking as the view zooms out still fades out as today), and
   otherwise sits out the frame: not given to the label pass, so it takes no room and joins no stack.
2. Each frame lays out new label text (a label's name and its "Name (+N)" stack text) only until `LAYOUT_BUDGET_NS`
   (2 ms) has passed since its first new layout, and always at least one, so a crowd always comes in. Cached layouts
   cost nothing and are never held back.
3. A label whose text, or stack text, isn't laid out yet sits out the frame as in Goal 1; once laid out it enters the
   pass as a new label and fades in (`LABEL_FADE_SECONDS`), as any new label does.
4. New layouts go in priority order (selected, hovered, places, the rest, as `LabelOf` raises them), then nearest the
   middle of the screen first, so the player's own label and the places' never wait and a crowd fills in from the
   middle. Only labels that miss the cache are sorted.
5. Captions, damage numbers and the HUD keep the current `Lay` and are never held back: they are few, and HUD text must
   never blink out (accessibility).
6. `Renderer::Capture` lifts the budget for its frame: every reference picture stays as it is.
7. A cached layout is forgotten after `LAYOUT_SECONDS` (5 s) unused by the frame clock, replacing `LAYOUT_FRAMES`
   (300 frames: 5 s at 60 Hz, 1.8 s at 165 Hz), so eviction no longer depends on the frame rate.

## Non-goals and anti-goals

- No shaping on a worker (option C), no prewarming of month-step layouts.
- Never a label drawn with gaps or at a wrong size while it waits: it is absent, then fades in.
- Never a budget on HUD text or anything a player reads to act on (Goal 5).
- No median cost: the per-frame sort and checks must not move the frame median in `measure`.

## Approaches (picked by the user 2026-11-20)

- **What the budget counts: time (picked) or a count.** Time adapts to the machine (a slower CPU lays out fewer names a
  frame, still within 2 ms); a count (say 300) costs 1.5 ms here and perhaps 4 on the minimum spec, so it bounds
  nothing. Tests use a budget of zero and an unlimited one, never a timing.
- **Where it lives: the layout cache (picked) or the renderer's label code.** Only the cache knows a cached layout
  (free) from a new one (the cost), so it holds the budget: a second, budgeted way to ask for a layout that answers
  "not yet" once the frame's budget is spent; the renderer's label code asks through it.

## Left to the builder

- The budgeted call's name and shape (an optional or pointer result, say), how the frame's budget starts and resets
  (with `EndFrame`, which now takes the frame clock for Goal 7), and how Capture lifts it.
- Where the ordering and the too-small rule live: pure, tested helpers where the renderer can't be unit tested.
- A label sitting out gives `LabelPass::Anchor` nothing, so a damage number of a creature too small to see falls back to
  its own anchor for the frames its label waits (the arena's few labels are cached at once); keep it so unless a test
  shows otherwise.

## Acceptance

- Unit tests (small, representative): a zero budget lays out nothing new but still one a frame and serves cached
  layouts; an unlimited one lays out all; the budget resets each frame; the too-small rule and the order; eviction by
  the clock.
- `reference` passes with no picture moved; shimmer unchanged.
- `measure --runs world` against main's numbers: no frame in the widest view or the month step led by `Make`; `Make`
  self time a frame within about the budget plus one layout; hitches not up; median unchanged.
