# Review: data-yaml-core (part 2)

### DATA-YAML-CORE-11: Five private re-implementations of the 'what line N: why' refusal

- **Severity:** low · **Verdict:** deliberate · **Category:** duplication · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/data/strings.cpp:11`

internal::RefusalAt exists, but strings.cpp (FailAt), game_data.cpp (Fail), manifest.cpp (Refuse) and names.cpp (RefuseNames) each build the same message by hand. settings.cpp builds it inline in several places.

**Impact:** Message formats can drift, and fixes such as the 'line 0' handling need five edits.

**Recommendation:** Use internal::RefusalAt in all of them. Add a `Failure` convenience wrapper in yaml.hpp.

**Verification:** The duplication is true. The sweep-up backlog (line 15) explicitly plans to merge the duplicated reader helpers (Refuse/Fail/Text/Whole variants) into src/data/internal/yaml.hpp, conditional on the unity-build measurement. It is a known cleanup item, not a defect.

### DATA-YAML-CORE-12: Duplicate keys are silently accepted in settings, creatures, names, manifest and every key lookup except strings and animations

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** input-validation · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/names.cpp:36`

yaml-cpp keeps both entries of a duplicated key and `map[key]` returns the first. strings.cpp and animations.cpp detect duplicates, but names.cpp (`surnames:` twice), game_data.cpp (`fox:` twice), settings.cpp (`language:` twice) and manifest.cpp do not. A later, intended override is silently ignored.

**Impact:** A hand-merged file whose second `surnames:` list is ignored, or a settings file whose second `language:` has no effect, gives no warning at all.

**Recommendation:** Add one shared check in NoteUnknownKeys (it already walks the map) that warns or refuses on a repeated key. Add tests for each reader.

**Verification:** The claim that strings.cpp and animations.cpp detect duplicates while other readers use map[key] and take the first match matches the code I saw. I did not read each reader to confirm that none dedupes. I did not find the gap recorded in the backlog or design docs. A real consistency gap; low impact because the files are hand-edited game data that tests load.

### DATA-YAML-CORE-13: Manifest geometry (page_size, pages) is not validated

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** input-validation · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/manifest.cpp:139`

Parse reads page_size, pages and mip_levels as u32. mip_levels is range-checked and gutter is checked against it. page_size has no bounds (zero, not a power of two, over 16384, or below 2^(mips-1)), and `pages` has no upper limit. The comment on MAX_MIP_LEVELS says 'a 16384-texel page's', which assumes a page size no code checks.

**Impact:** A stale or corrupt manifest with `pages: 4000000000` or `page_size: 100000` passes the parse and fails or allocates huge texture arrays later, in the renderer. The manifest is generated, so this needs an unusual input, but ParseManifest is a fuzzed reader promising to refuse such input.

**Recommendation:** Require a power-of-two page_size in a sane range (for example 256 to 16384), at least 2^(mip_levels-1), and pages between 1 and a small cap such as 64. Add test cases.

**Verification:** pages and page_size have no range checks (manifest.cpp 139-147), so an absurd value is accepted. However, every sprite's region plus edge is checked against pageSize (lines 94-101) and its page against pages (line 90). The sprite list must match SPRITE_NAMES in order, so a zero or tiny page_size is refused indirectly as soon as sprites exist. Only an oversized page_size or pages value passes, and the renderer is what would then fail. The manifest is generated and lint checks it for staleness. The medium rating overstates it; low.

### DATA-YAML-CORE-14: Manifest accepts zero-size sprites, duplicate scene names, and `file` strings unchecked

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** input-validation · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/manifest.cpp:44`

Sprite() accepts width 0 or height 0 (so a foot of 0,0 and an empty region pass). Scene() accepts an empty scene name, two scenes with the same name (SceneNamed then finds the first), and an empty scene. `file` is any string, including an absolute path or `..`.

**Impact:** A broken atlas generator output makes a zero-area sprite reach the GPU upload path. Duplicate scene names make one scene unreachable. A hostile or corrupt manifest could point a sprite load outside assets/.

