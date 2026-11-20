# Text layout budget: new labels laid out a little each frame, none too small to read

Status: implemented 2026-11-20 (approved by the user 2026-11-20; short design: a small change).

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

## Outcome

Implemented on branch `layout-budget` in three tasks, 2026-11-20: the cache ("perf(gfx): give text layouts a per-frame
budget and forget them by time"), the renderer's labels ("feat(gfx): lay out new labels in turn within a frame budget",
the widest count's fallback below folded in), and this measurement. The plan's ledger
(`.superpowers/sdd/`, gitignored) held the rulings below; they are kept here with their reasons.

### Measured

`pixi run project_n measure --runs world --keep` on the dev machine, 2026-11-20, main's head ("docs(design): record the
final review's rulings on input latency") against the branch head: releasedbg, fullscreen 2560x1440, 2 workers,
unpaced, under Tracy. Only the 59.95 Hz monitor was connected, so both runs are paced at 60 Hz (frame median 16.7 ms
in every segment of both) and the comparison is on work (a frame less its wait for the GPU) and on `Make`'s self time
per frame on the main thread, read from the kept captures (`build/measure/20261120-145157/` main,
`build/measure/20261120-145352/` branch).

| segment | `Make` worst frame (layouts made in it), main | branch | work worst in the first 0.5 s, main | branch | work median (rest), main | branch |
|---|---|---|---|---|---|---|
| pan | 4.56 ms (982) | 1.70 ms (234) | 4.71 ms | 3.41 ms | 2.23 ms | 1.94 ms |
| zoom in and out | 3.96 ms (838) | 1.68 ms (376) | 4.58 ms | 2.52 ms | 2.08 ms | 1.73 ms |
| widest view (the cut-in) | 7.05 ms (2,758) | 0.05 ms (4) | 11.72 ms | 2.99 ms | 3.08 ms | 1.56 ms |
| month step | 7.70 ms (2,223) | 0.02 ms (1) | 13.97 ms | 2.32 ms | 3.15 ms | 1.32 ms |
| labels | 3.02 ms (910) | 1.65 ms (746) | 6.15 ms | 5.07 ms | 2.44 ms | 2.16 ms |

- Frames fall in a segment by their start, so the month step's last frame on the branch (20.28 s, 1.65 ms, 746) is
  the labels zoom's first, counted under labels above; at the step itself (17.34 s) the branch made one layout. The
  labels zoom's names came in over three frames: 1.65, 1.64 and 0.66 ms (746, 689 and 286 layouts), where main made
  910 in one.
- Frames led by `Make` with over 2 ms of it: main 2 (the first frame, 16.02 ms and 7,412 layouts, and the month step
  at 17.33 s), branch none. The worst `Make` frame on the branch, 1.70 ms, is within the budget plus one layout.
- Hitches: one in each run, the month step's 51 to 54 ms stall at 17.3 s, whose own zones are all under 1.3 ms (the
  frame waits on the step, not on text): not up. Median work is equal or lower in every segment (follow 2.05 against
  2.10 ms, the only one not lower, is noise).
- Shimmer unchanged (0.0481872 in both); `reference` passed with no picture moved.
- Input latency read 32.4 ms on main and 17.5 ms on the branch (8 presses each): at 60 Hz the presses' phase against
  the tick dominates, and this branch touches nothing on that path. Not a result.
- By eye: the same camera path windowed, with screenshot bursts at the pan, the zoom out, the cut-in, the month step
  and the labels segment, on both heads. Names on the pan are complete, the newest fading in; the zoom out's shrinking
  labels fade and overlap exactly as on main; at 0.25 and 0.6 travellers' names are too small to read on both. Stills
  can't show a single frame's drop-out; the widest-count fallback below is argued, not seen.

### Rulings and the builder's calls

- **A label goes in at once only when its name and stack text are cached and complete.** `Cached` never makes a
  layout again, so a label cached while a glyph was still being rasterised (every name `Capture` asks for before its
  flush) would stay faded out for good. Such a label waits for its turn instead, where `TryLay` makes it again once the
  glyph has landed, or serves it as it is if the budget is already spent when its turn comes. Turns stop at the first
  label that needs a new layout once the budget is spent, so a cached-incomplete label behind it waits a frame more:
  it is new, so it has no opacity yet, and its glyphs are prewarmed. Going on past it would cost a lookup for every
  waiting label left, thousands in the cut-in.
- **The widest count's fallback.** The task review found that when the frame's label count gains or loses a digit
  (9/10, 99/100, 999/1000), every stack text ("Name (+99)" becomes "Name (+999)") is new, so every label on screen
  would wait, and those past the budget would leave the pass for a frame, fade a step and leave their stacks. A
  readable label whose name is cached and complete now falls back to its stack text at the previous widest count
  (kept until the count changes again), goes in at once with that width, and has the new text made in its turn. No
  unit test: the fallback lives in the renderer's use of the cache, and a test of the count tracker alone would not
  fail on the drop-out. Its commit was folded into the feature's at the final review, as no release ever had the
  drop-out to fix.
- **`PlaceLabel` keeps the unbudgeted `Lay` for a drawn stack's "Name (+N)".** `GatherLabels`' turns lay out the stack
  text at the widest count, "Name (+99)", only to measure it; the drawn text with the stack's real N is laid out by
  `PlaceLabel` alone, once for each new N of each stack (both N's while the count cross-fades), and kept after. With
  the budget a crowd's labels arrive over about three frames, so a stack's N can change in several frames running,
  each costing one layout per changed stack. Measured, the branch's worst frame holds 1.70 ms of `Make` in all, so it
  never shows; no budget added.
- **Turn order uses the label's anchor before the top-centre height shift**, worked out once per item: a waiting label
  may have no layout to know its height yet, and the shift changes only distance ties.
- **A clock that goes back** (a capture's fixed view) keeps layouts in use, and a stopped clock only delays eviction;
  the frame clock is monotonic in play. Both bounded, as Goal 7 chose.
- **The budget's tests came after the code in the cache's task** (no failing run recorded); the review checked by hand
  that each would fail without its behaviour, and the unlimited-budget case was fixed to set a zero budget first, as it
  passed vacuously before. The renderer's task recorded its failing run.
- **The final whole-branch review** (Opus) found only Minor issues, all fixed: the three lookups of the cache share one
  `Find` and one `Get` behind `Lay` and `TryLay`, so staleness can't differ between them; the turn order's test gained
  a label off the middle's height, which an order by width alone would pass; `EndFrame`'s clock is documented as the
  ending frame's; this Outcome's table took each worst frame's own layout count and the month step's own frame (it
  had counted the most layouts in any frame, and the labels zoom's first frame as the month step's). GLM (glm-5.3)
  found nothing.
- **The by-eye check moved from the renderer's task to the measurement**, as a subagent can't judge the screen.
