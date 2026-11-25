# Review: x-text-l10n (part 2)

### X-TEXT-L10N-11: Procedural world has no place, sect or technique name generator; place names only from handwritten table

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `assets/data/strings.yaml:160`

Place names are data strings `place_<key>` in strings.yaml, found by world.yaml's place entries (world_map.cpp ReadPlaceName refuses a place whose name has no string). The project states maps are procedural per seed, yet the only naming mechanism for non-person things is a hand-authored string per key, with both languages, and the 16-bit StringId (cap 65535) is shared by named and data strings.

**Impact:** A procedural world of thousands of settlements cannot name them from this table. Sects, cities, techniques, items and events will need compositional naming (the same zh+pinyin pattern as people), and generated names must not count against the u16 StringId space or require rebuilding the table.

**Recommendation:** Design name generation for places and entities (zh+pinyin composition with seeded draws like NameOf) before the world generator lands, and keep strings.yaml for UI text only.

**Verification:** Place names are hand-authored strings, and world.yaml is documented as only a test map, per the procedural-world-map memory. So the premise that procedural place naming is not designed yet is true. But it is a future-phase design gap, not a defect in current code. Localisation beyond en/zh is explicitly out of scope in design 0002.4. A place-name generator is naturally part of the world generator work. Valid as a forward-looking note, not a bug.

### X-TEXT-L10N-12: People names can collide and English names are ambiguous

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/names.cpp:71`

A name is drawn from 309 weighted surnames and 105 given characters (one or two). The space is ~300 x (105 + 105^2). There is no uniqueness or disambiguation. English text is tone-less pinyin, so distinct names collapse: 徐 and 许 are both `xu`, and many given characters share a pinyin (shi, yi, jian, yuan appear twice). NameKey identifies the table indices, not the text, so stacking works, but two different NPCs can show identical text.

**Impact:** With thousands of NPCs, same-text namesakes are routine (birthday-paradox). Players cannot tell people apart in English, and the 8 console-common pinyin collisions blur sect rosters. Names are fixed to the table; changing names.yaml renames every NPC in existing saves (acceptable pre-1.0 but undocumented in names.hpp).

**Recommendation:** Decide whether namesakes are acceptable; if not, add style names or titles, a nickname draw, or a uniqueness pass. Consider tone marks or the Latin disambiguator for English.

**Verification:** No uniqueness pass exists, and English is tone-less pinyin. Homophone collisions follow directly from that. Namesakes are a game-design choice and are normal in wuxia. NameKey keys on table indices, so the code is consistent. The claim about names.hpp documentation of save impact is minor, and pre-1.0 has no compatibility by policy. Info-level design question.

### X-TEXT-L10N-13: Date shown as month/day with no year, era or calendar localisation

- **Severity:** low · **Verdict:** confirmed · **Category:** localisation-readiness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/hud.cpp:205`

FillDate takes calendar->month + 1 (u64) and day + 1 and prints plain decimals through "Month {month}, day {day}" or "{month}月{day}日". There is no year, solar term, era name or lunar calendar, and the string is a fixed two-placeholder template.

**Impact:** For a long-lived cultivation sandbox (lifespans of centuries) the month counter grows without bound ("Month 14400"). The Chinese form claims a calendar month 月 that does not wrap at 12.

**Recommendation:** Show year/season as the calendar design matures; give the date formatter a structured input rather than two integers so languages can reorder or add fields.

**Verification:** FillDate is called with calendar->month + 1 and calendar->day + 1, as two integers into DateLine, with no year. The facts hold. The calendar design is still maturing, so the unbounded month counter is a placeholder. Valid low-priority note.

### X-TEXT-L10N-14: SDF at 48 px base may blur dense CJK strokes at small sizes

