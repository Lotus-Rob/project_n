# Review: x-text-l10n

**Scope:** CROSS-CUTTING text and localisation: strings tables (assets/data/strings.yaml, src/core/strings.*, src/data/strings.*), hard-coded user-facing text in src/, name generation, fonts shipped (assets/fonts) and their glyph coverage and licences, readiness for CJK and other scripts in a wuxia game, number/date formatting, text overflow handling.

**Findings:** 18 standing (0 critical, 0 high, 3 medium, 11 low, 4 info), 0 raised but refuted on verification.

**Parts:** x-text-l10n.md, x-text-l10n-part2.md

## Assessment

## Assessment: text and localisation (x-text-l10n)

**Scope read:** the strings and names code and data, the font and its provenance, the text pipeline (`TextLayout`), the HUD text building, and the tests that touch them. I ran no builds. I did run read-only Python checks that every non-ASCII character in `strings.yaml` and `names.yaml` is in the shipped font's cmap (all are).

**Strengths**
- The shipped text is coherent. Every named string has both languages, and a parser test rejects a missing one.
- The parser reports line numbers, rejects duplicate keys and refuses a table past the cap.
- Unknown languages and keys are only warned about.
- Names are never stored. `NameOf` is a pure hash of the seed and the stable ID. English is composed from the pinyin with the apostrophe rule, and `NameKey` bit-packing is consistent with `MOST_NAMES`.
- `Names::Hash` covers the table, so replay and game-data hashing see it.
- `TextLayout` is carefully engineered: SDF glyph cache, worker rasterisation, a per-frame layout budget, and prewarm so common text never waits.
- The font is OFL-licensed with a provenance and SHA-256 record. The Windows path handling in `platform/paths.cpp` is correct.
- Number formatting through `std::format("{}")` is locale-independent, which is what you want for the simulation.

**Weaknesses and risks**
1. **A design promise is missing.** Design doc 0002.4 §3 says a test checks every string in `assets/data/` against the font. That test does not exist. `ttf_tests.cpp` checks only 修 and A. A missing glyph is invisible: `glyph_index != 0` is skipped, so a missing glyph draws nothing, with only a one-time log warning.
2. **One font with no fallback.** The font is a Simplified-Chinese region subset. It has no Traditional-only or Japanese or Korean glyphs, and no other scripts. The design intended `TTF_AddFallbackFont`, and the code does not use it.
3. **The font path can break on Windows.** `.string()` converts to the ANSI code page, but `TTF_OpenFont` expects UTF-8. A non-ASCII install or user path breaks the font load.
4. **The localisation model is hard-coded to two languages.**
   - `Language`, `LANGUAGES` and `LANGUAGE_CODES` are fixed, and `Strings::text` is an `array<string, LANGUAGES>`.
   - The parser's known-language list `{"en","zh"}` is a second copy of the language codes.
   - Names exist only as zh plus pinyin.
   - There are no plural, gender, ordering or locale-aware number rules (万 grouping, say), and no OS-locale detection.
   - All of this is partly recorded as deferred.
5. **Placeholders are not validated.** A translated string can drop `{value}` or `{count}` and nothing complains. Three separate fillers exist (`core::Fill`, `gfx::StackText`, `Filled` in `text_layout.cpp`) with slightly different semantics.
6. **Accessibility gap in the HUD.** Control names are fixed to Left click, Right click and Space, and the digit keys "1" to "5". There is no gamepad glyph or name even though gamepads are supported.
7. **No text overflow handling.** There is no wrapping, truncation or max width in labels or the HUD. Only the date line widens its box.
8. **Prewarm and the glyph cache will not scale to content-heavy text.** Prewarm shapes every string in both directions plus combined variants. `Warm` uses a linear `ranges::contains` on `unseen` per glyph occurrence, which is O(occurrences × unique glyphs). The glyph pages never evict. A real wuxia CJK corpus of several thousand glyphs, plus dialogue, will cost both startup time and VRAM.
9. **Procedural content has no naming path.** Place names come only from a handwritten table keyed `place_<key>`. A procedural world needs generated place, sect and technique names, and there is none, with no collision handling for people names either.

**Overall health:** good for the current scope (a two-language HUD, name labels and place names). The runtime is a well-built text core, and the main risks are future scale and the missing guard tests, not current correctness.

**Files read in full:** (not reported)

## Findings

