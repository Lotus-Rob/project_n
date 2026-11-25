# Review: gfx-text (part 2)

### GFX-TEXT-11: DistanceField indexes the rgba span with no size check

- **Severity:** low · **Verdict:** confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/distance_field.cpp:112`

`rgba[(row * pitch) + (column * BYTES_PER_TEXEL) + ALPHA_OFFSET]` uses span::operator[] with no assertion that rgba.size() >= (height-1)*pitch + width*4. The only caller passes pitch*h from an SDL surface, but the function is public and documented as pure.

**Evidence:** distance_field.cpp:110-113 (file line 232 in the concatenated listing).

**Impact:** A caller mismatch reads out of bounds silently in release builds.

**Recommendation:** PN_ASSERT the minimum size at entry.

**Verification:** rgba[(row*pitch)+(column*4)+ALPHA_OFFSET] has no size assertion at entry, and the function is public. The only caller passes an SDL surface's own pitch and size, so there is no reachable OOB today. A PN_ASSERT would be cheap hardening.

### GFX-TEXT-12: The layout eviction test is masked by its own probes

- **Severity:** low · **Verdict:** confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/gfx/text_layout_tests.cpp:507`

'a layout unused for five seconds is forgotten' lays out at clock 0, then probes with Cached() at 4.0 and 8.9. Cached() counts as asked for and stamps lastUsed. Without the probe at 4.0, the layout would already be gone at 8.9 (0 < 3.9). So the test measures five seconds from the last probe, not from the Lay, and could not catch an eviction measured from the wrong timestamp.

**Evidence:** text_layout_tests.cpp:507-517; text_layout.cpp:561-568 (Cached stamps lastUsed), 580.

**Impact:** A regression in eviction timing, e.g. stamping with the wrong clock, would pass.

**Recommendation:** Probe only after the final EndFrame, or add a non-stamping query for tests. Check the boundary case explicitly: kept at last use + 5 s, gone just after.

**Verification:** Traced the test. Lay stamps lastUsed=0. EndFrame(4.0) keeps it (0 < -1 is false). Cached() at 4.0 then stamps 4.0. At EndFrame(8.9), 4.0 < 3.9 is false, so it is kept; at 14.0, 4.0 < 9 is true, so it is gone. Without the 4.0 probe the layout would already be gone at 8.9. So the test measures from the probe, not from the Lay, and an eviction timed from the wrong stamp could slip through.

### GFX-TEXT-13: Missing tests for text failure paths and label invariants

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/gfx/text_layout_tests.cpp:440`

No case covers:
- a text with an embedded NUL or invalid UTF-8 (the hang above);
- a glyph whose rasterisation fails, or is too large for a page, going Missing and the layout becoming complete without it;
- a stale incomplete layout seen through Cached() versus remade by TryLay;
- LabelPass with duplicate entities or NaN anchors;
- the budget across the renderer's draw-time Lay calls.
The labels tests are otherwise thorough.

**Evidence:** Case lists of text_layout_tests.cpp, labels_tests.cpp, text_place_tests.cpp and number_place_tests.cpp.

**Impact:** The defects reported above would not be caught by the current suites.

**Recommendation:** Add the small cases above. For rasterisation failure, a test-only hook, or a field font fed a glyph index past the face's count, would exercise Missing.

**Verification:** Real gap: grep of text_layout_tests.cpp finds no NUL or invalid-UTF-8 case and no rasterisation-failure case. A case for a character the font lacks does exist (line 440), and the stale-incomplete behaviour is covered around lines 495-505 (TryLay). The cited 'hang above' refers to another finding I could not check here, so that part is unverifiable. The list is a mix of valid gaps and speculative ones (duplicate entities, NaN anchors).

### GFX-TEXT-14: Glyph cache and pages grow without bound; no eviction, cap or warning

- **Severity:** info (reviewer said low) · **Verdict:** confirmed · **Category:** scaling/memory · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/shelf_packer.hpp:30`

ShelfPacker never removes anything and TextLayout never forgets a glyph. Each new page is another 2048x2048 R8 texture (4 MiB) made by MakeGlyphPages with no upper limit. Text drawn from new sources (more names, procedural place names, a third language, mod strings, debug UI) only ever adds pages.

