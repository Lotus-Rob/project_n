# Review: data-combat

**Scope:** src/data/seals.{hpp,cpp}, abilities.{hpp,cpp}, effects.{hpp,cpp}. Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 11 standing (0 critical, 0 high, 1 medium, 7 low, 3 info), 0 raised but refuted on verification.

**Parts:** data-combat.md, data-combat-part2.md

## Assessment

## Assessment of data-combat (seals, abilities, effects readers and their tests)

I read all three source pairs in full, the shared helpers they call in `src/data/yaml.cpp` and `src/data/internal/yaml.hpp`, and the three matching test files. I read the test files partly: the case lists, the harness code and several bodies, not every line. Nothing was built or run, as instructed.

### Strengths
- **`seals.cpp` is the most careful of the three.** It checks the f32 narrowing of values the file says are above 0. It walks the satellite graph with an explicit stack, so a long chain cannot overflow the call stack. It detects loops and nesting depth, and it refuses a seal-key hash collision at load. Errors carry line numbers throughout.
- **Reading is consistent: first refusal wins, unknown keys only warn.** `Fields` and the `FirstError` pattern keep readers flat. Seals and effects always return a refusal with a line.
- **Tests exist for each refusal class.** They are table-driven and assert exact messages, so a wrong line number or wrong text is caught. The shipped `seals.yaml` is loaded in a test and checked to produce zero warnings.
- **Presentation and simulation data are separated.** Effects and seals are never hashed or saved. Abilities are hashed, and their `Valid()` methods live in `core`.

### Weaknesses
- **Three readers, three number-handling disciplines.**
  - `seals.cpp` has `Fields`, `effects.cpp` has `EffectFields`, and `abilities.cpp` has free functions plus `FirstError`.
  - Only `seals.cpp` guards against f64 values that overflow f32 or narrow to 0 or a denormal.
  - `TunedNumber` and `RequiredNumber` default to an unbounded `high` and return an f64. The effects and abilities readers then `static_cast<f32>` the result.
  - A value above `FLT_MAX` converts out of range (UB), and an open-lower-bound value like `1e-50` becomes 0, breaking the "above 0" contract.
  - `core::*::Valid()` would catch the resulting infinity or zero, but `ParseEffects` and `ParseAbilities` never call it. I grepped for `Valid()` calls on effects and found none, so the bad value goes straight to the renderer or the hashed simulation data.
- **Semantic validation of abilities is thin.**
  - Nothing cross-checks `qi_cost` against `qi.max`, so an ability can be uncastable.
  - A `burn` or `poison` block can have 0 ticks or 0 damage.
  - `element: poison` need not have an affliction block.
  - Keys that are meaningless for a slot (`distance`, `duration`) are silently accepted.
  - Duplicate ability or glow names in the YAML overwrite or merge silently.
- **Abilities are a closed C++ enum.** `core::ABILITY_NAMES` fixes the list, and the effects readers hard-code `fire_bolt`, `flying_swords` and `poison_lance`. This is the recorded data-driven sweep, but it limits what the YAML can express.
- **Silent typos in new glows.** A misspelt glow name in `glows:` creates a new, unused glow with no warning.
- **Scale concern.** A nested satellite expansion of `63 * 63` seals is accepted by the loader and truncated only at draw time (`MAX_SEALS_DRAWN = 64`). The author gets no load-time feedback that most satellites will not draw.

### Tests
The tests would catch regressions in what they cover: caps, line numbers and per-key range errors. They would not catch the defects listed above:
- Nothing tests a value over `FLT_MAX` in abilities or effects.
- Nothing tests `1e-50` or `1e-40` in abilities or effects. Seals has these tests.
- Nothing tests duplicate keys.
- Nothing tests `qi_cost` above `qi.max`.
- Nothing tests a zero-tick affliction.
- Nothing tests a duplicate glyph-set name.
- Nothing tests a closed-range denormal in seals, such as `spin: 1e-40`.

