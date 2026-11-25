# Review: data-world

**Scope:** src/data/world_look.{hpp,cpp}, src/data/world_map.{hpp,cpp}. Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 10 standing (0 critical, 0 high, 0 medium, 5 low, 5 info), 0 raised but refuted on verification.

## Assessment

## Scope: src/data/world_look.{hpp,cpp} and src/data/world_map.{hpp,cpp}

I read all four files in full. For the tests I read the case lists and grepped for specific gaps, not every test body. I did not read the core types (`core::WorldMap::Build`, `core::WorldLook`) or the `data/internal/yaml.hpp` helpers beyond targeted greps.

### Strengths
- Both readers are careful, fail-closed validators, so they are not a source of crashes or sim desyncs.
- Every refusal names a line.
- Ranges are explicit, `NaN` and `inf` are rejected, and narrowing casts follow range checks.
- Neither reader throws anything other than `YAML::Exception`, and both catch it.
- The reader refuses a folded or quoted `grid:` instead of misreading it.
- A kind the map doesn't use still has its sprites validated, so a typo can't hide.
- Cross-checks against the manifest and the world scene's resident sprite list prevent drawing from a page that isn't loaded.
- Unknown-key warnings are consistent across both readers.
- Determinism is fine: the readers iterate YAML maps in file order, and all derived data comes from `Build`.
- Tests are extensive. They cover refusals with line names, CRLF and trailing spaces, tabs, folded grids, bands, profiles and the shipped map and look.

### Weaknesses
- There is a lot of copy-paste boilerplate. The `const expected x = ...; if (!x) return unexpected{x.error()};` ladder repeats dozens of times in `world_look.cpp`.
- `world_map.cpp` has its own `Whole`, `Point` and `Text` helpers that overlap `internal::WholeNumber`, `RequiredNumber` and similar.
- `world_look.cpp` is about 980 lines of near-identical per-section readers and is already on the known oversized-files list.
- The look reader is coupled to the hand-written test map. Per-key paintings and the "every map kind needs a look" rule work against the stated direction that maps are procedural per seed (see the `places.by_key` finding).
- A few limits are redundant or stricter than they look: the 255-kind check is unreachable, and the kind-cost limit disagrees with `Build`'s.

### Overall health
Good for a data reader. There are no bugs I could confirm from reading. The risks are design drift towards procedural maps, one fragile dependency on yaml-cpp's mark semantics, and maintainability of the boilerplate.

**Files read in full:** (not reported)

## Findings

### DATA-WORLD-01: Look refuses a map whose place keys it has no painting for, and every map kind needs a look: coupled to a fixed hand-written map

- **Severity:** low (reviewer said medium) · **Verdict:** deliberate · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/world_look.cpp:829`

`places.by_key` paintings are refused when no place of the map has that key (`PaintingsIn` with `isKey`). `ReadLookKinds` also refuses when a map kind has no look. Only the map's own danger sorts need a marker or painting; extras there are accepted, so the rules are asymmetric. The look is validated per loaded map, and `by_key` names the test map's place keys.

**Evidence:** `PaintingsIn(*byKey, "painting for key", atlas, isKey)` returns `RefuseLook(... "names {}, which no place of the map has")`; `dangers` is passed `nullptr` and so tolerates extras.

**Impact:** The project records that maps are procedural per seed and `world.yaml` is only a test map. A generated map has other place keys, so the shipped look would be refused for it. The `by_key` mechanism would also need either per-seed look files or constant edits. The asymmetry forces `world_look.yaml` and `world.yaml` to be edited in lockstep.

**Recommendation:** Decide the look's relation to procedural maps. Either accept unknown `by_key` entries like unused kinds or markers, with a warning, or move key-specific art out of the look. Keep refusing only the missing coverage the map needs.

**Verification:** world_look.cpp:~853-860 confirms PaintingsIn(by_key, isKey) refuses unknown keys while dangers uses nullptr. The chunk5b spec Outcome (line ~447) records 'A by_key painting naming a place the map lacks is refused too' as a decision. The tension with procedural maps is real, but world.yaml is currently the test map and the look is validated per loaded map. It is a future design question, not a defect.

### DATA-WORLD-02: Grid literal-block check relies on yaml-cpp `Mark().pos` being a byte offset into the original string

- **Severity:** low · **Verdict:** unverifiable · **Category:** robustness · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/data/world_map.cpp:166`

`ReadGrid` decides whether the grid is a literal block by indexing the original `yaml` string_view with `grid.Mark().pos` and checking for `|`. This assumes the mark's `pos` equals the byte offset in the text.

**Evidence:** `const auto indicator = static_cast<std::size_t>(grid.Mark().pos); if (indicator >= yaml.size() || yaml[indicator] != '|') refuse`