**Recommendation:** Require a nonzero region, unique non-empty scene names, and a relative `file` with no `..`. Test each.

**Verification:** Sprite() does not reject width or height 0, as stated: it only checks foot against the size and the region against the page. Scene() duplicate and empty names are plausible but I did not read Scene() in full. The path-escape risk is overstated: the manifest is a generated, committed file, and the sprite names are fixed to SPRITE_NAMES. The file field is unchecked. Low hardening.

### DATA-YAML-CORE-15: names.yaml entries aren't checked for duplicates or valid UTF-8, and the weight total isn't bounded

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** input-validation · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/data/names.cpp:36`

ReadList enforces text non-empty, lower-case ASCII pinyin, weight in 1..65535 and at most 1024 entries. It doesn't check that `zh` is valid UTF-8 or free of control characters, doesn't reject duplicate zh or pinyin entries (which skew weights), and doesn't bound the sum of weights (up to 1024 * 65535, which fits a u32 but is undocumented).

**Impact:** Duplicated surnames silently double their draw rate. Invalid UTF-8 or control characters in zh reach the text shaping and glyph prewarm code. A future widening of MOST_NAMES could overflow a u32 weight sum in core::NameOf.

**Recommendation:** Add a UTF-8 and character-class check, a duplicate check, and a static_assert or validation that the weight sum fits. Test them.

**Verification:** names.cpp ReadList checks non-empty text, lower-case pinyin, weight range and MOST_NAMES, as stated. It has no UTF-8 check, no duplicate check and no weight-sum bound. The u32 overflow claim is speculative: 1024*65535 fits in u32. The duplicates-skew-weights point is real but is a data-authoring concern.

### DATA-YAML-CORE-16: strings.yaml text isn't checked for UTF-8, whitespace-only text, or an empty key, and 'null'-looking text is a Null node

- **Severity:** low · **Verdict:** confirmed · **Category:** input-validation · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/data/strings.cpp:59`

The text check accepts any non-empty scalar, including whitespace-only text. An empty key (`"": {en: x, zh: y}`) becomes a data string named "". Because yaml-cpp turns a plain `null`, `~` or `Null` into a Null node, a string whose English text is literally `null` is refused as 'isn't text' unless quoted.