### X-TEXT-L10N-01: Promised font-coverage test for all shipped strings does not exist

- **Severity:** medium · **Verdict:** confirmed · **Category:** missing-tests · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/gfx/ttf_tests.cpp:41`

docs/design/0002.4-ui-and-text.md section 3 says a test checks every string in assets/data/ and assets/ui/ against the font via TTF_FontHasGlyph so a missing character fails before it ships. The only test checks two codepoints (修 and A). Names, places and strings are never checked. At runtime TextLayout::Shape skips glyph_index 0, so a missing character draws nothing and only logs a warning once.

**Impact:** A new or edited string or name using a character outside Noto Sans SC subset will silently render as nothing (blank hole in a name) and pass CI. This gets likelier with Traditional, rare-hanzi, or imported names.

**Recommendation:** Add a test that loads strings.yaml and names.yaml (both languages, including pinyin and the apostrophe) and asserts TTF_FontHasGlyph for every codepoint. Alternatively draw the notdef box or a fallback glyph for glyph 0 so the problem is visible.

**Verification:** Design 0002.4 lines 90-91 promise a test checking every string in assets/data and assets/ui via TTF_FontHasGlyph. grep finds TTF_FontHasGlyph only in ttf_tests.cpp (XIU, LATIN_A) and the runtime WarnMissing at text_layout.cpp:348. No such test exists. The promise is unmet, though the font is a broad SC subset so near-term risk is modest.

### X-TEXT-L10N-02: Font path built with .string(): breaks on Windows with non-ASCII install path

- **Severity:** medium · **Verdict:** confirmed · **Category:** portability · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/text_layout.cpp:452`

TextLayout::Open does `(fonts / FONT_FILE).string()` and passes it to TTF_OpenFont, which expects UTF-8. On Windows path::string() yields the ANSI code page. platform/paths.cpp explicitly builds the path from UTF-8 (FromUtf8) to avoid exactly this, but the conversion back is not done. The same pattern is in ttf_tests.cpp.