### Overall
Healthy and well-structured. The main risk is that numeric-safety discipline is applied in one reader of three, plus a few unvalidated gameplay-semantic combinations. The fixes are small and mechanical: lift the narrowing guard into `internal::TunedNumber`, or add a templated `ReadF32` in `data/internal`.

**Files read in full:** (not reported)

## Findings

### DATA-COMBAT-01: f64 to f32 narrowing in abilities and effects readers has no range guard (out-of-range UB, silent zero)

- **Severity:** medium · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/data/abilities.cpp:47`

`NonNegative()` (abilities.cpp:47-57), `Motion()` (about 83-125), `SealAt` and `EffectFields::Read` (effects.cpp:27-39) read an f64 via `TunedNumber` (default `high` = infinity) and `static_cast<f32>` it. There is no check against `numeric_limits<f32>::max()` and no check that an open-lower-bound value survives narrowing. Seals' `Fields::Read` (seals.cpp:93-104) has both guards.

**Impact:** A radius, distance, speed, short_bend, aim_scatter, effects `trail`, `sword_scale` or hud width of `1e300` converts out of range, which is UB and in practice an infinity. `retarget_speed: 1e-50` or `short_bend: 1e-50` becomes 0 after narrowing and `/TICKS`, violating the 'above 0' contract. `size: 1e-50` for effects reaches rendering as 0. `ParseEffects` and `ParseAbilities` never call `Valid()`, so the bad number goes to the renderer, or into the hashed game data. Hand-edited or modded YAML can trigger it. Debug-build hot reload makes this likely to happen.

**Recommendation:** Move the guard into `internal::TunedNumber`, or add a shared `ReadF32(range)` helper in data/internal, and use it from all three readers. Optionally call `Valid()` on the result before returning. Add tests for `1e300` and `1e-50` in abilities and effects.

**Verification:** TunedNumber (src/data/yaml.cpp:86-110) only checks finite, low and range.high (default infinity). abilities.cpp:56 and effects.cpp:35-40 then static_cast<f32> with no guard. seals.cpp:93-104 has both guards, which makes the omission visible. Out-of-range f64 to f32 is UB, and 1e-50 narrows to 0 under a POSITIVE range such as sword_scale. Hand-edited YAML can reach this. I found no tests for 1e300 or 1e-50 in the abilities or effects tests. Impact is on hand-edited data only, and the shipped data is fine.

### DATA-COMBAT-02: Three divergent field-reader abstractions (Fields, EffectFields, ad hoc expected+FirstError)

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** duplication · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/effects.cpp:20`

seals.cpp `Fields`, effects.cpp `EffectFields` (includes an f64 overload `Read(key, f64&)` that I could not find used), and abilities.cpp's free `NonNegative/Fraction/Count/Amount` plus `FirstError` each re-implement 'first error wins' with different checks. `RefuseAbilities` in abilities.cpp (line 22) duplicates `internal::RefusalAt`, which seals and effects use. The comment says the duplicate is deliberate.

**Impact:** Fixes such as the narrowing guard land in one reader only (see finding 1). New data files will copy a fourth variant.

**Recommendation:** One `internal::MapReader` in data/internal taking an owner string, used by all readers. Drop `RefuseAbilities` and use `RefusalAt`. Remove the unused f64 overload if unreferenced.

**Verification:** The three reader styles do differ, and the guard divergence is real (see 0). The claim that the f64 overload of EffectFields::Read is unused is probably wrong: effects.cpp:207 and :209 read periodSeconds and fadeSeconds with it, if those fields are f64. Not checked further. The duplicate RefuseAbilities is minor and the header says it is deliberate. This is a refactor suggestion, not a defect.

### DATA-COMBAT-03: Closed-range values may narrow to a denormal; doc says 'a value above 0 its f32 would hold as 0 or a denormal' is refused