**Impact:** If yaml-cpp counts positions differently from bytes (a UTF-8 BOM it eats, UTF-16 or UTF-32 input it transcodes, multi-byte characters in comments before the grid), a valid literal grid is refused with a confusing message. In the worst case the offset lands on another `|` and a folded grid is accepted and misread. `grid: &a |` or `!!str |` may also move the mark. The tests cover only ASCII, CRLF and tab cases.

**Recommendation:** Add tests for a leading BOM, a non-ASCII comment before `grid:`, and an anchored or tagged grid. Alternatively, find the style independently: scan the line of the mark for `|` after the colon, or compare the mark's line text.

**Verification:** world_map.cpp:~166 does index yaml with Mark().pos as claimed. Whether yaml-cpp's pos is a byte offset in every case (BOM, anchor or tag before the `|`) can't be settled by reading. A test with a BOM, a non-ASCII comment and an anchored grid would decide it. The failure is a refusal, so the impact is small.

### DATA-WORLD-03: Kind cost limit in the reader (65534) disagrees with Build's (MAX_WALK_COST = 1000)

- **Severity:** low · **Verdict:** confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/data/world_map.cpp:99`

`KindCost` accepts a whole number up to `IMPASSABLE_COST - 1`. `Build` refuses a kind cost over `MAX_WALK_COST` (line 220 in `core/world_map.cpp`) with no line number. The comment above `ReadSizes` says the reader and `Build` must agree on these bounds.

**Evidence:** `Whole(cost, ..., core::IMPASSABLE_COST - 1)` against `kind.cost > MAX_WALK_COST` in `Build`.

**Impact:** A typo such as `cost: 5000` gives an error with no line, whereas the same typo in a profile gets a line. This is a diagnostics inconsistency, not a correctness bug.

**Recommendation:** Use `core::MAX_WALK_COST` as the limit in `KindCost`, as `Cost` and `ReadSizes` do, and add a test.

**Verification:** KindCost (world_map.cpp:99) limits to IMPASSABLE_COST-1, while core/world_map.cpp:220 refuses kind.cost > MAX_WALK_COST (1000). Other readers use core::MAX_WALK_COST (lines 354, 391). A cost of 5000 is therefore refused by Build without a line number.

### DATA-WORLD-04: The reader accepts duplicate YAML keys silently

- **Severity:** low · **Verdict:** unverifiable · **Category:** robustness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/world_map.cpp:374`

yaml-cpp does not reject duplicate map keys. `node[key]` returns the first match. Iterating a map such as `draws:`, `features:`, `kinds:` of a profile, markers or paintings visits every duplicate. For `draws` and profile `features` and `kinds` the last duplicate wins, and for most other lookups the first does.

**Evidence:** `ReadEntries` and `ReadDraws` iterate pairs and overwrite. `ReadMarkers` and `PaintingsIn` push every duplicate into vectors. Lookup of markers and paintings takes the first.

**Impact:** A copy-paste duplicate gives different outcomes depending on the section, and no warning. It is a data-authoring hazard rather than a runtime fault.

**Recommendation:** Detect duplicate keys while iterating (a set of seen names) and refuse, naming the line. Apply it in `NoteUnknownKeys` or a shared helper.

**Verification:** yaml-cpp does accept duplicate map keys, and iteration visits each of them, so the hazard is plausible. I did not trace every reader's overwrite and first-match behaviour, so the claimed inconsistency across sections is unchecked. The effect is limited to data authoring.

### DATA-WORLD-05: Tests would not catch the grid-mark fragility, duplicate keys, or reader/Build bound disagreements

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/unit/data/world_map_tests.cpp:159`

The reader tests exercise many refusals but I found nothing for the cases listed in the other findings. I did not look for each case in a test body, so this rests on the case list and a few greps.

**Evidence:** A grep of `world_map_tests.cpp` for BOM, UTF, `.nan` and `.inf` found nothing. The `too many kinds` branch is not tested. A grep for duplicate/twice across both test files found nothing.

**Impact:** A yaml-cpp upgrade changing mark semantics, or a bound change in `Build`, would go unnoticed until a data edit trips it.

**Recommendation:** Add small cases for a BOM or non-ASCII comment before the grid, an anchored grid, a kind cost over 1000 (assert the message names a line), duplicate keys, and `1e3` or `5.0` as a whole number if that should be refused.

**Verification:** Grepped both test files for BOM, utf, .nan, .inf, twice, duplicate and 'too many kinds': no hits. src/data/world_map.cpp:126 has the 'too many kinds' branch and no test reaches it. So the missing cases are real. The finding itself admits it did not read test bodies, and it depends on other findings (index numbers not shown) that I did not verify. Impact is small: gaps in refusal-path coverage, not a defect.

### DATA-WORLD-06: The 255-kind limit in ReadKinds is unreachable

- **Severity:** info · **Verdict:** confirmed · **Category:** dead-code · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/data/world_map.cpp:125`

