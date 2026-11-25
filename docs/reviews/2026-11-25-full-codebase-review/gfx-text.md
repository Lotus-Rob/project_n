# Review: gfx-text

**Scope:** src/gfx/labels.{hpp,cpp}, text_layout.{hpp,cpp}, text_place.{hpp,cpp}, distance_field.cpp, shelf_packer.hpp, hud_draw.hpp and any other text/glyph file in src/gfx. Consider Unicode/CJK, shaping, bidi, glyph cache growth. Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 24 standing (0 critical, 0 high, 0 medium, 13 low, 11 info), 0 raised but refuted on verification.

**Parts:** gfx-text.md, gfx-text-part2.md, gfx-text-part3.md

## Assessment

## gfx-text: assessment

**Overall health: good.** This is one of the more carefully built parts of the renderer. It has no critical defects and no outright memory-safety bugs. The design is sound for the game it serves today: Chinese and English, labels over a large living world.

### Strengths
- **Clear split.** `TextLayout` shapes, caches and rasterises on the CPU with no device. `text_place` is pure placement. `LabelPass` is pure declutter logic. All three are unit-tested without a GPU.
- **Distance fields.** Each glyph is rasterised once at 48 px base, uploaded incrementally and placed at sub-pixel positions with no rounding, which avoids text shimmer. The padding rationale for `GLYPH_PADDING` is written down.
- **Async rasterisation.** Done right: one worker batch at a time, low-priority jobs, a clean wait in the destructor, and a `landings` counter so incomplete layouts are remade lazily. The thread-safety annotations (`PN_REQUIRES`/`PN_GUARDED_BY`) are a plus.
- **Label pass.** Unusually well thought out: grid broad-phase, stack merging through the stack text's room, hysteresis on every decision, cross-fades, glides, kept-clear box. `labels_tests.cpp` (about 60 cases) covers the anti-flicker behaviour in depth.
- **Numbers.** Composed per character and placed in fixed chunks, so the output is byte-identical whatever the worker count, and a test pins that.
- **Lifetimes.** Reference and pointer lifetimes into `std::unordered_map` nodes are used correctly: node addresses survive a rehash, and erasures happen only at `EndFrame` or `Place`.

### Weaknesses
- **Robustness to odd input.** `WarnMissing` can loop forever on a text with an embedded NUL. No UTF-8 or control-character validation happens anywhere between YAML and the shaper. NaN anchors reach a float-to-size_t cast.
- **The layout budget leaks.** Stack texts with the real count are laid out without a budget at draw time, and `Capture` resets a custom budget.
- **Start-up cost.** Rasterisation reads each texel through `SDL_ReadSurfacePixel`, single-threaded on the main thread. Pre-warming the names takes "a second or two", by the repo's own account.
- **Scaling and localisation limits.**
  - One font (Noto Sans SC), no fallback chain.
  - The glyph cache is keyed by glyph index alone.
  - No script itemisation or bidi.
  - Numbers can't be formatted for a locale.
  - Glyph pages are never evicted or capped.
  - Two places pack language or direction into one bit with no `static_assert`.
  - HUD text is a fixed 18 target pixels, with no DPI or resolution scaling, which works against the accessibility-first goal.