- **Severity:** low · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/seals.cpp:98`

The `isnormal` check only runs for `range.lowOpen`. A closed-range field such as `lift: 1e-40`, a layer `spin: 1e-40`, `rotation: -1e-40`, `orbit` or `appear` narrows to a denormal and is accepted. The header says any value above 0 that holds as 0 or a denormal is refused. Only shard spin has an explicit least check.

**Impact:** Denormal f32s are slow on some CPUs and can break later divisions or cycle maths (the reason the open-range guard exists). The header comment overstates the guarantee.

**Recommendation:** Reject a nonzero value that narrows to a denormal or 0 for every range, or document that only open-lower-bound fields are checked. Add a test for `lift: 1e-40`.

**Verification:** seals.cpp:101 runs the isnormal check only when range.lowOpen. The seals.hpp:25 doc says a value above 0 that its f32 would hold as 0 or a denormal is refused. A closed-range field such as lift: 1e-40 narrows to a denormal and is accepted. The doc overstates the guarantee. A denormal is harmless in practice, so the impact is cosmetic.

### DATA-COMBAT-04: No validation of cross-field ability semantics (qi_cost above qi.max, zero-tick affliction, element/affliction mismatch)

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/abilities.cpp:349`

`Ability()`/`Read()` accept: `qi_cost` greater than `qi.max` (an ability that can never be cast); `burn`/`poison` blocks with `ticks: 0` or `damage: 0` (`AfflictionAt` requires the keys but not positive values); `element: poison|burn` without a matching affliction block, or the reverse; `qi_gain` on non-generators and `distance`/`duration` on non-movement slots (ignored without even a warning); healing abilities with damage.

**Impact:** Authoring mistakes produce dead abilities or no-op effects that nothing flags. This is acute once abilities become data-driven.

**Recommendation:** Add at least: qi_cost <= qi.max, affliction ticks >= 1 (or refuse a block that does nothing), and warnings for keys with no meaning for the slot. Test each.

**Verification:** The code reads qi_cost (abilities.cpp:350) with no comparison to qi.max, and I saw no cross-field checks there. The ability can then never be cast. Whether zero-tick afflictions or element/affliction mismatches are really unflagged was not checked. These are validation wishes, not bugs. The shipped abilities are authored in-repo, so medium is too high. I did not check whether the abilities tests cover any cross-field case.

### DATA-COMBAT-05: Duplicate keys in abilities, glows, glyph sets and loadout are silently merged or overwritten

- **Severity:** low · **Verdict:** unverifiable · **Category:** error-handling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/abilities.cpp:501`

yaml-cpp iterates duplicate map keys. abilities.cpp's `Read()` overwrites `table.at(*kind)` for a repeated ability. effects.cpp's `ReadGlowTable` merges the second block onto the first. seals.cpp's `ReadGlyphSets` pushes a duplicate set name, so `ReadGlyphs` (via `std::ranges::find`) resolves to the first and the second set is unreachable. Duplicate seal names are caught only incidentally as a 'share a key' error.

**Impact:** A copy-paste mistake silently loses data, and the second definition never takes effect, with no warning.

**Recommendation:** Track the seen names and refuse or warn on a repeat, naming the line. Add tests.

**Verification:** I did not read abilities.cpp:501, ReadGlowTable or ReadGlyphSets. yaml-cpp does iterate duplicate map keys, so this is plausible. A test or a short repro would decide it. Low impact either way.

### DATA-COMBAT-06: Misspelt or unused new glow is silently accepted

- **Severity:** low · **Verdict:** unverifiable · **Category:** error-handling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/effects.cpp:298`

`ReadGlowTable` creates a new glow for any name not shipped. A typo such as `sword_aur` produces an extra glow that no role references, and the intended shipped glow keeps its old look. There is no warning for a glow no role uses.

**Impact:** Visual tuning edits silently do nothing. Debug hot reload feedback is 'nothing happened'.

**Recommendation:** Warn (through `warnings`) for a new glow that no glow_role references and which is not a shipped name, and cap the glow count. The list is also searched linearly.

**Verification:** I did not open ReadGlowTable at effects.cpp:298. A typo creating a new unused glow is plausible by design, since glows are open-ended tables. This is a UX nicety, not a defect.