Symbols must be unique visible ASCII characters (0x21 to 0x7E), so at most 94 kinds can ever be read. The 'too many kinds: at most 255' branch can never fire and the message is misleading.

**Evidence:** `source.kinds.size() >= std::numeric_limits<u8>::max()` and the symbol check at line 140.

**Impact:** Dead code. The real limit is 94, which the message does not state, and a procedural map with more than 94 kinds could not be expressed in this format.

**Recommendation:** Drop it or state the real limit. `Build`'s u8 check remains as the backstop.

**Verification:** ReadKinds' size()>=255 check (line ~125) is unreachable. Symbols must be unique single characters in 0x21-0x7E (line 140), which allows 94 kinds at most. The message names 255, which is misleading.

### DATA-WORLD-07: Duplicated numeric helpers: local `Whole`, `Point` and `Text` beside internal::WholeNumber and RequiredNumber

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** duplication · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/world_map.cpp:39`

`world_map.cpp` defines its own `Whole` (via `as<f64>` with floor checks) and also uses `internal::WholeNumber` for other fields. `ReadStayDays` uses the local one; `ReadTravelWhole` and the profile pace use the internal one.

**Evidence:** `Whole` at line 39 against `internal::WholeNumber(WORLD_READER, entry, owner, "ticks_per_cost", true, ...)` at line 428.

**Impact:** There are two number-parsing paths with different messages. Both accept `5.0` and `1e3` as whole numbers because they parse as f64. Any later tightening has to be done twice.

**Recommendation:** Move the local helpers into `data/internal/yaml.hpp` next to the existing ones, or use one family everywhere. Consider parsing integers as integers.

**Verification:** The local Whole (line 39) is used at lines 58, 62, 99, 332, 354, 391, 520, 521 and 669, while ReadTravelWhole uses internal::WholeNumber. Two families do exist, and both parse via f64. The claim that ReadStayDays uses the local one is accurate. The impact is small and the messages differ only slightly.

### DATA-WORLD-08: world_look.cpp is a long run of repeated expected-unwrapping boilerplate

- **Severity:** info (reviewer said low) · **Verdict:** deliberate · **Category:** maintainability · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `src/data/world_look.cpp:436`

Almost every reader is a ladder of `const expected x = NumberIn(...); if (!x) return unexpected{x.error()};` followed by an assignment. `ReadGround`, `ReadShoreAndGrid`, `ReadFogMotion`, `ReadMist` and `ReadRibbon` are the main cases.

**Evidence:** The file is about 980 lines for roughly 15 small structs, and is already listed as oversized in the deferred items.

**Impact:** Adding a field means three or four coordinated edits (key list, read, validate, assign). A mistake in the unknown-keys list silently warns on a valid key, or fails to warn on a typo.

**Recommendation:** Add a small field-descriptor or `and_then` helper, or a macro-free `TRY` pattern, and split the file by section such as ground, fog and ribbons. Generate the known-key list from the reads so the two cannot drift.

**Verification:** The spec Outcome for chunk 5b says 'world_look.cpp's growth (about 450 lines) is noted, not split'. The finding itself carries known-deferred status. The boilerplate is real but accepted.

### DATA-WORLD-09: Local constant named DELETE may collide with the Windows macro

- **Severity:** info · **Verdict:** confirmed · **Category:** portability · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/data/world_map.cpp:14`

`constexpr char DELETE = 0x7F;` uses a name that `<winnt.h>` defines as a macro (`DELETE` is an access-right constant).

**Evidence:** Line 14. The file currently includes no Windows headers, so the Windows MSVC build passes today.

**Impact:** A future change that pulls `windows.h` into the data module's precompiled header or includes would break this with an obscure error.

**Recommendation:** Rename it (`LAST_VISIBLE`, `ASCII_DEL`).

**Verification:** world_map.cpp:14 declares constexpr char DELETE = 0x7F and uses it at line 140. The file includes no windows.h, so the build is fine today. It is a theoretical macro collision only, and a rename would remove it.

### DATA-WORLD-10: by_key painting lookup is O(paintings x places)

- **Severity:** info · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/data/world_look.cpp:853`

`isKey` scans every place for each painting named. Cost is paintings times places on a load or hot reload.

**Evidence:** `std::ranges::any_of(map.Places(), ...)` inside the per-entry callback of `PaintingsIn`.

**Impact:** Negligible at the shipped size of about 26 places. On a very large procedural map with thousands of places and a look that names hundreds, a hot reload on the main thread could take noticeable time.

**Recommendation:** Build an unordered set of keys once, or drop the check if `by_key` is reworked for procedural maps.

**Verification:** src/data/world_look.cpp around lines 853-855: isKey does ranges::any_of over map.Places() and runs once per by_key entry, so the cost is O(paintings x places). That matches the claim. It is only a few dozen places in the shipped data, so the cost is negligible. The finding's own severity of info is right.