**Evidence:** shelf_packer.hpp:28-29 'Nothing is ever removed'; renderer.cpp:740-746 creates pages while `glyphPages.size() < text->Pages()`.

**Impact:** Today's roughly 400 CJK glyphs fit on one or two pages. With procedural world text, or full CJK coverage of about 20k glyphs, the result is around 25+ pages (100+ MiB of VRAM) and more page switches, each splitting a draw run.

**Recommendation:** Log once when the page count passes a threshold. Plan least-recently-used page eviction, or a texture array with a fixed layer count, before text becomes procedural or user-generated. Note the limit in the TextLayout docs.

**Verification:** The header says nothing is ever removed, and pages are created without a cap. This is accurate, but the text set today is fixed and prewarmed, so it is speculative scaling. The page-count logging suggestion is cheap.

### GFX-TEXT-15: Single font, glyph cache keyed by glyph index only, no script itemisation or bidi

- **Severity:** info (reviewer said low) · **Verdict:** confirmed · **Category:** localisation/design · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/text_layout.cpp:269`

The cache maps a bare glyph index to a field (`std::unordered_map<u32, CachedGlyph> glyphs`), so it assumes one font forever. A fallback font (TTF_AddFallbackFont) would produce copy ops whose glyph_index refers to another face, and they would collide. Text is shaped as one run with no script or language set, so HarfBuzz guesses one script for mixed Han+Latin strings; a comment already notes digits shaping differently after Latin. There is no RTL/bidi or complex-script support, and NotoSansSC lacks e.g. Hangul completeness, Arabic and Devanagari. ComposeNumber also bakes in this font's tabular digits and lack of kerning, and numbers can't take locale grouping.

**Evidence:** text_layout.cpp:19 FONT_FILE, 269 glyph map, 296-301 TTF_CreateText with no TTF_SetTextScript or language; text_place.hpp:126-128 relies on the font's tabular digits.

**Impact:** Adding a language beyond zh/en, or any fallback font, needs a cache-key change (font, glyph), script runs and number formatting. Done naively, it gives wrong glyphs rather than a clear error.

**Recommendation:** Key the cache by (font id, glyph index) now; it costs little. Record in the localisation plan that script itemisation, bidi, a font fallback chain and locale number formatting are needed, and that NumberFace's assumptions must be checked per font.

**Verification:** The glyph map is keyed by glyph index alone and shaping uses a single font with no script or bidi handling. Core::LANGUAGES is 2 (en, zh). The facts hold, but this is future localisation work, not a present defect. Changing the key to (font, glyph) would be cheap.

### GFX-TEXT-16: Capture resets the layout budget to the default instead of restoring the previous one

- **Severity:** info (reviewer said low) · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/renderer.cpp:1701`

Capture sets SetLayoutBudget(max), then SetLayoutBudget(LAYOUT_BUDGET_NS). TextLayout::SetLayoutBudget is public and documented as 'from now on', but the renderer never records the budget it set, so a custom budget is lost after any capture.

**Evidence:** renderer.cpp:1699-1701.

**Impact:** Small today, since only Capture changes the budget. Any future tuning hook (a debug UI, measure) would be silently reset by a screenshot or reference capture.

**Recommendation:** Add a getter, or a scoped guard that restores the previous budget.

**Verification:** Capture sets the budget to u64 max, then to LAYOUT_BUDGET_NS (renderer.cpp:1699-1701) without saving the old value. This is true, but nothing else changes the budget today, so it is latent only.

### GFX-TEXT-17: Vertical text and layout boxes depend on undocumented SDL3_ttf 3.2.2 behaviour

