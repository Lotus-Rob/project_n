# Review: data-combat (part 2)

### DATA-COMBAT-11: Combat reader narrows u64 fallbacks to u32 with a cast

- **Severity:** info · **Verdict:** confirmed · **Category:** integer · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/data/abilities.cpp:500`

The `ticks` lambda in `Combat()` takes `u64 fallback` and `static_cast<u32>`s it. The CombatRules fields are u64 and the readers return u32 (cap about 6e7 ticks).

**Impact:** Harmless today (shipped values are small), but the narrowing is silent and the CombatRules fields are wider than the data can express.

**Recommendation:** Use u32 throughout or assert the fallback fits.

**Verification:** The lambda at line 499 takes u64 fallback and static_cast<u32>s it (cited line 525 is wrong; it is 499-500). CombatRules fields are u64 (combat_rules.hpp:47-58) and shipped defaults are 30 and 60, so the cast is harmless today. The narrowing is silent, as described.