- **Severity:** low · **Verdict:** unverifiable · **Category:** rendering · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/gfx/text_layout.hpp:43`

Every glyph is rasterised once at 48 px as a single-channel SDF with an 8-texel spread and drawn scaled down. Complex hanzi (20+ strokes) at small on-screen sizes lose thin strokes and counters with a single-channel SDF (no MSDF), and the Regular weight is the only one.

**Impact:** Small labels, HUD and dense future UI text in Chinese may look muddy or closed up, which affects readability and accessibility; only reference pictures at current sizes were checked.

**Recommendation:** Verify legibility at the minimum label sizes and with the accessibility text scale at its smallest; consider MSDF, hinted bitmap glyphs below a size threshold, or a second base size.

**Verification:** Legibility of a single-channel SDF at small CJK sizes can only be decided by looking at rendered output at the minimum text scale. Reading the code cannot settle it. The 48 px base size and the spread are real. The reference pictures cover only the current sizes.

### X-TEXT-L10N-15: No locale or number formatting beyond plain decimal

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** localisation-readiness · **Status:** known-deferred · **Reviewer confidence:** low
- **Where:** `src/core/strings.cpp:24`

Numbers are formatted with std::format("{}") as plain decimal everywhere: no digit grouping, no 万/亿 for Chinese, no K/M abbreviations, and the language is only a settings.yaml field (default English; no SDL_GetPreferredLocales detection on first run).

**Impact:** Wuxia stats and prices run into the millions, which are hard to read as bare digits; a Chinese player gets an English UI on first launch.

**Recommendation:** Add per-language number formatting hooks (locale-independent, deterministic) and pick the default language from the OS locale on first run.

**Verification:** Fill uses std::format "{}" with plain decimals, as claimed. Localisation beyond en/zh is out of scope in design 0002.4:277. The system-locale default language is a minor feature gap. The wuxia-scale numbers argument is speculative until stats exist.

### X-TEXT-L10N-16: Vertical text and BTT workaround depends on a SDL3_ttf 3.2.2 bug

- **Severity:** info (reviewer said low) · **Verdict:** deliberate · **Category:** fragility · **Status:** known-deferred · **Reviewer confidence:** low
- **Where:** `src/gfx/text_layout.cpp:267`

Top-to-bottom text is laid out by asking SDL3_ttf for bottom-to-top and correcting the box, because of documented bugs in 3.2.2 (vertical advances, padding of short columns, SDF shaping). No bidi or RTL support exists (TextDirection is LTR or TTB only; design explicitly excludes RTL). Vertical punctuation forms are font-feature dependent.

**Impact:** Upgrading SDL3_ttf changes caption layout silently (reference tests cover only specific pictures). A future RTL language is not possible without replacing the layout path.

**Recommendation:** Pin the SDL3_ttf version in a comment-test pair, add a test that the vertical caption box matches expected values, and record RTL as an explicit non-goal in the docs.

**Verification:** RTL is explicitly excluded in docs/design/0002.4-ui-and-text.md:277 as a non-goal, so the recommendation to record it is already met. The BTT workaround for SDL3_ttf 3.2.2 bugs is documented in the code as deliberate. Library pins are exact, so an upgrade is an intentional act. A test on the caption box would be a reasonable hardening, not a defect.

### X-TEXT-L10N-17: Strings::Find scans the named list linearly before hashing

- **Severity:** info · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/strings.hpp:158`

Find does a `std::ranges::find` over the 50 STRING_NAMES before consulting the hash index, and Add calls Find (so each data string add costs a 50-entry scan). Strings::Text uses .at() twice per call in per-frame code.

**Impact:** Negligible at 50 names, but each load of a large table repeats the scan per entry and the named table is meant to grow.

**Recommendation:** Seed m_index with the named keys too, so a single hash lookup suffices.

**Verification:** Find (strings.hpp 158-167) does std::ranges::find over STRING_NAMES before the m_index lookup, and Add calls Find inside a PN_ASSERT, so the scan is real. Text uses .at() twice as described, which is a bounds check, not a scan. It runs only at load, over about 50 entries, so the cost is negligible and no recorded decision covers it. Seeding m_index with the named keys is a reasonable tidy-up, but nothing is wrong today.

### X-TEXT-L10N-18: Licence handling for the font is correct but undocumented checks

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** licensing · **Status:** new · **Reviewer confidence:** low
- **Where:** `assets/fonts/PROVENANCE.md:8`

The font is OFL 1.1 Noto Sans CJK subset with OFL.txt beside it and a SHA-256 record; design says artifact copies OFL beside the font. No test or lint verifies that the artefact includes OFL.txt or that the SHA-256 matches the file. OFL asks that licence travel with every copy.

**Impact:** A change to the packaging or a font swap without updating PROVENANCE would not be caught.

**Recommendation:** Have lint compare the font's SHA-256 with PROVENANCE.md and have the artifact check assert OFL.txt is shipped.

**Verification:** The licence-travels half is weaker than claimed. artifact in tools/project_n.py (about line 2376) does shutil.copytree(ROOT/'assets', dist/'assets'), so OFL.txt beside the font ships with every artefact. A font swap that left OFL.txt behind would need someone to delete it by hand. The SHA-256 half holds: grep of tools/project_n.py finds no check of PROVENANCE.md against the font. PROVENANCE.md is a human-maintained record, assets/fonts/ is in UNCHECKED_PREFIXES, and the font is not changed often. Low value.