**Impact:** A Windows user whose profile or install folder has Chinese or other non-ASCII characters (very likely for the genre's audience) gets 'the font wasn't opened' and the game fails to start, or at best mojibake path lookup.

**Recommendation:** Use `path.u8string()` (convert char8_t to char) or SDL's path APIs for TTF_OpenFont; add a test with a non-ASCII temp folder on Windows CI.

**Verification:** Line 452 uses (fonts / FONT_FILE).string() and passes it to TTF_OpenFont. paths.cpp FromUtf8 builds paths from UTF-8, so the reverse conversion is missing. On Windows, string() gives the ANSI code page, so a non-ASCII install path can fail. The assets dir comes from the executable's location, so this can happen. The impact depends on the Windows runtime, so it is not directly verifiable here. Medium is fair.

### X-TEXT-L10N-03: HUD control names hard-code mouse and keyboard, no gamepad or rebinding text

- **Severity:** medium · **Verdict:** partially-confirmed · **Category:** accessibility · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/hud.cpp:15`

KeyOf maps slots to the strings Left click, Right click and Space, and the specials/ultimate use fixed digit keys "1" to "5" (DIGIT_KEYS). The HUD shows the same prompts whatever device was used last, although 0002.4 section 2/10 plans device-specific prompts and gamepad is a first-class input. The slot line layout (control, ability, seconds) is fixed concatenation with a fixed two-space separator.

**Impact:** A gamepad user (accessibility is the stated top priority) is told to Left click. Once bindings are rebindable the labels will be wrong. Word order and spacing cannot be changed by a translator.

**Recommendation:** Make control names come from the binding plus device (a prompt string table per device family); build each slot line from one translatable template with named placeholders.

**Verification:** hud.cpp:15 hard-codes DIGIT_KEYS and line 155 builds control + SEPARATOR + ability, so the fixed concatenation holds. The HUD shows mouse and keyboard names regardless of device. The backlog (line 92) records that gamepad casts aim at the mouse, with a design needed, so gamepad support is incomplete and tracked. Control prompts per device are a real accessibility gap, but not recorded as a separate item. Medium stands.

### X-TEXT-L10N-04: Single CJK-SC font, no fallback chain

- **Severity:** low (reviewer said medium) · **Verdict:** deliberate · **Category:** localisation-readiness · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/gfx/text_layout.cpp:19`

FONT_FILE is one Noto Sans SC region subset (Sans/SubsetOTF/SC). Design 0002.4 section 5 planned TTF_AddFallbackFont, but none is used. The font lacks Traditional-only, Japanese and Korean forms and any non-CJK script (Arabic, Thai, Devanagari). Only Regular weight ships, so no bold for headings. No TTF_SetFontLanguage or script is set either, so Han-unification variants depend on the font default (SC).

**Impact:** Adding zh-Hant, ja, ko or any player-entered or modded text will give missing glyphs (invisible). Rare characters in procedurally composed names have no recourse.

**Recommendation:** Decide the supported-script set; add fallback fonts and a regional-variant strategy (full Noto CJK or per-locale subsets), set the font language per locale, and make glyph 0 visible in debug builds.

**Verification:** The single font is a known gap. The backlog 'Localisation to industry practice' item lists font and glyph coverage per locale, BCP 47 codes and a fallback chain as to-do. Only zh and en ship, so nothing is broken today. Design 0002.4 section 5 planned fallback fonts. The claim is true but deferred, so low.

### X-TEXT-L10N-05: Missing glyph is silently skipped (glyph_index 0), notdef never drawn

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/text_layout.cpp:310`

The `draws` predicate drops operations with glyph_index == 0 and relies on WarnMissing logging once per codepoint (tracked in a vector with linear contains).

**Impact:** In the shipped game a missing character is invisible rather than a tofu box. A name loses a character with no on-screen clue. The one-time warning is easy to miss.

**Recommendation:** Draw the notdef glyph (or a fallback) at least in debug builds, and make the missing-character check a test instead of a runtime-only log.

**Verification:** The code comment says glyph 0 is the font's box and is skipped, and WarnMissing logs once per character (line 348, via ranges::contains on a vector). So it is invisible but warned. Design 0002.4 line 90 specifies the warning, not drawing a box. The test gap is finding 0, so this mostly duplicates it. Low.

### X-TEXT-L10N-06: String placeholders are not validated and three different fill implementations exist

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** duplication · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/core/strings.cpp:14`

core::Fill (FillValue/FillDate) replaces every occurrence of two placeholders in one pass; gfx::StackText (labels.cpp 531) replaces only the first of each and silently omits a missing placeholder; text_layout.cpp's Filled() does sequential first-occurrence replace with different semantics, and may substitute inside the substituted text. ParseStrings checks only that both languages exist and are non-empty: it never verifies that a string contains the placeholders its code fills, or that no unknown {tokens} are present. StackText's header says a missing placeholder is 'left out, so a bad strings table can't throw'.

**Impact:** A translation that omits {count} or {value}, or typos {Max}, loads fine and shows wrong or incomplete text (a stack label with no count; a health bar with no numbers). Behaviour differs depending on which of the three paths a string takes. Adding the ordinals, plurals or reordered placeholders needed by other languages will multiply this.

**Recommendation:** Give each filled StringId a declared placeholder set and validate it at load (error naming the line). Unify on one fill routine. Consider a real message format (ICU MessageFormat, Fluent) for plurals and gender when more languages arrive.

**Verification:** Real duplication: labels.cpp StackText (line 531) and text_layout.cpp Filled (line 59) both find only the first occurrence of each placeholder. StackText is documented to leave out a missing placeholder and does not substitute inside substituted text. Filled does substitute sequentially, so a name containing {count} could be replaced. That path only fills prewarm texts, where name is empty or a table string. Placeholder validation at load is absent, but the backlog localisation item records plurals and placeholder handling as deferred. Impact is lower than stated because of the single-language-pair scope.

### X-TEXT-L10N-07: Language set is hard-coded in several places

- **Severity:** low · **Verdict:** deliberate · **Category:** architecture · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/data/strings.cpp:51`

Language, LANGUAGES, LANGUAGE_CODES and Strings::text's std::array<std::string, LANGUAGES> fix the language count at compile time. strings.cpp separately passes the literal list { "en", "zh" } to NoteUnknownKeys instead of LANGUAGE_CODES. names.yaml has only zh and pinyin, and NameText only knows Chinese-vs-else (English = pinyin).

**Impact:** Adding a third language needs edits in the enum, constants, the duplicate literal, every YAML entry and NameText. The literal copy can drift from LANGUAGE_CODES and then cause spurious or missing unknown-language warnings. Every string in one file must carry every language, which forces complete translation before any language can ship, with no per-locale file or fallback-to-English chain.

**Recommendation:** Derive the unknown-key list from LANGUAGE_CODES now. When localisation is done properly, use per-locale files, BCP 47 tags, and a fallback chain.

**Verification:** The literal { "en", "zh" } at line 51 duplicates LANGUAGE_CODES, which is true and trivially fixable. The rest (one file per locale, BCP 47, fallback chain) is the backlog localisation item, which says 'every string needs both' and plans per-locale files. Low.

### X-TEXT-L10N-08: No text overflow, wrapping or truncation policy

- **Severity:** low · **Verdict:** deliberate · **Category:** ui · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `src/app/hud.cpp:210`

Labels, HUD bar text and place names are single lines with no max width, wrap or ellipsis. Only the date line widens its hit area (max of fixed width and text width). Bar text is placed at a fixed inset over a fixed-size bar regardless of text length. FillValue accepts any i64 and the date line any u64 month.

**Impact:** Longer translations (German, Russian) or large values (10-digit health) overflow bars and overlap neighbours. Long NPC names or stack labels (a name with +N count) have no clamp.

**Recommendation:** Define a measured-width policy (shrink, ellipsis, wrap) in gfx text API and test with pseudo-localised strings (the deferred pseudo-loc item).

**Verification:** Bar text uses a fixed inset with no width policy, as claimed. The backlog localisation item lists pseudo-localisation to catch truncation as deferred and chunk 8 UI work is expected to add layout policy. Only en and zh ship, both compact. Low.

### X-TEXT-L10N-09: Prewarm shapes every string many times; Warm uses O(n) contains per glyph occurrence

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/text_layout.cpp:484`

PrewarmTexts emits for each string in each language: the text, the label stack filled with it and digits, and a string concatenated with the HUD filled line, so roughly 3 shaped texts per string, each shaped in two directions. Warm() checks `std::ranges::contains(unseen, glyph)` (linear) for every glyph occurrence not yet cached, so cost grows as occurrences times unique glyphs. PrewarmNameTexts joins every name into one very long label-stack string. WarnMissing also does a linear `contains` on a vector. The glyph cache and its pages never evict.

**Impact:** Fine for 50 strings and 300 names. With thousands of strings (skills, items, dialogue, sect and place names) plus a CJK corpus of several thousand glyphs, startup stalls (quadratic-ish) and VRAM grows without bound (each ~48px SDF glyph is a few KB; 20k glyphs is tens of MB across 2048x2048 pages).

**Recommendation:** Use an unordered_set for unseen and missing; prewarm only the glyph set (unique codepoints through the shaper) instead of whole strings; cap pages and add eviction or lazy loading by script; rely on the worker for the long tail.

**Verification:** The linear std::ranges::contains on `unseen` at line 484 and on `missing` at 348 are real. Cost is occurrences x unique glyphs, but this only runs at startup, and the unique glyph count is small for the current Latin and zh tables (a few hundred). It becomes a real cost only with a large CJK corpus. The no-eviction claim and the 'thousands of strings' scaling are speculative. The fix of using an unordered_set is trivial and valid.

### X-TEXT-L10N-10: Placeholders recomputed: Prewarm duplicates the HUD and stack composition logic

- **Severity:** low · **Verdict:** confirmed · **Category:** duplication · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/text_layout.cpp:88`

PrewarmTexts hard-codes HUD_FILLED (Health, Qi, Cooldown), the separator behaviour (a two-space join) and the stack placeholder names, mirroring app/hud.cpp and gfx/labels.cpp. Adding a HUD string or changing its format silently breaks prewarm coverage, which then only shows as a late-landing glyph or hole.

**Impact:** Drift between what the HUD draws and what is prewarmed. The shaping-dependent claim (HarfBuzz shapes digits after Latin differently) is untested for new strings.

**Recommendation:** Derive prewarm text from the same builders the HUD and labels use, or mark StringIds as filled in the string table so Prewarm iterates them.

**Verification:** PrewarmTexts uses a hard-coded HUD_FILLED list and the literal format "{}  {}". The doc comment states it mirrors the HUD's composition. It is duplication, and a drift risk. The impact is limited to a late-landing glyph, since the worker rasterises misses. The cited line 72 is the comment, and the code is around 88-100.
