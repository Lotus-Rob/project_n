# Review: data-yaml-core (part 3)

### DATA-YAML-CORE-21: MAX_HEALTH comment says below 2^53 but the value is exactly 2^53

- **Severity:** info · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/data/game_data.cpp:16`

The comment says "below 2^53, which an f64 holds exactly". The constant is 9'007'199'254'740'992, which equals 2^53. WholeNumber also says `limit` is below 2^53.

**Impact:** No functional bug: 2^53 is exactly representable, and the text "9007199254740993" rounds to 2^53 and passes. The comment is wrong, though, and a creature with health 2^53 will overflow-risk any i64 sum downstream.

**Recommendation:** Make the limit 2^53 - 1 or fix the comment. Consider a far lower sanity cap, because health is summed and multiplied elsewhere.

**Verification:** MAX_HEALTH is 9'007'199'254'740'992, which is exactly 2^53. The comment says 'below 2^53'. This is a wording error only. The value is exactly representable in an f64.

### DATA-YAML-CORE-22: ReadText has no size cap and doesn't detect directories, and WriteText/ReadText failures are untested

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/data/yaml.cpp:7`

ReadText slurps the whole file with istreambuf_iterator and no limit. On Linux, opening a directory with ifstream succeeds, and the failed reads may not set badbit through a streambuf iterator, so the result is an empty string and no error. No test exercises ReadText, WriteText or ErrorOf directly.

**Impact:** A multi-GB or mis-pointed file is read entirely into memory before parsing. A directory named strings.yaml gives a misleading parse error such as 'not a map' instead of 'couldn't read'.

**Recommendation:** Check `is_regular_file`, cap the size (for example 16 MiB), and use the error code. Add unit tests for the missing file, directory, empty file and write-to-a-folder cases.

**Verification:** ReadText has no size cap, as stated (yaml.cpp 7-17). The inputs are the game's own data files and the user's settings, so the memory risk is theoretical. The directory claim is only plausible: libstdc++ ifstream on a directory opens, and the read error may not set badbit through the iterator. I did not run it. The lack of direct ReadText/WriteText tests I did not check. Fair low-value hardening.

### DATA-YAML-CORE-23: Manifest sprite parse evaluates every field even after the first error, and sprites[i] indexing is repeated

- **Severity:** info · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/data/manifest.cpp:44`

Sprite() builds file, page, region and foot (four expected<> values) before it checks any of them. Parse then calls `sprites[i]` for each i, and `sprites.size()` on a const sequence node. Neither is a problem at the current count, but the cost grows with the atlas (thousands of sprites).

**Impact:** Negligible now. With a large procedural or content-heavy atlas, startup cost grows, and `Numbers` allocates a vector per sprite.

**Recommendation:** Iterate the sequence with the iterator and return at the first error. Use std::array for the fixed-length lists.

**Verification:** manifest.cpp Sprite() builds file/page/region/foot before any check, and Numbers allocates a vector each. True but negligible and the atlas is a fixed SPRITE_NAMES list. The perf claim is not worth acting on.

### DATA-YAML-CORE-24: animations.yaml refuses unknown keys while the other readers warn and ignore them

- **Severity:** info · **Verdict:** confirmed · **Category:** consistency · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/data/animations.cpp:18`

OnlyKeys turns an unknown entry or walk key into a refusal that stops the game. creatures, names, strings and settings add a warning and carry on. Animations also has no `warnings` parameter.

**Impact:** A typo in animations.yaml breaks the game at start, unlike a typo in creatures.yaml. This is defensible (the key is rare and the table is presentation), but it is an undocumented inconsistency with the `known keys` convention.

**Recommendation:** Either document the difference in assets/AGENTS.md, or move to NoteUnknownKeys with a warnings parameter.

**Verification:** animations.cpp uses OnlyKeys (lines 56 and 125), which refuses, and has no warnings parameter. The other readers use NoteUnknownKeys. The inconsistency is real. I found no record of it as a decision, but it is harmless.

### DATA-YAML-CORE-25: Animation frames aren't checked against the standing sprite's size or foot, and a frame list may repeat or include the standing sprite

- **Severity:** info · **Verdict:** unverifiable · **Category:** design · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/data/animations.cpp:70`

ReadWalk only checks that each frame is in the manifest and in every scene holding the standing sprite. It doesn't compare width, height or foot with the standing sprite, nor check that all frames of one clip are mutually consistent.

**Impact:** A walk frame of a different size or foot position makes the figure visibly jump or slide when it starts walking. Nothing catches it in data or tests.

**Recommendation:** Warn, or refuse, when a frame's size or foot differs from the standing sprite's by more than a tolerance. Alternatively, test it in the shipped-animations test.

**Verification:** I did not read ReadWalk to check whether frame size or foot is compared. A shipped-data test or a real look at the game would decide whether placeholder frames match. It is a design opinion with low impact.