### DATA-COMBAT-07: AfflictionAt and damage amounts allow up to 2^53 per hit with i64 accumulation

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** integer · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/data/abilities.cpp:13`

`MAX_WHOLE` of 2^53 is used for damage, qi_cost, qi_gain, qi max and affliction damage. Affliction ticks up to 1e6 multiply damage.

**Impact:** `damage` per tick times `ticks` up to 1e6 times many hits can overflow i64 in the combat code. Whether the combat code saturates is outside this scope. The reader bound is far larger than any game need and gives no headroom.

**Recommendation:** Cap amounts at something like 2^40 or the game's real maximum health. Check game/combat overflow handling.

**Verification:** MAX_WHOLE is 2^53 at line 13 and used for the whole-number reader. The overflow claim is speculative: the combat code was not shown to lack saturation, and the finding itself says that is out of scope. Single values near 2^53 are still allowed by the reader. Low-impact hardening only.

### DATA-COMBAT-08: Tests do not cover narrowing, duplicates, semantic combos or parse-then-Valid for abilities/effects

- **Severity:** low · **Verdict:** unverifiable · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/data/effects_tests.cpp:175`

The out-of-range tables in effects_tests.cpp and abilities_tests.cpp cover `0`, negatives, `.nan`, `.inf` and over-million-second times. They never feed a value above `FLT_MAX` or below `FLT_MIN` for an open-lower-bound key, a duplicate key, a duplicate glyph set, or `qi_cost` above `qi.max`. The seals tests have the narrowing cases (seals_tests.cpp:22) but not for closed ranges or satellite fan-out totals.

**Impact:** The defects above would pass the current suite.

**Recommendation:** Add the cases listed in findings 1 to 5 as table rows.

**Verification:** The finding refers to 'findings 1 to 5', which are not in the list given to me, so the specific missing cases cannot be judged. I did not read the test tables, so I can't confirm or refute the coverage gaps. Reading the tests, and the other findings, would decide it.

### DATA-COMBAT-09: Satellite nesting multiplies past the drawn-seal cap without any load-time check

- **Severity:** info (reviewer said low) · **Verdict:** unverifiable · **Category:** design · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/data/seals.cpp:734`

`CheckSatellites` limits depth (2) and per-seal fan-out (63), but a look can expand to 63 + 63*63 seals. The core comment says 'MAX_SEAL_SATELLITES ... one frame's most drawn seals'. The check in `gfx` truncates at `MAX_SEALS_DRAWN = 64` (counted in `droppedSeals`).

**Impact:** An authored seal can silently draw only a fraction of its satellites. The cost of walking the expansion is paid per frame and per seal instance before dropping, and the data author learns of it only from an overflow report.

**Recommendation:** Compute the total expanded seal count per look at load time (a simple DP over `depths`) and refuse over 64, or document that truncation is intended.

**Verification:** core::MAX_SEAL_SATELLITES = 63 is confirmed (core/seals.hpp:36). I did not verify the nesting arithmetic or the gfx truncation at 64. The backlog (sweep-up-backlog.md:34) already records the 4b caps on seal and satellite tables. If gfx truncates and reports droppedSeals, the behaviour is probably intended.

### DATA-COMBAT-10: Ticks part's length is not checked against its radius

- **Severity:** info · **Verdict:** unverifiable · **Category:** correctness · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/data/seals.cpp:330`

Ticks `length` is stored in `part.size` with the range (0,1], and `radius` is (0,1] with no relation between them. Similar: star/polygon parts' `radius` are independent of the layer's inner/outer band.

**Impact:** A tick longer than its radius reaches through the centre. The same holds for parts outside their layer band. This may be intentional but is unvalidated and untested.

**Recommendation:** Confirm intent; refuse or warn for tick length greater than radius, or document why not.

**Verification:** I did not read seals.cpp:330. The reviewer already flags this as a possible design intent and as speculative. No evidence of a defect.