- **Severity:** info · **Verdict:** unverifiable · **Category:** dependency-risk · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/text_layout.cpp:297`

Top-to-bottom text asks for TTF_DIRECTION_BTT to undo a sign bug in SDL3_ttf 3.2.2. Column tops are recomputed because 3.2.2 pads columns shorter than three characters. Shaping uses a separate non-SDF font because 3.2.2 offsets SDF glyphs. The code also reads `laid->internal` (TTF_TextData from SDL_textengine.h).

**Evidence:** text_layout.cpp:196-199, 297-301, 314-332, 306.

**Impact:** A pin bump that fixes any of these upstream bugs would flip or shift vertical captions. The tests 'a vertical caption runs down, its first character on top' and 'a lone character's column is its outline's height' should catch it, so this is upkeep cost rather than a silent break.

**Recommendation:** Note these workarounds in the bump-toolchain/add-dependency checklist for SDL3_ttf, and report the bugs upstream (fix-xmake-package workflow).

**Verification:** Nothing in docs/design, the skill files or an AGENTS.md was found that records these SDL3_ttf workarounds. I did not open text_layout.cpp lines 196-332, so the claims about the specific upstream bugs are unchecked. Whether a version bump would break them can only be settled by bumping SDL3_ttf and running the tests.

### GFX-TEXT-18: Tiny fading labels can sample neighbouring glyph fields

- **Severity:** info (reviewer said low) · **Verdict:** unverifiable · **Category:** rendering · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/gfx/text_layout.cpp:38`

GLYPH_PADDING (9 texels) is justified for text drawn at about 2.2 px or more, on the grounds that smaller text is 'faded out or never drawn that small'. TextPixels puts no floor on labels or place names. A label fading out (0.15 s), or a gone label still fading, is drawn at any size. In a small window at zoom 0.25, a label is 20 x 0.25 x (360/1080) ≈ 1.7 px, where the 0.375 px supersampling plus the shadow shift reaches beyond 9 texels.

**Evidence:** text_layout.cpp:33-38 comment; text_place.cpp:170-182; labels.cpp Fade pushes labels with opacity > 0 regardless of size.

**Impact:** Faint ghost fragments of a neighbouring glyph in fading labels in small windows. Cosmetic.

**Recommendation:** Skip drawing text below about 2.2 px (or clamp the supersampling offset), or widen the padding. Add the case to the padding comment's assumptions.

**Verification:** Not examined. The claim depends on numeric text-size and padding arithmetic and on the real minimum label size, which reading alone doesn't settle. The reviewer also gave it low confidence. A render at zoom 0.25 in a small window would decide it.

### GFX-TEXT-19: Worker request queue pops from the front of a vector (O(n^2))

- **Severity:** info (reviewer said low) · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/text_layout.cpp:180`

RasteriseRequests takes `requests.front()`, then `requests.erase(requests.begin())`, holding queueMutex. A large batch of new glyphs (a new language, a long modded string) costs quadratic shifting, and the main thread's Exchange contends on the lock meanwhile.

**Evidence:** text_layout.cpp:176-181.

**Impact:** Negligible today. It matters only with thousands of runtime glyphs.

**Recommendation:** Swap the whole request vector out under the lock and iterate it locally, or use a std::deque or a head index.

**Verification:** requests.front() followed by erase(begin()) under queueMutex (text_layout.cpp:176-181) is O(n) per pop. The quadratic cost is real but irrelevant at the current glyph counts.

### GFX-TEXT-20: Layout contents can change under a held reference if Flush() is called mid-frame

- **Severity:** info · **Verdict:** confirmed · **Category:** api-contract · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/text_layout.cpp:534`

Get() remakes a stale cached layout in place (`cached->second.layout = Make(...)`), which reallocates its glyph vector. That is safe only because `landings` changes only in Exchange (EndFrame or Flush). Flush is public. A caller holding a `const Layout&` from Lay, or a NumberFace pointer, across a Flush and another Lay would see its glyphs span reallocated. The documentation says only that the reference holds 'until the next EndFrame()'.

**Evidence:** text_layout.cpp:520-547, 584-591; text_layout.hpp:127, 153-156.

**Impact:** No current caller does this (Capture flushes before Upload). It is a trap for future callers.

**Recommendation:** Document that Flush() also ends the validity of earlier layout references, or PN_ASSERT that Flush is not called between Lay and EndFrame.

**Verification:** Get() assigns `cached->second.layout = Make(...)` in place, only for stale incomplete layouts, and `landings` changes only in Exchange (EndFrame or Flush). Flush is public, and the doc for Lay says 'holds until the next EndFrame()'. The only non-test Flush caller is renderer.cpp:1670 (capture), so no live bug. Trap is real but small.