**Impact:** A translator who writes `en: ~` or `en: No` style values gets a refusal whose hint ('isn't text') doesn't explain the quoting. Whitespace-only or empty-key strings pass.

**Recommendation:** Trim-check the text, reject empty keys, check UTF-8, and say 'quote it' in the message for a Null node. Add tests.

**Verification:** strings.cpp checks only IsScalar and non-empty Scalar(). An empty key, whitespace-only text and invalid UTF-8 all pass. A plain null/~ is a Null node, not a scalar, so it is refused with "isn't text". Real but minor. Duplicate names are already refused.

### DATA-YAML-CORE-17: ReadNamed leaves the field untouched when the key is absent, and gives an empty name for a non-scalar

- **Severity:** low (reviewer said info) · **Verdict:** confirmed · **Category:** api · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/internal/yaml.hpp:137`

ReadNamed returns success without writing the field when the key is missing, so the caller's default silently applies, and it is documented as such. For a non-scalar value the text is empty, so the message reads `x's key "" isn't known`, with no hint that a list or map was given. The function has no direct test.

**Impact:** A confusing message for a list-valued key, and no coverage for the shared reader that many tables depend on.

**Recommendation:** Say 'isn't text' for a non-scalar, and add direct tests for the helpers in internal/yaml.hpp (ReadNamed, DescribeRange, TunedTicks edge cases such as 0 and a value rounding to 0).

**Verification:** internal/yaml.hpp ReadNamed (about line 137) sets text to an empty string for a non-scalar, so the message reads `key "" isn't known` with no hint that the value was a list or map. When the key is absent it returns success and leaves the field alone, as documented. The message flaw is real and trivial.

### DATA-YAML-CORE-18: internal/yaml.hpp helpers have no direct tests, and TunedTicks edge cases are only covered through other tables

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/internal/yaml.hpp:1`

There is no tests/unit/data/yaml_tests.cpp. WholeNumber (negative zero, 2^53, fractional, `1e3`), TunedNumber range open/closed ends, TunedTicks (sub-tick value rounding to 1 tick, `least` > 1, max seconds), RequiredNumber, ReadSprite/ReadPalette/ReadGlowColour, NoteUnknownKeys (null warnings, non-map) and the hex/octal/duplicate-key behaviour are covered only by whatever the callers' tests happen to hit.

**Impact:** A regression in a shared helper (for example the rounding rule in TunedTicks) would show up as scattered failures in effects, seals or world-look tests, or not at all.

**Recommendation:** Add a focused yaml_tests.cpp with about 20 representative samples per helper, including the boundary values above.

**Verification:** tests/unit/data has no yaml_tests.cpp, so the internal/yaml.hpp helpers have no dedicated file. Callers' tests cover them indirectly. I did not check boundary coverage such as TunedTicks rounding, so the specific gaps are plausible but unchecked.

### DATA-YAML-CORE-19: The fuzz harness has a fixed input budget and never exercises the Load* paths or alias bombs

- **Severity:** low · **Verdict:** deliberate · **Category:** test-coverage · **Status:** known-deferred · **Reviewer confidence:** low
- **Where:** `tests/fuzz/yaml.cpp:49`

The harness runs every Parse* on the same bytes with libFuzzer's default max length (about 4 KiB). The known amplification paths (aliases multiplying NoteUnknownKeys warnings, `&a` repeated maps) need either larger inputs or explicit seeds. It doesn't check warnings count or timing, and the seed corpus has no alias or deep-nesting sample. It doesn't call ReadText, LoadSettings or any Load*.

**Impact:** The one known amplification issue (NoteUnknownKeys) and any quadratic behaviour in the readers can't be found by this target. A timeout-class bug (slow-unit) is only reported if the fuzzer's default time limit is hit.

**Recommendation:** Add seeds with aliases and deep nesting, run with `-max_len=65536`, and assert that warnings.size() stays below a cap. Consider a second target for the Load* functions with a temporary directory.

**Verification:** The sweep-up backlog (line 34) records the alias amplification in NoteUnknownKeys as a known deferral. The finding's own status is known-deferred. The coverage gap in the fuzz harness (no aliases, no Load*) is plausible but I did not read tests/fuzz/yaml.cpp.

### DATA-YAML-CORE-20: Test gaps for Parse* error paths in game_data and animations

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/data/game_data_tests.cpp:118`

The game_data tests don't cover: a malformed abilities text passed through ParseGameData, a scalar or null `orbit`, an unknown top-level creature key warning, `sight` with a hex value, a duplicate creature key, a very large `health`, or `sprite` given as a list. The animations tests don't cover a non-scalar standing key or an `entry` that is a list.

**Impact:** These paths run only under fuzzing, which doesn't check what the message says.

**Recommendation:** Add one table-driven test per reader for the cases listed, each asserting the message names the field.

**Verification:** I read game_data_tests.cpp and animations_tests.cpp. Most of the listed cases have no test: malformed abilities text through ParseGameData, a scalar or null orbit, hex sight, a duplicate creature key, a huge health, sprite as a list, and in animations a non-scalar standing key or an entry that is a list. The claim that the gap is total is overstated. The Broken table (12 cases) already covers bad walk_speed, .inf, missing or unknown sprite, missing orbit radius, health -5, 12.5 and 0, a missing creature and a bad orbit target. The animations table (14 cases) covers duplicates, unknown keys, frame limits and stride bounds. An unknown creature-level key warning is tested (line 35). The claim that these paths run only under fuzzing is true only for the listed cases. The recommended per-reader table-driven tests would extend the existing tables, so they are cheap. Low severity: the gaps are real but minor.