- **Fragile SDL3_ttf dependency.** It relies on undocumented SDL3_ttf 3.2.2 behaviour: `TTF_TextData` internals, a bottom-to-top direction workaround for vertical text, and column-padding corrections. Tests pin these, so a version bump will fail loudly rather than silently. It is still upkeep debt.
- **Small duplication.** The placeholder fill exists three times (`Filled`, `StackText`, the test's `Stacked`), with slightly different meanings.
- **Test gaps.**
  - No case for NUL or invalid UTF-8 text.
  - No case for a glyph whose rasterisation fails, or one too big for a page, inside `TextLayout`.
  - No case for duplicate label entities or NaN anchors.
  - No case for drawing incomplete stack or number text.
  - The 5-second eviction test is weakened by its own `Cached()` probes, which refresh the timestamp.

### Risks
1. A mod or data typo with `"\0"` in `strings.yaml` hangs start-up.
2. Large crowds of named NPCs whose stack counts change could exceed the frame budget the layout budget was meant to protect.
3. A 4K or high-DPI display gets an unreadable HUD by default.
4. Adding a third language or a fallback font later needs several silent assumptions changed: the 1-bit language key, the glyph-index-only cache, the single-script shaping, and digit composition that assumes this font's tabular digits.
5. Glyph pages grow without bound once text is user- or procedurally generated.

**Files read in full:** src/gfx/text_layout.hpp, src/gfx/text_layout.cpp, src/gfx/text_place.hpp, src/gfx/text_place.cpp, src/gfx/labels.hpp, src/gfx/labels.cpp, src/gfx/shelf_packer.hpp, src/gfx/shelf_packer.cpp, src/gfx/distance_field.hpp, src/gfx/distance_field.cpp, src/gfx/hud_draw.hpp, src/gfx/renderer.cpp (text-related parts: CreateGlyphPage, Impl members, Upload, MakeGlyphPages, TextOf, PlaceTexts, GatherLabels, TakeTurns, StackOf, PlaceLabel, LabelOf, PlaceText/Layout/At, PlaceHudLines, UploadGlyphs, Submitted, DrawWorld text draw, Create, SetNames, HudLineWidth, Capture), src/data/strings.cpp, tests/unit/gfx/text_layout_tests.cpp, tests/unit/gfx/shelf_packer_tests.cpp, tests/unit/gfx/labels_tests.cpp (case list), tests/unit/gfx/text_place_tests.cpp (case list), tests/unit/gfx/number_place_tests.cpp (case list), tests/unit/gfx/distance_field_tests.cpp (case list), src/AGENTS.md, tests/AGENTS.md

## Findings

### GFX-TEXT-01: WarnMissing loops forever on a text containing an embedded NUL

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** robustness/hang · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/text_layout.cpp:343`

WarnMissing walks the text with `while (left > 0) { const u32 character = SDL_StepUTF8(&next, &left); ... }`. SDL_StepUTF8 is documented to return 0 at a null terminator without moving the pointer or reducing the length. A std::string_view holding a '\0' byte therefore never makes progress, and the loop spins on the main thread. Nothing upstream rejects NUL or control characters: ParseStrings accepts any non-empty scalar, and a YAML double-quoted "\0" escape yields a NUL. names.yaml goes through the same kind of path.

**Evidence:** text_layout.cpp:344-352; data/strings.cpp:59-62 only checks `text.Scalar().empty()`. Prewarm() calls Shape() -> WarnMissing() on every string at Renderer::Create.

**Impact:** A typo or a mod putting "\0" in strings.yaml or names.yaml hangs start-up inside Prewarm, with no log line, because the warning is never reached. The same applies to any later runtime text. Mods and data-driven content are planned.

**Recommendation:** If SDL_StepUTF8 returns 0 while `left > 0`, skip one byte (`++next; --left;`) or break. Better still, reject NUL and control characters, and invalid UTF-8, in data::ParseStrings and ParseNames with a line-numbered error. Add a TextLayout test with an embedded NUL and one with invalid UTF-8.

**Verification:** The loop at text_layout.cpp:343-352 is as described. The SDL 3.4.16 header (SDL_stdinc.h, SDL_StepUTF8 docs) says a zero codepoint does not advance *pstr or *pslen, so an embedded NUL spins forever. data/strings.cpp:59 only rejects an empty scalar, and a YAML "\0" escape can produce a NUL. I found no test with a NUL or invalid UTF-8. Reaching it needs bad data, so I put it at low, though the hang is silent.

### GFX-TEXT-02: Stack labels' real-count text is laid out without a budget at draw time, bypassing LAYOUT_BUDGET_NS

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** performance/frame-budget · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/renderer.cpp:1001`

GatherLabels/TakeTurns measure only the widest-count stack text ("Name (+99)") within the TryLay budget. PlaceLabel then draws the real count: `StackText(stacked, ..., says, count)` followed by `text->Lay(says, ...)`, which is Asking::Always. Each new name/count pair is shaped by HarfBuzz there, without a budget. During a cross-fade it does this for both `placed.previous` and `placed.count`. Every distinct string stays cached for 5 s.

**Evidence:** renderer.cpp:989-1006 (PlaceLabel), called twice per placed label from PlaceTexts. TextLayout::Lay -> Get(..., Asking::Always) makes regardless of `spent`.

**Impact:** On the world map, crowds of named NPCs (unique names) form stacks whose counts change as travellers come and go. Each change shapes one or two new strings in that frame, unbounded. That is exactly the hitch the budget was added to prevent. The layout cache also churns with short-lived count strings.

**Recommendation:** Measure and draw the same text. Either draw the stack with a budgeted TryLay and fall back to the previous count's cached layout, as the width measuring already does, or compose the "(+N)" suffix from cached per-digit layouts, as numbers do with NumberFace, and append it to the name's layout. Add a test that a frame with N new stack counts makes at most the budgeted number of layouts.

**Verification:** PlaceLabel calls text->Lay(), and the text_layout.hpp docs say Lay() never counts against the budget. The real-count text is therefore shaped unbudgeted at draw time, as claimed. Impact is smaller than stated. PrewarmNames rasterises the stack text with every digit, so only HarfBuzz shaping of the name and count is paid, with no glyph rasterisation. Lay() also leaves out glyphs that are not rasterised yet and asks the worker for them. Cache churn from count strings is plausible. I found no test of the budget with stack counts.

### GFX-TEXT-03: Glyph rasterisation reads every texel through SDL_ReadSurfacePixel, on the main thread at start-up

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** performance/startup · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/text_layout.cpp:145`

Rasterise() copies the SDF surface one texel at a time with SDL_ReadSurfacePixel, which does a format lookup and a pixel unpack per call: about 4,000 calls for a 64x64 field. Prewarm and PrewarmNames rasterise every glyph synchronously on the main thread, one at a time, under one font. tests/AGENTS.md notes that composing every name's glyphs ahead "takes a second or two".

**Evidence:** text_layout.cpp:145-154 nested loops calling SDL_ReadSurfacePixel; Warm() at 490-493 is serial; Renderer::Create calls Prewarm and App calls SetNames -> PrewarmNames before the first frame.

**Impact:** Start-up pause grows with the glyph count (about 400 CJK characters now, more with more names, places or languages). Part of that time is pure copying overhead.

**Recommendation:** Check `image->format`; SDL3_ttf SDF glyphs are 32-bit ARGB. Then copy the alpha byte straight from `image->pixels` row by row, honouring the pitch. Consider rasterising Prewarm's glyphs on several workers, each with its own TTF_Font (open and close faces serialised, as FreeType requires), then Place them in order on the main thread. Log the split between rasterising and placing.

**Verification:** Rasterise() copies texel by texel through SDL_ReadSurfacePixel (lines 145-154). The cost is real but only at start-up. text_layout.hpp documents Prewarm as the designed one-off pause, so this is an optimisation opportunity. I did not check the surface pixel format.

### GFX-TEXT-04: HUD text is a fixed 18 target pixels with no DPI or resolution scaling

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** accessibility · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/hud_draw.hpp:18`

HUD_PIXELS = 18 target pixels, multiplied only by the user's text scale. Nothing in src queries the display scale or pixel density (no SDL_GetWindowDisplayScale or PixelDensity anywhere). World text scales with the window through PixelsPerUnit, but the HUD and its effects.yaml insets are in raw target pixels.

**Evidence:** renderer.cpp:298 and 1070 use `HUD_PIXELS * textOptions.scale`; a grep for DisplayScale, PixelDensity, ContentScale or dpi in src finds nothing.

**Impact:** On a 4K or high-DPI display the HUD (bars' words, cooldowns, the clickable date line) comes out a fraction of its intended physical size, and its click area shrinks with it. The memory rule 'accessibility first' makes a readable default important.

**Recommendation:** Scale HUD pixels by the display's content scale, or by target height / 1080, times the user's text scale, and apply the same factor to effects.yaml's HUD layout. Re-measure the date-line hit box with it.

**Verification:** HUD_PIXELS = 18 is a fixed target-pixel size, multiplied only by TextOptions::scale. A grep of src for DisplayScale, PixelDensity and ContentScale finds nothing, and I found no design doc recording this as deliberate. The user-adjustable text scale softens the accessibility impact. I did not check how large the target is relative to the window, so the high-DPI effect is not verified. Medium is too high.

### GFX-TEXT-05: Two cache keys pack language/direction into one bit with no static_assert

- **Severity:** low · **Verdict:** confirmed · **Category:** latent-bug · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/text_place.cpp:193`

NameTexts::Of builds its key as `(u64{NameKey} << 1) | language`; renderer.cpp's measuredNames does the same with direction. Both rely on core::LANGUAGES == 2 and two TextDirections. A third language (value 2) sets bit 1, so its key collides with a different name's key in language 0.

**Evidence:** text_place.cpp:193; renderer.cpp StackOf `(u64{ core::NameKey(*item.npcName) } << 1U) | std::to_underlying(item.direction)`; core/strings.hpp:141 LANGUAGES = 2.

**Impact:** When a language is added, labels could silently show another NPC's name.

**Recommendation:** Shift by a width computed from LANGUAGES, or put a `static_assert(core::LANGUAGES <= 2)` with a reason next to each packing.

**Verification:** Line 193 packs `(NameKey << 1) | language` with no static_assert on LANGUAGES, which is 2 at core/strings.hpp:141. The renderer's StackOf packs direction the same way. A third language would collide, but it is latent, and a static_assert would be a trivial fix. I did not verify the renderer line.

### GFX-TEXT-06: LabelPass assumes unique entities but neither asserts nor handles duplicates (entity 0 is legal in TextItem)

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/labels.cpp:77`

Begin() maps each label to `m_seen[entity]`. Two labels with the same StableId (two labels on one entity, or two labels with entity 0, which TextItem documents as 'none') would share one Seen. Its index is overwritten, it is faded twice per frame, and Glide reads the wrong label's anchor. The header states the precondition ('each label names a different one'), but nothing checks it.

**Evidence:** labels.cpp:76-83; core/frame.hpp:252 'zero for none (a caption)'.

**Impact:** A scene that publishes two labels for one entity, or an unset entity on a label, makes them flicker and fade at double speed, and one of them is drawn at the wrong anchor. This is hard to diagnose.

**Recommendation:** PN_ASSERT in Begin that try_emplace created the entry or that its frame != m_frame, and that entity != StableId{} for a Label/Place. Add a test.

**Verification:** Begin() does use try_emplace on labels[index].entity, so two labels with the same entity share one Seen entry. The header states the uniqueness precondition. Nothing asserts it, and I did not check whether any caller can publish entity 0 or a duplicate. This is an assert-hardening suggestion, not an observed bug.

### GFX-TEXT-07: NaN anchors reach a float-to-size_t cast (UB) in the label grid

- **Severity:** low · **Verdict:** confirmed · **Category:** undefined-behaviour · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/labels.cpp:35`

CellOf does `static_cast<std::size_t>(std::clamp(std::floor(pixels / LABEL_GRID_PIXELS), 0.0F, cells - 1))`. std::clamp with NaN returns NaN, and casting NaN to an integer is UB. Anchors come from snapshot positions times the camera transform. A NaN position (a degenerate camera, a zero-size window, a bad interpolation) would give NaN boxes.

**Evidence:** labels.cpp:34-36; Resolve's off-screen test compares with `<=`/`>=`, which are all false for NaN, so a NaN box is treated as on screen and reaches Cells().

**Impact:** UB, possibly a huge index into m_cells and an out-of-bounds write in Cover. Unlikely in normal play.

**Recommendation:** Treat any non-finite box as off-screen in Resolve (`!std::isfinite(...)`), or make CellOf map NaN to 0.

**Verification:** CellOf (labels.cpp:34-36) uses clamp then a float-to-size_t cast. Resolve's off-screen test (lines 120-121) uses <=/>= so a NaN box is not off-screen and reaches the grid. A NaN input is unlikely, but the UB path exists as described. No guard found.

### GFX-TEXT-08: Incomplete number and stack-count layouts are drawn with gaps

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** correctness/visual · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/text_place.cpp:143`

ComposeNumber computes `complete`, but PlaceNumberChunk ignores it. PlaceLabel likewise draws the real-count stack text via Lay without checking `complete`, while captions (CaptionFades) and label names (LabelInput::complete) are held back until complete. Pre-warm covers the usual glyphs, but a digit shaped in a new context, which the code itself says HarfBuzz can do, would draw with a hole.

**Evidence:** text_place.cpp:143-151 never reads number.complete; renderer.cpp:989-1006.

**Impact:** Rare visible gaps in damage numbers or stack counts. It contradicts the stated goal that text 'never draws with gaps'.

**Recommendation:** Skip, or hold back, numbers whose face isn't complete (cheap: check the face once per frame), and draw a stack's previous count until the new text is complete.

**Verification:** ComposeNumber computes complete (text_place.cpp:281), and the PlaceNumberChunk code at 143-151 never reads it. So the omission is real. Pre-warm covers digits, which makes a gap rare. Didn't check the stack-count path or the claim that HarfBuzz shapes a digit differently in a new context.

### GFX-TEXT-09: A glyph whose rasterisation fails is marked Missing forever

- **Severity:** low · **Verdict:** confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/text_layout.cpp:396`

Place() sets GlyphState::Missing whenever the field is empty, which includes a transient failure such as an allocation failure in TTF_GetGlyphImageForIndex. The glyph is never retried for the rest of the session. Rasterise also ignores the returned TTF_ImageType: a colour (COLR/bitmap) glyph would be uploaded as if its alpha were a distance field.

**Evidence:** text_layout.cpp:135-141 (type unused), 396-399.

**Impact:** A one-off failure removes that character from every text until restart. A colour font would draw wrong. Low likelihood with the current font.

**Recommendation:** Reject types other than TTF_IMAGE_SDF with a warning. Optionally let Missing glyphs retry after a back-off.

**Verification:** Place sets state Missing before the empty-texels return (lines 392-396), so a failed rasterisation is never retried. Rasterise (lines 134-141) declares `type` and never checks it. Both claims hold. The impact is small with the current SDF font.

### GFX-TEXT-10: Placeholder filling is implemented three times with different meanings

- **Severity:** low · **Verdict:** confirmed · **Category:** duplication · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/text_layout.cpp:59`

Filled() in text_layout.cpp substitutes {name} first, then searches the result for {count}. labels.cpp StackText finds both placeholders in the template first, so a name containing "{count}" is left alone. text_layout_tests.cpp has a third copy, Stacked(), which assumes both placeholders exist. Pre-warm may therefore shape a slightly different string from the one drawn.

**Evidence:** text_layout.cpp:59-67 vs labels.cpp:531-559 vs tests/unit/gfx/text_layout_tests.cpp:55-61.

**Impact:** Upkeep risk. Pre-warm coverage can quietly drift from what is drawn when the stack template changes.

**Recommendation:** Use gfx::StackText in PrewarmTexts and PrewarmNameTexts (with a count placeholder string, e.g. via an overload taking a string_view count), and in the tests.

**Verification:** Filled (text_layout.cpp:59-67) replaces {name} first, then searches the result for {count}. labels.cpp has its own StackText (531-563). The orders differ, so a name containing {count} would be handled differently. The tests copy was not opened. It is an upkeep risk only.
